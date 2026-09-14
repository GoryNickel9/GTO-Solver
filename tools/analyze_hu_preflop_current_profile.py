#!/usr/bin/env python3
"""Audit final regret-matching frequencies against full-current action EVs."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from statistics import NormalDist
from typing import Any

from analyze_hu_preflop_allin_diagnostics import (
    PHYSICAL_HANDS,
    PHYSICAL_HANDS_BY_CLASS,
    history_key,
    opponent_sequence_reach,
    own_sequence_reach,
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--minimum-frequency", type=float, default=0.05)
    parser.add_argument("--minimum-gap-ante", type=float, default=0.1)
    parser.add_argument("--minimum-public-reach", type=float, default=0.01)
    return parser.parse_args()


def audit(
    candidate: dict[str, Any],
    minimum_frequency: float,
    minimum_gap_ante: float,
    minimum_public_reach: float,
) -> dict[str, Any]:
    if not candidate.get("current_profile_evaluated"):
        raise ValueError("candidate does not contain a full-current profile evaluation")
    nodes = list(candidate["preflop_nodes"].values())
    nodes_by_history = {history_key(node["history"]): node for node in nodes}
    cases: list[dict[str, Any]] = []
    summary = {
        "rowsWithCurrentEv": 0,
        "eligibleActionComparisons": 0,
        "selectedInferiorActions": 0,
        "materialSelectedInferiorActions": 0,
        "materialOnPublicPath": 0,
        "resolvedMaterialOnPublicPath": 0,
        "familyWiseResolvedMaterialOnPublicPath": 0,
        "totalRootDealWeightedLossAnte": 0.0,
        "maximumMaterialGapAnte": 0.0,
    }

    for node in nodes:
        current_ev = node.get("current_action_ev")
        if not isinstance(current_ev, dict):
            raise ValueError(f"node {node['tree_node_id']} lacks current action EVs")
        actor = node["player"]
        for hand, frequencies in node["current_strategy"].items():
            ev_row = current_ev.get(hand)
            if not isinstance(ev_row, dict):
                continue
            available = {
                action: values
                for action, values in ev_row.items()
                if isinstance(values, dict) and math.isfinite(values["ev_ante"])
            }
            if not available:
                continue
            summary["rowsWithCurrentEv"] += 1
            summary["eligibleActionComparisons"] += max(0, len(available) - 1)
            best_action, best_values = max(
                available.items(), key=lambda item: item[1]["ev_ante"]
            )
            own_reach = own_sequence_reach(
                hand, actor, node["history"], nodes_by_history, "current_strategy"
            )
            opponent_reach = opponent_sequence_reach(
                hand,
                actor,
                node["history"],
                nodes_by_history,
                "current_strategy",
            )
            public_reach = own_reach * opponent_reach
            for action, frequency in frequencies.items():
                values = available.get(action)
                if values is None or frequency < minimum_frequency:
                    continue
                gap = best_values["ev_ante"] - values["ev_ante"]
                if gap <= 0.0:
                    continue
                summary["selectedInferiorActions"] += 1
                combined_se = math.hypot(
                    best_values["standard_error_ante"], values["standard_error_ante"]
                )
                approximate_z = gap / combined_se if combined_se > 0.0 else math.inf
                material = gap >= minimum_gap_ante
                on_public_path = material and public_reach >= minimum_public_reach
                resolved = on_public_path and approximate_z >= 2.0
                hand_prior = len(PHYSICAL_HANDS_BY_CLASS[hand]) / len(PHYSICAL_HANDS)
                realization_loss = hand_prior * public_reach * frequency * gap
                summary["materialSelectedInferiorActions"] += int(material)
                summary["materialOnPublicPath"] += int(on_public_path)
                summary["resolvedMaterialOnPublicPath"] += int(resolved)
                if on_public_path:
                    summary["totalRootDealWeightedLossAnte"] += realization_loss
                    summary["maximumMaterialGapAnte"] = max(
                        summary["maximumMaterialGapAnte"], gap
                    )
                cases.append(
                    {
                        "nodeId": node["tree_node_id"],
                        "player": actor,
                        "history": node["history"],
                        "hand": hand,
                        "action": action,
                        "bestAction": best_action,
                        "frequency": frequency,
                        "actionEvAnte": values["ev_ante"],
                        "bestEvAnte": best_values["ev_ante"],
                        "gapAnte": gap,
                        "combinedStandardErrorAnte": combined_se,
                        "approximateSeparationZ": approximate_z,
                        "ownSequenceReach": own_reach,
                        "opponentSequenceReach": opponent_reach,
                        "publicSequenceReach": public_reach,
                        "physicalHandPrior": hand_prior,
                        "rootDealWeightedLossAnte": realization_loss,
                        "material": material,
                        "onPublicPath": on_public_path,
                        "resolvedAtTwoApproximateSe": resolved,
                    }
                )

    comparisons = summary["eligibleActionComparisons"]
    family_wise_z = (
        NormalDist().inv_cdf(1.0 - 0.05 / (2.0 * comparisons))
        if comparisons > 0
        else math.inf
    )
    for case in cases:
        family_wise_resolved = (
            case["onPublicPath"] and case["approximateSeparationZ"] >= family_wise_z
        )
        case["familyWiseResolvedAtFivePercent"] = family_wise_resolved
        summary["familyWiseResolvedMaterialOnPublicPath"] += int(family_wise_resolved)
    summary["bonferroniTwoSidedFivePercentZ"] = family_wise_z

    cases.sort(
        key=lambda case: (
            case["onPublicPath"],
            case["rootDealWeightedLossAnte"],
            case["gapAnte"],
        ),
        reverse=True,
    )
    return {
        "schema": "gtosd.hu_preflop_full_current_profile_audit.v1",
        "candidate": candidate.get("algorithm"),
        "iterations": candidate.get("iterations"),
        "seed": candidate.get("seed"),
        "treeFingerprint": candidate.get("tree_fingerprint"),
        "evScope": "full_current_profile_action_ev_with_current_reach_v1",
        "thresholds": {
            "minimumFrequency": minimum_frequency,
            "minimumGapAnte": minimum_gap_ante,
            "minimumPublicReach": minimum_public_reach,
            "resolvedApproximateSeparationZ": 2.0,
            "familyWiseErrorRate": 0.05,
        },
        "summary": summary,
        "cases": cases,
    }


def main() -> int:
    args = parse_args()
    candidate = json.loads(args.candidate.read_text(encoding="utf-8"))
    result = audit(
        candidate,
        args.minimum_frequency,
        args.minimum_gap_ante,
        args.minimum_public_reach,
    )
    if args.output:
        args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    summary = result["summary"]
    print(
        "HU_PREFLOP_FULL_CURRENT_PROFILE_AUDIT=PASS"
        f" rows={summary['rowsWithCurrentEv']}"
        f" material={summary['materialSelectedInferiorActions']}"
        f" public_on_path={summary['materialOnPublicPath']}"
        f" resolved_public_on_path={summary['resolvedMaterialOnPublicPath']}"
        f" family_wise_resolved="
        f"{summary['familyWiseResolvedMaterialOnPublicPath']}"
        f" weighted_loss_ante={summary['totalRootDealWeightedLossAnte']:.9f}"
    )
    for case in result["cases"][:10]:
        print(
            f"node={case['nodeId']} hand={case['hand']} action={case['action']}"
            f" best={case['bestAction']} frequency={case['frequency']:.6f}"
            f" gap={case['gapAnte']:.6f} z={case['approximateSeparationZ']:.3f}"
            f" public_reach={case['publicSequenceReach']:.6f}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
