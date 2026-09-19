"""Build compact audit records from local raw outputs; reject missing evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path
from typing import Any


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input-dir", type=Path, default=Path("out/nash_audit"))
    parser.add_argument(
        "--output-dir", type=Path, default=Path("docs/research/preflop_vector_cfr")
    )
    args = parser.parse_args()
    root, destination = args.input_dir, args.output_dir

    def read(name: str) -> dict[str, Any]:
        return json.loads((root / name).read_text(encoding="utf-8"))

    def write(label: str, data: dict[str, Any]) -> None:
        (destination / f"{label}_2026-09-19.json").write_text(
            json.dumps(data, indent=2, allow_nan=False) + "\n", encoding="utf-8"
        )

    mechanism = read("constrained_br_mechanisms_with_oracles.json")
    mechanism.pop("oracle_cases")
    groups: dict[tuple[Any, ...], list[float]] = {}
    for row in mechanism["runs"]:
        key = tuple(row[k] for k in ("partition", "algorithm", "seed", "iterations"))
        groups.setdefault(key, []).append(row["normalized_internal_gain"])
    mechanism["maximum_normalized_scaling_spread"] = max(
        max(values) - min(values) for values in groups.values()
    )
    write("CONSTRAINED_BR_MECHANISMS", mechanism)

    sources = [
        ("CONTROL", "short_deck_constrained", 72),
        ("CONFLICT", "short_deck_conflict", 72),
        ("SAVED_CONFLICT", "short_deck_saved_conflict", 6),
        ("LONG_CONFLICT", "short_deck_conflict_long", 54),
    ]
    for label, source, expected_count in sources:
        data = read(source + "_strict_bounds.json")
        corpus = read(source + "_corpus.json")
        assert len(data["results"]) == expected_count
        assert (
            data["input_sha256"]
            == hashlib.sha256(
                (root / (source + "_corpus.json")).read_bytes()
            ).hexdigest()
        )
        for row in data["results"]:
            row.pop("policy", None)
        data["corpus"] = {key: value for key, value in corpus.items() if key != "games"}
        data["partitions"] = [
            {
                key: game[key]
                for key in ("stack", "partition", "recall", "public_tree_fingerprint")
            }
            for game in corpus["games"]
        ]
        # Preserve current/average metrics and measured work, without huge profiles.
        data["trajectory_metrics"] = [
            {
                "stack": game["stack"],
                "partition": game["partition"],
                **{key: value for key, value in run.items() if key != "profile"},
            }
            for game in corpus["games"]
            for run in game["runs"]
        ]
        if label == "LONG_CONFLICT":
            previous = read("short_deck_conflict_corpus.json")
            checked = 0
            for game in corpus["games"]:
                old_game = next(
                    old
                    for old in previous["games"]
                    if old["stack"] == game["stack"]
                    and old["partition"] == game["partition"]
                )
                for run in game["runs"]:
                    if run["iterations"] != 2500:
                        continue
                    old_run = next(
                        old
                        for old in old_game["runs"]
                        if all(
                            old[key] == run[key]
                            for key in ("algorithm", "seed", "iterations")
                        )
                    )
                    assert run["profile"] == old_run["profile"]
                    checked += 1
            assert checked == 9
            data["identical_initial_profiles"] = checked
        data["closed_count"] = sum(
            row["optimal_within_tolerance"] for row in data["results"]
        )
        data["total_milp_seconds"] = sum(row["seconds"] for row in data["results"])
        write(f"CONSTRAINED_BR_{label}", data)
        print(label, data["closed_count"], "/", expected_count)

    route = read("co40_class_route_final.json")
    previous = read("co40_class_full_replay.json")
    assert route["max_gain"] == previous["max_gain"] and route["ev"] == previous["ev"]
    assert route["exact"] and route["resumed_flops"] == 573
    route["route_accounting"] = []
    for player in (0, 1):
        preflop = (
            route["best_response_route_average_value"][player] - route["ev"][player]
        )
        postflop = sum(
            row["postflop_gain_on_response_route"]
            for row in route["postflop_entry_route"]
            if row["hero"] == player
        )
        error = preflop + postflop - route["gain"][player]
        assert abs(error) < 1e-9
        route["route_accounting"].append(
            {
                "player": player,
                "preflop_change": preflop,
                "postflop_change": postflop,
                "identity_error": error,
            }
        )
    write("CO40_BR_ROUTES", route)

    changed = set(
        subprocess.check_output(
            ["git", "diff", "--name-only", "9b9d427"], text=True
        ).splitlines()
    )
    changed.update(
        subprocess.check_output(
            ["git", "ls-files", "--others", "--exclude-standard"], text=True
        ).splitlines()
    )
    hashes = {
        name: hashlib.sha256(Path(name).read_bytes()).hexdigest()
        for name in sorted(changed)
        if Path(name).suffix in (".cpp", ".hpp", ".py")
        or name.endswith("CMakeLists.txt")
    }
    write(
        "CONSTRAINED_BR_SOURCE_MANIFEST",
        {"base_commit": "9b9d427", "source_sha256": hashes},
    )


if __name__ == "__main__":
    main()
