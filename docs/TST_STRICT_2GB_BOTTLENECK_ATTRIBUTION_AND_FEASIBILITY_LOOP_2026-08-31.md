# TST Strict-2GB Bottleneck Attribution and Production-Feasibility Loop — 2026-08-31

## Analisi

### Stato repository e authority map

Il loop è stato eseguito in un worktree esterno, senza modificare il checkout
principale.

| Campo | Valore |
|---|---|
| repository | `C:\Users\GoryNickel\Documents\GitHub\GTO-Solver` |
| worktree di ricerca | `C:\tmp\gtosd-tst-strict-2gb-20260831` |
| branch | `research/tst-strict-2gb-bottleneck-loop-20260831` |
| `main` / `origin/main` iniziali | `77677b2f0be711b588bebdbd2256fe8a28f4b863` / identici |
| base di ricerca | `b47923b5765f52656455a07471c1a6d4b6f95f96` |
| code HEAD prima del report | `9b63dcdad2d396661f1db5a3d8c171d99cb1a66e` |
| divergenza iniziale main | `0/0` |
| stato main iniziale | tre fixture tracked modificate dall'utente (`gamma 2 -> 3`), `.reasonix/` e `.tmp/` untracked |

Gerarchia applicata:

1. fixture B committate e test `gtosd_production_dcfr_contract`;
2. `docs/GTO_PLUS_PARITY_JOURNEY.md`, `docs/IMPLEMENTATION_STATUS.md` e
   `docs/specifications/PERFORMANCE.md` per gate/dashboard/contratto;
3. qualification S6 per il più recente confronto schedule, senza promuoverla;
4. report di profiling, architettura, state representation e real-node replay
   per i closure bound;
5. nuovi report JSON freschi del presente loop.

I report antecedenti alla qualification S6 e alla baseline fresca restano
evidenza storica o diagnostica, non baseline temporale corrente. In particolare
`NEXT_TRAVERSAL_OPTIMIZATION_2026-08-30.md` è authority per la tassonomia dei
costi, non per il tempo Release corrente; la baseline `208.111772 s` del
constraint-governance gate è superseded dalla misura fresca di questo loop.

### Production contract congelato

Il contract SHA-256 canonico è
`aa739634c8bd01effd19903db1c17a93400d35d6dcf73f0098fc861ec13e3371`:

```text
exact alternating signed DCFR
alpha/beta/gamma = 1.5/0/2
averaging delay = 0; reach-weighted t^2 averaging
ScaledUint16RegretStrategy; float32 compute
exact outcomes and exact best response
lossless canonical public DAG/isomorphism
certification interval = 20
target dEV < 1% (strict)
maximum 8 solver threads
sampling/bucketing/RBP/GPU/fast-math/fixture dispatch = off
```

Le fixture versionate B non sono state modificate. SHA-256 sorgente:

| Fixture | SHA-256 | Fingerprint | State bytes |
|---|---|---|---:|
| AHKHQH | `894ebf504dcefe0ea517a86102c98f33ff4adaeefbefe9c75b0c08ffa05730c1` | `fnv1a64:1b6f30a930cd9bd0` | 5,300,664 |
| TH7D6S | `59142a02d40af30bc35dd2f9a27d4b1a749c9dcf38329e2355173d1233254f04` | `fnv1a64:fcba3c9eff1b7147` | 334,452,416 |
| TSTC9D | `a9e0cf3c161e37a9cb5e5c1a452d13078ba6bc959416f0a7f499185f3877de41` | `fnv1a64:731bf9562e90e792` | 1,472,605,376 |

### Definizione del cap memoria

Il gate è `peak process RSS < 2,000,000,000 B`, non `<=`, non working set
finale e non il solo solver-state. Il runner campiona `PeakWorkingSet64` ogni
50 ms, confronta anche il `PeakWorkingSetSize` interno e arresta il processo
quando il sample è `>= 2,000,000,000`. Ogni run è un processo fresco.

Nel TST fresco il peak è `1,971,036,160 B`; lo state è `1,472,605,376 B` e
l'overhead osservato è `498,430,784 B`. L'headroom effettivo è solo
`28,963,840 B`, pari all'`1.448192%` del cap. Il cap è quindi un vincolo attivo,
non un obiettivo secondario.

### Baseline storiche e baseline fresca

La qualification S6 resta `D. REJECT`: B `238.430899 s`, S6 `219.311579 s`,
solo `8.018810%` di miglioramento e nessuna common production promotion.

Baseline fresca B, singolo binario Release SHA-256
`a317de864f547522882ceade73812461947880c12db08898f39a68d01cc554e0`:

| Fixture | Iter | dEV | Traversal | Cert. | Solver | Limite task | Peak RSS | Esito tempo |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| AHKHQH | 80 | 0.655665% | 0.579028 s | 0.120926 s | 0.728164 s | 1.900000 s | 167,559,168 B | PASS |
| TH7D6S | 80 | 0.806385% | 17.269996 s | 2.276895 s | 19.974421 s | 19.622222 s | 800,169,984 B | FAIL di 0.352199 s |
| TSTC9D | 202 | 0.991863% | 197.392990 s | 36.592053 s | 234.437726 s | 128.988889 s | 1,971,036,160 B | FAIL di 105.448837 s |

I tre report passano contract, layout, state, Root, correctness, payoff sum,
normalizzazione, exact-BR finita e cap RAM. TH non è sostituito: il suo tempo
FAIL resta pubblicato.

Tre run TST @120 indipendenti, tutti validi e non sostituiti:

| Metrica | Min | Mediana | Max |
|---|---:|---:|---:|
| traversal | 114.208 s | 114.270 s | 119.500 s |
| certification | 29.873 s | 30.383 s | 30.437 s |
| solver | 144.592 s | 145.099 s | 150.332 s |
| wall inclusa preparazione | 169.457 s | 169.733 s | 175.461 s |
| peak RSS | 1,970,540,544 B | 1,971,339,264 B | 1,971,355,648 B |

### Decomposizione TST e speedup richiesto

```text
fresh solver                  234.4377261 s
fresh traversal               197.3929899 s
fresh exact certification      36.5920528 s
fresh other                     0.4526834 s
task limit                    128.9888890 s
required total reduction      105.4488371 s = 44.979466%
required whole-solver speedup   1.817503259x
traversal budget if cert fixed  91.9441528 s
required traversal speedup      2.146879208x
```

Se traversal e certification scalassero insieme servirebbe comunque
`1.820382371x`. Ridurre soltanto callback/report/finalization non è materiale.

### Bottleneck attribution

È stato costruito un profiler Release compile-gated e runtime opt-in. La
modalità lightweight conserva timer/contatori scalari ma disabilita i set di
hash dei reach e le mappe di classi topologiche, che non sono compatibili col
margine TST. Il run TST @20 profilato è valido a `1,970,749,440 B`, headroom
`29,250,560 B`, senza cap hit.

Ultimi pass P0/P1, tempi CPU-equivalent (il wall profilato non è confrontabile
con Release):

| Componente | P0 | P1 | Classificazione |
|---|---:|---:|---|
| value + state update | 5,216.7 ms | 6,913.5 ms | dominante; bandwidth/recode |
| scale scan dentro state path | 4,503.1 ms | 6,019.3 ms | maggiore sottocosto |
| showdown rank/prefix/output | 1,537.8 ms | 1,575.5 ms | molto chiamato/scatter |
| regret matching | 418.7 ms | 326.3 ms | action-entry dense |
| chance prepare/accumulate | 157.0 ms | 189.5 ms | secondario |
| reach propagation | 14.9 ms | 8.1 ms | non dominante |

Il profilo conta, nei soli due pass, 153,873,036/200,318,965 entry signed,
307,746,072/400,637,930 entry ricodificate e 411,998/394,081 scale overflow.
Il collo è dunque il dataflow node-global `decode -> update -> max/scale ->
encode/store`; showdown è il secondo componente, regret matching il terzo.

### Replay e Amdahl/ceiling

Il real-node replay già versionato resta l'oracolo bounded: 288 sample reali,
162,317 entry, byte/bit fidelity del current codec. La pipeline isolata pesa
5.952% policy, 2.492% current reduction, 23.037% regret update, 8.512% average
update e 60.007% encode/store.

I bound favorevoli già misurati sono:

| Famiglia | Miglior ceiling/proiezione | Vincolo decisivo |
|---|---:|---|
| producer whole-vector streaming | circa 1.041x micro; 168.04 s traversal idealizzato | tempo insufficiente |
| compiled-control traversal | <=2.94% traversal | tempo insufficiente |
| wavefront/treelet/batching | 1.059x–1.191x local/river | economics end-to-end insufficienti |
| direct 16/16, 4 B/action | 0.746x; 202.71 s traversal | più lento e policy instabile |
| float32 regret + 16-bit average | 154.54 s traversal | RAM FAIL e tempo FAIL |
| float32/float32 | 119.98 s traversal | RAM FAIL; ancora sopra 91.94 s |
| ideal state elimination | 93.30 s traversal | non realizzabile; ancora 1.36 s sopra budget fresco |
| joint ideal state+producer | 89.62 s traversal | solo lower bound non realizzabile |
| lazy/predictive payload | >=2.41/2.55 GB lower bound | RAM FAIL |
| memory-neutral FD/FTRL/OMD | crossing richiede <=83/85 iterazioni | local update cost e convergenza non dimostrata |

Anche eliminare idealmente l'intero state path storico non ha margine robusto
contro il budget fresco; ogni implementazione reale aggiunge compute e non può
raggiungere il joint ideal. Questo è un blocker quantitativo, non una mancata
idea di micro-ottimizzazione.

### Closure ledger M01–M80

Ogni voce è stata ricondotta a evidenza esistente o al profilo fresco. `CLOSED`
significa che il ceiling standalone o composto non può arrivare a `2.1469x`
traversal sotto exactness e 2 GB; non significa che il tema non possa produrre
un piccolo speedup in un altro obiettivo.

| ID | Ipotesi auditate | Stato e decisione |
|---|---|---|
| M01–M10 | LTCG/LTO, PGO, ordering, hot/cold, inline, devirtualization, bounds, exception split, ranges, dead telemetry | CLOSED: codegen agisce sul compute ma non elimina il doppio pass state; `/O2 /Ob3`, static dispatch e Release dead-code già presenti. Un >2.14x sarebbe incompatibile con i bound component-level; nessun build candidato autorizzato. |
| M11–M20 | decode, reciprocal, positive-sum fusion, all-nonpositive, contiguous SIMD, alignment, branchless sign, reductions, policy reuse, uint16/int16 conversion | CLOSED: regret matching è solo 326–419 ms/pass; anche costo zero non compone il gap. Fusion/rounding cambia la traiettoria se riordina FP. |
| M21–M30 | discount/update/scale/quantization fusion, strategy fusion, write combining, reload, zeroing, scratch, stack, arity kernels | CLOSED: è la famiglia dominante ma exact pre-value richiede 63 bit/entry; current node-global scale impone revisit/encode. Zero-fill e arity controls già non promuovibili; i codec RAM-feasible falliscono replay/speed. |
| M31–M40 | canonical lookup, metadata/child locality, producer/street/player/terminal dispatch, offsets, metadata compaction, pointer chasing | CLOSED: compiled-control ceiling 2.94%; AoSoA/metadata e treelet controls insufficienti. |
| M41–M50 | chance loop/multiplicity/masks/evaluator/showdown/fold/cache/range map/terminal reuse/branch prediction | CLOSED: chance è 157–190 ms/pass; showdown reuse/cache e fusion sono stati falsificati dalla provenance/reuse e dai benchmark. RBP/caching non è sound per DCFR senza nuova prova. |
| M51–M60 | static/dynamic queue, stealing, barriers, reduction, false sharing, allocator, affinity, NUMA, oversubscription | CLOSED: massimo 8 thread, CPU 0.78–0.81, coda bounded; scheduling/compiled traversal ceilings non compongono 1.82x whole. NUMA non applicabile alla macchina 4C/8T. |
| M61–M70 | policy reuse BR, BR locality/chance/terminal/scratch/partition/reduction/metadata/report, exactness tests | CLOSED: certification è 36.59 s; perfino azzerarla lascia 197.85 s. Deve essere exact e non può chiudere da sola; pairing/parallel certification già integrati. |
| M71–M80 | checkpoint copies, callbacks, formatting, JSON, finalization, telemetry conversion, I/O, allocation, dormant diagnostics, error paths | CLOSED: other è 0.4527 s; checkpoint non attivo nel benchmark; profiler è compile-gated e default-off. |

Sono state modellate dieci macro-ipotesi (gli otto gruppi sopra, più state
representation e producer streaming composti). Zero candidate kernel nuovi
sono stati replayati e zero candidate full-solver sono state implementate:
nessuna supera il kill threshold matematico prima del codice.

## Piano

La ladder eseguita è stata:

1. fetch/audit main, authority map e worktree esterno;
2. freeze contract/fixture/hash e runner con hard kill RSS;
3. build Release MSVC `/W4 /WX` e preflight contract;
4. warm-up TST @20 escluso, poi @20/@40/@80 e tre @120;
5. target-driven fresco AHK/TH/TST;
6. profiling lightweight @20 sotto cap;
7. replay/static model/Amdahl e ledger M01–M80;
8. early stop prima di candidate code quando il ceiling è insufficiente;
9. rebuild default-off, fixed @20 finale e full CTest;
10. report e invariance audit.

Early stops: hard kill a `RSS >= 2,000,000,000`; reject prima del codice per
ceiling inferiore a `1.8175x` whole/`2.1469x` traversal; reject dopo replay per
qualunque drift; niente @120/target candidate senza @20/@40 projection
compatibile. Budget usato: 10 baseline processi validi, un profilo e un fixed
finale; 0/5 replay candidate, 0/3 full-solver candidate, 0/2 qualification
candidate, 0/1 target candidate.

Contaminazione: temperature/outlier senza evidenza concreta non autorizzano
la sostituzione; ogni run directory è immutabile; stdout/stderr e metadata
sono conservati. Gerarchia evidenza: E0 modello/static lower bound, E1
micro/replay/profile, E2 fixed/full target fresh process. L'avanzamento
richiedeva pass simultaneo di semantica, RAM, tempo proiettato e genericità.

## Implementazione

Sono state promosse soltanto infrastrutture diagnostiche:

- `tools/run_tst_strict_cap.ps1`: materializza fixture da committed HEAD,
  congela il contract, produce i sette artifact obbligatori, campiona il peak
  RSS e applica il kill rigido;
- `tests/verify_tst_strict_cap_runner.ps1`: parse e token contract del runner;
- `GTOSD_PROFILE_HOTPATH_LIGHTWEIGHT`: modalità opt-in disponibile solo nelle
  build `GTOSD_ENABLE_HOTPATH_PROFILE=ON`; evita strutture telemetry ad alta
  memoria ma conserva timer e contatori scalari.

Commit creati prima del report:

| Commit | Contenuto |
|---|---|
| `80c92b5` | strict-cap runner + test |
| `9b63dcd` | lightweight profiling compile-gated |

Binary SHA-256:

| Binario | SHA-256 |
|---|---|
| baseline Release | `a317de864f547522882ceade73812461947880c12db08898f39a68d01cc554e0` |
| profile lightweight | `f4347abb93140b44e0e48da187b7176c61e49b4a933ff5c4af3fdedadae893c2` |
| final Release default-off | `12005e14ea3f82e27c3cddd0563c3fd3ee2418f82f621305640905f32ffd85c3` |

Il fixed @20 finale mostra identici dEV, Root, payoff sum, normalizzazione,
fingerprint e state bytes al baseline. Delta peak: `+12,288 B` rispetto al
baseline @20 statistico, rumore di processo; nessun buffer production è stato
aggiunto. Non esiste candidate diff production e non sono stati modificati
schedule, codec, solver semantics, fixture o checkpoint format.

## Validazione

### Run ledger

| Evidenza | Risultato |
|---|---|
| warm-up TST @20 | valido, escluso dalle statistiche |
| baseline TST @20 | 15.447068%, 16.894 s traversal circa, PASS semantica/RAM |
| baseline TST @40 | 7.438141%, 35.906 s traversal circa, PASS semantica/RAM |
| baseline TST @80 | 3.190581%, 74.190 s traversal circa, PASS semantica/RAM |
| baseline TST @120 x3 | min/median/max pubblicati sopra; 0 replacement |
| baseline TST target | 202 iter, 234.437726 s, time FAIL, RAM/correctness PASS |
| profile TST @20 | 57.658967 s perturbato; peak 1,970,749,440 B; PASS |
| final default-off @20 | 15.447068%, 16.788539 s traversal, peak 1,970,888,704 B; PASS |

Non esistono paired B/candidate ratios perché nessun candidato supera il kill
model. La proiezione @40 di qualunque candidato non è stata fabbricata; il
bound conservativo rimane almeno `154.54 s` traversal per il punto state
numericamente più credibile, che è già RAM FAIL. @80 candidate, @120 candidate
e target candidate sono quindi `not authorized by early stop`.

Correctness finale:

- dEV TST `0.9918630823% < 1%` a iterazione 202;
- Root TST `8.4946978034`, delta `-0.0069521966`, PASS;
- payoff sum `3.2196467714e-15`, PASS;
- maximum normalization error `0`, PASS;
- normalized NashConv `0.0167569583`, finita; exact-BR attiva;
- fingerprint TST e layout/state counts identici alla fixture;
- peak RSS strict PASS con headroom `28,963,840 B`;
- checkpoint schema/format e callback non modificati; compatibility per
  source invariance e test storage, nessun nuovo checkpoint TST serializzato.

Build/test:

| Verifica | Esito |
|---|---|
| Release MSVC 19.51, C++20, `/O2 /Ob3`, `/W4 /WX` | PASS |
| production contract | PASS |
| phase7 / phase10 / GTO+ reference | PASS |
| full CTest finale | 26/26 PASS |
| `git diff --check` prima dei commit | PASS |
| runner parse/static contract | PASS |

Il primo full-build/CTest ha esposto DLL runtime non copiate nel nuovo build
tree (`gtest*.dll`, poi `benchmark*.dll`, exit `0xc0000135`). Le dipendenze
sono state verificate con `dumpbin`, copiate dal vcpkg già validato e il full
CTest è stato rieseguito da zero: 26/26 PASS. Sono failure tecniche ambientali,
non regressioni sorgente.

Accounting:

```text
valid benchmark/profile processes = 12
contaminated runs                 = 0
replacement runs                  = 0
technical benchmark-run failures  = 0
technical setup/test attempts      = 5
```

Le cinque failure tecniche sono due configure offline/dependency iniziali,
una attesa Phase10 per DLL runtime, una discovery GoogleTest e un primo CTest
benchmark senza Google Benchmark DLL. Tutte hanno diagnosi concreta; nessun
run prestazionale pubblicato è stato sostituito.

## Decisioni

### Decision matrix

| Gate | Evidenza | Decisione |
|---|---|---|
| contract/exactness | tutti i run validi | PASS |
| strict peak RSS | TST max pubblicato 1,971,355,648 B | PASS stretto |
| baseline TST time | 234.437726 / 128.988889 s | FAIL |
| dominant cause | state scale/recode dataflow | ATTRIBUTED |
| required ceiling | 2.146879x traversal | vincolante |
| state family | realistic points RAM/numerical/time FAIL | CLOSED |
| producer/traversal/scheduling | ceiling insufficiente | CLOSED |
| BR/callback/codegen-only | Amdahl insufficiente | CLOSED |
| candidate admissibile | nessuna | NONE |
| regression | full CTest 26/26 | PASS |

**Outcome unico: C. ADMISSIBLE FRONTIER EXHAUSTED.**

La frontiera software documentata M01–M80 è chiusa rispetto all'obiettivo
congelato. Non viene autorizzato un candidato production e non viene dichiarata
parity. Production resta B `1.5/0/2`; S6 resta respinto; fixture, target e cap
restano invariati. Il protocollo five-process rimane non autorizzato perché
non esiste candidato che superi gli early gate. Non sono stati raccolti
hardware cache counters/ETW, un checkpoint TST round-trip fresco, PGO training
o un candidate target: nessuno di questi può cambiare il lower bound corrente
senza una nuova domanda/constraint.

Nessun push, cherry-pick, merge o modifica automatica di `main` è stato
eseguito. Gli artifact rimangono esclusivamente sotto
`.tmp/tst-strict-2gb-bottleneck-loop/`.

L'audit finale del checkout principale conferma HEAD/origin `77677b2`, stessi
soli tre file tracked dirty e SHA-256 byte-identici all'ingresso:
`A7CB7482...F6DC`, `8C475ED4...F9B7`, `D0371E10...833`; `.reasonix/` e `.tmp/`
sono rimasti untracked e intatti.

## Passo successivo

Unica azione: **governance decision** su quale vincolo riaprire (tempo, cap
RAM, exactness/formato di stato oppure hardware); con il contract corrente non
resta un'ulteriore attività software autorizzata per questa frontiera.
