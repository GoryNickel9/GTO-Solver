"""Independent oracle of the MonkerSolver preflop trees (no build needed).

1. Parses the MonkerSolver 3-way 50a chart files (one per preflop decision node).
2. Re-implements, in Python, the betting rules of gtosd core legal_actions /
   apply_action and of preflop_blueprint game_model / compiled_game (commit
   c7d6ba0), plus the pot-relative preflop raise (preflop_open_sizes_basis_points),
   and builds the preflop tree.
3. Names every preflop decision node the MonkerSolver way (first-action folds
   omitted) and compares node set and action sets with the files.
4. Counts the full tree (postflop rules of HU50 G1) and the trainer cells for
   the compact abstraction (15x4 buckets + TX2 turn classes), the numbers that
   tests/preflop_blueprint_game_tests.cpp (test_three_way_full_counts) checks in
   C++. It validates itself on every run against the HU50 trees whose counts
   were measured in C++ (493 / 571 / 8,599 nodes, G1 rows).

It is the cross-check of gtosd_preflop_blueprint_monker_tree (C1/C2), written
independently of the C++ code.

Usage: python monker_tree_oracle.py [--charts <3-way 50a folder>] [--json out.json]
"""
from __future__ import annotations

import argparse
import json
import pathlib
import sys
from dataclasses import dataclass, field, replace

CHARTS = pathlib.Path(
    r"C:/Users/GoryNickel/Documents/GitHub/GTO-Chart-Browser/ranges/Short Deck/Symmetrical Chart/3-way/50a")
U = 10_000  # money units per ante
NO = 0xFF

# ---------------------------------------------------------------- chart files


def combos(label: str) -> int:
    return 6 if len(label) == 2 else (4 if label.endswith("s") else 12)


def read_charts(root: pathlib.Path) -> dict[str, dict]:
    files = {}
    for path in sorted(root.rglob("*_strategy.txt")):
        rel = path.relative_to(root).as_posix()
        lines = [l for l in path.read_text(encoding="utf-8").splitlines() if l.strip()]
        header = lines[0].split("\t")
        assert header[0] == "Combination" and header[-1] == "Total", rel
        actions = header[1:-1]
        rows = {}
        for line in lines[1:]:
            parts = line.split("\t")
            values = [float(x) for x in parts[1:-1]]
            rows[parts[0]] = (values, float(parts[-1]))
        in_range = sum(combos(k) for k, (v, t) in rows.items() if t > 0.5)
        zero_cols = [a for i, a in enumerate(actions)
                     if all(v[i] == 0.0 for v, t in rows.values())]
        freq = {}
        for i, a in enumerate(actions):
            num = sum(combos(k) * v[i] for k, (v, t) in rows.items() if t > 0.5)
            freq[a] = round(num / in_range, 4) if in_range else None
        totals = sorted({round(t, 3) for v, t in rows.values()})
        files[rel] = dict(actions=actions, classes=len(rows), in_range_combos=in_range,
                          zero_columns=zero_cols, frequency=freq, totals=totals)
    return files


# ---------------------------------------------------------------- core rules


def percent_of(value: int, bp: int) -> int:
    whole, rem = divmod(value, 10_000)
    return whole * bp + (rem * bp + 5_000) // 10_000


@dataclass
class State:
    n: int
    street: int = 0  # 0 preflop .. 3 river
    status: str = "InProgress"
    pot: int = 0
    current_bet: int = 0
    last_full: int = 0
    committed: list = field(default_factory=list)
    total: list = field(default_factory=list)
    stacks: list = field(default_factory=list)
    active: int = 0
    allin: int = 0
    acted: int = 0
    raise_count: int = 0
    to_act: int = 0

    def copy(self) -> "State":
        return replace(self, committed=list(self.committed), total=list(self.total),
                       stacks=list(self.stacks))


def bit(p): return 1 << p
def is_active(s, p): return bool(s.active & bit(p))
def is_allin(s, p): return bool(s.allin & bit(p))


def next_actionable(s, actor):
    for off in range(1, s.n + 1):
        c = (actor + off) % s.n
        if is_active(s, c) and not is_allin(s, c):
            return c
    return actor


def commitments_equal(s):
    vals = {s.committed[p] for p in range(s.n) if is_active(s, p)}
    return len(vals) <= 1


def close_if_complete(s):
    can = s.active & ~s.allin
    if commitments_equal(s) and (can == 0 or (s.acted & can) == can):
        if s.street == 3:
            s.status = "Showdown"
        elif s.allin:
            s.status = "AllInRunout"
        else:
            s.status = "StreetComplete"


@dataclass
class Cfg:
    sizes: tuple = ()           # basis points of the pot after the call
    targets: tuple = ()         # live commitment targets (units)
    raise_depth: int = 0
    allin_mode: str = "Disabled"  # Disabled | Add
    threshold_bp: int = 0
    strict: bool = True
    unconditional: bool = False
    min_bet: int = U
    allow_incomplete: bool = False


def legal(s: State, c: Cfg):
    p = s.to_act
    to_call = max(0, s.current_bet - s.committed[p])
    stack = s.stacks[p]
    acts = []

    def add_unique(a):
        if not any(x[0] in ("Bet", "Raise", "AllIn") and x[1] == a[1] for x in acts):
            acts.append(a)

    if to_call == 0:
        acts.append(("Check", 0))
    else:
        acts.append(("Fold", 0))
        acts.append(("Call", min(to_call, stack)))
        if stack <= to_call:
            return acts
    pac = s.pot + to_call
    push = stack - min(stack, to_call)
    trig = False
    if c.threshold_bp > 0 and pac > 0:
        thr = percent_of(pac, c.threshold_bp)
        trig = push < thr if c.strict else push <= thr
    regular = push > 0 and (to_call == 0 or c.raise_depth > s.raise_count)
    if regular:
        if c.targets:
            for t in c.targets:
                if t <= s.current_bet:
                    continue
                req = t - s.current_bet
                pay = t - s.committed[p]
                if (to_call == 0 and req < c.min_bet) or (
                        to_call > 0 and req < s.last_full and not c.allow_incomplete):
                    continue
                add_unique(("AllIn", stack) if pay >= stack else
                           ("Bet" if to_call == 0 else "Raise", pay))
        else:
            for bp in c.sizes:
                inc = percent_of(pac, bp)
                if to_call == 0 and inc < c.min_bet:
                    continue
                if to_call > 0 and inc < s.last_full and not c.allow_incomplete:
                    continue
                tot = to_call + inc
                add_unique(("AllIn", stack) if tot >= stack else
                           ("Bet" if to_call == 0 else "Raise", tot))
    explicit = (trig and c.allin_mode == "Add") or (c.unconditional and c.allin_mode == "Add")
    if push > 0 and explicit:
        add_unique(("AllIn", stack))
    return acts


def apply(s: State, a) -> State:
    kind, amount = a
    p = s.to_act
    nx = s.copy()
    if kind == "Fold":
        nx.active &= ~bit(p)
        if bin(nx.active).count("1") >= 2:
            nx.to_act = next_actionable(nx, p)
            close_if_complete(nx)
        else:
            nx.status = "Folded"
        return nx
    opponent = next_actionable(s, p)
    nx.acted |= bit(p)
    if kind == "Check":
        nx.to_act = opponent
        close_if_complete(nx)
        return nx
    nx.stacks[p] -= amount
    nx.committed[p] += amount
    nx.total[p] += amount
    nx.pot += amount
    if nx.stacks[p] == 0:
        nx.allin |= bit(p)
    aggressive = kind in ("Bet", "Raise") or (kind == "AllIn")
    if aggressive:
        old = s.current_bet
        nx.current_bet = nx.committed[p]
        inc = nx.current_bet - old
        if inc >= s.last_full:
            nx.last_full = inc
        if kind == "Raise":
            nx.raise_count += 1
        nx.acted = bit(p)
    else:
        close_if_complete(nx)
    nx.to_act = next_actionable(nx, p)
    close_if_complete(nx)
    return nx


def advance(s: State) -> State:
    nx = s.copy()
    nx.street += 1
    nx.status = "InProgress"
    nx.current_bet = 0
    nx.last_full = 0
    nx.committed = [0] * s.n
    nx.acted = 0
    nx.raise_count = 0
    nx.to_act = 0
    if not is_active(nx, 0) or is_allin(nx, 0):
        nx.to_act = next_actionable(nx, 0)
    return nx


# ---------------------------------------------------------------- game model


@dataclass
class Game:
    n: int
    positions: tuple
    stack: int = 50 * U
    ante: int = U
    blind: int = U
    open_targets: tuple = ()
    open_sizes: tuple = ()  # proposed: preflop_open_sizes_basis_points
    postflop_sizes: tuple = (10_000,)
    postflop_min_bet: int = U
    donk: bool = True
    allin_cap_bp: int | None = None


def initial(g: Game) -> State:
    s = State(n=g.n)
    s.pot = g.ante * g.n + g.blind
    s.current_bet = g.blind
    s.last_full = g.blind
    s.active = (1 << g.n) - 1
    s.committed = [0] * g.n
    s.total = [0] * g.n
    s.stacks = [0] * g.n
    for p in range(g.n):
        live = g.blind if p == g.n - 1 else 0
        s.committed[p] = live
        s.total[p] = live
        s.stacks[p] = g.stack - g.ante - live
    return s


def facing_all_in(s):
    if s.current_bet == 0:
        return False
    return any(is_active(s, p) and is_allin(s, p) and s.committed[p] == s.current_bet
               for p in range(s.n))


def action_config(g: Game, s: State, level: int, prev_aggr: int) -> Cfg:
    allin = dict(allin_mode="Add", threshold_bp=100_000, unconditional=True)
    if s.street > 0:
        if (not g.donk and s.current_bet == 0 and prev_aggr != NO and is_active(s, prev_aggr)
                and not is_allin(s, prev_aggr) and prev_aggr > s.to_act):
            return Cfg(min_bet=g.postflop_min_bet)
        if g.allin_cap_bp is None:
            return Cfg(sizes=g.postflop_sizes, raise_depth=63, min_bet=g.postflop_min_bet, **allin)
        return Cfg(sizes=g.postflop_sizes, raise_depth=63, min_bet=g.postflop_min_bet,
                   allin_mode="Add", threshold_bp=g.allin_cap_bp, strict=False)
    if facing_all_in(s):
        return Cfg(min_bet=g.blind)
    if level == 0:
        if g.open_sizes:
            return Cfg(sizes=g.open_sizes, raise_depth=63, min_bet=g.blind, **allin)
        return Cfg(targets=g.open_targets, raise_depth=63, min_bet=g.blind, **allin)
    return Cfg(min_bet=g.blind, **allin)  # response lists empty: fold, call, all-in


# ---------------------------------------------------------------- tree walk


@dataclass
class Counts:
    nodes: int = 0
    decisions: list = field(default_factory=lambda: [0, 0, 0, 0])
    edges_by_street: list = field(default_factory=lambda: [0, 0, 0, 0])
    chance: int = 0
    folds: list = field(default_factory=lambda: [0, 0, 0, 0])
    showdowns: int = 0
    runouts: list = field(default_factory=lambda: [0, 0, 0, 0])
    entries: dict = field(default_factory=dict)  # (active count) -> preflop flop entries
    max_depth: int = 0


def token(s: State, child: State, a, p: int) -> str:
    kind = a[0]
    if kind in ("Fold", "Check", "Call"):
        return kind
    if kind == "AllIn":
        return "AllIn"
    return f"{child.committed[p] / U:.1f}ante"


def walk(g: Game, s: State, level=0, aggr=(NO, NO), depth=0, counts=None,
         prefix="", acted_before=0, charts=None, preflop_only=False):
    counts.nodes += 1
    counts.max_depth = max(counts.max_depth, depth)
    st = s.status
    if st == "Folded":
        counts.folds[s.street] += 1
        return
    if st == "Showdown":
        counts.showdowns += 1
        return
    if st == "AllInRunout":
        counts.runouts[s.street] += 1
        return
    if st == "StreetComplete":
        counts.chance += 1
        if s.street == 0:
            k = bin(s.active).count("1")
            counts.entries[k] = counts.entries.get(k, 0) + 1
            if preflop_only:
                return
        walk(g, advance(s), 0, (NO, aggr[0]), depth + 1, counts, prefix, acted_before,
             charts, preflop_only)
        return
    p = s.to_act
    cfg = action_config(g, s, level, aggr[1])
    acts = legal(s, cfg)
    counts.decisions[s.street] += 1
    counts.edges_by_street[s.street] += len(acts)
    kids = []
    for a in acts:
        child = apply(s, a)
        kids.append((a, child))
    if s.street == 0 and charts is not None:
        pos = g.positions[p]
        charts[f"{pos}/{prefix}{pos}_strategy.txt"] = dict(
            actions=[token(s, c, a, p) for a, c in kids], pot=s.pot / U,
            to_call=(s.current_bet - s.committed[p]) / U, level=level,
            live=[x / U for x in s.committed], active=[g.positions[q] for q in range(s.n)
                                                      if is_active(s, q)])
    for a, child in kids:
        aggressive = a[0] in ("Bet", "Raise", "AllIn")
        nlevel = level + (1 if aggressive else 0)
        naggr = (p, aggr[1]) if aggressive else aggr
        tok = token(s, child, a, p)
        first_action = not (acted_before & bit(p))
        # MonkerSolver file names omit a fold that is the player's first action.
        step = "" if (a[0] == "Fold" and first_action) else f"{g.positions[p]}_{tok}_"
        walk(g, child, nlevel, naggr, depth + 1, counts,
             prefix + step if s.street == 0 else prefix, acted_before | bit(p), charts,
             preflop_only)


def layout_cells(g: Game, caps, preflop_only=False):
    """Cells = sum over decision nodes of rows(street) * actions."""
    c = Counts()
    walk(g, initial(g), counts=c, preflop_only=preflop_only)
    rows = [81, *caps]
    cells = [c.edges_by_street[i] * rows[i] for i in range(4)]
    # lazy-discount timestamps: one uint16 per row of every decision node
    row_count = [c.decisions[i] * rows[i] for i in range(4)]
    return c, cells, row_count


def fmt_gb(x): return f"{x / 1e9:.2f} GB"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--charts", type=pathlib.Path, default=CHARTS,
                    help="MonkerSolver 3-way 50a chart folder (UTG/, CO/, BTN/)")
    ap.add_argument("--json", type=pathlib.Path)
    args = ap.parse_args()
    report: dict = {}

    # --- validation on the HU50 trees measured in C++
    hu = dict(n=2, positions=("CO", "BTN"), open_targets=(50_000,))
    compact = (573 * 60, 4_482 * 60, 4_482 * 15)
    for name, g, expect in [
        ("HU50_step2 (no donk)", Game(**hu, donk=False), "493 nodes (doc 5)"),
        ("HU50_step2_donk (G1)", Game(**hu, donk=True),
         "571 nodes; G1 rows_by_street 648/962,640/18,286,560/8,336,520 -> decisions 8/28/68/124"),
        ("HU50_step2_2size_donk (G4)", Game(**hu, donk=True, postflop_sizes=(5_000, 10_000)),
         "8,599 nodes, 3,220 postflop decisions"),
        ("HU50 pot mode (open 100 %)", Game(n=2, positions=("CO", "BTN"), open_sizes=(10_000,)),
         "same preflop as HU50 (open 5a, iso 5a)"),
    ]:
        c, cells, rows = layout_cells(g, compact)
        print(f"{name}: nodes {c.nodes}, decisions {c.decisions}, postflop decisions "
              f"{sum(c.decisions[1:])}, cells {sum(cells):,} -- expected {expect}")
        report.setdefault("validation", {})[name] = dict(
            nodes=c.nodes, decisions=c.decisions, cells=cells, expected=expect)

    # --- 3-way preflop vs chart files
    files = read_charts(args.charts)
    g3 = Game(n=3, positions=("UTG", "CO", "BTN"), open_sizes=(10_000,))
    charts: dict = {}
    c3 = Counts()
    walk(g3, initial(g3), counts=c3, charts=charts, preflop_only=True)
    ours, theirs = set(charts), set(files)
    print(f"\n3-way preflop: our decision nodes {len(ours)}, chart files {len(theirs)}")
    print("  missing in ours :", sorted(theirs - ours))
    print("  extra in ours   :", sorted(ours - theirs))
    mismatched = []
    for rel in sorted(ours & theirs):
        if sorted(charts[rel]["actions"]) != sorted(files[rel]["actions"]):
            mismatched.append((rel, charts[rel]["actions"], files[rel]["actions"]))
    print("  action mismatches:", mismatched)
    print(f"  preflop counts: nodes {c3.nodes}, decisions {c3.decisions[0]}, folds {c3.folds[0]},"
          f" all-in runouts {c3.runouts[0]}, flop entries by players {c3.entries}")
    # fixed targets (what the current engine can express) for comparison
    for targets in [(60_000,), (70_000,), (60_000, 70_000)]:
        gt = Game(n=3, positions=("UTG", "CO", "BTN"), open_targets=targets)
        ct: dict = {}
        walk(gt, initial(gt), counts=Counts(), charts=ct, preflop_only=True)
        bad = sum(1 for rel in set(ct) & theirs if sorted(ct[rel]["actions"]) !=
                  sorted(files[rel]["actions"]))
        print(f"  fixed open_target_units {targets}: nodes {len(ct)}, missing "
              f"{len(theirs - set(ct))}, extra {len(set(ct) - theirs)}, action mismatches {bad}")

    # --- 3-way full tree sizes (G1 postflop rules), with and without the 5x cap
    report["three_way"] = {}
    for label, cap in [("no all-in cap (reference)", None), ("all-in cap 5x pot", 50_000)]:
        g = Game(n=3, positions=("UTG", "CO", "BTN"), open_sizes=(10_000,), allin_cap_bp=cap)
        c, cells, rows = layout_cells(g, compact)
        total = sum(cells)
        ts = sum(rows) * 2
        print(f"\n3-way 50a, {label}: nodes {c.nodes:,}, decisions by street {c.decisions}, "
              f"edges {c.edges_by_street}, flop entries {c.entries}, max depth {c.max_depth}")
        print(f"  compact cells by street {[f'{x:,}' for x in cells]} = {total:,}; "
              f"double R+S {fmt_gb(16 * total)}, float32 R+S {fmt_gb(8 * total)}, "
              f"lazy timestamps {fmt_gb(ts)}")
        report["three_way"][label] = dict(
            nodes=c.nodes, decisions=c.decisions, edges=c.edges_by_street,
            entries=c.entries, folds=c.folds, runouts=c.runouts, showdowns=c.showdowns,
            chance=c.chance, cells=cells, cells_total=total,
            double_bytes=16 * total, float32_bytes=8 * total, timestamp_bytes=ts)
    report["preflop_nodes"] = {rel: dict(ours=charts.get(rel), file=files.get(rel))
                               for rel in sorted(ours | theirs)}
    if args.json:
        args.json.write_text(json.dumps(report, indent=1), encoding="utf-8")
    return 0 if not (ours ^ theirs) and not mismatched else 1


if __name__ == "__main__":
    sys.exit(main())
