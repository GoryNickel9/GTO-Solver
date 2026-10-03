# GTOSD

A Short Deck poker solver in C++20 for Windows and MSVC. It is one product with two engines: a
preflop blueprint solver for heads-up and 3-way play, and an exact heads-up postflop solver.

Solving uses only the CPU and system RAM. GPU, CUDA, ROCm, OpenCL, Vulkan Compute, DirectCompute
and other accelerators are never used for tree building, CFR traversal, best response,
certification or post-processing of a solution. This is a permanent product decision, recorded in
[`docs/specifications/LIMITATIONS.md`](docs/specifications/LIMITATIONS.md).

## Engines

### Preflop blueprint: `libs/preflop_blueprint` and `libs/card_abstraction`

This is the current line of work: reproducing the MonkerSolver preflop recipe, heads-up first and
then multiway. The solver runs vector CFR over hand classes and combos, sampling the public cards.
Its command-line tools are built from `benchmarks/preflop_blueprint_*.cpp`. The game
configurations are in `benchmarks/monker/` (for example `HU50_step2.json` and
`3WAY50_donk_rake25cap2.json`) and in `benchmarks/fixtures/`.

- **Step 1, checkdown preflop.** The postflop is empty: every pot that reaches the flop is checked
  down to the river. `gtosd_preflop_blueprint_checkdown` solves heads-up at the combo level, with
  no card abstraction. `gtosd_preflop_blueprint_checkdown_classes` solves 2 or 3 seats at the class level.
- **Step 2, preflop with a sparse abstracted postflop.** `gtosd_preflop_blueprint_train` trains
  heads-up and 3-seat games. The postflop uses bucket tables (`gtosd_preflop_blueprint_buckets`,
  `gtosd_preflop_blueprint_monker_buckets`) and board-class rows. The heads-up reference is HU50
  with board-class rows.
- **Charts and evaluation.**
  - Charts are written in the MonkerSolver text format, by the trainer (`--chart-every`) or by
    `gtosd_preflop_blueprint_monker_charts`.
  - `gtosd_preflop_blueprint_monker_values` is heads-up only. It computes the best response on the
    real cards and plays MonkerSolver charts inside our game.
  - `gtosd_preflop_blueprint_policy_values` gives the per-seat values of a fixed policy, for 2 or
    3 seats.
- **Scope.** 3-way does not have to be exact: sampling and abstraction are allowed. Games with 4
  to 6 players are not implemented yet.

The preflop blueprint must not link the postflop libraries.
`tests/verify_preflop_blueprint_isolation.cmake` enforces this through the CTest
`gtosd_preflop_blueprint_dependency_check`.

### Exact heads-up postflop: `gto_cli`

`apps/gto_cli` uses the libraries `core`, `equity`, `tree`, `isomorphism`, `solver`,
`best_response`, `memory`, `postflop` and `storage`. It enumerates the configured discretized game
without sampling or bucketing. It solves with the `ProductionDcfr` profile, certifies the solution
with an exact best response, and writes checkpoints and authenticated `.gtsd` archives. Run
`gto_cli` with no arguments to print its commands. Its contract is in
[`docs/specifications/`](docs/specifications/README.md). It is also the heads-up postflop engine of
the web UI.

The external-sampling preflop code in `libs/preflop` and `benchmarks/hu_preflop_*.cpp` is still
built and tested. That program closed on 2026-09-15
([`docs/archive/preflop-es-2026-09/`](docs/archive/preflop-es-2026-09/README.md)).

## Build

Bootstrap the pinned vcpkg baseline and expose its root:

```powershell
$env:VCPKG_ROOT = "C:\path\to\vcpkg"
& "$env:VCPKG_ROOT\bootstrap-vcpkg.bat" -disableMetrics
```

Then, from a Visual Studio Developer PowerShell with Ninja available:

```powershell
$env:GTOSD_NINJA_EXE = (Get-Command ninja).Source
cmake --preset windows-release
cmake --build --preset windows-release
```

`GTOSD_NINJA_EXE` must hold Ninja's absolute path. This keeps the vcpkg toolchain search root from
interfering with tool discovery in recent CMake versions. The presets are `windows-debug`,
`windows-release` and `windows-asan` (`CMakePresets.json`). Warnings are errors (`/W4 /WX`). The
option `GTOSD_BUILD_PREFLOP_BLUEPRINT` (default `ON`) builds the preflop blueprint, and
`cmake --build --preset windows-release --target format-check` runs clang-format when it is
installed.

The root `install()` rules still list four schema files that are not in `schemas/`
(`postflop_tree_config`, `gto_plus_reference`, `gto_plus_convergence_benchmark` and its `v2`).
`cmake --install` should therefore fail until that list is fixed. This has not been run.

## Tests

```powershell
ctest --preset windows-release
```

Many preflop blueprint tests read precomputed resources from `out/preflop_blueprint_resources` and
the 200/500/1000 bucket tables from `out/preflop_blueprint_buckets_200_500_1000`. If those files are
missing, the tests skip themselves with exit code 77. Build the resources once, from the
repository root (commands from the P2 and P3 reports and from the MonkerSolver recipe):

```powershell
$b = ".\out\build\windows-release\benchmarks"
& "$b\gtosd_preflop_blueprint_resources.exe" --threads 8 --verify-oracle 200000 `
  --output-dir out/preflop_blueprint_resources
& "$b\gtosd_preflop_blueprint_buckets.exe" --resources-dir out/preflop_blueprint_resources `
  --output-dir out/preflop_blueprint_buckets_200_500_1000 --threads 8 --flop 200 --turn 500 `
  --river 1000 --restarts 10 --screening-iterations 10 --max-iterations 25 --screening-sample 500000
& "$b\gtosd_preflop_blueprint_three_way_table.exe" --resources-dir out/preflop_blueprint_resources `
  --hero-classes all --threads 3 --output out/preflop_blueprint_resources/preflop_three_way_v1.bin
```

Useful subsets, by CTest label:

```powershell
# preflop blueprint and card abstraction, without the long 3-way entry
ctest --preset windows-release -L "preflop_blueprint|card_abstraction" -LE phase3_long
# gto_cli, the postflop libraries and the rest of the legacy suite
ctest --preset windows-release -LE "preflop_blueprint|card_abstraction|research|monker|monker_exact|phase3_long|nightly|slow"
```

A plain run includes two long entries:

- `phase3_long`, about 30-60 minutes at 2 threads;
- `monker_exact`, about 13 minutes on 3 threads.

Run them on purpose, or leave them out with `-LE`. The correctness battery of the HU50 step-2
path is described in [`benchmarks/monker/correctness/README.md`](benchmarks/monker/correctness/README.md).
The independent references, including the 3-way referee, are described in
[`tools/independent/README.md`](tools/independent/README.md).

## User interface

The desktop Qt GUI was removed on 2026-10-02. Its replacement is a web UI that launches the frozen
solver executables. It is developed in `apps/solver-ui` on the branch `feat/solver-ui`, which is not
part of this branch. Its specification is
[`docs/solver-ui/WEB_UI_PROTOTYPE_PROMPT.md`](docs/solver-ui/WEB_UI_PROTOTYPE_PROMPT.md), with the
addendum [`WEB_UI_ADDENDUM_ENGINES_2026-10-02.md`](docs/solver-ui/WEB_UI_ADDENDUM_ENGINES_2026-10-02.md).

## Documentation

- Current work and next steps:
  [`docs/handoff/NEXT_STEPS_2026-10-02.md`](docs/handoff/NEXT_STEPS_2026-10-02.md).
- The diary, with the state table, the gate register and the decisions taken since 21/09:
  [`docs/research/preflop_vector_cfr/PROGRESS_LOG.md`](docs/research/preflop_vector_cfr/PROGRESS_LOG.md).
- The MonkerSolver recipe:
  [`MONKER_RECIPE_REPRODUCTION_2026-09-28.md`](docs/research/preflop_vector_cfr/MONKER_RECIPE_REPRODUCTION_2026-09-28.md).
- The 3-way specifications: [`docs/research/preflop_vector_cfr/threeway/`](docs/research/preflop_vector_cfr/threeway/).
- The module reports of the preflop blueprint (`P0`-`P8`) and the RAM and time work
  (`MEMORY_TIME_OPTIMIZATION_2026-09-21.md`), in the same folder.
- The design decisions up to 21/09:
  [`docs/research/PREFLOP_ARCHITECTURE_DECISION_LOG.md`](docs/research/PREFLOP_ARCHITECTURE_DECISION_LOG.md).
  The log is frozen; later decisions are in the diary.
- The `gto_cli` contract: [`docs/specifications/`](docs/specifications/README.md).
- Also in `docs/`:
  - the postflop ADRs (`ADR_0002`, `ADR_0003`);
  - the error and versioning policy;
  - the GTO+ convergence benchmark and its guide;
  - the GTO+ memory semantics;
  - the GTO+ parity journey;
  - the two postflop optimization plans of 2026-09-10.
- The archive, by era, in [`docs/archive/`](docs/archive/):
  - `legacy-postflop-2026-07-09/`: the F0-F10 postflop and GTO+ program, including the old
    roadmap, the phase reports and the GUI documents;
  - `preflop-es-2026-09/`: the external-sampling preflop;
  - `preflop-blueprint-research-2026-09/`: the P0-P10 roadmap and P9;
  - `history7-suite-2026-09/`: history7 and the HU10-HU40 suite;
  - `legacy-memory-gate/`: the false memory gate.

  Each folder has a README. Files deleted in the cleanup of 2026-10-03 are at the tag
  `docs-pre-cleanup-2026-10-02`. The last tree with history7 is the tag `history7-final`.
- Third-party licenses: [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
