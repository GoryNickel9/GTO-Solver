"""Offline global pure-policy BR with numerical bounds; tree inputs only.

Binary actions are shared across all nodes in an information set. A variable
for each own-action sequence is its realization probability: z = parent * y.
Three linear inequalities implement this product with M=1. Chance/opponent
reach appears only in terminal objective coefficients. Repeated own information
sets on one path are rejected: pure optimality need not imply behavioral
optimality in absent-minded games. No rational/exact-arithmetic claim is made.
"""

from __future__ import annotations

import argparse
import json
import math
import time
import warnings
from pathlib import Path
from typing import Any

import numpy as np
import scipy
from scipy.optimize import Bounds, LinearConstraint, milp
from scipy.sparse import coo_matrix


def validate(game: dict[str, Any], profile: dict[str, Any]) -> None:
    nodes = game["nodes"]
    if not nodes or len(nodes) > 200_000 or not 0 <= game["root"] < len(nodes):
        raise ValueError("invalid game size/root")
    incoming = [0] * len(nodes)
    definitions: dict[str, tuple[int, list[int]]] = {}
    for node in nodes:
        kind, edges = node["kind"], node["edges"]
        if kind not in (0, 1, 2) or (kind == 0) != (len(edges) == 0):
            raise ValueError("invalid node kind/edges")
        if len(node["payoff"]) != 2 or not all(map(math.isfinite, node["payoff"])):
            raise ValueError("invalid payoff")
        for edge in edges:
            if not 0 <= edge["child"] < len(nodes):
                raise ValueError("invalid child")
            incoming[edge["child"]] += 1
        if kind == 1:
            probabilities = [edge["probability"] for edge in edges]
            if any(not math.isfinite(p) or p <= 0 for p in probabilities):
                raise ValueError("invalid chance probability")
            if abs(math.fsum(probabilities) - 1) > 1e-12:
                raise ValueError("chance probabilities do not sum to one")
        if kind == 2:
            key, player = node["information_set"], node["player"]
            actions = [edge["action"] for edge in edges]
            if not key or player not in (0, 1) or len(set(actions)) != len(actions):
                raise ValueError("invalid information set")
            definition = (player, actions)
            if definitions.setdefault(key, definition) != definition:
                raise ValueError("inconsistent information set")
    if any(
        count != (0 if i == game["root"] else 1) for i, count in enumerate(incoming)
    ):
        raise ValueError("MILP input must be a tree (no shared nodes)")
    visited: set[int] = set()
    pending = [game["root"]]
    while pending:
        node_id = pending.pop()
        if node_id in visited:
            raise ValueError("cycle")
        visited.add(node_id)
        pending.extend(edge["child"] for edge in nodes[node_id]["edges"])
    if len(visited) != len(nodes) or set(profile) != set(definitions):
        raise ValueError("unreachable nodes or incomplete profile")
    for key, (player, actions) in definitions.items():
        strategy = profile[key]
        probabilities = strategy["probabilities"]
        if strategy["player"] != player or strategy["actions"] != actions:
            raise ValueError("profile signature mismatch")
        if (
            len(probabilities) != len(actions)
            or any(not math.isfinite(p) or p < 0 or p > 1 for p in probabilities)
            or abs(math.fsum(probabilities) - 1) > 1e-12
        ):
            raise ValueError("invalid strategy probabilities")


def evaluate(
    game: dict[str, Any],
    profile: dict[str, Any],
    player: int,
    policy: dict[str, int] | None = None,
) -> float:
    """Independent profile evaluation, without the MILP sequence coefficients."""
    nodes = game["nodes"]
    order, pending = [], [game["root"]]
    while pending:
        node_id = pending.pop()
        order.append(node_id)
        pending.extend(edge["child"] for edge in nodes[node_id]["edges"])
    values = [0.0] * len(nodes)
    for node_id in reversed(order):
        node = nodes[node_id]
        if node["kind"] == 0:
            values[node_id] = node["payoff"][player]
        else:
            if node["kind"] == 1:
                probabilities = [edge["probability"] for edge in node["edges"]]
            elif node["player"] == player and policy is not None:
                probabilities = [
                    float(edge["action"] == policy[node["information_set"]])
                    for edge in node["edges"]
                ]
            else:
                probabilities = profile[node["information_set"]]["probabilities"]
            values[node_id] = math.fsum(
                p * values[edge["child"]]
                for p, edge in zip(probabilities, node["edges"])
            )
    return values[game["root"]]


def solve(
    game: dict[str, Any],
    profile: dict[str, Any],
    player: int,
    time_limit: float = 30.0,
    node_limit: int = 10_000,
) -> dict[str, Any]:
    if (
        player not in (0, 1)
        or not math.isfinite(time_limit)
        or time_limit <= 0
        or node_limit < 0
    ):
        raise ValueError("invalid solve configuration")
    start = time.perf_counter()
    validate(game, profile)
    own = {
        key: strategy
        for key, strategy in sorted(profile.items())
        if strategy["player"] == player
    }
    binary = {
        (key, action): index
        for index, (key, action) in enumerate(
            (key, action)
            for key, strategy in own.items()
            for action in strategy["actions"]
        )
    }
    sequence_ids: dict[tuple[int, str, int], int] = {}
    sequences: list[tuple[int, str, int] | None] = [None]
    coefficients = [0.0]
    pending = [(game["root"], 0, 1.0, frozenset())]
    while pending:
        node_id, sequence, counterfactual, seen = pending.pop()
        node = game["nodes"][node_id]
        if node["kind"] == 0:
            coefficients[sequence] += counterfactual * node["payoff"][player]
            continue
        key = node["information_set"]
        is_own = node["kind"] == 2 and node["player"] == player
        if is_own and key in seen:
            raise ValueError("absent-minded responder is unsupported")
        for index, edge in enumerate(node["edges"]):
            next_sequence, weight, next_seen = sequence, counterfactual, seen
            if is_own:
                descriptor = (sequence, key, edge["action"])
                if descriptor not in sequence_ids:
                    sequence_ids[descriptor] = len(sequences)
                    sequences.append(descriptor)
                    coefficients.append(0.0)
                next_sequence = sequence_ids[descriptor]
                next_seen = seen | {key}
            else:
                weight *= (
                    edge["probability"]
                    if node["kind"] == 1
                    else profile[key]["probabilities"][index]
                )
            pending.append((edge["child"], next_sequence, weight, next_seen))
    offset, size = len(binary), len(binary) + len(sequences)
    objective = np.zeros(size)
    objective[offset:] = -np.asarray(coefficients)
    rows: list[int] = []
    cols: list[int] = []
    values: list[float] = []
    lower: list[float] = []
    upper: list[float] = []

    def add(entries: list[tuple[int, float]], lo: float, hi: float) -> None:
        for column, value in entries:
            rows.append(len(lower))
            cols.append(column)
            values.append(value)
        lower.append(lo)
        upper.append(hi)

    for key, strategy in own.items():
        add([(binary[key, action], 1) for action in strategy["actions"]], 1, 1)
    for index, descriptor in enumerate(sequences[1:], start=1):
        assert descriptor is not None
        parent, key, action = descriptor
        z, x, y = offset + index, offset + parent, binary[key, action]
        add([(z, 1), (x, -1)], -np.inf, 0)
        add([(z, 1), (y, -1)], -np.inf, 0)
        add([(z, 1), (x, -1), (y, -1)], -1, np.inf)
    matrix = coo_matrix((values, (rows, cols)), shape=(len(lower), size)).tocsc()
    bound_lower = np.zeros(size)
    bound_lower[offset] = 1
    integrality = np.zeros(size)
    integrality[:offset] = 1
    options = {
        "time_limit": time_limit,
        "node_limit": node_limit,
        "mip_rel_gap": 0.0,
        "mip_abs_gap": 0.0,
        # Default 1e-6 caused a reproducible 8.9e-7 error against lossless BR
        # despite a reported zero MIP gap. The independent check caught it.
        "mip_feasibility_tolerance": 1e-9,
        "presolve": True,
    }
    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter("always")
        result = milp(
            objective,
            integrality=integrality,
            bounds=Bounds(bound_lower, np.ones(size)),
            constraints=LinearConstraint(matrix, lower, upper),
            options=options,
        )
    solver_warnings = [str(item.message) for item in caught]
    for item in caught:
        # SciPy forwards these HiGHS-specific options unchanged. Preserve that
        # notice in the artifact, and re-emit any different warning.
        if not str(item.message).startswith("Unrecognized options detected:"):
            warnings.warn(str(item.message), item.category, stacklevel=2)
    baseline = evaluate(game, profile, player)
    policy, candidate = None, None
    if result.x is not None:
        policy = {
            key: max(strategy["actions"], key=lambda a: result.x[binary[key, a]])
            for key, strategy in own.items()
        }
        candidate = evaluate(game, profile, player, policy)
    feasible = max(baseline, candidate if candidate is not None else -math.inf)
    dual = getattr(result, "mip_dual_bound", None)
    bound = -float(dual) if dual is not None and math.isfinite(dual) else None
    if offset == 0:
        # No responder decision exists: the only policy is the original one.
        # HiGHS solves a continuous constant problem and emits no MIP bound.
        bound = baseline
    tolerance = 1e-8 * max(
        1.0, max(abs(node["payoff"][player]) for node in game["nodes"])
    )
    objective_error = (
        abs(candidate + result.fun)
        if candidate is not None and result.fun is not None
        else None
    )
    integrality_error = (
        float(np.max(np.abs(result.x[:offset] - np.round(result.x[:offset]))))
        if offset and result.x is not None
        else 0.0
    )
    consistent = bound is not None and bound + tolerance >= feasible
    gap = bound - feasible if consistent else None
    return {
        "player": player,
        "status": int(result.status),
        "message": result.message,
        "profile_ev": baseline,
        "candidate_value": candidate,
        "feasible_lower_bound": feasible,
        "numerical_upper_bound": bound,
        "absolute_gap": gap,
        "tolerance": tolerance,
        "bound_consistent": consistent,
        "optimal_within_tolerance": bool(
            result.status == 0
            and consistent
            and gap <= tolerance
            and objective_error is not None
            and objective_error <= tolerance
            and integrality_error <= 1e-7
        ),
        "objective_recheck_error": objective_error,
        "integrality_error": integrality_error,
        "exact_arithmetic": False,
        "policy": policy,
        "binary_variables": offset,
        "sequence_variables": len(sequences),
        "constraints": len(lower),
        "time_limit": time_limit,
        "node_limit": node_limit,
        "mip_node_count": getattr(result, "mip_node_count", None),
        "scipy_version": scipy.__version__,
        "solver_options": options,
        "solver_warnings": solver_warnings,
        "seconds": time.perf_counter() - start,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--time-limit", type=float, default=30.0)
    args = parser.parse_args()
    data = json.loads(args.input.read_text(encoding="utf-8"))
    results = []
    for case in data["oracle_cases"]:
        for player in (0, 1):
            result = solve(case["game"], case["profile"], player, args.time_limit)
            expected = case["expected_br"][player]
            if (
                not result["optimal_within_tolerance"]
                or abs(result["feasible_lower_bound"] - expected) > 1e-9
            ):
                raise RuntimeError(
                    f"MILP disagrees with C++ enumeration: {result}, expected={expected}"
                )
            results.append(
                {"game_id": case["game"]["game_id"], "expected": expected, **result}
            )
    args.output.write_text(
        json.dumps(
            {"schema": "gtosd.research.milp_oracle.v1", "results": results},
            indent=2,
            allow_nan=False,
        )
        + "\n",
        encoding="utf-8",
    )
    print(f"MILP_ORACLE=PASS cases={len(results)} scipy={scipy.__version__}")


if __name__ == "__main__":
    main()
