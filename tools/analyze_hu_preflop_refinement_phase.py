#!/usr/bin/env python3
"""Recover and compare the root strategy accumulated only during preflop refinement."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
from typing import Any


ACTIONS = ("all_in", "raise_6", "raise_10", "call", "fold")
MONKER_COLUMNS = {
    "all_in": "AllIn",
    "raise_6": "6.0ante",
    "raise_10": "10.0ante",
    "call": "Call",
    "fold": "Fold",
}
EPSILON = 1.0e-9


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


def normalized(row: dict[str, float], context: str) -> dict[str, float]:
    if set(row) != set(ACTIONS):
        raise ValueError(f"{context} does not contain the five root actions")
    values = {action: float(row[action]) for action in ACTIONS}
    total = sum(values.values())
    if total <= 0.0 or any(not math.isfinite(value) or value < -EPSILON for value in values.values()):
        raise ValueError(f"{context} is not a valid probability row")
    return {action: max(0.0, value) / total for action, value in values.items()}


def average_policy(candidate: dict[str, Any]) -> dict[str, dict[str, float]]:
    node = candidate.get("preflop_nodes", {}).get("CO")
    if not isinstance(node, dict) or node.get("history") != [] or node.get("player") != "CO":
        raise ValueError("candidate does not contain the CO root node")
    raw = node.get("strategy")
    if not isinstance(raw, dict) or len(raw) != 81:
        raise ValueError("candidate root strategy must contain 81 classes")
    return {combo: normalized(row, f"average.{combo}") for combo, row in raw.items()}


def refinement_policy(
    baseline: dict[str, Any], refined: dict[str, Any]
) -> tuple[dict[str, dict[str, float]], dict[str, Any]]:
    base_diagnostics = baseline.get("root_regret_diagnostics")
    final_diagnostics = refined.get("root_regret_diagnostics")
    if not isinstance(base_diagnostics, dict) or not isinstance(final_diagnostics, dict):
        raise ValueError("both candidates must export root regret diagnostics")
    if set(base_diagnostics) != set(final_diagnostics) or len(base_diagnostics) != 81:
        raise ValueError("root diagnostic class sets differ")

    policy: dict[str, dict[str, float]] = {}
    minimum_delta = math.inf
    total_phase_weight = 0.0
    for combo in base_diagnostics:
        delta_weights: dict[str, float] = {}
        for action in ACTIONS:
            base_weight = float(
                base_diagnostics[combo]["actions"][action]["cumulative_average_weight"]
            )
            final_weight = float(
                final_diagnostics[combo]["actions"][action]["cumulative_average_weight"]
            )
            delta = final_weight - base_weight
            minimum_delta = min(minimum_delta, delta)
            if delta < -EPSILON:
                raise ValueError(
                    f"{combo}.{action} cumulative average weight decreased by {delta}"
                )
            delta_weights[action] = max(0.0, delta)
        phase_total = sum(delta_weights.values())
        if phase_total <= 0.0:
            raise ValueError(f"{combo} has no refinement average weight")
        total_phase_weight += phase_total
        policy[combo] = {action: delta_weights[action] / phase_total for action in ACTIONS}

    return policy, {
        "minimumCumulativeWeightDelta": minimum_delta,
        "totalRootRefinementAverageWeight": total_phase_weight,
        "classes": len(policy),
        "physicalCombos": sum(combo_mass(combo) for combo in policy),
    }


def monker_policy(path: Path) -> dict[str, dict[str, float]]:
    with path.open("r", encoding="utf-8-sig", newline="") as source:
        rows = list(csv.DictReader(source, delimiter="\t"))
    if len(rows) != 81:
        raise ValueError(f"Monker root contains {len(rows)} classes instead of 81")
    result: dict[str, dict[str, float]] = {}
    for source_row in rows:
        combo = source_row["Combination"]
        row = {
            action: float(source_row[column]) for action, column in MONKER_COLUMNS.items()
        }
        result[combo] = normalized(row, f"monker.{combo}")
    return result


def external_metrics(
    policy: dict[str, dict[str, float]], reference: dict[str, dict[str, float]]
) -> dict[str, Any]:
    if set(policy) != set(reference):
        raise ValueError("candidate and Monker class sets differ")
    weighted_l1 = 0.0
    root_actions = {action: 0.0 for action in ACTIONS}
    reference_actions = {action: 0.0 for action in ACTIONS}
    for combo in policy:
        mass = combo_mass(combo)
        for action in ACTIONS:
            weighted_l1 += mass * abs(policy[combo][action] - reference[combo][action])
            root_actions[action] += mass * policy[combo][action] / 630.0
            reference_actions[action] += mass * reference[combo][action] / 630.0
    return {
        "weightedMeanAbsoluteActionErrorPercentagePoints": 100.0 * weighted_l1 / (630.0 * 5.0),
        "weightedMeanClassTotalVariationPercentagePoints": 100.0 * weighted_l1 / (630.0 * 2.0),
        "rootActionFrequencyPercentagePoints": {
            action: 100.0 * root_actions[action] for action in ACTIONS
        },
        "rootActionDeltaPercentagePoints": {
            action: 100.0 * (root_actions[action] - reference_actions[action])
            for action in ACTIONS
        },
    }


def interseed_tv(
    first: dict[str, dict[str, float]], second: dict[str, dict[str, float]]
) -> float:
    if set(first) != set(second):
        raise ValueError("seed class sets differ")
    weighted_tv = 0.0
    for combo in first:
        weighted_tv += combo_mass(combo) * 0.5 * sum(
            abs(first[combo][action] - second[combo][action]) for action in ACTIONS
        )
    return 100.0 * weighted_tv / 630.0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline-seed1", type=Path, required=True)
    parser.add_argument("--baseline-seed2", type=Path, required=True)
    parser.add_argument("--refined-seed1", type=Path, required=True)
    parser.add_argument("--refined-seed2", type=Path, required=True)
    parser.add_argument("--monker-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    paths = {
        "baselineSeed1": args.baseline_seed1,
        "baselineSeed2": args.baseline_seed2,
        "refinedSeed1": args.refined_seed1,
        "refinedSeed2": args.refined_seed2,
        "monkerRoot": args.monker_root,
    }
    baseline_one = load_json(args.baseline_seed1)
    baseline_two = load_json(args.baseline_seed2)
    refined_one = load_json(args.refined_seed1)
    refined_two = load_json(args.refined_seed2)
    reference = monker_policy(args.monker_root)

    phase_one, phase_one_invariants = refinement_policy(baseline_one, refined_one)
    phase_two, phase_two_invariants = refinement_policy(baseline_two, refined_two)
    baseline_policies = (average_policy(baseline_one), average_policy(baseline_two))
    refined_policies = (average_policy(refined_one), average_policy(refined_two))

    baseline_metrics = [external_metrics(policy, reference) for policy in baseline_policies]
    refined_metrics = [external_metrics(policy, reference) for policy in refined_policies]
    phase_metrics = [external_metrics(policy, reference) for policy in (phase_one, phase_two)]

    def pair_mean(key: str, rows: list[dict[str, Any]]) -> float:
        return 0.5 * (float(rows[0][key]) + float(rows[1][key]))

    baseline_wmae = pair_mean("weightedMeanAbsoluteActionErrorPercentagePoints", baseline_metrics)
    refined_wmae = pair_mean("weightedMeanAbsoluteActionErrorPercentagePoints", refined_metrics)
    phase_wmae = pair_mean("weightedMeanAbsoluteActionErrorPercentagePoints", phase_metrics)
    output = {
        "schema": "gtosd.hu_preflop_refinement_phase_analysis.v1",
        "status": "DIAGNOSTIC_ONLY_NOT_A_SOLUTION_OR_PROMOTION_GATE",
        "inputs": {
            key: {"path": str(path), "sha256": sha256(path)} for key, path in paths.items()
        },
        "invariants": [phase_one_invariants, phase_two_invariants],
        "baseline": {
            "seedMetrics": baseline_metrics,
            "meanWmaePercentagePoints": baseline_wmae,
            "meanExternalTvPercentagePoints": pair_mean(
                "weightedMeanClassTotalVariationPercentagePoints", baseline_metrics
            ),
            "interseedTvPercentagePoints": interseed_tv(*baseline_policies),
        },
        "v18Final": {
            "seedMetrics": refined_metrics,
            "meanWmaePercentagePoints": refined_wmae,
            "meanExternalTvPercentagePoints": pair_mean(
                "weightedMeanClassTotalVariationPercentagePoints", refined_metrics
            ),
            "interseedTvPercentagePoints": interseed_tv(*refined_policies),
            "relativeWmaeImprovementPercent": 100.0 * (baseline_wmae - refined_wmae) / baseline_wmae,
        },
        "refinementPhaseOnly": {
            "definition": "normalized positive delta of root cumulative_average_weight between V15/V17 and V18",
            "seedMetrics": phase_metrics,
            "meanWmaePercentagePoints": phase_wmae,
            "meanExternalTvPercentagePoints": pair_mean(
                "weightedMeanClassTotalVariationPercentagePoints", phase_metrics
            ),
            "interseedTvPercentagePoints": interseed_tv(phase_one, phase_two),
            "relativeWmaeImprovementVersusBaselinePercent": 100.0
            * (baseline_wmae - phase_wmae)
            / baseline_wmae,
        },
        "interpretationBoundary": (
            "The refinement-only average is a post-hoc diagnostic recovered from training state. "
            "It has no separately evaluated EV, policy file, continuation profile, or preregistered gate."
        ),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8", newline="\n") as destination:
        json.dump(output, destination, indent=2, sort_keys=True)
        destination.write("\n")

    print(
        "HU_PREFLOP_REFINEMENT_PHASE_ANALYSIS=PASS"
        f" baseline_wmae={baseline_wmae:.6f}"
        f" v18_wmae={refined_wmae:.6f}"
        f" phase_wmae={phase_wmae:.6f}"
        f" phase_interseed_tv={output['refinementPhaseOnly']['interseedTvPercentagePoints']:.6f}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
