# Archivio C: ricerca del preflop blueprint, roadmap P0-P10 e P9 (15-21 settembre 2026)

Stato: **storico, non normativo**. Archiviato il 2026-10-03 con il piano dell'audit dei documenti del
2026-10-02, approvato dall'utente. Il piano chiamava questa cartella `preflop-blueprint-p0-p9/`. La
revisione del piano ha suggerito questo nome perché i report P0-P8 sono rimasti al loro posto.

## Cosa contiene

- `PREFLOP_VECTOR_CFR_CODER_ROADMAP_2026-09-15.md`, prima in `docs/research/`: la roadmap P0-P10 per
  l'agent coder. Il suo stato si ferma al 21/09, con P9 aperta.
- `HU_PREFLOP_ALGORITHM_AND_ABSTRACTION_ANALYSIS_2026-09-15.md`, prima in `docs/research/`: l'analisi da
  cui è nato il solver vettoriale.
- `GPU_CUDA_FEASIBILITY_2026-09-18.md`, prima in `docs/research/`: lo studio di un backend GPU. Il
  prodotto usa solo CPU e RAM.
- `P9_CONVERGENCE_DIAGNOSIS.md`, `NASH_AUDIT_2026-09-19.md` e `CONSTRAINED_BR_AUDIT_2026-09-19.md`, prima
  in `docs/research/preflop_vector_cfr/`: P9 (qualificazione CO40) e gli audit del 19/09 sulla
  convergenza e sulla best response congiunta.

## Rimasti al loro posto

- I report di gate P0-P8, da `P0_SCAFFOLDING.md` a `P8_EXPORT.md`, in
  [`docs/research/preflop_vector_cfr/`](../../research/preflop_vector_cfr/). Sono gli unici documenti di
  progetto per modulo del codice in produzione (`libs/preflop_blueprint`, `libs/card_abstraction`) e
  hanno un banner di stato.
- Il registro decisioni [`PREFLOP_ARCHITECTURE_DECISION_LOG.md`](../../research/PREFLOP_ARCHITECTURE_DECISION_LOG.md),
  congelato al 21/09 con un banner.

## File cancellati e recupero

Il 2026-10-03 sono stati cancellati 14 dump JSON del 19/09, prodotti dalla ricerca di P9. Stavano in
`docs/research/preflop_vector_cfr/` e il loro contenuto è riassunto nei report di questa cartella.

| File | Riassunto in | Generato da |
|---|---|---|
| `CONSTRAINED_BR_{MECHANISMS,CONTROL,CONFLICT,SAVED_CONFLICT,LONG_CONFLICT,SOURCE_MANIFEST}_2026-09-19.json`, `CO40_BR_ROUTES_2026-09-19.json` | `CONSTRAINED_BR_AUDIT_2026-09-19.md` | `scripts/research/summarize_constrained_br_audit.py` |
| `CO40_BUCKET_SCREEN`, `CO40_FULL_BUCKET_AUDIT`, `CO40_NORMALIZATION_AUDIT`, `DIAGNOSTIC_BUILD`, `STACK_ENTRY_SCREEN`, `TEMPORAL_CONTROLS`, `TEMPORAL_SCREEN` (tutti `_2026-09-19.json`) | `NASH_AUDIT_2026-09-19.md`, che li nomina; il diario cita per nome `CO40_FULL_BUCKET_AUDIT` e `TEMPORAL_SCREEN` | strumenti diagnostici di P9 |

Il tag annotato `docs-pre-cleanup-2026-10-02` punta all'ultimo commit che li contiene (`a97e7b5`). A quel
tag anche ogni file di questa cartella si trova al suo vecchio percorso:

```text
git show docs-pre-cleanup-2026-10-02:docs/research/preflop_vector_cfr/CONSTRAINED_BR_CONTROL_2026-09-19.json
git show docs-pre-cleanup-2026-10-02:docs/research/preflop_vector_cfr/TEMPORAL_SCREEN_2026-09-19.json
```

I sette file della prima riga si possono rigenerare con
`python scripts/research/summarize_constrained_br_audit.py --input-dir <output grezzi> --output-dir <cartella>`.
Lo script ha come default `--input-dir out/nash_audit` e `--output-dir docs/research/preflop_vector_cfr`.
Gli output grezzi di `out/nash_audit` non sono più su C: (verificato il 2026-10-02 e il 2026-10-03), quindi
oggi il tag è l'unica copia di quei file nel repository.

## Documenti correnti

- Il prodotto, la build e i test: [`README.md`](../../../README.md).
- Il programma in corso: il diario [`PROGRESS_LOG.md`](../../research/preflop_vector_cfr/PROGRESS_LOG.md),
  la ricetta [`MONKER_RECIPE_REPRODUCTION_2026-09-28.md`](../../research/preflop_vector_cfr/MONKER_RECIPE_REPRODUCTION_2026-09-28.md)
  e l'handoff [`NEXT_STEPS_2026-10-02.md`](../../handoff/NEXT_STEPS_2026-10-02.md).
