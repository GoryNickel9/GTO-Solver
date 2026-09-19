"""Run with --oracle-file pointing to the C++ enumeration export."""

from __future__ import annotations

import copy
import json
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

import constrained_br_milp as br

ORACLE_FILE: Path | None = None
SHORT_DECK_FILE: Path | None = None


class GlobalBestResponseTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        if ORACLE_FILE is None:
            raise RuntimeError("provide --oracle-file from gtosd_constrained_br_audit")
        cls.cases = json.loads(ORACLE_FILE.read_text(encoding="utf-8"))["oracle_cases"]

    def test_independent_cpp_enumeration(self) -> None:
        for case in self.cases:
            for player in (0, 1):
                with self.subTest(game=case["game"]["game_id"], player=player):
                    result = br.solve(case["game"], case["profile"], player)
                    self.assertTrue(result["optimal_within_tolerance"])
                    self.assertAlmostEqual(
                        result["candidate_value"],
                        case["expected_br"][player],
                        places=10,
                    )
                    self.assertLessEqual(result["objective_recheck_error"], 1e-10)

    def test_positive_payoff_scaling(self) -> None:
        case = self.cases[0]
        for scale in (0.01, 10, 40):
            game = copy.deepcopy(case["game"])
            for node in game["nodes"]:
                node["payoff"] = [value * scale for value in node["payoff"]]
            result = br.solve(game, case["profile"], 0)
            self.assertTrue(result["optimal_within_tolerance"])
            self.assertAlmostEqual(
                result["candidate_value"], scale * case["expected_br"][0], places=10
            )

    def test_lossless_numeric_regression(self) -> None:
        if SHORT_DECK_FILE is None:
            self.skipTest(
                "provide --short-deck-file with the generated conflict corpus"
            )
        data = json.loads(SHORT_DECK_FILE.read_text(encoding="utf-8"))
        game = next(
            g
            for g in data["games"]
            if g["stack"] == 20 and g["partition"] == "lossless"
        )
        run = next(
            r
            for r in game["runs"]
            if r["algorithm"] == "linear_cfr" and r["iterations"] == 25
        )
        result = br.solve(game["game"], run["profile"], 1)
        self.assertTrue(result["optimal_within_tolerance"])
        self.assertAlmostEqual(
            result["candidate_value"], run["physical_br"][1], places=9
        )

    def test_absent_minded_rejected(self) -> None:
        terminal = {
            "kind": 0,
            "edges": [],
            "information_set": "",
            "player": 0,
            "payoff": [0, 0],
        }
        nodes = [copy.deepcopy(terminal) for _ in range(5)]
        for node_id in (0, 2):
            nodes[node_id].update(
                kind=2,
                information_set="same",
                edges=[
                    {"action": 0, "child": node_id + 1, "probability": 0},
                    {"action": 1, "child": node_id + 2, "probability": 0},
                ],
            )
        nodes[3]["payoff"], nodes[4]["payoff"] = [4, -4], [1, -1]
        game = {"root": 0, "nodes": nodes}
        profile = {
            "same": {"player": 0, "actions": [0, 1], "probabilities": [0.5, 0.5]}
        }
        self.assertEqual(br.evaluate(game, profile, 0), 1.25)
        with self.assertRaisesRegex(ValueError, "absent-minded"):
            br.solve(game, profile, 0)

    def test_incomplete_search_never_certified(self) -> None:
        case = self.cases[0]
        interrupted = SimpleNamespace(
            x=None,
            fun=None,
            status=1,
            message="test time limit",
            mip_dual_bound=None,
            mip_node_count=0,
        )
        with patch.object(br, "milp", return_value=interrupted):
            result = br.solve(case["game"], case["profile"], 0)
        self.assertFalse(result["optimal_within_tolerance"])
        self.assertIsNone(result["numerical_upper_bound"])
        self.assertIsNone(result["policy"])
        self.assertEqual(result["feasible_lower_bound"], result["profile_ev"])

    def test_invalid_inputs_fail(self) -> None:
        case = self.cases[0]
        profile = copy.deepcopy(case["profile"])
        profile[next(iter(profile))]["probabilities"][0] = float("nan")
        with self.assertRaises(ValueError):
            br.solve(case["game"], profile, 0)
        game = copy.deepcopy(case["game"])
        game["nodes"][game["root"]]["edges"][0]["child"] = game["root"]
        with self.assertRaises(ValueError):
            br.solve(game, case["profile"], 0)
        with self.assertRaises(ValueError):
            br.solve(case["game"], case["profile"], 2)


if __name__ == "__main__":
    if "--short-deck-file" in sys.argv:
        index = sys.argv.index("--short-deck-file")
        SHORT_DECK_FILE = Path(sys.argv[index + 1])
        del sys.argv[index : index + 2]
    if "--oracle-file" in sys.argv:
        index = sys.argv.index("--oracle-file")
        ORACLE_FILE = Path(sys.argv[index + 1])
        del sys.argv[index : index + 2]
    unittest.main()
