"""S6 of the correctness coverage (shared_components.md section 3): the board catalog and the exact bucket
tables against an independent orbit computation (sdref/boards.py, rules B1-B5).

The trainer and the exact evaluator both read board_catalog_v1.bin (row indices, the multiplicities that
weight chance) and the bucket tables (the hand ids of every row). The lossless runs V0/V1/V2 (B1L, B2L,
...) rest on two premises this script re-derives in Python from the rules, without the engine code:
(i) the catalog lists every suit-isomorphism class of flops, flop + turns, river boards and histories once,
weighted by its orbit size; (ii) an exact table (--flop-exact, --turn-exact, --river-exact of
gtosd_preflop_blueprint_monker_buckets) gives the live combos of every board exactly one id per orbit under
the board's stabilizer.

Python-only checks (the reference checks itself):
- P0 sdref.cards and sdref.boards invariants.
- P1 per kind, the Burnside count (fixed objects per permutation, rule B4) equals the number of distinct
  canonical labels over all physical objects, and the label counts sum to the physical count (B1).
Catalog checks (board_catalog_v1.bin, format K4):
- C0 the file parses: magic, version 1, counts, FNV-1a trailer; the fingerprint (K5) is recomputed.
- C1 transversal, convention-free: every entry is a valid object; entries are pairwise non-isomorphic;
  multiplicity == orbit size (distinct images counted directly) == 24 / |Stab|; multiplicities sum to the
  physical count; the entry count equals the Burnside count. Together: one entry per orbit, weighted right.
- C2 convention K3: each code is the smallest packed code of its orbit, the codes increase, and the
  catalog equals the (label, count) list of the physical enumeration exactly.
- C3 cross references: the flop of a flop + turn entry, and the flop, flop + turn and five-card set of a
  history entry, are isomorphic to the entries they point to.
- C4 chance weights: per flop, its flop + turns weigh 33 x the flop and its histories 33 x 32 x the flop;
  per flop + turn, its histories weigh 32 x; per river board, the histories reaching it weigh 20 x (the
  C(5, 3) * 2 orderings of a five-card set into flop, turn and river).
Table checks (every *_buckets_v1.bin under --buckets-root, format K6):
- T0 the file parses (trailer, fingerprint recomputed), the street matches the file name, rows equal the
  catalog count, and the stored catalog fingerprint equals the recomputed one.
- T1 exactly the combos overlapping the row's board hold 0xFFFF; every live combo holds an id < capacity.
- T2 every table: ids are constant on the stabilizer orbits of the live combos (required of any table, or
  the id of a hand would depend on the canonicalizing permutation).
- T3 streets flagged exact in the table's monker_buckets_report.json (or named with --require-exact): the id
  partition of every row equals its orbit partition (no two orbits share an id), the capacity equals the
  largest orbit count. Streets not flagged report "observed_exact" for information.
- T4 physical boards: --physical-samples uniform random physical flops / flop + turns / river boards per
  street, found in the catalog by their own orbit label; for EVERY permutation mapping the board onto the
  row's representative, the ids of all 630 physical combos agree, dead combos are exactly the overlapping
  ones, and on exact streets two live combos share an id iff a permutation fixing the physical board maps
  one onto the other (rule B5, the lossless premise stated on physical cards).

Pass = every check true. Exit codes: 0 pass, 1 a mismatch, 2 usage, input or format error.
Outputs in --out-dir (default out/monker/correctness/independent/S6_catalog): sd_catalog_check.json.
Expected cost (one core; parts measured 30/09 on smoke runs, the total is their sum): catalog with the P1
enumeration about 45 s (25 s of it the 7,539,840 histories); per table directory about 0.7 s flop, 12 s turn,
12 s river with checksums (pure-Python FNV-1a, about 0.16 s per MB, twice per file) and 3,000 physical
boards per street; about 3.5 min for the 7 directories of out/monker/correctness/buckets. Peak commit
0.87 GB (the river table checks), 0.22 GB for the history enumeration.
Can fail (tested in memory on v1_flopturn_exact/flop): two merged orbits fail T3 and T4, a split orbit fails
T2 and T3, an id on a dead combo fails T1, two swapped rows fail T1-T4.

Usage: python tools/independent/sd_catalog_check.py [--catalog PATH] [--buckets-root DIR]
       [--tables NAME ...] [--streets flop,turn,river] [--kinds flop,flop_turn,river,history]
       [--physical-samples N] [--require-exact TABLE:STREET ...] [--out-dir DIR] [--skip-checksum]
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

import numpy as np

from sdref import boards
from sdref import cards as sdcards
from sdref import resources

DEFAULT_BUCKETS_ROOT = resources.REPO_ROOT / "out" / "monker" / "correctness" / "buckets"
DEFAULT_OUT_DIR = resources.INDEPENDENT_OUT_DIR / "S6_catalog"
REPORT_NAME = "monker_buckets_report.json"


def log(message: str) -> None:
    print(f"[{time.strftime('%H:%M:%S')}] {message}", file=sys.stderr, flush=True)


class Report:
    def __init__(self) -> None:
        self.checks: list[dict] = []

    def add(self, name: str, passed: bool, detail: dict | None = None) -> bool:
        self.checks.append({"check": name, "passed": bool(passed), **(detail or {})})
        log(f"{name}: {'PASS' if passed else 'FAIL'} {json.dumps(detail or {})[:300]}")
        return passed

    @property
    def passed(self) -> bool:
        return all(check["passed"] for check in self.checks)


def objects_text(objects: np.ndarray, kind: str) -> str:
    cards = [int(card) for card in objects]
    set_size = boards.KIND_SET_SIZE[kind]
    text = sdcards.format_cards(cards[:set_size])
    for card in cards[set_size:]:
        text += " " + sdcards.format_card(card)
    return text


def first_indices(mask: np.ndarray, limit: int) -> list[int]:
    return [int(index) for index in np.flatnonzero(mask)[:limit]]


# ---------------------------------------------------------------- Python-only and catalog checks


class KindState:
    """Decoded catalog entries of one kind and their independent orbit data."""

    def __init__(self, kind: str, entries: np.ndarray) -> None:
        self.kind = kind
        self.entries = entries
        self.codes = entries[:, 0].astype(np.int64)
        self.multiplicity = entries[:, 1].astype(np.int64)
        self.objects = boards.unpack(self.codes, kind)
        self.labels = boards.canonical_codes(self.objects, kind)
        self.orbit_size = boards.orbit_sizes(self.objects, kind)
        self.stabilizer = boards.stabilizer_mask(self.objects, kind)
        self.row_of_label = {int(label): row for row, label in enumerate(self.labels.tolist())}


def valid_objects(objects: np.ndarray, kind: str) -> np.ndarray:
    set_size = boards.KIND_SET_SIZE[kind]
    ok = np.all((objects >= 0) & (objects < sdcards.DECK_SIZE), axis=1)
    ok &= np.all(objects[:, 1:set_size] > objects[:, :set_size - 1], axis=1)  # the set part strictly ascends
    for a in range(objects.shape[1]):
        for b in range(a + 1, objects.shape[1]):
            ok &= objects[:, a] != objects[:, b]
    return ok


def check_kind(report: Report, catalog: boards.BoardCatalogFile, kind: str, enumerate_physical: bool,
               max_report: int) -> KindState:
    started = time.perf_counter()
    burnside, fixed = boards.burnside_orbit_count(kind)
    physical = boards.PHYSICAL_COUNTS[kind]
    state = KindState(kind, catalog.entries(kind))
    n = state.codes.shape[0]

    enumeration = None
    if enumerate_physical:
        objects = boards.physical_objects(kind)
        labels = boards.canonical_codes(objects, kind)
        del objects
        unique, counts = np.unique(labels, return_counts=True)
        del labels
        enumeration = (unique, counts)
        report.add(f"P1[{kind}] Burnside == enumeration", unique.shape[0] == burnside and
                   int(counts.sum()) == physical,
                   {"burnside": burnside, "enumerated_orbits": int(unique.shape[0]),
                    "enumerated_objects": int(counts.sum()), "physical": physical,
                    "fixed_by_identity": fixed[0]})

    valid = valid_objects(state.objects, kind)
    distinct = np.unique(state.labels).shape[0]
    stab_size = state.stabilizer.sum(axis=1)
    size_ok = (state.multiplicity == state.orbit_size) & (state.orbit_size * stab_size == boards.PERM_COUNT)
    report.add(f"C1[{kind}] catalog is a weighted orbit transversal",
               bool(valid.all()) and distinct == n and bool(size_ok.all()) and
               int(state.multiplicity.sum()) == physical and n == burnside,
               {"entries": n, "burnside": burnside, "invalid": int((~valid).sum()),
                "isomorphic_duplicates": n - distinct, "multiplicity_mismatches": int((~size_ok).sum()),
                "multiplicity_sum": int(state.multiplicity.sum()), "physical": physical,
                "symmetric_entries": int((stab_size > 1).sum()),
                "samples": [{"entry": i, "board": objects_text(state.objects[i], kind),
                             "multiplicity": int(state.multiplicity[i]), "orbit": int(state.orbit_size[i])}
                            for i in first_indices(~size_ok | ~valid, max_report)]})

    minimal = state.labels == state.codes
    increasing = bool(np.all(np.diff(state.codes) > 0))
    detail = {"non_minimal_codes": int((~minimal).sum()), "codes_increasing": increasing,
              "samples": [{"entry": i, "code": int(state.codes[i]), "orbit_min": int(state.labels[i])}
                          for i in first_indices(~minimal, max_report)]}
    passed = bool(minimal.all()) and increasing
    if enumeration is not None:
        same = enumeration[0].shape[0] == n and np.array_equal(enumeration[0], state.codes) and \
            np.array_equal(enumeration[1], state.multiplicity)
        detail["equals_enumeration"] = bool(same)
        passed = passed and bool(same)
    report.add(f"C2[{kind}] representatives and order (convention K3)", passed, detail)
    log(f"{kind}: {time.perf_counter() - started:.1f} s")
    return state


def check_cross_references(report: Report, states: dict[str, KindState], max_report: int) -> None:
    flop, flop_turn, river = states.get("flop"), states.get("flop_turn"), states.get("river")
    if flop is not None and flop_turn is not None:
        index = flop_turn.entries[:, 2].astype(np.int64)
        in_range = index < flop.codes.shape[0]
        labels = boards.canonical_codes(flop_turn.objects[:, :3], "flop")
        ok = in_range & (labels == flop.labels[np.minimum(index, flop.codes.shape[0] - 1)])
        report.add("C3[flop_turn.flop_index]", bool(ok.all()),
                   {"mismatches": int((~ok).sum()), "samples": first_indices(~ok, max_report)})
        weight = np.bincount(index[in_range], weights=flop_turn.multiplicity[in_range],
                             minlength=flop.codes.shape[0])
        ok = weight == 33 * flop.multiplicity
        report.add("C4[flop_turns per flop weigh 33x]", bool(ok.all()), {"mismatches": int((~ok).sum())})
    history = states.get("history")
    if history is None:
        return
    targets = (("flop", "flop_index", 2, lambda o: o[:, :3], 33 * 32),
               ("flop_turn", "flop_turn_index", 3, lambda o: o[:, :4], 32),
               ("river", "river_board_index", 4, lambda o: o, 20))
    for kind, field, column, part, factor in targets:
        state = states.get(kind)
        if state is None:
            continue
        index = history.entries[:, column].astype(np.int64)
        in_range = index < state.codes.shape[0]
        objects = part(history.objects)
        if kind == "river":
            objects = np.sort(objects, axis=1)
        labels = boards.canonical_codes(objects, kind)
        ok = in_range & (labels == state.labels[np.minimum(index, state.codes.shape[0] - 1)])
        report.add(f"C3[history.{field}]", bool(ok.all()),
                   {"mismatches": int((~ok).sum()), "samples": first_indices(~ok, max_report)})
        weight = np.bincount(index[in_range], weights=history.multiplicity[in_range],
                             minlength=state.codes.shape[0])
        ok = weight == factor * state.multiplicity
        report.add(f"C4[histories per {kind} weigh {factor}x]", bool(ok.all()),
                   {"mismatches": int((~ok).sum()), "samples": first_indices(~ok, max_report)})


# ---------------------------------------------------------------- bucket tables


def partition_counts(group: np.ndarray, first: np.ndarray, second: np.ndarray) -> tuple[int, int, int]:
    """Distinct (group, first), (group, second) and (group, first, second) over the given entries."""
    a = group * 4096 + first
    b = group * 65536 + second
    ab = a * 65536 + second
    return np.unique(a).shape[0], np.unique(b).shape[0], np.unique(ab).shape[0]


def check_table(report: Report, table_name: str, table: boards.BucketTableFile, state: KindState,
                catalog_fingerprint: str, exact: bool, samples: np.ndarray, max_report: int) -> dict:
    tag = f"{table_name}/{table.street}"
    ids = table.ids
    report.add(f"T0[{tag}] format", table.rows == state.codes.shape[0] and
               table.catalog_fingerprint == catalog_fingerprint and table.fingerprint_verified is not False,
               {"rows": table.rows, "catalog_rows": int(state.codes.shape[0]), "capacity": table.capacity,
                "width": table.width, "catalog_fingerprint": table.catalog_fingerprint,
                "recomputed_catalog_fingerprint": catalog_fingerprint,
                "checksum_verified": table.checksum_verified, "fingerprint_verified": table.fingerprint_verified,
                "feature_fingerprint": table.feature_fingerprint})
    if table.rows != state.codes.shape[0]:
        return {}

    live = boards.live_combo_mask(state.objects)
    dead_ok = np.where(live, ids < table.capacity, ids == boards.NO_BUCKET)
    report.add(f"T1[{tag}] dead combos 0xFFFF, live ids < capacity", bool(dead_ok.all()),
               {"bad_cells": int((~dead_ok).sum()),
                "bad_rows": first_indices(~dead_ok.all(axis=1), max_report)})

    constant_mismatches = 0
    bad_rows = np.zeros(table.rows, dtype=bool)
    for p in range(1, boards.PERM_COUNT):
        rows = np.flatnonzero(state.stabilizer[:, p])
        if rows.size == 0:
            continue
        moved = ids[rows][:, boards.COMBO_PERM[p]]
        differ = (moved != ids[rows]) & live[rows]
        constant_mismatches += int(differ.sum())
        bad_rows[rows[differ.any(axis=1)]] = True
    report.add(f"T2[{tag}] ids constant on stabilizer orbits", constant_mismatches == 0,
               {"mismatching_cells": constant_mismatches, "symmetric_rows": int((state.stabilizer.sum(axis=1) > 1)
                                                                                .sum()),
                "bad_rows": first_indices(bad_rows, max_report)})

    representative = boards.combo_orbit_representatives(state.stabilizer)
    row_index, combo = np.nonzero(live)
    reps = representative[row_index, combo]
    values = ids[row_index, combo].astype(np.int64)
    orbit_pairs, id_pairs, triples = partition_counts(row_index.astype(np.int64), reps, values)
    orbits_per_row = np.bincount(row_index[reps == combo], minlength=table.rows)
    ids_per_row = np.zeros(table.rows, dtype=np.int64)
    keys = np.unique(row_index.astype(np.int64) * 65536 + values)
    np.add.at(ids_per_row, keys // 65536, 1)
    observed_exact = orbit_pairs == id_pairs == triples
    detail = {"flagged_exact": exact, "observed_exact": bool(observed_exact),
              "orbits": orbit_pairs, "distinct_row_ids": id_pairs, "row_orbit_id_triples": triples,
              "max_orbits_per_row": int(orbits_per_row.max()), "capacity": table.capacity,
              "rows_with_fewer_ids_than_orbits": int((ids_per_row < orbits_per_row).sum()),
              "ids_per_row": {"min": int(ids_per_row.min()), "median": float(np.median(ids_per_row)),
                              "max": int(ids_per_row.max())},
              "orbits_per_row": {"min": int(orbits_per_row.min()), "median": float(np.median(orbits_per_row)),
                                 "max": int(orbits_per_row.max())}}
    if exact:
        merged = first_indices(ids_per_row != orbits_per_row, max_report)
        detail["merged_rows"] = [{"row": r, "board": objects_text(state.objects[r], state.kind),
                                  "orbits": int(orbits_per_row[r]), "ids": int(ids_per_row[r])} for r in merged]
        report.add(f"T3[{tag}] exact: id partition == orbit partition",
                   bool(observed_exact) and table.capacity == int(orbits_per_row.max()), detail)
    else:
        report.add(f"T3[{tag}] not flagged exact (information only)", True, detail)

    check_physical(report, tag, table, state, exact, samples, max_report)
    return detail


def check_physical(report: Report, tag: str, table: boards.BucketTableFile, state: KindState, exact: bool,
                   samples: np.ndarray, max_report: int) -> None:
    kind = state.kind
    n = samples.shape[0]
    if n == 0:
        return
    labels = boards.canonical_codes(samples, kind)
    rows = np.array([state.row_of_label.get(int(label), -1) for label in labels.tolist()], dtype=np.int64)
    found = rows >= 0
    rows = np.where(found, rows, 0)
    target = state.codes[rows]
    ids = table.ids
    physical_ids = np.full((n, sdcards.COMBO_COUNT), -1, dtype=np.int64)
    disagreements = 0
    permutations_used = np.zeros(n, dtype=np.int64)
    for p in range(boards.PERM_COUNT):
        reaches = boards.pack(boards.image(samples, kind, p), kind) == target
        boards_p = np.flatnonzero(reaches & found)
        if boards_p.size == 0:
            continue
        values = ids[rows[boards_p]][:, boards.COMBO_PERM[p]].astype(np.int64)
        fresh = physical_ids[boards_p, 0] < 0
        physical_ids[boards_p[fresh]] = values[fresh]
        disagreements += int((physical_ids[boards_p[~fresh]] != values[~fresh]).sum())
        permutations_used[boards_p] += 1
    stabilizer = boards.stabilizer_mask(samples, kind)
    live = boards.live_combo_mask(samples)
    dead_ok = np.where(live, (physical_ids >= 0) & (physical_ids < table.capacity),
                       physical_ids == boards.NO_BUCKET)
    reach_ok = permutations_used == stabilizer.sum(axis=1)  # |{s : s(x) = rep}| = |Stab(x)|
    detail = {"boards": n, "not_in_catalog": int((~found).sum()), "permutation_disagreements": disagreements,
              "canonicalizing_permutation_count_mismatches": int((~reach_ok).sum()),
              "bad_dead_or_range_cells": int((~dead_ok).sum())}
    passed = bool(found.all()) and disagreements == 0 and bool(reach_ok.all()) and bool(dead_ok.all())

    representative = boards.combo_orbit_representatives(stabilizer)
    board_index, combo = np.nonzero(live)
    orbit_pairs, id_pairs, triples = partition_counts(board_index.astype(np.int64),
                                                      representative[board_index, combo],
                                                      physical_ids[board_index, combo])
    detail.update({"orbits": orbit_pairs, "distinct_board_ids": id_pairs, "board_orbit_id_triples": triples})
    constant = triples == orbit_pairs
    passed = passed and constant
    if exact:
        passed = passed and orbit_pairs == id_pairs == triples
    bad = first_indices(~found | ~reach_ok | ~dead_ok.all(axis=1), max_report)
    detail["samples"] = [objects_text(samples[i], kind) for i in bad]
    report.add(f"T4[{tag}] physical boards{' (exact)' if exact else ''}", passed, detail)


def sample_physical(kind: str, count: int, rng: np.random.Generator) -> np.ndarray:
    """Uniform physical objects of rule B1 (set part sorted)."""
    deck = np.arange(sdcards.DECK_SIZE)
    width = boards.KIND_WIDTH[kind]
    out = np.empty((count, width), dtype=np.int64)
    for index in range(count):
        out[index] = rng.choice(deck, size=width, replace=False)
    set_size = boards.KIND_SET_SIZE[kind]
    out[:, :set_size] = np.sort(out[:, :set_size], axis=1)
    return out


def exact_streets_from_report(directory: Path) -> tuple[set[str], str | None]:
    path = directory / REPORT_NAME
    if not path.exists():
        return set(), f"no {REPORT_NAME}"
    try:
        settings = json.loads(path.read_text(encoding="utf-8")).get("street_settings", {})
    except (OSError, ValueError) as error:
        return set(), f"unreadable {REPORT_NAME}: {error}"
    return {street for street, value in settings.items() if isinstance(value, dict) and value.get("exact")}, None


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--catalog", type=Path, default=boards.CATALOG_PATH)
    parser.add_argument("--buckets-root", type=Path, default=DEFAULT_BUCKETS_ROOT)
    parser.add_argument("--tables", nargs="*", default=None,
                        help="table directory names under --buckets-root (default: every one with tables; "
                             "none: pass --tables with no names)")
    parser.add_argument("--streets", default="flop,turn,river")
    parser.add_argument("--kinds", default="flop,flop_turn,river,history",
                        help="catalog kinds to check (the tables need the kinds of their streets)")
    parser.add_argument("--no-enumeration", action="store_true",
                        help="skip P1 and the enumeration part of C2 (all 7,539,840 histories otherwise)")
    parser.add_argument("--physical-samples", type=int, default=3000,
                        help="random physical boards per street for T4")
    parser.add_argument("--require-exact", action="append", default=[],
                        help="TABLE:STREET that must be exact even if its report does not flag it")
    parser.add_argument("--seed", type=int, default=20260930)
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT_DIR)
    parser.add_argument("--skip-checksum", action="store_true",
                        help="skip the FNV-1a trailers and the table fingerprints")
    parser.add_argument("--max-report", type=int, default=10)
    args = parser.parse_args(argv)

    streets = [s for s in args.streets.split(",") if s]
    kinds = [k for k in args.kinds.split(",") if k]
    if any(s not in boards.STREET_NAMES for s in streets) or any(k not in boards.KINDS for k in kinds):
        print(f"unknown street or kind in {streets} / {kinds}", file=sys.stderr)
        return 2
    required: dict[str, set[str]] = {}
    for item in args.require_exact:
        name, _, street = item.partition(":")
        if street not in boards.STREET_NAMES:
            print(f"--require-exact {item}: expected TABLE:STREET", file=sys.stderr)
            return 2
        required.setdefault(name, set()).add(street)
    if args.tables is None:
        tables = sorted(d.name for d in args.buckets_root.iterdir()
                        if d.is_dir() and any((d / f"{s}_buckets_v1.bin").exists() for s in boards.STREET_NAMES)) \
            if args.buckets_root.exists() else []
    else:
        tables = args.tables
    for kind in {boards.STREET_KIND[s] for s in streets} if tables else set():
        if kind not in kinds:
            kinds.append(kind)

    started = time.perf_counter()
    report = Report()
    try:
        sdcards.self_check()
        boards.self_check()
        report.add("P0 sdref invariants", True)
    except AssertionError as error:
        report.add("P0 sdref invariants", False, {"error": repr(error)})
    try:
        catalog = boards.read_board_catalog(args.catalog, verify=not args.skip_checksum)
    except (OSError, ValueError) as error:
        print(f"cannot read the catalog: {error}", file=sys.stderr)
        return 2
    report.add("C0 catalog format", catalog.version == 1,
               {"path": str(args.catalog), "version": catalog.version,
                "counts": {k: int(catalog.entries(k).shape[0]) for k in boards.KINDS},
                "checksum_verified": catalog.checksum_verified, "fingerprint": catalog.fingerprint})

    states: dict[str, KindState] = {}
    for kind in boards.KINDS:
        if kind in kinds:
            states[kind] = check_kind(report, catalog, kind, not args.no_enumeration, args.max_report)
    check_cross_references(report, states, args.max_report)

    rng = np.random.default_rng(args.seed)
    samples = {s: sample_physical(boards.STREET_KIND[s], args.physical_samples, rng) for s in streets}
    table_summaries = {}
    for name in tables:
        directory = args.buckets_root / name
        flagged, note = exact_streets_from_report(directory)
        exact = flagged | required.get(name, set())
        table_summaries[name] = {"exact_streets": sorted(exact), "report_note": note, "streets": {}}
        if note:
            log(f"{name}: {note}; only --require-exact streets are checked as exact")
        for street in streets:
            path = directory / f"{street}_buckets_v1.bin"
            if not path.exists():
                report.add(f"T0[{name}/{street}] present", street not in exact, {"path": str(path)})
                continue
            table_started = time.perf_counter()
            try:
                table = boards.read_bucket_table(path, verify=not args.skip_checksum)
            except (OSError, ValueError) as error:
                report.add(f"T0[{name}/{street}] format", False, {"path": str(path), "error": str(error)})
                continue
            if table.street != street:
                report.add(f"T0[{name}/{street}] street", False, {"stored_street": table.street})
                continue
            detail = check_table(report, name, table, states[boards.STREET_KIND[street]], catalog.fingerprint,
                                 street in exact, samples[street], args.max_report)
            table_summaries[name]["streets"][street] = {
                "observed_exact": detail.get("observed_exact"), "capacity": table.capacity,
                "fingerprint": table.fingerprint, "seconds": round(time.perf_counter() - table_started, 1)}
            del table

    args.out_dir.mkdir(parents=True, exist_ok=True)
    summary = {
        "schema": "gtosd.independent.sd_catalog_check.v1",
        "passed": report.passed,
        "catalog": str(args.catalog),
        "catalog_fingerprint": catalog.fingerprint,
        "buckets_root": str(args.buckets_root),
        "tables": table_summaries,
        "physical_samples_per_street": args.physical_samples,
        "seed": args.seed,
        "seconds": round(time.perf_counter() - started, 1),
        "checks": report.checks,
    }
    out_path = args.out_dir / "sd_catalog_check.json"
    out_path.write_text(json.dumps(summary, indent=1) + "\n", encoding="utf-8")
    log(f"{'PASS' if report.passed else 'FAIL'}: {sum(c['passed'] for c in report.checks)}/"
        f"{len(report.checks)} checks, {summary['seconds']} s; summary in {out_path}")
    return 0 if report.passed else 1


if __name__ == "__main__":
    sys.exit(main())
