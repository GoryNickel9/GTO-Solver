"""Generate or verify the sealed ProductionDcfr product holdout corpus.

The selection is deliberately independent of solver output. For each declared
board-texture stratum, it chooses the legal flop with the smallest SHA-256 of
``seed|stratum|canonical-board`` after excluding every board already assigned
to development, validation, or regression.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from itertools import combinations
from pathlib import Path
from typing import Any, Callable


SEED = 1_305_092_026
RANKS = "6789TJQKA"
SUITS = "cdhs"
ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "benchmarks" / "fixtures"
V1_MANIFEST = FIXTURES / "production_dcfr_product_corpus_v1.json"
V2_MANIFEST = FIXTURES / "production_dcfr_product_corpus_v2.json"

EXCLUDED_BOARDS = {
    "Qh,Kh,Ah",  # AHK regression/development
    "6s,7d,Th",  # TH regression
    "9d,Tc,Ts",  # TST regression
    "7c,Qd,As",  # reduced development/validation fixtures
}


def card_key(card: str) -> tuple[int, int]:
    return RANKS.index(card[0]), SUITS.index(card[1])


def canonical_board(cards: tuple[str, str, str]) -> str:
    return ",".join(sorted(cards, key=card_key))


def texture(cards: tuple[str, str, str]) -> tuple[list[int], int]:
    ranks = sorted({card_key(card)[0] for card in cards})
    return ranks, len({card[1] for card in cards})


def paired_two_tone(cards: tuple[str, str, str]) -> bool:
    ranks = [card_key(card)[0] for card in cards]
    return len(set(ranks)) == 2 and max(ranks.count(rank) for rank in set(ranks)) == 2 and texture(cards)[1] == 2


def connected_two_tone(cards: tuple[str, str, str]) -> bool:
    ranks, suit_count = texture(cards)
    return (
        len(ranks) == 3
        and suit_count == 2
        and ranks[-1] - ranks[0] <= 4
        and ranks[1] - ranks[0] <= 2
        and ranks[2] - ranks[1] <= 2
    )


def ace_low_rainbow(cards: tuple[str, str, str]) -> bool:
    ranks, suit_count = texture(cards)
    return len(ranks) == 3 and suit_count == 3 and ranks[-1] == 8 and ranks[-1] - ranks[0] >= 6


STRATA: dict[str, Callable[[tuple[str, str, str]], bool]] = {
    "paired_two_tone": paired_two_tone,
    "connected_two_tone": connected_two_tone,
    "ace_low_rainbow": ace_low_rainbow,
}

PROFILES: dict[str, dict[str, Any]] = {
    "paired_two_tone": {
        "id": "H-PAIRED-TWOTONE-001",
        "initial_pot_units": 240_000,
        "effective_stack_units": 720_000,
        "raise_depth": 2,
        "all_in_mode": "add",
        "all_in_threshold_bp": 20_000,
        "co_sizes": {"flop": [3_300], "turn": [7_500], "river": [10_000]},
        "btn_sizes": {"flop": [5_000], "turn": [7_500], "river": [12_500]},
    },
    "connected_two_tone": {
        "id": "H-CONNECTED-TWOTONE-001",
        "initial_pot_units": 180_000,
        "effective_stack_units": 900_000,
        "raise_depth": 1,
        "all_in_mode": "disabled",
        "all_in_threshold_bp": 0,
        "co_sizes": {"flop": [2_500, 7_500], "turn": [5_000], "river": [7_500]},
        "btn_sizes": {"flop": [3_300, 10_000], "turn": [7_500], "river": [10_000]},
    },
    "ace_low_rainbow": {
        "id": "H-ACELOW-RAINBOW-001",
        "initial_pot_units": 320_000,
        "effective_stack_units": 960_000,
        "raise_depth": 3,
        "all_in_mode": "go",
        "all_in_threshold_bp": 15_000,
        "co_sizes": {"flop": [5_000], "turn": [7_500], "river": [12_500]},
        "btn_sizes": {"flop": [3_300], "turn": [5_000], "river": [10_000]},
    },
}

RANGE_WEIGHT_SETS: dict[str, dict[str, tuple[int, ...]]] = {
    "paired_two_tone": {"co": (10_000,), "btn": (10_000,)},
    "connected_two_tone": {"co": (0, 5_000, 10_000), "btn": (2_500, 7_500, 10_000)},
    "ace_low_rainbow": {"co": (0, 2_500, 7_500, 10_000), "btn": (0, 5_000, 10_000)},
}


def selection_digest(stratum: str, board: str) -> str:
    return hashlib.sha256(f"{SEED}|{stratum}|{board}".encode()).hexdigest().upper()


def select_boards() -> dict[str, tuple[tuple[str, str, str], str]]:
    cards = tuple(rank + suit for rank in RANKS for suit in SUITS)
    selected: dict[str, tuple[tuple[str, str, str], str]] = {}
    for stratum, predicate in STRATA.items():
        eligible = []
        for candidate in combinations(cards, 3):
            board = canonical_board(candidate)
            if board not in EXCLUDED_BOARDS and predicate(candidate):
                eligible.append((selection_digest(stratum, board), candidate))
        if not eligible:
            raise RuntimeError(f"no eligible board for {stratum}")
        digest, board_cards = min(eligible)
        selected[stratum] = (board_cards, digest)
    return selected


def scenario(sizes: list[int], raise_depth: int, mode: str, threshold: int, *, facing: bool) -> dict[str, Any]:
    return {
        "sizes_bp": sizes,
        "raise_depth": raise_depth if facing else 0,
        "all_in_mode": mode,
        "all_in_threshold_bp": threshold,
        "all_in_strict_boundary": True,
        "minimum_bet_units": 10_000,
    }


def make_config(stratum: str, board: tuple[str, str, str]) -> dict[str, Any]:
    profile = PROFILES[stratum]
    streets: dict[str, Any] = {}
    for street in ("flop", "turn", "river"):
        players: dict[str, Any] = {}
        for player, sizes_key in (("co", "co_sizes"), ("btn", "btn_sizes")):
            sizes = profile[sizes_key][street]
            players[player] = {
                "lead": scenario(sizes, profile["raise_depth"], profile["all_in_mode"], profile["all_in_threshold_bp"], facing=False),
                "after_check": scenario(sizes, profile["raise_depth"], profile["all_in_mode"], profile["all_in_threshold_bp"], facing=False),
                "facing_bet": scenario(sizes, profile["raise_depth"], profile["all_in_mode"], profile["all_in_threshold_bp"], facing=True),
            }
        streets[street] = players
    return {
        "version": 1,
        "flop": list(board),
        "initial_pot_units": profile["initial_pot_units"],
        "effective_stack_units": profile["effective_stack_units"],
        "rake": {
            "enabled": False,
            "percentage_bp": 0,
            "cap_units": 0,
            "no_flop_no_drop": True,
            "minimum_pot_units": 0,
        },
        "streets": streets,
    }


def make_ranges(stratum: str) -> dict[str, Any]:
    cards = tuple(rank + suit for rank in RANKS for suit in SUITS)
    combos = tuple(first + second for first, second in combinations(cards, 2))
    players = []
    for player in ("co", "btn"):
        choices = RANGE_WEIGHT_SETS[stratum][player]
        weights = []
        for combo in combos:
            digest = hashlib.sha256(
                f"{SEED}|range-v1|{stratum}|{player}|{combo}".encode()
            ).digest()
            weights.append(choices[int.from_bytes(digest[:4], "big") % len(choices)])
        players.append({"player": player, "weights_bp": weights})
    return {
        "schema": "gtosd.postflop_combo_ranges.v1",
        "combo_order": "short_deck rank-major 6789TJQKA, suit-major cdhs, first-index then second-index",
        "combo_count": len(combos),
        "generation": {
            "method": "SHA-256 modulo frozen per-player weight set",
            "seed": SEED,
            "weight_sets_bp": RANGE_WEIGHT_SETS[stratum],
            "blocked_combo_policy": "retain declared weight; apply board card removal at load time",
        },
        "players": players,
    }


def encoded_json(value: Any) -> bytes:
    return (json.dumps(value, indent=2, ensure_ascii=False) + "\n").encode()


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def expected_files() -> dict[Path, bytes]:
    previous = json.loads(V1_MANIFEST.read_text(encoding="utf-8"))
    selected = select_boards()
    files: dict[Path, bytes] = {}
    holdout = []
    for stratum in STRATA:
        board, digest = selected[stratum]
        profile = PROFILES[stratum]
        path = FIXTURES / f"postflop_holdout_{stratum}_001.json"
        payload = encoded_json(make_config(stratum, board))
        files[path] = payload
        ranges_path = FIXTURES / f"postflop_holdout_{stratum}_ranges_001.json"
        ranges_payload = encoded_json(make_ranges(stratum))
        files[ranges_path] = ranges_payload
        holdout.append(
            {
                "id": profile["id"],
                "stratum": stratum,
                "board": list(board),
                "selection_sha256": digest,
                "config": path.relative_to(ROOT).as_posix(),
                "sha256": sha256(payload),
                "ranges": ranges_path.relative_to(ROOT).as_posix(),
                "ranges_sha256": sha256(ranges_payload),
                "sealed": True,
                "purpose": "blind final confirmation; forbidden for architecture or parameter selection",
            }
        )
    manifest = {
        "schema": "gtosd.production_dcfr_product_corpus.v2",
        "frozen_at": "2026-09-05",
        "seed": SEED,
        "selection": {
            "version": 1,
            "method": "minimum SHA-256 of seed|stratum|canonical-board",
            "strata": list(STRATA),
            "excluded_boards": sorted(EXCLUDED_BOARDS),
            "profile_assignment": "fixed table in tools/generate_production_dcfr_holdout.py before preflight",
            "solver_outputs_used": False,
        },
        "development": previous["development"],
        "validation": previous["validation"],
        "holdout": holdout,
        "regressions": previous["regressions"],
        "seal": {
            "status": "sealed",
            "opened_at": None,
            "permitted_before_open": [
                "JSON parsing and rules validation",
                "partition and checksum verification",
                "hard resource-feasibility preflight without solver iterations",
            ],
            "forbidden_before_open": [
                "solve, strategy, EV, dEV, NashConv or timing inspection",
                "architecture, branch, threshold or parameter selection",
            ],
        },
    }
    files[V2_MANIFEST] = encoded_json(manifest)
    return files


def referenced_path(entry: dict[str, Any]) -> Path:
    relative = entry.get("config", entry.get("specification"))
    if not isinstance(relative, str):
        raise RuntimeError(f"entry {entry.get('id')} has no referenced file")
    return ROOT / relative


def board_from_entry(entry: dict[str, Any]) -> str:
    payload = json.loads(referenced_path(entry).read_text(encoding="utf-8"))
    cards = payload.get("flop")
    if cards is None:
        cards = payload.get("fixture", {}).get("flop")
    if not isinstance(cards, list) or len(cards) != 3:
        raise RuntimeError(f"entry {entry.get('id')} has no valid flop")
    return canonical_board(tuple(cards))


def validate_manifest() -> None:
    manifest = json.loads(V2_MANIFEST.read_text(encoding="utf-8"))
    if manifest.get("schema") != "gtosd.production_dcfr_product_corpus.v2":
        raise RuntimeError("unexpected manifest schema")
    partitions = ("development", "validation", "holdout", "regressions")
    entries = [(partition, entry) for partition in partitions for entry in manifest[partition]]
    ids = [entry["id"] for _, entry in entries]
    if len(ids) != len(set(ids)):
        raise RuntimeError("corpus IDs are not unique")
    non_holdout_boards = {
        board_from_entry(entry) for partition, entry in entries if partition != "holdout"
    }
    for partition, entry in entries:
        path = referenced_path(entry)
        if sha256(path.read_bytes()) != entry["sha256"]:
            raise RuntimeError(f"checksum mismatch: {path.relative_to(ROOT)}")
        if partition == "holdout":
            ranges_path = ROOT / entry["ranges"]
            if sha256(ranges_path.read_bytes()) != entry["ranges_sha256"]:
                raise RuntimeError(f"range checksum mismatch: {ranges_path.relative_to(ROOT)}")
            ranges = json.loads(ranges_path.read_text(encoding="utf-8"))
            players = ranges.get("players", [])
            if (
                ranges.get("schema") != "gtosd.postflop_combo_ranges.v1"
                or ranges.get("combo_count") != 630
                or not isinstance(players, list)
                or len(players) != 2
                or [player.get("player") for player in players] != ["co", "btn"]
                or any(
                    len(player.get("weights_bp", [])) != 630
                    or any(
                        not isinstance(weight, int) or weight < 0 or weight > 10_000
                        for weight in player.get("weights_bp", [])
                    )
                    for player in players
                )
            ):
                raise RuntimeError(f"invalid explicit range payload: {ranges_path.relative_to(ROOT)}")
            if board_from_entry(entry) in non_holdout_boards:
                raise RuntimeError(f"holdout board overlaps another partition: {entry['id']}")
    if manifest.get("seal", {}).get("status") != "sealed":
        raise RuntimeError("holdout is not sealed")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--write", action="store_true", help="write the frozen fixtures and manifest")
    arguments = parser.parse_args()
    expected = expected_files()
    mismatches = []
    for path, payload in expected.items():
        if arguments.write:
            path.write_bytes(payload)
        elif not path.exists() or path.read_bytes() != payload:
            mismatches.append(path.relative_to(ROOT).as_posix())
    if mismatches:
        print("HOLDOUT_CORPUS_MISMATCH")
        for mismatch in mismatches:
            print(mismatch)
        return 1
    validate_manifest()
    print("HOLDOUT_CORPUS_WRITTEN" if arguments.write else "HOLDOUT_CORPUS_VERIFIED")
    for stratum, (board, digest) in select_boards().items():
        print(f"{stratum}={','.join(board)} selection_sha256={digest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
