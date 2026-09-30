"""Readers of the engine resource files rank_table_v1.bin and preflop_all_in_v1.bin, and of the Python
all-in counts written by sd_allin_check.py.

File formats (conventions of the engine, read from libs/card_abstraction/src/resource_file.cpp,
rank_table.cpp and all_in_table.cpp; they are choices, not rules):
- F1 container: "GTOSDRES" (8 bytes), u32 version, u32 kind length + kind bytes, u32 fingerprint length +
  fingerprint bytes, u64 payload size, payload, u64 FNV-1a-64 of everything before the trailer. All
  integers little-endian.
- F2 rank table (kind "rank_table_ordinal", version 1): payload = u16 distinct, u64 376,992 + that many u16
  five-card ordinals, u64 8,347,680 + that many u16 seven-card ordinals, both indexed by the colex rank of
  the sorted cards (cards.py C2). Fingerprint = "fnv1a64:" + hex16 of FNV-1a over the text
  "gtosd.card_abstraction.rank_table_ordinal.v1|" + ruleset + "|" + decimal distinct + "|" + hex16(FNV-1a
  of the payload), ruleset = "short_deck_36_flush_over_full_house_a6789_exact_v1".
- F3 all-in table (kind "preflop_all_in_pairs", version 1): payload = u64 198,135 + that many (u32 wins,
  u32 ties, u32 losses) in triangular combo-pair order (cards.py C4); the stored outcome is the one of the
  LOWER combo id (hero = lower id; AllInTable::outcome swaps wins and losses for the other side).
  Fingerprint = "fnv1a64:" + hex16 of FNV-1a over "gtosd.card_abstraction.preflop_all_in_pairs.v1|" + the
  rank-table fingerprint text + the entry bytes (the payload without its u64 count).
"""

from __future__ import annotations

import json
import struct
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

from . import cards as sdcards

REPO_ROOT = Path(__file__).resolve().parents[3]
RESOURCES_DIR = REPO_ROOT / "out" / "preflop_blueprint_resources"
RANK_TABLE_PATH = RESOURCES_DIR / "rank_table_v1.bin"
ALL_IN_PATH = RESOURCES_DIR / "preflop_all_in_v1.bin"
INDEPENDENT_OUT_DIR = REPO_ROOT / "out" / "monker" / "correctness" / "independent"

RESOURCE_MAGIC = b"GTOSDRES"
RANK_TABLE_KIND = "rank_table_ordinal"
ALL_IN_KIND = "preflop_all_in_pairs"
RANKSET_FINGERPRINT = "short_deck_36_flush_over_full_house_a6789_exact_v1"

FNV_OFFSET = 14695981039346656037
FNV_PRIME = 1099511628211
_MASK64 = (1 << 64) - 1


def fnv1a64(data, value: int = FNV_OFFSET) -> int:
    """FNV-1a 64 over bytes (pure Python, about 1-2 s per 10 MB)."""
    prime = FNV_PRIME
    mask = _MASK64
    for byte in bytes(data):
        value = ((value ^ byte) * prime) & mask
    return value


def fnv1a64_text(text: str, value: int = FNV_OFFSET) -> int:
    return fnv1a64(text.encode("latin-1"), value)


def hex64(value: int) -> str:
    return f"{value:016x}"


@dataclass
class ResourceFile:
    path: Path
    kind: str
    version: int
    fingerprint: str
    payload: memoryview
    checksum_verified: bool | None  # None when not verified


def read_resource(path: Path, expected_kind: str, expected_version: int = 1,
                  verify_checksum: bool = True) -> ResourceFile:
    """Parse the F1 container; raises ValueError on any format or integrity problem."""
    raw = Path(path).read_bytes()
    if len(raw) < 8 + 3 * 4 + 2 * 8 or raw[:8] != RESOURCE_MAGIC:
        raise ValueError(f"{path}: not a GTOSDRES resource")
    payload_end = len(raw) - 8
    position = 8
    version, kind_size = struct.unpack_from("<II", raw, position)
    position += 8
    kind = raw[position:position + kind_size].decode("latin-1")
    position += kind_size
    (fingerprint_size,) = struct.unpack_from("<I", raw, position)
    position += 4
    fingerprint = raw[position:position + fingerprint_size].decode("latin-1")
    position += fingerprint_size
    (payload_size,) = struct.unpack_from("<Q", raw, position)
    position += 8
    if position + payload_size != payload_end:
        raise ValueError(f"{path}: payload size {payload_size} does not match the file size")
    if kind != expected_kind:
        raise ValueError(f"{path}: kind {kind!r}, expected {expected_kind!r}")
    if version != expected_version:
        raise ValueError(f"{path}: version {version}, expected {expected_version}")
    verified = None
    if verify_checksum:
        (stored,) = struct.unpack_from("<Q", raw, payload_end)
        if fnv1a64(memoryview(raw)[:payload_end]) != stored:
            raise ValueError(f"{path}: FNV-1a trailer mismatch")
        verified = True
    return ResourceFile(Path(path), kind, version, fingerprint, memoryview(raw)[position:payload_end],
                        verified)


@dataclass
class RankTableFile:
    path: Path
    fingerprint: str
    distinct: int
    five: np.ndarray  # uint16, 376,992, colex order
    seven: np.ndarray  # uint16, 8,347,680, colex order
    checksum_verified: bool | None
    fingerprint_verified: bool | None
    notes: list[str] = field(default_factory=list)


def rank_table_fingerprint(payload, distinct: int) -> str:
    value = fnv1a64_text("gtosd.card_abstraction.rank_table_ordinal.v1|")
    value = fnv1a64_text(RANKSET_FINGERPRINT, value)
    value = fnv1a64_text("|", value)
    value = fnv1a64_text(str(distinct), value)
    value = fnv1a64_text("|", value)
    value = fnv1a64_text(hex64(fnv1a64(payload)), value)
    return "fnv1a64:" + hex64(value)


def read_rank_table(path: Path = RANK_TABLE_PATH, verify: bool = True) -> RankTableFile:
    resource = read_resource(path, RANK_TABLE_KIND, 1, verify)
    payload = resource.payload
    position = 0
    (distinct,) = struct.unpack_from("<H", payload, position)
    position += 2
    tables = []
    for expected in (sdcards.FIVE_CARD_SET_COUNT, sdcards.SEVEN_CARD_SET_COUNT):
        (count,) = struct.unpack_from("<Q", payload, position)
        position += 8
        if count != expected:
            raise ValueError(f"{path}: table size {count}, expected {expected}")
        tables.append(np.frombuffer(payload, dtype="<u2", count=count, offset=position).astype(np.uint16))
        position += 2 * count
    if position != len(payload):
        raise ValueError(f"{path}: {len(payload) - position} trailing payload bytes")
    fingerprint_ok = None
    if verify:
        fingerprint_ok = rank_table_fingerprint(payload, distinct) == resource.fingerprint
        if not fingerprint_ok:
            raise ValueError(f"{path}: fingerprint {resource.fingerprint} does not match its payload")
    return RankTableFile(Path(path), resource.fingerprint, distinct, tables[0], tables[1],
                         resource.checksum_verified, fingerprint_ok)


@dataclass
class AllInFile:
    path: Path
    fingerprint: str
    entries: np.ndarray  # (198,135, 3) uint32: wins, ties, losses of the lower combo id
    checksum_verified: bool | None
    fingerprint_verified: bool | None  # None when no rank fingerprint was given


def all_in_fingerprint(rank_fingerprint: str, entry_bytes) -> str:
    value = fnv1a64_text("gtosd.card_abstraction.preflop_all_in_pairs.v1|")
    value = fnv1a64_text(rank_fingerprint, value)
    value = fnv1a64(entry_bytes, value)
    return "fnv1a64:" + hex64(value)


def read_all_in(path: Path = ALL_IN_PATH, verify: bool = True,
                rank_fingerprint: str | None = None) -> AllInFile:
    resource = read_resource(path, ALL_IN_KIND, 1, verify)
    payload = resource.payload
    (count,) = struct.unpack_from("<Q", payload, 0)
    if count != sdcards.COMBO_PAIR_COUNT:
        raise ValueError(f"{path}: {count} entries, expected {sdcards.COMBO_PAIR_COUNT}")
    if len(payload) != 8 + 12 * count:
        raise ValueError(f"{path}: payload size {len(payload)} does not match {count} entries")
    entries = np.frombuffer(payload, dtype="<u4", count=3 * count, offset=8).astype(np.uint32)
    fingerprint_ok = None
    if verify and rank_fingerprint is not None:
        fingerprint_ok = all_in_fingerprint(rank_fingerprint, payload[8:]) == resource.fingerprint
    return AllInFile(Path(path), resource.fingerprint, entries.reshape(count, 3),
                     resource.checksum_verified, fingerprint_ok)


def all_in_matrices(entries: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """(wins, ties) 630 x 630 int64 from triangular entries: wins[i, j] = boards where i beats j."""
    first, second = sdcards.combo_pair_arrays()
    n = sdcards.COMBO_COUNT
    wins = np.zeros((n, n), dtype=np.int64)
    ties = np.zeros((n, n), dtype=np.int64)
    wins[first, second] = entries[:, 0]
    wins[second, first] = entries[:, 2]
    ties[first, second] = entries[:, 1]
    ties[second, first] = entries[:, 1]
    return wins, ties


# ---------------------------------------------------------------- Python all-in counts (S2 output)

PYTHON_ALLIN_SCHEMA = "gtosd.independent.allin_counts.v1"


def write_python_allin(path: Path, wins: np.ndarray, ties: np.ndarray, metadata: dict) -> None:
    """npz with wins / ties (630 x 630 int32, meaningful for disjoint combos only; engine combo order) and
    a JSON metadata string."""
    meta = dict(metadata)
    meta["schema"] = PYTHON_ALLIN_SCHEMA
    np.savez_compressed(path, wins=wins.astype(np.int32), ties=ties.astype(np.int32),
                        metadata=np.array(json.dumps(meta, sort_keys=True)))


def read_python_allin(path: Path) -> tuple[np.ndarray, np.ndarray, dict]:
    with np.load(path) as data:
        meta = json.loads(str(data["metadata"]))
        if meta.get("schema") != PYTHON_ALLIN_SCHEMA:
            raise ValueError(f"{path}: schema {meta.get('schema')!r}")
        return data["wins"].astype(np.int64), data["ties"].astype(np.int64), meta
