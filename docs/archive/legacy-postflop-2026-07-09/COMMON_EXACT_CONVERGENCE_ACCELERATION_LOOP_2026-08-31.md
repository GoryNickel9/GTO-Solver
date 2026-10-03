# Common Exact Convergence Acceleration Objective Loop

Data: 2026-08-31
Decisione: **COMMON EXACT SCHEDULE SPACE EXHAUSTED — nessuna promozione production**

## 1. Checkout iniziale e decisione di governance

- branch: `main`;
- HEAD iniziale e finale del solver misurato:
  `e2c6a2a4507c7256e82044f414566a0f14782394`;
- commit: `e2c6a2a docs(perf): record constraint governance gate`;
- dopo `git fetch origin main`: `main...origin/main = 0/0` e
  `origin/main = e2c6a2a4507c7256e82044f414566a0f14782394`;
- nessun reset e nessun push;
- `.reasonix/`, `.tmp/`, il corpus real-node e gli output precedenti sono stati
  preservati.

Il worktree iniziale non era pulito. Le tre fixture production avevano già una
modifica non attribuita a questo loop: `dcfr_average_exponent` da `2.0` a
`3.0`. Gli artefatti di ricerca sono stati generati in `.tmp` sovrascrivendo
esplicitamente il contratto richiesto in copie temporanee; le modifiche
preesistenti non sono state sovrascritte né usate come prova di promozione.

La task riapre esplicitamente il vincolo `C1`, severity `S2`: algoritmo e
schedule non sono più immutabili. Restano congelati hardware, RAM, precisione,
gioco/albero, fixture, target, riferimenti GTO+, certificazione production e
ogni semantica esterna.

## 2. Invariante common contract

Ogni run nuovo è stato prodotto da
`tools/run_common_schedule_probe.ps1`. Il runner costruisce un solo oggetto
contratto e lo applica alle fixture AHKHQH, TH7D6S e TSTC9D in sequenza:

```text
state_precision        = scaled_uint16_regret_strategy
maximum_solver_threads = 8
averaging_delay        = 0
certification_interval = 20
fixture_specific_logic = none
```

Algorithm, alpha, beta, gamma e limite diagnostico sono parametri unici della
singola invocazione. Non esiste dispatch per benchmark ID, board, range,
dimensione dell'albero, infoset, fingerprint, dEV o wall time. Ogni manifest
in `.tmp/common-convergence-loop/<candidate>/manifest.json` registra lo stesso
contratto prima dei tre risultati.

La ricerca usa fixed iterations soltanto come diagnostica. La policy
production resta target-driven con strict `dEV < 1%` e certificazione exact BR
ogni 20 iterazioni.

## 3. Baseline production autorevole

Contratto: exact alternating signed DCFR `1.5/0/2`, delay zero,
`ScaledUint16RegretStrategy`, massimo 8 thread.

| Fixture | Target iter | dEV | Root | Solver | Limite | Ratio | Esito |
|---|---:|---:|---:|---:|---:|---:|---|
| AHKHQH | 80 | 0,655665% | 19,108984 | 0,670928 s | 1,900000 s | 0,353120 | PASS |
| TH7D6S | 80 | 0,806385% | 8,221632 | 17,645055 s | 19,622222 s | 0,899237 | PASS |
| TSTC9D | 202 | 0,991863% | 8,494698 | 208,111772 s | 128,988889 s | 1,613409 | FAIL |

TST comprende `174,926380 s` di traversal e `32,780800 s` di
certificazione. Payoff sum, normalization, layout, exact outcomes, Root e
solver-state passano. Il worst ratio autorevole è `1,613409`.

## 4. Audit storico obbligatorio

I run non omogenei sono etichettati e non vengono combinati come una
certificazione. `Scaled` nella tabella significa
`ScaledUint16RegretStrategy`; `final-only` indica un solo exact BR finale.

| Schedule | Fixture | Iter | dEV | Traversal | s/iter | Certificazione | State | Rilevanza storica |
|---|---|---:|---:|---:|---:|---|---|---|
| production `1.5/0/2` | TST | 202 | 0,991863% | 174,926380 | 0,865972 | current, ogni 20, 32,780800 s | Scaled | E4 autorevole |
| storico `1.9/0/3` | TST | 160 | 1,078426% | 130,372985 | 0,814831 | final-only, 5,439364 s | Scaled | E3 singolo, non production |
| storico `1.9/0/3` | TST | 170 | 0,985760% | 147,063172 | 0,865077 | final-only, 5,505125 s | Scaled | primo target storico, tuning TST-specifico |
| `1.9/1/3` | TST | 135 | 4,075340% | 103,669488 | 0,767922 | final-only, 5,265509 s | Scaled | beta positivo molto negativo |
| `1.9/0/5` | TST | 135 | 1,263294% | 116,240892 | 0,861044 | final-only, 5,407843 s | Scaled | miglior gamma storico @135 |
| `1.9/0/2` | TST | 135 | 1,532120% | 118,777602 | 0,879834 | final-only, 5,829853 s | Scaled | dominato da gamma 5 |
| DCFR+ `1.5/0/2` | TST | 135 | 1,541924% | 168,504169 | 1,248179 | final-only, 5,754863 s | Scaled | troppo lento |
| HS-DCFR(30) | TST | 135 | 2,265369% | 118,920330 | 0,880891 | final-only, 5,366816 s | Scaled | dynamic exact control insufficiente |
| `1.9/0.5/3` | TST | 120 | 1,520192% | 126,020471 | 1,050171 | final-only, 6,368396 s | Scaled | beta sensitivity control |

Per le schedule storiche non-production non esistono target run AHK/TH
comparabili sullo stesso final HEAD. Per questo sono state usate soltanto per
generare/falsificare ipotesi, mai per promuovere `1.9/0/3`.

## 5. Boundary di convergenza

Con il costo e la cadence correnti TST deve chiudere circa entro @120; il
boundary continuo è circa @125. Anche usando il proxy final-only storico il
limite sale soltanto a circa @142, ma C4 non è aperto in questo loop.

La ricerca ha quindi richiesto una curva credibile verso `dEV < 1%` entro
@120–140. Una schedule che proietta il primo checkpoint a @160 non è
autorizzata al target-driven, anche se migliora il baseline.

## 6. Modello statico e generazione candidati

Per signed DCFR statico, all'iterazione `t`:

```text
positive multiplier = (t-1)^alpha / ((t-1)^alpha + 1)
negative multiplier = (t-1)^beta  / ((t-1)^beta  + 1)
average weight       = (t-delay)^gamma, per t > delay
```

Con `beta=0`, il negative multiplier è sempre `0,5`. Alpha più alto conserva
più regret positivo early/mid; gamma più alto concentra la strategia media
sulle iterazioni recenti. Il rischio numerico principale è una media troppo
densa/recente; tutti i valori provati restano finiti e bounded nel dominio
misurato.

Metodo adattivo, non brute force:

1. seed da storia `1.9/0/3`, gamma 5 e baseline;
2. tiny triplet @20;
3. rimozione del punto interpolato non informativo `1.7/0/3`;
4. curve @120 per baseline, gamma/alpha storicamente supportati;
5. aggiunta mirata di gamma 4 e 5 con alpha 1.5 dopo il fit della prima curva;
6. controlli beta e dynamic/exact dalle famiglie già implementate;
7. A/B ripetuto sul migliore; nessun target-driven privo dei prerequisiti.

## 7. Parameter history

| ID | Algorithm | alpha | beta | gamma | delay | Dynamic? | Same all fixtures? |
|---|---|---:|---:|---:|---:|---|---|
| B | signed DCFR | 1,5 | 0 | 2 | 0 | NO | YES |
| S1 | signed DCFR | 1,5 | 0 | 3 | 0 | NO | YES |
| S2 | signed DCFR | 1,7 | 0 | 3 | 0 | NO | YES |
| S3 | signed DCFR | 1,9 | 0 | 3 | 0 | NO | YES |
| S4 | signed DCFR | 1,9 | 0 | 5 | 0 | NO | YES |
| S5 | signed DCFR | 1,5 | 0 | 4 | 0 | NO | YES |
| S6 | signed DCFR | 1,5 | 0 | 5 | 0 | NO | YES |
| D1 | HS-DCFR(30) | formula | formula | formula | 0 | YES | YES |
| X1 | DCFR+ | 1,5 | projection non-negative | 2 | 0 | NO | YES |

## 8. Tiny common probe

| Candidate | AHK dEV @20 | TH dEV @20 | TST dEV @20 | Decisione |
|---|---:|---:|---:|---|
| B `1.5/0/2` | 8,24777% | 11,6669% | 15,4471% | baseline |
| S1 `1.5/0/3` | 8,33522% | 11,0646% | 15,7520% | curve |
| S2 `1.7/0/3` | 7,600% | 11,0959% | 16,4016% | stop: nessun vantaggio informativo vs S1/S3 |
| S3 `1.9/0/3` | 7,610% | 11,2035% | 16,8172% | curve per supporto storico |
| S4 `1.9/0/5` | 7,680% | 10,9497% | 18,4747% | curve per supporto storico |
| D1 HS-DCFR(30) | 13,8694% | 25,4866% | 36,5141% | reject |
| X1 DCFR+ | 6,02741% | 11,8786% | 15,1766% | reject per throughput/storia lunga |

Tutti i report hanno `layout_matches_fixture=true`, `exact_outcomes=true`,
normalization error zero e payoff sum finita vicino a zero. Root e NashConv
sono finiti. Un `correctness=fail` a @20 indica target production non ancora
raggiunto, non un errore di esecuzione.

## 9. Common convergence curves

### AHKHQH dEV (%)

| Contract | @20 | @40 | @60 | @80 | @100 | @120 |
|---|---:|---:|---:|---:|---:|---:|
| B `1.5/0/2` | 8,248 | 2,228 | 0,923 | 0,656 | 0,509 | 0,429 |
| S1 `1.5/0/3` | 8,335 | 1,977 | 0,839 | 0,636 | 0,481 | 0,401 |
| S3 `1.9/0/3` | 7,610 | 1,981 | 1,074 | 0,769 | 0,616 | 0,517 |
| S4 `1.9/0/5` | 7,680 | 2,184 | 1,236 | 0,830 | 0,640 | 0,531 |
| S5 `1.5/0/4` | 8,447 | 2,010 | 0,847 | 0,651 | 0,477 | 0,391 |
| S6 `1.5/0/5` | 8,587 | 2,111 | 0,883 | 0,660 | 0,488 | 0,388 |

### TH7D6S dEV (%)

| Contract | @20 | @40 | @60 | @80 | @100 | @120 |
|---|---:|---:|---:|---:|---:|---:|
| B `1.5/0/2` | 11,667 | 3,061 | 1,328 | 0,806 | 0,593 | 0,506 |
| S1 `1.5/0/3` | 11,065 | 2,604 | 1,194 | 0,728 | 0,553 | 0,473 |
| S3 `1.9/0/3` | 11,204 | 3,249 | 1,701 | 1,098 | 0,777 | 0,592 |
| S4 `1.9/0/5` | 10,950 | 3,200 | 1,721 | 1,102 | 0,775 | 0,581 |
| S5 `1.5/0/4` | 10,932 | 2,412 | 1,157 | 0,716 | 0,560 | 0,468 |
| S6 `1.5/0/5` | 11,018 | 2,338 | 1,174 | 0,713 | 0,576 | 0,472 |

### TSTC9D dEV (%)

| Contract | @20 | @40 | @60 | @80 | @100 | @120 |
|---|---:|---:|---:|---:|---:|---:|
| B `1.5/0/2` | 15,447 | 7,438 | 4,739 | 3,191 | 2,220 | 1,640 |
| S1 `1.5/0/3` | 15,752 | 7,498 | 4,607 | 2,979 | 2,011 | 1,463 |
| S3 `1.9/0/3` | 16,817 | 7,677 | 4,962 | 3,266 | 2,258 | 1,690 |
| S4 `1.9/0/5` | 18,475 | 8,020 | 4,952 | 3,129 | 2,110 | 1,565 |
| S5 `1.5/0/4` | 16,511 | 7,659 | 4,572 | 2,868 | 1,903 | 1,386 |
| S6 `1.5/0/5` | 17,465 | 7,848 | 4,591 | 2,818 | 1,859 | **1,354** |

S6 è il migliore late-static TST e passa AHK/TH agli stessi checkpoint utili:
AHK @60 `0,883%`, Root finale @120 `19,121604`; TH @80 `0,713%`, Root finale
@120 `8,228065`. TST Root @120 è `8,486925`. I payoff sum finali sono
rispettivamente `-1,78e-15`, `2,22e-16`, `9,99e-16`; normalization error zero.

## 10. Timing curves e density cost

Tempi solver cumulativi diagnostici S6:

| Fixture | @20 | @40 | @60 | @80 | @100 | @120 |
|---|---:|---:|---:|---:|---:|---:|
| AHK | 0,23 | 0,53 | 0,84 | 1,09 | 1,31 | 1,54 |
| TH | 4,67 | 9,57 | 14,37 | 19,34 | 24,21 | 29,12 |
| TST | 22,11 | 45,99 | 70,12 | 94,45 | 118,14 | 141,55 |

Per TST S6 @120: traversal `111,546421 s`, certification cumulativa
`29,573465 s`. Non è stata usata la proiezione lineare da @20: sono stati
usati i costi cumulativi reali e il costo final-head ai checkpoint successivi.

Dai punti TST @100/@120, un fit power-law locale colloca `dEV=1%` circa a
@145 continuo; con cadence 20 il primo checkpoint plausibile è @160. La curva
final-head baseline misura già @160 `161,046904 s` (`137,622315` traversal,
`23,021581` certification). Poiché alpha e il traversal signed sono invariati
e l'A/B corto non rileva regressione sistematica, questo è il proxy più
favorevole per S6: `161,046904 / 128,988889 = 1,24854`. La sequenza S6 sotto
carico proietta invece circa 188 s a @160. L'intervallo previsto è quindi
`1,25–1,46`, incertezza media/alta, e fallisce in ogni caso il gate.

## 11. Dynamic schedule audit

La dynamic schedule credibile già presente è HS-DCFR(30):

```text
alpha(t) = min(5, 1 + 0.003 t)
beta(t)  = max(-5, -1 - 0.002 t)
gamma(t) = max(5, 30 - 0.005 t)
```

Il positive multiplier è `(t-1)^alpha(t)/(1+(t-1)^alpha(t))`, il negative
multiplier usa la stessa formula con `beta(t)`, e la media precedente viene
scontata di `((t-1)/t)^gamma(t)` prima di aggiungere la strategia corrente.
La funzione è deterministic, bounded, fixture-independent e testata a livello
API. Il tiny triplet la respinge su tutte le fixture; il controllo lungo TST
@135 (`2,265369%`, `124,762 s`) conferma che non può raggiungere il boundary.

Non è stata introdotta una nuova dynamic schedule ad hoc. I gamma statici 3–5
mostrano una frontier monotona late ma insufficiente; un ramp deterministico
fra gli stessi estremi resta nel loro envelope di recency weighting e non ha
evidenza quantitativa per colmare il residuo da `1,354%` a `<1%` entro @120.
Implementarlo avrebbe aggiunto complexity senza superare il Level 0 model.

## 12. Beta sensitivity e controlli di famiglia

`beta=0.5` a TST@120 produce `1,520192%` e `1,050171 s/iter`, peggio di S6
sia in convergenza sia in throughput. `beta=1` a @135 produce `4,075340%`.
La direzione positiva è quindi falsificata con due intensità; non è stata
aperta una griglia beta.

DCFR+ ha dEV TST@20 leggermente migliore del baseline (`15,1766%`) ma solver
`31,0269 s` e storico @135 `1,541924% / 174,729 s`. HS-DCFR è peggiore early
e long. Entrambe sono dominate per worst time-to-target.

## 13. Pareto frontier e score previsto

| Contract | AHK target | TH target | TST target previsto | Worst ratio previsto | Complexity | Decisione |
|---|---:|---:|---:|---:|---|---|
| B `1.5/0/2` | 0,670928 s @80 | 17,645055 s @80 | 208,111772 s @202 | 1,613409 actual | static | baseline |
| S1 `1.5/0/3` | <1% @60 | <1% @80 | circa @160, timing sequenziale sfavorevole | >1,25 | static | dominated late da S5/S6 |
| S5 `1.5/0/4` | 0,85 s @60 | 19,27 s @80 | circa @160 | ~1,25–1,47 | static | frontier |
| S6 `1.5/0/5` | 0,84 s @60 | 19,34 s @80 | circa @160 | **~1,25–1,46** | static | best studied, REJECT |

S6 riduce le iterazioni TST previste da 202 a circa 160 (`20,8%`), ma non
chiude il limite: anche il proxy più favorevole lascia circa `32,06 s` e
`0,2485x` di residuo. TH è inoltre vicino al proprio limite nel run
diagnostico. Non esiste un secondo miglioramento common exact, indipendente e
componibile dimostrato; pertanto S6 non soddisfa `STRONG PROMOTE` e non cambia
production.

## 14. A/B ripetuto

Ordine: B/C, C/B, B/C; processi distinti, triplet completi, @20.

| Fixture | B solver (min/med/max) | S6 solver (min/med/max) | B traversal med | S6 traversal med |
|---|---:|---:|---:|---:|
| AHK | 0,22 / 0,23 / 0,26 | 0,26 / 0,27 / 0,32 | 0,19 | 0,22 |
| TH | 4,80 / 5,26 / 5,73 | 4,65 / 4,88 / 5,35 | 4,11 | 3,77 |
| TST | 20,43 / 22,22 / 26,69 | 21,38 / 21,77 / 26,12 | 17,12 | 16,79 |

La variabilità di sistema è visibile, ma S6 non ha una regressione throughput
incompatibile. La dEV deterministica @20 resta peggiore su AHK/TST e migliore
su TH; il vantaggio S6 emerge soltanto late, come nelle curve @120.

## 15. Target-driven authorization ledger

| Candidate | Common PASS | AHK projected PASS | TH projected PASS | TST verso @120–140 | Numerical PASS | Repeated timing | Autorizzato? | reason_target_driven_was_authorized |
|---|---|---|---|---|---|---|---|---|
| S6 `1.5/0/5` | YES | YES | YES, margine ridotto | NO: ~@145 continuo/@160 cadence | YES | non regressivo | **NO** | `not_authorized: TST boundary missed` |

Target-driven eseguiti: **0**. Five-process eseguiti: **0**, correttamente
bloccati perché non esiste un single TST PASS. Non sono stati spesi full TST
per confermare una proiezione già rossa.

## 16. Loop ledger

| Loop | Common schedule | Rationale | AHK curve | TH curve | TST curve | Worst ratio previsto | Decisione | Next hypothesis |
|---:|---|---|---|---|---|---:|---|---|
| 0 | B `1.5/0/2` | authority | target @80 | target @80 | target @202 | 1,613409 actual | baseline | alpha/gamma history |
| 1 | S1/S2/S3/S4 @20 | tiny common falsification | finite | finite | finite | non stimato | S2 stop; altri curve | gamma late |
| 2 | B/S1/S3/S4 @120 | common curves | tutti <1 @120 | tutti <1 @120 | best 1,463 S1 | >1,25 | nessun finalist | alpha 1.5, gamma 4/5 |
| 3 | S5/S6 @120 | storico gamma 4/5 | <1 @60 | <1 @80 | 1,386 / 1,354 | 1,25–1,46 | S6 frontier | beta/dynamic controls |
| 4 | beta 0.5/1 storico | sensitivity | n/a | n/a | 1,520 @120 / 4,075 @135 | dominato | reject | exact families |
| 5 | HS-DCFR/DCFR+ | dynamic/family controls | tiny measured | tiny measured | storico @135 rosso | dominato | reject | A/B S6 |
| 6 | B/S6 B/C C/B B/C | timing confirmation | noisy | S6 non regressivo | S6 non regressivo | 1,25–1,46 | no target-driven | close loop |

## 17. Objective function e decisione production

Per tutti i candidati misurati:

```text
mathematical_contract_fail_count = 0
common_contract_violation_count  = 0
```

I `correctness_gate_fail` dei fixed probe sono target/root checkpoint non
ancora maturi, non NaN, Inf, normalization, payoff o layout failure. Nessun
candidato riduce `worst_time_to_target_ratio` a `<=1`. S6 è il minimo studiato
ma fallisce il primo obiettivo tempo dopo i gate matematici.

Decisione:

> **COMMON EXACT SCHEDULE SPACE EXHAUSTED** per il vicinato statico
> alpha/gamma credibile, la sensitivity beta positiva, la dynamic schedule
> HS-DCFR pubblicata e i controlli DCFR+. Non è una prova di impossibilità per
> ogni algoritmo exact futuro.

Production resta globalmente exact alternating signed DCFR `1.5/0/2`, delay
zero, ScaledUint16RegretStrategy e massimo 8 thread. Non viene introdotto un
default gamma 5 per TST né un doppio contratto. Le modifiche gamma 3
preesistenti alle fixture non sono promosse da questo report.

## 18. Benchmark cost accounting

| Voce | Conteggio/tempo |
|---|---:|
| solver report prodotti | 58 |
| TST solver run | 19 |
| target-driven run | 0 |
| full CTest run | 0 |
| somma timer solver | 1.415,581 s |
| somma wall inclusa tree preparation | 2.382,104 s (39,70 min) |

Il conteggio include il primo AHK probe che produsse report ma restituì exit 4
per target non raggiunto; il runner è stato corretto per accettare exit 4 solo
in fixed diagnostic con report presente. Nessun compiled source è cambiato,
quindi la policy non richiede un full CTest finale. La validazione finale usa
report grezzi, manifest, esecuzioni del runner e `git diff --check`.

Validazione finale eseguita:

```text
PowerShell parse tools/run_common_schedule_probe.ps1 = PASS
19 manifest completi / 58 report: common state/thread/delay/fixture-specific invariant = PASS
cmake -P tests/verify_production_dcfr_contract.cmake su fixture estratte da HEAD = PASS
git diff --check = PASS (soli warning EOL sulle tre modifiche preesistenti)
full CTest = NOT RUN (nessun compiled source modificato)
```

Il contract test è stato eseguito sulle fixture del commit autorevole perché le
copie nel worktree erano già a gamma 3 all'ingresso. Questo separa la verifica
del contratto production committed dalla preservazione delle modifiche utente.

## 19. Limiti

- I timing delle curve lunghe sono single-process e mostrano load drift; sono
  diagnostici, non median/p95 production.
- Le proiezioni S6 @160 hanno incertezza media/alta e non vengono presentate
  come target result.
- Non sono state modificate certification policy, state representation,
  hardware, RAM, fixture semantics o reference target.
- L'esaurimento riguarda le famiglie exact studiate e credibili, non ogni
  possibile algoritmo futuro.

## 20. Passo successivo

**Mantenere `1.5/0/2` production e aprire una nuova task soltanto per una
famiglia exact con razionale teorico indipendente capace di spostare il primo
checkpoint TST da @160 a circa @120; non riaprire micro-sweep alpha/gamma o C4
da sola, perché neppure il proxy final-only compone il residuo S6 fino al
gate.**
