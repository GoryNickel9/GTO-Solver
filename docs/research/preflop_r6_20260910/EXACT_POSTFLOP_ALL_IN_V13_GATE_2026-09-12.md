# R6 — Gate V13 con enumerazione postflop all-in lazy

Data: 2026-09-12  
Esito: `ENGINEERING_PASS / PAIRED_2M_PASS / POLICY_CONDITIONING_AUDIT_PASS / COMPARATIVE_GATE_FAIL / R6_GATE_FAIL`

## Risultato

V13 enumera tutti i runout legali quando un all-in termina sul Flop o sul Turn e riusa i conteggi
`win/tie/loss` tramite una cache bounded lazy. Il River richiede un solo showdown. La modifica
elimina una sorgente reale di rumore senza cambiare albero, size, range, bucket o payoff.

Il candidato supera lo screen di efficienza, completa due run indipendenti da 2.000.000 di
iterazioni e riduce la TV fra seed da `16,9686 pp` di V8 e `17,3695 pp` di V12 a `12,6370 pp`.
La policy corrente finale non conserva errori Call/Fold materiali sui rami con reach pubblica
almeno `1%`. La strategia media rimane però distante dal riferimento Monker: WMAE media
`14,0837 pp`, TV media `35,2092 pp` e P95 media `96,7074 pp`.

V13 diventa la migliore sorgente di ricerca nel viewer, ma non è una soluzione qualificata e non
autorizza il passaggio a R7.

## Implementazione

- `HuPreflopPostflopAllInExpectationMode` separa runout campionato, Turn esatto e Flop+Turn
  esatti;
- l'enumeratore fisico produce `406/28/1` completamenti da Flop, Turn e River;
- la chiave della cache contiene mani fisiche ordinate, board visibile e street, canonicalizzati
  sulle 24 permutazioni globali dei semi;
- la cache ha capacità totale dichiarata di 1.000.000 entry, ripartita fra i worker, ed espone hit,
  miss, picco ed eviction;
- l'EV monetaria viene ricalcolata con `settle_terminal`; la cache non contiene range, history,
  pot, strategia o payoff.

La tabella completa di tutti gli stati fisici non viene materializzata. Le chiavi grezze sarebbero
circa 1,75 miliardi sul Flop, 12,7 miliardi sul Turn e 71,2 miliardi sul River. Il lookup lazy
calcola soltanto gli stati realmente raggiunti e mantiene un limite RAM verificabile.

## Screen accoppiato

I run a 100k servono esclusivamente a selezionare il trattamento dell'attesa. Non sono versioni
pubblicate.

| Modalità | Tempo medio | Root SE media | TV fra seed | Varianza x tempo |
| --- | ---: | ---: | ---: | ---: |
| Runout campionato | 75,35 s | 0,08687a | 34,27 pp | 1,000 |
| Turn esatto, senza cache | 97,13 s | 0,08099a | 32,37 pp | 1,120 |
| Flop+Turn esatti, senza cache | 229,43 s | 0,06655a | 31,47 pp | 1,787 |
| Turn esatto, cache | 88,73 s | 0,08099a | 32,37 pp | 1,024 |
| **Flop+Turn esatti, cache** | **109,80 s** | **0,06655a** | **31,47 pp** | **0,855** |

La cache rende Flop+Turn esatto il solo challenger con efficienza migliore della baseline:
`-14,5%` di varianza per unità di tempo. Questo risultato autorizza la coppia 2M senza usare
Monker come criterio di selezione.

## Coppia 2M

| Metrica | Seed 1 | Seed 2 | Media/coppia |
| --- | ---: | ---: | ---: |
| Iterazioni | 2.000.000 | 2.000.000 | — |
| Solve | 1.994,12 s | 2.244,05 s | 2.119,08 s |
| Root EV ± SE | -0,11774 ± 0,05015a | -0,05915 ± 0,05017a | — |
| Infoset | 1.568.010 | 1.567.923 | — |
| Payload numerico | 248.954.832 B | 248.552.928 B | — |
| Scratch parallelo di picco | 6.682.408 B | 6.663.608 B | — |
| WMAE contro Monker | 14,8951 pp | 13,2722 pp | **14,0837 pp** |
| TV contro Monker | 37,2378 pp | 33,1805 pp | **35,2092 pp** |
| P95 TV | 95,4971 pp | 97,9177 pp | **96,7074 pp** |
| TV fra seed | — | — | **12,6370 pp** |

Il confronto esterno resta `REJECTED / REFERENCE_CONFIG_INCOMPLETE`: non conosciamo albero e
abstraction postflop, build, stopping rule o metrica di convergenza di Monker. Le metriche
descrivono la distanza dall'export; non identificano da sole un errore del motore locale.

## Telemetria dell'enumerazione

| Metrica | Seed 1 | Seed 2 |
| --- | ---: | ---: |
| Valutazioni Flop | 29.815.810 | 31.282.635 |
| Valutazioni Turn | 76.014.428 | 75.241.562 |
| Runout Flop | 12.105.218.860 | 12.700.749.810 |
| Runout Turn | 2.128.403.984 | 2.106.763.736 |
| Cache hit | 92.231.838 | 92.945.836 |
| Cache miss | 13.598.400 | 13.578.361 |
| Hit rate | 87,1507% | 87,2533% |
| Picco entry | 1.000.000 | 1.000.000 |
| Eviction | 12.480.578 | 12.462.251 |

Per entrambi i seed vale `hit + miss = valutazioni Flop + valutazioni Turn`. Il picco non supera
la capacità configurata. Il numero di eviction mostra perché una tabella illimitata non è una
scelta accettabile, mentre l'hit rate dimostra che la cache bounded riusa la maggior parte degli
stati richiesti.

## Audit della policy corrente

Una riga è materiale quando assegna almeno il `5%` all'azione Call/Fold esattamente inferiore e
il gap è almeno `0,1a`.

| Controllo corrente contro corrente | Seed 1 | Seed 2 |
| --- | ---: | ---: |
| Righe materiali | 0 | 1 |
| Materiali con reach propria ≥1% | 0 | 1 |
| Materiali con reach pubblica ≥1% | **0** | **0** |
| Righe medie materiali passate all'azione migliore nella corrente | 159 | 168 |

L'unico residuo corrente è `KTo` al nodo 13 nel seed 2: Fold `100%`, gap `0,23362a`, reach
propria `1,338%`, reach avversaria `3,071%` e reach pubblica `0,0411%`. È fuori dal sotto-gate
dei rami pubblicamente raggiunti. V12 aveva ancora `11/14` errori materiali con reach pubblica
almeno `1%`; la riduzione a zero su entrambi i seed è una correzione misurabile.

La strategia media conserva `159/169` righe materiali perché integra anche policy storiche. V13
corregge la qualità dell'aggiornamento corrente, non cancella retroattivamente la massa accumulata
dall'averaging CFR.

## Validazione

| Controllo | Esito |
| --- | --- |
| Build Release dei target modificati | PASS |
| Suite HU preflop Release completa | PASS, 15.950 assertion |
| Test enumeratore `406/28/1`, complementarità, conditioning e input invalidi | PASS |
| Test cache disattiva/attiva con output bit-identico | PASS |
| Due run complete da 2M | PASS |
| Export preflop | PASS, 20 nodi, 1.620 righe, 4.617 EV per seed |
| Normalizzazione massima | PASS, `3,33e-16` |
| Identità telemetria cache | PASS |
| Policy postflop | PASS, caricamento e query live su 81 classi |
| Viewer | PASS, default V13 seed 2 / 2M e 10.060 nodi decisionali postflop |
| Audit corrente contro corrente sui rami con reach pubblica ≥1% | PASS, `0/0` |
| Gate comparativo R6 | **FAIL** |

## Artefatti

| Artefatto | SHA-256 |
| --- | --- |
| candidato seed 1 | `7E3F9E5BA078BA6105ECA9F79CA361173A50EB025A4CDB28D1D0B55DE995EDEC` |
| candidato seed 2 | `1F584CD50F764DD83942B2AA52FABF327F3CEAC3614316CD7163C273EC198A06` |
| policy seed 1 | `DD01796BD0E2F85DB24F28B9017B41A1DBEB9C8FCFC6FF69D69E1E5AE71ECD89` |
| policy seed 2 | `E52303F8265D0B261665E5238C8EE4EBC774792EF13DA20AD1C75A0F5B7708E4` |

I risultati strutturati sono `v13_exact_postflop_allin_cached_2m_pair_analysis_v1.json`,
`v13_exact_postflop_allin_cached_2m_pair_policy_audit_v1.json` e i due file
`v13_exact_postflop_allin_cached_2m_seed*_training_diagnostic_v1.json`.

## Decisione

L'enumerazione lazy degli all-in postflop è promossa come opzione di ricerca: è esatta nel dominio
dichiarato, riduce la varianza per unità di tempo e migliora nettamente la stabilità della policy
corrente. Non viene attivata nei default di produzione.

V13 è la candidata locale più promettente e il viewer la usa come sorgente iniziale. R6 resta
aperto perché la strategia media è ancora troppo lontana dai range esterni e varia troppo fra
seed.

La successiva [decomposizione della TV fra seed](V13_SEED_TV_DECOMPOSITION_2026-09-12.md) mostra
che la policy corrente è più instabile della media (`14,8250` contro `12,6370 pp`) e che il `96,0%`
della TV media riguarda azioni con gap medio non superiore a `0,1a`. L'averaging ritardato non è
quindi il primo intervento. Il [protocollo V14](ROOT_K8_VARIANCE_V14_PROTOCOL_2026-09-12.md)
pre-registra due run da 2M con otto rollout root, lasciando invariati gioco, astrazione ed equity.
