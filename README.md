# GTOSD

Exact Short Deck Heads-Up solver foundation following
`docs/ROADMAP_HU_SHORT_DECK_GTO_SOLVER.md`.

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

The project does not yet claim a solved or certified GTO strategy for the
complete Short Deck postflop tree. Phase 6 measures exact memory
architectures before that integration.
