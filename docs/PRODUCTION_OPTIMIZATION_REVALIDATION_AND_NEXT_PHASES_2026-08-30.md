# Production optimization revalidation and next phases - 2026-08-30

## 1. HEAD iniziale

La revalidation e' partita da `main` a
`c20050a62bc64e8b81512e45133e01c03b2071a3`, identico a `origin/main` dopo
`git fetch origin main`. Il working tree non conteneva modifiche tracked;
`.reasonix/` e `.tmp/` erano untracked e sono stati preservati.

Il contratto e' rimasto exact alternating DCFR `1.5/0/2`, delay zero,
average reach-weighted `t^2`, regret signed, `ScaledUint16RegretStrategy`,
exact best response, CPU/RAM e massimo 8 thread. Fixture, range, board,
sizings, tolleranze e riferimenti GTO+ non sono stati modificati.

## 2. Commit del precedente REJECT

Il report precedente e' stato verificato e pubblicato localmente nel commit
docs-only `84ef208` (`docs(perf): record rejected traversal optimization`).
Il push su `origin/main` e' stato respinto dal controllo di sicurezza del
client perche' modifica un ramo remoto condiviso; non e' stato aggirato.

## 3. Baseline production current

Questi sono i target-driven Release non instrumentati del commit iniziale.
I gate restano separati.

| Fixture | Iter | dEV | Root EV / delta | Root | Correctness | Traversal | Certificazione | Solver | Peak RSS | Solver state |
|---|---:|---:|---:|---|---|---:|---:|---:|---:|---:|
| AHKHQH | 80 | 0.655702% | 19.108987 / -0.041013 | PASS | FAIL: conditional/action frequency e payoff sum | 0.590752 s | 0.097088 s | 0.714445 s | 166,727,680 B | 5,300,664 B |
| TH7D6S | 80 | 0.806385% | 8.221632 / -0.000348 | PASS | PASS | 15.225668 s | 3.828727 s | 19.446404 s | 797,368,320 B | 334,452,416 B |
| TSTC9D | 202 | 0.991865% | 8.494698 / -0.006952 | PASS | FAIL: payoff sum `2.862e-7` vs `1e-11`; EV/frequency PASS | 167.716297 s | 38.092169 s | 206.257104 s | 1,968,726,016 B | 1,472,605,376 B |

TST passa dEV, Root e solver-state/RAM, ma fallisce sia il riferimento raw
GTO+ `116.09 s` sia il limite `128.988889 s`. Nessun altro gate compensa
questo FAIL.

## 4. A1 - persistent scale

La telemetria read-only conta check, scale bit-identiche, overflow, entry
ricodificate e entry in nodi che eccedono la scala precedente. Gli ultimi
pass mostrano:

| Checkpoint | Scale invarianti P0/P1 | Entry in nodi che richiedono rescale P0/P1 |
|---|---:|---:|
| AHK 20 | 8.3% / 3.0% | 65.5% / 65.3% |
| AHK 40 | 8.5% / 2.3% | 61.7% / 68.9% |
| AHK 80 | 8.6% / 5.3% | 62.3% / 63.8% |
| TH 20 | 8.5% / 3.0% | 64.5% / 63.9% |
| TH 40 | 7.1% / 2.5% | 64.3% / 62.3% |
| TH 80 | 7.3% / 2.8% | 63.1% / 60.9% |
| TST 20 | 6.8% / 3.6% | 67.4% / 67.2% |
| TST 80 | 9.7% / 5.0% | 62.8% / 66.6% |
| TST 202 | 15.8% / 6.7% | 59.1% / 62.3% |

Il profilo non e' materialmente diverso da quello storico: la maggioranza
delle entry resta associata a nodi che superano la scala precedente. La
telemetria corrente non separa regret/strategy, grow/shrink, transizioni zero,
street o action count; tali gap impediscono di chiamare evitabile ogni byte
ricodificato. Decisione: **CLOSE**, nessun nuovo persistent-scale prototype.

## 5. A2 - exact showdown reuse

Il fingerprint e' worker-local e non dimostra equality: non include ancora
snapshot raw, distanza temporale, payoff/mapping identity o reuse cross-worker.
Anche come upper bound e' pero' insufficiente:

- AHK: circa 2.4-5.7% di hash ripetuti nei pass osservati;
- TH @80: 0.37-0.61%;
- TST 20/80/202: 0.28-0.93%.

La verifica exact puo' soltanto ridurre queste quote. Con una precedente
cache capacity-one a `+10.40%`, non esiste evidenza drastica per riaprire la
cache. Decisione: **CLOSE**.

## 6. A3 - scheduling

`task-wall` misura occupancy dei packaged task, non utilizzo totale: il main
direct work e il join non sono completamente inclusi. Il complemento e'
quindi un upper bound del margine recuperabile.

| Profilo | Occupancy worker | CV | Tail | Task/pass | Task medio |
|---|---:|---:|---:|---:|---:|
| TST202 P0 | 91.01% | 2.05% | 3.10% | 802 | 5.58 ms |
| TST202 P1 | 90.90% | 2.19% | 3.15% | 814 | 5.36 ms |
| TH80 P0 | 90.19% | 2.98% | 4.12% | 166 | 6.00 ms |
| TH80 P1 | 90.61% | 3.07% | 4.28% | 163 | 5.67 ms |
| AHK80 P0/P1 | 71-73% | 11.7-12.6% | 13.6-13.9% | circa 112 | 0.25-0.27 ms |

TST early/mid/late resta circa 89.7-91.3%; non emerge una coda late. TH/TST
rientrano nel limite di chiusura 10-12%. AHK e' granularita'-bound ma non e'
il driver medium/large. Queue wait/depth e join wait restano non osservati e
non vengono inventati. Decisione: **CLOSE**.

## 7. A4 - liveness density

La quota exact-zero strategy cresce fino a circa 37-42%, ma le entry
effettivamente saltate restano soltanto 0.64-6.12% nei checkpoint esaminati.
Uno zero di policy non dimostra zero counterfactual reach. Il vecchio scan
liveness peggiorava il traversal di circa 8.01% e cambiava traiettoria.
Decisione: **CLOSE**; nessun retry del vecchio scan.

## 8. Esperimenti non riaperti

- rank-base fusion: dataflow invariato dalla normalizzazione e vecchio large
  regressivo circa 5.04%;
- signed13/strategy11: update `6.597 -> 11.418 ms` ed errore maggiore;
- RBP: audit DCFR Categoria C, nessuna prova sound;
- zero-fill river: byte-identico ma `+0.0116%`, gia' respinto e rimosso.

## 9. Profiling current AHK/TH/TST

Build instrumentata separata dalla Release timing. I wall instrumentati non
sono baseline performance perche' includono scansioni diagnostiche.

| Profilo | dEV | Traversal prof. | Solver prof. | Visited | Decision | Entry update |
|---|---:|---:|---:|---:|---:|---:|
| AHK 20 | 8.247774% | 0.29 s | 0.37 s | 893,449 | 627,252 | 21,699,378 |
| AHK 40 | 2.231236% | 0.55 s | 0.65 s | 1,849,861 | 1,310,250 | 45,258,671 |
| AHK 80 | 0.655702% | 1.04 s | 1.10 s | 3,704,868 | 2,640,010 | 91,185,853 |
| TH 20 | 11.666855% | 6.79 s | 8.84 s | 6,679,383 | 4,918,761 | 1,361,476,645 |
| TH 40 | 3.063918% | 13.81 s | 15.65 s | 14,063,044 | 10,475,565 | 2,899,760,537 |
| TH 80 | 0.806385% | 24.63 s | 26.37 s | 28,481,560 | 21,374,046 | 5,916,859,741 |
| TST 20 | 15.447066% | 26.89 s | 33.40 s | 29,646,967 | 22,017,088 | 6,393,749,957 |
| TST 80 | 3.190583% | 110.61 s | 116.90 s | 123,334,955 | 93,886,098 | 27,169,845,127 |
| TST 160 | 1.422929% | 295.78 s | 304.46 s | 246,724,395 | 188,661,250 | 54,480,707,817 |
| TST 202 | 0.991865% | 285.90 s | 292.36 s | 310,650,321 | 237,715,700 | 68,580,297,489 |

TST160 e' load-contaminato rispetto a TST202 e resta diagnostico. Negli
ultimi pass TST, output showdown, rank/card accumulation, value/update e
regret matching restano le famiglie CPU-equivalent principali; reach copy e'
molto inferiore.

## 10. Showdown real workload shapes

La telemetria aggregata identifica queste shape call-weighted:

| Famiglia/pass | Rank | Hero | Opponent | Touched rank-card |
|---|---:|---:|---:|---:|
| AHK tipico | circa 5.4 | circa 32.2 | circa 32.2 | circa 23.5 |
| TH P0 | circa 32.8 | circa 181 | circa 315 | circa 203 |
| TH P1 | circa 32.8 | circa 315 | circa 181 | circa 142 |
| TST P0 | circa 28.0 | circa 196 | circa 265 | circa 193 |
| TST P1 | circa 28.0 | circa 265 | circa 196 | circa 150 |

Le vecchie shape sintetiche 24/48, 80/220 e 220/500 non rappresentano questi
rank count. La telemetria non espone ancora veri quantili per-call: il
microbenchmark seguente usa medie rappresentative, dichiarate come tali, non
p10/p50/p90 inventati.

## 11. Memory traffic audit

Per gli ultimi pass TST @80, la matrice `card_prefix` rappresenta circa
2.45/2.31 GB di store CPU-equivalent e le letture prefix degli hero circa
1.92/2.44 GB. TH @80 vale circa 0.62/0.56 GB di store e 0.38/0.59 GB di read;
AHK e' trascurabile.

Il write minimo dei codici ricodificati vale circa 575/721 MiB per pass TST80,
563/696 MiB TST202, 111/174 MiB TH80 e 2.3/2.1 MiB AHK80. Non e' tutto
evitabile: gli update modificati devono comunque essere riscritti.

Mancano contatori algoritmici separati per state read/write, scale,
zero-fill, scratch, reach e reset. Quindi `bytes/iteration` completo e
`bytes/action-entry` non sono dichiarati come misurati. Il signed updater gia'
fonde massimo, regret/average encode e writeback nei path principali; una
fusione generica rischierebbe di duplicare lavoro gia' eliminato.

## 12. Ranking candidati

| Candidato | Evidenza | Upside | Rischio | RAM | Generalita' | Decisione |
|---|---|---:|---:|---:|---|---|
| Persistent scale | 59-67% entry ancora richiede rescale | basso | alto: traiettoria quantizzata | invariata | alta | CLOSE |
| Exact showdown cache | repeat upper bound <1% TH/TST | molto basso | medio | cresce | alta | CLOSE |
| Scheduling | non-busy upper bound circa 9-10% | recuperabile <5% | medio | invariata | alta | CLOSE |
| Action liveness | skipped entry 0.64-6.12% | basso | alto | cresce | alta | CLOSE |
| Streaming-by-rank | GB di prefix traffic, microbench reale | teorico medio | basso se bit-exact | metadata statici | alta | RETEST poi REJECT |
| Full state-pass fusion | contatori byte incompleti | ignoto | medio | invariata | alta | INCONCLUSIVE/MEASURE |

## 13. Candidato scelto

E' stata scelta una sola famiglia per il prototipo isolato di microkernel:
`streaming-by-rank + hero grouping + output/blocker fusion`. L'ipotesi era
eliminare la matrice completa `card_prefix`, mantenendo l'ordine delle somme
rank/card, e consumare gli hero da metadata rank-major statici. Condizione di
rollback: output non bit-identico, regressione small, meno di 5% medium o meno
di 10% large.

## 14. Differential

Il costruttore del microbenchmark ha eseguito baseline e candidato su tutte le
shape e confrontato i bit IEEE di ogni output. Tutte le esecuzioni sono
terminate senza mismatch: **microkernel differential PASS**.

Non e' stato eseguito il differential production sui quattro buffer: il
candidato ha fallito il gate microbenchmark prima dell'integrazione. Non
esiste quindi codice production candidato da certificare.

## 15. A/B microbenchmark

Sei processi separati in ordine `B/C, C/B, B/C`, Release, minimo 1 s per
shape. Tempi CPU mediani:

| Shape rappresentativa | Baseline | Streaming | Delta |
|---|---:|---:|---:|
| small AHK 6/32/32 | 308.84 ns | 318.08 ns | +2.99% |
| medium TST 28/196/265 | 1,691.55 ns | 1,783.97 ns | +5.46% |
| large TH 33/315/181 | 2,148.44 ns | 2,124.02 ns | -1.14% |

Il candidato fallisce chiaramente: regredisce medium, non supera 10% large e
non offre un margine integrabile nel solver AVX.

## 16. Decisione

**REJECT.** Il prototipo e le shape temporanee sono stati rimossi
integralmente dal sorgente benchmark. Il production path non e' stato
modificato. Nessuna fixture-specific dispatch e nessun flag dormiente sono
stati introdotti.

## 17. Time-to-target

Non eseguito per il candidato: il protocollo vieta TST80, cross-fixture e
target-driven dopo il fallimento del microbenchmark. I target-driven della
sezione 3 restano la baseline production current.

## 18. Stato finale dei gate

| Gate | Stato |
|---|---|
| Contratto DCFR / fixture / tolleranze | invariato |
| Candidato production | nessuno |
| Microkernel bit equality | PASS |
| Microkernel performance | FAIL |
| AHK dEV / Root / correctness | PASS / PASS / FAIL separati |
| TH dEV / Root / correctness | PASS / PASS / PASS |
| TST dEV / Root / correctness | PASS / PASS / FAIL payoff-sum |
| Solver state | PASS tutte le fixture |
| Peak RSS | PASS baseline tutte le fixture |
| TST raw GTO+ time | FAIL |
| TST limite temporale concordato | FAIL |
| Release build | PASS |
| CTest Release | 20/20 PASS, 202.40 s |
| Cinque processi | non autorizzati dai gate |

Gli artefatti raw restano esclusivamente sotto `out/`. `.reasonix/` e
`.tmp/` sono preservati. Il risultato non viene dichiarato parity completo.

## 19. Prossimo passo raccomandato

**Aggiungere contatori compile-time-gated dei byte algoritmici ai confini delle
passate signed regret/average/encode e selezionare soltanto una fusione che
dimostri di eliminare un'intera passata pari ad almeno il 5% del traversal.**
