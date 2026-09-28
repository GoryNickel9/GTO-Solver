"""MonkerSolver's preflop charts played inside our game.

Reads the JSON of gtosd_preflop_blueprint_monker_values (the counterfactual
value of every action at every preflop node of each player, per combo, with
the opponent reach and chance inside and our average strategy everywhere
else) and, for each player, computes by exact recursion over its own preflop
nodes the EV of:

  (a) our preflop: the policy rows stored in the JSON (exact) and, with
      --ours, our chart files (rounded to three decimals by the export);
  (b) the chart set of --monker (one row per hand class; a class outside the
      chart's range at a node, an all-zero row, plays our row there: it has
      zero reach under the charts, otherwise it is reported as a fallback);
  (c) the best preflop response to the same values: per class (one action
      for every combo of the class, as a chart) and per combo.

Counterfactual values are additive over the hero's own later nodes: the value
of an action that leads to a later own node n' is its stored value minus
cfv(n' | ours) plus cfv(n' | replacement). With N = 630 hero combos,

  EV(x) = mean_h [ root(h) + sum over the first own nodes m of (V_x(m, h) - V_ours(m, h)) ].

Loss = EV(a) - EV(b) and Gain = EV(c) - EV(a) are reported in antes and in
per cent of the initial pot, per player, per node and for the hand classes
that lose most. The loss of a node and class is the performance-difference
term (1/N) pi_b(m, c) sum_{h in c} sum_a (a - b)(m, c, a) Q_a(m, a, h), with
pi_b the charts' own reach of the class at the node: these terms add up to
the loss exactly. The local loss is the same difference per hand of the
class that reaches the node (conditional class EVs), the measure of how bad
each chart decision is where it is taken.

Usage:
  python tools/monker_compare/monker_in_our_game.py values.json \
      --monker "<...>/HU/50a" [--ours out/monker/step2/HU50/charts/it_24000] \
      [--json report.json] [--top 8] [--check]
"""
from __future__ import annotations

import argparse
import json
import math
import os
import pathlib
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from compare_charts import read_chart  # noqa: E402

HANDS = 630
TOLERANCE = 1e-9


def native_path(text: str) -> pathlib.Path:
    # Git Bash paths such as /c/Users/... reach a Windows Python unconverted
    # when the script runs from a detached login shell.
    if os.name == "nt" and len(text) > 2 and text[0] == "/" and text[2] == "/":
        text = text[1] + ":" + text[2:]
    return pathlib.Path(text)


class Hero:
    """One player's preflop nodes with their values from the tool's JSON."""

    def __init__(self, data: dict, combo_labels: list[str]):
        self.position = data["position"]
        self.hero = data["hero"]
        self.ev_tool = data["ev_antes"]
        if "root_values" not in data:
            raise SystemExit("the values JSON was written with --no-combo-values")
        self.root = data["root_values"]
        self.top = data["top"]
        self.nodes = {node["chart"]: node for node in data["nodes"]}
        self.labels = combo_labels
        self.order = []
        queue = list(self.top)
        while queue:
            chart = queue.pop(0)
            self.order.append(chart)
            for children in self.nodes[chart]["next"].values():
                queue.extend(children)
        if sorted(self.order) != sorted(self.nodes):
            raise SystemExit(f"{self.position}: nodes outside the tree of own decisions")
        # Our policy rows, edge order.
        self.ours = {chart: {label: [node["strategy"][label][t] for t in node["tokens"]]
                             for label in node["strategy"]}
                     for chart, node in self.nodes.items()}
        # V_ours(m, h) from the stored action values.
        self.value_ours = {chart: self._mix(chart, self.ours[chart], self._stored(chart))
                           for chart in self.nodes}

    def _stored(self, chart: str) -> list[list[float]]:
        node = self.nodes[chart]
        return [node["combo_values"][token] for token in node["tokens"]]

    def _mix(self, chart, rows, action_values):
        return [sum(p * q[h] for p, q in zip(rows[self.labels[h]], action_values))
                for h in range(HANDS)]

    def action_values(self, chart: str, value_x: dict) -> list[list[float]]:
        """Q_x(m, a, h): stored value plus V_x - V_ours of the next own nodes."""
        node = self.nodes[chart]
        values = []
        for token, stored in zip(node["tokens"], self._stored(chart)):
            row = list(stored)
            for child in node["next"][token]:
                delta_x, delta_ours = value_x[child], self.value_ours[child]
                for h in range(HANDS):
                    row[h] += delta_x[h] - delta_ours[h]
            values.append(row)
        return values

    def evaluate(self, strategy: dict) -> tuple[float, dict, dict]:
        """EV of a preflop strategy {chart: {label: [freq]}}, its V and Q."""
        value, action = {}, {}
        for chart in reversed(self.order):
            action[chart] = self.action_values(chart, value)
            value[chart] = self._mix(chart, strategy[chart], action[chart])
        return self._root_ev(value), value, action

    def best_response(self, per_class: bool) -> tuple[float, dict]:
        value, choice = {}, {}
        for chart in reversed(self.order):
            q = self.action_values(chart, value)
            best = [0] * HANDS
            if per_class:
                sums: dict[str, list[float]] = {}
                for h in range(HANDS):
                    entry = sums.setdefault(self.labels[h], [0.0] * len(q))
                    for a in range(len(q)):
                        entry[a] += q[a][h]
                picks = {label: max(range(len(s)), key=lambda a, s=s: (s[a], -a))
                         for label, s in sums.items()}
                choice[chart] = {label: self.nodes[chart]["tokens"][a] for label, a in picks.items()}
                best = [picks[self.labels[h]] for h in range(HANDS)]
            else:
                best = [max(range(len(q)), key=lambda a, h=h: (q[a][h], -a)) for h in range(HANDS)]
            value[chart] = [q[best[h]][h] for h in range(HANDS)]
        return self._root_ev(value), choice

    def root_identity(self) -> tuple[float, float]:
        """Largest residual of root(h) = sum over the first own nodes of V_ours(m, h) + c * P(the
        hand ends before the hero acts | h), with one constant c fitted over the combos (the
        payoff of those endings, the opponent's fold at the root in heads-up). It ties the
        stored action values and our rows to the tool's root values, which come from a
        separate pass, so it is not circular."""
        residual = [self.root[h] - sum(self.value_ours[chart][h] for chart in self.top)
                    for h in range(HANDS)]
        ended = [1.0 - sum(self.nodes[chart]["opponent_reach"][h] for chart in self.top)
                 for h in range(HANDS)]
        norm = sum(e * e for e in ended)
        constant = sum(r * e for r, e in zip(residual, ended)) / norm if norm > 1e-12 else 0.0
        return max(abs(r - constant * e) for r, e in zip(residual, ended)), constant

    def _root_ev(self, value: dict) -> float:
        total = 0.0
        for h in range(HANDS):
            total += self.root[h]
            for chart in self.top:
                total += value[chart][h] - self.value_ours[chart][h]
        return total / HANDS

    def own_reach(self, strategy: dict) -> dict:
        reach = {chart: {label: 1.0 for label in strategy[chart]} for chart in self.top}
        for chart in self.order:
            node = self.nodes[chart]
            for a, token in enumerate(node["tokens"]):
                for child in node["next"][token]:
                    reach[child] = {label: reach[chart][label] * strategy[chart][label][a]
                                    for label in strategy[chart]}
        return reach


def chart_strategy(hero: Hero, directory: pathlib.Path) -> tuple[dict, dict, float]:
    """Chart rows in edge order; ours where the chart has no row for a class."""
    strategy, source = {}, {}
    rows_by_chart = {}
    for chart, node in hero.nodes.items():
        path = directory / chart
        if not path.exists():
            raise SystemExit(f"missing chart {path}")
        actions, rows = read_chart(path)
        if sorted(actions) != sorted(node["tokens"]):
            raise SystemExit(f"{path}: actions {actions} differ from the game {node['tokens']}")
        rows_by_chart[chart] = rows
    fallback = 0.0
    # Parents first, so the charts' own reach is known where a row is missing.
    reach: dict[str, dict[str, float]] = {chart: {} for chart in hero.nodes}
    for chart in hero.top:
        reach[chart] = {label: 1.0 for label in hero.ours[chart]}
    for chart in hero.order:
        node = hero.nodes[chart]
        strategy[chart], source[chart] = {}, {}
        for label in hero.ours[chart]:
            row = rows_by_chart[chart].get(label)
            if row is None:
                raise SystemExit(f"{directory / chart}: hand class {label} missing")
            total = sum(row.values())
            if total >= 0.5:
                strategy[chart][label] = [row[token] / total for token in node["tokens"]]
                source[chart][label] = "chart"
            else:
                strategy[chart][label] = list(hero.ours[chart][label])
                in_play = reach[chart][label] > 0.0
                source[chart][label] = "fallback" if in_play else "outside_range"
                if in_play:
                    fallback += combos_of(label) * reach[chart][label]
        for a, token in enumerate(node["tokens"]):
            for child in node["next"][token]:
                reach[child] = {label: reach[chart][label] * strategy[chart][label][a]
                                for label in strategy[chart]}
    return strategy, source, fallback


def combos_of(label: str) -> int:
    if len(label) == 2:
        return 6
    return 4 if label.endswith("s") else 12


def decompose(hero: Hero, reference: dict, alternative: dict, top: int) -> list[dict]:
    """Performance-difference terms of EV(reference) - EV(alternative)."""
    _, _, q_reference = hero.evaluate(reference)
    reach_alt = hero.own_reach(alternative)
    reach_ref = hero.own_reach(reference)
    nodes = []
    for chart in hero.order:
        node = hero.nodes[chart]
        per_class: dict[str, float] = {}
        for h in range(HANDS):
            label = hero.labels[h]
            local = sum((r - b) * q[h] for r, b, q in
                        zip(reference[chart][label], alternative[chart][label], q_reference[chart]))
            per_class[label] = per_class.get(label, 0.0) + reach_alt[chart][label] * local / HANDS
        classes = []
        for label, loss in per_class.items():
            ev = node["class_ev"][label]
            weights_ref = reference[chart][label]
            weights_alt = alternative[chart][label]
            class_ev = [ev[token]["ev"] for token in node["tokens"]]
            classes.append({
                "class": label,
                "loss_antes": loss,
                "reach_reference": reach_ref[chart][label],
                "reach_alternative": reach_alt[chart][label],
                # Per hand of the class that reaches the node (our values after it).
                "local_loss_antes": sum((r - b) * e for r, b, e in
                                        zip(weights_ref, weights_alt, class_ev)),
                "best_action": node["tokens"][max(range(len(class_ev)),
                                                  key=lambda a: (class_ev[a], -a))],
                "reference": dict(zip(node["tokens"], (round(v, 4) for v in weights_ref))),
                "alternative": dict(zip(node["tokens"], (round(v, 4) for v in weights_alt))),
            })
        classes.sort(key=lambda entry: -entry["loss_antes"])
        nodes.append({
            "chart": chart,
            "loss_antes": sum(per_class.values()),
            "reach_combos_reference": sum(combos_of(l) * r for l, r in reach_ref[chart].items()),
            "reach_combos_alternative": sum(combos_of(l) * r for l, r in reach_alt[chart].items()),
            "largest": classes[:top],
            "largest_negative": sorted(classes, key=lambda e: e["loss_antes"])[:max(1, top // 2)],
        })
    return nodes


def same_directory(left: str, right: pathlib.Path) -> bool:
    try:
        return os.path.samefile(native_path(left), right)
    except OSError:
        return False


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("values", help="JSON of gtosd_preflop_blueprint_monker_values")
    parser.add_argument("--monker", required=True, help="chart set to play (MonkerSolver format)")
    parser.add_argument("--ours", help="our chart files (rounded rows), optional")
    parser.add_argument("--json", help="write the report as JSON")
    parser.add_argument("--top", type=int, default=8, help="hand classes per node (default 8)")
    parser.add_argument("--check", action="store_true",
                        help="exit 1 unless our rows reproduce the tool's EV, the per-combo "
                             "response its gain_preflop and a chart set it valued its loss")
    args = parser.parse_args()

    data = json.loads(native_path(args.values).read_text(encoding="utf-8"))
    monker_dir = native_path(args.monker)
    ours_dir = native_path(args.ours) if args.ours else None
    pot = data["initial_pot_antes"]
    target = data["target_antes"]
    labels = data["combos"]["labels"]
    evaluation = data["evaluation"]
    tool_sets = [s for s in data["chart_sets"] if same_directory(s["directory"], monker_dir)]
    no_tool_set = not tool_sets

    def pct(antes: float) -> float:
        return 100.0 * antes / pot if pot else 0.0

    print(f"Charts inside our game: {args.values}")
    print(f"  evaluation {evaluation['mode']}, {evaluation['flops']} flops "
          f"({evaluation['physical_flops']} physical, {evaluation['boards']} boards), "
          f"policy {data['policy_fingerprint']}, abstraction {data['abstraction']}")
    print(f"  initial pot {pot:g} antes; target {data['target_pot_percent']:g} % = {target:g} antes")
    print(f"  played charts: {monker_dir}")
    failures = []
    report = {"values": args.values, "monker": str(monker_dir),
              "ours": str(ours_dir) if ours_dir else None, "initial_pot_antes": pot,
              "target_antes": target, "evaluation": evaluation, "players": []}
    for hero_data in data["heroes"]:
        hero = Hero(hero_data, labels)
        ev_rows, _, _ = hero.evaluate(hero.ours)
        monker, source, fallback = chart_strategy(hero, monker_dir)
        ev_monker, _, _ = hero.evaluate(monker)
        ev_class, choice = hero.best_response(per_class=True)
        ev_combo, _ = hero.best_response(per_class=False)
        tool_gain = data["estimate"]["gain_preflop_antes"][hero.hero]
        player = {"hero": hero.hero, "position": hero.position, "ev_tool_antes": hero.ev_tool,
                  "ev_ours_rows_antes": ev_rows, "ev_monker_antes": ev_monker,
                  "ev_best_response_class_antes": ev_class,
                  "ev_best_response_combo_antes": ev_combo,
                  "loss_antes": ev_rows - ev_monker, "loss_pot_percent": pct(ev_rows - ev_monker),
                  "gain_best_response_antes": ev_class - ev_rows,
                  "gain_best_response_pot_percent": pct(ev_class - ev_rows),
                  "gain_best_response_combo_antes": ev_combo - ev_rows,
                  "fallback_reach_combos": fallback,
                  "fallback_rows": sorted(f"{c}:{l}" for c, per in source.items()
                                          for l, s in per.items() if s == "fallback")}
        if abs(ev_rows - hero.ev_tool) > TOLERANCE:
            failures.append(f"{hero.position}: our rows give {ev_rows}, the tool {hero.ev_tool}")
        identity, ended_payoff = hero.root_identity()
        player["root_identity_residual_antes"] = identity
        player["ended_before_acting_payoff_antes"] = ended_payoff
        if identity > 1e-8:
            failures.append(f"{hero.position}: root values differ from the mixed action values "
                            f"by up to {identity}")
        if abs((ev_combo - ev_rows) - tool_gain) > TOLERANCE:
            failures.append(f"{hero.position}: per-combo response gain {ev_combo - ev_rows}, "
                            f"tool gain_preflop {tool_gain}")
        for tool_set in tool_sets:
            tool = next(p for p in tool_set["players"] if p["hero"] == hero.hero)
            player["tool_loss_antes"] = tool["loss_antes"]
            player["tool_loss_standard_error_antes"] = tool["loss_standard_error_antes"]
            if abs(tool["loss_antes"] - player["loss_antes"]) > TOLERANCE:
                failures.append(f"{hero.position}: loss {player['loss_antes']}, "
                                f"tool ({tool_set['name']}) {tool['loss_antes']}")
        print(f"\n{hero.position}: EV ours {ev_rows:+.5f} antes (tool {hero.ev_tool:+.5f})")
        if ours_dir is not None:
            rounded, rounded_source, rounded_fallback = chart_strategy(hero, ours_dir)
            ev_rounded, _, _ = hero.evaluate(rounded)
            # Rounding only if the files were exported from the same policy: every
            # in-range row within the three-decimal rounding of the export plus the
            # renormalisation of the rounded row (observed up to 1.1e-3 on HU50 step 2;
            # another policy differs by far more, 0.27 on the smoke run).
            row_gap = max((abs(a - b) for chart, per in rounded.items() for label, row in per.items()
                           if rounded_source[chart][label] == "chart"
                           for a, b in zip(row, hero.ours[chart][label])), default=0.0)
            same_policy = row_gap <= 2.5e-3
            key = "ours_charts_rounding_antes" if same_policy else "ours_charts_difference_antes"
            player["ev_ours_charts_antes"] = ev_rounded
            player[key] = ev_rounded - ev_rows
            player["ours_charts_row_gap"] = row_gap
            player["ours_charts_fallback_reach_combos"] = rounded_fallback
            what = "rounding" if same_policy else f"NOT the same policy, rows differ by up to {row_gap:.4f}"
            print(f"  our chart files {ev_rounded:+.5f} antes ({what}: {ev_rounded - ev_rows:+.6f}, "
                  f"fallback reach {rounded_fallback:.3f} combos)")
        se = player.get("tool_loss_standard_error_antes")
        se_text = f" +- {se:.5f}" if se else ""
        verdict = "within" if player["loss_antes"] <= target else "ABOVE"
        print(f"  charts played: EV {ev_monker:+.5f}, loss {player['loss_antes']:.5f}{se_text} antes "
              f"= {player['loss_pot_percent']:.3f} % of the pot ({verdict} the {target:g}-ante target)")
        print(f"  best preflop response: EV {ev_class:+.5f}, gain {ev_class - ev_rows:.5f} antes "
              f"= {pct(ev_class - ev_rows):.3f} % (per combo {ev_combo - ev_rows:.5f}, "
              f"tool {tool_gain:.5f})")
        if fallback > 0.0:
            print(f"  fallback: {fallback:.3f} combos reach nodes where the charts have no row "
                  f"(our row played): {', '.join(player['fallback_rows'][:10])}")
        nodes = decompose(hero, hero.ours, monker, args.top)
        decomposed = sum(node["loss_antes"] for node in nodes)
        if abs(decomposed - player["loss_antes"]) > TOLERANCE:
            failures.append(f"{hero.position}: node terms add up to {decomposed}, "
                            f"loss {player['loss_antes']}")
        for node in nodes:
            print(f"  {node['chart']}: loss {node['loss_antes']:+.5f} antes "
                  f"({pct(node['loss_antes']):+.3f} %), reach combos ours "
                  f"{node['reach_combos_reference']:.1f}, charts {node['reach_combos_alternative']:.1f}")
            for entry in node["largest"]:
                if entry["loss_antes"] <= 0.0:
                    break
                print(f"      {entry['class']:>4} {entry['loss_antes']:+.5f} "
                      f"(local {entry['local_loss_antes']:+.4f}/hand, best {entry['best_action']}; "
                      f"ours {entry['reference']} charts {entry['alternative']})")
            worst_negative = node["largest_negative"][0]
            if worst_negative["loss_antes"] < 0.0:
                print(f"      most negative: {worst_negative['class']} "
                      f"{worst_negative['loss_antes']:+.5f} (local "
                      f"{worst_negative['local_loss_antes']:+.4f}/hand)")
        player["nodes"] = nodes
        player["best_response_choices"] = choice
        report["players"].append(player)
    if args.json:
        pathlib.Path(args.json).write_text(json.dumps(report, indent=1), encoding="utf-8")
    if no_tool_set:
        # Without a matching chart set the tool's own loss cannot be cross-checked.
        failures.append(f"no chart set of the values JSON has the directory {monker_dir}")
    if failures:
        for failure in failures:
            print(f"CHECK FAILED: {failure}", file=sys.stderr)
        if args.check:
            return 1
    elif args.check:
        print("\nMONKER_IN_OUR_GAME=PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
