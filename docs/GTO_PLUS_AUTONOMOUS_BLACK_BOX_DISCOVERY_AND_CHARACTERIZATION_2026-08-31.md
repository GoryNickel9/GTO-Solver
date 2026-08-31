# GTO+ autonomous black-box discovery and characterization — 2026-08-31

## Analisi

### Esito e risposta alla domanda utente

**Outcome unico: E. UNSAFE OR INCONCLUSIVE.**

L'agent ha trovato, identificato e avviato autonomamente GTO+. Non è però
riuscito a identificare con SHA-256 il progetto TST ripristinato, a verificare
l'intera configurazione, a timestampare autorevolmente l'azione manuale `Run
Solver`, né a provare lo stato finale `SOLUTION_CONSULTABLE`. La singola
osservazione resta quindi invalida e non autorizza altre quattro repliche,
scaling o claim prestazionali.

Risposta operativa osservata:

- l'utente non ha dovuto aprire GTO+;
- l'utente non ha dovuto caricare il progetto nella sessione osservata, perché
  GTO+ ha ripristinato `GTOSD3`;
- l'utente ha dovuto premere manualmente `Run Solver`;
- il ripristino automatico non prova che il progetto fosse il file TST corretto.

### Authority map

| Fonte | Commit | Data | Ruolo e autorità | Superseded da |
|---|---|---|---|---|
| `README.md` | `d798d896` | 2026-08-30 | riepilogo gate final-head | dashboard 2026-08-31 per stato corrente |
| `docs/IMPLEMENTATION_STATUS.md` | `da39f4b1` | 2026-08-31 | dashboard production corrente | nessuno nel base |
| `docs/specifications/PERFORMANCE.md` | `da39f4b1` | 2026-08-31 | contratto CPU/RAM/tempo | nessuno nel base |
| `docs/GTO_PLUS_PARITY_JOURNEY.md` | `da39f4b1` | 2026-08-31 | gate e storia delle evidenze | report fresh strict-2GB per il solo tempo TST |
| `docs/GTO_PLUS_CONVERGENCE_BENCHMARK.md` | `f4dd1f7d` | 2026-08-29 | definizione GTO+ e timing scope | nessuno per la reference GTO+ |
| `docs/GTO_PLUS_NEW_BENCHMARK_GUIDE.md` | `f4dd1f7d` | 2026-08-29 | protocollo fixture | nessuno |
| `benchmarks/fixtures/gto_plus_tstc9d_101.json` | `2b332a70` | 2026-08-30 | configurazione machine-readable TST | nessuno |
| `docs/S6_COMMON_PRODUCTION_QUALIFICATION_LOOP_2026-08-31.md` | `b47923b5` | 2026-08-31 | qualification S6, outcome `D. REJECT` | strict-2GB per baseline TST fresca |
| `docs/TST_STRICT_2GB_BOTTLENECK_ATTRIBUTION_AND_FEASIBILITY_LOOP_2026-08-31.md` | `d3ddcda3` | 2026-08-31 | baseline TST fresca e blocker | authority temporale più recente |

Gli ultimi due documenti sono presenti in commit/worktree di ricerca della
repository, non nel base `77677b2f`. Per il confronto prevalgono i valori
freschi strict-2GB: solver `234.437726 s`, traversal `197.392990 s`,
certification `36.592053 s`, other `0.452683 s`, peak RSS
`1,971,036,160 B`. La reference GTO+ resta `116.09 s`, `0.91%`,
`2,000,000,000 B`, 8 thread.

### Installazione ed executable identity

La discovery bounded ha ispezionato processi, registro, associazione `.gto`,
shortcut e directory collegate. Ha prodotto un solo candidato con confidence
100:

| Campo | Valore |
|---|---|
| path redatto | `[volume esterno]\\GTO6+\\GTO\\GTO.exe` |
| SHA-256 | `CE3EA87A5BF755AC359DE1D2AE8595DAF0E8331FCF5DA30DDEF047FDC9FD522C` |
| File/ProductVersion | `1.6.9.0` |
| architettura | x64 |
| firma | Authenticode valida, StoxEV B.V. |
| associazione | `.gto -> GTO.Document` |
| shell open | executable più `"%1"` |

L'avvio diretto senza progetto è riuscito. L'app è passata da `Untitled - GTO`
a `GTOSD3 - GTO` tramite ripristino nativo dell'ultima sessione.

### Project identity e configurazione

La ricerca bounded in Recent Items, Documents e directory dell'installazione
non ha trovato file `.gto`. Non esiste quindi un project path redatto né un
project SHA-256 pubblicabile: entrambi sono `unknown`.

L'interfaccia mostrava board `Ts Tc 9d`, target `1%`, 8 thread e GTO+ 1.6.9.
Pot, stack, range, sizings, rake, valore raw `Memory needed for solving` e
identità del file non sono stati letti da una fonte indipendente. La
configurazione TST completa è pertanto **non verificata**.

### Workflow, UI Automation e macchina a stati

La vista accessibility della sessione esponeva la finestra top-level ma non i
controlli figli necessari. I tentativi di invocazione semantica non hanno
fornito una transizione verificabile; coordinate e OCR non sono stati promossi
a fonti autorevoli. Classificazione della sessione: **UIA_NONE**.

La macchina a stati osservata è documentata in
`GTO_PLUS_OBSERVED_WORKFLOW_STATE_MACHINE_2026-08-31.md`. Il punto critico è:

```text
MAIN_WINDOW_READY
  -> RESTORED_SESSION_AMBIGUOUS
  -> [manual Run Solver]
  -> SOLVING
  -> TARGET_FIRST_OBSERVED_BELOW_1
  -> SOLVE_COMPLETING_EXTERNAL_ONLY
  -> COMPLETION_UNVERIFIED
```

Il dEV stale `0.146`/`0.91%` prima del run impediva di distinguere
`READY_TO_SOLVE` da una soluzione già risolta. Il reset del file progress ha
poi provato una nuova trace, ma non l'istante esatto del click.

### dEV, completion e memoria

Fonte dEV: file progress nativo autorizzato dell'installazione, copiato come
snapshot raw negli artifact. Con pot iniziale di 16 ante:

- ultimo punto `>=1%`: `0.180646 / 16 * 100 = 1.1290375%`;
- primo punto `<1%`: `0.145654 / 16 * 100 = 0.9103375%`;
- intervallo osservato tra i due aggiornamenti: `6.6227233 s`;
- crossing timestamp: `2026-08-31T21:23:16.8663054Z`.

Il crossing è osservabile per la trace corrente, ma il tempo click-to-cross non
lo è. Dal reset della trace al crossing trascorrono `144.0195159 s`: è solo un
limite diagnostico, non il timing ufficiale.

La memoria è scesa e la CPU è diventata idle `11.5973211 s` dopo il crossing,
ma questi segnali non provano una soluzione consultabile. Nessun controllo
`Completed`, browser di soluzione o read-only query è stato verificato.

`memory_metric_mapping = unresolved`: non è provato che il valore GTO+
`Memory needed for solving` corrisponda a working set o private bytes. I picchi
OS non sono trattati come violazione del contratto GTO+.

## Piano

Il piano gated applicato è stato:

1. audit repository, authority map, hash del main dirty e worktree esterno;
2. discovery bounded di installazione, file association e progetto;
3. avvio sicuro e probe accessibility senza solve;
4. ricostruzione della macchina a stati osservabile;
5. implementazione e test del tooling first-party;
6. osservazione di una sola TST dopo azione manuale;
7. stop obbligatorio quando la replica non è risultata completamente
   interpretabile;
8. nessun dry-run dichiarato PASS, nessuno smoke, nessuna serie G1-G5 e nessuno
   scaling.

## Implementazione

| Campo | Valore |
|---|---|
| branch | `research/gto-plus-autonomous-black-box-20260831` |
| base | `77677b2f0be711b588bebdbd2256fe8a28f4b863` |
| tooling commit | `fb637747473b1ad5262ecc2f4e00e054e29865b9` |
| output runtime | `.tmp/gto-plus-black-box/` |
| production changes | nessuna |

Tooling aggiunto:

- `tools/probe_gto_plus_installation.ps1`;
- `tools/probe_gto_plus_projects.ps1`;
- `tools/probe_gto_plus_uia.ps1`;
- `tools/run_gto_plus_black_box.ps1`;
- `tools/evaluate_gto_plus_black_box.ps1`;
- `tools/gto_plus_black_box_common.ps1`;
- `tests/verify_gto_plus_black_box_runner.ps1`.

Il runner rifiuta directory run esistenti, profili non legati a hash/versione,
selector coordinate/OCR, processi GTO+ preesistenti, configurazione non
verificata e valori dEV stale senza attività del run. Copia il progetto in
read-only, usa clock monotonic, conserva raw dEV, applica `<1%` strettamente e
produce `validity.json`. Non è stato eseguito contro GTO+ perché non esistevano
project identity e selector profile autorevoli. La cattura screenshot audit e
la telemetria I/O restano limiti dichiarati del runner corrente.

Nessun executable, progetto, soluzione, screenshot, file di licenza o artifact
`.tmp` è incluso nei commit.

## Validazione

### Tooling e discovery

| Controllo | Risultato |
|---|---|
| parsing di tutti gli script | PASS |
| test statici safety/contract | PASS |
| parser dEV punto/virgola e strict `<1%` | PASS |
| aggregazione sintetica cinque run, median e p95 nearest-rank | PASS |
| installation probe reale | PASS, 1 candidato confidence 100 |
| project probe reale | PASS, 0 candidati |
| evaluator sull'osservazione | PASS, outcome E |
| `git diff --check` | PASS |

### Run ledger

| Run | Stato | Crossing | Completion | Motivo |
|---|---|---|---|---|
| `manual-user-marker-tst-01` | INVALID | osservato, timing non ufficiale | non verificata | timestamp click assente; consultabilità non provata; project/config identity assenti |

Conteggi: `valid=0`, `invalid=1`, `replacement=0`. Mediana first `<1%` e
mediana consultable solution: `N/A`.

Metriche diagnostiche della replica invalida:

| Metrica | Valore |
|---|---:|
| max instantaneous working set | `2,183,884,800 B` |
| OS peak working set | `2,184,028,160 B` |
| max private bytes | `2,157,686,784 B` |
| process-tree peak | non disponibile |
| CPU reset-trace -> crossing | `797.71875 CPU-s / 144.0195159 s` |
| CPU normalizzata su 8 logical processor | `69.2370%` |
| process thread-count range | `2..11` |
| sample non-responsive | `0` |
| I/O | non disponibile |

`ThreadCount` è una metrica di processo, non la prova del numero di solver
thread; gli 8 solver thread sono soltanto il valore visuale osservato.

### Test repository

Il full CTest sul build Release corrente ha prodotto `22/23 PASS` in
`229.48 s`. È fallito soltanto `gtosd_production_dcfr_contract` su
`gto_plus_ahkhqh_101.json`: il build punta al main dirty dell'utente, dove i tre
fixture GTO+ erano già modificati prima della task. Nessun file production è
stato modificato dal branch black-box. Non è stata eseguita una nuova build
`/W4 /WX`, perché il cambiamento contiene solo tooling PowerShell e documenti.

Il main è rimasto byte-invariant rispetto al pre-flight:

| File tracked già modificato | SHA-256 iniziale e finale |
|---|---|
| `gto_plus_ahkhqh_101.json` | `A7CB748226A7D0E421C87361111AA7813DA8E90F43F0B3725788AB98FD6DF6DC` |
| `gto_plus_th7d6s_101.json` | `8C475ED4B487E10D6DE703A0DA8BD74FBDC7A0C2B7DA95BFE7A1E6512419F9B7` |
| `gto_plus_tstc9d_101.json` | `D0371E10FF926BEEE4012DEB9BA0A4E96A0D3958846B035EE417FE5CEAE0E833` |

`.reasonix/` e `.tmp/` sono rimasti untracked. `main`, `origin/main` e il base
del worktree erano allineati a `77677b2f` (`ahead=0`, `behind=0`) al pre-flight.

## Decisioni

| Decisione | Esito |
|---|---|
| automation classification | `E. UNSAFE OR INCONCLUSIVE` |
| GTO+ characterization validity | INVALID |
| reference `116.09 s` riprodotta | NO |
| project/configuration identity | UNRESOLVED |
| Run Solver invocabile | YES manuale; NO autonomo verificato |
| target crossing osservabile | YES per trace corrente; NO timing click-to-cross |
| completion consultabile osservabile | NO |
| memory parity | UNRESOLVED |
| cinque repliche/scaling | NOT AUTHORIZED |
| vantaggio black-box | `5. INSUFFICIENT OBSERVABILITY` |

Il solo confronto valido resta quello tra riferimenti congelati: GTOSD fresco
`234.437726 s` contro GTO+ reference `116.09 s`, rapporto `2.019448x`. La run
osservata non produce un nuovo rapporto perché è invalida. I dati non
distinguono un vantaggio algoritmico da dataflow, stopping/certification o una
combinazione; non provano algoritmo interno, iterazioni, precisione, codec,
layout o cadenza best response.

L'utente non deve aprire GTO+ nella macchina osservata. Per una futura replica
scientifica deve però indicare o salvare una volta il file progetto TST esatto
in modo che l'agent possa copiarlo e hasharlo; finché UIA resta `NONE`, deve
anche eseguire il singolo click deterministico `Run Solver` dopo conferma del
marker di start.

## Passo successivo

Creare una copia read-only identificabile del progetto TSTC9D e validarne
SHA-256, configurazione completa e almeno un segnale nativo di completion in un
dry-run senza solve; non avviare una nuova TST prima che questo gate sia PASS.
