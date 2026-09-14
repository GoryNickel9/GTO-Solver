# R6 — Implementazione adaptive-category-history v11

## Esito

`IMPLEMENTATION_COMPLETE / ENGINEERING_GATE_PASS / PAIRED_2M_PASS / COMPARATIVE_GATE_FAIL`.

## Implementazione

`DistributionalStrengthAdaptiveCategoryHistoryV11` combina due componenti già isolati:

- il bucket della street corrente usa esattamente il mapping street-adaptive V8;
- la storia conserva soltanto la categoria Short Deck esatta della street precedente.

Al Flop la chiave contiene solo il bucket V8 corrente. Al Turn contiene categoria Flop `0..8` e bucket V8 Turn. Al River contiene categoria Turn `0..8` e bucket V8 River. La categoria storica viene valutata dalle carte visibili e non estratta dal bucket V8: i quattro gruppi V8 del Flop non permettono di ricostruire le nove categorie originali.

La modalità è disattivata per default e si seleziona con `--postflop-distributional-adaptive-category-history-v11`. Il formato policy passa a `1.10`; loader, validatore e query rifiutano una V11 marcata `1.9`.

## File modificati

- `include/gtosd/preflop/hu_preflop.hpp`: enum V11, formato `1.10` e wrapper testabile del mapping;
- `libs/preflop/src/hu_preflop_solver.cpp`: identità, categoria storica, chiavi parallele e sequenziali, export, query e vincoli;
- `libs/preflop/src/hu_preflop_sampled_persistence.cpp`: compatibilità e rifiuto dei minor precedenti;
- `benchmarks/hu_preflop_solve.cpp`: flag CLI e telemetria;
- `tests/hu_preflop_abstraction_tests.cpp`: mapping, dominio, persistenza, query e versionamento;
- `tests/hu_preflop_parallel_tests.cpp`: determinismo fra uno e otto worker.

## Validazione di ingegneria

Build Release MSVC con `/W4 /WX`: PASS.

```text
R5_HU_PREFLOP_ABSTRACTION_TESTS=PASS
assertions=91774

R6_HU_PREFLOP_PARALLEL_TESTS=PASS
assertions=2082
```

CTest HU preflop: `11/11 PASS`, tempo reale `458,42 s`.

## Invarianti verificati

1. Il bucket corrente V11 è identico al bucket V8 per ogni street testata.
2. La storia contiene una sola categoria compresa fra `0` e `8`.
3. Al River non viene conservata la categoria del Flop.
4. La classe preflop non entra nella chiave postflop.
5. Save/load/query preservano fingerprint e versione `1.10`.
6. Il risultato del test parallelo è bit-identico fra uno e otto worker.

## Stato delle run

Entrambi i seed a 2M sono completi. Gli export contengono 20 nodi preflop e le policy postflop `1.10` sono state persistite. La WMAE media è `15,3904 pp` e la TV fra seed è `15,0952 pp`: entrambe peggiorano rispetto a V10, quindi il gate comparativo fallisce. I risultati completi sono in `DISTRIBUTIONAL_ADAPTIVE_CATEGORY_HISTORY_V11_GATE_2026-09-12.md`.
