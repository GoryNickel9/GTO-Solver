"""Short-deck hand ranking written from the rules, and the heads-up all-in counts built on it.

This module is the independent reference of S1 and S2 (coverage study shared_components.md). It was written
from the rules stated below, NOT by translating libs/equity/src/evaluator.cpp or
libs/card_abstraction/src/rank_table.cpp / all_in_table.cpp.

Rules of short-deck (36-card) hold'em showdowns, as the engine plays them:
- R1 A player's hand is the best five-card hand that can be formed from their two hole cards and the five
  board cards (any five of the seven).
- R2 Categories, best first: straight flush > four of a kind > flush > full house > straight > three of a
  kind > two pair > one pair > high card. A flush beats a full house (fewer flush combinations exist in a
  36-card deck).
- R3 Straight vs three of a kind is a switch (ORDER_STRAIGHT_OVER_TRIPS, the default and the engine rule,
  or ORDER_TRIPS_OVER_STRAIGHT, the rule the user's calculator README states while its code agrees with the
  engine). Every other category order is fixed.
- R4 Straights are five consecutive ranks among 6 7 8 9 T J Q K A, plus A-6-7-8-9 where the ace plays low:
  it is the lowest straight (its top card is the nine) and, suited, the lowest straight flush. There is no
  "wrap-around" beyond that (K-A-6-7-8 is not a straight).
- R5 Within a category hands compare by: straight / straight flush: the top card; four of a kind: the quad
  rank, then the kicker; full house: the trips rank, then the pair rank; flush / high card: the five ranks in
  descending order, lexicographically; three of a kind: the trips rank, then the two kickers descending; two
  pair: the higher pair, the lower pair, then the kicker; one pair: the pair rank, then the three kickers
  descending. Suits never break ties; equal keys split the pot.

Key encoding (a choice of this module): an int whose bits 20-23 hold the category value (its position in the
chosen order, 0 = high card) and bits 16-19, 12-15, 8-11, 4-7, 0-3 hold up to five tie-break ranks (rank
index 0 = six .. 8 = ace), unused slots 0. Comparing two keys as integers is comparing the hands.

Two independent 7-card paths are provided and cross-checked by sd_rank_check.py:
- seven_card_keys_from_five: the literal rule R1, the maximum over the 21 five-card subsets (numpy);
- evaluate_seven: a direct evaluator (rank / suit counts, best hand of each category, maximum).

Heads-up all-in counts (S2): for two disjoint combos i, j and the C(32,5) = 201,376 boards that avoid their
four cards, W = boards where i's hand beats j's, T = equal hands, L = j's beats i's.
"""

from __future__ import annotations

import multiprocessing
import sys
import time
from itertools import combinations
from math import comb
from pathlib import Path

import numpy as np

from . import cards as sdcards

# ---------------------------------------------------------------- categories and keys

HIGH_CARD = "high_card"
PAIR = "pair"
TWO_PAIR = "two_pair"
TRIPS = "trips"
STRAIGHT = "straight"
FULL_HOUSE = "full_house"
FLUSH = "flush"
QUADS = "quads"
STRAIGHT_FLUSH = "straight_flush"

ORDER_STRAIGHT_OVER_TRIPS = "straight"  # engine rule (default)
ORDER_TRIPS_OVER_STRAIGHT = "trips"

_WEAK_TO_STRONG = {
    ORDER_STRAIGHT_OVER_TRIPS: (HIGH_CARD, PAIR, TWO_PAIR, TRIPS, STRAIGHT, FULL_HOUSE, FLUSH, QUADS,
                                STRAIGHT_FLUSH),
    ORDER_TRIPS_OVER_STRAIGHT: (HIGH_CARD, PAIR, TWO_PAIR, STRAIGHT, TRIPS, FULL_HOUSE, FLUSH, QUADS,
                                STRAIGHT_FLUSH),
}
CATEGORIES = _WEAK_TO_STRONG[ORDER_STRAIGHT_OVER_TRIPS]


def category_values(order: str = ORDER_STRAIGHT_OVER_TRIPS) -> dict[str, int]:
    """Category name -> value (0 = weakest) under the chosen straight / trips order."""
    try:
        return {name: value for value, name in enumerate(_WEAK_TO_STRONG[order])}
    except KeyError:
        raise ValueError(f"unknown category order {order!r}") from None


def make_key(category: str, tiebreak, order: str = ORDER_STRAIGHT_OVER_TRIPS) -> int:
    values = category_values(order)
    key = values[category] << 20
    shift = 16
    for rank in tiebreak:
        key |= int(rank) << shift
        shift -= 4
    return key


def key_category(key: int, order: str = ORDER_STRAIGHT_OVER_TRIPS) -> str:
    return _WEAK_TO_STRONG[order][int(key) >> 20]


_TIEBREAK_SLOTS = {HIGH_CARD: 5, PAIR: 4, TWO_PAIR: 3, TRIPS: 3, STRAIGHT: 1, FULL_HOUSE: 2, FLUSH: 5,
                   QUADS: 2, STRAIGHT_FLUSH: 1}


def describe_key(key: int, order: str = ORDER_STRAIGHT_OVER_TRIPS) -> str:
    key = int(key)
    category = key_category(key, order)
    shifts = (16, 12, 8, 4, 0)[:_TIEBREAK_SLOTS[category]]
    ranks = "".join(sdcards.RANK_CHARS[(key >> shift) & 0xF] for shift in shifts)
    return f"{category}:{ranks}"


# The six straights (R4) as (rank set, top card), best first.
_STRAIGHTS = tuple([(frozenset(range(top - 4, top + 1)), top) for top in range(8, 3, -1)]
                   + [(frozenset((8, 0, 1, 2, 3)), 3)])


def straight_top(ranks_present) -> int | None:
    """Top card of the best straight contained in a set of rank indices, or None."""
    present = set(ranks_present)
    for straight, top in _STRAIGHTS:
        if straight <= present:
            return top
    return None


# ---------------------------------------------------------------- five-card evaluator (R2-R5)


def evaluate_five(cards, order: str = ORDER_STRAIGHT_OVER_TRIPS) -> int:
    """Key of exactly five distinct cards."""
    cards = [int(card) for card in cards]
    if len(cards) != 5 or len(set(cards)) != 5:
        raise ValueError("evaluate_five needs five distinct cards")
    ranks = sorted((card // 4 for card in cards), reverse=True)
    flush = len({card % 4 for card in cards}) == 1
    top = straight_top(ranks) if len(set(ranks)) == 5 else None
    counts: dict[int, int] = {}
    for rank in ranks:
        counts[rank] = counts.get(rank, 0) + 1
    # groups: (count, rank) sorted by count, then rank, descending
    groups = sorted(((count, rank) for rank, count in counts.items()), reverse=True)
    shape = [count for count, _ in groups]
    grouped = [rank for _, rank in groups]
    if top is not None and flush:
        return make_key(STRAIGHT_FLUSH, [top], order)
    if shape == [4, 1]:
        return make_key(QUADS, grouped, order)
    if shape == [3, 2]:
        return make_key(FULL_HOUSE, grouped, order)
    if flush:
        return make_key(FLUSH, ranks, order)
    if top is not None:
        return make_key(STRAIGHT, [top], order)
    if shape == [3, 1, 1]:
        return make_key(TRIPS, grouped, order)
    if shape == [2, 2, 1]:
        return make_key(TWO_PAIR, grouped, order)
    if shape == [2, 1, 1, 1]:
        return make_key(PAIR, grouped, order)
    return make_key(HIGH_CARD, ranks, order)


# ---------------------------------------------------------------- direct seven-card evaluator


def evaluate_seven(cards, order: str = ORDER_STRAIGHT_OVER_TRIPS) -> int:
    """Key of the best five-card hand of 5-7 distinct cards, without enumerating subsets.

    It forms the best hand of every category the cards contain and returns the maximum key. A candidate
    that is not a legal hand of its category (e.g. "high card" on five ranks that form a straight) is always
    beaten by a legal candidate of a higher category in both orders, so the maximum is the best hand.
    """
    cards = [int(card) for card in cards]
    if not 5 <= len(cards) <= 7 or len(set(cards)) != len(cards):
        raise ValueError("evaluate_seven needs 5 to 7 distinct cards")
    values = category_values(order)

    def key(category: str, tiebreak) -> int:
        result = values[category] << 20
        shift = 16
        for rank in tiebreak:
            result |= rank << shift
            shift -= 4
        return result

    count = [0] * 9
    suited: list[list[int]] = [[], [], [], []]
    for card in cards:
        count[card // 4] += 1
        suited[card % 4].append(card // 4)
    present = [rank for rank in range(8, -1, -1) if count[rank]]  # distinct ranks, descending
    candidates = []
    for suit_ranks in suited:
        if len(suit_ranks) >= 5:
            top = straight_top(suit_ranks)
            if top is not None:
                candidates.append(key(STRAIGHT_FLUSH, [top]))
            candidates.append(key(FLUSH, sorted(suit_ranks, reverse=True)[:5]))
    quads = [rank for rank in present if count[rank] == 4]
    trips = [rank for rank in present if count[rank] == 3]
    pairs_or_better = [rank for rank in present if count[rank] >= 2]
    if quads:
        quad = quads[0]
        kickers = [rank for rank in present if rank != quad]
        if kickers:
            candidates.append(key(QUADS, [quad, kickers[0]]))
    if trips:
        trip = trips[0]
        fillers = [rank for rank in pairs_or_better if rank != trip]
        if fillers:
            candidates.append(key(FULL_HOUSE, [trip, fillers[0]]))
    top = straight_top(present)
    if top is not None:
        candidates.append(key(STRAIGHT, [top]))
    if trips or quads:
        trip = (trips or quads)[0]
        kickers = [rank for rank in present if rank != trip][:2]
        if len(kickers) == 2:
            candidates.append(key(TRIPS, [trip] + kickers))
    if len(pairs_or_better) >= 2:
        high_pair, low_pair = pairs_or_better[:2]
        kickers = [rank for rank in present if rank not in (high_pair, low_pair)]
        if kickers:
            candidates.append(key(TWO_PAIR, [high_pair, low_pair, kickers[0]]))
    if pairs_or_better:
        pair = pairs_or_better[0]
        kickers = [rank for rank in present if rank != pair][:3]
        if len(kickers) == 3:
            candidates.append(key(PAIR, [pair] + kickers))
    if len(present) >= 5:
        candidates.append(key(HIGH_CARD, present[:5]))
    return max(candidates)


# ---------------------------------------------------------------- closed forms (from the rules)


def five_card_census_closed_form() -> dict[str, tuple[int, int]]:
    """Category -> (number of 5-card sets, number of distinct hand values), counted from the rules.

    9 ranks x 4 suits; 6 straights (R4); C(9,5) - 6 = 120 non-straight rank sets.
    """
    non_straight_rank_sets = comb(9, 5) - 6
    offsuit_patterns = 4 ** 5 - 4  # suit patterns of five distinct ranks that are not a flush
    return {
        STRAIGHT_FLUSH: (6 * 4, 6),
        QUADS: (9 * 8 * 4, 9 * 8),
        FLUSH: (4 * non_straight_rank_sets, non_straight_rank_sets),
        FULL_HOUSE: (9 * comb(4, 3) * 8 * comb(4, 2), 9 * 8),
        STRAIGHT: (6 * offsuit_patterns, 6),
        TRIPS: (9 * comb(4, 3) * comb(8, 2) * 4 * 4, 9 * comb(8, 2)),
        TWO_PAIR: (comb(9, 2) * comb(4, 2) ** 2 * 7 * 4, comb(9, 2) * 7),
        PAIR: (9 * comb(4, 2) * comb(8, 3) * 4 ** 3, 9 * comb(8, 3)),
        HIGH_CARD: (non_straight_rank_sets * offsuit_patterns, non_straight_rank_sets),
    }


# ---------------------------------------------------------------- tables in colex order


def five_card_keys(order: str = ORDER_STRAIGHT_OVER_TRIPS, subsets: np.ndarray | None = None) -> np.ndarray:
    """uint32 keys of all 376,992 five-card sets, indexed by colex rank."""
    if subsets is None:
        subsets = sdcards.colex_subsets(5)
    return np.fromiter((evaluate_five(row, order) for row in subsets.tolist()), dtype=np.uint32,
                       count=subsets.shape[0])


def dense_ordinals(keys: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """(ordinals, distinct): ordinals[i] = position of keys[i] among the sorted distinct keys (0 = worst)."""
    distinct, inverse = np.unique(keys, return_inverse=True)
    if distinct.shape[0] >= 65535:
        raise ValueError("too many distinct keys for uint16 ordinals")
    return inverse.astype(np.uint16).reshape(keys.shape), distinct


_FIVE_OF_SEVEN = tuple(combinations(range(7), 5))  # 21 position subsets, each increasing


def seven_card_keys_from_five(five_keys: np.ndarray, seven_subsets: np.ndarray | None = None,
                              chunk: int = 1 << 20) -> np.ndarray:
    """uint32 key of every 7-card set (colex order) = max over its 21 five-card subsets (rule R1)."""
    if seven_subsets is None:
        seven_subsets = sdcards.colex_subsets(7)
    binomial = sdcards.BINOMIAL
    result = np.empty(seven_subsets.shape[0], dtype=np.uint32)
    for start in range(0, seven_subsets.shape[0], chunk):
        rows = seven_subsets[start:start + chunk].astype(np.int64)
        best = np.zeros(rows.shape[0], dtype=np.uint32)
        for positions in _FIVE_OF_SEVEN:
            index = np.zeros(rows.shape[0], dtype=np.int64)
            for slot, position in enumerate(positions):
                index += binomial[rows[:, position], slot + 1]
            np.maximum(best, five_keys[index], out=best)
        result[start:start + rows.shape[0]] = best
    return result


class RankTables:
    """Python counterpart of rank_table_v1.bin, built from evaluate_five and rule R1.

    five_keys / seven_keys: uint32 keys in colex order; distinct: sorted distinct five-card keys;
    five_ordinals / seven_ordinals: uint16 positions of the keys in `distinct` (0 = worst). The seven-card
    ordinal of a set is the ordinal of its best five-card hand, the same scale as the five-card ordinals.
    """

    def __init__(self, order: str = ORDER_STRAIGHT_OVER_TRIPS, keep_subsets: bool = False,
                 log=None) -> None:
        started = time.perf_counter()
        self.order = order
        five_subsets = sdcards.colex_subsets(5)
        self.five_keys = five_card_keys(order, five_subsets)
        self.five_ordinals, self.distinct = dense_ordinals(self.five_keys)
        if log:
            log(f"five-card keys: {self.five_keys.shape[0]} sets, {self.distinct.shape[0]} distinct "
                f"({time.perf_counter() - started:.1f} s)")
        seven_subsets = sdcards.colex_subsets(7)
        self.seven_keys = seven_card_keys_from_five(self.five_keys, seven_subsets)
        self.seven_ordinals = np.searchsorted(self.distinct, self.seven_keys).astype(np.uint16)
        if not np.array_equal(self.distinct[self.seven_ordinals], self.seven_keys):
            raise AssertionError("a seven-card key is not a five-card key")
        self.five_subsets = five_subsets if keep_subsets else None
        self.seven_subsets = seven_subsets if keep_subsets else None
        self.seconds = time.perf_counter() - started
        if log:
            log(f"seven-card keys: {self.seven_keys.shape[0]} sets ({self.seconds:.1f} s total)")


# ---------------------------------------------------------------- hand ranks on boards


def hand_ordinals_on_boards(hand, boards: np.ndarray, seven_ordinals: np.ndarray) -> np.ndarray:
    """Seven-card ordinals of one hand on many boards (rows of 5 cards, disjoint from the hand).

    Straightforward path: append the two cards, sort each row, take the colex rank.
    """
    rows = np.empty((boards.shape[0], 7), dtype=np.uint8)
    rows[:, :5] = boards
    rows[:, 5] = hand[0]
    rows[:, 6] = hand[1]
    rows.sort(axis=1)
    if np.any(rows[:, 1:] == rows[:, :-1]):
        raise ValueError("a board shares a card with the hand")
    return seven_ordinals[sdcards.colex_of_sorted_rows(rows)]


def combo_ordinals_on_boards(boards: np.ndarray, seven_ordinals: np.ndarray) -> np.ndarray:
    """(B, 630) int16 seven-card ordinals of every combo on each board (rows of 5 sorted cards); -1 where
    the combo shares a card with the board.

    Fast path without sorting: in the merged sorted 7-card set, board card m sits at position
    m + [h1 < b_m] + [h2 < b_m], hole card h1 (< h2) at #(board cards < h1), h2 at 1 + #(board cards < h2).
    """
    binomial = sdcards.BINOMIAL
    board = boards.astype(np.int64)[:, None, :]  # (B, 1, 5)
    first = sdcards.COMBOS[:, 0].astype(np.int64)[None, :, None]  # (1, 630, 1)
    second = sdcards.COMBOS[:, 1].astype(np.int64)[None, :, None]
    slot = np.arange(5, dtype=np.int64)[None, None, :]
    board_position = slot + (first < board) + (second < board)  # (B, 630, 5)
    index = binomial[board, board_position + 1].sum(axis=2)
    first_position = (board < first).sum(axis=2)
    second_position = 1 + (board < second).sum(axis=2)
    index += binomial[first[:, :, 0], first_position + 1]
    index += binomial[second[:, :, 0], second_position + 1]
    dead = ((board == first) | (board == second)).any(axis=2)
    index[dead] = 0
    ranks = seven_ordinals[index].astype(np.int16)
    ranks[dead] = -1
    return ranks


# ---------------------------------------------------------------- heads-up all-in counts


def pair_counts_direct(first_combo: int, second_combo: int, seven_ordinals: np.ndarray,
                       board_subsets_32: np.ndarray | None = None) -> tuple[int, int, int]:
    """(W, T, L) of combo `first_combo` against `second_combo` by enumerating the 201,376 boards that avoid
    their four cards (the combos must be disjoint)."""
    hand = sdcards.COMBOS[first_combo]
    other = sdcards.COMBOS[second_combo]
    dead = {int(hand[0]), int(hand[1]), int(other[0]), int(other[1])}
    if len(dead) != 4:
        raise ValueError("the combos share a card")
    if board_subsets_32 is None:
        board_subsets_32 = sdcards.colex_subsets(5, 32)
    remaining = np.array([card for card in range(sdcards.DECK_SIZE) if card not in dead], dtype=np.uint8)
    boards = remaining[board_subsets_32]
    mine = hand_ordinals_on_boards(hand, boards, seven_ordinals)
    theirs = hand_ordinals_on_boards(other, boards, seven_ordinals)
    return int(np.count_nonzero(mine > theirs)), int(np.count_nonzero(mine == theirs)), \
        int(np.count_nonzero(mine < theirs))


def accumulate_boards(seven_ordinals: np.ndarray, first_board: int, last_board: int,
                      batch: int = 256) -> tuple[np.ndarray, np.ndarray, int]:
    """Board-by-board accumulation over the five-card boards with colex index in [first_board, last_board).

    Returns (wins, ties, boards): wins[i, j] = boards on which combos i and j are both live and i's hand
    beats j's; ties[i, j] = boards on which both are live and the hands are equal. Entries of overlapping
    combos and the diagonal are meaningless and must be ignored.
    Dead combos get -1 as row value and +32000 (wins) / -2 (ties) as column value, so no comparison that
    involves a dead combo can count.
    """
    boards = sdcards.colex_subsets(5)[first_board:last_board]
    n = sdcards.COMBO_COUNT
    wins = np.zeros((n, n), dtype=np.int64)
    ties = np.zeros((n, n), dtype=np.int64)
    small_wins = np.zeros((n, n), dtype=np.uint8)  # flushed before 255 boards
    small_ties = np.zeros((n, n), dtype=np.uint8)
    scratch = np.empty((n, n), dtype=bool)
    scratch_bytes = scratch.view(np.uint8)  # numpy bools are single bytes 0 / 1
    pending = 0
    for start in range(0, boards.shape[0], batch):
        ranks = combo_ordinals_on_boards(boards[start:start + batch], seven_ordinals)
        dead = ranks < 0
        win_columns = np.where(dead, np.int16(32000), ranks)
        tie_columns = np.where(dead, np.int16(-2), ranks)
        for row in range(ranks.shape[0]):
            rank = ranks[row]
            np.greater(rank[:, None], win_columns[row][None, :], out=scratch)
            np.add(small_wins, scratch_bytes, out=small_wins)
            np.equal(rank[:, None], tie_columns[row][None, :], out=scratch)
            np.add(small_ties, scratch_bytes, out=small_ties)
            pending += 1
            if pending == 255:
                wins += small_wins
                ties += small_ties
                small_wins.fill(0)
                small_ties.fill(0)
                pending = 0
    wins += small_wins
    ties += small_ties
    return wins, ties, int(boards.shape[0])


def _accumulate_worker(task):
    ordinals_path, first_board, last_board, batch = task
    seven_ordinals = np.array(np.load(ordinals_path, mmap_mode="r"))
    started = time.perf_counter()
    wins, ties, boards = accumulate_boards(seven_ordinals, first_board, last_board, batch)
    return first_board, last_board, wins, ties, boards, time.perf_counter() - started


def allin_counts_exhaustive(ordinals_path: Path, processes: int = 1, chunks: int | None = None,
                            batch: int = 256, log=None, first_board: int = 0,
                            last_board: int | None = None) -> tuple[np.ndarray, np.ndarray, int]:
    """(wins, ties, boards) summed over all 376,992 boards; see accumulate_boards.

    ordinals_path: a .npy file of the 8,347,680 Python seven-card ordinals (loaded by each worker).
    first_board / last_board restrict the colex board range (tests only; the check needs all boards).
    """
    last_board = sdcards.FIVE_CARD_SET_COUNT if last_board is None else last_board
    total_boards = last_board - first_board
    chunks = chunks or max(1, processes) * 8
    bounds = [first_board + total_boards * k // chunks for k in range(chunks + 1)]
    tasks = [(str(ordinals_path), bounds[k], bounds[k + 1], batch) for k in range(chunks)]
    n = sdcards.COMBO_COUNT
    wins = np.zeros((n, n), dtype=np.int64)
    ties = np.zeros((n, n), dtype=np.int64)
    boards = 0
    started = time.perf_counter()

    def merge(result) -> None:
        nonlocal boards
        first, last, part_wins, part_ties, part_boards, seconds = result
        np.add(wins, part_wins, out=wins)
        np.add(ties, part_ties, out=ties)
        boards += part_boards
        if log:
            log(f"boards [{first}, {last}) done in {seconds:.1f} s; {boards}/{total_boards} "
                f"({time.perf_counter() - started:.0f} s elapsed)")

    if processes <= 1:
        for task in tasks:
            merge(_accumulate_worker(task))
    else:
        context = multiprocessing.get_context("spawn")
        with context.Pool(processes=processes) as pool:
            for result in pool.imap_unordered(_accumulate_worker, tasks):
                merge(result)
    return wins, ties, boards


def triangle_counts(wins: np.ndarray, ties: np.ndarray) -> np.ndarray:
    """(198,135, 3) int64 (W, T, L) in triangular pair order (convention C4), hero = lower combo id."""
    first, second = sdcards.combo_pair_arrays()
    return np.stack([wins[first, second], ties[first, second], wins[second, first]], axis=1)


def class_pair_counts(wins: np.ndarray, ties: np.ndarray) -> dict[str, np.ndarray]:
    """Class-level all-in counts for S4: for hero class a and opponent class b (81 x 81 each),
    W/T/L = sums over disjoint combo pairs (i in a, j in b) of the combo counts, and N = the number of
    such disjoint combo pairs (each pair has 201,376 boards)."""
    disjoint = sdcards.combos_disjoint_matrix()
    indicator = np.zeros((sdcards.CLASS_COUNT, sdcards.COMBO_COUNT), dtype=np.int64)
    indicator[sdcards.COMBO_CLASS, np.arange(sdcards.COMBO_COUNT)] = 1
    masked_wins = np.where(disjoint, wins, 0)
    masked_ties = np.where(disjoint, ties, 0)
    result = {
        "W": indicator @ masked_wins @ indicator.T,
        "T": indicator @ masked_ties @ indicator.T,
        "L": indicator @ masked_wins.T @ indicator.T,
        "N": indicator @ disjoint.astype(np.int64) @ indicator.T,
    }
    return result


def log_to_stderr(message: str) -> None:
    print(f"[{time.strftime('%H:%M:%S')}] {message}", file=sys.stderr, flush=True)
