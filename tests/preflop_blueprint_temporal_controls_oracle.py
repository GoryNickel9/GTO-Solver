"""Analytic transfer-loss oracle; injected Q values are not poker EVs."""
import argparse
import json
import math
from pathlib import Path
import subprocess


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", required=True)
    parser.add_argument("--resources-dir", required=True)
    parser.add_argument("--buckets-dir", required=True)
    parser.add_argument("--scratch-dir", required=True)
    args = parser.parse_args()
    scratch = Path(args.scratch_dir)
    scratch.mkdir(parents=True, exist_ok=True)
    # Within each flop the states have identical features and opposing actions.
    # Excluding that flop leaves two exactly tied neighbors. Their transferred
    # mixture loses 1/2 in every query, independently of feature distances.
    observations = []
    for index, (flop, combo) in enumerate([(387, 393), (549, 371)]):
        for action in range(2):
            observations.append({
                "flop_index": flop, "combo": combo, "row": 0,
                "action_ev": [1.0 - action, float(action)],
                "opponent_probability": (index * 2 + action + 1) / 5,
                "hero_reach": 0.25 if index == action == 0 else 1.0,
                "board_multiplicity": 1.0,
            })
    # This row has no other flop: contributes to total mass but not support.
    observations.append({
        "flop_index": 387, "combo": 393, "row": 1,
        "action_ev": [1.0, 0.0], "opponent_probability": 1.0,
        "hero_reach": 1.0, "board_multiplicity": 1.0,
    })
    source = scratch / "input.json"
    output = scratch / "output.json"
    source.write_text(json.dumps({"nodes": [{"node": 0, "observations": observations}]}))
    command = [
        args.executable, "--resources-dir", args.resources_dir,
        "--buckets-dir", args.buckets_dir, "--diagnostics", str(source),
        "--output", str(output), "--mode", "controls",
    ]
    subprocess.run(command, check=True)
    result = json.loads(output.read_text())
    node = result["nodes"][0]
    assert node["supported_observations"] == 4
    assert node["unsupported_observations"] == 1
    for view, mass in [("self_reach", 1.85), ("forced_hero_prefix", 2.0)]:
        actual = node[view]
        for key, expected in {
            "flat_loss": 0.5, "temporal_loss": 0.5, "covered_mass": mass,
            "total_mass": mass + 1.0, "coverage": mass / (mass + 1.0),
        }.items():
            assert math.isclose(actual[key], expected, abs_tol=1e-12), (view, key, actual)
    assert result["unique_states"] == 2
    assert result["unique_pair_distances"] == 1
    for field, invalid in [("action_ev", []), ("combo", 65536), ("hero_reach", -1.0)]:
        original = observations[0][field]
        observations[0][field] = invalid
        source.write_text(json.dumps({"nodes": [{"node": 0, "observations": observations}]}))
        rejected = subprocess.run(command, capture_output=True, text=True)
        assert rejected.returncode == 1, (field, rejected.stdout, rejected.stderr)
        assert "TEMPORAL_WITNESS=FAIL" in rejected.stderr
        observations[0][field] = original
    print("TEMPORAL_CONTROLS_ORACLE=PASS loss=0.5 ties=averaged leave_flop_out=true")


if __name__ == "__main__":
    main()
