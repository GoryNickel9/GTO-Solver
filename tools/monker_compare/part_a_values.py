"""Part A of phase 3b (PHASE3_SPEC_2026-09-30 section 6): the report of a
gtosd_preflop_blueprint_policy_values JSON for 2 or 3 seats.

Per seat:
  (a) EV (pooled per class, the JSON's convention) with its standard error on
      a sampled pass, and the direct EV (the per-board estimator);
  (b) the rake identity: sum of the direct EVs = - expected rake (the rake
      computed independently at the terminals), gate 1e-9 antes where it is
      exact (the JSON's rake_identity_exact: heads-up, 3 seats in
      board_kernels mode, or an exact list); 3 seats in class_cache mode on a
      sampled list carry the cache's board-free preflop terminal values, so
      the identity holds there in expectation only: the residual is reported
      with its standard error and z, not gated;
  (c) the preflop-only best response per seat, recomputed here by the exact
      recursion of monker_in_our_game.py (per class and per combo) and
      cross-checked against the tool's own values, with the G3 gate (0.04
      antes per seat, 1 % of the 4-ante pot) and the tool's standard error;
  (d) MonkerSolver's charts (--monker DIR) played by each seat inside our
      game: the loss, its standard error, the fallback rows and the loss by
      node and hand class (the decompose_br inputs);
  (e) the lock-test metric when the policy is a lock-test run: the same
      per-seat preflop gains, labelled.

Usage:
  python tools/monker_compare/part_a_values.py values.json [--monker DIR]
      [--ours CHARTS_DIR] [--json report.json] [--top 8] [--gate-antes 0.04]
      [--label "run 1 final"] [--check]
"""
from __future__ import annotations

import argparse
import json
import math
import os
import pathlib
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from monker_in_our_game import Hero, chart_strategy, decompose, native_path, same_directory  # noqa: E402

TOLERANCE = 1e-9


def fmt(value, digits=5):
    if value is None:
        return "n/a"
    return f"{value:+.{digits}f}"


def pm(value, error, digits=5):
    text = fmt(value, digits)
    if error is not None:
        text += f" +- {error:.{digits}f}"
    return text


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("values", help="JSON of gtosd_preflop_blueprint_policy_values")
    parser.add_argument("--monker", help="MonkerSolver chart set to play inside our game")
    parser.add_argument("--ours", help="our chart files (rounded rows), optional")
    parser.add_argument("--json", help="write the report as JSON")
    parser.add_argument("--top", type=int, default=8, help="hand classes per node (default 8)")
    parser.add_argument("--gate-antes", type=float, default=0.04,
                        help="G3 gate on the per-seat preflop gain (default 0.04 a)")
    parser.add_argument("--label", default="", help="label of the run in the report")
    parser.add_argument("--check", action="store_true",
                        help="exit 1 unless every identity and cross-check holds")
    args = parser.parse_args()

    data = json.loads(native_path(args.values).read_text(encoding="utf-8"))
    if data.get("schema") != "gtosd.preflop_blueprint_policy_values.v1":
        raise SystemExit(f"{args.values}: not a policy_values JSON ({data.get('schema')})")
    pot = data["initial_pot_antes"]
    labels = data["combos"]["labels"]
    evaluation = data["evaluation"]
    estimate = data["estimate"]
    sampled = evaluation["mode"] == "sampled"
    players = data["players"]
    monker_dir = native_path(args.monker) if args.monker else None
    ours_dir = native_path(args.ours) if args.ours else None
    tool_sets = ([s for s in data["chart_sets"] if same_directory(s["directory"], monker_dir)]
                 if monker_dir else [])

    def pct(antes):
        return 100.0 * antes / pot if pot else 0.0

    failures = []
    print(f"Part A: {args.values}" + (f" ({args.label})" if args.label else ""))
    print(f"  {players} seats {data['positions']}, evaluation {evaluation['mode']} "
          f"({evaluation['board_source']}), chunks {evaluation['chunks']}, boards "
          f"{evaluation['boards']}, seed {evaluation['seed']}, convention "
          f"{evaluation['convention']}, preflop terminals {evaluation['preflop_terminals']}, "
          f"folded cards {evaluation['folded_cards']}, {evaluation['seconds']:.0f} s")
    print(f"  policy {data['policy_fingerprint']}, abstraction {data['abstraction']}, "
          f"initial pot {pot:g} antes, G3 gate {args.gate_antes:g} antes per seat "
          f"({pct(args.gate_antes):.2f} % of the pot)")
    if data.get("checks_failed"):
        for failure in data["checks_failed"]:
            failures.append(f"tool self-check: {failure}")

    # (b) the rake identity.
    rake = estimate["expected_rake_antes"]
    residual = estimate["rake_identity_residual_antes"]
    # JSON files written before 02/10 have no rake_identity_exact: gated.
    identity_exact = estimate.get("rake_identity_exact", True)
    identity_se = estimate.get("rake_identity_standard_error_antes")
    if identity_exact:
        identity_note = "(gate 1e-9)"
    elif identity_se:
        identity_note = (f"(class_cache, sampled: in expectation only, se {identity_se:.2e}, "
                         f"z {residual / identity_se:+.2f}; not gated)")
    else:
        identity_note = "(class_cache, not an exact list: in expectation only; not gated)"
    print(f"\nRake identity: expected rake {rake:.6f} antes"
          + (f" ({data['rake']['basis_points'] / 100:.2f} %, cap {data['rake']['cap_antes']:g} a)"
             if "rake" in data else " (no rake)")
          + f"; sum of the direct EVs {estimate['ev_direct_sum_antes']:+.6f}, "
          f"residual {residual:+.2e} {identity_note}"
          + f"; pooled EV sum {estimate['ev_sum_antes']:+.6f}")
    if identity_exact and abs(residual) > TOLERANCE:
        failures.append(f"rake identity residual {residual}")

    report = {"values": args.values, "label": args.label, "monker": str(monker_dir) if monker_dir else None,
              "initial_pot_antes": pot, "gate_antes": args.gate_antes, "evaluation": evaluation,
              "expected_rake_antes": rake, "rake_identity_residual_antes": residual,
              "rake_identity_exact": identity_exact,
              "rake_identity_standard_error_antes": identity_se,
              "players": []}
    print("\nPer seat (antes; % = of the initial pot):")
    header = f"  {'seat':<5} {'EV':>22} {'direct EV':>22} {'BR gain/class':>22} {'BR gain/combo':>14} {'%':>7} {'gate':>5}"
    print(header)
    worst_gain = 0.0
    for index, hero_data in enumerate(data["heroes"]):
        hero = Hero(hero_data, labels)
        ev_rows, _, _ = hero.evaluate(hero.ours)
        ev_class, choice = hero.best_response(per_class=True)
        ev_combo, _ = hero.best_response(per_class=False)
        gain_class = ev_class - ev_rows
        gain_combo = ev_combo - ev_rows
        tool_gain_class = estimate["gain_preflop_class_antes"][index]
        tool_gain_combo = estimate["gain_preflop_antes"][index]
        gain_se = estimate["gain_preflop_standard_error_antes"][index] if sampled else None
        ev_se = estimate["ev_standard_error_antes"][index] if sampled else None
        ev_direct_se = estimate["ev_direct_standard_error_antes"][index] if sampled else None
        identity, ended_payoff = hero.root_identity()
        if abs(ev_rows - hero.ev_tool) > TOLERANCE:
            failures.append(f"{hero.position}: our rows give {ev_rows}, the tool {hero.ev_tool}")
        if abs(gain_class - tool_gain_class) > TOLERANCE:
            failures.append(f"{hero.position}: per-class gain {gain_class}, tool {tool_gain_class}")
        if abs(gain_combo - tool_gain_combo) > TOLERANCE:
            failures.append(f"{hero.position}: per-combo gain {gain_combo}, tool {tool_gain_combo}")
        if identity > 1e-8:
            failures.append(f"{hero.position}: root identity residual {identity}")
        passes = gain_class <= args.gate_antes
        worst_gain = max(worst_gain, gain_class)
        print(f"  {hero.position:<5} {pm(ev_rows, ev_se):>22} "
              f"{pm(estimate['ev_direct_antes'][index], ev_direct_se):>22} "
              f"{pm(gain_class, gain_se):>22} {gain_combo:>14.5f} {pct(gain_class):>7.3f} "
              f"{'PASS' if passes else 'FAIL':>5}")
        player = {"hero": index, "position": hero.position, "ev_antes": ev_rows,
                  "ev_standard_error_antes": ev_se,
                  "ev_direct_antes": estimate["ev_direct_antes"][index],
                  "ev_direct_standard_error_antes": ev_direct_se,
                  "gain_best_response_class_antes": gain_class,
                  "gain_best_response_combo_antes": gain_combo,
                  "gain_standard_error_antes": gain_se,
                  "gain_pot_percent": pct(gain_class), "passes_gate": passes,
                  "root_identity_residual_antes": identity,
                  "ended_before_acting_payoff_antes": ended_payoff,
                  "ended_before_acting_payoff_tool_antes": hero_data.get("ended_before_acting_payoff_antes"),
                  "best_response_choices": choice}
        if monker_dir is not None:
            monker, source, fallback = chart_strategy(hero, monker_dir)
            ev_monker, _, _ = hero.evaluate(monker)
            loss = ev_rows - ev_monker
            player.update({"ev_monker_antes": ev_monker, "loss_antes": loss,
                           "loss_pot_percent": pct(loss), "fallback_reach_combos": fallback,
                           "fallback_rows": sorted(f"{c}:{l}" for c, per in source.items()
                                                   for l, s in per.items() if s == "fallback")})
            for tool_set in tool_sets:
                tool = next(p for p in tool_set["players"] if p["hero"] == index)
                player["tool_loss_antes"] = tool["loss_antes"]
                player["loss_standard_error_antes"] = tool["loss_standard_error_antes"]
                if abs(tool["loss_antes"] - loss) > TOLERANCE:
                    failures.append(f"{hero.position}: chart loss {loss}, tool {tool['loss_antes']}")
            nodes = decompose(hero, hero.ours, monker, args.top)
            decomposed = sum(node["loss_antes"] for node in nodes)
            if abs(decomposed - loss) > TOLERANCE:
                failures.append(f"{hero.position}: node terms add up to {decomposed}, loss {loss}")
            player["nodes"] = nodes
        if ours_dir is not None:
            rounded, _, rounded_fallback = chart_strategy(hero, ours_dir)
            ev_rounded, _, _ = hero.evaluate(rounded)
            player["ev_ours_charts_antes"] = ev_rounded
            player["ours_charts_rounding_antes"] = ev_rounded - ev_rows
            player["ours_charts_fallback_reach_combos"] = rounded_fallback
        report["players"].append(player)
    report["max_gain_antes"] = worst_gain
    report["passes_gate"] = worst_gain <= args.gate_antes
    print(f"  largest per-seat preflop gain {worst_gain:.5f} antes = {pct(worst_gain):.3f} % "
          f"({'within' if worst_gain <= args.gate_antes else 'ABOVE'} the gate)"
          + ("; sampled: a maximum over noisy values, an upper estimate" if sampled else ""))

    if monker_dir is not None:
        print(f"\nMonkerSolver charts played inside our game ({monker_dir}):")
        for player in report["players"]:
            se = player.get("loss_standard_error_antes")
            print(f"  {player['position']:<5} EV {player['ev_monker_antes']:+.5f}, loss "
                  f"{pm(player['loss_antes'], se)} antes = {player['loss_pot_percent']:.3f} %"
                  f", fallback {player['fallback_reach_combos']:.3f} combos")
            for node in player.get("nodes", []):
                if abs(node["loss_antes"]) < 1e-6:
                    continue
                print(f"      {node['chart']}: {node['loss_antes']:+.5f} antes "
                      f"(reach combos ours {node['reach_combos_reference']:.1f}, charts "
                      f"{node['reach_combos_alternative']:.1f})")
                for entry in node["largest"][:3]:
                    if entry["loss_antes"] <= 0.0:
                        break
                    print(f"          {entry['class']:>4} {entry['loss_antes']:+.5f} "
                          f"(local {entry['local_loss_antes']:+.4f}/hand, best {entry['best_action']})")
            if player.get("fallback_rows"):
                print(f"      fallback rows: {', '.join(player['fallback_rows'][:10])}")
        if not tool_sets:
            failures.append(f"no chart set of the values JSON has the directory {monker_dir} "
                            "(the tool's loss standard errors are then missing)")

    if args.json:
        pathlib.Path(args.json).write_text(json.dumps(report, indent=1), encoding="utf-8")
    if failures:
        for failure in failures:
            print(f"CHECK FAILED: {failure}", file=sys.stderr)
        if args.check:
            return 1
    elif args.check:
        print("\nPART_A_VALUES=PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
