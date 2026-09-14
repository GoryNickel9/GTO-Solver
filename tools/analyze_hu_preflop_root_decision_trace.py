#!/usr/bin/env python3
"""Render a causal root-decision audit from a V20 trace export."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
from typing import Any


ACTIONS = ("all_in", "raise_6", "raise_10", "call", "fold")
Z_95 = 1.959963984540054
Z_BONFERRONI_95_FOUR = 2.497705474412374


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Explain a CO root choice using regret state and paired continuation EVs."
    )
    parser.add_argument("--trace", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    return parser.parse_args()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def finite_number(value: Any, name: str) -> float:
    require(isinstance(value, (int, float)) and math.isfinite(value), f"{name} is not finite")
    return float(value)


def fmt_ante(value: float) -> str:
    return f"{value:+.4f}a"


def fmt_percent(value: float) -> str:
    return f"{100.0 * value:.2f}%"


def fmt_weight(value: float) -> str:
    return f"{value:.6g}"


def escape_cell(value: str) -> str:
    return value.replace("|", "\\|")


def confidence_interval(
    mean: float, standard_error: float, z_score: float = Z_95
) -> tuple[float, float]:
    radius = z_score * standard_error
    return mean - radius, mean + radius


def load_rows(path: Path) -> tuple[dict[str, Any], list[dict[str, Any]], str]:
    payload = path.read_bytes()
    document = json.loads(payload)
    require(isinstance(document, dict), "trace root must be an object")
    require(
        document.get("schema") == "gtosd.hu_preflop_root_decision_trace.v1",
        "unsupported root decision trace schema",
    )
    rows = document.get("rows")
    require(isinstance(rows, list) and rows, "trace rows must be a non-empty array")
    return document, rows, hashlib.sha256(payload).hexdigest().upper()


def validate_row(row: dict[str, Any]) -> None:
    require(isinstance(row.get("hand_class"), str), "trace row has no hand class")
    deals = row.get("deals_per_action")
    require(isinstance(deals, int) and deals > 0, "trace row has an invalid deal count")
    training = row.get("training_state")
    require(isinstance(training, dict), "trace row has no training state")
    training_actions = training.get("actions")
    require(isinstance(training_actions, dict), "trace row has no training actions")
    policies = row.get("policies")
    require(isinstance(policies, dict) and "average" in policies, "average trace is missing")
    for action in ACTIONS:
        require(action in training_actions, f"training state is missing {action}")
    for policy_name, policy in policies.items():
        require(policy_name in {"average", "current"}, "unknown policy view")
        require(isinstance(policy, dict), "policy trace must be an object")
        actions = policy.get("actions")
        paired = policy.get("paired_action_differences")
        require(
            isinstance(actions, dict) and isinstance(paired, dict),
            "policy trace is incomplete",
        )
        for action in ACTIONS:
            require(action in actions and action in paired, f"policy trace is missing {action}")
            value = actions[action].get("value")
            require(isinstance(value, dict), f"{policy_name}/{action} has no value")
            require(value.get("samples") == deals, f"{policy_name}/{action} sample count differs")
            require(
                abs(finite_number(value.get("probability"), "action probability") - 1.0) < 1e-12,
                f"{policy_name}/{action} does not cover every conditional deal",
            )


def action_value(policy: dict[str, Any], action: str) -> tuple[float, float]:
    value = policy["actions"][action]["value"]
    return finite_number(value["mean_payoff_ante"], "action EV"), finite_number(
        value["standard_error_ante"], "action EV standard error"
    )


def paired_call_difference(policy: dict[str, Any], other: str) -> tuple[float, float]:
    value = policy["paired_action_differences"]["call"][other]
    return finite_number(value["mean_ante"], "paired mean"), finite_number(
        value["standard_error_ante"], "paired standard error"
    )


def trace_interpretation(row: dict[str, Any]) -> list[str]:
    training_actions = row["training_state"]["actions"]
    positive_regrets = {
        action: max(
            0.0,
            finite_number(
                training_actions[action]["cumulative_weighted_regret"], "regret"
            ),
        )
        for action in ACTIONS
    }
    average_weights = {
        action: max(
            0.0,
            finite_number(
                training_actions[action]["cumulative_average_weight"], "weight"
            ),
        )
        for action in ACTIONS
    }
    total_positive_regret = sum(positive_regrets.values())
    total_average_weight = sum(average_weights.values())
    current_call = finite_number(training_actions["call"]["current_strategy"], "current Call")
    average_call = finite_number(training_actions["call"]["average_strategy"], "average Call")
    expected_current_call = (
        positive_regrets["call"] / total_positive_regret
        if total_positive_regret > 0.0
        else 1.0 / len(ACTIONS)
    )
    expected_average_call = (
        average_weights["call"] / total_average_weight
        if total_average_weight > 0.0
        else 1.0 / len(ACTIONS)
    )
    require(
        abs(expected_current_call - current_call) < 1e-9,
        "current strategy does not match regret matching",
    )
    require(
        abs(expected_average_call - average_call) < 1e-9,
        "average strategy does not match strategy sum",
    )

    if total_positive_regret > 0.0:
        positive_actions = [
            action for action, regret in positive_regrets.items() if regret > 0.0
        ]
        if positive_actions == ["call"]:
            current_statement = (
                f"Call è l'unica azione con regret cumulativo positivo "
                f"({fmt_weight(positive_regrets['call'])}). Il regret matching assegna quindi a "
                f"Call il {fmt_percent(current_call)} della strategia corrente."
            )
        else:
            current_statement = (
                f"La frequenza corrente di Call ({fmt_percent(current_call)}) deriva esattamente "
                f"dalla normalizzazione dei regret positivi. Call possiede "
                f"{fmt_weight(positive_regrets['call'])} su "
                f"{fmt_weight(total_positive_regret)} di "
                "regret positivo totale."
            )
    else:
        current_statement = (
            f"Nessuna azione possiede regret positivo. La frequenza corrente di Call "
            f"({fmt_percent(current_call)}) deriva quindi dal fallback uniforme del regret "
            "matching."
        )

    if total_average_weight > 0.0:
        average_statement = (
            f"La frequenza media di Call ({fmt_percent(average_call)}) deriva dalla strategy "
                f"sum accumulata: {fmt_weight(average_weights['call'])} su "
                f"{fmt_weight(total_average_weight)}."
        )
    else:
        average_statement = (
            f"La strategy sum non contiene ancora peso. La frequenza media di Call "
            f"({fmt_percent(average_call)}) usa il fallback uniforme."
        )

    statements = [current_statement, average_statement]

    policies = row["policies"]
    for policy_name in ("average", "current"):
        if policy_name not in policies:
            continue
        policy = policies[policy_name]
        alternatives = {
            action: action_value(policy, action)[0]
            for action in ACTIONS
            if action != "call"
        }
        comparator = max(alternatives, key=alternatives.get)
        difference, standard_error = paired_call_difference(policy, comparator)
        lower, upper = confidence_interval(
            difference, standard_error, Z_BONFERRONI_95_FOUR
        )
        label = "media" if policy_name == "average" else "corrente"
        if lower > 0.0:
            conclusion = "Call è superiore nel campione paired"
        elif upper < 0.0:
            conclusion = "Call è inferiore nel campione paired"
        else:
            conclusion = "il campione paired non separa le due azioni"
        statements.append(
            f"Con la continuation {label}, Call − {comparator} vale {fmt_ante(difference)} "
            f"(IC simultaneo 95% Bonferroni [{fmt_ante(lower)}, {fmt_ante(upper)}]): "
            f"{conclusion}."
        )

    if average_call > current_call + 0.10:
        statements.append(
            "La strategia media conserva molto più Call della strategia corrente. Il divario misura "
            "inerzia dell'averaging o un cambio tardivo dei regret durante il training."
        )
    elif current_call > average_call + 0.10:
        statements.append(
            "La strategia corrente usa Call più della media storica. I regret recenti stanno ancora "
            "spostando la policy verso Call."
        )
    return statements


def render_policy_table(policy_name: str, policy: dict[str, Any]) -> list[str]:
    lines = [
        f"#### Continuation {policy_name}",
        "",
        "| Azione | EV | SE | Call − azione | IC 95% puntuale |",
        "| --- | ---: | ---: | ---: | --- |",
    ]
    for action in ACTIONS:
        mean, standard_error = action_value(policy, action)
        difference, difference_se = paired_call_difference(policy, action)
        lower, upper = confidence_interval(difference, difference_se)
        lines.append(
            f"| `{action}` | {fmt_ante(mean)} | {standard_error:.4f}a | "
            f"{fmt_ante(difference)} | [{fmt_ante(lower)}, {fmt_ante(upper)}] |"
        )
    return lines


def render_call_breakdown(policy_name: str, policy: dict[str, Any]) -> list[str]:
    call = policy["actions"]["call"]
    lines = [f"#### Scomposizione di Call, continuation {policy_name}", ""]

    terminal_groups: dict[tuple[str, str], dict[str, float]] = {}
    for branch in call["preflop_branches"]:
        key = (branch["terminal_type"], branch["terminal_street"])
        value = branch["value"]
        group = terminal_groups.setdefault(
            key, {"samples": 0.0, "probability": 0.0, "contribution": 0.0}
        )
        group["samples"] += finite_number(value["samples"], "terminal samples")
        group["probability"] += finite_number(value["probability"], "terminal probability")
        group["contribution"] += finite_number(
            value["ev_contribution_ante"], "terminal contribution"
        )
    lines.extend(
        [
            "| Terminale | Street | Probabilità | EV condizionato | Contributo EV |",
            "| --- | --- | ---: | ---: | ---: |",
        ]
    )
    for (terminal, street), group in sorted(
        terminal_groups.items(), key=lambda item: abs(item[1]["contribution"]), reverse=True
    ):
        conditional = (
            group["contribution"] / group["probability"]
            if group["probability"] > 0.0
            else 0.0
        )
        lines.append(
            f"| `{terminal}` | `{street}` | {fmt_percent(group['probability'])} | "
            f"{fmt_ante(conditional)} | {fmt_ante(group['contribution'])} |"
        )

    branches = sorted(
        call["preflop_branches"],
        key=lambda branch: abs(
            finite_number(
                branch["value"]["ev_contribution_ante"], "branch contribution"
            )
        ),
        reverse=True,
    )[:5]
    lines.extend(
        [
            "",
            "| Continuation preflop | Terminale | Probabilità | EV condizionato | Contributo EV |",
            "| --- | --- | ---: | ---: | ---: |",
        ]
    )
    for branch in branches:
        path = " → ".join(
            f"{step['player']}:{step['action']}" for step in branch["preflop_continuation"]
        ) or "nessuna azione successiva"
        value = branch["value"]
        lines.append(
            f"| {escape_cell(path)} | `{branch['terminal_type']}` | "
            f"{fmt_percent(finite_number(value['probability'], 'branch probability'))} | "
            f"{fmt_ante(finite_number(value['mean_payoff_ante'], 'branch EV'))} | "
            f"{fmt_ante(finite_number(value['ev_contribution_ante'], 'branch contribution'))} |"
        )

    lines.extend(
        [
            "",
            "Le righe per street non sono additive: ogni riga riusa il payoff terminale "
            "dell'intera mano e lo condiziona al raggiungimento della street.",
            "",
            "| Street raggiunta | Reach | EV finale condizionato | Reach × EV finale |",
            "| --- | ---: | ---: | ---: |",
        ]
    )
    for street in call["street_reach"]:
        value = street["value"]
        lines.append(
            f"| `{street['street']}` | "
            f"{fmt_percent(finite_number(value['probability'], 'street probability'))} | "
            f"{fmt_ante(finite_number(value['mean_payoff_ante'], 'street EV'))} | "
            f"{fmt_ante(finite_number(value['ev_contribution_ante'], 'street contribution'))} |"
        )

    buckets = sorted(
        call["buckets"],
        key=lambda bucket: abs(
            finite_number(
                bucket["value"]["ev_contribution_ante"], "bucket contribution"
            )
        ),
        reverse=True,
    )[:8]
    lines.extend(
        [
            "",
            "| Street | Player | Bucket | Reach | EV finale condizionato | Reach × EV finale |",
            "| --- | --- | ---: | ---: | ---: | ---: |",
        ]
    )
    for bucket in buckets:
        value = bucket["value"]
        lines.append(
            f"| `{bucket['street']}` | `{bucket['player']}` | `{bucket['bucket_key']}` | "
            f"{fmt_percent(finite_number(value['probability'], 'bucket probability'))} | "
            f"{fmt_ante(finite_number(value['mean_payoff_ante'], 'bucket EV'))} | "
            f"{fmt_ante(finite_number(value['ev_contribution_ante'], 'bucket contribution'))} |"
        )
    return lines


def render_summary(rows: list[dict[str, Any]]) -> list[str]:
    lines = [
        "## Esito sintetico",
        "",
        "| Classe | Continuation | Call nella policy | Migliore alternativa osservata | Call − alternativa | IC simultaneo 95% | Esito |",
        "| --- | --- | ---: | --- | ---: | --- | --- |",
    ]
    for row in rows:
        training_actions = row["training_state"]["actions"]
        for policy_name in ("average", "current"):
            if policy_name not in row["policies"]:
                continue
            policy = row["policies"][policy_name]
            call_strategy_field = (
                "average_strategy"
                if policy_name == "average"
                else "current_strategy"
            )
            call_strategy = finite_number(
                training_actions["call"][call_strategy_field], "Call strategy"
            )
            alternatives = {
                action: action_value(policy, action)[0]
                for action in ACTIONS
                if action != "call"
            }
            comparator = max(alternatives, key=alternatives.get)
            difference, standard_error = paired_call_difference(policy, comparator)
            lower, upper = confidence_interval(
                difference, standard_error, Z_BONFERRONI_95_FOUR
            )
            outcome = (
                "Call superiore"
                if lower > 0.0
                else "Call inferiore"
                if upper < 0.0
                else "non separato"
            )
            lines.append(
                f"| `{row['hand_class']}` | `{policy_name}` | "
                f"{fmt_percent(call_strategy)} | `{comparator}` | "
                f"{fmt_ante(difference)} | [{fmt_ante(lower)}, {fmt_ante(upper)}] | "
                f"{outcome} |"
            )
    return lines


def render_report(
    document: dict[str, Any],
    rows: list[dict[str, Any]],
    checksum: str,
    source: Path,
) -> str:
    for row in rows:
        validate_row(row)
    lines = [
        "# V20 — Root decision trace",
        "",
        "Stato: `DIAGNOSTIC / NOT A CONVERGENCE CERTIFICATE`  ",
        f"Input: `{source}`  ",
        f"SHA-256: `{checksum}`  ",
        f"Tree: `{document.get('tree_fingerprint', 'unknown')}`  ",
        f"Algoritmo: `{document.get('algorithm', 'unknown')}`  ",
        f"Astrazione: `{document.get('abstraction', 'unknown')}`  ",
        f"Evaluator: `{document.get('evaluator_backend', 'unknown')}`  ",
        f"Iterazioni: `{document.get('iterations', 'unknown')}`  ",
        f"Seed training/partizione/valutazione: `{document.get('seed', 'unknown')}` / "
        f"`{document.get('partition_seed', 'unknown')}` / "
        f"`{document.get('evaluation_seed', 'unknown')}`",
        "",
        "## Ambito",
        "",
        "La trace legge la policy dopo il training. Per ogni classe usa gli stessi deal fisici e lo "
        "stesso stato RNG iniziale per le cinque azioni root. Regret, strategy sum e policy non "
        "vengono aggiornati. Gli intervalli descrivono questa valutazione campionata; non sono una "
        "NashConv né una prova di equivalenza con Monker. Le tabelle mostrano intervalli puntuali; "
        "la lettura contro la migliore alternativa osservata applica Bonferroni ai quattro "
        "confronti con Call.",
    ]
    lines.extend([""] + render_summary(rows))
    for row in rows:
        training = row["training_state"]
        lines.extend(
            [
                "",
                f"## {row['hand_class']}",
                "",
                f"Deal condizionati per azione: `{row['deals_per_action']}`. Ultimo update "
                f"dell'infoset root: `{training['last_update_iteration']}`.",
                "",
                "| Azione | Strategia media | Strategia corrente | Regret cumulativo positivo | Strategy sum | Vantaggio medio nel training |",
                "| --- | ---: | ---: | ---: | ---: | ---: |",
            ]
        )
        for action in ACTIONS:
            state = training["actions"][action]
            advantage = training["action_advantages"][action]
            lines.append(
                f"| `{action}` | {fmt_percent(finite_number(state['average_strategy'], 'average strategy'))} | "
                f"{fmt_percent(finite_number(state['current_strategy'], 'current strategy'))} | "
                f"{fmt_weight(max(0.0, finite_number(state['cumulative_weighted_regret'], 'regret')))} | "
                f"{fmt_weight(finite_number(state['cumulative_average_weight'], 'strategy sum'))} | "
                f"{fmt_ante(finite_number(advantage['weighted_mean_ante'], 'training advantage'))} |"
            )
        lines.extend(["", "### Lettura causale", ""])
        lines.extend(f"- {statement}" for statement in trace_interpretation(row))
        for policy_name in ("average", "current"):
            if policy_name not in row["policies"]:
                continue
            lines.extend([""] + render_policy_table(policy_name, row["policies"][policy_name]))
            lines.extend([""] + render_call_breakdown(policy_name, row["policies"][policy_name]))
    lines.extend(
        [
            "",
            "## Decisione consentita",
            "",
            "La trace identifica il meccanismo interno che sostiene Call e i rami che ne producono "
            "l'EV. Un cambiamento al solver richiede il successivo holdout fisico paired. La "
            "frequenza Monker resta un confronto descrittivo perché il suo contratto completo non è "
            "disponibile.",
            "",
        ]
    )
    return "\n".join(lines)


def main() -> int:
    args = parse_args()
    document, rows, checksum = load_rows(args.trace)
    report = render_report(document, rows, checksum, args.trace)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(report, encoding="utf-8", newline="\n")
    print(f"HU_PREFLOP_ROOT_DECISION_TRACE_ANALYSIS=PASS output={args.output} rows={len(rows)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
