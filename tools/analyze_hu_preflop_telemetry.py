"""Analyze V19 action-conditioned telemetry without changing solver artifacts."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from collections import defaultdict
from pathlib import Path
from typing import Any


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def family(hand_class: str) -> str:
    if len(hand_class) == 2:
        return "pairs"
    return "suited" if hand_class.endswith("s") else "offsuit"


def finite(value: Any) -> float:
    parsed = float(value)
    if not math.isfinite(parsed):
        raise ValueError("telemetry contains a non-finite value")
    return parsed


def group_key(row: dict[str, Any]) -> tuple[Any, ...]:
    return (
        int(row["node_id"]),
        row["player"],
        int(row["history"]),
        row["hand_class"],
        int(row["bucket_key"]),
        row["postflop_street"],
    )


def analyze(payload: dict[str, Any]) -> dict[str, Any]:
    rows = payload.get("rows")
    if not isinstance(rows, list):
        raise ValueError("telemetry rows must be an array")
    groups: dict[tuple[Any, ...], list[dict[str, Any]]] = defaultdict(list)
    for row in rows:
        if not isinstance(row, dict):
            raise ValueError("telemetry row must be an object")
        for field in (
            "physical_combo_mass",
            "sample_count",
            "mean_action_value",
            "variance_action_value",
            "standard_error_action_value",
            "spread_action_value",
        ):
            finite(row[field])
        groups[group_key(row)].append(row)

    group_rows: list[dict[str, Any]] = []
    family_spread: dict[str, float] = defaultdict(float)
    terminal_spread: dict[str, float] = defaultdict(float)
    gap_histogram = {"le_0_1": 0.0, "between_0_1_0_5": 0.0, "ge_0_5": 0.0}
    for key, members in groups.items():
        values = [finite(member["mean_action_value"]) for member in members]
        if not values:
            continue
        spread = max(values) - min(values)
        mass = max(finite(member["physical_combo_mass"]) for member in members)
        occupancy = max(int(member["bucket_occupancy"]) for member in members)
        impact = mass * spread
        hand_class = str(key[3])
        group_rows.append(
            {
                "node_id": key[0],
                "player": key[1],
                "history": key[2],
                "hand_class": hand_class,
                "bucket_key": key[4],
                "postflop_street": key[5],
                "action_count": len(members),
                "weighted_mass": mass,
                "bucket_occupancy": occupancy,
                "max_action_pair_spread": spread,
                "impact": impact,
                "actions": {
                    str(member["action_id"]): finite(member["mean_action_value"])
                    for member in sorted(members, key=lambda item: int(item["action_id"]))
                },
            }
        )
        family_name = family(hand_class)
        family_spread[family_name] += impact
        for member in members:
            terminal_spread[str(member["terminal_type"])] += mass * finite(
                member["spread_action_value"]
            )
        for left_index, left in enumerate(values):
            for right in values[left_index + 1 :]:
                gap = abs(left - right) * mass
                if abs(left - right) <= 0.1:
                    gap_histogram["le_0_1"] += gap
                elif abs(left - right) < 0.5:
                    gap_histogram["between_0_1_0_5"] += gap
                else:
                    gap_histogram["ge_0_5"] += gap

    group_rows.sort(key=lambda row: (row["max_action_pair_spread"], row["impact"]), reverse=True)
    top_spread = group_rows[:100]
    top_impact = sorted(group_rows, key=lambda row: row["impact"], reverse=True)[:100]
    total_family = sum(family_spread.values())
    total_terminal = sum(terminal_spread.values())
    return {
        "schema": "gtosd.hu_preflop_action_conditioned_telemetry_analysis.v1",
        "source": {
            "path": str(payload.get("source", "")),
            "sha256": payload.get("source_sha256"),
            "tree_fingerprint": payload.get("tree_fingerprint"),
            "algorithm": payload.get("algorithm"),
            "iterations": payload.get("iterations"),
            "seed": payload.get("seed"),
            "partition_seed": payload.get("partition_seed"),
            "maximum_entries": payload.get("maximum_entries"),
            "dropped_observations": payload.get("dropped_observations", 0),
        },
        "completeness": {
            "rows": len(rows),
            "bucket_groups": len(group_rows),
            "complete": int(payload.get("dropped_observations", 0)) == 0,
        },
        "top_100_bucket_spread": top_spread,
        "top_100_bucket_impact": top_impact,
        "gap_histogram_weighted": gap_histogram,
        "family_contribution": {
            name: {
                "weighted_spread_impact": value,
                "fraction": value / total_family if total_family else 0.0,
            }
            for name, value in sorted(family_spread.items())
        },
        "terminal_contribution": {
            name: {
                "weighted_spread_impact": value,
                "fraction": value / total_terminal if total_terminal else 0.0,
            }
            for name, value in sorted(terminal_spread.items())
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("telemetry", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    payload = load_json(args.telemetry)
    payload["source"] = str(args.telemetry)
    payload["source_sha256"] = sha256(args.telemetry)
    report = analyze(payload)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
