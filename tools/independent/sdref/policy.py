"""The engine's policy.bin file (a bucket policy of the preflop blueprint trainer): read the header, lay out
the dense (node, row, action) table of a compiled tree, and write a policy the exact evaluator loads.

Nothing here is game logic: the file format and the table layout are choices of the engine, read from
include/gtosd/preflop_blueprint/policy_file.hpp, libs/preflop_blueprint/src/policy_file.cpp, binary_io.hpp,
stream_io.hpp and layout_state() in compiled_game.cpp. They are listed as conventions:

- P1 file: "GTOSDPOL" (8 bytes), u32 version (1), string tree fingerprint, u32 flop capacity, u32 turn
  capacity, u32 river capacity, u64 entries, string source, then `entries` little-endian float64 values,
  then u64 FNV-1a-64 of every preceding byte (header and table). A string is a u32 byte length followed by
  the bytes. All integers little-endian.
- P2 loader checks (policy_file.cpp): magic, version, entries = remaining bytes / 8, every value finite and
  non-negative, the checksum, the tree fingerprint of the evaluator's compiled game, and entries = the
  layout of that game at the stored capacities (GameMismatch otherwise). The exact evaluator also requires
  the source to end with "|abstraction=<board class rows fingerprint>|flop=<fp>|turn=<fp>|river=<fp>" of the
  tables it loads (monker_values.cpp), so a written policy keeps the source suffix of a policy trained with
  the same tables.
- P3 layout: decision nodes in node-id (preorder) order; node n takes rows(street) x action_count(n)
  consecutive entries, row-major (row, action); rows = 81 preflop hand classes (class id order of
  sdref.cards, convention C5 there) on the preflop street and the capacity of the street's table after it.
  Chance and terminal nodes take no entries.
- P4 the policy fingerprint the evaluator prints is "fnv1a64:" + hex16 of FNV-1a over the table bytes only.
"""

from __future__ import annotations

import os
import struct
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from . import cards as sdcards

POLICY_MAGIC = b"GTOSDPOL"
POLICY_VERSION = 1
STREETS = ("preflop", "flop", "turn", "river")
FNV_OFFSET = 14695981039346656037
FNV_PRIME = 1099511628211
_MASK64 = (1 << 64) - 1


def fnv1a64(data, value: int = FNV_OFFSET) -> int:
    """FNV-1a 64 over bytes (pure Python, about 1.3 s per 8 MB measured on 30/09)."""
    prime = FNV_PRIME
    mask = _MASK64
    for byte in memoryview(data).cast("B"):
        value = ((value ^ byte) * prime) & mask
    return value


@dataclass(frozen=True)
class PolicyHeader:
    tree_fingerprint: str
    flop_capacity: int
    turn_capacity: int
    river_capacity: int
    entries: int
    source: str
    version: int = POLICY_VERSION

    @property
    def capacities(self) -> tuple[int, int, int]:
        return self.flop_capacity, self.turn_capacity, self.river_capacity


def _string(value: str) -> bytes:
    data = value.encode("utf-8")
    return struct.pack("<I", len(data)) + data


def serialize_header(header: PolicyHeader) -> bytes:
    """P1 header bytes (everything before the table)."""
    return (POLICY_MAGIC + struct.pack("<I", header.version) + _string(header.tree_fingerprint)
            + struct.pack("<III", header.flop_capacity, header.turn_capacity, header.river_capacity)
            + struct.pack("<Q", header.entries) + _string(header.source))


def read_header(path: Path) -> tuple[PolicyHeader, bytes, int]:
    """(header, header bytes, file size). Checks the magic, the version and that the table fills the file."""
    path = Path(path)
    size = path.stat().st_size
    with open(path, "rb") as handle:
        head = handle.read(min(size, 1 << 21))
    if head[:8] != POLICY_MAGIC:
        raise ValueError(f"{path}: not a policy file (magic {head[:8]!r})")
    position = 8

    def take(count: int) -> bytes:
        nonlocal position
        if position + count > len(head):
            raise ValueError(f"{path}: truncated header")
        chunk = head[position:position + count]
        position += count
        return chunk

    (version,) = struct.unpack("<I", take(4))
    if version != POLICY_VERSION:
        raise ValueError(f"{path}: policy version {version}, expected {POLICY_VERSION}")
    (length,) = struct.unpack("<I", take(4))
    tree = take(length).decode("utf-8")
    flop, turn, river = struct.unpack("<III", take(12))
    (entries,) = struct.unpack("<Q", take(8))
    (length,) = struct.unpack("<I", take(4))
    source = take(length).decode("utf-8")
    header = PolicyHeader(tree, flop, turn, river, entries, source, version)
    header_bytes = head[:position]
    if size != position + 8 * entries + 8:
        raise ValueError(f"{path}: {size} bytes, header {position} + {entries} doubles + 8 expected "
                         f"{position + 8 * entries + 8}")
    if serialize_header(header) != header_bytes:
        raise ValueError(f"{path}: header does not re-serialize to the same bytes")
    return header, header_bytes, size


def read_table_slice(path: Path, header_size: int, offset: int, count: int) -> np.ndarray:
    """`count` float64 entries of the table starting at entry `offset` (no checksum verification)."""
    with open(path, "rb") as handle:
        handle.seek(header_size + 8 * offset)
        data = handle.read(8 * count)
    if len(data) != 8 * count:
        raise ValueError(f"{path}: table slice [{offset}, {offset + count}) past the end")
    return np.frombuffer(data, dtype="<f8").copy()


def verify_checksum(path: Path) -> bool:
    """Recompute the P1 trailer (about 11 s per 68 MB)."""
    data = Path(path).read_bytes()
    (stored,) = struct.unpack("<Q", data[-8:])
    return fnv1a64(memoryview(data)[:-8]) == stored


@dataclass(frozen=True)
class NodeSlot:
    node: int
    street: int
    rows: int
    actions: int
    offset: int


def rows_for(street: int, capacities: tuple[int, int, int]) -> int:
    return sdcards.CLASS_COUNT if street == 0 else capacities[street - 1]


def layout(records: list[dict], capacities: tuple[int, int, int]) -> tuple[list[NodeSlot], int]:
    """P3 layout of a tree given as node records (dump format: id, kind, street, edges), in id order."""
    slots = []
    entries = 0
    for record in sorted(records, key=lambda item: item["id"]):
        if record["kind"] != "decision":
            continue
        street = STREETS.index(record["street"])
        rows = rows_for(street, capacities)
        actions = len(record["edges"])
        slots.append(NodeSlot(record["id"], street, rows, actions, entries))
        entries += rows * actions
    return slots, entries


def build_table(slots: list[NodeSlot], entries: int, row_of) -> np.ndarray:
    """Dense table: row_of(slot) returns (81, actions) for a preflop node (one row per class) or (actions,)
    for a later street (every row of the node gets it: a hand-independent row)."""
    table = np.empty(entries, dtype="<f8")
    for slot in slots:
        rows = np.asarray(row_of(slot), dtype=np.float64)
        part = table[slot.offset:slot.offset + slot.rows * slot.actions].reshape(slot.rows, slot.actions)
        if slot.street == 0:
            if rows.shape != (slot.rows, slot.actions):
                raise ValueError(f"node {slot.node}: preflop rows {rows.shape}, expected "
                                 f"{(slot.rows, slot.actions)}")
            part[:, :] = rows
        else:
            if rows.shape != (slot.actions,):
                raise ValueError(f"node {slot.node}: row {rows.shape}, expected {(slot.actions,)}")
            part[:, :] = rows[None, :]
    if not np.all(np.isfinite(table)) or np.any(table < 0.0):
        raise ValueError("policy table has a negative or non-finite entry (the loader rejects it)")
    return table


def write_policy(path: Path, header: PolicyHeader, table: np.ndarray) -> str:
    """Write header + table + FNV-1a trailer atomically (temporary file then rename). Returns the file
    checksum as "fnv1a64:<hex16>"."""
    if table.dtype != np.dtype("<f8") or table.ndim != 1 or table.size != header.entries:
        raise ValueError(f"table of {table.size} {table.dtype} entries, header says {header.entries} <f8")
    head = serialize_header(header)
    body = table.tobytes()
    checksum = fnv1a64(body, fnv1a64(head))
    path = Path(path)
    temporary = path.with_name(path.name + ".tmp")
    with open(temporary, "wb") as handle:
        handle.write(head)
        handle.write(body)
        handle.write(struct.pack("<Q", checksum))
    os.replace(temporary, path)
    return f"fnv1a64:{checksum:016x}"
