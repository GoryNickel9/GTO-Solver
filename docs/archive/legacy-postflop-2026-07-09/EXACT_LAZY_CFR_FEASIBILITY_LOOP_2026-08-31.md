# Exact Lazy-CFR / Lazy-CFR+ Feasibility + Work-Reduction Objective Loop

> **CORREZIONE SEMANTICA 2026-09-04 — RAM BLOCKER RITIRATO.** Il lower bound
> addizionale resta utile, ma il cap contro cui fu confrontato non è mai stato
> un requisito desktop indipendente. L'esito `LAZY FAMILY RAM BLOCKER` non è più
> una decisione corrente; la famiglia richiede un nuovo pre-gate dopo
> l'accounting solver-owned. Vedere il
> [`piano di correzione`](../../GTO_PLUS_SOLVER_MEMORY_SEMANTICS_AND_GATE_CORRECTION_PLAN_2026-09-03.md).

Data: 2026-08-31
Decisione: **LAZY FAMILY RAM BLOCKER — nessuna implementazione solver, nessuna promozione production**

## 1. Initial HEAD

- branch iniziale: `main`;
- HEAD iniziale: `2b377ec93b5e0ef7874e5bd148c5a0aa8ff182d1`;
- commit: `2b377ec docs(perf): record predictive cfr feasibility`;
- dopo `git fetch origin main`: `main...origin/main = 0/0`;
- nessun reset e nessun push.

Il worktree iniziale conteneva le tre modifiche utente preesistenti richieste:
`dcfr_average_exponent` da `2` a `3` nelle fixture AHKHQH, TH7D6S e TSTC9D,
oltre a `.reasonix/` e `.tmp/` untracked. Non sono state modificate, staged o
usate come authority. Il contratto production e i cardinali sono stati
riverificati da `HEAD`; gamma committed resta `2`. Corpus real-node e output
dei loop precedenti sono stati preservati.

## 2. Inherited blockers e invarianti

Restano autorevoli:

```text
COMMON EXACT SCHEDULE SPACE EXHAUSTED
PREDICTIVE FAMILY EXHAUSTED UNDER THE FROZEN RAM/STATE CONTRACT
```

Questa fase apre soltanto Lazy-CFR/Lazy-CFR+. Per ogni candidate concettuale:

```text
same algorithm / threshold / averaging = AHKHQH = TH7D6S = TSTC9D
fixture_specific_logic                 = none
state backend                          = ScaledUint16RegretStrategy
CPU / threads                          = same desktop / maximum 8
certification                          = current exact BR policy
target                                 = strict dEV < 1%
game/tree/ranges/boards/sizings        = unchanged
```

Non sono stati riaperti predictive CFR, representation, hardware, RAM,
sampling, pruning, bucketing o certification.

## 3. Baseline B e comparator S6

Production B resta exact alternating signed DCFR `1.5/0/2`, delay zero.

| Fixture | Target | dEV | Solver | Limite | Ratio | Esito |
|---|---:|---:|---:|---:|---:|---|
| AHK | @80 | 0,655665% | 0,670928 s | 1,900000 s | 0,353120 | PASS |
| TH | @80 | 0,806385% | 17,645055 s | 19,622222 s | 0,899237 | PASS |
| TST | @202 | 0,991863% | 208,111772 s | 128,988889 s | 1,613409 | FAIL |

TST B separa `174,926380 s` traversal, `32,780800 s` certification e
`0,404592 s` altro accounting.

S6, exact alternating signed DCFR `1.5/0/5`, resta **STRONG RESEARCH
BASELINE**, non production. Passa AHK circa @60 e TH @80; TST misura
`1,35433%` @120 e proietta crossing continuo circa @145 / checkpoint reale
circa @160. Il proxy favorevole @160 è `161,046904 s` (`137,622315 s`
traversal, `23,021581 s` certification, `0,403008 s` altro), ratio `1,24854`.

## 4. Primary-source theory audit

Fonte primaria principale:

- Zhou, Ren, Yan, Li e Zhu, [Lazy-CFR: fast and near-optimal regret
  minimization for extensive games with imperfect information](https://openreview.net/pdf?id=rJx4p3NYDB),
  ICLR 2020; [pagina arXiv](https://arxiv.org/abs/1810.04433).

Per ogni infoset `I` del player `i`, la fonte divide le iterazioni in segmenti
`[t_j(I), t_(j+1)(I)-1]`, mantiene `sigma(I)` fissa nel segmento e accumula:

```text
m_t(I) = sum_(tau=tau_t(I)+1)^t pi^(-i)_(sigma_tau)(I)

r_j(I,a) = sum_(tau=t_j(I))^(t_(j+1)(I)-1)
             pi^(-i)_(sigma_tau)(I)
             u_i(sigma_tau | I -> a, I)
```

La root viene aggiornata ogni round. Quando `I` viene attivato, la strategia
locale viene aggiornata via regret matching; un successivo decision point
`I'` entra nella coda quando `m_t(I') >= B`, con `B > 0` comune. Il reward
vector batched `r_j` sostituisce i reward dei round collassati. La strategia
media resta reach-weighted sui round virtuali, non è la sola media delle
attivazioni.

La garanzia pubblicata è un regret bound per segmentazione arbitraria; per la
regola Lazy-CFR con RM il bound riportato è `O(xi sqrt(D A T))`, contro
`O(xi sqrt(A T))` del CFR nel modello del paper. La complessità di visita è
`O(sum_t |S_t|)`. Non è pruning: i contributi differiti devono essere
ricostruiti e contabilizzati.

Lazy-CFR+ usa la stessa segmentazione e le stesse data structures, sostituendo
RM con RM+ e adottando l'averaging CFR+. Non è signed DCFR e non applica
discount positivo/negativo DCFR.

## 5. Alternating validity

Il paper non definisce una variante chiamata `alternating` e non dimostra
equivalenza con i due full player-pass fresh della production. Algorithm 2
esegue un loop ordinato sui due player dentro un round e aggiorna poi le
reward structures per entrambi; questa è una definizione pubblicata esatta,
ma non è una prova che la trajectory coincida con exact alternating DCFR.

Classificazione:

```text
published exact lazy algorithm                 = YES
published ordered two-player pseudocode        = YES
formal production-alternating equivalence      = NOT ESTABLISHED
formal Lazy-DCFR / Lazy-S6 derivation           = NOT FOUND
```

Questo gap avrebbe richiesto un oracle/differential prima di qualsiasi claim
alternating. Il RAM gate fallisce prima e rende tale prototype non autorizzato.

## 6. Exactness semantics e interaction con DCFR

`Skip` è exact soltanto perché il reward vector e la reach di tutti i round
nel segmento vengono sommati. Ignorare reach piccole, nodi a basso regret o
contributi terminali non è Lazy-CFR ed è fuori scope.

Una composizione informale con B/S6 non è valida. DCFR applica a ogni round
discount dipendenti da `t` e dal segno corrente del regret; dentro un segmento
il segno può cambiare. La somma `r_j` non contiene l'informazione sufficiente
per ricostruire la sequenza di clamp/discount signed. La fonte pubblica
Lazy-CFR, Lazy-CFR+ e Lazy-LCFR, non Lazy-DCFR. Inoltre riporta che Lazy-LCFR
peggiora perché la segmentazione è progettata per pesi uniformi mentre LCFR
favorisce i round tardi. S6 non viene quindi composto numericamente con Lazy.

## 7. Candidate definitions comuni

| ID | Algoritmo | Trigger | Local update | Averaging | Same all fixtures? | Stato |
|---|---|---:|---|---|---|---|
| L1 | published Lazy-CFR | `m(I)>=1,0` | RM | reach/time average | YES | RAM pre-gate |
| L2 | published Lazy-CFR | `m(I)>=0,1` | RM | reach/time average | YES | RAM pre-gate |
| L3 | published Lazy-CFR+ | `m(I)>=1,0` | RM+ | CFR+ average | YES | RAM pre-gate |
| L4 | published Lazy-CFR+ | `m(I)>=0,1` | RM+ | CFR+ average | YES | RAM pre-gate |
| LS6 | Lazy signed DCFR `1.5/0/5` | n/a | signed DCFR | gamma 5 | YES conceptual | THEORY REJECT |

`0,1` e `1,0` sono i due controlli pubblicati, non tuning per fixture. Nessuna
threshold search viene aperta perché L1-L4 condividono lo stesso requisito di
stato che fallisce il cap. `fixture_specific_logic = none` per tutte le righe.

## 8. Required persistent state

La derivazione efficiente pubblicata mantiene, per ciascun player quando
applicabile:

| Quantità | Livello | Persistente? | Funzione |
|---|---|---|---|
| `Gamma(I)` | infoset/action | sì | strategia corrente mantenuta nel segmento |
| `U_i(h)` | history | sì | counterfactual reward corrente del subtree |
| `alpha_i(h)` | history | sì | somma di reach prima dell'ultimo refresh locale |
| `alpha_hat_i(h)` | history | sì | reach accumulata fra boundary parent/child |
| `beta_i(h,a)` | history/action | sì | counterfactual reward differita |
| `flag_i(h)` | history | sì | marca le history da aggiornare nel round |
| `r(I,a)` | infoset/action | transiente all'attivazione | reward vector collassato |
| `m(I)`, `theta_i(h)` | visita/scratch | transiente se derivati dalle DS | trigger e propagazione |
| average strategy | infoset/action | sì | output certificato |

Il backend production contiene soltanto cumulative signed regret e cumulative
average compressi. `Gamma` è oggi derivabile dal regret corrente, ma le
quantità `alpha/alpha_hat/beta/U/flag` non lo sono. La fonte dichiara infatti
space `O(|H|)` per `alpha`, `beta`, `alpha_hat`, contro `O(|I|)` di CFR, e
stima nel proprio flop-hold'em circa 10 TB contro 10 GB. Non propone una
versione memory-neutral.

## 9. Information lower bound

Prima ancora delle DS per-history, una segmentazione indipendente deve sapere
per ogni infoset pending quanto resta prima del crossing `m(I)>=B`. Un bit
active/inactive non basta: due residui distinti sotto `B` possono reagire in
modo diverso al medesimo incremento successivo. Senza una prova di
quantizzazione, l'accumulatore deve almeno conservare la precisione finita
delle reach production. Il modello più favorevole usa quindi **un solo
`float32` per infoset**, pur essendo insufficiente a implementare `r_j`.

Lower bounds accessori:

```text
boolean pending state                 >= 1 bit / infoset (insufficiente)
explicit last-update time through @202 >= ceil(log2(203)) = 8 bit / infoset
trigger residual under current floats  >= 32 bit / infoset (ottimistico)
published alpha/alpha_hat/beta          = history/history-action, molto maggiore
```

Il timestamp non viene sommato al `float32` nel lower bound: può essere
evitato da una rappresentazione con reset implicito. Questo rende il gate
deliberatamente favorevole al candidato.

## 10. RAM model AHK/TH/TST

Cardinali committed e peak autorevoli:

| Fixture | Infoset | Action entry | Current state | Production peak RSS |
|---|---:|---:|---:|---:|
| AHK | 595.626 | 1.288.290 | 5.300.664 B | 166.510.592 B |
| TH | 36.596.832 | 83.318.592 | 334.452.416 B | 798.371.840 B |
| TST | 145.524.152 | 366.890.152 | 1.472.605.376 B | 1.969.922.048 B |

Modello extra, mantenendo irrealisticamente invariato ogni altro overhead:

| Fixture | 1 bit/infoset | 1 B/infoset peak | 2 B/infoset peak | 4 B/infoset extra | 4 B peak | Gate 2 GB |
|---|---:|---:|---:|---:|---:|---|
| AHK | 74.454 B | 167.106.218 B | 167.701.844 B | 2.382.504 B | 168.893.096 B | PASS |
| TH | 4.574.604 B | 834.968.672 B | 871.565.504 B | 146.387.328 B | 944.759.168 B | PASS |
| TST | 18.190.519 B | **2.115.446.200 B** | **2.260.970.352 B** | **582.096.608 B** | **2.552.018.656 B** | **FAIL +552.018.656 B** |

Persino `1 B/infoset`, che non può rappresentare fedelmente `m`, supera il
cap di `115.446.200 B`. Il solo bitset proietterebbe `1.988.112.567 B` e
lascerebbe `11.887.433 B`, ma è matematicamente insufficiente e senza margine
operativo. Il published path per-history/action è strettamente più grande del
lower bound `float32/infoset`.

## 11. Operational RAM margin, sparse e recompute

Il margine nominale B è `30.077.952 B`, pari a circa `0,206687 B/infoset`
(`1,653496 bit/infoset`) TST. Non
può contenere un residuo di trigger indipendente.

Sparse metadata non salva il worst case: tutti i descendant infoset possono
essere simultaneamente sotto threshold e avere contributi pending distinti.
La teoria non fornisce un upper bound sublineare sulla cardinalità pending;
il modello comune deve quindi coprire tutti gli infoset.

Ricostruire `m` richiede riprodurre le reach da `tau(I)+1` al round corrente;
ricostruire `r_j` richiede anche i counterfactual value storici. Senza storage
di strategy history, ciò significa rifare i traversal differiti. Si
risparmierebbero centinaia di MB al prezzo di ripristinare proprio il lavoro
che Lazy vuole eliminare. Disk/mmap conserva i byte e introduce I/O, quindi
non è una soluzione entro il cap desktop operativo.

Una schedule globale a segmenti fissi può evitare metadata per-infoset, ma
mantiene tutte le strategie ferme per `K` round virtuali. Con segmenti uguali,
moltiplicare lo stesso reward per `K` non cambia la policy RM rispetto a un
solo effective update: riduce il contatore di round, non gli effective update
necessari alla convergenza. Non realizza il work reduction adattivo del paper.
Bucket indipendenti richiederebbero di nuovo un accumulatore per bucket e non
sono una variante pubblicata; non vengono inventati.

## 12. RAM kill gate

```text
raw mathematical minimum       = non booleano; precisione illimitata sui reali
frozen-numerical lower bound    = 4 B/infoset
practical published minimum     = alpha/alpha_hat/beta/U/flag su histories
TST projected peak lower bound  = 2.552.018.656 B
desktop cap                     = 2.000.000.000 B
```

Esito:

> **LAZY FAMILY RAM BLOCKER.** L1-L4 falliscono prima di trace, oracle e solve.

Questo non afferma che nessun futuro algoritmo work-reducing possa essere
memory-neutral. Afferma che la famiglia Lazy-CFR/Lazy-CFR+ pubblicata e le sue
segmentazioni per-infoset non entrano nel contratto congelato.

## 13. Work-reduction upper bound ed economic boundary

Il work-elision model candidato non è autorizzato dopo RAM FAIL. Vengono
comunque fissati i boundary economici, senza attribuire a Lazy lavoro che non
è stato misurato:

| Scenario | Traversal | Cert | Altro | Traversal massimo per PASS | Riduzione richiesta |
|---|---:|---:|---:|---:|---:|
| B actual | 174,926380 s | 32,780800 s | 0,404592 s | 95,803497 s | **45,2321%** |
| S6 favorable proxy, solo controfattuale | 137,622315 s | 23,021581 s | 0,403008 s | 105,564300 s | **23,2942%** |

Il secondo scenario non è una candidate: non esiste una derivazione
Lazy-DCFR/S6 sound. Serve solo a quantificare il residuo che un futuro
miglioramento componibile dovrebbe colmare.

L'assoluto e irrealistico `traversal=0` darebbe B `33,185392 s` e S6 proxy
`23,424589 s`; dimostra soltanto che il target non è aritmeticamente
impossibile, non che quella quota sia lazy-eliminabile.

## 14. Producer/traversal work decomposition

Il profilo production già autorevole separa:

| Classe traversal | Share | Credito Lazy autorizzato |
|---|---:|---|
| showdown value production | 27,4% | none; dipende da quali subtrees non entrano in `S_t` |
| value/update | 22,0% | none; le DS Lazy hanno propri update |
| rank/card accumulation | 19,3% | none |
| regret matching | 13,6% | none; soltanto activation-count shadow può stimarlo |
| prefix construction | 9,1% | none |
| chance/board | 8,3% | none |
| reach | 0,3% | none; il trigger richiede reach accounting |
| exact BR certification | esterna al traversal | **0% per contratto congelato** |

La complessità del paper conta history visitate in `S_t`, non una percentuale
statica di kernel. Sommare categorie come “eliminabili” senza un trace reale
sarebbe cherry-picking. Il ceiling realistico resta quindi `NOT MEASURED`.

## 15. Trace/shadow scheduler, quantili e oracle

| Fase | Stato | Motivo |
|---|---|---|
| bounded common trace AHK/TH/TST | NOT RUN | RAM gate non superato |
| offline shadow scheduler | NOT RUN | nessun persistent-state contract valido |
| interval p10/p25/p50/p75/p90/p95/p99/max | NOT AVAILABLE | trace non autorizzato |
| updates-skipped quantiles | NOT AVAILABLE | trace non autorizzato |
| pending-infoset quantiles | NOT AVAILABLE | trace non autorizzato |
| Lazy 2/3-action oracle | NOT RUN | gate order richiede RAM PASS |

Non è stato aggiunto alcun enum, flag, array o dispatch Lazy. Il predictive
oracle esistente è stato preservato.

## 16. Tiny triplet, common curves e B/S6/L comparison

| Misura | B | S6 | L1-L4 |
|---|---|---|---|
| theory | production PASS | research PASS | exact published; alternating mapping gap |
| RAM | PASS | PASS | **FAIL TST** |
| AHK/TH/TST @20 | historical/measured | measured | NOT RUN |
| fixed common curves | measured | measured | NOT RUN |
| repeated `B/L/S6` | n/a | n/a | NOT RUN |
| target-driven | actual | not authorized | NOT AUTHORIZED |

L1-L4 sono dominati al primo gate lessicografico applicabile: non possono
produrre un worst ratio valido entro il cap. LS6 è respinto prima per
soundness della composizione.

## 17. Objective ledger

| Loop | Algorithm | Common params | Theory | RAM | Work ceiling | AHK curve | TH curve | TST curve | Worst ratio | Decision |
|---:|---|---|---|---|---|---|---|---|---:|---|
| 0 | B | signed `1.5/0/2` | PASS | PASS | n/a | target | target | @202 | 1,613409 | production |
| 1 | S6 | signed `1.5/0/5` | PASS | PASS | n/a | measured | measured | 1,35433%@120 | 1,25–1,46 | strong research |
| 2 | L1/L2 | Lazy-CFR `B=1/0,1` | published; alternating gap | **FAIL** | not authorized | n/r | n/r | n/r | n/a | reject pre-solve |
| 3 | L3/L4 | Lazy-CFR+ `B=1/0,1` | published; alternating gap | **FAIL** | not authorized | n/r | n/r | n/r | n/a | reject pre-solve |
| 4 | LS6 | lazy signed DCFR gamma5 | **FAIL: no derivation** | n/e | n/e | n/r | n/r | n/r | n/a | reject |

## 18. Target authorization e target results

```text
reason_target_driven_was_authorized = not_authorized:
  required persistent state exceeds TST cap before trace/oracle/solve
```

Target-driven AHK/TH/TST: `0`. Five-process: `0`. Non vengono inventati dEV,
Root, NashConv, RSS o timing Lazy.

## 19. Production decision

Outcome ufficiale:

# LAZY FAMILY RAM BLOCKER

Production resta un unico common contract:

```text
exact alternating signed DCFR 1.5/0/2
averaging_delay = 0
state = ScaledUint16RegretStrategy
threads = maximum 8
```

S6 `1.5/0/5` resta STRONG RESEARCH BASELINE e non viene promossa. Non esiste
dispatch per fixture né codice Lazy dormant.

## 20. Benchmark cost accounting

| Voce | Conteggio |
|---|---:|
| solver run | 0 |
| TST solver run | 0 |
| trace/shadow run | 0 |
| Lazy oracle run | 0 |
| target-driven run | 0 |
| repeated process | 0 |
| full CTest | 0 (nessun compiled source modificato) |

Il budget solver è stato protetto dal RAM kill gate. Le uniche operazioni sono
state verification read-only, audit delle fonti e calcoli deterministici del
lower bound.

## 21. Limiti della conclusione

- Il lower bound `float32/infoset` non implementa da solo Lazy-CFR: è
  intenzionalmente molto più piccolo della DS pubblicata.
- Non è stata misurata la distribuzione degli intervalli Lazy perché il trace
  era successivo al RAM PASS.
- Non viene dichiarata impossibilità universale di ogni algoritmo futuro con
  deferred work.
- Il gap di equivalenza production-alternating resta secondario al RAM FAIL,
  non viene trasformato in una falsa prova negativa.

## 22. Next exact family or blocker

Non è autorizzata automaticamente un'altra famiglia exact. Il prossimo passo
ad alta priorità è un **nuovo governance gate**: o si riapre esplicitamente il
RAM/state contract, oppure si ammette allo studio soltanto una famiglia
work-reducing con prova primaria di `O(1)`/bounded extra persistent state sul
layout corrente. Non riaprire predictive, Lazy threshold tuning o codec già
falsificati per aggirare questo blocker.
