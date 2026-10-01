"""Convergence curve of a step-2 run from its policy snapshots.

A run of tools/monker_compare/run_step2_continuous.sh with POLICY_SNAPSHOTS=1 (trainer
--policy-snapshots) keeps, next to the charts of every snapshot, the average policy of
that iteration: <run>/charts/it_<N>/policy.bin. For every snapshot with a policy this
script runs gtosd_preflop_blueprint_monker_values on it with the run's game (same
config, bucket tables, board class rows and texture map, --all-flops: the exact
physical best response over the 573 canonical flops) and the snapshot's own charts as
the required chart set (plus MonkerSolver's charts with --monker: their loss inside
our game at that iteration), then reads the distance and the pooled range difference
from MonkerSolver of the same snapshot (it_<N>/vs_monker.json written by the runner,
or compare_charts.py with --monker when it is missing).

Output: a table iteration -> training seconds (of the trainer process that wrote the
snapshot: they restart at a resume), gain of each player (best response
minus EV, antes), lower gain, preflop-only gain, NashConv (antes and per cent of the
initial pot), distance and range difference from MonkerSolver, in <run>/convergence.txt,
and the same rows with the fingerprints in <run>/convergence.json (--out-prefix
changes the two names). The evaluation of a snapshot is kept in it_<N>/values.json
(it_<N>/values.log): a rerun evaluates only the new snapshots (--force: all), and a
snapshot whose policy.bin was deleted after its evaluation (to free disk space) keeps
its cached row.

Usage (paths relative to the repository root, as for the runner):
  python tools/monker_compare/convergence_curve.py <run dir> [--config <config.json>]
      [--buckets <bucket dir>] [--texture-map <map> | --no-texture-map]
      [--monker <MonkerSolver chart dir>] [--threads 8] [--flop-limit K]
Config, buckets and texture map default to the first start line of <run>/run.log
(written by the runner; the texture map from --board-texture-map in TRAIN_ARGS); the
evaluator rejects a policy trained with other rows or another map. Environment: BIN
(executables, default out/build/windows-release-suite/benchmarks), RES (resources,
default out/preflop_blueprint_resources), THREADS (8). --flop-limit K evaluates only
the first K canonical flops (partial pass, for smoke tests).
"""
from __future__ import annotations

import argparse
import json
import os
import pathlib
import re
import shlex
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from compare_charts import compare, native_path  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
SNAPSHOT = re.compile(r"^it_(\d+)$")


def run_settings(run: pathlib.Path) -> dict:
    """Config, buckets and texture map of the first start line of the runner's log."""
    log = run / "run.log"
    if not log.exists():
        return {}
    for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
        match = re.search(r"step 2 continuous start: config (\S+) buckets (\S+) step \S+ max \S+ "
                          r"threshold \S+ ?(.*)$", line)
        if not match:
            continue
        settings = {"config": match.group(1), "buckets": match.group(2)}
        tokens = shlex.split(match.group(3))
        for index, token in enumerate(tokens[:-1]):
            if token == "--board-texture-map":
                settings["texture_map"] = tokens[index + 1]
        return settings
    return {}


def training_seconds(run: pathlib.Path) -> dict[int, float]:
    """Training seconds of every chart snapshot (charts events of train.jsonl)."""
    seconds: dict[int, float] = {}
    path = run / "train.jsonl"
    if not path.exists():
        return seconds
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if '"event":"charts"' not in line:
            continue
        try:
            event = json.loads(line)
        except json.JSONDecodeError:
            continue
        seconds[int(event["iteration"])] = float(event["training_seconds"])
    return seconds


def executable(directory: pathlib.Path, name: str) -> pathlib.Path:
    return directory / (name + (".exe" if os.name == "nt" else ""))


def usable(values: pathlib.Path, mode: str, flops: int | None, monker: bool) -> bool:
    """An earlier evaluation of the snapshot with the same pass and chart sets and passed
    self-checks."""
    if not values.exists():
        return False
    try:
        data = json.loads(values.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return False
    evaluation = data.get("evaluation", {})
    has_monker = any(s.get("name") == "monker" for s in data.get("chart_sets", []))
    return (evaluation.get("mode") == mode and (flops is None or evaluation.get("flops") == flops)
            and (has_monker or not monker)
            and data.get("self_checks", {}).get("passed", False))


def fmt(value, digits: int = 5) -> str:
    return "na" if value is None else f"{value:.{digits}f}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("run", type=native_path, help="output directory of the runner")
    parser.add_argument("--config", type=native_path)
    parser.add_argument("--buckets", type=native_path)
    parser.add_argument("--texture-map", type=native_path)
    parser.add_argument("--no-texture-map", action="store_true",
                        help="identity texture even when run.log names a map")
    parser.add_argument("--monker", type=native_path, help="MonkerSolver chart directory")
    parser.add_argument("--bin", type=native_path,
                        default=native_path(os.environ.get("BIN",
                                                           "out/build/windows-release-suite/benchmarks")))
    parser.add_argument("--res", type=native_path,
                        default=native_path(os.environ.get("RES", "out/preflop_blueprint_resources")))
    parser.add_argument("--threads", type=int, default=int(os.environ.get("THREADS", "8")))
    parser.add_argument("--flop-limit", type=int, default=0,
                        help="first K canonical flops only (partial pass, smoke tests)")
    parser.add_argument("--force", action="store_true", help="re-evaluate every snapshot")
    parser.add_argument("--out-prefix", default="convergence")
    args = parser.parse_args()
    os.chdir(ROOT)

    run = args.run
    settings = run_settings(run)
    config = args.config or (native_path(settings["config"]) if "config" in settings else None)
    buckets = args.buckets or (native_path(settings["buckets"]) if "buckets" in settings else None)
    texture = None if args.no_texture_map else (
        args.texture_map or (native_path(settings["texture_map"]) if "texture_map" in settings
                             else None))
    if config is None or buckets is None:
        raise SystemExit("--config and --buckets are needed (no start line in run.log)")
    values_exe = executable(args.bin, "gtosd_preflop_blueprint_monker_values")
    if not values_exe.exists():
        raise SystemExit(f"{values_exe} not found (set BIN or --bin)")
    mode = "partial" if args.flop_limit > 0 else "exact"
    print(f"run {run}: config {config}, buckets {buckets}, texture map "
          f"{texture if texture else 'identity'}, pass {mode}"
          + (f" ({args.flop_limit} flops)" if args.flop_limit else "") + f", bin {args.bin}")

    charts_dir = run / "charts"
    snapshots = sorted((int(m.group(1)), p) for p in (charts_dir.iterdir() if charts_dir.exists()
                                                      else [])
                       if p.is_dir() and (m := SNAPSHOT.match(p.name)))
    seconds = training_seconds(run)
    rows = []
    failures = []
    positions = None
    initial_pot = None
    for iteration, snapshot in snapshots:
        policy = snapshot / "policy.bin"
        values = snapshot / "values.json"
        cached = usable(values, mode, args.flop_limit or None, bool(args.monker))
        if not policy.exists() and (args.force or not cached):
            # No --policy-snapshots, an iteration off --policy-snapshot-every, a policy
            # skipped for disk space, or one deleted before it was evaluated.
            print(f"it_{iteration}: no policy.bin and no cached evaluation")
            continue
        evaluated = False
        started = time.time()
        if args.force or not cached:
            command = [str(values_exe), "--config", str(config), "--resources-dir", str(args.res),
                       "--buckets-dir", str(buckets), "--board-class-rows", "--policy", str(policy),
                       "--charts", f"ours={snapshot}", "--all-flops", "--threads", str(args.threads),
                       "--no-combo-values", "--out", str(values)]
            if texture:
                command += ["--board-texture-map", str(texture)]
            if args.monker:
                command += ["--charts", f"monker={args.monker}"]
            if args.flop_limit:
                command += ["--flop-limit", str(args.flop_limit)]
            with open(snapshot / "values.log", "w", encoding="utf-8") as log:
                result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=False)
            evaluated = True
            if result.returncode != 0 or not values.exists():
                # A JSON written before failed self-checks stays for inspection; it is
                # not cached (usable() requires passed self-checks).
                tail = (snapshot / "values.log").read_text(encoding="utf-8",
                                                          errors="replace").strip().splitlines()[-3:]
                failures.append({"iteration": iteration, "exit": result.returncode, "log": tail})
                print(f"it_{iteration}: monker_values failed (exit {result.returncode}): "
                      + " | ".join(tail))
                continue
        data = json.loads(values.read_text(encoding="utf-8"))
        estimate = data["estimate"]
        positions = [hero["position"] for hero in data["heroes"]]
        initial_pot = data["initial_pot_antes"]
        # Distance and pooled range difference from MonkerSolver: the runner's report.
        report_path = snapshot / "vs_monker.json"
        report = None
        if report_path.exists():
            report = json.loads(report_path.read_text(encoding="utf-8"))
        elif args.monker:
            report = compare(snapshot, args.monker, 5)
            report_path.write_text(json.dumps(report, indent=1), encoding="utf-8")
        monker_loss = None
        for chart_set in data.get("chart_sets", []):
            if chart_set["name"] == "monker":
                monker_loss = [player["loss_antes"] for player in chart_set["players"]]
        nashconv = estimate["nashconv_antes"]
        row = {
            "iteration": iteration,
            "training_seconds": seconds.get(iteration),
            "mode": data["evaluation"]["mode"],
            "flops": data["evaluation"]["flops"],
            "policy_fingerprint": data["policy_fingerprint"],
            "policy_source": data["policy_source"],
            "ev_antes": estimate["ev_antes"],
            "gain_antes": estimate["gain_antes"],
            "gain_lower_antes": estimate["gain_lower_antes"],
            "gain_preflop_antes": estimate["gain_preflop_antes"],
            "max_gain_antes": estimate["max_gain_antes"],
            "nashconv_antes": nashconv,
            "nashconv_pot_percent": 100.0 * nashconv / initial_pot if initial_pot else None,
            "distance": report.get("overall_mean_distance") if report else None,
            "range_difference": report.get("overall_range_difference") if report else None,
            "monker_loss_antes": monker_loss,
            "values_json": values.as_posix(),
            "evaluation_seconds": round(time.time() - started, 1) if evaluated else None,
        }
        # With rake the game is not zero-sum: the values JSON carries the expected rake
        # (exact pass, or --expected-rake per player); absent without rake.
        for key in ("ev_sum_antes", "expected_rake_antes", "expected_rake_by_hero_antes"):
            if key in estimate:
                row[key] = estimate[key]
        rows.append(row)
        print(f"it_{iteration}: gain {' / '.join(fmt(v) for v in row['gain_antes'])} a, "
              f"nashconv {fmt(nashconv)} a ({fmt(row['nashconv_pot_percent'], 3)} % pot), distance "
              f"{fmt(row['distance'], 4)}, range difference {fmt(row['range_difference'], 4)}"
              + (f" (evaluated in {row['evaluation_seconds']} s)" if evaluated else " (cached)"))

    # One column per player of the evaluated game (len(positions); 2 heads-up, 3 for the 3-way).
    names = positions or [f"P{seat}" for seat in range(len(rows[0]["gain_antes"]) if rows else 2)]
    header = (["iteration", "train_s"] + [f"gain_{p}" for p in names]
              + [f"gain_lower_{p}" for p in names] + [f"gain_pre_{p}" for p in names]
              + ["nashconv_a", "nashconv_%pot", "distance", "range_diff"]
              + ([f"monker_loss_{p}" for p in names]
                 if any(r["monker_loss_antes"] for r in rows) else []))
    lines = ["\t".join(header)]
    for row in rows:
        cells = [str(row["iteration"]),
                 "na" if row["training_seconds"] is None else f"{row['training_seconds']:.0f}"]
        cells += [fmt(v) for v in row["gain_antes"]]
        cells += [fmt(v) for v in row["gain_lower_antes"]]
        cells += [fmt(v) for v in row["gain_preflop_antes"]]
        cells += [fmt(row["nashconv_antes"]), fmt(row["nashconv_pot_percent"], 3),
                  fmt(row["distance"], 4), fmt(row["range_difference"], 4)]
        if len(header) > len(cells):
            cells += ([fmt(v) for v in row["monker_loss_antes"]] if row["monker_loss_antes"]
                      else ["na"] * len(names))
        lines.append("\t".join(cells))
    table = "\n".join(lines) + "\n"
    summary = {
        "schema": "gtosd.preflop_blueprint_convergence_curve.v1",
        "run": run.as_posix(),
        "config": config.as_posix(),
        "buckets": buckets.as_posix(),
        "texture_map": texture.as_posix() if texture else None,
        "monker": args.monker.as_posix() if args.monker else None,
        "pass": mode,
        "flop_limit": args.flop_limit or None,
        "positions": names,
        "initial_pot_antes": initial_pot,
        "value_scope": "antes per hand; gain = best response minus EV of each player in the "
                       "physical game with the lifted policy; gain_preflop = best preflop response "
                       "only; nashconv = sum of the gains; distance and range difference from "
                       "MonkerSolver by compare_charts.py",
        "rows": rows,
        "failures": failures,
    }
    (run / f"{args.out_prefix}.txt").write_text(table, encoding="utf-8")
    (run / f"{args.out_prefix}.json").write_text(json.dumps(summary, indent=1), encoding="utf-8")
    sys.stdout.write(table)
    print(f"{len(rows)} snapshots, {len(failures)} failures: {run / (args.out_prefix + '.txt')}, "
          f"{run / (args.out_prefix + '.json')}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
