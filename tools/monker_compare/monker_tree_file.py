#!/usr/bin/env python3
# Decodes MonkerSolver .tree files (saved game trees: a header with players, blinds and stacks, then the full betting tree).
# Verified on two 6-max NLHE/PLO trees (the user's 100bb6maxsmall.tree and 100bb6maxmedium.tree, 30 September 2026).
# Headers with antes or a button blind are untested (their fields were all zero in both samples); copied from the session scratchpad.
"""monker_tree.py - parser for MonkerSolver .tree files (format reverse-engineered, 2026-09-30).

Usage:
    python monker_tree.py FILE.tree                 # full report to stdout
    python monker_tree.py --diff A.tree B.tree      # markdown diff of two trees to stdout

The input file is opened read-only and never modified.

FORMAT (derived from 100bb6maxsmall.tree and 100bb6maxmedium.tree)
------------------------------------------------------------------
Layer 1 - text encoding (VERIFIED):
  the whole file is one standard UTF-8 text (not Java *modified* UTF-8: the value 0 is the
  raw byte 00, not c0 80). Every code point is one integer value 0..65535. Decoding strictly
  and re-encoding reproduces the file byte for byte. No BOM, no separators, no length fields.

Layer 2 - header, 3*N+5 values for N players (layout of the zero fields INFERRED):
  v[0]            33484  constant in both files (magic / format version?)          UNKNOWN
  v[1]            N      number of players (6)
  v[2..4]         3 values, all 0 in both files                                      UNKNOWN
  v[5..5+2N-1]    N pairs (u_i, blind_i): blind_i in mchip (1000 = 1 chip);
                  seat 0 posts 1000 (SB), seat 1 posts 2000 (BB); u_i all 0        u_i UNKNOWN
                  (u_i may be a per-seat ante, or the high 16 bits of a 32-bit blind)
  v[5+2N..5+3N-1] N stacks in chips (200 chips = 100 bb when BB = 2 chips)

Layer 3 - game tree, pre-order (grammar VERIFIED: consumes every remaining value exactly):
  node   := k  action_1 node_1  action_2 node_2 ... action_k node_k
  k      := number of children (0 = terminal)
  action := 0 fold | 1 check/call | 3 all-in | 40100 bet/raise "pot" (100 % pot, pot-limit rule)
  The tree is the COMPLETE game tree: preflop AND flop/turn/river betting. No chance nodes are
  stored (board cards are implicit: a street ends when the betting round closes).
  Action 40100 = pot-size bet/raise is INFERRED from game logic (see verify section): with
  that size, every terminal/non-terminal node agrees with poker rules and the pot raise is
  offered exactly when it is smaller than the stack (all-in replaces it otherwise). Other
  fractions of pot (0.5 .. 1.5) break this rule. The decimal look of 40100 ("4" + "0100" =
  100 %) is a guess; codes 4xxxx never seen so far are simulated as xxxx % pot and labelled GUESS.
  Child order inside a node: pot, fold, call, all-in (all nodes of both samples except the 3
  BTN flat nodes of the medium tree: call, pot, fold).
  Not stored anywhere in the samples: game type (NLHE/PLO - the betting is exactly pot-limit),
  rake, board/deck, abstraction. Ante/straddle: unknown fields are all zero, so untested.
  Alternative header reading: (u_i, blind_i) may be one 32-bit int written as two 16-bit chars
  (high, low); both readings give the same numbers for the samples.

Seat names are derived from the blinds (seat after the biggest blind = first to act); postflop
the seat after the button acts first (HU: BB). N-player generality (HU, 3-way) is untested on
real files: only synthetic self-test trees (selftest/) were parsed with N = 2, 3.
"""
import sys
import argparse
from collections import Counter, defaultdict, OrderedDict

sys.setrecursionlimit(20000)

FOLD, CALL, ALLIN, POT = 0, 1, 3, 40100
KNOWN_ACTIONS = {FOLD: 'fold', CALL: 'call', ALLIN: 'allin', POT: 'pot'}
STREETS = ['preflop', 'flop', 'turn', 'river']


# ----------------------------------------------------------------------------- layer 1
def read_values(path):
    with open(path, 'rb') as fh:            # read-only
        raw = fh.read()
    text = raw.decode('utf-8', errors='strict')
    if text.encode('utf-8') != raw:
        raise ValueError('UTF-8 round trip differs')
    vals = [ord(c) for c in text]
    width = Counter(len(c.encode('utf-8')) for c in text)
    return raw, vals, width


# ----------------------------------------------------------------------------- layer 2
def parse_header(v):
    n = v[1]
    h = OrderedDict()
    h['magic_or_version'] = v[0]
    h['players'] = n
    h['unknown_3'] = v[2:5]
    pairs = [(v[5 + 2 * i], v[6 + 2 * i]) for i in range(n)]
    h['seat_u'] = [a for a, _ in pairs]
    h['blinds_mchip'] = [b for _, b in pairs]
    h['stacks_chips'] = v[5 + 2 * n: 5 + 3 * n]
    h['tree_start'] = 5 + 3 * n
    return h


# ----------------------------------------------------------------------------- layer 3
class Node:
    __slots__ = ('kids', 'offset')

    def __init__(self, offset):
        self.offset = offset     # index of the child-count value in the value stream
        self.kids = []           # list of (action, Node)


def parse_tree(v, start):
    """Iterative pre-order parser; returns (root, end_index)."""
    root = Node(start)
    i = start
    stack = []                   # [node, remaining_children]
    k = v[i]; i += 1
    stack.append([root, k])
    while stack:
        top = stack[-1]
        if top[1] == 0:
            stack.pop(); continue
        top[1] -= 1
        a = v[i]; i += 1
        child = Node(i)
        k = v[i]; i += 1
        top[0].kids.append((a, child))
        stack.append([child, k])
    return root, i


# ----------------------------------------------------------------------------- game model
def position_names(n):
    if n == 2:
        return ['SB/BTN', 'BB']
    tail = ['CO', 'BTN'] if n >= 4 else ['BTN']
    mid = []
    if n >= 5:
        mid = ['HJ']
    if n >= 6:
        front = ['UTG'] + ['UTG+%d' % k for k in range(1, n - 5)]
        mid = front + mid
    return ['SB', 'BB'] + mid + tail


def seat_names(hdr):
    """Name seats from the blinds: the seat after the biggest blind acts first preflop.
    In the samples seat 0 = SB, seat 1 = BB, seats 2..5 = UTG, HJ, CO, BTN (INFERRED)."""
    n = hdr['players']
    bl = hdr['blinds_mchip']
    bbseat = max(range(n), key=lambda q: (bl[q], q))
    generic = position_names(n)
    acting = generic[2:] + generic[:2]          # UTG .. BTN, SB, BB
    names = [None] * n
    for k in range(n):
        names[(bbseat + 1 + k) % n] = acting[k]
    return names


class State:
    """Chips in mchip. Seat order = acting order (INFERRED). Preflop the seat after the biggest
    blind acts first; postflop the seat after the button acts first (HU: the BB).
    hdr['u_as_ante'] = True treats the unknown per-seat field u_i as a dead ante (UNVERIFIED option)."""

    def __init__(self, hdr):
        n = self.n = hdr['players']
        self.bb = max(hdr['blinds_mchip'])
        self.stack = [s * 1000 for s in hdr['stacks_chips']]      # behind
        self.street = [0] * n
        self.total = [0] * n
        self.folded = [False] * n
        self.allin = [False] * n
        self.acted = [False] * n
        self.st = 0
        self.raises = 0                    # bets/raises on this street (blinds excluded)
        self.aggr = [None] * 4             # last aggressor per street
        if hdr.get('u_as_ante'):
            for p, a in enumerate(hdr['seat_u']):
                a = min(a, self.stack[p])
                self.stack[p] -= a; self.total[p] += a
        for p, b in enumerate(hdr['blinds_mchip']):
            b = min(b, self.stack[p])
            self.stack[p] -= b; self.street[p] += b; self.total[p] += b
            if self.stack[p] == 0:
                self.allin[p] = True
        names = seat_names(hdr)
        bbseat = names.index('BB')
        self.last = bbseat                 # preflop: first to act is the seat after BB
        self.post_last = names.index('SB/BTN' if n == 2 else 'BTN')
        self.path = []                     # (street, seat, token)

    def copy(self):
        t = State.__new__(State)
        for k in ('n', 'bb', 'st', 'raises', 'last', 'post_last'):
            setattr(t, k, getattr(self, k))
        for k in ('stack', 'street', 'total', 'folded', 'allin', 'acted', 'aggr', 'path'):
            setattr(t, k, list(getattr(self, k)))
        return t

    pot = property(lambda s: sum(s.total))
    maxbet = property(lambda s: max(s.street))

    def live(self):
        return [p for p in range(self.n) if not self.folded[p]]

    def canact(self):
        return [p for p in range(self.n) if not self.folded[p] and not self.allin[p]]

    def needs(self, p):
        return (not self.folded[p] and not self.allin[p]
                and (not self.acted[p] or self.street[p] < self.maxbet))

    def next_actor(self):
        for k in range(1, self.n + 1):
            p = (self.last + k) % self.n
            if self.needs(p):
                return p
        return None

    def advance(self):
        """Close finished betting rounds. Returns (actor, None) or (None, terminal_reason)."""
        while True:
            if len(self.live()) == 1:
                return None, 'fold'
            p = self.next_actor()
            if p is not None:
                if len(self.canact()) == 1 and self.street[p] >= self.maxbet:
                    return None, 'allin'
                return p, None
            if len(self.canact()) <= 1:
                return None, 'allin'
            if self.st == 3:
                return None, 'showdown'
            self.st += 1
            self.street = [0] * self.n
            self.acted = [False] * self.n
            self.raises = 0
            # postflop: the seat after the button acts first (HU: BB, the SB is the button)
            self.last = self.post_last

    def pot_raise_to(self, p, pct=100):
        mb = self.maxbet
        return mb + (self.pot + (mb - self.street[p])) * pct // 100

    def apply(self, p, a):
        s = self.copy()
        mb = s.maxbet
        if a == FOLD:
            s.folded[p] = True; tok = 'f'
        elif a == CALL:
            amt = min(mb - s.street[p], s.stack[p])
            s.stack[p] -= amt; s.street[p] += amt; s.total[p] += amt
            if s.stack[p] == 0:
                s.allin[p] = True
            tok = ('c%s' % s.fmt(s.street[p])) if amt > 0 else 'x'
        elif a == ALLIN or is_pct_code(a):
            to = s.street[p] + s.stack[p] if a == ALLIN else s.pot_raise_to(p, a - 40000)
            amt = to - s.street[p]
            if a != ALLIN and amt >= s.stack[p]:
                raise ValueError('sized action would not fit the stack')
            s.stack[p] -= amt; s.street[p] += amt; s.total[p] += amt
            if s.stack[p] == 0:
                s.allin[p] = True
            if s.street[p] > mb:
                s.acted = [False] * s.n
                s.raises += 1
                s.aggr[s.st] = p
            tok = ('A%s' % s.fmt(s.street[p])) if a == ALLIN else ('r%s' % s.fmt(s.street[p]))
            if a != ALLIN and mb == 0:
                tok = 'b%s' % s.fmt(s.street[p])
            if a != ALLIN and a != POT:
                tok += '(%d%%?)' % (a - 40000)
        else:
            raise ValueError('unknown action code %d' % a)
        s.acted[p] = True
        s.last = p
        s.path.append((self.st, p, tok))
        return s

    def fmt(self, mchip):
        x = mchip / self.bb
        return ('%.2f' % x).rstrip('0').rstrip('.')


def describe_action(s, p, a):
    mb = s.maxbet
    if a == FOLD:
        return 'fold'
    if a == CALL:
        return 'check' if mb == s.street[p] else 'call %s' % s.fmt(min(mb, s.street[p] + s.stack[p]))
    if a == ALLIN:
        return 'allin %s' % s.fmt(s.street[p] + s.stack[p])
    if a == POT:
        return ('bet' if mb == 0 else 'raise') + ' %s(pot)' % s.fmt(s.pot_raise_to(p))
    if is_pct_code(a):
        return ('bet' if mb == 0 else 'raise') + ' %s(%d%% pot? GUESS from code %d)' % (
            s.fmt(s.pot_raise_to(p, a - 40000)), a - 40000, a)
    return 'UNKNOWN-CODE-%d' % a


def is_pct_code(a):
    """GUESS for codes never seen in the samples: 4xxxx = bet/raise xxxx % of pot (40100 = pot)."""
    return 40000 < a < 50000


def tagset(codes):
    return ''.join({FOLD: 'f', CALL: 'c', ALLIN: 'A', POT: 'P'}.get(a, '?') for a in codes)


SHORT_CALL = 0.2   # INFERRED: somewhere in (0.117, 0.321] - only one geometry observed


def predicted_actions(s, p):
    """Rule model INFERRED from the two sample files (checked node by node in the report).
    R1 bet/raise size is always pot; if the pot bet/raise does not fit the stack, all-in replaces it
       (never both).
    R2 no donk bets: a player first to bet on a street who is out of position to the previous
       street's aggressor (still to act) may only check - unless a pot bet would be all-in, then
       all-in is offered.
    R3 facing a bet: fold always; raising always allowed (R1) unless facing an all-in.
    R4 calling only when it closes the action (nobody else still to act) or when facing an all-in
       (no limps, no cold/flat calls with players behind, no multiway calls postflop).
    R5 before the river a call that would leave less than SHORT_CALL x pot behind is not offered
       (fold or all-in only)."""
    mb = s.maxbet
    call = mb - s.street[p]
    fits = s.pot_raise_to(p) - s.street[p] < s.stack[p]
    out = set()
    if call == 0:
        out.add(CALL)
        donk = False
        if s.st > 0:
            prev = s.aggr[s.st - 1]
            donk = (prev is not None and prev != p and not s.folded[prev] and not s.allin[prev]
                    and not s.acted[prev])
        if donk:
            if not fits:
                out.add(ALLIN)
        else:
            out.add(POT if fits else ALLIN)
        return frozenset(out)
    out.add(FOLD)
    facing_allin = any(s.allin[q] and s.street[q] == mb for q in range(s.n))
    behind = sum(1 for q in range(s.n) if q != p and s.needs(q))
    if facing_allin:
        out.add(CALL)
        return frozenset(out)
    out.add(POT if fits else ALLIN)
    left = s.stack[p] - call
    if behind == 0 and not (s.st < 3 and left < SHORT_CALL * (s.pot + call)):
        out.add(CALL)
    return frozenset(out)


# ----------------------------------------------------------------------------- analysis
class Report:
    def __init__(self, path, u_as_ante=False):
        self.path = path
        self.raw, self.v, self.width = read_values(path)
        self.hdr = parse_header(self.v)
        self.hdr['u_as_ante'] = u_as_ante
        try:
            self.root, self.end = parse_tree(self.v, self.hdr['tree_start'])
        except IndexError:
            raise SystemExit('tree grammar ran past the end of %s: format differs from the samples' % path)
        if self.end != len(self.v):
            sys.stderr.write('WARNING: tree grammar stopped at value %d of %d\n' % (self.end, len(self.v)))
        self.pos = seat_names(self.hdr)
        self.checks = OrderedDict()
        self.problems = Counter()
        self.pre_nodes = []          # (depth, state, actor, [(a, desc)])
        self.flop_entries = []       # (state, postflop node count, postflop decision count)
        self.pre_terminals = Counter()
        self.nodes_by_street = Counter()
        self.term_by_reason = Counter()
        self.actions_by_code = Counter()
        self.first_bet = Counter()   # postflop no-bet node rule table
        self.facing = Counter()      # postflop facing-bet rule table
        self.post_sizes = defaultdict(set)
        self.pre_sets = {}           # preflop path -> action string (for diff)
        self.total_nodes = 0
        self.x_and_allin = 0
        self.pot_ratio_offered = []
        self.pot_ratio_replaced = []
        self.model_miss = []
        self.post_per_pos = Counter()
        self.model_hit = 0
        self._walk()

    # path helpers
    def ptxt(self, s, only_street=None):
        out = []
        for st, p, tok in s.path:
            if only_street is not None and st != only_street:
                continue
            out.append('%s %s' % (self.pos[p], tok))
        return ', '.join(out) if out else '(start)'

    def _walk(self):
        s0 = State(self.hdr)
        todo = [(self.root, s0, 0)]
        while todo:
            node, s, depth = todo.pop()
            self.total_nodes += 1
            last_st = s.path[-1][0] if s.path else 0
            p, why = s.advance()
            if p is not None and s.st == 1 and last_st == 0:
                self.flop_entries.append((s, node))
            if p is None:
                self.term_by_reason[why] += 1
                if node.kids:
                    self.problems['children at a terminal state (%s)' % why] += 1
                if s.st == 0:
                    self.pre_terminals[why] += 1
                continue
            if not node.kids:
                self.problems['no children at a decision state (%s)' % STREETS[s.st]] += 1
                continue
            self.nodes_by_street[s.st] += 1
            codes = [a for a, _ in node.kids]
            for a in codes:
                self.actions_by_code[a] += 1
                if a not in KNOWN_ACTIONS:
                    self.problems['action code %d not seen in the 2026-09-30 samples' % a] += 1
            mb = s.maxbet
            call = mb - s.street[p]
            facing_allin = call > 0 and any(s.allin[q] and s.street[q] == mb for q in range(s.n))
            pr = s.pot_raise_to(p)
            fits = pr - s.street[p] < s.stack[p]
            if POT in codes and ALLIN in codes:
                self.x_and_allin += 1
            if POT in codes:
                self.pot_ratio_offered.append((pr) / (s.street[p] + s.stack[p]))
            elif ALLIN in codes:
                self.pot_ratio_replaced.append((pr) / (s.street[p] + s.stack[p]))
            if FOLD in codes and call == 0:
                self.problems['fold offered without a bet to face'] += 1
            if POT in codes and not fits:
                self.problems['pot action larger than stack'] += 1
            pred = predicted_actions(s, p)
            if pred != frozenset(codes):
                self.model_miss.append((STREETS[s.st], self.pos[p], self.ptxt(s),
                                        tagset(codes), tagset(sorted(pred))))
            else:
                self.model_hit += 1
            if s.st == 0:
                descs = [(a, describe_action(s, p, a)) for a in codes]
                self.pre_nodes.append((depth, s, p, descs))
                self.pre_sets[self.ptxt(s)] = (self.pos[p], ' / '.join(d for _, d in descs))
            else:
                self.post_per_pos[(s.st, self.pos[p])] += 1
                self._postflop_rules(s, p, codes, call, facing_allin, fits)
            for a, child in reversed(node.kids):
                try:
                    todo.append((child, s.apply(p, a), depth + 1))
                except ValueError as ex:
                    n_sub = self._count(child)
                    self.problems['subtree not simulated (%s; code %d)' % (ex, a)] += n_sub
                    self.total_nodes += n_sub

    def _postflop_rules(self, s, p, codes, call, facing_allin, fits):
        tag = ''.join({FOLD: 'f', CALL: 'c', ALLIN: 'A', POT: 'P'}.get(a, '?') for a in codes)
        nlive = len(s.live())
        if call == 0:
            prev = s.aggr[s.st - 1]
            if prev == p:
                role = 'previous-street aggressor'
            elif prev is None or s.folded[prev] or s.allin[prev]:
                role = 'no previous-street aggressor'
            elif not s.acted[prev]:
                role = 'OOP to previous aggressor (donk spot)'
            else:
                role = 'previous aggressor checked'
            self.first_bet[(role, 'pot bet < stack' if fits else 'pot bet >= stack', tag)] += 1
            if POT in codes:
                self.post_sizes[s.st].add('bet pot')
        else:
            behind = sum(1 for q in range(s.n) if q != p and s.needs(q))
            # players who still have to act after p (would be over-called)
            kind = 'facing all-in' if facing_allin else 'facing bet/raise'
            self.facing[(kind, 'raises so far %d' % s.raises,
                         'others still to act %d' % (behind), 'pot raise < stack' if fits else 'pot raise >= stack',
                         tag)] += 1

    # ------------------------------------------------------------------ output
    def text(self):
        L = []
        h = self.hdr
        v = self.v
        L.append('MonkerSolver .tree report: %s' % self.path)
        L.append('=' * 78)
        L.append('bytes %d  -> %d UTF-8 code points (1-byte %d, 2-byte %d, 3-byte %d, 4-byte %d)' % (
            len(self.raw), len(v), self.width[1], self.width[2], self.width[3], self.width[4]))
        L.append('')
        L.append('HEADER (values 0..%d)' % (h['tree_start'] - 1))
        L.append('  v[0]            = %d   constant, meaning unknown (magic/version?)' % h['magic_or_version'])
        L.append('  v[1] players    = %d' % h['players'])
        L.append('  v[2..4]         = %s   unknown (all zero)' % h['unknown_3'])
        bbm = max(h['blinds_mchip'])
        for i in range(h['players']):
            L.append('  seat %d %-6s u=%d  blind=%5d mchip (%s bb)  stack=%d chips (%s bb)' % (
                i, self.pos[i], h['seat_u'][i], h['blinds_mchip'][i],
                ('%g' % (h['blinds_mchip'][i] / bbm)), h['stacks_chips'][i],
                ('%g' % (h['stacks_chips'][i] * 1000 / bbm))))
        if any(h['unknown_3']) or any(h['seat_u']):
            L.append('  WARNING: unknown header fields are NON-ZERO here (ante/straddle/rake?): compare with the samples')
        else:
            L.append('  ante            = none found (u_i fields and v[2..4] are all zero)')
        L.append('')
        L.append('VERIFICATION')
        L.append('  tree grammar consumed values %d..%d of %d -> %s' % (
            h['tree_start'], self.end - 1, len(v), 'EXACT' if self.end == len(v) else 'MISMATCH'))
        edges = self.total_nodes - 1
        L.append('  nodes %d, edges %d, values in tree %d = nodes + edges -> %s' % (
            self.total_nodes, edges, len(v) - h['tree_start'],
            'OK' if self.total_nodes + edges == len(v) - h['tree_start'] else 'MISMATCH'))
        L.append('  action codes used: %s' % ', '.join('%s=%d' % (KNOWN_ACTIONS.get(a, a), c)
                                                     for a, c in sorted(self.actions_by_code.items())))
        L.append('  game-rule consistency problems: %s' % (dict(self.problems) if self.problems else 'none'))
        L.append('  terminal nodes by reason: %s' % dict(self.term_by_reason))
        L.append('  nodes offering both pot and all-in: %d' % self.x_and_allin)
        if self.pot_ratio_offered:
            L.append('  pot-raise-to / stack where pot offered: max %.3f;  where all-in replaces it: min %.3f' % (
                max(self.pot_ratio_offered), min(self.pot_ratio_replaced) if self.pot_ratio_replaced else float('nan')))
        L.append('  rule model R1-R5 (see predicted_actions): %d decision nodes match, %d differ' % (
            self.model_hit, len(self.model_miss)))
        mm = Counter((m[0], m[1], m[3], m[4]) for m in self.model_miss)
        for k, c in sorted(mm.items()):
            ex = next(m[2] for m in self.model_miss if (m[0], m[1], m[3], m[4]) == k)
            L.append('    %s %s actual %s predicted %s x%d  e.g. [%s]' % (k + (c, ex)))
        L.append('')
        L.append('COUNTS')
        dec = sum(self.nodes_by_street.values())
        L.append('  total nodes %d = decision %d + terminal %d' % (self.total_nodes, dec, sum(self.term_by_reason.values())))
        L.append('  decision nodes by street: %s' % ', '.join('%s %d' % (STREETS[k], c) for k, c in sorted(self.nodes_by_street.items())))
        L.append('  preflop terminals: %s;  preflop lines reaching the flop: %d' % (dict(self.pre_terminals), len(self.flop_entries)))
        per = Counter(self.pos[p] for _, _, p, _ in self.pre_nodes)
        acting = position_names(h['players'])[2:] + position_names(h['players'])[:2]
        L.append('  preflop decision nodes per position: %s' % ', '.join('%s %d' % (nm, per[nm]) for nm in acting))
        post = Counter()
        for (st_, nm), c in self.post_per_pos.items():
            post[nm] += c
        L.append('  postflop decision nodes per position: %s' % ', '.join('%s %d' % (nm, post[nm]) for nm in acting))
        L.append('')
        L.append('PREFLOP SITUATION SUMMARY (position, raises faced, own earlier action, live callers in front, closing?, facing all-in? -> offered actions, count, example sizes in bb)')
        summ = Counter()
        szs = defaultdict(set)
        for depth, s, p, descs in self.pre_nodes:
            lastok = {}
            for st_, q, tok in s.path:
                lastok[q] = tok
            callers = sum(1 for q in range(s.n) if q != p and not s.folded[q] and lastok.get(q, '').startswith('c'))
            own = {'r': 'had raised', 'A': 'had raised', 'c': 'had called'}.get(lastok.get(p, ' ')[0], 'fresh')
            closes = not any(q != p and s.needs(q) for q in range(s.n))
            fa = any(s.allin[q] and s.street[q] == s.maxbet for q in range(s.n))
            sizes = ' | '.join(d for _, d in descs)
            key = (self.pos[p], s.raises, own, callers, 'closes' if closes else 'others behind',
                   'faces all-in' if fa else '', ' | '.join(d.split(' ')[0] for _, d in descs))
            summ[key] += 1
            szs[key].add(sizes)
        order = {n_: i for i, n_ in enumerate(position_names(h['players'])[2:] + position_names(h['players'])[:2])}
        for k in sorted(summ, key=lambda k: (k[1], order[k[0]], k[2], k[3], k[4], k[5], k[6])):
            lvl = ['unopened', 'vs open', 'vs 3-bet', 'vs 4-bet', 'vs 5-bet', 'vs 6-bet', 'vs 7-bet'][min(k[1], 6)]
            ex = sorted(szs[k])
            L.append('  %-4s %-8s %-10s callers %d %-13s %-12s: %-20s x%-3d e.g. %s' % (
                k[0], lvl, k[2], k[3], k[4], k[5], k[6], summ[k], ex[0] if len(ex) == 1 else ex[0] + ' ... (%d variants)' % len(ex)))
        L.append('')
        L.append('PREFLOP TREE (every preflop decision node, pre-order; sizes = total put in this street, bb)')
        for depth, s, p, descs in self.pre_nodes:
            L.append('%s%s to act after [%s]: %s' % ('  ' * min(depth, 30), self.pos[p], self.ptxt(s),
                                                   ' | '.join(d for _, d in descs)))
        L.append('')
        L.append('FLOP ENTRIES (preflop line -> players, pot, effective stack behind, SPR, postflop nodes below)')
        for t, child in self.flop_entries:
            live = t.live()
            eff = min(t.stack[q] for q in live if not t.allin[q]) if t.canact() else 0
            cnt = self._count(child)
            L.append('  [%s] -> %d-way %s, pot %s bb, eff %s bb, SPR %.2f, subtree %d nodes' % (
                self.ptxt(t), len(live), '/'.join(self.pos[q] for q in live), t.fmt(t.pot), t.fmt(eff),
                eff / t.pot, cnt))
        L.append('')
        L.append('POSTFLOP (the file stores the full flop/turn/river betting tree)')
        L.append('  sizes seen: bet = pot (100 %), raise = pot, plus all-in; no other size exists in the file')
        L.append('  first bet on a street (role of the player to act, pot bet vs stack, offered actions):')
        for k in sorted(self.first_bet):
            L.append('    %-40s %-18s %-4s x%d' % (k[0], k[1], k[2], self.first_bet[k]))
        L.append('  facing a bet (kind, raises on street, players still to act behind, pot raise vs stack, offered):')
        for k in sorted(self.facing):
            L.append('    %-16s %-17s %-22s %-20s %-4s x%d' % (k + (self.facing[k],)))
        L.append('  (codes: f fold, c check/call, P pot bet/raise, A all-in)')
        return '\n'.join(L) + '\n'

    def _count(self, node):
        c = 0; st = [node]
        while st:
            n = st.pop(); c += 1
            st.extend(ch for _, ch in n.kids)
        return c


def diff_markdown(a, b):
    L = []
    L.append('# MonkerSolver .tree diff: %s vs %s' % (a.path.split('/')[-1], b.path.split('/')[-1]))
    L.append('')
    va, vb = a.v, b.v
    k = 0
    while k < min(len(va), len(vb)) and va[k] == vb[k]:
        k += 1
    t = 0
    while t < min(len(va), len(vb)) - k and va[-1 - t] == vb[-1 - t]:
        t += 1
    L.append('- bytes: %d vs %d; code points: %d vs %d' % (len(a.raw), len(b.raw), len(va), len(vb)))
    L.append('- header (values 0..%d): %s' % (a.hdr['tree_start'] - 1,
                                             'identical' if va[:a.hdr['tree_start']] == vb[:b.hdr['tree_start']] else 'DIFFERENT'))
    L.append('- common prefix: %d code points; common suffix: %d code points' % (k, t))
    L.append('- nodes: %d vs %d; decision nodes by street: %s vs %s' % (
        a.total_nodes, b.total_nodes, dict((STREETS[s], c) for s, c in sorted(a.nodes_by_street.items())),
        dict((STREETS[s], c) for s, c in sorted(b.nodes_by_street.items()))))
    L.append('- preflop decision nodes: %d vs %d; preflop lines reaching the flop: %d vs %d' % (
        len(a.pre_nodes), len(b.pre_nodes), len(a.flop_entries), len(b.flop_entries)))
    fa = Counter(len(t_.live()) for t_, _ in a.flop_entries)
    fb = Counter(len(t_.live()) for t_, _ in b.flop_entries)
    L.append('- flop entries by number of players: %s vs %s' % (dict(sorted(fa.items())), dict(sorted(fb.items()))))
    L.append('')
    L.append('## Preflop nodes whose action set differs (same action path)')
    L.append('')
    L.append('| path | actor | %s | %s |' % ('A', 'B'))
    L.append('|---|---|---|---|')
    common = [p for p in a.pre_sets if p in b.pre_sets]
    nd = 0
    for p in common:
        if a.pre_sets[p][1] != b.pre_sets[p][1]:
            nd += 1
            L.append('| %s | %s | %s | %s |' % (p, a.pre_sets[p][0], a.pre_sets[p][1], b.pre_sets[p][1]))
    L.append('')
    L.append('%d common preflop nodes, %d with a different action set; %d preflop nodes only in A, %d only in B.' % (
        len(common), nd, sum(1 for p in a.pre_sets if p not in b.pre_sets), sum(1 for p in b.pre_sets if p not in a.pre_sets)))
    L.append('')
    L.append('## Postflop subtrees of preflop lines present in both files')
    L.append('')
    sa = {a.ptxt(t): serialize(n) for t, n in a.flop_entries}
    sb = {b.ptxt(t): serialize(n) for t, n in b.flop_entries}
    both = [k for k in sa if k in sb]
    same = sum(1 for k in both if sa[k] == sb[k])
    L.append('%d flop entries in both files; postflop subtree identical (value by value) in %d of them.' % (len(both), same))
    L.append('')
    return '\n'.join(L) + '\n'


def serialize(node):
    out = []
    st = [node]
    while st:
        n = st.pop()
        out.append(len(n.kids))
        for a, ch in reversed(n.kids):
            st.append(ch)
        out.extend(a for a, _ in n.kids)
    return tuple(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('files', nargs='+')
    ap.add_argument('--diff', action='store_true')
    ap.add_argument('--u-as-ante', action='store_true',
                    help='treat the unknown per-seat header field as a dead ante in mchip (unverified)')
    args = ap.parse_args()
    if args.diff:
        a, b = Report(args.files[0], args.u_as_ante), Report(args.files[1], args.u_as_ante)
        sys.stdout.write(diff_markdown(a, b))
        return
    for f in args.files:
        sys.stdout.write(Report(f, args.u_as_ante).text())


if __name__ == '__main__':
    main()
