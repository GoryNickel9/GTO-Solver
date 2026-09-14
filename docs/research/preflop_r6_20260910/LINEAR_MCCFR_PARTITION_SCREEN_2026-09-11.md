# Linear MCCFR: screening della partizione postflop

Data: 2026-09-11

## Obiettivo

Confrontare la partizione `32/128/512` con `64/256/1024` dopo la selezione di `training_batch_iterations=32`. Il confronto usa due seed indipendenti a 500.000 iterazioni. Questa fase sceglie una capacità interna; non dimostra che i bucket coincidano con quelli ignoti di Monker.

## Contratto congelato

- gioco: `benchmarks/fixtures/hu_preflop_co40_game_v1.json`;
- algoritmo: Linear MCCFR;
- iterazioni: 500.000;
- worker: 8;
- batch: 32;
- partition seed: `5923736619020287489`;
- seed training/evaluation 1: `5923736619020283393` / `5923736619020279297`;
- seed training/evaluation 2: `5200000000000000102` / `5300000000000000202`;
- feature: distributional strength prototype, MC8, current observation, imperfect recall v2;
- cache: 1.000.000 entry;
- budget stato numerico: 8 GiB;
- binario: SHA-256 `8BB2AB018B2BB5429EA2874DB83CDC1C62C88BCD47421D4B5CABEBB208C62229`.

L'unica variabile fra ciascuna coppia di run è la capacità Flop/Turn/River.

## Implementazione della fase

Non è stato modificato il codice di produzione. Sono stati prodotti quattro solve confrontabili:

| Partizione | Seed 1 | Seed 2 |
|---|---|---|
| `64/256/1024` | `linear_mccfr_8t_500k_batch32.json` | `linear_mccfr_8t_500k_batch32_seed2.json` |
| `32/128/512` | `linear_mccfr_8t_500k_batch32_partition_32_128_512.json` | `linear_mccfr_8t_500k_batch32_partition_32_128_512_seed2.json` |

Ogni solve ha il proprio stdout, stderr e report `.comparison.json`.

## Qualità rispetto al riferimento

| Partizione | Seed | WMAE azioni (pp) | TV media (pp) | P95 TV (pp) | Errore max root (pp) | EV CO (ante) | SE EV | Errore EV (ante) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `64/256/1024` | 1 | 20,195219 | 50,488048 | 95,897177 | 28,386481 | +0,104224 | 0,114983 | 0,404224 |
| `32/128/512` | 1 | **19,344159** | **48,360397** | **95,508322** | **26,387556** | -0,096547 | 0,117304 | 0,203453 |
| `64/256/1024` | 2 | 21,460041 | 53,650104 | 98,732217 | 31,869923 | -0,059679 | 0,116032 | 0,240321 |
| `32/128/512` | 2 | **19,756852** | **49,392130** | **95,958384** | **28,989995** | +0,124768 | 0,112607 | 0,424768 |

La WMAE media sui due seed scende da `20,827630` a `19,550505 pp`, un miglioramento di `1,277125 pp`. La TV media sui due seed scende da `52,069076` a `48,876264 pp`.

L'EV puntuale non migliora in modo coerente: il seed migliore cambia passando da una partizione all'altra. Con errori standard fra `0,1126` e `0,1173 ante` e configurazione Monker postflop incompleta, l'EV resta diagnostico e non supera il gate finale con intervallo di confidenza.

## Stabilità fra seed

| Partizione | WMAE fra policy (pp) | TV fra policy (pp) | Max delta azione aggregata (pp) |
|---|---:|---:|---:|
| `64/256/1024` | 13,068123 | 32,670309 | 3,483441 |
| `32/128/512` | **12,075768** | **30,189421** | **2,913336** |

La partizione ridotta diminuisce la WMAE fra seed di `0,992355 pp`, pari al `7,59%`.

## Costo

| Partizione | Seed | Solve (s) | Infoset | Payload numerico (B) | Picco private campionato (B) |
|---|---:|---:|---:|---:|---:|
| `64/256/1024` | 1 | 97,124977 | 1.314.793 | 215.035.776 | 414.035.968 |
| `32/128/512` | 1 | **87,922163** | 1.067.991 | **176.579.856** | **340.541.440** |
| `64/256/1024` | 2 | 91,527615 | 1.335.337 | 219.234.816 | 400.773.120 |
| `32/128/512` | 2 | **88,554543** | 1.076.033 | **178.164.720** | **342.720.512** |

Sulla media dei due seed, la partizione ridotta taglia il tempo di solve del `6,45%`, il payload numerico del `18,31%` e il picco private campionato del `16,15%`.

## Validazione e gate

| Controllo | Esito | Evidenza |
|---|---|---|
| Due seed indipendenti | PASS | training ed evaluation seed distinti; partition seed congelato |
| Solve e log | PASS | quattro `HU_PREFLOP_SOLVE=PASS`; stderr vuoti |
| Impronta del gioco | PASS | fingerprint `fnv1a64:0f9919d7d6030cf0` in tutti i run |
| 81 classi e normalizzazione | PASS | errore massimo della somma `2,220446049250313e-16`; nessun valore negativo o non finito |
| WMAE migliore di almeno 0,5 pp su ciascun seed | PASS | `-0,851060` e `-1,703189 pp` |
| TV, P95 ed errore max root non peggiorano | PASS | tutte migliorano su entrambi i seed |
| Stabilità fra seed non peggiora | PASS | WMAE fra policy `-0,992355 pp` |
| Tempo e RAM non peggiorano | PASS | entrambi diminuiscono sulla media e su ciascun seed |
| Azioni dominate prossime a zero | PASS | fold AA massimo `0,0000915%`; all-in 76o massimo `0,0007374%` nei candidati ridotti |
| Equivalenza con la configurazione Monker | NOT EVALUATED | `REFERENCE_CONFIG_INCOMPLETE` |
| Gate finale `1 pp` | FAIL | WMAE residua `19,55 pp` sulla media dei seed |

## Decisione

La partizione `32/128/512` supera il gate interno e sostituisce `64/256/1024` nei prossimi esperimenti Linear MCCFR. La promozione vale per il percorso sperimentale, non per i default production e non come ricostruzione dell'astrazione Monker.

Il prossimo esperimento isolato è la baseline di riduzione della varianza già supportata dal trainer, con partizione `32/128/512`, batch `32` e due seed. Non si aumenta ancora il numero di iterazioni.
