"""S1 of the correctness coverage (shared_components.md section 3): exhaustive short-deck hand ranking
against the engine's rank_table_v1.bin, from an evaluator written from the rules (sdref/ranking.py).

The trainer and the exact evaluator both load out/preflop_blueprint_resources/rank_table_v1.bin, so a
ranking error there is invisible to NashConv. This script rebuilds the table in Python and compares:

Python-only checks (the reference checks itself):
- P0 sdref.cards invariants (combo order, classes, colex indexing).
- P1 the 376,992 five-card sets per category equal closed-form counts from the rules, and so do the
  distinct hand values per category (1,404 in total).
- P2 the direct seven-card evaluator (evaluate_seven, no subset enumeration) agrees with evaluate_five on
  every five-card set.
- P3 the direct evaluator agrees with the maximum over the 21 five-card subsets (rule R1) on every
  seven-card set (default) or on --direct-sample random sets.
Engine comparisons:
- E0 the file parses (container, sizes, FNV-1a trailer, fingerprint recomputed from the payload) and its
  "distinct" field equals the Python number of distinct five-card values.
- E1 five-card ordinals: engine == Python dense rank, exactly, for all 376,992 sets.
- E2 seven-card ordinals: engine == Python ordinal of the best five-card hand, exactly, for all 8,347,680
  sets.
- E3 weak order: the dense ranks of the engine values and of the Python keys are equal for both tables
  (the relabelling-proof form of E1 / E2).
- V1 (if a values.json is given or the default exists): combos.labels, combos.class and classes equal the
  Python combo order and class rules.

Pass = every check true. With --straight-vs-trips trips the engine checks are expected to fail (the engine
plays straight > trips); that mode exists for S7.

Outputs in --out-dir (default out/monker/correctness/independent/S1_rank): sd_rank_check.json (summary,
mismatch samples) and, unless --no-save-ordinals, python_seven_ordinals.npy (8,347,680 uint16, the Python
seven-card ordinals that sd_allin_check.py --python-ordinals can reuse).

Exit codes: 0 pass, 1 a mismatch, 2 usage, input or format error.
Expected cost (one core, measured 30/09 with a 20,000-set P3): about 15 s for the file, the tables and the
engine checks, plus about 10 us per set for P3 (about 1.5 min over all 8,347,680 sets); peak commit
0.67 GB (working set 0.46 GB) measured with a 1,000-set P3.

Usage: python tools/independent/sd_rank_check.py [--rank-table PATH] [--values-json PATH|""]
       [--out-dir DIR] [--direct-sample N] [--straight-vs-trips straight|trips] [--skip-checksum]
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

DEFAULT_VALUES_JSON = resources.REPO_ROOT / "out" / "monker" / "correctness" / "V2" / "charts" / "it_250" / \
    "values.json"
DEFAULT_OUT_DIR = resources.INDEPENDENT_OUT_DIR / "S1_rank"
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


def set_text(rows: np.ndarray, index: int) -> str:
    return sdcards.format_cards(rows[index].tolist())


def mismatch_samples(indices: np.ndarray, rows: np.ndarray, python_keys: np.ndarray, python_values,
                     engine_values, order: str, limit: int) -> list[dict]:
    samples = []
    for index in indices[:limit].tolist():
        samples.append({"colex": index, "cards": set_text(rows, index),
                        "python": ranking.describe_key(python_keys[index], order),
                        "python_ordinal": int(python_values[index]),
                        "engine_ordinal": int(engine_values[index])})
    return samples


def check_python_self(report: Report, tables: ranking.RankTables, order: str, direct_sample: int,
                      seed: int, limit: int) -> None:
    sdcards.self_check()
    report.add("P0_cards_invariants", True)

    values = ranking.category_values(order)
    categories = tables.five_keys >> 20
    census = {}
    ok = True
    for name, (sets, distinct) in ranking.five_card_census_closed_form().items():
        mask = categories == values[name]
        got_sets = int(mask.sum())
        got_distinct = int(np.unique(tables.five_keys[mask]).shape[0])
        census[name] = {"sets": got_sets, "expected_sets": sets, "distinct": got_distinct,
                        "expected_distinct": distinct}
        ok &= got_sets == sets and got_distinct == distinct
    ok &= tables.distinct.shape[0] == 1404
    report.add("P1_five_card_census", ok, {"distinct": int(tables.distinct.shape[0]), "census": census})

    started = time.perf_counter()
    rows = tables.five_subsets
    direct = np.fromiter((ranking.evaluate_seven(row, order) for row in rows.tolist()), dtype=np.uint32,
                         count=rows.shape[0])
    bad = np.flatnonzero(direct != tables.five_keys)
    report.add("P2_direct_vs_five_on_five_card_sets", bad.shape[0] == 0,
               {"sets": int(rows.shape[0]), "mismatches": int(bad.shape[0]),
                "seconds": round(time.perf_counter() - started, 1),
                "samples": [{"cards": set_text(rows, i),
                             "evaluate_five": ranking.describe_key(tables.five_keys[i], order),
                             "evaluate_seven": ranking.describe_key(direct[i], order)}
                            for i in bad[:limit].tolist()]})

    started = time.perf_counter()
    rows = tables.seven_subsets
    total = rows.shape[0]
    if direct_sample < 0 or direct_sample >= total:
        indices = None
        count = total
    else:
        rng = np.random.default_rng(seed)
        indices = np.sort(rng.choice(total, size=direct_sample, replace=False))
        count = direct_sample
    mismatches = 0
    samples = []
    chunk = 200_000
    for start in range(0, count, chunk):
        chosen = np.arange(start, min(start + chunk, count)) if indices is None else indices[start:start + chunk]
        keys = np.fromiter((ranking.evaluate_seven(row, order) for row in rows[chosen].tolist()),
                           dtype=np.uint32, count=chosen.shape[0])
        bad = np.flatnonzero(keys != tables.seven_keys[chosen])
        mismatches += int(bad.shape[0])
        for position in bad[:max(0, limit - len(samples))].tolist():
            index = int(chosen[position])
            samples.append({"colex": index, "cards": set_text(rows, index),
                            "max_of_21": ranking.describe_key(tables.seven_keys[index], order),
                            "direct": ranking.describe_key(keys[position], order)})
        if (start // chunk) % 10 == 9:
            log(f"P3 progress {start + chosen.shape[0]}/{count} ({time.perf_counter() - started:.0f} s)")
    report.add("P3_direct_vs_max_of_21_on_seven_card_sets", mismatches == 0,
               {"sets": int(count), "exhaustive": indices is None, "mismatches": mismatches,
                "seconds": round(time.perf_counter() - started, 1), "samples": samples})


def check_engine(report: Report, tables: ranking.RankTables, engine: resources.RankTableFile, order: str,
                 limit: int) -> None:
    report.add("E0_file_integrity", engine.distinct == tables.distinct.shape[0],
               {"path": str(engine.path), "fingerprint": engine.fingerprint,
                "checksum_verified": engine.checksum_verified,
                "fingerprint_verified": engine.fingerprint_verified,
                "engine_distinct": engine.distinct, "python_distinct": int(tables.distinct.shape[0])})

    for name, python_values, engine_values, keys, rows in (
            ("E1_five_card_ordinals_exact", tables.five_ordinals, engine.five, tables.five_keys,
             tables.five_subsets),
            ("E2_seven_card_ordinals_exact", tables.seven_ordinals, engine.seven, tables.seven_keys,
             tables.seven_subsets)):
        bad = np.flatnonzero(python_values != engine_values)
        report.add(name, bad.shape[0] == 0,
                   {"sets": int(engine_values.shape[0]), "mismatches": int(bad.shape[0]),
                    "samples": mismatch_samples(bad, rows, keys, python_values, engine_values, order, limit)})

    for name, keys, engine_values in (("E3_five_card_weak_order", tables.five_keys, engine.five),
                                      ("E3_seven_card_weak_order", tables.seven_keys, engine.seven)):
        python_dense = np.unique(keys, return_inverse=True)[1].reshape(-1)
        engine_dense = np.unique(engine_values, return_inverse=True)[1].reshape(-1)
        bad = np.flatnonzero(python_dense != engine_dense)
        report.add(name, bad.shape[0] == 0,
                   {"python_levels": int(python_dense.max()) + 1, "engine_levels": int(engine_dense.max()) + 1,
                    "mismatches": int(bad.shape[0])})


def check_values_json(report: Report, path: Path) -> None:
    data = json.loads(path.read_text(encoding="utf-8"))
    labels = data["combos"]["labels"]
    classes = data["combos"]["class"]
    names = data.get("classes")
    bad_labels = [i for i, label in enumerate(labels) if label != sdcards.COMBO_LABELS[i]]
    bad_classes = [i for i, value in enumerate(classes) if value != int(sdcards.COMBO_CLASS[i])]
    names_ok = names is None or list(names) == list(sdcards.CLASS_NAMES)
    report.add("V1_values_json_combos_and_classes",
               len(labels) == sdcards.COMBO_COUNT and len(classes) == sdcards.COMBO_COUNT and not bad_labels
               and not bad_classes and names_ok,
               {"path": str(path), "combos": len(labels), "label_mismatches": bad_labels[:20],
                "class_mismatches": bad_classes[:20], "class_names_checked": names is not None,
                "class_names_equal": names_ok})


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--rank-table", type=Path, default=resources.RANK_TABLE_PATH)
    parser.add_argument("--values-json", default=None,
                        help="values.json to compare combos with; default the V2 it_250 one if present, "
                             "'' to skip")
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT_DIR)
    parser.add_argument("--direct-sample", type=int, default=-1,
                        help="seven-card sets for P3; -1 (default) = all 8,347,680")
    parser.add_argument("--straight-vs-trips", choices=("straight", "trips"), default="straight",
                        help="category order: straight > trips (engine, default) or trips > straight")
    parser.add_argument("--seed", type=int, default=20260930)
    parser.add_argument("--skip-checksum", action="store_true",
                        help="skip the FNV-1a trailer and fingerprint verification of the file")
    parser.add_argument("--no-save-ordinals", action="store_true")
    parser.add_argument("--max-report", type=int, default=20)
    args = parser.parse_args(argv)

    order = ranking.ORDER_STRAIGHT_OVER_TRIPS if args.straight_vs_trips == "straight" else \
        ranking.ORDER_TRIPS_OVER_STRAIGHT
    if args.values_json is None:
        values_json = DEFAULT_VALUES_JSON if DEFAULT_VALUES_JSON.exists() else None
    elif args.values_json == "":
        values_json = None
    else:
        values_json = Path(args.values_json)
        if not values_json.exists():
            print(f"missing {values_json}", file=sys.stderr)
            return 2
    started = time.perf_counter()
    try:
        engine = resources.read_rank_table(args.rank_table, verify=not args.skip_checksum)
    except (OSError, ValueError) as error:
        print(f"cannot read the rank table: {error}", file=sys.stderr)
        return 2
    log(f"read {args.rank_table} ({time.perf_counter() - started:.1f} s)")

    report = Report()
    tables = ranking.RankTables(order, keep_subsets=True, log=log)
    check_python_self(report, tables, order, args.direct_sample, args.seed, args.max_report)
    check_engine(report, tables, engine, order, args.max_report)
    if values_json is not None:
        check_values_json(report, values_json)

    args.out_dir.mkdir(parents=True, exist_ok=True)
    if not args.no_save_ordinals:
        np.save(args.out_dir / "python_seven_ordinals.npy", tables.seven_ordinals)
    summary = {
        "schema": "gtosd.independent.sd_rank_check.v1",
        "passed": report.passed,
        "rules": {"category_order_weak_to_strong": list(ranking._WEAK_TO_STRONG[order]),
                  "straight_vs_trips": args.straight_vs_trips,
                  "engine_rule": "straight > trips; a mismatch is expected with trips"},
        "rank_table": str(args.rank_table),
        "values_json": str(values_json) if values_json else None,
        "seconds": round(time.perf_counter() - started, 1),
        "checks": report.checks,
    }
    (args.out_dir / "sd_rank_check.json").write_text(json.dumps(summary, indent=1) + "\n", encoding="utf-8")
    log(f"{'PASS' if report.passed else 'FAIL'}: {sum(c['passed'] for c in report.checks)}/"
        f"{len(report.checks)} checks, {summary['seconds']} s; summary in {args.out_dir / 'sd_rank_check.json'}")
    return 0 if report.passed else 1


if __name__ == "__main__":
    sys.exit(main())
