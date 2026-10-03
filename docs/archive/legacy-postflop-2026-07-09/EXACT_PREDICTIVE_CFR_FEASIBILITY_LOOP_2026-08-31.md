# Exact Predictive-CFR Feasibility + Common Convergence Objective Loop

> **CORREZIONE SEMANTICA 2026-09-04 — RAM BLOCKER RITIRATO.** I requisiti di
> stato derivati restano evidenza, ma il cap 2 GB usato per respingere la
> famiglia era inesistente. L'esito memory-based non è più corrente; servono un
> ledger solver-owned e un nuovo pre-gate. Vedere il
> [`piano di correzione`](../../GTO_PLUS_SOLVER_MEMORY_SEMANTICS_AND_GATE_CORRECTION_PLAN_2026-09-03.md).

Data: 2026-08-31
Decisione: **PREDICTIVE FAMILY EXHAUSTED UNDER THE FROZEN RAM/STATE CONTRACT — nessuna promozione production**

## 1. Checkout iniziale

- branch: `main`;
- HEAD iniziale: `bf7b11ef603a43bc23debe0f09d781de44ee0cf1`;
- commit: `bf7b11e docs(perf): record common convergence acceleration`;
- dopo `git fetch origin main`: `main...origin/main = 0/0`;
- nessun reset e nessun push;
- `.reasonix/`, `.tmp/`, corpus real-node e output del common convergence loop preservati.

Il worktree iniziale conteneva modifiche preesistenti nelle tre fixture: gamma
da `2` a `3`. Non sono state modificate, staged o usate come authority. Tutti
i valori production e layout riportati qui sono stati letti da `HEAD` con
`git show HEAD:<fixture>`; il contratto committed resta gamma `2`.

## 2. Provenienza e invarianti

La fase precedente ha chiuso il vicinato statico signed DCFR alpha/gamma, la
sensitivity beta positiva, DCFR+ e HS-DCFR(30) come **COMMON EXACT SCHEDULE
SPACE EXHAUSTED**. Il report autorevole è
[`COMMON_EXACT_CONVERGENCE_ACCELERATION_LOOP_2026-08-31.md`](COMMON_EXACT_CONVERGENCE_ACCELERATION_LOOP_2026-08-31.md).

Questa fase riapre soltanto la famiglia predictive/optimistic. Restano comuni
e congelati per AHKHQH, TH7D6S e TSTC9D:

```text
game/tree/ranges/boards/sizings/outcomes = unchanged
state backend                              = ScaledUint16RegretStrategy
CPU / maximum threads                     = same machine / 8
certification                             = current exact BR cadence
target                                    = strict dEV < 1%
fixture_specific_logic                    = none
```

## 3. Baseline B e comparator S6

Production authority B è exact alternating signed DCFR `1.5/0/2`, delay zero.

| Fixture | Target | dEV | Tempo | Limite | Ratio | Esito |
|---|---:|---:|---:|---:|---:|---|
| AHK | @80 | 0,655665% | 0,670928 s | 1,900000 s | 0,353120 | PASS |
| TH | @80 | 0,806385% | 17,645055 s | 19,622222 s | 0,899237 | PASS |
| TST | @202 | 0,991863% | 208,111772 s | 128,988889 s | 1,613409 | FAIL |

S6, signed DCFR `1.5/0/5`, resta **STRONG RESEARCH BASELINE**, non production:
AHK passa circa @60, TH @80, TST misura `1,35433%` @120 e proietta il crossing
continuo circa @145 / checkpoint reale circa @160. Il worst ratio favorevole è
circa `1,25x`, quello avverso circa `1,46x`.

## 4. Theory audit

Fonti primarie:

- Farina, Kroer e Sandholm, [Predictive Blackwell Approachability / PCFR+](https://arxiv.org/html/2007.14358);
- Xu et al., [PDCFR+: weighted CFR con optimistic OMD](https://arxiv.org/html/2404.13891);
- [implementazione PDCFR+ degli autori](https://github.com/rpSebastian/PDCFRPlus);
- Farina et al., [Stable-Predictive Optimistic CFR](https://proceedings.mlr.press/v97/farina19a/farina19a.pdf).

Per un infoset `j`, perdita counterfactuale `ell`, policy giocata `x` e regret
istantaneo:

```text
r = <ell, x> 1 - ell
```

PCFR+ / PRM+ con predictor standard `m^(t+1)=ell^t`:

```text
R^t                 = [R^(t-1) + r^t]+
v^(t+1)             = <m^(t+1), x^t> 1 - m^(t+1) = r^t
R_pred^(t+1)        = [R^t + v^(t+1)]+
x^(t+1)             = normalize(R_pred^(t+1))
average gamma=2/5   = common weighted average, chosen once for all fixtures
```

PDCFR+ applica il weighting derivato da weighted OMD:

```text
d_t                  = (t-1)^alpha / ((t-1)^alpha + 1)
R^t                  = [d_t R^(t-1) + r^t]+
d_(t+1)              = t^alpha / (t^alpha + 1)
R_pred^(t+1)         = [d_(t+1) R^t + v^(t+1)]+
x^(t+1)              = normalize(R_pred^(t+1))
X^t                  = ((t-1)/t)^gamma X^(t-1) + x^t
```

PCFR+ mantiene regret non negativo; PDCFR+ mantiene regret non negativo e lo
sconta. Non sono signed DCFR con un termine aggiunto. Il predictive DCFR signed
dell'appendice PCFR è solo “predictive-in-spirit”: gli stessi autori dichiarano
di non avere una prova che sia predictive nel senso formale. Non è quindi una
base production sound per comporre B/S6.

### Alternating-update validity

La derivazione PCFR+/PDCFR+ costruisce regret minimizer locali e la prova di
equilibrio usa profili dei due giocatori allo stesso indice `t`. PCFR+ definisce
poi l'alternanza come heuristic sperimentale; PDCFR+ usa anch'esso alternating
in tutti gli esperimenti, ma la sua Theorem 2 è scritta sul profilo simultaneo
`(x^t,y^t)`. Quindi:

```text
published alternating algorithm definition = YES
published empirical alternating evidence   = YES
formal transfer of the stated NE theorem    = NOT ESTABLISHED BY THESE SOURCES
```

Non viene presentata l'alternanza come theorem-equivalent. Una variante
simultaneous aderente alla derivazione eliminerebbe questa incertezza, ma non
elimina il requisito di stato descritto sotto e fallisce lo stesso RAM gate.

## 5. Candidate definitions

| ID | Algorithm | Common params | Predictor | Averaging | Same all fixtures? |
|---|---|---|---|---|---|
| P1 | PCFR+ | projection non-negative | previous loss / current instant regret | gamma 2 | YES |
| P2 | PCFR+ | projection non-negative | previous loss / current instant regret | gamma 5 | YES |
| P3 | PDCFR+ | alpha 2,3 | previous loss / current instant regret | gamma 5 | YES |
| P4 | predictive signed DCFR | alpha 1,5; beta 0 | previous loss | gamma 2/5 | YES |

`2,3/5` è soltanto il common seed empirico pubblicato per PDCFR+, non tuning
fixture. Il theorem PDCFR+ richiede inoltre che `tau_t/w_t` sia positivo e non
crescente; il paper distingue esplicitamente questa condizione dal range più
ampio scelto in pratica. P4 fallisce theory perché la fonte lo presenta senza
prova formale. P1–P3 passano la formula audit; le garanzie P3 sono condizionali
ai pesi del theorem e la versione alternating resta una definizione
sperimentale pubblicata, non una garanzia trasferita.

Adaptive/stable-predictive non è stata implementata: richiede almeno le stesse
due decisioni (base e optimistic) e ulteriore step-size/state, quindi è
strictly non migliore al RAM pre-gate. Aprirla dopo il kill gate non potrebbe
produrre un common contract valido sotto il backend congelato.

## 6. State requirement e reuse analysis

Il backend corrente occupa già entrambi i payload per azione:

```text
scaled signed cumulative regret = 2 B/action
scaled cumulative average       = 2 B/action
regret + average node scales     = 8 B/canonical decision node
```

In B/S6 la current policy è derivabile dal cumulative regret. In P1–P3 non lo
è: `x^(t+1)` deriva da `R_pred`, mentre l'update successivo deve conservare
separatamente `R`. Il regret istantaneo può essere consumato nel node scratch
durante l'update, ma una delle due quantità seguenti deve sopravvivere tra le
visite:

- current predictive policy; oppure
- predicted regret dal quale ricostruirla.

La cumulative average non è riusabile: è l'output certificato. Il cumulative
regret non è riusabile: serve al prossimo update. La clipping rende
irreversibile il tentativo di ricostruire `R` da `R_pred`. Un replay della
perdita precedente richiederebbe la policy precedente o un'altra traversata e
non rimuove lo stato necessario. Disk paging conserva i byte e aggiunge I/O;
non è una soluzione di RAM/tempo production.

Persistent state P1–P3: cumulative regret + cumulative average + current
predictive policy. Transient state: action values, `r` e `R_pred` node-locali,
fondibili nello scratch esistente. Per-thread scratch non cresce nel modello
più favorevole. P4 richiede almeno lo stesso terzo payload; la quadratic-average
loss prediction ne richiederebbe un altro.

## 7. RAM pre-gate

Il modello più favorevole salva solo `actions - information_sets` probabilità
indipendenti a 16 bit e ricostruisce l'ultima probabilità. È già più ottimistico
di un backend operativo e implicherebbe una nuova codifica; serve come lower
bound. I layout sono quelli committed in `HEAD`.

| Fixture | Actions | Infosets | State B | Extra policy u16 lower bound | State projected |
|---|---:|---:|---:|---:|---:|
| AHK | 1.288.290 | 595.626 | 5.300.664 B | 1.385.328 B | 6.685.992 B |
| TH | 83.318.592 | 36.596.832 | 334.452.416 B | 93.443.520 B | 427.895.936 B |
| TST | 366.890.152 | 145.524.152 | 1.472.605.376 B | **442.732.000 B** | **1.915.337.376 B** |

Mantenendo invariato l'overhead RSS osservato, il lower bound produce:

| Fixture | Peak B | Peak projected | Cap desktop | Gate |
|---|---:|---:|---:|---|
| AHK | 166.510.592 B | 167.895.920 B | 2.000.000.000 B | PASS |
| TH | 798.371.840 B | 891.815.360 B | 2.000.000.000 B | PASS |
| TST | 1.969.922.048 B | **2.412.654.048 B** | 2.000.000.000 B | **FAIL +412.654.048 B** |

La rappresentazione naturale `uint16 predicted regret + scale` costa su TST
`736.302.688 B` e proietta `2.706.224.736 B` peak. Perfino l'irrealistica
lower bound a 8 bit per probabilità indipendente aggiunge `221.366.000 B` e
proietta `2.191.288.048 B`, oltre il cap di `191.288.048 B`; inoltre violerebbe
la representation freeze e non è numericamente validata.

Conclusione RAM:

```text
P1 RAM = FAIL
P2 RAM = FAIL
P3 RAM = FAIL
P4 RAM = FAIL
```

Poiché la common candidate deve passare tutte le fixture, il FAIL TST termina
la candidate prima dei solver benchmark, come richiesto dal RAM kill gate.

## 8. Small formula oracle

È stato aggiunto l'oracle test-only
[`predictive_cfr_oracle_tests.cpp`](../../../tests/predictive_cfr_oracle_tests.cpp).
Non espone enum, flag o dispatch production. Verifica:

- 2 e 3 azioni;
- regret positivo, negativo e zero;
- predictor esatto e inaccurato;
- predictor degenere, che riduce la strategy selection a RM+;
- perdite costanti e oscillanti;
- normalizzazione e finitezza;
- formule PCFR+ e PDCFR+ discount-before-prediction.

Risultato Release `/W4 /WX` standalone:

```text
predictive_cfr_oracle assertions=169
PASS
```

## 9. Tiny triplet, overhead e common curves

Nessun solve P1–P4 è stato autorizzato. Di conseguenza:

| Misura | Stato | Motivo |
|---|---|---|
| AHK/TH/TST @20 | NOT RUN | common contract già RAM FAIL |
| predictor construction telemetry | NOT RUN | nessun solver path valido |
| AHK/TH/TST common curves | NOT RUN | early stop obbligatorio |
| B/P/S6 repeated timing | NOT RUN | nessun finalist |
| target-driven | NOT RUN | prerequisiti theory/RAM non superati |

Non vengono inventate curve, Root, NashConv o throughput. B e S6 restano gli
unici comparator misurati. Un prototipo che ignora il terzo payload non sarebbe
PCFR+/PDCFR+; uno che alloca il payload non è un contract RAM-valido.

## 10. Dominance e objective ledger

| Loop | Algorithm | Common params | Theory | RAM | AHK curve | TH curve | TST curve | Worst predicted ratio | Decision |
|---:|---|---|---|---|---|---|---|---:|---|
| 0 | B | signed 1.5/0/2 | PASS | PASS | target measured | target measured | @202 measured | 1,613409 actual | production baseline |
| 1 | S6 | signed 1.5/0/5 | PASS | PASS | @120 measured | @120 measured | 1,35433% @120 | 1,25–1,46 | strong research baseline |
| 2 | P1 | PCFR+ gamma2 | simultaneous PASS; alternating proof gap | FAIL | n/r | n/r | n/r | n/a | reject before solve |
| 3 | P2 | PCFR+ gamma5 | simultaneous PASS; alternating proof gap | FAIL | n/r | n/r | n/r | n/a | reject before solve |
| 4 | P3 | PDCFR+ 2,3/gamma5 | conditional theorem; empirical seed; alternating proof gap | FAIL | n/r | n/r | n/r | n/a | reject before solve |
| 5 | P4 | predictive signed DCFR | FAIL: no formal predictive proof | FAIL | n/r | n/r | n/r | n/a | reject |

`baseline_B_comparison`: nessuna P ha un contratto allocabile, quindi non può
ridurre validamente il worst ratio B. `baseline_S6_comparison`: S6 usa meno
stato, ha curve reali e rimane non dominato; ogni P è dominata sul primo gate
lessicografico applicabile (theory per P4, RAM per P1–P3).

## 11. Target authorization

| Candidate | Theory | RAM | Common | AHK/TH projected | TST <=140 | Throughput | Authorized? | reason_target_driven_was_authorized |
|---|---|---|---|---|---|---|---|---|
| P1/P2/P3 | qualified / alternating gap | FAIL | conceptual YES | unknown | unknown | unknown | NO | `not_authorized: TST RAM pre-gate failed` |
| P4 | FAIL | FAIL | conceptual YES | unknown | unknown | unknown | NO | `not_authorized: theory and RAM failed` |

Target result, five-process e production rebaseline: **0**.

## 12. Production decision

> **PREDICTIVE FAMILY EXHAUSTED UNDER THE FROZEN RAM/STATE CONTRACT.** Le
> implementazioni matematicamente fedeli PCFR+ e PDCFR+ richiedono una current
> predictive policy distinta da cumulative regret e cumulative average. Anche
> il lower bound di storage più favorevole rompe il cap TST prima di qualsiasi
> solve. La variante signed predictive DCFR disponibile in letteratura è
> inoltre dichiarata senza prova predictive formale.

Questo non afferma che nessun algoritmo optimistic futuro possa funzionare.
Afferma che le famiglie predictive credibili studiate non sono allocabili con
`ScaledUint16RegretStrategy` e il cap corrente, senza riaprire representation,
RAM o una diversa decomposizione di stato.

Production resta exact alternating signed DCFR `1.5/0/2`. S6 `1.5/0/5` resta
STRONG RESEARCH BASELINE, non viene promossa e non esiste dispatch per fixture.

## 13. Benchmark cost accounting

| Voce | Conteggio |
|---|---:|
| solver run | 0 |
| TST solver run | 0 |
| target-driven run | 0 |
| repeated B/P/S6 process | 0 |
| oracle executable run | 3 PASS (standalone, targeted CTest, full CTest) |
| oracle assertions | 169 PASS |

Il budget è stato protetto dal pre-gate: nessun minuto solver è stato speso per
un candidato già matematicamente incompatibile con il cap.

Validazione finale:

```text
fresh Release build /W4 /WX                         PASS
targeted predictive oracle CTest                    1/1 PASS
full CTest on preserved dirty worktree              23/24 PASS
sole failure: production contract sees gamma=3      PRE-EXISTING FIXTURE EDIT
production contract on fixtures extracted from HEAD PASS
git diff --check                                    PASS (EOL warnings only)
```

Il fallimento CTest non è stato nascosto né corretto modificando file utente:
`gtosd_production_dcfr_contract` legge intenzionalmente le fixture del
worktree e rileva il gamma 3 preesistente. Lo stesso test, eseguito sulle tre
fixture committed estratte in `.tmp`, passa e conferma B `1.5/0/2` comune.

## 14. Limiti

- Il peak projected mantiene l'overhead B invariato ed è quindi ottimistico.
- Non è stata misurata convergence predictive: il report non la stima.
- L'alternating PCFR+/PDCFR+ ha evidenza sperimentale pubblicata, ma le fonti
  esaminate non trasferiscono esplicitamente il theorem simultaneo.
- Nessuna certification, fixture, hardware, state backend o target è cambiata.

## 15. Passo successivo

**Aprire soltanto una Lazy-CFR / Lazy-CFR+ feasibility task iniziando dal RAM
pre-gate di persistent update scheduling metadata e dalla prova che il lazy
state non supera il margine TST. Non implementare Lazy-CFR prima di quel
modello e non riaprire predictive insieme alla nuova architettura.**
