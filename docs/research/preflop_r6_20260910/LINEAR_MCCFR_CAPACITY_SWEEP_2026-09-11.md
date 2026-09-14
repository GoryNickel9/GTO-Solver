# Linear MCCFR: capacity sweep postflop

Data: 2026-09-11

## Obiettivo

Verificare se il miglioramento ottenuto dimezzando `64/256/1024` continua con `16/64/256`. Il confronto usa due seed, 500.000 iterazioni, batch `32`, sampling fisico indipendente e lo stesso partition seed.

Non è stato modificato il codice di produzione. I nuovi artefatti usano il suffisso `partition_16_64_256`, con JSON, stdout, stderr e confronto Monker.

## Confronto delle capacità

| Capacità F/T/R | Seed | WMAE (pp) | TV media (pp) | P95 TV (pp) | Errore max root (pp) | Solve (s) | Payload numerico (B) | Picco private campionato (B) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `16/64/256` | 1 | 20,355036 | 50,887589 | 97,829640 | 29,505693 | 89,501062 | 128.766.240 | 290.701.312 |
| `16/64/256` | 2 | 19,876017 | 49,690044 | 96,905049 | 33,213768 | 89,529647 | 127.675.152 | 291.778.560 |
| `32/128/512` | 1 | **19,344159** | **48,360397** | **95,508322** | **26,387556** | **87,922163** | 176.579.856 | 340.541.440 |
| `32/128/512` | 2 | **19,756852** | **49,392130** | **95,958384** | **28,989995** | **88,554543** | 178.164.720 | 342.720.512 |
| `64/256/1024` | 1 | 20,195219 | 50,488048 | 95,897177 | 28,386481 | 97,124977 | 215.035.776 | 414.035.968 |
| `64/256/1024` | 2 | 21,460041 | 53,650104 | 98,732217 | 31,869923 | 91,527615 | 219.234.816 | 400.773.120 |

Medie sui due seed:

| Capacità F/T/R | WMAE (pp) | TV (pp) | Errore max root (pp) | Solve (s) | Payload (B) | Picco private (B) |
|---|---:|---:|---:|---:|---:|---:|
| `16/64/256` | 20,115527 | 50,288817 | 31,359731 | 89,515354 | 128.220.696 | 291.239.936 |
| `32/128/512` | **19,550505** | **48,876264** | **27,688775** | **88,238353** | 177.372.288 | 341.630.976 |
| `64/256/1024` | 20,827630 | 52,069076 | 30,128202 | 94,326296 | 217.135.296 | 407.404.544 |

`16/64/256` risparmia il `27,71%` del payload e il `14,75%` del picco private rispetto a `32/128/512`, ma peggiora la WMAE media di `0,565021 pp`, la TV di `1,412553 pp` e l'errore massimo root medio di `3,670956 pp`.

## Stabilità fra seed

| Capacità F/T/R | WMAE fra policy (pp) |
|---|---:|
| `16/64/256` | **10,816173** |
| `32/128/512` | 12,075768 |
| `64/256/1024` | 13,068123 |

La capacità minore riduce la dispersione, ma converge verso una policy mediamente più lontana dal riferimento. È un miglioramento di varianza accompagnato da maggiore errore di astrazione.

## Validazione e gate

| Controllo | Esito | Evidenza |
|---|---|---|
| Due seed e contratto congelato | PASS | stessa configurazione salvo capacità |
| Solve e stderr | PASS | entrambi i nuovi solve passano; stderr vuoti |
| Strategie valide | PASS | 81 classi, frequenze finite, non negative e normalizzate |
| WMAE non peggiore su entrambi i seed | FAIL | `+1,010877` e `+0,119165 pp` rispetto a `32/128/512` |
| Errore max root non peggiore | FAIL | peggiora su entrambi i seed |
| Stabilità fra seed | PASS | migliora di `1,259596 pp` |
| RAM | PASS | riduzione media del `14,75%` |
| Equivalenza Monker | NOT EVALUATED | configurazione esterna incompleta |

## Decisione

`32/128/512` resta la capacità selezionata. È il minimo locale osservato nella sweep `16/64/256`, `32/128/512`, `64/256/1024`: entrambe le direzioni peggiorano la WMAE media.

Il prossimo esperimento varia soltanto i campioni equity per bucket da MC8 a MC16. Lo scopo è ridurre il rumore delle feature senza cambiare capacità o sampling del trainer.
