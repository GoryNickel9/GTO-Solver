"""S2-DLL of the correctness coverage (shared_components.md section 3): a third opinion on hand ranking and
on the heads-up all-in table, from the user's equity calculator DLL.

The DLL is equity-calculator-web-app/cpp/build/Release/equity_calculator.dll (built 14/05/2026 from the
user's repository; its evaluator ranks short deck in its own code, written independently of this engine).
It is loaded read-only through ctypes: this script never builds, modifies or writes anything in the
user's repository. If the DLL or one of the two exports below is missing, the script stops with exit 2.

Exports used (cpp/src/equity_api.cpp of the user's repository, read 01/10/2026):
- compute_equity_enumeration(combos, n, board, board_cards, equities): runTrueEnumerationAPI. With a full
  board it evaluates the hands directly (shares 1/winners); with an empty board and 2 hands it enumerates
  all C(32,5) = 201,376 runouts exactly (the Monte Carlo fallback needs more than 1,000,000 runouts or
  more than 4 hands). On any C++ exception it returns 1/n to every hand (the catch-all).
- evaluate_hand_direct(cards, 7): FastHandEvaluator::evaluateFast, an int score, larger is better; 0 on
  error (a real hand never scores 0).

Not used, and why the known bias of the user's calculator does not reach these checks:
- The bias recorded on 28-30/09 (MONKER_RECIPE_REPRODUCTION_2026-09-28.md, 9.2) is in the web app's
  multiway range Monte Carlo (src/lib/multiwaySimulation.ts, TypeScript): it samples player 1's combo from
  its whole range and then player 2's from what is left, instead of sampling the joint deal conditioned on
  no shared card (rejection), so blocking ranges are weighted wrongly. It is not in the DLL.
- The DLL's own Monte Carlo (time-seeded std::rand) and the Python wrapper's routing of 3+ hands to it are
  not reached either: every call here is 2 single combos, preflop (201,376 runouts) or on a full board.
- Runtime guards prove the exact branch ran: every preflop equity times 402,752 is an integer (a Monte
  Carlo answer of 2,000,000 deals would not be), repeated calls are bit-identical, both equities sum to 1,
  and no answer is the 1/n catch-all unless the exact shares are 1/2. On the river every tie answer
  (0.5, 0.5) is confirmed by equal non-zero evaluate_hand_direct scores.

Checks:
- D0 the DLL exists and exports both functions; its size, modification time and SHA-256 are recorded.
- R7 (exhaustive) evaluate_hand_direct on all 8,347,680 seven-card sets. The DLL scores and the seven-card
  ordinals of rank_table_v1.bin are the same weak order: the map score <-> ordinal over all sets is one to
  one and strictly increasing, and no score is 0. This covers every river showdown of every pair of hands.
- RV (random) N river triples (hand, hand, five-card board), 9 distinct uniform cards each (default
  1,000,000): the DLL's winner or tie from compute_equity_enumeration with the full board equals the sign of
  the ordinal difference in rank_table_v1.bin; the API answer also equals the comparison of the DLL's own
  scores (consistency of the two entry points); ties confirmed as above.
- PA (preflop pairs) N random disjoint combo pairs (default 2,000, without replacement) plus 12 fixed edge
  pairs; for each, the DLL enumerates the 201,376 runouts and its two equities must equal
  (2W + T) / 402,752 and (2L + T) / 402,752 from preflop_all_in_v1.bin within --tolerance (1e-12); the
  integer 402,752 * equity must equal 2W + T; the DLL is called with the lower combo id first or second at
  random.
- PA2 the same pairs: W, T and L separately, counted over the 201,376 boards with the DLL's
  evaluate_hand_direct scores of R7, must equal the file's integers. (The equity alone fixes only 2W + T.)
- M (--self-test, in memory, no extra DLL calls) the checks have power: swapping the engine ordinals of the
  lowest straight and the highest trips must fail R7 and RV; W+1/L-1 on one pair must fail PA; W+1/T-2/L+1
  (same 2W + T) must pass PA and fail PA2.

Conventions: cards as in sdref.cards (C1 index = rank * 4 + suit, rank 0 = six .. 8 = ace, suits c d h s);
the DLL's CCard is {int rank 6..14, int suit 0..3}, so DLL rank = 6 + our rank and the suit is unchanged
(the DLL's Suit enum is CLUB, DIAMOND, HEART, SPADE). Suit names do not affect ranking.

Outputs in --out-dir (default out/monker/correctness/independent/S2_dll): sd_dll_check.json (summary and
mismatch samples) and dll_seven_scores.npy (the R7 scores, int32 in colex order; the PA2 workers read it).

Exit codes: 0 pass, 1 a mismatch (or a self-test mutation not detected), 2 usage, input or DLL error.
Processes: --processes 1 or 2 (default 2: the parent and one worker split PA/PA2; R7 and RV run in the
parent). OPENBLAS_NUM_THREADS and OMP_NUM_THREADS are set to 1 unless already set.
Measured cost (01/10/2026, see README): DLL evaluate_hand_direct about 1.2 us per call, full-board
enumeration about 8 us, preflop enumeration about 90 ms per pair; PA2 about 40 ms per pair in numpy.

Usage: python tools/independent/sd_dll_check.py [--dll PATH] [--pairs N] [--river N] [--processes 1|2]
       [--self-test] [--skip-checksum] [--seed S] [--out-dir DIR]
"""

from __future__ import annotations

import os

os.environ.setdefault("OPENBLAS_NUM_THREADS", "1")
os.environ.setdefault("OMP_NUM_THREADS", "1")

import argparse  # noqa: E402
import ctypes  # noqa: E402
import hashlib  # noqa: E402
import json  # noqa: E402
import multiprocessing  # noqa: E402
import sys  # noqa: E402
import time  # noqa: E402
from pathlib import Path  # noqa: E402

import numpy as np  # noqa: E402

from sdref import cards as sdcards  # noqa: E402
from sdref import ranking, resources  # noqa: E402

DEFAULT_DLL = Path(r"C:/Users/GoryNickel/Documents/GitHub/equity-calculator-web-app/cpp/build/Release/"
                   r"equity_calculator.dll")
DEFAULT_OUT_DIR = resources.INDEPENDENT_OUT_DIR / "S2_dll"
REQUIRED_EXPORTS = ("compute_equity_enumeration", "evaluate_hand_direct")
OPTIONAL_EXPORTS = ("compute_equity_montecarlo_ultra_fast", "compute_equity_range_vs_range_enumeration",
                    "compute_equity_range_vs_range_weighted")
RUNOUTS = sdcards.ALL_IN_RUNOUT_COUNT  # 201,376
TWICE_RUNOUTS = 2 * RUNOUTS  # 402,752: equity * this = 2W + T
SEVEN_BYTES = 7 * 2 * 4  # seven CCard {int, int}
COMBO_PAIR_BYTES = 2 * 2 * 2 * 4  # two CCombo of two CCard
BOARD_BYTES = 5 * 2 * 4
# DLL score categories: score // 1,000,000 (hand_evaluator.cpp *_BASE constants).
DLL_CATEGORIES = ("high_card", "pair", "two_pair", "trips", "straight", "full_house", "flush", "quads",
                  "straight_flush")
EDGE_PAIRS = (  # S2's ten edge pairs plus two: a wheel draw against a pair, and identical ranks
    ("AcAd", "KhKs"), ("Ah9h", "KcKd"), ("7c6c", "AhAd"), ("AcKc", "AdKd"), ("AhKh", "AsKs"),
    ("9h8h", "TcTd"), ("6c6d", "6h6s"), ("AcKd", "AhKs"), ("7c6c", "9d8d"), ("TcTd", "AhKh"),
    ("Ah6h", "9c9d"), ("7c6d", "7h6s"),
)
REPEATS_PER_PROCESS = 10  # determinism guard: these pairs are enumerated twice
log = ranking.log_to_stderr


class DllError(RuntimeError):
    pass


class Report:
    def __init__(self) -> None:
        self.checks: list[dict] = []

    def add(self, name: str, passed: bool, detail: dict | None = None) -> bool:
        self.checks.append({"check": name, "passed": bool(passed), **(detail or {})})
        log(f"{name}: {'PASS' if passed else 'FAIL'} {json.dumps(detail or {})[:400]}")
        return passed

    @property
    def passed(self) -> bool:
        return all(check["passed"] for check in self.checks)


class DllApi:
    """The two exports, called with raw addresses (c_void_p) so numpy buffers are passed without copies."""

    def __init__(self, path: Path) -> None:
        if not Path(path).is_file():
            raise DllError(f"{path} does not exist")
        try:
            self.library = ctypes.CDLL(str(path))
        except OSError as error:
            raise DllError(f"{path} cannot be loaded: {error}") from error
        self.exports = {name: hasattr(self.library, name) for name in REQUIRED_EXPORTS + OPTIONAL_EXPORTS}
        missing = [name for name in REQUIRED_EXPORTS if not self.exports[name]]
        if missing:
            raise DllError(f"{path} does not export {', '.join(missing)}")
        self.enumeration = self.library.compute_equity_enumeration
        self.enumeration.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_int,
                                     ctypes.c_void_p]
        self.enumeration.restype = None
        self.direct = self.library.evaluate_hand_direct
        self.direct.argtypes = [ctypes.c_void_p, ctypes.c_int]
        self.direct.restype = ctypes.c_int
        self._combos = np.zeros((4, 2), dtype=np.int32)
        self._equities = np.zeros(2, dtype=np.float64)

    def preflop(self, first_combo: int, second_combo: int) -> tuple[float, float]:
        """Exact preflop equities of two combos (DLL order = argument order)."""
        cards = np.array([*sdcards.COMBOS[first_combo], *sdcards.COMBOS[second_combo]], dtype=np.uint8)
        self._combos[:] = dll_cards(cards)
        self._equities[:] = -1.0
        self.enumeration(self._combos.ctypes.data, 2, None, 0, self._equities.ctypes.data)
        return float(self._equities[0]), float(self._equities[1])


def dll_cards(cards: np.ndarray) -> np.ndarray:
    """Our card indices (any shape) -> C-contiguous int32 (..., 2) of DLL CCard {6 + rank, suit}."""
    cards = np.asarray(cards, dtype=np.int32)
    out = np.empty(cards.shape + (2,), dtype=np.int32)
    out[..., 0] = cards // sdcards.SUIT_COUNT + 6
    out[..., 1] = cards % sdcards.SUIT_COUNT
    return np.ascontiguousarray(out)


def dll_provenance(path: Path, api: DllApi) -> dict:
    stat = path.stat()
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    source_dir = path.parents[2] / "src"
    sources = {}
    for name in ("hand_evaluator.cpp", "equity_api.cpp"):
        source = source_dir / name
        if source.is_file():
            sources[name] = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(source.stat().st_mtime))
    return {"path": str(path), "bytes": stat.st_size,
            "modified": time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(stat.st_mtime)),
            "sha256": digest, "exports": api.exports, "source_modified": sources}


# ---------------------------------------------------------------- R7: all seven-card sets


def seven_card_scores(api: DllApi, chunk: int = 1_000_000) -> np.ndarray:
    subsets = sdcards.colex_subsets(7)
    scores = np.empty(subsets.shape[0], dtype=np.int32)
    direct = api.direct
    for start in range(0, subsets.shape[0], chunk):
        block = dll_cards(subsets[start:start + chunk])
        base = block.ctypes.data
        values = [direct(base + SEVEN_BYTES * i, 7) for i in range(block.shape[0])]
        scores[start:start + block.shape[0]] = values
        log(f"R7: {start + block.shape[0]:,} / {subsets.shape[0]:,} seven-card sets scored")
    return scores


def order_isomorphism(scores: np.ndarray, ordinals: np.ndarray, max_report: int) -> dict:
    """Are DLL scores and engine ordinals the same weak order on these sets? (one-to-one, increasing)"""
    zero = int(np.count_nonzero(scores <= 0))
    key = (scores.astype(np.int64) << 16) | ordinals.astype(np.int64)
    pairs = np.unique(key)
    pair_scores = pairs >> 16
    pair_ordinals = pairs & 0xFFFF
    score_levels = int(np.unique(pair_scores).shape[0])
    ordinal_levels = int(np.unique(pair_ordinals).shape[0])
    # pairs are sorted by score, then ordinal; with a bijection the ordinals must strictly increase.
    descents = np.flatnonzero(np.diff(pair_ordinals) <= 0)
    samples = []
    for k in descents[:max_report].tolist():
        example = []
        for level in (k, k + 1):
            where = int(np.flatnonzero(key == pairs[level])[0])
            cards = sdcards.subset_from_colex(where, 7)
            example.append({"cards": sdcards.format_cards(cards), "dll_score": int(pair_scores[level]),
                            "engine_ordinal": int(pair_ordinals[level]),
                            "rules": ranking.describe_key(ranking.evaluate_seven(cards))})
        samples.append(example)
    passed = (zero == 0 and score_levels == ordinal_levels == pairs.shape[0] and descents.shape[0] == 0)
    by_category = {}
    for category in range(len(DLL_CATEGORIES)):
        inside = (pair_scores // 1_000_000) == category
        by_category[DLL_CATEGORIES[category]] = int(np.count_nonzero(inside))
    return {"passed": passed, "sets": int(scores.shape[0]), "zero_scores": zero,
            "score_levels": score_levels, "ordinal_levels": ordinal_levels,
            "score_ordinal_pairs": int(pairs.shape[0]), "order_violations": int(descents.shape[0]),
            "levels_by_dll_category": by_category, "samples": samples}


# ---------------------------------------------------------------- RV: random river triples


def river_triples(api: DllApi, engine_seven: np.ndarray, dll_scores: np.ndarray, count: int,
                  rng: np.random.Generator, chunk: int = 100_000) -> dict:
    """Returns per-triple arrays: engine ordinals of both hands, DLL API outcome, DLL score outcome."""
    hero_ordinal = np.empty(count, dtype=np.uint16)
    villain_ordinal = np.empty(count, dtype=np.uint16)
    api_outcome = np.empty(count, dtype=np.int8)  # +1 hero wins, 0 tie, -1 villain wins, 9 invalid
    score_outcome = np.empty(count, dtype=np.int8)
    tie_unconfirmed = 0
    invalid = 0
    category_pairs = np.zeros((9, 9), dtype=np.int64)
    first_invalid: list[dict] = []
    enumeration = api.enumeration
    done = 0
    while done < count:
        size = min(chunk, count - done)
        deal = np.argsort(rng.random((size, sdcards.DECK_SIZE)), axis=1)[:, :9].astype(np.uint8)
        board = deal[:, 4:9]
        indices = []
        for hand in (deal[:, 0:2], deal[:, 2:4]):
            rows = np.concatenate([hand, board], axis=1)
            rows.sort(axis=1)
            indices.append(sdcards.colex_of_sorted_rows(rows))
        hero_ordinal[done:done + size] = engine_seven[indices[0]]
        villain_ordinal[done:done + size] = engine_seven[indices[1]]
        hero_score = dll_scores[indices[0]].astype(np.int64)
        villain_score = dll_scores[indices[1]].astype(np.int64)
        score_outcome[done:done + size] = np.sign(hero_score - villain_score)
        np.add.at(category_pairs, (hero_score // 1_000_000, villain_score // 1_000_000), 1)
        combos = dll_cards(deal[:, :4])  # (size, 4, 2) = two CCombo per triple
        boards = dll_cards(board)  # (size, 5, 2)
        equities = np.full((size, 2), -1.0, dtype=np.float64)
        combo_base, board_base, out_base = combos.ctypes.data, boards.ctypes.data, equities.ctypes.data
        for i in range(size):
            enumeration(combo_base + COMBO_PAIR_BYTES * i, 2, board_base + BOARD_BYTES * i, 5,
                        out_base + 16 * i)
        outcome = np.full(size, 9, dtype=np.int8)
        outcome[(equities[:, 0] == 1.0) & (equities[:, 1] == 0.0)] = 1
        outcome[(equities[:, 0] == 0.0) & (equities[:, 1] == 1.0)] = -1
        tie = (equities[:, 0] == 0.5) & (equities[:, 1] == 0.5)
        outcome[tie] = 0
        tie_unconfirmed += int(np.count_nonzero(tie & ((hero_score != villain_score) | (hero_score <= 0))))
        bad = np.flatnonzero(outcome == 9)
        invalid += int(bad.shape[0])
        for i in bad[:max(0, 5 - len(first_invalid))].tolist():
            first_invalid.append({"cards": sdcards.format_cards(deal[i]), "dll": equities[i].tolist()})
        api_outcome[done:done + size] = outcome
        done += size
        log(f"RV: {done:,} / {count:,} river triples")
    return {"hero_ordinal": hero_ordinal, "villain_ordinal": villain_ordinal, "api_outcome": api_outcome,
            "score_outcome": score_outcome, "tie_unconfirmed": tie_unconfirmed, "invalid": invalid,
            "invalid_samples": first_invalid, "category_pairs": category_pairs}


def river_verdict(triples: dict, hero_ordinal: np.ndarray, villain_ordinal: np.ndarray,
                  max_report: int) -> dict:
    expected = np.sign(hero_ordinal.astype(np.int32) - villain_ordinal.astype(np.int32)).astype(np.int8)
    api = triples["api_outcome"]
    mismatch = np.flatnonzero(api != expected)
    entry_points_disagree = int(np.count_nonzero(api != triples["score_outcome"]))
    categories = triples["category_pairs"]
    straight, trips, full_house, flush = 4, 3, 5, 6
    coverage = {
        "straight_vs_trips": int(categories[straight, trips] + categories[trips, straight]),
        "flush_vs_full_house": int(categories[flush, full_house] + categories[full_house, flush]),
        "same_category": int(np.trace(categories)),
        "by_hero_category": {DLL_CATEGORIES[c]: int(categories[c].sum()) for c in range(9)},
    }
    passed = (mismatch.shape[0] == 0 and triples["invalid"] == 0 and triples["tie_unconfirmed"] == 0
              and entry_points_disagree == 0)
    return {"passed": passed, "triples": int(api.shape[0]),
            "hero_wins": int(np.count_nonzero(expected == 1)), "ties": int(np.count_nonzero(expected == 0)),
            "villain_wins": int(np.count_nonzero(expected == -1)), "mismatches": int(mismatch.shape[0]),
            "invalid_answers": triples["invalid"], "ties_not_confirmed_by_scores": triples["tie_unconfirmed"],
            "api_vs_dll_scores_disagree": entry_points_disagree, "coverage": coverage,
            "mismatch_indices": mismatch[:max_report].tolist(), "invalid_samples": triples["invalid_samples"]}


# ---------------------------------------------------------------- PA / PA2: preflop pairs


def choose_pairs(count: int, rng: np.random.Generator) -> list[tuple[int, int, int]]:
    """(lower combo id, higher combo id, flip) - flip 1 calls the DLL with the higher id first."""
    pairs = []
    for hero, opponent in EDGE_PAIRS:
        a = sdcards.combo_index(*sdcards.parse_cards(hero))
        b = sdcards.combo_index(*sdcards.parse_cards(opponent))
        pairs.append((min(a, b), max(a, b), 0 if a < b else 1))
    first, second = sdcards.combo_pair_arrays()
    disjoint = sdcards.combos_disjoint_matrix()[first, second]
    edge = {sdcards.combo_pair_index(a, b) for a, b, _ in pairs}
    candidates = np.array([t for t in np.flatnonzero(disjoint).tolist() if t not in edge])
    chosen = np.sort(rng.choice(candidates, size=min(count, candidates.shape[0]), replace=False))
    flips = rng.integers(0, 2, size=chosen.shape[0])
    pairs.extend((int(first[t]), int(second[t]), int(f)) for t, f in zip(chosen.tolist(), flips.tolist()))
    return pairs


def preflop_worker(task: tuple) -> list[dict]:
    """Runs in the parent or in the worker process: DLL enumeration and score-based W/T/L per pair."""
    dll_path, scores_path, pairs, repeats, label = task
    api = DllApi(Path(dll_path))
    scores = np.load(scores_path)
    boards32 = sdcards.colex_subsets(5, 32)
    results = []
    started = time.perf_counter()
    for k, (low, high, flip) in enumerate(pairs):
        call = (high, low) if flip else (low, high)
        answer = api.preflop(*call)
        low_equity, high_equity = (answer[1], answer[0]) if flip else answer
        repeat_identical = None
        if k < repeats:
            again = api.preflop(*call)
            repeat_identical = again == answer
        wins, ties, losses = ranking.pair_counts_direct(low, high, scores, boards32)
        results.append({"low": low, "high": high, "flip": flip, "low_equity": low_equity,
                        "high_equity": high_equity, "repeat_identical": repeat_identical,
                        "dll_score_wtl": (wins, ties, losses)})
        if (k + 1) % 250 == 0:
            log(f"PA[{label}]: {k + 1} / {len(pairs)} pairs ({time.perf_counter() - started:.0f} s)")
    return results


def preflop_verdict(results: list[dict], entries: np.ndarray, tolerance: float, max_report: int) -> tuple:
    errors = []
    equity_bad, lattice_bad, sum_bad, catch_alls, repeat_bad, wtl_bad = [], [], [], [], [], []
    bit_identical = 0
    repeats = 0
    for result in results:
        low, high = result["low"], result["high"]
        wins, ties, losses = (int(v) for v in entries[sdcards.combo_pair_index(low, high)])
        text = f"{sdcards.format_combo(low)} vs {sdcards.format_combo(high)}"
        got = (result["low_equity"], result["high_equity"])
        twice = (2 * wins + ties, 2 * losses + ties)
        expected = (twice[0] / TWICE_RUNOUTS, twice[1] / TWICE_RUNOUTS)
        error = max(abs(g - e) for g, e in zip(got, expected))
        errors.append(error)
        bit_identical += int(got == expected)
        if got[0] == got[1] == 0.5 and twice[0] != RUNOUTS:
            catch_alls.append(text)
        if error > tolerance:
            equity_bad.append({"pair": text, "dll": got, "file": expected, "file_wtl": (wins, ties, losses)})
        scaled = [g * TWICE_RUNOUTS for g in got]
        if any(abs(s - round(s)) > 1e-6 for s in scaled) or (round(scaled[0]), round(scaled[1])) != twice:
            lattice_bad.append({"pair": text, "dll_times_402752": scaled, "file_2W_T": twice})
        if abs(got[0] + got[1] - 1.0) > tolerance:
            sum_bad.append({"pair": text, "dll": got})
        if result["repeat_identical"] is not None:
            repeats += 1
            if not result["repeat_identical"]:
                repeat_bad.append(text)
        if tuple(result["dll_score_wtl"]) != (wins, ties, losses):
            wtl_bad.append({"pair": text, "dll_scores_wtl": result["dll_score_wtl"],
                            "file_wtl": (wins, ties, losses)})
    pa = {"pairs": len(results), "edge_pairs": len(EDGE_PAIRS), "tolerance": tolerance,
          "max_abs_equity_error": max(errors) if errors else None, "bit_identical_pairs": bit_identical,
          "equity_mismatches": len(equity_bad), "lattice_failures": len(lattice_bad),
          "sum_failures": len(sum_bad), "catch_all_answers": len(catch_alls),
          "repeated_calls": repeats, "repeat_not_identical": len(repeat_bad),
          "dll_called_higher_id_first": sum(result["flip"] for result in results),
          "samples": (equity_bad + lattice_bad + sum_bad)[:max_report] + catch_alls[:max_report]
          + repeat_bad[:max_report]}
    pa["passed"] = not (equity_bad or lattice_bad or sum_bad or catch_alls or repeat_bad) and repeats > 0
    pa2 = {"pairs": len(results), "wtl_mismatches": len(wtl_bad), "samples": wtl_bad[:max_report],
           "passed": not wtl_bad}
    edge = {}
    for result in results[:len(EDGE_PAIRS)]:
        low, high = result["low"], result["high"]
        edge[f"{sdcards.format_combo(low)} vs {sdcards.format_combo(high)}"] = {
            "file_wtl": [int(v) for v in entries[sdcards.combo_pair_index(low, high)]],
            "dll_low_equity": result["low_equity"]}
    pa["edge_results"] = edge
    return pa, pa2


# ---------------------------------------------------------------- M: power of the checks


def self_test(report: Report, dll_scores: np.ndarray, engine_seven: np.ndarray, triples: dict | None,
              results: list[dict], entries: np.ndarray, tolerance: float) -> None:
    straight_sets = (dll_scores // 1_000_000) == 4
    trips_sets = (dll_scores // 1_000_000) == 3
    lowest_straight = int(engine_seven[straight_sets].min())
    highest_trips = int(engine_seven[trips_sets].max())
    remap = np.arange(65536, dtype=np.uint16)
    remap[lowest_straight], remap[highest_trips] = highest_trips, lowest_straight
    mutated = remap[engine_seven]
    r7 = order_isomorphism(dll_scores, mutated, 1)
    detail = {"swapped_ordinals": [lowest_straight, highest_trips], "r7_detects": not r7["passed"],
              "r7_order_violations": r7["order_violations"]}
    detected = not r7["passed"]
    if triples is not None:
        rv = river_verdict(triples, remap[triples["hero_ordinal"]], remap[triples["villain_ordinal"]], 1)
        detail.update({"rv_detects": not rv["passed"], "rv_mismatches": rv["mismatches"]})
        detected = detected and not rv["passed"]
    report.add("M1_straight_trips_swap_detected", detected, detail)
    if not results:
        return
    def mutable(result: dict) -> bool:
        _, ties, losses = entries[sdcards.combo_pair_index(result["low"], result["high"])]
        return ties >= 2 and losses >= 1

    probe = next((result for result in results if mutable(result)), None)
    if probe is None:
        report.add("M2_entry_mutations_detected", False, {"reason": "no pair with T >= 2 and L > 0"})
        return
    index = sdcards.combo_pair_index(probe["low"], probe["high"])
    outcomes = {}
    for name, delta in (("W+1_L-1", (1, 0, -1)), ("W+1_T-2_L+1", (1, -2, 1))):
        changed = entries.astype(np.int64).copy()
        changed[index] += np.array(delta)
        pa, pa2 = preflop_verdict([probe], changed, tolerance, 1)
        outcomes[name] = {"pa_detects": not pa["passed"], "pa2_detects": not pa2["passed"]}
    ok = (outcomes["W+1_L-1"]["pa_detects"] and outcomes["W+1_L-1"]["pa2_detects"]
          and not outcomes["W+1_T-2_L+1"]["pa_detects"] and outcomes["W+1_T-2_L+1"]["pa2_detects"])
    report.add("M2_entry_mutations_detected", ok,
               {"pair": f"{sdcards.format_combo(probe['low'])} vs {sdcards.format_combo(probe['high'])}",
                **outcomes})


# ---------------------------------------------------------------- main


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--dll", type=Path, default=DEFAULT_DLL)
    parser.add_argument("--rank-table", type=Path, default=resources.RANK_TABLE_PATH)
    parser.add_argument("--all-in", type=Path, default=resources.ALL_IN_PATH)
    parser.add_argument("--pairs", type=int, default=2000, help="random disjoint preflop pairs (PA, PA2)")
    parser.add_argument("--river", type=int, default=1_000_000, help="random river triples (RV)")
    parser.add_argument("--processes", type=int, default=2, choices=(1, 2))
    parser.add_argument("--tolerance", type=float, default=1e-12)
    parser.add_argument("--self-test", action="store_true", help="M1/M2 mutation checks (in memory)")
    parser.add_argument("--skip-checksum", action="store_true")
    parser.add_argument("--seed", type=int, default=20261001)
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT_DIR)
    parser.add_argument("--max-report", type=int, default=20)
    args = parser.parse_args(argv)
    if args.pairs < 0 or args.river < 0:
        parser.error("--pairs and --river must be >= 0")

    started = time.perf_counter()
    report = Report()
    timings: dict[str, float] = {}
    try:
        api = DllApi(args.dll)
    except DllError as error:
        log(f"D0: {error}")
        print(f"SD_DLL_CHECK=ERROR {error}")
        return 2
    provenance = dll_provenance(args.dll, api)
    report.add("D0_dll", True, {key: provenance[key] for key in ("path", "bytes", "modified", "sha256")})
    args.out_dir.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(args.seed)

    try:
        rank = resources.read_rank_table(args.rank_table, verify=not args.skip_checksum)
        engine = resources.read_all_in(args.all_in, verify=not args.skip_checksum,
                                       rank_fingerprint=None if args.skip_checksum else rank.fingerprint)
    except (OSError, ValueError) as error:
        log(f"input error: {error}")
        return 2
    report.add("E0_engine_files", engine.fingerprint_verified is not False,
               {"rank_table": rank.fingerprint, "all_in": engine.fingerprint,
                "checksums_verified": not args.skip_checksum,
                "all_in_fingerprint_from_rank_table": engine.fingerprint_verified})
    timings["read_files"] = round(time.perf_counter() - started, 1)

    stage = time.perf_counter()
    dll_scores = seven_card_scores(api)
    scores_path = args.out_dir / "dll_seven_scores.npy"
    np.save(scores_path, dll_scores)
    r7 = order_isomorphism(dll_scores, rank.seven, args.max_report)
    report.add("R7_seven_card_order_exhaustive", r7.pop("passed"), r7)
    timings["R7"] = round(time.perf_counter() - stage, 1)

    triples = None
    if args.river:
        stage = time.perf_counter()
        triples = river_triples(api, rank.seven, dll_scores, args.river, rng)
        rv = river_verdict(triples, triples["hero_ordinal"], triples["villain_ordinal"], args.max_report)
        report.add("RV_random_river_triples", rv.pop("passed"), rv)
        timings["RV"] = round(time.perf_counter() - stage, 1)

    results: list[dict] = []
    if args.pairs:
        stage = time.perf_counter()
        pairs = choose_pairs(args.pairs, rng)
        halves = [pairs[0::2], pairs[1::2]] if args.processes == 2 else [pairs]
        tasks = [(str(args.dll), str(scores_path), half, REPEATS_PER_PROCESS, f"p{i}")
                 for i, half in enumerate(halves)]
        if len(tasks) == 2:
            with multiprocessing.get_context("spawn").Pool(1) as pool:
                pending = pool.apply_async(preflop_worker, (tasks[1],))
                mine = preflop_worker(tasks[0])
                theirs = pending.get()
            results = [None] * len(pairs)
            results[0::2], results[1::2] = mine, theirs
        else:
            results = preflop_worker(tasks[0])
        pa, pa2 = preflop_verdict(results, engine.entries, args.tolerance, args.max_report)
        report.add("PA_preflop_equity_exact", pa.pop("passed"), pa)
        report.add("PA2_preflop_wtl_from_dll_scores", pa2.pop("passed"), pa2)
        timings["PA"] = round(time.perf_counter() - stage, 1)

    if args.self_test:
        self_test(report, dll_scores, rank.seven, triples, results, engine.entries, args.tolerance)

    summary = {"schema": "gtosd.independent.sd_dll_check.v1", "passed": report.passed,
               "dll": provenance, "rank_table": str(rank.path), "all_in": str(engine.path),
               "settings": {"pairs": args.pairs, "river": args.river, "processes": args.processes,
                            "seed": args.seed, "tolerance": args.tolerance, "self_test": args.self_test},
               "timings_s": timings, "seconds": round(time.perf_counter() - started, 1),
               "checks": report.checks}
    (args.out_dir / "sd_dll_check.json").write_text(json.dumps(summary, indent=1) + "\n", encoding="utf-8")
    log(f"summary written to {args.out_dir / 'sd_dll_check.json'} ({summary['seconds']} s)")
    print(f"SD_DLL_CHECK={'PASS' if report.passed else 'FAIL'}")
    return 0 if report.passed else 1


if __name__ == "__main__":
    sys.exit(main())
