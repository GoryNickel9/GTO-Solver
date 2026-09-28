"""Compare two directories of preflop charts in the MonkerSolver text format.

Each chart file is tab separated: `Combination`, one column per action, `Total`,
and one row per hand class (81 classes in short deck). Files are matched by
their path relative to each directory (for example `CO/CO_strategy.txt`).

For every chart present on both sides the script reports, over the hand classes
weighted by their number of combos (pairs 6, suited 4, offsuit 12):
- the mean total-variation distance of the action frequencies (half the L1
  distance: 0 means identical strategies, 1 means no action in common);
- the share of combos whose most frequent action is the same on both sides;
- the hand classes with the largest distances.
An action that exists on one side only counts as frequency 0 on the other.
"""
from __future__ import annotations

import argparse
import json
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


def main_action(frequencies: dict[str, float]) -> str:
    return max(sorted(frequencies), key=lambda action: frequencies[action])


def compare(ours: pathlib.Path, theirs: pathlib.Path, top: int) -> dict:
    ours_files = {p.relative_to(ours).as_posix() for p in ours.rglob("*_strategy.txt")}
    theirs_files = {p.relative_to(theirs).as_posix() for p in theirs.rglob("*_strategy.txt")}
    report = {"charts": [], "only_ours": sorted(ours_files - theirs_files),
              "only_theirs": sorted(theirs_files - ours_files)}
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
            "classes": len(per_class),
            "outside_both_ranges": outside_range,
            "in_range_only_ours": only_ours,
            "in_range_only_theirs": only_theirs,
            "mean_distance": round(distance_sum / weight_sum, 4) if weight_sum else None,
            "same_main_action_share": round(same_sum / weight_sum, 4) if weight_sum else None,
            "largest": per_class[:top],
        })
    distances = [c["mean_distance"] for c in report["charts"] if c["mean_distance"] is not None]
    shares = [c["same_main_action_share"] for c in report["charts"]
              if c["same_main_action_share"] is not None]
    report["overall_mean_distance"] = round(sum(distances) / len(distances), 4) if distances else None
    report["overall_same_main_action_share"] = round(sum(shares) / len(shares), 4) if shares else None
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("ours", type=pathlib.Path)
    parser.add_argument("theirs", type=pathlib.Path)
    parser.add_argument("--top", type=int, default=5)
    parser.add_argument("--json", type=pathlib.Path)
    args = parser.parse_args()
    report = compare(args.ours, args.theirs, args.top)
    for chart in report["charts"]:
        flag = "" if chart["actions_ours"] == chart["actions_theirs"] else "  [actions differ]"
        if chart["mean_distance"] is None:
            print(f"{chart['chart']}: no class in both ranges; in range only ours "
                  f"{len(chart['in_range_only_ours'])}, only theirs "
                  f"{len(chart['in_range_only_theirs'])}{flag}")
            continue
        print(f"{chart['chart']}: distance {chart['mean_distance']:.3f}, "
              f"same main action {100 * chart['same_main_action_share']:.1f} % "
              f"on {chart['classes']} classes in both ranges; in range only ours "
              f"{len(chart['in_range_only_ours'])}, only theirs "
              f"{len(chart['in_range_only_theirs'])}{flag}")
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
        print(f"overall: distance {report['overall_mean_distance']:.3f}, same main action "
              f"{100 * report['overall_same_main_action_share']:.1f} %")
    if args.json:
        args.json.write_text(json.dumps(report, indent=1), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
