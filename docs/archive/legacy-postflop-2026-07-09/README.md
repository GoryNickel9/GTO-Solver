# Archivio A: postflop HU esatto e parità con GTO+ (luglio-settembre 2026)

Stato: **storico, non normativo**. Archiviato il 2026-10-03 con il piano dell'audit dei documenti del
2026-10-02, approvato dall'utente.

## Cosa contiene

51 documenti del programma postflop HU esatto e del confronto con GTO+, scritti fra il 2026-07-28 e il
2026-09-15. Prima stavano in `docs/`, tranne `DESKTOP_UI.md`, che stava in `docs/specifications/`.

- La roadmap F0-F11+ del prodotto legacy, `ROADMAP_HU_SHORT_DECK_GTO_SOLVER.md`.
- La dashboard `IMPLEMENTATION_STATUS.md`. La sua ultima voce è la V23 del preflop external sampling,
  del 15/09.
- I report di fase `PHASE_1` ... `PHASE_10_COMPLETION_REPORT.md` e `PHASE_9_BENCHMARK_PROTOCOL.md`.
- L'ADR della GUI, `ADR_0001_GUI_FRAMEWORK.md`, e la specifica della GUI desktop, `DESKTOP_UI.md`.
- I cicli di ottimizzazione e di fattibilità del solver postflop: `*_LOOP_*`, `*_FEASIBILITY_*`,
  `PRODUCTION_DCFR_*`, `NEXT_*`, `PERFORMANCE_ANALYSIS_2026-08-29.md`, `TH7D6S_SPEED_OPTIMIZATION.md` e
  `speed_optimization_journey.md`.
- Le analisi di GTO+: memoria, black box e workflow osservato.

## Perché è archiviato

- Il motore postflop HU esatto, cioè `gto_cli` e le librerie da cui dipende, **resta nel prodotto**
  (decisione del 2026-10-02). Il suo contratto corrente è in
  [`docs/specifications/`](../../specifications/README.md). Questi file ne raccontano la storia: gate,
  misure e decisioni chiuse.
- La GUI desktop è stata tolta il 2026-10-02: `gto_gui`, i prototipi Qt e Dear ImGui, la libreria
  `gtosd::gui_prototype` e il preset `windows-gui-release`. I commit sono `921f424`, `1591a10`, `797a7d4`,
  `c2e9138` e `8c70044`, uniti nel merge `a97e7b5`. `ADR_0001`, `DESKTOP_UI.md`, i report delle fasi 9 e 10
  e le righe F9/F10 di `IMPLEMENTATION_STATUS.md` descrivono codice che non esiste più. Il sorgente resta
  nella storia git, per esempio `git show 2aa24d8:apps/gto_gui/product_window.cpp`.

## Rimasti in `docs/`

- `ADR_0002_MEMORY_BOUNDED_EXACT_SOLVER.md`, `ADR_0003_CANONICAL_CHANCE_TREE_CFR_STORAGE.md` e
  `ERROR_AND_VERSIONING_POLICY.md`.
- `GTO_PLUS_CONVERGENCE_BENCHMARK.md` e `GTO_PLUS_NEW_BENCHMARK_GUIDE.md`, con il loro runner
  `tools/run_gto_plus_convergence_benchmark.ps1`.
- `GTO_PLUS_SOLVER_MEMORY_SEMANTICS_AND_GATE_CORRECTION_PLAN_2026-09-03.md`: è l'autorità sulla semantica
  della memoria di GTO+, citata dalle specifiche e dagli ADR.
- `GTO_PLUS_PARITY_JOURNEY.md`, il registro del gate di parità con GTO+. Se la parità resti un obiettivo
  di `gto_cli` è una decisione dell'utente ancora aperta.
- `POSTFLOP_RESEARCH_DECISION_2026-09-10.md` e `POSTFLOP_AGENT_EXECUTION_ROADMAP_2026-09-10.md`: restano
  perché l'utente potrebbe riprendere l'ottimizzazione di `gto_cli`.

Dello stesso periodo è anche [`../legacy-memory-gate/`](../legacy-memory-gate/README.md), il falso gate
della memoria, archiviato il 2026-09-04 e lasciato dov'era.

## File cancellati e recupero

Il 2026-10-03 sono stati cancellati tre prompt di lavoro dell'8 agosto, superati da
`speed_optimization_journey.md`:

- `docs/cfr_iteration_optimization_agent.md`
- `docs/cfr_next_optimization_phase.md`
- `docs/cfr_phase_cde_dataflow_optimization.md`

Il tag annotato `docs-pre-cleanup-2026-10-02` punta all'ultimo commit che li contiene (`a97e7b5`). A quel
tag anche ogni file di questa cartella si trova al suo vecchio percorso:

```text
git show docs-pre-cleanup-2026-10-02:docs/cfr_next_optimization_phase.md
git show docs-pre-cleanup-2026-10-02:docs/IMPLEMENTATION_STATUS.md
```

I branch di ricerca chiusi del postflop sono ai tag `archive/research/*`.

I link relativi di questi documenti sono stati aggiornati ai nuovi percorsi. Alcuni link di
`IMPLEMENTATION_STATUS.md` puntano a `docs/research/preflop_r6_20260910/`, una cartella tolta il
2026-09-15: il suo contenuto è al tag `preflop-legacy-es-2026-09-15`.

## Documenti correnti

- Il prodotto, la build e i test: [`README.md`](../../../README.md).
- Il contratto di `gto_cli`: [`docs/specifications/`](../../specifications/README.md).
- Il programma in corso (solver preflop blueprint, HU e 3-way): il diario
  [`PROGRESS_LOG.md`](../../research/preflop_vector_cfr/PROGRESS_LOG.md) e l'handoff
  [`NEXT_STEPS_2026-10-02.md`](../../handoff/NEXT_STEPS_2026-10-02.md).
