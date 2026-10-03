"""S3 of the correctness coverage (shared_components.md section 3): the compiled game tree, its amounts,
pots, uncalled returns and every terminal payoff row (rake included) against a referee written from the
rules (sdref/rules.py), not from the engine.

Input: the JSON Lines dump of `gtosd_preflop_blueprint_game --config X --dump-nodes FILE` (header, one record
per node in preorder, end record). With --executable the script runs the benchmark itself, once per config;
with --dump it reads an existing dump of one config.

Checks, per config:
- D  dump structure: header (schema, config id, players, positions, units), end record and node count,
     preorder ids, parent / edge_index / child consistency, every path = parent path + incoming edge; and the
     header's tree_fingerprint equals the one recorded for the runs (RECORDED_TREE_FINGERPRINTS, the HU6
     family of benchmarks/monker/correctness/README.md, plus --expect-tree), so that the conclusions on the
     new build transfer to the runs (critic S-b).
- R  replay of every engine path from the config JSON with the rules R1-R13, edge by edge, independent of
     the action abstraction:
     R-legal   every engine action is legal (fold / check / call amount = min(to call, stack), bet >= minimum
               bet, raise increment >= minimum raise unless all-in or the configured incomplete level-1
               target, nobody puts in more than their stack, no re-raise after an incomplete raise);
     R-state   at every node: kind, street, status, actor, players in the hand, all-in players, pot, antes,
               gross and net contributions, street commitments, stacks, uncalled returns, bet to match and
               minimum raise (where the actor can raise), remaining board cards;
     R-payoff  every stored payoff row (the fold row, every winner subset at a showdown: 3 rows with two
               players left, 7 with three) = share of (pot - rake) - own net contribution, rake =
               min(percentage x pot, cap) when raked, odd units per C11;
     R-sidepot on the engine's own fields at every node (R9b): every all-in player in the hand holds the
               largest net contribution (no side pot), and once the action on a street is over the players
               in the hand have equal street commitments, or after a fold the winner's commitment equals
               the largest other one (nothing left uncalled);
     R-identity every payoff row satisfies R11-R12 whatever the odd-chip order: it sums to minus the rake,
               every non-winner loses exactly their contribution, the winners' receipts differ by at most one
               unit and exactly (pot - rake) mod |winners| of them hold the extra unit.
- A  the configured action abstraction: the engine's actions at every decision = the referee's (C3-C10),
     aggression level and limped-pot flag; and the path set of the engine tree = the path set of a tree the
     referee enumerates on its own (missing and extra paths).
- C  labels and edge order (C15).
- Coverage (SD_REFEREE_COVERAGE, per config): terminals by kind, street and players in the hand, decisions
  by players with chips, payoff rows with odd units, with the rake capped, at the percentage or unraked,
  incomplete all-in raises and short calls (always 0, R9b), so that the 3-way report can say which
  situations the comparison exercised.

Three players (3WAY50 family): the betting rounds, folds with dead money, two- and three-way showdowns, the
odd chips of the three-way ties and the rake per winner set are all covered by the same R, A and C checks;
the games are side-pot free by R9b, which R-sidepot verifies on the engine's tree instead of assuming.

Classification: a failure that disappears when one convention takes one of its alternatives (and no new
failure appears) is reported as a convention mismatch (name, id, the engine's apparent choice), not as a rule
failure; the conventions in force are always listed. Rule and abstraction failures are the bugs.

Exit codes: 0 PASS; 1 rule, abstraction or dump failures; 3 only convention mismatches; 2 usage error;
77 every config was missing (CTest SKIP).

Usage:
  python tools/independent/sd_referee.py --executable <gtosd_preflop_blueprint_game> \
      [--scratch-dir DIR] [--report report.json] CONFIG[#MODE] ...
  python tools/independent/sd_referee.py --dump nodes.jsonl CONFIG[#MODE]
  python tools/independent/sd_referee.py --enumerate-only CONFIG[#MODE] ...
MODE is full (default), preflop_only or checkdown (the benchmark's --preflop-only / --checkdown).
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
try:
    from sdref import rules as R
except ImportError:  # the package __init__ imports numpy; rules.py itself needs only the standard library
    import importlib.util

    _spec = importlib.util.spec_from_file_location("sdref_rules", HERE / "sdref" / "rules.py")
    R = importlib.util.module_from_spec(_spec)
    sys.modules["sdref_rules"] = R
    _spec.loader.exec_module(R)

DUMP_SCHEMA = "gtosd.preflop_blueprint_game_nodes.v1"
MODES = ("full", "preflop_only", "checkdown")
MODE_FLAGS = {"full": [], "preflop_only": ["--preflop-only"], "checkdown": ["--checkdown"]}
# Tree fingerprints recorded for the training / certification runs (benchmarks/monker/correctness/README.md).
# The dump comes from a new build, so its conclusions transfer to those runs only if the dump's tree_fingerprint
# equals the recorded one (critic S-b, plan 3.5). Key: (config file stem, mode). More with --expect-tree.
RECORDED_TREE_FINGERPRINTS = {
    ("HU6_all", "full"): "fnv1a64:fb76ddcd880fec5f",
    ("HU6_all_rake25cap2", "full"): "fnv1a64:f226b87d43f28215",
    ("HU6_V0_flop", "full"): "fnv1a64:cd66796bdbdac5c1",
    ("HU6_V1_flopturn", "full"): "fnv1a64:da5c6942354ad5ad",
    ("HU6_V2_river", "full"): "fnv1a64:2f109f6f1891d9f2",
    # 3WAY50 family (benchmarks/monker), as compiled on 01/10/2026 by the phase-3 build (library of commit
    # 12fe441, the one frozen for the first 3WAY50_15x4_rake25cap2 run). 3WAY50_donk and the allin5x twin
    # also appear in the lane-1 game report of 30/09. 7,225 nodes (7,126 for allin5x), 3 seats, full mode.
    ("3WAY50_donk", "full"): "fnv1a64:49412deedbe6f6cc",
    ("3WAY50_donk_rake", "full"): "fnv1a64:e32bb91ba7097549",
    ("3WAY50_donk_rake25cap2", "full"): "fnv1a64:71abeabaab92fe56",
    ("3WAY50_donk_rake5cap075", "full"): "fnv1a64:19983d7479b9bfe4",
    ("3WAY50_donk_allin5x", "full"): "fnv1a64:78322185bfa4d03d",
}


# ---------------------------------------------------------------------------------------------- model


@dataclass
class RefNode:
    path: tuple[str, ...]
    kind: str
    settle_kind: str | None
    remaining: int
    hand: "R.Hand"
    actions: list = field(default_factory=list)


def classify(hand, mode: str) -> tuple[str, str | None, int]:
    """(node kind, settlement kind, board cards still to come) of a state (R8, R10, C16)."""
    status = hand.status
    if status == "in_progress":
        return "decision", None, 0
    if status == "folded":
        return "terminal_fold", "fold", 0
    if status == "showdown":
        return "terminal_showdown", "showdown", 0
    if status == "all_in_runout":
        return "terminal_showdown", "runout", 5 - R.BOARD_CARDS_DEALT[hand.street]
    if status == "street_complete":
        if hand.street == R.PREFLOP and mode == "checkdown":
            return "terminal_showdown", "checkdown", 5
        return "chance", None, 0
    raise ValueError(f"unknown status {status}")


def enumerate_tree(rules, conv, mode: str) -> list[RefNode]:
    """The referee's own game tree in preorder (edge order C15)."""
    nodes: list[RefNode] = []

    def visit(hand, path: tuple[str, ...]) -> None:
        kind, settle_kind, remaining = classify(hand, mode)
        node = RefNode(path, kind, settle_kind, remaining, hand)
        nodes.append(node)
        if kind == "decision":
            node.actions = R.abstraction_actions(rules, conv, hand)
            for action in node.actions:
                visit(R.apply_action(hand, action), path + (action.token,))
        elif kind == "chance" and not (hand.street == R.PREFLOP and mode == "preflop_only"):
            visit(R.next_street(rules, hand), path + ("chance",))

    visit(R.initial_hand(rules), ())
    return nodes


def tree_stats(nodes: list[RefNode]) -> dict:
    stats = Counter()
    for node in nodes:
        street = R.STREET_NAMES[node.hand.street]
        stats["nodes"] += 1
        stats[node.kind] += 1
        if node.kind == "decision":
            stats[f"decisions_{street}"] += 1
            stats["edges"] += len(node.actions)
        if node.kind == "chance" and node.hand.street == R.PREFLOP:
            stats["postflop_entries"] += 1
        if node.settle_kind in ("runout", "checkdown"):
            stats[f"all_in_runouts_{street}"] += 1
        stats["maximum_depth"] = max(stats["maximum_depth"], len(node.path))
    return dict(sorted(stats.items()))


def coverage_stats(rules, conv, nodes: list[RefNode]) -> dict:
    """Which situations the referee's tree (= the engine's once check A passes) exercises: terminals by kind,
    street and players in the hand, decisions by players with chips, payoff rows by odd units and rake
    regime, incomplete all-in raises and short calls (R9b: always 0)."""
    stats = Counter()
    for node in nodes:
        hand = node.hand
        street = R.STREET_NAMES[hand.street]
        live = sum(1 for p in range(hand.players) if hand.in_hand[p])
        if node.kind == "decision":
            with_chips = sum(1 for p in range(hand.players) if hand.has_chips(p))
            stats[f"decisions_{with_chips}chips"] += 1
            to_call = hand.to_call(hand.to_act)
            for action in node.actions:
                if action.kind == "call" and action.amount < to_call:
                    stats["short_calls"] += 1
                if action.aggressive:
                    increment = hand.street_bets[hand.to_act] + action.amount - hand.bet_to_match
                    if increment < hand.minimum_raise:
                        stats["incomplete_all_in_raises" if action.kind == "all_in"
                              else "incomplete_raises"] += 1
            continue
        if node.kind == "chance":
            continue
        label = "fold" if node.settle_kind == "fold" else node.settle_kind
        stats[f"terminal_{label}_{street}" + ("" if label == "fold" else f"_{live}live")] += 1
        rake = R.rake_of(rules, conv, hand, node.settle_kind)
        for mask, _ in expected_payoffs(rules, conv, hand, node.settle_kind):
            stats["rows"] += 1
            if (hand.pot - rake) % bin(mask).count("1"):
                stats["rows_odd_units"] += 1
            if rake == 0:
                stats["rows_unraked"] += 1
            elif rules.rake.enabled and rake == rules.rake.cap:
                stats["rows_rake_capped"] += 1
            else:
                stats["rows_rake_percent"] += 1
    for key in ("short_calls", "incomplete_all_in_raises", "incomplete_raises", "rows_odd_units"):
        stats.setdefault(key, 0)
    return dict(sorted(stats.items()))


def winner_masks(mask: int) -> list[int]:
    """Non-empty subsets of `mask` in increasing numeric order (the engine's row order)."""
    return [winners for winners in range(1, mask + 1) if winners & ~mask == 0]


def expected_payoffs(rules, conv, hand, settle_kind: str) -> list[tuple[int, list[int]]]:
    if settle_kind == "fold":
        winner = hand.in_hand_mask
        return [(winner, R.settle(rules, conv, hand, "fold", winner)[0])]
    return [(mask, R.settle(rules, conv, hand, settle_kind, mask)[0]) for mask in winner_masks(hand.in_hand_mask)]


# ----------------------------------------------------------------------------------------------- dump


class DumpError(RuntimeError):
    pass


def load_dump(path: Path) -> tuple[dict, list[dict]]:
    lines = path.read_text(encoding="utf-8").splitlines()
    if not lines:
        raise DumpError(f"{path}: empty dump")
    header = json.loads(lines[0])
    if header.get("record") != "header" or header.get("schema") != DUMP_SCHEMA:
        raise DumpError(f"{path}: first record is not a {DUMP_SCHEMA} header")
    nodes, end = [], None
    for line in lines[1:]:
        record = json.loads(line)
        if record.get("record") == "node":
            nodes.append(record)
        elif record.get("record") == "end":
            end = record
    if end is None:
        raise DumpError(f"{path}: no end record (truncated dump)")
    if end["nodes"] != len(nodes) or header["node_count"] != len(nodes):
        raise DumpError(f"{path}: {len(nodes)} node records, end says {end['nodes']}, "
                        f"header says {header['node_count']}")
    if [record["id"] for record in nodes] != list(range(len(nodes))):
        raise DumpError(f"{path}: node ids are not 0..n-1 in order")
    return header, nodes


def engine_action(edge: dict):
    return R.Action(edge["type"], edge["amount"], edge["all_in_kind"], edge["requested_basis_points"])


# --------------------------------------------------------------------------------------------- checks


class Findings:
    """Failures keyed by (check, node id or path) so that the convention runs can be compared."""

    def __init__(self) -> None:
        self.items: dict[tuple, dict] = {}
        self.counts = Counter()

    def fail(self, category: str, check: str, where, detail: str, path=None) -> None:
        key = (category, check, where)
        if key not in self.items:
            self.items[key] = {"category": category, "check": check, "where": where,
                               "path": "/".join(path) if path is not None else None, "detail": detail}

    def count(self, what: str, amount: int = 1) -> None:
        self.counts[what] += amount


def check_header(rules, header: dict, mode: str, findings: Findings) -> None:
    expected = {"config_id": rules.config_id, "player_count": rules.players,
                "positions": list(rules.positions), "units_per_ante": R.UNITS_PER_ANTE, "mode": mode}
    for key, value in expected.items():
        if header.get(key) != value:
            findings.fail("dump", "header", key, f"engine {header.get(key)!r}, expected {value!r}")


def compare_state(rules, record: dict, hand, kind: str, remaining: int, findings: Findings) -> None:
    """R-state: the engine's public state at a node against the referee's (C14 accounting)."""
    node = record["id"]
    path = record["path"]
    players = hand.players

    def expect(check: str, engine, referee, category: str = "rule") -> None:
        if engine != referee:
            findings.fail(category, check, node, f"engine {engine}, referee {referee}", path)

    expect("kind", record["kind"], kind)
    expect("street", record["street"], R.STREET_NAMES[hand.street])
    expect("state_street", record["state_street"], R.STREET_NAMES[hand.street])
    expect("status", record["status"], hand.status)
    expect("remaining_board_cards", record["remaining_board_cards"], remaining)
    expect("active_mask", record["active_mask"], hand.in_hand_mask)
    expect("all_in_mask", record["all_in_mask"], hand.all_in_mask)
    expect("pot", record["pot"], hand.pot)
    expect("initial_pot", record["initial_pot"], sum(hand.antes))
    expect("initial_pot_contributions", record["initial_pot_contributions"], hand.antes[:players])
    expect("committed_total", record["committed_total"], hand.put_in)
    expect("committed_this_street", record["committed_this_street"], hand.street_bets)
    expect("remaining_stacks", record["remaining_stacks"], hand.stacks)
    expect("returned_uncalled_by_player", record["returned_uncalled_by_player"], hand.returned)
    expect("returned_uncalled", record["returned_uncalled"], sum(hand.returned))
    net_engine = [record["initial_pot_contributions"][p] + record["committed_total"][p]
                  - record["returned_uncalled_by_player"][p] for p in range(players)]
    expect("net_contribution", net_engine, [hand.contribution(p) for p in range(players)])
    expect("chip_conservation", sum(record["remaining_stacks"]) + record["pot"], players * rules.stack)
    # R-sidepot (R9b, R7-R8) on the engine's own fields, not on the replay.
    why = R.side_pot_violation(players, record["active_mask"], record["remaining_stacks"], net_engine)
    if why is not None:
        findings.fail("rule", "side_pot", node, why, path)
    why = R.unmatched_round(players, record["active_mask"], record["status"], record["committed_this_street"])
    if why is not None:
        findings.fail("rule", "round_matched", node, why, path)
    expect("level", record["level"], hand.level, "abstraction")
    expect("limped_pot", record["limped_pot"], hand.limped, "abstraction")
    if kind == "decision":
        expect("actor", record["actor"], hand.to_act)
        expect("player_to_act", record["player_to_act"], hand.to_act)
        expect("bet_to_match", record["current_bet"], hand.bet_to_match)
        to_call = hand.to_call(hand.to_act)
        if 0 < to_call < hand.stacks[hand.to_act]:
            expect("minimum_raise", record["last_full_raise_increment"], hand.minimum_raise)
    else:
        expect("actor", record["actor"], None)
    if kind == "terminal_fold":
        expect("fold_winner", record["terminal_winner_mask"], hand.in_hand_mask)


def check_payoffs(rules, conv, record: dict, hand, settle_kind: str, findings: Findings) -> int:
    """R-payoff: every stored row against the referee's settlement. Returns the number of rows compared."""
    node = record["id"]
    engine_rows = [(row["winners"], row["payoff"]) for row in record["payoffs"]]
    expected = expected_payoffs(rules, conv, hand, settle_kind)
    if [mask for mask, _ in engine_rows] != [mask for mask, _ in expected]:
        findings.fail("rule", "payoff_winner_masks", node,
                      f"engine {[m for m, _ in engine_rows]}, referee {[m for m, _ in expected]}",
                      record["path"])
        return 0
    rake = R.rake_of(rules, conv, hand, settle_kind)
    for (mask, engine), (_, referee) in zip(engine_rows, expected):
        if engine != referee:
            findings.fail("rule", "payoff_row", (node, mask),
                          f"winners {mask}: engine {engine}, referee {referee} (pot {hand.pot}, "
                          f"rake {rake})", record["path"])
        why = R.settlement_identity_violation(hand, mask, rake, engine)  # R-identity
        if why is not None:
            findings.fail("rule", "payoff_identity", (node, mask), f"winners {mask}: {why}", record["path"])
    return len(engine_rows)


def check_actions(rules, conv, record: dict, hand, findings: Findings) -> list:
    """R-legal and A-actions at a decision. Returns the engine's actions."""
    node = record["id"]
    path = record["path"]
    engine = [engine_action(edge) for edge in record["edges"]]
    incomplete = R.incomplete_allowed_at(rules, hand)
    for index, action in enumerate(engine):
        why = R.rule_violation(rules, hand, action, incomplete)
        if why is not None:
            findings.fail("rule", "legal_action", (node, index), why, path)
    try:
        referee = R.abstraction_actions(rules, conv, hand)
    except R.ConfigError as error:
        findings.fail("abstraction", "abstraction_error", node, str(error), path)
        return engine
    engine_tokens = [a.token for a in engine]
    referee_tokens = [a.token for a in referee]
    if sorted(engine_tokens) != sorted(referee_tokens):
        findings.fail("abstraction", "action_set", node,
                      f"engine {engine_tokens}, referee {referee_tokens}", path)
    elif engine_tokens != referee_tokens:
        findings.fail("convention", "edge_order_C15", node,
                      f"engine {engine_tokens}, referee {referee_tokens}", path)
    else:
        for index, (mine, theirs) in enumerate(zip(referee, engine)):
            if mine.all_in_kind != theirs.all_in_kind:
                findings.fail("rule", "all_in_kind", (node, index),
                              f"{theirs.token}: engine {theirs.all_in_kind}, referee {mine.all_in_kind}", path)
            if mine.size_bp != theirs.size_bp:
                findings.fail("convention", "size_label_C15", (node, index),
                              f"{theirs.token}: engine {theirs.size_bp} bp, referee {mine.size_bp} bp", path)
    return engine


def referee_check(rules, conv, mode: str, header: dict, records: list[dict]) -> Findings:
    """All checks D, R, A, C of one dump under one set of conventions."""
    findings = Findings()
    check_header(rules, header, mode, findings)
    hands: list = [None] * len(records)
    hands[0] = R.initial_hand(rules)
    if records[0]["parent"] is not None or records[0]["path"]:
        findings.fail("dump", "root", 0, "the first record is not the root")
    for record in records:
        node = record["id"]
        hand = hands[node]
        if hand is None:
            findings.fail("dump", "unreachable", node, "no parent edge leads to this node", record["path"])
            continue
        kind, settle_kind, remaining = classify(hand, mode)
        compare_state(rules, record, hand, kind, remaining, findings)
        findings.count("nodes")
        edges = record["edges"]
        children = []
        if record["kind"] == "decision" and kind == "decision":
            findings.count("decisions")
            actions = check_actions(rules, conv, record, hand, findings)
            for edge, action in zip(edges, actions):
                try:
                    children.append((edge, action.token, R.apply_action(hand, action)))
                except R.UnsupportedSituation as error:
                    findings.fail("rule", "unsupported", node, str(error), record["path"])
            findings.count("edges", len(edges))
        elif record["kind"] == "chance" and kind == "chance":
            stop = hand.street == R.PREFLOP and mode == "preflop_only"
            if len(edges) != (0 if stop else 1):
                findings.fail("dump", "chance_edges", node, f"{len(edges)} edges", record["path"])
            for edge in edges:
                children.append((edge, "chance", R.next_street(rules, hand)))
        elif record["kind"].startswith("terminal") and kind == record["kind"]:
            if edges:
                findings.fail("dump", "terminal_edges", node, f"{len(edges)} edges", record["path"])
            findings.count("payoff_rows", check_payoffs(rules, conv, record, hand, settle_kind, findings))
        for index, (edge, token, child_hand) in enumerate(children):
            child = edge["child"]
            if not 0 <= child < len(records) or records[child]["parent"] != node \
                    or records[child]["edge_index"] != index:
                findings.fail("dump", "child_link", (node, index), f"child {child}", record["path"])
                continue
            if records[child]["path"] != record["path"] + [token]:
                findings.fail("dump", "child_path", child, f"path {records[child]['path']}",
                              record["path"])
            hands[child] = child_hand
    # A: path sets of the engine tree and of the referee's own enumeration.
    try:
        own = enumerate_tree(rules, conv, mode)
    except (R.ConfigError, R.UnsupportedSituation) as error:
        findings.fail("abstraction", "enumeration_error", "tree", str(error))
        return findings
    engine_paths = {tuple(record["path"]) for record in records}
    own_paths = {node.path for node in own}
    for path in sorted(own_paths - engine_paths):
        findings.fail("abstraction", "missing_path", path, "the referee's tree has it, the engine's not", path)
    for path in sorted(engine_paths - own_paths):
        findings.fail("abstraction", "extra_path", path, "the engine's tree has it, the referee's not", path)
    findings.count("paths_engine", len(engine_paths))
    findings.count("paths_referee", len(own_paths))
    return findings


# ------------------------------------------------------------------------------------ classification


def classify_findings(rules, mode: str, header: dict, records: list[dict], base: Findings,
                      enabled: bool) -> tuple[list[dict], list[dict], list[dict]]:
    """(failures, convention mismatches, partial matches). A failure is a convention mismatch when some
    single-convention alternative removes it without adding a failure."""
    explained: dict[tuple, list[str]] = {}
    mismatches, partial = [], []
    if enabled and any(item["category"] != "dump" for item in base.items.values()):
        for name, value, conv in R.alternative_conventions():
            other = referee_check(rules, conv, mode, header, records)
            removed = set(base.items) - set(other.items)
            added = set(other.items) - set(base.items)
            if not removed:
                continue
            convention_id = R.CONVENTIONS[name][0]
            entry = {"convention": name, "id": convention_id, "engine_behaves_as": value,
                     "explains": len(removed), "new_failures": len(added)}
            if added:
                partial.append(entry)
                continue
            mismatches.append(entry)
            for key in removed:
                explained.setdefault(key, []).append(f"{convention_id} {name}={value}")
    failures = []
    for key, item in base.items.items():
        if key in explained:
            continue
        failures.append(item)
    return failures, mismatches, partial


# ------------------------------------------------------------------------------------------ self-test


def synthesize_dump(rules, conv, mode: str) -> tuple[dict, list[dict]]:
    """A dump in the engine's format written from the referee's own tree (self-test only: it checks the
    referee's machinery, not the engine)."""
    nodes = enumerate_tree(rules, conv, mode)
    index = {node.path: i for i, node in enumerate(nodes)}
    records = []
    for i, node in enumerate(nodes):
        hand = node.hand
        parent = index[node.path[:-1]] if node.path else None
        edge_index = None
        if parent is not None:
            above = nodes[parent]
            edge_index = 0 if above.kind == "chance" else [a.token for a in above.actions].index(node.path[-1])
        edges = []
        if node.kind == "decision":
            edges = [{"type": a.kind, "amount": a.amount, "all_in_kind": a.all_in_kind,
                      "requested_basis_points": a.size_bp, "child": index[node.path + (a.token,)]}
                     for a in node.actions]
        elif node.kind == "chance" and node.path + ("chance",) in index:
            edges = [{"type": "chance", "child": index[node.path + ("chance",)]}]
        payoffs = []
        if node.kind.startswith("terminal"):
            payoffs = [{"winners": mask, "payoff": row}
                       for mask, row in expected_payoffs(rules, conv, hand, node.settle_kind)]
        records.append({
            "record": "node", "id": i, "parent": parent, "edge_index": edge_index, "depth": len(node.path),
            "kind": node.kind, "street": R.STREET_NAMES[hand.street],
            "actor": hand.to_act if node.kind == "decision" else None, "level": hand.level,
            "limped_pot": hand.limped, "remaining_board_cards": node.remaining,
            "active_mask": hand.in_hand_mask, "status": hand.status,
            "state_street": R.STREET_NAMES[hand.street],
            "player_to_act": hand.to_act if hand.to_act is not None else 0,
            "all_in_mask": hand.all_in_mask,
            "terminal_winner_mask": hand.in_hand_mask if node.kind == "terminal_fold" else 0,
            "pot": hand.pot, "initial_pot": sum(hand.antes), "current_bet": hand.bet_to_match,
            "last_full_raise_increment": hand.minimum_raise, "returned_uncalled": sum(hand.returned),
            "initial_pot_contributions": list(hand.antes), "committed_total": list(hand.put_in),
            "committed_this_street": list(hand.street_bets), "remaining_stacks": list(hand.stacks),
            "returned_uncalled_by_player": list(hand.returned), "path": list(node.path), "edges": edges,
            "payoffs": payoffs})
    header = {"record": "header", "schema": DUMP_SCHEMA, "config_id": rules.config_id, "mode": mode,
              "player_count": rules.players, "positions": list(rules.positions),
              "units_per_ante": R.UNITS_PER_ANTE, "node_count": len(records)}
    return header, records


def self_test(config: Path, mode: str) -> bool:
    """The referee passes its own tree, catches tampered amounts and payoffs, and attributes a dump written
    under an alternative convention to that convention."""
    rules = R.load_rules(config)
    label = config.stem + ("" if mode == "full" else f"#{mode}")
    header, records = synthesize_dump(rules, R.Conventions(), mode)
    outcomes, ok = [], True
    base = referee_check(rules, R.Conventions(), mode, header, records)
    ok &= not base.items
    outcomes.append(f"own_tree={'pass' if not base.items else 'FAIL ' + str(len(base.items))}")

    tampered = json.loads(json.dumps(records))
    terminal = next(r for r in tampered if r["payoffs"])
    terminal["payoffs"][0]["payoff"][0] += 1
    found = referee_check(rules, R.Conventions(), mode, header, tampered)
    caught = any(item["check"] == "payoff_row" for item in found.items.values())
    ok &= caught
    outcomes.append(f"payoff+1={'caught' if caught else 'MISSED'}")

    tampered = json.loads(json.dumps(records))
    decision = next(r for r in tampered if any(e["type"] == "call" for e in r["edges"]))
    edge = next(e for e in decision["edges"] if e["type"] == "call")
    edge["amount"] += 1
    found = referee_check(rules, R.Conventions(), mode, header, tampered)
    caught = any(item["check"] == "legal_action" for item in found.items.values())
    ok &= caught
    outcomes.append(f"call+1={'caught' if caught else 'MISSED'}")

    if rules.players >= 3:
        # A three-way tie with odd units: one unit moved from the lowest winner to the highest (a wrong C11).
        tampered = json.loads(json.dumps(records))
        tie = next(((r, row) for r in tampered for row in r["payoffs"]
                    if bin(row["winners"]).count("1") == 3 and len(set(row["payoff"])) > 1), None)
        if tie is not None:
            _, row = tie
            seats = [p for p in range(rules.players) if row["winners"] >> p & 1]
            row["payoff"][seats[0]] -= 1
            row["payoff"][seats[-1]] += 1
            found = referee_check(rules, R.Conventions(), mode, header, tampered)
            failures, _, _ = classify_findings(rules, mode, header, tampered, found, True)
            caught = any(item["check"] == "payoff_row" for item in failures)
            ok &= caught
            outcomes.append(f"odd_chip_moved={'caught' if caught else 'MISSED'}")
        # The rake charged to the non-winner instead of the winners (a wrong R11/R13 with three seats).
        tampered = json.loads(json.dumps(records))
        raked = next(((r, row) for r in tampered for row in r["payoffs"]
                      if sum(row["payoff"]) < 0 and bin(row["winners"]).count("1") < bin(r["active_mask"]).count("1")),
                     None)
        if raked is not None:
            record, row = raked
            rake = -sum(row["payoff"])
            loser = next(p for p in range(rules.players) if record["active_mask"] >> p & 1 and not row["winners"] >> p & 1)
            winner = next(p for p in range(rules.players) if row["winners"] >> p & 1)
            row["payoff"][loser] -= rake
            row["payoff"][winner] += rake
            found = referee_check(rules, R.Conventions(), mode, header, tampered)
            checks = {item["check"] for item in found.items.values()}
            caught = "payoff_row" in checks and "payoff_identity" in checks
            ok &= caught
            outcomes.append(f"rake_on_loser={'caught' if caught else 'MISSED'}")
        # An all-in player with less in the pot than another player in the hand (a side pot, against R9b):
        # at a showdown without all-in players, one seat is marked all-in with one unit less committed.
        tampered = json.loads(json.dumps(records))
        showdown = next((r for r in tampered if r["kind"] == "terminal_showdown" and r["all_in_mask"] == 0), None)
        if showdown is not None:
            seat = next(p for p in range(rules.players) if showdown["active_mask"] >> p & 1)
            showdown["remaining_stacks"][seat] = 0
            showdown["committed_total"][seat] -= 1
            found = referee_check(rules, R.Conventions(), mode, header, tampered)
            caught = any(item["check"] == "side_pot" for item in found.items.values())
            ok &= caught
            outcomes.append(f"side_pot={'caught' if caught else 'MISSED'}")

    for name, value, conv in R.alternative_conventions():
        other_header, other = synthesize_dump(rules, conv, mode)
        if other == records:
            continue  # this alternative does not change this game
        found = referee_check(rules, R.Conventions(), mode, other_header, other)
        failures, mismatches, _ = classify_findings(rules, mode, other_header, other, found, True)
        attributed = any(m["convention"] == name and m["engine_behaves_as"] == value for m in mismatches)
        clean = attributed and not failures
        ok &= clean
        outcomes.append(f"{R.CONVENTIONS[name][0]}:{name}={value}:"
                        f"{'attributed' if clean else f'NOT ATTRIBUTED ({len(failures)} left)'}")
    print(f"SD_REFEREE_SELFTEST {label} {' '.join(outcomes)} -> {'PASS' if ok else 'FAIL'}")
    return ok


# ----------------------------------------------------------------------------------------------- main


def parse_case(text: str) -> tuple[Path, str]:
    path, _, mode = text.partition("#")
    mode = mode or "full"
    if mode not in MODES:
        raise SystemExit(f"unknown mode {mode!r} in {text!r} (expected one of {', '.join(MODES)})")
    return Path(path), mode


def run_engine(executable: Path, config: Path, mode: str, dump: Path) -> None:
    dump.parent.mkdir(parents=True, exist_ok=True)
    command = [str(executable), "--config", str(config), *MODE_FLAGS[mode], "--dump-nodes", str(dump)]
    result = subprocess.run(command, capture_output=True, text=True, check=False)
    if result.returncode != 0 or "PREFLOP_BLUEPRINT_GAME=PASS" not in result.stdout:
        raise DumpError(f"{' '.join(command)} failed ({result.returncode}): {result.stderr.strip()}")


def json_key(where) -> str:
    return where if isinstance(where, str) else json.dumps(where)


def summarize(items: list[dict], limit: int) -> dict:
    by_check = Counter(f"{item['category']}:{item['check']}" for item in items)
    examples = [dict(item, where=json_key(item["where"])) for item in items[:limit]]
    return {"count": len(items), "by_check": dict(sorted(by_check.items())), "examples": examples}


def conventions_in_force() -> dict:
    listing = {cid: text for cid, text in R.FIXED_CONVENTIONS.items()}
    defaults = R.Conventions()
    for name, (cid, text, alternatives) in R.CONVENTIONS.items():
        listing[f"{cid} {name}"] = (f"{text} [default {getattr(defaults, name)!r}; alternatives "
                                    f"{', '.join(repr(a) for a in alternatives)}]")
    return dict(sorted(listing.items(), key=lambda item: int(item[0].split()[0][1:])))


def referee_case(args, config: Path, mode: str, index: int) -> dict:
    rules = R.load_rules(config)
    label = f"{config.stem}" + ("" if mode == "full" else f"#{mode}")
    if args.enumerate_only:
        stats = tree_stats(enumerate_tree(rules, R.Conventions(), mode))
        print(f"SD_REFEREE_TREE {label} " + " ".join(f"{k}={v}" for k, v in stats.items()))
        return {"config": str(config), "mode": mode, "referee_tree": stats, "status": "PASS"}
    if args.dump is not None:
        dump = args.dump
    else:
        suffix = "" if mode == "full" else f"_{mode}"
        dump = args.scratch_dir / f"{index:02d}_{config.stem}{suffix}.jsonl"
        run_engine(args.executable, config, mode, dump)
    header, records = load_dump(dump)
    base = referee_check(rules, R.Conventions(), mode, header, records)
    failures, mismatches, partial = classify_findings(rules, mode, header, records, base,
                                                      not args.no_classify)
    recorded = args.expected_trees.get((config.stem, mode))
    if recorded is not None and header.get("tree_fingerprint") != recorded:
        failures.append({"category": "dump", "check": "recorded_tree_fingerprint", "where": "header",
                         "path": None, "detail": f"dump {header.get('tree_fingerprint')}, recorded {recorded}: "
                                                 "the built benchmark compiles another tree than the runs"})
    rule = [f for f in failures if f["category"] in ("rule", "dump")]
    abstraction = [f for f in failures if f["category"] == "abstraction"]
    labels = [f for f in failures if f["category"] == "convention"]
    status = "FAIL" if rule or abstraction else ("CONVENTION" if mismatches or labels else "PASS")
    counts = dict(base.counts)
    print(f"SD_REFEREE {label} nodes={len(records)} referee_paths={counts.get('paths_referee', 0)} "
          f"decisions={counts.get('decisions', 0)} edges={counts.get('edges', 0)} "
          f"payoff_rows={counts.get('payoff_rows', 0)} rule_failures={len(rule)} "
          f"abstraction_failures={len(abstraction)} convention_mismatches={len(mismatches) + len(labels)} "
          f"-> {status}")
    coverage = {}
    try:
        coverage = coverage_stats(rules, R.Conventions(), enumerate_tree(rules, R.Conventions(), mode))
        print(f"SD_REFEREE_COVERAGE {label} " + " ".join(f"{k}={v}" for k, v in coverage.items()))
    except (R.ConfigError, R.UnsupportedSituation):
        pass  # already reported as an enumeration error
    for item in (rule + abstraction + labels)[:args.examples]:
        print(f"  {item['category']}:{item['check']} at {json_key(item['where'])} "
              f"[{item['path'] or 'root'}]: {item['detail']}")
    for entry in mismatches:
        print(f"  convention {entry['id']} {entry['convention']}: the engine behaves as "
              f"{entry['engine_behaves_as']!r} ({entry['explains']} failures explained)")
    return {
        "config": str(config), "config_id": rules.config_id, "mode": mode, "dump": str(dump),
        "tree_fingerprint": header.get("tree_fingerprint"), "recorded_tree_fingerprint": recorded,
        "config_fingerprint": header.get("config_fingerprint"), "compared": counts, "coverage": coverage,
        "rule_failures": summarize(rule, args.examples),
        "abstraction_failures": summarize(abstraction, args.examples),
        "label_and_order_conventions": summarize(labels, args.examples),
        "convention_mismatches": mismatches, "partial_convention_matches": partial,
        "status": status,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("cases", nargs="+", help="config JSON, optionally suffixed #preflop_only or #checkdown")
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--executable", type=Path, help="gtosd_preflop_blueprint_game (writes the dumps)")
    source.add_argument("--dump", type=Path, help="an existing --dump-nodes file (one config only)")
    source.add_argument("--enumerate-only", action="store_true",
                        help="no engine: print the referee's own tree statistics")
    source.add_argument("--self-test", action="store_true",
                        help="no engine: the referee against dumps of its own tree (clean, tampered, and "
                             "written under every alternative convention)")
    parser.add_argument("--scratch-dir", type=Path, default=Path("out/monker/correctness/independent/S3_referee"),
                        help="where --executable writes the dumps")
    parser.add_argument("--report", type=Path, help="write the JSON report here")
    parser.add_argument("--examples", type=int, default=12, help="failures listed per category")
    parser.add_argument("--no-classify", action="store_true", help="skip the convention re-runs")
    parser.add_argument("--expect-tree", action="append", default=[], metavar="STEM[#MODE]=FINGERPRINT",
                        help="recorded tree fingerprint the dump of that config must carry (adds to the "
                             "built-in README list)")
    args = parser.parse_args()
    args.expected_trees = dict(RECORDED_TREE_FINGERPRINTS)
    for item in args.expect_tree:
        case, _, fingerprint = item.partition("=")
        stem, _, mode = case.partition("#")
        if not stem or not fingerprint or (mode or "full") not in MODES:
            parser.error(f"--expect-tree {item!r}: expected STEM[#MODE]=FINGERPRINT")
        args.expected_trees[(stem, mode or "full")] = fingerprint
    cases = [parse_case(text) for text in args.cases]
    if args.dump is not None and len(cases) != 1:
        parser.error("--dump takes exactly one config")

    if args.self_test:
        present = [(config, mode) for config, mode in cases if config.is_file()]
        if not present:
            print("SD_REFEREE_SELFTEST=SKIP no config found")
            return 77
        passed = all([self_test(config, mode) for config, mode in present])
        print(f"SD_REFEREE_SELFTEST={'PASS' if passed else 'FAIL'}")
        return 0 if passed else 1

    results, skipped, errors = [], [], []
    for index, (config, mode) in enumerate(cases):
        if not config.is_file():
            print(f"SD_REFEREE {config} SKIP (config not found)")
            skipped.append(str(config))
            continue
        try:
            results.append(referee_case(args, config, mode, index))
        except (DumpError, R.ConfigError, OSError, json.JSONDecodeError) as error:
            print(f"SD_REFEREE {config} ERROR {error}")
            errors.append({"config": str(config), "mode": mode, "error": str(error)})
    report = {"schema": "gtosd.independent.sd_referee.v1", "conventions_in_force": conventions_in_force(),
              "results": results, "errors": errors, "skipped": skipped}
    if args.report is not None:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if not results and not errors:
        print("SD_REFEREE=SKIP no config found")
        return 77
    if errors or any(r["status"] == "FAIL" for r in results):
        print("SD_REFEREE=FAIL")
        return 1
    if any(r["status"] == "CONVENTION" for r in results):
        print("SD_REFEREE=CONVENTION (only named conventions differ; see the report)")
        return 3
    print("SD_REFEREE=PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
