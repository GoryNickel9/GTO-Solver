# ADR 0003 — Canonical chance tree and node-owned CFR state

Status: **accepted; node-owned traversal implemented, production benchmark pending**
Date: 2026-08-27

## Context

The TSTC9D physical-tree solve exceeded 12 GB and did not complete within the
600-second benchmark window. The lossless layout compiler subsequently counted
2,791,872 physical public nodes, 1,758,624 canonical public nodes,
145,524,152 information sets and 366,890,152 action entries. This shows that
the target state fits near the GTO+ memory class only if the solver does not
materialize independent double-precision regret and strategy arrays for the
physical tree.

An experimental global canonical DAG was rejected. A mutable CFR block could
be reached more than once in one player pass, which required an additional
O(actions) deferred-regret buffer and made CFR+ clamping order-sensitive.
Replacing that DAG with a recursively canonicalized tree removed shared
subtrees, but the experiment also proved that multiplying one representative
update by a physical-path multiplicity does not reproduce the finite-iteration
trajectory of the current physical oracle for asymmetric ranges. The new core
therefore needs its own node-local ISO-off oracle; the old global-infoset
physical traversal is not a suitable state-by-state oracle for the new update
schedule.

## External implementations reviewed

- [b-inary/postflop-solver](https://github.com/b-inary/postflop-solver) combines
  isomorphic turn and river deals, stores solver values in the game nodes, uses
  float32 for most values and float64 for summation, and optionally compresses
  each node to 16-bit values plus a float32 scale. Its chance traversal recurses
  only into represented children and expands omitted outcomes by hand-index
  swaps when aggregating CFVs.
- [exinori/DCFR-SOLVER](https://github.com/exinori/DCFR-SOLVER) independently
  reports a node-oriented SoA layout, range-aware compact hand maps, SIMD,
  chance parallelism and differential ISO-on/ISO-off tests. Its documented
  forward-versus-inverse permutation defect on non-involutory suit groups is a
  required regression case here.
- [OpenSpiel CFR](https://github.com/google-deepmind/open_spiel/tree/master/open_spiel/algorithms)
  keeps cumulative regret and policy values keyed by information state and is
  retained as the small-game mathematical oracle.

The AGPL implementation is used only as architectural evidence. No source code
is copied into this project.

## Decision

Implement a **tree, not a DAG**, with these ownership rules:

1. Every betting node owns exactly one mutable CFR state block.
2. Chance nodes store only non-isomorphic turn/river children plus an immutable
   expansion table for omitted cards.
3. No mutable state block is reachable through two child pointers.
4. The production tree is compiled directly; the full physical public tree is
   available only in test/oracle builds.
5. Regret and average-strategy storage is action-major SoA over the node's live
   private hands. The first production format is the existing packed 24-bit
   action state; float32 remains the accuracy oracle and float64 is used for
   reductions and certification.
6. CFR+ and DCFR share the same traversal and storage API. DCFR is the preferred
   benchmark candidate; CFR+ remains selectable for parity and regression.

At a chance node the traversal will:

1. scale counterfactual reach by the exact chance denominator;
2. recurse once for every stored representative child;
3. retain the representative CFV vector;
4. apply the **inverse** private-hand permutation for each omitted outcome;
5. accumulate the expanded CFVs in float64;
6. update the child node's state once, without physical-path multiplication.

This is the crucial distinction from the rejected experiment: isomorphic
outcomes are expanded in the returned CFV, not represented by repeatedly
updating or multiplying the same mutable CFR block.

## Memory model

For TSTC9D, 366,890,152 action entries imply:

- packed 3-byte regret+strategy state: 1,100,670,456 bytes;
- compact canonical topology, board tables, expansion maps and bounded worker
  scratch: approximately 0.42–0.45 GB from the measured layout model;
- expected total: approximately 1.52–1.55 GB for 1–8 workers.

The design forbids a dense per-worker or global double-precision regret delta.
Workers may allocate only bounded CFV/reach arenas proportional to traversal
depth and live hands. Checkpoint encoding uses the same packed state and writes
chunks atomically.

## Parallelism

Parallel work is split only across disjoint representative chance subtrees.
Each worker owns all writes below its assigned child. Parent CFV vectors are
reduced after join in stable card order. No lock or atomic operation is allowed
in the regret-matching hot path.

## Validation gates

The new core is promoted only after all gates pass:

1. CFR+ and DCFR on Kuhn and Leduc against exact OpenSpiel/reference values.
2. Node-local ISO-off versus ISO-on differential tests on turn-only, river-only
   and flop trees, with asymmetric player ranges.
3. Explicit S3 three-cycle tests checking forward reach mapping and inverse CFV
   mapping on monotone and paired boards.
4. Equal profile EV, best-response EV and NashConv within declared tolerances;
   state equality is required only when both modes use the same update schedule.
5. Float32 oracle versus packed-state dEV and root-EV gates.
6. TSTC9D preflight below 2 GB, then an isolated solve with measured
   `solver_state_bytes`, wall time and convergence.

The first asymmetric-range differential gate now passes in Release after two
CFR+ iterations. ISO-off materializes 165,774 node-owned public nodes and
4,361,904 action entries; ISO-on materializes 46,065 nodes and 1,234,628
entries. Profile EV, best-response EV and NashConv agree within `1e-11`.
This removes the former symmetric-range fallback. The turn-only, river-only,
S3 permutation, packed-state and TSTC9D gates remain required before promotion
is complete; F11+ remains frozen.

The implementation also uncovered and fixed two correctness/stability defects:

- reach and CFV permutations must respect per-player compact slot spaces; a
  raw `ComboId` permutation is valid only for combo-indexed vectors;
- certification traversal scratch must be heap-owned. Keeping the traversal
  object on the Windows thread stack caused a reproducible out-of-core stack
  overflow on the 630-combo oracle.

The final Release regression suite passes 18/18 tests, including the 24-assertion
GTO+ reference, the asymmetric node-owned differential, the zero-sum oracle,
checkpoint resume, out-of-core certification and range-query regression.

## Consequences

The architecture removes the dominant physical-tree and deferred-delta memory
costs and gives chance parallelism natural ownership boundaries. It also means
the current postflop traversal cannot be incrementally patched into the final
form: tree compilation, node storage and traversal must move together behind a
new internal interface. GUI, CLI, rules, ranges, evaluator and persistence
schemas remain reusable.

The traversal and ownership model are now implemented, but the runtime builder
still creates the physical public tree before compiling the canonical tree.
Therefore the steady-state architecture is validated while the TSTC9D peak-RSS
gate is not: the next implementation slice is a direct streaming canonical
compiler that never materializes the full physical tree.
