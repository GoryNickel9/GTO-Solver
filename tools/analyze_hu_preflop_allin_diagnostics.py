#!/usr/bin/env python3
"""Compare exact all-in action values with average and current preflop policies."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


RANKS = "AKQJT9876"
SUITS = "cdhs"


def hand_class(card_a: str, card_b: str) -> str:
    rank_a, rank_b = card_a[0], card_b[0]
    if rank_a == rank_b:
        return rank_a + rank_b
    if RANKS.index(rank_a) > RANKS.index(rank_b):
        rank_a, rank_b = rank_b, rank_a
        card_a, card_b = card_b, card_a
    return rank_a + rank_b + ("s" if card_a[1] == card_b[1] else "o")


def physical_hands() -> list[tuple[frozenset[str], str]]:
    cards = [rank + suit for rank in RANKS for suit in SUITS]
    return [
        (frozenset((cards[left], cards[right])), hand_class(cards[left], cards[right]))
        for left in range(len(cards))
        for right in range(left + 1, len(cards))
    ]


PHYSICAL_HANDS = physical_hands()
PHYSICAL_HANDS_BY_CLASS: dict[str, list[frozenset[str]]] = {}
for cards, class_name in PHYSICAL_HANDS:
    PHYSICAL_HANDS_BY_CLASS.setdefault(class_name, []).append(cards)


def history_key(history: list[dict[str, str]]) -> tuple[tuple[str, str], ...]:
    return tuple((step["player"], step["action"]) for step in history)


def opponent_sequence_reach(
    actor_hand: str,
    actor: str,
    history: list[dict[str, str]],
    nodes_by_history: dict[tuple[tuple[str, str], ...], dict[str, Any]],
    strategy_field: str = "strategy",
) -> float:
    opponent = "BTN" if actor == "CO" else "CO"
    reach_by_class: dict[str, float] = {}
    for _, opponent_hand in PHYSICAL_HANDS:
        if opponent_hand in reach_by_class:
            continue
        reach = 1.0
        prefix: list[dict[str, str]] = []
        for step in history:
            if step["player"] == opponent:
                node = nodes_by_history[history_key(prefix)]
                if node["player"] != opponent:
                    raise ValueError("history prefix does not resolve to the opponent")
                reach *= node[strategy_field][opponent_hand][step["action"]]
            prefix.append(step)
        reach_by_class[opponent_hand] = reach

    weighted_reach = 0.0
    compatible_pairs = 0
    for actor_cards in PHYSICAL_HANDS_BY_CLASS[actor_hand]:
        for opponent_cards, opponent_hand in PHYSICAL_HANDS:
            if actor_cards.isdisjoint(opponent_cards):
                weighted_reach += reach_by_class[opponent_hand]
                compatible_pairs += 1
    if compatible_pairs == 0:
        raise ValueError(f"no compatible physical deals for {actor_hand}")
    return weighted_reach / compatible_pairs


def own_sequence_reach(
    actor_hand: str,
    actor: str,
    history: list[dict[str, str]],
    nodes_by_history: dict[tuple[tuple[str, str], ...], dict[str, Any]],
    strategy_field: str,
) -> float:
    reach = 1.0
    prefix: list[dict[str, str]] = []
    for step in history:
        if step["player"] == actor:
            node = nodes_by_history[history_key(prefix)]
            if node["player"] != actor:
                raise ValueError("history prefix does not resolve to the acting player")
            reach *= node[strategy_field][actor_hand][step["action"]]
        prefix.append(step)
    return reach


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--audit", type=Path, required=True)
    parser.add_argument("--candidate-index", type=int, default=0)
    parser.add_argument("--diagnostic", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    return parser.parse_args()


def analyze(audit: dict[str, Any], diagnostic: dict[str, Any], candidate_index: int) -> dict[str, Any]:
    candidate = audit["candidates"][candidate_index]
    nodes = {int(node["tree_node_id"]): node for node in diagnostic["preflop_nodes"].values()}
    nodes_by_history = {
        history_key(node["history"]): node for node in diagnostic["preflop_nodes"].values()
    }
    summary = {
        "averageMaterialRows": 0,
        "averageMaterialRowsOwnReachAtLeastOnePercent": 0,
        "averageMaterialRowsPublicReachAtLeastOnePercent": 0,
        "currentMaterialRows": 0,
        "currentMaterialRowsOwnReachAtLeastOnePercent": 0,
        "currentMaterialRowsPublicReachAtLeastOnePercent": 0,
        "averageMaterialRowsCurrentMovesToBest": 0,
    }
    cases: list[dict[str, Any]] = []
    for audited_node in candidate["nodes"]:
        node_id = audited_node["nodeId"]
        node = nodes[node_id]
        for hand, exact in audited_node["rows"].items():
            inferior_action = exact["inferiorAction"]
            gap = abs(exact["exactCallMinusFoldAnte"])
            average_inferior_frequency = exact["inferiorFrequency"]
            current_inferior_frequency = (
                node["current_strategy"][hand][inferior_action]
                if inferior_action != "none"
                else 0.0
            )
            average_material = average_inferior_frequency >= 0.05 and gap >= 0.1
            current_material = current_inferior_frequency >= 0.05 and gap >= 0.1
            average_opponent_reach = opponent_sequence_reach(
                hand,
                audited_node["player"],
                audited_node["history"],
                nodes_by_history,
            )
            current_own_reach = own_sequence_reach(
                hand,
                audited_node["player"],
                audited_node["history"],
                nodes_by_history,
                "current_strategy",
            )
            current_opponent_reach = opponent_sequence_reach(
                hand,
                audited_node["player"],
                audited_node["history"],
                nodes_by_history,
                "current_strategy",
            )
            average_public_reach = exact["ownSequenceReach"] * average_opponent_reach
            current_public_reach = current_own_reach * current_opponent_reach
            summary["averageMaterialRows"] += int(average_material)
            summary["averageMaterialRowsOwnReachAtLeastOnePercent"] += int(
                average_material and exact["ownSequenceReach"] >= 0.01
            )
            summary["averageMaterialRowsPublicReachAtLeastOnePercent"] += int(
                average_material and average_public_reach >= 0.01
            )
            summary["currentMaterialRows"] += int(current_material)
            summary["currentMaterialRowsOwnReachAtLeastOnePercent"] += int(
                current_material and current_own_reach >= 0.01
            )
            summary["currentMaterialRowsPublicReachAtLeastOnePercent"] += int(
                current_material and current_public_reach >= 0.01
            )
            summary["averageMaterialRowsCurrentMovesToBest"] += int(
                average_material and current_inferior_frequency < 0.05
            )
            if not (average_material or current_material):
                continue
            training = node["training_state"][hand]
            cases.append(
                {
                    "nodeId": node_id,
                    "hand": hand,
                    "inferiorAction": inferior_action,
                    "averageInferiorFrequency": average_inferior_frequency,
                    "currentInferiorFrequency": current_inferior_frequency,
                    "averageMaterial": average_material,
                    "currentMaterial": current_material,
                    "exactEvGapAnte": gap,
                    "ownSequenceReach": exact["ownSequenceReach"],
                    "averageOpponentSequenceReach": average_opponent_reach,
                    "averagePublicSequenceReach": average_public_reach,
                    "currentOwnSequenceReach": current_own_reach,
                    "currentOpponentSequenceReach": current_opponent_reach,
                    "currentPublicSequenceReach": current_public_reach,
                    "averageRealizationWeightedLossAnte": exact[
                        "realizationWeightedLocalLossAnte"
                    ],
                    "lastUpdateIteration": training["last_update_iteration"],
                    "callCumulativeRegret": training["actions"]["call"][
                        "cumulative_weighted_regret"
                    ],
                    "foldCumulativeRegret": training["actions"]["fold"][
                        "cumulative_weighted_regret"
                    ],
                    "averageStrategy": node["strategy"][hand],
                    "currentStrategy": node["current_strategy"][hand],
                }
            )
    cases.sort(key=lambda case: case["averageRealizationWeightedLossAnte"], reverse=True)
    return {
        "schema": "gtosd.hu_preflop_all_in_training_diagnostic.v1",
        "treeFingerprint": audit["treeFingerprint"],
        "exactEquityTableFingerprint": audit["exactEquityTableFingerprint"],
        "candidate": candidate["source"],
        "diagnostic": diagnostic.get("algorithm"),
        "iterations": diagnostic["iterations"],
        "summary": summary,
        "cases": cases,
    }


def main() -> int:
    args = parse_args()
    audit = json.loads(args.audit.read_text(encoding="utf-8"))
    diagnostic = json.loads(args.diagnostic.read_text(encoding="utf-8"))
    result = analyze(audit, diagnostic, args.candidate_index)
    if args.output:
        args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(
        "HU_PREFLOP_ALL_IN_TRAINING_DIAGNOSTIC=PASS"
        f" average_material={result['summary']['averageMaterialRows']}"
        f" average_public_on_path="
        f"{result['summary']['averageMaterialRowsPublicReachAtLeastOnePercent']}"
        f" current_material={result['summary']['currentMaterialRows']}"
        f" current_on_path={result['summary']['currentMaterialRowsOwnReachAtLeastOnePercent']}"
        f" current_public_on_path={result['summary']['currentMaterialRowsPublicReachAtLeastOnePercent']}"
        f" current_moves_to_best={result['summary']['averageMaterialRowsCurrentMovesToBest']}"
    )
    for case in result["cases"][:10]:
        print(
            f"node={case['nodeId']} hand={case['hand']} inferior={case['inferiorAction']}"
            f" average={case['averageInferiorFrequency']:.6f}"
            f" current={case['currentInferiorFrequency']:.6f}"
            f" gap={case['exactEvGapAnte']:.6f}"
            f" own_reach={case['ownSequenceReach']:.6f}"
            f" current_own_reach={case['currentOwnSequenceReach']:.6f}"
            f" current_opponent_reach={case['currentOpponentSequenceReach']:.6f}"
            f" current_public_reach={case['currentPublicSequenceReach']:.6f}"
            f" last_update={case['lastUpdateIteration']}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
