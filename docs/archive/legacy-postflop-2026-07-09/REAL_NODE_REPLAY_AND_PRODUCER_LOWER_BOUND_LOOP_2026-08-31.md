# Real-Node Replay + Joint Precision/Producer Lower-Bound Loop — 2026-08-31

> **CORREZIONE SEMANTICA 2026-09-04 — MEMORY KILL GATE RITIRATO.** Replay,
> stabilità numerica e costi restano validi; le esclusioni basate sul presunto
> cap desktop 2 GB devono essere rivalutate con l'accounting solver-owned.
> Vedere il
> [`piano di correzione`](../../GTO_PLUS_SOLVER_MEMORY_SEMANTICS_AND_GATE_CORRECTION_PLAN_2026-09-03.md).

## Analisi

### Obiettivo e contratto

La funzione obiettivo usata nel loop è, in ordine lessicografico:

```text
F = (
  mathematical_contract_fail_count,
  correctness_model_fail_count,
  replay_fidelity_fail,
  numerical_stability_fail,
  ram_fail,
  projected_time_to_target_ratio,
  replay_pipeline_cost,
  state_bytes
)
```

Restano congelati exact alternating DCFR `alpha=1.5`, `beta=0`, `gamma=2`,
delay zero, average reach-weighted immediato `t^2`, exact outcomes/BR, tree,
fixture, range, board, sizing, target dEV, Root e massimo otto thread. Nessun
RBP, sampling, bucketing, GPU, fast-math o dispatch fixture-specific è stato
introdotto.

### Stato iniziale

Il fetch iniziale del 2026-08-31 ha verificato:

| Campo | Valore |
|---|---|
| branch | `main` |
| HEAD iniziale | `72b31ee607771c400a46a8443cf0f3a66d86d548` |
| `origin/main` | `72b31ee607771c400a46a8443cf0f3a66d86d548` |
| divergenza | `0/0` |
| working tree | solo `.reasonix/` e `.tmp/` untracked, preservati |
| commit locali non pubblicati iniziali | nessuno |

Baseline autorevole invariata: AHK `0,670928 s @80`, TH `17,645055 s @80`,
TST `208,111772 s @202`, di cui `174,926380 s` traversal e `32,780800 s`
certification. Il limite TST è `128,988889 s`; mantenendo la certification,
il traversal richiesto è `<=96,208089 s` (`~1,818x`).

### Blocker ereditati

Sono stati trattati solo come control: tile K8/16/32/64, power-of-two K32,
per-hand scale, hybrid tile/node, bfloat16/bfloat16, signed-float24/bfloat16,
adaptive tile, retain/recompute/mixed, wavefront e treelet precedenti. Non è
stato aperto un altro codec sintetico. Il current traversal micro-loop resta
`EXHAUSTED`; le famiglie di redesign già studiate restano bloccate; la
rappresentazione tile/direct precedente resta `EXHAUSTED`.

## Piano e benchmark ladder

Il loop eseguito è stato:

```text
CAPTURE -> VALIDATE REPLAY -> CHARACTERIZE -> DERIVE LOWER BOUNDS
-> GENERATE CANDIDATES -> CHEAP FALSIFICATION -> REPLAY BENCHMARK
-> (SHORT/MID/TARGET solo se autorizzati) -> REJECT -> UPDATE MODEL
```

La ladder ha applicato early stop. Level 0/1 hanno respinto ogni candidate;
nessun Level 2 candidate solve, Level 3 candidate mid-run, Level 4 candidate
cross-fixture o Level 5 target-driven è stato autorizzato. Le solve fixed
AHK/TH/TST elencate sotto servono esclusivamente a catturare workload reali,
non misurano una candidate production.

## Implementazione

### Replay capture

`GTOSD_ENABLE_REAL_NODE_REPLAY=OFF` è il compile gate. Con il gate ON, il
caller deve fornire esplicitamente `PostflopRealNodeReplayCapture`; altrimenti
il percorso resta inerte. Il sampler usa seed e hash strutturale deterministici,
iteration filter, modulo configurabile e reservoir bounded per stratum. Le
rare update flop vengono ammesse prima del reservoir; turn/river passano dal
gate hash. Il limite resta globale e non esiste una seconda copia completa
dello state.

Ogni sample conserva:

- identity: iteration, representative node, board mask/street, update player,
  actor, action/hand count e structural signature;
- old scaled codes e scale;
- policy corrente, actor reach e opponent reach quando mappabile;
- action values reali, current value, immediate regret delta;
- peso `t^2` e average contribution effettivo;
- producer per action: fold, showdown, decision subtree, chance o transformed;
- valori pre-quantizzati, codici/scale risultanti e parent returned values.

La CLI emette `gtosd.real_node_replay.v1`. Il tool
`gtosd_real_node_state_replay` valida il current backend, caratterizza il
corpus, esegue sweep precisione, replay multi-step, timing pipeline,
packing/alignment e producer streaming control.

### Corpus riproducibile

Artefatti diagnostici preservati:

| Workload | Iterazioni relative | Sample | Entry | File |
|---|---:|---:|---:|---|
| AHK fixed-20 | `1/10/20` | 63 | 4.797 | `.tmp/real-node-replay/ahk20-corpus.json` |
| TH fixed-20 | `1/10/20` | 86 | 51.853 | `.tmp/real-node-replay/th20-corpus.json` |
| TST fixed-80 | `1/40/80` | 139 | 105.667 | `.tmp/real-node-replay/tst80-corpus.json` |

Il corpus decisionale è TST fixed-80. Contiene 46/13/80 sample flop/turn/river,
35/52/52 early/mid/late e action count 2/3/4/5 con 41/46/36/16 sample.
Le classi producer osservate nel campione sono 96 fold, 56 showdown, 236
decision-subtree e 56 chance action. La stratificazione intenzionale rende
questi conteggi non adatti a stimare la prevalenza production; per quella si
usa la telemetria completa sotto.

### Quantili reali TST

| Metrica | p10 | p25 | p50 | p75 | p90 | p95 | p99 |
|---|---:|---:|---:|---:|---:|---:|---:|
| hands/update | 195 | 200 | 223 | 270 | 301 | 301 | 301 |
| entries/update | 420 | 552 | 669 | 903 | 1.120 | 1.204 | 1.505 |
| old regret max abs | 0 | 7,88e-7 | 0,009792 | 0,038033 | 0,090302 | 0,165190 | 0,345005 |
| action value max abs | 0,000260 | 0,001859 | 0,011397 | 0,035689 | 0,103936 | 0,140054 | 0,286478 |

Questo include workload near-zero, low, medium, high e mixed-sign; i sample
sono classificati nel reservoir anche per dynamic range, street, iteration,
action count, player e producer mask.

## Validazione replay

### Fidelity current scaled backend

| Corpus | Regret code mismatch | Strategy code mismatch | Scale/encode | Parent bit mismatch |
|---|---:|---:|---|---:|
| AHK | 0 / 4.797 | 0 / 4.797 | PASS | 0 |
| TH | 0 / 51.853 | 0 / 51.853 | PASS | 0 |
| TST | 0 / 105.667 | 0 / 105.667 | PASS | 0 |

Sul TST il modello dell'update ha regret abs-error p99 `3,61e-9`, max
`2,80e-8`; per l'average p99 `0,001812`, max `0,015445`. Quest'ultimo delta è
la differenza tra la ricostruzione scalar-double e l'output float/AVX
autorevole, non un mismatch di state: i codici finali sono byte-equal e le
scale registrate sono bit-equal. La decisione numerica usa sempre l'output
autorevole catturato.

### Current replay cost breakdown

Sette ripetizioni, 105.667 entry reali TST:

| Fase | Secondi | Quota pipeline replay |
|---|---:|---:|
| policy decode/regret matching | 0,001664 | 5,952% |
| current-value reduction | 0,000697 | 2,492% |
| regret update | 0,006439 | 23,037% |
| average update | 0,002379 | 8,512% |
| encode/store | 0,016773 | 60,007% |
| totale | 0,027948 | 100% |

Queste percentuali descrivono il replay pipeline isolato; non vengono
spacciate per quote wall del solver completo.

## Precision lower bound e Pareto frontier

Lo sweep usa float E8 con total bit crescenti: 16=E8M7 (bfloat control),
18=E8M9, 20=E8M11, 22=E8M13, 24=E8M15 (signed-f24 control), 32=float32.
Non implica automaticamente uno specifico layout production.

### Single-update TST

| R/S bit | Regret p99/max abs | Policy L1 p99 | Average L1 p99 | Argmax changes | B/action modellati |
|---:|---:|---:|---:|---:|---:|
| 16/16 | 1,31e-4 / 9,46e-4 | 0,002128 | 0,002127 | 31 | 4 |
| 18/18 | 3,55e-5 / 2,22e-4 | 0,000535 | 0,000537 | 20 | 6 |
| 20/20 | 8,15e-6 / 6,03e-5 | 0,000133 | 0,000133 | 13 | 6 |
| 22/22 | 2,12e-6 / 1,53e-5 | 3,34e-5 | 3,44e-5 | 10 | 6 |
| 24/16 | 5,36e-7 / 3,70e-6 | 8,34e-6 | 0,002127 | 10 | 5 |
| 24/24 | 5,36e-7 / 3,70e-6 | 8,34e-6 | 8,45e-6 | 10 | 6 |
| 32/32 | 0 / 0 | 0 | 0 | 0 | 8 |

Non sono avvenuti sign/positive-class changes nel single update; ciò non è
sufficiente per promuovere i punti intermedi.

### Multi-step replay

Ventotto sequenze strutturalmente compatibili, 8 cicli bounded, 520 step:

| R/S bit | Policy L1 p99 | Policy max | Average L1 p99 | Average max |
|---:|---:|---:|---:|---:|
| 16/16 | 0,009433 | 1,500000 | 0,005403 | 0,666664 |
| 18/18 | 0,002273 | 1,333333 | 0,001376 | 0,429008 |
| 20/20 | 0,000507 | 1,333333 | 0,000334 | 0,428513 |
| 22/22 | 9,60e-5 | 1,333333 | 8,20e-5 | 0,428576 |
| 24/16 | 2,41e-5 | 1,333333 | 0,004591 | 0,429669 |
| 24/24 | 2,41e-5 | 1,333333 | 2,08e-5 | 0,428575 |
| 32/16 | 0 | 0 | 0,004578 | 0,012845 |
| 32/32 | 0 | 0 | 0 | 0 |

I cicli ripetono sequenze reali compatibili ma non pretendono di essere una
solve trajectory completa. Sono un cheap falsifier: tutti i regret 16–24 bit
mostrano almeno un cambio di regime policy catastrofico. Nel dominio testato,
il lower bound prudente per signed regret dinamicamente stabile è float32.
Strategy 16 bit resta molto più stabile con regret float32, ma non è exact e
non risolve il blocker RAM/tempo descritto sotto.

### Throughput frontier e packing

| R/S bit | B/action | Replay speedup vs scaled current | Proiezione traversal | Ratio vs 96,208089 s |
|---:|---:|---:|---:|---:|
| 16/16 | 4 | 0,746x | 202,71 s | 2,107 |
| 20/16 | 5 | 0,745x | ~202,8 s | ~2,108 |
| 24/16 | 5 | 0,724x | 206,02 s | 2,141 |
| 32/16 | 6 | 1,333x | 154,54 s | 1,606 |
| 32/32 | 8 | 3,059x | 119,98 s | 1,247 |

La proiezione usa intenzionalmente un optimistic state wall fraction del
`46,6636%`, derivato dividendo per otto il serial-equivalent state time della
telemetria TST completa. Favorisce le candidate; nonostante ciò nessun punto
misurato raggiunge `96,208089 s`.

L'audit alignment su 105.667 entry x7 misura packed-24 `1,6129x` il costo di
aligned-32 (`0,001497 / 0,000928 s`). Conferma che risparmiare un byte può
perdere sul packing. Non viene ottimizzato B/action in isolamento.

### RAM model

TST ha 366.890.152 action entry; l'overhead non-state osservato rispetto al
peak autorevole è 497.316.672 B. Il modello, favorevole alle candidate, rimuove
le scale node-global quando usa direct state:

| Direct B/action | State bytes | Peak proiettato | Desktop 2 GB |
|---:|---:|---:|---|
| 4 | 1.467.560.608 | 1.964.877.280 | PASS stretto |
| 5 | 1.834.450.760 | 2.331.767.432 | FAIL |
| 6 | 2.201.340.912 | 2.698.657.584 | FAIL |
| 8 | 2.935.121.216 | 3.432.437.888 | FAIL |

Ne segue che il solo punto direct RAM-feasible è il control 16/16, già
numericamente instabile e più lento. I punti float32 numericamente forti
falliscono il cap prima di qualunque solver probe.

## Producer provenance e streaming ceilings

La telemetria completa TST @202 (68.580.297.489 entry) riverifica:

| Producer | Entry | Work share |
|---|---:|---:|
| fold | 20.687.982.716 | 30,166073% |
| showdown | 23.904.037.746 | 34,855547% |
| decision subtree | 23.705.469.289 | 34,566005% |
| chance | 282.807.738 | 0,412375% |
| transformed | 0 | 0% |

Il ~69,83% showdown+decision ha producer non banale; fold non è zero-cost.

Il control replay confronta `whole vector -> copy/write/read -> consume` con
chunk 32 `consume -> discard`. Il costo riportato è il sottopercorso di
materializzazione/consumo, non il costo ricorsivo del producer:

| Producer | Work share | Current ns/entry | Streamable fraction del buffer | Ideal micro speedup | Realistic upper bound |
|---|---:|---:|---:|---:|---:|
| fold | 30,166% | ~2,30 | 100% del buffer, 0% del producer compute | 1,045x | <=1,034x |
| showdown | 34,856% | ~2,30 | idem | 1,043x | <=1,032x |
| decision subtree | 34,566% | ~2,32 | idem | 1,041x | <=1,031x |
| chance | 0,412% | ~2,28 | idem | 1,037x | <=1,028x |

Il weighted ideal micro ceiling è circa `1,041x`. Anche applicarlo in modo
irrealisticamente favorevole all'intero traversal produce `168,04 s`, molto
sopra il target. Il vero redesign non può eliminare il compute ricorsivo e
avrà setup/chunk/call overhead: questo è quindi un upper bound, non una stima
conservativa.

## Critical lower-bound experiment

| Scenario | Assunzione | Best traversal proiettato | Esito vs 96,208089 s |
|---|---|---:|---|
| ideal state | decode/update/encode state = 0; producer current | 93,30 s | PASS teorico di 2,91 s |
| ideal producer streaming | materializzazione whole-vector = 0; state current | 168,04 s | FAIL di 71,83 s |
| joint ideal | entrambi ideali | 89,62 s | PASS teorico di 6,58 s |

Il joint ideal non chiude da solo la famiglia: mostra che esiste margine solo
se quasi tutto il costo state sparisce. La frontier reale lo falsifica: il
punto misurato più veloce, float32/float32, resta a ~119,98 s ed è RAM FAIL;
il punto RAM PASS è più lento e instabile. Pertanto si attiva l'altra clausola
del kill criterion: **la precisione necessaria implica throughput/RAM
insufficienti e il producer ceiling separato è insufficiente**.

## Candidate pool e cheap falsification

Generato solo dopo i bound:

| Candidate | Numerical replay | Multi-step | RAM | Replay speed | Proiezione | Decisione |
|---|---|---|---|---:|---:|---|
| E8M7/E8M7 aligned control | single piccolo errore | max policy 1,50 | PASS | 0,746x | 202,71 s | REJECT numerical+speed |
| E8M11/E8M7 intermediate asymmetric | single accettabile | max policy 1,33 | FAIL (5 B/action) | 0,745x | ~202,8 s | REJECT numerical+RAM+speed |
| E8M15/E8M7 packed control | single molto preciso | max policy 1,33 | FAIL | 0,724x | 206,02 s | REJECT numerical+RAM+packing |
| float32/E8M7 aligned | regret policy stable; average non-exact | policy max 0; average max 0,0128 | FAIL | 1,333x | 154,54 s | REJECT RAM+time |
| float32/float32 aligned | exact nel replay | zero drift | FAIL | 3,059x | 119,98 s | REJECT RAM+time |

I punti intermediate packed sono dominati: più byte del 16/16, meno stabili
del float32 e più lenti del current scaled. Nessun production path, enum o flag
candidate è stato aggiunto.

## Objective ledger

| Loop | Hypothesis | Replay numerical | Replay speed | Projected gain | Short solve | Mid solve | Full authorized? | Decision |
|---:|---|---|---:|---:|---|---|---|---|
| 0 | current corpus è autorevole | byte/bit PASS | reference | 0% | n/a | n/a | no | PROMOTE tooling |
| 1 | 16-bit direct è RAM-feasible | FAIL multi-step | 0,746x | -15,9% | not authorized | not authorized | no | REJECT |
| 2 | 20–24-bit è il precision sweet spot | FAIL outlier | 0,72–0,75x | regressione | not authorized | not authorized | no | REJECT |
| 3 | aligned float32 regret evita packing | PASS regret; average 16 diagnostico | 1,333x | 11,7% | blocked by RAM and target projection | not authorized | no | REJECT |
| 4 | aligned float32/float32 chiude TST | PASS | 3,059x | 31,4% | blocked by RAM and 119,98 s projection | not authorized | no | REJECT |
| 5 | producer streaming compone il residuo | n/a | ideal micro 1,041x | <=3,94% micro | not authorized | not authorized | no | REJECT primary path |

`reason_full_run_was_justified`: non applicabile. Zero target-driven sono stati
eseguiti; nessun candidate ha superato Level 0/1.

## Cost accounting

| Voce | Costo |
|---|---:|
| corpus solver runs | 4 (due AHK capture, una TH, una TST) |
| candidate solver runs | 0 |
| target-driven runs | 0 |
| full CTest durante esplorazione | 0 |
| full CTest finale | 1 |
| TST fixed-80 capture | 1, ~108 s wall task / 75,997 s solver report |
| TH fixed-20 capture | 1, ~29 s wall task / 4,774 s solver report |
| AHK capture valide | 1, ~6,4 s wall; una prima capture è stata scartata dopo fix fidelity |
| replay analyzer | tre corpus, pochi secondi totali |

La prima AHK capture ha individuato un errore del capture timing (current value
letto prima del fused update); è stata corretta e sovrascritta prima delle
decisioni. Non ha generato un candidate benchmark più costoso.

## Validazione

- Release replay build `/W4 /WX`: PASS;
- Release final build `/W4 /WX`: PASS;
- test specifico capture bounded e strutturale in Phase 10: PASS;
- Phase 10 replay executable: `assertions=10631`, PASS;
- Phase 7 final: `assertions=215`, PASS;
- Phase 10 final gate-off: `assertions=10593`, PASS;
- GTO+ reference: `assertions=24`, PASS; asymmetric range e root lock PASS,
  differenziale seriale/parallelo zero;
- fidelity AHK/TH/TST: zero code e parent mismatch;
- analyzer TST/TH/AHK: PASS;
- full CTest finale: `23/23` PASS in `194,31 s`;
- `git diff --check`: PASS;
- `.reasonix/` e `.tmp/`: preservati;
- nessun full target-driven o five-process: correttamente non autorizzato.

## Decisioni

### Esito finale

# JOINT STATE/PRODUCER LOWER-BOUND BLOCKER

La famiglia `state representation + producer whole-vector streaming` viene
chiusa come strada primaria sotto i vincoli correnti:

1. 16–24 bit regret non è stabile nel multi-step reale bounded;
2. float32 regret è stabile ma 6–8 B/action falliscono il cap desktop;
3. il fastest replay point reale non raggiunge il traversal richiesto anche
   usando una proiezione state-share ottimistica;
4. producer streaming da solo ha ceiling ~1,041x e non può chiudere il gap;
5. solo il joint *perfetto* passa, con margine troppo stretto e senza una
   rappresentazione realizzabile sulla frontier.

Non è stato promosso alcun codec o redesign production. Il backend autorevole
resta `ScaledUint16RegretStrategy`; il confronto peak-RSS-vs-GTO+ resta fuori
scope e invariato; F11+ e five-process restano congelati.

### Compromessi e limiti

- Il multi-step ciclico è un falsifier bounded, non una prova formale di
  convergenza globale. Gli outlier bastano al REJECT; non basterebbero a una
  PROMOTE.
- La state wall fraction usa telemetria instrumented serial-equivalent divisa
  per otto ed è deliberatamente ottimistica. Un valore reale minore peggiora
  tutte le proiezioni candidate e alza l'ideal-state bound.
- Il producer control misura materializzazione/consumo su valori reali, non
  ricrea il compute ricorsivo: applicare il suo speedup all'intero traversal è
  già un favore irrealistico alla candidate.

## Passo successivo

**Aprire un gate di governance sui vincoli/target prima di altro codice**:
decidere esplicitamente quale assunzione può cambiare (cap desktop, contratto
DCFR exact, target temporale o hardware). Con tutti i vincoli invariati, i loop
current traversal, redesign studiati e joint state/producer non lasciano una
strada primaria evidence-backed; un altro codec sarebbe ripetizione, non
progresso.
