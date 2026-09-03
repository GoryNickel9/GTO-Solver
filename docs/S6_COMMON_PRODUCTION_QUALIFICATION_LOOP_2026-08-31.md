# S6 Common Production Qualification Objective Loop — 2026-08-31

> **CORREZIONE SEMANTICA 2026-09-04 — REPORT STORICO.** Il REJECT temporale S6
> resta valido perché nasce dagli early gate di tempo/iterazioni. Le diciture
> RAM PASS e cap desktop non sono più normative. Vedere il
> [`piano di correzione`](GTO_PLUS_SOLVER_MEMORY_SEMANTICS_AND_GATE_CORRECTION_PLAN_2026-09-03.md).

## Analisi

### Obiettivo e decisione

Questa qualification confronta una baseline B fresca e S6 sulla stessa build
Release, sulla stessa macchina e con un unico contratto comune per AHKHQH,
TH7D6S e TSTC9D.

**Outcome unico: D. REJECT.**

S6 conserva correctness, Root, payoff-sum, normalizzazione, exact BR, layout,
stato e RAM. AHK e TH restano sotto i limiti espliciti della task. Tuttavia,
nella prima coppia TST S6 migliora il solver time soltanto dell'`8,018810%` e
le target iterations soltanto dello `0,990099%`. Entrambi i valori sono sotto
il minimo early-stop del `10%`; il traversal migliora soltanto del `6,441213%`.
Il protocollo impone quindi REJECT immediato e vieta Pair 2/3.

Production resta exact alternating signed DCFR `1.5/0/2`. Nessun default,
fixture, contract test, checkpoint o percorso solver è stato modificato.

### Stato iniziale della repository

- repository principale: `C:\Users\GoryNickel\Documents\GitHub\GTO-Solver`;
- branch principale: `main`;
- HEAD locale iniziale: `77677b2f0be711b588bebdbd2256fe8a28f4b863`;
- `origin/main` dopo fetch: `77677b2f0be711b588bebdbd2256fe8a28f4b863`;
- ahead/behind iniziale: `0/0`;
- tracked modifications: le sole tre fixture AHKHQH, TH7D6S e TSTC9D con
  modifica utente `dcfr_average_exponent: 2.0 -> 3.0`;
- untracked preservati: `.reasonix/`, `.tmp/`;
- staged changes: nessuno.

Hash SHA-256 iniziali delle fixture utente, usati come guardia di
byte-invarianza:

| Fixture | SHA-256 |
|---|---|
| AHKHQH | `A7CB748226A7D0E421C87361111AA7813DA8E90F43F0B3725788AB98FD6DF6DC` |
| TH7D6S | `8C475ED4B487E10D6DE703A0DA8BD74FBDC7A0C2B7DA95BFE7A1E6512419F9B7` |
| TSTC9D | `D0371E10FF926BEEE4012DEB9BA0A4E96A0D3958846B035EE417FE5CEAE0E833` |

Il fetch ha inizialmente incontrato un diniego sandbox su `.git/FETCH_HEAD`;
la ripetizione autorizzata è riuscita. Non sono stati usati reset, checkout,
stash o modifiche sul working tree principale.

### Worktree pulito e hardware

La qualification è avvenuta nel worktree esterno:

```text
C:\tmp\gto-solver-s6-production-qualification-20260831
branch = research/s6-production-qualification-20260831
base   = 77677b2f0be711b588bebdbd2256fe8a28f4b863
```

Il runner generalizzabile è stato isolato nel commit tooling `7322812` prima
dei run. I manifest dei benchmark riportano quindi source commit
`732281237ec410148d6a6f94d7e87e7696dc215a`; rispetto alla base, quel commit
aggiunge soltanto il runner e lascia le fixture committed a B.

Macchina comune B/S6 e macchina di provenienza dei riferimenti GTO+:

- CPU: Intel Core i3-10100F @ 3.60 GHz;
- logical processors: 8;
- RAM fisica: `34.294.738.944 B`;
- OS: Windows NT `10.0.26200.0`;
- CPU-only, massimo otto solver thread.

### Contratti congelati

| Campo | B | S6 |
|---|---:|---:|
| algorithm | exact alternating signed DCFR | exact alternating signed DCFR |
| alpha | 1,5 | 1,5 |
| beta | 0 | 0 |
| gamma | 2 | 5 |
| averaging delay | 0 | 0 |
| state | `ScaledUint16RegretStrategy` | `ScaledUint16RegretStrategy` |
| threads | max 8 | max 8 |
| certification interval | 20 | 20 |
| strict target | dEV `<1%` | dEV `<1%` |
| best response | exact | exact |

Ogni manifest riporta:

```text
fixture_specific_logic = none
same_contract_all_fixtures = true
```

Gioco, tree, board, range, sizing, stack, pot, rake, terminal outcomes,
canonical DAG, isomorfismi, Root references/tolerances, payoff-sum e
normalization tolerance, GTO+ references e layout/fingerprint sono rimasti
congelati. Il solo delta B/S6 è `gamma: 2 -> 5`.

## Piano

Il loop applicato è stato:

```text
CLEAN WORKTREE
-> PRE-FLIGHT CONTRACT
-> BUILD RELEASE
-> FRESH B/S6 TARGET PAIR
-> EARLY STOP CHECK
-> REJECT
-> DOCUMENT
```

Il budget successivo `Pair 2: S6 -> B`, `Pair 3: B -> S6` era autorizzato
soltanto se la prima coppia avesse superato tutti gli early gate. Non è stato
consumato perché TST ha mancato entrambi i minimi del 10%.

## Implementazione

### Runner comune target-driven

È stato aggiunto `tools/run_common_schedule_target.ps1` con queste proprietà:

- un solo contratto per le tre fixture;
- ordine fisso `AHK -> TH -> TST`;
- fixture materializzate con `git show <HEAD>:<fixture>`, non lette dal main
  dirty;
- strict target-driven; nessuna variabile fixed-iteration;
- exact BR, exact outcomes, no sampling, alternating update;
- output per-fixture JSON e log separati;
- manifest aggregato `gtosd.common_schedule_target.v1`;
- verifica Release, parametri, precisione, thread, interval, dEV, NashConv e
  target mode;
- exit nonzero per report assente o strutturalmente invalido;
- artifact confinati a `.tmp/s6-production-qualification/`.

Commit tooling:

```text
732281237ec410148d6a6f94d7e87e7696dc215a
perf(postflop): add common target qualification runner
```

### Build e ordine benchmark

La build `windows-release` è stata configurata e compilata con MSVC
`19.51.36248.0`, Ninja, C++20, `GTOSD_WARNINGS_AS_ERRORS=ON` (`/W4 /WX`) e
massimo otto job. Nessun test o build è stato eseguito in parallelo ai
benchmark.

Ordine effettivo:

```text
Pair 1: B(AHK, TH, TST) -> S6(AHK, TH, TST)
```

Artifact:

```text
.tmp/s6-production-qualification/pair1-b/
.tmp/s6-production-qualification/pair1-s6/
```

## Validazione

### Regole di contaminazione

Nessun run è stato escluso e nessuna replacement replica è stata eseguita.
Non sono stati osservati processi concorrenti, thermal/power event o failure
tecnici. Il B fresco è più lento dello storico e sul TST mostra CPU utilization
inferiore, ma ciò non è prova concreta di contaminazione e quindi non autorizza
lo scarto.

I dati interni sono coerenti:

- tree preparation B/S6: AHK `5,377501/5,439878 s`, TH
  `15,847791/15,323636 s`, TST `24,570736/24,328916 s`;
- TST CPU utilization B/S6: `0,767321/0,809280`; S6 ha quindi condizioni CPU
  osservate più favorevoli, non peggiori;
- peak RSS, state bytes, fingerprint, node/action counts sono stabili;
- nessun wall/solver discrepancy incompatibile con la tree preparation.

La Pair 1 è pertanto valida. Il risultato sfavorevole a S6 non viene
reinterpretato o scartato.

### Risultati target-driven freschi

Timer `solver` è il campo comparabile autorevole `elapsed_seconds` / phase
`run_solver`. `Other = solver - traversal - certification`.

| Candidate | Fixture | Iter | dEV | Root EV / delta | Traversal | Certification | Other | Solver | Process wall | CPU util. | Peak RSS |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| B | AHK | 80 | 0,655665% | 19,108984 / -0,041016 PASS | 0,661609 | 0,136040 | 0,014425 | 0,812074 | 6,189575 | 0,769634 | 167.825.408 B |
| B | TH | 80 | 0,806385% | 8,221632 / -0,000348 PASS | 16,284081 | 2,116555 | 0,376672 | 18,777307 | 34,625098 | 0,860622 | 799.891.456 B |
| B | TST | 202 | 0,991863% | 8,494698 / -0,006952 PASS | 202,163742 | 35,827109 | 0,440049 | 238,430899 | 263,001635 | 0,767321 | 1.971.363.840 B |
| S6 | AHK | 80 | 0,662003% | 19,113924 / -0,036076 PASS | 0,579319 | 0,122491 | 0,018911 | 0,720721 | 6,160599 | 0,834668 | 167.964.672 B |
| S6 | TH | 80 | 0,714763% | 8,226663 / +0,004683 PASS | 16,903773 | 2,155221 | 0,381973 | 19,440967 | 34,764603 | 0,823908 | 799.883.264 B |
| S6 | TST | 200 | 0,874169% | 8,497690 / -0,003960 PASS | 189,141945 | 29,715274 | 0,454361 | 219,311579 | 243,640495 | 0,809280 | 1.970.999.296 B |

Tutti e sei i report passano:

- strict dEV `<1%`;
- Root reference/tolerance;
- payoff-sum `abs <=1e-11`;
- normalization `<=1e-11`;
- finite NashConv;
- exact outcomes ed exact BR;
- layout/fingerprint;
- solver state;
- desktop peak RSS `<2.000.000.000 B`.

NashConv finali B/S6:

| Fixture | B | S6 |
|---|---:|---:|
| AHK | 0,0119317764 | 0,0113685005 |
| TH | 0,0153423221 | 0,0125627210 |
| TST | 0,0167569583 | 0,0144624441 |

Payoff-sum finali B/S6:

| Fixture | B | S6 |
|---|---:|---:|
| AHK | 0 | -3,774758e-15 |
| TH | -4,440892e-16 | -2,220446e-16 |
| TST | 3,219647e-15 | -1,221245e-15 |

### Min/median/max

L'early stop obbligatorio lascia `n=1` run valido per candidato. Pertanto
minimo, mediana e massimo coincidono; nessuna varianza viene inventata.

| Candidate | Fixture | Iter min/med/max | Traversal min/med/max | Certification min/med/max | Solver min/med/max | Peak RSS min/med/max |
|---|---|---|---|---|---|---|
| B | AHK | 80/80/80 | 0,661609/0,661609/0,661609 | 0,136040/0,136040/0,136040 | 0,812074/0,812074/0,812074 | 167.825.408/167.825.408/167.825.408 B |
| B | TH | 80/80/80 | 16,284081/16,284081/16,284081 | 2,116555/2,116555/2,116555 | 18,777307/18,777307/18,777307 | 799.891.456/799.891.456/799.891.456 B |
| B | TST | 202/202/202 | 202,163742/202,163742/202,163742 | 35,827109/35,827109/35,827109 | 238,430899/238,430899/238,430899 | 1.971.363.840/1.971.363.840/1.971.363.840 B |
| S6 | AHK | 80/80/80 | 0,579319/0,579319/0,579319 | 0,122491/0,122491/0,122491 | 0,720721/0,720721/0,720721 | 167.964.672/167.964.672/167.964.672 B |
| S6 | TH | 80/80/80 | 16,903773/16,903773/16,903773 | 2,155221/2,155221/2,155221 | 19,440967/19,440967/19,440967 | 799.883.264/799.883.264/799.883.264 B |
| S6 | TST | 200/200/200 | 189,141945/189,141945/189,141945 | 29,715274/29,715274/29,715274 | 219,311579/219,311579/219,311579 | 1.970.999.296/1.970.999.296/1.970.999.296 B |

### Delta, ratio e RAM/state

I limiti task sono AHK `1,900000 s`, TH `19,622222 s`, TST
`128,988889 s`. Il ratio aggregato è la media aritmetica dei tre ratio
fixture/limit, coerente con l'autorità precedente.

| Metrica | B fresco | S6 | Delta relativo S6 vs B |
|---|---:|---:|---:|
| TST solver | 238,430899 s | 219,311579 s | -8,018810% |
| TST iterations | 202 | 200 | -0,990099% |
| TST traversal | 202,163742 s | 189,141945 s | -6,441213% |
| worst-time-ratio | 1,848461 | 1,700236 | -8,018810% |
| aggregate-time-ratio | 1,077603 | 1,023442 | -5,026076% |

Guardrail temporali S6:

| Fixture | Solver | Limite task | Esito |
|---|---:|---:|---|
| AHK | 0,720721 s | 1,900000 s | PASS |
| TH | 19,440967 s | 19,622222 s | PASS, margine 0,181255 s |
| TST | 219,311579 s | 128,988889 s | FAIL, gap 90,322690 s |

Il campo `time_gate` interno dei report confronta il solver direttamente con
il raw GTO+ reference; questa task congela invece i limiti espliciti sopra.
La decision matrix usa i limiti della task, senza reinterpretarli.

Equality strutturale B/S6 per ogni fixture:

| Fixture | decision scales | information sets | actions | solver state bytes | fingerprint |
|---|---:|---:|---:|---:|---|
| AHK | 18.438 | 595.626 | 1.288.290 | 5.300.664 | `fnv1a64:1b6f30a930cd9bd0` |
| TH | 147.256 | 36.596.832 | 83.318.592 | 334.452.416 | `fnv1a64:fcba3c9eff1b7147` |
| TST | 630.596 | 145.524.152 | 366.890.152 | 1.472.605.376 | `fnv1a64:731bf9562e90e792` |

Variazione peak RSS S6 vs B: AHK `+0,0830%`, TH `-0,0010%`, TST
`-0,0185%`. Non esiste crescita sistematica materiale; tutti i valori restano
sotto 2 GB. Il confronto peak-RSS-vs-GTO+ resta deferred.

### Curva TST che determina lo stop

La previsione precedente di crossing S6 al checkpoint circa 160 non è stata
confermata dal target-driven reale. La curva è non monotona:

| Iterazione S6 | dEV |
|---:|---:|
| 20 | 17,4652% |
| 40 | 7,84814% |
| 80 | 2,81722% |
| 120 | 1,35433% |
| 160 | 1,50224% |
| 200 | 0,874169% |

Il primo checkpoint strict reale è quindi 200, non 160. B raggiunge il target
a 202. La riduzione di due sole iterazioni non sostiene una promozione.

### Test eseguiti

| Verifica | Risultato |
|---|---|
| PowerShell parse runner | PASS |
| Release configure/build MSVC `/W4 /WX` | PASS |
| `gtosd_production_dcfr_contract` pre-flight | 1/1 PASS, fixture committed B |
| runner end-to-end B triplet | PASS, tre report validi |
| runner end-to-end S6 triplet | PASS, tre report validi |
| `git diff --check` tooling | PASS prima del commit |
| full CTest | non eseguito: count 0, non autorizzato/necessario dopo early REJECT |
| post-commit gamma-5 triplet | non applicabile: nessuna promotion |

### Cost accounting

| Voce | Conteggio/tempo |
|---|---:|
| B target runs | 1 triplet |
| S6 target runs | 1 triplet |
| AHK processes | 2 |
| TH processes | 2 |
| TST processes | 2 |
| contaminated runs | 0 |
| replacement runs | 0 |
| full CTest count | 0 |
| benchmark bracket wall | 11,150467 min |
| somma process wall pubblicati | 588,382006 s = 9,806367 min |
| somma comparable solver timer | 497,493548 s |

## Decisioni

### Decision matrix prima di ogni modifica production

| Gate | B fresco | S6 | Decisione |
|---|---|---|---|
| Common contract | PASS | PASS | identico salvo gamma |
| AHK correctness | PASS | PASS | PASS |
| AHK time | 0,812074 PASS | 0,720721 PASS | PASS |
| TH correctness | PASS | PASS | PASS |
| TH time | 18,777307 PASS | 19,440967 PASS | PASS, vicino al boundary |
| TST correctness | PASS | PASS | PASS |
| TST time | 238,430899 FAIL | 219,311579 FAIL | full parity FAIL |
| TST time improvement | baseline | 8,018810% | FAIL early gate `<10%` |
| TST iteration improvement | baseline | 0,990099% | FAIL early gate `<10%` |
| TST traversal improvement | baseline | 6,441213% | insufficiente |
| State/layout/fingerprint | PASS | PASS, equal | PASS |
| Desktop RAM | PASS | PASS | PASS |
| Worst ratio | 1,848461 | 1,700236 | 8,018810%, FAIL `<10%` |
| Aggregate ratio | 1,077603 | 1,023442 | 5,026076%, insufficiente |

### Applicazione dei criteri

S6 non soddisfa la soglia minima per continuare la qualification:

```text
required early TST time improvement       >= 10%
observed                                  = 8,018810%  FAIL

required early TST iteration improvement >= 10%
observed                                  = 0,990099%  FAIL
```

Di conseguenza non è ammesso raccogliere Pair 2/3 per cercare una mediana più
favorevole. Non si applicano le soglie di conditional promotion del 15% perché
il candidato è già respinto dall'early gate.

Compromesso rifiutato: promuovere S6 per il migliore dEV finale o per il
minor certification time TST. La task qualifica tempo al target, iterazioni,
traversal e worst ratio insieme; un dEV più basso al checkpoint 200 non
compensa il mancato miglioramento minimo.

### Stato finale

- production common contract: B `1.5/0/2`;
- S6 `1.5/0/5`: REJECT come production candidate;
- fixture production: invariate;
- default gamma: invariato a 2;
- production contract test: invariato;
- checkpoint format e policy: invariati;
- dormant production path: nessuno;
- five-process phase: **NOT AUTHORIZED**;
- full GTO+ parity: non dichiarata;
- remaining TST gap di S6: `90,322690 s`, ratio `1,700236x`;
- remaining TST gap della production B fresca: `109,442010 s`, ratio
  `1,848461x`.

Il report e il runner possono essere conservati; gli artifact restano nel
worktree esterno. Nessun cherry-pick automatico viene effettuato sul main
dirty.

## Passo successivo

Unica attività successiva ad alta priorità: mantenere B `1.5/0/2` e portare la
decisione al constraint-governance gate; non riaprire un altro gamma/schedule
loop senza un nuovo contratto quantitativo esplicito.
