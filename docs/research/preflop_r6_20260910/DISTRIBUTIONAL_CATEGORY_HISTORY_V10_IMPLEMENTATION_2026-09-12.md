# R6 — Implementazione category-history v10

## Esito

`IMPLEMENTATION_COMPLETE / ENGINEERING_GATE_PASS / PAIRED_2M_COMPLETE`.

## Implementazione

La rappresentazione `DistributionalStrengthCategoryHistoryV10` conserva il bucket v7 della street corrente e soltanto la categoria Short Deck visibile della street precedente:

- Flop: bucket v7 Flop;
- Turn: categoria Flop e bucket v7 Turn;
- River: categoria Turn e bucket v7 River.

La categoria è estratta dai quattro bit alti del bucket strutturato v7 ed è limitata al dominio esatto `0–8`. La classe preflop e il bucket completo della street precedente non vengono conservati. La modalità è disattivata per default e si abilita con `--postflop-distributional-category-history-v10`.

Il formato policy passa a `1.9`. Il loader continua ad accettare i formati precedenti e rifiuta una policy v10 marcata come `1.8`.

## File modificati

- `include/gtosd/preflop/hu_preflop.hpp`: enum v10 e versione del formato;
- `libs/preflop/src/hu_preflop_solver.cpp`: identità, costruzione, validazione, export e query delle chiavi;
- `libs/preflop/src/hu_preflop_sampled_persistence.cpp`: vincolo di compatibilità v10;
- `benchmarks/hu_preflop_solve.cpp`: flag CLI e telemetria;
- `tests/hu_preflop_abstraction_tests.cpp`: categorie, pattern delle street, persistenza e rifiuti;
- `tests/hu_preflop_parallel_tests.cpp`: riproducibilità numerica fra uno e otto worker.

## Validazione

Build Release MSVC `/W4 /WX`: PASS per libreria, runner, benchmark e test preflop.

```text
R5_HU_PREFLOP_ABSTRACTION_TESTS=PASS
assertions=63048

R6_HU_PREFLOP_PARALLEL_TESTS=PASS
assertions=2079
```

CTest con label `preflop`: `13/13 PASS`, tempo reale `552,30 s`.

## Invarianti verificati

1. Il bucket corrente coincide con v7 per la stessa osservazione.
2. La storia contiene una sola street e una categoria compresa fra 0 e 8.
3. Al River non sopravvive alcuna informazione del Flop.
4. Policy con storia non valida o minor incompatibile vengono rifiutate.
5. Save/load/query preservano identità e fingerprint.
6. Il risultato dei test deterministici è bit-identico fra uno e otto worker.

## Decisione

Il gate ingegneristico è superato. Per disposizione dell'utente, la valutazione strategica ha saltato lo screen a 250k e completato due seed da 2M. Un artefatto inferiore a 2M non è ammesso nella soluzione pubblicata. Il verdetto quantitativo è in [DISTRIBUTIONAL_CATEGORY_HISTORY_V10_GATE_2026-09-12.md](DISTRIBUTIONAL_CATEGORY_HISTORY_V10_GATE_2026-09-12.md).
