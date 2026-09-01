# GTOSD

Exact Short Deck Heads-Up solver foundation following
`docs/ROADMAP_HU_SHORT_DECK_GTO_SOLVER.md`.

I contratti tecnici correnti sono raccolti nell'indice
[`docs/specifications/README.md`](docs/specifications/README.md). Roadmap e
report di fase conservano ordine dei gate ed evidenza storica.

Il calcolo del solver è **CPU + RAM only**. GPU, CUDA, ROCm, OpenCL, Vulkan
Compute, DirectCompute e altri acceleratori non possono essere usati per tree
building, traversal CFR, best response, certificazione o post-processing della
soluzione. Un'eventuale accelerazione grafica della GUI riguarda soltanto il
rendering e non partecipa mai al solve.

## Build

Bootstrap the pinned vcpkg baseline and expose its root:

```powershell
$env:VCPKG_ROOT = "C:\path\to\vcpkg"
& "$env:VCPKG_ROOT\bootstrap-vcpkg.bat" -disableMetrics
```

From a Visual Studio Developer PowerShell with Ninja available:

```powershell
$env:GTOSD_NINJA_EXE = (Get-Command ninja).Source
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
cmake --install out/build/windows-release --config Release `
  --prefix out/install/windows-release
```

`GTOSD_NINJA_EXE` must contain Ninja's absolute path. This prevents the vcpkg
toolchain search root from interfering with tool discovery in recent CMake
versions.

Phase 0 is complete: the pinned build, Debug/Release/ASan presets, framework
tests, benchmark runner, static checks, CI matrix and install tree are tracked
in `docs/IMPLEMENTATION_STATUS.md`.

Phase 1 is complete: cards, fixed-point chip arithmetic, CO/BTN preflop
posting, legal actions, street closure, uncalled returns, rake, split pots and
terminal payoff accounting are covered by the full F1 rules suite. See
`docs/PHASE_1_COMPLETION_REPORT.md`.

Phase 2 is complete: the first-party exact Short Deck evaluator, typed
`IHandEvaluator` adapter, scalar/batch API, exact 2–6 player showdown,
independent exhaustive oracle and deterministic million-deal nightly are
tracked in `docs/IMPLEMENTATION_STATUS.md`.

Phase 3 is complete: `gtosd::tree` builds and preflights the physical
flop–turn–river public tree, enumerates all legal board runouts, supports
CO/BTN scenario sizing and all-in runouts, resolves exact showdown terminals,
and exposes a deterministic inspector:

```powershell
.\out\build\windows-release\apps\gto_cli\gto_cli.exe `
  tree-inspect tests\fixtures\postflop_check_only.json
```

Phase 4 is complete: `gtosd::isomorphism` canonicalizes board, physical
private deals, weighted ranges, dead/future cards and nodelocks under one
global suit permutation. It retains inverse mappings and exact physical
chance multiplicities. Audit all 24 representatives with:

```powershell
.\out\build\windows-release\apps\gto_cli\gto_cli.exe `
  isomorphism-audit tests\fixtures\postflop_check_only.json
```

Phase 5 is complete locally: `gtosd::solver` and `gtosd::best_response`
provide finite extensive-form reference games, Vanilla CFR, CFR+, Linear CFR,
DCFR, laboratory MCCFR, exact infoset-aware best response, NashConv,
checkpoint/resume and convergence curves:

```powershell
.\out\build\windows-release\apps\gto_cli\gto_cli.exe `
  solver-lab kuhn cfr+ 20000 1 1
```

Phase 6 is complete locally: `gtosd::memory` compares lazy in-RAM, street
decomposition and memory-mapped out-of-core layouts on the versioned
PF-F1/PF-F2/PF-F3 fixtures:

```powershell
.\out\build\windows-release\apps\gto_cli\gto_cli.exe `
  memory-lab pf-f1 lazy

.\out\build\windows-release\apps\gto_cli\gto_cli.exe `
  memory-probe pf-f1 .\out\pf-f1-probe.bin
```

Phase 7 is complete locally: `gtosd::postflop` integrates exact physical-combo
CFR+, periodic best response/NashConv certification, atomic checkpoint/resume,
pause/cancel controls, strategy queries, Markdown/JSON reports and a paged
out-of-core fallback. PF-F1 reached `NashConv / pot = 0.741405%` at iteration
125 and passes the `<1%` gate.

```powershell
.\out\build\windows-release\apps\gto_cli\gto_cli.exe `
  postflop benchmark-config pf-f1 .\out\pf-f1.json

.\out\build\windows-release\apps\gto_cli\gto_cli.exe `
  postflop solve .\out\pf-f1.json 125 .\out\pf-f1.chk `
  .\out\pf-f1-report 12 8 25

.\out\build\windows-release\apps\gto_cli\gto_cli.exe `
  postflop certify .\out\pf-f1.json .\out\pf-f1.chk
```

The claim is scoped to the versioned PF-F1 configuration and is backed by
exact best response, not profile EV alone. See
`docs/PHASE_7_COMPLETION_REPORT.md`.

Phase 8 is complete locally: `gtosd::storage` adds the durable `.gtsd` 1.0
container, per-chunk Zstandard compression, independent authenticated
XChaCha20-Poly1305 secretstreams, bounded-memory random access, atomic save,
non-destructive migration, a verification tool and an external SQLite catalog.
The encryption key is supplied by the license layer; `.gtsd` never derives a
key from a user password.

```powershell
$key = .\out\build\windows-release\apps\gto_cli\gto_cli.exe storage keygen

.\out\build\windows-release\apps\gto_cli\gto_cli.exe `
  storage pack .\out\pf-f1.json .\out\pf-f1.chk .\out\pf-f1.gtsd $key

.\out\build\windows-release\apps\gto_cli\gto_cli.exe `
  storage verify .\out\pf-f1.gtsd $key

.\out\build\windows-release\apps\gto_cli\gto_cli.exe `
  storage query .\out\pf-f1.gtsd $key 0 100
```

The F8 storage benchmark used the full PF-F1 topology at one iteration:
1,068,121,299 logical bytes became a 5,618,173-byte encrypted file, and its
root index opened with 676 bytes of metadata. This measures the format, not
convergence; the F7 125-iteration certification remains authoritative. See
`docs/PHASE_8_COMPLETION_REPORT.md`.

Phase 9 is complete locally: Qt 6 Widgets and Dear ImGui docking prototypes
share a 100,000-node virtual tree, the exact Short Deck 9×9 matrix, lazy
authenticated `.gtsd` opening and ten automated workflows. Qt 6 Widgets is
selected for the F10 product GUI by `docs/ADR_0001_GUI_FRAMEWORK.md`.

```powershell
cmake --preset windows-gui-release
cmake --build --preset windows-gui-release
ctest --preset windows-gui-release -L phase9
powershell -ExecutionPolicy Bypass -File .\tools\run_f9_benchmarks.ps1
powershell -ExecutionPolicy Bypass -File .\tools\verify_f9_install.ps1
```

On the measured four-core i3-10100F host, Qt raster reached 238.95 FPS,
Dear ImGui DX11 4,362.19 FPS and forced WARP 62.20 FPS; all p95 frame times
were below 16.666667 ms. This is a local four-physical-core result, not an
emulation of a 2 GHz / 16 GB machine. See
`docs/PHASE_9_COMPLETION_REPORT.md`.

Phase 10 is complete locally: `gto_gui` is the Qt 6 product application. It
connects weighted physical CO/BTN ranges to exact CFR+/BR, performs resource
preflight, solves on a worker thread, writes authenticated crash-recovery
checkpoints, saves/opens `.gtsd`, and navigates real strategy data by 9×9 hand
class and physical combo. Configuration is visual: starting pot/stack/rake,
separate CO/OOP and BTN/IP betting panels, street overrides and a visual
three-to-five-card Short Deck board picker; the versioned JSON remains an
internal persistence/API format. The interactive solve target is GTO+-style
maximum unilateral deviation gain divided by pot (default 1%); NashConv/Pot is
shown separately. Certification runs every 20 iterations and at solve end. RAM, disk and backing mode
are selected automatically by preflight. Range classes start at 0% and are
painted directly with click/drag or a percentage slider. Pause and cancel
remain available during solving; local solution keys are managed transparently
and diagnostic logs are available from the application toolbar.

```powershell
cmake --preset windows-gui-release
cmake --build --preset windows-gui-release --target gto_gui
ctest --preset windows-gui-release -L phase10
.\out\build\windows-gui-release\apps\gto_gui\gto_gui.exe
```

The automated product workflow is
`create → solve → save → reopen → navigate → resume`. On the measured
i3-10100F host, the installed application completed the reduced E2E fixture in
1.937 s with a 104.239.104-byte peak RSS and a 12,0331 ms maximum UI heartbeat
gap during solving. These values validate integration and responsiveness, not
convergence or the exact 2 GHz / 16 GB release target. See
`docs/PHASE_10_COMPLETION_REPORT.md`.

The automated GTO+ convergence benchmark applies the same Target dEV definition
under a fixed, versioned fixture and timing protocol. Run five independent
Release processes with:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
  .\tools\run_gto_plus_convergence_benchmark.ps1
```

The versioned per-run JSON and aggregate mediana/p95 report are described in
`docs/GTO_PLUS_CONVERGENCE_BENCHMARK.md`.

Further roadmap phases are frozen by the three-fixture GTO+ parity gate. The
authoritative single-process checkpoint is the homogeneous final-head
revalidation dated 2026-08-30 and recorded in
`docs/OBJECTIVE_DRIVEN_GATE_CLOSURE_2026-08-30.md`. All three Release runs pass
strict `Target dEV < 1%`, root EV and `solver_state_bytes`; AHKHQH and TH7D6S
pass their time ceilings, while TSTC9D remains the timing blocker. The measured
solver times are `0.670928 s`, `17.645055 s` and `208.111772 s` against limits
`1.900000 s`, `19.622222 s` and `128.988889 s`. TST therefore exceeds its
limit by `79.122883 s` (`1.613409x`, `+61.3409%`). Its user-confirmed raw GTO+
reference remains `116.09 s` at the first strict sub-1% point.

Solver-state limits remain `8.000.000 B`, `399.000.000 B` and
`2.000.000.000 B`, and all three pass. Peak RSS is a separate metric with the
common strict desktop cap `< 2 GiB` (`2.147.483.648 B`): all three final-head
runs pass, including TST at `1.969.922.048 B`. The runner no longer compares
process RSS against the heterogeneous GTO+ state-memory references, which had
produced false AHK/TH failures. TST time remains red, so five-process timing
certification is still deferred. Contract and frontier recheck:
`docs/TWO_GIB_RESOURCE_CONTRACT_AND_FRONTIER_RECHECK_2026-09-01.md`.

The three-fixture suite also contains `GTP-TH7D6S-101` and
`GTP-TSTC9D-101`. On 2026-08-13 the TST action-tree contract was corrected
generally: percentage pushes use stack above the call divided by the pot after
the call, `Add` and `Go` remain distinct, and configurations may declare sizes
per raise count. Aggressive target amounts now use a general, serializable
piecewise rounding policy in the core; TST selects the observed `5.3/14/47`
policy without benchmark-ID branches. GTO+ benchmarks are target-driven with
no iteration cap and stop only at a certified `Target dEV < 1%`. Periodic
best-response certification starts only after average-strategy sampling begins;
finite completion, pause and cancellation still force a final certification.
The production core uses benchmark-independent signed regret and average
strategy state through `ScaledUint16RegretStrategy`, with the exact common
DCFR `1.5/0/2` contract. Current single Release runs pass dEV, root EV and
solver-state gates on all three fixtures. AHKHQH is `0.655665% @80`,
`19.108984`, `0.670928 s` and `5.300.664 B`; TH7D6S is `0.806385% @80`,
`8.221632`, `17.645055 s` and `334.452.416 B`; TSTC9D is `0.991863% @202`,
`8.494698`, `208.111772 s` and `1.472.605.376 B`. These are comparable single
processes, not promoted timing medians. TST execution time and the separate
AHK/TH peak-RSS parity comparisons remain red; none is hidden under a generic
RAM label.
