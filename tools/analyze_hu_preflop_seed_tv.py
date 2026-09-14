#!/usr/bin/env python3
"""Decompose paired HU preflop root-strategy TV without modifying either solve."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
from typing import Any


ROOT_ACTION_ORDER = ("all_in", "raise_6", "raise_10", "call", "fold")
NEAR_INDIFFERENT_GAP_ANTE = 0.1
MATERIAL_GAP_ANTE = 0.5
EPSILON = 1.0e-12


def combo_mass(combo: str) -> int:
    if len(combo) == 2:
        return 6
    return 4 if combo.endswith("s") else 12


def combo_family(combo: str) -> str:
    if len(combo) == 2:
        return "pair"
    return "suited" if combo.endswith("s") else "offsuit"


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as source:
        return json.load(source)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def normalized_row(row: dict[str, Any], context: str) -> dict[str, float]:
    if set(row) != set(ROOT_ACTION_ORDER):
        raise ValueError(f"{context} does not contain the five root actions")
    result = {action: float(row[action]) for action in ROOT_ACTION_ORDER}
    if any(not math.isfinite(value) or value < 0.0 for value in result.values()):
        raise ValueError(f"{context} contains an invalid probability")
    total = sum(result.values())
    if total <= 0.0 or abs(total - 1.0) > 1.0e-9:
        raise ValueError(f"{context} probabilities sum to {total}")
    return {action: value / total for action, value in result.items()}


def root_node(candidate: dict[str, Any]) -> dict[str, Any]:
    nodes = candidate.get("preflop_nodes")
    if not isinstance(nodes, dict) or "CO" not in nodes:
        raise ValueError("candidate does not export the CO root preflop node")
    node = nodes["CO"]
    if node.get("player") != "CO" or node.get("history") != []:
        raise ValueError("CO root node has an invalid identity")
    return node


def root_policy(candidate: dict[str, Any], policy: str) -> dict[str, dict[str, float]]:
    node = root_node(candidate)
    field = "strategy" if policy == "average" else "current_strategy"
    raw = node.get(field)
    if not isinstance(raw, dict) or len(raw) != 81:
        raise ValueError(f"candidate root {field} must contain 81 classes")
    result = {
        combo: normalized_row(row, f"{policy}.{combo}") for combo, row in raw.items()
    }
    if sum(combo_mass(combo) for combo in result) != 630:
        raise ValueError(f"candidate root {field} does not represent 630 physical combos")
    return result


def validate_candidate(candidate: dict[str, Any], path: Path) -> None:
    if int(candidate.get("iterations", 0)) < 2_000_000:
        raise ValueError(f"{path} has fewer than 2,000,000 iterations")
    root_policy(candidate, "average")
    root_policy(candidate, "current")
    diagnostics = candidate.get("root_action_advantage_diagnostics")
    if not isinstance(diagnostics, dict) or len(diagnostics) != 81:
        raise ValueError(f"{path} lacks the 81 root action-advantage diagnostics")


def deterministic_transport(
    first: dict[str, float], second: dict[str, float]
) -> list[tuple[str, str, float]]:
    """Return one stable seed-1 to seed-2 flow whose mass equals row TV."""

    surplus = [
        [action, first[action] - second[action]]
        for action in ROOT_ACTION_ORDER
        if first[action] - second[action] > EPSILON
    ]
    deficit = [
        [action, second[action] - first[action]]
        for action in ROOT_ACTION_ORDER
        if second[action] - first[action] > EPSILON
    ]
    transfers: list[tuple[str, str, float]] = []
    left = 0
    right = 0
    while left < len(surplus) and right < len(deficit):
        amount = min(float(surplus[left][1]), float(deficit[right][1]))
        if amount > EPSILON:
            transfers.append((str(surplus[left][0]), str(deficit[right][0]), amount))
        surplus[left][1] = float(surplus[left][1]) - amount
        deficit[right][1] = float(deficit[right][1]) - amount
        if float(surplus[left][1]) <= EPSILON:
            left += 1
        if float(deficit[right][1]) <= EPSILON:
            right += 1
    residual = sum(float(item[1]) for item in surplus[left:]) + sum(
        float(item[1]) for item in deficit[right:]
    )
    if residual > 1.0e-9:
        raise ValueError(f"transport residual is {residual}")
    expected = 0.5 * sum(abs(first[action] - second[action]) for action in ROOT_ACTION_ORDER)
    observed = sum(amount for _, _, amount in transfers)
    if abs(expected - observed) > 1.0e-9:
        raise ValueError(f"transport mass {observed} does not reconstruct TV {expected}")
    return transfers


def pairwise_diagnostic(
    first: dict[str, Any],
    second: dict[str, Any],
    combo: str,
    source_action: str,
    destination_action: str,
) -> dict[str, Any]:
    seed_values = []
    seed_standard_errors = []
    for candidate in (first, second):
        diagnostic = candidate["root_action_advantage_diagnostics"][combo]
        pair = diagnostic["pairwise_action_differences"][source_action][destination_action]
        seed_values.append(float(pair["weighted_mean_ante"]))
        seed_standard_errors.append(float(pair["weighted_standard_error_ante"]))
    mean_gap = 0.5 * (seed_values[0] + seed_values[1])
    combined_marginal_se = 0.5 * math.hypot(
        seed_standard_errors[0], seed_standard_errors[1]
    )
    absolute_gap = abs(mean_gap)
    return {
        "source_minus_destination_mean_ante": mean_gap,
        "absolute_mean_gap_ante": absolute_gap,
        "combined_marginal_standard_error_ante": combined_marginal_se,
        "absolute_gap_over_combined_marginal_se": (
            absolute_gap / combined_marginal_se if combined_marginal_se > 0.0 else None
        ),
        "seed_pairwise_mean_ante": seed_values,
        "seed_pairwise_standard_error_ante": seed_standard_errors,
        "near_indifferent_at_0_1_ante": absolute_gap <= NEAR_INDIFFERENT_GAP_ANTE,
        "material_at_0_5_ante": absolute_gap >= MATERIAL_GAP_ANTE,
        "within_two_marginal_standard_errors": (
            absolute_gap <= 2.0 * combined_marginal_se
            if combined_marginal_se > 0.0
            else absolute_gap == 0.0
        ),
        "favored_action_by_mean": (
            source_action if mean_gap > 0.0 else destination_action if mean_gap < 0.0 else "tie"
        ),
    }


def decompose_policy(
    first: dict[str, Any], second: dict[str, Any], policy: str
) -> dict[str, Any]:
    first_policy = root_policy(first, policy)
    second_policy = root_policy(second, policy)
    if first_policy.keys() != second_policy.keys():
        raise ValueError(f"{policy} policy class sets differ")

    action_signed_delta = {action: 0.0 for action in ROOT_ACTION_ORDER}
    action_half_l1 = {action: 0.0 for action in ROOT_ACTION_ORDER}
    pair_aggregate: dict[tuple[str, str], dict[str, float]] = {}
    family_tv_mass = {"pair": 0.0, "suited": 0.0, "offsuit": 0.0}
    rows: list[dict[str, Any]] = []
    total_tv_mass = 0.0
    total_gap_mass_ante = 0.0
    near_indifferent_mass = 0.0
    material_gap_mass = 0.0
    within_two_se_mass = 0.0

    for combo in first_policy:
        seed_one = first_policy[combo]
        seed_two = second_policy[combo]
        mass = combo_mass(combo)
        tv = 0.5 * sum(
            abs(seed_one[action] - seed_two[action]) for action in ROOT_ACTION_ORDER
        )
        weighted_tv_mass = mass * tv / 630.0
        total_tv_mass += weighted_tv_mass
        family_tv_mass[combo_family(combo)] += weighted_tv_mass
        for action in ROOT_ACTION_ORDER:
            delta = seed_two[action] - seed_one[action]
            action_signed_delta[action] += mass * delta / 630.0
            action_half_l1[action] += mass * 0.5 * abs(delta) / 630.0

        transfers = []
        combo_gap_mass = 0.0
        combo_near_mass = 0.0
        combo_material_mass = 0.0
        combo_two_se_mass = 0.0
        for source_action, destination_action, amount in deterministic_transport(
            seed_one, seed_two
        ):
            diagnostic = pairwise_diagnostic(
                first, second, combo, source_action, destination_action
            )
            gap_mass = amount * diagnostic["absolute_mean_gap_ante"]
            combo_gap_mass += gap_mass
            combo_near_mass += amount * float(diagnostic["near_indifferent_at_0_1_ante"])
            combo_material_mass += amount * float(diagnostic["material_at_0_5_ante"])
            combo_two_se_mass += amount * float(
                diagnostic["within_two_marginal_standard_errors"]
            )
            pair_key = (source_action, destination_action)
            aggregate = pair_aggregate.setdefault(
                pair_key,
                {"physical_probability_mass": 0.0, "gap_mass_ante": 0.0},
            )
            aggregate["physical_probability_mass"] += mass * amount / 630.0
            aggregate["gap_mass_ante"] += mass * gap_mass / 630.0
            transfers.append(
                {
                    "from_seed1_surplus_action": source_action,
                    "to_seed2_surplus_action": destination_action,
                    "probability_mass": amount,
                    "seed1_source_action_reach": seed_one[source_action],
                    "seed2_destination_action_reach": seed_two[destination_action],
                    "descriptive_gap_mass_ante": gap_mass,
                    **diagnostic,
                }
            )

        total_gap_mass_ante += mass * combo_gap_mass / 630.0
        near_indifferent_mass += mass * combo_near_mass / 630.0
        material_gap_mass += mass * combo_material_mass / 630.0
        within_two_se_mass += mass * combo_two_se_mass / 630.0
        dominant = max(transfers, key=lambda item: item["probability_mass"], default=None)
        rows.append(
            {
                "combo": combo,
                "family": combo_family(combo),
                "physical_combo_mass": mass,
                "root_public_reach": 1.0,
                "seed1_strategy": seed_one,
                "seed2_strategy": seed_two,
                "seed2_minus_seed1_percentage_points": {
                    action: 100.0 * (seed_two[action] - seed_one[action])
                    for action in ROOT_ACTION_ORDER
                },
                "total_variation_percentage_points": 100.0 * tv,
                "contribution_to_weighted_root_tv_percentage_points": 100.0
                * weighted_tv_mass,
                "descriptive_gap_mass_ante": combo_gap_mass,
                "near_indifferent_tv_share": combo_near_mass / tv if tv > 0.0 else 0.0,
                "material_gap_tv_share": combo_material_mass / tv if tv > 0.0 else 0.0,
                "within_two_marginal_se_tv_share": combo_two_se_mass / tv if tv > 0.0 else 0.0,
                "dominant_seed1_action": max(seed_one, key=seed_one.get),
                "dominant_seed2_action": max(seed_two, key=seed_two.get),
                "dominant_transfer": dominant,
                "transfers": transfers,
            }
        )

    rows.sort(
        key=lambda row: (
            -row["contribution_to_weighted_root_tv_percentage_points"], row["combo"]
        )
    )
    pair_rows = []
    for (source_action, destination_action), aggregate in pair_aggregate.items():
        probability_mass = aggregate["physical_probability_mass"]
        pair_rows.append(
            {
                "from_seed1_surplus_action": source_action,
                "to_seed2_surplus_action": destination_action,
                "contribution_to_weighted_root_tv_percentage_points": 100.0
                * probability_mass,
                "descriptive_gap_mass_ante": aggregate["gap_mass_ante"],
                "mean_absolute_gap_ante": (
                    aggregate["gap_mass_ante"] / probability_mass
                    if probability_mass > 0.0
                    else 0.0
                ),
            }
        )
    pair_rows.sort(
        key=lambda row: -row["contribution_to_weighted_root_tv_percentage_points"]
    )

    reconstructed_tv_pp = 100.0 * sum(
        row["contribution_to_weighted_root_tv_percentage_points"] / 100.0
        for row in pair_rows
    )
    total_tv_pp = 100.0 * total_tv_mass
    if abs(reconstructed_tv_pp - total_tv_pp) > 1.0e-9:
        raise ValueError("pair transport does not reconstruct aggregate root TV")
    return {
        "policy": policy,
        "paired_root_total_variation_percentage_points": total_tv_pp,
        "descriptive_value_weighted_transport_ante": total_gap_mass_ante,
        "near_indifferent_at_0_1_ante_tv_percentage_points": 100.0
        * near_indifferent_mass,
        "material_at_0_5_ante_tv_percentage_points": 100.0 * material_gap_mass,
        "within_two_marginal_standard_errors_tv_percentage_points": 100.0
        * within_two_se_mass,
        "root_action_seed2_minus_seed1_percentage_points": {
            action: 100.0 * action_signed_delta[action] for action in ROOT_ACTION_ORDER
        },
        "root_action_half_l1_contribution_percentage_points": {
            action: 100.0 * action_half_l1[action] for action in ROOT_ACTION_ORDER
        },
        "family_contribution_percentage_points": {
            family: 100.0 * value for family, value in family_tv_mass.items()
        },
        "pair_transports": pair_rows,
        "rows": rows,
    }


def format_number(value: float, digits: int = 4) -> str:
    return f"{value:.{digits}f}".replace(".", ",")


def transfer_label(transfer: dict[str, Any] | None) -> str:
    if transfer is None:
        return "—"
    return (
        f"{transfer['from_seed1_surplus_action']}→"
        f"{transfer['to_seed2_surplus_action']}"
    )


def policy_table(report: dict[str, Any]) -> str:
    lines = [
        "| Combo | Massa | TV | Contributo | Spostamento principale | Massa spostata | Gap EV | Reach azione S1 → S2 | TV gap ≤0,1a | TV entro 2 SE |",
        "| --- | ---: | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: |",
    ]
    for row in report["rows"]:
        transfer = row["dominant_transfer"]
        if transfer is None:
            shifted = gap = reach = "—"
        else:
            shifted = f"{format_number(100.0 * transfer['probability_mass'], 2)} pp"
            gap = f"{format_number(transfer['absolute_mean_gap_ante'], 3)}a"
            reach = (
                f"{format_number(100.0 * transfer['seed1_source_action_reach'], 1)}% → "
                f"{format_number(100.0 * transfer['seed2_destination_action_reach'], 1)}%"
            )
        lines.append(
            "| "
            + " | ".join(
                (
                    row["combo"],
                    str(row["physical_combo_mass"]),
                    f"{format_number(row['total_variation_percentage_points'], 2)} pp",
                    f"{format_number(row['contribution_to_weighted_root_tv_percentage_points'], 3)} pp",
                    transfer_label(transfer),
                    shifted,
                    gap,
                    reach,
                    f"{format_number(100.0 * row['near_indifferent_tv_share'], 1)}%",
                    f"{format_number(100.0 * row['within_two_marginal_se_tv_share'], 1)}%",
                )
            )
            + " |"
        )
    return "\n".join(lines)


def pair_table(report: dict[str, Any]) -> str:
    lines = [
        "| Da seed 1 | A seed 2 | Contributo TV | Gap EV medio | Massa gap EV |",
        "| --- | --- | ---: | ---: | ---: |",
    ]
    for row in report["pair_transports"]:
        lines.append(
            f"| {row['from_seed1_surplus_action']} | {row['to_seed2_surplus_action']} | "
            f"{format_number(row['contribution_to_weighted_root_tv_percentage_points'], 3)} pp | "
            f"{format_number(row['mean_absolute_gap_ante'], 3)}a | "
            f"{format_number(row['descriptive_gap_mass_ante'], 4)}a |"
        )
    return "\n".join(lines)


def markdown_report(report: dict[str, Any]) -> str:
    average = report["average_policy"]
    current = report["current_policy"]
    candidate_label = report["candidate_label"]
    report_date = report["report_date"]
    current_delta = (
        current["paired_root_total_variation_percentage_points"]
        - average["paired_root_total_variation_percentage_points"]
    )
    recommendation = (
        "La policy corrente è più instabile della media. L'averaging ritardato ridurrebbe la "
        "finestra di mediazione e non è il primo intervento da promuovere. Il prossimo test deve "
        "ridurre la varianza dei continuation value e migliorare la copertura dei rami."
        if current_delta > 0.0
        else "La policy corrente è più stabile della media. Un averaging ritardato è il primo "
        "challenger coerente, perché può rimuovere massa storica senza cambiare i regret."
    )
    return f"""# R6 — Decomposizione della TV fra i seed {candidate_label}

Data: {report_date}  
Esito: `DIAGNOSTIC_PASS / {candidate_label}_SEED_TV_ABOVE_5PP`

## Risultato

La TV root fra i due seed {candidate_label} è `{format_number(average['paired_root_total_variation_percentage_points'])} pp`
per la strategia media e `{format_number(current['paired_root_total_variation_percentage_points'])} pp`
per la policy corrente. Il nuovo gate richiede al massimo `5 pp`.

La decomposizione usa le 81 classi e le relative masse fisiche `6/4/12`. Per ogni classe costruisce
un flusso deterministico dalla probabilità in eccesso del seed 1 a quella in eccesso del seed 2.
La somma dei flussi ricostruisce esattamente la TV. Il pairing non è unico; serve a indicare quali
azioni scambiano massa, non a definire una distanza matematica diversa.

I gap EV provengono dalla diagnostica accoppiata degli action advantage durante il training. Le SE
sono marginali e descrittive: i campioni sono serialmente dipendenti e la misura non è un intervallo
di confidenza formale.

## Sintesi

| Metrica | Strategia media | Policy corrente |
| --- | ---: | ---: |
| TV fra seed | {format_number(average['paired_root_total_variation_percentage_points'])} pp | {format_number(current['paired_root_total_variation_percentage_points'])} pp |
| TV con gap medio ≤0,1a | {format_number(average['near_indifferent_at_0_1_ante_tv_percentage_points'])} pp | {format_number(current['near_indifferent_at_0_1_ante_tv_percentage_points'])} pp |
| TV con gap medio ≥0,5a | {format_number(average['material_at_0_5_ante_tv_percentage_points'])} pp | {format_number(current['material_at_0_5_ante_tv_percentage_points'])} pp |
| TV entro due SE marginali | {format_number(average['within_two_marginal_standard_errors_tv_percentage_points'])} pp | {format_number(current['within_two_marginal_standard_errors_tv_percentage_points'])} pp |
| Massa TV × gap EV | {format_number(average['descriptive_value_weighted_transport_ante'], 5)}a | {format_number(current['descriptive_value_weighted_transport_ante'], 5)}a |

Differenza corrente meno media: `{format_number(current_delta)} pp`.

{recommendation}

## Trasferimenti aggregati — strategia media

{pair_table(average)}

## Contributo per famiglia

| Famiglia | Strategia media | Policy corrente |
| --- | ---: | ---: |
| Coppie | {format_number(average['family_contribution_percentage_points']['pair'])} pp | {format_number(current['family_contribution_percentage_points']['pair'])} pp |
| Suited | {format_number(average['family_contribution_percentage_points']['suited'])} pp | {format_number(current['family_contribution_percentage_points']['suited'])} pp |
| Offsuit | {format_number(average['family_contribution_percentage_points']['offsuit'])} pp | {format_number(current['family_contribution_percentage_points']['offsuit'])} pp |

## Tutte le 81 classi — strategia media

Il nodo root ha reach pubblica `100%`. La colonna reach mostra la frequenza dell'azione sorgente
nel seed 1 e dell'azione destinazione nel seed 2 per lo spostamento principale.

{policy_table(average)}

## Tutte le 81 classi — policy corrente

{policy_table(current)}

## Contratto e limiti

- entrambi gli input hanno 2.000.000 di iterazioni e lo stesso fingerprint dell'albero;
- la TV usa la strategia media realization-weighted, come il gate esistente;
- la policy corrente è riportata come diagnostica di convergenza, non come soluzione pubblicabile;
- i gap non usano Monker e non modificano training, bucket, frequenze o payoff;
- `Massa TV × gap EV` è una misura descrittiva del trasporto scelto, non exploitability.

## Decisione

{recommendation}

Il prossimo esperimento deve essere pre-registrato contro questa decomposizione. Un candidato passa
soltanto se riduce la TV media verso `5 pp`, non peggiora la WMAE e conserva gli audit monetari e
Call/Fold di {candidate_label}.
"""


def self_test() -> None:
    first = {"all_in": 0.7, "raise_6": 0.3, "raise_10": 0.0, "call": 0.0, "fold": 0.0}
    second = {"all_in": 0.2, "raise_6": 0.4, "raise_10": 0.4, "call": 0.0, "fold": 0.0}
    transfers = deterministic_transport(first, second)
    expected = [("all_in", "raise_6", 0.1), ("all_in", "raise_10", 0.4)]
    if len(transfers) != len(expected):
        raise AssertionError("unexpected transport count")
    for observed, wanted in zip(transfers, expected, strict=True):
        if observed[:2] != wanted[:2] or abs(observed[2] - wanted[2]) > 1.0e-12:
            raise AssertionError(f"unexpected transport {observed}")
    if combo_mass("AA") != 6 or combo_mass("AKs") != 4 or combo_mass("AKo") != 12:
        raise AssertionError("combo masses are invalid")
    print("HU_PREFLOP_SEED_TV_SELF_TEST=PASS assertions=5")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("first", type=Path, nargs="?")
    parser.add_argument("second", type=Path, nargs="?")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--markdown-output", type=Path)
    parser.add_argument("--candidate-label", default="V13")
    parser.add_argument("--report-date", default="2026-09-12")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if args.first is None or args.second is None:
        parser.error("first and second candidate paths are required")

    first = load_json(args.first)
    second = load_json(args.second)
    validate_candidate(first, args.first)
    validate_candidate(second, args.second)
    if first.get("tree_fingerprint") != second.get("tree_fingerprint"):
        raise ValueError("candidate tree fingerprints differ")
    report = {
        "schema": "gtosd.hu_preflop_seed_tv_decomposition.v1",
        "candidate_label": args.candidate_label,
        "report_date": args.report_date,
        "scope": "paired_root_average_and_current_policy_with_descriptive_training_action_gaps",
        "tree_fingerprint": first["tree_fingerprint"],
        "candidates": [
            {
                "path": str(args.first),
                "sha256": sha256(args.first),
                "iterations": int(first["iterations"]),
                "seed": int(first["seed"]),
            },
            {
                "path": str(args.second),
                "sha256": sha256(args.second),
                "iterations": int(second["iterations"]),
                "seed": int(second["seed"]),
            },
        ],
        "near_indifferent_gap_ante": NEAR_INDIFFERENT_GAP_ANTE,
        "material_gap_ante": MATERIAL_GAP_ANTE,
        "action_advantage_scope": first.get("root_action_advantage_diagnostics_scope"),
        "average_policy": decompose_policy(first, second, "average"),
        "current_policy": decompose_policy(first, second, "current"),
    }
    encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(encoded, encoding="utf-8", newline="\n")
    if args.markdown_output:
        args.markdown_output.write_text(
            markdown_report(report), encoding="utf-8", newline="\n"
        )
    print(
        "HU_PREFLOP_SEED_TV_DECOMPOSITION=PASS"
        f" average_tv_pp={report['average_policy']['paired_root_total_variation_percentage_points']:.9f}"
        f" current_tv_pp={report['current_policy']['paired_root_total_variation_percentage_points']:.9f}"
        f" rows={len(report['average_policy']['rows'])}"
    )


if __name__ == "__main__":
    main()
