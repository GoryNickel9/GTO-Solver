# Linear MCCFR: gate delle rappresentazioni history-aware

Data: 2026-09-11

## Obiettivo

Verificare se conservare la history dei bucket o il perfect recall corregge il deficit sistematico di all-in osservato a 4M. Tutti i test usano lo stesso gioco, Linear MCCFR, MC8, sampling fisico indipendente e seed primario. Non è stato modificato il codice.

## Bucket-history, capacità `8/32/128`

| Iterazioni | Rappresentazione | WMAE (pp) | P95 TV (pp) | Delta all-in (pp) | Delta call (pp) | Infoset | Payload (B) | Solve (s) |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 100k | Current observation | 20,471532 | 98,758528 | -29,252625 | +13,969215 | 314.006 | 57.446.208 | 19,414 |
| 100k | Bucket-history | **20,071381** | **95,751525** | -29,897211 | +15,976699 | 835.788 | 138.610.080 | 19,906 |
| 500k | Current observation | 21,344308 | 97,483119 | -33,444425 | +26,240045 | 424.117 | 75.461.328 | 87,822 |
| 500k | Bucket-history | **20,963195** | **96,640711** | **-30,944506** | +28,402792 | 1.644.200 | 259.715.232 | 93,649 |

Bucket-history migliora la WMAE di circa `0,4 pp` a 100k e `0,381112 pp` a 500k. A 500k riduce il deficit all-in di `2,499919 pp`, ma aumenta l'eccesso di call di `2,162747 pp` e usa 3,44 volte il payload.

## Bucket-history, capacità `32/128/512`

| Rappresentazione, 100k | WMAE (pp) | P95 TV (pp) | Delta all-in (pp) | Delta call (pp) | Infoset | Payload (B) |
|---|---:|---:|---:|---:|---:|---:|
| Current observation | **18,748250** | 95,217455 | **-25,434912** | **+14,174774** | 687.763 | 118.959.552 |
| Bucket-history | 20,697930 | **95,055059** | -29,595324 | +23,185375 | 2.858.854 | 442.849.824 |

A capacità uguale a quella candidata, bucket-history peggiora la WMAE di `1,949680 pp`, il deficit all-in di `4,160412 pp` e l'eccesso di call di `9,010601 pp`; il payload cresce di 3,72 volte. Il lieve miglioramento P95 di `0,162396 pp` non compensa.

## Perfect recall ridotto

| Rappresentazione, 50k, `8/32/128` | WMAE (pp) | P95 TV (pp) | Delta all-in (pp) | Delta call (pp) | Infoset | Payload (B) |
|---|---:|---:|---:|---:|---:|---:|
| Current observation | **22,616786** | **99,067955** | **-26,968751** | **+14,278623** | 264.342 | 50.230.080 |
| Hierarchical perfect recall | 24,210460 | 99,732856 | -27,968811 | +25,637817 | 2.083.127 | 335.089.008 |

Perfect recall peggiora la WMAE di `1,593673 pp` e usa 6,67 volte il payload già a 50k. Non è autorizzato un run più lungo.

## Category/equity perfect recall

Il run escluso inizialmente dallo screening batch è utile solo come limite negativo della rappresentazione:

| Iterazioni | Batch | WMAE (pp) | Errore max root (pp) | Infoset | Payload (B) | Picco private (B) |
|---:|---:|---:|---:|---:|---:|---:|
| 500k | 16 | 24,270935 | 50,404788 | 35.722.490 | 5.197.777.776 | 6.093.766.656 |

Non è un confronto matched sul batch, quindi non misura un delta causale preciso. Dimostra però che questa variante è troppo costosa e lontana dal riferimento per meritare una replica.

## Validazione e gate

| Controllo | Esito | Evidenza |
|---|---|---|
| A/B matched current contro bucket-history | PASS | capacità, seed, batch e iterazioni uguali in ciascuna coppia |
| Solve e strategie | PASS | exit zero, stderr vuoti, 81 classi normalizzate |
| Bucket-history migliora la candidata `32/128/512` | FAIL | WMAE `+1,949680 pp` a 100k |
| Bucket-history corregge all-in/call | FAIL | entrambi i delta peggiorano alla capacità candidata |
| Perfect recall migliora il controllo ridotto | FAIL | WMAE `+1,593673 pp` |
| Memoria sostenibile con vantaggio di qualità | FAIL | payload da 3,72 a 6,67 volte il controllo |
| Equivalenza Monker | NOT EVALUATED | rappresentazione e albero postflop esterni ignoti |

## Decisione

Le rappresentazioni history-aware attualmente implementate sono scartate dalla candidata. `current_observation` con capacità `32/128/512` resta il miglior compromesso misurato, pur mantenendo il bias strutturale all-in/call.

Il gate R6 resta `FAIL`: nessuna variante disponibile raggiunge la qualità richiesta. Prima di sviluppare una nuova astrazione occorre ottenere o ricostruire valori d'azione root fisici per distinguere un errore di continuazione locale da una differenza dell'albero postflop Monker.
