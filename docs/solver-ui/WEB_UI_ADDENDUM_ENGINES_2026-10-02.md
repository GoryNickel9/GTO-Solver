# Addendum al prompt della web UI: motori, instradamento automatico, 3-way e postflop HU esatto

Versione del 02/10/2026, scritta fra le 00:30 e le 00:50 e rivista fra le 00:55 e le 01:17 (commit `d026e86` alle 01:17:33) contro codice e dati, sempre in
sola lettura: nessun run, build o test lanciato, nessun file del repository modificato a parte questo documento. Il run 3-way 1
in corso non è stato toccato; di lui sono stati letti soltanto file di testo piccoli. Le correzioni della revisione sono
elencate nell'Appendice C.

**Destinatario.** L'agent coder che ha costruito il prototipo in `GTO-Solver-solver-ui/apps/solver-ui` (branch
`feat/solver-ui`, commit `179dea8`, `9f00e39`, `2612663`, `99e492a` del 01/10).

**Rapporto con il prompt originale.** Il prompt di partenza è `docs/solver-ui/WEB_UI_PROTOTYPE_PROMPT.md` (30/09, revisione 2).
- Tutto ciò che questo addendum non cambia resta valido: regole R1-R10 (§3), sicurezza (§6), modello dei job (§5), regole di
  ingegneria (§11), trappole (§12) e appendici.
- Dove i due documenti si contraddicono, prevale l'addendum.
- Se il codice contraddice entrambi, vale il codice: segnala la differenza all'utente.
- Le risposte dell'utente a D1-D8 stanno in `apps/solver-ui/docs/DECISIONS.md` e restano valide. Vale anche la revisione
  dell'interfaccia del 01/10 (`DECISIONS.md:76-89`): niente riferimenti esterni, Libreria con le sole soluzioni nostre, niente
  argv grezzi o percorsi storici nell'interfaccia.

**Legenda.** Stessa del prompt originale.
- **[V]** = verificato nel codice o nei dati (file:riga, oppure comando).
- **[I]** = dedotto e non provato. Verificalo prima di costruirci sopra e annota l'esito in `DECISIONS.md`.

I numeri di riga del solver e dell'handoff sono al commit `9ab2035` di `feat/monker-step1-checkdown` (per il codice è
identico a `dafcee2`: da allora è cambiato solo l'handoff). Quelli della UI sono al commit `99e492a` e si riferiscono a
`apps/solver-ui/backend/solver_ui/`, salvo indicazione diversa. `SP` è la cartella temporanea della sessione principale
indicata sotto.

**Percorsi**

| Sigla | Percorso |
|---|---|
| `REPO` | `C:/Users/GoryNickel/Documents/GitHub/GTO-Solver`: checkout principale, branch `feat/monker-step1-checkdown` |
| `UI` | `C:/Users/GoryNickel/Documents/GitHub/GTO-Solver-solver-ui/apps/solver-ui` |
| `WT` | `C:/Users/GoryNickel/Documents/GitHub/GTO-Solver-phase3`: worktree della fase 3, branch `feat/threeway-step2` |
| `OUT1` | `REPO/out/monker/step2_3way/3WAY50_15x4_rake25cap2`: cartella del run 3-way 1, **in corso, sola lettura** |
| `FZ` | `REPO/out/frozen/threeway_step2_12fe441`: copie congelate di runner, `compare_charts.py`, config e texture del run 1 |
| `Q` | `REPO/out/frozen/queue_3way50_15x4.sh`: coda giornaliera congelata del run 1 |
| `R` | `FZ/tools/monker_compare/run_step2_continuous.sh`: runner congelato |
| `TR` | `REPO/benchmarks/preflop_blueprint_train.cpp` (identico al sorgente del trainer congelato [V: `git diff 12fe441f HEAD` vuoto sui percorsi del trainer]) |
| `CLI` | `REPO/apps/gto_cli/main.cpp` |
| `MONKER3` | `C:/Users/GoryNickel/Documents/GitHub/GTO-Chart-Browser/ranges/Short Deck/Symmetrical Chart/3-way/50a` (sola lettura) |

**Rapporti di studio da cui viene questo addendum.** Stanno nella cartella temporanea della sessione principale
(`C:/Users/GORYNI~1/AppData/Local/Temp/claude/C--Users-GoryNickel-Documents-GitHub-GTO-Solver/94e439a5-4749-4983-9219-1f8b6fb59a5f/scratchpad/webui2/`):
`ui_map.md`, `gto_cli.md`, `threeway_formats.md`, `chart_table_3way.md`. Windows può ripulire quella cartella: tutto ciò che
ti serve è riportato qui.

---

## 0. Come procedere

1. Leggi questo addendum per intero e poi le sezioni del prompt originale che richiama.
2. Le domande aperte sono nella sezione 8 (QA1-QA7). Nessuna blocca E0 ed E1; QA4 tocca soltanto le viste Monker di E2. Prima di iniziare una milestone, fai
   all'utente **insieme e una sola volta** le domande che la bloccano (colonna "Blocca" della sezione 8), poi registra
   risposte e default in `DECISIONS.md`.
3. Le milestone sono E0-E6 (sezione 7). Si aggiungono dopo M0-M3 del prompt originale e non le sostituiscono.
   - È contenuto nuovo nella roadmap della UI: annuncialo all'utente (R10).
   - Non riordinarle in silenzio.
4. Alla fine di ogni milestone consegna, come nel prompt originale:
   - una demo su fixture o in mock;
   - l'elenco dei test verdi;
   - le domande per la milestone successiva;
   - i fatti [I] verificati o smentiti.
5. **Subagent.** Se ne usi, falli girare sempre con il modello **opus** (Opus 5.5), mai con Fable. È una richiesta esplicita
   dell'utente.
6. **Lingua.** Rispondi all'utente in italiano; anche i testi della UI sono in italiano. Codice, identificatori, messaggi di
   commit e commenti sono in inglese.

---

## 1. Ha senso tenere `gto_cli`, visto che si fa tutto dalla web UI?

**Risposta: sì.** `gto_cli` (o un suo eseguibile gemello più snello) resta, ma cambia ruolo. Non è più un'interfaccia per
l'utente: diventa **il motore HU postflop esatto che il backend della UI lancia**, come lancia oggi i tool
`gtosd_preflop_blueprint_*`. Non va collegato dentro il backend e non va rimosso. È anche ciò che l'utente ha già scelto il
02/10, togliendo la GUI desktop perché la web UI la sostituisce e lancia `gto_cli` (messaggio del commit, riga 6 sotto).
Resta da decidere solo come completarlo (QA1, QA2).

**Evidenze**

| # | Fatto | Fonte |
|---|---|---|
| 1 | La UI non chiama mai il C++ in-process: lancia eseguibili frozen e non legge né checkpoint né policy. Ogni motore deve quindi essere un eseguibile headless. Senza `gto_cli` la UI non avrebbe nessun motore HU postflop | [V] `UI/docs/ARCHITECTURE.md:3, 24-25`; `filesystem.py:86-89` |
| 2 | `gto_cli` ha già il modello dei tool preflop: checkpoint a ogni certificazione, resume, pausa e cancel con un file di controllo, report JSON (schema 2), marker su stdout ed exit code distinti | [V] `CLI:253-420`, `:422-433` |
| 3 | La libreria preflop non può linkare le librerie postflop legacy (separazione imposta da CMake): un eseguibile per motore rispetta questo confine | [V] `tests/verify_preflop_blueprint_isolation.cmake:1-30`; `libs/preflop_blueprint/CMakeLists.txt:46-51` |
| 4 | `gto_cli` è anche l'harness di 4 ctest e di 8 script in `tools/` (più `tools/build_gto_cli.bat`), compresa la parità con GTO+ | [V] `apps/gto_cli/CMakeLists.txt:27-72`; grep `gto_cli` in `tools/` |
| 5 | Ambito del prodotto: un solo solver con preflop **e** postflop. Il codice postflop legacy non si rimuove senza una decisione di progetto con l'utente, che il 01/10 ha risposto "non ancora" | [V] memoria `preflop-product-target.md`; handoff `docs/handoff/NEXT_STEPS_2026-10-02.md:677-680` |
| 6 | **L'utente ha già scelto la web UI al posto della GUI desktop, e la web UI lancia `gto_cli`.** Il 02/10 ha deciso di eliminare `gto_gui` e i prototipi Qt e ImGui; il messaggio del commit dice che la web UI li sostituisce e lancia `gto_cli` | [V] commit `921f424` in `WT` (`git log 2aa24d8..c2e9138`), non ancora unito al branch principale (handoff T10). La decisione stessa non è registrata altrove [I] |
| 7 | Toglierlo farebbe risparmiare poco tempo di build: `postflop_solver.cpp` (21.274 righe) sta nella libreria `gtosd_postflop`, che si compila comunque per `libs/storage`, `libs/postflop_subgame` e i test (anche come oracolo nei test preflop). `gto_gui` era già fuori dalla build di default | [V] `libs/postflop/CMakeLists.txt:1-5`; `libs/storage/CMakeLists.txt:17`; `tests/CMakeLists.txt:269, 352, 600`; `CMakeLists.txt:14, 112-116` |
| 8 | Collegare la libreria al backend Python richiederebbe binding nuovi. In più, un crash o un esaurimento di memoria durante un solve da 1,5 GB farebbe cadere il web server, e il kill per albero di processi non si applicherebbe più | [I] |

**Cosa non va oggi.** `gto_cli` così com'è **non basta** per uno studio vero:

| Limite | Fonte |
|---|---|
| `postflop solve` usa sempre range uniformi al 100 % per entrambi i giocatori, su tutte le 630 combo. La config v1 non ha campi per i range. La libreria invece accetta già range pesati (`prepare_postflop_tree(config, ranges, …)`): il limite è solo del CLI | [V] `CLI:358`; `include/gtosd/tree/config.hpp:46-57`; `apps/gto_gui/product_window.cpp:1331` |
| Si ferma solo dopo un numero di iterazioni: non accetta un target di dEV. Anche qui il limite è solo del CLI: la richiesta di produzione della libreria accetta `target_normalized_nash_conv` oppure `target_normalized_max_deviation` (in alternativa) | [V] `CLI:314-316`; `include/gtosd/postflop/postflop_solver.hpp:413-415`; `libs/postflop/src/postflop_solver.cpp:17029-17033` |
| Nessun comando restituisce la strategia di un nodo per tutte le mani. `postflop query` risponde su 1 nodo × 1 combo e ricarica il checkpoint a ogni chiamata | [V] `CLI:435-461` |
| L'output è testo `key=value`, non JSON Lines | [V] `CLI:325-332, 368-377, 410-418` |
| Nessun bin set frozen contiene `gto_cli`: l'exe esiste solo in `out/build`, che la UI rifiuta | [V] `ls out/monker/bin*`; `assets.py:161-165` |
| La build Linux non è mai stata provata | [I] `gto_cli.md` §10.7 |
| Range pesati, target dEV e browser dei nodi esistevano solo nell'app desktop `gto_gui`, che chiama la libreria in-process. Con la sua rimozione (riga 6 sopra) non resterà nessun frontend per uno studio postflop completo: la web UI deve prenderne il posto passando da `gto_cli` | [V] `apps/gto_gui/product_window.cpp:1331, 1439-1441, 1782` (ancora presente in `REPO`; tolto in `WT` da `921f424`) |

**Raccomandazione**
1. Tenere `gto_cli`. La UI ne usa solo il sottoinsieme `postflop validate | estimate | solve | resume` più il file di
   controllo; lab, benchmark e `storage` restano fuori dalla UI.
2. Aggiungere, con una decisione dell'utente (QA2) e una build in finestra, tre comandi. Possono stare dentro `gto_cli`
   oppure in un eseguibile nuovo e snello:
   - un solve con range e target dEV;
   - eventi JSON Lines;
   - un worker `serve` per la vista dei nodi.
   Sono involucri sottili sulle funzioni di libreria che `gto_gui` chiamava (`prepare_postflop_tree` con i range,
   `resolve_postflop_production_options` con il target, `analyze_postflop_node`). Con la GUI desktop in rimozione **non sono
   facoltativi**: senza di loro la web UI non può sostituirla sul postflop. Il contratto proposto è in 4.12.
3. Non spetta a te scrivere quel C++ (prompt originale §11.4: `libs/` e `apps/`, salvo `apps/solver-ui`, sono in sola
   lettura). Tu costruisci UI e mock contro il contratto di 4.12; il C++ lo farà un'altra sessione dopo la decisione.
4. `gto_gui` **non** è un ripiego: è in rimozione per decisione dell'utente (riga 6). Il suo sorgente
   (`apps/gto_gui/product_window.cpp`, in `REPO` fino al merge T10 e poi nella storia git) resta il riferimento per i tre
   involucri e per la vista dei nodi.

---

## 2. Cosa è cambiato dal prompt originale

### 2.1 I cambi

| # | Cambio | Fatti | Conseguenza per la UI |
|---|---|---|---|
| C1 | **Fase 3a unita** (`238a41e`, 01/10): step 2 a 3 giocatori | Il trainer accetta 2 o 3 giocatori [V `TR:624-625`]. Con 3 giocatori servono `--iterations` ed `--eval-every 0`, senza `--eval-only` e senza `--certificate-out` [V `TR:627-642`]. Chart (`--chart-every`) e lock (`--lock-charts`) funzionano con 3 seat [V `TR:7-18`]. Opzioni nuove: `--pause-file` (stop giornaliero: solo checkpoint, verdetto `PAUSED`) e `--three-way-table` (default `<resources-dir>/preflop_three_way_v1.bin`). Opzioni di validazione da non esporre: `--checkdown`, `--validation`, `--three-seat-harness`, `--preflop-terminals`, `--hero-folded-shortcut`, `--boards-file`, `--canonical-river-boards`. Novità nell'output: blocco `start.phase3` [V `TR:880-908`], evento `pause_file` [V `TR:1269-1273`], verdetto `PREFLOP_BLUEPRINT_TRAIN=PAUSED` [V `TR:1551-1556`] | Nuovo caso di `step2`: 3 seat (sezione 5) |
| C2 | `compare_charts.py` esteso | Ogni chart ha `facing_all_in`. A livello globale ci sono `all_in_mean_distance`, `all_in_charts`, `non_all_in_mean_distance` e `non_all_in_charts` [V `FZ/tools/monker_compare/compare_charts.py:61-67, 192-197`] | Nuove metriche e una nuova regola d'arresto (5.4) |
| C3 | Runner continuo con default per 3 giocatori | Metrica `non_all_in`, soglia 0,008, minimo 16.000, tetto 48.000, `PAUSE` [V `R:30-60`] | Default del form 3-way |
| C4 | **Run 3-way 1 in corso** | Partito alle 00:09:30 del 02/10 in `OUT1`, guidato dalla coda congelata `Q` [V `OUT1/queue.log`, `run.log`] | Esempio vivo, **in sola lettura** (5.6) |
| C5 | **Fase 3b, parte A**: valori a policy fissa per seat | CLI `gtosd_preflop_blueprint_policy_values`, schema `gtosd.preflop_blueprint_policy_values.v1`. Il codice è in `WT`: non compilato, non committato [V `git -C WT status`]. Dati veri in `OUT1/part_a/` solo dopo la fine del run 1 (handoff T7) | Vista dei risultati della parte A (5.10), prima su fixture sintetiche |
| C6 | **Batteria di correttezza HU chiusa con 0 FAIL** (01/10, 16:28), **poi cancellata** con la pulizia dell'utente | Nessun formato cambia [V `docs/research/preflop_vector_cfr/MONKER_RECIPE_REPRODUCTION_2026-09-28.md` §10.13]. Il 02/10 alle 00:53 l'utente ha cancellato le run della batteria (`F:\GTO-Solver-out\out\monker\correctness`, gruppo `F_correctness`, 62,4 GB) e alle 00:55 `post_delete.sh` ha tolto le 34 junction rimaste [V `SP/cleanup/delete_data.log`, `SP/cleanup/post_delete.log`]. In `out/monker/correctness` restano solo `buckets/`, `independent/`, `smoke/`, i log `*.out` e `results_2026-09-30.md` [V `ls`]. Nella stessa pulizia sono stati cancellati i binari (`*.ckpt`, `policy.bin`, altri `*.bin` sopra 1 MB) delle varianti di `out/monker/variants`, tranne `HU50_m30x4_rake25`, `HU50_g1_rake` e `HU50_lock_all_m30x4_rake25` [V gruppo `F_variants_bin`, 64 file; handoff T15] | La root `out/monker/correctness` non contiene più run. La fixture `fixtures/runs/V1L` è ormai una delle poche copie dei file di V1L: non modificarla. Gli snapshot delle varianti senza `policy.bin` non offrono valutazioni (6, F6) |
| C7 | Batteria di correttezza 3-way | Solo proposta da scrivere (handoff T12) | Niente da fare per ora |
| C8 | **history7 rimossa** (decisione dell'utente del 01/10, circa 19:30) | Rimozione pronta in una sandbox, non ancora applicata. Il tag `history7-final` = `88118a6` segna l'ultimo albero che la contiene. Spariscono: `HistoryBucketRows`, la best response astratta esatta, gli eseguibili `abstract_br`, `history_rows` e `history_census`, l'opzione `--history-rows`, `tools/preflop_suite/*` e `benchmarks/suite/*` (tranne `fixtures/`). Spariscono anche le chiavi `history_map_resident_bytes` (evento `start`, `memory_breakdown`, JSON di certify), `"history_map"` (JSON delle chart) e `historyMapFingerprint` (`export --serve`) [V handoff T9, §9e] | Nessun parser deve richiedere quelle chiavi. **Non modificare** la fixture `fixtures/runs/V1L/train.jsonl`, che contiene ancora la chiave come dato |
| C9 | **Ritiro del gate "best response astratta ≤ 0,03 a"** | Era legato a history7. La NashConv fisica di HU50 non può fare da soglia: G1 sta a circa 0,78 a, il 26 % del pot, che è il pavimento dell'astrazione. Le soglie del prodotto sono una decisione aperta dell'utente [V handoff T16] | La linea dell'1 % del pot nei grafici HU resta un **riferimento**, non un verdetto. Niente badge "PASS/FAIL" sull'exploitability HU (QA7) |
| C10 | **Decisione dell'utente** (02/10, circa 00:15): una sola UI che instrada da sé verso il motore giusto | Postflop HU esatto → solver legacy (`gto_cli`). Preflop HU e 3-way → preflop blueprint. Più avanti, una modalità postflop multiway | Sezione 3 |
| C11 | **Ambito del prodotto** (01/10, circa 23:55) | Un solo solver, con preflop e postflop. Il codice postflop legacy non si toglie senza una decisione di progetto; l'unica eccezione decisa sono le GUI desktop (C13) | Il motore legacy entra nel prodotto (sezione 1) |
| C12 | Nuovo bin set `out/monker/bin_3way_step2` | Contiene `gtosd_preflop_blueprint_train.exe` (sha256 `12c0dd6b…`) e `gtosd_preflop_blueprint_monker_tree.exe` (`27450586…`), build di `12fe441f`. Ha un `SOURCE.txt` con le sha256 nel formato `<hex> *<file>`, ma non ha `README.txt` né `SHA256SUMS.txt` [V `ls`, `cat`] | Oggi non è registrabile (`assets.py:62-63`): QA3. Pianificato `bin_3way_partA` (handoff T6) |
| C13 | **GUI desktop in rimozione** (decisione dell'utente del 02/10) | In `WT` i commit `921f424`, `1591a10`, `797a7d4` e `c2e9138` (00:23-00:25 del 02/10) tolgono `apps/gto_gui`, `apps/gui_qt_prototype`, `apps/gui_imgui_prototype`, il preset `windows-gui-release` e la feature vcpkg delle GUI. Il messaggio di `921f424` dice che la web UI le sostituisce e lancia `gto_cli`. Arriveranno nel branch principale con il merge T10 dell'handoff [V `git log` in `WT`; handoff §4, T10] | La web UI diventa l'unico frontend del prodotto: il motore postflop deve passare da `gto_cli` (sezione 1) |

### 2.2 Frasi del prompt originale superate

| Prompt originale | Dice | Ora vale |
|---|---|---|
| §2, riga 126 | step 2 "Solo heads-up" | 2 o 3 giocatori (C1) |
| §2, riga 130 | "3-way step 2 e 6-way non sono addestrabili oggi" | Il 3-way sì; da 4 a 6 giocatori ancora no |
| §2, riga 135; §8.2 "linea del target" | target 0,03 a in HU | Riferimento visivo, non gate (C9) |
| §4.6, riga 336 | `gto_cli` e `gto_gui` "non c'entrano con questo lavoro e non vanno toccate" | `gto_cli` diventa un motore lanciato dalla UI: puoi leggerne il sorgente e lanciare il suo binario **congelato**. Il suo C++ resta in sola lettura. `gto_gui` e i due prototipi sono in rimozione (C13) |
| §4.4, `browse_roots` | `out/monker/correctness` piena di run; manca `step2_3way` | `correctness` non ha più run (C6): la si può togliere dall'esempio o lasciare, la discovery deve tollerarla vuota. Si aggiunge `out/monker/step2_3way` |
| App. D.2 | righe reali da `out/monker/correctness/V1L/train.jsonl` | Il file non esiste più (C6); resta la copia nella fixture `fixtures/runs/V1L` |
| §5.2, tabella dei kind | `step2` con 2 giocatori | `step2` con 2-3 giocatori. `evaluation` (`monker_values`) resta solo HU [V `benchmarks/preflop_blueprint_monker_values.cpp:654-655`] |
| §8.1, punto 1 | "`step2` ed `evaluation` solo HU" | Vedi la riga sopra |
| §12, "Solo HU" | `--chart-every`, lock del trainer e trainer sono solo HU | Restano solo HU l'evaluator, `--eval-every > 0`, `--eval-only` e `--certificate-out` |
| App. A.1 | stati di successo del trainer | Si aggiunge `PAUSED` (exit 0, ripartibile) |
| App. A.2 | "(step 2, solo HU)"; `--chart-every` e `--lock-charts` "solo HU" | 2-3 giocatori; più `--pause-file` e `--three-way-table` |
| App. E.4 | chiavi di `compare_charts.py` | Più quelle di C2 |
| §5.14 | riconoscimento da `step 2 continuous start:` | Esiste anche la variante dettagliata con `metric … min … players 3` (5.7.2) |
| §11.4, riga 1152; App. A.2, riga 1359; App. A.8, riga 1467 | `tools/preflop_suite`, `--history-rows`, `abstract_br` | Esistono solo fino al tag `history7-final` (C8) |
| App. H.1, riga 1941 | "3-way step 2 (pianificato, non esiste)" | Misure reali in 5.5 |
| App. G, bin set | manca `bin_3way_step2` | C12 |

### 2.3 Stato della macchina e del programma (02-03/10)

**02/10: giorno libero, senza finestre.** Il run 1, però, occupa la macchina.
- Il trainer ha un working set di circa 14,8 GB e 8 thread [V `OUT1/train.jsonl`, `memory_breakdown`].
- Dopo il run 1 il programma del solver ha la precedenza (handoff T3-T11): parte A, rimozione di history7, test del blocco.
- **Niente test nativi né build** senza un OK esplicito dell'utente (QA5).

**Dal 03/10: finestre normali.**
- Run 00:00-20:00, build e test fino alle 21:00.
- La coda del run 1 crea `PAUSE` alle 19:40 e riprende alle 00:00.
- Per ripartire, la coda vuole **18.874.368 KB liberi** e **zero** processi `gtosd_preflop_blueprint_train` vivi
  [V `Q:38`, `Q:139-142`, `Q:209`, `queue.log`]. Un trainer reale lanciato dalla UI a mezzanotte terrebbe chiuso il gate:
  il run 1 non riprenderebbe finché quel trainer non esce.

**Cosa puoi fare mentre il run 1 gira.** Scrivere codice e far girare i test in mock (D5), con quattro cautele (handoff R8):
- almeno 4 GB liberi e nessun tuo processo oltre i 2 GB;
- la sera non lasciare aperti Chromium di Playwright, dev server o backend di prova: a mezzanotte toglierebbero alla coda la
  memoria che le serve;
- non tenere aperti handle sui file del run (5.6);
- le righe di comando dei tuoi processi (backend, mock, test) e i run id non devono contenere nomi di config o di coda
  (`3WAY50`, `queue_3way50_15x4`, …): le guardie di altre sessioni uccidono per pattern di riga di comando (prompt
  originale §11.5).

---

## 3. Registro dei motori e instradamento

### 3.1 Principio

- **Un solo flusso "Nuovo studio".** L'utente sceglie il tipo di studio rispondendo a domande di dominio: giocatori, punto di
  partenza, profondità. **Non sceglie mai il motore.**
- **Il backend instrada** con una funzione pura e deterministica, coperta da test di tabella:
  `route(StudyRequest) -> (engine, kind)`.
- **La preview mostra il motore scelto** con un nome di prodotto ("Motore preflop", "Motore postflop esatto HU"), insieme a
  passi, stime e admission. L'argv esatto resta nel backend, nell'audit e in `meta.json` (revisione del 01/10).
- **Le azioni su una run esistente non sono tipi di studio.** Valutazione, tree check, confronto e parte A si offrono da
  Risultati e Monitor sulla run di partenza, e il motore è quello della run.

### 3.2 Tabella di instradamento

Nella tabella, `StudyRequest = {players: 2..6, start: "preflop"|"flop"|"turn"|"river", depth: "quick"|"full"}`.

| Giocatori | Partenza | Profondità | Motore | Kind | Oggi in mock | Oggi in reale |
|---|---|---|---|---|---|---|
| 2 | preflop | rapido (showdown esatto, minuti) | `preflop_blueprint` | `step1_hu` | sì | solo con `checkdown` registrato (D3) |
| 2 | preflop | completo (postflop astratto) | `preflop_blueprint` | `step2` (2 seat) + `evaluation` | sì | sì, con `c123` |
| 3 | preflop | rapido | `preflop_blueprint` | `step1_classes` | sì | solo con `checkdown_classes` registrato (D3) |
| 3 | preflop | completo | `preflop_blueprint` | `step2` (3 seat) + confronto degli snapshot; parte A dopo (5.10) | da E3 | dopo QA3, con un OK e a macchina libera |
| 4-6 | preflop | — | `preflop_blueprint` | solo `validate` | sì | sì |
| 2 | flop, turn o river | sempre esatto | `postflop_exact` | `postflop_solve` | da E4 | dopo QA3, con il bin set di `gto_cli` |
| ≥ 3 | flop, turn o river | — | `postflop_multiway` (**riservato**) | — | disabilitato | disabilitato: "non disponibile: motore non ancora progettato" |

**Note**
- Un motore o un kind senza tool registrati appare disabilitato, con il motivo. È la regola di §8.1 del prompt originale,
  estesa ai motori.
- **Postflop HU**: le posizioni sono fisse, CO (OOP) contro BTN (IP) [V `libs/tree/src/config.cpp:233-235`; chiavi
  `ev_co_antes`/`ev_btn_antes` di `CLI:181-184`; OOP/IP da `gto_cli.md` §5]. Il form le mostra e non le chiede.
- **Profondità**: per il postflop esatto il campo non esiste (il motore enumera sempre turn e river).
- **Numero di chart**: 54 vale solo per l'albero 3WAY50 donk. Altri alberi 3-way (`3WAY100_donk`, …) ne hanno un numero
  diverso: non cablarlo.

### 3.3 Interfaccia `Engine`

Sostituisce i rami sparsi oggi in una decina di file (tabella 3.4). Admission, finestre, supervisor, auth, sandbox e store
sono già indipendenti dal motore e restano come sono [V `ui_map.md` §8.3].

| Hook | Ruolo | `preflop_blueprint` | `postflop_exact` | `postflop_multiway` |
|---|---|---|---|---|
| `id`, `label` | identità e nome di prodotto | "Motore preflop" | "Motore postflop esatto HU" | riservato |
| `kinds(bin_set)` | kind con giocatori, `resumable`, `long_running`, `real_allowed` e motivo | quelli di oggi più `step2` a 3 seat | `postflop_solve` | nessuno |
| `validate(spec)` | regole di kind e seat (oggi in `models.py:187-209`) | come oggi, con i cambi di 5.1 | 4.3 | — |
| `plan(spec, assets, run_dir)` | elenco degli step con argv (oggi `builders.py` più `scheduler.py:266-282`) | argv identici a oggi; B.5 per 3 seat | 4.4 | — |
| `estimate(spec, cache)` | RAM, disco e tempo, con la fonte | oggi `scheduler.py:172-229`; 5.5 per 3 seat | da `postflop estimate` (4.8) | — |
| `log_files(step)` | file di stdout e stderr | `train.jsonl` / `train.stderr.log`, … | `solve.log` / `solve.stderr.log` | — |
| `parse_line(stream, bytes)` | da riga a evento normalizzato | JSONL più verdetto `PREFLOP_BLUEPRINT_*` | righe `k=v` (4.5) | — |
| `started(step, events)` | guardia d'avvio | evento `start` | header `GTOSD_POSTFLOP_SOLVE_1`, oppure una riga `solver_phase=` su stderr | — |
| `startup_timeout_s(step)` | oggi fisso a 15 s (`scheduler.py:878-886`) | 15 s, più al resume il caricamento del checkpoint (dimensione di `state.ckpt` / 50 MB/s): il trainer carica `state.ckpt` **prima** di stampare `start` [V `TR:804-810, 850-919`] (F8) | ≥ 120 [I: la preparazione dell'albero precede l'header], più lo stesso termine per il checkpoint al resume | — |
| `long_running(step)` | esenta lo step da `short_job_timeout_s` | training, evaluation, step 1 | solve | — |
| `verdict(step, out_tail, err_tail, exit)` | esito: done / paused / cancelled / failed / blocked, con il testo | 5.3 | 4.5.5 | — |
| `request_stop(run, step, reason)` | `reason` ∈ {`user_pause`, `window`, `rule`, `finish`, `cancel`} | file `STOP` o `PAUSE` (5.3) | file `<checkpoint>.control` (4.7) | — |
| `resumable(run_dir)` | resume possibile | `state.ckpt` presente | `state.ckpt` presente | — |
| `detect(path)` / `details(path)` | discovery di run esterne | quella di oggi più 3-way (5.7) | `report.json` più `solve.log` | — |
| `progress(events)`, `curve(run)` | avanzamento ed ETA; righe della curva | da `train.jsonl`, `values.json` e `vs_*.json` | dalle righe `progress` (4.6) | — |
| `results()` | provider dei risultati | chart a 81 classi | riepilogo e curva (E4); nodi via `serve` (E6) | — |
| `match_external(proc)` | riconosce un processo non gestito e ne stima i thread | `gtosd_*`, `--threads` | nome `gto_cli`, 8 thread fissi | — |

Il registro è `ENGINES = {"preflop_blueprint": ..., "postflop_exact": ..., "postflop_multiway": Reserved()}`. Scheduler,
adapter e API chiamano soltanto l'interfaccia.

### 3.4 Punti d'innesto nel codice esistente

Righe [V]: `ui_map.md` §3.5, §8.2, §9.2 e controlli a campione di questa sessione. I cambi indicati sono progetto [I].

| # | File:righe | Oggi | Cambio | Milestone |
|---|---|---|---|---|
| P1 | `models.py:131-209` | `RunSpec.kind` è un `Literal`; c'è solo `game: GameConfig` (preflop); regole dei seat a `:190-193` | Introdurre `StudySpec` come unione discriminata su `engine`: `PreflopRunSpec` (quello di oggi, più `stop_metric`, `min_iterations`, `pause_at_window_end` e `policy_snapshot_reserve_gb`) e `PostflopRunSpec` (4.3). Le regole di kind e seat passano in `Engine.validate` | E1, E3, E4 |
| P2 | `assets.py:11-14` | `TOOLS` cabla gli stem `gtosd_preflop_blueprint_*` | Catalogo dei tool per motore | E1 |
| P3 | `assets.py:58-82` | Due soli percorsi di bin set; servono `README.txt` e `SHA256SUMS.txt`; regex `([a-fA-F0-9]{64}) \*(\w+\.exe)` a `:66`; la capacità "street di puntata" è cablata sul nome `c123` (`:81`) | Bin set definiti nel TOML (3.7); regex `^([0-9a-fA-F]{64}) \*([A-Za-z0-9_.+-]+)$` (nomi senza estensione su Linux, DLL); le righe che non corrispondono (intestazione di `SOURCE.txt`) si saltano | E0, E1 |
| P4 | `assets.py:131-168` | `tool()` risolve un solo exe | Risolve l'exe e i file che lo accompagnano (le DLL di `gto_cli`); verifica **tutti** gli sha256 prima dello spawn; continua a rifiutare `out/build` (`:161-165`) | E1, E4 |
| P5 | `builders.py:7-187` | un'unica catena di `if`; `--policy-out` solo in `continuous` (`:164-165`); `--policy-snapshots` solo se `evaluation_policy != "none"` (`:166-173`) | `PreflopEngine.plan()` con argv identici byte per byte (golden test). Aggiunte per 3 seat: `--pause-file`, `--policy-snapshot-reserve-gb`, `--three-way-table` (5.2); `--policy-snapshots` legato a `policy_snapshot_every`, perché con 3 seat non c'è valutazione ma le policy servono alla parte A. `--three-way-table` **solo** con 3 seat: con 2 il trainer esce con errore [V `TR:643-648`] | E1, E3 |
| P6 | `scheduler.py:172-229` | 0,65 s per iterazione (`:223`, `:944`); RAM = 1,1 × (34 B × celle) + 0,15 GB (`:201`, `:221`); snapshot di policy nel disco solo se `evaluation_policy != "none"` (`:211`) | `Engine.estimate`; misure dei 3 seat (5.5), dove la formula di oggi dà circa 31,8 GB contro 14,91 GB misurati e, senza valutazione, dimentica circa 20 GB di snapshot; postflop da `postflop estimate` | E1, E3, E4 |
| P7 | `scheduler.py:238-299` | filtro dei kind reali a `:242-243`; espansione degli step a `:266-282` | `Engine.kinds(bin_set)` e `Engine.plan`; nessuno step di valutazione con 3 seat | E1, E3 |
| P8 | `scheduler.py:355-404` | `freeze`; hash delle due resources a `:386-404`, con codice proprio: `filesystem.sha256_file` rifiuta i `.bin` (`filesystem.py:86-89`) e `ARCHITECTURE.md:26-27` limita l'eccezione ai due file | Aggiungere `preflop_three_way_v1.bin` (21 MB) alla stessa eccezione esplicita e aggiornare `ARCHITECTURE.md`: hash al congelamento, dimensione e mtime a ogni resume. Postflop: `inputs/postflop_config.json` | E3, E4 |
| P9 | `scheduler.py:409-445` | `protect_snapshots` / `restore_snapshots` | Solo `preflop_blueprint` (il postflop non ha snapshot) | E1 |
| P10 | `scheduler.py:447-539` | substep a `:488-494`; file di log a `:495-503` | `Engine.log_files`, substep per motore | E1 |
| P11 | `scheduler.py:541-607` | Stop scrive `STOP` (`:562`); resume solo se `kind == "step2"` (`:586`) | `Engine.request_stop(reason)`, `Engine.resumable` | E1, E3, E4 |
| P12 | `scheduler.py:609-738` | verdetto dall'ultima riga `PREFLOP_BLUEPRINT_` (`:615-623`); `=STOPPED` → paused (`:674`) | `Engine.verdict`; `PAUSED`; testo del FAIL letto da stderr; riga `status=` del postflop | E0, E1, E3, E4 |
| P13 | `scheduler.py:740-767` | stop rule su `overall_mean_distance` | Campo configurabile, `min_iterations`, controllo che la spaziatura sia `chart_every` (5.3) | E3 |
| P14 | `scheduler.py:769-968` | guardia d'avvio di 15 s (`:878-886`, difetto F8); timeout dei job brevi a `:866-873` (difetto F1, sezione 6); STOP di fine finestra a `:874-877` | `Engine.started`, `startup_timeout_s`, `long_running`; a fine finestra `request_stop(reason="window")` | E0, E1 |
| P15 | `tailer.py:20-67` | solo `train.jsonl` (`:24-25`) | Parser per motore; ammettere `solve.log` e `solve.stderr.log` (righe `k=v`) e il log di avanzamento della parte A (JSON su stderr) | E1, E4, E5 |
| P16 | `adapters.py:30-68, 133-405` | `progress`, `discover`, `details` (kind `:210-218`, seat `:219-221`, stato `:230-249`), `snapshots`, `values`, `curve` | Un adapter per motore; adapter 3-way con la coda (5.7, 5.8); adapter postflop | E2, E4 |
| P17 | `charts.py:104-262` | copia congelata di `compare_charts.py` con sha sui byte grezzi (`:107-119`) | sha sul contenuto con fine riga LF; copia per run conservata (F5); medie all-in / non all-in | E0, E2 |
| P18 | `telemetry.py:47-87` | processi esterni solo con prefisso `gtosd_` (`:54`); thread da `--threads` (`:58-68`) | `Engine.match_external`: `gto_cli` conta 8 thread. Le code bash (`queue_*.sh`) si **mostrano** soltanto | E0 |
| P19 | `api.py:183-194, 256-276, 414-508`; allowlist dei log a `:485-492` | API solo preflop | Rotte di 3.5; allowlist più ampia (3.5) | E1-E5 |
| P20 | `config/solver-ui.example.toml:19-28` | `browse_roots` | Aggiungere `out/monker/step2_3way`; `correctness` ormai senza run (C6): toglierla o lasciarla, purché la discovery la tolleri vuota; sezione `[[bin_sets]]` (3.7) | E0 |
| P21 | `frontend/src/Launch.tsx:6-7, 167-185`; `Results.tsx:127-140, 297-299`; `Monitor.tsx:166-172`; `domain.ts:130-136, 161-179`; `main.tsx:115-129` | un solo form preflop; matrice 9×9 preflop | Procedura guidata "Nuovo studio" (3.6); schede 3-way; vista postflop; badge del motore | E1-E5 |
| P22 | `mock/mock_gtosd.py` | flag e verdetti preflop, solo HU nello step 2 | Blocco `phase3`, eventi `pause_file`, `PAUSED`, snapshot a 3 seat; nuovo `mock/mock_gto_cli.py` (4.11) | E3, E4 |
| P23 | `fixtures/runs/` | niente step 2 3-way, niente postflop | Fixture 3-way (5.6) e postflop (4.11) | E2, E4 |

### 3.5 API: rotte nuove e modifiche

| Metodo | Percorso | Scopo | Milestone |
|---|---|---|---|
| GET | `/api/engines` | Motori, kind, disponibilità in mock e in reale, motivi | E1 |
| POST | `/api/runs/preview` | Il body diventa `StudySpec` (unione per `study_type`). La risposta aggiunge `routing: {engine, kind, label, reason}`. Il `RunSpec` preflop di oggi resta accettato come `study_type` preflop | E1 |
| POST | `/api/postflop/configs/validate` | Job breve: `postflop validate`, poi `postflop estimate` (4.4). Esito via SSE o `GET /api/short-jobs/{id}` | E4 |
| GET | `/api/runs/{id}/postflop/summary` | Riepilogo da `report.json` | E4 |
| GET | `/api/runs/{id}/curve` | Stessa rotta di oggi; le righe dipendono dal motore (4.6, 5.4) | E2, E4 |
| GET | `/api/runs/{id}/queue` | Stato della coda esterna 3-way (5.7.1); 404 sulle run gestite | E2 |
| GET | `/api/runs/{id}/snapshots/{it}/compare?with=previous` | Legge `vs_previous.json`. Se manca, lo calcola in memoria con la copia congelata e lo mette in cache sotto `data_root/cache`. **Non scrive mai nella run** | E2 |
| GET | `/api/runs/{id}/part-a` | Riepilogo di `part_a/values_*.json` e `report_*.json` (5.10) | E5 |
| POST | `/api/runs/{id}/postflop/session` | Apre un worker `serve`: passa dall'admission e richiede CSRF. Una GET non può lanciare processi | E6 |
| GET | `/api/runs/{id}/postflop/node?session=&path=&board=` | Interroga il worker aperto | E6 |
| POST | `/api/runs/{id}/postflop/session/close` | Chiude il worker (EOF su stdin; kill per identità se non esce) | E6 |
| SSE | `/api/stream` | Si aggiungono `step.updated` e `log.append`, che mancano ancora [V `ui_map.md` §3.2] | E1 |

**Allowlist dei log** (oggi `api.py:485-492`, che ammette già `solve.log` e `solve.stderr.log`). Si aggiungono `queue.log`,
`runner_round<N>.log`, `report.json`, `report.md`, `part_a/cli_<n>.log` e `part_a/cli_<n>.progress.log`. Restano negati
`state.ckpt*`, `*.bin`, `*.control` e `*.tmp`.

### 3.6 Frontend: "Nuovo studio"

1. **Tipo di studio.** Tre domande:
   - "Quanti giocatori?";
   - "Da dove parte lo studio?" (preflop / flop / turn / river);
   - per il preflop, "Rapido o completo?".
   Le schede non disponibili restano visibili, disabilitate, con il motivo preso da `/api/engines`.
2. **Gioco o spot.**
   - Preflop: il form di oggi. Per 3 seat, i default di 5.1.
   - Postflop HU: form di 4.3, con un selettore di board (3 carte più turn e river facoltativi, mazzo corto 6-A), pot e stack
     in ante, rake, size per street, giocatore e scenario, iterazioni. Preset: "solo check" (la fixture
     `tests/fixtures/postflop_check_only.json`).
3. **Risorse e programmazione**: come oggi (thread, priorità, finestre).
4. **Preview.** Mostra:
   - un'etichetta "Motore: …";
   - passi, stime con la loro fonte, esito dell'admission e note di riproducibilità;
   - per il postflop di E4, il banner "Range uniformi al 100 %: è un test del motore, non uno spot reale".
5. **Conferma.**

**Monitor e Libreria.** Mostrano il badge del motore e permettono di filtrare per motore. **Risultati** sceglie la vista in
base al motore della run.

### 3.7 Modello dei dati, bin set e migrazione

- **Record di run.** Aggiungi il campo `engine`.
  - Un record vecchio senza il campo si legge come `preflop_blueprint`.
  - `meta.json` delle run gestite passa allo schema `gtosd.solver_ui.run.v2`.
  - Le run scoperte non si toccano mai.
- **Unità.** Dichiara un'unità sola per la RAM (GiB oppure GB) e usala ovunque: nel TOML, nelle stime e nei messaggi. Oggi
  `ram_per_run_gb` non dice quale delle due sia.
- **Bin set** nel TOML, al posto dei due percorsi cablati:

```toml
[[bin_sets]]
id = "c123"
engine = "preflop_blueprint"
path = "out/monker/bin_correct/c123"
manifest = "SHA256SUMS.txt"            # righe "<hex> *<file>"
readme = "README.txt"
capabilities = { train = ["stop_file"] }               # niente --pause-file: build di 68cf367 (30/09, README.txt); l'opzione arriva con 4e4bce3 (01/10) [V]

[[bin_sets]]                           # solo dopo QA3(a)
id = "threeway_12fe441"
engine = "preflop_blueprint"
path = "out/monker/bin_3way_step2"
manifest = "SOURCE.txt"                # stesso formato di riga; l'intestazione porta il commit
capabilities = { train = ["stop_file", "pause_file", "three_seats", "policy_out"], monker_tree = [] }

[[bin_sets]]                           # solo dopo QA3(b): la cartella non esiste ancora
id = "postflop_<commit>"
engine = "postflop_exact"
path = "out/monker/bin_postflop_<commit>"
manifest = "SHA256SUMS.txt"
readme = "README.txt"
files = ["gto_cli.exe", "libsodium.dll", "sqlite3.dll", "zstd.dll"]
capabilities = { gto_cli = ["postflop_validate", "postflop_estimate", "postflop_solve", "postflop_resume", "control_file"] }
```

- **Regole dei bin set**
  - Ogni file elencato nel manifest si verifica **prima di ogni spawn**, DLL comprese. Le tre DLL di `gto_cli` sono caricate
    in modo ritardato e servono solo ai comandi `storage` [V `apps/gto_cli/CMakeLists.txt:15-24`]: si congelano e si
    verificano comunque, perché un sottocomando le può caricare.
  - Il congelamento verifica anche le dipendenze di runtime (per esempio con `dumpbin /dependents`): le ctest di `gto_cli`
    girano con la cartella del compilatore MSVC in testa al `PATH` [V `CMakeLists.txt:57-62`], quindi non provano che l'exe
    parta da una cartella frozen senza quel `PATH` [I]. È un compito della sessione del solver (QA3).
  - Le capacità non si sondano mai con probe automatici. Si dichiarano a mano nel TOML oppure, in finestra e con l'OK
    dell'utente, con probe innocui (Q12 del prompt originale).
  - Su Linux il nome è `gto_cli`, senza estensione; le librerie condivise sono da scoprire [I].
  - Non scrivere mai dentro `out/monker/bin*`. Creare un bin set nuovo è un compito della sessione del solver, dopo QA3.

---

## 4. Adattatore `gto_cli` (motore `postflop_exact`)

### 4.1 Contratto di oggi [V `CLI`]

- **Sottocomandi ammessi alla UI**: `postflop validate`, `postflop estimate`, `postflop solve` e `postflop resume`.
- **Mai lanciati dalla UI**:
  - `postflop pause|cancel`: il backend scrive da sé il file di controllo (4.7);
  - `postflop query`: troppo lento, un processo per (nodo, combo);
  - `postflop certify`: "should", dopo E4;
  - `storage`, lab e benchmark.
- **Gioco.** Short deck HU postflop, CO (OOP) contro BTN (IP). La radice è sul flop, oppure su un turn o un river fissati.
- **Esattezza.** Turn e river sono enumerati esattamente: niente campionamento e niente bucket [V `docs/specifications/LIMITATIONS.md:5-9`].
- **Algoritmo.** ProductionDcfr; certificazione ogni 20 iterazioni, con salvataggio del checkpoint a ogni certificazione;
  8 thread fissi (7 worker più il thread principale) [V `include/gtosd/postflop/postflop_solver.hpp:407-419`;
  `libs/postflop/src/postflop_solver.cpp:4503-4506, 19848`]. Sul server di produzione (104 thread) un solve resta a 8
  thread: si guadagna solo facendo girare più solve insieme [I].
- **Backend di memoria.** Solo LazyInRam, tutto in RAM: se il picco stimato supera `ram_gib`, il solve esce con 3 [V `CLI:283-306`].
- **Range.** Uniformi al 100 % (sezione 1). Finché non esiste E6, ogni studio postflop è **un test del motore**, e la UI lo
  scrive (banner).

### 4.2 Bin set e capacità

- Oggi non ne esiste uno (QA3(b)). L'exe `out/build/windows-release-suite/apps/gto_cli/gto_cli.exe` è del 28/09 18:11 e ha
  accanto le tre DLL caricate in modo ritardato [V `ls`; `apps/gto_cli/CMakeLists.txt:15-24`]. Sta in `out/build`, quindi è
  vietato.
- Probe innocuo per il congelamento (in finestra e con OK, Q12 del prompt originale): `gto_cli` senza argomenti stampa
  l'uso ed esce con 0; con argomenti sconosciuti esce con 2 [V `CLI:4353-4354`].
- Capacità di oggi: `postflop_validate`, `postflop_estimate`, `postflop_solve`, `postflop_resume`, `control_file`.
- Capacità di E6 (non esistono ancora): `study_solve`, `jsonl_events`, `serve`.

### 4.3 Input: `PostflopConfig` (JSON `version: 1`, scritto dal backend)

**Il parser C++ ignora le chiavi sconosciute**: controlla solo con `contains` [V `libs/tree/src/config.cpp:108-200, 294-400`].
Come per il preflop, il validatore della UI deve **rifiutarle**.

| Campo | Tipo e vincoli | Fonte | Nel form |
|---|---|---|---|
| `version` | intero `1` | [V `config.hpp:46-48`; `config.cpp:299-307`] | fisso |
| `flop` | array di esattamente 3 stringhe di 2 caratteri: rank in `6789TJQKA` (maiuscole), seme in `cdhs` (minuscole), come `"As"`; tutte distinte | [V `libs/core/src/cards.cpp:14-27`; `config.cpp:308-323, 249-251`] | selettore di carte |
| `turn`, `river` | facoltativi oppure `null`, stessa sintassi, distinti dal flop; `river` richiede `turn` | [V `config.cpp:324-343, 246-248`] | selettore di carte |
| `initial_pot_units`, `effective_stack_units` | interi JSON > 0, in units (1 a = 10.000 units) | [V `config.cpp:15-25, 345-352, 243-245`; `include/gtosd/core/money.hpp:43`] | ante decimali × 10.000; i valori non interi si rifiutano |
| `rake` | `{enabled: bool, percentage_bp: intero 0..10.000, cap_units: intero ≥ 0, no_flop_no_drop: bool, minimum_pot_units: intero ≥ 0}`, tutti obbligatori | [V `config.cpp:354-372`; `libs/core/src/money.cpp:10-18, 33-38`] | % → basis points, ante → units |
| `streets.{flop,turn,river}.{co,btn}.{lead,after_check,facing_bet}` | tutti e 18 obbligatori | [V `config.cpp:374-403`] | griglia street × giocatore × scenario, con preset |
| scenario `sizes_bp` | array con al massimo 3 interi, ciascuno 0..100.000 bp | [V `config.cpp:115-116, 27-36`; `money.cpp:22-30`] | fino a 3 size in % del pot |
| scenario `raise_depth` | intero 0..4 | [V `config.cpp:118-125`] | |
| scenario `all_in_mode` | `disabled` \| `add` \| `go` | [V `config.cpp:49-64`] | |
| scenario `all_in_threshold_bp` | obbligatorio, intero 0..100.000 bp | [V `config.cpp:110-112, 185`] | |
| scenario `minimum_bet_units` | obbligatorio, intero **> 0** | [V `config.cpp:186, 268-270`] | |
| facoltativi | `all_in_strict_boundary` (default true); `sizes_by_raise_count_bp`: se presente, **esattamente `raise_depth` liste** da 1 a 3 elementi; `aggressive_target_rounding {mode: nearest\|down\|up, bands[{upper_bound_exclusive_units, quantum_units}]}` con almeno una banda, `quantum_units` > 0, limiti superiori strettamente crescenti e `0` (illimitato) solo sull'ultima | [V `config.cpp:136-193, 258-264, 271-283`] | sezione "avanzate" |

- Il backend scrive `<run>/inputs/postflop_config.json` partendo dal modello validato. Un JSON caricato dall'utente passa
  dallo stesso validatore.
- Il controllo autorevole è il job breve `postflop validate`, seguito da `estimate` (4.4).
- **Test incrociato**, solo in finestra e con OK: un campione di mutazioni (4 size, `raise_depth` 5, carta doppia, rank `2`,
  seme maiuscolo, `turn` assente con `river` presente, `minimum_bet_units` 0, `sizes_by_raise_count_bp` con un numero di
  liste diverso da `raise_depth`) deve essere accettato o rifiutato **come dal tool reale**. Gli errori del parser hanno 10
  nomi fissi (`invalid_json`, `unsupported_version`, `missing_field`, `invalid_card`, `duplicate_card`, `invalid_money`,
  `invalid_percentage`, `too_many_sizes`, `invalid_raise_depth`, `invalid_configuration`), stampati da
  `postflop validate failed: <nome>`: il form li traduce [V `config.cpp:461-484`; `CLI:3660-3666`].

### 4.4 Builder dell'argv

L'argv è posizionale, con percorsi nativi assoluti, `cwd` = run dir e interi decimali senza zeri iniziali. Gli argomenti
numerici si parsano con `parse_u64` [V `CLI:254-256`].

| Step | argv | Note |
|---|---|---|
| validate | `[<bin>/gto_cli, postflop, validate, <run>/inputs/postflop_config.json]` | job breve |
| estimate | `[<bin>/gto_cli, postflop, estimate, <cfg>, <ram_gib>, <disk_gib>]` | job breve; `ram_gib` intero ≥ 1 (limite per run); `disk_gib` intero |
| solve | `[<bin>/gto_cli, postflop, solve, <cfg>, <N>, <run>/state.ckpt, <run>/report, <ram_gib>, <disk_gib>]` | **non passare** `cert_interval`: se presente deve valere 20, altrimenti exit 2 [V `CLI:257-271`]. `N` ≥ 1 |
| resume | come solve, con `resume` | `N` è il **totale assoluto**: deve essere ≥ delle iterazioni già fatte, altrimenti `postflop solve failed: checkpoint_mismatch` ed exit 1; lo stesso errore arriva se la config è cambiata [V `libs/postflop/src/postflop_solver.cpp:19070-19083, 21262-21263`]. Si fa solo se `state.ckpt` esiste. Config e range non cambiano dopo il primo step |

`report` è un prefisso: il CLI scrive `<run>/report.json` e `<run>/report.md` [V `CLI:147-236`].

### 4.5 Parsing degli output

#### 4.5.1 stdout di `validate` ed `estimate` [V `CLI:3660-3728`]

```
GTOSD_POSTFLOP_VALIDATE_1
status=valid exact_outcomes=true bucketing=false
nodes=<n> edges=<n> decision_nodes=<n> chance_edges=<n>

GTOSD_POSTFLOP_ESTIMATE_1
exact_outcomes=true bucketing=false
nodes=<n> infosets=<n> actions=<n>
lazy_peak_bytes=<b> out_of_core_peak_bytes=<b> out_of_core_backing_bytes=<b>
ram_budget_bytes=<b> disk_budget_bytes=<b> selected_backend=lazy-in-ram|out-of-core|rejected
```

- `estimate` esce con 3 se nessuno dei due backend ci sta, e scrive su stderr `postflop estimate rejected: insufficient_ram_or_disk`.
- **Attenzione**: il solve usa solo LazyInRam. Se `selected_backend=out-of-core`, il solve fallisce comunque con exit 3.
  L'admission deve guardare **solo** `lazy_peak_bytes`.

#### 4.5.2 stdout di `solve` / `resume` [V `CLI:325-332, 368-377, 410-418`]

```
GTOSD_POSTFLOP_SOLVE_1
backend=lazy-in-ram exact_outcomes=true bucketing=false production_profile=1.0 algorithm=production_dcfr state_precision=scaled_uint16_regret_strategy target_iterations=<N> certification_interval=20 parallel_action_depth=7
progress iteration=<i> ev_co_antes=<x> ev_btn_antes=<x> br_co_antes=<x> br_btn_antes=<x> nash_conv_antes=<x> normalized_nash_conv=<x>
...
status=completed|paused|cancelled completed_iterations=<i> checkpoint=<path> report_json=<p>.json report_markdown=<p>.md peak_rss_bytes=<b> elapsed_seconds=<s> build_to_ready_seconds=<s> solve_to_consultable_seconds=<s> build_to_consultable_seconds=<s>
```

- L'header esce dopo il controllo della RAM, il caricamento del checkpoint (al resume) e la preparazione dell'albero, quindi
  può arrivare tardi su alberi grandi [V ordine in `CLI:273-377`; I sulla durata].
- C'è una riga `progress` a ogni certificazione: ogni 20 iterazioni, più una a fine run e una a ogni pausa. È scritta con
  `std::endl`, quindi esce subito. A ogni certificazione il CLI salva anche il checkpoint: scrive `state.ckpt.tmp` e poi lo
  sostituisce in modo atomico [V `CLI:334-338`; `postflop_solver.cpp:19848, 20856-20859, 20912`].
- `converged` esiste solo con un target dEV, che il CLI di oggi non imposta [V `CLI:115-127, 314-316`].
- **Numeri**: formattazione di default degli stream C++, cioè 6 cifre significative, anche in notazione scientifica (`1.9e-16`).
  Un valore può essere `nan` o `inf` [I]. Il parser accetta `[-+]?(\d+\.?\d*(e[-+]?\d+)?|nan|inf)` e lo rappresenta come
  `null`, con un avviso.
- `path` può contenere spazi. Parsalo dalle chiavi note e non con uno `split` sugli spazi; meglio ancora, non usarlo e
  prendere i percorsi dal piano.

#### 4.5.3 stderr

- `solver_phase=iteration_complete iteration=<i> traversal_seconds=<s> traversed_nodes=<n>` per le iterazioni ≤ 5 e poi ogni
  20 [V `postflop_solver.cpp:19733-19741`]. Serve come battito e per la guardia d'avvio.
- Errori, una riga con exit ≠ 0: `postflop solve failed: <reason>`, oppure `postflop resume failed: <reason>`
  [V `CLI:269-321, 362, 384, 391, 407`]. Anche un `resume` può stampare `postflop solve failed:` (config, preparazione,
  solve, salvataggio): il parser accetta entrambi i prefissi senza legarli al sottocomando.
- Eccezioni: `gto_cli failed: internal_exception: <testo>` (exit 1) e, per un crash Windows,
  `gto_cli unhandled_windows_exception code=0x… address=0x…` [V `CLI:49-61, 4361-4369`].
- Altre righe `solver_phase=…` (preparazione, allocazione, `traversal_ready`, `traversal_failed`) vanno tollerate; usale
  solo come battito [V `postflop_solver.cpp:18929-19741`].

#### 4.5.4 `report.json` (`schema_version` 2) [V `CLI:147-205`]

| Chiave | Uso in UI |
|---|---|
| `status`, `iterations`, `nodes`, `infosets`, `actions` | riepilogo |
| `ev_co_antes`, `ev_btn_antes`, `br_co_antes`, `br_btn_antes` | EV e best response per giocatore, in a |
| `nash_conv_antes`, `normalized_nash_conv` | NashConv in a e in % del pot. Nell'esempio reale vale `normalized = nash_conv / pot` (3,9e-16 / 10 a = 3,9e-17) [I: da confermare nel codice]. Mostra `100 × normalized_nash_conv` % |
| `maximum_normalization_error` | diagnostica |
| `peak_rss_bytes`, `elapsed_seconds`, `phase_seconds{layout, initialization, traversal, certification, finalization}`, `build_to_ready_seconds`, `solve_to_consultable_seconds` | risorse e tempi |
| `production_profile` (oggetto `{major, minor}`), `algorithm`, `state_precision`, `averaging_delay`, `certification_interval`, `parallel_action_depth`, `backend`, `exact_outcomes`, `uses_bucketing`, `timer_scope` | identità del motore, sotto "dettagli tecnici" |

`report.md` è un gemello in italiano: mostralo come testo non fidato.

`report.json` è scritto con gli stream C++, non con una libreria JSON [V `CLI:147-205`]: un valore `nan` o `inf` lo
renderebbe JSON non valido. In quel caso la UI lo segnala come report illeggibile e mostra il testo, senza fallire [I].

#### 4.5.5 Exit code → stato

| Exit | Contenuto | Stato della run |
|---|---|---|
| 0 | `status=completed` | `done` |
| 0 | `status=paused` | `paused` (motivo dal DB: utente o finestra) |
| 0 | `status=cancelled` | `cancelled`, con checkpoint conservato |
| 1 | `postflop … failed: <reason>` oppure `gto_cli failed: …` su stderr. Al resume, `checkpoint_mismatch` (N troppo piccolo o config cambiata) | `failed`, con il testo |
| 2 | argomento non valido | `failed`: è un **bug del builder**, va segnalato come tale |
| 3 | `production_profile_backend_unsupported` (solve) oppure `rejected` (estimate) | `blocked(ram)`, con i numeri dell'estimate |
| 4 | checkpoint con algoritmo o precisione incompatibili al resume, oppure opzioni non risolte | `failed`; resume non offerto |
| altro (crash, per esempio `0xC0000005`), con la riga `unhandled_windows_exception` | — | `failed`, con il testo; resume offerto se `state.ckpt` esiste |
| exit non disponibile (backend riavviato) o kill, senza riga `status=` | — | `interrupted`; resume offerto se `state.ckpt` esiste |

Codici [V `gto_cli.md` §4; `CLI:262-322, 4353-4369`]. È la stessa regola di `scheduler.py:666-671` per il preflop.

### 4.6 Progresso ed ETA

- L'**iterazione** si prende dall'ultima riga `progress` o `solver_phase`.
- Le righe non hanno l'ora: il tailer registra l'**ora di ricezione** di ogni riga. Con un poll di 1 s, la granularità è di 1 s.
- **Rate**: iterazioni al secondo su una finestra mobile, calcolata dalle righe `solver_phase` (una ogni 20 iterazioni).
- **ETA** = (N − i) / rate, più una certificazione finale e la scrittura del checkpoint. Per il primo studio di uno spot, mostra
  la stima come "da misurare".
- **Curva**: NashConv in a e in % del pot (scala logaritmica), EV e BR per giocatore, in funzione dell'iterazione e del tempo.
  Le righe vengono dalle `progress`.

### 4.7 Pausa, cancel, kill e resume

| Azione | Meccanismo | Effetto | Note |
|---|---|---|---|
| Pausa (utente o fine finestra) | Intento nel DB, poi scrittura **atomica** di `<run>/state.ckpt.control` con `pause\n` (file `.tmp` più `os.replace`) | Il solver legge il file a ogni iterazione (`control_callback(iteration)`), lo cancella, forza una certificazione, salva checkpoint e report, stampa `status=paused` ed esce con 0 [V `postflop_solver.cpp:19743-19757`; `CLI:339-356`] | Latenza: un'iterazione più una certificazione esatta (su alberi grandi può non essere breve [I]) più la scrittura |
| Cancel | come sopra, con `cancel\n` | `status=cancelled`; checkpoint e report scritti | La run va in `cancelled` |
| Contenuto non riconosciuto | qualunque testo diverso da `pause`/`cancel`, anche un file letto a metà scrittura | il file viene **cancellato e ignorato** [V `CLI:344-355`] | Per questo la scrittura atomica è obbligatoria. Su Windows `os.replace` può fallire se il CLI tiene aperto il file in quell'istante: riprova dopo un attimo [I] |
| Controllo scritto durante l'avvio | il CLI cancella un file di controllo vecchio **prima** di preparare l'albero [V `CLI:310-312`] | un file scritto prima di quel punto **si perde**; uno scritto dopo resta e vale alla prima iterazione | Stessa regola del trainer (prompt originale §5.9): l'intento vive nel DB e si riscrive quando compare l'header (che arriva dopo la cancellazione) |
| Kill | albero del processo, solo con identità verificata (pid, create_time, exe) | brutale; si perde tutto dall'ultima certificazione | Mai per pattern |
| Resume | `postflop resume` con gli stessi `cfg`, `checkpoint` e `report`, e `N` più grande | | Config e (in E6) range sono **fissi per la run**: la UI li blocca dopo il primo step |

Il file di controllo sta nella run dir della UI, quindi scriverlo è lecito. Non usare mai il file di controllo di una run non
gestita.

### 4.8 Budget: thread, RAM, disco, tempo, finestre

| Budget | Regola |
|---|---|
| Thread | **8 fissi** per solve; nessuna opzione li cambia [V `postflop_solver.hpp:411`]. Sull'i3 un solve occupa tutto `threads_total` = 8. Un `gto_cli` esterno conta 8 |
| RAM | `lazy_peak_bytes` dell'estimate, più un margine da calibrare sul `peak_rss_bytes` dei report (almeno 3 run reali; fino ad allora +10 % + 0,15 GB). Si passa `ram_gib` = limite per run, intero; il CLI rifiuta da sé con exit 3 se il picco stimato lo supera |
| Disco | Il checkpoint di produzione contiene regret e strategia in `uint16` per azione, più le scale per nodo decisionale: circa **4 B × `actions`** dell'estimate [V formato in `postflop_solver.cpp:20860-20900`; smoke del 01/10: 1.015.872 azioni → `solve.chk` di 4.081.045 B]. Durante il salvataggio esistono il vecchio file e il `.tmp`: stima 2 × 4 B × `actions` + report + `disk_reserve_gb`. Dopo il primo checkpoint si ricalibra sulla dimensione vera |
| Tempo | Non c'è un modello. Riferimenti misurati sull'i3, con 8 thread, target dell'1 % e range GTO+: AHKHQH 0,76 s / 7,8 MB; TH7D6S 19,9 s / 364 MB; TSTC9D 184 s / 1,53 GB [V `README.md:209-214`; picchi RSS in `docs/specifications/LIMITATIONS.md:20-22`]. Con range uniformi i valori cambiano [I]. La prima run di uno spot fa da misura |
| Finestre | Lo step ha una pausa graceful, quindi si tratta come un `continuous`: parte se il tempo rimasto è ≥ `min_useful_minutes` + `stop_margin` + preparazione. A fine finestra meno `stop_margin` si scrive `pause`; a fine finestra meno `kill_margin` scatta il kill, con verifica; alla riapertura si fa resume (Q11) |
| Limite R7 | Vale anche qui: oltre il limite per run, la run è `blocked` |

### 4.9 Vista dei risultati

| Elemento | E4 (CLI di oggi) | E6 (contratto 4.12) |
|---|---|---|
| Scheda di riepilogo | `report.json`: stato, iterazioni, nodi, infoset, EV e BR per giocatore, NashConv in a e in %, picco RSS, tempi | come E4, più dEV % e range |
| Curva | righe `progress` | eventi `progress` JSONL |
| Report testuale | `report.md` | idem |
| Albero e nodi | **non disponibile**, con il motivo: "serve il worker `serve` del motore postflop (E6)" | breadcrumb delle azioni dalla radice e scelta delle carte di turn e river; `PostflopNodeAnalysis` [V `postflop_solver.hpp:705-724`] |
| Matrice 9×9 | — | le 630 combo meno quelle bloccate dal board, aggregate in 81 classi e pesate con `reach_weight`. Colori e tooltip come la matrice preflop |
| Dettaglio mano | — | per ogni combo: reach, equity, categoria, probabilità delle azioni |
| EV per azione | — | da E6 in poi, se il worker la fornisce (4.12) |
| Banner | "Range uniformi 100 %: test del motore" | range dello studio |

### 4.10 Run dir gestita (postflop)

```
<runs_root>/<run_id opaco>/
  meta.json               schema gtosd.solver_ui.run.v2, engine = "postflop_exact"
  NO_ARCHIVE
  inputs/postflop_config.json        (E6: inputs/study.json)
  solve.log               stdout del CLI (appeso, uno step dopo l'altro)
  solve.stderr.log
  state.ckpt              (mai letto dalla UI)
  state.ckpt.tmp          (transitorio, durante ogni salvataggio)
  state.ckpt.control      (transitorio: lo scrive la UI, lo cancella il CLI)
  report.json, report.md  (riscritti alla fine di ogni step)
  CANCEL                  (marker della UI, prompt originale R4)
```

- Il checkpoint passa da `state.ckpt.tmp` [V `postflop_solver.cpp:20856-20859, 20912`]; la sandbox continua a negare la
  lettura di `state.ckpt*` (`filesystem.py:86-89`).
- `solve.log` e `solve.stderr.log` sono già i nomi che lo scheduler usa per gli step preflop "solving" (step 1, validate,
  tree check) [V `scheduler.py:488-501`]: il tailer li distingue per motore, non per nome.

### 4.11 Mock e fixture

**`mock/mock_gto_cli.py`**
- Implementa `postflop validate|estimate|solve|resume` con argomenti posizionali e **le stesse righe** di 4.5. I testi e i
  messaggi di errore si estraggono dal sorgente, non da questo documento.
- Un test confronta i messaggi del mock con un elenco scritto a mano dal sorgente (prefissi di `CLI:253-461, 3660-3728` e
  nomi d'errore di `tree_config_error_name` e `postflop_solver_error_name`), con le righe citate: i messaggi sono composti
  a runtime, quindi un grep automatico sul sorgente non basta.
- Implementa il file di controllo: cancellazione di un file vecchio all'avvio, lettura a ogni iterazione, cancellazione dopo
  la lettura.
- Scrive un checkpoint finto (`MOCKPFCK` più l'iterazione) via `.tmp` e rename, e scrive `report.json` (schema 2) e `report.md`.
- Usa gli exit code di 4.5.5.
- **Iniezione di guasti**:
  - exit 3 per RAM;
  - exit 4 per checkpoint incompatibile;
  - crash senza riga `status=`;
  - preparazione lenta (oltre 15 s);
  - `nan` in una riga `progress`;
  - pausa scritta durante la preparazione.
- Il bin set mock mappa `gto_cli` su `[python, <copia congelata di mock_gto_cli.py>]`.

**Fixture `fixtures/runs/PF_check_only/`**
- Config: copia di `tests/fixtures/postflop_check_only.json`.
- Report: copia di `out/build/windows-release-suite/apps/gto_cli/product_timing_smoke/solve.json` e `solve.md` del 01/10,
  salvati come `report.json` e `report.md` (il layout di 4.10). Sono file di testo e leggerli è lecito. **Mai** `solve.chk`.
- `solve.log`: sintetico, costruito con i formati di 4.5.2. Sostituiscilo con una cattura reale quando avrai l'OK (E4,
  accettazione reale).
- Annota tutto in `fixtures/manifest.json` (sha256, origine).

### 4.12 Contratto proposto per lo studio completo (E6): solo una proposta

Non scrivere il C++. Implementa il mock e la UI contro questo contratto, e registra in `DECISIONS.md` la versione che usi. Se
chi scrive il motore lo cambia, ti adegui.

**`inputs/study.json`, schema `gtosd.postflop_study.v1`**

```json
{"schema": "gtosd.postflop_study.v1",
 "tree": { "...": "PostflopConfig v1, sezione 4.3" },
 "ranges": {"co": "AA-QQ,AKs-AQs,[50.0]KQo[/50.0]", "btn": "..."},
 "stop": {"target_dev_percent": 1.0, "max_iterations": 2000}}
```

- I range usano la sintassi accettata da `parse_hand_class_range` [V `CLI:690-800`]: token separati da virgole, senza
  spazi; i nomi delle 81 classi (`AA`, `AKs`, `AKo`) a peso pieno; intervalli `X-Y`; pesi **per classe singola**
  `[73.0]77[/73.0]`, con i due numeri uguali e fra 0 e 100. Un token vuoto o sconosciuto invalida tutto il range.
  - Un intervallo copre gli **ID di classe consecutivi** dentro la stessa categoria (coppie, suited, offsuit), non una
    "scala" di poker [V `CLI:699, 778-792`; ordine degli ID in `libs/core/src/ranges.cpp:46-64`]: `AKs-KQs` prende tutti gli
    Ax suited più KQs. L'editor 9×9 deve quindi emettere solo classi singole, con o senza peso.
- Il target: la libreria accetta `target_normalized_nash_conv` oppure `target_normalized_max_deviation`, non entrambi
  [V `postflop_solver.cpp:17029-17033`]. `gto_gui` usava il secondo come "dEV" [V `apps/gto_gui/product_window.cpp:1439`].
  Il campo `target_dev_percent` del contratto corrisponde a quello [I: conversione da % a valore normalizzato da fissare con
  chi scrive il C++].
- Il fingerprint dei range entra nell'identità della run.

**Comandi** (nomi proposti)

| Comando | Effetto |
|---|---|
| `gto_cli postflop study-solve <study.json> <checkpoint> <report_prefix> <ram_gib>` | solve con range e target |
| `gto_cli postflop study-resume …` | stessi argomenti, riprende dal checkpoint |
| `gto_cli postflop serve <study.json> <checkpoint>` | worker persistente per la vista dei nodi |

Il controllo resta `<checkpoint>.control`.

**Eventi JSONL su stdout**

| Evento | Campi |
|---|---|
| `start` | `config_fingerprint`, `ranges_fingerprint`, `nodes`, `infosets`, `actions`, `lazy_peak_bytes`, `threads`, `certification_interval`, `resumed_iteration` |
| `progress` | `iteration`, `ev_antes[2]`, `br_antes[2]`, `nash_conv_antes`, `normalized_nash_conv`, `dev_percent`, `elapsed_seconds`, `process_bytes` |
| `checkpoint` | `iteration`, `bytes`, `seconds` |
| `end` | le chiavi di `report.json` |
| riga di verdetto, non JSON | `GTOSD_POSTFLOP_STUDY=CONVERGED\|ITERATION_LIMIT\|PAUSED\|CANCELLED` (exit 0) oppure `=FAIL <messaggio>` (exit ≠ 0) |

**`serve`: protocollo a righe**
1. All'avvio il worker scrive una riga `{"event":"ready", "nodes":…, "root":0}`.
2. Le richieste arrivano una per riga su stdin, per esempio
   `{"id":7, "op":"node", "actions":[0,2], "board":["Td","4c"]}`.
3. Ogni risposta è una riga su stdout:
   `{"id":7, "ok":true, "node":{public_node, street, player_to_act, pot_antes, actions[{label, amount_units}], action_frequencies[], profile_value_antes[2], gto_plus_ev_antes[2], combos[{combo, cards, reach_weight, equity, hand_category, action_probabilities[]}]}, "children":[…]}`.
   I campi ricalcano `PostflopNodeAnalysis` [V `include/gtosd/postflop/postflop_solver.hpp:705-724`], che però contiene solo
   `public_node`, `player_to_act`, `profile_value_antes`, `gto_plus_ev_antes`, `actions`, `action_frequencies` e
   `combos[{combo, reach_weight, equity, hand_category, action_probabilities}]`. `street`, `pot_antes`, `cards`,
   `children` e un eventuale EV per azione li deve calcolare il worker [I].
4. Gli errori sono `{"id":7, "ok":false, "error":"…"}`.
5. Con EOF su stdin il worker esce con 0.
6. Il modello è `gtosd_preflop_blueprint_export --serve` [V `benchmarks/preflop_blueprint_export.cpp:1-9`;
   `include/gtosd/preflop_blueprint/query_worker.hpp:12-27`].

**Lato backend del worker**
- Ogni worker passa dall'admission: RAM = `lazy_peak_bytes`, 8 thread [I].
- Al massimo un worker per run e `max_workers` in tutto.
- Chiusura dopo `idle_timeout_s` (default 600) con EOF; kill per identità se non esce.
- Il worker è un processo solver: gira solo in finestra.

---

## 5. Supporto 3-way

### 5.1 Kind e regole

| Kind | 3 seat | Strumento | Note |
|---|---|---|---|
| `step2` | **sì** (nuovo) | `train` di un bin set con capacità `three_seats` e `pause_file` | Servono `--iterations` ed `--eval-every 0`; niente certificate, niente modalità automatica [V `TR:627-642`] |
| `evaluation` | **no** | `monker_values` | Solo HU [V `monker_values.cpp:654-655`]. Il form lo spiega |
| `step1_classes`, `chart_eval_step1` | sì | `checkdown_classes` | Esistono già (fixture `3WAY50_rake_dead`) |
| `tree_check` | sì | `monker_tree --config C --charts <run>/charts/it_N --manifest <run>/inputs/3WAY50_tree_manifest.tsv` | Il manifest non richiede chart esterne, quindi rispetta la revisione del 01/10; si congela in `inputs/` come la config (la copia del run 1 è in `FZ`, sha256 `2dc86dea…` [V `FZ/SOURCE.txt`]). Con `--charts` e `--manifest` insieme il tool controlla anche il manifest contro la cartella [V `benchmarks/preflop_blueprint_monker_tree.cpp:1-25`]. Atteso `PASS` su 54/54 [V handoff T3] |
| `part_a` | sì | `policy_values` (non ancora congelato) | "Should", dopo il congelamento previsto da T6 (5.10) |

**Default del form per 3 seat** [V `R:51-61, 108-115`; `Q:57-61`]

| Campo | Valore |
|---|---|
| segmentazione | `continuous` |
| `chart_every` | 4.000 (fa parte della definizione della metrica: la soglia è tarata su questa spaziatura) |
| `stop_metric` | `non_all_in` |
| soglia | 0,008 |
| `min_iterations` | 16.000 |
| `max` | 48.000 |
| `checkpoint_every` | 16.000. Il runner da solo usa 0 con 3 giocatori (`R:56`); 16.000 è la scelta dell'utente del 01/10 alle 18:00, contro un riavvio di Windows Update (`Q:59`) |
| `--policy-snapshots --policy-snapshot-every` | 16.000 |
| `--policy-snapshot-reserve-gb` | 40 |
| storage | `double` |
| thread | 8 |
| `batch` | 32 (default del form oggi: 32) |
| `partition_target` | 64 (default del form oggi: 4) |
| `progress_every` | 500 (default del form oggi: 25) |
| `--pause-file` | sempre |

**Seat e posizioni.**
- Run gestite: dalla config.
- Run scoperte: da `start.phase3.players` e `start.phase3.positions`.
- Se `phase3` manca, la run è HU (`CO, BTN`) [V `TR:651-653`].

### 5.2 Argv canonico B.5 (step 2 a 3 seat, run gestita)

```
<bin>/gtosd_preflop_blueprint_train(.exe)
  --config <run>/inputs/config.json --resources-dir <res> --buckets-dir <buckets>
  --board-class-rows --board-texture-map <run>/inputs/texture_map.txt
  --threads 8 --table-storage double --eval-every 0 --batch 32 --partition-target 64
  --scheme dcfr --update alternating --batch-policy-refresh --lazy-discount
  --progress-every 500 --iterations <MAX>
  --checkpoint <run>/state.ckpt --checkpoint-every 16000 --policy-out <run>/policy.bin
  --chart-every 4000 --chart-dir <run>/charts
  --stop-file <run>/STOP --pause-file <run>/PAUSE
  --seed <S> --lazy-discount-epoch 65535
  --policy-snapshots --policy-snapshot-every 16000 --policy-snapshot-reserve-gb 40
  --three-way-table <res>/preflop_three_way_v1.bin
  [--lock-charts <run>/inputs/lock_charts --lock-nodes all] [--resume]
```

- Contiene tutti gli argomenti del runner congelato `R` (righe 108-115 del file) con i valori della coda `Q`, `TRAIN_ARGS`
  compreso; l'ordine è diverso e non conta. In più:
  - `--seed` e `--lazy-discount-epoch`, che il builder di oggi passa sempre [V `builders.py:155-158`]. Con il seed di default
    del form (`0x545241494E494E47`) e l'epoca 65.535 coincidono con i default del trainer usati dal run 1
    [V `include/gtosd/preflop_blueprint/trainer.hpp:155, 162`], quindi il run è lo stesso;
  - `--three-way-table` esplicito (sotto).
- `--three-way-table` ha già questo default. La UI lo passa esplicito per registrarne lo sha256 e confrontare il
  `start.phase3.three_way_table.fingerprint` a ogni resume: se cambia, la UI si ferma e chiede all'utente. Solo con 3 seat:
  su un gioco HU il trainer lo rifiuta [V `TR:643-648`].
- Il lock con 3 seat esiste: V12 PASS a 15×4 [V handoff T11]. Copia le chart in `inputs/lock_charts` senza colonne EV.
  L'argv è una lista, quindi gli spazi nel percorso (la trappola di `TRAIN_ARGS` nel runner bash) non creano problemi.
- **Percorsi lunghi (MAX_PATH).**
  - Il nome di chart 3-way più lungo ha 76 caratteri, e il trainer lo scrive sotto `it_<N>.tmp/`.
  - Con una cartella chart di 187 caratteri lo snapshot fallisce (`charts_failed "cannot write"`) [V `threeway_formats.md` §1.2].
  - **Regola**: al lancio, rifiuta una `chart_dir` più lunga di 165 caratteri, con il motivo.

### 5.3 Finestra, pausa, stop rule, verdetti

**Fine finestra con 3 seat: `PAUSE`, non `STOP`.**
- `PAUSE` scrive solo il checkpoint, circa 13,56 GB, senza `policy.bin`, ed esce con `PAUSED`.
- `STOP` scriverebbe anche una policy da 6,78 GB e chiuderebbe il run come finito.
- È il comportamento della coda a mano [V `Q:251-267`; `R:38-42`]. È il default proposto (domanda 4 di `ui_map.md`): annotalo
  in `DECISIONS.md`.

**`PAUSE` va tenuto presente, non scritto una volta.**
- Il runner e il trainer cancellano `PAUSE` (e `STOP`) all'avvio [V `R:95`; `TR:586-595`]. Un `PAUSE` scritto mentre lo
  step parte si perde, e il run allenerebbe per tutta la notte: è il difetto che la coda ha corretto il 01/10 [V `Q:249-259`,
  `Q:289-292`].
- Regola: dal momento della pausa l'intento sta nel DB e la UI **riscrive `PAUSE` a ogni tick se manca**, finché lo step non
  esce. È la regola di §5.9 del prompt originale per `STOP`, estesa a `PAUSE`.
- Nessuno step a 3 seat parte nel tratto finale della finestra (la coda: nessun round dopo le 19:40 [V `Q:225-230`]);
  l'admission lo garantisce con `min_useful_minutes` + `stop_margin`.
- Se esistono sia `PAUSE` sia `STOP`, il trainer controlla prima `PAUSE` ed esce `PAUSED` [V `TR:1269-1283`]: "Termina ora"
  deve quindi togliere l'intento di pausa e non riscrivere `PAUSE`.

**Margini.**
- `stop_margin` per questo kind = max(`stop_margin_min`, 2 × il `write_seconds` osservato + un'iterazione).
- Il checkpoint richiede 1,3-4,5 minuti a 50-180 MB/s [V spec `PHASE3_SPEC_2026-09-30.md:697-701`].
- La coda a mano mette la pausa 20 minuti prima delle 20:00.

**Stop rule.** Si scrive `STOP` quando valgono tutte queste condizioni:
- `vs_previous[stop_metric_field] < soglia`;
- `iteration ≥ min_iterations`;
- il valore non è `na`;
- `STOP` non esiste già;
- `vs_previous` confronta `it_N` con `it_{N − chart_every}`.

Il campo è `non_all_in_mean_distance`, `all_in_mean_distance` oppure `overall_mean_distance` [V `R:77-82, 143-152`]. Le prime
quattro condizioni sono quelle del runner [V `R:149`]. L'ultima è una regola in più della UI [I]: il runner confronta con
l'ultimo snapshot presente, qualunque sia la distanza (`R:118-148`), anche dopo un `charts_failed` o una ripresa.

**Stop dell'utente**, due azioni distinte:
- **"Pausa"** → `PAUSE`, ripartibile;
- **"Termina ora"** → `STOP`: run finito, con `policy.bin`. Chiede conferma e spiega che non si riprende.

**Verdetti e stati**

| Verdetto (stdout) | Intento registrato nel DB | Stato |
|---|---|---|
| `PAUSED` | finestra o pausa utente | `paused(window \| user)`, ripartibile da `state.ckpt` |
| `STOPPED` | stop rule oppure "Termina ora" | `done` |
| `STOPPED` | finestra, su un bin set senza `pause_file` (HU `c123`) | `paused(window)`: il comportamento HU di oggi |
| `ITERATION_LIMIT` | — | `done` (tetto raggiunto) |
| exit ≠ 0 | — | `failed`. Il testo `PREFLOP_BLUEPRINT_TRAIN=FAIL <msg>` sta su **stderr** [V `TR:1560`]: leggilo da `train.stderr.log` |
| `CONVERGED`, `PLATEAU` | — | non capitano con 3 seat (`--eval-every 0`) |

**Etichette dei risultati finali.**
- Il runner o lo scheduler crea `STOP` dopo aver confrontato `it_N`.
- Il trainer controlla il file a ogni iterazione e si ferma a K = N + k [V `TR:1276-1283`].
- Quindi le ultime chart sono `it_N`, mentre `policy.bin` e `state.ckpt` sono all'iterazione K, presa dall'evento
  `stop_file`. La UI etichetta i due numeri separatamente.

**Crash prima del primo checkpoint.**
- Se il trainer muore prima di 16.000 iterazioni, `state.ckpt` non esiste.
- Un rilancio ripartirebbe da 0 sopra chart già scritte: è un caso non provato [V handoff T2, punto 5].
- La run va in `interrupted` **senza** resume automatico: la UI chiede all'utente. Prima di un'eventuale ripartenza, la
  procedura "superseded" del prompt originale (§5.8) sposta gli `it_N`.

### 5.4 Chart: 54 nodi, metriche e conteggi

**Formato.** Una chart per nodo, `<ATTORE>/<linea>_strategy.txt`: header `Combination<TAB>azioni…<TAB>Total`, 81 righe, 3
decimali (prompt originale App. F).
- Le nostre chart hanno fine riga LF; quelle di `MONKER3` CRLF, senza colonne EV.
- Nel nome **il primo fold di un giocatore è implicito**: va ricostruito nell'ordine `UTG, CO, BTN`, saltando chi ha già
  foldato o è all-in. `charts.py:61-101` lo fa già.
- Totali: UTG 16, CO 18, BTN 20; non all-in: 5 + 6 + 7 = 18.
- L'elenco completo è nell'Appendice A.

**Metriche di `compare_charts.py`** (copia congelata)

| Chiave | Significato |
|---|---|
| `overall_mean_distance` | media TV su tutte le chart con `mean_distance` diversa da `null` |
| `non_all_in_mean_distance`, `non_all_in_charts` | media e numero delle chart che **non** fronteggiano un all-in |
| `all_in_mean_distance`, `all_in_charts` | media e numero delle chart che fronteggiano un all-in (azioni esattamente `Call, Fold`) |
| per chart: `facing_all_in`, `mean_distance` | |
| `overall_range_difference` | differenza di range |

- **I conteggi `*_charts` contano solo le chart con `mean_distance` diversa da `null`** [V `compare_charts.py:188-197`
  congelato]. Una chart in cui nessuna classe è in range da entrambe le parti ha `mean_distance` `null` ed esce dalla media
  (una distanza `0` invece resta).
  - Esempio: nello step 1 `rake25_dead` entrano nella media all-in **22** chart su 36 [V `threeway_formats.md` §6.5].
  - Mostra sempre i conteggi accanto alle medie; non assumere 18/36.
- Gli arrotondamenti sono quelli dello script (4 decimali nel JSON): i golden test valgono entro 5e-5, oppure identici
  chiamando `compare()`.

**Curve da mostrare**
1. Variazione rispetto allo snapshot precedente, per le tre medie, in funzione dell'iterazione. La serie della metrica
   d'arresto è evidenziata, con la linea della soglia (0,008) e una zona grigia prima di `min_iterations`.
2. Tabella per chart della variazione (`charts[].mean_distance` di `vs_previous.json`), ordinabile: mostra quale nodo frena
   l'arresto.
3. Distanza da Monker sulle tre medie, con il riferimento step 1 come linea orizzontale: **solo se QA4 lo consente**.

### 5.5 Stime di risorse (3WAY50, bucket 15×4, TX2, storage double, 8 thread)

| Voce | Valore | Fonte |
|---|---|---|
| `state_bytes` | 14.324.289.748 B | [V `OUT1/train.jsonl`, evento `start`] |
| `accounted_total_bytes` dopo la prima iterazione | 14.836.224.337 B | [V `memory_breakdown`] |
| picco di private commit dopo la prima iterazione | **14.910.525.440 B** (13,89 GiB) | [V `memory_breakdown.process`] |
| `class_cache_bytes` | 345.223.705 B | [V] |
| velocità | **1,745 s per iterazione** (2.618,16 s per 1.500 iterazioni alle 00:53; 1,741 s sulle prime 500), senza salvataggi | [V `training_progress`] |
| celle (somma di `cells_by_street`) | 847.242.189; × 8 B = 6.777.937.512 B, la dimensione di una policy | [V `memory_breakdown`] |
| preparazione | 7,43 s, più il caricamento del checkpoint al resume (13,56 GB a 50-180 MB/s) | [V evento `start`; I per il caricamento] |
| `state.ckpt` | circa 13,56 GB (2 × 6.777.937.512 B) | [I dalla formula] |
| policy (finale o snapshot) | circa 6,78 GB | [I] |
| disco | a riposo 27-34 GB; picco 41-48 GB durante il `.tmp` del checkpoint; fino a circa 55 GB con lo snapshot a 48.000 e la policy finale. La coda ne esige 60 liberi | [I spec `:713-719`; V `Q:50`] |
| durata | 48.000 iterazioni ≈ 23,3 h di training, più i salvataggi e le pause di finestra. La specifica stima l'arresto fra 24.000 e 40.000 | [I] |

**Regola della RAM.**
- La formula di oggi è 1,1 × (34 B × celle + 16 B × (epoca + 1)) + 0,15 GB [V `scheduler.py:199-201, 221`]. Con le celle di
  questo run dà circa **31,8 GB** contro i 14,91 GB misurati [I: se le `entries` del game tool coincidono con la somma di
  `cells_by_street`]: il run risulterebbe `blocked` dal limite R7 di 16 GB.
- Il costo misurato è di circa 17,6 B per cella (14.910.525.440 / 847.242.189) [I: rapporto, non una legge].
- Quando esiste un picco misurato per la stessa combinazione (config, bucket, texture, storage, seat), usa quello con un
  margine piccolo (+3 %), e scrivi la fonte.
- Per combinazioni nuove a 3 seat usa 17,6 B per cella (il rapporto comprende già la cache delle classi di questo run) e il
  margine del prompt, ed etichetta la stima come non calibrata [I].
- La guardia RAM di oggi ferma la run sopra `ram_per_run_gb × 1e9` byte di RSS [V `scheduler.py:858-865`]: con 16 GB il
  run 1 (14,86 GB di working set) passa con meno di 1,2 GB di margine.

**Regola del disco.** La formula di oggi conta gli snapshot di policy solo se c'è una valutazione [V `scheduler.py:208-212`].
Con 3 seat non c'è valutazione ma gli snapshot si scrivono: vanno contati (3 × 6,78 GB con `max` 48.000 e uno snapshot ogni
16.000).

### 5.6 Il run 1 (`OUT1`): esempio vivo in sola lettura

**È un run esterno (D6).** Valgono regole assolute:
1. **Mai scrivere** in `OUT1`, né altrove sotto `out/monker/step2_3way`, nemmeno un file di cache. La UI non crea mai
   `PAUSE`, `STOP` o `QUEUE_CANCEL` in un run esterno (QA6).
2. **Mai tenere un handle aperto** sui suoi file fra due poll.
   - Il tailer di oggi apre e chiude a ogni lettura [V `tailer.py:37-39`]: mantieni questo comportamento per ogni file.
   - Niente `tail -f` e niente watcher che bloccano i file. Un lettore aperto ha già fatto fallire un archivio
     [V handoff §8, punto 9].
   - Dopo la parte A (T3, T7), fra le 20:00 e le 24:00, la cartella può essere spostata su `F:` con `archive_run.ps1`
     (handoff T8) e diventare una junction: la run allora è `archived`. Un handle aperto della UI farebbe fallire lo
     spostamento.
3. **Mai aprire** `state.ckpt*`, `policy.bin` o `charts/*/policy.bin`, nemmeno per l'hash: solo dimensione e mtime.
4. Il processo della coda si riconosce **solo per mostrarlo**: `Win32_Process` con `CommandLine` che contiene
   `queue_3way50_15x4`.
   - Il PID scritto in `queue.lock/pid` è un PID MSYS (1644), non un PID Windows [V handoff T2, punto 3].
   - Nessun kill, mai.
5. **Chart incomplete.** Uno snapshot è completo solo quando esiste `it_N` (non `.tmp`). I confronti `vs_*.json` arrivano
   dopo, scritti dal runner, che controlla ogni 15 s [V `R:124-155`]. Se mancano, la UI scrive "confronto in attesa".
6. **Fixture.**
   - Copia in `fixtures/runs/3WAY50_step2_live/` solo file di testo: `queue.log`, `run.log`, le righe complete di
     `train.jsonl` e gli `it_N` completi con i loro `vs_*.json`. **Mai** `policy.bin` (6,78 GB in `it_16000`, `it_32000`,
     `it_48000`) né `state.ckpt*`: copia file per file con un filtro sui nomi, non intere cartelle.
   - Copia a snapshot finito: `it_4000` è atteso verso le 02:00-02:10 del 02/10 [I].
   - Fino ad allora, fixture sintetica costruita da 5.7.
   - Registra ogni file in `fixtures/manifest.json`.

### 5.7 Formati di `OUT1`

**Layout.**
```
OUT1/
  queue.log, queue.lock/pid, runner_round<N>.log, run.log, train.jsonl, train.stderr.log
  charts/it_<N>/{UTG,CO,BTN}/*.txt, vs_monker.{json,txt}, vs_previous.{json,txt}
  charts/it_{16000,32000,48000}/policy.bin
  state.ckpt (state.ckpt.tmp mentre scrive), policy.bin (solo alla fine vera)
  PAUSE | STOP | QUEUE_CANCEL      (file di controllo; nessuno esiste ora)
  part_a/...                       (dopo T7)
```

**Lo stato non sta in un file solo.** Servono tutti e quattro:
- `queue.log`: coda, finestre e gate della memoria;
- `run.log`: snapshot e regola d'arresto;
- `train.jsonl`: il trainer, con un blocco `start`..`end` per round;
- la cartella: i file di controllo e i `.tmp`.

#### 5.7.1 `queue.log`

Ogni riga ha la forma `YYYY-MM-DD HH:MM:SS <messaggio>` [V `Q`, righe indicate].

| Regex sul messaggio | Riga di `Q` | Stato UI |
|---|---|---|
| `queue start \(pid (\d+)\): (\S+) sha256 (\w+); OUT (.+), start at (.+), free days (.+), pause at (\d+), runner (\S+), overrides (.+)` | 190 | `queued`. Se `overrides != none`, banner "run di prova" |
| `checks:` seguita da `  ok   <testo>` oppure `  FAIL <testo>` | 191, 80-81 | pannello dei controlli |
| `a check failed: queue ends \(nothing started\)` | 193 | `failed(preflight)` |
| nessuna riga mentre `start at` è nel futuro | 197-203 | `waiting(start_at)` |
| `memory gate closed: free (\d+) KB \(needs (\d+)\), trainer processes (\d+); waiting` | 214 | `waiting(budget_ram)` |
| `memory gate open: free (\d+) KB, trainer processes (\d+)` | 210 | transitorio |
| `after the daily stop time (\d+) on a window day: no round starts; waiting for (.+)` | 227 | `waiting(window)` |
| `round (\d+): runner start` / `round (\d+): runner pid (\d+)` | 233, 241 | `running`. Il PID del runner è MSYS |
| `round (\d+): daily window, PAUSE touched \((.+)\)` | 255 | `pausing(window)` |
| `round (\d+): PAUSE had been removed \(round start-up\): touched again` | 256 | `pausing(window)` |
| `round (\d+): QUEUE_CANCEL seen, PAUSE touched` | 264 | `pausing(cancel)` |
| `round (\d+): runner exit (\d+), trainer status (\S+) \(new status lines (-?\d+)\)` | 274 | fine del round |
| `round (\d+): runner failed: queue ends without retry \(see .+\)` | 276 | `failed` |
| `run finished \((PREFLOP_BLUEPRINT_TRAIN=\w+)\): queue done` | 280 | `done` |
| `paused for QUEUE_CANCEL: queue ends; remove QUEUE_CANCEL and rerun this script to resume` | 283 | `cancelled`, ripartibile |
| `round (\d+): paused by a PAUSE file the queue did not write: treated as a daily stop` | 284 | avviso |
| `paused: resume at (.+)` | 286 | `paused(window)`, con l'ora di ripresa |
| `QUEUE_CANCEL seen (while waiting\|at the memory gate): exit` | 199, 207 | `cancelled` |
| `QUEUE_CANCEL present at start .*` | 187 | `blocked(cancel_file)` |

**Dopo un riavvio del PC nessuna riga dice che la coda è morta.**
- Se `queue.lock/` esiste e nessun processo ha la riga di comando della coda, la UI mostra "lock orfano": lo toglie solo
  l'utente.
- Se la coda è viva ed è un giorno di finestra, la UI mostra "in pausa, riprende alle 00:00".

#### 5.7.2 `run.log`

Ogni riga ha la forma `YYYY-MM-DD HH:MM:SS <messaggio>` [V `R:104, 141-171`].

1. **Start**, una riga per round:
   `step 2 continuous start: config <CFG> buckets <DIR> step <S> max <M> threshold <T> metric <metric> min <m> checkpoint_every <c> players <p> <TRAIN_ARGS…> [--resume]`.
   Un `--resume` in fondo indica una ripresa.
2. **Snapshot.** Una regex valida per il primo snapshot e per i successivi:
   `iteration (\d+): elapsed (\d+) s, change vs previous (na|[\d.e-]+)(?: \(all-in (\S+), non-all-in (\S+)\))?, stop metric (\w+) (\S+), vs monker distance (\S+) \(all-in (\S+), non-all-in (\S+)\) range difference (\S+)`.
   `elapsed` riparte da 0 a ogni round: il tempo cumulativo si prende da `train.jsonl`.
3. `comparison with it_<P> failed at <N>`.
4. `STABLE at <N> (non_all_in change <C> < <T>): stopping the trainer`.
5. `trainer failed (exit <rc>)`, seguita da 5 righe di stderr.
6. `trainer finished: PREFLOP_BLUEPRINT_TRAIN=<S>, last snapshot <N>, stop event "event":"stop_file","iteration":<K>`.
7. `paused (daily stop) at <K>: checkpoint only, no policy.bin; rerun the same command to resume`.
8. `policy snapshots: skipped for disk space at iterations <lista|none>, failed <n>`.

**Fonte preferita.** Usa i due JSON di `it_N` come fonte primaria delle metriche e `run.log` come riserva e per gli eventi
(`STABLE`, `paused`).

#### 5.7.3 `train.jsonl` con 3 seat

**Blocco di ogni round**
- `start`;
- `memory_breakdown`, due volte (`after_initialization`, `after_first_iteration`);
- `training_progress` ogni 500 iterazioni, con flush immediato [V `TR:1151-1159`];
- `charts` ogni 4.000;
- secondo i casi: `policy_snapshot_skipped`, `policy_snapshot_failed`, `charts_failed`, `pause_file`, `stop_file`;
- `end`;
- la riga di stato `PREFLOP_BLUEPRINT_TRAIN=<S>` (non JSON).

**Campi utili**
- `start.phase3`: `{players, positions, three_way_table{path, fingerprint}, pause_file, preflop_terminals, folded_cards, hero_folded_shortcut, checkdown, validation, …}` [V `TR:880-908`].
- `start`: `config_id`, `tree_fingerprint`, `trainer_identity`, `board_texture`, `state_bytes`, `capacities`, `resumed_iteration`.
- `training_progress`: `{iteration, training_seconds (per processo), traversal_seconds, boards_processed, process_bytes}`.
  Ogni iterazione elabora 96 board (32 board × 3 eroi).
- `end`: `policy_fingerprint`, che è **vuoto** in una pausa; `convergence_status` = `NOT_REACHED`;
  `three_seat_profile{players, paused, …}`.

**Il checkpoint non ha un evento.** La UI lo vede da mtime e dimensione di `state.ckpt` e dalla presenza di `state.ckpt.tmp`.

**Raggruppare con lo step 1.** Si raggruppa per `config_id` più l'hash della config normalizzata LF. I `tree_fingerprint` di
step 1 e step 2 dello stesso gioco **differiscono** [V `threeway_formats.md` §4.1].

### 5.8 Oggetto normalizzato di una run 3-way (backend)

```json
{"engine": "preflop_blueprint", "kind": "step2", "players": 3, "positions": ["UTG","CO","BTN"],
 "config_id": "MONKER-3WAY50-DONK-RAKE25-CAP2-001", "tree_fingerprint": "fnv1a64:71abeabaab92fe56",
 "queue": {"external": true, "script": "out/frozen/queue_3way50_15x4.sh", "sha256": "7581f0f8…",
           "state": "running|waiting|pausing|paused|cancelled|done|failed|blocked",
           "reason": "window|budget_ram|start_at|cancel_file|stale_lock|null", "resume_at": null,
           "round": 1, "free_days": ["2026-10-02"], "pause_at": "19:40", "overrides": null, "alive": true},
 "trainer": {"iteration": 500, "max": 48000, "min_stop": 16000, "s_per_iteration": 1.741,
             "cumulative_training_seconds": 870.6, "status_line": null,
             "checkpoint": {"bytes": null, "mtime": null, "writing": false}, "policy_final": null},
 "snapshots": [{"iteration": 4000, "charts": 54, "policy": false, "comparison_pending": false,
                "change": {"overall": null, "all_in": null, "non_all_in": null, "all_in_charts": null, "non_all_in_charts": null},
                "vs_monker": {"overall": 0.0, "all_in": 0.0, "non_all_in": 0.0, "range_difference": 0.0}}],
 "stop_rule": {"metric": "non_all_in", "threshold": 0.008, "spacing": 4000, "stable_at": null, "stop_event_iteration": null},
 "reference_step1": {"run": "out/monker/step1_3way/3WAY50_rake25_dead"},
 "part_a": {"values": null, "report": null, "progress": null, "provisional": true}}
```

- `vs_monker` e `reference_step1` si **calcolano** sempre, ma si **mostrano** solo secondo QA4.
- Per le run gestite `queue.external` è `false` e lo stato viene dallo scheduler.

### 5.9 Step 1 3-way come riferimento (`out/monker/step1_3way`)

- **Layout** (prompt originale E.5): 5 giochi (`norake_dead`, `rake_dead`, `rake075_dead`, `rake25_dead`, `rake_ignore`). Ognuno
  ha `summary.json` (`…checkdown_classes.v1`), `charts/` (54 file), `vs_monker.json`, `tree_check.txt` e una cartella
  `_monker_eval`.
- **Ricalcolo.** I `vs_monker.json` dello step 1 sono stati scritti **prima** delle medie di gruppo: non hanno né
  `facing_all_in` né `*_mean_distance` [V su tutti e 5]. La UI li ricalcola in memoria con la copia congelata di `compare()`
  e mette il risultato in cache. Non li riscrive mai.
- **Riferimento del run 1** (stesso `config_id`): `rake25_dead`. Distanza 0,2641; all-in 0,1369 su 22 chart; non all-in
  0,4195 su 18; differenza di range 0,7209 [V ricalcolato con la copia congelata in `FZ`].

### 5.10 Parte A: vista dei risultati

**Stato.** Il codice è scritto ma né compilato né committato (`WT`). I primi dati veri arriveranno in `OUT1/part_a/`, dopo il
run 1 (handoff T7). Fino ad allora usa una **fixture sintetica** conforme allo schema qui sotto [V `WT/benchmarks/preflop_blueprint_policy_values.cpp:907-1329`].

**File e formati**

| File | Contenuto |
|---|---|
| `part_a/cli_<n>.progress.log` (stderr) | JSON per riga: `{"event":"start", mode, players, threads, policy_entries, load_seconds}`, `{"event":"progress", chunks_done, chunks_total, boards_done, elapsed_seconds, eta_seconds, process_bytes}`, `{"event":"stopped", …}`; poi `CHECK FAILED: <testo>` oppure `PREFLOP_BLUEPRINT_POLICY_VALUES=FAIL <errore>` |
| `part_a/cli_<n>.log` (stdout) | righe `preflop response <POS>: EV … gain per class … per combo …`, `expected rake …`, `charts monker played by <POS>: loss …`; verdetto `PREFLOP_BLUEPRINT_POLICY_VALUES=PASS\|WARN boards <n> seconds <s>`, `=FAIL checks <n>` (exit 1 con `--check`) oppure `=PARTIAL chunks a/b` |
| `part_a/values_<n>.json` | schema `gtosd.preflop_blueprint_policy_values.v1`: `estimate{ev_antes[N], ev_direct_antes[N], ev_direct_standard_error_antes[N], expected_rake_antes, rake_identity_residual_antes, gain_preflop_antes[N] (PER COMBO), gain_preflop_class_antes[N] (PER CLASSE), gain_preflop_standard_error_antes[N], gain_preflop_pot_percent[N], max_gain_preflop_antes, passes_target}`, `heroes[N].nodes[]{chart, strategy, class_ev{class:{token:{ev, se}}}, …}`, `preflop_response[N]`, `chart_sets[]`, `checks_failed[]`, `evaluation{mode, flops, seed, process_peaks, …}` |
| `part_a/values_<n>.series.json` | un chunk per flop campionato (circa 20 MB): non serve alle viste |
| `part_a/report_<n>.json` (di `part_a_values.py`) | `players[N]{ev_direct_antes ± se, gain_best_response_class_antes, gain_best_response_combo_antes, gain_pot_percent, passes_gate, …}`, `passes_gate`; verdetto `PART_A_VALUES=PASS` |

**Regole di presentazione**
1. **Due gain diversi.**
   - `gain_preflop_antes` è **per combo**; `gain_preflop_class_antes` è **per classe**.
   - `passes_target` e il gate di `part_a_values.py` usano il valore per classe. L'handoff scrive il gate G3 sul valore per
     combo, che è più severo.
   - Mostra entrambi, con l'etichetta giusta. Quale dei due decide il verdetto lo stabilisce l'utente (QA7).
2. **EV.** Mostra `ev_direct ± ev_direct_standard_error`. Le SE "pooled" omettono un termine finché non arriva la patch D1
   (handoff T5).
3. **Etichetta.** Ogni gain campionato è una stima per eccesso e porta l'etichetta "provvisorio, 64 flop fisici".
4. **Tooltip EV per azione.** `heroes[].nodes[].class_ev` ha la stessa posizione che ha in `values.json` HU: il tooltip riusa
   il codice esistente, con in più la SE.
5. **Checklist G3**, con le condizioni calcolate e la loro fonte:
   - run finito per arresto o tetto;
   - `tree_check` 54/54;
   - `rake_identity_residual_antes` ≤ 1e-9;
   - gain preflop ≤ soglia per seat (default 0,04 a, configurabile);
   - `checks_failed` vuoto.
6. **Monker.** Le sezioni "chart Monker nel nostro gioco" (`chart_sets`) seguono QA4.

**Lancio da UI della parte A** ("should", dopo T6 e QA3).
- Kind `part_a`; argv come il comando di handoff T7.
- La CLI **non ha un file di pausa**: Stop = kill, con perdita al più del chunk in corso. Il resume rilancia lo stesso comando
  con `--state`.
- Thread: 8. RAM: circa 7,3 GB [I].

### 5.11 Difetti del prototipo sui formati 3-way

| # | Problema | Dove [V] | Correzione |
|---|---|---|---|
| U1 | I seat si leggono solo da `summary.json` (default 2): uno step 2 3-way risulta HU | `adapters.py:219-221` | `summary.seats` → `start.phase3.players` → 2; posizioni da `start.phase3.positions` |
| U2 | `STOPPED` diventa "paused"; `PAUSED` e tutto il resto (tranne FAIL) diventano "done" | `adapters.py:233-242` | Tabella di 5.3; per le run esterne, causa della pausa da `queue.log` |
| U3 | Si riconosce solo `CANCEL` | `adapters.py:230` | Aggiungere `QUEUE_CANCEL`, `PAUSE`, `STOP` e `queue.lock/` |
| U4 | "running" se un processo ha la run dir in argv; fra due finestre la run diventa "interrupted" | `adapters.py:243-249` | Con `queue.log` e la coda viva, lo stato è "in pausa, riprende alle …" |
| U5 | La curva vuole lo schema HU di `monker_values` | `adapters.py:79-82` | Curve da `vs_*.json` (5.4); adapter della parte A (5.10) |
| U6 | `has_values` cerca solo `values.json` | `adapters.py:300-302` | Cercare anche `part_a/values_*.json` |
| U7 | `step2_3way` non è fra le `browse_roots` | `config/solver-ui.example.toml:19-28` | P20 |
| U8 | Il set Monker suggerito è `HU/50a` perché i seat valgono 2 | `adapters.py:250, 435-449` | Si corregge con U1 |
| U9 | `compare_charts.py` preso dal checkout (CRLF) con sha sui byte grezzi; per i run 3-way la copia di riferimento è quella congelata in `FZ` (`e243f0b1…` sul contenuto LF) | `api.py:49-50`, `charts.py:107-119` | F5 |
| U10 | Nessun supporto a `non_all_in`, `all_in` e `facing_all_in` | grep vuoto | 5.4 |
| U11 | Con la root aggiunta, le chart di `UTG/*` falliscono con 422 ("Nome nodo non valido") | `charts.py:74-75`, `api.py:418-426` | Si corregge con U1 |

---

## 6. Altri difetti da correggere prima dell'uso reale

| # | Difetto | Dove [V] | Correzione | Milestone |
|---|---|---|---|---|
| F1 | `short_job_timeout_s` (300 s) uccide **ogni** step non di training dopo 300 s: valutazioni e step 1 compresi. Una valutazione HU50 dura 279 s (al limite) e una G4 84 minuti [V prompt originale `:1430, 1935-1938`]. In più la guardia d'avvio uccide dopo 15 s uno step che non ha ancora scritto su stdout | `scheduler.py:866-873` (`A or B and C` = `A or (B and C)`); `:878-886` | Timeout solo per i job brevi (`validate`, `tree_check`, `estimate`); gli step `long_running` ne sono esenti; parentesi esplicite; guardia d'avvio per motore e per step (`startup_timeout_s`, 3.3) | E0 |
| F2 | La regex del manifest accetta solo `\w+\.exe`: su Linux non si registra niente | `assets.py:66` | P3 | E0 |
| F3 | Si contano solo i processi esterni `gtosd_*`: un `gto_cli` lanciato a mano è invisibile al budget | `telemetry.py:54` | P18 | E0 |
| F4 | Il FAIL del trainer va su stderr e la UI non lo legge: il testo dell'errore si perde | `TR:1560`; `scheduler.py:611-618, 666-672` | Alla chiusura con exit ≠ 0, leggere le ultime righe di `train.stderr.log` e salvarle in `run.error` | E0 |
| F5 | La guardia di resume confronta lo sha della copia congelata di `compare_charts.py` con quello del checkout, sui byte grezzi. Il checkout ha CRLF (`core.autocrlf=true`; `file` dice "CRLF line terminators" [V]) e lo script è cambiato con la fase 3a: le run create prima non riprendono | `charts.py:105-119`; `scheduler.py:449-452` | sha calcolato sul contenuto normalizzato LF; ogni run tiene la propria copia (`data_root/tools/<sha>/`) e riprende con quella; una copia nuova si usa solo per le run nuove, oppure con un "upgrade" esplicito | E0 |
| F6 | Dopo la pulizia del 02/10 `out/monker/correctness` non ha più run; le varianti in `out/monker/variants` (junction verso `F:`) non hanno più `policy.bin` né checkpoint, tranne tre run; `F:` è un disco USB che può mancare | `toml:19-28`; C6; `adapters.py:300` (`has_policy`) | La discovery tollera root vuote, junction pendenti e `F:` scollegato senza errori né attese lunghe; le root su `F:` restano `slow = true` (lette solo se selezionate); niente valutazione offerta su snapshot senza `policy.bin` | E0 |
| F7 | `history_map_resident_bytes` sparirà (C8) | fixture V1L | Tutti i parser devono trattarla come facoltativa. Non toccare la fixture | E0 |
| F8 | La guardia d'avvio di 15 s uccide un resume con checkpoint grande: il trainer carica `state.ckpt` prima di scrivere l'evento `start` (e lo scrive insieme al primo `memory_breakdown`). A 3 seat sono 13,56 GB a 50-180 MB/s, cioè 75-270 s; già un HU da 2 GB può superare 15 s [I sui tempi] | `TR:804-810, 850-919`; `scheduler.py:878-886` | Timeout d'avvio per step: 15 s più dimensione di `state.ckpt` / 50 MB/s al resume (3.3); nel frattempo lo step è "in caricamento", non bloccato | E0 |

---

## 7. Milestone, accettazione e test

### 7.0 Regole che restano (non negoziabili)

Valgono per ogni milestone, insieme a §3 e §11 del prompt originale:

1. **Niente shell arbitraria.** Spawn con lista argv, `shell=False`; un test statico vieta `shell=True` e `os.system`. Solo
   sottocomandi in whitelist, per motore (4.1, 5.1); niente "argomenti extra".
2. **Solo eseguibili frozen** da bin set registrati, con sha256 verificati prima di ogni spawn, DLL comprese. Mai `out/build`.
3. **Finestre macchina.**
   - Ogni processo solver reale (`validate`, `estimate`, `serve` compresi) gira solo in finestra e con l'OK dell'utente per i
     test.
   - Il 02/10 la macchina è del run 1 (2.3).
   - Scrivere codice è sempre ammesso; i test in mock seguono D5, con le cautele di 2.3.
4. **Mai cancellare dati.**
   - Nessun endpoint di delete; nessuna pulizia automatica di checkpoint, policy o snapshot.
   - Le sole operazioni distruttive ammesse restano quelle del prompt originale (§5.10).
   - I file di controllo che la UI scrive nelle **proprie** run dir (`STOP`, `PAUSE`, `CANCEL`, `state.ckpt.control`) li
     consumano i tool: la UI non li cancella.
5. **Kill solo per PID verificato**: (pid, create_time, exe) più i discendenti, poi uno scan di verifica. Mai per pattern di
   riga di comando: i pattern servono solo a mostrare. Una run cancellata non riparte mai, nemmeno dopo un restart.
6. **Run esterne in sola lettura** (D6): nessuna scrittura e nessun handle persistente (5.6).
7. **Niente C++ e niente build.**
   - `libs/`, `include/`, `apps/` (tranne `apps/solver-ui`), `benchmarks/`, `tests/`, `CMakeLists.txt` restano in sola
     lettura.
   - Lo stesso vale per `out/frozen/`, `out/monker/bin*` e le cartelle dei run.
   - Le proposte per il motore vanno in un documento, e poi chiedi.
8. **Git.**
   - Lavora nel worktree `GTO-Solver-solver-ui`.
   - Commit piccoli con percorsi espliciti; mai `add -A`, `stash`, `reset` o `rebase`.
   - Mai push né PR senza richiesta.
   - Nel checkout principale nessuna operazione git e nessun file tracciato; restano ammesse solo le scritture già decise
     sotto `out/solver-ui-mock/` (dati, venv e dipendenze, `DECISIONS.md:25-28`).

### E0. Allineamento e difetti

**Scope**
- Merge (non rebase) di `feat/monker-step1-checkdown` in `feat/solver-ui`, con `--no-ff`, nel worktree della UI.
  - Le due branch toccano percorsi disgiunti, quindi il merge dovrebbe essere pulito [I].
  - Annuncialo all'utente prima di farlo: porta nel branch della UI 22 commit (fase 3a compresa).
  - Se compare un conflitto, fermati e chiedi.
- Difetti F1-F8.
- Config di esempio: P20.
- `DECISIONS.md` aggiornato con i default di questo addendum.

**Accettazione**
1. Tutti i test esistenti sono verdi: Python 65, Vitest 5, Playwright 4.
2. Un test per ogni difetto, con fake clock dove serve:
   - una valutazione mock di 2 h non viene uccisa a 300 s;
   - un manifest con nomi senza estensione e con DLL si registra;
   - un `gto_cli` esterno finto conta 8 thread;
   - un FAIL su stderr compare nel testo d'errore della run;
   - una copia CRLF e una LF di `compare_charts.py` danno lo stesso sha;
   - una run creata con la copia vecchia riprende con la sua copia;
   - una root `slow` non viene aperta all'avvio;
   - una root vuota, una junction pendente e una root su un'unità assente non bloccano la discovery;
   - un resume mock che scrive `start` dopo 270 s, con la dimensione del checkpoint simulata a 13,56 GB, non viene ucciso.
3. Tutte le 33 config del repo passano ancora il validatore [V oggi, `ui_map.md` §1].

### E1. Registro dei motori e "Nuovo studio"

**Scope**
- Interfaccia `Engine` (3.3).
- `PreflopBlueprintEngine` costruito sopra il codice di oggi.
- `postflop_exact` e `postflop_multiway` presenti nel registro e disabilitati, ciascuno con il suo motivo.
- Bin set da TOML (3.7).
- `StudySpec`, `/api/engines`, `routing` nella preview, eventi SSE mancanti.
- Procedura guidata "Nuovo studio" (3.6).

**Accettazione**
1. **Golden argv**: ogni kind esistente produce, attraverso l'engine, argv identici byte per byte a quelli di oggi.
2. **Instradamento**: un test di tabella copre ogni riga di 3.2. Ogni richiesta dà (motore, kind, disponibile, motivo)
   esatti; il multiway risulta non disponibile; da 4 a 6 giocatori resta solo `validate`.
3. **Disponibilità**: in modalità reale, senza il bin set di `gto_cli`, `/api/engines` dichiara `postflop_exact` non
   disponibile, con il motivo.
4. **Migrazione**: un DB con record senza `engine` si apre, e i record valgono `preflop_blueprint`.
5. **E2E Playwright**: Nuovo studio → "2 giocatori, preflop, completo" → la preview mostra "Motore preflop" e nessun argv
   grezzo → la conferma crea una run mock che arriva a `done`.
6. I test statici di sempre restano verdi: niente shell, niente kill per pattern.

### E2. 3-way in sola lettura (`OUT1` e step 1)

**Scope**: U1-U11, l'adapter della coda (5.7.1), `run.log` (5.7.2), `train.jsonl` a 3 seat (5.7.3), curve e tabelle di 5.4,
riferimento dello step 1 (5.9).

**Accettazione**
1. **Fixture** `3WAY50_step2_live` (5.6), reale o sintetica:
   - posizioni UTG, CO, BTN;
   - 54 nodi con i fold impliciti giusti;
   - nessuna risposta 422.
2. **Grammatica di `queue.log`**: ogni riga della tabella 5.7.1 ha un unit test con una riga d'esempio e lo stato atteso. Ci
   sono casi anche per "lock orfano" e per "in pausa, riprende alle 00:00".
3. **Verdetti**: la tabella 5.3 è coperta da test (PAUSED, STOPPED dopo `STABLE`, ITERATION_LIMIT, FAIL su stderr).
4. **Curve e conteggi**:
   - la curva della variazione non all-in ha la soglia a 0,008 e la zona grigia sotto 16.000;
   - i conteggi `*_charts` sono visibili;
   - la tabella per chart è ordinabile.
5. **Step 1**: `rake25_dead`, ricalcolato in memoria, dà 0,2641 / 0,1369 su 22 / 0,4195 su 18 / 0,7209 entro 5e-5.
6. **Sola lettura**:
   - su una copia statica dei file di testo di `OUT1` (la fixture di 5.6), hash e mtime restano invariati;
   - file sentinella al posto di `state.ckpt` e `policy.bin` fanno fallire il test se vengono aperti;
   - un test statico verifica che nessun percorso di scrittura cada sotto le `browse_roots`.
7. **Run vivo**, in sola lettura (Q10 del prompt originale: default sì):
   - la UI mostra `OUT1` con iterazione e s/iterazione coerenti con l'ultimo `training_progress`;
   - fra due poll nessun handle resta aperto: verificalo su una copia (rinominarla fra due poll deve riuscire), oppure con
     `handle.exe` solo se è già installato; non scaricare strumenti.

### E3. 3-way gestito (mock; reale solo dopo QA3, QA5 e QA6)

**Scope**: `step2` a 3 seat in `models`, `builders`, `scheduler` e mock; `PAUSE` di fine finestra; stop rule configurabile;
admission per 3 seat (5.5); regola MAX_PATH.

**Accettazione, in mock**
1. **Validazione**: `step2` con 3 seat viene accettato; `evaluation` con 3 seat viene rifiutato con il motivo.
2. **Golden argv** di B.5.
3. **Ciclo di finestra** con fake clock: `PAUSE` a fine finestra meno il margine → `PAUSED` → `paused(window)`, senza
   `policy.bin` → resume automatico alla riapertura con `--resume`. Un caso con `PAUSE` cancellato dal mock all'avvio dello
   step: la UI lo riscrive e lo step esce comunque `PAUSED`.
4. **Stop rule**:
   - `STOP` arriva solo con tutte le condizioni di 5.3 vere;
   - poi `STOPPED` → `done`;
   - `policy.bin` è etichettata K (dall'evento `stop_file`), le chart N.
5. **"Termina ora"** chiede conferma e porta a `done`. **"Pausa"** porta a `paused`.
6. **Admission**:
   - RAM dal picco misurato (14,91 GB) quando esiste, con la fonte nel messaggio;
   - il disco richiede i numeri di 5.5, snapshot di policy compresi anche senza valutazione;
   - una `chart_dir` di 166 caratteri o più si rifiuta.
7. **Crash prima del primo checkpoint** → `interrupted`, senza resume automatico, con la richiesta all'utente.

**Reale**: solo con un OK esplicito, a macchina libera dal programma del solver (dopo T3-T11) e con un bin set a 3 seat
registrato.

### E4. Motore `postflop_exact` con il `gto_cli` di oggi

**Scope**: 4.1-4.11.

**Accettazione, in mock**
1. **Parser**: test sulle righe di 4.5, compresi `1.9e-16`, `nan`, `inf`, un header ritardato e righe `solver_phase`.
2. **Config**:
   - il round trip di `tests/fixtures/postflop_check_only.json` (parse → modello → scrittura) è semanticamente uguale;
   - le chiavi sconosciute si rifiutano;
   - le mutazioni di 4.3 si rifiutano con l'errore sul campo giusto.
3. **Golden argv** per validate, estimate, solve e resume (`cert_interval` assente).
4. **Ciclo di vita**:
   - `queued → running → paused` (pausa via file di controllo) `→ resume → done`;
   - cancel → `cancelled`, con il checkpoint;
   - un controllo scritto durante la preparazione viene riscritto dopo l'header;
   - un file di controllo con contenuto non riconosciuto viene ignorato e cancellato dal mock, come dal CLI;
   - un resume con `N` minore delle iterazioni fatte porta a `failed` con `checkpoint_mismatch`;
   - kill dell'albero verificato.
5. **Admission**:
   - la RAM viene da `lazy_peak_bytes`;
   - un exit 3 porta a `blocked`, con i numeri;
   - il solve conta 8 thread;
   - la guardia d'avvio aspetta `startup_timeout_s`.
6. **Risultati**:
   - la scheda di riepilogo coincide con il `report.json` della fixture;
   - la curva viene dalle righe `progress`;
   - il banner dei range uniformi è visibile;
   - il browser dei nodi è disabilitato, con il motivo.

**Accettazione reale** (in finestra, con OK, dopo QA3(b))
1. `postflop validate` ed `estimate` su `tests/fixtures/postflop_check_only.json`.
2. `solve` di 20 iterazioni (circa 1 s):
   - `status=completed`;
   - `report.json` con `iterations` 20, `nodes` 3270, `infosets` 1015872, come lo smoke del 01/10.
3. Pausa e resume su un solve più lungo, con `N` scelto in modo che duri almeno 1 minuto [I: misura prima la velocità].

### E5. Vista della parte A

**Scope**: 5.10, con una fixture sintetica. I dati veri si aggiungono dopo T7.

**Accettazione**
1. Le etichette "per classe" e "per combo" compaiono entrambe.
2. L'EV si mostra come `ev_direct ± SE`.
3. L'etichetta "provvisorio, 64 flop fisici" è presente.
4. La checklist G3 si calcola dalla fixture, e la soglia si legge dalla config.
5. `OUT1/part_a` si legge senza scritture.
6. Il progresso di `cli_<n>.progress.log` si segue in tail.

### E6. Studio postflop completo (contratto 4.12)

**Scope**:
- contratto documentato in `UI/docs/POSTFLOP_STUDY_CONTRACT.md`, versionato;
- mock di `study-solve`, degli eventi JSONL e di `serve`;
- editor dei range (9×9 → testo);
- target dEV;
- browser dei nodi su una sessione `serve`.

**Accettazione, in mock**
1. Il round trip dell'editor dei range dà gli stessi pesi per classe per 10 range d'esempio, pesi e intervalli compresi; il
   testo emesso usa solo classi singole (4.12) e un parser di prova che segue `CLI:690-800` lo rilegge uguale.
2. Ciclo di vita del worker: admission; chiusura per inattività; kill per identità; al massimo `max_workers`; una GET non
   lancia mai processi.
3. La matrice postflop aggrega le 630 combo meno quelle bloccate in 81 classi; si verifica su una fixture costruita a mano.

**Reale**: solo quando i comandi C++ esistono (QA2).

---

## 8. Domande aperte per l'utente

Ci sono soltanto domande a cui il codice non sa rispondere. Falle quando la milestone indicata nella colonna "Blocca" sta per
iniziare, tutte insieme. Fino alla risposta vale il default.

**Risposte dell'utente del 02/10.** QA1 e QA4 hanno avuto risposta verso le 01:30; QA2 e QA5, spiegate all'utente (chiarimento
nella riga), poco dopo, entro le 01:35 [V, nota di memoria della sessione `ui-postflop-decisions-2026-10-02.md`]. Ogni risposta è
scritta nella sua riga. Restano aperte QA3, QA6 e QA7: per loro vale ancora il default.

**Altre decisioni dello stesso momento che toccano la UI** [V, stessa nota]. (1) I test legacy del postflop (`phase7`,
`phase10`, `gto_plus_reference`) si rifanno nel ciclo di build e test dopo il run 3-way 1, e i guasti si correggono prima che
la UI usi davvero `gto_cli`: è la seconda via della parte (b) di QA3 [I], che per il resto resta aperta. (2) Un lettore del
postflop delle policy dello step 2 (HU e 3-way: il postflop sparso con le righe per classe di board), anche se approssimato:
oggi si esportano solo le chart preflop; lo strumento è da scrivere e la UI lo mostrerà.

| # | Domanda | Default fino alla risposta | Blocca |
|---|---|---|---|
| QA1 | **Range degli studi HU postflop.** Da dove vengono: inseriti a mano (sintassi GTO+ o editor 9×9), ricavati più avanti dal preflop blueprint, o entrambe le cose? E ti serve il passo intermedio con range uniformi (E4), oppure si passa direttamente allo studio completo (E6)? | **Risposta (02/10): entrambe le fonti.** (1) La nostra soluzione preflop: i range che arrivano al flop lungo una linea preflop, ricavati dalle chart; (2) l'inserimento a mano, con l'editor 9×9 o con la sintassi GTO+ (4.12). Il passaggio dalla soluzione preflop ai range del postflop esatto è da scrivere: oggi nessun codice del blueprint produce range per il postflop [V, `webui2/gto_cli.md`]. Resta aperto se serve il passo E4 con range uniformi: fino ad allora E4 si fa in mock, perché collauda l'adattatore, e la sua accettazione reale aspetta la risposta su E4 | E4 reale (passo intermedio), E6 |
| QA2 | **Comandi C++ nuovi** (solve con range e target dEV, eventi JSONL, `serve`): dentro `gto_cli` o in un eseguibile nuovo e snello (per esempio `gtosd_postflop`)? Chi li scrive e in quale finestra di build, rispetto al programma del solver (handoff T3-T11)? Con la GUI desktop in rimozione (C13), sono l'unica strada per uno studio postflop completo. *Chiarimento (02/10):* i tre comandi sono (1) un solve con i range dell'utente e un target di precisione (dEV) al posto del numero fisso di iterazioni, (2) gli eventi di progresso in JSON Lines, (3) un worker a vita lunga (`query`/`serve`) che tiene la soluzione caricata e risponde al browser dei nodi; contratto in 4.12 | **Risposta (02/10): dentro `gto_cli`**, scritti dopo la chiusura del lavoro 3-way. Fino ad allora nessun C++: la UI usa il mock del contratto 4.12 | E6 reale |
| QA3 | **Bin set.** (a) Posso registrare `out/monker/bin_3way_step2` usando le sha256 del suo `SOURCE.txt` (stesso formato di `SHA256SUMS.txt`, commit `12fe441f` nell'intestazione, senza README)? (b) Per `gto_cli`: si congela l'exe della build suite del 28/09, più le 3 DLL, in una cartella nuova con `SHA256SUMS.txt` e `README.txt`? Oppure prima si ricompila e si rifanno i test legacy (`phase7`, `phase10`, `gto_plus_reference`), che non girano da quando `libs/core` è cambiata il 28/09? | Niente registrazione: 3-way e postflop restano in mock | E3 reale, E4 reale |
| QA4 | **Monker nelle viste 3-way.** La revisione del 01/10 ha tolto i riferimenti esterni dall'interfaccia. I file del 3-way però contengono la distanza da Monker (`vs_monker`), il riferimento dello step 1 e, nella parte A, "chart Monker nel nostro gioco". Li mostro nel 3-way come informazione di ricerca, oppure restano nascosti? | **Risposta (02/10): MonkerSolver non serve più nella UI.** La distanza da Monker (`vs_monker`, anche come linea di riferimento dello step 1) e le chart Monker nel nostro gioco (`chart_sets`) restano nascoste in tutte le viste, 3-way e parte A comprese: il default diventa la regola. Le chart dello step 1 (5.9) sono nostre, non di MonkerSolver, e restano mostrabili come riferimento [I: lettura della risposta] | Nessuna (risolta) |
| QA5 | **Test nativi della UI.** Quando posso far girare il solver reale per i test (smoke di `gto_cli` di circa 1 s, osservazione del run vivo), visto il programma del 02-03/10 (run 1, poi T3-T11)? Un trainer reale lanciato dalla UI tiene chiuso il gate della coda del run 1 (2.3). *Chiarimento (02/10):* la domanda è quando la UI può lanciare il solver vero per i propri test nativi | **Risposta (02/10):** solo quando non è attivo nessun run di training. L'osservazione in sola lettura dei run vivi (`OUT1`, E2.7) è sempre ammessa | E3, E4 reali (solo senza run attivi) |
| QA6 | **Code 3-way a mano.** La UI resta in sola lettura sulle code esterne (D6), oppure vuoi un pulsante "Annulla coda" (`QUEUE_CANCEL`, con doppia conferma)? E i prossimi run 3-way (il test del blocco, T11) partono dalla coda a mano oppure dalla UI? | Sola lettura; T11 con la coda a mano | E3 reale |
| QA7 | **Soglie da mostrare come verdetto.** In HU, con il gate astratto ritirato, la linea dell'1 % del pot resta solo un riferimento? Nel 3-way, il gate G3 (≤ 0,04 a per seat) vale sul gain **per classe** (come il codice) o **per combo** (come l'handoff)? | HU: solo riferimento, nessun verdetto. G3: si mostrano entrambi i valori; il verdetto usa la classe, con un avviso | E5 |

Default che applichi senza aspettare una risposta: annotali in `DECISIONS.md` e annunciali all'utente insieme alle
domande della milestone (l'utente può cambiarli):
- fine finestra a 3 seat con `PAUSE` tenuto presente, e `STOP` riservato a stop rule e "Termina ora" (5.3);
- merge (non rebase) del branch di integrazione (E0), annunciato prima;
- RAM dal picco misurato quando esiste (5.5);
- limite di 165 caratteri per la cartella delle chart (5.2).

---

## Appendice A: le 54 chart 3-way 50a (albero 3WAY50 donk)

Generata da `chart_table_3way.py`, che legge le intestazioni di `MONKER3` e usa `parent_decisions` del `compare_charts.py`
congelato [V]. I fold impliciti sono fra parentesi. "**no**" = non fronteggia un all-in: sono le 18 chart della metrica d'arresto.

| # | Chart | Linea | Azioni | All-in? |
|---|---|---|---|---|
| 1 | `UTG/UTG_strategy.txt` | root → **UTG** | AllIn,6.0ante,Call,Fold | **no** |
| 2 | `UTG/UTG_6.0ante_BTN_AllIn_UTG_strategy.txt` | UTG 6.0ante (CO Fold) BTN AllIn → **UTG** | Call,Fold | sì |
| 3 | `UTG/UTG_6.0ante_CO_AllIn_BTN_Call_UTG_strategy.txt` | UTG 6.0ante CO AllIn BTN Call → **UTG** | Call,Fold | sì |
| 4 | `UTG/UTG_6.0ante_CO_AllIn_UTG_strategy.txt` | UTG 6.0ante CO AllIn (BTN Fold) → **UTG** | Call,Fold | sì |
| 5 | `UTG/UTG_6.0ante_CO_Call_BTN_AllIn_UTG_strategy.txt` | UTG 6.0ante CO Call BTN AllIn → **UTG** | Call,Fold | sì |
| 6 | `UTG/UTG_Call_BTN_6.0ante_UTG_strategy.txt` | UTG Call (CO Fold) BTN 6.0ante → **UTG** | AllIn,Call,Fold | **no** |
| 7 | `UTG/UTG_Call_BTN_AllIn_UTG_strategy.txt` | UTG Call (CO Fold) BTN AllIn → **UTG** | Call,Fold | sì |
| 8 | `UTG/UTG_Call_CO_7.0ante_BTN_AllIn_UTG_strategy.txt` | UTG Call CO 7.0ante BTN AllIn → **UTG** | Call,Fold | sì |
| 9 | `UTG/UTG_Call_CO_7.0ante_BTN_Call_UTG_strategy.txt` | UTG Call CO 7.0ante BTN Call → **UTG** | AllIn,Call,Fold | **no** |
| 10 | `UTG/UTG_Call_CO_7.0ante_UTG_strategy.txt` | UTG Call CO 7.0ante (BTN Fold) → **UTG** | AllIn,Call,Fold | **no** |
| 11 | `UTG/UTG_Call_CO_AllIn_BTN_Call_UTG_strategy.txt` | UTG Call CO AllIn BTN Call → **UTG** | Call,Fold | sì |
| 12 | `UTG/UTG_Call_CO_AllIn_UTG_strategy.txt` | UTG Call CO AllIn (BTN Fold) → **UTG** | Call,Fold | sì |
| 13 | `UTG/UTG_Call_CO_Call_BTN_7.0ante_UTG_strategy.txt` | UTG Call CO Call BTN 7.0ante → **UTG** | AllIn,Call,Fold | **no** |
| 14 | `UTG/UTG_Call_CO_Call_BTN_AllIn_UTG_strategy.txt` | UTG Call CO Call BTN AllIn → **UTG** | Call,Fold | sì |
| 15 | `UTG/UTG_Call_CO_Call_BTN_7.0ante_UTG_Call_CO_AllIn_BTN_Call_UTG_strategy.txt` | … BTN 7.0ante UTG Call CO AllIn BTN Call → **UTG** | Call,Fold | sì |
| 16 | `UTG/UTG_Call_CO_Call_BTN_7.0ante_UTG_Call_CO_AllIn_BTN_Fold_UTG_strategy.txt` | … BTN 7.0ante UTG Call CO AllIn BTN Fold → **UTG** | Call,Fold | sì |
| 17 | `CO/CO_strategy.txt` | (UTG Fold) → **CO** | AllIn,6.0ante,Call,Fold | **no** |
| 18 | `CO/UTG_6.0ante_CO_strategy.txt` | UTG 6.0ante → **CO** | AllIn,Call,Fold | **no** |
| 19 | `CO/UTG_AllIn_CO_strategy.txt` | UTG AllIn → **CO** | Call,Fold | sì |
| 20 | `CO/UTG_Call_CO_strategy.txt` | UTG Call → **CO** | AllIn,7.0ante,Call,Fold | **no** |
| 21 | `CO/CO_6.0ante_BTN_AllIn_CO_strategy.txt` | (UTG Fold) CO 6.0ante BTN AllIn → **CO** | Call,Fold | sì |
| 22 | `CO/CO_Call_BTN_6.0ante_CO_strategy.txt` | (UTG Fold) CO Call BTN 6.0ante → **CO** | AllIn,Call,Fold | **no** |
| 23 | `CO/CO_Call_BTN_AllIn_CO_strategy.txt` | (UTG Fold) CO Call BTN AllIn → **CO** | Call,Fold | sì |
| 24 | `CO/UTG_6.0ante_CO_Call_BTN_AllIn_UTG_Call_CO_strategy.txt` | UTG 6.0ante CO Call BTN AllIn UTG Call → **CO** | Call,Fold | sì |
| 25 | `CO/UTG_6.0ante_CO_Call_BTN_AllIn_UTG_Fold_CO_strategy.txt` | UTG 6.0ante CO Call BTN AllIn UTG Fold → **CO** | Call,Fold | sì |
| 26 | `CO/UTG_Call_CO_7.0ante_BTN_AllIn_UTG_Call_CO_strategy.txt` | UTG Call CO 7.0ante BTN AllIn UTG Call → **CO** | Call,Fold | sì |
| 27 | `CO/UTG_Call_CO_7.0ante_BTN_AllIn_UTG_Fold_CO_strategy.txt` | UTG Call CO 7.0ante BTN AllIn UTG Fold → **CO** | Call,Fold | sì |
| 28 | `CO/UTG_Call_CO_7.0ante_BTN_Call_UTG_AllIn_CO_strategy.txt` | UTG Call CO 7.0ante BTN Call UTG AllIn → **CO** | Call,Fold | sì |
| 29 | `CO/UTG_Call_CO_7.0ante_UTG_AllIn_CO_strategy.txt` | UTG Call CO 7.0ante (BTN Fold) UTG AllIn → **CO** | Call,Fold | sì |
| 30 | `CO/UTG_Call_CO_Call_BTN_7.0ante_UTG_AllIn_CO_strategy.txt` | UTG Call CO Call BTN 7.0ante UTG AllIn → **CO** | Call,Fold | sì |
| 31 | `CO/UTG_Call_CO_Call_BTN_7.0ante_UTG_Call_CO_strategy.txt` | UTG Call CO Call BTN 7.0ante UTG Call → **CO** | AllIn,Call,Fold | **no** |
| 32 | `CO/UTG_Call_CO_Call_BTN_7.0ante_UTG_Fold_CO_strategy.txt` | UTG Call CO Call BTN 7.0ante UTG Fold → **CO** | AllIn,Call,Fold | **no** |
| 33 | `CO/UTG_Call_CO_Call_BTN_AllIn_UTG_Call_CO_strategy.txt` | UTG Call CO Call BTN AllIn UTG Call → **CO** | Call,Fold | sì |
| 34 | `CO/UTG_Call_CO_Call_BTN_AllIn_UTG_Fold_CO_strategy.txt` | UTG Call CO Call BTN AllIn UTG Fold → **CO** | Call,Fold | sì |
| 35 | `BTN/CO_6.0ante_BTN_strategy.txt` | (UTG Fold) CO 6.0ante → **BTN** | AllIn,Call,Fold | **no** |
| 36 | `BTN/CO_AllIn_BTN_strategy.txt` | (UTG Fold) CO AllIn → **BTN** | Call,Fold | sì |
| 37 | `BTN/CO_Call_BTN_strategy.txt` | (UTG Fold) CO Call → **BTN** | AllIn,6.0ante,Check | **no** |
| 38 | `BTN/UTG_6.0ante_BTN_strategy.txt` | UTG 6.0ante (CO Fold) → **BTN** | AllIn,Call,Fold | **no** |
| 39 | `BTN/UTG_6.0ante_CO_AllIn_BTN_strategy.txt` | UTG 6.0ante CO AllIn → **BTN** | Call,Fold | sì |
| 40 | `BTN/UTG_6.0ante_CO_Call_BTN_strategy.txt` | UTG 6.0ante CO Call → **BTN** | AllIn,Call,Fold | **no** |
| 41 | `BTN/UTG_AllIn_BTN_strategy.txt` | UTG AllIn (CO Fold) → **BTN** | Call,Fold | sì |
| 42 | `BTN/UTG_AllIn_CO_Call_BTN_strategy.txt` | UTG AllIn CO Call → **BTN** | Call,Fold | sì |
| 43 | `BTN/UTG_Call_BTN_strategy.txt` | UTG Call (CO Fold) → **BTN** | AllIn,6.0ante,Check | **no** |
| 44 | `BTN/UTG_Call_CO_7.0ante_BTN_strategy.txt` | UTG Call CO 7.0ante → **BTN** | AllIn,Call,Fold | **no** |
| 45 | `BTN/UTG_Call_CO_AllIn_BTN_strategy.txt` | UTG Call CO AllIn → **BTN** | Call,Fold | sì |
| 46 | `BTN/UTG_Call_CO_Call_BTN_strategy.txt` | UTG Call CO Call → **BTN** | AllIn,7.0ante,Check | **no** |
| 47 | `BTN/CO_Call_BTN_6.0ante_CO_AllIn_BTN_strategy.txt` | (UTG Fold) CO Call BTN 6.0ante CO AllIn → **BTN** | Call,Fold | sì |
| 48 | `BTN/UTG_Call_BTN_6.0ante_UTG_AllIn_BTN_strategy.txt` | UTG Call (CO Fold) BTN 6.0ante UTG AllIn → **BTN** | Call,Fold | sì |
| 49 | `BTN/UTG_Call_CO_7.0ante_BTN_Call_UTG_AllIn_CO_Call_BTN_strategy.txt` | UTG Call CO 7.0ante BTN Call UTG AllIn CO Call → **BTN** | Call,Fold | sì |
| 50 | `BTN/UTG_Call_CO_7.0ante_BTN_Call_UTG_AllIn_CO_Fold_BTN_strategy.txt` | UTG Call CO 7.0ante BTN Call UTG AllIn CO Fold → **BTN** | Call,Fold | sì |
| 51 | `BTN/UTG_Call_CO_Call_BTN_7.0ante_UTG_AllIn_CO_Call_BTN_strategy.txt` | UTG Call CO Call BTN 7.0ante UTG AllIn CO Call → **BTN** | Call,Fold | sì |
| 52 | `BTN/UTG_Call_CO_Call_BTN_7.0ante_UTG_AllIn_CO_Fold_BTN_strategy.txt` | UTG Call CO Call BTN 7.0ante UTG AllIn CO Fold → **BTN** | Call,Fold | sì |
| 53 | `BTN/UTG_Call_CO_Call_BTN_7.0ante_UTG_Call_CO_AllIn_BTN_strategy.txt` | UTG Call CO Call BTN 7.0ante UTG Call CO AllIn → **BTN** | Call,Fold | sì |
| 54 | `BTN/UTG_Call_CO_Call_BTN_7.0ante_UTG_Fold_CO_AllIn_BTN_strategy.txt` | UTG Call CO Call BTN 7.0ante UTG Fold CO AllIn → **BTN** | Call,Fold | sì |

**Etichette.**
- `6.0ante` è l'open o l'iso-raise a 6 a; `7.0ante` è il raise a 7 a dopo un limp.
- `Call` in un piatto non rilanciato si mostra come "Limp".

---

## Appendice B: fonti lette per questo addendum

- **Prompt e handoff.** `docs/solver-ui/WEB_UI_PROTOTYPE_PROMPT.md` (per intero, appendici comprese) e
  `docs/handoff/NEXT_STEPS_2026-10-02.md` (§1-§8).
- **Ricerca.** `docs/research/preflop_vector_cfr/MONKER_RECIPE_REPRODUCTION_2026-09-28.md` §10 e §10.13.
- **Memoria.** `preflop-product-target.md`.
- **UI.** `UI/docs/DECISIONS.md`; `UI/backend/solver_ui/{scheduler.py:860-890, models.py:131-210, assets.py:8-70, tailer.py:1-70}`.
- **`gto_cli`.** `CLI:115-236, 253-462, 686-700, 3655-3730`.
- **Libreria postflop.** `libs/postflop/src/postflop_solver.cpp:19725-19760` (con grep su `completed_iterations`);
  `include/gtosd/postflop/postflop_solver.hpp:700-730`.
- **Config dell'albero.** `include/gtosd/tree/config.hpp`; `libs/tree/src/config.cpp:90-200` (con grep su `contains`);
  `tests/fixtures/postflop_check_only.json`.
- **Trainer e export.** `TR:1-24, 284-300, 620-660` (con grep sulle opzioni a 3 seat); `benchmarks/preflop_blueprint_export.cpp:1-40`;
  `include/gtosd/preflop_blueprint/query_worker.hpp:1-40`.
- **Run 1 e copie congelate.** `R` (righe 25-175); `out/monker/bin_3way_step2/SOURCE.txt`; da `OUT1`, i `memory_breakdown`
  di `train.jsonl` e la coda di `run.log` e `queue.log`.
- **Comandi.** `Get-ChildItem` di `out/monker/correctness` e `out/monker/variants`; `file tools/monker_compare/compare_charts.py` (CRLF).
- **Rapporti di studio del 02/10** (`webui2/`): `ui_map.md`, `gto_cli.md`, `threeway_formats.md`, `chart_table_3way.md`.
- **Revisione (00:55-01:17 del 02/10).** `CLI:49-61, 105-470, 686-800, 3640-3760, 4270-4370`;
  `apps/gto_cli/CMakeLists.txt`; `CMakeLists.txt:14, 57-62, 104-116`; `libs/postflop/src/postflop_solver.cpp:4495-4590,
  17020-17075, 19070-19085, 19725-19775, 20824-20912`; `include/gtosd/postflop/postflop_solver.hpp:395-440, 680-735`;
  `libs/tree/src/config.cpp:1-410, 461-484`; `libs/core/src/{cards,money,ranges}.cpp`; `TR:1-26, 436-520, 575-660, 876-910,
  1264-1290, 1545-1563`; `R` e `Q` per intero; `FZ/SOURCE.txt`; `FZ/tools/monker_compare/compare_charts.py:55-70, 150-200`;
  della UI `assets.py`, `builders.py:118-187`, `models.py:128-234`, `scheduler.py:172-230, 355-405, 447-540, 609-700,
  855-900`, `adapters.py:76-84, 205-252, 290-304, 433-449`, `charts.py:18, 58-122`, `api.py:45-52, 180-196, 254-278,
  410-430, 480-495`, `telemetry.py:47-70`, `tailer.py:20-58`, `filesystem.py:84-92`, `docs/{ARCHITECTURE,DECISIONS}.md`,
  `config/solver-ui.example.toml`; `git log 2aa24d8..c2e9138` in `WT`; `SP/cleanup/{delete_data,post_delete}.log`
  (righe finali); da `OUT1`, `queue.log`, `run.log`, `train.jsonl` e il listato della cartella. L'Appendice A è stata
  ricontrollata contro le intestazioni di `MONKER3` e contro l'algoritmo di `charts.py:71-101`: 54/54 uguali.

---

## Appendice C: correzioni della revisione del 02/10 (00:55-01:17)

1. **GUI desktop in rimozione** (C13, sezione 1): la bozza diceva che `gto_gui` restava come ripiego. In `WT` i commit
   `921f424`..`c2e9138` la tolgono per decisione dell'utente, e il messaggio dice che la web UI lancia `gto_cli`. Di
   conseguenza i tre comandi di 4.12 non sono più facoltativi; QA2 non chiede più del ripiego desktop.
2. **Pulizia del 02/10 alle 00:53** (C6, F6, P20): la bozza diceva che le run di correttezza erano junction verso `F:`. Sono
   state cancellate, insieme ai binari di quasi tutte le varianti.
3. **`PAUSE` va tenuto presente** (5.3, E3): runner e trainer lo cancellano all'avvio; la bozza non lo diceva.
4. **Nuovo difetto F8**: la guardia d'avvio di 15 s ucciderebbe ogni resume a 3 seat, perché il trainer carica il
   checkpoint da 13,56 GB prima di scrivere `start`.
5. **Stime a 3 seat** (P6, 5.5): la bozza diceva solo che la formula RAM di oggi supera 16 GB; dà circa 31,8 GB, più del
   doppio dei 14,91 GB misurati. La formula del disco dimentica gli snapshot quando non c'è valutazione. Velocità
   aggiornata a 1,745 s per iterazione su 1.500 iterazioni.
6. **Builder** (P5, 5.1, 5.2): `--policy-snapshots` oggi dipende dalla valutazione; `--seed` e `--lazy-discount-epoch` sono
   sempre passati; `--three-way-table` è rifiutato su un gioco HU; default del form per `partition_target` e
   `progress_every`.
7. **`gto_cli`** (4.3-4.10): regole della config verificate nel parser (sintassi delle carte, `minimum_bet_units` > 0,
   numero di liste di `sizes_by_raise_count_bp`, bande di arrotondamento, nomi degli errori); checkpoint atomico via
   `.tmp` e di circa 4 B × azioni; `checkpoint_mismatch` (exit 1) al resume; righe di crash; contenuto non riconosciuto
   del file di controllo; numeri di riga corretti (`CLI:310-312`, `314-316`); i limiti su range e target sono solo del CLI.
8. **Range** (4.12): gli intervalli seguono gli ID delle classi; l'editor emette classi singole.
9. **Riferimenti**: handoff `:677-680` (non `:583-587`); `Q:38` per la memoria (non `Q:50`); righe di `CMakeLists.txt`
   delle app e del preflop; testo esatto della regex di `assets.py:66`; numeri di riga del solver al commit `9ab2035`.
10. **Regole**: `apps/solver-ui` escluso dalla sola lettura di `apps/`; le scritture già decise sotto `out/solver-ui-mock/`;
   niente download di strumenti; fixture senza `policy.bin`; trainer reali della UI e gate della coda (2.3, QA5); merge
   di E0 annunciato prima.
