# Memory-neutral exact FD-FTRL / FD-OMD feasibility loop

> **CORREZIONE SEMANTICA 2026-09-04 — REPORT STORICO.** Il blocker di costo
> locale resta supportato dalle misure; il precedente “RAM gate” non è invece
> normativo, perché il riferimento GTO+ non è Peak RSS e non esiste un cap
> desktop generale. Vedere il
> [`piano di correzione`](../../GTO_PLUS_SOLVER_MEMORY_SEMANTICS_AND_GATE_CORRECTION_PLAN_2026-09-03.md).

Data: 2026-08-31

Fixture: `GTP-AHKHQH-003`, `GTP-TH7D6S-101`, `GTP-TSTC9D-101`

Outcome: **FD-FTRL/OMD LOCAL-COST BLOCKER**

La famiglia practical `R` può essere rappresentata, in linea di principio,
nei due payload persistenti correnti. Il kill gate è successivo: sul mix reale
di arità TST il solve locale direct aggiunge un lower bound ottimistico di
`0,295609 s/iterazione` per FD-FTRL(R) e `0,270888 s/iterazione` per
FD-OMD(R), già assumendo scaling perfetto su otto thread ed escludendo costi
di integrazione. Restano al massimo `82,4768` e `84,2703` iterazioni per
raggiungere il limite TST. Non esiste evidenza common che renda plausibile un
crossing così anticipato: S6 è ancora a `1,35433%` @120 e attraversa
continuamente circa @145. Il loop si è quindi fermato prima di implementare o
eseguire un solver candidate.

## 1. Correzione aritmetica Lazy

Pre-loop correction: il report Lazy convertiva erroneamente bytes/infoset in
bits/infoset. `0,254` è stato corretto in:

```text
30.077.952 / 145.524.152 = 0,20668701096 B/infoset
0,20668701096 * 8        = 1,6534960877 bit/infoset
```

La ricerca con `rg -n "0[,.]254|bit/infoset|30[.]077[.]952" docs` ha trovato
una sola occorrenza errata. La correzione è nel commit
`21275a2ccfe83c1cc731c2f6f4e87fa69e40665d`. Il **LAZY FAMILY RAM BLOCKER**
non cambia: un bit non rappresenta il residuo continuo e `1 B/infoset`
supera già il cap.

## 2. Initial HEAD post-correzione

Prima della correzione, `main`, `HEAD` e `origin/main` erano allineati a
`5d17a2ad2f852d037750c3d3560eb93386aca2bb`. Dopo `git fetch origin main` e
il commit documentale, l'initial HEAD del research loop è:

```text
21275a2ccfe83c1cc731c2f6f4e87fa69e40665d
```

Le tre modifiche utente `dcfr_average_exponent: 2 -> 3`, `.reasonix/`,
`.tmp/` e i corpus esistenti sono rimasti non staged e non modificati.
Nessun push è stato eseguito.

## 3. Blocker ereditati e contratto congelato

Restano autorevoli:

```text
COMMON EXACT SCHEDULE SPACE EXHAUSTED
PREDICTIVE FAMILY EXHAUSTED UNDER THE FROZEN RAM/STATE CONTRACT
LAZY FAMILY RAM BLOCKER
S6 = STRONG RESEARCH BASELINE, NOT PRODUCTION
```

Sono rimasti congelati gioco, tree, range, board, sizing, terminal outcome,
semantica canonica, `strict dEV < 1%`, Root, exact BR, payoff sum,
normalizzazione, tempi GTO+, CPU-only, otto thread e cap
`2.000.000.000 B`. Non sono stati introdotti sampling, bucketing, pruning,
GPU, hardware diverso o dispatch per fixture.

## 4. Baseline production B

`B` è exact alternating signed DCFR comune `alpha=1.5`, `beta=0`,
`gamma=2`, delay zero.

| Fixture | Target | Solver / limite | Esito |
|---|---:|---:|---|
| AHKHQH | @80 | `0,670928 / 1,900000 s` | PASS |
| TH7D6S | @80 | `17,645055 / 19,622222 s` | PASS |
| TSTC9D | @202 | `208,111772 / 128,988889 s` | FAIL |

Per TST: traversal `174,926380 s`, certification `32,780800 s`, other
`0,404592 s`; il traversal authority vale `0,865972178 s/iterazione`.

## 5. Baseline di ricerca S6

`S6` è signed DCFR comune `1.5/0/5`. AHK attraversa prima di @60, TH prima
di @80, ma TST misura `1,35433%` @120, crossing continuo circa @145 e primo
checkpoint realistico @160. Il proxy favorevole TST è `161,046904 s`, ratio
`1,24854x`. S6 resta il comparator algoritmico più forte e non è production.

## 6. Audit integrale della fonte primaria

Fonte: Liu, Jiang, Li, Li, [*Equivalence Analysis between Counterfactual
Regret Minimization and Online Mirror Descent*](https://proceedings.mlr.press/v162/liu22e.html),
ICML/PMLR 2022. È stato letto il paper completo, incluse appendici e prove. Il modello opera
su treeplex di decisioni locali `x_hat_j` nel simplex. Le loss locali sono le
counterfactual loss ricorsive, ottenute bottom-up aggiungendo ai loss di
sequenza i termini future-dependent dei figli.

La regularizer locale FD è:

```text
psi_j^t(x) = 1/2 beta_j^t ||x||_2^2
           + 1/2 beta_j^t ||x_hat_j^{t+1}||_2^2,
beta_j^t > 0.
```

FD-FTRL minimizza cumulative loss più la DGF dilatata; FD-OMD usa la
linearizzazione corrente e il centro mirror precedente. Il termine che
dipende da `x^{t+1}` si localizza bottom-up e non crea una dipendenza
circolare. Il teorema 3.5 dà un regret bound adattivo; con lambda non
decrescente e della scala prescritta, il corollario 3.9 dà regret
sublineare `O(sqrt(T))` alle practical `R` variants.

Il teorema 3.7 prova:

```text
CFR-RM  == FD-FTRL con beta_j^t = ||[R_hat_j^t]^+||_1
CFR-RM+ == FD-OMD  con beta_j^t = ||Q_hat_j^t||_1
```

Il paper chiama questi control `FD-FTRL(CFR)` e `FD-OMD(CFR)`. Le practical
variants hanno la stessa complessità spaziale asintotica di vanilla CFR, ma
risolvono un vincolo piecewise ad ogni decisione: `O(n_j^2)` direct oppure
`O(n_j log n_j)` con sort e ricerca binaria.

Configurazioni pubblicate:

```text
CW: lambda_j^t = eta * average_opponent_reach_j * n_j * ||A||_inf^2 * T
LW: lambda_j^t = eta * current_opponent_reach_j * n_j * ||A||_inf^2 * t
default: CW + Linear Averaging
```

Per FD-FTRL(R) il paper pesa inoltre linearmente le loss come LCFR. `eta` è
globale nella formula, ma gli esperimenti pubblicati lo selezionano con grid
search per gioco: questo non autorizza tuning per fixture in GTOSD.

## 7. Semantica alternating

Il regret theorem è formulato per la sequenza online del singolo player. Il
paper dichiara che tutti gli esperimenti usano alternating updates come CFR,
ma non prova che quella implementazione coincida con il fresh-player-pass di
GTOSD. La compatibilità operativa è plausibile, non una theorem equivalence.

Un secondo confine irrisolto sarebbe rimasto prima della produzione: CW usa
un orizzonte `T`, mentre il solve target-driven non ha necessariamente un
orizzonte fisso. Inoltre la derivazione practical richiede `lambda_j^t > 0`;
reach zero rende la formula CW/LW nulla. Non è stata nascosta una epsilon: il
caso è testato e registrato come boundary esplicito. Il local-cost gate rende
inutile scegliere ora una policy aggiuntiva.

## 8. Formule FD-FTRL / FD-OMD

Con `L_hat'_j^t` cumulative future-dependent loss, FD-FTRL(R) trova l'unico
`alpha_j^t` tale che:

```text
||[alpha_j^t 1 - L_hat'_j^t]^+||_2^2 = lambda_j^t             (13)
R_hat'_j^t = alpha_j^t 1 - L_hat'_j^t
x_hat_j^{t+1} = [R_hat'_j^t]^+ / ||[R_hat'_j^t]^+||_1
beta_j^t = sqrt(lambda_j^t) / ||x_hat_j^{t+1}||_2.
```

Con `l_hat'_j^t` loss istantanea future-dependent, FD-OMD(R) risolve:

```text
||[sqrt(lambda_j^{t-1}) x_hat_j^t / ||x_hat_j^t||_2
   + alpha_j^t 1 - l_hat'_j^t]^+||_2^2 = lambda_j^t          (14)
Q_hat'_j^t = [Q_hat'^{t-1}_j + alpha_j^t 1 - l_hat'_j^t]^+
x_hat_j^{t+1} = Q_hat'_j^t / ||Q_hat'_j^t||_1.
```

Nei control CFR il target è L1 invece di L2: beta uguale al positive regret
L1 ricostruisce RM; beta uguale a `||Q||_1` ricostruisce RM+.

## 9. Requisiti state byte-level

| Quantità | Granularità | FD-FTRL(R) | FD-OMD(R) | Persistenza |
|---|---|---|---|---|
| current strategy | action | normalize positive state | normalize `Q'` | derivata |
| average strategy | action | linear average | linear average | payload B |
| cumulative loss | action | `L^t=t A y_bar^t` | non richiesta | ricalcolata / no |
| regularizer beta | infoset | da lambda e norma | da lambda e norma | transiente |
| lambda | infoset | CW/LW formula | CW/LW formula | derivata |
| opponent reach | infoset | da average/current opponent strategy | idem | transiente |
| previous iterate | action | non indipendente | dentro `Q'` | payload A per OMD |
| prox/dual state | action | `R'=alpha1-L'` | `Q'=beta*x` | payload A |
| normalization scalar | node | codec scale | codec scale | scale esistente |

Per FTRL, `L^t = sum_k A y^k = t A y_bar^t` prova che il cumulative loss può
essere ricalcolato dalla average strategy avversaria già disponibile. Per OMD,
`Q' = beta*x_next` è contemporaneamente accumulator e centro del passo
successivo. Non serve conservare separatamente `x`, beta o lambda.

## 10. Reuse mapping

```text
FD-FTRL(R):
  payload A uint16 + signed node scale -> R' / dual decision state
  payload B uint16 + average node scale -> linear average strategy

FD-OMD(R):
  payload A uint16 + nonnegative node scale -> Q' = beta*x / prox center
  payload B uint16 + average node scale     -> linear average strategy
```

Il significato di payload A cambierebbe e richiederebbe un nuovo algorithm e
checkpoint version esplicito in caso di promotion, ma non richiede un terzo
payload. Nessun checkpoint production è stato reinterpretato.

## 11. Modello RAM

TST contiene `366.890.152` action entry e `630.596` decision node canonici:

```text
2 payload * 2 B * 366.890.152 = 1.467.560.608 B
2 scale   * 4 B *     630.596 =     5.044.768 B
current/projected state         = 1.472.605.376 B
current/projected peak RSS      = 1.969.922.048 B
headroom                        =    30.077.952 B
```

Il preliminary RAM gate passa esclusivamente con il reuse sopra. Qualunque
lambda, beta, previous iterate o loss indipendente per infoset/action
costituirebbe un terzo state e riaprirebbe il RAM kill gate; non è autorizzato.

## 12. Modello di precisione

FD-FTRL quantizza uno state signed `R'`: il segno decide il positive part e il
codec deve mantenere entrambe le code. FD-OMD quantizza `Q' >= 0`; non serve
un segno, ma piccoli pesi possono essere azzerati. In entrambi i casi la
decision map normalizzata è invariante a una scala positiva comune al nodo.

Il numerical oracle verifica error bound di mezzo step per i16/u16,
invarianza di scala, errore L1 di policy `<1e-4` sui vettori piccoli e una
recurrence OMD u16 di 64 passi con errore L1 `<1e-3`. È un gate bounded, non
una prova di stabilità sui real-node production: l'esito resta
`preliminary numerical PASS`, sufficiente solo per raggiungere il cost gate.

## 13. Equivalence oracle

`tests/fd_ftrl_omd_oracle_tests.cpp`, commit
`112272b90d61e7dc5b90c4f5f7ab4493b3f73618`, copre:

```text
2, 3, 4 actions
depth 1 e depth 2 con child branching
zero, constant, alternating e dominant loss
tie, zero reach, non-uniform opponent reach
FD-FTRL(CFR) vs CFR-RM
FD-OMD(CFR) vs CFR-RM+
```

Risultato Release: `fd_ftrl_omd_oracle assertions=18670`; targeted CTest
`2/2 PASS` includendo il kernel smoke. L'inizializzazione zero usa il fallback
uniforme CFR; il limite epsilon del paper non viene simulato con un valore
finito che altererebbe l'equivalenza.

## 14. Distribuzione reale action count

La distribuzione è ricavata dai layout/corpus esistenti; non è stato eseguito
un solver.

| Fixture | Infoset | 2 actions | 3 actions | 4 actions | 5 actions |
|---|---:|---:|---:|---:|---:|
| AHK | `595.626` | `498.588` (83,71%) | `97.038` (16,29%) | 0 | 0 |
| TH | `36.596.832` | `26.471.904` (72,33%) | `10.124.928` (27,67%) | 0 | 0 |
| TST | `145.524.152` | `91.492.368` (62,87%) | `36.289.672` (24,94%) | `13.674.160` (9,40%) | `4.067.952` (2,80%) |

Questa distribuzione, e non la sola complessità asintotica, pesa il boundary.

## 15. Timing del kernel locale

`benchmarks/fd_ftrl_omd_local_benchmark.cpp`, commit
`eeca5239ff9949f2a8036b8ac06c2a86bf8aef93`, usa C++20 Release, Google
Benchmark 1.9.5, 4.096 sample deterministici, sette ripetizioni e mediane.
Macchina: otto logical CPU a 3.600 MHz, L3 6 MiB. Valori real-time per
infoset:

| n | RM | FTRL direct | OMD direct | FTRL sorted | OMD sorted |
|---:|---:|---:|---:|---:|---:|
| 2 | `6,651 ns` | `19,293 ns` | `18,220 ns` | `22,548 ns` | `21,118 ns` |
| 3 | `6,854 ns` | `30,315 ns` | `27,934 ns` | `33,756 ns` | `32,175 ns` |
| 4 | `8,700 ns` | `37,862 ns` | `36,297 ns` | `45,190 ns` | `44,162 ns` |
| 5 | `10,285 ns` | `47,564 ns` | `45,609 ns` | `59,142 ns` | `57,387 ns` |

Componenti direct, sempre ns/infoset:

| n | load | regularizer | piecewise | normalize/store | bisection |
|---:|---:|---:|---:|---:|---:|
| 2 | 1,474 | 1,504 | 15,391 | 3,792 | 272,761 |
| 3 | 1,804 | 1,571 | 24,535 | 5,188 | 302,450 |
| 4 | 1,528 | 1,596 | 30,324 | 7,037 | 323,547 |
| 5 | 2,026 | 1,674 | 38,728 | 8,852 | 345,979 |

Direct small-N domina sort per ogni arità; la bisection a 32 passi è molto
più lenta. Le alternative `O(n log n)` sono quindi chiuse presto. Pesato sulle
distribuzioni reali, RM/FTRL/OMD misurano rispettivamente:

| Fixture | RM | FTRL | ratio | OMD | ratio |
|---|---:|---:|---:|---:|---:|
| AHK | `6,684 ns` | `21,089 ns` | `3,155x` | `19,803 ns` | `2,963x` |
| TH | `6,707 ns` | `22,343 ns` | `3,331x` | `20,908 ns` | `3,117x` |
| TST | `6,996 ns` | `24,577 ns` | `3,513x` | `23,107 ns` | `3,303x` |

## 16. Boundary costo/convergenza

Il contatore production esistente registra `40.695.440.603` regret action
update su 120 iterazioni TST, cioè `339.128.671,692 action/iterazione` e una
frazione visitata `0,924332991`. Applicata agli infoset, il lower bound è
`134.512.774,731 local update/iterazione`.

Per essere deliberatamente favorevoli al candidate, si conta solo il delta
microbench rispetto a RM e lo si divide per otto, come se fosse perfettamente
parallelizzabile:

```text
FD-FTRL extra >= (24,577005 - 6,995965) ns * 134.512.774,731 / 8
              = 0,295609303 s/iterazione

FD-OMD extra  >= (23,106731 - 6,995965) ns * 134.512.774,731 / 8
              = 0,270887986 s/iterazione
```

Il budget traversal TST, sottratti certification e other correnti, è:

```text
128,988889 - 32,780800 - 0,404592 = 95,803497 s
```

| Candidate | Lower-bound traversal/iter | Maximum target iterations |
|---|---:|---:|
| FD-FTRL(R) | `0,865972178 + 0,295609303 = 1,161581481 s` | `82,4768` |
| FD-OMD(R) | `0,865972178 + 0,270887986 = 1,136860164 s` | `84,2703` |

Sono upper bound favorevoli: non includono ricostruzione `t A y_bar`, reach,
future-dependent traversal, codec/rescale, synchronization o load imbalance.
La family dovrebbe quindi attraversare prima di @83/@85, contro S6 circa
@145. Il segnale richiesto @120 (`~1,0–1,15%`) sarebbe già troppo tardi. Il
maggiore costo locale non è economicamente plausibile sotto il gate corrente.

## 17. Tiny common triplet

**NON AUTORIZZATO.** Theory, preliminary RAM e oracle hanno superato i loro
gate, ma `LOCAL COST PLAUSIBLE` è falso. AHK@20, TH@20 e TST@20: `0` run.

## 18. Common curves

**NON AUTORIZZATE.** Nessuna solve fixed e nessun checkpoint FD sono stati
creati. Le curve B e S6 ereditate restano le sole authority.

## 19. Frontiera B / S6 / FD

| Punto | State | Evidenza convergenza | Economia TST | Stato |
|---|---|---|---|---|
| B `1.5/0/2` | production | target @202 | `208,111772 s` | production, gate FAIL TST |
| S6 `1.5/0/5` | stesso formato | `1,35433%` @120, crossing ~145 | proxy `161,046904 s` | strong research baseline |
| FD-FTRL(R) | memory-neutral derivabile | nessuna solve autorizzata | richiede `<82,48` iter | local-cost reject |
| FD-OMD(R) | memory-neutral derivabile | nessuna solve autorizzata | richiede `<84,28` iter | local-cost reject |

FD-OMD domina FTRL solo nel microcosto, ma non supera il boundary. Nessuna
candidate FD entra nella frontiera misurata di time-to-target.

## 20. Ledger eta e weighting

| Candidate | Weighting / average | eta | Decisione |
|---|---|---|---|
| C0 FD-FTRL(CFR) | equivalence control | adattivo da regret | oracle PASS, non production |
| C0 FD-OMD(CFR) | equivalence control | adattivo da Q | oracle PASS, non production |
| C1 FD-FTRL(R) | published default CW + LA | non selezionata | REJECT al local-cost gate |
| C2 FD-OMD(R) | published default CW + LA | non selezionata | REJECT al local-cost gate |
| LW / UA / eta controls | non aperti | non selezionata | fuori budget dopo blocker |

Il `0,01` nel kernel serve solo a materializzare il costo della formula
regularizer; non è un eta candidate e non influenza il solve piecewise
misurato. Nessun eta per fixture è stato usato. Non è stata composta S6
gamma5 senza una prova di weighted-average validity.

## 21. Repeated A/B

**NON AUTORIZZATO.** Nessun finalista ha superato il cost gate; sequenze
`B/F/S6`, `S6/F/B`, `F/B/S6`: zero processi.

## 22. Target authorization

```text
reason_target_driven_was_authorized = NONE
reason_target_driven_was_rejected =
  optimistic maximum TST iterations is 82.4768 FTRL / 84.2703 OMD;
  no common evidence supports such an early strict-dEV crossing;
  S6 crosses only around 145 iterations.
```

Theory PASS non basta; il predicate `TST crossing/time plausibly PASS` è
falso. Numero totale di solver run FD: `0`, conforme al budget.

## 23. Risultati target

**NON ESEGUITI.** AHK, TH e TST target-driven FD sono tutti `not authorized`.
Non esistono nuovi dEV, Root, NashConv, peak RSS o timing da presentare.

## 24. Decisione production

Outcome finale:

```text
FD-FTRL/OMD LOCAL-COST BLOCKER
```

Production resta exact alternating signed DCFR `1.5/0/2`. S6 resta
`STRONG RESEARCH BASELINE, NOT PRODUCTION`. Non sono presenti enum, runtime
dispatch, flag, state dormiente, migration o checkpoint FD. Restano soltanto
l'oracolo matematico e il bounded benchmark generalizzabile.

## 25. Cost accounting e validazione finale

Objective lessicografico alla chiusura:

```text
theory_fail             = 0 per derivazione/oracle; alternating/CW boundaries documented
common_contract_fail    = not exercised; no per-fixture contract introduced
ram_fail                = 0 preliminary, with exactly two reused payloads
numerical_state_fail    = 0 in bounded oracle; real-node proof not reached
correctness_fail_count  = 0 in 18.670 oracle assertions
worst_time_ratio        = unavailable; solver not authorized
aggregate_time_ratio    = unavailable; solver not authorized
tst_time_to_target      = unavailable; maximum feasible derived analytically
tst_iterations_target   < 82,4768 FTRL / 84,2703 OMD
local_update_overhead   = 3,513x FTRL / 3,303x OMD versus RM on TST mix
complexity              = direct O(n^2), n in [2,5]
```

Full Release CTest dopo il compiled tooling: `25/26 PASS` in `201,74 s`.
L'unico FAIL è `gtosd_production_dcfr_contract`, come atteso perché il test
vede la modifica utente non committata gamma-3 in AHK. Le tre fixture sono
state estratte dal committed HEAD con `git archive`; lo stesso script
`verify_production_dcfr_contract.cmake` su quelle copie è `PASS`. Le fixture
utente non sono state corrette o staged.

## 26. Prossimo blocker / famiglia exact

Non aprire ReCFR, altri eta o nuovi codec per aggirare questo risultato:
ReCFR ricade nel framework FD-FTRL e deve prima dimostrare un costo locale
materialmente diverso. Predictive e Lazy restano chiusi. Il prossimo passo ad
alta priorità è il già documentato **constraint governance gate**: sotto CPU,
RAM, exact certification e target GTO+ congelati, nessuna family exact comune
studiata ha una strada economica dimostrata. Serve una decisione esplicita su
quale vincolo esterno rilassare prima di un altro solver research loop.
