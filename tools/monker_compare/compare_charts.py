"""Compare two directories of preflop charts in the MonkerSolver text format.

Each chart file is tab separated: `Combination`, one column per action, `Total`,
and one row per hand class (81 classes in short deck). Files are matched by
their path relative to each directory (for example `CO/CO_strategy.txt`).

For every chart present on both sides the script reports, over the hand classes
weighted by their number of combos (pairs 6, suited 4, offsuit 12):
- the mean total-variation distance of the action frequencies (half the L1
  distance: 0 means identical strategies, 1 means no action in common);
- the share of combos whose most frequent action is the same on both sides (JSON
  only: it hides mixed strategies, so the text output leaves it out);
- the hand classes with the largest distances.
An action that exists on one side only counts as frequency 0 on the other.

These two measures say what each hand does if it reaches the node, not which
hands reach it. For that, every chart also reports its range on both sides: the
reach of a class is the product of the frequencies of the earlier actions of the
same player on the path (read from the parent charts, when present), the range
size is the sum of combos times reach, and the range difference is one minus the
ratio between the common part and the union of the two weighted ranges (0 = the
same hands with the same frequencies, 1 = no hand in common). The overall range
difference pools effective combos instead of averaging the charts: the common part
and the union are summed over the distinct restricted ranges (two charts reached by
the same earlier actions of the same player share one range and count once), then
one minus their ratio.

The mean distance is also reported over two groups of charts (JSON
all_in_mean_distance / non_all_in_mean_distance with their chart counts, and on
the overall text line): a chart faces an all-in when its actions, on both sides
together, are exactly Call and Fold. In the 3-way 50a tree 36 of the 54 charts
face an all-in and depend only on equity, rake and ranges; the step-2 stop rule
watches the 18 others (phase 3 spec, section 5.3).
"""
from __future__ import annotations

import argparse
import json
import os
import pathlib
import sys


def combos(label: str) -> int:
    if len(label) == 2:
        return 6
    return 4 if label.endswith("s") else 12


def read_chart(path: pathlib.Path) -> tuple[list[str], dict[str, dict[str, float]]]:
    lines = [line for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]
    header = lines[0].split("\t")
    actions = [name for name in header[1:] if name != "Total"]
    rows: dict[str, dict[str, float]] = {}
    for line in lines[1:]:
        parts = line.split("\t")
        rows[parts[0]] = {action: float(value) for action, value in zip(actions, parts[1:])}
    return actions, rows


ALL_IN_FACING_ACTIONS = frozenset({"Call", "Fold"})


def facing_all_in(actions_ours: list[str], actions_theirs: list[str]) -> bool:
    """A chart that faces an all-in: its actions, on both sides together, are exactly
    Call and Fold."""
    return set(actions_ours) | set(actions_theirs) == ALL_IN_FACING_ACTIONS


def main_action(frequencies: dict[str, float]) -> str:
    return max(sorted(frequencies), key=lambda action: frequencies[action])


def parent_decisions(relative: str) -> list[tuple[str, str]]:
    """Earlier decisions of the acting player on the path to a chart.

    "BTN/CO_Call_BTN_5.0ante_CO_AllIn_BTN_strategy.txt" is the line CO Call, BTN
    5.0ante, CO AllIn with BTN to act: the reach of BTN is its 5.0ante frequency
    in BTN/CO_Call_BTN_strategy.txt.
    """
    actor, name = relative.split("/")
    tokens = name[: -len("_strategy.txt")].split("_")
    pairs = list(zip(tokens[0:-1:2], tokens[1:-1:2]))
    parents = []
    for index, (player, action) in enumerate(pairs):
        if player == actor:
            prefix = [token for pair in pairs[:index] for token in pair]
            parents.append((f"{player}/{'_'.join(prefix + [player])}_strategy.txt", action))
    return parents


def reach(directory: pathlib.Path, relative: str, cache: dict) -> dict[str, float] | None:
    """Reach of every class at a chart, or None when a parent chart is missing."""
    def rows(chart: str):
        if chart not in cache:
            path = directory / chart
            cache[chart] = read_chart(path)[1] if path.exists() else None
        return cache[chart]
    node_rows = rows(relative)
    if node_rows is None:
        return None
    result = {label: (1.0 if sum(f.values()) >= 0.5 else 0.0) for label, f in node_rows.items()}
    for parent, action in parent_decisions(relative):
        parent_rows = rows(parent)
        if parent_rows is None:
            return None
        for label in result:
            f = parent_rows.get(label, {})
            total = sum(f.values())
            result[label] *= f.get(action, 0.0) / total if total >= 0.5 else 0.0
    return result


def range_report(ours: pathlib.Path, theirs: pathlib.Path, relative: str,
                 caches: tuple[dict, dict]) -> dict:
    ours_reach = reach(ours, relative, caches[0])
    theirs_reach = reach(theirs, relative, caches[1])
    if ours_reach is None or theirs_reach is None:
        return {"range_combos_ours": None, "range_combos_theirs": None, "range_difference": None,
                "restricted_range": bool(parent_decisions(relative))}
    labels = set(ours_reach) | set(theirs_reach)
    size_ours = sum(combos(l) * ours_reach.get(l, 0.0) for l in labels)
    size_theirs = sum(combos(l) * theirs_reach.get(l, 0.0) for l in labels)
    common = sum(combos(l) * min(ours_reach.get(l, 0.0), theirs_reach.get(l, 0.0)) for l in labels)
    union = sum(combos(l) * max(ours_reach.get(l, 0.0), theirs_reach.get(l, 0.0)) for l in labels)
    return {"range_combos_ours": round(size_ours, 2), "range_combos_theirs": round(size_theirs, 2),
            "range_difference": round(1.0 - common / union, 4) if union > 0 else None,
            "restricted_range": bool(parent_decisions(relative)),
            "range_common_combos": common, "range_union_combos": union}


def compare(ours: pathlib.Path, theirs: pathlib.Path, top: int) -> dict:
    ours_files = {p.relative_to(ours).as_posix() for p in ours.rglob("*_strategy.txt")}
    theirs_files = {p.relative_to(theirs).as_posix() for p in theirs.rglob("*_strategy.txt")}
    report = {"charts": [], "only_ours": sorted(ours_files - theirs_files),
              "only_theirs": sorted(theirs_files - ours_files)}
    caches: tuple[dict, dict] = ({}, {})
    for relative in sorted(ours_files & theirs_files):
        our_actions, our_rows = read_chart(ours / relative)
        their_actions, their_rows = read_chart(theirs / relative)
        actions = sorted(set(our_actions) | set(their_actions))
        weight_sum = 0.0
        distance_sum = 0.0
        same_sum = 0.0
        per_class = []
        outside_range = []
        only_ours = []
        only_theirs = []
        for label in sorted(set(our_rows) & set(their_rows)):
            ours_f = {a: our_rows[label].get(a, 0.0) for a in actions}
            theirs_f = {a: their_rows[label].get(a, 0.0) for a in actions}
            # An all-zero row is a class outside the acting range at the node
            # (it never gets there): it has no strategy to compare.
            ours_in = sum(ours_f.values()) >= 0.5
            theirs_in = sum(theirs_f.values()) >= 0.5
            if not ours_in or not theirs_in:
                if ours_in:
                    only_ours.append(label)
                elif theirs_in:
                    only_theirs.append(label)
                else:
                    outside_range.append(label)
                continue
            distance = 0.5 * sum(abs(ours_f[a] - theirs_f[a]) for a in actions)
            weight = combos(label)
            weight_sum += weight
            distance_sum += weight * distance
            same = main_action(ours_f) == main_action(theirs_f)
            same_sum += weight if same else 0.0
            per_class.append({"class": label, "distance": round(distance, 4),
                              "ours": {a: round(v, 3) for a, v in ours_f.items()},
                              "theirs": {a: round(v, 3) for a, v in theirs_f.items()}})
        per_class.sort(key=lambda entry: -entry["distance"])
        report["charts"].append({
            "chart": relative,
            "actions_ours": our_actions,
            "actions_theirs": their_actions,
            "facing_all_in": facing_all_in(our_actions, their_actions),
            "classes": len(per_class),
            "outside_both_ranges": outside_range,
            "in_range_only_ours": only_ours,
            "in_range_only_theirs": only_theirs,
            "mean_distance": round(distance_sum / weight_sum, 4) if weight_sum else None,
            "same_main_action_share": round(same_sum / weight_sum, 4) if weight_sum else None,
            "largest": per_class[:top],
            **range_report(ours, theirs, relative, caches),
        })
    distances = [c["mean_distance"] for c in report["charts"] if c["mean_distance"] is not None]
    shares = [c["same_main_action_share"] for c in report["charts"]
              if c["same_main_action_share"] is not None]
    report["overall_mean_distance"] = round(sum(distances) / len(distances), 4) if distances else None
    # The same mean over the charts facing an all-in and over the others.
    for group, facing in (("all_in", True), ("non_all_in", False)):
        values = [c["mean_distance"] for c in report["charts"]
                  if c["mean_distance"] is not None and c["facing_all_in"] == facing]
        report[f"{group}_mean_distance"] = round(sum(values) / len(values), 4) if values else None
        report[f"{group}_charts"] = len(values)
    report["overall_same_main_action_share"] = round(sum(shares) / len(shares), 4) if shares else None
    # Pooled on effective combos over the distinct restricted ranges (elsewhere every
    # class reaches the node on both sides and the difference is 0).
    seen = set()
    common = union = size_ours = size_theirs = 0.0
    for c in report["charts"]:
        if not c.get("restricted_range") or c["range_difference"] is None:
            continue
        key = (c["chart"].split("/")[0], tuple(parent_decisions(c["chart"])))
        if key in seen:
            continue
        seen.add(key)
        common += c["range_common_combos"]
        union += c["range_union_combos"]
        size_ours += c["range_combos_ours"]
        size_theirs += c["range_combos_theirs"]
    report["overall_range_difference"] = round(1.0 - common / union, 4) if union > 0 else None
    report["overall_range_combos_ours"] = round(size_ours, 2) if seen else None
    report["overall_range_combos_theirs"] = round(size_theirs, 2) if seen else None
    report["distinct_restricted_ranges"] = len(seen)
    return report


def native_path(text: str) -> pathlib.Path:
    # Git Bash paths such as /c/Users/... reach a Windows Python unconverted
    # when the script runs from a detached login shell.
    if os.name == "nt" and len(text) > 2 and text[0] == "/" and text[2] == "/" and text[1].isalpha():
        text = text[1].upper() + ":" + text[2:]
    return pathlib.Path(text)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("ours", type=native_path)
    parser.add_argument("theirs", type=native_path)
    parser.add_argument("--top", type=int, default=5)
    parser.add_argument("--json", type=native_path)
    args = parser.parse_args()
    report = compare(args.ours, args.theirs, args.top)
    for chart in report["charts"]:
        flag = "" if chart["actions_ours"] == chart["actions_theirs"] else "  [actions differ]"
        if chart["mean_distance"] is None:
            print(f"{chart['chart']}: no class in both ranges; in range only ours "
                  f"{len(chart['in_range_only_ours'])}, only theirs "
                  f"{len(chart['in_range_only_theirs'])}{flag}")
        else:
            print(f"{chart['chart']}: distance {chart['mean_distance']:.3f} "
                  f"on {chart['classes']} classes in both ranges; in range only ours "
                  f"{len(chart['in_range_only_ours'])}, only theirs "
                  f"{len(chart['in_range_only_theirs'])}{flag}")
        if chart.get("restricted_range") and chart["range_difference"] is not None:
            print(f"    reach-weighted range: ours {chart['range_combos_ours']:.1f} combos, theirs "
                  f"{chart['range_combos_theirs']:.1f}, range difference {chart['range_difference']:.3f}")
        if chart["mean_distance"] is None:
            continue
        if chart["in_range_only_ours"] or chart["in_range_only_theirs"]:
            print(f"    range: only ours {' '.join(chart['in_range_only_ours'])} | only theirs "
                  f"{' '.join(chart['in_range_only_theirs'])}")
        for entry in chart["largest"]:
            print(f"    {entry['class']:4s} {entry['distance']:.3f} ours {entry['ours']} "
                  f"theirs {entry['theirs']}")
    if report["only_ours"]:
        print("only ours:", ", ".join(report["only_ours"]))
    if report["only_theirs"]:
        print("only theirs:", ", ".join(report["only_theirs"]))
    if report["overall_mean_distance"] is not None:
        print(f"overall: distance {report['overall_mean_distance']:.3f}"
              + (f", range difference {report['overall_range_difference']:.3f} (effective combos "
                 f"ours {report['overall_range_combos_ours']:.1f}, theirs "
                 f"{report['overall_range_combos_theirs']:.1f}, "
                 f"{report['distinct_restricted_ranges']} distinct restricted ranges)"
                 if report["overall_range_difference"] is not None else "")
              + "".join(f"; {label} {report[f'{group}_mean_distance']:.3f} "
                        f"({report[f'{group}_charts']} charts)"
                        for group, label in (("all_in", "facing all-in"),
                                             ("non_all_in", "not facing all-in"))
                        if report[f"{group}_mean_distance"] is not None))
    if args.json:
        args.json.write_text(json.dumps(report, indent=1), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
