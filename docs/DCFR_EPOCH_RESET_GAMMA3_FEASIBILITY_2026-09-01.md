# DCFR epoch-reset gamma=3 feasibility — 2026-09-01

> **CORREZIONE SEMANTICA 2026-09-04 — REPORT STORICO.** Schedule, dEV e tempi
> restano evidenza. Il vincolo Peak RSS `<2.000.000.000 B` e i relativi PASS
> erano basati sul display TSTC9D “Memory needed for solving” e non sono
> normativi. Vedere il
> [`piano di correzione`](GTO_PLUS_SOLVER_MEMORY_SEMANTICS_AND_GATE_CORRECTION_PLAN_2026-09-03.md).

## Analisi

Obiettivo: verificare se la schedule usata da `b-inary/postflop-solver` può
ridurre il tempo TSTC9D sotto il limite concordato `128.988889 s`, mantenendo
il contratto exact CPU/RAM-only, massimo otto thread, stato
`ScaledUint16RegretStrategy`, peak RSS `<2.000.000.000 B` e nessuna logica per
fixture.

Baseline production congelata: alternating signed DCFR `1.5/0/2`. Il
candidato è stato aggiunto come enum separato
`DcfrEpochResetGamma3`; fixture e default production non sono stati cambiati.

Fonte primaria della schedule:
[`b-inary/postflop-solver/src/solver.rs`](https://github.com/b-inary/postflop-solver/blob/main/src/solver.rs).
Il contatore sorgente è zero-based. Per l'iterazione GTOSD one-based `i`:

- `t = i - 1`;
- `t_alpha = max(t - 1, 0)`;
- discount regret positivo `t_alpha^1.5 / (t_alpha^1.5 + 1)`;
- discount regret negativo `0.5`;
- l'average riparte quando `t` è `0, 1, 4, 16, 64, 256, ...`;
- nell'epoca, `gamma_t = (k / (k + 1))^3`.

La ricorrenza `S_k = gamma_t S_(k-1) + sigma_k` è stata implementata nella
forma a scala comune `T_k = T_(k-1) + (k + 1)^3 sigma_k`. Le strategie
normalizzate sono identiche in aritmetica esatta; questa forma evita una
scansione completa dei 145.524.152 scale value TST a ogni iterazione. Solo il
reset di epoca azzera le scale strategy; regret e codici non vengono azzerati.

## Implementazione sperimentale

Checkout isolato:
`C:/tmp/gtosd-dcfr-epoch-reset-20260901`, branch
`research/dcfr-epoch-reset-20260901`, base
`77677b2f0be711b588bebdbd2256fe8a28f4b863`.

File modificati nel solo worktree sperimentale:

- `include/gtosd/postflop/postflop_solver.hpp`;
- `libs/postflop/src/postflop_solver.cpp`;
- `apps/gto_cli/main.cpp`;
- `tests/phase10_tests.cpp`;
- `tools/run_common_schedule_probe.ps1`.

Il parser benchmark accetta `dcfr_epoch_reset_gamma3` e il report lo identifica
come `exact_dcfr_epoch_reset_gamma3`. Il backend richiesto resta signed scaled
uint16 e lo stato solver non aumenta.

## Validazione

Build: MSVC 19.51 Release, `/fp:fast` non abilitato.

- `gtosd_phase10_tests`: PASS. Verificati punti di reset, clock alpha
  zero-based, identità della riscrittura cubica e resume byte-equivalente
  attraverso il reset a iterazione 17.
- `gtosd_gto_plus_reference_tests`: PASS, 24 asserzioni.
- CTest completo sul primo prototipo: 26/26 PASS. Dopo la correzione del clock
  alpha sono stati rieseguiti i due test direttamente interessati, entrambi
  PASS.
- `git diff --check`: PASS; soli warning EOL del worktree.

Il primo prototipo conservava il clock alpha production `i-1` e non era una
replica fedele del repository. È stato misurato separatamente e non viene usato
per decidere il candidato upstream:

| Variante diagnostica | Iter | dEV | Elapsed | Traversal | Certification | Peak RSS |
|---|---:|---:|---:|---:|---:|---:|
| production-alpha + epoch average | 120 | 1.363686% | 112.788226 s | 107.051063 s | 5.328593 s | 1.969.352.704 B |
| production-alpha + epoch average | 140 | 1.350681% | 136.567983 s | 130.714044 s | 5.433759 s | 1.969.278.976 B |

Il candidato upstream corretto ha prodotto:

| Fixture | Iter | dEV | Root EV | Elapsed | Traversal | Certification | State | Peak RSS |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| TSTC9D | 120 | 1.735877% | 8.480841 | 128.549624 s | 122.425443 s | 5.663958 s | 1.472.605.376 B | 1.969.508.352 B |
| TSTC9D | 140 | 2.192890% | 8.498720 | 134.611880 s | 129.371045 s | 4.829272 s | 1.472.605.376 B | 1.969.442.816 B |
| TSTC9D clean | 160 | 1.449442% | 8.498162 | 149.776154 s | 144.128117 s | 5.221366 s | 1.472.605.376 B | 1.969.512.448 B |
| TSTC9D clean | 180 | 1.034887% | 8.499380 | 166.846529 s | 161.789250 s | 4.624304 s | 1.472.605.376 B | 1.969.291.264 B |
| TSTC9D clean | 181 | 1.021157% | 8.499407 | 171.432126 s | 165.926469 s | 5.079867 s | 1.472.605.376 B | 1.969.537.024 B |
| TSTC9D clean | 183 | 0.993975% | 8.499461 | 174.852508 s | 169.460534 s | 4.956261 s | 1.472.605.376 B | 1.969.913.856 B |

Per TSTC9D passano layout, exact outcomes, correctness root, payoff zero-sum,
normalizzazione, stato e cap peak RSS. Fallisce il gate dEV strettamente sotto
1%. Il run aggiuntivo a 140 è stato eseguito per verificare anche il criterio
di miglioramento interno rispetto alla production, nonostante il kill gate di
parità fosse già fallito: il dEV sale da `1.735877%` a `2.192890%`, quindi il
candidato non produce una soluzione qualificata a 140.

La baseline production qualificata raggiunge `0.991863%` a iterazione 202 in
`208.111772 s`. Se il candidato si fosse fermato a 140, avrebbe ridotto le
iterazioni del `30.693%` e il tempo osservato del `35.317%`; sarebbe stato un
miglioramento interno reale pur restando oltre il limite GTO+ `128.988889 s`.
Il dato misurato esclude questa ipotesi: né il candidato upstream corretto
(`2.192890%`) né il prototipo ibrido (`1.350681%`) raggiungono il target a 140.

È stato quindi valutato separatamente un gate di miglioramento interno di
almeno il 10% sia nelle iterazioni sia nel tempo, a identica accuratezza. Con
baseline 202, il massimo intero ammissibile è 181 iterazioni; il limite
temporale è `187.300595 s`. Un primo run a 160 è stato escluso dal confronto
temporale perché contaminato da forte carico desktop (CPU normalizzata 55,2%).
Dopo la rimozione del carico, i run puliti a 160, 180 e 181 passano correttezza,
layout, exact outcomes, stato, RSS e tempo. A 181 il candidato riduce le
iterazioni del `10.396%` e il tempo del `17.625%`, ma il dEV è `1.021157%`:
supera il target di `0.021157` punti percentuali e quindi fallisce il gate
completo.

Il primo punto TST qualificato è stato poi localizzato esattamente: 182 resta
FAIL a `1.007270%`, mentre 183 passa a `0.993975%`. Il run pulito con una sola
certificazione a 183 impiega `174.852508 s`: rispetto alla baseline production
202/`208.111772 s`, riduce le iterazioni del `9.406%` e il tempo del `15.981%`.
Il candidato è quindi un miglioramento TST reale anche se la riduzione delle
iterazioni è leggermente inferiore al 10%.

### Ablazione causale dei componenti

Sono state isolate tre modifiche rispetto a production: esponente average
`gamma=3`, reset per epoche e clock alpha upstream. A iterazione 120 su TST:

| Variante | dEV |
|---|---:|
| production gamma2, no reset, clock production | 1.640370% |
| gamma3, no reset, clock production | 1.461968% |
| gamma3, reset, clock production | 1.363686% |
| gamma3, reset, clock upstream | 1.735877% |

`gamma=3` produce il primo miglioramento TST (`-0.178402` punti percentuali)
senza compromettere i benchmark comuni: a 120 misura `0.401197%` su AHKHQH e
`0.472491%` su TH7D6S. Il reset aggiunge un vantaggio TST iniziale di
`-0.098282` punti, mentre il clock upstream peggiora fortemente il punto 120.

Per il punto decisivo TST a 183 è stato completato il fattoriale
`reset x clock`, mantenendo `gamma=3`:

| Clock alpha | No reset | Reset |
|---|---:|---:|
| production | 1.108690% | 1.088830% |
| upstream | 1.077916% | 0.993975% |

Effetti misurati in punti percentuali di dEV:

- reset con clock production: `-0.019860`;
- clock upstream senza reset: `-0.030774`;
- reset con clock upstream: `-0.083938`;
- clock upstream con reset: `-0.094853`;
- interazione non additiva reset x clock: `-0.064078`.

Nessun singolo componente ablatto raggiunge l'1% a 183. Il PASS TST nasce
quindi dalla combinazione completa, e in particolare dall'interazione tra
reset e clock upstream, sopra il miglioramento di base prodotto da `gamma=3`.
Questa interazione non è trasferibile: su TH7D6S il clock upstream senza reset
è già FAIL a `1.973680%` a 183, mentre reset più clock production passa a
`0.192041%`. Il clock upstream è dunque la causa primaria della regressione
TH; il reset ne amplifica l'effetto dopo il confine di epoca.

Per evitare una cadenza scelta specificamente per TST, candidato e production
sono stati confrontati target-driven con la stessa `certification_interval=30`:

| Fixture | Variante | Stop iter | dEV | Elapsed | Traversal | Certification | Correctness |
|---|---|---:|---:|---:|---:|---:|---|
| TSTC9D | epoch-reset gamma3 | 185 | 0.969220% | 194.696234 s | 170.785436 s | 23.469554 s | PASS |
| TSTC9D | production 1.5/0/2 | 210 | 0.927424% | 228.900 s | 203.74 s | 24.72 s | PASS |
| AHKHQH | epoch-reset gamma3 | 61 | 0.979100% | 0.602779 s | 0.43 s | 0.16 s | PASS |
| AHKHQH | production 1.5/0/2 | 60 | 0.924180% | 0.559535 s | 0.45 s | 0.09 s | FAIL root gate |
| TH7D6S | production 1.5/0/2 | 120 | 0.512104% | 30.929123 s | 28.273717 s | 2.239278 s | PASS |

Sul TST A/B omogeneo il candidato riduce le iterazioni del `11.905%` e il
tempo del `14.941%`. Tuttavia la qualifica condivisa fallisce su TH7D6S: il
candidato misura `1.903880%` a 180, `1.887910%` a 240, `1.887130%` a 300 e
`1.876520%` a 360, poi supera 400 iterazioni senza convergere. Il run è stato
interrotto dal kill gate. Il reset dell'average strategy a 257 rende questa
regressione particolarmente rilevante; production passa invece a 120.

Report grezzi:

- `out/dcfr-epoch-reset-20260901/epoch-reset-gamma3-iter120-r1/tst.upstream-iter120.report.json`;
- `out/dcfr-epoch-reset-20260901/epoch-reset-gamma3-iter120-r1/tst.upstream-iter120.log`;
- `out/dcfr-epoch-reset-20260901/epoch-reset-gamma3-iter120-r1/tst.upstream-iter140.report.json`;
- `out/dcfr-epoch-reset-20260901/epoch-reset-gamma3-iter120-r1/tst.upstream-iter140.log`;
- `out/dcfr-epoch-reset-20260901/epoch-reset-gamma3-iter120-r1/tst.upstream-iter160-clean-r1.report.json`;
- `out/dcfr-epoch-reset-20260901/epoch-reset-gamma3-iter120-r1/tst.upstream-iter180-clean-r1.report.json`;
- `out/dcfr-epoch-reset-20260901/epoch-reset-gamma3-iter120-r1/tst.upstream-iter181-clean-r1.report.json`;
- `out/dcfr-epoch-reset-20260901/epoch-reset-gamma3-iter120-r1/tst.upstream-iter182-183-curve-r1.report.json`;
- `out/dcfr-epoch-reset-20260901/epoch-reset-gamma3-iter120-r1/tst.upstream-iter183-clean-r1.report.json`;
- `out/dcfr-epoch-reset-20260901/epoch-reset-gamma3-iter120-r1/tst.upstream-target-ci30-r1.report.json`;
- `out/dcfr-epoch-reset-20260901/epoch-reset-gamma3-iter120-r1/tst.production-target-ci30-r1.report.json`;
- `out/dcfr-epoch-reset-20260901/epoch-reset-gamma3-iter120-r1/ahk.upstream-target-ci30-r1.report.json`;
- `out/dcfr-epoch-reset-20260901/epoch-reset-gamma3-iter120-r1/ahk.production-target-ci30-r1.report.json`;
- `out/dcfr-epoch-reset-20260901/epoch-reset-gamma3-iter120-r1/th.upstream-target-ci30-r1.log`;
- `out/dcfr-epoch-reset-20260901/epoch-reset-gamma3-iter120-r1/th.production-target-ci30-r1.report.json`;
- `out/dcfr-component-ablation-20260901/dcfr-gamma3-no-reset-iter120-r1/manifest.json`;
- `out/dcfr-component-ablation-20260901/dcfr-gamma3-no-reset-iter120-r1/tst.iter183.report.json`;
- `out/dcfr-component-ablation-20260901/epoch-reset-gamma3-production-clock-iter183-r1/manifest.json`;
- `out/dcfr-component-ablation-20260901/gamma3-no-reset-upstream-clock-iter183-r1/manifest.json`;
- `out/dcfr-epoch-reset-20260901/epoch-reset-gamma3-iter120-r1/manifest.json`;
- `out/dcfr-epoch-reset-20260901/epoch-reset-gamma3-iter120-r1/tst.iter140.report.json`.

## Decisione

**REJECT FOR COMMON PRODUCTION — TH7D6S CONVERGENCE REGRESSION.**

La schedule completa non viene integrata né promossa come algoritmo comune. Production
resta alternating signed DCFR `1.5/0/2`; non cambiano fixture, default, formati
o contratti. Il candidato è inequivocabilmente superiore su TSTC9D, quindi non
viene scartato per avere mancato una soglia nominale del 10%. Viene escluso
dalla production perché la stessa implementazione regredisce gravemente su
TH7D6S. Adottarla soltanto per TST sarebbe logica fixture-specific e violerebbe
il contratto del motore condiviso.

L'ablazione identifica però un sottocomponente riutilizzabile: `gamma=3` senza
reset e con clock production preserva il PASS AHKHQH e migliora i dEV TH7D6S
e TSTC9D osservati a 120. Non riproduce da solo il PASS TST a 183 e resta
quindi un candidato separato da qualificare, non una modifica production
autorizzata.

## Passo successivo

Qualificare target-driven `gamma=3` senza reset e con clock production come
candidato comune separato. È l'unico componente isolato che preserva AHKHQH e
migliora TH7D6S/TSTC9D a iterazione identica senza introdurre la regressione
del clock upstream; production rimane congelata fino al completamento del gate.

## Sweep comune target-driven successiva

La decisione precedente sulla schedule completa resta valida, ma il frontier è
stato riaperto con reset limitati alle epoche `1,2,5,17,65` e clock interpolati.
Ogni esperimento ha eseguito AHKHQH, TH7D6S e TSTC9D fino a dEV esatto `<1%`,
con `certification_interval=30`, stesso binario Release, 8 thread e nessuna
logica fixture-specific.

| Candidato | AHK iter / s | TH iter / s | TST iter / s | Gate |
|---|---:|---:|---:|---|
| Release 1.5/0/2 | 60 / 0.670 | 120 / 30.989 | 210 / 277.692 | AHK correctness FAIL |
| gamma3, no reset | 60 / 0.687 | 67 / 20.375 | 210 / 270.710 | PASS |
| bounded65, production clock | 60 / 0.627 | 90 / 30.789 | 210 / 311.930 | PASS |
| bounded65, clock 25% | 60 / 0.651 | 64 / 19.156 | 240 / 290.090 | PASS, TST regression |
| bounded65, clock 50% | 61 / 0.603 | 62 / 18.556 | 180 / 225.370 | AHK correctness FAIL |
| bounded65, clock 75% | 90 / 0.866 | 61 / 17.673 | 180 / 230.530 | PASS, AHK regression |
| bounded65, upstream dopo 65 | 60 / 0.514 | 90 / 23.960 | 180 / 223.300 | PASS |

Il leader comune è `bounded65-upstream-after65`: preserva le 60 iterazioni AHK,
riduce TH da 120 a 90 e TST da 210 a 180. Cinque processi indipendenti hanno
riprodotto esattamente `60/90/180` e dEV `0.868396%/0.615665%/0.745378%`.
Le mediane elapsed Release contro leader sono rispettivamente: AHK `0.55/0.59 s`,
TH `27.25/23.53 s`, TST `227.06/223.30 s`. Il tempo TST ha intervalli sovrapposti
(`221.58-277.69 s` contro `192.24-236.74 s`), quindi il beneficio temporale non
è ancora statisticamente robusto; la riduzione deterministica delle iterazioni
è invece `14.286%` su TST e `25%` su TH.

La soglia adattiva di certificazione `1.50` è risultata neutra rispetto al
default `1.25`: stessi stop e stessi dEV sui tre fixture. Il default resta 1.25.
Peak RSS leader massimo TST: `1,969,954,816 B`, sotto il limite stretto
`2,000,000,000 B`. Suite finale Release: `26/26 PASS`.

Report grezzi: `out/dcfr-option-loop-20260901/*/manifest.json`.

## Decisione aggiornata

**CANDIDATO COMUNE QUALIFICATO, NON ANCORA PROMOSSO A DEFAULT.**

`bounded65-upstream-after65` supera la sweep condivisa e la certificazione a
cinque processi senza regressioni di iterazioni, correttezza o RAM. Production
resta congelata: la variabilità dei tempi TST impedisce ancora di affermare un
speedup temporale robusto, e il codice sperimentale non viene integrato o
committato automaticamente.

## Certificazione A/B interleaved a carico controllato

La conclusione temporale precedente è stata sottoposta a una nuova campagna
indipendente. Un primo tentativo è stato invalidato e interrotto perché il
preflight CPU non è rimasto entro il limite; i suoi report non partecipano ai
risultati. La campagna autorevole `v2` ha eseguito cinque round, alternando
l'ordine `Release -> leader` e `leader -> Release`. Prima di ciascuno dei dieci
bracci ha richiesto media CPU idle su cinque campioni `<=15%`, almeno 4 GB di
RAM libera, piano `Prestazioni elevate` e assenza di `ProjectZomboid64`.

I dieci preflight persistiti misurano CPU media `10,4-14,8%` (mediana `12,3%`)
e RAM libera minima `16.178.921.472 B`. Ogni braccio ha eseguito tutti e tre i
fixture, in un processo nuovo, fino al primo dEV esatto `<1%`.

| Fixture | Release iter / dEV | Leader iter / dEV | Release mediana / p95 | Leader mediana / p95 | Delta mediana |
|---|---:|---:|---:|---:|---:|
| AHKHQH | 60 / 0,924180% | 60 / 0,868396% | 0,566447 / 0,629813 s | 0,549697 / 0,597157 s | -2,96% |
| TH7D6S | 120 / 0,512104% | 90 / 0,615665% | 28,735305 / 31,985548 s | 23,161294 / 24,046050 s | -19,40% |
| TSTC9D | 210 / 0,927424% | 180 / 0,745378% | 233,330510 / 248,176727 s | 199,239600 / 205,525964 s | -14,61% |

Il leader vince tutte le cinque coppie TH (`16,02-27,24%`) e tutte le cinque
coppie TST (`11,34-20,11%`). La mediana degli speedup accoppiati è `21,36%` su
TH e `15,03%` su TST. AHK conserva le stesse iterazioni; il tempo sub-secondo è
rumoroso ma mediana e p95 non regrediscono. Il leader passa correttezza, layout
ed exact outcomes in `15/15` solve. La Release riproduce il noto correctness
FAIL AHK in `5/5` processi.

Lo stato è identico fra i due bracci: AHK `5.300.664 B`, TH `334.452.416 B`,
TST `1.472.605.376 B`. Peak RSS massimo leader: AHK `166.621.184 B`, TH
`798.969.856 B`, TST `1.970.135.040 B`; TST resta sotto il cap desktop stretto
di `2.000.000.000 B`. Root EV leader è deterministico nei cinque processi:
`19,102074794`, `8,227669013`, `8,497660533` ante.

### Decisione finale della ricerca

**SUPERIORE ALLA RELEASE E QUALIFICATO PER INTEGRAZIONE.**

La nuova campagna chiude il dubbio dovuto alla variabilità temporale: il
vantaggio TST è ripetuto in tutte le coppie, supera il 10% anche nel campione
peggiore e preserva i gate comuni. Questo non significa ancora parità temporale
con GTO+ (`128,988889 s` su TST): significa che il candidato è una sostituzione
generica e misurabilmente migliore della Release corrente. L'integrazione e il
cambio del default restano una modifica successiva separata.

Evidenza grezza autorevole:
`out/dcfr-interleaved-certification-20260901-v2/`.

## Integrazione production e qualificazione final-head

Il candidato qualificato e' stato ripulito e integrato come unico percorso
`production_dcfr`. Sono state rimosse le enum e i parser delle varianti
sperimentali respinte. Il contratto implementato e' DCFR exact alternating
signed `alpha=1.5`, `beta=0`, `gamma=3`, reset one-based soltanto a
`1,2,5,17,65` e regret clock ritardato di una iterazione dopo 65. Il valore
checkpoint dell'enum production resta `11`; checkpoint/resume e' coperto da un
test byte-exact. Le tre fixture ufficiali usano ora `production_dcfr` e
`certification_interval=20`.

La qualificazione final-head autorevole usa i processi `r2-r6`. `r1` ha
prodotto risultati validi ma viene escluso dalle statistiche perche' il suo
preflight non era stato persistito. Prima di ciascuno dei cinque processi
autorevoli sono stati registrati cinque campioni CPU idle, RAM libera, piano di
alimentazione e assenza di `ProjectZomboid64`: media CPU `9,8-14,6%` (mediana
`12,2%`), RAM libera minima `16.890.228.736 B`.

| Fixture | Iter / dEV | Solver mediana / p95 | Wall mediana / p95 | Root EV | State | Peak RSS massimo |
|---|---:|---:|---:|---:|---:|---:|
| AHKHQH | 80 / 0,951423% | 0,758705 / 0,790918 s | 6,231883 / 6,468465 s | 19,118977716 | 5.300.664 B | 166.645.760 B |
| TH7D6S | 80 / 0,807956% | 19,948228 / 24,192260 s | 35,208170 / 41,015117 s | 8,226793157 | 334.452.416 B | 799.043.584 B |
| TSTC9D | 160 / 0,904505% | 184,095930 / 197,865030 s | 208,403423 / 221,888253 s | 8,495660681 | 1.472.605.376 B | 1.969.860.608 B |

Tutti i `15/15` solve hanno `correctness_passed=true`, layout fixture uguale ed
exact outcomes. Il cap desktop stretto `<2.000.000.000 B` e' rispettato in
ogni processo. La suite Release finale passa `26/26` test.

### Decisione di integrazione

**INTEGRATO E QUALIFICATO COME NUOVA PRODUCTION COMUNE.**

La promozione sostituisce il precedente contratto `1.5/0/2` come default delle
fixture e autorita' di benchmark. Il confronto A/B interleaved CI=30 resta la
prova causale contro la Release: leader vincente in tutte le cinque coppie TH
e TST, mediane rispettivamente `-19,40%` e `-14,61%`. La campagna final-head
CI=20 dimostra separatamente che il refactoring production conserva convergenza,
correttezza, layout, esiti, stato e RAM su cinque processi.

Il risultato non viene reinterpretato come parita' temporale con GTO+. TH
final-head mediano `19,948228 s` resta `0,326006 s` (`1,661%`) sopra il limite
`19,622222 s`; TST `184,095930 s` resta `55,107041 s` (`42,722%`) sopra
`128,988889 s`. Il parity gate complessivo rimane quindi **NON SUPERATO** su TH
e TST e F11+ resta congelata.

Evidenza final-head: `out/production-final-head-20260901/`.
