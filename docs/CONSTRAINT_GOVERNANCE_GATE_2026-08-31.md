# Constraint governance gate — 2026-08-31

## Analisi

### Obiettivo e stop condition

Questa fase non cerca un'altra ottimizzazione production. Determina il minimo
insieme di vincoli da riaprire affinché TSTC9D possa tornare nella regione
`solver <= 128,988889 s`, mantenendo distinti fattibilità tecnica, desiderabilità
di prodotto e comparabilità con GTO+.

Il loop applicato è stato:

```text
FREEZE CURRENT EVIDENCE
-> ENUMERATE CONSTRAINTS
-> MODEL SINGLE RELAXATIONS
-> ELIMINATE INSUFFICIENT OPTIONS
-> MODEL MINIMAL PAIRS
-> MODEL MINIMAL TRIPLES ONLY IF REQUIRED
-> BUILD PARETO FRONTIER
-> STRESS-TEST ASSUMPTIONS
-> RANK GOVERNANCE OPTIONS
-> RECOMMEND DECISION
-> STOP
```

Esito: **A. SAME CONTRACT FEASIBLE WITH RESOURCE CHANGE**, ma solo come soglia
model-based da verificare su hardware candidato. La prima alternativa a severità
massima S1 è `RAM >= 3.432.437.888 B + progress detection economica + exact BR
finale`, ma è soltanto `POTENTIALLY SUFFICIENT`: dipende dalla proiezione replay
ottimistica del backend direct float32/float32 e richiede una nuova validazione
solver. Nessun contratto viene riaperto da questo documento.

### Repository state

Verifica eseguita dopo `git fetch origin main`:

| Campo | Valore |
|---|---|
| branch | `main` |
| HEAD | `5e65e90df15d03a849808e905fbcccefdba5cf23` |
| `origin/main` | `5e65e90df15d03a849808e905fbcccefdba5cf23` |
| divergenza `main...origin/main` | `0/0` |
| working tree iniziale | `.reasonix/` e `.tmp/` untracked |
| ultimi commit | `04d0335`, `6a5164c`, `5e65e90` presenti nell'ordine atteso |

`.reasonix/`, `.tmp/`, corpus replay e output diagnostici sono stati preservati.
Nessun reset e nessun push.

### Baseline autorevole E4

| Fixture | Iter | dEV | Root EV | Traversal | Certification | Solver | Limite | Gate |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| AHKHQH | 80 | 0,655665% | 19,108984 | n/d | n/d | 0,670928 s | 1,900000 s | PASS |
| TH7D6S | 80 | 0,806385% | 8,221632 | n/d | n/d | 17,645055 s | 19,622222 s | PASS |
| TSTC9D | 202 | 0,991863% | 8,494698 | 174,926380 s | 32,780800 s | 208,111772 s | 128,988889 s | FAIL |

TST eccede il limite di `79,122883 s`, rapporto `1,613409x` e `+61,3409%`.
Il target traversal autorevole, che sottrae soltanto la certification, è
`96,208089 s` (`174,926380 / 96,208089 = 1,818x`). La decomposizione completa
trova inoltre `T_other = 0,404592 s`; se questo residuo resta fisso, il budget
traversal rigoroso è `95,803497 s` e il moltiplicatore sale a `1,825887x`.
Il valore `96,208089 s` non viene sovrascritto: i due modelli rispondono a
domande leggermente diverse.

### Blocker ereditati e lower bound

Restano chiuse come primary path le famiglie elencate nel mandato: micro-loop
traversal, persistent scale, showdown cache, liveness, RBP, batching,
wavefront, tiling, sink, compiled/continuation traversal, AoSoA, exact state
retain/recompute/mixed, scale locali, compact formats e producer streaming
standalone. Production resta `ScaledUint16RegretStrategy`.

Input autorevole da
[`REAL_NODE_REPLAY_AND_PRODUCER_LOWER_BOUND_LOOP_2026-08-31.md`](REAL_NODE_REPLAY_AND_PRODUCER_LOWER_BOUND_LOOP_2026-08-31.md):

- 288 sample reali, 162.317 action entry, replay byte/bit exact;
- producer streaming standalone: ceiling micro ideale circa `1,041x`;
- ideal state: `93,299454 s` traversal, E0;
- ideal producer: `168,036868 s`, E0/E2-composed;
- joint ideal: `89,624835 s`, E0;
- float32/float32: `~119,98 s` traversal, 8 B/action, E2 projection, RAM FAIL;
- float32/16: `~154,54 s`, 6 B/action, E2 projection, RAM FAIL;
- 4 B/action: RAM-feasible ma numericamente instabile e più lento.

Questi bound chiudono il tentativo di usare un altro codec come variabile
nascosta della governance analysis.

## Modello di governance

### Inventario dei vincoli

| ID | Contratto corrente | Rilassamento esaminato | Classe |
|---|---|---|---|
| C1 | alternating DCFR exact `1.5/0/2`, average immediato reach-weighted `t^2` | altra schedule/parametri, gioco e BR exact | S2 |
| C2 | cap desktop 2 GB | cap sufficiente a 5/6/8 B/action | S1 |
| C3 | CPU-only, massimo 8 thread, i3-10100F 4C/8T | thread policy o hardware/core/bandwidth diversi | S1 |
| C4 | exact BR/dEV con cadence corrente | detection meno costosa e exact final BR | S0 |
| C5 | no sampling/bucketing, outcome e BR exact | sampling, abstraction, approximate state/BR | S3 |
| C6 | limite TST `128,988889 s` | limite/reference/target più permissivi | S4 |

S0 conserva il risultato esterno; S1 cambia l'envelope risorse; S2 cambia il
contratto solver; S3 cambia la semantica prodotto; S4 cambia il benchmark.

La funzione obiettivo è lessicografica, non una somma:

```text
G = (
  external_semantics_change_level,
  number_of_relaxed_constraints,
  maximum_relaxation_severity,
  projected_gate_fail_count,
  projected_tst_time_ratio,
  uncertainty,
  engineering_cost
)
```

### Livelli di evidenza

| Livello | Significato | Esempi usati qui |
|---|---|---|
| E0 | ideale teorico | state/producer gratuiti, zero certification |
| E1 | cost model/Amdahl | soglie hardware e composizioni di coppia |
| E2 | replay/micro misurato | pipeline float32 e producer ceiling |
| E3 | fixed solver misurato | scaling 1/2/4/8, schedule @135/@160/@170 |
| E4 | target-driven misurato | final-head TST @202 |

Una composizione eredita il livello più debole dei suoi termini. Nessun E0/E1
è presentato come certificazione E4.

### Time model

```text
T_solver = T_traversal + T_certification + T_other
         = 174,926380 + 32,780800 + 0,404592
         = 208,111772 s
```

Per scenari algoritmici:

```text
T_traversal(N) = N * c_average(N, schedule, state, hardware)
```

Il costo non è costante. Nel target corrente il cumulativo medio passa da
`0,7922 s/iter @20` a `0,8767 @80` e `0,8660 @202`; la strategy density rende
invalida l'estrapolazione del solo early cost. I modelli sotto usano il costo
medio reale @202 o pubblicano esplicitamente un range.

## Single-relaxation analysis

| Constraint | Minimal relaxation | Best-case TST | Realistic TST | Gate? | Severity | Confidence |
|---|---:|---:|---:|---|---|---|
| C1 algorithm | nuova schedule exact: convergenza entro `@120` con cadence corrente, oppure `@142` con final-only (che sarebbe però C1+C4) | `<=128,99 s` per definizione del boundary | nessuna schedule storica dimostra il boundary | no evidence-backed | S2 | bassa |
| C2 RAM | almeno 3.432.437.888 B per il punto 8 B/action | 153,165392 s con costi correnti | insufficiente evidenza solver; non può essere migliore del best model pubblicato | **NO** | S1 | medio-alta sul kill |
| C3 hardware | `>=1,615339x` effettivo su traversal+certification, o `>=1,825887x` sul solo traversal | 128,988889 s al boundary | dipende soprattutto da bandwidth e da nuova macchina | potenzialmente sì | S1 | media-bassa |
| C4 certification | final exact BR storico `5,439364 s`; zero-cert E0 | 175,330972 s con zero certification | 180,770336 s con final-only proxy | **NO** | S0 | alta sul kill |
| C5 approximation | speedup solver netto `>1,613409x`, più margine per degradation | sconosciuto | insufficient evidence | unknown | S3 | bassa |
| C6 time target | limite almeno `208,111772 s` più epsilon | 208,111772 s | meccanicamente sì | sì, non tecnico | S4 | alta |

`C3` è l'unica single relaxation a severità inferiore a S3 che può chiudere
senza cambiare algoritmo/state/certification, ma non esiste una misura E3/E4 su
un hardware candidato. La classificazione corretta è quindi `POTENTIALLY
SUFFICIENT — NEEDS NEW STUDY`, non `PROVEN SUFFICIENT`.

### Single-option kill table

| Option | Alone closes TST? | Evidence | Decision |
|---|---|---|---|
| RAM only | no; best model 153,165392 s | E2 replay + E1 composition | INSUFFICIENT |
| hardware only | sì al threshold `1,615339x` whole-compute | E1, scaling storico E3 | POTENTIALLY SUFFICIENT — NEEDS NEW STUDY |
| algorithm only | nessuna schedule storica arriva al boundary | E3 storico incompatibile/superseded | POTENTIALLY SUFFICIENT — NEEDS NEW STUDY |
| certification only | no, anche zero-cert dà 175,330972 s | E4 decomposition + E0 | INSUFFICIENT |
| approximation only | unknown | nessuna evidence repository comparabile | PRODUCT SEMANTICS CHANGE |
| time target only | mechanically yes | aritmetica E4 | BENCHMARK RELAXATION ONLY |

## RAM thresholds

TST contiene 366.890.152 action entry. Il modello direct rimuove le scale
node-global e conserva `497.316.672 B` di overhead non-state osservato.

| Direct state | State | Peak/cap minimo raw | GiB | Cap operativo prudente | Gate 2 GB |
|---:|---:|---:|---:|---:|---|
| 5 B/action | 1.834.450.760 B | 2.331.767.432 B | 2,171628 GiB | almeno 2,5 GiB | FAIL |
| 6 B/action | 2.201.340.912 B | 2.698.657.584 B | 2,513321 GiB | almeno 3 GiB | FAIL |
| 8 B/action | 2.935.121.216 B | 3.432.437.888 B | 3,196707 GiB | almeno 4 GiB | FAIL |

I cap raw sono minimi modellati, non margini operativi: allocator, build,
telemetria e variazione del peak motivano l'arrotondamento prudente.

Con RAM infinita e tutti gli altri vincoli invariati, il miglior punto reale
disponibile è float32/float32:

```text
T = 119,98 + 32,780800 + 0,404592 = 153,165392 s
ratio = 1,187431
```

Quindi **RAM-only è INSUFFICIENT AS SINGLE RELAXATION**. `Traversal PASS` non
equivale a `solver PASS`; in realtà `119,98 s` fallisce anche il budget
traversal autorevole `96,208089 s`.

## Hardware and thread thresholds

### Threshold richiesti

| Accelerazione applicata a | Equazione | Speedup minimo |
|---|---|---:|
| whole solver, residuo incluso | `208,111772 / 128,988889` | 1,613409x |
| traversal+certification, residuo fisso | `(174,926380+32,780800)/(128,988889-0,404592)` | 1,615339x |
| solo traversal, certification+residuo fissi | `174,926380/(128,988889-32,780800-0,404592)` | 1,825887x |
| solo traversal, modello autorevole senza residuo | `174,926380/96,208089` | circa 1,818x |

Il final-head riporta utilizzo CPU normalizzato `84,96%`; il run storico
`1.9/0/3` riportava `86,1%`. Lo scaling TST fixed breve 1/2/4/8 è
`1,00/1,84/3,13/3,97x`, da cui una frazione seriale Amdahl apparente circa
`0,087/0,093/0,145`. La gamma prudente per il large è `s=0,09..0,15`.

| Thread/core equivalenti | Ideale vs 8 | Amdahl vs 8 (`s=0,09..0,15`) | Bandwidth-limited, stesso BW | Interpretazione |
|---:|---:|---:|---:|---|
| 8 | 1,000x | 1,000x | 1,000x | contratto corrente |
| 12 | 1,500x | 1,160–1,229x | circa 1,00–1,10x, non misurato | richiede hardware, non solo setting |
| 16 | 2,000x | 1,262–1,387x | circa 1,00–1,15x, non misurato | richiede hardware e più bandwidth |

Le bande bandwidth-limited sono stress bounds, non forecast. L'evidenza
repository misura lower-bound state+reach `10,6 GB/s` sul large, contro una
stima storica di circa `20 GB/s`, senza includere scratch, cache-line overfetch
e write allocate; lo stesso hardware è descritto come bandwidth/latency-bound.
Più thread sul corrente i3-10100F 4C/8T non esistono, e vecchi smoke mostrano
che 8 thread possono essere neutri o peggiori di 6. `12/16 thread` significa
quindi sostituzione hardware.

Conclusione hardware: più core senza bandwidth non raggiunge il `1,615x`.
Serve un miglioramento effettivo congiunto di compute, scheduling e memory
subsystem, verificato end-to-end. Un candidato prudente dovrebbe mirare ad
almeno `1,70x` per non vivere esattamente sul boundary; questo è un requisito
di acquisto/test, non una prestazione prevista.

## Algorithm convergence thresholds

### Audit schedule storico

Tutti i run sotto sono E3, a certificazione finale unica e sul percorso
storico `1.9/0/3` o sue varianti; non sono direttamente componibili con il
final-head comune senza nuova misura.

| Schedule | Iter/dEV | Traversal sec/iter medio | Convergence trend | Time-to-target evidence | Contract delta |
|---|---:|---:|---|---|---|
| production `1.5/0/2` | @202 / 0,991863% | 0,865972 | 1,640%@120; 1,423%@160; 1,009%@200 | E4, target raggiunto @202 | nessuno |
| storico `1.9/0/3` | @160 / 1,078426%; @170 / 0,985760% | 0,814831 @160; 0,865078 @170 | raggiunge <1% @170 | E3 singolo, superseded | S2 |
| DCFR+ `1.5/0/2` | @135 / 1,541924% | 1,248179 | troppo lento e sopra target | nessuno | S2 |
| HS-DCFR 3.0 | @135 / 2,265369% | 0,880891 | insufficiente per iterazione | nessuno | S2 |
| `1.9/1/3` | @135 / 4,075340% | 0,767922 | più veloce, convergence degradata | nessuno | S2 |
| `1.9/0/5` | @135 / 1,263294% | 0,861044 | migliore delle altre @135 ma >1% | nessuno | S2 |
| `1.9/0/2` | @135 / 1,532120% | 0,879834 | peggiore di gamma 5 @135 | nessuno | S2 |

### Iteration budget

| Timing model | Boundary | `N_max` | Accelerazione convergence richiesta `202/N_max` |
|---|---|---:|---:|
| costo medio current target + quota costi current media | `128,988889 / (208,111772/202)` | 125 continuo; ultimo checkpoint reale @120 | 1,616x continuo; 1,683x a cadence reale |
| current traversal cost + exact final-only proxy 5,439364 s | `(128,988889-5,439364-0,404592)/0,865972` | 142 | 1,423x |
| current traversal cost + zero certification E0 | `(128,988889-0,404592)/0,865972` | 148 | 1,365x |
| best historical per-iter 0,767922 + sua final cert 5,265509 | cost envelope, non composizione autorizzata | 160 | 1,263x |
| best historical per-iter + zero cert E0 | cost envelope | 167 | 1,210x |

Il profilo storico `1.9/0/3` accelera le iterazioni target soltanto
`202/170 = 1,188x`, sotto tutti i boundary non-E0. Nessuna schedule provata si
avvicina al requisito algorithm-only con cadence corrente. Una nuova schedule
exact resta `RESEARCH-PLAUSIBLE`, non `PROVEN SUFFICIENT`.

## Certification thresholds

| Protocollo | Certification assunta | TST totale | Gate | Evidenza |
|---|---:|---:|---|---|
| corrente | 32,780800 s | 208,111772 s | FAIL | E4 |
| metà costo | 16,390400 s | 191,721372 s | FAIL | E1 |
| final-only proxy storico exact BR | 5,439364 s | 180,770336 s | FAIL | E3 composto con E4 |
| zero certification | 0 | 175,330972 s | FAIL | E0 |

Certification-only è chiusa. Il protocollo `cheap progress detection + exact
final BR` conserva l'exact final result ed è S0 se detection non controlla la
semantica di arresto in modo approssimato; un approximate BR è invece S3.

Soglie in combinazione:

- con 8 B/action / `119,98 s` traversal, certification deve essere
  `<=8,604297 s`;
- con current traversal e final-only `5,439364 s`, il traversal necessita
  `>=1,420492x` speedup hardware;
- con zero certification, current traversal necessita ancora `1,360402x`.

## Exactness and benchmark relaxation

Sampling, bucketing, approximate solve state o approximate BR devono produrre
uno speedup solver netto **maggiore di `1,613409x`**, con margine aggiuntivo
per l'eventuale peggioramento della convergence. Non esiste evidence repository
compatibile: classificazione `UNKNOWN / REQUIRES NEW PRODUCT STUDY` e
`PRODUCT SEMANTICS CHANGE`, S3.

Il limite minimo meccanico per il current production sample è
`208,111772 s` più la tolleranza necessaria a evitare un equality-edge. Esso
equivale a `1,792676x` il riferimento GTO+ `116,09 s`, contro `1,111111x` del
limite corrente; è un aumento del `61,3409%` del limite. Classificazione
obbligatoria: **BENCHMARK RELAXATION — NOT A PERFORMANCE FIX**, S4.

## Minimal-pair analysis

Le equazioni non assumono moltiplicatività fra state, hardware e schedule.

| Pair | Boundary quantitativo | Risultato | Decisione |
|---|---|---|---|
| RAM + certification | con 8 B/action, cap `>=3.432.437.888 B` e `C<=8,604297 s` | final-only proxy dà 125,823956 s | POTENTIALLY SUFFICIENT — NEEDS NEW STUDY |
| RAM + hardware | 8 B/action: `H_trav>=1,252355x` se cert resta fissa; `H_both>=1,188021x` se accelera anche BR | plausibile su hardware con più bandwidth, ma state traffic cambia | POTENTIALLY SUFFICIENT — NEEDS NEW STUDY |
| RAM + algorithm | 8 B/action: `N*c_state(N)+C_current(N)+O<=128,988889`; al costo medio target, boundary circa @160–170 | schedule storica @170 è vicina, ma composizione non validata | POTENTIALLY SUFFICIENT — NEEDS NEW STUDY |
| algorithm + certification | current state: target entro @142 al costo corrente, oppure @160 nel best cost envelope storico | nessuna schedule misurata passa | INSUFFICIENT evidence-backed |
| algorithm + hardware | per ogni `N`, `H >= (T_trav(N)+C_current(N))/(128,988889-O)` | per storico @170 con cadence corrente stimata serve circa 1,34x; bassa separabilità | POTENTIALLY SUFFICIENT — NEEDS NEW STUDY |
| hardware + certification | final-only: `H_trav>=1,420492x`; zero-cert: `>=1,360402x` | meno hardware del single, conserva exact final BR | POTENTIALLY SUFFICIENT — NEEDS NEW STUDY |

La coppia RAM+algorithm usa un intervallo perché `C_current(N)` è discreta e
il costo state dipende dalla density. Moltiplicare alla cieca il rapporto replay
per il vecchio schedule @170 violerebbe la separabilità; il documento non lo fa.

### Minimal triples

Non vengono valutate triple. Hardware-only ha già un boundary S1, mentre
RAM+certification e hardware+certification sono pair S1/S0 plausibili. Ogni
triple aggiungerebbe un vincolo a severità uguale o maggiore senza essere
necessaria per raggiungere la frontier: è dominata nella funzione `G`.

## Feasibility frontier

La superficie generale è:

```text
N * c_traversal(N, schedule, state, hardware) / H_traversal
+ C(protocol, N) / H_certification
+ O
= 128,988889 s
```

Punti discreti utili:

| RAM/state | Algorithm target | Hardware effettivo | Certification | Solver model | Stato |
|---|---:|---:|---:|---:|---|
| current 2 GB/scaled | @202 | 1,000x | 32,780800 s | 208,111772 s | E4 FAIL |
| current | @202 | 1,615339x su trav+cert | scalata | 128,988889 s | E1 boundary |
| current | @202 | 1,420492x traversal | final-only 5,439364 s | 128,988889 s | E1 boundary |
| >=3.432.437.888 B / 8 B | @202 | 1,000x | 32,780800 s | 153,165392 s | E1/E2 FAIL |
| >=3.432.437.888 B / 8 B | @202 | 1,000x | <=8,604297 s | <=128,988889 s | E1/E2 boundary |
| >=3.432.437.888 B / 8 B | @202 | 1,000x | final-only 5,439364 s | 125,823956 s | optimistic PASS model |

## Required scenarios

| Scenario | Minimum path | Technically feasible? | Product/governance desirable? |
|---|---|---|---|
| A — exact game + exact BR | hardware-only `>=1,615339x` effective, CPU-only | potentially; E1 | resource cost and lost hardware comparability da decidere |
| B — current algorithm | hardware-only sopra, oppure RAM 8 B + final exact BR `<=8,604297 s` | potentially | RAM+S0 riduce hardware need ma riapre state backend |
| C — current hardware/8T/2 GB | nuova exact schedule entro @120, oppure S3 approximation, oppure S4 target | non dimostrato per S2; S3 unknown; S4 mechanical | richiede contract decision |
| D — benchmark parity | hardware-only; in alternativa RAM+final-cert | potentially | preferire S1/S0 a S2/S3/S4 |
| E — product semantics exact | hardware-only è il minimo numero di vincoli; RAM+final-cert è il miglior pair | potentially | principale frontier raccomandabile |

## GTO+ comparability

| Relaxation | Apples-to-apples? | Caveat |
|---|---|---|
| più RAM, stessa macchina | sì per gioco/target; no se si confronta envelope memoria | nuovo backend deve preservare exact game/outcome/BR |
| hardware diverso | no per performance hardware-to-hardware | gioco e dEV restano comparabili, tempo GTO+ storico no |
| schedule exact diversa | sì per game/target result, no per contratto production | serve revalidation root/dEV/cross-fixture |
| cadence con exact final BR | sì se stopping/detection non cambia il risultato | detection economica va validata contro missed crossing |
| sampling/bucketing/approx BR | no | product semantics cambiano |
| limite tempo diverso | no | parity criterion cambia direttamente |

## Stress test e dominance

- Il punto float32/float32 `119,98 s` è una proiezione replay ottimistica, non
  una solve. Se la state wall fraction reale è minore o la bandwidth pressure
  maggiore, RAM+final-cert può tornare sopra il gate; realistic/worst case:
  **insufficient evidence**.
- Il final-only `5,439364 s` proviene da schedule/commit storico. Prova che un
  exact BR finale di quell'ordine è esistito, non che il current final-head lo
  replichi senza un nuovo short measurement.
- Amdahl 12/16 non modella saturazione DRAM. Più core con stesso bandwidth è
  dominato da una macchina che migliori anche il memory subsystem.
- Cambiare insieme state e hardware non è moltiplicativo: 8 B/action elimina
  scale ma aumenta traffico. Le due soglie hardware sono pubblicate come
  boundary alternativi, non moltiplicate.
- RAM+algorithm+certification e altre triple sono dominate dalle pair già
  sulla frontier. Approximation+resource e target+qualsiasi altra relaxation
  sono dominate rispettivamente da uno studio S3 singolo e dal target-only S4.

## Primary final table

| Rank | Changes | Severity | Best TST | Realistic TST | Confidence | GTO+ comparability | Decision |
|---:|---|---:|---:|---:|---|---|---|
| 1 | hardware con `>=1,615339x` effettivo trav+BR | S1 | 128,99 s al boundary; target prudente <122,6 s a 1,70x | insufficient evidence finché non misurato | medio-bassa | tempo hardware non apples-to-apples | POTENTIALLY SUFFICIENT — NEEDS NEW STUDY |
| 2 | RAM raw >=3.432.437.888 B + exact final BR <=8,604297 s | S1+S0 | 125,823956 s col proxy 5,439364 | insufficient evidence | medio-bassa | gioco sì; envelope RAM no | POTENTIALLY SUFFICIENT — NEEDS NEW STUDY |
| 3 | current state + final exact BR + traversal hardware >=1,420492x | S1+S0 | 128,99 s al boundary | insufficient evidence | media-bassa | hardware time no | POTENTIALLY SUFFICIENT — NEEDS NEW STUDY |
| 4 | RAM 8 B + hardware `>=1,252355x` traversal | S1 (due resource subcontracts) | 128,99 s al boundary | insufficient evidence | bassa | no per hardware/RAM | POTENTIALLY SUFFICIENT — NEEDS NEW STUDY |
| 5 | nuova schedule exact entro @120 current cadence | S2 | <=128,99 s per requirement | nessuna schedule storica vicina | bassa | game sì, solver contract no | POTENTIALLY SUFFICIENT — NEEDS NEW STUDY |
| 6 | sampling/bucketing/approx BR >1,613409x netto | S3 | unknown | unknown | bassa | no | PRODUCT SEMANTICS CHANGE |
| 7 | limite >208,111772 s | S4 | mechanical PASS | 208,111772 s sample | alta | no | BENCHMARK RELAXATION ONLY |

Non esistono intervalli numerici evidence-backed per i realistic/worst case
dei nuovi hardware o del backend 8 B; inventarli sarebbe più fuorviante di
pubblicare `insufficient evidence`.

### Minimum-change frontier

| Categoria | Minimo cambiamento |
|---|---|
| resource-only | hardware capace di `>=1,615339x` effettivo su traversal+exact BR, con margine consigliato verso `1,70x` |
| solver-contract | schedule exact che raggiunga <1% entro @120 con cadence corrente; non dimostrata |
| approximation | architettura S3 con speedup netto >1,613409x più degradation margin; studio nuovo |
| benchmark-only | limite >208,111772 s; non è un fix |

## Decisione

La minimum-severity technically plausible path è:

```text
Option A — single resource relaxation
keep algorithm, exactness, state, certification and benchmark
require >=1,615339x effective traversal+BR speedup
prefer candidate headroom near >=1,70x
severity S1
confidence medium-low
```

È lessicograficamente prima perché cambia un solo vincolo e conserva la
semantica esterna. Tuttavia cambia l'hardware di riferimento e deve essere
misurata con un fixed short benchmark prima di qualunque target-driven.

Se la macchina corrente deve restare il riferimento, la prima frontier è:

```text
Option B — RAM + certification protocol
cap raw >=3.432.437.888 B (operationally >=4 GiB)
direct float32/float32 state candidate
cheap progress detection + exact final BR <=8,604297 s
best model 125,823956 s with 5,439364 s final-BR proxy
severity S1 + S0
confidence medium-low
```

Questa opzione non è autorizzazione a integrare float32/float32: richiede prima
un decisione esplicita e poi una validation ladder separata. Se entrambe le
frontier S1 vengono rifiutate, il solver contract C1 deve cambiare; le schedule
storiche non ne dimostrano ancora la sufficienza.

# REQUIRES GOVERNANCE DECISION

Parametri da approvare, scegliendo una sola frontier:

1. sostituzione hardware e soglia di accettazione `>=1,615339x` effettiva
   (`>=1,70x` consigliata come headroom), accettando la perdita di comparabilità
   temporale diretta col riferimento GTO+ sulla macchina corrente; oppure
2. cap desktop raw almeno `3.432.437.888 B`/operativo almeno 4 GiB e studio
   separato di exact final-only certification `<=8,604297 s`, mantenendo gioco,
   outcomes, dEV e BR finale exact; oppure
3. riapertura S2 della schedule, con gate quantitativo `<1% entro @120` sul
   costo/cadence correnti, senza assumere che `1.9/0/3` sia sufficiente.

Nessuna di queste decisioni è stata presa silenziosamente.

## Validazione e cost accounting

- fonti storiche auditate per commit/config, algorithm, state precision,
  cadence, iteration count e rilevanza corrente;
- algebra, Amdahl, RAM model e boundary ricontrollati;
- nessun codice production, fixture, target, tolerance o checkpoint modificato;
- nuovi solver run: **0**;
- nuovi full target-driven run: **0**;
- nuovi full CTest run: **0**;
- five-process: **0**;
- validazione richiesta: `git diff --check` e audit documentale.

## Passo successivo

**Il maintainer deve scegliere Option A, Option B o la riapertura S2 prima di
qualsiasi nuovo codice o benchmark lungo.**
