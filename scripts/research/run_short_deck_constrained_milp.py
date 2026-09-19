"""Certify predeclared checkpoints, preserve incomplete searches as bounds."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from constrained_br_milp import solve


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--time-limit", type=float, default=30.0)
    parser.add_argument("--all-checkpoints", action="store_true")
    args = parser.parse_args()
    raw = args.input.read_bytes()
    data = json.loads(raw)
    output = {
        "schema": "gtosd.research.short_deck_constrained_bounds.v1",
        "input_sha256": hashlib.sha256(raw).hexdigest(),
        "results": [],
    }
    for game in data["games"]:
        for run in game["runs"]:
            if not args.all_checkpoints and (
                run["algorithm"] not in ("linear_cfr", "saved_co40_class")
                and run["iterations"] != 2500
            ):
                continue
            for player in (0, 1):
                result = solve(game["game"], run["profile"], player, args.time_limit)
                physical = run["physical_br"][player]
                if abs(result["profile_ev"] - run["ev"][player]) > 1e-9:
                    raise RuntimeError("Python/C++ EV mismatch")
                if result["feasible_lower_bound"] > physical + result["tolerance"]:
                    raise RuntimeError(
                        "constrained deviation exceeds independent physical optimum"
                    )
                if (
                    game["partition"] == "lossless"
                    and result["optimal_within_tolerance"]
                ):
                    if abs(result["candidate_value"] - physical) > result["tolerance"]:
                        raise RuntimeError("MILP/lossless C++ best response mismatch")
                output["results"].append(
                    {
                        "stack": game["stack"],
                        "partition": game["partition"],
                        "algorithm": run["algorithm"],
                        "seed": run["seed"],
                        "iterations": run["iterations"],
                        "physical_br": physical,
                        "physical_gain": physical - run["ev"][player],
                        "internal_gain_lower": result["feasible_lower_bound"]
                        - run["ev"][player],
                        "internal_gain_upper": (
                            result["numerical_upper_bound"] - run["ev"][player]
                            if result["bound_consistent"]
                            else None
                        ),
                        **result,
                    }
                )
                # Atomic replacement retains completed rows if a later solve fails.
                temporary = args.output.with_suffix(args.output.suffix + ".tmp")
                temporary.write_text(
                    json.dumps(output, indent=2, allow_nan=False) + "\n",
                    encoding="utf-8",
                )
                temporary.replace(args.output)
                print(
                    f"stack={game['stack']} {game['partition']} {run['algorithm']} "
                    f"seed={run['seed']} t={run['iterations']} p={player} "
                    f"closed={result['optimal_within_tolerance']} gap={result['absolute_gap']}",
                    flush=True,
                )
    print(f"SHORT_DECK_MILP=COMPLETE responses={len(output['results'])}")


if __name__ == "__main__":
    main()
