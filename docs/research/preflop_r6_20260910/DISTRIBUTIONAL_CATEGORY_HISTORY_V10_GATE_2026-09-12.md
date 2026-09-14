# R6 — Gate dell'astrazione category-history v10

## Esito

`ENGINEERING_PASS / PAIRED_2M_PASS / COMPARATIVE_GATE_PASS / FINAL_R6_GATE_FAIL`.

La categoria della street precedente riduce la distanza fra seed oltre la soglia relativa del 20%, senza una regressione WMAE superiore a 0,5 punti. V10 supera quindi il confronto con V7, ma non il gate scientifico finale R6.

## Risultati a 2M

| Metrica | V7 seed 1 | V7 seed 2 | V7 media/coppia | V10 seed 1 | V10 seed 2 | V10 media/coppia |
|---|---:|---:|---:|---:|---:|---:|
| Iterazioni | 2.000.000 | 2.000.000 | — | 2.000.000 | 2.000.000 | — |
| WMAE contro Monker | 15,2418 pp | 14,5270 pp | **14,8844 pp** | 15,3165 pp | 15,1106 pp | **15,2136 pp** |
| TV contro Monker | 38,1045 pp | 36,3174 pp | 37,2110 pp | 38,2913 pp | 37,7766 pp | 38,0339 pp |
| P95 TV | 97,9350 pp | 95,7112 pp | 96,8231 pp | 98,6642 pp | 98,0442 pp | 98,3542 pp |
| TV fra seed | — | — | **18,6455 pp** | — | — | **14,8441 pp** |
| Infoset | 998.535 | 996.278 | — | 2.491.178 | 2.482.134 | — |
| Payload numerico | 147.547.440 B | 147.461.328 B | — | 362.872.944 B | 361.473.984 B | — |
| Bucket occupati F/T/R | 15/61/145 | 15/62/146 | — | 15/62/145 | 15/62/146 | — |
| Solve | 2.077,13 s | 2.077,46 s | — | 1.897,57 s | 1.485,75 s | — |

V10 riduce la TV fra seed di `3,8014 pp`, pari al `20,39%`. La WMAE media peggiora di `0,3292 pp` e la P95 media di `1,5311 pp`: entrambe restano entro le tolleranze comparative congelate. Il costo è circa `2,46×` in infoset e payload rispetto a V7.

## Integrità

Entrambi gli artefatti esportano 20 nodi, 1.620 righe e 630 combo fisiche per nodo. Le history coincidono con l'albero Monker. Tutti i 4.617 valori d'azione esportati per seed sono finiti; l'errore massimo di normalizzazione è `2,22e-16` e la ricostruzione dei regret root ha errore zero.

| Seed | SHA-256 |
|---|---|
| 1 | `5800C46099019AACC0D06498A30D086B06351AD7E499C07FCFB1BB0F0CD88DA7` |
| 2 | `86491ECFEAFEABD71A0A25505E4F7F651FFCD924482D113E7C1FD0B6FD86CAFA` |

Il confronto esterno restituisce per entrambi `REJECTED / REFERENCE_CONFIG_INCOMPLETE`: il postflop Monker non è noto e il valore `normalized_nashconv=0` non è certificato.

## AA al CO root

| Seed | All-in | Raise 6a | Raise 10a | Call | Fold |
|---|---:|---:|---:|---:|---:|
| Frequenza 1 | 0,0127% | 0,0053% | 0,0050% | **99,9771%** | ~0% |
| EV 1 ± SE | 8,333 ± 4,088a | 5,583 ± 3,022a | 8,333 ± 4,088a | 9,222 ± 2,923a | −1,000 ± 0a |
| Frequenza 2 | 0,0158% | 8,7737% | 0,0041% | **91,2063%** | ~0% |
| EV 2 ± SE | 14,667 ± 10,342a | 3,667 ± 1,361a | 2,000 ± 0a | 11,873 ± 11,773a | −1,000 ± 0a |

Le frequenze sono la media lineare delle strategie prodotte durante il training. Gli EV della tabella sono una valutazione post-hoc con appena 12 campioni per azione nel seed 1 e 3 nel seed 2 per AA. Non sono il segnale che assegna retroattivamente le percentuali, e gli intervalli sono troppo larghi per ordinare con precisione call e raise.

## Decisione

V10 sostituisce V7 come candidata più stabile sul confronto accoppiato, ma non è qualificata: WMAE `15,2136 pp`, TV esterna `38,0339 pp`, P95 `98,3542 pp` e TV fra seed `14,8441 pp` restano molto sopra i gate finali `1/2/5/1 pp`.

Per istruzione dell'utente, anche V8 e V9 devono essere rieseguite a 2M su due seed prima di scegliere il prossimo intervento. Nessun limite RAM arbitrario può interromperle.
