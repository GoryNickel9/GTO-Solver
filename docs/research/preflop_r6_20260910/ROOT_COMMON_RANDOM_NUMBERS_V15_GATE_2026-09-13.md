# R6 — Gate V15 Common Random Numbers

Data: 2026-09-13  
Esito: `ENGINEERING_PASS / COST_PASS / LOCAL_AUDIT_PASS / QUALITY_GATE_FAIL`

## Risultato

V15 completa due run indipendenti da 2.000.000 iterazioni con quattro rollout root e Common
Random Numbers. Il tempo torna nell'ordine di V13: `39,15` e `35,58 minuti`, contro i `139,43`
minuti della V14 eseguita con il backend sbagliato.

CRN riduce la TV della strategia media fra seed da `12,6370` a `10,9453 pp` e quella della policy
corrente da `14,8250` a `11,1472 pp`. Il miglioramento è reale, ma non raggiunge il limite di
`5 pp`. Anche la WMAE resta lontana dal gate: `14,1312` e `13,7631 pp`. V15 non entra nel viewer.

## Correzione del costo V14

V13 usava `seven_card_table_v1:2236291214962974841`. La run V14 aveva omesso
`--seven-card-table` ed era ricaduta su `exact_hand_evaluator_oracle_v1`. I due backend producono
la stessa equity esatta, ma hanno costi diversi. Non è quindi corretto attribuire a K=8 l'intero
rapporto di tempo `4,195x` osservato in V14.

Una prima run V15 con lo stesso errore è stata fermata prima dell'export. Le due run valide
serializzano entrambe il fingerprint della tabella V13. Questo chiude il falso vincolo dei 140
minuti senza ridurre le iterazioni o cambiare l'astrazione.

## Implementazione

L'opzione `root_common_random_numbers`, disattivata per default:

- usa lo stesso seed di continuazione per le cinque azioni root nello stesso rollout;
- conserva la distribuzione marginale di ogni valore d'azione;
- mantiene K=4 e gli update medi simmetrici per CO e BTN;
- richiede un batch congelato e registra `root_common_random_numbers_v1` nell'ID algoritmo.

Non cambiano albero, range, size, payoff, rake, bucket o metodi exact. Monker non viene letto dal
trainer.

## Coppia 2M

| Metrica | Seed 1 | Seed 2 | Media/coppia |
| --- | ---: | ---: | ---: |
| Iterazioni | 2.000.000 | 2.000.000 | — |
| Solve | 2.348,70 s | 2.134,81 s | 2.241,76 s |
| Solve | 39,15 min | 35,58 min | 37,36 min |
| Root EV ± SE | -0,12239 ± 0,05103a | -0,13887 ± 0,05101a | — |
| Infoset | 1.567.910 | 1.564.841 | — |
| Payload numerico | 249.066.144 B | 248.092.560 B | — |
| WMAE contro Monker | 14,1312 pp | 13,7631 pp | **13,9472 pp** |
| TV contro Monker | 35,3280 pp | 34,4078 pp | **34,8679 pp** |
| P95 TV | 92,6791 pp | 94,7986 pp | **93,7389 pp** |
| TV media fra seed | — | — | **10,9453 pp** |
| TV corrente fra seed | — | — | **11,1472 pp** |

Il confronto esterno resta `REJECTED / REFERENCE_CONFIG_INCOMPLETE`: non dimostra equivalenza con
il gioco postflop Monker e serve solo come diagnostica.

## Decomposizione della TV

| Componente | Strategia media | Policy corrente |
| --- | ---: | ---: |
| TV totale | 10,9453 pp | 11,1472 pp |
| Gap medio non superiore a 0,1a | 10,2475 pp | 11,1472 pp |
| Gap medio almeno 0,5a | 0,0001 pp | 0,0000 pp |
| Massa TV per gap EV | 0,00497a | 0,00307a |

Il `93,62%` della TV media e il `100%` della TV corrente ricadono fra azioni quasi indifferenti.
CRN riduce il rumore comune, ma non stabilizza la selezione fra più azioni con valore quasi uguale.
Forzare le frequenze verso Monker non sarebbe una correzione valida.

## Audit corrente contro corrente

| Controllo | Seed 1 | Seed 2 |
| --- | ---: | ---: |
| Righe medie materiali | 156 | 159 |
| Righe correnti materiali | 2 | 2 |
| Correnti con reach propria almeno 1% | 0 | 2 |
| Correnti con reach pubblica almeno 1% | **0** | **0** |
| Righe medie passate all'azione migliore nella corrente | 155 | 157 |

L'audit exact passa il gate locale già superato da V13. Le righe storiche della media CFR non sono
un errore dell'enumeratore e non vengono cancellate retroattivamente.

## Validazione

| Controllo | Esito |
| --- | --- |
| Build Release dei target modificati | PASS |
| Test CRN mirato | PASS, `R6_HU_PREFLOP_PARALLEL_TESTS`, 2.088 assertion |
| Suite HU Release | PASS, 11/11 |
| Due run complete da 2M | PASS |
| Backend evaluator identico a V13 | PASS |
| Export preflop | PASS, 20 nodi, 1.620 righe, 4.617 EV per seed |
| Normalizzazione massima | PASS, `3,33e-16` |
| Identità cache exact | PASS per entrambi i seed |
| Policy postflop | PASS, due file integri |
| Audit corrente su reach pubblica almeno 1% | PASS, `0/0` |
| Gate tempo | PASS, entrambi sotto 60 minuti |
| Gate TV fra seed | **FAIL, 10,9453 pp > 5 pp** |
| Gate WMAE | **FAIL, entrambi sopra 5 pp** |

## Artefatti

| Artefatto | SHA-256 |
| --- | --- |
| candidato seed 1 | `F479525FB5B90F5CD299E813C15D5FE5C3E388932CBC80F3882BB066C0EF205B` |
| policy seed 1 | `4A6F0AE915B86897D95F13BDF1DA5290D0B04116CF90F6B8A54797917522E9E7` |
| candidato seed 2 | `0580174F67905F3F999FD560FDD50B35AFE968D5858AB394C816E55C81F1E464` |
| policy seed 2 | `F5F256F435D006AC4EC7F46C08D9C76E8C69A473085965AFC8CE7236741E07CC` |
| analisi coppia | `2CC9C83F28968BCDFB96DA342D756A20ED5678C2FC9B7781CA4C23099E1B1B12` |
| audit policy | `F0C06D0FE9E78B9E8021FB31B7A96E6E089E9B23514E620287FC9C62BBAF68FC` |
| decomposizione TV | `5B3C58BAF93D004FD0F1805F86BE902CB7DA5B87D8CECEF24EE84F1A71570865` |

## Decisione

CRN resta disponibile come modalità sperimentale: migliora la stabilità con un costo medio solo
del `5,8%` superiore a V13. Non è promossa come nuova soluzione perché fallisce TV e WMAE.

Il viewer resta su V13 seed 2: è la migliore soluzione singola replicata contro il riferimento
(`13,2722 pp`) e V15 non supera il proprio gate preregistrato. Il prossimo esperimento deve
stratificare, senza ulteriori rollout, la prima risposta avversaria dopo ogni azione root. Questo
attacca la copertura dei rami che CRN correla ma continua a campionare singolarmente.
