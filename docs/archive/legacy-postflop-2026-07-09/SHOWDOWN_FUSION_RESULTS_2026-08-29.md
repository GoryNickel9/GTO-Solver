# Showdown Fusion Results — 2026-08-29

## Decision

The exact rank-base fusion candidate is **rejected**. It did not meet the
standalone kernel gate, so it was not integrated into the production solver.
No solver smoke, full TH7D6S run, full TSTC9D run, or five-process
certification was warranted.

This is outcome C from `NEXT_SHOWDOWN_OPTIMIZATION_CROSS_BENCHMARK.md`: the
experiment and its rejection are recorded, while the production showdown path
remains unchanged.

## Scope and invariant

The experiment targeted only the `PlayerIndexed<float>` showdown dataflow.
The baseline constructs the scalar/card prefixes and then makes a separate
rank pass to construct `rank_base`. The candidate:

1. reproduces the final total reach with a rank-ordered sum;
2. constructs the scalar/card prefixes in the original order;
3. constructs each `rank_base` cell in the same prefix iteration.

The candidate preserves the payoff expression and floating-point operation
order for every result. The benchmark constructor executes both kernels on all
three workloads and compares every output by its exact IEEE-754 bit pattern;
the process aborts on any difference.

No cache, SIMD-first rewrite, persistent scale, liveness metadata, signed
codec, chance scheduling, or fixture-specific branch is part of this change.

## Reproducible benchmark

Target: `gtosd_showdown_fusion_benchmark`

Build and host:

- source base: `36c971ad8d45264270fad1262d4ae4c1d1ea5924`;
- build: `windows-release-current`, Release, MSVC 19.51.36231;
- host: Windows, 8 logical CPUs reported at 3.6 GHz;
- cache: 4 x 32 KiB L1D, 4 x 256 KiB L2, 6 MiB shared L3;
- Google Benchmark minimum time: 1 second per workload;
- process order: B, C, B, C, B, C;
- reported decision metric: median CPU nanoseconds per showdown;
- wall-clock nanoseconds are retained as a secondary check.

The deterministic synthetic shapes exercise the same accumulation, rank/card
prefix, blocker correction, output, and touched-cell reset dataflow:

| Workload | Rank cells | Hero hands | Opponent hands | Touched rank/card cells |
|---|---:|---:|---:|---:|
| small | 24 | 48 | 48 | 94 |
| medium | 80 | 220 | 220 | 440 |
| large | 220 | 500 | 500 | 911 |

The estimated traffic counter falls from 53,200 to 52,560 bytes per showdown
on medium and from 137,964 to 136,204 on large. This estimate intentionally
does not claim hardware-counter accuracy; it describes the explicit array
traffic modeled by the benchmark.

## Raw B/C results

CPU nanoseconds per showdown:

| Pair | small B | small C | medium B | medium C | large B | large C |
|---|---:|---:|---:|---:|---:|---:|
| 1 | 675 | 691 | 2,490 | 2,579 | 6,649 | 6,984 |
| 2 | 659 | 652 | 2,511 | 2,483 | 7,673 | 7,673 |
| 3 | 652 | 668 | 2,483 | 2,520 | 6,519 | 6,676 |
| median | **659** | **668** | **2,490** | **2,520** | **6,649** | **6,984** |

Median candidate change, where positive means faster:

| Workload | CPU change | Wall-clock change |
|---|---:|---:|
| small | -1.37% | +1.04% |
| medium | -1.20% | -0.59% |
| large | -5.04% | -2.98% |

The isolated preliminary run showed apparent improvements on medium and large,
but those did not survive the required alternating-process repetitions. This
is why a single favorable sample is not used for promotion.

## Gate evaluation

The plan requires at least 10% improvement on both medium and large before a
production integration. The measured medians are regressions on both. The
candidate therefore fails Phase A and is rejected before Phase B.

Consequences:

- `libs/postflop/src/postflop_solver.cpp` is unchanged;
- solver fingerprints, dEV, BR, root EV, state bytes, and traversal behavior
  are unchanged by construction;
- the existing TSTC9D full-run reference remains 153.176351 seconds elapsed,
  147.063172 seconds traversal, 0.985759519% dEV, root EV 8.492540035,
  1,968,537,600 peak RSS, and 1,472,605,376 solver-state bytes;
- the 128.988889-second TSTC9D promotion gate was not re-run because the
  kernel prerequisite failed.

## Validation

- Release compilation of the standalone benchmark: pass.
- Exact output parity across small, medium, and large fixtures: pass (enforced
  by the executable before timing).
- Alternating B/C/B/C/B/C benchmark sequence: complete.
- Full Release CTest suite after benchmark integration: 19/19 pass in 188.22
  seconds.
- Production integration: intentionally not performed.
- Full solver correctness and performance suite: intentionally not performed;
  the plan stops before those phases when the kernel gate fails.

## Next action

Profile a different measured showdown dataflow family that can remove traffic
from the per-hero blocker/output path; do not revisit rank-base fusion without
new evidence that changes its cost model.
