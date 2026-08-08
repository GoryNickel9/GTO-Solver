# Task: Optimize CFR Solver Cost per Iteration

## Objective

Optimize the solver's **cost per CFR pass/iteration**.

Current benchmark:

- Our solver: **~164 s**
- GTO+ on an equivalent solve: **~17 s**
- Current gap: approximately **9.6×**
- Current wall time for one parallel pass: **~575 ms**

The priority is **not** to change convergence behavior or reduce the number of iterations artificially.  
The priority is to make **each iteration substantially cheaper while preserving correctness**.

Treat the existing codebase as the source of truth. Inspect the actual implementation before proposing changes.

---

## Current Hot-Path Profile

Profile of one smoke-test pass:

| Component | CPU time/pass | Approx. share |
|---|---:|---:|
| Terminal showdown | 1.1–1.4 s | ~47% |
| Reach propagation / copies | ~1.1 s | ~38% |
| Value + update (regret/strategy) | 0.35–0.6 s | ~13% |
| Board/card filtering | ~24 ms | ~1% |
| Synchronization / dispatch pool | ~0.4 ms | ~0% |
| Regret matching | ~2.3 ms | ~0% |
| Parallel wall time | ~575 ms | — |

Important:

- The component timings appear to be **aggregate CPU time across worker threads**.
- The wall-clock pass is ~575 ms.
- Roughly **85% of the cost is terminal showdown + reach propagation/copies**.
- Do **not** spend meaningful optimization effort on regret matching or dispatch synchronization at this stage.

---

# Primary Goal

Reduce the wall-clock cost of one pass as aggressively as possible.

Initial milestone:

- **575 ms/pass → <250 ms/pass**

Secondary milestone:

- **<150 ms/pass**

Long-term target:

- Move toward the performance class required to close the ~9.6× gap with GTO+.

Do not assume micro-optimizations alone can close the gap.  
Look first for cases where the solver is performing **unnecessary algorithmic work, memory traffic, repeated computations, or O(H²) operations**.

---

# Priority 1 — Terminal Showdown

Current share: **~47%**

This is the first major optimization target.

## Step 1: Profile the showdown internally

Add detailed instrumentation before changing the algorithm.

Measure at minimum:

- number of terminal showdown evaluations per pass
- hero hands processed
- villain hands processed
- total hero × villain pairs examined
- collision checks
- valid pair count
- rank comparisons
- hand-rank computations
- hand-rank lookups
- reach-vector reads
- EV writes
- time spent in:
  - rank evaluation / lookup
  - collision detection
  - opponent-hand traversal
  - win/loss/tie accumulation
  - payoff calculation
  - output write

Produce a report similar to:

```text
Terminal showdown
  calls/pass:                    ...
  hero hands/pass:               ...
  hero×villain pairs examined:   ...
  collision checks:              ...
  valid comparisons:             ...

  rank lookup:                   ... ms
  opponent traversal:            ... ms
  collision handling:            ... ms
  accumulation:                  ... ms
  EV writes:                     ... ms
```

Do not optimize blindly before these numbers are available.

---

## Step 2: Determine whether showdown is pairwise O(H²)

Look for patterns equivalent to:

```cpp
for (hero_hand : hero_range) {
    float ev = 0;

    for (villain_hand : villain_range) {
        if (cards_overlap(hero_hand, villain_hand))
            continue;

        int result = compare(hero_hand, villain_hand, board);
        ev += villain_reach[villain_hand] * payoff(result);
    }
}
```

If the current showdown performs a full hero × villain scan for most terminal nodes, flag it as a **major architectural bottleneck**.

Do not merely SIMD-vectorize an O(H²) implementation before evaluating whether the O(H²) work can be removed.

---

## Step 3: Investigate rank-bucket / cumulative-mass showdown

Evaluate replacing pairwise comparison with a structure based on already-known hand ranks.

Conceptually:

```text
hand -> showdown rank
```

For the opponent range, accumulate reach mass by rank:

```text
rank -> total opponent reach
```

Then derive cumulative masses:

```text
weaker_mass[rank]
equal_mass[rank]
stronger_mass[rank]
```

For each hero hand:

```text
win_mass
tie_mass
lose_mass
```

should ideally be obtainable without scanning every opponent hand.

Target complexity should move from approximately:

```text
O(H²)
```

toward:

```text
O(H log H)
```

or preferably:

```text
O(H + R)
```

where `R` is the number of relevant rank buckets.

---

## Step 4: Handle card blockers efficiently

A naive cumulative-rank implementation is insufficient because hero and villain hands may share cards.

Do not fall back to scanning all villain hands just to correct blockers.

Investigate blocker-aware cumulative data such as:

```text
total_reach_by_rank
reach_by_card_and_rank[card][rank]
```

For a hero hand containing cards `c1` and `c2`, derive valid opponent mass using aggregate subtraction rather than pairwise checks.

Conceptual form:

```text
valid_mass
    = total_mass
    - mass_containing(c1)
    - mass_containing(c2)
    + required overlap correction
```

Implement the exact correction required by the project's hand representation and game rules.

Important:

- correctness is mandatory
- explicitly test double-counting corrections
- test ties
- test board blockers
- test duplicate rank buckets
- test impossible/invalid hands

---

## Step 5: Precompute everything static

During the solve, terminal showdown should not repeatedly recompute information that is static for a board.

Inspect whether any of these are recomputed unnecessarily:

- hand card masks
- valid hand lists
- showdown hand ranks
- sorted order by rank
- rank bucket offsets
- card membership
- collision metadata
- board masks
- payoff constants
- range-local hand mapping

Where possible, build compact lookup data once when the board/tree is created.

The hot terminal loop should contain as little control logic as possible.

---

# Priority 2 — Reach Propagation / Copies

Current share: **~38%**

This is the second major optimization target.

The profiling label explicitly mentions **copies**, so investigate memory traffic before arithmetic.

---

## Step 1: Instrument reach propagation

Measure:

- reach vectors created per pass
- reach vectors copied per pass
- reach vector elements processed
- total bytes read per pass
- total bytes written per pass
- total bytes copied per pass
- number of `memcpy` calls
- number of temporary allocations
- number of vector resizes
- time spent in:
  - copy
  - multiply
  - allocation
  - filtering
  - traversal setup

Produce something similar to:

```text
Reach propagation
  vectors created/pass:        ...
  vectors copied/pass:         ...
  elements processed/pass:     ...
  bytes copied/pass:           ...
  bytes written/pass:          ...
  temporary allocations/pass:  ...

  memcpy/copy:                 ... ms
  reach × strategy:            ... ms
  allocation/setup:            ... ms
```

---

## Step 2: Eliminate copy-then-multiply

Search for patterns equivalent to:

```cpp
child_reach = parent_reach;

for (int i = 0; i < n; ++i)
    child_reach[i] *= strategy[i];
```

or:

```cpp
memcpy(child_reach, parent_reach, bytes);

for (...)
    child_reach[i] *= strategy[i];
```

Replace, where valid, with a single fused pass:

```cpp
for (int i = 0; i < n; ++i)
    child_reach[i] = parent_reach[i] * strategy[i];
```

This reduces memory traffic from approximately:

```text
read parent
write child
read child
read strategy
write child
```

to:

```text
read parent
read strategy
write child
```

Do not optimize `memcpy` if the copy itself can be removed.

---

## Step 3: Avoid materializing reach vectors when possible

Inspect whether every child truly needs a complete materialized reach vector.

Potential approaches to evaluate:

- fused propagation + child consumption
- scratch buffers reused per recursion depth
- thread-local workspaces
- alternating/ping-pong buffers
- streaming values directly into the next computation
- partial reach representation when only a subset is needed

Avoid introducing excessive recomputation to save memory traffic. Benchmark both versions.

---

## Step 4: Zero allocations in the hot path

The steady-state iteration path should ideally perform:

```text
0 malloc
0 free
0 new/delete
0 std::vector resize
0 hash-table insertion
0 temporary container construction
```

Search specifically for:

```cpp
std::vector<float> x = ...
std::vector<float> x(n)
auto copy = existing_vector
resize(...)
reserve(...)
make_shared(...)
make_unique(...)
```

inside traversal code.

Use preallocated workspaces.

Prefer thread-local or worker-local reusable buffers when synchronization is unnecessary.

---

# Priority 3 — Value + Regret/Strategy Update

Current share: **~13%**

Do not work here until the first two hotspots have been addressed or proven irreducible.

Still inspect for:

- repeated strategy normalization
- redundant writes
- temporary EV arrays
- unnecessary action loops
- repeated node metadata lookup
- non-contiguous access
- avoidable conversions between float/double

Potential optimizations:

- specialize 2-action nodes
- specialize 3-action nodes
- hoist invariant branches outside hand loops
- fuse EV calculation and regret update
- avoid storing intermediate values not used later
- vectorize large contiguous loops

---

# Memory Layout Audit

Perform a dedicated memory-layout review.

Identify hot structures containing:

- regrets
- strategy sums
- current strategy
- reach values
- node EV
- action EV
- hand ranks
- validity/blocker metadata

Look for:

- nested `std::vector`
- pointer-heavy object graphs
- per-node heap allocations
- `shared_ptr`
- virtual dispatch
- `unordered_map`
- `map`
- AoS layouts that prevent vectorization
- poor alignment
- large structs with hot and cold fields mixed together

For hot numeric arrays, evaluate contiguous storage and SoA layouts.

Example direction:

```cpp
struct NodeMeta {
    uint32_t child_offset;
    uint32_t data_offset;
    uint8_t action_count;
    uint8_t type;
};
```

with separate contiguous arrays for numeric data.

Do not refactor blindly. Benchmark memory layout changes.

---

# Branch Audit

Find branches inside loops over hands.

Bad pattern:

```cpp
for (int h = 0; h < hand_count; ++h) {
    if (action_count == 2) ...
    if (player == OOP) ...
    if (node_type == ...) ...
}
```

Prefer:

```cpp
if (action_count == 2)
    process_two_action_node(...);
else if (action_count == 3)
    process_three_action_node(...);
```

with a clean numeric loop inside.

Hoist invariant conditions out of the per-hand loop.

---

# SIMD / Compiler Vectorization

SIMD is important, but only after unnecessary work and memory traffic are reduced.

For candidate loops:

- ensure contiguous arrays
- remove aliasing ambiguity where valid
- inspect alignment
- reduce branches
- reduce dependencies between iterations
- use `restrict`-equivalent semantics where safe
- inspect compiler vectorization reports

Build an optimized benchmark with appropriate release flags, for example where supported:

```text
-O3
-march=native
```

Do not assume auto-vectorization happened. Verify generated optimization reports or assembly for important loops.

Possible targets:

- reach × strategy
- regret updates
- EV accumulation
- cumulative rank mass construction
- blocker corrections
- array initialization

Do not introduce explicit AVX2/AVX-512 intrinsics unless profiling shows the compiler is failing or the explicit implementation benchmarks better.

---

# Multithreading

Synchronization currently appears negligible:

```text
dispatch pool ~0.4 ms
```

Therefore thread dispatch is not currently a priority.

However, measure scaling after major memory changes.

Benchmark:

```text
1 thread
2 threads
4 threads
8 threads
all physical cores
```

Record:

```text
wall time/pass
speedup
parallel efficiency
```

Poor scaling after memory-layout improvements may indicate:

- memory-bandwidth saturation
- false sharing
- NUMA effects
- work imbalance
- shared hot counters
- cache-line contention

Avoid using additional threads to hide poor single-thread efficiency.

---

# Benchmarking Rules

Every optimization must be benchmarked using the same fixed scenario.

For each change record:

```text
baseline commit
test commit

wall ms/pass
CPU time/pass
terminal showdown ms
reach propagation ms
value/update ms

iterations/sec
showdown calls/pass
hero×villain pairs/pass
reach bytes copied/pass
reach bytes written/pass

1-thread result
full-thread result
```

Run enough repetitions to avoid noise.

Report:

- median
- minimum
- maximum
- preferably p95 if enough samples are collected

Do not report a speedup from a single run.

---

# Correctness Requirements

Performance changes must preserve solver correctness.

For every major optimization, compare against the current implementation.

Test at minimum:

- strategy outputs within expected numerical tolerance
- regrets within expected tolerance
- EVs within expected tolerance
- exploitability/convergence behavior
- deterministic test cases where applicable
- terminal showdown output hand-by-hand
- blocker handling
- ties
- zero-reach hands
- invalid hands
- different action counts
- different boards
- different range sizes

For a new showdown algorithm, keep the old implementation temporarily as a reference implementation.

Example test strategy:

```text
new_showdown(...)
vs
reference_pairwise_showdown(...)
```

Run thousands of randomized valid scenarios and fail on divergence beyond the chosen tolerance.

Only remove the reference path after validation is complete.

---

# Do Not Do Yet

Do not spend substantial time optimizing:

- regret matching (~2.3 ms)
- dispatch synchronization (~0.4 ms)
- board/card filtering (~24 ms)

unless new profiling after the major changes shows their relative importance has increased materially.

Do not start with:

- cosmetic refactors
- broad architecture rewrites unrelated to the hotspots
- GPU work
- database work
- UI work
- serialization
- logging optimizations outside hot paths

---

# Required Execution Plan

## Phase A — Measurement

1. Inspect the existing implementation.
2. Map the full call path for one CFR pass.
3. Add detailed showdown profiling.
4. Add detailed reach-propagation profiling.
5. Count pairwise showdown operations.
6. Count bytes moved for reach propagation.
7. Establish single-thread and full-thread baselines.

Deliver findings before large refactors.

---

## Phase B — Low-risk hot-path fixes

Implement and benchmark:

1. remove unnecessary reach copies
2. fuse copy + multiply
3. reuse preallocated scratch memory
4. remove hot-path allocations
5. hoist invariant branches
6. remove repeated static metadata calculations
7. ensure release compiler configuration is correct

Run correctness suite.

---

## Phase C — Showdown algorithm

If showdown is pairwise/O(H²):

1. build a reference test harness
2. precompute hand ranks
3. implement rank buckets / cumulative reach
4. implement blocker-aware corrections
5. compare against reference implementation
6. benchmark independently
7. integrate into CFR traversal
8. profile again

This phase is expected to offer one of the largest possible gains.

---

## Phase D — Data layout and SIMD

After algorithmic improvements:

1. identify remaining memory-bound loops
2. improve contiguous storage
3. evaluate SoA where beneficial
4. specialize common action counts
5. confirm compiler vectorization
6. add explicit SIMD only when justified by benchmark data

---

## Phase E — Parallel scaling

After single-thread performance is substantially improved:

1. benchmark thread scaling
2. inspect memory bandwidth
3. inspect false sharing
4. inspect load balance
5. optimize only proven parallel bottlenecks

---

# Expected Deliverables

Create/update engineering notes containing:

## 1. Baseline

```text
wall time/pass:
CPU time/pass:
iterations/sec:

showdown:
reach:
value/update:
other:
```

## 2. Hotspot findings

For each hotspot:

```text
problem
evidence
root cause
proposed solution
risk
expected impact
```

## 3. Changes implemented

For each optimization:

```text
files changed
what changed
why
correctness implications
benchmark before
benchmark after
speedup
```

## 4. Final comparison

Use a table:

| Version | Wall/pass | Showdown | Reach | Value/update | Relative speed |
|---|---:|---:|---:|---:|---:|
| Baseline | ~575 ms | ... | ... | ... | 1.00× |
| Change 1 | ... | ... | ... | ... | ... |
| Change 2 | ... | ... | ... | ... | ... |
| Final | ... | ... | ... | ... | ... |

---

# Decision Principle

The central question for every optimization is:

> Can we eliminate this work instead of making the same work slightly faster?

Priority order:

```text
1. eliminate unnecessary algorithmic work
2. eliminate memory copies / memory traffic
3. precompute invariants
4. improve layout/cache locality
5. fuse loops
6. reduce branches
7. vectorize
8. improve multithreading
```

The current profile strongly suggests that the largest gains will come from:

1. **reworking terminal showdown**
2. **removing reach-vector copies and excessive memory traffic**

Do not optimize components that currently account for negligible runtime merely because they are easy to modify.

---

# Final Success Criteria

A successful optimization pass should achieve all of the following:

- solver results remain numerically correct
- no convergence regression
- no hidden increase in iterations required
- no hot-path heap allocation
- substantially fewer reach bytes copied
- substantially fewer showdown pair comparisons if currently O(H²)
- wall time per pass below the initial ~575 ms baseline
- detailed benchmark evidence for each important change

Aim first for **<250 ms/pass**, then reassess the new profile before choosing the next optimization target.
