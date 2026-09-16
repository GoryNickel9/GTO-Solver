"""Static validator of preflop blueprint chart exports (gtosd.preflop_blueprint_chart.v1).

Checks, without the solver: schema id and required fields, the four
fingerprints, the status badge vocabulary and its exploitability block, one
node per preflop id with 81 class rows, action sets consistent between
strategy and action EV, frequencies summing to one, histories consistent with
the node ids, the root strategy equal to the root node, and the checksum
recomputed over the text that precedes it.

Usage: validate_preflop_blueprint_chart.py CHART [CHART ...]
Exit code 0 on success, 1 on failure.
"""

from __future__ import annotations

import json
import math
import sys
from pathlib import Path

SCHEMA = "gtosd.preflop_blueprint_chart.v1"
BADGES = {"ESTIMATED", "CERTIFIED_EXACT"}
CLASS_COUNT = 81
RANKS = "AKQJT9876"


def class_names() -> list[str]:
    names = [rank + rank for rank in RANKS]
    for suffix in ("s", "o"):
        for high in range(len(RANKS)):
            for low in range(high + 1, len(RANKS)):
                names.append(RANKS[high] + RANKS[low] + suffix)
    assert len(names) == CLASS_COUNT
    return names


def fnv1a64(data: bytes) -> int:
    value = 14695981039346656037
    for byte in data:
        value ^= byte
        value = (value * 1099511628211) % (1 << 64)
    return value


def fail(message: str) -> None:
    raise ValueError(message)


def validate(path: Path) -> dict:
    text = path.read_text(encoding="utf-8")
    chart = json.loads(text)
    if chart.get("schema") != SCHEMA:
        fail(f"{path.name}: schema {chart.get('schema')!r} is not {SCHEMA}")
    for key in ("config_id", "tree_fingerprint", "fingerprints", "game", "capacities", "status",
                "evaluation", "root_ev_ante", "root_ev_standard_error_ante", "strategy",
                "root_action_ev", "preflop_nodes", "checksum"):
        if key not in chart:
            fail(f"{path.name}: missing {key}")
    fingerprints = chart["fingerprints"]
    for key in ("rules", "tree", "catalog", "flop_table", "turn_table", "river_table", "policy"):
        if not str(fingerprints.get(key, "")).startswith("fnv1a64:"):
            fail(f"{path.name}: fingerprint {key} missing or malformed")
    if fingerprints["tree"] != chart["tree_fingerprint"]:
        fail(f"{path.name}: tree fingerprint mismatch")
    status = chart["status"]
    if status.get("badge") not in BADGES:
        fail(f"{path.name}: badge {status.get('badge')!r} not in {sorted(BADGES)}")
    exploitability = status.get("exploitability", {})
    for key in ("exact", "max_gain_antes", "max_gain_lower_antes", "max_gain_half_width_antes",
                "nashconv_antes", "flops", "boards"):
        if key not in exploitability:
            fail(f"{path.name}: exploitability block lacks {key}")
    if (status["badge"] == "CERTIFIED_EXACT") != bool(exploitability["exact"]):
        fail(f"{path.name}: badge and exact flag disagree")
    if status["badge"] == "CERTIFIED_EXACT" and fingerprints["policy"] != exploitability.get(
        "certificate_policy_fingerprint"
    ):
        fail(f"{path.name}: certificate policy fingerprint differs from the chart policy")
    names = class_names()
    positions = chart["game"]["positions"]
    nodes = chart["preflop_nodes"]
    if not isinstance(nodes, dict) or not nodes:
        fail(f"{path.name}: preflop_nodes is empty")
    root_id = positions[0]
    if root_id not in nodes:
        fail(f"{path.name}: root node {root_id} missing")
    rows = 0
    for node_id, node in nodes.items():
        if node.get("id") != node_id:
            fail(f"{path.name}: node {node_id} id field differs")
        if node.get("player") not in positions:
            fail(f"{path.name}: node {node_id} player unknown")
        history = node.get("history")
        if not isinstance(history, list):
            fail(f"{path.name}: node {node_id} lacks a history")
        expected = "_".join(f"{step['player']}_{step['action']}" for step in history)
        expected = f"{expected}_{node['player']}" if history else node["player"]
        if expected != node_id:
            fail(f"{path.name}: node {node_id} history does not rebuild the id ({expected})")
        actions = node.get("actions")
        if not isinstance(actions, list) or not actions:
            fail(f"{path.name}: node {node_id} lacks actions")
        strategy = node.get("strategy", {})
        action_ev = node.get("action_ev", {})
        if set(strategy) != set(names) or set(action_ev) != set(names):
            fail(f"{path.name}: node {node_id} does not have exactly the 81 classes")
        for name in names:
            row = strategy[name]
            if set(row) != set(actions):
                fail(f"{path.name}: node {node_id}.{name} strategy actions differ")
            total = 0.0
            for action in actions:
                frequency = float(row[action])
                if not math.isfinite(frequency) or frequency < -1e-12 or frequency > 1.0 + 1e-12:
                    fail(f"{path.name}: node {node_id}.{name}.{action} frequency {frequency}")
                total += frequency
                estimate = action_ev[name].get(action)
                if not isinstance(estimate, dict):
                    fail(f"{path.name}: node {node_id}.{name}.{action} lacks an EV estimate")
                for key in ("ev_ante", "standard_error_ante", "samples"):
                    if key not in estimate or not math.isfinite(float(estimate[key])):
                        fail(f"{path.name}: node {node_id}.{name}.{action} EV field {key}")
            if abs(total - 1.0) > 1e-9:
                fail(f"{path.name}: node {node_id}.{name} frequencies sum to {total}")
            rows += 1
    if chart["strategy"] != nodes[root_id]["strategy"]:
        fail(f"{path.name}: top-level strategy differs from the root node")
    if chart["root_action_ev"] != nodes[root_id]["action_ev"]:
        fail(f"{path.name}: top-level root_action_ev differs from the root node")
    marker = '  "checksum": '
    position = text.rfind(marker)
    if position < 0:
        fail(f"{path.name}: checksum line not found")
    expected_checksum = f"fnv1a64:{fnv1a64(text[:position].encode('utf-8')):016x}"
    if chart["checksum"] != expected_checksum:
        fail(f"{path.name}: checksum {chart['checksum']} differs from {expected_checksum}")
    return {"file": path.name, "nodes": len(nodes), "rows": rows, "badge": status["badge"]}


def main(arguments: list[str]) -> int:
    if not arguments:
        print("usage: validate_preflop_blueprint_chart.py CHART [CHART ...]", file=sys.stderr)
        return 1
    try:
        for argument in arguments:
            summary = validate(Path(argument))
            print(
                "PREFLOP_BLUEPRINT_CHART=PASS "
                + " ".join(f"{key}={value}" for key, value in summary.items())
            )
    except (ValueError, KeyError, TypeError, json.JSONDecodeError, OSError) as error:
        print(f"PREFLOP_BLUEPRINT_CHART=FAIL {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
