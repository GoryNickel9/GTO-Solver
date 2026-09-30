"""S2 of the correctness coverage (shared_components.md section 3): the heads-up preflop all-in table
preflop_all_in_v1.bin against Python counts built on the rules-based evaluator (sdref/ranking.py).

The trainer and the exact evaluator both load out/preflop_blueprint_resources/preflop_all_in_v1.bin: for every
unordered pair of disjoint combos, the wins / ties / losses of the lower combo id over the C(32,5) = 201,376
boards. A wrong entry would be invisible to NashConv. Checks:

Engine file only:
- A0 the file parses (container, FNV-1a trailer); its fingerprint equals the one recomputed from the
  rank-table fingerprint and the entries (it was built from that rank table), unless --rank-table "".
- A1 every one of the 176,715 disjoint pairs has W + T + L = 201,376; the 21,420 overlapping pairs are 0.
- A2 suit symmetry (a rule: suits are unordered): the table is invariant under all 24 suit permutations.
Python vs engine:
- S  (--sampled N) N random disjoint pairs plus 10 fixed edge pairs, each enumerated on its own 201,376
  boards (sort-based ranking path): exact integer (W, T, L) equality.
- X  (--exhaustive) board by board over all 376,992 boards (fast merge ranking path, checked first on 64
  boards against the sort path), accumulated for all pairs on up to --processes 4 processes: exact (W, T, L)
  equality for all 176,715 disjoint pairs, W + T + L = 201,376 on the Python side, and the sampled pairs
  equal to the exhaustive ones when both modes run.

The Python seven-card ordinals come from --python-ordinals (python_seven_ordinals.npy written by
sd_rank_check.py) or are rebuilt here (about 10 s). Either way they are the rules-based Python table, never
the engine's.

Outputs in --out-dir (default out/monker/correctness/independent/S2_allin): sd_allin_check.json (summary,
mismatch samples), python_seven_ordinals.npy when rebuilt, and with --exhaustive allin_counts_python.npz
(wins / ties 630 x 630, read with sdref.resources.read_python_allin; the input of the S4 step-1 LP).

Exit codes: 0 pass, 1 a mismatch, 2 usage, input or format error.
Expected cost (measured 30/09 on 3,000 boards and 210 pairs): sampled about 41 ms per pair on one core
(2,000 pairs about 1.5 min); exhaustive about 0.75 ms per board, i.e. about 280 CPU-s, 1.5-2 min wall on 4
processes; memory measured on the smoke runs: parent peak commit 0.47 GB (working set 0.25 GB) when it
rebuilds the Python ordinals, each worker 0.29 GB commit (0.08 GB working set).

Usage: python tools/independent/sd_allin_check.py [--sampled N] [--exhaustive] [--processes 1-4]
       [--python-ordinals PATH] [--all-in PATH] [--rank-table PATH|""] [--out-dir DIR]
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

import numpy as np

from sdref import cards as sdcards
from sdref import ranking, resources

DEFAULT_OUT_DIR = resources.INDEPENDENT_OUT_DIR / "S2_allin"
EDGE_PAIRS = (  # (hero, opponent) texts; checked in every sampled run
    ("AcAd", "KhKs"), ("Ah9h", "KcKd"), ("7c6c", "AhAd"), ("AcKc", "AdKd"), ("AhKh", "AsKs"),
    ("9h8h", "TcTd"), ("6c6d", "6h6s"), ("AcKd", "AhKs"), ("7c6c", "9d8d"), ("TcTd", "AhKh"),
)
log = ranking.log_to_stderr


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


def pair_text(first: int, second: int) -> str:
    return f"{sdcards.format_combo(first)} vs {sdcards.format_combo(second)}"


def disjoint_triangle() -> np.ndarray:
    """bool per triangular entry: True when the two combos are disjoint."""
    first, second = sdcards.combo_pair_arrays()
    return sdcards.combos_disjoint_matrix()[first, second]


def check_engine_file(report: Report, engine: resources.AllInFile, disjoint: np.ndarray) -> None:
    report.add("A0_file_integrity", engine.fingerprint_verified is not False,
               {"path": str(engine.path), "fingerprint": engine.fingerprint,
                "checksum_verified": engine.checksum_verified,
                "fingerprint_from_rank_table_verified": engine.fingerprint_verified})
    sums = engine.entries.astype(np.int64).sum(axis=1)
    bad_sums = np.flatnonzero(disjoint & (sums != sdcards.ALL_IN_RUNOUT_COUNT))
    bad_overlap = np.flatnonzero(~disjoint & (sums != 0))
    first, second = sdcards.combo_pair_arrays()
    report.add("A1_sums_and_overlaps", bad_sums.shape[0] == 0 and bad_overlap.shape[0] == 0
               and int(disjoint.sum()) == sdcards.DISJOINT_COMBO_PAIR_COUNT,
               {"disjoint_pairs": int(disjoint.sum()), "bad_sums": int(bad_sums.shape[0]),
                "nonzero_overlapping": int(bad_overlap.shape[0]),
                "samples": [pair_text(int(first[t]), int(second[t])) for t in
                            np.concatenate([bad_sums, bad_overlap])[:10].tolist()]})
    wins, ties = resources.all_in_matrices(engine.entries)
    broken = []
    card_maps = sdcards.card_permutation_table()
    for p, card_map in enumerate(card_maps):
        mapped = card_map[sdcards.COMBOS]  # (630, 2)
        image = sdcards.COMBO_INDEX[mapped[:, 0], mapped[:, 1]]
        if not (np.array_equal(wins[np.ix_(image, image)], wins)
                and np.array_equal(ties[np.ix_(image, image)], ties)):
            broken.append(list(sdcards.SUIT_PERMUTATIONS[p]))
    report.add("A2_suit_symmetry", not broken, {"permutations": len(card_maps), "broken": broken[:5]})


def python_ordinals(args, out_dir: Path, order: str) -> tuple[np.ndarray, Path]:
    if args.python_ordinals:
        path = Path(args.python_ordinals)
        values = np.load(path)
        if values.shape != (sdcards.SEVEN_CARD_SET_COUNT,) or values.dtype != np.uint16:
            raise ValueError(f"{path}: expected {sdcards.SEVEN_CARD_SET_COUNT} uint16 values")
        log(f"Python seven-card ordinals loaded from {path}")
        return values, path
    tables = ranking.RankTables(order, log=log)
    path = out_dir / "python_seven_ordinals.npy"
    np.save(path, tables.seven_ordinals)
    return tables.seven_ordinals, path


def sampled_pairs(count: int, seed: int, disjoint: np.ndarray) -> list[tuple[int, int]]:
    pairs = []
    for hero, opponent in EDGE_PAIRS:
        a = sdcards.combo_index(*sdcards.parse_cards(hero))
        b = sdcards.combo_index(*sdcards.parse_cards(opponent))
        pairs.append((a, b))
    first, second = sdcards.combo_pair_arrays()
    candidates = np.flatnonzero(disjoint)
    rng = np.random.default_rng(seed)
    chosen = rng.choice(candidates, size=min(count, candidates.shape[0]), replace=False)
    pairs.extend((int(first[t]), int(second[t])) for t in np.sort(chosen).tolist())
    return pairs


def engine_outcome(entries: np.ndarray, hero: int, opponent: int) -> tuple[int, int, int]:
    """(W, T, L) of `hero` against `opponent` from the triangular entries (hero = lower id, convention F3)."""
    wins, ties, losses = (int(v) for v in entries[sdcards.combo_pair_index(hero, opponent)])
    return (wins, ties, losses) if hero < opponent else (losses, ties, wins)


def run_sampled(report: Report, engine: resources.AllInFile, ordinals: np.ndarray, pairs, limit: int) -> dict:
    started = time.perf_counter()
    boards32 = sdcards.colex_subsets(5, 32)
    results = {}
    mismatches = []
    bad_sums = 0
    for position, (hero, opponent) in enumerate(pairs):
        python = ranking.pair_counts_direct(hero, opponent, ordinals, boards32)
        results[(hero, opponent)] = python
        bad_sums += sum(python) != sdcards.ALL_IN_RUNOUT_COUNT
        expected = engine_outcome(engine.entries, hero, opponent)
        if python != expected:
            mismatches.append({"pair": pair_text(hero, opponent), "combos": [hero, opponent],
                               "python_wtl": list(python), "engine_wtl": list(expected)})
        if position % 500 == 499:
            log(f"sampled {position + 1}/{len(pairs)} ({time.perf_counter() - started:.0f} s)")
    report.add("S_sampled_pairs_exact", not mismatches and bad_sums == 0,
               {"pairs": len(pairs), "edge_pairs": len(EDGE_PAIRS), "mismatches": len(mismatches),
                "python_bad_sums": int(bad_sums), "seconds": round(time.perf_counter() - started, 1),
                "samples": mismatches[:limit],
                "edge_results": {pair_text(h, o): list(results[(h, o)])
                                 for h, o in pairs[:len(EDGE_PAIRS)]}})
    return results


def check_fast_path(report: Report, ordinals: np.ndarray, seed: int) -> None:
    rng = np.random.default_rng(seed + 1)
    boards = sdcards.colex_subsets(5)[np.sort(rng.choice(sdcards.FIVE_CARD_SET_COUNT, 64, replace=False))]
    fast = ranking.combo_ordinals_on_boards(boards, ordinals)
    bad = 0
    for row, board in enumerate(boards):
        live = np.flatnonzero(fast[row] >= 0)
        expected_live = [i for i, (a, b) in enumerate(sdcards.COMBOS.tolist()) if a not in board and b not in board]
        bad += live.tolist() != expected_live
        for combo in live.tolist():
            slow = ranking.hand_ordinals_on_boards(sdcards.COMBOS[combo], board.reshape(1, 5), ordinals)[0]
            bad += int(slow) != int(fast[row, combo])
    report.add("X_pre_fast_ranking_path", bad == 0, {"boards": 64, "mismatches": int(bad)})


def run_exhaustive(report: Report, engine: resources.AllInFile, ordinals_path: Path, processes: int,
                   batch: int, disjoint: np.ndarray, sampled: dict, out_dir: Path, limit: int,
                   metadata: dict) -> None:
    started = time.perf_counter()
    wins, ties, boards = ranking.allin_counts_exhaustive(ordinals_path, processes=processes, batch=batch,
                                                         log=log)
    seconds = time.perf_counter() - started
    python = ranking.triangle_counts(wins, ties)
    sums = python.sum(axis=1)
    bad_sums = np.flatnonzero(disjoint & (sums != sdcards.ALL_IN_RUNOUT_COUNT))
    report.add("X0_python_enumeration", boards == sdcards.FIVE_CARD_SET_COUNT and bad_sums.shape[0] == 0,
               {"boards": boards, "bad_sums": int(bad_sums.shape[0]), "seconds": round(seconds, 1),
                "processes": processes})
    engine_entries = engine.entries.astype(np.int64)
    bad = np.flatnonzero(disjoint & np.any(python != engine_entries, axis=1))
    first, second = sdcards.combo_pair_arrays()
    report.add("X1_all_disjoint_pairs_exact", bad.shape[0] == 0,
               {"pairs": int(disjoint.sum()), "mismatches": int(bad.shape[0]),
                "samples": [{"pair": pair_text(int(first[t]), int(second[t])),
                             "python_wtl": python[t].tolist(), "engine_wtl": engine_entries[t].tolist()}
                            for t in bad[:limit].tolist()]})
    if sampled:
        differing = [pair_text(h, o) for (h, o), value in sampled.items()
                     if (int(wins[h, o]), int(ties[h, o]), int(wins[o, h])) != value]
        report.add("X2_sampled_equals_exhaustive", not differing,
                   {"pairs": len(sampled), "differing": differing[:limit]})
    path = out_dir / "allin_counts_python.npz"
    resources.write_python_allin(path, wins, ties, {**metadata, "boards": boards,
                                                    "passed_vs_engine": bad.shape[0] == 0})
    log(f"Python counts written to {path}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--all-in", type=Path, default=resources.ALL_IN_PATH)
    parser.add_argument("--rank-table", default=str(resources.RANK_TABLE_PATH),
                        help="rank table whose fingerprint the all-in fingerprint must derive from; '' to skip")
    parser.add_argument("--python-ordinals", default="",
                        help="python_seven_ordinals.npy from sd_rank_check.py (default: rebuild)")
    parser.add_argument("--sampled", type=int, default=None,
                        help="random disjoint pairs to enumerate (default 2000 unless --exhaustive)")
    parser.add_argument("--exhaustive", action="store_true")
    parser.add_argument("--processes", type=int, default=4, help="1-4 worker processes for --exhaustive")
    parser.add_argument("--batch", type=int, default=256, help="boards per ranking batch")
    parser.add_argument("--straight-vs-trips", choices=("straight", "trips"), default="straight")
    parser.add_argument("--seed", type=int, default=20260930)
    parser.add_argument("--skip-checksum", action="store_true")
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT_DIR)
    parser.add_argument("--max-report", type=int, default=20)
    args = parser.parse_args(argv)
    if not 1 <= args.processes <= 4:
        print("--processes must be 1-4", file=sys.stderr)
        return 2
    sampled_count = args.sampled if args.sampled is not None else (0 if args.exhaustive else 2000)
    order = ranking.ORDER_STRAIGHT_OVER_TRIPS if args.straight_vs_trips == "straight" else \
        ranking.ORDER_TRIPS_OVER_STRAIGHT
    started = time.perf_counter()
    args.out_dir.mkdir(parents=True, exist_ok=True)
    try:
        rank_fingerprint = None
        if args.rank_table:
            rank_fingerprint = resources.read_rank_table(Path(args.rank_table),
                                                         verify=not args.skip_checksum).fingerprint
        engine = resources.read_all_in(args.all_in, verify=not args.skip_checksum,
                                       rank_fingerprint=rank_fingerprint)
        sdcards.self_check()
        ordinals, ordinals_path = python_ordinals(args, args.out_dir, order)
    except (OSError, ValueError) as error:
        print(f"input error: {error}", file=sys.stderr)
        return 2

    report = Report()
    disjoint = disjoint_triangle()
    check_engine_file(report, engine, disjoint)
    sampled = {}
    if sampled_count > 0:
        sampled = run_sampled(report, engine, ordinals, sampled_pairs(sampled_count, args.seed, disjoint),
                              args.max_report)
    if args.exhaustive:
        check_fast_path(report, ordinals, args.seed)
        metadata = {"all_in": str(args.all_in), "engine_fingerprint": engine.fingerprint,
                    "straight_vs_trips": args.straight_vs_trips, "python_ordinals": str(ordinals_path),
                    "created": time.strftime("%Y-%m-%dT%H:%M:%S")}
        run_exhaustive(report, engine, ordinals_path, args.processes, args.batch, disjoint, sampled,
                       args.out_dir, args.max_report, metadata)

    summary = {
        "schema": "gtosd.independent.sd_allin_check.v1",
        "passed": report.passed,
        "mode": {"sampled": sampled_count, "exhaustive": args.exhaustive, "processes": args.processes},
        "straight_vs_trips": args.straight_vs_trips,
        "all_in": str(args.all_in),
        "seconds": round(time.perf_counter() - started, 1),
        "checks": report.checks,
    }
    (args.out_dir / "sd_allin_check.json").write_text(json.dumps(summary, indent=1) + "\n", encoding="utf-8")
    log(f"{'PASS' if report.passed else 'FAIL'}: {sum(c['passed'] for c in report.checks)}/"
        f"{len(report.checks)} checks, {summary['seconds']} s; summary in "
        f"{args.out_dir / 'sd_allin_check.json'}")
    return 0 if report.passed else 1


if __name__ == "__main__":
    sys.exit(main())
