# Peak RSS audit — 2026-09-03

> **ARCHIVIATO 2026-09-04 — CONCLUSIONE MEMORIA INVALIDA.** Questo report
> confrontava Peak RSS con “Memory needed for solving”, che è una stima interna
> GTO+ di composizione non identificata. Il claim `3/3 PASS` è ritirato; misure
> OS e modifiche generiche restano evidenza. Non esiste un cap desktop `<2 GiB`.
> Vedere il [`piano di correzione`](../../GTO_PLUS_SOLVER_MEMORY_SEMANTICS_AND_GATE_CORRECTION_PLAN_2026-09-03.md).

## Scope and invariants

This audit targets the per-fixture GTO+ `peak_rss_bytes` gates without changing
the game, ranges, action tree, exact outcomes, alternating signed DCFR
`1.5/0/3`, `ScaledUint16RegretStrategy`, CPU-only execution, or the eight-thread
limit. The fixtures are measurements, never dispatch keys. The desktop
`< 2 GiB` contract and `solver_state_bytes` remain separate diagnostics.

The final measurements below were produced by the Release build on the same
Windows host. AHKHQH and TH7D6S are maxima from five independent full
time-to-target processes. TSTC9D is a full 160-iteration time-to-target
non-regression process, not a fixed-iteration estimate.

| Fixture | Processes | State residency | Current peak RSS | GTO+ reference | Status |
|---|---:|---|---:|---:|---|
| AHKHQH-101 | 5 | budgeted OS-page-backed | 7,790,592 B max | 8,000,000 B | PASS |
| TH7D6S-101 | 5 | resident vectors | 363,569,152 B max | 399,000,000 B | PASS |
| TSTC9D-101 | 1 | resident vectors | 1,534,152,704 B | 2,000,000,000 B | PASS |

AHKHQH, TH7D6S and TSTC9D completed at iterations `80/80/160`, target dEV
`0.951423%/0.807956%/0.904505%`, and passed the complete correctness gate.
AHKHQH also passes its time gate; TH7D6S and TSTC9D still fail their separate
time gates. This audit closes only Peak RSS.

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
- Per-board private-hand lookup tables now live in each player's actual flop
  range space. A narrow range no longer pays for two dense 630-entry arrays on
  every turn and river board; full-range games retain the same O(hands) bound.
- The traversal factory now instantiates its existing 36-combo specialization
  when the real player ranges fit it instead of jumping directly to the
  256-combo class.
- Showdown accumulators are sized once from the maximum rank cardinality
  actually present in the prepared boards. They remain allocation-free in the
  hot path, but each worker no longer reserves the 630-combo maximum for a
  narrow game.
- Canonical edges are allocated in one stable contiguous arena. Identical
  action descriptors are interned in a stable pool, canonical nodes derive
  their representative id in the direct compiler, and exceptional legacy
  mappings remain out-of-line. Canonical nodes are now 40 bytes and edges 24
  bytes, down from 48 and 32 respectively.
- The final node/edge/outcome records encode their bounded fields directly and
  occupy `16/8/8` bytes. Terminal construction reuses the node union for its
  temporary payoff index, and the bounded board/payoff intern tables use
  deterministic linear lookup instead of retaining hash-node allocations.
- An explicit nonzero working-set target can select an exact OS-page-backed
  `ScaledUint16RegretStrategy` state when the logical state plus current
  residency cannot fit. The selection formula uses only byte counts and
  process RSS; no fixture id, cards or fingerprint participate. State pages
  are released only after their decision update, while codec, update order and
  checkpoint bytes stay unchanged.
- The JSON report publishes `solver_state_residency`, the requested working-set
  target and whether persistence requires explicit materialization. Ordinary
  callers with a zero target retain resident vectors.

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
game. CFR-D and the user-managed file backend remain separate scale tiers. The
new budgeted runtime backend changes only OS residency and is explicitly
reported; it may incur local paging I/O and is not presented as a reduction of
`solver_state_bytes`.

## AHKHQH lower bound and selected backend

Before the budgeted backend, the resident implementation measured:

- process startup peak before preparation: 5,844,992 B;
- required solver state: 5,300,664 B;
- ranked resident layout accounting: 3,643,734 B;
- preparation peak: 10,231,808 B;
- state-ready current RSS: 15,548,416 B;
- traversal-ready current RSS: 15,695,872 B;
- measured full-process peak: 18,161,664 B;
- GTO+ reference: 8,000,000 B.

The required state alone plus the observed process baseline is 11,145,656 B,
before topology, board metadata, traversal scratch or allocator pages. Thus an
8 MB whole-process RSS PASS is not reachable by allocator trimming or ordinary
resident vectors alone. The gate remained unchanged; this lower bound is why
the generic budgeted residency backend is selected for AHKHQH.

The exact stabilizer census also bounds private-hand orbit storage. AHKHQH has
595,626 physical infosets / 1,288,290 state actions and 385,980 exact private
orbits / 834,636 actions. Even an ideal orbit implementation would reduce state
only from 5,300,664 B to 3,486,048 B; combined with the process baseline it
already exceeds 8 MB before topology and scratch. Private orbits remain useful,
but cannot close this gate alone.

A clean-room compression probe over the exact 80-iteration
`ScaledUint16RegretStrategy` bytes found:

| Independent raw block target | Zstandard level 1 bytes |
|---:|---:|
| 1,024 B | 3,545,729 B |
| 4,096 B | 2,148,307 B |
| 16,384 B | 1,227,023 B |
| 65,536 B | 991,106 B |

The corresponding simple palette and delta-varint codecs remained between
2.75 MB and 3.81 MB, so dictionary coding alone is insufficient. These are
offline size measurements, not promotion evidence: a production in-memory
compressed arena must preserve every code and per-node scale, bound decoded
pages per worker, prove thread ownership, and pass a local encode/decode/update
cost gate before it can replace the direct vectors. The next valid frontier is
therefore a bounded lossless state-page backend, not fixture-specific
scheduling or a lossy codec. The promoted solution keeps the same logical bytes
in an address-stable OS-page-backed mapping, bounds residency with 64 KiB
release quanta, and materializes those exact bytes only when persistence is
requested.

The production contract's CPU/RAM-only wording excludes GPU and compute
accelerators. This backend remains CPU-only and local, but OS page backing can
use the machine's paging subsystem. A deployment requiring physically resident
RAM with paging forbidden must leave the target at zero; under that stricter
interpretation the 8 MB AHK whole-process gate is impossible by the measured
lower bound above.

## Validation completed

- Complete Release build: PASS.
- Full Release CTest: 28/28 PASS in 198.25 s.
- Exact reference executable: 24/24 PASS; asymmetric range-orbit
  counterexample: 11/11 PASS.
- Resident versus forced budgeted state: byte-identical regret codes, strategy
  codes and both scale arrays; exact certification equal; materialized archive
  accepted. Phase 10: 10,660 assertions PASS.
- AHKHQH five-process peak RSS min/median/max:
  `7,761,920/7,778,304/7,790,592 B`; memory, dEV, root/layout and time PASS
  in 5/5.
- TH7D6S five-process peak RSS min/median/max:
  `363,442,176/363,491,328/363,569,152 B`; memory and correctness PASS in
  5/5, time FAIL in 5/5.
- TSTC9D full convergence: peak RSS `1,534,152,704 B`, iteration 160,
  dEV `0.904505%`, correctness and memory PASS; time FAIL.
- Per-fixture Peak RSS gate: 3/3 PASS. No fixture-specific dispatch exists.
