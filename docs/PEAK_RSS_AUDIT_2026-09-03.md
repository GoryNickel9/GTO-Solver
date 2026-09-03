# Peak RSS audit — 2026-09-03

## Scope and invariants

This audit targets the per-fixture GTO+ `peak_rss_bytes` gates without changing
the game, ranges, action tree, exact outcomes, alternating signed DCFR
`1.5/0/3`, `ScaledUint16RegretStrategy`, CPU-only execution, or the eight-thread
limit. The fixtures are measurements, never dispatch keys. The desktop
`< 2 GiB` contract and `solver_state_bytes` remain separate diagnostics.

The measurements below were produced by the Release build on the same Windows
host. A one-iteration fixed diagnostic exercises layout, state allocation, one
update and exact certification; the TH7D6S row was also confirmed by the full
80-iteration convergence run.

| Fixture | Pre-stream peak RSS | Current peak RSS | GTO+ reference | Status |
|---|---:|---:|---:|---|
| AHKHQH-101 | 33,071,104 B | 23,216,128 B | 8,000,000 B | FAIL |
| TH7D6S-101 | 398,049,280 B | 391,462,912 B | 399,000,000 B | PASS |
| TSTC9D-101 | 1,666,785,280 B | 1,647,755,264 B | 2,000,000,000 B | PASS |

TH7D6S completed 80 iterations at `0.807956%` target dEV and passed the
correctness gate. Its latest elapsed solver time was 24.7373 s; temporal parity
remains a separately certified gate. TSTC9D was a fixed one-iteration memory
regression run, not convergence evidence.

## Root causes found

1. The benchmark CLI retained a second physical analysis layout whenever a
   reference contained non-root paths. That doubled most topology and board
   metadata even though the production canonical layout can navigate ordinary
   action paths directly.
2. Every canonical node paid for terminal payoff arrays, a 24-byte `Action`,
   a standard 24-byte edge vector and a standard 24-byte outcome vector,
   regardless of node kind or outcome arity.
3. `BoardData` retained pre-expanded compatibility tables and rank lookup
   arrays that were either derivable from immutable card masks or needed only
   while preparing a river board.
4. Exact certification created worker scratch arenas although it is read-only
   and does not participate in the DCFR update. Those arenas raised the high
   water mark without changing edge-order reductions.
5. Construction retained hash tables and excess vector capacity after their
   final consumer.
6. The direct canonical path still materialized the complete `PublicTree`
   before compiling the node-owned graph. On AHKHQH this temporary tree alone
   raised preparation from a roughly 12 MB resident graph to a 33 MB peak.
7. Every traversal instance retained both `float` and `double` showdown
   accumulators even though its scalar type selects exactly one of them.

## Generic changes

- The production benchmark prepares only the canonical solver layout. A new
  read-only action-navigation API exposes `(Action, child)` pairs without
  materializing the physical browser tree. GUI/browser preparation is
  unchanged.
- Terminal payoff matrices are bit-exactly interned and terminal nodes point
  to stable shared values.
- `Action` was reordered from 24 to 16 bytes without changing fields or
  serialization semantics.
- Canonical nodes now use a kind-specific union and occupy 56 bytes instead of
  144. The duplicated decision `board_index` was removed.
- Canonical edges use a 16-byte owner vector and a 12-byte small outcome list;
  a single outcome stays inline, while larger suit-isomorphic orbits retain
  owned heap storage. Edge size is 32 bytes.
- The 72 pre-expanded per-board compatibility vectors were replaced by direct
  immutable card-mask checks. The per-board rank index is now call-local
  scratch and is released after terminal metadata is prepared. The legacy
  union-range local index is derived from the already sorted combo list rather
  than retaining another fixed 630-entry array in every board.
- Legacy union-range slot tables are not built for the player-indexed
  canonical production traversal. The physical/browser traversal still gets
  them.
- Certification evaluates the two profile/best-response pairs sequentially
  with the deterministic serial traversal, avoiding an additional worker
  pool. Solver updates retain the configured parallelism.
- Builder-only hash tables are released early and board capacity is compacted
  before solver state allocation.
- The public-tree builder now also exposes a depth-first streaming consumer.
  The production canonical compiler consumes states and edges directly, keeps
  node ids and edge order identical to the materialized builder, and never
  retains a second `PublicState` tree. Physical inspection/browser layouts are
  unchanged.
- `DecisionLayout` overlays the mutually exclusive direct-action and physical-
  infoset offsets. It occupies 16 instead of 24 bytes, reducing every canonical
  node from 56 to 48 bytes without changing checkpoint offsets or fingerprints.
- Traversal showdown scratch is now typed by the traversal scalar, so a worker
  retains one accumulator family instead of parallel `float` and `double`
  copies. Exact certification remains `double`; reusing the resident `float`
  solver traversal was explicitly rejected after violating the `1e-11`
  zero-sum tolerance.

No fixture identifier, fingerprint, board literal or benchmark threshold is
used by these paths.

## External implementation audit

The main clean-room comparison was
[`b-inary/postflop-solver`](https://github.com/b-inary/postflop-solver) at commit
`9d1509fe5077d019825f833eed04b16d342dfda1`. Its useful architectural signals
are:

- compact nodes and contiguous child ranges in `src/game/mod.rs` and
  `src/game/node.rs`;
- global contiguous strategy/regret/value buffers assigned to nodes in
  `src/game/base.rs`;
- explicit turn/river chance isomorphism and private-hand swap tables in
  `src/card.rs`;
- stack-like temporary allocation in `src/alloc.rs`;
- 16-bit state arrays plus per-node scales, with wider arithmetic for
  aggregation.

The project is AGPL-3.0. No source was copied. Only general data-layout ideas
were applied to the independently implemented C++ engine. Its README memory
figures are internal solver estimates for a different 52-card NLHE game and
must not be compared directly with this repository's whole-process Windows
peak RSS.

Primary literature reviewed for admissibility:

- Zinkevich et al., *Regret Minimization in Games with Incomplete Information*
  (CFR);
- Brown and Sandholm, *Solving Imperfect-Information Games via Discounted
  Regret Minimization* (DCFR);
- Burch, Johanson and Bowling, *Solving Imperfect Information Games Using
  Decomposition* (CFR-D);
- Tammelin et al., *Solving Heads-up Limit Texas Hold'em* / CFR+ engineering;
- Waugh, *A Fast and Optimal Hand Isomorphism Algorithm*;
- *Compact CFR* as a separate algorithmic trade-off.

Compact CFR, sampling, bucketing and lossy abstraction were rejected for this
gate because they change the algorithm, convergence contract or represented
game. CFR-D and out-of-core state remain separate scale tiers, not silent
replacements for the exact in-memory benchmark.

## Remaining AHKHQH blocker

AHKHQH is still red after removing the duplicate analysis tree and streaming
the canonical compiler. The latest
run measured:

- process startup peak before preparation: 5,791,744 B;
- required solver state: 5,300,664 B;
- ranked resident layout accounting: 4,850,750 B;
- preparation peak: 12,206,080 B;
- measured full-process peak: 23,216,128 B;
- GTO+ reference: 8,000,000 B.

The required state alone plus the observed process baseline is 11,092,408 B,
before topology, board metadata, traversal scratch or allocator pages. Thus an
8 MB whole-process RSS PASS is not reachable by allocator trimming alone.
The gate is intentionally left unchanged and failing.

The next technically valid frontier is a direct flat-arena compiler combined
with lossless player-local private-hand orbit storage under each public
history's stabilizer. That work must prove reach/value permutation parity and
update each logical information set exactly once. It cannot reuse the rejected
range-asymmetric physical-orbit aggregation, and it must pass the existing
asymmetric-range and exact-reference oracles before promotion.

## Validation completed

- Release build: PASS.
- Full Release CTest suite: 28/28 PASS in 215.82 s.
- TH7D6S full convergence and correctness: PASS at iteration 80.
- TH7D6S per-fixture Peak RSS: PASS.
- TSTC9D fixed-iteration Peak RSS regression: PASS.
- AHKHQH per-fixture Peak RSS: FAIL; lower-bound evidence above.
