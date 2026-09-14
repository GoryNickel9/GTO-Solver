"""Integer reference oracle for bucket terminal masses, not a poker solver.

Synthetic shared hand ranks isolate compatibility from hand evaluation. The
oracle enumerates every legal private pair; the candidate uses rank/card prefix
sums. Equality establishes terminal mass identities only, not Nash convergence,
abstraction quality, native performance, or a production implementation.
"""

from __future__ import annotations

import argparse
from collections import defaultdict
from itertools import combinations
import json
from pathlib import Path
import random


DECK_SIZE = 36
BOARD = frozenset((0, 7, 14, 21, 28))
HANDS = list(combinations([card for card in range(DECK_SIZE) if card not in BOARD], 2))
MASKS = [(1 << first) | (1 << second) for first, second in HANDS]
# These are deliberately synthetic labels, shared between the two players.
RANKS = [(37 * first + 13 * second) % 17 for first, second in HANDS]


def explicit_pairs(own: list[int], opponent: list[int], groups: list[tuple]) -> dict:
    masses = {group: [0, 0, 0] for group in groups}
    for first, own_weight in enumerate(own):
        if not own_weight:
            continue
        for second, opponent_weight in enumerate(opponent):
            if not opponent_weight or MASKS[first] & MASKS[second]:
                continue
            outcome = (0 if RANKS[first] < RANKS[second] else
                       2 if RANKS[first] > RANKS[second] else 1)
            masses[groups[first]][outcome] += own_weight * opponent_weight
    return masses


def factorized(own: list[int], opponent: list[int], groups: list[tuple]) -> dict:
    rank_count = max(RANKS) + 1
    by_rank = [0] * rank_count
    by_card = [[0] * DECK_SIZE for _ in range(rank_count)]
    for hand, weight in enumerate(opponent):
        rank = RANKS[hand]
        by_rank[rank] += weight
        for card in HANDS[hand]:
            by_card[rank][card] += weight
    prefix = [0]
    card_prefix = [[0] * DECK_SIZE]
    for rank in range(rank_count):
        prefix.append(prefix[-1] + by_rank[rank])
        card_prefix.append([card_prefix[-1][card] + by_card[rank][card]
                            for card in range(DECK_SIZE)])

    members = defaultdict(list)
    for hand, group in enumerate(groups):
        members[group].append(hand)
    result = {}
    for group, hands in members.items():
        ranks = {RANKS[hand] for hand in hands}
        if len(ranks) != 1:
            raise ValueError("This factorization requires a single rank per bucket")
        rank = next(iter(ranks))
        mass = sum(own[hand] for hand in hands)
        cards = defaultdict(int)
        for hand in hands:
            for card in HANDS[hand]:
                cards[card] += own[hand]
        # The two shared cards of an identical combo are subtracted twice.
        # Add its product once so an impossible identical deal has zero mass.
        identical = sum(own[hand] * opponent[hand] for hand in hands)
        win = mass * prefix[rank] - sum(
            weight * card_prefix[rank][card] for card, weight in cards.items())
        tie = mass * by_rank[rank] - sum(
            weight * by_card[rank][card] for card, weight in cards.items()) + identical
        loss = mass * (prefix[-1] - prefix[rank + 1]) - sum(
            weight * (card_prefix[-1][card] - card_prefix[rank + 1][card])
            for card, weight in cards.items())
        result[group] = [loss, tie, win]
    return result


def run() -> dict:
    rng = random.Random(20260910)
    count = len(HANDS)
    shared = ([0] * count, [0] * count)
    shared[0][0], shared[1][0] = 10000, 7001
    scenarios = {
        "uniform": ([10000] * count, [10000] * count),
        "weighted_asymmetric": ([rng.randrange(10001) for _ in HANDS],
                                [rng.randrange(10001) for _ in HANDS]),
        "sparse_asymmetric": ([rng.randrange(10001) if i % 3 else 0 for i in range(count)],
                              [rng.randrange(10001) if i % 5 == 0 else 0 for i in range(count)]),
        "zero_weight_boundary": ([0] * count, [10000] * count),
        "identical_only_boundary": shared,
        "disjoint_range_support": ([10000 if i % 2 else 0 for i in range(count)],
                                   [0 if i % 2 else 3507 for i in range(count)]),
    }
    cases = []
    comparisons = 0
    for name, weights in scenarios.items():
        for player in (0, 1):
            # Player-specific partitions retain shared ranks but differ in membership.
            groups = [(RANKS[i], (hand[0] + hand[1] + player) % (player + 2))
                      for i, hand in enumerate(HANDS)]
            opponent_groups = [(RANKS[i], (hand[0] + hand[1] + 1 - player) % (3 - player))
                               for i, hand in enumerate(HANDS)]
            reach = {group: rng.randrange(1, 10001) for group in sorted(set(opponent_groups))}
            opponent = [weights[1 - player][i] * reach[opponent_groups[i]] for i in range(count)]
            direct = explicit_pairs(weights[player], opponent, groups)
            candidate = factorized(weights[player], opponent, groups)
            if direct != candidate or any(value < 0 for values in candidate.values() for value in values):
                raise AssertionError(f"Terminal compatibility mismatch: {name}, player {player}")
            comparisons += 3 * len(candidate)
            cases.append({"case": name, "player": player, "buckets": len(candidate),
                          "mass_by_outcome": [sum(values[outcome] for values in candidate.values())
                                              for outcome in range(3)],
                          "maximum_integer_error": 0, "passed": True})
    return {"schema": "gtosd.bucket_blocker_factorization_oracle.v1",
            "scope": "synthetic_rank_integer_terminal_mass_identity_only",
            "seed": 20260910, "deck_size": DECK_SIZE, "board": sorted(BOARD),
            "legal_private_combos": count, "scalar_mass_comparisons": comparisons,
            "native_timing_measured": False, "nash_convergence_measured": False,
            "cases": cases, "passed": True}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = run()
    with args.output.open("x", encoding="utf-8") as stream:
        json.dump(result, stream, indent=2, allow_nan=False)
    print(f"PASS: {len(result['cases'])} scenario/player combinations; "
          f"{result['scalar_mass_comparisons']} exact integer mass comparisons; "
          "no performance or convergence claim")


if __name__ == "__main__":
    main()
