"""Cards, subsets, combos and preflop classes of the 36-card short deck.

Rules (game facts):
- The deck has 36 cards: the ranks 6, 7, 8, 9, T, J, Q, K, A in each of the four suits.
- Suits have no order; a suit permutation maps the game onto itself.
- A hole-card combo is an unordered pair of distinct cards: C(36, 2) = 630 combos.
- A preflop class is a combo up to suit permutation: 9 pairs (6 combos each), 36 suited hands (4 each) and
  36 offsuit hands (12 each), 81 classes and 9 * 6 + 36 * 4 + 36 * 12 = 630 combos.

Conventions (engine choices, read from the C++ headers because the resource files and the JSON outputs are
indexed by them; they are not rules):
- C1 card index = rank * 4 + suit, rank 0 = six .. 8 = ace, suit 0 = clubs, 1 = diamonds, 2 = hearts,
  3 = spades (include/gtosd/core/cards.hpp, CardId::from_parts).
- C2 a k-subset of cards is indexed by its colex rank: with the cards sorted c_0 < c_1 < ... < c_{k-1},
  index = sum_i C(c_i, i + 1) (combinatorics.hpp, subset_index). The 5- and 7-card tables of
  rank_table_v1.bin and the boards of the all-in build use it.
- C3 combo order = all pairs (a, b), a < b, in lexicographic order (0,1), (0,2), .., (34,35)
  (ranges.cpp all_combos, combinatorics.hpp combo_index).
- C4 unordered combo pairs {i, j}, i < j, are indexed triangularly: i * (2 * 630 - i - 1) / 2 + (j - i - 1)
  (all_in_table.hpp combo_pair_index), 198,135 entries.
- C5 class order: the 9 pairs AA, KK, .., 66 (ids 0-8), then the 36 suited hands and then the 36 offsuit
  hands (ids 9-44 and 45-80), each block ordered by high rank descending and, within it, by low rank
  descending (AK, AQ, .., A6, KQ, .., 76) (ranges.cpp hand_class / class_name). Names are "AA", "AKs",
  "AKo".
"""

from __future__ import annotations

from itertools import combinations, permutations
from math import comb

import numpy as np

RANK_CHARS = "6789TJQKA"  # rank index 0 .. 8
SUIT_CHARS = "cdhs"  # suit index 0 .. 3 (convention C1)
RANK_COUNT = 9
SUIT_COUNT = 4
DECK_SIZE = 36
COMBO_COUNT = 630
CLASS_COUNT = 81
COMBO_PAIR_COUNT = COMBO_COUNT * (COMBO_COUNT - 1) // 2  # 198,135 triangular entries
DISJOINT_COMBO_PAIR_COUNT = comb(36, 2) * comb(34, 2) // 2  # 176,715
FIVE_CARD_SET_COUNT = comb(36, 5)  # 376,992
SEVEN_CARD_SET_COUNT = comb(36, 7)  # 8,347,680
ALL_IN_RUNOUT_COUNT = comb(32, 5)  # 201,376 boards once four hole cards are dealt

# ---------------------------------------------------------------- single cards


def make_card(rank: int, suit: int) -> int:
    if not (0 <= rank < RANK_COUNT and 0 <= suit < SUIT_COUNT):
        raise ValueError(f"invalid card rank={rank} suit={suit}")
    return rank * SUIT_COUNT + suit


def card_rank(card: int) -> int:
    return card // SUIT_COUNT


def card_suit(card: int) -> int:
    return card % SUIT_COUNT


def parse_card(text: str) -> int:
    """'Ah', 'tc', '6s' -> card index."""
    if len(text) != 2 or text[0].upper() not in RANK_CHARS or text[1].lower() not in SUIT_CHARS:
        raise ValueError(f"invalid card text {text!r}")
    return make_card(RANK_CHARS.index(text[0].upper()), SUIT_CHARS.index(text[1].lower()))


def parse_cards(text: str) -> list[int]:
    """'AhKd' or 'Ah Kd' or 'Ah,Kd' -> list of card indices."""
    compact = text.replace(" ", "").replace(",", "")
    if len(compact) % 2:
        raise ValueError(f"invalid card list {text!r}")
    return [parse_card(compact[i:i + 2]) for i in range(0, len(compact), 2)]


def format_card(card: int) -> str:
    return RANK_CHARS[card_rank(card)] + SUIT_CHARS[card_suit(card)]


def format_cards(cards) -> str:
    return "".join(format_card(int(card)) for card in cards)


def cards_mask(cards) -> int:
    mask = 0
    for card in cards:
        mask |= 1 << int(card)
    return mask


# ---------------------------------------------------------------- suit symmetry

# All 24 suit permutations; perm[s] is the image of suit s.
SUIT_PERMUTATIONS: tuple[tuple[int, ...], ...] = tuple(permutations(range(SUIT_COUNT)))


def permute_card(card: int, perm) -> int:
    return card_rank(card) * SUIT_COUNT + perm[card_suit(card)]


def card_permutation_table() -> np.ndarray:
    """(24, 36) uint8: row p maps every card through SUIT_PERMUTATIONS[p]."""
    table = np.empty((len(SUIT_PERMUTATIONS), DECK_SIZE), dtype=np.uint8)
    for p, perm in enumerate(SUIT_PERMUTATIONS):
        for card in range(DECK_SIZE):
            table[p, card] = permute_card(card, perm)
    return table


# ---------------------------------------------------------------- colex subsets (convention C2)

# BINOMIAL[n, k] = C(n, k) for n <= 36, k <= 7 (int64).
BINOMIAL = np.array([[comb(n, k) for k in range(8)] for n in range(DECK_SIZE + 1)], dtype=np.int64)


def colex_index(cards) -> int:
    """Colex rank of a set of distinct cards (any order)."""
    ordered = sorted(int(card) for card in cards)
    if len(set(ordered)) != len(ordered):
        raise ValueError("duplicate card")
    return sum(comb(card, position + 1) for position, card in enumerate(ordered))


def subset_from_colex(index: int, size: int) -> list[int]:
    """Inverse of colex_index for k = size; returns the sorted cards."""
    if not 0 <= index < comb(DECK_SIZE, size):
        raise ValueError("colex index out of range")
    cards = [0] * size
    upper = DECK_SIZE
    for position in range(size, 0, -1):
        value = upper - 1
        while comb(value, position) > index:
            value -= 1
        cards[position - 1] = value
        index -= comb(value, position)
        upper = value
    return cards


def colex_subsets(size: int, universe: int = DECK_SIZE) -> np.ndarray:
    """All size-subsets of range(universe), sorted ascending within a row, rows in colex order.

    Row r is the subset whose colex rank is r. Built by the colex recursion: the subsets whose largest
    element is m are the (size-1)-subsets of range(m) (a colex prefix) followed by m.
    """
    if size < 1 or size > universe:
        raise ValueError("invalid subset size")
    level = np.arange(universe, dtype=np.uint8).reshape(universe, 1)
    for k in range(2, size + 1):
        blocks = []
        for largest in range(k - 1, universe):
            prefix = level[:comb(largest, k - 1)]
            block = np.empty((prefix.shape[0], k), dtype=np.uint8)
            block[:, :k - 1] = prefix
            block[:, k - 1] = largest
            blocks.append(block)
        level = np.concatenate(blocks, axis=0)
    return level


def colex_of_sorted_rows(rows: np.ndarray) -> np.ndarray:
    """Colex ranks (int64) of rows that are sorted ascending (shape (n, k))."""
    rows = np.asarray(rows)
    index = np.zeros(rows.shape[0], dtype=np.int64)
    for position in range(rows.shape[1]):
        index += BINOMIAL[rows[:, position].astype(np.int64), position + 1]
    return index


# ---------------------------------------------------------------- combos (conventions C3, C4)


def all_combos() -> list[tuple[int, int]]:
    """The 630 combos (a, b), a < b, in engine order (convention C3)."""
    return [(a, b) for a in range(DECK_SIZE) for b in range(a + 1, DECK_SIZE)]


COMBOS: np.ndarray = np.array(all_combos(), dtype=np.uint8)  # (630, 2)


def combo_index(first: int, second: int) -> int:
    """Index of the combo {first, second} in COMBOS (convention C3)."""
    a, b = (first, second) if first < second else (second, first)
    if a == b or not (0 <= a and b < DECK_SIZE):
        raise ValueError("invalid combo")
    # combos starting with a card below a: sum_{x < a} (35 - x)
    return a * (DECK_SIZE - 1) - a * (a - 1) // 2 + (b - a - 1)


def _combo_index_table() -> np.ndarray:
    table = np.full((DECK_SIZE, DECK_SIZE), -1, dtype=np.int32)
    for index, (a, b) in enumerate(all_combos()):
        table[a, b] = index
        table[b, a] = index
    return table


COMBO_INDEX: np.ndarray = _combo_index_table()  # (36, 36), -1 on the diagonal


def combo_pair_index(i: int, j: int) -> int:
    """Triangular index of the unordered combo pair {i, j}, i != j (convention C4)."""
    if i == j:
        raise ValueError("a combo pair needs two different combos")
    a, b = (i, j) if i < j else (j, i)
    return a * (2 * COMBO_COUNT - a - 1) // 2 + (b - a - 1)


def combo_pair_arrays() -> tuple[np.ndarray, np.ndarray]:
    """(first, second) int32 arrays of length 198,135 in triangular order: entry t is the pair with
    combo_pair_index(first[t], second[t]) == t and first[t] < second[t]."""
    first, second = np.triu_indices(COMBO_COUNT, k=1)
    return first.astype(np.int32), second.astype(np.int32)


def combos_disjoint_matrix() -> np.ndarray:
    """(630, 630) bool: True when the two combos share no card (False on the diagonal)."""
    masks = np.zeros((COMBO_COUNT, DECK_SIZE), dtype=bool)
    masks[np.arange(COMBO_COUNT), COMBOS[:, 0]] = True
    masks[np.arange(COMBO_COUNT), COMBOS[:, 1]] = True
    shared = masks.astype(np.int16) @ masks.T.astype(np.int16)
    return shared == 0


def format_combo(index: int) -> str:
    a, b = COMBOS[index]
    return format_card(int(b)) + format_card(int(a))  # higher card first


# ---------------------------------------------------------------- preflop classes (convention C5)


def _class_names() -> list[str]:
    names = [RANK_CHARS[r] * 2 for r in range(RANK_COUNT - 1, -1, -1)]
    for suffix in ("s", "o"):
        for high in range(RANK_COUNT - 1, -1, -1):
            for low in range(high - 1, -1, -1):
                names.append(RANK_CHARS[high] + RANK_CHARS[low] + suffix)
    return names


CLASS_NAMES: tuple[str, ...] = tuple(_class_names())
CLASS_ID: dict[str, int] = {name: index for index, name in enumerate(CLASS_NAMES)}


def class_of_cards(first: int, second: int) -> int:
    """Class id of the combo {first, second} (convention C5)."""
    high, low = sorted((card_rank(first), card_rank(second)), reverse=True)
    if high == low:
        name = RANK_CHARS[high] * 2
    else:
        suffix = "s" if card_suit(first) == card_suit(second) else "o"
        name = RANK_CHARS[high] + RANK_CHARS[low] + suffix
    return CLASS_ID[name]


def class_name(class_id: int) -> str:
    return CLASS_NAMES[class_id]


def class_mass(class_id: int) -> int:
    """Number of combos in the class: 6 for a pair, 4 suited, 12 offsuit."""
    name = CLASS_NAMES[class_id]
    if len(name) == 2:
        return 6
    return 4 if name.endswith("s") else 12


COMBO_CLASS: np.ndarray = np.array([class_of_cards(a, b) for a, b in all_combos()], dtype=np.int16)
COMBO_LABELS: tuple[str, ...] = tuple(CLASS_NAMES[c] for c in COMBO_CLASS)
CLASS_COMBOS: tuple[tuple[int, ...], ...] = tuple(
    tuple(int(i) for i in np.flatnonzero(COMBO_CLASS == c)) for c in range(CLASS_COUNT))


def self_check() -> None:
    """Cheap invariants of this module (raises AssertionError)."""
    assert len(CLASS_NAMES) == CLASS_COUNT and len(set(CLASS_NAMES)) == CLASS_COUNT
    assert COMBOS.shape == (COMBO_COUNT, 2)
    assert all(combo_index(int(a), int(b)) == i for i, (a, b) in enumerate(COMBOS))
    assert sum(class_mass(c) for c in range(CLASS_COUNT)) == COMBO_COUNT
    assert all(len(CLASS_COMBOS[c]) == class_mass(c) for c in range(CLASS_COUNT))
    first, second = combo_pair_arrays()
    assert first.shape[0] == COMBO_PAIR_COUNT
    probe = np.arange(0, COMBO_PAIR_COUNT, 997)
    assert all(combo_pair_index(int(first[t]), int(second[t])) == t for t in probe)
    assert int(np.triu(combos_disjoint_matrix(), 1).sum()) == DISJOINT_COMBO_PAIR_COUNT
    for size in (1, 2, 3):
        rows = colex_subsets(size)
        assert rows.shape[0] == comb(DECK_SIZE, size)
        assert np.array_equal(colex_of_sorted_rows(rows), np.arange(rows.shape[0]))
    for index in (0, 1, 12345, FIVE_CARD_SET_COUNT - 1):
        assert colex_index(subset_from_colex(index, 5)) == index
    for combo in combinations(range(DECK_SIZE), 2):
        assert COMBO_INDEX[combo] == combo_index(*combo)
