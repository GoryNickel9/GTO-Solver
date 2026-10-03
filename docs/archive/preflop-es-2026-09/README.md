# Archivio B: preflop external sampling, R0-R6 (6-15 settembre 2026)

Stato: **storico, non normativo**. Archiviato il 2026-10-03 con il piano dell'audit dei documenti del
2026-10-02, approvato dall'utente.

Il programma preflop basato su external sampling MCCFR (fasi R0-R6, versioni V1-V23) è stato chiuso il
2026-09-15 e sostituito dal solver vettoriale `libs/preflop_blueprint`. Le sue evidenze erano già state
tolte dal working tree il 15/09 (tag `preflop-legacy-es-2026-09-15`, commit `04aa687`). Qui ci sono i tre
pezzi che erano rimasti in `docs/`.

## Cosa contiene

- [`PREFLOP_LEGACY_INDEX.md`](PREFLOP_LEGACY_INDEX.md), prima in `docs/research/`: l'indice del programma
  chiuso, con i comandi per recuperarlo dal tag `preflop-legacy-es-2026-09-15`. Vale come indice di questa
  cartella. La sua frase sulle due policy V17 "disponibili fino al gate P9" è superata: il lavoro di P9 è
  chiuso ed è nell'archivio C.
- [`HU_PREFLOP_CO40_BENCHMARK.md`](HU_PREFLOP_CO40_BENCHMARK.md), prima in `docs/`: contratto, provenienza
  e conteggi dell'albero del benchmark CO 40a.
- [`preflop_r0_20260910/`](preflop_r0_20260910/README.md), prima in `docs/research/`: il contratto R0,
  il suo amendment e tre JSON del 10/09. `manifest.json` è una fotografia dell'albero di quel giorno: i
  percorsi che elenca non sono stati aggiornati.

## Ancora in uso

`HU_PREFLOP_CO40_BENCHMARK.md` è la provenienza della fixture
`benchmarks/fixtures/hu_preflop_co40_reference_v1.json`. La fixture resta al suo posto ed è ancora letta
da:

- `tests/preflop_blueprint_export_tests.cpp`: il comparatore la usa come riferimento e contro HU10 si
  aspetta `ACTION_SET_MISMATCH`;
- `benchmarks/CMakeLists.txt`: i CTest `gtosd_hu_preflop_reference_preflight` (codice legacy) e
  `gtosd_preflop_blueprint_compare_smoke`.

Il codice legacy dell'external sampling (`libs/preflop`, `benchmarks/hu_preflop_*.cpp`) è ancora
compilato e testato. Le sue CLI e i suoi checkpoint sono descritti in `docs/specifications/CLI.md`
(sezione "HU preflop research") e in `docs/specifications/SOLUTION_FORMAT.md`.

## Recupero

In questa pulizia nessun file di quest'era è stato cancellato. Il layout precedente di `docs/` è al tag
annotato `docs-pre-cleanup-2026-10-02`; le evidenze tolte il 15/09 sono al tag
`preflop-legacy-es-2026-09-15`:

```text
git show docs-pre-cleanup-2026-10-02:docs/research/PREFLOP_LEGACY_INDEX.md
git show preflop-legacy-es-2026-09-15:docs/research/preflop_r6_20260910/README.md
```

## Documenti correnti

- Il prodotto, la build e i test: [`README.md`](../../../README.md).
- Il solver preflop in uso: il diario
  [`PROGRESS_LOG.md`](../../research/preflop_vector_cfr/PROGRESS_LOG.md), i report di modulo P0-P8 nella
  stessa cartella e l'handoff [`NEXT_STEPS_2026-10-02.md`](../../handoff/NEXT_STEPS_2026-10-02.md).
