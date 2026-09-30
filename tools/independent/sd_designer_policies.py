"""S5a of the correctness coverage (shared_components.md section 3): step-2 EV identities with designer
policies. Python writes policy.bin files whose postflop rows are hand-independent (every row of a postflop
node is the same), evaluates them with the frozen exact evaluator (gtosd_preflop_blueprint_monker_values
--all-flops, out/monker/bin_correct/c123) and compares its EVs and per-class action values with a closed form
computed here from the all-in counts, the terminal payoffs and the preflop reach alone.

Why the closed form is exact (the rules, stated here, not taken from the engine):
- G1 Deal: the two players' hole cards and the five board cards are a uniform deal of 2 + 2 + 5 distinct
  cards from the 36-card deck. So the opponent combo is uniform over the C(34,2) = 561 combos disjoint from
  the hero's, and the board is uniform over the C(32,5) = 201,376 boards disjoint from both.
- G2 With a postflop strategy that ignores the cards (every row of a postflop node equal), the probability of
  every postflop line depends only on the line, not on the cards or the board. The board is then still
  uniform given both combos at every terminal, so the expected value of a showdown or all-in runout terminal
  z for the pair (i, j) is pay_z(seat 0 wins) W_ij / N + pay_z(seat 1 wins) L_ij / N + pay_z(tie) T_ij / N,
  where W, T, L count the boards on which combo i beats, ties or loses to combo j (N = 201,376): the same
  counts as the preflop all-in, whichever street the showdown happens on. A fold pays its fixed row.
- G3 The value of a node for the pair (i, j) is then the policy-weighted sum over its children (preflop rows
  by hand class, postflop rows constant), and every quantity the evaluator reports is a finite sum of these.
The engine side of the identity is the whole step-2 exact path: per-board ranks and the river kernels,
the flop and turn runout aggregation over the 573 canonical flops and their images (605,088 boards), the
joint river engine, card removal above the river, the win / tie / lose payoff composition and the ante
scale. A mismatch above the tolerance is a bug on one side.

Inputs:
- the tree: the S3 dump of the engine (`gtosd_preflop_blueprint_game --dump-nodes`, a build after fdf8114;
  --game-exe or --dump-dir) and / or the tree the S3 referee enumerates from the rules
  (sdref/rules.py via sd_referee.synthesize_dump). The prediction uses the referee's payoffs; when the engine
  dump is present its structure and every payoff row must equal the referee's (else exit 1).
- the all-in counts: the Python counts of S2 (out/monker/correctness/independent/S2_allin/
  allin_counts_python.npz, from sd_allin_check.py --exhaustive) or, when that file is missing, the engine's
  preflop_all_in_v1.bin (S2 compares the two); the report names the source. --allin python requires the npz.
- a reference policy per game (policy.bin or its values.json) trained with the bucket tables and texture map
  the evaluation loads: its tree fingerprint, capacities and source suffix go into the header (P2 in
  sdref/policy.py); the table is ours. Its snapshot directory also serves as the --charts set the evaluator
  requires (the chart values play no part in the compared quantities).

Designer policies (postflop rows hand-independent; "prefer X, Y" = probability 1 on the first available):
- a  uniform everywhere.
- b  CO limps, BTN checks behind, then check / call everywhere: a pure 4a checkdown to the river showdown.
- c  b preflop; flop: the first player shoves, the other calls (flop all-in runouts); later streets check.
- d  as c on the turn (flop checked through).
- e  as c on the river (flop and turn checked through).
- f  b preflop; every street: CO checks 1/3, shoves 2/3; BTN after a check checks 1/2, shoves 1/2; a player
     facing a shove folds (postflop folds on every street, and the checkdown).
- g  class-dependent mixed preflop rows and mixed postflop rows, every probability positive (every
     terminal reached, preflop reach different per class).
- h  class-dependent pure preflop rows (classes that never reach a node: class weight 0, class EV 0 on both
     sides), then the checkdown of b.

Compared (each within --tolerance, default 1e-9 antes), per game and policy, on the exact pass:
- estimate.ev_antes (EV0, EV1), and with rake estimate.expected_rake_antes = -(EV0 + EV1);
- per hero: root_value_mean_antes and the 630 root_values;
- per hero preflop decision node: strategy (our rows read back), opponent_reach (630), class_weight (81),
  combo_values (630 per action) and class_ev (81 per action);
- best_response_preflop_antes and gain_preflop_antes (the hero best-responds preflop per combo, the average
  strategy from the flop on: a max over this closed form), and preflop_response gain_per_combo /
  gain_per_class;
- the evaluator's own self_checks passed, mode "exact", 573 flops, 7,140 physical flops, 605,088 boards.
Not predicted: best_response, gain, gain_lower, nashconv (a postflop best response sees the cards).

Output conventions of the evaluator (read from monker_values.cpp and best_response.hpp; choices, not rules):
- O1 values are in antes (10,000 money units) per hand of the hero, net of everything the hero put in
  (antes included); seat 0 = CO, seat 1 = BTN.
- O2 root_values[h] = (1 / 561) sum over opponent combos o disjoint from h of the pair's value at the root;
  ev = their mean over the 630 combos.
- O3 at a hero preflop node n: opponent_reach[h] = (1 / 561) sum_o reach_opp(o, n);
  combo_values[a][h] = (1 / 561) sum_o reach_opp(o, n) value(h, o, child a); class_weight[c] = sum over the
  combos h of class c of opponent_reach[h]; class_ev[a][c] = sum_{h in c} combo_values[a][h] /
  class_weight[c], and 0 when class_weight[c] = 0. JSON tokens are in edge order.
- O4 winner masks of a payoff row: bit s = seat s shares the pot (1: seat 0 wins, 2: seat 1, 3: split).
- O5 best_response_preflop: the hero takes, per combo, the action of largest value (O3 units) at each of
  its preflop nodes, bottom up, with the average strategy from the flop on; mean over the 630 combos
  (BestResponseEvaluator::aggregate, preflop_rule pass). gain_preflop = best_response_preflop - ev.
- O6 the evaluator requires a --charts set; the reference snapshot's chart directory is passed. Chart losses
  are not compared.

Modes: default = write the policies, run the evaluator, compare; --plan prints the evaluator commands and
writes nothing; --write-only writes policies, predictions and the command list without evaluating;
--compare-only compares values.json files already present (after running the listed commands by hand).
--flop-limit K runs a partial pass for plumbing only (the load and the strategy read-back are checked; the
values are not comparable on a partial pass).

Outputs in --out-dir (default out/monker/correctness/independent/S5a_designer): <game>/<policy>/
{policy.bin (deleted after a passing evaluation unless --keep-policies), prediction.json, values.json,
values.log}, commands.txt and sd_designer_policies.json (summary, max differences per quantity).
Exit codes: 0 pass, 1 mismatch or evaluator failure, 2 usage, input or format error.

Before any file is written the closed form checks itself (also --self-test, engine-free, about 6 s for 16
cases): the recursion equals a separate sum over terminal paths (EVs and combo values, 1e-12), EV0 + EV1 = 0
without rake, the symmetric lines b-e give EV0 = EV1 (0 without rake), gain_preflop >= 0.

Cost (HU6 games, 37 nodes, reference tables buckets_15x4, 8,532,369 entries): a policy is 68 MB, written in
about 9 s (pure-Python FNV-1a trailer, measured); the prediction takes about 0.3 s. The evaluator took 23.5 s stage one
+ 4.7 s load at 8 threads on the P1 smoke (peak commit 0.19 GB), so about 1.5 min per evaluation at 2
threads [inferred]: 16 evaluations (8 policies x 2 games) about 25-30 min at --threads 2, about 15 min at 4.
This script's own peak memory is about 0.4 GB [inferred: 68 MB table + its bytes copy + 630 x 630 matrices].

Usage: python tools/independent/sd_designer_policies.py [--plan | --write-only | --compare-only]
       [--games HU6_all,HU6_all_rake25cap2] [--policies a,b,c,d,e,f,g,h] [--threads 2]
       [--game-exe PATH | --dump-dir DIR] [--tree auto|engine|referee] [--allin auto|python|engine]
       [--bin DIR] [--res DIR] [--out-dir DIR] [--keep-policies] [--tolerance 1e-9]
       [--case NAME CONFIG REFERENCE BUCKETS TEXTURE_MAP]
"""

from __future__ import annotations

import argparse
import json
import shlex
import subprocess
import sys
import time
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import sd_referee as referee  # noqa: E402
from sdref import cards as sdcards  # noqa: E402
from sdref import policy as sdpolicy  # noqa: E402
from sdref import resources  # noqa: E402
from sdref import rules as R  # noqa: E402

REPO = resources.REPO_ROOT
DEFAULT_OUT_DIR = resources.INDEPENDENT_OUT_DIR / "S5a_designer"
DEFAULT_PYTHON_ALLIN = resources.INDEPENDENT_OUT_DIR / "S2_allin" / "allin_counts_python.npz"
DEFAULT_BIN = REPO / "out" / "monker" / "bin_correct" / "c123"
DEFAULT_GAME_EXE = REPO / "out" / "build" / "windows-release-suite" / "benchmarks" / "gtosd_preflop_blueprint_game.exe"
VALUES_EXE = "gtosd_preflop_blueprint_monker_values.exe"
CORRECTNESS = REPO / "benchmarks" / "monker" / "correctness"
IDENTITY_MAP = REPO / "benchmarks" / "monker" / "textures" / "identity_texture_map.txt"
BUCKETS_15X4 = REPO / "out" / "monker" / "buckets_15x4"

# name -> (config, reference snapshot dir, bucket dir, texture map, expected tree fingerprint)
DEFAULT_CASES = {
    "HU6_all": (CORRECTNESS / "HU6_all.json", REPO / "out/monker/correctness/smoke/P1/charts/it_50",
                BUCKETS_15X4, IDENTITY_MAP, "fnv1a64:fb76ddcd880fec5f"),
    "HU6_all_rake25cap2": (CORRECTNESS / "HU6_all_rake25cap2.json",
                           REPO / "out/monker/correctness/smoke/P3/charts/it_50",
                           BUCKETS_15X4, IDENTITY_MAP, "fnv1a64:f226b87d43f28215"),
}
POLICY_NAMES = "abcdefgh"
PAIR_COUNT = 561  # C(34, 2) opponent combos disjoint from a hero combo
EXACT_FLOPS, EXACT_PHYSICAL_FLOPS, EXACT_BOARDS = 573, 7140, 605088


def log(message: str) -> None:
    print(f"[{time.strftime('%H:%M:%S')}] {message}", file=sys.stderr, flush=True)


def rel(path: Path) -> str:
    try:
        return Path(path).resolve().relative_to(REPO).as_posix()
    except ValueError:
        return Path(path).as_posix()


# --------------------------------------------------------------------------------------------- tree


class Tree:
    """HU tree in dump-record form: records[id] with kind, street, actor, edges (type, amount, child) and
    payoff rows {winners: [seat 0, seat 1]} in money units."""

    def __init__(self, header: dict, records: list[dict], source: str) -> None:
        self.header = header
        self.records = records
        self.source = source
        if [record["id"] for record in records] != list(range(len(records))):
            raise ValueError(f"{source}: node ids are not 0..n-1")
        players = header.get("player_count", len(header.get("positions", [])))
        if players != 2:
            raise ValueError(f"{source}: {players} players; the closed form is heads-up only")
        self.units = header.get("units_per_ante", R.UNITS_PER_ANTE)

    def street(self, node: int) -> int:
        return sdpolicy.STREETS.index(self.records[node]["street"])

    def types(self, node: int) -> list[str]:
        return [edge["type"] for edge in self.records[node]["edges"]]

    def children(self, node: int) -> list[int]:
        return [edge["child"] for edge in self.records[node]["edges"]]

    def payoff_rows(self, node: int) -> dict[int, list[int]]:
        return {row["winners"]: row["payoff"] for row in self.records[node]["payoffs"]}

    def preflop_hero_nodes(self, hero: int) -> list[int]:
        return [r["id"] for r in self.records
                if r["kind"] == "decision" and r["street"] == "preflop" and r["actor"] == hero]


def referee_tree(config: Path) -> Tree:
    rules = R.load_rules(config)
    header, records = referee.synthesize_dump(rules, R.Conventions(), "full")
    return Tree(header, records, f"referee:{rel(config)}")


def engine_tree(dump: Path) -> Tree:
    header, records = referee.load_dump(dump)
    if header.get("mode", "full") != "full":
        raise ValueError(f"{dump}: mode {header.get('mode')}, expected full")
    return Tree(header, records, f"engine:{rel(dump)}")


def compare_trees(engine: Tree, ref: Tree, limit: int = 20) -> list[str]:
    """Differences between the engine dump and the referee's tree in what S5a uses: kinds, streets, actors,
    edges and payoff rows (empty = equal). The public states are S3's business (sd_referee.py)."""
    problems = []
    if len(engine.records) != len(ref.records):
        return [f"node count engine {len(engine.records)} referee {len(ref.records)}"]
    for mine, theirs in zip(engine.records, ref.records):
        node = mine["id"]
        for key in ("kind", "street", "actor"):
            if mine.get(key) != theirs.get(key):
                problems.append(f"node {node} {key}: engine {mine.get(key)} referee {theirs.get(key)}")
        edges_e = [(e["type"], e.get("amount"), e["child"]) for e in mine["edges"]]
        edges_r = [(e["type"], e.get("amount"), e["child"]) for e in theirs["edges"]]
        if edges_e != edges_r:
            problems.append(f"node {node} edges: engine {edges_e} referee {edges_r}")
        rows_e = sorted((row["winners"], list(row["payoff"])) for row in mine["payoffs"])
        rows_r = sorted((row["winners"], list(row["payoff"])) for row in theirs["payoffs"])
        if rows_e != rows_r:
            problems.append(f"node {node} payoffs: engine {rows_e} referee {rows_r}")
        if len(problems) >= limit:
            break
    return problems


# --------------------------------------------------------------------------------------- policies


def _prefer(types: list[str], order: tuple[str, ...]) -> np.ndarray:
    row = np.zeros(len(types))
    for kind in order:
        if kind in types:
            row[types.index(kind)] = 1.0
            return row
    raise ValueError(f"none of {order} among the actions {types}")


def _weights(values) -> np.ndarray:
    row = np.asarray(values, dtype=np.float64)
    return row / row.sum()


PASSIVE = ("check", "call")
SHOVE = ("all_in", "call")


def designer_row(policy: str, tree: Tree, node: int, hand_class: int | None) -> np.ndarray:
    """Row of `policy` at decision `node` (hand_class None on a postflop street: hand-independent)."""
    record = tree.records[node]
    types = tree.types(node)
    actions = len(types)
    street = tree.street(node)
    actor = record["actor"]
    if policy == "a":
        return np.full(actions, 1.0 / actions)
    if policy in "bcdef" and street == 0:
        return _prefer(types, PASSIVE)
    if policy == "b":
        return _prefer(types, PASSIVE)
    if policy in "cde":
        shove_street = {"c": 1, "d": 2, "e": 3}[policy]
        return _prefer(types, SHOVE if street == shove_street else PASSIVE)
    if policy == "f":
        if "fold" in types:
            return _prefer(types, ("fold",))
        if actor == 0:
            return _weights([1.0 if kind == "check" else 2.0 if kind == "all_in" else 0.0 for kind in types])
        return _weights([1.0 if kind in ("check", "all_in") else 0.0 for kind in types])
    if policy == "g":
        # Deterministic positive weights: by class and action preflop, by node and action later.
        if street == 0:
            return _weights([1.0 + ((7 * hand_class + 3 * action + 5 * node) % 11) for action in range(actions)])
        return _weights([1.0 + ((5 * node + 3 * action) % 7) for action in range(actions)])
    if policy == "h":
        if street != 0:
            return _prefer(types, PASSIVE)
        # Pure rows by class id: fold / passive / aggressive split so that every preflop line is used by
        # some classes and never by others.
        choice = (hand_class * 5 + node) % 3
        if choice == 0 and "fold" in types:
            return _prefer(types, ("fold",))
        if choice == 2:
            return _prefer(types, ("all_in", "call"))
        return _prefer(types, PASSIVE)
    raise ValueError(f"unknown designer policy {policy!r}")


def policy_rows(policy: str, tree: Tree) -> dict[int, np.ndarray]:
    """node -> (81, actions) preflop rows or (actions,) postflop row, for every decision node."""
    rows = {}
    for record in tree.records:
        if record["kind"] != "decision":
            continue
        node = record["id"]
        if tree.street(node) == 0:
            rows[node] = np.stack([designer_row(policy, tree, node, c) for c in range(sdcards.CLASS_COUNT)])
        else:
            rows[node] = designer_row(policy, tree, node, None)
        if np.any(rows[node] < 0) or not np.allclose(rows[node].sum(axis=-1), 1.0, rtol=0, atol=1e-15):
            raise ValueError(f"policy {policy} node {node}: rows do not sum to one")
    return rows


# ------------------------------------------------------------------------------------- closed form


class Outcomes:
    """Pair outcome probabilities (630 x 630, row = seat 0 combo, column = seat 1 combo), zero on
    overlapping pairs."""

    def __init__(self, wins: np.ndarray, ties: np.ndarray, source: str) -> None:
        n = float(sdcards.ALL_IN_RUNOUT_COUNT)
        self.disjoint = sdcards.combos_disjoint_matrix().astype(np.float64)
        losses = wins.T
        total = wins + ties + losses
        if not np.all(total[self.disjoint > 0] == sdcards.ALL_IN_RUNOUT_COUNT):
            raise ValueError(f"{source}: W + T + L != 201,376 on a disjoint pair")
        self.win = wins / n * self.disjoint
        self.tie = ties / n * self.disjoint
        self.lose = losses / n * self.disjoint
        self.source = source


def load_outcomes(choice: str, python_path: Path, engine_path: Path) -> tuple[Outcomes, dict]:
    info = {"requested": choice}
    if choice == "python" or (choice == "auto" and python_path.exists()):
        wins, ties, meta = resources.read_python_allin(python_path)
        info.update(source="python", path=rel(python_path), metadata=meta)
        outcomes = Outcomes(wins, ties, f"python:{rel(python_path)}")
        if engine_path.exists():
            engine = resources.read_all_in(engine_path, verify=False)
            e_wins, e_ties = resources.all_in_matrices(engine.entries)
            mask = sdcards.combos_disjoint_matrix()
            info["equal_to_engine_table"] = bool(np.array_equal(wins[mask], e_wins[mask])
                                                 and np.array_equal(ties[mask], e_ties[mask]))
        return outcomes, info
    engine = resources.read_all_in(engine_path, verify=True)
    if engine.checksum_verified is False:
        raise ValueError(f"{engine_path}: checksum mismatch")
    wins, ties = resources.all_in_matrices(engine.entries)
    info.update(source="engine", path=rel(engine_path), fingerprint=engine.fingerprint,
                note="the Python counts of S2 were not found; S2 compares this table with them")
    return Outcomes(wins, ties, f"engine:{rel(engine_path)}"), info


def predict(tree: Tree, rows: dict[int, np.ndarray], outcomes: Outcomes) -> dict:
    """Closed form G1-G3 and outputs O1-O3 for both heroes."""
    units = float(tree.units)
    klass = sdcards.COMBO_CLASS.astype(np.int64)
    disjoint = outcomes.disjoint

    def terminal_value(node: int):
        record = tree.records[node]
        payoffs = tree.payoff_rows(node)
        if record["kind"] == "terminal_fold":
            (row,) = payoffs.values()
            return [np.float64(row[0] / units), np.float64(row[1] / units)]
        if set(payoffs) != {1, 2, 3}:
            raise ValueError(f"node {node}: showdown winner masks {sorted(payoffs)}, expected 1, 2, 3")
        return [(payoffs[1][s] * outcomes.win + payoffs[2][s] * outcomes.lose + payoffs[3][s] * outcomes.tie)
                / units for s in (0, 1)]

    kept: dict[int, list] = {}  # values of the children of preflop decision nodes and of the root

    def value(node: int):
        record = tree.records[node]
        kind = record["kind"]
        if kind in ("terminal_fold", "terminal_showdown"):
            result = terminal_value(node)
        elif kind == "chance":
            (child,) = tree.children(node)
            result = value(child)
        else:
            children = [value(child) for child in tree.children(node)]
            prob = rows[node]
            result = [np.float64(0.0), np.float64(0.0)]
            for action, child in enumerate(children):
                if tree.street(node) == 0:
                    weight = prob[klass, action]
                    weight = weight[:, None] if record["actor"] == 0 else weight[None, :]
                else:
                    weight = prob[action]
                result = [result[s] + weight * child[s] for s in (0, 1)]
            if tree.street(node) == 0:
                for action, child in enumerate(tree.children(node)):
                    kept[child] = children[action]
        if node == 0:
            kept[0] = result
        return result

    value(0)

    # Own preflop reach of each seat to every preflop node (product of its class rows along the path).
    reach = {0: [np.ones(sdcards.COMBO_COUNT), np.ones(sdcards.COMBO_COUNT)]}
    for record in tree.records:
        node = record["id"]
        if record["kind"] != "decision" or tree.street(node) != 0:
            continue
        for action, child in enumerate(tree.children(node)):
            child_reach = [reach[node][0].copy(), reach[node][1].copy()]
            child_reach[record["actor"]] *= rows[node][klass, action]
            reach[child] = child_reach

    def hero_vector(matrix, hero: int, opponent_reach: np.ndarray) -> np.ndarray:
        """(1/561) sum_o D[h, o] reach(o) M[h, o] for the hero's combos h."""
        matrix = np.broadcast_to(matrix, disjoint.shape)
        if hero == 0:
            return (matrix * disjoint) @ opponent_reach / PAIR_COUNT
        return opponent_reach @ (matrix * disjoint) / PAIR_COUNT

    result = {"heroes": []}
    for hero in (0, 1):
        opp = 1 - hero
        root_values = hero_vector(kept[0][hero], hero, np.ones(sdcards.COMBO_COUNT))
        nodes = []
        for node in tree.preflop_hero_nodes(hero):
            r_opp = reach[node][opp]
            opponent_reach = hero_vector(np.float64(1.0), hero, r_opp)
            class_weight = np.bincount(klass, weights=opponent_reach, minlength=sdcards.CLASS_COUNT)
            combo_values, class_ev = [], []
            for child in tree.children(node):
                cv = hero_vector(kept[child][hero], hero, r_opp)
                sums = np.bincount(klass, weights=cv, minlength=sdcards.CLASS_COUNT)
                ev = np.where(class_weight > 0.0, sums / np.where(class_weight > 0.0, class_weight, 1.0), 0.0)
                combo_values.append(cv)
                class_ev.append(ev)
            nodes.append({"node": node, "opponent_reach": opponent_reach, "class_weight": class_weight,
                          "combo_values": combo_values, "class_ev": class_ev,
                          "strategy": rows[node]})

        # Best response at the preflop only (per combo), average strategy from the flop on.
        def best(node: int) -> np.ndarray:
            record = tree.records[node]
            if record["kind"] != "decision" or tree.street(node) != 0:
                return hero_vector(value_cache(node)[hero], hero, reach[node][opp])
            children = [best(child) for child in tree.children(node)]
            if record["actor"] == hero:
                return np.max(np.stack(children), axis=0)
            return np.sum(np.stack(children), axis=0)

        def value_cache(node: int):
            if node not in kept:
                raise KeyError(f"no value kept for node {node}")
            return kept[node]

        best_root = best(0)
        ev = float(root_values.mean())
        best_preflop = float(best_root.mean())
        result["heroes"].append({"hero": hero, "ev": ev, "root_values": root_values, "nodes": nodes,
                                 "best_response_preflop": best_preflop, "gain_preflop": best_preflop - ev})
    result["ev"] = [result["heroes"][0]["ev"], result["heroes"][1]["ev"]]
    result["ev_sum"] = result["ev"][0] + result["ev"][1]
    return result


def predict_by_paths(tree: Tree, rows: dict[int, np.ndarray], outcomes: Outcomes) -> dict:
    """Second, differently structured evaluation of the same closed form (self-test only): a sum over the
    terminals of path probability x pair value, no recursion. Returns ev and, per hero preflop node,
    combo_values."""
    units = float(tree.units)
    klass = sdcards.COMBO_CLASS.astype(np.int64)
    disjoint = outcomes.disjoint
    ones = np.ones(sdcards.COMBO_COUNT)
    paths = {}
    for record in tree.records:
        node = record["id"]
        paths[node] = [] if record["parent"] is None else paths[record["parent"]] + [(record["parent"],
                                                                                        record["edge_index"])]
    ev = np.zeros(2)
    cv = {}
    for record in tree.records:
        if not record["kind"].startswith("terminal"):
            continue
        payoffs = tree.payoff_rows(record["id"])
        if record["kind"] == "terminal_fold":
            (row,) = payoffs.values()
            utility = [np.full(disjoint.shape, row[s] / units) for s in (0, 1)]
        else:
            utility = [(payoffs[1][s] * outcomes.win + payoffs[2][s] * outcomes.lose
                        + payoffs[3][s] * outcomes.tie) / units for s in (0, 1)]
        utility = [matrix * disjoint for matrix in utility]
        path = paths[record["id"]]

        def seat_reach(seat: int, start: int) -> np.ndarray:
            vector = ones.copy()
            for node, edge in path[start:]:
                parent = tree.records[node]
                if parent["kind"] == "decision" and parent["actor"] == seat:
                    vector = vector * (rows[node][klass, edge] if tree.street(node) == 0 else rows[node][edge])
            return vector

        reach = [seat_reach(0, 0), seat_reach(1, 0)]
        for s in (0, 1):
            ev[s] += reach[0] @ utility[s] @ reach[1]
        for position, (node, edge) in enumerate(path):
            parent = tree.records[node]
            if parent["kind"] != "decision" or tree.street(node) != 0:
                continue
            hero = parent["actor"]
            own = seat_reach(hero, position + 1)
            opp = seat_reach(1 - hero, 0)
            if hero == 0:
                contribution = own * (utility[0] @ opp) / PAIR_COUNT
            else:
                contribution = own * (opp @ utility[1]) / PAIR_COUNT
            key = (node, edge)
            cv[key] = cv.get(key, 0.0) + contribution
    return {"ev": (ev / (sdcards.COMBO_COUNT * PAIR_COUNT)).tolist(), "combo_values": cv}


def self_test(outcomes: Outcomes, cases: dict, policies: list[str]) -> dict:
    """Engine-free checks of the closed form: recursion = path sum (ev, combo values); without rake EV0 +
    EV1 = 0; the symmetric lines b-e give EV0 = EV1 (= 0 without rake, = -rake / 2 with it)."""
    results = []
    for name, case in cases.items():
        tree = referee_tree(case["config"])
        raked = R.load_rules(case["config"]).rake.enabled
        for policy in policies:
            rows = policy_rows(policy, tree)
            pred = predict(tree, rows, outcomes)
            paths = predict_by_paths(tree, rows, outcomes)
            worst = float(np.max(np.abs(np.array(pred["ev"]) - np.array(paths["ev"]))))
            for hero in pred["heroes"]:
                for entry in hero["nodes"]:
                    for edge, values in enumerate(entry["combo_values"]):
                        other = paths["combo_values"].get((entry["node"], edge), np.zeros(sdcards.COMBO_COUNT))
                        worst = max(worst, float(np.max(np.abs(values - other))))
            checks = {"recursion_equals_path_sum": worst <= 1e-12}
            if not raked:
                checks["zero_sum"] = abs(pred["ev_sum"]) <= 1e-12
            if policy in "bcde":
                checks["symmetric_line"] = abs(pred["ev"][0] - pred["ev"][1]) <= 1e-12
                if not raked:
                    checks["symmetric_line_zero"] = abs(pred["ev"][0]) <= 1e-12
            checks["gain_preflop_non_negative"] = all(h["gain_preflop"] >= -1e-12 for h in pred["heroes"])
            results.append({"game": name, "policy": policy, "ev": pred["ev"], "path_sum_max_diff": worst,
                            "checks": checks, "passed": all(checks.values())})
            log(f"self-test {name}/{policy}: {'PASS' if results[-1]['passed'] else 'FAIL'} "
                f"path-sum diff {worst:.1e} {checks}")
    return {"passed": all(item["passed"] for item in results), "cases": results}


def prediction_json(pred: dict) -> dict:
    def plain(value):
        if isinstance(value, np.ndarray):
            return value.tolist()
        if isinstance(value, (np.floating,)):
            return float(value)
        if isinstance(value, dict):
            return {key: plain(item) for key, item in value.items()}
        if isinstance(value, list):
            return [plain(item) for item in value]
        return value
    return plain(pred)


# ------------------------------------------------------------------------------------- comparison


class Diff:
    def __init__(self, tolerance: float) -> None:
        self.tolerance = tolerance
        self.max: dict[str, float] = {}
        self.worst: dict[str, str] = {}
        self.failures: list[str] = []

    def add(self, quantity: str, engine, predicted, where: str) -> None:
        engine = np.asarray(engine, dtype=np.float64)
        predicted = np.asarray(predicted, dtype=np.float64)
        if engine.shape != predicted.shape:
            self.failures.append(f"{quantity} {where}: shape {engine.shape} vs {predicted.shape}")
            return
        if engine.size == 0:
            return
        if not np.all(np.isfinite(engine)):
            self.failures.append(f"{quantity} {where}: non-finite engine value")
            return
        delta = float(np.max(np.abs(engine - predicted)))
        if delta > self.max.get(quantity, -1.0):
            self.max[quantity] = delta
            self.worst[quantity] = where
        if delta > self.tolerance:
            self.failures.append(f"{quantity} {where}: max |engine - predicted| {delta:.3e}")

    def fail(self, message: str) -> None:
        self.failures.append(message)


def compare(values: dict, pred: dict, tree: Tree, rows: dict, tolerance: float, exact: bool) -> dict:
    diff = Diff(tolerance)
    evaluation = values.get("evaluation", {})
    checks = values.get("self_checks", {})
    if not checks.get("passed", False):
        diff.fail(f"evaluator self_checks failed: {checks.get('failures')}")
    if values.get("classes") != list(sdcards.CLASS_NAMES):
        diff.fail("values.json classes differ from sdref.cards.CLASS_NAMES")
    if values.get("combos", {}).get("class") != sdcards.COMBO_CLASS.tolist():
        diff.fail("values.json combos.class differs from sdref.cards.COMBO_CLASS")
    if exact:
        got = (evaluation.get("mode"), evaluation.get("flops"), evaluation.get("physical_flops"),
               evaluation.get("boards"))
        if got != ("exact", EXACT_FLOPS, EXACT_PHYSICAL_FLOPS, EXACT_BOARDS):
            diff.fail(f"evaluation {got}, expected ('exact', 573, 7140, 605088)")
    labels = list(sdcards.CLASS_NAMES)
    for hero_json, hero_pred in zip(values["heroes"], pred["heroes"]):
        hero = hero_json["hero"]
        tag = f"hero {hero}"
        node_ids = [node["node"] for node in hero_json["nodes"]]
        if node_ids != [entry["node"] for entry in hero_pred["nodes"]]:
            diff.fail(f"{tag}: preflop nodes {node_ids}, predicted {[e['node'] for e in hero_pred['nodes']]}")
            continue
        for node_json, entry in zip(hero_json["nodes"], hero_pred["nodes"]):
            node = entry["node"]
            tokens = node_json["tokens"]
            where = f"{tag} node {node} ({node_json.get('chart')})"
            if len(tokens) != len(tree.records[node]["edges"]):
                diff.fail(f"{where}: {len(tokens)} tokens, {len(tree.records[node]['edges'])} edges")
                continue
            strategy = np.array([[node_json["strategy"][label][token] for token in tokens] for label in labels])
            diff.add("strategy_read_back", strategy, rows[node], where)
            if not exact:
                continue
            weight = np.array([node_json["class_weight"][label] for label in labels])
            diff.add("class_weight", weight, entry["class_weight"], where)
            class_ev = np.array([[node_json["class_ev"][label][token]["ev"] for label in labels]
                                 for token in tokens])
            diff.add("class_ev", class_ev, np.stack(entry["class_ev"]), where)
            if "opponent_reach" in node_json:
                diff.add("opponent_reach", node_json["opponent_reach"], entry["opponent_reach"], where)
            if "combo_values" in node_json:
                combo = np.array([node_json["combo_values"][token] for token in tokens])
                diff.add("combo_values", combo, np.stack(entry["combo_values"]), where)
            elif exact:
                diff.fail(f"{where}: no combo_values (evaluator run with --no-combo-values?)")
        if not exact:
            continue
        diff.add("ev", hero_json["ev_antes"], hero_pred["ev"], tag)
        diff.add("root_value_mean", hero_json["root_value_mean_antes"], hero_pred["ev"], tag)
        if "root_values" in hero_json:
            diff.add("root_values", hero_json["root_values"], hero_pred["root_values"], tag)
    if exact:
        estimate = values["estimate"]
        diff.add("estimate_ev", estimate["ev_antes"], pred["ev"], "estimate")
        diff.add("best_response_preflop", estimate["best_response_preflop_antes"],
                 [h["best_response_preflop"] for h in pred["heroes"]], "estimate")
        diff.add("gain_preflop", estimate["gain_preflop_antes"], [h["gain_preflop"] for h in pred["heroes"]],
                 "estimate")
        for response in values.get("preflop_response", []):
            hero = response["hero"]
            gain = pred["heroes"][hero]["gain_preflop"]
            diff.add("preflop_response_gain_per_combo", response["gain_per_combo_antes"], gain, f"hero {hero}")
            diff.add("preflop_response_gain_per_class", response["gain_per_class_antes"], gain, f"hero {hero}")
        if "expected_rake_antes" in estimate:
            diff.add("expected_rake", estimate["expected_rake_antes"], -pred["ev_sum"], "estimate")
        elif "rake" not in values:
            diff.add("zero_sum", estimate["ev_antes"][0] + estimate["ev_antes"][1], 0.0, "estimate")
    return {"passed": not diff.failures, "tolerance": tolerance, "max_abs_diff": diff.max,
            "worst": diff.worst, "failures": diff.failures[:50], "failure_count": len(diff.failures)}


# --------------------------------------------------------------------------------------- the runs


def resolve_reference(reference: Path) -> tuple[sdpolicy.PolicyHeader, Path, str]:
    """(header fields, chart dir, kind) from a policy.bin, a values.json or a snapshot directory."""
    reference = Path(reference)
    if reference.is_dir():
        candidates = [reference / "policy.bin", reference / "values.json"]
        reference = next((path for path in candidates if path.exists()), candidates[0])
    if not reference.exists():
        raise FileNotFoundError(f"reference {reference} not found")
    if reference.suffix == ".json":
        data = json.loads(reference.read_text(encoding="utf-8"))
        flop, turn, river = data["capacities"]
        header = sdpolicy.PolicyHeader(data["tree_fingerprint"], flop, turn, river, -1, data["policy_source"])
        return header, reference.parent, "values.json"
    header, _, _ = sdpolicy.read_header(reference)
    return header, reference.parent, "policy.bin"


def evaluator_command(args, case: dict, policy_path: Path, values_path: Path) -> list[str]:
    def text(path) -> str:
        return Path(path).as_posix()

    command = [text(args.bin / VALUES_EXE), "--config", text(case["config"]), "--resources-dir", text(args.res),
               "--buckets-dir", text(case["buckets"]), "--board-class-rows", "--board-texture-map",
               text(case["texture"]), "--policy", text(policy_path), "--charts", f"ref={text(case['charts'])}",
               "--all-flops", "--threads", str(args.threads), "--out", text(values_path)]
    if args.flop_limit:
        command += ["--flop-limit", str(args.flop_limit)]
    return command


def load_tree(args, name: str, config: Path, work: Path) -> tuple[Tree, dict]:
    info = {"requested": args.tree}
    ref = referee_tree(config)
    engine = None
    dump = None
    if args.dump_dir is not None:
        dump = args.dump_dir / f"{name}.nodes.jsonl"
    elif args.tree != "referee" and args.game_exe is not None and args.game_exe.exists():
        dump = work / f"{name}.nodes.jsonl"
        work.mkdir(parents=True, exist_ok=True)
        completed = subprocess.run([str(args.game_exe), "--config", str(config), "--dump-nodes", str(dump)],
                                   capture_output=True, text=True, check=False)
        if completed.returncode != 0:
            info["engine_dump_error"] = (completed.stderr or completed.stdout).strip()[-300:]
            dump = None
    if dump is not None and dump.exists():
        engine = engine_tree(dump)
        info["engine_dump"] = rel(dump)
        info["engine_tree_fingerprint"] = engine.header.get("tree_fingerprint")
        problems = compare_trees(engine, ref)
        info["engine_equals_referee"] = not problems
        info["differences"] = problems
    if args.tree == "engine" and engine is None:
        raise RuntimeError(f"{name}: --tree engine but no engine dump ({info.get('engine_dump_error', 'no exe')})")
    info["used"] = "referee" if engine is None else "referee (engine dump equal)" if info["engine_equals_referee"] \
        else "referee (engine dump DIFFERS)"
    return ref, info


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--plan", action="store_true", help="print the evaluator commands only")
    mode.add_argument("--write-only", action="store_true", help="write policies and predictions, no evaluation")
    mode.add_argument("--compare-only", action="store_true", help="compare values.json already present")
    mode.add_argument("--self-test", action="store_true",
                      help="engine-free checks of the closed form only (also run before every other mode)")
    parser.add_argument("--games", default=",".join(DEFAULT_CASES))
    parser.add_argument("--case", nargs=5, action="append", default=[],
                        metavar=("NAME", "CONFIG", "REFERENCE", "BUCKETS", "TEXTURE_MAP"),
                        help="an extra game (reference = policy.bin, values.json or snapshot dir)")
    parser.add_argument("--policies", default=",".join(POLICY_NAMES))
    parser.add_argument("--threads", type=int, default=2)
    parser.add_argument("--flop-limit", type=int, default=0, help="partial pass (plumbing only)")
    parser.add_argument("--tree", choices=("auto", "engine", "referee"), default="auto")
    parser.add_argument("--game-exe", type=Path, default=DEFAULT_GAME_EXE,
                        help="gtosd_preflop_blueprint_game with --dump-nodes (built after fdf8114)")
    parser.add_argument("--dump-dir", type=Path, default=None, help="existing dumps <game>.nodes.jsonl")
    parser.add_argument("--allin", choices=("auto", "python", "engine"), default="auto")
    parser.add_argument("--python-allin", type=Path, default=DEFAULT_PYTHON_ALLIN)
    parser.add_argument("--bin", type=Path, default=DEFAULT_BIN)
    parser.add_argument("--res", type=Path, default=resources.RESOURCES_DIR)
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT_DIR)
    parser.add_argument("--keep-policies", action="store_true")
    parser.add_argument("--tolerance", type=float, default=1e-9)
    args = parser.parse_args()

    cases = {}
    for name in [item for item in args.games.split(",") if item]:
        if name not in DEFAULT_CASES:
            parser.error(f"unknown game {name} (known: {', '.join(DEFAULT_CASES)}; or use --case)")
        config, reference, buckets, texture, expected = DEFAULT_CASES[name]
        cases[name] = {"config": config, "reference": reference, "buckets": buckets, "texture": texture,
                       "expected_tree": expected}
    for name, config, reference, buckets, texture in args.case:
        cases[name] = {"config": Path(config), "reference": Path(reference), "buckets": Path(buckets),
                       "texture": Path(texture), "expected_tree": None}
    policies = [item for item in args.policies.split(",") if item]
    for policy in policies:
        if policy not in POLICY_NAMES:
            parser.error(f"unknown policy {policy} (known: {', '.join(POLICY_NAMES)})")
    if args.threads < 1 or args.flop_limit < 0:
        parser.error("--threads >= 1, --flop-limit >= 0")
    exact = args.flop_limit == 0

    # Plan: commands only, nothing written.
    commands = []
    for name, case in cases.items():
        try:
            _, charts, _ = resolve_reference(case["reference"])
        except (OSError, ValueError, KeyError):
            charts = Path(case["reference"])
        case["charts"] = charts
        for policy in policies:
            work = args.out_dir / name / policy
            commands.append(evaluator_command(args, case, work / "policy.bin", work / "values.json"))
    command_text = "\n".join(" ".join(shlex.quote(part) for part in command) for command in commands)
    if args.plan:
        print(command_text)
        log(f"plan: {len(commands)} evaluations ({len(cases)} games x {len(policies)} policies), "
            f"{args.threads} threads each; nothing written")
        return 0

    started = time.time()
    try:
        outcomes, allin_info = load_outcomes(args.allin, args.python_allin, args.res / "preflop_all_in_v1.bin")
    except (OSError, ValueError, KeyError) as error:
        log(f"all-in counts: {error}")
        return 2
    log(f"all-in counts: {allin_info['source']} ({allin_info['path']})")
    try:
        closed_form = self_test(outcomes, cases, policies)
    except (OSError, ValueError, KeyError) as error:
        log(f"self-test: {error}")
        return 2
    if args.self_test:
        log(f"self-test {'PASS' if closed_form['passed'] else 'FAIL'}")
        return 0 if closed_form["passed"] else 1
    args.out_dir.mkdir(parents=True, exist_ok=True)
    (args.out_dir / "commands.txt").write_text(command_text + "\n", encoding="utf-8")
    summary = {"schema": "gtosd.independent.designer_policies.v1", "check": "S5a",
               "started": time.strftime("%Y-%m-%d %H:%M:%S"), "exact": exact, "tolerance": args.tolerance,
               "allin": allin_info, "evaluator": rel(args.bin / VALUES_EXE), "threads": args.threads,
               "self_test": closed_form, "games": {}}
    failed = not closed_form["passed"]
    for name, case in cases.items():
        game = {"config": rel(case["config"]), "reference": rel(case["reference"]), "policies": {}}
        summary["games"][name] = game
        try:
            reference, charts, kind = resolve_reference(case["reference"])
            tree, tree_info = load_tree(args, name, case["config"], args.out_dir / name)
        except (OSError, ValueError, RuntimeError, KeyError) as error:
            log(f"{name}: {error}")
            game["error"] = str(error)
            failed = True
            continue
        game["tree"] = tree_info
        if tree_info.get("engine_equals_referee") is False:
            log(f"{name}: engine dump differs from the referee's tree: {tree_info['differences'][:3]}")
            failed = True
        slots, entries = sdpolicy.layout(tree.records, reference.capacities)
        if reference.entries not in (-1, entries):
            log(f"{name}: layout {entries} entries, reference policy {reference.entries}: other tree")
            game["error"] = "layout entries differ from the reference policy"
            failed = True
            continue
        expected = case["expected_tree"]
        engine_fp = tree_info.get("engine_tree_fingerprint")
        for label, fingerprint in (("expected", expected), ("engine dump", engine_fp)):
            if fingerprint is not None and fingerprint != reference.tree_fingerprint:
                log(f"{name}: reference tree {reference.tree_fingerprint}, {label} {fingerprint}")
                game["error"] = f"tree fingerprint differs from the {label} one"
                failed = True
        if "error" in game:
            continue
        game.update(reference_kind=kind, tree_fingerprint=reference.tree_fingerprint, entries=entries,
                    capacities=list(reference.capacities), charts=rel(charts))
        for policy in policies:
            work = args.out_dir / name / policy
            work.mkdir(parents=True, exist_ok=True)
            policy_path, values_path = work / "policy.bin", work / "values.json"
            record = {}
            game["policies"][policy] = record
            rows = policy_rows(policy, tree)
            pred = predict(tree, rows, outcomes)
            (work / "prediction.json").write_text(json.dumps(prediction_json(
                {"game": name, "policy": policy, "allin": allin_info["source"], **pred}), indent=1),
                encoding="utf-8")
            record["predicted_ev"] = pred["ev"]
            command = evaluator_command(args, case, policy_path, values_path)
            if not args.compare_only:
                header = sdpolicy.PolicyHeader(reference.tree_fingerprint, *reference.capacities, entries,
                                               f"sd_designer_policies:S5a-{policy}|{reference.source}")
                table = sdpolicy.build_table(slots, entries, lambda slot: rows[slot.node])
                record["file_checksum"] = sdpolicy.write_policy(policy_path, header, table)
                del table
                log(f"{name}/{policy}: wrote {rel(policy_path)} ({entries} entries), predicted EV "
                    f"{pred['ev'][0]:.12f} / {pred['ev'][1]:.12f}")
            if args.write_only:
                continue
            if not args.compare_only:
                if values_path.exists():
                    values_path.unlink()
                t0 = time.time()
                with open(work / "values.log", "w", encoding="utf-8") as handle:
                    completed = subprocess.run(command, stdout=handle, stderr=subprocess.STDOUT, check=False)
                record["evaluator_exit"] = completed.returncode
                record["evaluator_seconds"] = round(time.time() - t0, 1)
                log(f"{name}/{policy}: evaluator exit {completed.returncode} in {record['evaluator_seconds']} s")
            if not values_path.exists():
                record["result"] = {"passed": False, "failures": ["no values.json"]}
                failed = True
                continue
            values = json.loads(values_path.read_text(encoding="utf-8"))
            if values.get("tree_fingerprint") != reference.tree_fingerprint:
                record["result"] = {"passed": False, "failures": ["values.json of another tree"]}
                failed = True
                continue
            record["policy_fingerprint"] = values.get("policy_fingerprint")
            record["engine_ev"] = values["estimate"]["ev_antes"]
            record["result"] = compare(values, pred, tree, rows, args.tolerance, exact)
            if record.get("evaluator_exit", 0) != 0:
                record["result"]["passed"] = False
                record["result"]["failures"].insert(0, f"evaluator exit {record['evaluator_exit']}")
            passed = record["result"]["passed"]
            failed |= not passed
            worst = max(record["result"]["max_abs_diff"].values(), default=float("nan"))
            log(f"{name}/{policy}: {'PASS' if passed else 'FAIL'} max diff {worst:.3e} "
                f"{record['result']['failures'][:2]}")
            if passed and not args.keep_policies and policy_path.exists() and not args.compare_only:
                policy_path.unlink()
    summary["seconds"] = round(time.time() - started, 1)
    summary["passed"] = not failed and not args.write_only
    (args.out_dir / "sd_designer_policies.json").write_text(json.dumps(summary, indent=1), encoding="utf-8")
    if args.write_only:
        print(command_text)
        log("write-only: policies and predictions written; run the commands above, then --compare-only")
        return 0 if not failed else 1
    log(f"S5a {'PASS' if summary['passed'] else 'FAIL'} in {summary['seconds']} s: "
        f"{rel(args.out_dir / 'sd_designer_policies.json')}")
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
