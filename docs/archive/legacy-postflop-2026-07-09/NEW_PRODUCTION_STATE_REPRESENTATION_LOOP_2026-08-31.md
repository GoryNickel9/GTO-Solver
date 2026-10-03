# New Production State Representation Loop — 2026-08-31

> **CORREZIONE SEMANTICA 2026-09-04 — RAM GATE RITIRATO.** I byte model, gli
> oracle e i risultati numerici restano evidenza. Le classificazioni rispetto al
> cap TST/desktop 2 GB non sono più normative e le famiglie escluse soltanto per
> memoria richiedono un nuovo pre-gate solver-owned. Vedere il
> [`piano di correzione`](../../GTO_PLUS_SOLVER_MEMORY_SEMANTICS_AND_GATE_CORRECTION_PLAN_2026-09-03.md).

## 1. Decisione

**REPRESENTATION SPACE EXHAUSTED per le famiglie richieste e studiate.**
Nessun formato supera insieme i gate di dataflow, RAM, stabilità numerica,
fused-shadow e convergenza. Nessuna nuova representation entra in production;
`ScaledUint16RegretStrategy` resta il formato autorevole e non è stato
rebaselineato.

Il risultato non estende il precedente blocker byte-identico a ogni possibile
formato. Dimostra invece che, su questo motore e sui candidati obbligatori:

1. le scale locali `uint16` eliminano il max globale ma il costo di max,
   divisione, metadata e finalizzazione per tile assorbe il traffico evitato;
2. il per-hand shared scale è l'upper bound di accuratezza della famiglia ma
   supera il cap TST di 2 GB ed è più lento;
3. un formato direct a 4 B/action abbastanza veloce nello shadow (`bfloat16 +
   bfloat16`) non conserva una dinamica DCFR utile e regredisce nel traversal
   reale;
4. un formato direct più accurato a 5 B/action (`signed float24 + bfloat16`)
   resta sotto il gate shadow e ha un controllo storico action-major
   regressivo;
5. la sola sostituzione dello state non rende tile-local il producer ricorsivo
   corrente, che continua a restituire un intero `ComboVector` per action.

## 2. Stato iniziale e blocker ereditati

- HEAD iniziale: `c6b051dd161f3056dca9e311ad0de696f79743c3` su `main`.
- Dopo `git fetch origin main`, `origin/main` coincideva con HEAD.
- Working tree iniziale: soltanto `.reasonix/` e `.tmp/` non tracciati;
  entrambi preservati.
- Commit locali ereditati: `d68a134`, `325d018`, `c6b051d`.
- Blocker ereditato: il percorso byte-identico node-global richiede almeno 63
  bit prequantizzati per entry e i suoi shadow migliori erano `1,024x`.
- Non sono stati riaperti micro-kernel, traversal redesign o codec
  byte-identici già chiusi.

Contratto congelato: exact outcomes, alternating DCFR, `alpha=1.5`, `beta=0`,
`gamma=2`, average immediato reach-weighted `t^2`, BR esatta, fixture e
riferimenti GTO+, CPU/RAM-only, massimo otto thread.

## 3. Obiettivo lessicografico

```text
F = (
  mathematical_contract_fail_count,
  correctness_gate_fail_count,
  numerical_stability_fail_count,
  desktop_ram_fail,
  worst_time_to_target_ratio,
  aggregate_time_to_target_ratio,
  tst_time_to_target,
  solver_state_bytes
)
```

Baseline autorevole preservato:

| Fixture | Iter | dEV | Root EV | Solver s | Limite s | Esito |
|---|---:|---:|---:|---:|---:|---|
| AHKHQH | 80 | 0,655665% | 19,108984 | 0,670928 | 1,900000 | PASS |
| TH7D6S | 80 | 0,806385% | 8,221632 | 17,645055 | 19,622222 | PASS |
| TSTC9D | 202 | 0,991863% | 8,494698 | 208,111772 | 128,988889 | time FAIL |

Il rapporto peggiore resta `208,111772 / 128,988889 = 1,613409x`.
Restano `79,122883 s` TST e uno speedup traversal richiesto di circa `1,818x`
(`174,926380 -> <=96,208089 s`) a certification invariata.

## 4. Audit storico dei formati

| Formato | B/action | Scale semantics | Signed | Accuratezza storica | Velocità storica | Rilevanza corrente |
|---|---:|---|---|---|---|---|
| `Float64` | 16 | nessuna | sì | oracle più forte | RAM/traffic elevati | control, non production |
| `Float32` | 8 | nessuna | sì | differential control, non identico a float64 | AHK storico 3,128 s median | oracle numerico, TST RAM non feasible |
| `Float24RegretFloat16Strategy` | 5 | floating direct | regret no (CFR+) | TH dEV 0,926045% @140 | TH 86,105994 s; action-major smoke 1,527–1,608 s vs 1,438 s | control adiacente; non DCFR signed |
| `Float13RegretFloat11Strategy` | 3 | floating direct packed | regret no (CFR+) | AHK/TH/TST dEV PASS storico | 4,970917 / 37,810434 / 690,307523 s | RAM utile, tempo respinto |
| `ActionMajorFloat13RegretFloat11Strategy` | 3 | floating direct packed | regret no | non applicabile al regret DCFR firmato | packing/action-major misurato | non ripetuto |
| `ScaledUint16RegretStrategy` | 4 + 8 B/node | due scale float node-global | sì | production gate corrente | baseline autorevole | control/production |
| signed13 + strategy11 | 3 | floating direct packed | sì | regret mean abs 0,277688 vs 0,000488 scaled | update 11,418 vs 6,597 ms | REJECT storico |
| bfloat16 + uint8 / E4M4 | 3 | floating direct | sì | non convergeva AHK | non promosso | REJECT storico |
| 12+12 | 3 | floating direct | sì | TST 1,37624% @200 | time non competitivo | REJECT storico |
| 14+10 | 3 | floating direct | sì | non convergeva AHK | non promosso | REJECT storico |

Il nuovo signed-float24 conserva il segno e quindi non ripete semanticamente il
vecchio regret float24 CFR+; il suo risultato è riportato separatamente sotto.

## 5. Proprietà matematica della scala shared-across-actions

Per una mano `h`, con una scala positiva comune `s_h` e code firmati `c_a`:

```text
R_a = s_h c_a
sigma(a|h) = max(R_a, 0) / sum_b max(R_b, 0)
           = max(c_a, 0) / sum_b max(c_b, 0)
```

La scala si cancella esattamente; se tutti i regret sono non positivi resta la
stessa policy uniforme. Con scale action-specific `s_a`, invece,
`max(s_a c_a,0)` modifica direttamente i rapporti. Per questo ogni tile include
**tutte le actions x le hands del tile** e nessun candidato usa scale per-action.

L'oracle del benchmark verifica anche questa proprietà e costruisce un control
con scale per-action che produce una policy diversa.

## 6. Candidate pool e ranking iniziale

| Candidate | B/action | Scale | Errore atteso | Fusion unlocked | RAM TST | Rischio |
|---|---:|---|---|---|---:|---|
| A tile float `K=8/16/32/64` | 4 + 8 B/tile | arbitrary float | basso | sì nello shadow bounded | 1,489–1,615 GB | max/divide per tile |
| B tile power-of-two `K=32` | 4 + 2 B/tile | due exponent | medio-basso | sì | 1,477 GB aligned | utilization inferiore |
| C per-hand shared scale | 4 + 8 B/hand | arbitrary float | upper bound | sì | 2,632 GB | RAM FAIL |
| D hybrid `R tile / A node` | 4 + 4 B/tile + 4 B/node | mista | regret basso | parziale | 1,490 GB | average global barrier |
| E direct bfloat16/bfloat16 | 4 | nessuna | medio | sì | 1,468 GB | dinamica cumulativa |
| E2 signed-float24/bfloat16 | 5 | nessuna | regret molto basso | sì | 1,834 GB | packed 3-byte traffic |
| F adaptive structural | come A | `K` da hand count/SIMD | come A | sì | <= famiglia A | branch/metadata |

La regola adaptive valutata era deterministica: `K=16` fino a 64 hands,
`K=32` fino a 256, altrimenti `K=64`. Nessun board o fingerprint entra nella
scelta. Poiché ogni componente fixed misurato è dominato dal legacy, la
composizione adaptive non può superarne il costo sul medesimo corpus.

## 7. Modello memoria e tile count reali

Logical bytes per tile-float:

```text
4 * action_entries + 4 * regret_tiles + 4 * strategy_tiles
```

Runtime arena bytes aligned a 64:

```text
align64(2 * action_entries) * 2
+ align64(4 * tiles) * 2
```

I contatori vengono calcolati dal layout reale come
`sum_node ceil(hands(node)/K)`; non sono stime da fixture ID.

| Fixture | K | Tile reali | Logical state B | 64-byte aligned B |
|---|---:|---:|---:|---:|
| AHK | 8 | 83.556 | 5.821.608 | 5.821.824 |
| AHK | 16 | 47.610 | 5.534.040 | 5.534.336 |
| AHK | 32 | 29.172 | 5.386.536 | 5.386.752 |
| AHK | 64 | 18.438 | 5.300.664 | 5.300.864 |
| TH | 8 | 4.635.936 | 370.361.856 | 370.362.112 |
| TH | 16 | 2.356.898 | 352.129.552 | 352.129.792 |
| TH | 32 | 1.202.811 | 342.896.856 | 342.897.152 |
| TH | 64 | 613.782 | 338.184.624 | 338.184.832 |
| TST | 8 | 18.460.240 | 1.615.242.528 | 1.615.242.752 |
| TST | 16 | 9.415.234 | 1.542.882.480 | 1.542.882.688 |
| TST | 32 | 4.918.868 | 1.506.911.552 | 1.506.911.744 |
| TST | 64 | 2.711.252 | 1.489.250.624 | 1.489.250.816 |

Current logical state: AHK 5.300.664 B, TH 334.452.416 B, TST
1.472.605.376 B. Su AHK, `K=64` degenera esattamente a un tile per node e non
rimuove il blocker. Per-hand usa 595.626 / 36.596.832 / 145.524.152 coppie di
scale e costa 9.918.168 / 626.049.024 / **2.631.753.824 B**.

Power-of-two `K=32` aligned: 5.211.776 / 335.680.256 / 1.477.398.528 B.
Hybrid `R tile32 / A node`: 5.343.600 / 338.674.636 / 1.489.758.464 B
logical. Tutti tranne per-hand rispettano il cap 2 GB, senza doppio full state.

## 8. Numerical oracle e stabilità regret matching

`gtosd_tile_state_pipeline_benchmark` usa uno state float32 separato e misura
errore assoluto, sign/positive-class changes, policy current/average L1-Linf,
saturation, near-zero e utilization. Il corpus deterministico include:

- tutti i regret non positivi;
- un solo positivo piccolo;
- due positivi quasi uguali;
- attraversamenti dello zero;
- dynamic range `1e-4..1e3`.

Risultati principali dopo un update (`mean / p99 / max` dove applicabile):

| Candidate | Regret abs mean | p99 | max | sign / class changes | current policy L1 mean / p99 | Linf max |
|---|---:|---:|---:|---:|---:|---:|
| per-hand | 0,002031 | 0,003683 | 0,828326 | 1 / 2 | 0,0000110 / 0,0000661 | 0,000192 |
| tile K8 | 0,006941 | 0,004791 | 2,603736 | 2 / 2 | 0,0000339 / 0,000371 | 0,000829 |
| tile K16 | 0,007095 | 0,010855 | 2,603736 | 2 / 2 | 0,0000335 / 0,000359 | 0,000647 |
| tile K32 | 0,007327 | 0,015211 | 2,603736 | 2 / 2 | 0,0000425 / 0,000412 | 0,001335 |
| tile K64 | 0,007713 | 0,017982 | 2,603736 | 2 / 2 | 0,0000540 / 0,000520 | 0,001577 |
| pow2 K32 | 0,008028 | 0,015079 | 2,596931 | 2 / 2 | 0,0000558 / 0,000601 | 0,001671 |
| bfloat16 direct | 0,100055 | 0,734055 | 0,957336 | 0 / 0 | 0,000815 / 0,003403 | 0,002575 |
| signed-f24/bf16 | 0,000401 | 0,002838 | 0,003525 | 0 / 0 | 0,00000313 / 0,0000136 | 0,00000745 |

L'average policy p99 L1 è 0,0000324–0,0001136 per le scale locali,
0,003010 per bfloat16 e 0,002904 per signed-f24/bf16. I massimi patologici
tile (`~0,316`) sono prodotti deliberatamente dai near-zero cases amplificati
dal contributo `t^2`; non sono nascosti dalla media.

Scale utilization regret: K8 0,3243, K16 0,3065, K32 0,2868, K64 0,2651,
pow2 K32 0,2025. Saturation: 4,180%, 2,116%, 1,058%, 0,529%, 0%.
La campagna non dispone di una production trajectory tile, quindi la richiesta
flop/turn/river early/mid/late resta non verificata e non viene inventata.

## 9. Fused pipeline e byte traffic

Shadow bounded implementato:

```text
action-value producer
-> all actions x tile hands
-> current value
-> signed discounted regret update
-> t^2 * actor_reach * current_strategy
-> tile max / local encode (oppure direct encode)
-> parent output
```

Nel candidate shadow vengono eliminati:

- action-value materialization whole-node;
- second whole-node value read;
- global max barrier;
- global regret/average scratch lifetime;
- intermediate regret e average vectors.

Il modello benchmark passa da 44 a 24 B/entry per tile-scale (20 B/entry di
transient whole-node eliminati), 20 B/entry per direct bfloat16 e 22 B/entry
per signed-f24/bf16. La dipendenza locale residua è esattamente tutte le action
values della stessa hand/tile; nessun valore di un altro tile serve al regret
update.

Limite strutturale production: il traversal corrente invoca ricorsivamente il
child e riceve un intero `ComboVector`. Lo shadow misura il ceiling di una API
producer tile-aware; la sola sostituzione dei buffer state non elimina il
vettore intero restituito dal child.

## 10. Fused shadow benchmark

Release MSVC `/W4 /WX`, AVX2, Ryzen 4C/8T 3,6 GHz, 11 ripetizioni,
`benchmark_min_time=0.2s`; decisione sulla mediana wall:

| Pipeline | Mediana ns | Speedup vs legacy | Gate 1,5x | Decisione |
|---|---:|---:|---|---|
| legacy whole-node | 35.624 | 1,000x | reference | control |
| per-hand K1 | 40.736 | 0,874x | FAIL | REJECT |
| tile K8 | 37.558 | 0,948x | FAIL | REJECT |
| tile K16 | 36.810 | 0,968x | FAIL | REJECT |
| tile K32 | 36.611 | 0,973x | FAIL | REJECT |
| tile K64 | 36.475 | 0,977x | FAIL | REJECT |
| pow2 K32 | 36.631 | 0,972x | FAIL | REJECT |
| direct bfloat16 | 24.310 | 1,465x | FAIL finale | Level 3 eseguito per precedente sample promettente |
| signed-f24/bf16 | 25.508 | 1,396x | FAIL | REJECT |

Un sample precedente a min-time inferiore aveva dato `~1,96x` al bfloat16 e
ha correttamente attivato la promotion ladder; il sample finale più lungo non
lo conferma. Il Level 3 reale decide comunque il reject senza affidarsi alla
sola micro-misura.

## 11. Fixed convergence probe e time/iteration

Solo bfloat16 ha superato provvisoriamente il gate e ricevuto un path
sperimentale, poi rimosso integralmente:

| AHK @20 | dEV | Traversal s | Solver s | Iter/s traversal | Decisione |
|---|---:|---:|---:|---:|---|
| Scaled baseline | 8,24777% | ~0,131 | 0,169024 | ~153 | control |
| direct bfloat16 | 84,2584% | 0,271492 | 0,341450 | 73,667 | REJECT |

Il bfloat state era 5.153.160 B e layout/fingerprint passavano, ma correctness,
convergence e throughput fallivano. Profile payoff-sum restava
`8,88e-16` e normalization `0`, quindi il difetto è la trajectory quantizzata,
non la conservazione del payoff o il layout. Il path production, enum, parser e
dispatch sono stati rimossi; checkpoint e formato legacy non sono cambiati.

Nessun candidate arriva ai target-driven AHK/TH/TST: la promotion ladder lo
impedisce dopo i reject Level 2/3. Perciò `iterations_to_target` e curve
target-driven candidate sono **non disponibili**, non stimate. Non è stato
eseguito five-process.

## 12. Pareto frontier

| Candidate | Numerical quality | State | Pipeline | Convergence | Pareto/decision |
|---|---|---:|---:|---|---|
| per-hand | migliore scaled local | TST RAM FAIL | 0,874x | non eseguita | dominato/REJECT |
| tile K8 | migliore di K32/K64 | +9,7% TST state | 0,948x | non eseguita | REJECT |
| tile K32 | buon compromesso RAM/error | +2,3% TST state | 0,973x | non eseguita | REJECT |
| tile K64 | minor RAM tile | +1,1% TST state | 0,977x | non eseguita | REJECT |
| pow2 K32 | metadata ridotto | +0,3% TST state | 0,972x | non eseguita | REJECT |
| bfloat16 | errore maggiore | -0,34% vs current | 1,465x | AHK catastrofica | REJECT |
| signed-f24/bf16 | regret migliore | +24,6% vs current | 1,396x | pre-gate | REJECT |
| current scaled | gate matematici PASS | 1.472.605.376 B | baseline | AHK/TH/TST dEV PASS | production |

Non esiste un punto nuovo non dominato che soddisfi i gate di promozione.

## 13. Ledger del loop

| Loop | Representation | Scale granularity | Bytes/state TST | Pipeline ceiling | Numerical result | Convergence result | Time result | Decision |
|---:|---|---|---:|---:|---|---|---|---|
| 0 | storico | node/direct | varie | audit | noto | noto | noto | non ripetuto |
| 1 | tile float K8 | 8 hands | 1.615.242.528 | 0,948x | tail sign/class changes | pre-gate | regressivo | REJECT |
| 2 | tile float K16 | 16 hands | 1.542.882.480 | 0,968x | ragionevole | pre-gate | regressivo | REJECT |
| 3 | tile float K32 | 32 hands | 1.506.911.552 | 0,973x | ragionevole | pre-gate | regressivo | REJECT |
| 4 | tile float K64 | 64 hands | 1.489.250.624 | 0,977x | errore maggiore | pre-gate | regressivo | REJECT |
| 5 | tile pow2 K32 | 32 hands/exponent | 1.477.398.528 aligned | 0,972x | utilization 0,2025 | pre-gate | regressivo | REJECT |
| 6 | per-hand | 1 hand | 2.631.753.824 | 0,874x | upper bound local | RAM pre-gate | regressivo | REJECT |
| 7 | hybrid R-tile/A-node | 32/node | 1.489.758.464 | < tile full-fusion | regret-only utile | global barrier resta | <10% projected | REJECT |
| 8 | direct bfloat16 | nessuna | 1.467.560.608 | 1,465x finale | no sign flip shadow | AHK @20 84,2584% | real 0,495x | REJECT + cleanup |
| 9 | signed-f24/bf16 | nessuna | 1.834.450.760 | 1,396x | regret eccellente | pre-gate | storico adiacente regressivo | REJECT |
| 10 | adaptive structural | 16/32/64 | <=1.542.882.480 | <= componenti | generalizzabile | pre-gate | dominato | REJECT |

Dopo ogni ciclo: worst ratio `1,613409x`, remaining TST `79,122883 s`,
remaining traversal speedup `1,818x`. Nessun candidato autorizza un nuovo
numero production.

## 14. Correctness, checkpoint e cleanup

- Gioco, tree, exact outcomes, algoritmo, BR e riferimenti non modificati.
- `ScaledUint16RegretStrategy` e i suoi checkpoint restano byte-compatibili.
- Nessuna nuova `PostflopStatePrecision` permane.
- Nessun full old+new state è mai stato allocato su TST; il layout-only TST è
  stato fermato dopo la telemetria.
- Il benchmark/oracle resta perché generalizzabile e coperto da CTest.
- `.reasonix/` e `.tmp/` sono preservati.

## 15. Decisione finale e nuovo blocker

Il blocker aggiornato è:

```text
representation-only local finalization
does not make the recursive child producer tile-local;
scaled local finalization is not cheaper even at its ideal fused ceiling;
direct compact floating is either numerically unstable or packing-bound.
```

Questo è più stretto di “la quantizzazione non funziona”: signed-float24 mostra
ottima precisione node-level, ma non sufficiente economia. È anche più stretto
di “il traversal non può cambiare”: lo shadow concede esplicitamente un
producer tile-aware e misura comunque il fallimento dei local-scale candidate.

## 16. Next objective

Unica prossima attività ad alta priorità: costruire un **real-node replay
corpus** early/mid/late per flop/turn/river che separi producer, policy,
regret e average, quindi derivare un lower bound congiunto di precisione e
throughput per qualunque formato direct prima di riaprire un'integrazione
production. Non va ripetuto un altro codec sintetico senza quel corpus.

## 17. Validazione finale

La verifica è stata eseguita sul build MSVC Release
`out/build/windows-release-current`, con warning del progetto trattati come
errori (`/W4 /WX`):

- build Release completa: **PASS**;
- `gtosd_phase7_tests`: **PASS**, 215 assertion;
- `gtosd_phase10_tests`: **PASS**, 10.593 assertion;
- `gtosd_gto_plus_reference_tests`: **PASS**, 24 assertion;
- `gtosd_tile_state_pipeline_benchmark --smoke`: **PASS**;
- CTest Release completo: **23/23 PASS**, 203,87 s;
- `git diff --check`: **PASS** (soli avvisi EOL LF/CRLF, nessun errore di
  whitespace).

La suite completa comprende il nuovo smoke
`gtosd_tile_state_pipeline_benchmark_smoke`. Questi risultati validano il
cleanup e l'assenza di regressioni note; non promuovono alcun candidato e non
sostituiscono la certificazione five-process, correttamente non avviata.
