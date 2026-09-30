"""S4 of the correctness coverage (shared_components.md section 3): the exact value of the heads-up step-1 game
(preflop + checkdown) by a sequence-form LP, against the engine's step-1 EV and gains.

The game is built from the rules (sdref/rules.py, S3) and the Python all-in counts (sdref, S2) at hand-class
level, solved exactly as a zero-sum game with two sequence-form LPs (scipy HiGHS), and the value is certified
at the combo level (sdref/step1.py: lower = BTN's best response to the CO LP strategy, upper = CO's best
response to the BTN LP strategy, both over the 630 combos with exact card removal). Nothing of the engine is
used except its summary numbers (and, with --allin engine, its all-in table).

Comparison with an engine step-1 summary (gtosd_preflop_blueprint_checkdown, schema
gtosd.preflop_blueprint_checkdown.v1 or ..._classes.v1): the engine reports CO's EV0 of its average profile and
the best-response gains gain0 (CO) and gain1 (BTN). Zero-sum duality puts the true value in
    [EV0 - gain1, EV0 + gain0]
(for HU50 at 5,000 iterations: [-0.1210452592, -0.1210436490] a, width 1.6e-6 a). Since the certified interval
[lower, upper] also contains the true value, two disjoint intervals prove that the engine solved a different
game (tree, amounts, payoffs, all-in equities, card removal or chance).

Checks per case:
- T  tree: config id and node count equal the summary's; every payoff row sums to zero (no rake).
- O  all-in counts: W + T + L = 201,376 for every disjoint combo pair, ties symmetric, 353,430 ordered deals,
     561 opponents per combo; class sums consistent. With --allin python the counts are S2's Python counts
     (allin_counts_python.npz, written by sd_allin_check.py --exhaustive); the npz must cover all 376,992
     boards with straight > trips, and the engine table S2 compared (its fingerprint) must be the one the
     engine run used (the summary's all-in fingerprint).
- L  both LPs optimal and their values agree.
- C  the combo-level certificate is tight: upper - lower <= --certificate-tolerance (1e-9 a).
- W  the certified interval meets the engine window (a slack of 1e-11 a covers the summary's 12 printed
     digits); PASS also reports where v* lies in the window.
- M  sensitivity (--mutations, default on): the value of three wrong games, which must fall outside the
     window: no card removal in the chance weights, ties paid to the BTN, and stacks one ante shorter. When a
     summary is given and a wrong game lands inside the window, W cannot tell the games apart and the case is
     INCONCLUSIVE (exit 4), not PASS.

Without a summary (e.g. HU6_all, whose step-1 run does not exist yet) the case reports the certified value
only (status VALUE_ONLY). Raked configs are refused: the game is then general-sum and has no unique value.

Exit codes: 0 every case PASS or VALUE_ONLY; 1 a mismatch (FAIL); 2 usage or input error (e.g. the S2 npz is
missing); 4 inconclusive (a certificate wider than the tolerance, or an LP not optimal).
Expected cost (measured 30/09, one core, default cases with mutations): about 4 s wall in total (loading the
counts 0.5-1 s, per case 0.6 s for the two LPs and 0.2 s for the certificate, about 0.5 s per mutation);
peak working set 0.22 GB.

Usage: python tools/independent/sd_step1_lp.py [CONFIG[=SUMMARY] ...] [--allin python|engine]
       [--allin-npz PATH] [--all-in PATH] [--report PATH] [--no-mutations] [--method highs-ds|highs-ipm]
Default cases: benchmarks/monker/HU50.json=out/monker/step1/HU50/summary.json and
benchmarks/monker/correctness/HU6_all.json.
"""

from __future__ import annotations

import argparse
import dataclasses
import json
import sys
import time
from pathlib import Path

import numpy as np

from sdref import cards as sdcards
from sdref import ranking, resources
from sdref import rules as R
from sdref import step1

DEFAULT_CASES = (
    "benchmarks/monker/HU50.json=out/monker/step1/HU50/summary.json",
    "benchmarks/monker/correctness/HU6_all.json",
)
DEFAULT_NPZ = resources.INDEPENDENT_OUT_DIR / "S2_allin" / "allin_counts_python.npz"
DEFAULT_REPORT = resources.INDEPENDENT_OUT_DIR / "S4_step1" / "sd_step1_lp.json"
SUMMARY_SCHEMAS = ("gtosd.preflop_blueprint_checkdown.v1", "gtosd.preflop_blueprint_checkdown_classes.v1")
WINDOW_SLACK = 1e-11
log = ranking.log_to_stderr


class InputError(RuntimeError):
    pass


def repo_path(text: str) -> Path:
    path = Path(text)
    return path if path.is_absolute() else resources.REPO_ROOT / path


def parse_case(text: str) -> tuple[Path, Path | None]:
    config, _, summary = text.partition("=")
    return repo_path(config), (repo_path(summary) if summary else None)


# ------------------------------------------------------------------------------------------ inputs


def load_counts(args) -> tuple[step1.Outcomes, dict]:
    """(outcomes, provenance) from the S2 Python counts or, for diagnosis only, the engine table."""
    if args.allin == "python":
        if not args.allin_npz.is_file():
            raise InputError(f"{args.allin_npz} not found: run sd_allin_check.py --exhaustive first "
                             "(or pass --allin engine for a non-independent diagnosis)")
        wins, ties, meta = resources.read_python_allin(args.allin_npz)
        if meta.get("boards") != sdcards.FIVE_CARD_SET_COUNT:
            raise InputError(f"{args.allin_npz}: {meta.get('boards')} boards, expected all "
                             f"{sdcards.FIVE_CARD_SET_COUNT}")
        if meta.get("straight_vs_trips", "straight") != "straight":
            raise InputError(f"{args.allin_npz}: counts built with trips > straight")
        provenance = {"source": "python", "path": str(args.allin_npz),
                      "engine_fingerprint_compared_by_S2": meta.get("engine_fingerprint"),
                      "S2_passed_vs_engine": meta.get("passed_vs_engine"), "created": meta.get("created"),
                      "independent": True}
    else:
        table = resources.read_all_in(args.all_in, verify=True)
        if not table.checksum_verified:
            raise InputError(f"{args.all_in}: checksum mismatch")
        wins, ties = resources.all_in_matrices(table.entries)
        provenance = {"source": "engine", "path": str(args.all_in), "fingerprint": table.fingerprint,
                      "independent": False}
    return step1.outcomes_from_counts(wins, ties), provenance


def load_summary(path: Path) -> dict:
    document = json.loads(path.read_text(encoding="utf-8"))
    if document.get("schema") not in SUMMARY_SCHEMAS:
        raise InputError(f"{path}: schema {document.get('schema')!r}, expected one of {SUMMARY_SCHEMAS}")
    ev, gain = document.get("ev_antes"), document.get("gain_antes")
    if not (isinstance(ev, list) and len(ev) == 2 and isinstance(gain, list) and len(gain) == 2):
        raise InputError(f"{path}: ev_antes / gain_antes are not heads-up pairs")
    return document


def summary_all_in_fingerprint(summary: dict) -> str | None:
    if "all_in_table_fingerprint" in summary:
        return summary["all_in_table_fingerprint"]
    return (summary.get("terminal_source") or {}).get("fingerprint")


# ------------------------------------------------------------------------------------------- solving


def solve(nodes, out, method: str, weights=None) -> dict:
    form = step1.build_sequence_form(nodes, out, weights)
    started = time.perf_counter()
    co = step1.solve_seat(form, step1.CO, method)
    btn = step1.solve_seat(form, step1.BTN, method)
    return {"form": form, "co": co, "btn": btn, "seconds": time.perf_counter() - started}


def lp_summary(solved: dict) -> dict:
    form, co, btn = solved["form"], solved["co"], solved["btn"]
    return {"sequences": form.sizes, "constraints": [c.shape[0] for c in form.constraints],
            "payoff_nonzeros": int(form.payoff.nnz),
            "co": {"value": co.value, "status": co.status, "message": co.message, "iterations": co.iterations},
            "btn": {"value": btn.value, "status": btn.status, "message": btn.message,
                    "iterations": btn.iterations},
            "seconds": round(solved["seconds"], 3)}


def mutation_values(rules, nodes, out: step1.Outcomes, method: str) -> dict:
    """Values of three wrong games (sensitivity of the comparison; LP values, class level)."""
    results = {}
    mass = np.array([sdcards.class_mass(c) for c in range(sdcards.CLASS_COUNT)], dtype=np.float64)
    weights = np.outer(mass, mass) * (step1.ORDERED_DEALS / sdcards.COMBO_COUNT ** 2)
    cases = {
        "no_card_removal": (nodes, out, weights),
        "ties_to_btn": (nodes, dataclasses.replace(out, class_t=np.zeros_like(out.class_t),
                                                   class_l=out.class_l + out.class_t), None),
    }
    shorter = dataclasses.replace(rules, stack=rules.stack - R.UNITS_PER_ANTE)
    try:
        cases["stack_minus_one_ante"] = (step1.build_tree(shorter), out, None)
    except (step1.Step1Error, R.ConfigError) as error:
        results["stack_minus_one_ante"] = {"error": str(error)}
    for name, (tree, counts, chance) in cases.items():
        solved = solve(tree, counts, method, chance)
        results[name] = {"value": solved["co"].value, "btn_lp_value": solved["btn"].value,
                         "nodes": len(tree)}
    return results


# -------------------------------------------------------------------------------------------- a case


def run_case(args, config: Path, summary_path: Path | None, out: step1.Outcomes, provenance: dict) -> dict:
    rules = R.load_rules(config)
    label = config.stem
    result = {"config": str(config), "config_id": rules.config_id, "summary": str(summary_path or ""),
              "checks": {}}
    checks = result["checks"]
    nodes = step1.build_tree(rules)
    result["tree"] = {"nodes": len(nodes),
                      "decisions": sum(n.kind == "decision" for n in nodes),
                      "folds": sum(n.kind == "fold" for n in nodes),
                      "showdowns": sum(n.kind == "showdown" for n in nodes),
                      "runouts": sum(n.settle_kind == "runout" for n in nodes),
                      "checkdowns": sum(n.settle_kind == "checkdown" for n in nodes)}
    summary = None
    if summary_path is not None:
        summary = load_summary(summary_path)
        checks["T_config_id"] = summary.get("config_id") == rules.config_id
        checks["T_node_count"] = summary.get("nodes") == len(nodes)
        engine_fingerprint = summary_all_in_fingerprint(summary)
        compared = provenance.get("engine_fingerprint_compared_by_S2", provenance.get("fingerprint"))
        if engine_fingerprint is not None and compared is not None:
            checks["O_same_all_in_table_as_engine_run"] = engine_fingerprint == compared
        if provenance["source"] == "python" and provenance.get("S2_passed_vs_engine") is not None:
            checks["O_S2_passed"] = bool(provenance["S2_passed_vs_engine"])
    checks.update({f"O_{name}": value for name, value in step1.outcome_checks(out).items()})

    solved = solve(nodes, out, args.method)
    result["lp"] = lp_summary(solved)
    co, btn = solved["co"], solved["btn"]
    lp_ok = co.status == 0 and btn.status == 0
    checks["L_lp_optimal"] = lp_ok
    certificate = None
    if lp_ok:
        checks["L_lp_values_agree"] = abs(co.value - btn.value) <= 1e-7
        form = solved["form"]
        co_strategy = step1.behavioral(form, step1.CO, co.plan)
        btn_strategy = step1.behavioral(form, step1.BTN, btn.plan)
        started = time.perf_counter()
        certificate = step1.certify(nodes, out, co_strategy, btn_strategy)
        result["certificate"] = {**dataclasses.asdict(certificate),
                                 "seconds": round(time.perf_counter() - started, 3)}
        checks["C_certificate_tight"] = certificate.width <= args.certificate_tolerance
        # lower <= v* <= upper by construction; a negative width beyond float noise is a bug of the evaluation.
        checks["C_certificate_ordered"] = certificate.width >= -1e-12
        if args.strategies:
            result["strategies"] = {"CO": step1.strategy_report(form, step1.CO, co_strategy),
                                    "BTN": step1.strategy_report(form, step1.BTN, btn_strategy)}

    if summary is not None:
        ev0, ev1 = (float(v) for v in summary["ev_antes"])
        gain0, gain1 = (float(v) for v in summary["gain_antes"])
        window = [ev0 - gain1, ev0 + gain0]
        result["engine"] = {"ev_antes": [ev0, ev1], "gain_antes": [gain0, gain1], "window": window,
                            "window_width": window[1] - window[0], "iterations": summary.get("iterations"),
                            "tree_fingerprint": summary.get("tree_fingerprint"),
                            "all_in_fingerprint": summary_all_in_fingerprint(summary)}
        checks["T_engine_zero_sum"] = abs(ev0 + ev1) <= 1e-9
        checks["T_engine_gains_nonnegative"] = gain0 >= -1e-12 and gain1 >= -1e-12
        if certificate is not None:
            meets = (certificate.lower <= window[1] + WINDOW_SLACK
                     and certificate.upper >= window[0] - WINDOW_SLACK)
            checks["W_value_in_engine_window"] = meets
            middle = 0.5 * (certificate.lower + certificate.upper)
            result["comparison"] = {
                "value": middle, "value_minus_engine_ev": middle - ev0,
                "position_in_window": (middle - window[0]) / (window[1] - window[0])
                if window[1] > window[0] else None,
                "distance_outside_window": max(window[0] - certificate.upper, certificate.lower - window[1], 0.0)}

    if args.mutations and lp_ok:
        mutations = mutation_values(rules, nodes, out, args.method)
        reference = 0.5 * (certificate.lower + certificate.upper) if certificate is not None else co.value
        for entry in mutations.values():
            if "value" not in entry:
                continue
            entry["shift"] = entry["value"] - reference
            if summary is not None:
                low, high = result["engine"]["window"]
                entry["outside_engine_window"] = not (low - WINDOW_SLACK <= entry["value"] <= high + WINDOW_SLACK)
            else:
                entry["shift_exceeds_1.6e-6"] = abs(entry["shift"]) > 1.6e-6
        result["mutations"] = mutations
        if summary is not None:
            # Non-vacuity of W: a window wide enough to hold a wrong game proves nothing (e.g. an engine run
            # with too few iterations). Such a case is INCONCLUSIVE, not PASS.
            checks["M_window_rejects_mutations"] = all(entry.get("outside_engine_window", True)
                                                       for entry in mutations.values())

    failed = [name for name, ok in checks.items() if not ok]
    if not lp_ok or (failed and set(failed) <= {"C_certificate_tight", "M_window_rejects_mutations"}):
        status = "INCONCLUSIVE"
    elif failed:
        status = "FAIL"
    elif summary is None:
        status = "VALUE_ONLY"
    else:
        status = "PASS"
    result["failed_checks"] = failed
    result["status"] = status

    value_text = (f"v*=[{certificate.lower:.12f}, {certificate.upper:.12f}] width={certificate.width:.1e}"
                  if certificate is not None else f"lp={co.value!r}/{btn.value!r}")
    engine_text = ""
    if "engine" in result:
        low, high = result["engine"]["window"]
        engine_text = f" engine_window=[{low:.10f}, {high:.10f}]"
        if "comparison" in result:
            engine_text += f" v*-EV0={result['comparison']['value_minus_engine_ev']:+.3e}"
    print(f"SD_STEP1_LP {label} nodes={len(nodes)} {value_text}{engine_text} -> {status}")
    for name in failed:
        print(f"  failed: {name}")
    for name, entry in result.get("mutations", {}).items():
        if "value" in entry:
            flag = entry.get("outside_engine_window", entry.get("shift_exceeds_1.6e-6"))
            print(f"  mutation {name}: value {entry['value']:.9f} (shift {entry['shift']:+.3e}) "
                  f"{'detected' if flag else 'NOT DETECTED'}")
        else:
            print(f"  mutation {name}: {entry['error']}")
    return result


# ---------------------------------------------------------------------------------------------- main


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("cases", nargs="*", default=list(DEFAULT_CASES),
                        help="CONFIG[=SUMMARY] (paths relative to the repo root allowed)")
    parser.add_argument("--allin", choices=("python", "engine"), default="python",
                        help="all-in counts: S2's Python npz (independent) or the engine table (diagnosis)")
    parser.add_argument("--allin-npz", type=Path, default=DEFAULT_NPZ)
    parser.add_argument("--all-in", type=Path, default=resources.ALL_IN_PATH, help="engine table (--allin engine)")
    parser.add_argument("--report", type=Path, default=DEFAULT_REPORT)
    parser.add_argument("--method", choices=("highs-ds", "highs-ipm", "highs"), default="highs-ds")
    parser.add_argument("--certificate-tolerance", type=float, default=1e-9)
    parser.add_argument("--no-mutations", dest="mutations", action="store_false")
    parser.add_argument("--no-strategies", dest="strategies", action="store_false",
                        help="do not write the per-class LP strategies into the report")
    args = parser.parse_args(argv)

    started = time.perf_counter()
    try:
        cases = [parse_case(text) for text in args.cases]
        for config, summary in cases:
            for path in (config, summary):
                if path is not None and not path.is_file():
                    raise InputError(f"{path} not found")
        log(f"loading all-in counts ({args.allin})")
        out, provenance = load_counts(args)
        log(f"counts loaded in {time.perf_counter() - started:.1f} s")
        results = []
        for config, summary in cases:
            results.append(run_case(args, config, summary, out, provenance))
    except (InputError, R.ConfigError, step1.Step1Error, OSError, ValueError, json.JSONDecodeError) as error:
        print(f"SD_STEP1_LP ERROR {error}")
        return 2

    statuses = [r["status"] for r in results]
    report = {"schema": "gtosd.independent.sd_step1_lp.v1", "all_in_counts": provenance,
              "method": args.method, "certificate_tolerance": args.certificate_tolerance,
              "window_slack": WINDOW_SLACK, "conventions": {
                  "rules": "sdref/rules.py defaults (C1-C16); step1.py K1-K3",
                  "scipy_linprog": args.method},
              "results": results, "seconds": round(time.perf_counter() - started, 1)}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=1) + "\n", encoding="utf-8")
    log(f"report written to {args.report}")
    if "FAIL" in statuses:
        print("SD_STEP1_LP=FAIL")
        return 1
    if "INCONCLUSIVE" in statuses:
        print("SD_STEP1_LP=INCONCLUSIVE")
        return 4
    note = "" if provenance["independent"] else " (engine all-in table: not independent)"
    print(f"SD_STEP1_LP=PASS{note}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
