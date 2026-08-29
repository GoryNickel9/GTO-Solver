# CFR Solver Optimization — Next Profiling & Optimization Phase

> **STATO STORICO / SUPERATO (2026-08-08).** I baseline 164,0/150,7 s e
> l'ordine di priorità di questo documento non descrivono più il kernel
> corrente. Per riprendere usare l'handoff in `speed_optimization_journey.md`
> §§8.46-8.48. Lo snapshot 416.592.960 B / 86,1059943 s sotto è storico; il
> checkpoint 2026-08-14 è 249.955.776 B / 37,810434 s e tempo ancora FAIL.

## Context

Current benchmark against GTO+:

- Baseline full solve: **164.0 s**
- After Stage A+B: **150.7 s**
- Improvement so far: **~8%**
- Correctness: **bit-exact**
- Test suite: **15/15 passed**

The current implementation has already completed two important changes:

### Stage A — ReachRef refactor

The traversal now uses:

```cpp
using ReachRef = std::array<const ComboVector*, 2>;
```

instead of passing/copying complete reach objects.

The refactor was mechanical and correctness-neutral.

Verified:

- `dEV` bit-exact
- canonical `AhKhQh` result bit-exact
- full smoke test bit-exact
- no correctness regression

### Stage B — Remove opponent reach copies

Previously, each decision action copied both players' reach vectors and then multiplied the acting player's reach by strategy.

The opponent reach was therefore copied identically for every child action.

This has been removed.

Now:

- only the acting player's relevant flop-range slots are materialized
- the acting-player child reach is written into per-depth scratch
- the opponent reach is shared by reference from the parent
- no opponent reach copy occurs on the normal decision path

Current scratch:

```text
DecisionScratchLease
8 × 630 doubles per lease
```

Safety invariant has been checked:

> every read from `reach[p]` only accesses slots belonging to player `p`'s flop range

Therefore unwritten scratch slots are never accessed and no undefined behavior is introduced.

---

# Current Results

| Metric | Baseline | After Stage A+B | Delta |
|---|---:|---:|---:|
| Full run | 164.0 s | **150.7 s** | **-8%** |
| dEV th7d6s | 0.9249962879022555 | **bit-exact** | — |
| AhKhQh | 0.6741554018356799 | **bit-exact** | — |
| Reach propagation | ~1090 ms | **946 ms** | **-13%** |
| Serial-equivalent parts/pass | 2871 ms | **2534 ms** | **-12%** |
| Reach elements/pass | ~47M | **~16M** | **-66%** |
| Tests | 15/15 | **15/15** | — |

---

# Important Finding

The strongest signal is:

```text
reach elements/pass:
47M -> 16M
-66%

reach propagation time:
1090 ms -> 946 ms
-13%
```

This means runtime is **not scaling proportionally with the number of reach elements processed**.

Therefore the next priority is no longer simply "perform fewer reach copies".

We now need to determine what actually consumes the remaining **946 ms**.

Likely candidates:

- sparse/indexed memory access
- cache misses
- scratch working-set size
- loop setup/fixed cost
- address/index calculations
- lack of vectorization
- hidden work currently grouped under "reach propagation"
- poor locality between reach and strategy
- unnecessary simultaneous action buffers

Do not perform another large reach refactor until this cost is decomposed.

---

# Phase A.2 — Detailed Reach Profiling

## Goal

Break the current:

```text
Reach propagation: ~946 ms/pass
```

into concrete sub-costs.

Add profiling around the actual physical decision path.

Measure at minimum:

```text
actor multiply/store
scratch acquire
scratch release
scratch initialization/clearing
action-loop setup
ReachRef construction
child-call preparation
slot/index iteration
other reach-related work
```

Expected report:

```text
Reach propagation
  total:                         ... ms

  actor multiply/store:          ... ms
  scratch acquire/release:       ... ms
  scratch clear/init:            ... ms
  action-loop/setup:             ... ms
  slot/index traversal:          ... ms
  ReachRef/child setup:          ... ms
  misc:                          ... ms
```

---

# Add Reach Counters

Record per pass:

```text
decision nodes
actions processed
average actions per decision

actor elements multiplied
actor elements written
strategy elements read

slot indices read

scratch leases acquired
scratch buffers touched
scratch bytes written

opponent elements copied
opponent bytes copied
```

The final two counters must remain:

```text
opponent elements copied = 0
opponent bytes copied = 0
```

for the normal physical decision path.

---

# Phase B.3 — Isolated Actor Reach Kernel Benchmark

The current core operation is conceptually:

```cpp
dst[slot] = src[slot] * strategy[slot];
```

We need to know how expensive this kernel actually is.

## Create a standalone/internal microbenchmark

Benchmark the exact production data representation.

Measure:

```text
elements processed
iterations
elapsed time
ns / element
effective GB/s
```

Run separately for:

1. current indexed/sparse slot traversal
2. dense contiguous traversal, if possible
3. compiler auto-vectorized build
4. scalar/reference build if useful for comparison

Example dense kernel:

```cpp
for (size_t i = 0; i < hand_count; ++i) {
    dst[i] = src[i] * strategy[i];
}
```

Example indexed kernel:

```cpp
for (auto slot : flop_slots) {
    dst[slot] = src[slot] * strategy[slot];
}
```

Do not assume these have similar performance.

---

# Key Question — Are Flop Slots Dense or Sparse?

The current implementation writes:

> only the acting player's flop-range slots

Inspect the exact data representation.

If the hot loop is effectively:

```cpp
for (uint16_t slot : player_flop_slots) {
    dst[slot] = src[slot] * strategy[slot];
}
```

then every iteration may require:

```text
load slot index
calculate scattered address
load src
load strategy
store dst
```

This can prevent efficient SIMD and reduce cache/prefetch efficiency.

---

# Experiment — Dense Per-Player Hand Indexing

If hot reach arrays currently use global 0..629 combo slots with an indexed subset, benchmark a dense representation.

Concept:

```text
global combo slot:
0 ... 629

player dense range:
0 ... H-1
```

Maintain mapping outside the main arithmetic path:

```cpp
dense_to_combo[dense_index]
combo_to_dense[combo_slot]
```

Then the hot operation becomes:

```cpp
for (size_t i = 0; i < hand_count; ++i) {
    dst[i] = src[i] * strategy[i];
}
```

Potential advantages:

- contiguous reads
- contiguous writes
- hardware prefetch
- easier auto-vectorization
- fewer index loads
- less address arithmetic
- smaller active working set
- better cache-line utilization

Do not integrate this globally before benchmarking the isolated kernel.

First prove that the dense representation materially improves throughput.

---

# Compiler Vectorization Audit

For the actor-reach kernel:

- compile in full release mode
- verify optimizer flags
- inspect vectorization report
- inspect generated assembly if needed

Recommended release configuration where supported:

```text
-O3
-march=native
```

Check whether the compiler emits SIMD for:

```cpp
dst[i] = src[i] * strategy[i];
```

If alias analysis blocks vectorization, consider safe non-alias semantics such as:

```cpp
void propagate(
    double* __restrict dst,
    const double* __restrict src,
    const double* __restrict strategy,
    size_t n
);
```

Use only if the non-alias invariant is truly guaranteed.

Do not add handwritten AVX2/AVX-512 before confirming whether the compiler already produces equivalent code.

---

# Scratch Working-Set Audit

Current scratch size:

```text
8 × 630 doubles / lease
```

Approximate size:

```text
8 × 630 × 8 bytes
≈ 40 KB
```

This is large enough that scratch layout may materially affect L1/L2 behavior.

Inspect whether lease acquisition performs any of the following:

```cpp
std::fill(...)
memset(...)
clear(...)
value initialization
construction of full arrays
```

The ideal steady-state behavior is:

```text
scratch acquire:
almost zero work

scratch write:
only slots actually used by current actor/action

scratch reset:
none
```

Unwritten values should simply be overwritten next time, provided safety invariants guarantee they are never read.

---

# Critical Experiment — 8 Action Buffers vs 1 Buffer per Depth

Determine whether all action reach buffers must exist simultaneously.

Current conceptual layout:

```text
scratch[depth][action][slot]
```

If traversal is sequential:

```cpp
for (action) {
    build child reach
    recurse into child
    consume result
}
```

then evaluate whether this can become:

```text
scratch[depth][slot]
```

and reuse the same actor reach buffer for each action.

Conceptually:

```cpp
for (Action action : actions) {
    auto* actor_child = scratch[depth];

    propagate_actor(
        actor_child,
        *reach[player],
        strategy[action]
    );

    ReachRef child_reach = reach;
    child_reach[player] = actor_child;

    child_value = recurse(child[action], child_reach, depth + 1);

    update_from_child(...);
}
```

Potential benefit:

```text
working set:
8 × 630 doubles
->
1 × 630 doubles
```

per depth/lease.

This may substantially improve cache residency even if arithmetic work is unchanged.

---

# Safety Requirement for Single-Buffer Reuse

Before implementing, verify lifetime requirements.

Single-buffer reuse is valid only if:

- child recursion completes before the next action overwrites the buffer
- no child stores a persistent pointer/reference to that scratch after return
- no sibling action is processed concurrently using the same buffer
- value/update code does not require all child reaches simultaneously
- nested recursion uses a different depth buffer

If any of these assumptions are false, document why and do not force the optimization.

---

# Terminal Showdown — Updated Status

The terminal showdown has already been verified to use the desired non-pairwise algorithm.

Current algorithm:

```text
1. accumulate opponent reach:
   totals[rank]
   by_card[card][rank]

2. prefix scan:
   prefix[rank]
   card_prefix[card][rank]

3. for each hero hand:
   lower/equal/higher
   minus blocker corrections
   plus required own-reach correction
```

Complexity:

```text
O(H + 36 × R)
```

There is no O(H²) hero-villain scan to eliminate.

Therefore do not implement the previously planned algorithmic showdown refactor.

---

# Showdown Micro-Profile

Current observed cost:

```text
~27 us per terminal node
```

with approximately:

```text
~10 us
```

spent in:

```text
fill_n / array reset
prefix scans
```

Add a detailed breakdown:

```text
Showdown
  clear/fill arrays:          ... us
  opponent accumulation:     ... us
  prefix scans:              ... us
  hero correction/lookups:   ... us
  EV output/store:           ... us
  total:                     ... us
```

Also record:

```text
terminal nodes/pass
active rank count
rank-domain size
active cards
hands/opponent
hands/hero
```

---

# Showdown Optimization Experiments

Do not perform a broad rewrite.

Run isolated experiments in this order.

## 1. Clear/fill cost

Determine exact cost of resetting:

```text
totals
prefix
by_card
card_prefix
```

If full-array zeroing is a material percentage of terminal cost, benchmark alternatives.

Potential options:

- reduce arrays to effective rank domain
- reuse compact rank buckets
- touched-rank lists
- generation counters
- partial reset

Important:

For small cache-resident arrays, `fill_n` may already be faster than bookkeeping-heavy touched lists.

Benchmark; do not assume.

---

## 2. Prefix scan fusion

Inspect whether multiple passes can be fused.

Example question:

Can accumulation/prefix generation reduce the number of complete rank-array traversals?

Avoid harming vectorization or creating branch-heavy loops.

---

## 3. by_card layout

Inspect the actual hot access order.

Current conceptual data:

```cpp
by_card[card][rank]
```

Determine whether this matches both:

- prefix scan traversal
- hero blocker correction traversal

Use cache/profile evidence before changing to:

```cpp
by_card[rank][card]
```

or any alternative layout.

---

## 4. Scattered-load analysis

If hero processing performs scattered loads from multiple prefix arrays, measure:

- cache misses
- load stalls
- TLB effects if relevant
- address-generation overhead

Only optimize after identifying a measurable bottleneck.

---

# Value + Update

This is still secondary.

Do not optimize heavily until the new reach and showdown profiles are available.

Potential future work:

- remove unnecessary intermediate EV arrays
- fuse child-value consumption with regret update
- specialize 2-action nodes
- specialize 3-action nodes
- improve contiguous layout
- hoist invariant branches
- reduce duplicate strategy loads/writes

---

# New Performance Metrics

The next report should include more than element counts.

Add:

```text
reach ns/element
reach effective bandwidth
actor multiply/store ms
slot traversal ms
scratch management ms

showdown us/node
showdown clear us/node
showdown prefix us/node

decision nodes/pass
actions/pass
terminal nodes/pass
```

If hardware counters are available, also collect:

```text
L1 data cache misses
L2 misses
LLC misses
branch misses
instructions
cycles
IPC
memory bandwidth
```

Use the same fixed benchmark for all comparisons.

---

# Decision Logic After the New Profile

## Case A — Actor multiply is cheap

Example:

```text
actor multiply/store:
<100 ms

reach propagation total:
~946 ms
```

Then the `reach propagation` timer is hiding other work.

Focus on:

- traversal setup
- scratch management
- index iteration
- child preparation
- profiler categorization

Do not optimize multiplication.

---

## Case B — Indexed actor multiply is expensive

If:

```text
indexed kernel:
high ns/element

dense kernel:
much faster
```

then prioritize dense range representation or another layout that makes hot arrays contiguous.

This becomes a structural memory-layout optimization.

---

## Case C — Scratch working set is the problem

If reducing:

```text
8 × 630
```

to:

```text
1 × 630
```

per depth materially improves performance, prioritize the single-buffer architecture.

This would indicate cache capacity/locality is a major contributor.

---

## Case D — Showdown remains dominant

If reach drops significantly and showdown becomes the clear majority of CPU time, move to micro-optimization of:

```text
clear/fill
prefix scans
layout
scattered blocker loads
```

Do not replace the already-correct cumulative-mass algorithm.

---

# Benchmark Matrix

For every experiment record:

| Version | Full solve | Wall/pass | Reach ms | Reach elems | ns/elem | Showdown ms | Correct |
|---|---:|---:|---:|---:|---:|---:|---|
| Baseline | 164.0 s | ... | ~1090 | ~47M | ... | ... | yes |
| Stage A+B | 150.7 s | ... | 946 | ~16M | ... | ... | yes |
| Dense-kernel test | ... | ... | ... | ... | ... | ... | yes |
| Single-scratch test | ... | ... | ... | ... | ... | ... | yes |
| Next final | ... | ... | ... | ... | ... | ... | yes |

Do not accept a performance change that causes unexplained numerical differences.

---

# Correctness Requirements

All optimization experiments must preserve:

```text
dEV
specific canonical board results
strategy
EV
regrets where directly compared
convergence behavior
test suite
```

Prefer bit-exact behavior where the operation ordering has not intentionally changed.

If a SIMD, layout, or fused-loop change changes floating-point order, define and document an acceptable numerical tolerance before accepting the change.

---

# Current Priority Order

Proceed in this order:

```text
1. Decompose the remaining 946 ms reach cost
2. Benchmark the actor multiply kernel in isolation
3. Compare indexed vs dense traversal
4. Verify compiler vectorization
5. Audit scratch initialization and lifetime
6. Test 1 scratch buffer per depth vs 8 action buffers
7. Re-profile full pass
8. Only then micro-optimize showdown
9. Re-profile before touching value/update
```

---

# Key Engineering Principle

The latest result proves that reducing the count of logical reach operations alone is not enough.

The next question is:

> Why does processing only ~16M reach elements still cost ~946 ms of aggregate CPU time?

Do not guess.

Measure whether the residual cost is caused by:

```text
memory access pattern
cache behavior
index indirection
working-set size
hidden fixed work
lack of SIMD
or incorrect profiling attribution
```

The next optimization decision must be based on this breakdown.

---

# Success Criteria for This Phase

This phase is complete when all of the following are known:

- exact sub-breakdown of the current ~946 ms reach cost
- actual time spent in actor multiply/store
- measured ns per reach element
- indexed vs dense kernel performance
- whether the kernel is SIMD-vectorized
- whether scratch is initialized/cleared unnecessarily
- whether 8 action buffers can safely become 1 per depth
- detailed showdown sub-profile
- updated full-run benchmark
- correctness remains validated

The immediate target remains:

```text
150.7 s
->
meaningfully lower through measured hot-path improvements
```

Do not pursue speculative broad rewrites until these measurements identify the next dominant cost.
# Nota di stato

> **STATO: ANALISI STORICA / FASE CHIUSA.** Nuove ottimizzazioni richiedono
> profiling e benchmark aggiornati.
