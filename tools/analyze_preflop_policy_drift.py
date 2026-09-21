"""Compare two dense blueprint policies without loading them into RAM."""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

import numpy as np


def _u32(stream) -> int:
    return struct.unpack("<I", stream.read(4))[0]


def _u64(stream) -> int:
    return struct.unpack("<Q", stream.read(8))[0]


def _string(stream) -> str:
    return stream.read(_u32(stream)).decode("utf-8")


def policy_table(path: Path) -> tuple[np.memmap, dict]:
    with path.open("rb") as stream:
        if stream.read(8) != b"GTOSDPOL" or _u32(stream) != 1:
            raise ValueError(f"invalid policy: {path}")
        tree = _string(stream)
        capacities = [_u32(stream), _u32(stream), _u32(stream)]
        entries = _u64(stream)
        source = _string(stream)
        offset = stream.tell()
    table = np.memmap(path, dtype="<f8", mode="r", offset=offset, shape=(entries,))
    return table, {"tree": tree, "capacities": capacities, "entries": entries, "source": source}


def checkpoint_sums(path: Path) -> tuple[np.memmap, dict]:
    with path.open("rb") as stream:
        if stream.read(8) != b"GTOSDCKP" or _u32(stream) != 2:
            raise ValueError(f"invalid checkpoint: {path}")
        identity = _string(stream)
        iteration = _u64(stream)
        boards = _u64(stream)
        stream.seek(8 * 8, 1)
        entries = _u64(stream)
        regrets_offset = stream.tell()
    sums_offset = regrets_offset + entries * 8
    sums = np.memmap(path, dtype="<f8", mode="r", offset=sums_offset, shape=(entries,))
    return sums, {
        "identity": identity,
        "iteration": iteration,
        "boards_processed": boards,
        "entries": entries,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--left", type=Path, required=True)
    parser.add_argument("--right", type=Path, required=True)
    parser.add_argument("--checkpoint", type=Path, required=True)
    parser.add_argument("--coverage", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    left, left_info = policy_table(args.left)
    right, right_info = policy_table(args.right)
    sums, checkpoint_info = checkpoint_sums(args.checkpoint)
    coverage = json.loads(args.coverage.read_text(encoding="utf-8"))
    entries = left_info["entries"]
    if right_info["entries"] != entries or checkpoint_info["entries"] != entries:
        raise ValueError("entry count mismatch")
    if right_info["tree"] != left_info["tree"]:
        raise ValueError("tree mismatch")

    street_totals: dict[str, dict[str, float]] = {}
    node_reports = []
    offset = 0
    for node in sorted(coverage["nodes"], key=lambda item: item["node"]):
        rows = int(node["rows"])
        actions = int(node["actions"])
        cells = rows * actions
        left_rows = np.asarray(left[offset : offset + cells]).reshape(rows, actions)
        right_rows = np.asarray(right[offset : offset + cells]).reshape(rows, actions)
        sum_rows = np.asarray(sums[offset : offset + cells]).reshape(rows, actions)
        tv = 0.5 * np.abs(left_rows - right_rows).sum(axis=1)
        mass = sum_rows.sum(axis=1)
        active = mass > 0.0
        active_count = int(active.sum())
        total_mass = float(mass.sum())
        weighted_tv = float(np.dot(tv, mass) / total_mass) if total_mass > 0.0 else 0.0
        unweighted_tv = float(tv[active].mean()) if active_count else 0.0
        report = {
            "node": int(node["node"]),
            "street": node["street"],
            "actor": int(node["actor"]),
            "actions": actions,
            "rows": rows,
            "active_rows": active_count,
            "strategy_mass": total_mass,
            "reach_weighted_tv": weighted_tv,
            "active_row_mean_tv": unweighted_tv,
            "maximum_tv": float(tv.max(initial=0.0)),
        }
        node_reports.append(report)
        total = street_totals.setdefault(
            node["street"],
            {"mass": 0.0, "weighted_tv_sum": 0.0, "active_rows": 0, "tv_sum": 0.0},
        )
        total["mass"] += total_mass
        total["weighted_tv_sum"] += float(np.dot(tv, mass))
        total["active_rows"] += active_count
        total["tv_sum"] += float(tv[active].sum())
        offset += cells
    if offset != entries:
        raise ValueError(f"layout consumed {offset} of {entries} entries")

    streets = []
    for street, total in street_totals.items():
        streets.append(
            {
                "street": street,
                "strategy_mass": total["mass"],
                "reach_weighted_tv": total["weighted_tv_sum"] / total["mass"],
                "active_row_mean_tv": total["tv_sum"] / total["active_rows"],
                "active_rows": total["active_rows"],
            }
        )
    output = {
        "schema": "gtosd.research.policy_drift.v1",
        "left": left_info,
        "right": right_info,
        "checkpoint": checkpoint_info,
        "streets": streets,
        "nodes": node_reports,
    }
    args.output.write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
