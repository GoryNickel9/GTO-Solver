"""Heads-up step-1 game (preflop + checkdown) at hand-class level: its exact value by a sequence-form LP, and a
certificate of that value computed with best responses at the combo level.

Independent reference of check S4 (coverage study shared_components.md section 3). The game is built from the
rules below with sdref/rules.py (S3) and the Python all-in counts of sdref (S2); the equilibrium is computed by
linear programming (scipy HiGHS), not by CFR, so no part of the engine's solver is mirrored.

Game (rules):
- G1 Deal: CO (seat 0) and BTN (seat 1) each receive two cards of the 36-card short deck; every ordered pair
     of disjoint combos is equally likely (630 x 561 = 353,430 ordered pairs).
- G2 Preflop betting by rules.py R1-R10 with the configuration's preflop action abstraction (C5-C7).
- G3 A preflop fold ends the hand (R8). When the preflop betting round closes with both players in the hand
     (a check, a call, or a called all-in), the hand ends in a showdown on a uniformly random five-card board
     from the 32 unseen cards (201,376 boards): after an all-in because nobody can bet any more (R10), without
     one because of the step-1 model, in which both players check every postflop street (convention C16 of
     rules.py: "checkdown leaf").
- G4 Showdown (R11) with the short-deck ranking of sdref.ranking (S1); the win / tie / loss board counts of
     every combo pair are the Python all-in counts (S2, sd_allin_check.py --exhaustive).
- G5 Payoffs: net results of rules.settle (R8, R11, R12) in units of 1e-4 ante, reported in antes. Without
     rake the payoffs sum to zero, so the game has a unique value v* = max_x min_y = min_y max_x (CO's
     expected net result).

Information: a player sees their own cards and the public action history. The LP restricts both players to
class-level strategies (the 81 preflop classes). That loses nothing (suit symmetry), but the certificate does
not rely on it: it evaluates the best response of each player at the combo level (630 combos, exact card
removal) against the other's LP strategy lifted to combos, so
    lower = min_y EV(x_LP, y)  <=  v*  <=  max_x EV(x, y_LP) = upper
holds for the physical game whatever the LP's accuracy.

Sequence form (von Stengel 1996): realization plans x (CO) and y (BTN), E x = e, F y = f, x, y >= 0, CO payoff
x^T A y with A[s0, s1] = sum over leaves with those sequences of chance weight x payoff. The two LPs are
    CO:  max f^T q  s.t.  F^T q - A^T x <= 0, E x = e, x >= 0;
    BTN: min e^T p  s.t.  A y - E^T p <= 0,  F y = f, y >= 0.

Conventions (choices, not rules):
- K1 Chance weights are integer counts of ordered disjoint combo pairs (N[a, b] per class pair), payoffs in
     antes; the LP matrix is scaled by 6,561 / 353,430 (mean class-pair weight 1); values are divided back.
- K2 An information set that the player's own plan never reaches gets the uniform strategy (it does not
     change any value).
- K3 Negative LP entries of magnitude below the solver tolerance are clipped to 0 before normalization.
"""

from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np
from scipy import optimize, sparse

from . import cards as sdcards
from . import rules as R

SEATS = 2
CO, BTN = 0, 1
RUNOUTS = sdcards.ALL_IN_RUNOUT_COUNT            # 201,376 boards per disjoint combo pair
ORDERED_DEALS = sdcards.COMBO_COUNT * 561        # 353,430 ordered disjoint combo pairs
LP_SCALE = sdcards.CLASS_COUNT ** 2 / ORDERED_DEALS
WIN_MASK = {CO: 1, BTN: 2}
TIE_MASK = 3


class Step1Error(ValueError):
    """The configuration does not define a heads-up zero-sum step-1 game."""


# ------------------------------------------------------------------------------------------------ tree


@dataclass
class Node:
    index: int
    path: tuple[str, ...]
    kind: str                       # decision | fold | showdown
    actor: int | None = None
    labels: list[str] = field(default_factory=list)
    children: list[int] = field(default_factory=list)
    settle_kind: str | None = None  # fold | runout | checkdown
    rows: dict[int, list[int]] = field(default_factory=dict)  # winner mask -> net result per seat (units)
    last: tuple = (None, None)      # per seat: (decision node, action index) of its last action on the path
    pot: int = 0


def build_tree(rules, conv=None) -> list[Node]:
    """The step-1 tree in preorder (G2, G3) with the settlement rows of every terminal (G5)."""
    if conv is None:
        conv = R.Conventions()
    if rules.players != SEATS:
        raise Step1Error("the step-1 LP is heads-up only")
    if rules.rake.enabled:
        raise Step1Error("rake enabled: the game is not zero-sum, so it has no unique value")
    nodes: list[Node] = []

    def visit(hand, path, last) -> int:
        index = len(nodes)
        status = hand.status
        if status == "in_progress":
            if hand.street != R.PREFLOP:
                raise Step1Error("a postflop decision in the step-1 tree")
            actions = R.abstraction_actions(rules, conv, hand)
            node = Node(index, path, "decision", actor=hand.to_act, labels=[a.token for a in actions],
                        last=last, pot=hand.pot)
            nodes.append(node)
            for k, action in enumerate(actions):
                child_last = list(last)
                child_last[hand.to_act] = (index, k)
                node.children.append(visit(R.apply_action(hand, action), path + (action.token,),
                                           tuple(child_last)))
            return index
        if status == "folded":
            winners = hand.in_hand_mask
            rows = {winners: R.settle(rules, conv, hand, "fold", winners)[0]}
            nodes.append(Node(index, path, "fold", settle_kind="fold", rows=rows, last=last, pot=hand.pot))
            return index
        if status in ("all_in_runout", "street_complete") and hand.street == R.PREFLOP:
            kind = "runout" if status == "all_in_runout" else "checkdown"
            rows = {mask: R.settle(rules, conv, hand, kind, mask)[0] for mask in (1, 2, TIE_MASK)}
            nodes.append(Node(index, path, "showdown", settle_kind=kind, rows=rows, last=last, pot=hand.pot))
            return index
        raise Step1Error(f"unexpected state {status} on street {hand.street}")

    visit(R.initial_hand(rules), (), (None, None))
    for node in nodes:
        for row in node.rows.values():
            if sum(row) != 0:
                raise Step1Error(f"payoff row {row} at {node.path} does not sum to zero")
    return nodes


# ------------------------------------------------------------------------------------ outcome tables


@dataclass
class Outcomes:
    """Combo-level (630 x 630) and class-level (81 x 81) outcome counts, rows = CO, columns = BTN.
    wins[a, b] = boards on which a beats b, ties[a, b] = split boards, disjoint[a, b] = no shared card."""
    wins: np.ndarray
    ties: np.ndarray
    disjoint: np.ndarray
    class_w: np.ndarray
    class_t: np.ndarray
    class_l: np.ndarray
    class_n: np.ndarray


def outcomes_from_counts(wins: np.ndarray, ties: np.ndarray) -> Outcomes:
    disjoint = sdcards.combos_disjoint_matrix()
    wins = np.where(disjoint, wins, 0).astype(np.int64)
    ties = np.where(disjoint, ties, 0).astype(np.int64)
    indicator = np.zeros((sdcards.CLASS_COUNT, sdcards.COMBO_COUNT), dtype=np.int64)
    indicator[sdcards.COMBO_CLASS, np.arange(sdcards.COMBO_COUNT)] = 1
    return Outcomes(wins, ties, disjoint,
                    indicator @ wins @ indicator.T, indicator @ ties @ indicator.T,
                    indicator @ wins.T @ indicator.T, indicator @ disjoint.astype(np.int64) @ indicator.T)


def outcome_checks(out: Outcomes) -> dict[str, bool]:
    """Consistency of the counts with G1 and G4 (not an engine comparison)."""
    total = out.wins + out.ties + out.wins.T
    return {
        "combo_counts_sum_to_runouts": bool(np.array_equal(total[out.disjoint],
                                                           np.full(int(out.disjoint.sum()), RUNOUTS))),
        "ties_symmetric": bool(np.array_equal(out.ties, out.ties.T)),
        "class_counts_sum_to_runouts": bool(np.array_equal(out.class_w + out.class_t + out.class_l,
                                                           out.class_n * RUNOUTS)),
        "ordered_deals_353430": int(out.class_n.sum()) == ORDERED_DEALS,
        "each_combo_561_opponents": bool(np.all(out.disjoint.sum(axis=1) == 561)),
    }


def class_values(node: Node, out: Outcomes, weights: np.ndarray | None = None) -> np.ndarray:
    """81 x 81 CO payoff (antes) of a terminal, summed over the ordered disjoint combo pairs of each class
    pair. `weights` (optional, 81 x 81) replaces the chance weights N (mutation tests only)."""
    scale = 1.0 / R.UNITS_PER_ANTE
    n = out.class_n.astype(np.float64)
    if node.kind == "fold":
        (row,) = node.rows.values()
        value = n * (row[CO] * scale)
    else:
        value = (out.class_w * (node.rows[1][CO] * scale) + out.class_t * (node.rows[TIE_MASK][CO] * scale)
                 + out.class_l * (node.rows[2][CO] * scale)) / RUNOUTS
    if weights is not None:
        value = value / n * weights
    return value


# ----------------------------------------------------------------------------------------- sequence form


@dataclass
class SequenceForm:
    nodes: list[Node]
    decisions: list[list[int]]          # per seat: its decision nodes (preorder)
    pairs: list[list[tuple[int, int]]]  # per seat: its (decision node, action) pairs
    pair_index: list[dict]              # per seat: (node, action) -> pair number
    sizes: list[int]                    # per seat: number of sequences (1 + 81 * pairs)
    constraints: list[sparse.csr_matrix]  # E (CO) and F (BTN)
    payoff: sparse.csr_matrix           # A: CO payoff, antes x ordered combo pairs x LP_SCALE


def sequence_id(form_pairs: int, pair_index: dict, cls: int, last) -> int:
    return 0 if last is None else 1 + cls * form_pairs + pair_index[last]


def build_sequence_form(nodes: list[Node], out: Outcomes, weights: np.ndarray | None = None) -> SequenceForm:
    classes = sdcards.CLASS_COUNT
    decisions = [[n.index for n in nodes if n.kind == "decision" and n.actor == seat] for seat in range(SEATS)]
    pairs = [[(d, k) for d in decisions[seat] for k in range(len(nodes[d].children))] for seat in range(SEATS)]
    pair_index = [{pair: j for j, pair in enumerate(pairs[seat])} for seat in range(SEATS)]
    sizes = [1 + classes * len(pairs[seat]) for seat in range(SEATS)]
    constraints = []
    for seat in range(SEATS):
        rows, cols, vals = [0], [0], [1.0]
        count = len(pairs[seat])
        for c in range(classes):
            for i, d in enumerate(decisions[seat]):
                row = 1 + c * len(decisions[seat]) + i
                for k in range(len(nodes[d].children)):
                    rows.append(row)
                    cols.append(sequence_id(count, pair_index[seat], c, (d, k)))
                    vals.append(1.0)
                rows.append(row)
                cols.append(sequence_id(count, pair_index[seat], c, nodes[d].last[seat]))
                vals.append(-1.0)
        shape = (1 + classes * len(decisions[seat]), sizes[seat])
        constraints.append(sparse.csr_matrix((vals, (rows, cols)), shape=shape))
    classes_range = np.arange(classes)
    rows, cols, vals = [], [], []
    for node in nodes:
        if node.kind == "decision":
            continue
        value = class_values(node, out, weights) * LP_SCALE
        row_ids = np.array([sequence_id(len(pairs[CO]), pair_index[CO], c, node.last[CO]) for c in classes_range])
        col_ids = np.array([sequence_id(len(pairs[BTN]), pair_index[BTN], c, node.last[BTN])
                            for c in classes_range])
        rows.append(np.repeat(row_ids, classes))
        cols.append(np.tile(col_ids, classes))
        vals.append(value.reshape(-1))
    payoff = sparse.coo_matrix((np.concatenate(vals), (np.concatenate(rows), np.concatenate(cols))),
                               shape=(sizes[CO], sizes[BTN])).tocsr()  # duplicates are summed
    return SequenceForm(nodes, decisions, pairs, pair_index, sizes, constraints, payoff)


@dataclass
class LpResult:
    value: float          # antes
    plan: np.ndarray      # realization plan of the seat
    status: int
    message: str
    iterations: int


HIGHS_OPTIONS = {"presolve": True, "primal_feasibility_tolerance": 1e-10, "dual_feasibility_tolerance": 1e-10}


def solve_seat(form: SequenceForm, seat: int, method: str = "highs-ds") -> LpResult:
    """The LP of `seat` (CO maximizes, BTN minimizes CO's payoff); the value is CO's expected result."""
    E, F = form.constraints
    A = form.payoff
    if seat == CO:
        n_x, n_q = form.sizes[CO], F.shape[0]
        objective = np.zeros(n_x + n_q)
        objective[n_x] = -1.0                               # max f^T q, f = unit vector of the empty sequence
        a_ub = sparse.hstack([-A.T, F.T]).tocsr()
        a_eq = sparse.hstack([E, sparse.csr_matrix((E.shape[0], n_q))]).tocsr()
        b_eq = np.zeros(E.shape[0])
        b_eq[0] = 1.0
        bounds = [(0, None)] * n_x + [(None, None)] * n_q
        sign, plan_size = -1.0, n_x
    else:
        n_y, n_p = form.sizes[BTN], E.shape[0]
        objective = np.zeros(n_y + n_p)
        objective[n_y] = 1.0                                # min e^T p
        a_ub = sparse.hstack([A, -E.T]).tocsr()
        a_eq = sparse.hstack([F, sparse.csr_matrix((F.shape[0], n_p))]).tocsr()
        b_eq = np.zeros(F.shape[0])
        b_eq[0] = 1.0
        bounds = [(0, None)] * n_y + [(None, None)] * n_p
        sign, plan_size = 1.0, n_y
    result = optimize.linprog(objective, A_ub=a_ub, b_ub=np.zeros(a_ub.shape[0]), A_eq=a_eq, b_eq=b_eq,
                              bounds=bounds, method=method, options=HIGHS_OPTIONS)
    if result.status != 0:
        return LpResult(float("nan"), np.zeros(plan_size), int(result.status), str(result.message),
                        int(getattr(result, "nit", 0) or 0))
    return LpResult(sign * float(result.fun) / (LP_SCALE * ORDERED_DEALS), np.asarray(result.x[:plan_size]),
                    0, str(result.message), int(getattr(result, "nit", 0) or 0))


def behavioral(form: SequenceForm, seat: int, plan: np.ndarray) -> dict[int, np.ndarray]:
    """Per decision node of `seat`: (81, actions) action probabilities from a realization plan (K2, K3)."""
    plan = np.clip(plan, 0.0, None)
    count = len(form.pairs[seat])
    strategy = {}
    for d in form.decisions[seat]:
        node = form.nodes[d]
        actions = len(node.children)
        table = np.empty((sdcards.CLASS_COUNT, actions))
        for c in range(sdcards.CLASS_COUNT):
            mass = np.array([plan[sequence_id(count, form.pair_index[seat], c, (d, k))] for k in range(actions)])
            total = mass.sum()
            table[c] = mass / total if total > 1e-15 else np.full(actions, 1.0 / actions)
        strategy[d] = table
    return strategy


# ------------------------------------------------------------------------------ combo-level evaluation


def _terminal_matrix(node: Node, out: Outcomes, responder: int) -> np.ndarray:
    """630 x 630 payoff of `responder` (antes x board counts / runouts): rows = responder combos, columns =
    opponent combos, overlapping pairs 0."""
    scale = 1.0 / R.UNITS_PER_ANTE
    if node.kind == "fold":
        (row,) = node.rows.values()
        return out.disjoint * (row[responder] * scale)
    other = 1 - responder
    # wins[i, j] counts the boards on which combo i beats combo j whatever the seats, so with rows = the
    # responder's combos the responder wins on `wins` and loses on its transpose.
    return (out.wins * (node.rows[WIN_MASK[responder]][responder] * scale)
            + out.ties * (node.rows[TIE_MASK][responder] * scale)
            + out.wins.T * (node.rows[WIN_MASK[other]][responder] * scale)) / RUNOUTS


def combo_value(nodes: list[Node], out: Outcomes, responder: int, opponent: dict[int, np.ndarray],
                own: dict[int, np.ndarray] | None = None, cache: dict | None = None) -> float:
    """Expected net result (antes) of `responder` over the uniform deal G1, when the opponent plays the class
    strategy `opponent` and the responder plays `own` (class strategy) or, with own=None, a best response
    chosen per combo and decision node (physical information: own combo and public history)."""
    cache = {} if cache is None else cache
    combo_class = sdcards.COMBO_CLASS.astype(np.int64)

    def matrix(node: Node) -> np.ndarray:
        key = (node.index, responder)
        if key not in cache:
            cache[key] = _terminal_matrix(node, out, responder)
        return cache[key]

    def visit(index: int, reach: np.ndarray) -> np.ndarray:
        node = nodes[index]
        if node.kind != "decision":
            return matrix(node) @ reach
        if node.actor != responder:
            probabilities = opponent[index][combo_class]      # (630, actions)
            return sum(visit(child, reach * probabilities[:, k]) for k, child in enumerate(node.children))
        values = [visit(child, reach) for child in node.children]
        if own is None:
            return np.max(np.stack(values), axis=0)
        probabilities = own[index][combo_class]
        return sum(values[k] * probabilities[:, k] for k in range(len(values)))

    return float(visit(0, np.ones(sdcards.COMBO_COUNT)).sum() / ORDERED_DEALS)


@dataclass
class Certificate:
    lower: float          # BTN's best response against the CO LP strategy (CO's guaranteed value)
    upper: float          # CO's best response against the BTN LP strategy
    profile_ev: float     # CO's EV of the LP profile (x_LP, y_LP)
    width: float


def certify(nodes: list[Node], out: Outcomes, co_strategy: dict, btn_strategy: dict) -> Certificate:
    cache: dict = {}
    btn_best = combo_value(nodes, out, BTN, co_strategy, None, cache)
    co_best = combo_value(nodes, out, CO, btn_strategy, None, cache)
    profile = combo_value(nodes, out, CO, btn_strategy, co_strategy, cache)
    lower, upper = -btn_best, co_best
    return Certificate(lower, upper, profile, upper - lower)


def strategy_report(form: SequenceForm, seat: int, strategy: dict) -> list[dict]:
    """JSON-ready per-node class strategies (informative; equilibria need not be unique)."""
    report = []
    for d in form.decisions[seat]:
        node = form.nodes[d]
        report.append({"node": d, "path": "/".join(node.path) or "root", "actions": node.labels,
                       "classes": {sdcards.CLASS_NAMES[c]: [round(float(p), 9) for p in strategy[d][c]]
                                   for c in range(sdcards.CLASS_COUNT)}})
    return report
