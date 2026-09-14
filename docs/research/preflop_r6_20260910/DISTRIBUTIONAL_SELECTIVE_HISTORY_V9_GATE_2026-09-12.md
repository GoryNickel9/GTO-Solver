# R6 — Gate dell'astrazione selective-history v9

## Esito

`ENGINEERING_PASS / PAIRED_2M_COMPLETE / COMPARATIVE_GATE_FAIL / FINAL_R6_GATE_FAIL`.

> Aggiornamento 12 settembre 2026: il limite sperimentale di `512 MiB` non era un requisito fissato dall'utente e non è più un gate qualificante. Il risultato seguente resta valido come misura storica a 250k, ma la decisione di non eseguire 2M è superata. V9 deve essere ritestata a 2M su due seed.

Il retest richiesto è ora completo. V9 ottiene la migliore stabilità fra seed osservata, ma peggiora la WMAE oltre la tolleranza comparativa e non passa R6.

Il v9 supera tutti i controlli di correttezza, ma il primo seed a 250k usa `783.548.208 B` di solo payload numerico. Il limite congelato è `512 MiB`, pari a `536.870.912 B`. Il secondo seed non viene eseguito perché il vincolo di capacità è già violato in modo deterministico.

## Risultato del seed 1

| Metrica | v7 current-observation | v9 selective-history |
|---|---:|---:|
| Iterazioni | 250.000 | 250.000 |
| Infoset blueprint | 799.600 | 5.406.120 |
| Infoset BR | — | 35.187 |
| Payload blueprint | 115.142.400 B | 778.481.280 B |
| Payload BR | — | 5.066.928 B |
| Payload numerico totale | 119.062.080 B | **783.548.208 B** |
| Tempo | 293,294 s | 285,160 s |
| WMAE contro Monker | 19,3968 pp | 22,4664 pp |
| TV contro Monker | 48,4920 pp | 56,1659 pp |
| P95 TV | 96,8198 pp | 99,6099 pp |

Il payload dichiarato non comprende overhead dell'allocator, tabella hash o cache. Il consumo reale è quindi superiore al dato del gate.

## Integrità

L'artefatto dichiara v9, K=4, continuation mean e symmetric traverser mean. Contiene tutti i `20` nodi preflop e `1.620` righe classe-nodo. Probabilità ed EV sono finiti; l'errore massimo di normalizzazione è `3,33e-16`.

Artefatto: `v9_selective_history_symmetric_mean4_250k_seed1_monetary_v2.json`, SHA-256 `6F60AFDB159811CC39CE8F99CFC0F1C6B8441EC5023D88372A98767FD902B937`.

Il confronto esterno resta `REJECTED / REFERENCE_CONFIG_INCOMPLETE`.

## Decisione

## Risultati 2M

| Metrica | V7 seed 1 | V7 seed 2 | V7 media/coppia | V9 seed 1 | V9 seed 2 | V9 media/coppia |
|---|---:|---:|---:|---:|---:|---:|
| Iterazioni | 2.000.000 | 2.000.000 | — | 2.000.000 | 2.000.000 | — |
| WMAE contro Monker | 15,2418 pp | 14,5270 pp | **14,8844 pp** | 16,9192 pp | 16,2933 pp | **16,6062 pp** |
| TV contro Monker | 38,1045 pp | 36,3174 pp | 37,2110 pp | 42,2979 pp | 40,7333 pp | 41,5156 pp |
| P95 TV | 97,9350 pp | 95,7112 pp | 96,8231 pp | 98,8516 pp | 95,3136 pp | 97,0826 pp |
| TV fra seed | — | — | **18,6455 pp** | — | — | **13,9283 pp** |
| Infoset | 998.535 | 996.278 | — | 9.646.698 | 9.648.689 | — |
| Payload numerico | 147.547.440 B | 147.461.328 B | — | 1.394.067.312 B | 1.394.264.592 B | — |
| Solve | 2.077,13 s | 2.077,46 s | — | 3.960,08 s | 4.496,91 s | — |

V9 riduce la TV fra seed di `4,7172 pp`, pari al `25,30%`, ma peggiora la WMAE media di `1,7219 pp`. Fallisce quindi il vincolo che impedisce una regressione superiore a `0,5 pp` nella metrica primaria non scelta. Il costo sale a circa `9,67×` gli infoset e `9,45×` il payload del V7.

## Integrità e artefatti 2M

Entrambi i file esportano 20 nodi, 1.620 righe, history identiche a Monker e 4.617 valori d'azione finiti. L'errore massimo di normalizzazione è `2,22e-16` e la ricostruzione dei regret root ha errore zero.

| Seed | SHA-256 |
|---|---|
| 1 | `4562E37F4C0DBCC44FF86F3E1CB850BC9E7352B5DE9403B9B6AADFBF80024FA8` |
| 2 | `5EB00FB08846629685FD416B2B205A8076551701103F3316D10B555757C9718B` |

I confronti esterni restano `REJECTED / REFERENCE_CONFIG_INCOMPLETE` e il NashConv esportato non è certificato.

## Decisione aggiornata

V9 non viene promossa: è più stabile, ma la memoria del bucket precedente separa troppi stati e peggiora materialmente l'accordo medio. Il risultato sostiene una storia più compatta, come la categoria v10, non il recall del bucket intero.

Il prossimo candidato è v11: mapping street-adaptive v8 per la street corrente e categoria esatta della street precedente secondo v10. Il protocollo deve essere congelato prima del codice e la validazione deve usare due seed a 2M.

Il prossimo candidato deve conservare soltanto la categoria visibile della street precedente, non l'intero bucket. Questa informazione distingue una mano già formata da una appena migliorata, ma limita il dominio storico a nove valori.
