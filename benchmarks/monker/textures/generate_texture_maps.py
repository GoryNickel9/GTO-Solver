"""Writes the turn texture maps of the MonkerSolver-style board class rows
(format gtosd-board-texture-v1, read by BoardTextureMap::load, trainer option
--board-texture-map).

The canonical flop and flop+turn catalogs are reimplemented from
libs/card_abstraction/src/canonical_boards.cpp: card value = rank * 4 + suit
(rank 0..8 = 6 7 8 9 T J Q K A, suit 0..3 = c d h s); a flop code packs the
sorted suit-permuted card values (6 bits per card, smallest first in the high
bits), a flop+turn code is flop code << 6 | turn value; the canonical code is
the minimum over the 24 suit permutations and the catalog index its position
among the sorted codes. BoardTextureMap::load checks every code against the
catalog, so a different order is rejected.

Every rule reads only ranks, suit counts and which ranks share a suit, so it
is a function of the canonical representative and suit-permuted boards get
the same class. Classes are numbered by first appearance in catalog order.
The flop section is the identity and the river follows the turn (river key
"turn").

Rules (key of a canonical flop+turn entry):
  TX0_suit_only    flop texture x exact turn rank x flop cards sharing the turn's suit
  TX1_fallback     flop texture x rank relation x flop cards sharing the turn's suit x straight effect
  TX2_recommended  flop texture x rank relation x flush now x straight effect
  TX3_aggressive   flop texture x which card the turn pairs x flush now x (min(d1, 1), d3)
where flop texture = flop ranks x flop suit pattern (merges the three suit
variants of an unpaired two-tone flop), rank relation = which flop rank the
turn pairs, or the gap index among the distinct flop ranks with an
upper/lower half split in gaps of 3+ free ranks, flush now = largest suit
count of the turn board if 3 or 4, and straight effect = (windows newly
holding 3+ board ranks capped at 2, two-card straight rank pairs bucketed
0 / 1-2 / 3-4 / 5+, single ranks completing a straight capped at 2).

Usage: python generate_texture_maps.py [--out-dir DIR] [--check]
  --check compares the regenerated files with the ones in the output
  directory (default: this script's directory) instead of writing them.
Takes a few seconds; the files are byte-identical to the 28 September 2026
originals (sha256 in README.md).
"""
import argparse
import itertools
import os
import sys
from collections import Counter

DECK = 36
PERMUTATIONS = list(itertools.permutations(range(4)))
IMAGES = [[(card // 4) * 4 + permutation[card % 4] for card in range(DECK)]
          for permutation in PERMUTATIONS]
# Straight windows: A-6-7-8-9 (the ace low) and 6-T .. T-A.
WINDOWS = [(8, 0, 1, 2, 3)] + [tuple(range(low, low + 5)) for low in range(5)]


def pack(values):
    code = 0
    for value in values:
        code = (code << 6) | value
    return code


def unpack(code, count):
    return [(code >> (6 * (count - 1 - index))) & 63 for index in range(count)]


def canonical_flop(cards):
    return min(pack(sorted(image[card] for card in cards)) for image in IMAGES)


def canonical_flop_turn(flop, turn):
    return min((pack(sorted(image[card] for card in flop)) << 6) | image[turn] for image in IMAGES)


def catalog():
    """Canonical flops and flop+turn entries in catalog order."""
    flop_codes = sorted({canonical_flop(cards) for cards in itertools.combinations(range(DECK), 3)})
    assert len(flop_codes) == 573, len(flop_codes)
    flop_index = {code: index for index, code in enumerate(flop_codes)}
    # Every flop+turn orbit contains an element whose flop is the canonical flop.
    turn_codes = set()
    for code in flop_codes:
        flop = unpack(code, 3)
        for turn in range(DECK):
            if turn not in flop:
                turn_codes.add(canonical_flop_turn(flop, turn))
    turn_codes = sorted(turn_codes)
    assert len(turn_codes) == 13761, len(turn_codes)
    turns = [dict(code=code, flop=unpack(code >> 6, 3), turn=code & 63,
                  flop_index=flop_index[code >> 6]) for code in turn_codes]
    return flop_codes, turns


def ranks_of(cards):
    return [card // 4 for card in cards]


def suit_counts(cards):
    return Counter(card % 4 for card in cards)


def flop_texture(flop):
    return (tuple(sorted(ranks_of(flop), reverse=True)),
            tuple(sorted(suit_counts(flop).values(), reverse=True)))


def turn_rank_relation(flop, turn):
    ranks = ranks_of(flop)
    rank = turn // 4
    distinct = sorted(set(ranks), reverse=True)
    count = Counter(ranks)
    if rank in count:
        if len(distinct) == 3:
            return ("pair", ("top", "mid", "low")[distinct.index(rank)])
        if len(distinct) == 2:
            return ("pair", "trips_on_board" if count[rank] == 2 else "second_pair")
        return ("pair", "quads_on_board")
    return ("gap", sum(1 for other in distinct if other > rank))


def turn_rank_relation_half(flop, turn, minimum_width=3):
    kind, what = turn_rank_relation(flop, turn)
    if kind == "pair":
        return (kind, what)
    distinct = sorted(set(ranks_of(flop)), reverse=True)
    high = distinct[what - 1] if what > 0 else 9
    low = distinct[what] if what < len(distinct) else -1
    if high - low - 1 >= minimum_width:
        return (kind, what, "upper" if turn // 4 > (high + low) / 2 else "lower")
    return (kind, what)


def turn_suit_effect(flop, turn):
    return sum(1 for card in flop if card % 4 == turn % 4)


def flush_now(flop, turn):
    top = max(suit_counts(flop + [turn]).values())
    return top if top >= 3 else 0


def window_counts(rank_set):
    return [len(set(window) & rank_set) for window in WINDOWS]


def makes_straight(ranks):
    return any(len(set(window) & ranks) == 5 for window in WINDOWS)


def single_card_straight_ranks(rank_set):
    count = sum(1 for rank in range(9) if rank not in rank_set and makes_straight(rank_set | {rank}))
    return min(count, 2)


def two_card_straight_pairs(rank_set):
    free = [rank for rank in range(9) if rank not in rank_set]
    alone = {rank for rank in free if makes_straight(rank_set | {rank})}
    count = sum(1 for index, first in enumerate(free) for second in free[index + 1:]
                if first not in alone and second not in alone
                and makes_straight(rank_set | {first, second}))
    return 0 if count == 0 else 1 if count <= 2 else 2 if count <= 4 else 3


def turn_straight_effect(flop, turn):
    before = window_counts(set(ranks_of(flop)))
    rank_set = set(ranks_of(flop)) | {turn // 4}
    after = window_counts(rank_set)
    completes = sum(1 for old, new in zip(before, after) if new >= 3 > old)
    return (min(completes, 2), two_card_straight_pairs(rank_set),
            single_card_straight_ranks(rank_set))


def rule_tx0(entry):
    flop, turn = entry["flop"], entry["turn"]
    return (flop_texture(flop), turn // 4, turn_suit_effect(flop, turn))


def rule_tx1(entry):
    flop, turn = entry["flop"], entry["turn"]
    return (flop_texture(flop), turn_rank_relation_half(flop, turn),
            turn_suit_effect(flop, turn), turn_straight_effect(flop, turn))


def rule_tx2(entry):
    flop, turn = entry["flop"], entry["turn"]
    return (flop_texture(flop), turn_rank_relation_half(flop, turn), flush_now(flop, turn),
            turn_straight_effect(flop, turn))


def rule_tx3(entry):
    flop, turn = entry["flop"], entry["turn"]
    kind, what = turn_rank_relation(flop, turn)
    completes, _, singles = turn_straight_effect(flop, turn)
    return (flop_texture(flop), what if kind == "pair" else "no_pair", flush_now(flop, turn),
            (min(completes, 1), singles))


# file name, name line, rule (None = identity), expected turn classes
MAPS = [
    ("identity_texture_map.txt", "identity", None, 13761),
    ("texture_map_TX0_suit_only.txt", "TX0_suit_only", rule_tx0, 8217),
    ("texture_map_TX1_fallback.txt", "TX1_fallback", rule_tx1, 6768),
    ("texture_map_TX2_recommended.txt", "TX2_recommended", rule_tx2, 4482),
    ("texture_map_TX3_aggressive.txt", "TX3_aggressive", rule_tx3, 2680),
]


def first_appearance(keys):
    labels = {}
    return [labels.setdefault(key, len(labels)) for key in keys]


def map_text(name, flop_codes, turns, turn_classes):
    lines = ["gtosd-board-texture-v1", "name " + name, "river-key turn",
             "flop %d" % len(flop_codes)]
    lines += ["%d %d" % (code, index) for index, code in enumerate(flop_codes)]
    for section in ("turn", "river"):
        lines.append("%s %d" % (section, len(turns)))
        lines += ["%d %d" % (entry["code"], board_class)
                  for entry, board_class in zip(turns, turn_classes)]
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--out-dir", default=os.path.dirname(os.path.abspath(__file__)))
    parser.add_argument("--check", action="store_true")
    arguments = parser.parse_args()
    flop_codes, turns = catalog()
    failed = False
    for file_name, name, rule, expected in MAPS:
        keys = range(len(turns)) if rule is None else [rule(entry) for entry in turns]
        turn_classes = first_appearance(keys)
        classes = max(turn_classes) + 1
        assert classes == expected, (name, classes, expected)
        text = map_text(name, flop_codes, turns, turn_classes)
        path = os.path.join(arguments.out_dir, file_name)
        if arguments.check:
            with open(path, "rb") as existing:
                same = existing.read().replace(b"\r\n", b"\n") == text.encode("ascii")
            failed = failed or not same
            print("%-34s %5d turn classes  %s" % (file_name, classes, "same" if same else "DIFFERENT"))
        else:
            with open(path, "w", newline="\n", encoding="ascii") as output:
                output.write(text)
            print("%-34s %5d turn classes  written" % (file_name, classes))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
