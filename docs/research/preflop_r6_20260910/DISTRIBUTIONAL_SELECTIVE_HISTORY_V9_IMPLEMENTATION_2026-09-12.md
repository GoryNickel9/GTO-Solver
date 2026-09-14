# R6 — Implementazione selective-history v9

## Esito

`IMPLEMENTATION_COMPLETE / ENGINEERING_GATE_PASS / PAIRED_2M_COMPLETE`.

## Implementazione

La nuova rappresentazione `DistributionalStrengthSelectiveHistoryV9` riusa il mapping v7 e modifica soltanto la chiave postflop:

- Flop: bucket Flop;
- Turn: bucket Flop e Turn;
- River: bucket Turn e River.

La classe preflop non viene conservata. La modalità è disattivata per default ed è selezionabile con `--postflop-distributional-selective-history-v9`. L'identità serializzata contiene `category_equity_ordered_profile_selective_history_v9_imperfect_recall`.

Il formato della policy passa a `1.8`. Le policy v6/v7/v8 continuano a usare rispettivamente i minor `5/6/7`; il loader accetta i formati precedenti e rifiuta una v9 marcata come `1.7`.

## File modificati

- `include/gtosd/preflop/hu_preflop.hpp`: enum e versione del formato;
- `libs/preflop/src/hu_preflop_solver.cpp`: costruzione e validazione delle chiavi, identità, export e query;
- `libs/preflop/src/hu_preflop_sampled_persistence.cpp`: compatibilità e controllo versione;
- `benchmarks/hu_preflop_solve.cpp`: flag CLI e log esplicito;
- `tests/hu_preflop_abstraction_tests.cpp`: pattern delle tre street, persistenza, query e rifiuti;
- `tests/hu_preflop_parallel_tests.cpp`: parità numerica fra uno e otto worker.

## Validazione

Build Release MSVC `/W4 /WX`: PASS per librerie, runner, benchmark e test preflop.

```text
R5_HU_PREFLOP_ABSTRACTION_TESTS=PASS
assertions=42851

R6_HU_PREFLOP_PARALLEL_TESTS=PASS
assertions=2076
```

CTest con label `preflop`: `13/13 PASS`, tempo reale `567,14 s`.

Un tentativo CTest globale ha eseguito `45/52` test e non ha avviato sette eseguibili postflop assenti dalla build selettiva. L'esito globale è `NOT_AVAILABLE_FOR_THIS_BUILD`, non un PASS e non un fallimento numerico v9. Il perimetro preflop richiesto dal protocollo è completo.

## Invarianti verificati

1. Ogni chiave contiene esattamente current+previous, con la sola eccezione naturale del Flop.
2. La policy v9 usa minor 8, passa checksum/save/load e mantiene la stessa fingerprint dopo il reload.
3. Una River key che conserva anche il bucket Flop viene rifiutata.
4. Il risultato numerico è bit-identico fra uno e otto worker.
5. Capacità sotto 32 e versioni incompatibili continuano a essere rifiutate.

## Decisione

Il gate ingegneristico ha autorizzato lo screen CO40 a 250k. La successiva rimozione del cap RAM non qualificato e l'istruzione di usare almeno 2M per ogni versione hanno autorizzato due run complete a 2M; dettagli nel report del gate v9.
