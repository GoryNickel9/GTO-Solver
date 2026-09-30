"""No-limit betting rules, pots, uncalled returns, settlement and rake of the preflop blueprint games.

Independent reference of check S3 (coverage study shared_components.md, 30 September 2026). The game
logic is written from the rules stated below and from the configuration semantics documented in
schemas/preflop_blueprint_game.schema.json and include/gtosd/preflop_blueprint/game_config.hpp. It is not a
translation of libs/core/src/game.cpp or libs/preflop_blueprint/src/game_model.cpp: the engine was read only
for choices that are not rules (conventions C1-C16 below) and for the dump format. Every convention is a
field of `Conventions` with at least one alternative, so the referee can tell a disagreement explained by a
convention from a rule failure.

Money is in integer units, 10,000 per ante (1e-4 ante). "Seat" p = index in the config's positions list.

Rules (R)
- R1  Seats 0..N-1 in acting order; the last seat is the BTN. Every player starts with the effective stack.
      Every player posts the ante, which is dead: it is in the pot but never counts towards a bet. The BTN
      posts the button blind, which is live: it is the BTN's bet on the preflop street.
- R2  The pot is all the money put in (antes, blind, bets and calls) minus the money returned to its owner.
- R3  The bet to match on a street is the largest street commitment; to_call = bet to match - own street
      commitment. Facing to_call > 0 a player may fold, call or raise; with to_call = 0, check or bet.
- R4  A call puts in min(to_call, stack); a call of the whole stack is an all-in call. A player whose stack
      does not exceed to_call cannot raise.
- R5  A bet is at least the minimum bet unless it is all-in. A raise adds at least the minimum raise on top of
      the bet to match unless it is all-in. The minimum raise is the last full bet or raise increment of the
      street; on the preflop street the blind is the opening bet (minimum raise = the blind), on a later
      street it starts at the minimum bet. An all-in that raises by less is an incomplete raise: it does not
      change the minimum raise and does not reopen the action for players who had already acted (they may
      only fold or call). Nobody puts in more than their stack.
- R6  The action moves to the next seat in cyclic seat order that is still in the hand and has chips.
- R7  A bet or a full raise reopens the action: every other player in the hand with chips must act again.
      The betting round ends when nobody in the hand with chips still has to act (everyone with chips has
      acted since the last bet or raise and matched it).
- R8  When all players but one have folded, the hand ends at once: the remaining player wins the pot, after
      the uncalled part of their street commitment (the excess over every other player's street commitment)
      has been returned to them.
- R9  Heads-up, a call for less than to_call (all-in) leaves the excess of the bet uncalled; it is returned to
      the bettor at once. (With more players this needs side pots, which these games never reach.)
- R10 When a betting round ends with two or more players in the hand: on the river the hand goes to showdown;
      before the river, when at most one of them has chips nobody can bet any more and the rest of the board
      is dealt (all-in runout, then showdown); otherwise the next street starts (flop, turn, river).
- R11 At a showdown the winners (the best hands: any non-empty subset of the players in the hand) share the pot
      after rake equally; the other players receive nothing.
- R12 A player's net result is what they receive minus what they put in net of returns (ante + live chips -
      returned chips).
- R13 Rake, when enabled: min(percentage x pot, cap), taken from the pot before it is shared, only when the pot
      reaches the minimum pot and, with no-flop-no-drop, only when the flop was dealt. The pot is the called
      pot: uncalled chips were returned before (R8, R9). The winners bear it.

Conventions (C): choices of the engine and of the configuration format, not rules. Name, default and
alternatives are in CONVENTIONS; the referee lists them all in its report.
- C1  Seat order on every street: seat 0 acts first and the BTN last (decision D10), preflop and postflop.
- C2  Rounding of every percentage (bet sizes, the postflop all-in cap, the rake): half up to the unit.
- C3  A pot-relative size s adds s x (pot + to_call) on top of the call ("pot after the call", dead antes and
      every street commitment included): a bet or raise puts in to_call + increment.
- C4  A size whose increment is below the minimum bet or minimum raise is dropped (never raised to the
      minimum); a size or target that reaches the stack becomes the all-in; aggressive actions of equal amount
      collapse into the first one (sizes or targets in their configured order, then the explicit all-in).
- C5  Preflop action abstraction by aggression level (bets, raises and all-in raises already made on the
      street): level 0 = the open targets (live street commitment to reach, open_target_units) or the pot
      sizes (preflop_open_sizes_basis_points); level 1 = the response target matched by index to the open
      target equal to the bet to match (limp_response_target_units in a limped pot when present), empty = no
      sized raise; level 2 and above = no sized raise. A non-all-in raise below the minimum raise is allowed
      only for the level-1 target and only with allow_configured_incomplete_raise; it reopens the action
      like a raise and leaves the minimum raise unchanged. The all-in is offered at every preflop decision
      with chips above the call (include_all_in).
- C6  Preflop, a player facing an all-in (the bet to match was set by a player now all-in) may only fold or
      call.
- C7  Preflop with nothing to call (the BTN after limps, who holds the live blind) the raise is a "bet" of at
      least the button blind. A "limped pot" is a preflop street on which a player called at level 0.
- C8  Postflop all-in: at every decision (include_all_in), or with postflop_all_in_max_pot_basis_points only
      when the chips above the call are at most cap x (pot + to_call), boundary included.
- C9  Donk rule (postflop_donk_bets false): while nobody has bet on a postflop street, a player may only check
      when the last aggressor of the previous street is in the hand, has chips and acts later.
- C10 A postflop street outside postflop_betting_streets is checked through (check is the only action).
- C11 Odd units of a split pot go one each to the winners in seat order from seat 0 (the first seat after the
      BTN).
- C12 The raked pot includes the dead antes.
- C13 "Flop dealt" for no-flop-no-drop: every terminal after the flop (postflop folds included), a called
      preflop all-in (its runout deals the flop) and a checkdown leaf; only a preflop fold is exempt.
- C14 Accounting: an uncalled amount is recorded as returned; committed_total stays gross (chips put in,
      returns not subtracted), so net live contribution = committed_total - returned.
- C15 Tree shape: the edges of a decision in the order fold or check, call, sizes or targets, all-in; a street
      transition is a chance node with one edge; a sized all-in keeps its requested size (label only).
- C16 Step 1 (checkdown mode) settles the end of the preflop betting as a showdown with five cards to come,
      raked as a dealt flop; preflop-only mode stops at the end of the preflop betting (chance node, no edge).
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field, replace
from pathlib import Path

UNITS_PER_ANTE = 10_000
PREFLOP, FLOP, TURN, RIVER = 0, 1, 2, 3
STREET_NAMES = ("preflop", "flop", "turn", "river")
BOARD_CARDS_DEALT = (0, 3, 4, 5)
SCHEMA_ID = "gtosd.preflop_blueprint_game.v1"


class ConfigError(ValueError):
    """The configuration JSON does not define a game."""


class UnsupportedSituation(RuntimeError):
    """A situation outside these rules (side pots with three or more players)."""


# ---------------------------------------------------------------------------------------------- config


@dataclass(frozen=True)
class RakeRule:
    enabled: bool = False
    basis_points: int = 0
    cap: int = 0
    no_flop_no_drop: bool = True
    minimum_pot: int = 0


@dataclass(frozen=True)
class GameRules:
    config_id: str
    players: int
    positions: tuple[str, ...]
    stack: int
    ante: int
    blind: int
    open_targets: tuple[int, ...]
    open_sizes: tuple[int, ...]
    response_targets: tuple[int, ...]
    limp_response_targets: tuple[int, ...] | None
    allow_incomplete_response: bool
    postflop_sizes: tuple[int, ...]
    postflop_minimum_bet: int
    include_all_in: bool
    donk_bets: bool
    all_in_max_pot: int | None
    betting_streets: frozenset[int]
    rake: RakeRule


def _require(document: dict, key: str):
    if key not in document:
        raise ConfigError(f"missing field {key}")
    return document[key]


def _int_list(document: dict, key: str, default=None) -> tuple[int, ...]:
    values = document.get(key, default) if default is not None else _require(document, key)
    if not isinstance(values, list) or not all(isinstance(v, int) and v > 0 for v in values):
        raise ConfigError(f"{key} must be a list of positive integers")
    if any(b <= a for a, b in zip(values, values[1:])):
        raise ConfigError(f"{key} must be strictly increasing")
    return tuple(values)


def rules_from_json(document: dict) -> GameRules:
    """GameRules of a gtosd.preflop_blueprint_game.v1 document (field names = file format)."""
    if _require(document, "schema") != SCHEMA_ID:
        raise ConfigError("unsupported schema")
    for key, value in (("ante_accounting", "dead_initial_pot_contribution"),
                       ("preflop_target_basis", "live_commitment_excluding_dead_ante"),
                       ("raise_termination", "natural_stack")):
        if _require(document, key) != value:
            raise ConfigError(f"{key} must be {value}")
    players = int(_require(document, "player_count"))
    positions = tuple(_require(document, "positions"))
    if not 2 <= players <= 6 or len(positions) != players or positions[-1] != "BTN":
        raise ConfigError("player_count / positions invalid (the last position must be BTN)")
    stack = int(_require(document, "effective_stack_units"))
    ante = int(_require(document, "ante_units"))
    blind = int(_require(document, "button_blind_units"))
    if ante <= 0 or blind <= 0 or stack < ante + blind:
        raise ConfigError("stack, ante and blind invalid")
    open_targets = _int_list(document, "open_target_units")
    open_sizes = _int_list(document, "preflop_open_sizes_basis_points", [])
    response = _int_list(document, "response_target_units")
    limp = document.get("limp_response_target_units")
    limp_targets = None if limp is None else _int_list(document, "limp_response_target_units")
    if bool(open_targets) == bool(open_sizes):
        raise ConfigError("exactly one of open_target_units and preflop_open_sizes_basis_points")
    if open_sizes and (response or limp_targets):
        raise ConfigError("pot mode has no response targets")
    for targets in (response, limp_targets or ()):
        if targets and len(targets) != len(open_targets):
            raise ConfigError("response targets must be index-matched to the open targets")
        if any(t <= o for t, o in zip(targets, open_targets)):
            raise ConfigError("a response target must exceed its open target")
    postflop_sizes = _int_list(document, "postflop_sizes_basis_points")
    streets = document.get("postflop_betting_streets", ["flop", "turn", "river"])
    if not streets or any(s not in STREET_NAMES[1:] for s in streets):
        raise ConfigError("postflop_betting_streets invalid")
    mode = _require(document, "rake_mode")
    if mode == "enabled":
        rake = RakeRule(True, int(_require(document, "rake_basis_points")),
                        int(_require(document, "rake_cap_units")),
                        bool(_require(document, "rake_no_flop_no_drop")),
                        int(_require(document, "rake_minimum_pot_units")))
        if rake.basis_points <= 0 or rake.cap <= 0:
            raise ConfigError("an enabled rake needs a positive percentage and cap")
    elif mode == "disabled":
        rake = RakeRule()
    else:
        raise ConfigError("rake_mode invalid")
    cap = document.get("postflop_all_in_max_pot_basis_points")
    return GameRules(
        config_id=str(_require(document, "id")), players=players, positions=positions, stack=stack,
        ante=ante, blind=blind, open_targets=open_targets, open_sizes=open_sizes,
        response_targets=response, limp_response_targets=limp_targets,
        allow_incomplete_response=bool(_require(document, "allow_configured_incomplete_raise")),
        postflop_sizes=postflop_sizes,
        postflop_minimum_bet=int(_require(document, "postflop_minimum_bet_units")),
        include_all_in=bool(_require(document, "include_all_in")),
        donk_bets=bool(document.get("postflop_donk_bets", True)),
        all_in_max_pot=None if cap is None else int(cap),
        betting_streets=frozenset(STREET_NAMES.index(s) for s in streets), rake=rake)


def load_rules(path: Path) -> GameRules:
    return rules_from_json(json.loads(Path(path).read_text(encoding="utf-8")))


# ----------------------------------------------------------------------------------------- conventions


@dataclass(frozen=True)
class Conventions:
    rounding: str = "half_up"                 # C2: half_up | floor | ceil
    size_base: str = "pot_after_call"         # C3: pot_after_call | pot_before_call
    below_minimum: str = "drop"               # C4: drop | raise_to_minimum
    preflop_facing_all_in: str = "fold_call"  # C6: fold_call | rules_only
    all_in_cap_boundary: str = "inclusive"    # C8: inclusive | exclusive
    donk_rule: str = "configured"             # C9: configured | never
    odd_chip: str = "lowest_seat"             # C11: lowest_seat | highest_seat
    rake_base: str = "pot_with_antes"         # C12: pot_with_antes | live_money_only
    rake_all_in_runout: bool = True           # C13: preflop all-in runouts raked
    rake_checkdown: bool = True               # C13 / C16: checkdown leaves raked
    rake_postflop_fold: bool = True           # C13: postflop folds raked


CONVENTIONS = {
    # name: (id, description, alternatives)
    "rounding": ("C2", "percentages (sizes, all-in cap, rake) rounded half up to the unit",
                 ("floor", "ceil")),
    "size_base": ("C3", "size x (pot + to_call) added on top of the call", ("pot_before_call",)),
    "below_minimum": ("C4", "a size below the minimum bet/raise is dropped; one reaching the stack is the "
                            "all-in; equal amounts collapse into the first", ("raise_to_minimum",)),
    "preflop_facing_all_in": ("C6", "preflop, facing an all-in only fold and call", ("rules_only",)),
    "all_in_cap_boundary": ("C8", "postflop all-in cap: chips above the call <= cap x (pot + to_call)",
                            ("exclusive",)),
    "donk_rule": ("C9", "no bet into the previous street's aggressor acting later (when configured)",
                  ("never",)),
    "odd_chip": ("C11", "odd units of a split to the winners in seat order from seat 0",
                 ("highest_seat",)),
    "rake_base": ("C12", "the raked pot includes the dead antes", ("live_money_only",)),
    "rake_all_in_runout": ("C13", "a called preflop all-in (runout) is raked", (False,)),
    "rake_checkdown": ("C13", "a checkdown leaf (step 1) is raked", (False,)),
    "rake_postflop_fold": ("C13", "a postflop fold is raked", (False,)),
}

# Conventions without a switch: they fix the shape of the game the referee enumerates.
FIXED_CONVENTIONS = {
    "C1": "seat 0 acts first on every street, the BTN last (decision D10)",
    "C5": "preflop abstraction by aggression level: open targets or pot sizes, level-1 index-matched "
          "response (limp list in a limped pot), level 2+ fold/call/all-in; all-in at every preflop decision",
    "C7": "preflop with nothing to call the raise is a bet of at least the button blind; limped pot = a call "
          "at level 0",
    "C10": "postflop streets outside postflop_betting_streets are checked through",
    "C14": "uncalled chips recorded as returned; committed_total gross",
    "C15": "edge order fold/check, call, sizes or targets, all-in; one-edge chance nodes; sized all-in keeps "
           "its requested basis points",
    "C16": "checkdown leaf = showdown with five cards to come; preflop-only stops at the flop chance node",
}


def alternative_conventions(base: Conventions = Conventions()):
    """(name, value, Conventions) for every single-convention alternative."""
    for name, (_, _, alternatives) in CONVENTIONS.items():
        for value in alternatives:
            yield name, value, replace(base, **{name: value})


def percent(amount: int, basis_points: int, rounding: str) -> int:
    """amount x basis_points / 10,000 rounded to the unit (C2)."""
    numerator = amount * basis_points
    if rounding == "half_up":
        return (numerator + 5_000) // 10_000
    if rounding == "floor":
        return numerator // 10_000
    if rounding == "ceil":
        return -((-numerator) // 10_000)
    raise ValueError(f"unknown rounding {rounding}")


# ----------------------------------------------------------------------------------------------- state


@dataclass(frozen=True)
class Action:
    kind: str                # fold, check, call, bet, raise, all_in
    amount: int = 0          # chips the actor puts in with this action
    all_in_kind: str = "none"  # "call": a call of the whole stack; "raise": an aggressive all-in
    size_bp: int = 0         # requested pot size of a sized action (label only, C15)

    @property
    def token(self) -> str:
        return f"{self.kind}:{self.amount}"

    @property
    def aggressive(self) -> bool:
        return self.kind in ("bet", "raise", "all_in")


@dataclass
class Hand:
    """Public state of a hand in progress. Status: in_progress, street_complete, folded, all_in_runout,
    showdown."""

    street: int
    stacks: list[int]
    antes: list[int]
    put_in: list[int]          # live chips put in over the hand (gross, C14)
    street_bets: list[int]     # live chips on the current street, net of returns
    returned: list[int]
    in_hand: list[bool]
    bet_to_match: int
    minimum_raise: int
    to_act: int | None
    must_act: set[int]
    no_reraise: set[int] = field(default_factory=set)
    level: int = 0             # aggressive actions on the street (C5)
    limped: bool = False       # C7
    aggressor: int | None = None
    previous_aggressor: int | None = None
    status: str = "in_progress"

    def clone(self) -> "Hand":
        return Hand(self.street, list(self.stacks), list(self.antes), list(self.put_in),
                    list(self.street_bets), list(self.returned), list(self.in_hand), self.bet_to_match,
                    self.minimum_raise, self.to_act, set(self.must_act), set(self.no_reraise), self.level,
                    self.limped, self.aggressor, self.previous_aggressor, self.status)

    @property
    def players(self) -> int:
        return len(self.stacks)

    @property
    def pot(self) -> int:
        return sum(self.antes) + sum(self.put_in) - sum(self.returned)

    def contribution(self, player: int) -> int:
        """Net money of `player` in the pot (R12)."""
        return self.antes[player] + self.put_in[player] - self.returned[player]

    def has_chips(self, player: int) -> bool:
        return self.in_hand[player] and self.stacks[player] > 0

    def to_call(self, player: int) -> int:
        return max(0, self.bet_to_match - self.street_bets[player])

    @property
    def in_hand_mask(self) -> int:
        return sum(1 << p for p in range(self.players) if self.in_hand[p])

    @property
    def all_in_mask(self) -> int:
        return sum(1 << p for p in range(self.players) if self.in_hand[p] and self.stacks[p] == 0)


def initial_hand(rules: GameRules) -> Hand:
    """R1: antes dead, BTN blind live, seat 0 to act (C1)."""
    n = rules.players
    button = n - 1
    live = [rules.blind if p == button else 0 for p in range(n)]
    stacks = [rules.stack - rules.ante - live[p] for p in range(n)]
    hand = Hand(street=PREFLOP, stacks=stacks, antes=[rules.ante] * n, put_in=list(live),
                street_bets=list(live), returned=[0] * n, in_hand=[True] * n, bet_to_match=rules.blind,
                minimum_raise=rules.blind, to_act=None, must_act={p for p in range(n) if stacks[p] > 0})
    hand.to_act = _first_to_act(hand, 0)
    return hand


def _first_to_act(hand: Hand, start: int) -> int | None:
    """R6 / C1: the first seat from `start` (inclusive, cyclic) in the hand with chips."""
    for offset in range(hand.players):
        seat = (start + offset) % hand.players
        if hand.has_chips(seat):
            return seat
    return None


def minimum_bet(rules: GameRules, street: int) -> int:
    return rules.blind if street == PREFLOP else rules.postflop_minimum_bet  # C7 / config


def facing_all_in(hand: Hand) -> bool:
    return hand.bet_to_match > 0 and any(
        hand.in_hand[p] and hand.stacks[p] == 0 and hand.street_bets[p] == hand.bet_to_match
        for p in range(hand.players))


# ---------------------------------------------------------------------------------- action abstraction


def _offered_aggression(rules: GameRules, conv: Conventions, hand: Hand, player: int):
    """What the configuration lets `player` do beyond fold/check/call (C5-C10): None when nothing (a
    check-only street, the donk rule, preflop facing an all-in), else (candidates, all-in offered,
    incomplete allowed) with candidates ("size", basis points of the pot) or ("target", live street
    commitment to reach)."""
    if hand.street == PREFLOP:
        if conv.preflop_facing_all_in == "fold_call" and facing_all_in(hand):
            return None  # C6
        if hand.level == 0:  # C5: the first raise
            if rules.open_sizes:
                return [("size", bp) for bp in rules.open_sizes], True, False
            return [("target", t) for t in rules.open_targets], True, False
        if hand.level == 1:  # C5: the response to the first raise
            use_limp = hand.limped and rules.limp_response_targets is not None
            targets = rules.limp_response_targets if use_limp else rules.response_targets
            if not targets:
                return [], True, False
            if hand.bet_to_match not in rules.open_targets:
                raise ConfigError("level-1 response to a bet that is no open target")
            index = rules.open_targets.index(hand.bet_to_match)
            return [("target", targets[index])], True, rules.allow_incomplete_response
        return [], True, False  # C5: level 2 and above
    if hand.street not in rules.betting_streets:
        return None  # C10
    if (not rules.donk_bets and conv.donk_rule == "configured" and hand.bet_to_match == 0
            and hand.previous_aggressor is not None and hand.has_chips(hand.previous_aggressor)
            and hand.previous_aggressor > player):
        return None  # C9
    return [("size", bp) for bp in rules.postflop_sizes], rules.include_all_in, False


def abstraction_actions(rules: GameRules, conv: Conventions, hand: Hand) -> list[Action]:
    """The actions the configuration offers at a decision, in edge order (C15)."""
    player = hand.to_act
    to_call = hand.to_call(player)
    stack = hand.stacks[player]
    actions: list[Action] = []
    if to_call == 0:
        actions.append(Action("check"))
    else:
        actions.append(Action("fold"))
        actions.append(Action("call", min(to_call, stack), "call" if stack <= to_call else "none"))
        if stack <= to_call:
            return actions  # R4
    if player in hand.no_reraise:
        return actions  # R5: an incomplete raise did not reopen the action
    offered = _offered_aggression(rules, conv, hand, player)
    if offered is None:
        return actions
    candidates, all_in_offered, incomplete_allowed = offered
    # R5: the minimum bet with nothing to call (C7 preflop), else the minimum raise.
    minimum = minimum_bet(rules, hand.street) if to_call == 0 else hand.minimum_raise
    pot_after_call = hand.pot + to_call
    base = pot_after_call if conv.size_base == "pot_after_call" else hand.pot  # C3

    def add(action: Action) -> None:
        if not any(a.aggressive and a.amount == action.amount for a in actions):  # C4
            actions.append(action)

    for kind, value in candidates:
        if kind == "size":
            increment = percent(base, value, conv.rounding)  # C2, C3
            if increment < minimum:
                if conv.below_minimum == "drop":
                    continue  # C4
                increment = minimum
            if increment <= 0:
                continue
            payment = to_call + increment
            size_bp = value
        else:
            if value <= hand.bet_to_match:
                continue
            increment = value - hand.bet_to_match
            if increment < minimum and not (to_call > 0 and incomplete_allowed):
                continue  # R5 (C5: the configured incomplete level-1 target is the exception)
            payment = value - hand.street_bets[player]
            size_bp = 0
        if payment >= stack:
            add(Action("all_in", stack, "raise", size_bp))  # C4
        else:
            add(Action("bet" if to_call == 0 else "raise", payment, "none", size_bp))
    if all_in_offered and rules.include_all_in:
        allowed = True
        if hand.street != PREFLOP and rules.all_in_max_pot is not None:  # C8
            threshold = percent(pot_after_call, rules.all_in_max_pot, conv.rounding)
            above_call = stack - to_call
            allowed = (above_call <= threshold if conv.all_in_cap_boundary == "inclusive"
                       else above_call < threshold)
        if allowed:
            add(Action("all_in", stack, "raise", 0))
    return actions


def rule_violation(rules: GameRules, hand: Hand, action: Action, incomplete_allowed: bool) -> str | None:
    """Why `action` breaks the betting rules R3-R5 at `hand` (None when it is legal). Independent of the
    abstraction: any legal bet size passes. `incomplete_allowed` is the configuration's level-1 exception."""
    player = hand.to_act
    to_call = hand.to_call(player)
    stack = hand.stacks[player]
    if action.amount < 0 or action.amount > stack:
        return f"{action.token}: amount outside [0, stack {stack}]"
    if action.kind == "fold":
        return None if to_call > 0 else "fold with nothing to call"
    if action.kind == "check":
        return None if to_call == 0 and action.amount == 0 else f"check facing {to_call}"
    if action.kind == "call":
        if to_call == 0:
            return "call with nothing to call"
        if action.amount != min(to_call, stack):
            return f"call of {action.amount}, expected min(to_call {to_call}, stack {stack})"
        expected_kind = "call" if stack <= to_call else "none"
        if action.all_in_kind != expected_kind:
            return f"call all_in_kind {action.all_in_kind}, expected {expected_kind}"
        return None
    if not action.aggressive:
        return f"unknown action {action.kind}"
    if player in hand.no_reraise:
        return "raise after an incomplete raise that did not reopen the action"
    if stack <= to_call:
        return "raise with a stack that does not exceed to_call"
    if action.amount <= to_call:
        return f"{action.token} does not raise (to_call {to_call})"
    is_all_in = action.amount == stack
    if action.kind == "all_in":
        if not is_all_in:
            return f"all-in of {action.amount} with stack {stack}"
        if action.all_in_kind != "raise":
            return f"all-in with all_in_kind {action.all_in_kind}"
        return None
    if is_all_in:
        return f"{action.kind} of the whole stack not marked all-in"
    if action.kind == "bet":
        if to_call != 0:
            return f"bet facing {to_call}"
        if action.amount < minimum_bet(rules, hand.street):
            return f"bet of {action.amount} below the minimum bet {minimum_bet(rules, hand.street)}"
        return None
    if to_call == 0:
        return "raise with nothing to call"
    if action.amount - to_call < hand.minimum_raise and not incomplete_allowed:
        return f"raise increment {action.amount - to_call} below the minimum raise {hand.minimum_raise}"
    return None


def incomplete_allowed_at(rules: GameRules, hand: Hand) -> bool:
    """C5: the level-1 target may be an incomplete non-all-in raise when the configuration says so."""
    return hand.street == PREFLOP and hand.level == 1 and rules.allow_incomplete_response


# ---------------------------------------------------------------------------------------- transitions


def _close_round(hand: Hand) -> None:
    """R7 / R10 once nobody with chips still has to act."""
    in_hand = [p for p in range(hand.players) if hand.in_hand[p]]
    with_chips = [p for p in in_hand if hand.stacks[p] > 0]
    hand.to_act = None
    if hand.street == RIVER:
        hand.status = "showdown"
    elif len(with_chips) <= 1:
        hand.status = "all_in_runout"
    else:
        hand.status = "street_complete"


def _return_uncalled(hand: Hand, player: int, amount: int) -> None:
    if amount > 0:
        hand.street_bets[player] -= amount
        hand.stacks[player] += amount
        hand.returned[player] += amount


def apply_action(hand: Hand, action: Action) -> Hand:
    """R4-R10: the state after `action` (legality is checked separately)."""
    nxt = hand.clone()
    player = hand.to_act
    if action.kind == "fold":
        nxt.in_hand[player] = False
        nxt.must_act.discard(player)
        nxt.no_reraise.discard(player)
        remaining = [p for p in range(nxt.players) if nxt.in_hand[p]]
        if len(remaining) == 1:  # R8
            winner = remaining[0]
            others = max(nxt.street_bets[p] for p in range(nxt.players) if p != winner)
            _return_uncalled(nxt, winner, nxt.street_bets[winner] - others)
            nxt.bet_to_match = nxt.street_bets[winner]
            nxt.to_act = None
            nxt.status = "folded"
            return nxt
    elif action.kind == "check":
        nxt.must_act.discard(player)
    else:
        to_call = hand.to_call(player)
        nxt.stacks[player] -= action.amount
        nxt.put_in[player] += action.amount
        nxt.street_bets[player] += action.amount
        if action.aggressive:
            new_bet = nxt.street_bets[player]
            increment = new_bet - hand.bet_to_match
            full = increment >= hand.minimum_raise  # R5 (a bet: minimum_raise = the minimum bet)
            others = {p for p in range(nxt.players) if p != player and nxt.has_chips(p)}
            if full:
                nxt.minimum_raise = increment
                nxt.no_reraise = set()
            elif action.kind == "all_in":  # R5: incomplete all-in, no reopening for who already acted
                nxt.no_reraise |= {p for p in others if p not in hand.must_act}
            else:  # C5: the configured incomplete level-1 target reopens like a raise
                nxt.no_reraise = set()
            nxt.bet_to_match = new_bet
            nxt.must_act = others
            nxt.level += 1
            nxt.aggressor = player
        else:  # call
            nxt.must_act.discard(player)
            if action.amount < to_call:  # R9
                if nxt.players != 2:
                    raise UnsupportedSituation("short all-in call with three or more players (side pot)")
                bettor = 1 - player
                _return_uncalled(nxt, bettor, nxt.street_bets[bettor] - nxt.street_bets[player])
                nxt.bet_to_match = max(nxt.street_bets)
            if hand.street == PREFLOP and hand.level == 0:
                nxt.limped = True  # C7
    nxt.no_reraise.discard(player)
    nxt.must_act = {p for p in nxt.must_act if nxt.has_chips(p)}
    if not nxt.must_act:
        _close_round(nxt)
        return nxt
    nxt.to_act = _first_to_act(nxt, (player + 1) % nxt.players)
    if nxt.to_act not in nxt.must_act:
        raise UnsupportedSituation("the next seat with chips does not have to act")
    return nxt


def next_street(rules: GameRules, hand: Hand) -> Hand:
    """R10 / C1: a new betting round; the last aggressor of the finished street becomes the previous one."""
    nxt = hand.clone()
    nxt.street = hand.street + 1
    nxt.street_bets = [0] * hand.players
    nxt.bet_to_match = 0
    nxt.minimum_raise = rules.postflop_minimum_bet  # R5
    nxt.level = 0
    nxt.limped = False
    nxt.previous_aggressor = hand.aggressor
    nxt.aggressor = None
    nxt.no_reraise = set()
    nxt.must_act = {p for p in range(hand.players) if hand.has_chips(p)}
    nxt.status = "in_progress"
    nxt.to_act = _first_to_act(nxt, 0)
    return nxt


# ----------------------------------------------------------------------------------------- settlement


def flop_dealt(conv: Conventions, hand: Hand, kind: str) -> bool:
    """C13 / C16. kind: fold, showdown, runout, checkdown."""
    if kind == "checkdown":
        return conv.rake_checkdown
    if hand.street == PREFLOP:
        return kind == "runout" and conv.rake_all_in_runout
    if kind == "fold":
        return conv.rake_postflop_fold
    return True


def rake_of(rules: GameRules, conv: Conventions, hand: Hand, kind: str) -> int:
    """R13 with C2, C12, C13."""
    rake = rules.rake
    pot = hand.pot
    if not rake.enabled or pot < rake.minimum_pot:
        return 0
    if rake.no_flop_no_drop and not flop_dealt(conv, hand, kind):
        return 0
    base = pot if conv.rake_base == "pot_with_antes" else pot - sum(hand.antes)
    return min(percent(base, rake.basis_points, conv.rounding), rake.cap)


def settle(rules: GameRules, conv: Conventions, hand: Hand, kind: str, winners: int) -> tuple[list[int], int]:
    """Net result of every seat (R8, R11, R12, C11) and the rake, for the winner mask `winners`."""
    rake = rake_of(rules, conv, hand, kind)
    seats = [p for p in range(hand.players) if winners >> p & 1]
    if not seats or any(not hand.in_hand[p] for p in seats):
        raise ValueError(f"winner mask {winners} is not a subset of the players in the hand")
    share, odd = divmod(hand.pot - rake, len(seats))
    order = seats if conv.odd_chip == "lowest_seat" else list(reversed(seats))
    received = [0] * hand.players
    for index, seat in enumerate(order):
        received[seat] = share + (1 if index < odd else 0)
    return [received[p] - hand.contribution(p) for p in range(hand.players)], rake
