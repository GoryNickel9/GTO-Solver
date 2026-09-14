# R6 — Implementazione V16: stratificazione della prima risposta

Data: 2026-09-13  
Esito implementazione: `PASS`

## Modifica

V16 aggiunge l'opzione sperimentale
`root_first_opponent_response_stratification`, disattivata per default. Con quattro rollout, il
rollout `r` estrae la prima risposta avversaria dal quantile:

```text
U_r = (r + V_r) / 4, con V_r uniforme in [0, 1)
```

I quattro intervalli hanno la stessa massa. La loro media conserva il valore atteso della
variabile uniforme originale. Gli stream usati per il deal e per il punto interno allo strato sono
separati. Common Random Numbers riusa lo stesso quantile fra le azioni enumerate nello stesso
rollout.

La modifica riguarda soltanto il primo nodo avversario successivo alla prima decisione enumerata
del traverser. Le decisioni avversarie successive usano External Sampling ordinario.

## File modificati

- `include/gtosd/core/external_sampling.hpp`: mapping dello strato e inversione deterministica
  della CDF;
- `include/gtosd/preflop/hu_preflop.hpp`: opzione e telemetria del risultato;
- `libs/preflop/src/hu_preflop_solver.cpp`: applicazione simmetrica ai pass CO e BTN, ID algoritmo e
  validazione della configurazione;
- `benchmarks/hu_preflop_solve.cpp`: flag CLI
  `--root-first-opponent-response-stratification` ed export JSON;
- `tests/external_sampling_tests.cpp`: intervalli, CDF e valore atteso enumerabile;
- `tests/hu_preflop_parallel_tests.cpp`: contratto, default, configurazioni respinte e
  riproducibilità fra uno e otto worker.

## Invarianti

L'opzione viene respinta se manca uno dei seguenti requisiti:

- almeno due rollout;
- batch congelato;
- update medi delle continuation;
- media simmetrica dei traverser;
- chance sampling fisico indipendente.

Questi vincoli assicurano che ogni strato contribuisca con lo stesso peso agli update. La modalità
non modifica albero, range, size, rake, payoff, bucket o evaluator.

## Validazione precedente alle run

| Controllo | Risultato |
| --- | --- |
| Build MSVC Release con `/WX` | PASS |
| Test della formula e giochi ridotti | PASS, 108 assertion |
| Test V16 e determinismo 1/8 worker | PASS, 2.098 assertion |
| Suite HU Release | PASS, 11/11 in 496,50 s |
| Smoke CLI | PASS |
| Flag serializzato | PASS |
| Fingerprint evaluator nello smoke | `seven_card_table_v1:2236291214962974841` |

Il primo build senza `VsDevCmd.bat` non ha trovato l'header standard `type_traits`; nessun file è
stato compilato in quel tentativo. La ripetizione nell'ambiente MSVC inizializzato ha completato
senza warning o errori.

## Limite

La stratificazione riduce la varianza soltanto se le differenze fra le risposte avversarie
dominano il rumore della continuation. La correttezza dello stimatore non implica una riduzione
della TV fra due solve. La coppia 2M e il verdetto sono nel report di gate V16.
