# Archivio D: history7 e suite HU10-HU40 (19-28 settembre 2026)

Stato: **storico, non normativo**. Archiviato il 2026-10-03 con il piano dell'audit dei documenti del
2026-10-02, approvato dall'utente.

## Cosa contiene

Tutti i file stavano in `docs/research/preflop_vector_cfr/`.

- `BENCHMARK_SUITE_INVENTORY_2026-09-21.md`, `BENCHMARK_SUITE_PROTOCOL_2026-09-21.md` e
  `BENCHMARK_SUITE_REPORT_2026-09-23.md`: censimento, protocollo comune e tabelle della suite HU10-HU40
  (milestone RAM e tempi).
- `HISTORY7_TIME_AUDIT_2026-09-20.md`, `HU20_HU30_HU40_GOAL_2026-09-19.md` e
  `HU30_BUCKET_CAUSAL_AUDIT_2026-09-21.{md,json}`: l'astrazione history7 e la qualificazione di HU20, HU30
  e HU40.

## Perché è archiviato

L'utente ha deciso il ritiro il 2026-10-01. Il 2026-10-03 il codice è stato tolto con i commit `5d10baa`,
`82315d2` e `bac0f18`, uniti nel merge `a97e7b5`. Sono stati tolti:

- history7, cioè le righe di bucket con la storia e le mappe GTOSDHR1/HR2;
- la best response astratta esatta;
- la suite HU10-HU40.

Con loro è ritirato anche il gate "best response astratta ≤ 0,03 a". Lo standard ora è HU50 con le righe
per classe di board. I criteri di accettazione del prodotto sono una proposta ancora da approvare
(handoff, T16).

**Questi strumenti esistono solo al tag `history7-final` (= `88118a6`, l'ultimo albero con history7):**

- `tools/preflop_suite/` (`suite.py`, `run_queue.sh`);
- `benchmarks/suite/` tranne `fixtures/`: `preflop_blueprint_suite.json`, `resolved.json` e `actions/`;
- gli eseguibili `gtosd_preflop_blueprint_abstract_br`, `gtosd_preflop_blueprint_history_rows` e
  `gtosd_preflop_blueprint_history_census`;
- gli script `scripts/research/run_hu40_history7_solve.ps1` e `scripts/research/finalize_hu40_t37000.ps1`;
- l'opzione `--history-rows` di train, certify, export e bucket diagnostics, e `--rows history` di bucket
  diagnostics.

Per esempio: `git show history7-final:tools/preflop_suite/suite.py`.

Restano nel working tree:

- le 7 fixture `benchmarks/suite/fixtures/*.json`, lette dal game test;
- [`MEMORY_TIME_OPTIMIZATION_2026-09-21.md`](../../research/preflop_vector_cfr/MEMORY_TIME_OPTIMIZATION_2026-09-21.md),
  in `docs/research/preflop_vector_cfr/`. È l'unico documento di progetto di `--table-storage` e del motore
  del river, che sono ancora in produzione.

## File cancellati e recupero

Il 2026-10-03 è stato cancellato `BENCHMARK_SUITE_REPORT_2026-09-23.json` (1,09 MB), un dump generato da
`suite.py report`. Le sue tabelle sono in `BENCHMARK_SUITE_REPORT_2026-09-23.md`. Il tag annotato
`docs-pre-cleanup-2026-10-02` punta all'ultimo commit che lo contiene (`a97e7b5`). A quel tag anche ogni
file di questa cartella si trova al suo vecchio percorso:

```text
git show docs-pre-cleanup-2026-10-02:docs/research/preflop_vector_cfr/BENCHMARK_SUITE_REPORT_2026-09-23.json
```

## Documenti correnti

- Il prodotto, la build e i test: [`README.md`](../../../README.md).
- Il programma in corso: il diario [`PROGRESS_LOG.md`](../../research/preflop_vector_cfr/PROGRESS_LOG.md),
  la ricetta [`MONKER_RECIPE_REPRODUCTION_2026-09-28.md`](../../research/preflop_vector_cfr/MONKER_RECIPE_REPRODUCTION_2026-09-28.md)
  e l'handoff [`NEXT_STEPS_2026-10-02.md`](../../handoff/NEXT_STEPS_2026-10-02.md).
