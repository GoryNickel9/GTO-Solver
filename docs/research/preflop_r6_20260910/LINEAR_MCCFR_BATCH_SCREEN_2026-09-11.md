# Linear MCCFR: screening del training batch

Data: 2026-09-11

## Obiettivo

Selezionare il `training_batch_iterations` da usare nel successivo test della partizione postflop. Lo screening confronta `16`, `32` e `64` a 500.000 iterazioni. Questa fase seleziona un parametro interno; non qualifica ancora la strategia rispetto al riferimento Monker.

## Contratto congelato

- gioco: `benchmarks/fixtures/hu_preflop_co40_game_v1.json`;
- algoritmo: `linear_mccfr`;
- iterazioni: 500.000;
- worker: 8;
- seed training/partition/evaluation: invariati;
- astrazione: `preflop_exact81_postflop_distributional_strength_mc8_capacity_64_256_1024_current_observation_imperfect_recall_v2`;
- cache: 1.000.000 entry;
- budget stato numerico: 8 GiB;
- valutazione: 20.000 deal, BR 10.000 iterazioni e 10.000 deal;
- binario: SHA-256 `8BB2AB018B2BB5429EA2874DB83CDC1C62C88BCD47421D4B5CABEBB208C62229`.

L'unica variabile intenzionale è il batch.

## Implementazione della fase

Non è stato modificato il codice di produzione. Sono stati eseguiti due nuovi solve controllati:

- `linear_mccfr_8t_500k_batch32.json`;
- `linear_mccfr_8t_500k_batch16.json`.

Ogni output è stato confrontato con `hu_preflop_co40_reference_v1.json`. Il comparatore ha scritto il report diagnostico e ha restituito `REFERENCE_CONFIG_INCOMPLETE`, come previsto: al riferimento Monker mancano ancora albero postflop, astrazione e contratto di convergenza completi.

## Risultati

| Batch | WMAE azioni (pp) | TV media classe (pp) | P95 TV (pp) | Errore max root (pp) | EV CO (ante) | SE EV | Errore EV (ante) | Solve (s) | Infoset | Payload numerico (B) | Picco private campionato (B) |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 64 | 21,319472 | 53,298679 | 97,352637 | 30,050847 | -0,221459 | 0,119311 | 0,078541 | 89,445679 | 1.322.626 | 216.658.512 | 416.399.360 |
| 32 | **20,195219** | **50,488048** | **95,897177** | **28,386481** | +0,104224 | 0,114983 | 0,404224 | 97,124977 | 1.314.793 | 215.035.776 | 414.035.968 |
| 16 | 20,957811 | 52,394528 | 98,514553 | 29,528368 | -0,135883 | 0,116515 | 0,164117 | 98,531500 | 1.322.915 | 216.809.712 | 414.789.632 |

Rispetto al baseline `64`, il batch `32`:

- riduce la WMAE di `1,124252 pp`;
- riduce la TV media di `2,810631 pp`;
- riduce il P95 di `1,455460 pp`;
- riduce l'errore massimo della frequenza root di `1,664366 pp`;
- aumenta il tempo di solve dell'`8,59%` e riduce il picco private campionato dello `0,57%`.

L'EV puntuale del batch `32` peggiora, ma la sua incertezza Monte Carlo resta alta (`SE 0,114983 ante`). Inoltre l'EV Monker non è ancora confrontabile su un contratto postflop completo. Il dato viene conservato e non viene usato per dichiarare equivalenza.

## Invarianti e validazione

| Controllo | Esito | Evidenza |
|---|---|---|
| Configurazione invariata salvo batch | PASS | stesso fingerprint `fnv1a64:0f9919d7d6030cf0`, stessa astrazione e stessi seed |
| Solve completati | PASS | entrambi gli stdout riportano `HU_PREFLOP_SOLVE=PASS`; stderr vuoti |
| 81 classi presenti | PASS | tutte le strategie contengono 81 righe |
| Strategie normalizzate | PASS | errore massimo della somma `2,220446049250313e-16` |
| Valori finiti e non negativi | PASS | zero frequenze non finite o negative |
| Azioni dominate sotto controllo | PASS | batch 32: fold AA `0%`, all-in 76o `0,001028%` |
| WMAE migliore del baseline di almeno 0,5 pp | PASS | miglioramento `1,124252 pp` |
| TV, P95 ed errore max root non peggiorano | PASS | migliorano tutte e tre le metriche |
| Penalità tempo sotto il 15% | PASS | `+8,59%` |
| Validazione Monker completa | NOT EVALUATED | configurazione esterna incompleta; gate ufficiali restano chiusi |

I test del binario corrente `gtosd_external_sampling_tests`, `gtosd_hu_preflop_sampling_tests` e `gtosd_hu_preflop_parallel_tests` erano già passati `3/3` prima dei solve.

## Run escluso

Un primo tentativo `batch=16` è stato avviato senza `--postflop-distributional-prototype` e ha quindi usato `category_equity_mc_perfect_recall`. Non è comparabile e non entra in nessuna metrica. I relativi file sono conservati con suffisso `batch16_wrong_representation` per rendere l'incidente verificabile.

## Decisione

`training_batch_iterations=32` supera il gate di screening ed è il parametro selezionato per il prossimo esperimento. `16` e `64` non vengono promossi.

Il prossimo gate confronta, a 500.000 iterazioni e batch `32`, la partizione candidata `32/128/512` con il baseline `64/256/1024`. Nessun run a 8 milioni è autorizzato da questo risultato.
