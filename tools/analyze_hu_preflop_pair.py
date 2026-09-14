"""Analyze two paired HU preflop solution exports without changing them."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
from typing import Any


def combo_mass(combo: str) -> int:
    if len(combo) == 2:
        return 6
    return 4 if combo.endswith("s") else 12


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as source:
        return json.load(source)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def root_strategy(candidate: dict[str, Any]) -> dict[str, dict[str, float]]:
    strategy = candidate.get("strategy")
    if not isinstance(strategy, dict) or len(strategy) != 81:
        raise ValueError("root strategy must contain exactly 81 classes")
    return strategy


def normalized_row(row: dict[str, Any], context: str) -> dict[str, float]:
    result = {action: float(value) for action, value in row.items()}
    if not result or any(not math.isfinite(value) or value < 0.0 for value in result.values()):
        raise ValueError(f"invalid strategy row: {context}")
    total = sum(result.values())
    if total <= 0.0:
        raise ValueError(f"zero strategy row: {context}")
    return {action: value / total for action, value in result.items()}


def paired_root_tv(first: dict[str, Any], second: dict[str, Any]) -> float:
    first_strategy = root_strategy(first)
    second_strategy = root_strategy(second)
    if first_strategy.keys() != second_strategy.keys():
        raise ValueError("root strategy class sets differ")
    weighted_tv = 0.0
    total_mass = 0
    for combo in first_strategy:
        left = normalized_row(first_strategy[combo], f"first.{combo}")
        right = normalized_row(second_strategy[combo], f"second.{combo}")
        if left.keys() != right.keys():
            raise ValueError(f"root action sets differ for {combo}")
        mass = combo_mass(combo)
        weighted_tv += mass * 0.5 * sum(abs(left[action] - right[action]) for action in left)
        total_mass += mass
    if total_mass != 630:
        raise ValueError(f"physical root mass is {total_mass}, expected 630")
    return 100.0 * weighted_tv / total_mass


def aggregate_root_actions(candidate: dict[str, Any]) -> dict[str, float]:
    totals: dict[str, float] = {}
    for combo, raw_row in root_strategy(candidate).items():
        row = normalized_row(raw_row, f"root.{combo}")
        mass = combo_mass(combo)
        for action, probability in row.items():
            totals[action] = totals.get(action, 0.0) + mass * probability
    return {action: 100.0 * value / 630.0 for action, value in sorted(totals.items())}


def validate_tree(candidate: dict[str, Any]) -> dict[str, Any]:
    nodes = candidate.get("preflop_nodes")
    if nodes is None:
        return {
            "status": "NOT_EXPORTED",
            "nodes": 0,
            "rows": 0,
            "action_ev_values": 0,
            "max_normalization_error": None,
        }
    if not isinstance(nodes, dict) or len(nodes) != 20:
        raise ValueError("preflop_nodes must contain exactly 20 nodes")
    rows = 0
    max_normalization_error = 0.0
    action_ev_values = 0
    for node_id, node in nodes.items():
        strategy = node.get("strategy")
        action_ev = node.get("action_ev")
        if not isinstance(strategy, dict) or len(strategy) != 81:
            raise ValueError(f"{node_id} must contain exactly 81 strategy rows")
        if not isinstance(action_ev, dict) or action_ev.keys() != strategy.keys():
            raise ValueError(f"{node_id} action-EV rows do not match strategy rows")
        rows += len(strategy)
        for combo, raw_row in strategy.items():
            row = {action: float(value) for action, value in raw_row.items()}
            if any(not math.isfinite(value) or value < 0.0 for value in row.values()):
                raise ValueError(f"invalid probability in {node_id}.{combo}")
            max_normalization_error = max(max_normalization_error, abs(sum(row.values()) - 1.0))
            if action_ev[combo].keys() != row.keys():
                raise ValueError(f"action-EV set differs in {node_id}.{combo}")
            for action, estimate in action_ev[combo].items():
                if estimate is None:
                    raise ValueError(f"missing action EV in {node_id}.{combo}.{action}")
                ev = float(estimate["ev_ante"])
                se = float(estimate["standard_error_ante"])
                if not math.isfinite(ev) or not math.isfinite(se) or se < 0.0:
                    raise ValueError(f"invalid action EV in {node_id}.{combo}.{action}")
                action_ev_values += 1
    return {
        "status": "PASS",
        "nodes": len(nodes),
        "rows": rows,
        "action_ev_values": action_ev_values,
        "max_normalization_error": max_normalization_error,
    }


def regret_reconstruction_error(candidate: dict[str, Any]) -> float:
    strategy = root_strategy(candidate)
    diagnostics = candidate.get("root_regret_diagnostics")
    if not isinstance(diagnostics, dict) or diagnostics.keys() != strategy.keys():
        raise ValueError("root regret diagnostics do not match strategy classes")
    maximum = 0.0
    for combo, row in strategy.items():
        diagnostic_actions = diagnostics[combo].get("actions")
        if not isinstance(diagnostic_actions, dict) or diagnostic_actions.keys() != row.keys():
            raise ValueError(f"root regret action set differs for {combo}")
        for action, probability in row.items():
            reconstructed = float(diagnostic_actions[action]["average_strategy"])
            maximum = max(maximum, abs(float(probability) - reconstructed))
    return maximum


def comparison_metrics(candidate_path: Path) -> dict[str, Any]:
    comparison_path = candidate_path.with_suffix(".comparison.json")
    if not comparison_path.exists():
        comparison_path = candidate_path.with_name(f"{candidate_path.stem}_comparison.json")
    if not comparison_path.exists():
        raise ValueError(f"comparison report not found for {candidate_path}")
    comparison = load_json(comparison_path)
    metrics = comparison.get("metrics")
    if not isinstance(metrics, dict):
        raise ValueError(f"missing metrics in {comparison_path}")
    return {
        "path": str(comparison_path),
        "status": comparison.get("status"),
        "configuration_comparability": comparison.get("configuration_comparability"),
        "metrics": metrics,
    }


def candidate_summary(path: Path, candidate: dict[str, Any]) -> dict[str, Any]:
    return {
        "path": str(path),
        "sha256": sha256(path),
        "iterations": int(candidate["iterations"]),
        "seed": int(candidate["seed"]),
        "evaluation_seed": int(candidate["evaluation_seed"]),
        "tree_fingerprint": candidate["tree_fingerprint"],
        "abstraction": candidate["abstraction"],
        "solve_seconds": float(candidate["solve_seconds"]),
        "information_sets": int(candidate["information_sets"]),
        "best_response_information_sets": int(candidate["best_response_information_sets"]),
        "numeric_state_payload_bytes": int(candidate["numeric_state_payload_bytes"]),
        "minimum_blueprint_payload_bytes": int(candidate["minimum_blueprint_payload_bytes"]),
        "minimum_best_response_payload_bytes": int(candidate["minimum_best_response_payload_bytes"]),
        "occupied_distributional_buckets": candidate["occupied_distributional_buckets"],
        "root_ev_ante": float(candidate["root_ev_ante"]),
        "root_ev_standard_error_ante": float(candidate["root_ev_standard_error_ante"]),
        "postflop_all_in_expectation": candidate.get(
            "postflop_all_in_expectation", "sampled_runout_v1"
        ),
        "exact_postflop_all_in_evaluations": candidate.get(
            "exact_postflop_all_in_evaluations", [0, 0]
        ),
        "exact_postflop_all_in_runouts": candidate.get(
            "exact_postflop_all_in_runouts", [0, 0]
        ),
        "exact_postflop_all_in_seconds": float(
            candidate.get("exact_postflop_all_in_seconds", 0.0)
        ),
        "exact_postflop_all_in_cache_budget_entries": int(
            candidate.get("exact_postflop_all_in_cache_budget_entries", 0)
        ),
        "exact_postflop_all_in_cache_hits": int(
            candidate.get("exact_postflop_all_in_cache_hits", 0)
        ),
        "exact_postflop_all_in_cache_misses": int(
            candidate.get("exact_postflop_all_in_cache_misses", 0)
        ),
        "exact_postflop_all_in_cache_peak_entries": int(
            candidate.get("exact_postflop_all_in_cache_peak_entries", 0)
        ),
        "exact_postflop_all_in_cache_evictions": int(
            candidate.get("exact_postflop_all_in_cache_evictions", 0)
        ),
        "root_actions_percentage": aggregate_root_actions(candidate),
        "aa_strategy": normalized_row(candidate["strategy"]["AA"], "root.AA"),
        "aa_action_ev": candidate["root_action_ev"]["AA"],
        "tree_integrity": validate_tree(candidate),
        "root_regret_reconstruction_error": regret_reconstruction_error(candidate),
        "comparison": comparison_metrics(path),
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("first", type=Path)
    parser.add_argument("second", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    first = load_json(args.first)
    second = load_json(args.second)
    if first["tree_fingerprint"] != second["tree_fingerprint"]:
        raise ValueError("candidate tree fingerprints differ")
    report = {
        "schema": "gtosd.hu_preflop_pair_analysis.v1",
        "first": candidate_summary(args.first, first),
        "second": candidate_summary(args.second, second),
        "paired_root_total_variation_percentage_points": paired_root_tv(first, second),
    }
    encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(encoded, encoding="utf-8")
    print(encoded, end="")


if __name__ == "__main__":
    main()
