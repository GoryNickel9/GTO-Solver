#!/usr/bin/env python3
"""Differential-test the solver all-in table against Poker-Quant exact HU equity."""

from __future__ import annotations

import argparse
import hashlib
import itertools
import json
import math
from pathlib import Path
from typing import Any


RANKS = "6789TJQKA"
RANK_TO_VALUE = {rank: value for value, rank in enumerate(RANKS, start=6)}
VALUE_TO_RANK = {value: rank for rank, value in RANK_TO_VALUE.items()}
SUITS = range(4)
SUIT_PERMUTATIONS = tuple(itertools.permutations(SUITS))
EXPECTED_CANONICAL_MATCHUPS = 10_215
EXPECTED_PHYSICAL_HANDS = 630
EXPECTED_ORDERED_MATCHUPS = 353_430


Card = tuple[int, int]
Hand = tuple[Card, Card]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--solver-audit", type=Path, required=True)
    parser.add_argument("--poker-quant-table", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--tolerance", type=float, default=1.0e-12)
    return parser.parse_args()


def hand_class(hand: Hand) -> str:
    first, second = sorted(hand, key=lambda card: card[0], reverse=True)
    if first[0] == second[0]:
        return VALUE_TO_RANK[first[0]] * 2
    suffix = "s" if first[1] == second[1] else "o"
    return VALUE_TO_RANK[first[0]] + VALUE_TO_RANK[second[0]] + suffix


def physical_hands() -> list[Hand]:
    cards = [(rank, suit) for rank in range(6, 15) for suit in SUITS]
    return list(itertools.combinations(cards, 2))


def canonical_matchup(hands: tuple[Hand, Hand]) -> tuple[str, tuple[int, int]]:
    best_hands: tuple[tuple[Card, Card], tuple[Card, Card]] | None = None
    best_order: tuple[int, int] | None = None
    for permutation in SUIT_PERMUTATIONS:
        players = []
        for original_index, hand in enumerate(hands):
            transformed = tuple(sorted((rank, permutation[suit]) for rank, suit in hand))
            players.append((transformed, original_index))
        players.sort(key=lambda item: item[0])
        candidate = tuple(item[0] for item in players)
        if best_hands is None or candidate < best_hands:
            best_hands = (candidate[0], candidate[1])
            best_order = (players[0][1], players[1][1])
    if best_hands is None or best_order is None:
        raise RuntimeError("failed to canonicalize physical matchup")
    encoded = [
        "".join(f"{VALUE_TO_RANK[rank]}{suit}" for rank, suit in hand)
        for hand in best_hands
    ]
    return "|".join(encoded), best_order


def load_poker_quant_table(path: Path) -> dict[str, tuple[float, float]]:
    rows: dict[str, tuple[float, float]] = {}
    for line_number, line in enumerate(path.read_text(encoding="ascii").splitlines(), start=1):
        key, raw_first, raw_second = line.split("\t")
        equities = (float(raw_first), float(raw_second))
        if (
            key in rows
            or any(not math.isfinite(value) or value < 0.0 or value > 1.0 for value in equities)
            or abs(sum(equities) - 1.0) > 1.0e-12
        ):
            raise ValueError(f"invalid Poker-Quant row {line_number}")
        rows[key] = equities
    if len(rows) != EXPECTED_CANONICAL_MATCHUPS:
        raise ValueError(f"Poker-Quant table has {len(rows)} rows")
    return rows


def aggregate_poker_quant(
    table: dict[str, tuple[float, float]],
) -> tuple[dict[tuple[str, str], float], dict[tuple[str, str], int], int]:
    hands = physical_hands()
    if len(hands) != EXPECTED_PHYSICAL_HANDS:
        raise RuntimeError("physical Short Deck hand count changed")
    equity_sums: dict[tuple[str, str], float] = {}
    counts: dict[tuple[str, str], int] = {}
    unordered_matchups = 0
    for first_index, first in enumerate(hands):
        first_cards = frozenset(first)
        first_class = hand_class(first)
        for second in hands[first_index + 1 :]:
            if not first_cards.isdisjoint(second):
                continue
            key, canonical_to_original = canonical_matchup((first, second))
            canonical_equities = table.get(key)
            if canonical_equities is None:
                raise ValueError(f"Poker-Quant table omits {key}")
            original_equities = [0.0, 0.0]
            for canonical_index, original_index in enumerate(canonical_to_original):
                original_equities[original_index] = canonical_equities[canonical_index]
            second_class = hand_class(second)
            for class_pair, equity in (
                ((first_class, second_class), original_equities[0]),
                ((second_class, first_class), original_equities[1]),
            ):
                equity_sums[class_pair] = equity_sums.get(class_pair, 0.0) + equity
                counts[class_pair] = counts.get(class_pair, 0) + 1
            unordered_matchups += 1
    if unordered_matchups * 2 != EXPECTED_ORDERED_MATCHUPS:
        raise RuntimeError(f"observed {unordered_matchups * 2} ordered physical matchups")
    return (
        {key: equity_sums[key] / counts[key] for key in equity_sums},
        counts,
        unordered_matchups,
    )


def compare(
    audit: dict[str, Any],
    poker_quant_equity: dict[tuple[str, str], float],
    physical_counts: dict[tuple[str, str], int],
    poker_quant_sha256: str,
    tolerance: float,
) -> dict[str, Any]:
    runouts = int(audit["orderedPublicRunoutsPerPrivateDeal"])
    solver_rows = {
        (row["responder"], row["opponent"]): row for row in audit["classEquities"]
    }
    if len(solver_rows) != 81 * 81 or set(solver_rows) != set(poker_quant_equity):
        raise ValueError("solver and Poker-Quant class-pair domains differ")
    absolute_errors = []
    count_mismatches = []
    maximum_case: dict[str, Any] | None = None
    for key, external_equity in poker_quant_equity.items():
        row = solver_rows[key]
        solver_equity = float(row["equity"])
        absolute_error = abs(solver_equity - external_equity)
        absolute_errors.append(absolute_error)
        logical_outcomes = int(row["wins"]) + int(row["ties"]) + int(row["losses"])
        if logical_outcomes != physical_counts[key] * runouts:
            count_mismatches.append(
                {"responder": key[0], "opponent": key[1], "logicalOutcomes": logical_outcomes}
            )
        if maximum_case is None or absolute_error > maximum_case["absoluteError"]:
            maximum_case = {
                "responder": key[0],
                "opponent": key[1],
                "solverEquity": solver_equity,
                "pokerQuantEquity": external_equity,
                "absoluteError": absolute_error,
            }
    cells_above_tolerance = sum(error > tolerance for error in absolute_errors)
    return {
        "schema": "gtosd.hu_preflop_all_in_equity_differential.v1",
        "solverEquityTableFingerprint": audit["exactEquityTableFingerprint"],
        "pokerQuantTableSha256": poker_quant_sha256,
        "canonicalPokerQuantMatchups": EXPECTED_CANONICAL_MATCHUPS,
        "physicalUnorderedPrivateMatchups": EXPECTED_ORDERED_MATCHUPS // 2,
        "classMatchups": len(absolute_errors),
        "tolerance": tolerance,
        "maximumAbsoluteEquityError": max(absolute_errors),
        "meanAbsoluteEquityError": sum(absolute_errors) / len(absolute_errors),
        "cellsAboveTolerance": cells_above_tolerance,
        "physicalCountMismatches": count_mismatches,
        "maximumCase": maximum_case,
        "passed": cells_above_tolerance == 0 and not count_mismatches,
    }


def main() -> int:
    args = parse_args()
    if not math.isfinite(args.tolerance) or args.tolerance < 0.0:
        raise ValueError("tolerance must be finite and non-negative")
    audit = json.loads(args.solver_audit.read_text(encoding="utf-8"))
    table = load_poker_quant_table(args.poker_quant_table)
    equities, counts, _ = aggregate_poker_quant(table)
    table_sha256 = hashlib.sha256(args.poker_quant_table.read_bytes()).hexdigest()
    result = compare(audit, equities, counts, table_sha256, args.tolerance)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(
        f"HU_PREFLOP_ALL_IN_EQUITY_DIFFERENTIAL={'PASS' if result['passed'] else 'FAIL'}"
        f" class_matchups={result['classMatchups']}"
        f" max_abs_error={result['maximumAbsoluteEquityError']:.17g}"
        f" cells_above_tolerance={result['cellsAboveTolerance']}"
        f" count_mismatches={len(result['physicalCountMismatches'])}"
    )
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
