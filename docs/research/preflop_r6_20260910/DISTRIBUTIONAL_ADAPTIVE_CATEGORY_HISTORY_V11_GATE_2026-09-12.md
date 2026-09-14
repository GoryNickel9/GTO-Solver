# R6 — Gate adaptive-category-history V11

## Esito

`ENGINEERING_PASS / PAIRED_2M_PASS / COMPARATIVE_GATE_FAIL / FINAL_R6_GATE_FAIL`.

V11 conserva il mapping street-adaptive di V8 e aggiunge la categoria esatta della street precedente. Le due run richieste sono complete, ma l'aumento di risoluzione non migliora né la WMAE media né la stabilità rispetto a V10. V11 non viene promossa.

## Risultati a 2M

| Metrica | V11 seed 1 | V11 seed 2 | Media/coppia |
|---|---:|---:|---:|
| Iterazioni | 2.000.000 | 2.000.000 | — |
| WMAE contro Monker | 15,9367 pp | 14,8440 pp | **15,3904 pp** |
| TV contro Monker | 39,8417 pp | 37,1101 pp | **38,4759 pp** |
| P95 TV | 96,7248 pp | 93,5395 pp | **95,1322 pp** |
| Errore massimo azione root | 28,4972 pp | 25,2996 pp | 26,8984 pp |
| Errore assoluto root EV | 0,2648a | 0,2109a | 0,2379a |
| TV fra seed | — | — | **15,0952 pp** |
| Infoset | 3.788.481 | 3.786.493 | — |
| Payload numerico | 562.115.952 B | 561.913.632 B | — |
| Bucket occupati F/T/R | 29/125/213 | 29/125/213 | — |
| Solve | 2.012,19 s | 2.124,51 s | — |

Entrambi i seed usano Linear MCCFR, media simmetrica K=4, MC8, capacità `32/128/512`, batch 32 e otto worker. Nessuna run ridotta è stata usata per la decisione.

## Confronto congelato con V10

| Metrica | V10 | V11 | Variazione V11 | Soglia | Esito |
|---|---:|---:|---:|---:|---|
| WMAE media | 15,2136 pp | 15,3904 pp | +0,1768 pp | miglioramento ≥0,5 pp | FAIL |
| TV fra seed | 14,8441 pp | 15,0952 pp | +0,2511 pp | riduzione ≥20% | FAIL |
| Peggioramento della metrica non scelta | — | — | entro 0,5 pp | ≤0,5 pp | PASS |
| Integrità dei due export | — | 20 nodi per seed | completa | completa | PASS |

Il gate richiedeva il miglioramento di almeno una delle prime due metriche. V11 non ne migliora nessuna. La WMAE media resta inoltre peggiore di V8/2M, pari a `14,6858 pp`; il sito continua quindi a pubblicare V8.

## Integrità e riproducibilità

Ogni candidato contiene 20 nodi preflop, 1.620 righe, 630 combo fisiche per nodo e 4.617 EV d'azione finiti. Le history coincidono con quelle Monker e l'errore massimo di normalizzazione è `3,33e-16`.

| Artefatto | Dimensione policy | Fingerprint policy | SHA-256 candidato | SHA-256 policy |
|---|---:|---|---|---|
| Seed 1 | 871.455.293 B | `fnv1a64:c6c715fa05670c6a` | `508875E91DEC30742F91282002BE863A1780B36A1C8F9CFF8B0A18D6F4A3D53C` | `35D15F0526B589AE20D24D203E3FF52FEC566905D398979F5ED928BBB9E9AD39` |
| Seed 2 | 870.888.424 B | `fnv1a64:8916ca68c65d3b49` | `D18C2622E7F3869F69BA56981418067AE19178D7D062ED9A28979A3D4AE6F3EC` | `4019998DDFFD592B01B4057E3A2852C55751DC7AF4BF07FF5AC7954F25E9EB6B` |

Il confronto esterno resta `REJECTED / REFERENCE_CONFIG_INCOMPLETE`: non conosciamo il contratto postflop Monker. `normalized_nashconv=0` non è una certificazione di convergenza.

## AA al CO root

V11 assegna al call il `99,9675%` nel seed 1 e il `99,9960%` nel seed 2. Gli EV post-hoc usano rispettivamente 188 e 189 campioni per azione: nel seed 1 raise 10a vale `7,954 ± 1,358a` e call `7,629 ± 1,599a`; nel seed 2 call vale `10,143 ± 1,629a` e le altre azioni aggressive circa `8,2a`. Questi intervalli si sovrappongono. Le frequenze sono la media lineare delle strategie apprese durante il training, non una trasformazione retroattiva degli EV mostrati.

## Decisione

La memoria di categoria aumenta lo stato da circa 1,55 milioni di infoset in V8 a 3,79 milioni in V11, senza ridurre l'errore esterno o la divergenza fra seed. L'ipotesi V11 è respinta. V8 resta la versione più vicina a Monker fra le coppie 2M complete; V10 resta la più stabile fra le versioni già promosse dal proprio gate comparativo.

