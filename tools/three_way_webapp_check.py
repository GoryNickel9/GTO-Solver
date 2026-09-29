"""V9 of the phase 2a gate: the exact three-player class table against the
user's equity-calculator-web-app DLL.

Input: the JSON written by gtosd_preflop_blueprint_three_way_table --dump-path
(schema gtosd.preflop_three_way_webapp_dump.v1):
- combo_triples: random disjoint combo triples with their exact counts per
  winner set over the 142,506 runouts (count_combo_triple, rank table). The
  three pot shares are compared with the DLL's.
- class_triples: table entries with the list of their combo pairs. The hero's
  pot share summed over the pairs, (WW + (TW + WT) / 2 + TT / 3) / 142,506 from
  the entry, is compared with the sum of the DLL's hero shares.

The DLL export compute_equity_enumeration enumerates exactly for up to 4 hands
when the runouts are at most 1,000,000 (3 hands preflop: 142,506). It is called
directly with single combos: the app's Python wrapper sends 3-hand requests to
Monte Carlo, whose multiway sampling is biased with blocking ranges.
Guards: the export must exist, and an answer of exactly 1/n for every player
(the DLL's catch-all on errors) is rejected unless the exact shares are 1/n.

Exit codes: 0 every comparison within the tolerance, 1 a mismatch, 2 usage or
DLL error.
"""

from __future__ import annotations

import argparse
import ctypes
import json
import sys
import time
from fractions import Fraction
from pathlib import Path

DEFAULT_DLL = Path(
    r"C:/Users/GoryNickel/Documents/GitHub/equity-calculator-web-app/cpp/build/Release/"
    r"equity_calculator.dll")


class CCard(ctypes.Structure):
    _fields_ = [("rank", ctypes.c_int), ("suit", ctypes.c_int)]


class CCombo(ctypes.Structure):
    _fields_ = [("card1", CCard), ("card2", CCard)]


def load_dll(path: Path):
    library = ctypes.CDLL(str(path))
    try:
        function = library.compute_equity_enumeration
    except AttributeError:
        raise SystemExit(f"{path} does not export compute_equity_enumeration")
    function.argtypes = [ctypes.POINTER(CCombo), ctypes.c_int, ctypes.POINTER(CCard),
                         ctypes.c_int, ctypes.POINTER(ctypes.c_double)]
    function.restype = None
    return function


def to_ccombo(combo: list[list[int]]) -> CCombo:
    # Our cards are [rank 0 = six .. 8 = ace, suit 0..3]; the DLL ranks 6..14.
    (rank1, suit1), (rank2, suit2) = combo
    return CCombo(CCard(6 + rank1, suit1), CCard(6 + rank2, suit2))


def dll_shares(function, combos: list[list[list[int]]]) -> list[float]:
    array = (CCombo * len(combos))(*[to_ccombo(combo) for combo in combos])
    result = (ctypes.c_double * len(combos))()
    function(array, len(combos), None, 0, result)
    return list(result)


def exact_shares(by_winners: list[int], runouts: int) -> list[Fraction]:
    shares = [Fraction(0)] * 3
    for mask in range(1, 8):
        winners = [seat for seat in range(3) if mask >> seat & 1]
        for seat in winners:
            shares[seat] += Fraction(by_winners[mask - 1], len(winners))
    return [share / runouts for share in shares]


def is_catch_all(values: list[float]) -> bool:
    return all(value == 1.0 / len(values) for value in values)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dump", type=Path)
    parser.add_argument("--dll", type=Path, default=DEFAULT_DLL)
    parser.add_argument("--tolerance", type=float, default=1e-12)
    parser.add_argument("--limit", type=int, default=None,
                        help="at most this many combo triples (dry runs)")
    arguments = parser.parse_args()

    dump = json.loads(arguments.dump.read_text(encoding="utf-8"))
    if dump.get("schema") != "gtosd.preflop_three_way_webapp_dump.v1":
        print(f"unexpected schema {dump.get('schema')}", file=sys.stderr)
        return 2
    runouts = int(dump["runouts"])
    function = load_dll(arguments.dll)
    started = time.perf_counter()

    worst = 0.0
    mismatches = 0
    catch_alls = 0
    triples = dump["combo_triples"][:arguments.limit]
    for triple in triples:
        expected = exact_shares(triple["by_winners"], runouts)
        got = dll_shares(function, triple["combos"])
        if is_catch_all(got) and any(share != Fraction(1, 3) for share in expected):
            catch_alls += 1
            mismatches += 1
            continue
        error = max(abs(float(share) - value) for share, value in zip(expected, got))
        worst = max(worst, error)
        if error > arguments.tolerance:
            mismatches += 1
            print(f"combo triple {triple['combos']}: ours {[float(s) for s in expected]} "
                  f"dll {got}")
    combo_seconds = time.perf_counter() - started

    class_worst = 0.0
    class_mismatches = 0
    dll_calls = len(triples)
    for triple in dump["class_triples"]:
        cells = triple["entry"]["cells"]
        # cells[3 * versus_first + versus_second], 0 better, 1 tie, 2 worse.
        expected = (Fraction(cells[0]) + Fraction(cells[3] + cells[1], 2) +
                    Fraction(cells[4], 3)) / runouts
        total = 0.0
        for first, second in triple["pairs"]:
            got = dll_shares(function, [triple["hero"], first, second])
            if is_catch_all(got):
                catch_alls += 1
            total += got[0]
            dll_calls += 1
        pairs = len(triple["pairs"])
        error = abs(float(expected) - total)
        class_worst = max(class_worst, error / max(pairs, 1))
        if pairs != triple["entry"]["pairs"] or error > arguments.tolerance * max(pairs, 1):
            class_mismatches += 1
            print(f"class triple {triple['classes']}: ours {float(expected)} dll {total}")
    seconds = time.perf_counter() - started

    ok = mismatches == 0 and class_mismatches == 0
    print(json.dumps({
        "schema": "gtosd.preflop_three_way_webapp_check.v1",
        "dll": str(arguments.dll),
        "table_fingerprint": dump.get("table_fingerprint"),
        "tolerance": arguments.tolerance,
        "combo_triples": len(triples),
        "combo_mismatches": mismatches,
        "combo_max_abs_share_error": worst,
        "class_triples": len(dump["class_triples"]),
        "class_mismatches": class_mismatches,
        "class_max_abs_share_error_per_pair": class_worst,
        "catch_all_answers": catch_alls,
        "dll_calls": dll_calls,
        "combo_seconds": round(combo_seconds, 3),
        "seconds": round(seconds, 3),
    }, indent=2))
    print(f"THREE_WAY_WEBAPP_CHECK={'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
