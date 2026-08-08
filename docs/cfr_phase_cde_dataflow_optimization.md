# CFR Solver Optimization — Phase C/D/E

## Context

Current performance state:

| Metric | Baseline | Current | Delta |
|---|---:|---:|---:|
| Full run | 164.0 s | **145.8 s** | **-11%** |
| dEV th7d6s / AhKhQh | — | **bit-exact** | — |
| Reach propagation | ~1090 ms | **922.8 ms** | **-15%** |
| Reach elements/pass | ~47M | **~16M** | **-66%** |
| Tests | 15/15 | **15/15** | — |

Previous optimization stages:

### Stage A — ReachRef

Traversal now passes reach by reference:

```cpp
using ReachRef = std::array<const ComboVector*, 2>;
```

instead of copying complete reach objects.

### Stage B — Zero opponent copies

At physical decision nodes:

- only the acting player's reach is materialized
- opponent reach is inherited by reference
- opponent per-action copies are eliminated
- actor reach uses per-depth scratch
- correctness remains bit-exact

---

# Key Finding

The latest profiling answers the previous main question.

Why do approximately:

```text
16M reach elements/pass
```

still cost roughly:

```text
~923 ms aggregate CPU time
```

?

The following hypotheses have been tested and are **not** the dominant cause:

```text
sparse/scattered indexing       -> dense ~= indexed
scratch working-set size        -> single ~= multi-buffer
SIMD/vectorization              -> no material gain
```

Current conclusion:

> the remaining actor-reach kernel is largely limited by the intrinsic dependency and memory-access latency of the required accesses:
>
> strategy[action] + parent reach + actor reach destination

Local kernel tuning now has limited expected return.

Do not spend significant engineering time on further local tuning of:

```cpp
dst[i] = src[i] * strategy[i];
```

unless new evidence appears.

---

# Strategic Shift

The next optimization target is no longer:

> make the actor-reach kernel faster

The new target is:

> execute the actor-reach materialization fewer times, or eliminate intermediate materialization entirely where possible.

Priority now moves from local micro-optimization to **dataflow and traversal fusion**.

---

# Phase C — Eliminate Intermediate Reach Materialization

## Goal

Identify cases where a child reach vector is:

```text
written once
then read once
then discarded
```

These are prime candidates for elimination.

Current conceptual flow:

```cpp
for (action) {
    for (slot) {
        actor_reach[slot] =
            parent_reach[slot] * strategy[action][slot];
    }

    recurse(child, actor_reach);
}
```

The key question is:

> Does the child actually require a fully materialized actor reach array?

---

# Phase C.1 — Reach Lifetime Audit

Instrument and classify every actor-reach materialization.

For each materialized child reach, record:

```text
producer node type
consumer node type
number of downstream reads
whether it is transformed again
whether it reaches terminal immediately
whether it crosses a chance node
whether it is reused
lifetime depth
```

Build counts such as:

```text
Decision -> Terminal
Decision -> Chance -> Terminal
Decision -> Decision
Decision -> Fold terminal
Decision -> Showdown terminal
Decision -> Transform -> ...
```

Also record:

```text
actor reach vectors materialized/pass
bytes written/pass
bytes subsequently read/pass
average reads/materialization
```

---

# Important KPI

Add:

```text
materialized reach vectors/pass
materialized reach bytes/pass
consumer reads/materialization
```

A reach vector with:

```text
1 producer
1 consumer
0 reuse
```

is a strong candidate for fusion.

---

# Phase C.2 — Producer/Consumer Fusion

Look for paths equivalent to:

```text
Decision
  -> materialize reach
  -> immediate consumer
```

Example:

```cpp
actor_reach[i] =
    parent_reach[i] * strategy[i];

terminal_consume(actor_reach);
```

Potential fused form:

```cpp
terminal_consume(
    parent_reach,
    strategy
);
```

where the consumer computes:

```cpp
double reach =
    parent_reach[i] * strategy[i];
```

inline as part of the terminal computation.

This eliminates:

```text
actor_reach store
later actor_reach load
```

for that path.

---

# Important Constraint

Do not blindly replace stored reach with recomputation.

Fusion is attractive when the child reach is:

```text
computed once
consumed once
not reused
```

It may be harmful when the same reach value is used repeatedly.

For every fused path, compare:

```text
extra multiplies introduced
vs
loads/stores eliminated
```

---

# Candidate Reach Expression

If useful, introduce a lightweight compile-time representation.

Concept:

```cpp
struct MaterializedReach {
    const double* values;
};

struct ProductReach {
    const double* parent;
    const double* strategy;
};
```

Consumer access:

```cpp
inline double reach_at(
    const ProductReach& r,
    size_t i
) noexcept {
    return r.parent[i] * r.strategy[i];
}
```

Important:

- no virtual dispatch
- no heap allocation
- no `std::function`
- no reference counting
- fully inlineable
- avoid runtime polymorphism in the hot path

Templates or dedicated specialized functions are preferable.

---

# Phase C.3 — Decision -> Terminal Fusion

This should be tested first because it has the simplest lifetime.

Investigate separately:

```text
Decision -> Fold
Decision -> Showdown
Decision -> other terminal
```

## Fold

If fold EV does not require a full vector, avoid materialization entirely.

Example direction:

```cpp
fold_terminal(
    parent_reach,
    strategy,
    ...
);
```

instead of:

```cpp
materialize_actor_reach(...);
fold_terminal(actor_reach);
```

---

## Showdown

Current showdown already processes opponent reach into rank/card accumulators.

Test whether acting-player reach can remain lazy for hero-side use.

Possible pattern:

```cpp
hero_reach =
    parent_reach[hero_slot]
    * strategy[action][hero_slot];
```

inside the existing hero loop.

Do not change the showdown algorithm itself.

The goal is only to remove the intermediate reach vector where the downstream usage pattern permits it.

---

# Phase C.4 — Decision -> Chance -> Consumer Fusion

Inspect whether chance-node handling performs another transformation immediately after reach materialization.

Current potential pattern:

```text
Decision:
    dst = parent * strategy

Chance:
    dst2 = filter/transform(dst)

Child:
    consume dst2
```

Investigate combining:

```text
parent * strategy * chance-filter
```

into one pass.

Potential savings:

```text
remove dst store
remove dst load
remove second intermediate
```

Do this only when semantics and lifetime are clear.

---

# Phase D — Traversal Fusion

## Goal

Reduce the number of complete hand-array passes per edge.

Audit all hot-path sequences such as:

```text
reach propagation
board filtering
transform
child preparation
terminal accumulation
value update
```

Look for consecutive loops over the same hand domain.

Bad pattern:

```cpp
for (i)
    child_reach[i] = parent[i] * strategy[i];

for (i)
    transformed[i] = child_reach[i] * mask[i];

for (i)
    consume(transformed[i]);
```

Potential fused pattern:

```cpp
for (i) {
    const double r =
        parent[i]
        * strategy[i]
        * mask[i];

    consume(r);
}
```

---

# Loop Fusion Audit

For each hot node type, document:

```text
number of full hand-array passes
arrays read
arrays written
temporary arrays
```

Produce a table:

| Path | Current passes | Temporary arrays | Candidate fused passes |
|---|---:|---:|---:|
| Decision -> Showdown | ... | ... | ... |
| Decision -> Fold | ... | ... | ... |
| Decision -> Chance | ... | ... | ... |
| Decision -> Decision | ... | ... | ... |

Focus on eliminating complete array traversals.

---

# New Metric — Bytes per Edge

Add:

```text
bytes read / decision edge
bytes written / decision edge
full hand-array passes / edge
```

This is now more important than raw `reach elements/pass`.

The target is to reduce:

```text
memory operations per edge
```

not just optimize arithmetic.

---

# Phase D.1 — Child Reach Reuse Analysis

Not every child reach should be lazy.

Classify children into:

```text
single-use
multi-use
recursive/reused
terminal-only
chance-only
```

Use materialized reach only when it provides actual reuse.

A useful decision rule:

```text
if reach will be read multiple times:
    materialize
else:
    consider lazy/fused
```

Benchmark rather than relying on this rule blindly.

---

# Phase D.2 — Specialize Common Traversal Shapes

If profiling shows a few path shapes dominate runtime, create dedicated fast paths.

Examples:

```text
Decision -> Showdown
Decision -> Fold
Decision -> Decision(2 actions)
Decision -> Chance -> Showdown
```

Avoid routing all cases through a highly generic pipeline if specialization removes:

- temporary arrays
- branches
- metadata lookup
- function-call layers
- repeated range iteration

Correctness takes priority.

---

# Phase E — Terminal Showdown Micro-Optimization

## Current State

Terminal showdown is already algorithmically efficient.

Current approach:

```text
totals[rank]
by_card[card][rank]
prefix
card_prefix
hero blocker correction
```

Complexity:

```text
O(H + 36*R)
```

No O(H^2) path remains.

Historical observed cost:

```text
~27 us / terminal node
```

of which approximately:

```text
~10 us
```

comes from:

```text
fill/reset
prefix scans
```

Now that reach local optimizations are largely exhausted, showdown becomes a valid major target again.

---

# Phase E.1 — Exact Showdown Breakdown

Measure:

```text
clear/fill
opponent accumulation
total prefix
card prefix
hero blocker corrections
hero EV accumulation
stores/output
```

Report in:

```text
us / terminal
aggregate ms / pass
```

Example:

```text
Showdown
  fill/reset:              ... us
  opponent accumulation:  ... us
  total prefix:            ... us
  card prefix:             ... us
  hero correction:         ... us
  output:                  ... us
  total:                   ... us
```

---

# Phase E.2 — Reduce Reset Cost

Inspect all member arrays reset per terminal:

```text
totals
prefix
by_card
card_prefix
```

Determine:

```text
array size
bytes cleared
fraction actually touched
```

Benchmark alternatives where justified:

```text
smaller active rank domain
touched-rank lists
generation counters
partial clear
double-buffering
overwrite-without-clear
```

Important:

`std::fill_n` may already be optimal for small cache-resident arrays.

Do not replace it without measured improvement.

---

# Phase E.3 — Reduce Prefix Work

Ask:

> Are all 36 card-prefix scans required for every terminal?

Profile actual card usage.

If only a subset of cards can matter for the current flop-range/board state, test scanning only relevant cards.

Possible metadata:

```text
active_private_cards
active_rank_range
```

Precompute these per board/range where static.

---

# Phase E.4 — Precompute Static Showdown Metadata

The reach masses are dynamic.

But these may be static for a board:

```text
hand rank
rank bucket
card membership
sorted hand order
rank offsets
active rank min/max
active cards
hero correction indices
```

Ensure none of these are recomputed per terminal.

The terminal hot path should mostly perform:

```text
dynamic reach accumulation
prefix arithmetic
EV arithmetic
```

---

# Phase E.5 — Layout Audit

Profile current:

```cpp
by_card[card][rank]
card_prefix[card][rank]
```

against actual access patterns.

Do not change layout speculatively.

Use hardware counters or isolated benchmarks to test:

```text
card-major
rank-major
flattened contiguous
```

Keep whichever is measurably better.

---

# What NOT to Optimize Further Without New Evidence

The following paths have already shown low expected return:

```text
dense vs indexed actor reach
single vs multi scratch
manual SIMD of actor reach
local multiply kernel
dispatch pool
regret matching
```

Do not revisit them unless later architectural changes alter the profile.

---

# Updated Profiling Counters

Add the following:

## Traversal

```text
decision nodes/pass
actions/pass
terminal nodes/pass
chance nodes/pass
```

## Reach materialization

```text
actor reach materializations/pass
bytes materialized/pass
reads/materialization
single-use materializations
multi-use materializations
```

## Fusion

```text
Decision->Terminal fused count
Decision->Chance fused count
materializations eliminated/pass
bytes eliminated/pass
```

## Per-edge

```text
bytes read/edge
bytes written/edge
full array passes/edge
```

## Showdown

```text
us/terminal
clear us
prefix us
accumulate us
hero correction us
```

---

# Required Benchmark Table

Maintain:

| Version | Full run | Reach | Materializations/pass | Bytes materialized | Showdown | Correct |
|---|---:|---:|---:|---:|---:|---|
| Baseline | 164.0 s | ~1090 ms | ... | ... | ... | yes |
| Stage A+B | 145.8 s | 922.8 ms | ... | ... | ... | yes |
| C1 fusion | ... | ... | ... | ... | ... | yes |
| D fusion | ... | ... | ... | ... | ... | yes |
| E showdown | ... | ... | ... | ... | ... | yes |

---

# Correctness Requirements

All changes must preserve:

```text
dEV
canonical AhKhQh result
strategy
EV
convergence
test suite
```

Prefer bit-exact output whenever floating-point operation order is unchanged.

For fused/lazy paths, floating-point operation order may change.

If exact equality is no longer possible:

1. compare against the reference implementation
2. define a strict numerical tolerance
3. test many boards/ranges
4. verify convergence behavior
5. document the reason for non-bit-exact differences

Do not accept unexplained drift.

---

# Implementation Order

Proceed in this order:

```text
1. Instrument reach materialization lifetime
2. Count single-use vs multi-use reach vectors
3. Implement Decision -> Fold fusion
4. Implement Decision -> Showdown fusion
5. Re-profile
6. Inspect Decision -> Chance fusion
7. Audit full hand-array pass count per edge
8. Fuse proven consecutive passes
9. Re-profile
10. Micro-optimize showdown
11. Re-profile before touching other subsystems
```

---

# Primary Engineering Question

The previous phase answered:

> Why is the actor reach kernel itself expensive?

Answer:

> it is largely irreducible with local dense/SIMD/scratch tuning.

The new question is:

> Why are we materializing this actor reach array at all?

For every hot edge, determine whether the materialization is:

```text
necessary
reused
or merely an intermediate representation
```

The largest next speedups are expected from eliminating intermediate arrays and reducing complete hand-domain passes.

---

# Success Criteria

This phase is successful if it demonstrates measurable reduction in one or more of:

```text
actor reach materializations/pass
bytes written/pass
bytes read/pass
full array passes/edge
terminal us/node
```

while preserving solver correctness.

The current benchmark to beat is:

```text
145.8 s full run
```

The next target should be achieved through **less dataflow work**, not further tuning of the already-tested actor-reach kernel.
