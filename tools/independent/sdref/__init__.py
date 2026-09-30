"""sdref: independent short-deck references for the correctness checks S1-S6.

The game logic here (hand ranking, all-in outcomes) is written from the rules stated in the module
docstrings, not translated from the C++ engine. Only indexing and file-format conventions follow the engine,
and each one is listed as a convention (cards.py C1-C5, resources.py F1-F3).

Modules:
- sdref.cards: cards (index = rank * 4 + suit), suit permutations, colex subsets, the 630 combos in engine
  order (COMBOS, combo_index, COMBO_INDEX, combo_pair_index, combo_pair_arrays, combos_disjoint_matrix)
  and the 81 preflop classes (CLASS_NAMES, CLASS_ID, COMBO_CLASS, COMBO_LABELS, CLASS_COMBOS, class_mass).
- sdref.ranking: evaluate_five / evaluate_seven (keys: larger is better), the straight / trips switch
  (ORDER_STRAIGHT_OVER_TRIPS default, ORDER_TRIPS_OVER_STRAIGHT), RankTables (Python 5- and 7-card
  ordinals in colex order), hand_ordinals_on_boards, combo_ordinals_on_boards, pair_counts_direct (W, T, L
  of one combo pair), allin_counts_exhaustive (all pairs), triangle_counts, class_pair_counts (81 x 81
  W / T / L / N for the step-1 LP).
- sdref.resources: readers of rank_table_v1.bin and preflop_all_in_v1.bin (with checksum and fingerprint
  verification), all_in_matrices, and write_python_allin / read_python_allin for the Python counts that
  sd_allin_check.py --exhaustive saves (allin_counts_python.npz).

Usage from a script in tools/independent/:
    from sdref import cards, ranking, resources
    wins, ties, meta = resources.read_python_allin(path)       # S2 output
    per_class = ranking.class_pair_counts(wins, ties)           # {"W","T","L","N"}: (81, 81) int64
"""

from . import cards, ranking, resources  # noqa: F401

__all__ = ["cards", "ranking", "resources"]
