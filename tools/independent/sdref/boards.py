"""Boards up to suit symmetry: orbits, stabilizers, multiplicities, and readers of the engine's board catalog
and bucket tables (S6 of the correctness coverage).

Rules (game facts, the base of every check here):
- B1 A board is dealt from the 36-card deck without replacement: the flop is an unordered set of three cards,
  then the turn card, then the river card. Physical objects:
    flop            unordered 3-set                         C(36, 3)           =     7,140
    flop + turn     (3-set, card outside it)                7,140 * 33         =   235,620
    river board     unordered 5-set (what a river row sees) C(36, 5)           =   376,992
    history         (3-set, turn, river), all distinct      7,140 * 33 * 32    = 7,539,840
- B2 Suits carry no value: a permutation s of the four suits, applied to every card (rank kept, suit
  replaced), maps the game onto itself. The 24 permutations form the symmetric group S4.
- B3 Two objects are isomorphic when some s maps one onto the other (sets map as sets, the turn and river
  cards map card by card). The isomorphism classes are the orbits; by the orbit-stabilizer theorem an
  orbit has 24 / |Stab(x)| members, where Stab(x) is the set of permutations that map x onto itself.
- B4 Burnside: the number of orbits equals the average over the 24 permutations of the number of objects
  each one fixes.
- B5 A hole-card combo is live on a board when it shares no card with it. Two live combos h1, h2 on a board
  x are strategically identical (every future card, every opponent hand, every payoff maps one-to-one) iff
  some s in Stab(x) maps h1 onto h2: the classes of identical hands on x are the orbits of the live combos
  under Stab(x). A lossless per-board hand abstraction must give exactly one id per such orbit.

Conventions (engine choices, read from include/gtosd/card_abstraction/canonical_boards.hpp,
libs/card_abstraction/src/canonical_boards.cpp and bucket_tables.cpp because the files are laid out by
them; they are not rules):
- K1 card index = rank * 4 + suit (cards.py C1); a permutation is a tuple perm with new_suit = perm[old_suit],
  the 24 of them in lexicographic order (itertools.permutations(range(4)), = std::next_permutation order).
- K2 packed code, 6 bits per card, most significant first: flop = its three cards sorted ascending; flop +
  turn = flop code << 6 | turn; history = flop + turn code << 6 | river; river board = its five cards sorted
  ascending.
- K3 the canonical representative of an orbit is the member with the smallest packed code; each catalog
  lists its representatives by increasing code, and the catalog index of an object is the position of its
  representative. A lookup also returns the first permutation (K1 order) that maps the object onto the
  representative; the table rows are stored in the frame of the representative, so a physical combo h is
  read at combo_index(s(h)). Since any permutation reaching the representative differs from that one by an
  element of Stab(representative), the checks here try all of them (the result must not depend on it).
- K4 board_catalog_v1.bin: "GTOSDCAT", u32 version 1, u32 counts of flops, flop + turns, river boards and
  histories, then the entries as little-endian u32: flop (code, multiplicity), flop + turn (code,
  multiplicity, flop_index), river board (code, multiplicity), history (code, multiplicity, flop_index,
  flop_turn_index, river_board_index), then a u64 FNV-1a-64 of all preceding bytes.
- K5 catalog fingerprint = "fnv1a64:" + hex16 of FNV-1a over the text "gtosd.card_abstraction.board_catalog.v1"
  followed, for each catalog in the order above, by its u32 entry count and its entries' u32 fields (the same
  bytes as the file blocks). It is not stored in the catalog file; bucket tables store it.
- K6 bucket table (resource container F1 of resources.py, kind "bucket_table", version 1): payload = u8 street
  (0 flop, 1 turn, 2 river), u16 capacity, u32 rows, u32 width, u32 restarts, u32 screening_iterations, u32
  maximum_iterations, u32 screening_sample, u64 partition_seed, u32 length + catalog fingerprint text, u32
  length + feature fingerprint text, u64 count + count u16 centroids, u64 count + rows * 630 u16 ids (row =
  catalog index of the flop, flop + turn or river board; column = combo index in the representative's frame,
  cards.py C3; 0xFFFF on the combos that overlap the board). Fingerprint = "fnv1a64:" + hex16 of FNV-1a over
  "gtosd.card_abstraction.bucket_table.v1|" + street name + "|" + "capacity|restarts|screening_iterations|
  maximum_iterations|screening_sample|" (decimal) + hex16(partition_seed) + "|" + catalog fingerprint + "|" +
  feature fingerprint, continued over the payload bytes of the two vectors (counts included).
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from math import comb
from pathlib import Path

import numpy as np

from . import cards as sdcards
from . import resources

KINDS = ("flop", "flop_turn", "river", "history")
CARD_BITS = 6
PHYSICAL_COUNTS = {
    "flop": comb(36, 3),
    "flop_turn": comb(36, 3) * 33,
    "river": comb(36, 5),
    "history": comb(36, 3) * 33 * 32,
}
# Columns of an object array per kind: flop (3 sorted), flop_turn (3 sorted + turn), river (5 sorted),
# history (3 sorted + turn + river).
KIND_WIDTH = {"flop": 3, "flop_turn": 4, "river": 5, "history": 5}
# Cards that form an unordered set at the start of each object; the rest are single cards.
KIND_SET_SIZE = {"flop": 3, "flop_turn": 3, "river": 5, "history": 3}

CATALOG_PATH = resources.RESOURCES_DIR / "board_catalog_v1.bin"
CATALOG_MAGIC = b"GTOSDCAT"
CATALOG_DOMAIN = "gtosd.card_abstraction.board_catalog.v1"
BUCKET_KIND = "bucket_table"
BUCKET_DOMAIN = "gtosd.card_abstraction.bucket_table.v1|"
STREET_NAMES = ("flop", "turn", "river")
STREET_KIND = {"flop": "flop", "turn": "flop_turn", "river": "river"}
NO_BUCKET = 0xFFFF

PERM_TABLE = sdcards.card_permutation_table().astype(np.int64)  # (24, 36)
PERM_COUNT = PERM_TABLE.shape[0]


def combo_permutation_table() -> np.ndarray:
    """(24, 630) int64: entry [p, c] is the combo index of the image of combo c under permutation p."""
    first = PERM_TABLE[:, sdcards.COMBOS[:, 0].astype(np.int64)]
    second = PERM_TABLE[:, sdcards.COMBOS[:, 1].astype(np.int64)]
    return sdcards.COMBO_INDEX[first, second].astype(np.int64)


COMBO_PERM = combo_permutation_table()

# ---------------------------------------------------------------- objects and packed codes (K2)


def _sort_columns(block: np.ndarray) -> np.ndarray:
    """Rows sorted ascending (a 3-comparator network for width 3, np.sort otherwise)."""
    if block.shape[1] == 3:
        a, b, c = block[:, 0], block[:, 1], block[:, 2]
        low = np.minimum(np.minimum(a, b), c)
        high = np.maximum(np.maximum(a, b), c)
        middle = a + b + c - low - high
        return np.stack([low, middle, high], axis=1)
    return np.sort(block, axis=1)


def pack(objects: np.ndarray, kind: str) -> np.ndarray:
    """Packed codes (int64) of objects whose set part is already sorted ascending."""
    objects = np.asarray(objects, dtype=np.int64)
    code = np.zeros(objects.shape[0], dtype=np.int64)
    for column in range(KIND_WIDTH[kind]):
        code = (code << CARD_BITS) | objects[:, column]
    return code


def unpack(codes: np.ndarray, kind: str) -> np.ndarray:
    """Inverse of pack: (n, width) int64 card arrays."""
    codes = np.asarray(codes, dtype=np.int64)
    width = KIND_WIDTH[kind]
    out = np.empty((codes.shape[0], width), dtype=np.int64)
    for column in range(width):
        out[:, column] = (codes >> (CARD_BITS * (width - 1 - column))) & ((1 << CARD_BITS) - 1)
    return out


def image(objects: np.ndarray, kind: str, perm_index: int) -> np.ndarray:
    """Objects mapped by one suit permutation, set part re-sorted (rule B2)."""
    mapped = PERM_TABLE[perm_index][np.asarray(objects, dtype=np.int64)]
    set_size = KIND_SET_SIZE[kind]
    mapped[:, :set_size] = _sort_columns(mapped[:, :set_size])
    return mapped


def image_codes(objects: np.ndarray, kind: str) -> np.ndarray:
    """(n, 24) int64: packed code of every image."""
    objects = np.asarray(objects, dtype=np.int64)
    out = np.empty((objects.shape[0], PERM_COUNT), dtype=np.int64)
    for p in range(PERM_COUNT):
        out[:, p] = pack(image(objects, kind, p), kind)
    return out


def canonical_codes(objects: np.ndarray, kind: str, chunk: int = 500_000) -> np.ndarray:
    """Smallest packed code over the 24 images (the orbit label of K3), int64, chunked for memory."""
    objects = np.asarray(objects)
    out = np.empty(objects.shape[0], dtype=np.int64)
    for start in range(0, objects.shape[0], chunk):
        block = objects[start:start + chunk].astype(np.int64)
        best = None
        for p in range(PERM_COUNT):
            codes = pack(image(block, kind, p), kind)
            best = codes if best is None else np.minimum(best, codes)
        out[start:start + chunk] = best
    return out


def stabilizer_mask(objects: np.ndarray, kind: str) -> np.ndarray:
    """(n, 24) bool: permutation p maps the object onto itself (set part as a set, the rest card by card)."""
    objects = np.asarray(objects, dtype=np.int64)
    base = pack(image(objects, kind, 0), kind)  # permutation 0 is the identity (K1 order)
    return image_codes(objects, kind) == base[:, None]


def orbit_sizes(objects: np.ndarray, kind: str) -> np.ndarray:
    """Number of distinct images (int64): the orbit size, counted directly (not via 24 / |Stab|)."""
    codes = np.sort(image_codes(objects, kind), axis=1)
    return 1 + np.count_nonzero(codes[:, 1:] != codes[:, :-1], axis=1)


def physical_objects(kind: str) -> np.ndarray:
    """Every physical object of rule B1 (uint8), set part sorted ascending."""
    flops = sdcards.colex_subsets(3).astype(np.uint8)
    if kind == "flop":
        return flops
    if kind == "river":
        return sdcards.colex_subsets(5).astype(np.uint8)
    deck = np.arange(sdcards.DECK_SIZE, dtype=np.uint8)
    flop_live = np.ones((flops.shape[0], sdcards.DECK_SIZE), dtype=bool)
    flop_live[np.arange(flops.shape[0])[:, None], flops] = False
    rows, turns = np.nonzero(flop_live)  # row-major: every flop, then its 33 turns ascending
    flop_turns = np.concatenate([flops[rows], deck[turns][:, None]], axis=1)
    if kind == "flop_turn":
        return flop_turns
    if kind != "history":
        raise ValueError(f"unknown kind {kind}")
    live = np.ones((flop_turns.shape[0], sdcards.DECK_SIZE), dtype=bool)
    live[np.arange(flop_turns.shape[0])[:, None], flop_turns] = False
    rows, rivers = np.nonzero(live)
    return np.concatenate([flop_turns[rows], deck[rivers][:, None]], axis=1)


def burnside_orbit_count(kind: str) -> tuple[int, list[int]]:
    """Orbit count by Burnside (rule B4): the mean over the 24 permutations of the fixed objects, where the
    fixed objects are counted from the fixed sets and the fixed single cards of each permutation, never by
    listing orbits. Returns (count, fixed-object counts per permutation)."""
    set_size = KIND_SET_SIZE[kind]
    sets = sdcards.colex_subsets(set_size).astype(np.int64)
    fixed_counts = []
    for p in range(PERM_COUNT):
        mapped = np.sort(PERM_TABLE[p][sets], axis=1)
        invariant = np.all(mapped == sets, axis=1)
        if kind in ("flop", "river"):
            fixed_counts.append(int(invariant.sum()))
            continue
        fixed_cards = np.flatnonzero(PERM_TABLE[p] == np.arange(sdcards.DECK_SIZE))
        inside = np.isin(sets[invariant], fixed_cards).sum(axis=1)
        outside = len(fixed_cards) - inside  # fixed cards available for the turn
        if kind == "flop_turn":
            fixed_counts.append(int(outside.sum()))
        else:  # history: a fixed turn and a different fixed river, both outside the flop
            fixed_counts.append(int((outside * (outside - 1)).sum()))
    total = sum(fixed_counts)
    if total % PERM_COUNT != 0:
        raise ArithmeticError(f"Burnside sum {total} not divisible by 24 for {kind}")
    return total // PERM_COUNT, fixed_counts


# ---------------------------------------------------------------- live combos and their orbits (rule B5)


def live_combo_mask(boards: np.ndarray) -> np.ndarray:
    """(n, 630) bool: combo shares no card with the board (any number of board cards)."""
    boards = np.asarray(boards, dtype=np.int64)
    dead_card = np.zeros((boards.shape[0], sdcards.DECK_SIZE), dtype=bool)
    dead_card[np.arange(boards.shape[0])[:, None], boards] = True
    first = sdcards.COMBOS[:, 0].astype(np.int64)
    second = sdcards.COMBOS[:, 1].astype(np.int64)
    return ~(dead_card[:, first] | dead_card[:, second])


def combo_orbit_representatives(stabilizer: np.ndarray) -> np.ndarray:
    """(n, 630) int64: smallest combo index in the orbit of each combo under the board's stabilizer (a group
    containing the identity, so the orbit is the set of images)."""
    n = stabilizer.shape[0]
    representative = np.tile(np.arange(sdcards.COMBO_COUNT, dtype=np.int64), (n, 1))
    for p in range(PERM_COUNT):
        rows = np.flatnonzero(stabilizer[:, p])
        if rows.size:
            representative[rows] = np.minimum(representative[rows], COMBO_PERM[p][None, :])
    return representative


# ---------------------------------------------------------------- board catalog (K4, K5)


@dataclass
class BoardCatalogFile:
    path: Path
    version: int
    flops: np.ndarray  # (n, 2) uint32: code, multiplicity
    flop_turns: np.ndarray  # (n, 3): code, multiplicity, flop_index
    river_boards: np.ndarray  # (n, 2): code, multiplicity
    histories: np.ndarray  # (n, 5): code, multiplicity, flop_index, flop_turn_index, river_board_index
    checksum_verified: bool | None
    fingerprint: str

    def entries(self, kind: str) -> np.ndarray:
        return {"flop": self.flops, "flop_turn": self.flop_turns, "river": self.river_boards,
                "history": self.histories}[kind]


def read_board_catalog(path: Path = CATALOG_PATH, verify: bool = True) -> BoardCatalogFile:
    """Parse board_catalog_v1.bin (K4); raises ValueError on a format or integrity problem. The fingerprint
    (K5) is always recomputed, since the file does not store it."""
    raw = Path(path).read_bytes()
    header = len(CATALOG_MAGIC) + 5 * 4
    if len(raw) < header + 8 or raw[:len(CATALOG_MAGIC)] != CATALOG_MAGIC:
        raise ValueError(f"{path}: not a GTOSDCAT catalog")
    version, *counts = struct.unpack_from("<5I", raw, len(CATALOG_MAGIC))
    widths = (2, 3, 2, 5)
    expected = header + sum(4 * w * c for w, c in zip(widths, counts)) + 8
    if len(raw) != expected:
        raise ValueError(f"{path}: size {len(raw)}, expected {expected} from the header counts")
    verified = None
    if verify:
        (stored,) = struct.unpack_from("<Q", raw, len(raw) - 8)
        if resources.fnv1a64(memoryview(raw)[:len(raw) - 8]) != stored:
            raise ValueError(f"{path}: FNV-1a trailer mismatch")
        verified = True
    blocks = []
    position = header
    fingerprint = resources.fnv1a64_text(CATALOG_DOMAIN)
    for width, count in zip(widths, counts):
        size = 4 * width * count
        fingerprint = resources.fnv1a64(struct.pack("<I", count), fingerprint)
        fingerprint = resources.fnv1a64(memoryview(raw)[position:position + size], fingerprint)
        blocks.append(np.frombuffer(raw, dtype="<u4", count=width * count, offset=position)
                      .astype(np.uint32).reshape(count, width))
        position += size
    return BoardCatalogFile(Path(path), version, *blocks, verified, "fnv1a64:" + resources.hex64(fingerprint))


# ---------------------------------------------------------------- bucket tables (K6)


@dataclass
class BucketTableFile:
    path: Path
    street: str
    capacity: int
    rows: int
    width: int
    parameters: dict
    catalog_fingerprint: str
    feature_fingerprint: str
    fingerprint: str
    centroids: np.ndarray  # (capacity * width,) uint16
    ids: np.ndarray  # (rows, 630) uint16
    checksum_verified: bool | None
    fingerprint_verified: bool | None


def read_bucket_table(path: Path, verify: bool = True) -> BucketTableFile:
    resource = resources.read_resource(path, BUCKET_KIND, 1, verify)
    payload = resource.payload
    position = 0
    street_code = payload[0]
    position += 1
    if street_code >= len(STREET_NAMES):
        raise ValueError(f"{path}: street {street_code}")
    capacity, rows, width, restarts, screening_iterations, maximum_iterations, screening_sample, seed = \
        struct.unpack_from("<HIIIIIIQ", payload, position)
    position += struct.calcsize("<HIIIIIIQ")
    texts = []
    for _ in range(2):
        (size,) = struct.unpack_from("<I", payload, position)
        position += 4
        texts.append(bytes(payload[position:position + size]).decode("latin-1"))
        position += size
    vectors_start = position
    arrays = []
    for expected in (capacity * width, rows * sdcards.COMBO_COUNT):
        (count,) = struct.unpack_from("<Q", payload, position)
        position += 8
        if count != expected:
            raise ValueError(f"{path}: vector of {count} values, expected {expected}")
        arrays.append(np.frombuffer(payload, dtype="<u2", count=count, offset=position).astype(np.uint16))
        position += 2 * count
    if position != len(payload):
        raise ValueError(f"{path}: {len(payload) - position} trailing payload bytes")
    street = STREET_NAMES[street_code]
    parameters = {"capacity": capacity, "restarts": restarts, "screening_iterations": screening_iterations,
                  "maximum_iterations": maximum_iterations, "screening_sample": screening_sample,
                  "partition_seed": seed}
    fingerprint_ok = None
    if verify:
        value = resources.fnv1a64_text(BUCKET_DOMAIN)
        value = resources.fnv1a64_text(street, value)
        value = resources.fnv1a64_text("|", value)
        value = resources.fnv1a64_text(
            f"{capacity}|{restarts}|{screening_iterations}|{maximum_iterations}|{screening_sample}|"
            f"{resources.hex64(seed)}|", value)
        value = resources.fnv1a64_text(texts[0], value)
        value = resources.fnv1a64_text("|", value)
        value = resources.fnv1a64_text(texts[1], value)
        value = resources.fnv1a64(payload[vectors_start:], value)
        fingerprint_ok = "fnv1a64:" + resources.hex64(value) == resource.fingerprint
    return BucketTableFile(Path(path), street, capacity, rows, width, parameters, texts[0], texts[1],
                           resource.fingerprint, arrays[0], arrays[1].reshape(rows, sdcards.COMBO_COUNT),
                           resource.checksum_verified, fingerprint_ok)


def self_check() -> None:
    """Cheap invariants (raises AssertionError)."""
    assert PERM_TABLE.shape == (24, 36) and len(set(map(tuple, PERM_TABLE.tolist()))) == 24
    assert np.array_equal(COMBO_PERM[0], np.arange(sdcards.COMBO_COUNT))
    assert all(sorted(row) == list(range(sdcards.COMBO_COUNT)) for row in COMBO_PERM.tolist())
    for kind in KINDS:
        sample = physical_objects(kind)[:: max(1, PHYSICAL_COUNTS[kind] // 997)][:1000]
        assert np.array_equal(unpack(pack(sample, kind), kind), sample.astype(np.int64))
        stab = stabilizer_mask(sample, kind)
        assert stab[:, 0].all()
        assert np.array_equal(orbit_sizes(sample, kind) * stab.sum(axis=1), np.full(sample.shape[0], 24))
