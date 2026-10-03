# Diario dell'agent coder: solver preflop vettoriale

Roadmap: [PREFLOP_VECTOR_CFR_CODER_ROADMAP_2026-09-15.md](../../archive/preflop-blueprint-research-2026-09/PREFLOP_VECTOR_CFR_CODER_ROADMAP_2026-09-15.md)
Registro decisioni: [PREFLOP_ARCHITECTURE_DECISION_LOG.md](../PREFLOP_ARCHITECTURE_DECISION_LOG.md)

Regole del diario: le voci non si cancellano; una correzione è una nuova voce che rimanda alla
precedente. Un fallimento si registra prima di tentare la correzione. Aggiornare a fine di ogni
sessione, a ogni gate e a ogni dubbio bloccante.

> **Nota del 2026-10-03 (archivio dei documenti).** Le voci precedenti al 2026-10-03 citano
> strumenti che ora esistono solo al tag `history7-final` (= `88118a6`, l'ultimo albero con history7):
> `tools/preflop_suite/` (`suite.py`, `run_queue.sh`), `benchmarks/suite/` tranne `fixtures/`,
> l'opzione `--history-rows` delle CLI, gli eseguibili `gtosd_preflop_blueprint_abstract_br`,
> `gtosd_preflop_blueprint_history_rows` e `gtosd_preflop_blueprint_history_census` e gli script
> `scripts/research/run_hu40_history7_solve.ps1` e `finalize_hu40_t37000.ps1`.
>
> I report di P9, della suite e di history7 citati qui sono in `docs/archive/`, e i link sono
> aggiornati. Alcuni dump JSON del 19/09 citati per nome, come
> `CO40_FULL_BUCKET_AUDIT_2026-09-19.json` e `TEMPORAL_SCREEN_2026-09-19.json`, sono stati
> cancellati e restano al tag `docs-pre-cleanup-2026-10-02`. Le voci non sono state riscritte.

## 1. Stato corrente

| Campo | Valore |
|---|---|
| Fase in corso | Riproduzione della ricetta MonkerSolver per il preflop multiway (dal 2026-09-28). HU50: configurazione di riferimento G1 decisa il 2026-09-29 (astrazione compatta 15 livelli × 4 + TX2, donk bet, una size; distanza da MonkerSolver 0,0641); dal 2026-09-30 riaperta sul rake (l'utente: "molto probabile" rake 5 %, cap 3a, no flop no drop; le impostazioni vere non sono note). Il preflop di MonkerSolver, con il postflop che il nostro CFR impara contro i suoi range, non è un equilibrio del nostro gioco: migliore risposta preflop al CO 1,44 % del piatto senza rake (test 5), 0,92-1,13 % con le ipotesi di rake 5 % / cap 3a, 2a o 0,75a e 2,5 % / cap 2a (test 6-6d), 1,60-1,80 % con un postflop più fine (test 5M-5D: raffinare l'astrazione non è la leva); il nostro preflop nel suo gioco circa 0,1 %; limite del test: postflop appreso contro range fissi, non prova che nessun postflop lo renda un equilibrio (manca la migliore risposta preflop contro un postflop risolto esattamente). Contro la nostra strategia le chart di MonkerSolver restano equivalenti in EV (al massimo lo 0,09 % del piatto). 3-way 50a: fasi 1, 2a e 2b fatte (2b chiusa alle 05:20 del 2026-09-30, 90/90 test); prime chart 3-way del passo 1 in cinque giochi (distanza da MonkerSolver 0,245-0,288, come il passo 1 HU); fase 3 (postflop sparso a tre) dopo le decisioni dell'utente. Pomeriggio del 2026-09-30: il gioco con i bucket di default di MonkerSolver (30 × 4) e rake 2,5 % / cap 2a dà le chart più vicine finora (distanza 0,0568 / differenza di range 0,320 all'arresto a 32.000; 0,0532 / 0,289 a 48.000 nell'estensione in corso) e la preferenza suited di MonkerSolver (0,214 contro 0,224); il preflop di MonkerSolver bloccato in quel gioco lascia al CO ancora l'1,03-1,04 % (test 7, anche con il doppio delle iterazioni): fra le leve provate il rake è quella che abbassa di più lo scarto (−35 %), un bucket più grossolano lo abbassa del 10-11 %, raffinare l'astrazione lo alza; texture più grossolane (TXM, TXM2) non ancora provate con il preflop bloccato. Specifica della fase 3 del 3-way scritta e criticata; decisioni D1-D4 e D7 prese il 2026-10-01 (ambito ridotto sull'i3, senza server); fase 3a scritta, integrata e rivista il 2026-10-01; gate 3a completo alle 18:11 (tutto PASS tranne la clausola della traiettoria di V7, accettata), merge `238a41e` nel branch di fase, primo run 3WAY50 (15 × 4 double, rake 2,5 % / cap 2a, 1,5 s per iterazione misurati allo smoke) in coda per le 00:00 del 2026-10-02, partito alle 00:09:30 (1,745 s per iterazione sulle prime 1.500). Batteria di correttezza HU del passo 2 HU50 chiusa il 2026-10-01 con 0 FAIL (dati dei run cancellati il 2026-10-02, conclusioni nei documenti). Notte fra il 2026-10-01 e il 2026-10-02: fase 3b, parte A scritta e rivista per lettura ma non compilata, arbitro Python del postflop 3-way committato (`2aa24d8`); history7 da togliere per decisione dell'utente (tag `history7-final` = `88118a6`; rimozione pronta in una sandbox, non applicata), e con lui la suite HU10-HU40 e il gate della best response astratta; vecchia GUI desktop tolta in `feat/threeway-step2` (la libreria `gui_prototype` in un commit rinviato); pulizia di 445,34 GiB eseguita dall'utente. Ambito del prodotto (utente, 2026-10-01 sera): un solo solver con preflop e postflop. Decisioni dell'utente del 2026-10-02 fra le 01:30 e le 01:35 sulla web UI (QA1, QA2, QA4, QA5), sui test legacy del postflop e su un lettore del postflop dello step 2 (diario). Ottimizzazione dei 35 minuti di HU40 in pausa (la suite HU10-HU40 si ritira con history7). **2026-10-02, 19:00:** il run 3-way 1 è finito alle 10:38 (`STOPPED` a 24.000, distanza 0,0645 dalle 54 chart di MonkerSolver; sulle 40 chart confrontabili con il passo 1 dello stesso rake 0,072 contro 0,264), la parte A è compilata, committata (`ec3eec5`) ed eseguita sul run 1 con il gate G3 "PASS provvisorio" (64 flop campionati, identità del rake in forma statistica) e il test del blocco 3-way è in coda per le 20:00 (diario del 2026-10-02 09:00-19:00, ricetta 9.8) |
| Ultimo gate | P8 PASS (2026-09-16) |
| Branch di integrazione | `feature/preflop-blueprint` |
| Branch di fase | `feat/monker-step1-checkdown` (dal 2026-09-28, da `feat/preflop-phase1-time`); in precedenza `feat/preflop-phase1-time` e `codex/fix-preflop-deep-stack-convergence` |
| Worktree | `C:/Users/GoryNickel/Documents/GitHub/GTO-Solver` |
| Commit di partenza | `744113c69342a82f3b920add498106af2b763d52`; correzione normalizzazione in `17984a9` |
| Build | **2026-10-02, dopo il run 1: build riprese nel worktree della fase 3** (parte A `ec3eec5` con la CLI congelata in `out/monker/bin_3way_partA`, commit D1 `8c70044` con ctest 102/102 e test legacy 48/48, sandbox di history7 in `out/laneH/build` con ctest 101/101 e V1 IDENTICAL, sandbox delle mutazioni in `out/review3b`); quanto segue è lo stato della notte. **Notte fra il 2026-10-01 e il 2026-10-02: nessuna build** (le catene della parte A e di history7 sono state annullate alle 23:38 per proteggere il run 3-way): la parte A, la rimozione di history7 (sandbox `out/laneH` del worktree della fase 3, senza cartella di build) e la rimozione della GUI non sono mai state compilate e si compilano dopo il run. L'ultima build è quella della suite del worktree della fase 3 a `12fe441` (2026-10-01, 16:30-16:33). Il 2026-10-02 l'utente ha cancellato le build vecchie di `out/build` (fra cui `windows-gui-release`); restano `windows-release`, `windows-release-current`, `windows-release-main-integration` e `windows-release-suite`, e tutti gli eseguibili congelati (`out/monker/bin` e `out/monker/bin_*`). Storia: `out/build/windows-release-suite` (dal 2026-09-28 le build di `feat/monker-step1-checkdown`, fino a `67de1d7` del 2026-09-30; in precedenza HEAD `ba93c75` per la baseline e le candidate), Release, MSVC, /W4 /WX; copie degli eseguibili per i run lunghi in `out/monker/bin`, `out/monker/bin_texture`, `out/monker/bin_allin` (`410a380`), `out/monker/bin_abd` (`5578ab8`), `out/monker/bin_lock` (`c7d6ba0`), `out/monker/bin_rake` (`3ec4027`), `out/monker/bin_threeway` (tabella a tre giocatori), `out/monker/bin_3way_step1` (`67de1d7`, passo 1 a tre giocatori), `out/monker/bin_3way_step2` (dal 2026-10-01, `12fe441`, passo 2 a tre giocatori, primo run 3WAY50) e `out/monker/bin_correct/c123` (dal 2026-09-30, gli eseguibili congelati dei certificati di correttezza: `2184d66` + `591724c` + `dc34131` + `68cf367`); copie congelate del runner in `out/frozen/` (anche `run_step2_continuous_lock.sh`, `run_step2_continuous_rake.sh` e, dal 2026-09-30, `run_step2_continuous_algo.sh` con `LAZY_ARG` per l'opzione A; dal 2026-10-01 `threeway_step2_12fe441/` (runner, `compare_charts.py`, configurazione, manifest e mappa TX2 del primo run 3-way, da `git show 12fe441`) e `queue_3way50_15x4.sh`); eseguibili, bucket, risorse, runner congelati e build restano sull'SSD C: |
| Archivio dei run | Dal 2026-09-30 (decisione dell'utente verso le 12:00, prima dell'inizio dello spostamento alle 12:01:28): i run si allenano sull'SSD C: e dopo la loro valutazione vanno sul disco USB F: (Seagate Basic da 2 TB) in `F:\GTO-Solver-out`, stessi percorsi relativi, con una directory junction al vecchio percorso (lettura trasparente); script `archive_run.ps1` nella cartella temporanea della sessione, che salta le cartelle con `NO_ARCHIVE` e dalle 14:31 prende un lock esclusivo per cartella. Spostate 37 cartelle (299,7 GiB, 4.515 file) dalle 12:01 alle 14:18, ognuna verificata in file e byte; dopo, C: ha 308,5 GiB liberi. Una junction non si cancella mai in modo ricorsivo. Il 2026-10-02 fra le 00:48 e le 00:53 l'utente ha cancellato 445,34 GiB (su F: i dati di history7 e della suite, i run della batteria di correttezza, i vecchi smoke e i binari delle varianti superate; su C: il worktree di Codex e le vecchie build) e le junction rimaste senza destinazione sono state tolte; dopo, C: ha 354,7 GiB liberi e F: 505 (diario del 2026-10-01 notte - 2026-10-02). Il 2026-10-02 il run 3-way 1 (`out/monker/step2_3way/3WAY50_15x4_rake25cap2`, con `part_a/`) e l'uscita del test del blocco restano sull'SSD C:; il loro archivio su F: è T8 dell'handoff, la sera del 03/10 |
| Merge su `main` | eseguito dall'utente il 2026-09-16 (`97d8121`, tag P3/P6/P8); il completamento di P8 (viewer) è unito nell'integrazione e in `main` con lo stesso mandato; `main` non è pushato (non richiesto); correzione EV e size HU10 5a/8a unite in integrazione (`f047484`) e in `main` (`9c68a63`) il 2026-09-16, branch di fase e integrazione pushati |
| Gate di accettazione | **Dal 2026-09-28 (12:40): solo best response esatta dentro l'astrazione <= 0,03 a (1 % del piatto iniziale); il limite di 0,15 a sul certificato fisico e' tolto, il fisico resta una misura di qualita' dell'astrazione.** HU10 e HU20 passano (fisico 0,0020 e 0,0280 a, e l'astratta non supera il fisico). HU30 a 48.000 iterazioni: astratta 0,0269 a (passa), fisico 0,1609 a (7 % oltre il vecchio limite di 0,15 a). HU40 a 64.000 iterazioni: 0,0417 / 0,2383 a, FAIL. Fino al 2026-09-27 il gate era il certificato fisico all'1 %. **Dal 2026-10-01 sera questo gate si ritira con history7** (conseguenza della decisione dell'utente di togliere history7): per le righe per classe di board non esiste una best response esatta dentro l'astrazione; i criteri di accettazione del prodotto sono da proporre all'utente (handoff T16). **Fase 3 del 3-way, 2026-10-02:** gate G3 (3b) "PASS provvisorio" sul run 1, con il guadagno preflop massimo 0,0305 a (CO, stima per eccesso su 64 flop) sotto 0,04, l'albero 54/54, i controlli verdi e l'identità del rake in forma statistica (+0,0248 ± 0,0248 a) per decisione dell'utente; diventa definitivo con la parte A sulla lista esatta. |
| Limite RAM corrente | **8 GiB** di picco solo per la suite di benchmark HU10-HU40 sul PC di sviluppo (precisazione dell'utente del 2026-09-28); il prodotto è pensato per un server da 256 GB. I censimenti a 12 e 25 GiB restano misure storiche. |
| Prossimo passo | **Aggiornamento del 2026-10-02 alle 19:00**: test del blocco 3-way alle 20:00 (fine verso le 06:30 del 03/10, se la memoria libera apre il gate), poi il 03/10 la parte A sul blocco, T9b-T9d (radici dei V1 corte; vincolo del rake del `class_cache` in V11 se l'utente approva), il merge T10, l'archivio dei documenti e T13/T8, secondo la sezione 6 dell'[handoff](../../handoff/NEXT_STEPS_2026-10-02.md). **Aggiornamento del 2026-10-02 alle 01:30**: il primo run 3WAY50 (15 × 4 double, rake 2,5 % / cap 2a, arresto a 0,008 sulle 18 chart non all-in ogni 4.000 iterazioni, minimo 16.000, tetto 48.000, checkpoint ogni 16.000) è partito alle 00:09:30, dopo nove minuti di attesa del gate della memoria; 1,745 s per iterazione sulle prime 1.500, 1,686 di media a 2.500 [I]. Il 02/10 gira senza pausa, dal 03/10 pausa alle 19:40 e ripresa alle 00:00; fine attesa [I] fra le 11:50 e le 19:40 del 02/10 se l'arresto cade fra 24.000 e 40.000 iterazioni, tetto verso le 23:40. Il run non si tocca, e mentre gira non si compila né si prova nulla. Dopo il run, secondo l'handoff [NEXT_STEPS_2026-10-02.md](../../handoff/NEXT_STEPS_2026-10-02.md) (T3-T12): risultato del run 1; build, V11 e commit della parte A, parte A campionata su 64 flop fisici del run 1 e gate 3b; archivio del run 1 su F:; rimozione di history7 (build della sandbox, ctest, V1, commit) e commit rinviato di `gui_prototype` con build e ctest (questo da `gui_removal/removal.md`, non dall'handoff); merge in `feat/monker-step1-checkdown`; test del blocco a 15 × 4; proposta della batteria di correttezza 3-way. Decisioni attese: QA3, QA6 e QA7 dell'[addendum della web UI](../../solver-ui/WEB_UI_ADDENDUM_ENGINES_2026-10-02.md) (QA1 e QA4 hanno avuto risposta verso le 01:30, QA2 e QA5 entro le 01:35), U1-U12 dell'handoff, criteri di accettazione del prodotto (T16). Decisioni dell'utente fra le 01:30 e le 01:35: i test legacy del postflop (`phase7`, `phase10`, `gto_plus_reference`) si rifanno nel ciclo di build e test dopo il run 1, e i guasti si correggono prima che la UI usi davvero `gto_cli`; da scrivere un lettore del postflop delle policy dello step 2 (HU e 3-way), che la UI mostrerà, e, dopo la chiusura del lavoro 3-way, i tre comandi nuovi dentro `gto_cli` (QA2). Diario del 2026-10-01 notte - 2026-10-02. Quanto segue è lo stato degli altri filoni alle 14:40 del 2026-09-30, non aggiornato. In corso alle 14:40 del 2026-09-30: estensione del run 30 × 4 con rake 2,5 % / cap 2a fino a 64.000 iterazioni (valutazione esatta, poi archivio su F:), in coda il run con le texture TXM2 nello stesso gioco (64.000). Decisioni in attesa: le D1-D7 della specifica della fase 3 del 3-way (astrazione, rake del passo 2, regola di arresto 0,008 sulle 18 chart non all-in, ambito della valutazione, server a noleggio, build fino alle 24:00, conferme; prese il 2026-10-01, vedi il diario); configurazione HU di riferimento dopo l'estensione e il TXM2 (candidato 30 × 4 con rake 2,5 % / cap 2a); opzione A (algoritmo): la condizione dell'utente delle 12:26 (dopo il risultato del 30 × 4 e il test 7) è soddisfatta; non rimessa in coda, aspetta la conferma dell'utente (gioco: G1 come preparata, oppure il 30 × 4 con rake 2,5 % / cap 2a secondo il piano delle 12:26); criterio di somiglianza in EV; push del branch (commit solo locali); correzione del campionamento distorto del multiway con range nel calcolatore web dell'utente. Poi il codice della fase 3 (dalle 15:00 circa secondo la specifica). Da stimare: migliore risposta preflop contro un postflop risolto esattamente per flop. L'utente non conosce le impostazioni di MonkerSolver usate per le chart: non chiederle più. Dettagli in [MONKER_RECIPE_REPRODUCTION_2026-09-28.md](MONKER_RECIPE_REPRODUCTION_2026-09-28.md), sezioni 5.11, 6, 8, 9.6 e 9.7, e nella [specifica della fase 3](threeway/PHASE3_SPEC_2026-09-30.md). |

## 2. Registro dei gate

| Fase | Esito | Data | Commit | Report |
|---|---|---|---|---|
| P0 Contratto e scaffolding | PASS | 2026-09-15 | `ef6f691` | [P0_SCAFFOLDING.md](P0_SCAFFOLDING.md) |
| P1 Canonicalizzazione e cataloghi | PASS | 2026-09-15 | `ca80dab` | [P1_CANONICAL_BOARDS.md](P1_CANONICAL_BOARDS.md) |
| P2 Risorse esatte | PASS | 2026-09-15 | `9f8a6d3` | [P2_EXACT_RESOURCES.md](P2_EXACT_RESOURCES.md) |
| P3 Clustering e tabelle bucket | PASS | 2026-09-15 | `c7bb762` | [P3_BUCKET_TABLES.md](P3_BUCKET_TABLES.md) |
| P4 Modello di gioco e albero compilato | PASS | 2026-09-15 | `89f159e` | [P4_GAME_MODEL.md](P4_GAME_MODEL.md) |
| P5 Kernel vettoriale HU | PASS | 2026-09-15 | `738e361` | [P5_VECTOR_KERNELS.md](P5_VECTOR_KERNELS.md) |
| P6 Trainer con campionamento del board | PASS | 2026-09-16 | `c5ccef1` | [P6_TRAINER.md](P6_TRAINER.md) |
| P7 Certificatore board-major | PASS | 2026-09-16 | `01b7ca4` | [P7_CERTIFIER.md](P7_CERTIFIER.md) |
| P8 Export, query, comparatore, viewer | PASS | 2026-09-16 | `cdd3481`, `b41cee2` | [P8_EXPORT.md](P8_EXPORT.md) |
| P9 Qualificazione CO40 e archiviazione | NOT_RUN | | | |
| P10 Conteggio alberi 3-way | NOT_RUN | | | |

Esiti ammessi: `PASS`, `FAIL`, `INCONCLUSIVE`, `NOT_RUN`.

## 3. Diario

### 2026-10-02 (09:00-19:00) — fine del run 3-way 1, fase 3b compilata e committata, parte A sul run 1 e G3 provvisorio, slot del pomeriggio (GUI, history7, mutazioni), test del blocco in coda per le 20:00

Dettagli del 3-way nella nuova sezione 9.8 di [MONKER_RECIPE_REPRODUCTION_2026-09-28.md](MONKER_RECIPE_REPRODUCTION_2026-09-28.md);
roadmap del giorno nella sezione 6 dell'handoff [NEXT_STEPS_2026-10-02.md](../../handoff/NEXT_STEPS_2026-10-02.md).
Rapporti nella cartella temporanea della sessione: in `threeway/phase3b/` `run1_result.md` (T3), `t4_results.md` (T4),
`t56_results.md` (T5 D.2 e D3, T6), `partA_run1.md` (T7), `verify_g3/` (ricalcolo indipendente dei numeri di G3),
`review_commit/` (review del commit della parte A), `mutations_results.md` (T5 D.1), `history7_removal/t9a_results.md`
(T9a); `gui_removal/after_run_results.md` (commit D1 e test legacy); `threeway/lock/LOCK_TEST_PLAN.md` (test del blocco);
`threeway/viewer/STEP2_NOTES.md` (viewer); le note di memoria `g3-rake-identity-decision.md` e
`docs-archive-decisions-2026-10-02.md`; `git log` del checkout principale e del worktree della fase 3; `run.log`,
`queue.log`, `train.jsonl` e `part_a/` della cartella del run 1. Della cartella del test del blocco è stato letto solo
`queue.log`, con uno script Python. [V] = verificato, [I] = inferito; [V, sessione principale] = fatto riferito dalla
sessione principale, senza un file che lo registri. Ore locali (UTC+2).

**Cronologia** [V, salvo dove è segnato].

| Ora | Fatto |
|---|---|
| Fra le 09:00 e le 09:15 | Risposte dell'utente all'audit dell'età dei documenti; commit `a35f56c` dell'handoff alle 09:14 |
| Verso le 09:20 | Push degli 8 commit `88118a6..a35f56c` su `origin/feat/monker-step1-checkdown`, chiesto dall'utente |
| 09:20-09:55 | Estensione del viewer 3-way al passo 2 (`STEP2_NOTES.md`); review e correzioni dalle 10:15 alle 10:45 |
| 10:36:07 | `STABLE at 24000 (non_all_in change 0.007 < 0.008)`: il runner ferma il trainer |
| 10:38:40-10:38:45 | Trainer finito (`PREFLOP_BLUEPRINT_TRAIN=STOPPED`, ultima iterazione 24.010); coda chiusa (`queue done`) |
| Verso le 10:50 | Versione 2 del viewer pubblicata, con il run finito [V, sessione principale] |
| Verso le 10:55 | "Puoi startare da subito la roadmap": la sessione principale avvia la catena dei lavori; alla richiesta successiva dell'utente la ferma entro due minuti (nulla scritto) e scrive la roadmap in chat [V, sessione principale] |
| 10:58 | Commit `d39de2e` dell'handoff (run finito, roadmap approvata); la roadmap riparte verso le 11:00 |
| 11:00-11:08 | Preparazione del test del blocco (coda, copia delle chart, prova a secco), senza lancio |
| 11:01:22 | T3: controllo dell'albero sulle chart di `it_24000`, PASS 54/54 |
| 11:06-12:53 | T4: build della parte A, V11 (due fallimenti, due correzioni), V1, ctest, gamba esatta HU, referee, smoke della CLI |
| Verso le 11:15 | L'utente sposta il test del blocco alle 20:00 |
| 12:58-13:58 | T5 D.2 e D3, poi T6: commit `ec3eec5` della parte A alle 13:57:50, CLI congelata alle 13:58:10 |
| 13:19 | Commit `953e978` dell'handoff (blocco alle 20:00, slot 15:30-20:00) |
| Verso le 13:25 | Decisione dell'utente sull'identità del rake di G3 |
| 13:26:23 | Lanciata in background la coda congelata del test del blocco: aspetta le 20:00 |
| 13:27 | Commit `aeedc8d` dell'handoff (decisione su G3, coda lanciata) |
| 14:02:30-15:05:28 | T7: parte A sul run 1 (64 flop), poi `part_a_values.py` e `monker_in_our_game.py`; analisi in più fino alle 15:08 |
| 15:13-15:20 | Review del commit della parte A e ricalcolo indipendente dei numeri di G3 |
| 15:15-15:30 | Viewer ricostruito con la parte A e il controllo dell'albero (build delle 15:18); versione 3 pubblicata verso le 15:30 [V, sessione principale] |
| 15:30:46-15:57:42 | Commit D1 `8c70044` nel worktree della fase 3; build; ctest del preflop, test legacy e `gto_cli` |
| 16:02-16:51 | T9a completo sulla sandbox di history7, con ctest completa e V1 completo |
| 16:52-18:23 | T5 D.1: mutazioni della parte A in una sandbox di `ec3eec5` |
| 18:44 | `queue.log` del test del blocco: solo le 40 righe dell'avvio delle 13:26 (38 controlli ok, 0 FAIL), nessun round partito |

**Decisioni dell'utente.**

| Quando | Decisione |
|---|---|
| Fra le 09:00 e le 09:15 | Audit dell'età dei documenti: piano di archivio approvato, "Sì, dopo il merge"; i due piani `POSTFLOP_*` del 10/09 restano al loro posto ("Tieni i piani di ottimizzazione"); parità con GTO+: "Decido dopo i test"; i 18 file si cancellano dopo il tag: "Sì, dopo il tag" [V, nota `docs-archive-decisions-2026-10-02.md`, commit `a35f56c`] |
| Verso le 09:20 | "Sì, fai il push degli 8 commit": push `88118a6..a35f56c` [V, `origin/feat/monker-step1-checkdown` = `a35f56c`, 8 commit]. L'utente chiede anche quando finisce il run e se entra nel suo viewer |
| Verso le 09:25 | "Sì, fai preparare l'estensione del viewer" |
| Verso le 10:55 | "Puoi startare da subito la roadmap"; poi "Prima di startare, scrivi la roadmap qua in chat, poi aggiorna @docs/handoff/NEXT_STEPS_2026-10-02.md" |
| Verso le 11:15 | "Sì, spostalo alle 20 e aggiorna roadmap e handoff. Cerca di fare qualcosa fra le 15.30 e le 20": test del blocco alle 20:00 del 02/10, e nello slot 15:30-20:00 i lavori previsti a `-j 2` durante il blocco o il 03/10 (commit `953e978`) |
| Verso le 13:25 | Identità del rake di G3: "Lista esatta più avanti". Oggi l'identità si riporta in forma statistica (residuo ± errore standard), come provvisoria; la parte A sulla lista esatta (8-18 ore sull'i3, o un server) si fa più avanti (commit `aeedc8d`; handoff U4) [V, nota `g3-rake-identity-decision.md`] |
| Nel corso del giorno | Aggiornare l'utente a ogni passo fino alle 08:00 del 03/10 e quando parte il test del blocco |

**Fine del run 3-way 1 (T3)** [V, `run.log`, `queue.log` e `train.jsonl` del run, `run1_result.md`].

- Fermato dalla regola di arresto: `STABLE at 24000 (non_all_in change 0.007 < 0.008)` alle 10:36:07, trainer finito alle
  10:38:40 con `PREFLOP_BLUEPRINT_TRAIN=STOPPED` (ultima iterazione 24.010, evento `stop_file`), coda chiusa alle 10:38:45.
  Dalla partenza del runner (00:09:30): 10 h 29 min.
- Evento `end`: 37.490,2 s di training per 24.010 iterazioni, cioè **1,561 s per iterazione** (gli 1,74 s delle prime 500
  erano pessimisti); scrittura della policy 186,7 s; picco del working set 14,86 GB. Policy finale `policy.bin` (6,78 GB,
  `fnv1a64:5f21e9f22b783b56`), snapshot di policy in `charts/it_16000`, checkpoint `state.ckpt` (13,56 GB);
  `train.stderr.log` vuoto. Le chart finali sono `it_24000`, la policy è dell'iterazione 24.010: T7 misura lo scarto fra le
  due, arrotondamento delle chart compreso, sotto 3e-5 a.
- Controllo dell'albero (condizione 2 di G3), alle 11:01:22 con l'eseguibile congelato:
  `PREFLOP_BLUEPRINT_MONKER_TREE=PASS nodes=54 files=54`. L'impronta dell'albero dell'evento `start`
  (`fnv1a64:71abeabaab92fe56`) è quella registrata nel referee: nessun commit.

| Iterazione | Cambio dal salvataggio precedente (tutte / all-in / non all-in) | Distanza (54 chart) | 36 all-in | 18 non all-in | Differenza di range |
|---|---|---:|---:|---:|---:|
| 4.000 | — | 0,0791 | 0,0546 | 0,1281 | 0,4375 |
| 8.000 | 0,0307 / 0,0222 / 0,0477 | 0,0732 | 0,0575 | 0,1045 | 0,3098 |
| 12.000 | 0,014 / 0,0099 / 0,0224 | 0,0686 | 0,0535 | 0,0988 | 0,2684 |
| 16.000 | 0,0094 / 0,0064 / 0,0153 | 0,0672 | 0,0521 | 0,0974 | 0,2655 |
| 20.000 | 0,0066 / 0,0053 / 0,0093 | 0,0654 | 0,0496 | 0,0970 | 0,2669 |
| **24.000** | **0,005 / 0,004 / 0,007** (sotto 0,008: arresto) | **0,0645** | **0,0478** | **0,0980** | **0,2729** |

- Stessa azione principale nel 93,5 % dei casi. Sulle 40 chart in cui le chart del passo 1 con lo stesso rake
  (`rake25_dead`) hanno mani in comune con MonkerSolver: **0,072 contro 0,264** del passo 1 (0,0718 e 0,2641) [V, generatore
  del viewer, `STEP2_NOTES.md`; T3 non lo ha ricalcolato].
- Contro il passo 1 3-way (cinque giochi: distanza 0,245-0,288, differenza di range 0,699-0,736) la distanza è 3,8-4,5 volte
  più piccola e la differenza di range 2,6-2,7 volte; in HU il passo 2 aveva dato un fattore 3,6-4,6 [V numeri; I rapporti].
- Attese della specifica: la distanza 0,06-0,12 è rispettata, al bordo basso. **Due attese mancate, nessuna è un gate**:
  "chart all-in a 0,02 o meno ciascuna" (media 0,0478, 24 delle 36 sopra 0,02, la peggiore 0,207 su una linea profonda
  limp-raise-all-in dell'UTG, poi 0,184 e 0,143); "la maggior parte dello scarto nelle chart non all-in" (in HU il 91,5 %, qui
  le 18 chart non all-in portano il 50,6 % della somma delle distanze).

**Misure fuori dal gate: preferenza suited e mix di primo ingresso** [V, `run1_measures.py` in `threeway/phase3b/`, che sulle
chart di MonkerSolver riproduce esattamente i valori della specifica].

| Nodo | Misura | MonkerSolver | Run 1, `it_24000` | Passo 1 3-way |
|---|---|---|---|---|
| UTG alla radice | preferenza suited | 0,204 | 0,257 | non calcolata |
| UTG alla radice | all-in / open 6a / limp / fold (% delle combo) | 17,6 / 0,1 / 35,3 / 47,0 | 16,5 / 0,2 / 37,8 / 45,5 | 11,4-17,2 / 0,0 / 41,8-54,6 / 34,0-41,0 |
| CO dopo il fold dell'UTG | preferenza suited | 0,255 | 0,308 | non calcolata |
| CO dopo il fold dell'UTG | all-in / open 6a / limp / fold (% delle combo) | 32,8 / 3,8 / 31,4 / 32,0 | 34,8 / 2,1 / 34,0 / 29,1 | 29,0-34,2 / 0,0 / 56,8-65,6 / 5,4-9,8 |

- La patologia del passo 1 (il CO che limpa il 57-66 % e folda il 5-10 %) non c'è più: i due mix di primo ingresso stanno a 3
  punti da MonkerSolver su ogni azione [V numeri; I lettura].
- La preferenza suited è più alta di quella di MonkerSolver di 0,053 a entrambi i nodi; all'UTG scende piano verso la sua
  (0,272 → 0,257 fra 8.000 e 24.000), al CO sale ancora (0,283 → 0,308 fra 4.000 e 24.000). Anche l'open 6a del CO scende
  ancora (12,9 → 2,1 %, MonkerSolver 3,8 %): il nodo di primo ingresso del CO non è del tutto fermo a 24.000 [I].

**Viewer 3-way** [V, `STEP2_NOTES.md`; le pubblicazioni V, sessione principale].

- Versione 2, pubblicata verso le 10:50 sullo stesso indirizzo (artifact "Short Deck 3-way 50a",
  https://claude.ai/artifact/U5oA9gn6nCEb94Wb3a3Fnd), dalla build delle 10:41 dopo la fine della coda: distanze per
  snapshot, riga a parità di chart sulle 40 del passo 1, grafico dell'arresto, stato del run. La ricerca del run salta le
  cartelle con `--lock-charts` nella riga di avvio (il test del blocco).
- Versione 3, pubblicata verso le 15:30 dalla build delle 15:18: controllo dell'albero letto da `part_a/tree_check.txt`,
  tabella della parte A, voce del rake di G3 in forma statistica (residuo ± errore standard, "compatibile con zero", chip
  "provvisorio" entro 2 errori standard), policy riconosciuta come quella dell'evento `end`. Controlli: `STEP1_DATA=SAME`,
  `PROBLEMS 0` (ogni campo della parte A contro `report_64.json` e, a 1e-12, `values_64.json`), controlli di render PASS,
  `FAKE_TESTS=PASS`, `EXTRA_TESTS=PASS`, `AUDIT=PASS`, `DETERMINISM=SAME`, `WIDTH_PROBE=PASS` su 90 casi.

**T4: build e verifiche della parte A** [V, `t4_results.md`]. Worktree della fase 3, `feat/threeway-step2` a `c2e9138`,
dalle 11:06 alle 12:53.

| Passo | Esito |
|---|---|
| Build completa a `-j 4` | `BUILD_OK`, 64 passi, 0 warning (/W4 /WX), 3 min 33 s |
| V11 a 4 thread | FAIL alle 11:12 e alle 11:25; dopo due correzioni `PREFLOP_BLUEPRINT_POLICY_VALUES_TESTS=PASS assertions=761` (11:45-11:56) |
| V1 completo (g1, rake, hu10) | `V1_COMPARE=IDENTICAL`, 39/39 |
| ctest | 102/102, 0 falliti (1.265 s) |
| Gamba esatta HU `--long` (573 flop canonici, 605.088 board) | PASS, ogni scarto ≤ 3,2e-14 (26 min a 2 thread) |
| Referee | 2/2 con la famiglia 3WAY50 |
| Smoke della CLI | PASS, con una chiamata interrotta a metà e ripresa dallo stato |

**Due difetti del disegno della parte A, trovati da V11 e corretti** (solo nei file della parte A; `trainer.cpp` invariato,
i suoi valori a 3 posti esatti a circa 1e-14):

1. **Per classe e per combo sulle liste canoniche.** Il disegno diceva che i valori per classe e per combo coincidono "su una
   lista esatta, per simmetria dei semi". È falso sulle liste canoniche: hanno un rappresentante per orbita dei semi con il
   peso dell'orbita, non sono chiuse per permutazione dei semi, e il valore per combo non è simmetrico (scarto 0,25389 al
   primo V11). Su una lista chiusa per semi (le orbite intere di un board canonico ogni 199, 1.892 board) le due convenzioni
   coincidono a 1,0e-14 in HU e 6,2e-15 a 3 posti. Correzione: il test confronta le convenzioni sulla lista chiusa; sulle
   liste canoniche lo scarto per combo è solo informativo; `--values per_combo` è per le liste fisiche.
2. **L'identità del rake in `class_cache` vale solo in media.** Il disegno diceva che le EV dirette dei posti sommano a meno il
   rake atteso "su ogni lista". In `class_cache`, il modo di produzione (specifica 3.9), i terminali preflop portano i valori
   della cache senza board; su un board ogni posto li pesa con le proprie mani vive, quindi l'identità vale in media sui
   board, non per board: residuo −1,147 sul flop dello smoke, da −1,147 a +0,893 su 6 flop (media −0,138, errore standard
   0,28). Sullo stesso flop in `board_kernels` il residuo è −1,1e-14, e `class_cache` sulla lista canonica esatta dà 2,3e-14.
   Correzione: l'identità si vincola a 1e-9 dove è esatta (HU, 3 posti in `board_kernels`, lista esatta), altrimenti si
   riporta il residuo con il suo errore standard (campi JSON `rake_identity_exact` e
   `rake_identity_standard_error_antes`); nuova gamba v11smokebk.

Il secondo punto rende la condizione del rake di G3 (≤ 1e-9) non applicabile alla lettera nel modo previsto per T7: da qui
la decisione dell'utente delle 13:25. Altri valori misurati: v11hucd (19.998 board canonici) EV 6,4e-15 / 1,1e-16; v11three
EV ≤ 6,2e-15, guadagni ≤ 2,1e-14; v11huexact per classe contro `BestResponseEvaluator`: valori delle combo 3,2e-14 /
1,2e-14, EV 5,0e-16 / 1,5e-15, guadagni preflop 8,7e-15 / 2,4e-14.

**T5 D.2 e D3, T6: commit della parte A e CLI congelata** [V, `t56_results.md`, `git log` del worktree].

- D.2 (il rilievo D1 della review del 01/10): gli errori standard delle stime aggregate (EV, guadagno preflop, perdita delle
  chart) prendono il termine del denominatore (metodo delta). Le stime puntuali non cambiano: sulla stessa smoke 161.138
  foglie del JSON su 161.160 sono identiche, le 22 diverse sono 15 errori standard e 7 tempi o percorsi.
- D3: tre controlli della portata aggregata degli avversari in v11three e v11smoke (UTG alla radice: portata 1 su ogni combo,
  scarto 0; CO: somma 1 a 1,0e-14 e 3,3e-14); V11 `PASS assertions=770`; ctest delle due voci della parte A 2/2 (1.094,65 s e
  1.853,31 s, in parallelo).
- Commit **`ec3eec5`** alle 13:57:50 su `feat/threeway-step2`: i 7 percorsi della parte A, +3.619 / −1 righe; non pushato.
- CLI congelata in `out/monker/bin_3way_partA/` (sha256 `b0a26878…`, nessuna DLL, `SOURCE.txt` con le sha256 dei sorgenti del
  commit); dopo il commit la build non ha nulla da fare, quindi l'eseguibile è quello dei sorgenti committati.

**T7: parte A sul run 1 e verdetto di G3** [V, `partA_run1.md` e i file di `part_a/` nella cartella del run].

- Comando dell'handoff: CLI congelata, policy finale (iterazione 24.010), 64 flop fisici con seed 20261002, 8 thread, chart
  di MonkerSolver come insieme da valutare, `--check`. CLI dalle 14:02:30 alle 15:05:26: 62,9 min (caricamento della policy
  42,4 s, 847.242.189 voci; 58,3 s per flop di 1.056 board); picco del working set 7,31 GB (stima dell'handoff 7,3); la
  specifica stimava 0,9-2 ore.

| Posto | EV aggregata per classe ± SE | EV diretta ± SE | Guadagno preflop della migliore risposta ± SE (% del piatto) | Chart di MonkerSolver nel nostro gioco: perdita ± SE (% del piatto) |
|---|---|---|---|---|
| UTG | −0,35095 ± 0,00908 | −0,34383 ± 0,00696 | 0,02352 ± 0,01504 (0,588 %) | −0,00034 ± 0,00212 (−0,008 %) |
| CO | −0,10283 ± 0,00423 | −0,09372 ± 0,00899 | **0,03053 ± 0,01130 (0,763 %)** | −0,00108 ± 0,00203 (−0,027 %) |
| BTN | +0,03659 ± 0,00982 | +0,04658 ± 0,01663 | 0,01842 ± 0,00816 (0,461 %) | +0,00101 ± 0,00173 (+0,025 %) |

- Valori in ante per mano; il piatto iniziale è di 4a. Rake atteso **0,41579 a per mano** (2,5 %, cap 2a, no flop no drop).
  Somma delle EV dirette −0,39098: residuo **+0,02481 a, errore standard 0,02484, z +1,00** (`rake_identity_exact: false`).
  Somma delle EV aggregate più il rake: −0,00140 (z −0,38 nel ricalcolo indipendente).
- Il guadagno sta sulle decisioni di primo ingresso con offsuit marginali (KQo, 98o, Q9o, QTo, A6o); ogni nodo di ogni posto
  ha un termine positivo (16/16, 18/18, 20/20): una migliore risposta campionata trova sempre qualcosa.
- **Quanto è rumore** (analisi della serie per flop, 40 divisioni in due metà): sulle metà di 32 flop il guadagno nel campione
  raddoppia (0,0536 / 0,0635 / 0,0362), e una migliore risposta scelta su 32 flop e valutata sugli altri 32 **perde**
  0,060 / 0,054 / 0,027 a contro la nostra policy. L'estrapolazione 2 g(64) − g(32) dà −0,0066 / −0,0024 / +0,0006: il
  guadagno vero solo preflop è vicino a zero entro il rumore [I: le metà non sono campioni indipendenti, è un'euristica]. La
  specifica si aspettava lo 0,3 % o meno (HU circa 0,1 %): le stime per eccesso 0,46-0,76 % sono quasi tutta distorsione del
  campionamento [I]; non è un gate.
- **Chart di MonkerSolver nel nostro gioco** (un posto gioca le 54 chart ai propri nodi, gli altri posti e tutto il postflop
  sono nostri): le tre perdite stanno entro 0,6 errori standard da zero e sotto lo 0,03 % del piatto (in HU al massimo lo
  0,09 %): a questo campione il preflop di MonkerSolver e il nostro sono indistinguibili dentro il nostro gioco [I]. Righe di
  ripiego: 5, tutte del CO (A9s e KJs in due nodi, KQo in uno), 0,056 combo di portata.
- Le nostre chart arrotondate di `it_24000` contro la policy valutata: +2,6e-5 / +6,8e-6 / +1,8e-5 a.

| Condizione di G3 | Misura | Esito |
|---|---|---|
| Run finito per la regola di arresto | `STOPPED` a 24.000 | PASS |
| Controllo dell'albero 54/54 | `PREFLOP_BLUEPRINT_MONKER_TREE=PASS nodes=54 files=54` | PASS |
| Guadagno preflop ≤ 0,04 a per posto (stima per eccesso) | massimo CO 0,03053 a (margine 0,84 errori standard) | PASS |
| Controlli verdi | `checks_failed` vuoto; CLI, `part_a_values.py` e `monker_in_our_game.py` con exit 0 | PASS |
| Identità del rake ≤ 1e-9 | non applicabile in `class_cache` campionato; in forma statistica, per la decisione delle 13:25: +0,02481 ± 0,02484 a, compatibile con zero a 1,0 errori standard | provvisoria |

**Verdetto: G3 "PASS provvisorio"**, campionato su 64 flop fisici, con l'identità del rake in forma statistica. A 1e-9
l'identità vale in `board_kernels` e sulla lista esatta (V11: v11smokebk −1,1e-14, v11three 2,3e-14); la parte A sulla lista
esatta (U4, più avanti) toglie la distorsione e il rumore. L'identità del rake è un controllo di coerenza e non prova i kernel
(review del 01/10, D2): quella prova sono V2, V8, V13 e, per la parte A, V11.

**Controlli indipendenti** [V, `verify_g3/verify_g3.log`, `review_commit/`].

- Ricalcolo dei numeri di G3 (15:19-15:20, `verify_g3.py`, dai JSON della CLI): 64 flop fisici distinti (60 canonici distinti),
  67.584 board, peso totale 1; EV, guadagni, scelte della migliore risposta (0 differenze), termini per nodo, perdite delle
  chart (scarto per nodo dalla CLI 2,6e-17), identità della radice (≤ 7,6e-14), rake e residuo uguali a quelli di T7. Gli
  errori standard del jackknife (togliendo un flop alla volta) sono vicini a quelli del metodo delta: EV 0,00910 / 0,00426 /
  0,00974, guadagni 0,0145 / 0,0110 / 0,0079; senza il termine di D.2 le SE dell'EV aggregata sarebbero 0,0070 / 0,0090 /
  0,0167. Le sha256 degli otto file di `part_a/` sono quelle registrate da T7.
- Review del commit `ec3eec5` (15:13-15:20): i sette file estratti con `git show`; le due serie di diff (`x_*` e `y_*`: T4 sulla
  CLI, sui test e su `part_a_values.py`, D.2, D3) sono identiche byte per byte, cioè il commit contiene i cambiamenti rivisti
  [I]; `se_check.py` ricontrolla gli errori standard di D.2 sulla serie per flop. Nella cartella non c'è un rapporto scritto
  con il verdetto della review [V]: l'esito non è nei file letti per questa voce.

**Slot del pomeriggio (15:30-18:23, nessun run attivo).**

*(a) Commit D1 e test legacy* [V, `after_run_results.md`, `git log` del worktree].

- Commit **`8c70044`** alle 15:30:46: tolti la libreria `gtosd::gui_prototype` e il test `phase9`, 7 file, +2 / −738;
  `NO_GUI_PROTOTYPE_REFERENCES_OUTSIDE_DOCS`. Build: `ninja: no work to do` (D1 toglie solo target); 0 test GUI registrati.
- ctest delle etichette `preflop_blueprint` e `card_abstraction` 102/102 (1.079 s); **test legacy 48/48** (466 s), fra cui
  `phase7`, `phase10` e `gto_plus_reference` chiesti dall'utente; `gto_cli self-check` exit 0 (`GTOSD 0.10.0`, 36 carte, 630
  combo, 81 classi), `--help` exit 2. Riga finale `0 0 0 2`, come atteso. Nessun guasto: il cambio di `libs/core` del 28/09 è
  neutro per il codice legacy [I, dal PASS]. Esclusi per scelta: i test `slow` e `nightly`, l'oracolo range-orbit (opzione
  spenta), le smoke di ricerca.
- La domanda sulla parità con GTO+ ("Decido dopo i test") si può fare ora [I].

*(b) T9a: rimozione di history7 nella sandbox* [V, `t9a_results.md`].

- Catena dalle 16:02 alle 16:36 (`h7.NO_TRAINER_OK`; `-j 2`, fissato dallo script): build 300/300 in 29 min 45 s senza warning;
  test del kernel, del trainer, del certificatore e delle texture PASS; smoke 4/4; V1 hu10 13 SAME.
- ctest completa delle etichette del preflop sulla build della sandbox: 101/101 (483 s), compreso il test del gioco che carica
  le 7 fixture tenute dalla review.
- V1 completo: il primo tentativo dà `DIFFERENT (9)`: nella radice `v1_full_sandbox` il percorso di una chart è lungo 262
  caratteri, lo scrittore delle chart fallisce (evento `charts_failed`, non fatale, exit 0) e mancano le chart; tutti i campi
  del training erano SAME. Il secondo, con la radice corta `v1_sbx`: `V1_COMPARE=IDENTICAL`, 39/39.
- `LongPathsEnabled` vale 1 sulla macchina [V, registro, letto per questa voce], ma il manifest del trainer (sandbox e
  `bin_3way_step2`) non dichiara `longPathAware` [V, ricerca nei due eseguibili]: oltre 259 caratteri la scrittura fallisce
  comunque. Le radici di uscita dei V1 devono restare corte (handoff, 03/10, riga 11).
- T9a è finito: il 03/10 si passa a T9b variante (i). D1 tocca tre file CMake che la sandbox (a `2aa24d8`) non ha.

*(c) T5 D.1: le mutazioni della parte A* [V, `mutations_results.md`].

- Sandbox `out/review3b` del worktree, da `git archive ec3eec5`; build del solo target di V11 in 1 min 28 s; controllo senza
  mutazioni `PASS assertions=770`. Alla fine la sandbox è tornata identica al commit.

| Mutazione | Presa da |
|---|---|
| pm1, rake contato in ogni passata | v11three, v11smokebk |
| pm2, portata a 3 posti senza rimozione delle carte | v11three e v11smoke (D3: portata dell'UTG alla radice, scarto 0,517); v11smokebk (identità della radice del BTN, 0,975). Senza D3, v11three la prendeva solo con l'identità del BTN (0,974) |
| pm3, scala delle coppie HU sulle somme a 3 posti | crash 0xC0000005 in tutte le gambe; la forma voluta, pm3b, dal guadagno di v11three (351 volte) e dalle identità della radice |
| pm4, massa dello showdown HU tolta dal rake | v11hu, v11hucd, v11huexact (`--long`); non v11state, che non ha l'identità del rake |
| pm5, buffer del rake non azzerato | v11hu, v11state, v11hucd, v11three, v11smokebk |

- Le cinque mutazioni sono prese tutte. Due difetti dello script del 01/10, corretti solo in una copia generata: pm2 non compila
  con /W4 /WX (C4100 diventa C2220); pm3 legge `pair_probability`, vuoto sul percorso a 3 posti, invece di cambiare una scala.
- **Un buco trovato con una mutazione in più (pm6)**: se il ramo `class_cache` di `terminal3` smette di contare il rake, tutte
  le gambe a 3 posti passano, perché in quel modo (quello di T7) l'identità è solo stampata. Proposta, solo nei test: vincolare a
  1e-9 l'identità diretta del passaggio `class_cache` di v11three sulla lista canonica esatta, dove è esatta. Nella sandbox:
  senza mutazioni residuo 9,99e-16 e `PASS assertions=508`; con pm6 FAIL (residuo −0,967078); nessuna passata in più. Diff
  `review_partA/v11three_cc_rake_gate.diff`; proposto all'utente per il 03/10, insieme al V11 di T9c. Su T7 una distorsione
  del genere si vedrebbe solo come uno z [I].

*(d) Parte A sullo snapshot `it_16000` del run 1* (facoltativa): non fatta [V, `part_a/` contiene solo i file dei 64 flop
della policy finale].

**Test del blocco in coda per le 20:00** [V, `LOCK_TEST_PLAN.md`, sha256 delle code congelate, `queue.log` del blocco].

- Preparato dalle 11:00 alle 11:08 senza lancio: coda `queue_3way50_15x4_lock.sh` con `START_AT=now` (sha256 `0fc45128…`,
  `DRY_RUN_OK`), copia senza spazi delle 54 chart in `out/monker_lock/charts_3way_50a` con il manifest fissato
  (`a85813e0…`); una prova a secco con la cartella originale (con spazi) fallisce su 2 controlli, come deve.
- Dopo lo spostamento alle 20:00: variante congelata `out/frozen/queue_3way50_15x4_lock_2000.sh` (sha256 `002bc77458b1…`,
  diversa solo per `START_AT=2026-10-02 20:00` e due commenti), lanciata alle 13:26:23: 38 controlli ok, 0 FAIL, nessun
  override.
- Run: gioco e astrazione del run 1; preflop delle chart 3-way 50a di MonkerSolver bloccato su tutti i nodi
  (`--lock-charts … --lock-nodes all`) e nostro postflop allenato contro i loro range; soglia 0, minimo e tetto 24.000, quindi
  fine attesa con `ITERATION_LIMIT`; chart ogni 4.000, policy a 8.000, 16.000 e 24.000, checkpoint a 16.000; il gate della
  memoria vuole anche zero processi della parte A.
- Atteso all'avvio: evento `preflop_lock` con 2.681 righe, 1.688 fuori range e 5 di ripiego (CO KQo, A9s, KJs, circa 0,056
  combo), riga `LOCK_START_OK` nella coda. Durata circa 10,5 ore a 1,56 s per iterazione: fine verso le 06:30 del 03/10 se
  parte alle 20:00 [I]; circa 41 GB su C: alla fine [I].
- **Rischio della memoria** [V numeri; I conseguenza]: senza nulla di nostro in esecuzione la memoria libera era
  18.160.004-18.203.608 KB alle 15:58 e 18.294.048-18.584.988 KB fra le 18:06 e le 18:23, sotto i 18.874.368 KB del gate
  (alle 15:58 Brave circa 3,5 GB, Claude 2,8, Discord 1,1, Steam 0,8). All'utente è stato chiesto di chiudere Brave (e Discord
  e Steam) entro le 19:50 [V, sessione principale]; altrimenti la coda aspetta e scrive nel log ogni 10 minuti (trappola 11
  dell'handoff).
- Dopo il run, il 03/10 (handoff, riga 10): parte A su `it_16000` e `it_24000` con lo stesso seed, poi la tabella dei guadagni
  per posto contro il run 1, il blocco HU (CO 0,92-1,80 %) e il blocco del passo 1 (UTG e CO 3,65-5,46 %, BTN 1,40-1,64 %).

**Commit** [V, `git log`].

- Checkout principale, `feat/monker-step1-checkdown`: `a35f56c` (09:14), push `88118a6..a35f56c`; poi `d39de2e` (10:58),
  `953e978` (13:19) e `aeedc8d` (13:27), non pushati.
- Worktree della fase 3, `feat/threeway-step2`: `ec3eec5` (13:57, parte A) e `8c70044` (15:30, D1), non pushati; albero pulito.

**Cosa resta** (handoff, sezione 6, 03/10): parte A sul test del blocco e tabella dei guadagni (riga 10); T9b variante (i),
T9c con radici V1 corte e, se l'utente approva, il vincolo del rake del `class_cache` in V11, T9d (riga 11); merge T10;
archivio dei documenti (T15.4); T13 e T8 (archivio su F: del run 1 e del blocco). Più avanti: la domanda sulla parità con GTO+,
la parte A sulla lista esatta (U4), la parte A facoltativa su `it_16000` del run 1.

**Correzioni del controllo indipendente** (02/10, dopo il commit delle 18:48) [V, salvo dove è segnato]. Il controllo
ha riletto le fonti della voce: i rapporti elencati in testa, le due note di memoria, `git log` e reflog dei due checkout,
`run.log`, `queue.log` e `train.jsonl` del run 1, i `vs_monker.json` dei cinque giochi del passo 1, il `queue.log` del blocco
(con uno script Python, nessun altro file della cartella), il registro e i manifest dei due trainer. Il testo qui sopra
resta com'è; queste righe lo correggono o lo precisano.

- **Memoria libera senza nulla di nostro in esecuzione.** Oltre alle misure citate nel test del blocco: 17.914.556 KB alle
  16:00:57, prima di T9a, senza processi `gtosd`, `cl`, `ninja` o `ctest` vivi [V, `t9a_results.md` §1], e 18.101.176 KB
  alle 15:05:29, appena finita la CLI di T7 [V, `partA_run1.md` §1]. L'intervallo è quindi 17,9-18,6 GB, non 18,2-18,6:
  al gate di 18.874.368 KB mancavano da 0,3 a 1,0 GB. Corretti il riquadro delle 19:00 e la riga 8 dell'handoff e la
  ricetta 9.8.
- **Vincolo del rake del `class_cache` (pm6).** "Proposto all'utente per il 03/10" non è registrato in nessun file:
  `mutations_results.md` §4 lascia l'adozione alla sessione principale o a chi tiene i file della parte A [V]. Se la
  sessione principale non l'ha già presentato in chat, è una proposta ancora da fare; la riga 11 dell'handoff ora dice "se
  l'utente lo approva", come la ricetta.
- **Distanza del passo 1.** La distanza del passo 1 "dalle 54 chart" è la media sulle sole chart con mani in comune con
  MonkerSolver: in tutti e cinque i giochi 40 chart su 54, le altre 14 non hanno distanza (per `rake25_dead` media
  0,2641) [V, `vs_monker.json` delle cinque cartelle di `out/monker/step1_3way/`]. Il confronto alla pari è quindi 0,072
  contro 0,264 sulle stesse 40 chart (fattore 3,7); i fattori 3,8-4,5 confrontano la media del run 1 sulle 54 chart con
  quella del passo 1 sulle sue 40. Precisati la riga "Fase in corso" e il confronto con il passo 1 in 9.8.
- **Orari.**
  - Il push di `88118a6..a35f56c` è delle 09:17:24 [V, reflog di `origin/feat/monker-step1-checkdown`]: la richiesta
    dell'utente ("verso le 09:20") è di poco prima.
  - Il commit D1 `8c70044` ha data 15:30:47 [V, `git log`]; 15:30:46 è l'inizio del passo in `after_run_results.md`.
  - Risposte all'audit dei documenti: la nota `docs-archive-decisions-2026-10-02.md` le data "about 04:00-09:00" ed è
    stata salvata alle 09:14, come il commit `a35f56c` (09:14:52) [V]; la finestra 09:00-09:15 viene dalla sessione
    principale.
  - Estensione del viewer: l'intestazione di `STEP2_NOTES.md` dice 09:20-09:55, ma il backup del passo 1 è delle 09:27,
    dopo la richiesta dell'utente verso le 09:25 [V orari; I ordine].
  - Preparazione del test del blocco: 11:00-11:08 in `LOCK_TEST_PLAN.md`, coda congelata `queue_3way50_15x4_lock.sh` con
    data 11:04:43 [V]; la riga 8 dell'handoff diceva 11:09, corretta.
  - PID della coda del blocco: 47361 nel suo `queue.log`, 26804 in `partA_run1.md` [V]; con ogni probabilità lo stesso
    processo visto da MSYS e da Windows [I].
- **pm3 com'è scritta.** Il crash 0xC0000005 è stato visto nelle tre gambe provate (v11three, v11smokebk, v11smoke), non
  in tutte le gambe di V11; la mutazione è presa solo perché il processo esce con errore, non da un'asserzione [V,
  `mutations_results.md` §1-2].
- **Review di `ec3eec5`.** `extract.sh` registra solo l'estrazione dei sette file con `git show`; che le serie `x_*` e
  `y_*` siano una le modifiche riviste e l'altra il commit è dedotto dai nomi e dagli orari dei file [I]. La riga 6
  dell'handoff ora lo segna come [INFERRED].
- **Ricontrollato per questa voce** [V]: il `queue.log` del blocco alle 18:53 ha 40 righe, 38 `ok`, 0 FAIL e nessun round,
  partenza alle 13:26:23 con sha256 `002bc77458b1…` e `overrides none`; la coda `_2000` differisce da
  `queue_3way50_15x4_lock.sh` (sha256 `0fc45128…`) solo per `START_AT` e due commenti; `LongPathsEnabled` = 1 e nessun
  `longPathAware` nei due trainer; il run 1 ha 8 thread e 96 board per iterazione (2.304.960 board in 24.010 iterazioni).
  Gli altri numeri, orari, hash e citazioni della voce corrispondono alle fonti.

### 2026-10-01 notte - 2026-10-02 (21:08-01:35) — history7 da togliere, fase 3b (parte A e arbitro 3-way), pulizia dei dati, vecchia GUI tolta, partenza del run 3WAY50, risposte sulla web UI

Dettagli del 3-way nella sezione 9.7 di [MONKER_RECIPE_REPRODUCTION_2026-09-28.md](MONKER_RECIPE_REPRODUCTION_2026-09-28.md)
(aggiornamento della notte); ambito del prodotto e history7 nella sezione 1; dati cancellati della batteria HU nella sezione 10.
Handoff [NEXT_STEPS_2026-10-02.md](../../handoff/NEXT_STEPS_2026-10-02.md); addendum della web UI
[WEB_UI_ADDENDUM_ENGINES_2026-10-02.md](../../solver-ui/WEB_UI_ADDENDUM_ENGINES_2026-10-02.md).
Rapporti nella cartella temporanea della sessione: `history7/recommendation.md`; in `threeway/phase3b/` `partA.md`,
`review_partA.md`, `referee3.md`, `removal_note.md`, `review_removal.md`, `cleanup_audit.md`, `after_run.md`, `chain.log` e
`history7_removal/chain.log`; `cleanup/delete_data.log`, `post_delete.log`, `variants_summary.txt`; `gui_removal/removal.md`;
`webui2/ui_map.md`, `gto_cli.md`, `threeway_formats.md`; le note di memoria della sessione, fra cui
`ui-postflop-decisions-2026-10-02.md` (salvata alle 01:35:36). Del run 3-way sono stati letti solo i log. [V] = verificato,
[I] = inferito.

**Ore.** Sono ore locali (UTC+2), dai log, dalle date dei file e da `git log`. Le note di memoria della sessione scrivono "verso
le 19:30" per la decisione su history7 e "verso le 19:40" per l'eccezione sul modello dei subagent: sono ore UTC, perché lo
studio su cui l'utente ha deciso è stato scritto fra le 21:08 e le 21:21 e la nota della decisione porta l'ora 19:26 UTC [I].

**Cronologia** [V, salvo dove è segnato].

| Ora | Fatto |
|---|---|
| 21:08-21:21 | Studio di history7 in sola lettura (`code_map.md`, `value.md`, `recommendation.md`): raccomandazione B, congelarlo come codice legacy |
| Verso le 21:26 [I] | L'utente decide di togliere history7 |
| 21:36:26 | Tag annotato `history7-final` su `88118a6`, l'ultimo albero con history7 |
| Verso le 21:40 [I] | L'utente ammette Fable 5.1 come subagent per il lavoro della parte A e della rimozione, e per gli audit di pulizia |
| 21:40-22:05 | Arbitro Python del postflop 3-way; commit `2aa24d8` su `feat/threeway-step2` alle 21:56:14 |
| 22:00-22:23 | Parte A della fase 3b scritta nel worktree della fase 3, non committata |
| 22:16:42 e 22:44:51 | Partono in attesa le due catene automatiche: `chain.sh` (build, V11, V1 e CLI della parte A) e `h7_chain.sh` (configure, build, test, smoke e V1 della rimozione di history7); il loro gate voleva il trainer del run vivo, la seconda anche le 00:00 del 02/10 |
| 22:50-23:20 | Review indipendente della parte A e dell'arbitro, solo per lettura (`review_partA.md`) |
| 22:50-23:15 | Review indipendente della rimozione di history7 (`review_removal.md`): un difetto, corretto nella sandbox alle 23:08 |
| Fino alle 23:34 | Audit di pulizia, solo rapporto (`cleanup_audit.md`) |
| 23:37-23:38 | La sessione principale annulla le due catene per proteggere il run (file `chain.CANCEL` e `h7.CANCEL`; "cancelled" nei log alle 23:38:30 e alle 23:38:05) |
| Verso le 23:48 | Decisioni dell'utente sulla pulizia |
| 23:49 | La sessione principale cancella 13 branch locali e 14 branch remoti già uniti [V, handoff §4] |
| Verso le 23:50 | L'utente revoca le eccezioni sul modello: solo Opus 5.5 come subagent |
| Verso le 23:55 | L'utente fissa l'ambito del prodotto |
| 00:00:17 | Gate della memoria della coda chiuso: 17.392.736 KB liberi contro 18.874.368 (l'utente aveva riaperto Brave) |
| 00:08:11 | Commit `dafcee2` dell'handoff; il suo controllo indipendente `9ab2035` alle 00:39:11 |
| 00:09:29-00:09:30 | Dopo la chiusura di Brave il gate si apre con 20.549.132 KB liberi; parte il runner del round 1 (pid 11760) |
| Verso le 00:20 | L'utente decide di togliere la vecchia GUI desktop |
| 00:23:52-00:25:30 | Quattro commit della rimozione della GUI su `feat/threeway-step2` |
| Verso le 00:47 [I, date delle cartelle] | Le due istantanee di riferimento `smoke/P1` e `smoke/P3` `it_50` copiate fuori da F: |
| 00:48:55-00:53:13 | L'utente esegue `delete_data.ps1`: 1.848 voci, 445,34 GiB, 0 errori |
| 00:54:30 | `post_delete.sh`: tolte 34 junction rimaste senza destinazione, potati 7 worktree, cancellati 7 branch |
| Verso le 00:55 | Le due istantanee rimesse al loro posto come cartelle vere su C: |
| 01:17:33 | Commit `d026e86` dell'addendum della web UI |
| 01:19:53 | Run 3-way 1 a 2.500 iterazioni |
| Verso le 01:30 | Risposte dell'utente a QA1 e QA4 dell'addendum; QA2 e QA5 spiegate |
| Fra le 01:30 e le 01:35 [I] | Risposte dell'utente a QA2 e QA5, decisioni sui test legacy del postflop e su un lettore del postflop dello step 2 (la nota di memoria che le registra è salvata alle 01:35:36) |

**Decisioni dell'utente.**

| Quando | Decisione |
|---|---|
| Verso le 21:26 [I] | Togliere history7: "Se non viene utilizzato per HU50, che è il nuovo standard, bisogna eliminarlo". Lo standard sono le righe per classe di board di HU50. Scelta contraria alla raccomandazione dello studio (B, congelarlo) |
| Verso le 21:40 [I] | Fable 5.1 ammesso come subagent per la parte A e la rimozione di history7, e sempre per gli audit di pulizia |
| Verso le 23:48 | Pulizia dopo l'audit: cancellare i branch uniti; cancellare i dati di history7 e della suite, i run della batteria di correttezza, i vecchi smoke, `state.ckpt` e `policy.bin` delle varianti superate (tenendo intere `HU50_m30x4_rake25`, `HU50_g1_rake` e `HU50_lock_all_m30x4_rake25`), il worktree di Codex e le vecchie build. Lo script lo esegue l'utente. Rimozione del codice legacy: "Non ancora" |
| Verso le 23:50 | "Utilizza solo opure 5.5 come subagent": revocate le eccezioni, solo Opus 5.5, anche per gli audit. Due agenti Fable erano morti durante un cambio di modello e il lavoro è ripartito con Opus [V, sessione principale] |
| Verso le 23:55 | "Il solver preflop, non va eliminato. Questo solver dovrà avere sia il preflop che il postflop": un solo solver con preflop e postflop. Il solver preflop non si toglie mai; il codice legacy del postflop HU esatto resta finché non c'è una decisione di progetto |
| Verso le 00:20 | "Per la vecchia UI, in quanto non piu' utile, e' possibile anche eliminarla direttamente" |
| Verso le 01:30 | QA1: i range degli studi HU postflop vengono sia dalla nostra soluzione preflop sia dall'inserimento a mano. QA4: MonkerSolver non serve più nella UI e resta nascosto. QA2 e QA5 spiegate |
| Fra le 01:30 e le 01:35 [I] | QA2: i tre comandi nuovi (solve con i range dell'utente e un target di precisione, eventi di progresso JSONL, worker `query`/`serve`) vanno dentro `gto_cli` e si scrivono dopo la chiusura del lavoro 3-way. QA5: la UI lancia il solver vero per i suoi test nativi solo quando non è attivo nessun run di training; l'osservazione in sola lettura dei run vivi è sempre ammessa. Test legacy del postflop (`phase7`, `phase10`, `gto_plus_reference`): si rifanno nel ciclo di build e test dopo il run 1, e i guasti si correggono prima che la UI usi davvero `gto_cli`. Lettore del postflop dello step 2: uno strumento che legge e mostra la parte postflop di una policy dello step 2 (HU e 3-way: il postflop sparso con le righe per classe di board), anche se approssimata; oggi si esportano solo le chart preflop; la UI la mostrerà [V, nota di memoria `ui-postflop-decisions-2026-10-02.md`] |

**history7: decisione, tag, rimozione pronta e non applicata** [V, `recommendation.md`, `removal_note.md`, `review_removal.md`,
`history7_removal/chain.log`, `git cat-file -p history7-final`].

- Lo studio: history7 non è usato da HU50 né dal 3-way e non scala (circa 51,6 GB in float32 al 3-way [I, studio]); è però
  l'unica best response esatta dentro un'astrazione. Raccomandava di congelarlo; l'utente ha deciso di toglierlo.
- `history7-final` (tag annotato, 21:36:26) = `88118a6`, l'ultimo albero con history7.
- Rimozione preparata in una sandbox del worktree della fase 3 (`out/laneH/src` = `2aa24d8` più la rimozione): la classe
  `HistoryBucketRows` (GTOSDHR1 e HR2), la best response astratta esatta, i tre eseguibili solo history, l'opzione
  `--history-rows` dei CLI, la suite HU10-HU40 (`benchmarks/suite`, `suite.py`, `run_queue.sh`), i due script del solve HU40 del
  21/09 e i test di history; circa 5.400 righe tolte e 87 scritte. Resta il modo di valutazione a policy fissa del trainer, su
  cui si basa la parte A. L'identità del trainer non cambia per i run senza history.
- Review indipendente: approvata con un difetto, corretto nella sandbox alle 23:08. Le 7 fixture
  `benchmarks/suite/fixtures/*.json` restano, perché `test_fingerprints_without_the_cap` dei test del gioco le carica: senza di
  loro quel test sarebbe fallito dal primo commit.
- Con history7 si ritirano il gate "best response astratta ≤ 0,03 a" e la suite HU10-HU40 con i suoi numeri di riferimento; i
  documenti che li riportano restano come storia. I criteri di accettazione del prodotto sono da proporre all'utente (handoff
  T16).
- Configure, build, test e V1 (identità HU byte per byte, attesa IDENTICAL [I, review]) non sono partiti: la catena è stata
  annullata alle 23:38:05 mentre aspettava le 00:00. Si fanno dopo il run e dopo la parte A (handoff T9).

**Fase 3b: parte A e arbitro 3-way** [V, `partA.md`, `review_partA.md`, `referee3.md`, `chain.log`]. Dettagli nella sezione 9.7
della ricetta.

- Parte A scritta nel worktree della fase 3 e non committata: `trainer.hpp` +151, `trainer.cpp` +742, la CLI nuova
  `gtosd_preflop_blueprint_policy_values` (1.354 righe), i test V11 (713), `tools/monker_compare/part_a_values.py` (226). Mai
  compilata.
- Review per lettura: nessun difetto di correttezza. D1 (bassa): gli errori standard dell'EV aggregata, del guadagno preflop e
  della perdita delle chart trascurano il termine del denominatore di uno stimatore a rapporto; le stime puntuali non cambiano
  e l'errore standard dell'EV diretta è esatto. Patch pronta (`review_partA/patch_partA_se.py`), non applicata. D3 (bassa): la
  normalizzazione della portata degli avversari a tre posti è coperta da un solo controllo; tre controlli suggeriti.
- Arbitro del postflop 3-way, `2aa24d8`: cinque giochi 3WAY50 PASS, 0 errori di regola; quattro hanno 7.225 nodi e 10.410
  righe di payoff, quello con l'all-in fino a 5 volte il piatto 7.126 e 10.236. Side pot dimostrati irraggiungibili (stack
  uguali). La review lo ha messo alla prova con otto mutazioni del dump del motore: prese tutte.
- Le catene annullate alle 23:37-23:38 avrebbero compilato e provato dalle 00:00 accanto al run (-j 2, almeno 4 GB liberi). Si
  rilanciano dopo il run (`after_run.md`, handoff T4-T9).

**Vecchia GUI desktop tolta** [V, `gui_removal/removal.md`, `git log` e `git diff --shortstat 2aa24d8 c2e9138` nel worktree
della fase 3].

- Quattro commit su `feat/threeway-step2`: `921f424`, `1591a10`, `797a7d4`, `c2e9138`. Tolgono `apps/gto_gui`,
  `apps/gui_qt_prototype`, `apps/gui_imgui_prototype`, le due opzioni CMake, il preset `windows-gui-release`, la feature vcpkg
  `gui-prototypes` e i due script F9; `tests/install_consumer` ora si collega a `gtosd::storage`. In tutto 20 file, +9 / −4.178
  righe.
- La libreria `gui_prototype` e `gtosd_phase9_tests` restano fino a un commit rinviato, dopo quello della parte A, perché
  toccano `tests/CMakeLists.txt`, che ha modifiche della parte A non committate.
- Nessuna build né test: dopo il run. La web UI (`apps/solver-ui`) prende il posto della GUI. Il messaggio di `921f424` dice
  che la web UI lancia `gto_cli`, ma oggi il suo codice non lo nomina [V, `webui2/ui_map.md`, `git grep` in
  `GTO-Solver-solver-ui`]: l'adattatore è proposto nell'addendum (sezione 4).

**Pulizia dei dati, dei worktree e dei branch** [V, `delete_data.log`, `post_delete.log`, `variants_summary.txt`; per i branch
delle 23:49 handoff §4].

| Gruppo | Voci | GiB |
|---|---:|---:|
| F: history7 e suite (`hierarchy32`, `history7_optimized`, `hu40_history7_solve`, `matrix`, `suite`) | 5 | 158,46 |
| F: batteria di correttezza (`out/monker/correctness`) | 1 | 58,08 |
| F: vecchi smoke | 5 | 20,37 |
| F: `state.ckpt` e `policy.bin` delle varianti superate | 64 | 112,11 |
| C: worktree di Codex | 1 | 73,61 |
| C: vecchie build e cartelle | 1.772 | 22,73 |
| **Totale** | **1.848** | **445,34** |

- Il log scrive "GB", ma divide i byte per 2^30: sono GiB [V, byte nel log].
- Le varianti tengono le chart (`*_strategy.txt`), i JSON e i log, quindi il viewer HU funziona ancora; di `HU50_a_seed2` e
  `HU50_lock_all` sono andati anche i 10 `policy.bin` degli snapshot dentro `charts/` [V, `delete_data.log`];
  `HU50_m30x4_rake25`, `HU50_g1_rake` e `HU50_lock_all_m30x4_rake25` restano intere.
- Del worktree di Codex sono state prima sganciate le due junction interne, che puntavano alle cartelle dei bucket e delle
  risorse del checkout principale (rimaste intatte).
- `post_delete.sh` (00:54:30): tolte con `rmdir`, senza ricorsione, le 34 junction rimaste senza destinazione; potati 7
  worktree; cancellati 7 branch (4 uniti e 3 di ricerca coperti dai tag `archive/research/*`). Restano il checkout principale e
  i worktree `GTO-Solver-phase3` e `GTO-Solver-solver-ui`. In locale restano i branch `main`, `feat/monker-step1-checkdown`,
  `feat/threeway-step2` e `feat/solver-ui`; su origin `main` e `feat/monker-step1-checkdown` [V, `git branch -a`].
- Le istantanee di riferimento `out/monker/correctness/smoke/P1/charts/it_50` e `.../P3/charts/it_50`, usate dagli strumenti S1
  e S5a di `tools/independent`, sono state salvate prima e rimesse come cartelle vere su C: (5 voci ciascuna, 131 MiB in tutto
  [V, `ls`, `du`]).
- Spazio libero dopo la pulizia: C: 354,7 GiB, F: 505 GiB [V, sessione principale; `df` alle 01:29 dà 355 e 505].
- Il codice legacy (postflop HU esatto, `libs/solver`, `libs/postflop` e simili) resta: "Non ancora".

**Il run 3WAY50 1 è partito** [V, `queue.log`, `run.log` e `train.jsonl` della cartella del run, letti senza toccarla].

- Alle 00:00:17 il gate della memoria era chiuso (17.392.736 KB liberi contro 18.874.368); si è aperto alle 00:09:29 con
  20.549.132 KB, e il runner del round 1 è partito alle 00:09:30.
- Avvio del trainer: albero `fnv1a64:71abeabaab92fe56` (lo stesso che l'arbitro ha registrato per `3WAY50_donk_rake25cap2`),
  7.225 nodi, 3.122 decisioni, stato 14.324.289.748 byte, 8 thread, DCFR con sconto lazy.
- Passo: 1,745 s per iterazione sulle prime 1.500 (2.618 s). Alle 01:19:53, 2.500 iterazioni in 4.214 s: 1,686 s di media e
  1,596 nelle ultime 1.000 [I, rapporti]. Memoria del processo circa 14,8 GB.
- Fine prevista [I, sessione principale, a 1,745 s per iterazione]: 16.000 iterazioni verso le 08:00 del 02/10; l'arresto
  atteso fra 24.000 e 40.000 verso le 11:50-19:40; il tetto di 48.000 verso le 23:40, prima della prima pausa (03/10 alle
  19:40). Alla media di 1,686 s delle prime 2.500 iterazioni tutto arriverebbe fra un quarto d'ora e tre quarti d'ora
  prima [I].

**Handoff e addendum della web UI** [V, `git log`, i due documenti].

- `dafcee2` e il controllo `9ab2035`: [NEXT_STEPS_2026-10-02.md](../../handoff/NEXT_STEPS_2026-10-02.md), i lavori T1-T17 per
  un agent coder, con il calendario proposto e le decisioni da raccogliere (U1-U12).
- `d026e86`: [WEB_UI_ADDENDUM_ENGINES_2026-10-02.md](../../solver-ui/WEB_UI_ADDENDUM_ENGINES_2026-10-02.md), per l'agent coder
  della web UI: instradamento automatico dei motori, adattatore di `gto_cli`, supporto 3-way, difetti F1-F8 (F8: la guardia
  d'avvio di 15 s della UI ucciderebbe ogni resume 3-way, che carica il checkpoint in 75-270 s [I]), domande QA1-QA7.
- `gto_cli` resta come motore HU postflop esatto, ma non ha range, target di precisione né interrogazioni di un nodo per tutte
  le mani: servono tre comandi sottili, che vanno dentro `gto_cli` (QA2). I suoi test legacy (`phase7`, `phase10`, `gto_plus_reference`) non hanno run
  registrati da quando `libs/core` è cambiata il 28/09 [I, `webui2/gto_cli.md`, dai dati di ctest].
- Risposte dell'utente verso le 01:30, registrate nella sezione 8 dell'addendum: QA1, entrambe le fonti (i range che arrivano
  al flop lungo una linea preflop, ricavati dalle nostre chart; l'inserimento a mano con l'editor 9×9 o la sintassi GTO+);
  QA4, MonkerSolver nascosto nella UI. QA2 e QA5, dopo un chiarimento, hanno avuto risposta entro le 01:35 (vedi le
  decisioni); restano aperte QA3, QA6 e QA7.

**Cosa resta**, dopo il run e secondo l'handoff (T3-T12): il risultato del run 1; build, V11 e commit della parte A (con la
scelta su D1), la parte A campionata su 64 flop fisici del run 1 e il gate 3b; l'archivio del run 1 su F:; la rimozione di
history7 (build della sandbox, ctest, V1 completo, commit); il commit rinviato di `gui_prototype` con build e ctest della
rimozione della GUI (da `gui_removal/removal.md`, non dall'handoff); la configure e la ctest dell'arbitro; il merge in `feat/monker-step1-checkdown`; il test del blocco a
15 × 4; la proposta della batteria di correttezza 3-way; i test legacy del postflop, nel ciclo di build e test dopo il
run 1 (decisione dell'utente); il lettore del postflop dello step 2 e i tre comandi dentro `gto_cli`, dopo il lavoro 3-way
(per i comandi è la risposta a QA2); le risposte a QA3, QA6 e QA7; il push quando l'utente lo chiede.

### 2026-10-01 sera (16:30-18:15) — gate 3a completato a 15 × 4, blocco della memoria, guardie del 30 fermate, merge, congelamento e coda del primo run 3WAY50

Dettagli e tabelle nella sezione 9.7 di [MONKER_RECIPE_REPRODUCTION_2026-09-28.md](MONKER_RECIPE_REPRODUCTION_2026-09-28.md).
Rapporti nella cartella temporanea della sessione: `threeway/phase3a/gate3a_final.md`, `final_chain.log`, `final_chain_run2.out`;
`threeway/run1/NOT_READY.md`, `final_freeze.sh`, `queue_3way50_15x4.sh`, `qtest2/driver.log`. [V] = verificato, [I] = inferito.

**Cronologia** [V].

| Ora | Fatto |
|---|---|
| 16:30:42-16:33:49 | Build della suite a `12fe441` (-j 4, nessun warning con /W4 /WX); eseguibili copiati in `phase3a/final_bin`, su cui V1 è IDENTICAL contro `v1_baseline` e `v1_c123` |
| 16:31-16:42 | Archivio su F: dei quattro run HU del giorno (10.13) |
| 16:36:47-16:44:47 | ctest 101/101 (480,3 s) |
| 16:45:03-16:50:39 | Gamba 3-way di V9: PASS su `3WAY50_donk_rake` e `_rake25cap2` |
| 16:50:41-17:21:26 | Smoke a 15 × 4: il gate della memoria resta chiuso per 30 minuti, timeout (rc 3); V10 e V12 non partono |
| 16:51-16:57 | Prova della pausa e dell'annullamento della coda con il trainer vero su HU G1 |
| 17:24 | `run1/NOT_READY.md`: niente merge, congelamento o coda finché i controlli a 15 × 4 non passano |
| 17:36-17:37 | Congelamento di prova in una cartella di staging e dry run della coda: DRY_RUN_OK (il gate della memoria allora sarebbe stato chiuso: 15.672.948 KB liberi) |
| 17:39-17:54 | Prove della coda con un trainer finto: un primo giro non valido (codici di uscita letti in una subshell), il secondo tutto PASS (T7 dopo una correzione del driver) |
| Verso le 17:58 | La sessione principale ferma le guardie del 30 |
| Verso le 18:00 | L'utente chiude Brave e sceglie il checkpoint ogni 16.000 iterazioni; `R_CHECKPOINT_EVERY=16000` nello script della coda |
| 18:00:33-18:11:10 | Catena finale: smoke a 8 e 4 thread, V10 e V12 a 15 × 4, tutti PASS |
| 18:11:32 | Merge `238a41e` di `feat/threeway-step2` in `feat/monker-step1-checkdown` |
| 18:11:39-18:11:40 | Congelamento nel checkout principale |
| 18:11:54 | Coda del primo run lanciata (pid 1644), in attesa delle 00:00 del 02/10 |

**Decisioni dell'utente.**

| Quando | Decisione |
|---|---|
| Verso le 18:00 | Chiude Brave per liberare la memoria che teneva fermi i controlli a 15 × 4 |
| Verso le 18:00 (domanda della sessione principale) | Checkpoint ogni 16.000 iterazioni nel run 3-way, per non perdere il lavoro in un crash o in un riavvio di Windows Update, al posto di "niente checkpoint periodici sull'i3" (D7 della specifica) |
| Sera | Tenere aperta questa sessione di Claude per la notte e non riaprire Brave |

Restano valide le decisioni della giornata (voce delle 08:00-16:30): D1-D4 e D7 come proposte, D5 senza server (ambito ridotto:
parte A campionata su 64 flop fisici, un solo rake, test del blocco solo a 15 × 4, nessun run 30 × 4), D6 superata; il 02/10 senza
la finestra solita, dal 03/10 pausa alle 19:40 e ripresa alle 00:00.

**Il blocco della memoria e la soluzione** [V].

- I controlli a 15 × 4 e il run vogliono almeno 18.874.368 KB liberi (14,8 GB del trainer più 3 di margine) e nessun altro
  training.
- Dalle 16:50:41 alle 17:21:26 il gate è rimasto chiuso: 14,7-16,0 GB liberi (31 letture). Sotto 15 GB solo fra le 16:51 e le
  16:56, mentre girava la prova della coda con il trainer vero su HU G1 (il gate non conta i training del worktree della fase 3);
  dalle 16:57 alle 17:21, senza training nostri, 15,6-16,0 GB.
- La memoria la tenevano le applicazioni dell'utente (memoria privata alle 16:58): Brave 4,2 GB in 33 processi, Claude 2,8,
  ChatGPT 1,8, Steam 1,0, Telegram 0,9, Discord 0,8.
- Alle 17:24 il passo del run 1 si è fermato senza toccare nulla (`NOT_READY.md`, 15.826.468 KB liberi).
- Verso le 18:00 l'utente ha chiuso Brave. Alle 18:00:35 i KB liberi erano 18.700.448, appena sotto; alle 18:03:39 il gate si è
  aperto con 19.051.048, e alle 18:09:38 erano 21.769.376.

**Guardie del 30 fermate** [V, sessione principale]. Verso le 17:58 la sessione principale ha fermato `ext/stop_ext_1958.ps1` e
`night2/stop_night2_1958.ps1`, ancora armate alle 16:35 (voce precedente): alle 19:40 avrebbero scritto file CANCEL nelle cartelle
dei loro run, già archiviate su F: attraverso le junction.

**Controlli: il gate 3a è completo** [V, `final_chain.log`, `final_chain_run2.out`, `train.jsonl` degli smoke].

- Build a `12fe441`, ctest 101/101, V1 IDENTICAL.
- Gamba 3-way di V9 (19.998 board canonici, 6 iterazioni, 8 thread, 27,5-27,7 s per iterazione), `3WAY50_donk_rake` /
  `_rake25cap2`: 1,45e-12 / 5,64e-12 all'iterazione 1 (soglia 1e-9), righe medie 2,8e-11 / 3,5e-12 (soglia 0,001), EV 1,2e-13 /
  4,6e-13 a e guadagno 2,9e-13 / 5,3e-13 a (soglia 1e-6 a). Era l'unico oracolo indipendente della traversata preflop a 3 seggi
  rimasto aperto.
- Smoke a 8 thread (20 iterazioni): **1,499 s per iterazione**, picco del working set 14,86 GB, stato 14,32 GB; a 4 thread (10
  iterazioni) 1,766 s.
- V10 a 15 × 4: stato `fnv1a64:6a94a517133aa13a` identico a 8, 2 e 1 thread (1,418, 2,825 e 5,022 s per iterazione), 54 chart con
  lo stesso digest, 0 file non finiti: PASS.
- V12 a 15 × 4: albero PASS `nodes=54 files=54`; blocco 2.681 righe + 1.688 fuori range + 5 di ripiego; ritorno con differenza
  massima 0,0010 e 0 righe oltre la tolleranza: PASS.
- V7: la clausola della traiettoria resta il limite del disegno del test accettato dalla review. V2-V6, V8, la gamba HU di V9,
  V9b e V13 erano già PASS all'integrazione, e quelli che sono test di ctest ripassano nella 101/101 della build finale.
- Scala [I]: da 1 a 2 thread 1,78 volte, da 1 a 4 2,84, da 1 a 8 3,54 (4 core fisici: oltre i 4 thread conta l'hyperthreading);
  un fit di Amdahl dà circa il 12 % di lavoro non parallelo, quindi su 48 core fisici circa 0,5-0,8 s per iterazione, a parità di
  velocità per core e senza limiti di banda della memoria. La specifica stimava 2-4,5 s sull'i3: il passo misurato è più veloce.

**Merge, congelamento e lancio** [V].

- Merge `238a41e` alle 18:11:32: `git diff 12fe441 238a41e` su `libs`, `include`, `benchmarks`, `tests` e `tools` è vuoto. Il branch
  `feat/monker-step1-checkdown` è 12 commit avanti a origin al merge, non pushato.
- Congelamento con `run1/final_freeze.sh` (FREEZE_OK e DRY_RUN_OK): `out/monker/bin_3way_step2/` (train `12c0dd6b8ccd…`,
  monker_tree `274505865911…`), `out/frozen/threeway_step2_12fe441/` (runner, `compare_charts.py`, configurazione, manifest e
  mappa TX2 da `git show 12fe441`), `out/frozen/queue_3way50_15x4.sh` (sha256 `7581f0f8…`).
- Coda lanciata alle 18:11:54 dalla sessione principale (pid 1644 in `queue.lock`, nessun override, tutti i controlli ok, 267 GB
  liberi su C:): parte alle 00:00 del 02/10 se ci sono almeno 18.874.368 KB liberi, altrimenti aspetta e lo scrive nel log ogni 10
  minuti. Uscita `out/monker/step2_3way/3WAY50_15x4_rake25cap2`.
- Run: 15 × 4 + TX2 double, rake 2,5 % / cap 2a, 8 thread, arresto a 0,008 sulle 18 chart non all-in ogni 4.000 iterazioni,
  minimo 16.000, tetto 48.000, checkpoint e policy ogni 16.000. Il 02/10 gira senza pausa; dal 03/10 pausa alle 19:40 (solo
  checkpoint) e ripresa alle 00:00.
- Le prove della coda sono state fatte con `CHECKPOINT_EVERY=0`, prima della decisione delle 18:00 (la copia provata differisce
  da quella congelata solo per `R_CHECKPOINT_EVERY`): il checkpoint periodico, lo stesso salvataggio del trainer delle pause e
  della fine, con questa coda e a 15 × 4 non è provato.

**Fine prevista** [I]. A 1,5 s per iterazione (± 30 %): 16.000 iterazioni in circa 7 ore; l'arresto atteso dalla specifica fra
24.000 e 40.000 (una stima per analogia con l'HU) in circa 10-17 ore, cioè il 02/10 fra le 10:30 e le 17:30; il tetto di 48.000 in
circa 20 ore. Anche con il 30 % in più il tetto cade prima della prima pausa (03/10 alle 19:40): secondo `gate3a_final.md` basta
restare sotto 3,4 s per iterazione. Il costo dei checkpoint ogni 16.000 non è misurato (la specifica stima 1,3-4,5 minuti
ciascuno).

**Cosa resta**: il risultato del run 1; la fase 3b (parte A e V11, gate 3b); la parte A campionata su 64 flop fisici; il test del
blocco a 15 × 4 (24.000 iterazioni); la proposta della batteria di correttezza 3-way chiesta dall'utente la mattina, che non è
nei file letti per questa voce; il push del branch quando l'utente lo chiede.

### 2026-10-01 (dalle 08:00 alle 16:30) — batteria HU chiusa (B2L, B1L, HU19_B1L, S2-DLL), criterio 3 rivisto, archivio, fase 3a del 3-way, decisioni dell'utente

Batteria HU: sezione 10.13 di [MONKER_RECIPE_REPRODUCTION_2026-09-28.md](MONKER_RECIPE_REPRODUCTION_2026-09-28.md). Fase 3a:
rapporti `threeway/phase3a/setup_report.md`, `gate3a.md`, `review.md` e `deferred_tests.md` nella cartella temporanea della
sessione; worktree `C:/Users/GoryNickel/Documents/GitHub/GTO-Solver-phase3`, branch `feat/threeway-step2` (da `d0796f8`, non
pushato), [specifica](threeway/PHASE3_SPEC_2026-09-30.md). [V] = verificato, [I] = inferito.

**Decisioni dell'utente.**

| Quando | Decisione |
|---|---|
| Verso le 08:00 | Già nella voce della mattina: estendere B2L e B1L, eseguire HU19_B1, terzo parere con la DLL, push |
| Mattina, prima delle 09:24 (nota di memoria delle 09:19) | **Fase 3, proposte della specifica (sezione 9) accettate.** D1: 15 × 4 double sull'i3. D2: rake 2,5 % / cap 2a, poi 5 % / cap 0,75a. D3: arresto a 0,008 sulle 18 chart non all-in ogni 4.000 iterazioni, minimo 16.000, tetto 48.000. D4: parte A, campionata sull'i3. D7: confermata. D5: niente server, quindi l'ambito ridotto sull'i3 (parte A campionata su 64 flop fisici, un solo rake, test del blocco solo a 15 × 4, nessun run 30 × 4). D6: superata (riguardava le build della notte del 30) |
| Mattina | Il rake serve solo a riprodurre le chart di MonkerSolver (impostazioni inferite). Nelle soluzioni sue l'utente sceglierà il proprio: percentuale, cap, no flop no drop e piatto minimo sono chiavi di configurazione |
| Mattina | Prima di un run della fase 3, proporre una batteria di correttezza 3-way: oracolo esatto contro un CFR indipendente, regole e payoff indipendenti, guadagni di deviazione per giocatore (con 3 o più giocatori non c'è garanzia di Nash) |
| Mattina | Scrivere subito il codice della fase 3: preparazione dalle 09:24 |
| 09:10 e 10:29 | Due push di `feat/monker-step1-checkdown` [V, reflog di origin]: fino a `5bc2a9c` e fino a `0d7ef75` |
| Verso le 10:30 | Criterio 3 di T2: discesa tardiva, pendenza fra Tmax/4 e Tmax <= −0,35, al posto del fit del pavimento, che si riporta soltanto (10.13). Deciso prima dei risultati delle estensioni |
| Verso le 15:00 | Il 02/10 senza la finestra solita: il primo run 3-way può girare tutto il giorno. Dal 03/10 di nuovo la finestra normale (run 00:00-20:00, STOP alle 19:40 con checkpoint, ripresa alle 00:00), salvo nuove indicazioni |
| Verso le 15:00 | Finire stasera i controlli rinviati della fase 3 e, se tutto è verde, partire con il primo run 3WAY50 alle 00:00 del 02/10: D1 15 × 4 double sull'i3, D2 rake 2,5 % / cap 2a, D3 arresto 0,008 come sopra |

- **Correzione della voce della mattina** [V, reflog]: il primo push è delle 09:10:32, non delle 08:00. Le 08:00 sono l'ora
  della decisione [I].
- **Calendario**: la specifica (sezione 10) metteva il run principale alle 00:00 del 01/10; parte il 02/10, un giorno dopo,
  perché l'utente ha messo prima la chiusura della correttezza HU. Le date della specifica dal 01/10 in poi vanno quindi
  rilette con un giorno di ritardo [I].

**Batteria HU: chiusa con 0 FAIL** [V, `correctness/day1/results.md`, `checks.md`, `queue.log`].

- Coda `day1/queue_day1.sh` dalle 09:51 alle 16:28, eseguibili `c123`, 3 thread per run.
- **B2L** (estensione 09:51-13:42): dallo 0,13120 a (4,373 % del piatto) a 64.000 a **0,02280 a (0,760 %) a 256.000**;
  discesa tardiva −1,262, controllo circa 356 volte sopra a 256.000 [I]: **PASS**.
- **B1L** (estensione 09:51-14:02): dallo 0,09389 a (3,130 %) a **0,04556 a (1,519 %)**; discesa tardiva −0,522, controllo
  circa 29 volte sopra [I]: **PASS**.
- **HU19_B1L** (14:03-16:28, le forme complete dei piatti limpati di HU50, flop e turn esatti): dal 169,6 % a 250 all'**8,495 %
  a 64.000**; pendenza −0,760 fra 8.000 e 64.000, discesa tardiva −0,722, controllo circa 20,7 volte sopra [I]: **PASS**.
- In tutti e tre ogni snapshot è PASS exact, i componenti scendono in ogni raddoppio e non c'è nessuna condizione di FAIL.
- Con la regola vecchia B2L passerebbe comunque, B1L e HU19_B1L no; con la nuova B2L e B1L passano anche sulle righe fino a
  64.000.
- **S2-DLL** (09:19-09:23, `d0796f8`): tutto PASS.
  - R7: 8.347.680 insiemi di 7 carte nello stesso ordine debole del motore, 752 livelli.
  - RV: 1.000.000 di river, 0 differenze.
  - PA e PA2: 2.012 coppie preflop, equity identiche bit per bit e W/T/L uguali.
  - Le mutazioni sono prese.
- **Quadro finale**: tutto PASS tranne la lettera del livello G1 di V1L (0,553 % a 448.000, al ritmo Monte Carlo) e S4 su
  HU6_all, INCONCLUSIVE per costruzione. Nessun FAIL in tutta la batteria.
- **Archivio su F:**
  - Mattina, 09:13-09:40: 20 cartelle, circa 30,9 GB (i 18 run finiti, `smoke2` e `checks`); poi `smoke` e `R3`.
  - Pomeriggio, 16:31-16:42: B2L, B1L, HU19_B1L e HU19_B1LG1, circa 19,9 GB, ognuna verificata attraverso la junction. F: ha
    156,0 GB liberi.

**Fase 3a del 3-way** [V, salvo dove è segnato I].

- **Preparazione** (09:24-10:19):
  - worktree e branch da `d0796f8`; build completa in 29,9 minuti a `-j 2`;
  - ctest di base 96/96: 4 test erano falliti solo perché il checkout aveva scritto una fixture con CRLF; corretto con un
    checkout LF, era un artefatto dell'ambiente;
  - script di V1 (identità byte HU) riusabile dopo ogni merge.
- **Codice** in tre corsie, commit dalle 10:37 alle 11:38:
  - corsia T: cache delle classi preflop a tre giocatori (`306464a`), percorso del trainer a 3 seggi (`161758d`, `fef4305`);
  - corsia K: kernel multiway a tre seggi (`86dd104`, `2ebc4a5`);
  - corsia C: metrica di arresto 3-way, impostazioni del runner e pausa giornaliera (`12492c3`); CLI di training, chart e
    blocco delle chart a 3 seggi (`4e4bce3`).
- **Integrazione** (12:22-14:25):
  - nessuna correzione necessaria fra le corsie;
  - test nuovi K5 (`726c112`) e T3 (`6ec3016`);
  - ctest 99/99 su `4e4bce3` e 101/101 dopo K5 e T3 (1.037,6 s);
  - V1 IDENTICAL: 13 campi × 3 fixture (g1, rake, hu10).
- **Gate 3a.** Ha girato sugli alberi e sul codice di produzione, ma con le tabelle piccole 200/500/1000 per board (circa 5
  milioni di celle invece di 847 milioni), perché la memoria libera era 5,8-8 GB con la coda HU:
  - **PASS**:
    - V1 identità HU;
    - V2 kernel contro forza bruta (4,4e-15);
    - V3 seggio foldato uniforme;
    - V4 cache delle classi (115 tensori identici al passo 1, scala esatta 0,512140 su tutti i 278.256 board);
    - V5 payoff dei perdenti e dei foldati (quattro giochi alterati rifiutati);
    - V6 scorciatoia sì/no (4,6e-15 della scala);
    - V8 identità del rake attraverso `terminal3` (1,65e-15);
    - V9 gamba HU (6,9e-14 e 8,4e-14 contro il solver per classi del passo 1, soglia 1e-9);
    - V9b cache = kernel (5,2e-14);
    - V13 forza bruta a livello del trainer (1,2e-14, soglia 1e-12);
    - V10 (stato identico a 1, 2 e 8 thread) e V12 (54 chart, blocco 2.681 + 1.688 + 5 righe, round trip entro 0,0010) a
      tabelle piccole.
  - **V7 parziale**: le parti deterministiche passano. Un'iterazione da zero è a 2,0e-13 / 4,1e-13 della scala, un'iterazione
    alternata da una policy densa a 0,076 / 0,045 della tolleranza. La clausola della traiettoria a 100 iterazioni (1e-6 a
    sull'EV, 0,001 sulle chart) non è rispettata: 0,46 / 0,33 sulle righe preflop, 0,028 / 0,019 a sull'EV.
    - Causa: i pareggi esatti lasciano residui di arrotondamento. Dopo l'iterazione 1, 122 celle sono esattamente 0 su un solo
      percorso e 122 hanno il segno opposto (per esempio −3,1e-23 contro +4,0e-23).
    - Il regret matching ne fa righe diverse dall'iterazione 2, e le traiettorie campionate si separano come due seed.
    - È un limite del disegno del test, non un difetto: la clausola presumeva che l'arrotondamento restasse piccolo attraverso
      il regret matching. La review è d'accordo.
  - **Rinviati**:
    - lo smoke 3WAY50 a 15 × 4 (secondi per iterazione, picco di RAM), V10 a 15 × 4 e V12 a 15 × 4: ciascuno vuole almeno
      18 GB liberi e nessun run HU;
    - la gamba 3-way di V9 (circa 1 GB, 25-35 minuti), fermata alle 13:48 quando HU19_B1L aspettava la memoria. È l'unico
      oracolo indipendente per la traversata preflop a 3 seggi e per il calendario alternato dei tre eroi.
- **Tempi** a tabelle piccole, con la macchina carica: 12,4 s per iterazione a 1 thread, 5,8 a 2, 2,6 a 8. A 15 × 4 non sono
  misurati: circa 2-4 s a 8 thread e picco circa 14,8 GB secondo la specifica [I].
- **Review indipendente** (14:20-14:56; tutto il diff letto, 9 commit, 27 file):
  - nessun difetto di correttezza;
  - quattro mutazioni di prova (buffer di unità stantio, folder letto dal bit sbagliato, vettore congelato sbagliato,
    trasposizione sbagliata nella cache), mai committate, sono state prese ciascuna dal test atteso;
  - tre difetti minori corretti in `12fe441`: `--canonical-river-boards` ora rifiutato senza `--checkdown`, errore esplicito
    della CLI per la scorciatoia spenta a 3 giocatori senza `board_kernels`, byte delle liste di unità mancanti nella telemetria
    della memoria; più un commento;
  - V1 IDENTICAL dopo la correzione, su una build sandbox. La ctest completa su `12fe441` non è stata rifatta: è sicura [I], e
    spetta alla build finale;
  - tre rilievi informativi, fra cui il file di pausa letto prima del file di stop: una finestra di secondi al giorno in cui
    un run può fermarsi in pausa senza `policy.bin`, con l'arresto rimandato allo snapshot seguente.
- **Incidenti di processo**: il classificatore dei permessi ("Interfere With Workloads") ha rifiutato tre azioni, nessuna
  ripetuta: il rilancio della gamba 3-way di V9, una lettura diretta del log della coda HU e della memoria libera verso le
  14:00, un grep di tre log di integrazione.
- **Cosa resta prima del run** (review, sezione 6; `deferred_tests.md`), stasera entro le 21:00:
  - i controlli a 15 × 4 e la gamba 3-way di V9, ora possibili perché la coda HU è finita;
  - la build finale della suite su `12fe441`, la ctest completa (101 test) e V1;
  - il congelamento in `out/monker/bin_3way_step2` e `out/frozen/run_step2_continuous_3way.sh` del checkout principale;
  - lo script della coda giornaliera: PAUSE alle 19:40, ripresa alle 00:00, file di annullamento, arresti per PID; serve dal
    03/10;
  - la configurazione del run: `3WAY50_donk_rake25cap2.json`, `out/monker/buckets_15x4`, la mappa TX2 in `TRAIN_ARGS`, le chart
    3-way 50a di MonkerSolver, 8 thread, tabelle double;
  - la proposta della batteria di correttezza 3-way chiesta dall'utente.

  Il run lo lancia la sessione principale.
- **Guardie del 30 ancora armate alle 16:35** [V, elenco dei processi; schemi letti negli script]: `ext/stop_ext_1958.ps1` e
  `night2/stop_night2_1958.ps1`.
  - Alle 19:40 scriveranno file CANCEL vuoti anche nelle cartelle dei loro run, ormai archiviate su F: attraverso le junction.
  - Alle 19:58 fermeranno solo i processi i cui comandi nominano quei run, i driver di correttezza o `HU19_`. I comandi della
    fase 3 non li nominano [I].
  - La guardia del 02/10 della coda del giorno (`day1/stop_day1_0210.ps1`) non è stata armata, perché HU19_B1L è partito il
    01/10.

### 2026-10-01 (mattina) — esiti della coda di correttezza, estensione di V1L e V2L fino a 448.000, decisioni dell'utente, push

Dalle 06:42 alle 08:00 circa; dettagli nella sezione 10 di
[MONKER_RECIPE_REPRODUCTION_2026-09-28.md](MONKER_RECIPE_REPRODUCTION_2026-09-28.md). [V] = verificato, [I] = inferito.

- **06:42:31, fine dell'estensione di V1L e V2L** (partita alle 00:00:38, rc 0 per entrambi; tutti gli snapshot PASS exact)
  [V]. V2L: NashConv postflop **0,00176 a = 0,059 % del piatto a 448.000**, sotto lo 0,10 % del criterio di livello G1 (soglia
  passata fra 256.000 e 320.000; la stima del 30 era circa 420.000). V1L: **0,01658 a = 0,553 %**, pendenza costante circa
  −0,5 (ritmo Monte Carlo [I]); allo stesso ritmo lo 0,10 % chiederebbe circa 14 milioni di iterazioni [I]. Il ribasamento
  delle epoche dello sconto lazy (a 65.535 e multipli) gira qui per la prima volta in un run: nessun gradino a 96.000.
- **07:52:34, fine della coda notturna** e riepilogo automatico (`night2/results.md`) [V]: PASS T5 (D1-D4, V2L_s2, V1L_s2),
  T6 (P2-V0), T4 (V2Z, V2R, V2R5), T2 B0L e B0M; INCONCLUSIVE B2L e B1L, solo sul criterio 3 (fit del pavimento a + b T^-p
  sugli ultimi cinque snapshot: a = 0,014306 contro 0,013120 e 0,025975 contro 0,009389), con trend, componenti, controllo e
  strumento che passano; 0 FAIL in tutta la batteria.
- **Nota di questa voce** [V, calcolo con la stessa procedura di `summarize_night2.py` sui valori non arrotondati]: lo
  stesso fit sugli ultimi cinque snapshot dell'estensione di V2L (192.000-448.000) dà a = +0,00053 a (30 % del livello), quello
  di V1L −0,0012 a. Il criterio del pavimento con una sola potenza reagisce a una pendenza che si addolcisce (V2L da −1,63 a
  −1,29), come in B2L e B1L: da tenere presente leggendo le loro estensioni [I sulla causa: il rumore del campionamento dei
  board pesa di più quando la parte deterministica dell'errore scende].
- **Decisioni dell'utente, verso le 08:00**: estendere B2L e B1L; eseguire HU19_B1 (forme complete di flop e turn dei piatti
  limpati, circa 10,5 GB secondo il piano [I]); terzo parere con la sua DLL (S2-DLL, classifica ed equity) in sola lettura:
  il suo repository non si compila e non si modifica; push del branch `feat/monker-step1-checkdown` su origin, **fatto alle
  08:00**.
- **Regole di oggi**: run fino alle 20:00, build e test fino alle 21:00, run possibili in coda per le 00:00; l'utente può usare
  il PC, quindi al massimo 6 thread nostri in tutto e controllo della memoria fisica libera prima di ogni lavoro pesante.
  Guardie ancora armate: `ext/stop_ext_1958.ps1` e `night2/stop_night2_1958.ps1` (CANCEL alle 19:40, kill alle 19:58 dei
  processi che corrispondono ai loro schemi).
- Documentazione: sezione 10 del documento della ricetta e queste tre voci del diario.

### 2026-10-01 (notte) — coda di correttezza (T2, T4, T5, T6, D6, D5), T1 e T3 eseguiti, tre rafforzamenti di T1

Dalle 00:00 alle 07:52; sezione 10 di [MONKER_RECIPE_REPRODUCTION_2026-09-28.md](MONKER_RECIPE_REPRODUCTION_2026-09-28.md).
Tutti i tempi e i numeri da `night2/queue.log`, `night2/checks.md`, `night2/results.md` e dai rapporti dei workflow [V].

**Coda `night2/queue_night2.sh`** (eseguibili `out/monker/bin_correct/c123`, driver congelato
`out/frozen/run_correctness_v2.sh`, due slot da 2 thread accanto ai 4 thread dell'estensione di V1L e V2L; ogni run lungo
aspetta la memoria libera necessaria più 3 GB, e il suo primo segmento viene controllato: capacità, byte di stato, texture,
righe bloccate, picco, valutazione exact):

- 00:00-00:08: tabella `g1x1` e controlli rapidi D4 (valutatore 2 contro 8 thread), D1, D2, D3 (stato e valori con altri
  thread e partizioni) e P2-V0 (mappa TX2 contro identità): tutti identici bit per bit ai valori registrati.
- 00:08-00:16, D6 e D5 (aggiunti con un hook): su HU50 `bin_rake` e `c123` danno con 200 iterazioni la stessa identità del
  trainer (`1f515aa183549007`, quella del run HU50), lo stesso stato, la stessa policy e 8 chart identiche; i due valutatori la
  stessa NashConv su 20 flop (13,250337907950401 a); partizione 64 con 4 thread contro partizione 4 con 2 thread identiche su
  HU50 e su HU20. Quindi i certificati di `c123` coprono i risultati HU50 prodotti da `bin_rake`.
- Slot A: B2L 00:16-02:52, B1L 02:52-04:49, V2L_s2 04:49-05:56, B0LG1 05:56-06:19, B0MG1 06:19-06:52, V2R5 06:52-07:17.
- Slot B: V2Z 00:19-00:31, V2R 00:31-02:26, R3 02:28-02:38, B0L 02:38-04:06, V1L_s2 04:06-05:19, B2LG1 05:19-05:42, B1LG1
  05:42-06:09, B0M 06:09-07:52.
- Esiti: V2Z identico a V2 bit per bit (PASS); V2R 0,000028 a = 0,00092 % del piatto a 64.000, la policy di V2 nel gioco con il
  rake lascia 0,002850 a (100 volte di più: potenza), rake atteso per eroe = −(EV0 + EV1) (PASS); V2R5 0,0000566 a a 32.000
  (PASS); seed 2 di V2L e V1L: rapporto con il seed 1 fra 0,99 e 1,01 da 4.000 a 64.000, a 64.000 0,03499 contro 0,03488 a e
  0,04376 contro 0,04375 a (PASS); B0L 0,07093 a e B0M 0,05900 a a 64.000 (PASS); B2L 0,13120 a e B1L 0,09389 a
  (INCONCLUSIVE sul solo criterio del pavimento).

**T1 e T3** (workflow sulla build `out/build/windows-release-suite`; C++ 00:02-00:32, Python 00:34-01:17, rapporto
`t1t3/RESULTS.md` alle 01:22): 0 FAIL, 1 INCONCLUSIVE per costruzione (S4 su HU6_all: nessun riepilogo del motore da
confrontare), 0 differenze del motore, 0 correzioni. T1 A1-A5 PASS (errore massimo 2,3e-12); U1-U3 PASS; S1 10/10, S2 8/8,
S3 PASS su 24 configurazioni più HU20_deep e l'albero di produzione HU50, S4 PASS su HU50, S5a 16/16, S6 142/142 e 48/48;
etichetta `preflop_blueprint` 88/88. Limite O1: sui 9 board della prova di T1 ogni mano ha lo stesso esito di showdown su ogni
river, quindi l'A2 forse non vedeva un indice di classe sbagliato.

**Rafforzamento di T1** (decisione dell'utente): tre giri, ognuno con un implementatore e un reviewer indipendente che muta
una copia del test (mai committata); solo `tests/preflop_blueprint_board_texture_tests.cpp`, nessun cambiamento di `libs/`,
`include/` o del supporto dei test, output precedente identico riga per riga dopo ogni giro, commit con percorso esplicito.

- `bf0f63e` (02:55-03:20, review 03:20-03:50): 7 board su cui il river cambia il vincitore (36 coppie di mani su 36 cambiano
  esito fra i river del turn Ad).
  **Scoperta**: l'A2 senza blocco era cieco sulle righe del river (0 insiemi su 14.336 con media non uniforme: il preflop
  allenato non porta al river); le asserzioni di potenza girano ora con un preflop bloccato misto, e tre letture con la chiave
  sbagliata spostano la NashConv di almeno 0,0181 (mille volte la tolleranza). Il reviewer: l'A2 fallisce con ogni mutazione
  sotto il blocco e passa con tutte senza blocco.
- `c50ff3c` (03:45-03:53, review 04:00-04:45): potenza per giocatore (24 letture sbagliate confinate a un giocatore, tutte
  prese), varianti specchiate, non vacuità dell'A1 sul river. **Correzione del reviewer**: senza blocco nessuna cella del
  river cambia dopo la prima iterazione, quindi l'A1 senza blocco controllava solo l'iterazione 1.
- `5bc2a9c` (04:45-05:20, review 05:00-05:30): A1 sotto il blocco del preflop con una relazione esatta derivata dal codice
  (regret del trainer = regret dell'oracolo / fattore di blocco dell'attore, somma = somma / fattore dell'avversario, medie
  uguali), 6.140 celle entro 9,5e-11; 670 / 603 celle del river raggruppate cambiano dopo l'iterazione 1. Il reviewer: quattro
  mutazioni (una cella spostata di 1e-8 relativo, celle ferme all'iterazione 1, VanillaCfr nell'oracolo, blocco spostato di
  1,25e-4) fanno fallire l'A1 in 8 varianti su 8, gli scambi e i rimescolamenti di righe dove cambiano il gioco; la mutazione
  "celle ferme" senza blocco passa (la cecità vecchia, ora chiusa).
- Asserzioni del test da 50.079.891 a 134.975.163, tempo da 36 a circa 100-109 s. Commit non pushati fino al push delle 08:00.

### 2026-09-30 (pomeriggio tardi e sera) — batteria di correttezza del passo 2 HU50: progetto, giochi senza perdita, V2, V1L e V2L, piano di copertura, coda notturna; incidente di processo

Dalle 15:30 circa a mezzanotte; sezione 10 di [MONKER_RECIPE_REPRODUCTION_2026-09-28.md](MONKER_RECIPE_REPRODUCTION_2026-09-28.md).
La domanda: il solver calcola l'equilibrio del gioco che gli diamo? Il metodo: giochi piccoli senza perdita (ogni insieme di
informazione astratto è uno reale), lo stesso percorso di codice di HU50, la migliore risposta fisica esatta su tutti i 573
flop (la NashConv deve andare a 0: un pavimento sarebbe un bug), controlli grossolani che devono fermarsi.

- **Progetto e calibrazione** (fino alle 16:23, `design.md`), dopo la domanda dell'utente "Sul river non servono bucket
  esatti?" (per un certificato sì; nei run HU50 no, e non sarebbero realizzabili). Prove sul gioco a tutte le street (lo
  stesso di `HU6_all`; 15:46-16:21): A 0,027 % del piatto a 32.000, B 0,046 % a 16.000, controllo C fermo allo 0,53 % [V]. Dalle 16:30
  circa la prova profonda HU20_deep (20a, 15 × 4 + TX2 e controllo 3 × 1), finita alle 20:28 e alle 20:34 a 32.000.
- **Codice** (16:55-17:46): chiave `postflop_betting_streets` (`591724c`), `--turn-exact` e `--river-exact` (`dc34131`),
  chiave del river "river-board" (`68cf367`), driver `run_correctness.sh` (`b505ad5`), README (`28d4a78`); dalla review del
  progetto il preflop bloccato limp/check per dare potenza al postflop e la guardia dell'impronta dell'albero (`6bdb86f`,
  `2c0ee74`). Eseguibili congelati `out/monker/bin_correct/c123`; smoke 16:56-17:02.
- **Run** (17:45-19:47, con estensioni di V1L e V2L a 40.000 e 48.000 fino alle 19:29; rapporto alle 19:49) [V]: V2 (preflop
  libero, river esatto) 0,0014 % del piatto a 32.000, PASS; V2L 1,16 % e V1L 1,46 % del piatto a 64.000, PASS su trend e
  controllo (V2LG1 e V1LG1 fermi, 7,9 e 6,6 volte sopra a 16.000), INCONCLUSIVE sulla lettera del livello; i controlli a 3
  livelli non discriminavano.
- **Verso le 20:00, decisione dell'utente** ("Mettili in coda"): estendere V1L e V2L fino a 448.000 dalle 00:00 (coda lanciata
  alle 19:59 con la guardia `ext/stop_ext_1958.ps1`).
- **Piano di copertura** (20:00-21:15): cinque studi sulle lacune (chiave del river "turn", puntate e rilanci, componenti
  condivisi, seed, rake; finiti fra le 20:20 e le 20:31), una critica (20:35-21:00: tre errori di fatto, due argomenti deboli,
  una lacuna nuova di provenienza degli eseguibili, conflitti fra i piani) e il piano (`coverage/plan.md`, 21:00-21:15), con la
  sezione su cosa non si può certificare.
- **Verso le 20:50, decisione dell'utente**: tutto in coda per la notte dalle 00:00 (T2, la parte di T4 con i run, T5, T6), e
  scrivere i test T1 e T3.
- **Codice della sera** (workflow, 21:01-21:55): giochi B, gemelli di V2 con il rake e opzione `SEED` (`6f4a296`), B0M
  (`ba90a30`), dump dell'albero `--dump-nodes` (`fdf8114`), U1-U3 (`0bd2a5e`, `b8e1df0`), T1 (`e459c17`, `1c5932d`,
  `45d1485`, `b738e71`), riferimenti indipendenti in `tools/independent/` (da `207c00f` a `c1db800` e `ccd922e`), arbitro S3
  (`4d3a44c`, `65598b3`) e le correzioni delle review del codice (`8de0299`, `000521c`, `b440103`, `b1fffd8`, `c2bddc5`,
  `9d0072e`).
- **21:42:46**: coda notturna in attesa delle 00:00 e sua guardia armata (`night2/stop_night2_1958.ps1`: CANCEL alle 19:40 e
  kill alle 19:58 del 1° ottobre).
- **Correzioni registrate** (critica e piano) [V]: le prove A e B usavano la mappa identità, non TX2, e in `HU6_all` il flop
  arriva nello 0,3 % circa delle mani, quindi poca potenza sul postflop (non misuravano davvero la chiave "turn" di HU50);
  nella prova profonda il gain_lower postflop è solo 1,35 volte circa sotto il controllo (la differenza 1,692 % contro 3,441 %
  sta nel guadagno preflop e postflop del CO), quindi le puntate postflop erano in pratica scoperte; S7 (scala contro tris in
  MonkerSolver) era già risolto il 30 (5.9 del documento della ricetta) ed è stato tolto; l'affermazione che le regole fossero
  "già coperte" era vera solo in parte (controlli esterni per la classifica e l'albero preflop, nessun controllo indipendente
  di tabella dei ranghi, importi postflop, righe di payoff e cap del rake: ora S1, S2, S3, U1-U3).

**Incidente di processo (verso le 21:20)** [V per i fatti nei file; la causa è quella ricostruita dalla sessione principale].
Mentre i workflow della preparazione della notte erano in esecuzione, la sessione principale ha mandato messaggi (SendMessage)
ad agenti ancora in corso; invece di raggiungerli, i messaggi hanno avviato loro copie parallele, e due agenti hanno scritto
nella stessa cartella `night2` (notato alle 21:22, `night2/COORDINATION.md`). **Nessun danno**: i due si sono coordinati per
iscritto in quel file; il secondo ha aggiunto D6, D5 e B0M solo attraverso file di hook ed `extra_runs.txt`, senza toccare la
coda né gli altri file del primo; un verificatore indipendente ha controllato la coda con un dry run (uguale a parte il testo
del gate di memoria), ha serializzato il gate di memoria dei due slot e ha racchiuso la coda in `main()`, così una modifica
successiva del file non può corrompere la coda in attesa (provato), e ha prescritto di lanciare coda e guardia come due
comandi separati (la riga di comando di un wrapper contiene tutto il testo del comando, e la guardia l'avrebbe uccisa). La
coda ha girato senza errori fino alle 07:52.

**Lezione**: non mandare messaggi agli agenti di un workflow in esecuzione; per cambiarne il lavoro si aspetta che finisca, o
si passa da file che gli agenti leggono quando ci arrivano (hook, file di coordinamento); ogni file ha un solo scrittore, e
prima di lanciare si controlla che nessun altro agente scriva negli stessi file.

### 2026-09-30 (pomeriggio) — 30 × 4 con rake, test 7, studio I/N, texture di MonkerSolver (TXM, TXM2), file .tree, run su F:, specifica della fase 3

Dalle 09:30 alle 14:40 (nessuna riga nel `out/monker/variants/chain.log` fra le 08:43 e le 11:47: macchina ferma fino al
colloquio con l'utente, dalle 11:45 circa); dettagli nelle sezioni 5.11, 6, 8 e 9.6 di
[MONKER_RECIPE_REPRODUCTION_2026-09-28.md](MONKER_RECIPE_REPRODUCTION_2026-09-28.md); specifica in
[threeway/PHASE3_SPEC_2026-09-30.md](threeway/PHASE3_SPEC_2026-09-30.md). Nessun commit di codice.

`HU50_m30x4_rake25` (11:47-12:50, richiesta dell'utente): il gioco più vicino ai default di MonkerSolver, bucket 30 × 4, TX2,
donk bet, rake 2,5 % / cap 2a, soglia 0,005. Arresto a 32.000 (cambiamento 0,0045; trainer fermo a 32.117): distanza da
MonkerSolver 0,0568, differenza di range 0,320 (G1 0,0641 / 0,350, G1+ 0,0622 / 0,338); preferenza suited 0,214 (MonkerSolver
0,224, G1 0,365); radice del CO all-in / open 5a / limp / fold 35,6 / 3,5 / 24,0 / 37,0 % (MonkerSolver 33,1 / 0,5 / 29,0 /
37,4 %). Il miglioramento della distanza su G1 (0,0073 / 0,029) supera le soglie del 5.1 (0,005 / 0,02; su questa misura i
due seed di G1 differiscono di 0,0009 / 0,0026); contro G1+ è al limite (0,0054 / 0,018); le chart si spostano da quelle di
G1 di 0,0514 / 0,283 (soglia fra chart, rumore di due seed: 0,0088 / 0,0425; A continuato a 32.000 0,0163 / 0,0840) e la
preferenza suited di 0,151, più di 10 volte la differenza fra due seed. Valutazione esatta: le chart di MonkerSolver in
questo gioco perdono −0,024 % del piatto al CO (fanno un po' meglio delle nostre contro la nostra strategia) e 0,013 % al
BTN; il nostro scarto dalla migliore risposta preflop 0,092 / 0,032 %; rake atteso 0,239 a. Estensione fino a 64.000 chiesta dall'utente
verso le 13:10, in corso dalle 14:18 (cartella con `NO_ARCHIVE`): 0,0532 / 0,289 a 48.000 (14:38); poi il run TXM2 (in coda).

Test 7 (`HU50_lock_all_m30x4_rake25`, tutto il preflop di MonkerSolver bloccato nello stesso gioco, piano concordato alle
12:26): guadagno della migliore risposta preflop CO / BTN 1,04 / 0,54 % del piatto a 24.000 (12:51-13:38) e 1,03 / 0,49 % a
48.000 (esteso per la parità di I/N con i test a 15 × 4, 13:38-14:18; `br_split.txt` nella cartella, ora su F:). Confronto:
test 5 1,44 / 0,54, 5M 1,60 / 0,60, 6c 0,92 / 0,47. Il 30 × 4 non rende il preflop di MonkerSolver più coerente nemmeno con il
doppio delle iterazioni. Non viene dalla convergenza (al CO il test 7 a 48.000 è uguale a 24.000); raffinare la nostra
astrazione lo alza (15 × 4 < 30 × 4 < 30 × 8 < 60 × 8) e non lo toglie; fra le leve che lo abbassano il rake è la più forte
(−35/−36 %), un bucket più grossolano lo abbassa del 10-11 % (da 30 × 4 a 15 × 4); texture più grossolane come quelle di
MonkerSolver (TXM, TXM2) non sono ancora state provate con il preflop bloccato. Il resto (circa l'1 % al CO, lo 0,5 % al BTN)
sta nelle stesse linee: al CO soprattutto la spinta verso l'open a 5a alla radice (0,017 a su 0,031 a 48.000), al BTN il
check dietro il limp del CO (0,010 a su 0,015). Lettura: probabilmente strutturale, con il limite del test (5.9).

Studio I/N (workflow con due corsie e due verificatori, `scratchpad/monker_research/research_results.md`). La guida di
MonkerSolver indica 10 volte il numero di nodi in iterazioni come il punto in cui una soluzione comincia a essere solida; il
numero di nodi è, per inferenza, quello degli insiemi di informazione (nodi di decisione × bucket per nodo, capacità); sul forum
di solito 20-40. I nostri run hanno due scale (per board e per mano, che differiscono del fattore h = 5,7 al preflop, 12,2 al
flop, 14,6 al turn, 33,8 al river con 15 × 4) e un fattore f ignoto (uno o due posti per iterazione di MonkerSolver). Per mano
(f = 1): G1 8,7, G1 con rake 20,4, test 5 12,9 / 21,6 / 34,5 a 24.000 / 40.000 / 64.000, 5M 6,5, E / G1+ 3,2-4,5, F 1,6-1,9,
`HU50_m30x4_rake25` 8,66 (17,3 con f = 1/2); per board nessun run arriva a 10 nei nodi più raggiunti. Bucket più fini chiedono
più iterazioni in proporzione: i risultati 5M-5F possono essere in parte sotto-convergenti; per il 30 × 4 il test 7 a 48.000 lo
ha controllato (nessun cambiamento al CO).

Texture di MonkerSolver (stesso workflow): HoldemTools non contiene la regola; i conteggi "Large" 8.942 / 3.677 non sono
riprodotti; riprodotti gli spazi delle chiavi delle tabelle di MonkerSolver, 1.755 / 16.432 / 42.783 (short deck 573 / 3.663 /
6.318). Per inferenza le classi di MonkerSolver sono globali per street e dimenticano l'ordine delle carte e il flop (solido
per il river, per il turn poggia su un solo esempio). Stime short deck (scalate dall'Hold'em): turn "Large" circa 1.990 classi,
river "Large" circa 540-760; il nostro TX2 (4.482 classi di turn, river per classe del turn) è più fine per numero di classi
(stima) di circa 2 volte al turn e 6-8 al river; al river la nostra riga ignora la carta del river. Mappe nuove in
`benchmarks/monker/textures` (non committate): TXM (multinsieme dei ranghi × schema dei semi del turn, senza quali ranghi
condividono un seme, 1.899 classi) e TXM2 (più quali ranghi condividono ciascun seme sui board 2+2, 2.151 classi; in
Hold'em 7.566 e 8.996 contro gli 8.942 di MonkerSolver); verificate (ricalcolo indipendente della partizione, altre mappe identiche byte per byte, smoke del
caricatore e del trainer); il river resta per classe del turn (limite del motore).

File .tree di MonkerSolver (due alberi 6-max a 100 bb dell'utente, small e medium): formato decodificato e verificato in modo
indipendente (caratteri UTF-8 come interi; intestazione con 6 giocatori, blind 1.000 / 2.000 mchip, stack 200 fiche; albero
completo esplicito con il postflop; codici 0 fold, 1 check/call, 3 all-in, 40100 piatto). Alberi in stile pot limit (solo la
size del piatto, all-in solo quando un rilancio al piatto non ci sta, niente donk bet, call solo quando chiude l'azione o contro
un all-in, call tolto al flop e al turn quando lascerebbe 19 bb, 0,117 volte il piatto, una sola geometria osservata);
small 3.705 nodi, medium 11.034 (aggiunge il flat del BTN e l'overcall dello SB, le uniche eccezioni alla regola del call). Un .tree di un calcolo short deck mostrerebbe direttamente le size postflop; ignoti i campi a zero dell'intestazione e
i codici delle size diverse dal piatto. Lettore copiato in `tools/monker_compare/monker_tree_file.py` (non committato).

Algoritmo: nelle schermate (impostazioni generiche di MonkerSolver per l'Hold'em, non quelle delle chart) MonkerSolver usa
MCCFR (CSCFR o ESCFR) con strategia media ed EV solo sulla prima street; le impostazioni delle chart non sono note. Il
nostro trainer è CFR vettoriale con campionamento pubblico del caso (32 board per giocatore per iterazione), DCFR 1,5 / 0 / 2, alternato. Opzione A
(Linear CFR; DCFR con un board per batch; Linear simultaneo con un board per batch, sul gioco G1) preparata con il runner
congelato `out/frozen/run_step2_continuous_algo.sh` (`LAZY_ARG`: lo sconto lazy richiede DCFR), smoke passati, messa in coda
alle 12:23 e tenuta alle 12:26 su domanda dell'utente fino al risultato del 30 × 4.

Spazio su disco (decisione dell'utente verso le 12:00, prima dell'inizio dello spostamento alle 12:01:28): l'SSD C: era al 98 % (circa 20 GB liberi). 37 cartelle di run finiti
(299,7 GiB, 4.515 file: `out/monker/variants/*` tranne il run in corso, `out/monker/step2`, `out/monker/smoke_*`, `out/matrix`,
`out/hu40_history7_solve`, `out/suite`, `out/history7_optimized`, `out/hierarchy32`) spostate sul disco USB F: (Seagate Basic da
2 TB, circa 37 MiB/s) in `F:\GTO-Solver-out`, stessi percorsi relativi e una junction al vecchio percorso, ognuna verificata in
file e byte prima di togliere la sorgente; dalle 12:01 alle 14:18; dopo, C: ha 308,5 GiB liberi. Regola da allora: i run si
allenano sull'SSD e vanno su F: dopo la valutazione (`archive_run.ps1`, file `NO_ARCHIVE` per le cartelle ancora in uso, dalle
14:31 un lock esclusivo per cartella); eseguibili, bucket, risorse, runner congelati e build restano su C:; una junction non si
cancella mai in modo ricorsivo. Incidente delle 14:18-14:31: due script in coda hanno archiviato insieme la cartella del test 7;
il primo ha finito e verificato (156 file, 3.091.519.957 byte su F:), il robocopy del secondo copiava la destinazione su sé
stessa attraverso la nuova junction ed è stato bloccato da violazioni di condivisione; ucciso alle 14:31, nessun file perso.

Specifica della fase 3 del 3-way (13:20-13:40, rivista 14:00-14:30 dopo due critiche indipendenti, 19 rilievi accolti; 1.531
righe): percorso a tre posti separato nel trainer (HU identico byte per byte), showdown con una scansione e inclusione-esclusione
sulle carte condivise, carte foldate morte, cache per classi per i terminali preflop, validazione V1-V13 (più V9b), memoria
15 × 4 in double picco circa 14,8 GB (i3) e 30 × 4 circa 29,2 GB (server), 2-4,5 s per iterazione sull'i3 (stima), 26-36 ore di
agente. Decisioni D1-D7 per l'utente: astrazione, rake, regola di arresto (proposta 0,008 sulle 18 chart non all-in), ambito
della valutazione, server a noleggio, build fino alle 24:00, conferme.

Decisioni dell'utente del pomeriggio: regola dello spazio su disco; opzione A solo dopo il run 30 × 4; test 7 e poi la sua
estensione; estensione del 30 × 4 fino a 64.000; texture simili a MonkerSolver (TXM, run TXM2); specifica della fase 3.
Confermato: l'utente non conosce le impostazioni di MonkerSolver (non chiederle).

### 2026-09-30 (mattina) — test del rake 6-6d, B con rake, test 5 con altri bucket (5M-5D), impostazioni di MonkerSolver, fase 2b finita, prime chart 3-way del passo 1

Dalle 03:55 alle 08:50; cronologia in `out/monker/variants/chain.log`, dettagli nelle sezioni 5.10, 9.4 e 9.5 di
[MONKER_RECIPE_REPRODUCTION_2026-09-28.md](MONKER_RECIPE_REPRODUCTION_2026-09-28.md). Aggiorna la voce della notte (sotto):
la fase 2b non è più in corso e i test in coda alle 03:55 hanno i risultati.

Test 6-6d (tutto il preflop dei due giocatori bloccato alle chart di MonkerSolver nei giochi con rake, postflop appreso
per 24.000 iterazioni, valutazione esatta con il rake atteso, guadagno scomposto con `decompose_br.py`): guadagno della
migliore risposta preflop CO / BTN 0,94 / 0,63 % del piatto con rake 5 % / cap 3a (test 6), 0,99 / 0,56 % con 5 % / cap
2a (6b), 0,92 / 0,47 % con 2,5 % / cap 2a (6c), 1,13 / 0,47 % con 5 % / cap 0,75a (6d, aggiunto alle 04:35 con l'utente
via: la verifica sul 3-way a 50a, dello stesso periodo delle chart HU50, dà circa 0,75 a di rake sugli all-in da circa
100 a, mediana 0,745 a su 10 classi, indicativa); senza rake (test 5) 1,44 / 0,54 %. Il rake abbassa il guadagno del CO
(di circa un terzo con 6, 6b e 6c, di un quinto con 6d), nessuna ipotesi chiude lo scarto (il nostro preflop nel suo
gioco: circa 0,1 %); la somma è la più bassa con 2,5 % / cap 2a (1,40 % contro 1,98 %); il guadagno del BTN sale con 6 e
6b. Cambia la deviazione: con il 5 % la migliore risposta vuole più all-in (un piatto all-in paga al massimo il cap, un
piatto più piccolo il 5 % intero), di più con il cap di 0,75a (verso all-in 0,021 a alla radice), e il limp di
MonkerSolver diventa coerente (verso il limp al massimo 0,0015 a; 0,0054 con il 2,5 %); con il 2,5 % torna la spinta
verso l'open a 5a. Il BTN contro il limp vuole sempre più check (con il 5 % altrettanto isolation all-in). Nei nodi di
call contro un all-in solo il cap di 0,75a non lascia deviazioni (negli altri giochi 0,0001-0,0019 a): indizio
coerente con la verifica sul 3-way. Rake
atteso 0,373 / 0,282 / 0,234 / 0,152 a per mano.

Test B con rake (G1 con rake, policy a 37.850 iterazioni, chart a 36.000): NashConv della nostra policy 0,489 a (16,3 %
del piatto; G1 senza rake 0,897 a, 29,9 %, a 16.000 iterazioni). Le chart del CO di MonkerSolver sono sfruttate dal BTN
per 0,120 a in più delle nostre (differenza dei guadagni +4,01 %; senza rake −0,78 %, dove la differenza delle migliori
risposte è −0,74 %), soprattutto al postflop (+0,096 a con la sola migliore risposta postflop), nel piatto limp-check:
la probabilità della linea quasi raddoppia (0,145 -> 0,282; il nostro CO nel gioco con rake limpa il 14,7 %,
MonkerSolver il 29,0 %) mentre il guadagno per visita sale poco (0,70 -> 0,80 a): il CO arriva il doppio delle volte in
un piatto dove il nostro postflop è già sfruttabile, in parte con un range diverso da quello su cui è addestrato
(limite noto del test B). Le chart del BTN: +0,043 a (+1,42 %; senza rake +0,82 %). Controllo con le nostre chart esportate: entro 0,0004 a. La misura pulita resta il test 6.

Test 5M-5D (senza rake, come il test 5, con i bucket 30 × 4 = conteggi di default di MonkerSolver, 30 × 8, 60 × 8 e flop
esatto): guadagno CO / BTN 1,60 / 0,60, 1,67 / 0,64, 1,80 / 0,71, 1,63 / 0,61 %. Ogni raffinatura del nostro postflop
aumenta la sfruttabilità del preflop di MonkerSolver e la spinta verso l'open a 5a (0,021 -> 0,022-0,025 a, la più
grande con il flop esatto): la nostra astrazione postflop non è ciò che ci separa da MonkerSolver. Gli aumenti (somma
+0,22-0,53 punti) stanno ben sopra la variazione del test 5 fra 24.000 e 64.000 iterazioni (circa 0,03 punti), misurata
però solo con i bucket di G1: la convergenza dei run più fini a 24.000 non è verificata. Restano il postflop di
MonkerSolver (altra astrazione, campionamento, convergenza), le size postflop, il rake e il limite del test (postflop
appreso contro range fissi). M è stato aggiunto e messo per primo dopo le schermate dell'utente (coda sostituita alle
03:47).

Schermate delle impostazioni di MonkerSolver mandate dall'utente verso le 03:45 (impostazioni generiche per l'Hold'em,
non quelle delle chart): al flop 210.600 righe per nodo = 1.755 flop "Perfect" × 120 (30 livelli di forza × 4), al turn
1.073.040 = 8.942 classi "Large" × 120, al river 110.310 = 3.677 classi × 30; default quindi 30 × 4 al flop e al turn, 30
al river (il nostro primo passo 2 del 28 usava 30 × 4; G1 usa 15 × 4 con TX2, che fonde i turn più di "Large").
Algoritmo CSCFR o ESCFR (MCCFR), strategia media ed EV tenuti solo sulla prima street. Rake: percentuale, cap in mchip
(il cap in ante dipende dall'unità delle fiches), caselle rake preflop, uncalled bets e raised bets (quest'ultima di
significato non noto). Con 30 × 4 la fase 3 del 3-way raddoppierebbe la memoria (13,56 -> 27,11 GB in double, circa
13,6 in float32).

Fase 2b (workflow dalle 03:29; commit `76ed735`, `41cd7e0`, `e4daf68`, `af9cf15`, `2184d66`, `67de1d7`, gli ultimi alle
05:20): `gtosd_preflop_blueprint_checkdown_classes` (`benchmarks/checkdown_classes.hpp`,
`benchmarks/preflop_blueprint_checkdown_classes.cpp`) risolve il passo 1 per classi (DCFR sulle 81 classi) a 2 o 3 posti
con i terminali dalle tabelle esatte (HU da `preflop_all_in_v1.bin`; a tre da `preflop_three_way_v1.bin`: con tre attivi
per insieme di vincitori, dopo un fold con le carte morte e la trasposta, `--folded-cards ignore` per il confronto),
controlli di identità del rake, migliore risposta esatta per posto, chart attraverso `chart_nodes`/`write_charts`,
`--lock-charts` / `--lock-nodes` (con `--iterations 0` valuta un set di chart), `--expect-summary`. Equivalenza HU con il
programma per combo: chart identiche byte per byte, EV entro 4,6e-13 a; forza bruta dei terminali a tre 1,7e-13 a;
ricalcolo indipendente del revisore uguale in tutte le 12 cifre stampate; tre rilievi della revisione (copertura del test
del blocco, blocco parziale con 0 iterazioni, messaggio della tolleranza) corretti; `ctest -L
"preflop_blueprint|card_abstraction"` 90/90. Circa 17 ms per iterazione con 8 thread sull'i3 libero, 0,49 GB di tensori.

Prime chart 3-way del passo 1 (eseguibili `out/monker/bin_3way_step1` da `67de1d7`; 10.000 iterazioni, 8 thread, circa
2,9 minuti ciascuno; quattro run dalle 07:55 alle 08:07, un quinto alle 08:40 con la macchina libera; cartelle in
`out/monker/step1_3way/`): senza rake, 5 % / cap 3a, 5 % / cap 0,75a, 5 % / cap 3a con le carte foldate ignorate, 2,5 % /
cap 2a. Guadagno massimo di un posto fra 1,7e-5 e 1,6e-4 % del piatto (convergenti); 54 chart su 54 contro il manifest.
Distanza / differenza di range da MonkerSolver 0,288 / 0,736, 0,255 / 0,715, 0,257 / 0,709, 0,245 / 0,699, 0,264 /
0,721 (passo 1 HU 0,262 / 0,729, con rake 0,252 / 0,701). Radice dell'UTG, shove / open 6a / limp / fold: MonkerSolver
17,6 / 0,1 / 35,3 / 47,0 %; noi senza rake 11,4 / 0 / 54,6 / 34,0 %, con 5 % / cap 3a 17,2 / 0 / 41,8 / 41,0 %. Come in HU il
passo 1 non apre mai (niente valore postflop) e limpa troppo; il rake porta lo shove dell'UTG al livello di
MonkerSolver. Le chart 3-way di MonkerSolver bloccate nello stesso gioco lasciano alla migliore risposta di UTG / CO /
BTN 5,46 / 4,71 / 1,40 % senza rake, 4,47 / 4,17 / 1,58 % con 5 % / cap 3a, 3,95 / 3,65 / 1,42 % con 5 % / cap 0,75a,
5,06 / 4,52 / 1,41 % con 2,5 % / cap 2a: con il cap di 0,75a il preflop di MonkerSolver è il meno sfruttabile per UTG, CO
e nella somma (solo indicativo: nel passo 1 non c'è postflop).

Altro: documenti della notte committati (`a319d0c`, 04:34) e di questa mattina (`5c8343f`); viewer 3-way pubblicato alle
09:30 (artifact privato dell'utente "Short Deck 3-way 50a", generatore nella cartella temporanea
`scratchpad/threeway/viewer/build_viewer_3way.py`, non nel repository: i cinque run del passo 1, tabella dei 54 nodi,
griglie 9x9 MonkerSolver / nostre / distanza; 270 distanze per nodo uguali a `compare_charts.py`, 5.670 celle nostre e
10.449 di MonkerSolver uguali alle chart; l'agente che lo preparava si era fermato alle 05:49 in attesa del browser e il
viewer è stato completato a mano); promemoria delle 11:52 tolto su richiesta dell'utente; nuove configurazioni non committate
`HU50_step2_donk_rake5cap075.json`, `3WAY50_donk_rake5cap075.json`, `3WAY50_donk_rake25cap2.json` (oltre a
`HU50_step2_donk_rake5cap2.json` e `HU50_step2_donk_rake25cap2.json` della notte). L'utente ha chiesto di risentirsi verso
le 12:00 del 30 e ha permesso di anticipare i run 3-way con la macchina libera (partiti alle 07:55 invece che alle 11:30).
Decisioni in attesa: rake e configurazione HU di riferimento, criterio di somiglianza in EV, regola di arresto 0,005,
push, correzione del campionamento distorto del calcolatore web, rake e convenzione delle carte foldate del 3-way per il
passo 2, bucket 30 × 4 o 15 × 4 per la fase 3.

### 2026-09-30 (notte) — preflop bloccato alle chart di MonkerSolver (test 1, 2, 5), rake, G1 con rake, 3-way fasi 1, 2a e 2b parte 1

Dalle 20:28 del 29 alle 03:55 del 30; cronologia in `out/monker/variants/chain.log`, dettagli nella sezione 5.9
di [MONKER_RECIPE_REPRODUCTION_2026-09-28.md](MONKER_RECIPE_REPRODUCTION_2026-09-28.md) (3-way nella sezione 9).

Blocco dei nodi preflop (`c7d6ba0`: `--lock-charts DIR --lock-nodes FILE|all`, le righe bloccate giocano la
chart e non ricevono regret né somme delle strategie, impronta del blocco nell'identità del trainer; 47/47 PASS,
revisione senza difetti). Nel runner il cambiamento medio conta le chart bloccate come 0: il cambiamento dei
nodi liberi è quello misurato × 8/7 con un nodo bloccato e × 2 con quattro, quindi la soglia va moltiplicata per
7/8 o per 1/2 (test 2: 0,0025); il test 1 è partito con 0,005 non scalata. Test 1 (radice
del CO bloccata, anticipato alle 20:28 su richiesta dell'utente, fermo a 28.000) e test 2 (i quattro nodi del CO,
fermo a 28.000): entrambi falliscono il criterio del 29 sera (BTN contro il limp 0,118 / 0,123, contro l'open
0,117 / 0,226, limite 0,026). Il criterio era sbagliato in principio: con i nodi di un giocatore fermi il CFR
converge a una migliore risposta alle chart bloccate, non a una strategia bilanciata, e i nodi del BTN non
dovrebbero coincidere con quelli di MonkerSolver nemmeno se le sue chart fossero un equilibrio. Il guadagno di
una migliore risposta preflop del CO contro quel BTN (2,71 % e 9,46 % del piatto; 1,44 % nel test 5, 0,14 % in
G1) viene soprattutto dall'open a 5a (0,052 a su 0,081 e 0,219 su 0,284): la radice bloccata raggiunge quella
linea lo 0,5 % delle volte e il BTN vi risponde a un range ristretto, quindi sfruttabile. Errore mio,
riconosciuto con l'utente; corretto nel documento (5.8).

Test 5 (tutto il preflop dei due giocatori bloccato alle chart di MonkerSolver, il postflop impara; 24.000
iterazioni, poi esteso a 64.000 con la policy a 40.000): la migliore risposta preflop guadagna 1,44 / 1,43 / 1,45 %
del piatto al CO e 0,54 / 0,51 / 0,49 % al BTN a 24.000 / 40.000 / 64.000; al CO quasi tutto alla radice (verso
l'open a 5a circa 0,021 a, il limp 0,012, l'all-in 0,005), al BTN contro il limp verso il check (circa 0,010 a).
Stabile con più training del postflop, quindi non è un artefatto delle iterazioni: il preflop di MonkerSolver, con
il postflop che il nostro CFR impara contro i suoi range, non è un equilibrio del nostro gioco (a bucket, senza
rake). Non prova che nessun postflop lo renda un equilibrio: con il preflop bloccato il postflop non ha motivo di
scoraggiare le deviazioni preflop, e metà del guadagno del CO (0,0216 a su 0,0436 a con 64.000 iterazioni) sta
nell'open a 5a, linea che le chart raggiungono lo 0,5 % delle volte (3,1 combo), dove il postflop del BTN risponde
a quel range ristretto; le parti sulle linee frequenti (limp del CO, check del BTN) sono meno esposte.
L'equivalenza in EV del 28-29 (chart di MonkerSolver contro la nostra strategia) resta vera ma è locale: precisato
nel documento (5.4, 5.7, 5.8, 8). Test 3 e 4 (solo il BTN bloccato contro l'open o contro il limp) sospesi dopo il
test 5.

Incidente delle 00:00: cinque script in coda che TaskStop non aveva fermato (`run_lock34.sh` ×2, `run_lock5.sh`
×2, `run_lock.sh`) hanno lanciato due volte il test 3, non richiesto, e un secondo test 5 accanto
all'estensione; uccisi per PID alle 00:06, `HU50_lock_btn_open` cancellato, valutazione a 24.000 del test 5
intatta. Regola da allora: gli script in coda si fermano per PID e si verifica; controllano un file di
annullamento.

Rake (`3ec4027`): l'utente alle 23:13 del 29, dopo il test 5, dice che è "molto probabile" che le chart siano state
calcolate con rake 5 %, cap 3 ante, no flop no drop; il 30 alle 01:40 precisa 5 % sul postflop (sul preflop solo
per gli all-in), fino a 3a; alle 02:21 "abbastanza sicuro, non certo". Chiavi `rake_mode` `"enabled"`,
`rake_basis_points`, `rake_cap_units` (1 ante = 10.000 unità), `rake_no_flop_no_drop`, `rake_minimum_pot_units`,
scritte solo con il rake attivo; nel core il flop conta come distribuito anche per un all-in preflop con runout;
foglie del checkdown con rake; gioco non a somma zero (`monker_values --expected-rake`). Fixture `HU50_rake.json`,
`HU50_step2_donk_rake.json`, `preflop_blueprint_hu10_reduced_rake_v1.json`; 54/54 PASS, senza rake output identici
byte per byte, revisione senza difetti. Passo 1 con rake: 5.000 iterazioni, guadagno massimo 9,3e-5 % del piatto,
rake atteso 0,328 a, radice del CO all-in / open / limp / fold 30,7 / 0,0 / 55,0 / 14,3 % (senza rake 29,0 / 0,0 /
62,6 / 8,3; MonkerSolver 33,1 / 0,5 / 29,0 / 37,4).

G1 con rake (soglia 0,005): fermo per regola a 32.000 iterazioni (cambiamento 0,0049), distanza da MonkerSolver
0,0849 e differenza di range 0,490 (G1 senza rake 0,0641 / 0,350), ancora in discesa; alla radice del CO l'open
scende (8,0 -> 3,8 % da 20.000 a 32.000) e il limp sale rallentando (9,6 -> 14,5 %, contro il 29,0 % di
MonkerSolver). Su richiesta dell'utente (02:46) ripreso dopo l'arresto e poi fermato (richiesta alle 03:13, fermo
alle 03:15) a 37.850 iterazioni (chart a 36.000: open 3,5 %, limp 14,7 %), non rivalutato. Valutazione esatta
all'arresto: le chart di MonkerSolver nel gioco con rake perdono lo 0,028 % del piatto al CO e lo 0,027 % al BTN;
il nostro scarto dalla migliore risposta preflop è 0,089 % / 0,025 %; rake atteso 0,338 a per mano. Preferenza
suited 0,148 (MonkerSolver 0,224).

Convenzioni di MonkerSolver dagli EV del set 3-way a 60a (workflow con verificatore indipendente): negli showdown
a due dopo un fold le carte di chi ha foldato sono morte (RMS 0,092 a con un rake piatto di 2 a su 241 classi,
contro 0,46 con le carte ignorate); il rake che spiega gli all-in è piatto, circa 2 a (5 % / cap 3a e nessun rake
respinti); scala sopra tris confermata (tris sopra scala: RMS almeno 1,84 a). Limiti: il 60a è di agosto 2026, il
3-way 50a di settembre 2025; sul 50a
una stima indiretta dà circa 0,7 a di rake (indicativa). Le cartelle 3-way "40a" e "60a" sono identiche (calcolo
a 60a).

Cosa vuol dire "corretto" (con l'utente): la nostra strategia nel nostro gioco si misura con la migliore
risposta esatta (solo preflop sotto 0,03 a; completa a carte vere circa 0,78 a, 26 % del piatto, A a 32.000,
dominata dal postflop: soprattutto l'astrazione delle carte, con un resto di convergenza; l'astrazione delle
azioni non entra, stesso albero); se il nostro gioco è quello giusto lo dicono i test con il preflop
bloccato e le raffinature dell'astrazione, non la distanza dalle chart; manca la migliore risposta preflop
contro un postflop risolto esattamente per flop (costo da stimare).

3-way 50a (sezione 9 del documento; specifiche in [threeway/](threeway/)): fasi 1 e 2 avviate subito su
decisione dell'utente, in due rami di lavoro. Fase 1 (`518bbfa`, `6cb4a70`, `670f8fc`, `9c8f845`, `32ecef7`): primo
raise al 100 % del piatto (`preflop_open_sizes_basis_points`), poi solo all-in, limp e cold call ammessi; i nomi
dei file omettono il primo fold di un giocatore; `gtosd_preflop_blueprint_monker_tree` ricostruisce 54 file su 54
(0 mancanti, 0 in più, 0 azioni diverse) e anche il 3-way 100a; albero 7.225 nodi, 847.242.189 celle, 13,56 GB in
double. Fase 2a (`44a8a5a`, `93d805d`, `80e7afc`, `b7d42a1`, `fcf1ed9`): tabella esatta delle terne di classi
(81³ voci da 40 byte, 21,3 MB), carte foldate morte per default; costruzione completa 02:27-02:40 (108 s),
identità, invarianza e forza bruta su 20 terne senza errori, impronta `fnv1a64:31f1bb691ff8a4e8`; controllo con la
DLL del calcolatore PASS sulla tabella parziale e anche sulla tabella completa (02:40-02:42, 300 terne di combo e
5 di classi entro 1e-12, 358 chiamate alla DLL). Integrazione (merge `6c17b5c`, `ad51088`; seguiti `484208c`,
`ea1ef59`, `06dd436`): 65/65 test `preflop_blueprint`, output HU identici byte per byte, revisione senza difetti;
`ctest -R three_way_table` rieseguito alle 03:32 sull'ultimo binario, 3/3 PASS.

Fase 2b (trainer del passo 1 a tre giocatori, workflow dalle 03:29). Parte 1 committata alle 03:49: `76ed735`
(`gtosd_preflop_blueprint_checkdown_classes`, DCFR per classi a 2 o 3 posti con EV e guadagno per posto;
terminali a tre ancora rifiutati) e `41cd7e0` (test). Controllo di equivalenza HU superato: HU50 e HU50_rake a
100 e 2.000 iterazioni, chart identiche byte per byte al checkdown per combo (0 celle diverse su 1.701), EV entro
5e-13 a, rake atteso 0,328497 a; 0,4-0,9 ms per iterazione contro circa 30 ms; `ctest -R checkdown` 12/12 PASS.
In corso dalle 03:51 la parte 2: terminali a tre dalla tabella (115 tensori per posto, 0,49 GB in double), chart
a tre posizioni, valutazione delle chart 3-way di MonkerSolver.

Alle 03:55: test 6 (rake 5 % / cap 3a, training finito alle 03:48, valutazione in corso), poi 6b (5 % / cap 2a),
6c (2,5 % / cap 2a), B con rake (policy a 37.850 iterazioni), test 5 con i bucket M (30 × 4, i conteggi di
default di MonkerSolver dalle schermate delle impostazioni mandate dall'utente), E, F e D: il preflop di
MonkerSolver diventa meno sfruttabile con un rake o con un postflop più fine? Decisioni dell'utente della notte:
ordine dei test con il preflop bloccato (1, poi 2 solo se 1 fallisce), test 5 con priorità ed esteso a 64.000,
test 3 e 4 sospesi; rake codificato; test 6, 6b e 6c approvati; fasi 1 e 2 del 3-way subito, impostazioni della
fase 3 dopo l'HU; convenzione delle carte foldate da verificare sugli EV (fatto: morte); G1 con rake continuato e
poi fermato a 36.000; B ripetuto con il rake; test 5 con bucket diversi. Aperti: regola di arresto 0,005 per il
3-way, configurazione HU di riferimento e rake. L'utente non conosce le impostazioni di MonkerSolver usate per le
chart e ha chiesto di non domandarle più.

### 2026-09-29 (dalle 10 alle 18:38) — G4 a due size, bucket più fini (E, F), seed e curva di convergenza (A), migliore risposta contro le chart (B), flop esatto (D)

G4 (G1 + bet e raise al 50 % e al 100 %, 8.599 nodi, tabelle in double) lanciato alle 10:02, stabile a
20.000 iterazioni alle 13:29: picco 15,67 GB (la stima dal layout, 15,6 GB, era giusta), 0,617 s per
iterazione, valutazione esatta 5.067 s. Distanza da MonkerSolver 0,0663, differenza di range 0,373,
preferenza suited 0,432 (G1: 0,0641 / 0,350 / 0,365): la seconda size sposta le chart quanto i donk bet
(0,0219 / 0,1285 da G1; G0c 0,0212 / 0,1342) ma lontano da MonkerSolver; in EV resta equivalente
(MonkerSolver perde lo 0,032 % / 0,022 % del piatto). G1 resta il riferimento (criterio della notte: il gioco
più vicino a MonkerSolver a parità di EV).

Poi una modifica alla volta su G1, con gli eseguibili `out/monker/bin_allin` (E, F) e `out/monker/bin_abd`
(A, B, D). E (bucket 30 × 8) e F (60 × 8) stabili a 20.000 iterazioni, distanza 0,0625 per entrambi, range
0,339 / 0,3395, preferenza suited 0,394 / 0,395, picco 3,82 e 7,53 GB; F coincide con E (0,0043 / 0,0245):
il numero di bucket non è la leva. A (G1 con seed 2) stabile a 16.000 (0,0650 / 0,352): il rumore del seed su
G1 è 0,0088 / 0,0425 a 16.000 iterazioni (passo 2: 0,007 / 0,034 a 24.000; la differenza può venire dalle
iterazioni); le varianti fermate a 20.000 contengono anche 4.000 iterazioni in più (G1 contro A a 20.000:
0,0099 / 0,0497, 1,1 / 1,2 volte il rumore); rispetto a G1 le varianti di astrazione (D, E, F) spostano le
chart di 1,2-1,6 volte il rumore, i cambi dell'albero (G0c senza donk, G4) di 2,4-3,2 volte. D (flop esatto, un id per orbita dei semi, capacità 528) stabile a 20.000 (0,0621 / 0,342),
stato 1,41 GB (+30 % su G1), chart vicine a quelle di E (0,0072 / 0,0443). In EV tutte equivalenti:
MonkerSolver nel gioco di ciascun run perde al massimo lo 0,048 % del piatto.

Curva di convergenza esatta di A (policy a ogni salvataggio e `convergence_curve.py`), poi A ripreso fino a
32.000 iterazioni: NashConv a carte vere 1,903 a (4.000), 0,899 a (16.000), 0,777 a (32.000); guadagno solo
preflop del CO 0,0120 -> 0,0048 -> 0,0028 a (il preflop converge), solo postflop 0,460 -> 0,262 -> 0,236 a
(scende ancora). Le chart si muovono ancora (A a 32.000 contro A a 16.000: 0,0155 / 0,0838, circa 2 volte il
rumore), lentamente verso MonkerSolver (distanza 0,0650 -> 0,0631). La soglia 0,01 basta per il preflop ma
non per la posizione finale delle chart: proposta per il 3-way una soglia di 0,005 o il doppio delle
iterazioni. Corretta nel documento (5.3) la stima del 28 che prevedeva una distanza in salita con più
iterazioni; corrette anche la nota sugli 80 GB della seconda size e la decisione "niente donk bet" della
sezione 1.

B (migliore risposta esatta contro il preflop delle chart, su G1): con il preflop del CO di MonkerSolver (e
il nostro postflop) la migliore risposta del BTN vale 0,0221 a in meno che contro il nostro CO (sfruttabilità
−0,74 % del piatto), con il preflop del BTN di MonkerSolver quella del CO 0,0250 a in più (+0,83 %): poco
rispetto alla NashConv (29,9 %), ma circa 20 e 55 volte la perdita delle chart nel nostro gioco; il controllo
con le nostre chart esportate resta entro 0,0003 a. Il CO di MonkerSolver non apre quasi mai a 5a; il nostro
apre il 7,0-7,7 % in G1 e in tutte le varianti di astrazione del giorno (MonkerSolver 0,5 %; AA:
MonkerSolver limpa il 93 %, noi apriamo il 66-76 %), il 9,4 % con due size; l'open scende con le iterazioni
(A: 7,7 % a 16.000, 6,7 % a 32.000). Ipotesi: le chart dell'utente vengono da un albero postflop diverso nei
piatti rilanciati e/o portano l'errore dell'astrazione di MonkerSolver, e da parte nostra pesa la
convergenza incompleta; proposti test dell'albero nei soli piatti rilanciati o le impostazioni dell'albero
MonkerSolver dell'utente (chieste).

Decisioni dell'utente: configurazione HU di riferimento G1 (donk bet sì, all-in postflop fino a 5 volte il
piatto no, che revoca la decisione della notte), B solo su G1, test C (postflop ricalcolato con il preflop di MonkerSolver fermo) non si fa, A
fino a 32.000 iterazioni, fase 1 del 3-way prima fissata per le 00:00 del 30 settembre e poi rinviata alle
20:00 (si parte quando il problema della radice è capito meglio, probabilmente dopo le 3 del 30). Commit `5578ab8` (policy a ogni salvataggio, bucket per street e flop esatto, `--exploit` di
`gtosd_preflop_blueprint_monker_values`): 44/44 test `preflop_blueprint` e test di base PASS, 4 rilievi
della revisione corretti prima del commit. Incidenti: il runner di G4 è morto con un errore di sintassi dopo
l'arresto del trainer, perché un agente ha modificato lo script mentre bash lo leggeva (training e
valutazione intatti); una copia congelata del runner fuori dal repository è fallita alla partenza (la radice
è calcolata dal percorso dello script): le copie congelate stanno ora in `out/frozen/`. Dettagli in
[MONKER_RECIPE_REPRODUCTION_2026-09-28.md](MONKER_RECIPE_REPRODUCTION_2026-09-28.md), sezione 5.7.

Sera: G1+ (E ripreso fino alla soglia 0,005, fermo a 28.000) arriva a 0,0622 / 0,338, perdita di
MonkerSolver 0,00225 / 0,00022 a. Scomposizione dello scarto su G1: circa il 70 % sta nel piatto limpato
(CO contro l'isolation 28,6 % della distanza, BTN contro il limp 21 %; range di limp del CO 39,5 % e range di
isolation del BTN 31,5 % della differenza di range), circa il 30 % nell'open a 5a. L'utente chiede il test
della radice bloccata: codice la sera (anche dopo le 21), test in partenza automatica alle 00:00 del 30, secondo
test con tutto il CO preflop bloccato solo se il primo non converge (sezione 5.8).

### 2026-09-29 (notte) — all-in fino a 5 volte il piatto, giochi G1-G3 sull'astrazione compatta

Decisioni dell'utente: all-in postflop solo fino a 5 volte il piatto; tabelle in double; G4 = il gioco più
vicino a MonkerSolver sulla distanza, a parità di EV, più una seconda size (50 % + 100 %). Verifica della
memoria per due size (l'utente ha chiesto di ricontrollare la mia stima di 80 GB): calcolo esatto dal
layout, che riproduce tutti i picchi misurati; con l'astrazione del passo 2 servono 70,7 GB (non 80), con
quella compatta 11,7 GB (15,6 con i donk bet); una prova in float32 ha misurato 6,29 GB e 0,44 s per
iterazione. Opzione `postflop_all_in_max_pot_basis_points` (commit `410a380`, workflow con due
revisori): spariscono 6 all-in delle linee limp-check del piatto da 4a.

Risultati (documento, sezione 5.6): G1 compatta + donk 0,0641 / 0,350 (il migliore finora), G2 compatta +
all-in fino a 5× 0,0750 / 0,383, G3 con entrambi 0,0658 / 0,360, contro G0c 0,0706 / 0,3625; in EV
MonkerSolver nel gioco di ciascuno perde al massimo lo 0,068 % del piatto. G4 = G1 + due size (15,6 GB in
double): in attesa che l'utente chiuda llama-server, perché con 12,5 GiB liberi il run andrebbe nel file
di paging.

### 2026-09-28 (sera e notte) — passo 2 stabile, varianti, MonkerSolver nel nostro gioco, turn in texture

Passo 2 su HU50 stabile a 24.000 iterazioni (17:45): distanza da MonkerSolver 0,0725 (passo 1: 0,262),
differenza di range sulle combo effettive 0,383 (passo 1: 0,729). Dettagli per nodo in
[MONKER_RECIPE_REPRODUCTION_2026-09-28.md](MONKER_RECIPE_REPRODUCTION_2026-09-28.md), sezione 5.

Decisioni e correzioni dell'utente: la "stessa azione principale" è fuorviante e non si usa più; si
riportano la distanza media e la differenza di range; le combinazioni si contano come combo effettive
(combinazioni × frequenza con cui la mano arriva al nodo), non come medie. Una prima versione della
differenza di range era una media per nodo che contava due volte il range del CO dopo il limp (0,456):
corretta in un calcolo sulle combo effettive dei range distinti (0,383), nel viewer e in
`compare_charts.py` (commit `c9260ba`). Errore mio del pomeriggio trovato in serata: il campo aggiunto a
`ActionConfig` in mezzo alla struttura rompeva l'inizializzazione per posizione di `tests/core_tests.cpp`
(la build completa non era stata fatta); spostato in fondo, build completa e test di base PASS (`e9b5c93`).

Modalità continua (`7a34a0a`): le chart ogni 4.000 iterazioni lette dal trainer vivo (lettura delle righe
medie senza toccare lo stato, test con 23.216 righe con sconti in sospeso, differenza 3e-16), file di
stop, ripresa dal checkpoint; blocchi da 4,1-5,8 minuti invece di 5,4-7.

Varianti, una alla volta (tutte stabili a 24.000 iterazioni): seed 2 (rumore: chart distanti 0,007 /
0,034 dal passo 2), 15 livelli di forza (neutra, memoria dimezzata), potenziale a 1 livello (sposta le
chart di 0,018, non verso MonkerSolver), donk bet ammessi (distanza 0,0667, unica che avvicina oltre il
rumore, nei nodi del piatto limp-isolation-call); la size al 75 % è stata tolta dall'utente prima del run
(albero da 1.036 nodi, 11,7 GB). Un'analisi con tre analisti indipendenti e un arbitro ha localizzato lo
scarto soprattutto nelle suited e nelle coppie del CO (le offsuit coincidono dopo il limp, non del tutto
alla radice) e aveva previsto che i donk bet
allontanassero da MonkerSolver: il run ha detto il contrario.

MonkerSolver nel nostro gioco (`364fd5e`, workflow con due revisori): il valutatore della best response
legge le righe per classe di board; nuovo strumento `gtosd_preflop_blueprint_monker_values` e script
`monker_in_our_game.py`. Valutazione esatta su 573 flop (4 minuti): le chart di MonkerSolver giocate al
posto delle nostre perdono 0,00022 a (CO) e 0,00090 a (BTN), lo 0,007 % e lo 0,030 % del piatto, meno
del nostro stesso scarto dalla migliore risposta preflop (0,0058 e 0,0038 a); nel gioco con i donk bet
−0,00045 e 0,00063 a. Nel nostro gioco le chart di MonkerSolver sono quasi una migliore risposta quanto le nostre: le
differenze stanno fra azioni quasi
indifferenti.

Turn in classi di texture (`6de08ae`, progetto e implementazione con workflow, due revisori): mappa dei
13.761 flop+turn in texture, opzione `--board-texture-map`, identità bit per bit con la mappa identità,
regola TX2 a 4.482 classi (memoria 1/3). Run HU50 (sezione 5.5 del documento): TX2 stabile a 20.000
iterazioni, 1,96 GB, distanza da MonkerSolver 0,0710; sposta le chart del passo 2 di 0,020 / 0,115, oltre
la soglia del piano (gli asintoti stimati confermano uno spostamento reale, leggermente verso
MonkerSolver). La regola di riserva TX1 (6.768 classi, 2,86 GB) dà le stesse chart del TX2 (0,007 /
0,047): lo spostamento viene in gran parte dalla fusione in sé (il TX1 si sposta un po' meno). La combinazione 15 livelli + TX2 è neutra rispetto al
TX2 e scende a 1,03 GB (5,4 volte meno del passo 2), distanza 0,0706. In EV tutte le chart (varianti,
texture, combinazione) giocate nel gioco del passo 2 perdono meno di 0,001 a (le texture un po' meno di
zero: probabilmente perché più convergenti), e MonkerSolver nel gioco compatto perde lo 0,055 % del piatto. Decisione
sull'astrazione compatta e sui donk bet lasciata all'utente.

### 2026-09-28 (pomeriggio) — obiettivo multiway, ricetta MonkerSolver: passo 1 fatto, passo 2 in corso

Decisioni dell'utente: l'obiettivo del prodotto è un preflop short deck multiway fino al 6-way, su un
server da 52 core e 256 GB (test su macchine a noleggio); i limiti di 8 GiB e 35 minuti valgono solo per
la suite HU10-HU40; accettazione sulla sola best response astratta esatta <= 0,03 a (limite del 5 % sul
fisico tolto, commit `a0331d6`); direzione: riprodurre la ricetta di MonkerSolver e confrontarla con le
chart MonkerSolver short deck dell'utente (HU e 3-way a 50a), cercando chart simili; niente stile HRC e
niente history7 per ora; albero identico a quello delle chart, postflop con bet e raise al 100 %,
all-in sempre disponibile, niente donk bet. Le fasi 1 e 2 sui 35 minuti di HU40 sono in pausa (la best
response astratta veloce resta progettata, non implementata).

Ricerche (agenti Opus con verifica avversaria): panorama dei solver preflop e costo di una riscrittura
(3-4 settimane, nessun guadagno dimostrato), definizione dei bucket di MonkerSolver, HRC e Simple
Preflop Holdem (i "15-30" di Monker sono livelli di forza per board, con strategie separate per flop:
più righe dei nostri bucket globali), server economici a noleggio, repository dell'equity dell'utente
(stessa classifica, utile solo come controllo). Misure delle dimensioni multiway da 3 a 6 giocatori con
diverse astrazioni. Tutto nel documento
[MONKER_RECIPE_REPRODUCTION_2026-09-28.md](MONKER_RECIPE_REPRODUCTION_2026-09-28.md).

Passo 1 (preflop con postflop vuoto), commit `e576396`: all-in sempre disponibile nel modello di gioco
(alberi HU10-HU40 invariati), opzione di compilazione che chiude il flop con uno showdown sull'intero
runout, solver esatto `gtosd_preflop_blueprint_checkdown` (HU50: 5.000 iterazioni in 75 s, 3e-5 % del
piatto), export nel formato MonkerSolver e script di confronto. Revisione Opus senza errori di calcolo;
sei suite di test PASS. Confronto con le chart MonkerSolver HU50: 72,2 % di azione principale uguale,
distanza media 0,262 (media sui 7 nodi confrontabili); 87,5-98 % sulle decisioni contro un all-in
(stesse regole e stessa equity), 44-69 % dove conta il postflop. Viewer web con le tre griglie per nodo pubblicato come artifact dell'utente.

Passo 2 (postflop sparso con bucket per board), commit `7ff711b`: opzione "niente donk bet" nel
costruttore (agente Opus, revisione, test), costruttore dei bucket per board 30 x 4 / 30 (agente Opus,
revisione; tabelle costruite in 13 s), righe (classe di board, gruppo) nel trainer, export delle chart
dalla policy, run a blocchi da 4.000 iterazioni con arresto sotto 0,01. Sette suite di test PASS.
Albero HU50 senza donk: 493 nodi, 325 milioni di celle, 4,84 GiB in double. Costo 0,067 s per
iterazione nella prova breve e 0,070-0,079 s nel run, con lo sconto lazy (0,75 s senza: errore del primo
script corretto nella prova breve). Run lanciato alle 17:10
in `out/monker/step2/HU50`.

Prossimo passo: risultati del passo 2 e confronto con MonkerSolver; varianti se le chart non si
avvicinano; poi il 3-way.

### 2026-09-28 — nuovo criterio di accettazione, diagnosi per street, fase 0: HU30 passa la best response astratta a 48.000 iterazioni

Fatto nella notte (eccezione dell'utente solo per il 28: nessuno stop alle 21:00, la finestra
normale torna il 29 alle 00:05 con lo switch automatico): diagnosi per street con la candidata N
sulle policy a 32.000 iterazioni (history7 e L, sezione 5.4.12 di
[MEMORY_TIME_OPTIMIZATION_2026-09-21.md](MEMORY_TIME_OPTIMIZATION_2026-09-21.md)): nessuna street
dominante (quote postflop 33 / 29 / 38 % su HU30, 29 / 29 / 42 % su HU40), limp-check 79-84 % del guadagno dal flop,
guadagno amplificato dal preflop (la risposta del CO arriva al limp-check con probabilita' 0,51-0,57 contro
0,12-0,20 della policy); L riduce tutte le street del 4-5 %. Workflow sul postflop esatto per flop con
`libs/postflop`: fattibile ma 19-29 ore per passata su HU30/HU40 e 56-88 su HU100, scartato come
strada principale e tenuto come oracolo. Workflow sui bucket: le feature di flop e turn sono gia'
istogrammi di equity confrontati con EMD, il river usa 9 valori; il 95,3 % dei genitori river e' al
tetto di 7 righe. HU100 definito dall'utente (open 150 %, isolation 150 %, 3-bet 100 %,
limp/raise 100 %, stack 100 a, all-in sempre disponibile): bozza, stime di memoria (12-23 GiB) e
buco dell'all-in nel motore oltre il 1000 % del piatto (sezione 5.4.13).

Decisioni dell'utente: nella notte (commit delle 02:03) l'accettazione passa alla best response esatta dentro
l'astrazione (<= 0,03 a, 1 % del piatto); poi (commit delle 02:38) il certificato fisico resta un limite
superiore di 0,15 a (5 %), non piu' un obiettivo. Protocollo aggiornato (`0980dec`, `ae13445`).
Alle 03:00 roadmap in fasi: 0 curve di convergenza, 1 arresto automatico e best response astratta
veloce, 2 convergenza per iterazione (algoritmo), 3 rappresentazione per il fisico <= 0,15, 4 tempo
di 35 minuti su tutti i benchmark e HU100; scelta fra fase 1 e 2 con regola pre-registrata sul
valore di HU40 a 64.000 iterazioni, e domanda all'utente se il valore cade nella fascia incerta.

Fase 0 (coda continua 03:03-09:40, sezione 5.4.14): HU40 a 64.000 iterazioni best response
astratta 0,0417 a (32.000: 0,0640), fisico 0,2383; HU30 a 48.000 iterazioni **0,0269 a, sotto la
soglia**, fisico 0,1609 (7 % oltre il limite di 0,15 a: non ancora accettato). La best response astratta
scende come T^-0,62/-0,64: HU30 passerebbe a circa 40.500 iterazioni (circa 88 minuti di training),
HU40 a circa 109.000 (circa 4,1 ore). La differenza fisico - astratta resta ferma (HU30 0,133 ->
0,134) o cresce (HU40 0,189 -> 0,197): HU40 non arriva a 0,15 con le sole iterazioni. Taratura
della best response campionata (16/32/64 flop, due seed): sovrastima di 2-7 volte, con un fattore
che dipende da benchmark e seed, quindi non utilizzabile ne' come stima ne' come filtro. HU40 cade
nella fascia incerta (0,040-0,048): decisione chiesta all'utente alle 06:24, raccomandata la fase 2.

Prossimo passo: scelta dell'utente fra fase 1 e fase 2. In entrambe il primo lavoro e' la best
response astratta esatta veloce (tecniche di J piu' simmetria di seme su turn e river, obiettivo
circa 10 minuti, bit-identica ai quattro valori di riferimento): con il nuovo criterio i 35 minuti
comprendono anche questa misura, che oggi da sola dura 47 minuti.

### 2026-09-26/27 — H su tre ripetizioni, candidata J (certificatore 2,6 volte piu' veloce), astrazioni K e L, riferimenti a 32.000 iterazioni, candidata N

26 settembre: ripetizione 4 appaiata di D, E, F (sezione 5.4.8); H su tre ripetizioni,
bit-identica a F, picchi 2,01 / 7,56 / 7,78 / 7,78 GiB contro 2,11 / 7,94 / 8,18 / 8,18 di A
(sezione 5.4.7); censimento delle astrazioni della fase 3 e prima candidata K (sezione 5.4.9);
build di J alle 20:06, sonda HU10 bit-identica e 2,6 volte piu' veloce (sezione 5.4.10); primi
riferimenti a 32.000 iterazioni: HU30 0,1676 a, HU40 0,2526 a (fisico). Decisioni dell'utente:
finestra estesa alle 21:00 e obiettivo di 35 minuti per ogni benchmark.

27 settembre (sezione 5.4.11): J sui quattro benchmark, certificati bit-identici a H e
certificazione 2,3-2,9 volte piu' veloce (HU30 325 s contro 856); K (river cap 16) nessun guadagno a
32.000 iterazioni; L (flop 500, river cap 12) -5 % a 32.000 iterazioni, peggiore a 16.000; HU20 con
meno iterazioni: 8.000 0,0438 a in 23,8 minuti, 12.000 0,0335 in 29,0, batch 64 a 8.000 0,0303 in
38,3. Decisioni dell'utente: priorita' a HU30 e HU40, il tempo di HU20 e' sospeso (coda parcheggiata
in `out/suite/queue_parked_hu20.txt`); build e test ammessi 00:00-21:00 tranne durante i run di
tempo; mai riordinare la roadmap concordata senza dirlo (errore mio corretto in giornata sulle
ripetizioni di J). Run a 32.000 iterazioni con policy conservata (`diag-h-32k`, `diag-l-32k`):
best response astratta HU40 0,0640 a (la misura di HU30, 0,0349 a, e' delle 02:10 del 28). Candidata N (best response ristretta per
street, commit `943c7b4`) costruita e testata la sera del 27 per la diagnosi della notte.

### 2026-09-25 — fase 1 conclusa (D, E, F su tre ripetizioni), finestra e decisioni dell'utente, fase 2 avviata (candidata H)

Fatto: coda della fase 1 completata alle 13:10 (D: ripetizioni 1-2 su HU20-HU40 e 1-3 su HU10;
E ed F: 1-3 su tutti e quattro i benchmark), report in `out/suite/report.md` e tabella
conclusiva nella sezione 5.4.6 di
[MEMORY_TIME_OPTIMIZATION_2026-09-21.md](MEMORY_TIME_OPTIMIZATION_2026-09-21.md). F (trainer di E
con prefetch, certificatore a scheduling dinamico) e' la versione di riferimento: end-to-end
1.216 / 2.953 / 3.190 / 3.221 s su HU10/HU20/HU30/HU40 (-19 / -20 / -14 / -19 % rispetto ad A,
-30 / -28 / -33 / -36 % rispetto alla baseline), picchi di memoria identici ad A, criteri 7.2
PASS ovunque, policy identiche fra ripetizioni. Le ripetizioni di E ed F sono state misurate
con 5,2-6,0 core contro i 6,3-6,5 delle notti di D: la fonte e' l'app Claude stessa (0,3-0,5
core continui) oltre a Brave finche' era aperto; il confronto robusto e' la CPU del trainer
(-17 % da D a E/F su HU20-HU40) e il verdetto sul tempo di parete viene dalla ripetizione 4
appaiata (D, E, F consecutive nelle stesse condizioni). HU20 appaiata (13:10-15:45): training D 2.418 s, E 1.928, F 1.932 (-20 %), CPU del trainer -18,5 %, certificatore F 877 s contro 908 di E e 938 di D, end-to-end F 2.853 s (-16 % su D, -23 % su A); HU40, HU30 e HU10 appaiati nella notte del 26 settembre (sezione 5.4.8).

Regole e decisioni dell'utente (protocollo, sezioni 6 e 7.2): nessun processo del milestone
puo' caricare la macchina fuori dalle 00:00-18:00 (regola del 24 e del 25 settembre; dal 26 la
fascia e' 00:00-21:00: run misurati 00:00-20:00, manutenzione 20:00-21:00); fino al 25 i run
misurati usano 00:00-17:00 (`SUITE_WINDOW=00:00-17:00`) e build, test e sonde solo 17:00-18:00;
il runner della coda e'
avviato in modo distaccato (`start /min bash -lc ...` da un file .cmd: il lancio inline da cmd
falliva in silenzio) cosi' l'app puo' restare chiusa durante le misure. Vincoli fissati: il
picco di RAM non deve superare quello di A (2,11 / 7,94 / 8,18 / 8,18 GiB) con qualsiasi
architettura; l'algoritmo puo' cambiare e le 16.000 iterazioni non sono un vincolo (contano
solo tempo e RAM); la convergenza a Nash certificata (1 % del piatto) e' richiesta su tutti e
quattro i benchmark, HU30 e HU40 compresi. La metrica principale diventa il tempo end-to-end
per certificare l'1 % sotto il tetto di memoria.

Fase 2 avviata: candidata H (`cand-h-timestamps16`, sezione 5.4.7): timestamp del discount
lazy a 16 bit in epoche (bit-identica a F sotto 65.535 iterazioni, -0,38 GiB su HU20, -0,40 su
HU30/HU40, -0,10 su HU10), materializzazioni complete che saltano le righe mai toccate, base
dell'epoca allineata al caricamento di un checkpoint (ripresa bit-identica), telemetria di
copertura delle righe e delle pagine da 4 KiB nell'evento finale, test `test_lazy_discount_epoch`.
Revisione statica (agent Opus) prima della build: due difetti logici corretti (copertura
azzerata dalle materializzazioni intermedie; base dell'epoca al caricamento) e tre minori
(conteggio delle pagine a cavallo dei nodi, limite di riga in `discount_last_iteration`,
validazione dell'opzione CLI). Catena di manutenzione delle 17:05 (build, sei suite di test,
sonde di identita' HU10/HU20 contro E, archiviazione, sonde sulle distanze di prefetch):
build riuscita alle 17:07, sei suite di test PASS alle 17:10 (test delle epoche: 34.696 righe toccate su 47.848 nel gioco ridotto), identita' PASS su HU10 e HU20, eseguibili archiviati in `out/suite/bin/cand-h-timestamps16`. Prima misura di copertura: a 100 iterazioni su HU20 e' toccato il 26 % delle righe river ma il 100 % delle pagine da 4 KiB su ogni street (righe da 18 byte sparse ovunque): l'allocazione sparsa a pagine (candidata I) e' esclusa; il margine per la fase 3 verra' dallo storage narrow e dai timestamp a 16 bit, salvo che la copertura per riga a 16.000 iterazioni (run di H del 26/09) giustifichi un indice per riga. Sonde sulle distanze di prefetch (HU20, 2.000 iterazioni, stato bit-identico in tutte): senza prefetch 284 s di training, 4/8 (predefinite) 244,5, 8/16 242,9, 16/32 252,6; distanze maggiori non rendono, i default restano 4/8 e la variante E2 e' chiusa.

Prossimo passo: dal 26 settembre la finestra di misura e' 00:00-20:00 con build, test e sonde 20:00-21:00 (runner riavviato con `SUITE_NOT_BEFORE`) e ogni benchmark deve chiudere sotto i 35 minuti end-to-end con certificato all'1 % (decisioni dell'utente, protocollo sezione 6). Domani: verdetto appaiato notturno, tre ripetizioni di H con la copertura a 16.000 iterazioni, run di riferimento a 32.000 iterazioni su HU30/HU40 con F, e alle 20:05 la build della candidata successiva: certificatore vettorizzato (linea A, tempo per certificare su HU20) e preparazione delle tabelle della fase 3 (linea B, convergenza su HU30/HU40).

### 2026-09-24 — adozione di A, push, roadmap e fase 1 (tempo a memoria invariata)

Fatto: commit `92106db` sul branch `feat/preflop-compact-policy-suite` (solver con policy
compatta, storage narrow opzionale, certificatore corretto, suite e documenti), pushato su
`origin`; da questo albero ogni eseguibile usa la soluzione A con `--table-storage double` di
default. Roadmap in quattro fasi con vincolo di memoria al livello di A e finestra di misura
01:00-09:00 (protocollo, sezione 6; `run_queue.sh` attende la finestra e verifica che il run
possa finire prima delle 09:00). Branch `feat/preflop-phase1-time`, candidata
`cand-d-phase1-time`: discount lazy in tempo costante con prodotti prefissi, checkpoint finale
scritto in parallelo all'export della policy, timer del refresh; build pulita, test e sonde
in corso (D non e' bit-identica ad A: criteri 7.2). Dettagli in
[MEMORY_TIME_OPTIMIZATION_2026-09-21.md](MEMORY_TIME_OPTIMIZATION_2026-09-21.md), sezione 5.4.
Prossimo passo: archiviare gli eseguibili di D, coda notturna (ripetizioni 1-3 su HU10-HU40),
report con confronto verso baseline e A, poi profilo del certificatore.

### 2026-09-21 — milestone RAM e tempi su tutta la suite: censimento, protocollo comune, baseline rieseguita, candidate in coda

Fatto: (1) censimento di tutti i benchmark del solver preflop blueprint (HU10 ridotto e completo,
HU20, HU30, HU40 = CO40 test, CO40 completo) piu' le famiglie legacy e postflop, con
implementazione, astrazione, protocollo, certificazione, risultati e stato di ogni run storico:
[BENCHMARK_SUITE_INVENTORY_2026-09-21.md](../../archive/history7-suite-2026-09/BENCHMARK_SUITE_INVENTORY_2026-09-21.md). Rapporto sulle
differenze originarie: tre astrazioni diverse (bucket diretti per HU10, class-major, history7),
iterazioni 2.000-37.000, size postflop 66 % contro 100 %, HU20 senza la 3-bet a 17a per decisione
dell'utente (a 20 ante il motore la terrebbe distinta: scenario derivato `HU20-2`), HU30 in
modalita' automatica con certificazione in-process, nessun SHA-256 dell'eseguibile del run HU20 di
riferimento. (2) Profilo comune `benchmarks/suite/preflop_blueprint_suite.json` con i soli tre
parametri per scenario (stack, numero di size preflop, numero di size postflop), catalogo delle
size con regola deterministica (preflop `[5a, 17a]` con cap allo stack; postflop `{1: [100 %],
3: [33/66/120 %]}`), astrazione `history7`, protocollo 16.000 iterazioni / batch 32 / 8 thread /
partizione 64 / DCFR alternato lazy v2 / BR fisica esatta chunk 16, SHA-256 di risorse, tabelle e
mappa; resolver che genera le fixture (`benchmarks/suite/fixtures/`: HU10-FULL, HU20, HU30, HU40,
HU40-FULL byte per byte uguali alle storiche, `HU10` nuova con il 100 % postflop, id
`PREFLOP-BLUEPRINT-HU10-POT-001`, albero `fnv1a64:d7b31d6f2cb759fc`); controllo automatico
`tools/preflop_suite/suite.py check` che confronta fixture, artefatti, eseguibili e eventi
`start` dei run ammettendo solo i campi derivati dai tre parametri; driver `run` con monitor di
memoria (campioni ogni 0,5 s piu' contatori esatti dal processo uscito: picco di private commit
e di working set, page fault, tempi CPU, memoria disponibile e paging del sistema) e manifest con
hash; `report` con tabelle per scenario. Protocollo, misure e criteri di accettazione
preregistrati: [BENCHMARK_SUITE_PROTOCOL_2026-09-21.md](../../archive/history7-suite-2026-09/BENCHMARK_SUITE_PROTOCOL_2026-09-21.md).
(3) Build pulita dell'HEAD `ba93c75` in `out/build/windows-release-suite` (trainer SHA-256
`d9d7bba6...`), archiviata in `out/suite/bin/baseline-ba93c75`; baseline canonica in esecuzione
sequenziale (`out/suite/baseline-ba93c75`). (4) Codice delle candidate scritto nel working tree
(non compilato finche' la baseline gira, per non perturbare i tempi): policy compatta per batch al
posto della terza tabella densa, matrici all-in raccolte dalle tabelle dense 630 x 630 senza copia
per board, export della policy in streaming (`PolicyStreamWriter`), storage delle tabelle
`double` / `mixed` (somme float32) / `float32` con arrotondamento solo alla scrittura,
strumentazione (evento `memory_breakdown`, `write_seconds`, contatori delle celle di policy,
picchi di processo nel trainer e nel certificatore, `--actions` nel report dell'albero); test
aggiornati (export in streaming uguale byte per byte, storage narrow, opzione di riuso rifiutata).
Descrizione: [MEMORY_TIME_OPTIMIZATION_2026-09-21.md](MEMORY_TIME_OPTIMIZATION_2026-09-21.md).
Comandi: `suite.py resolve --game-exe ...`, `suite.py check`, `suite.py run --version
baseline-ba93c75 --scenario HU10|HU20|HU30|HU40`; driver della fase candidate
(`candidate_phase.sh`: attende la baseline, compila, esegue i test, verifica l'identita' bit per
bit su HU10 a 40 iterazioni e HU20 a 100, archivia, lancia la suite di A) e coda sequenziale
(`tools/preflop_suite/run_queue.sh out/suite/queue.txt`: B, C, ripetizioni 2 e 3 di baseline e A).
Risultati: HU10 canonico (history7, protocollo comune) baseline: training 1.481,95 s (0,0926
s/iterazione; refresh 376,3 s, board 363,3 s, traversata 742,4 s), trainer 1.508,1 s, BR esatta
281,4 s (+7,7 s di preparazione), end-to-end interno 1.797,1 s; picco private commit 3.292.786.688
B (3,07 GiB) nel trainer e 2.010.173.440 B nel certificatore; **max gain 0,002045731569542797 a,
PASS** (NashConv 0,003674811 a, EV CO 0,136116 a); policy `fnv1a64:38564a90b4f1577d`, stato
`fnv1a64:f5f34a40bc39910f`. HU20 baseline avviata con identita' del trainer
`fnv1a64:90d07de511eb9968`, uguale a quella del run di riferimento del 2026-09-20. HU10-FULL e
HU40-FULL: `RESOURCE_LIMIT` con `history7` (34,1 GiB e oltre 18,6 GB di sole tabelle).
Fallimenti: (1) `gtosd_preflop_blueprint_game` accettava capacita' a 16 bit: con `history7` i
valori 222.865 e 1.539.270 venivano troncati; corretto a 32 bit nel codice candidato, il resolver
calcola il layout dalle colonne per riga. (2) Il fingerprint dell'albero include la
serializzazione della configurazione: la chiave opzionale `limp_response_target_units: []`
cambia il fingerprint anche a struttura identica; il resolver la emette solo quando la lista
delle risposte non e' vuota, come nelle fixture storiche. (3) Il certificatore su HU10 con
`history7` ha un picco di commit di 2,0 GiB nella fase di preparazione e 1,05 GiB stabili: da
misurare con la build strumentata (il picco non e' spiegato dalla policy da 30 MB).
Dubbi: (1) il catalogo postflop non e' annidato (100 % contro 33/66/120 %): entrambe le righe
sono decisioni dell'utente e non vengono cambiate; (2) tre ripetizioni per scenario e versione
richiedono circa 30 ore di macchina: la coda esegue prima le ripetizioni 1 di tutte le versioni,
poi le 2 e le 3 di baseline e A; le versioni B e C ricevono ripetizioni ulteriori solo se
promosse.
Prossimo passo: al termine della baseline il driver compila le candidate; se i test e l'identita'
bit per bit passano, suite di A, poi B, C e ripetizioni; report finale con le tabelle per
scenario, la scomposizione di memoria e tempi per street e componente, e la valutazione di
qualita' (EV, certificato).
Aggiornamento 2026-09-22 03:45: baseline canonica completa e uniforme (`UNIFORMITY_CHECK=PASS`
sui quattro run): HU10 PASS 0,002046 a (e2e 1.797 s, picco 3,07 GiB), HU20 PASS 0,028000 a
(policy `fnv1a64:362045ee45623b7a` uguale al run di riferimento; e2e 5.679 s, picco 11,49 GiB),
HU30 FAIL a lavoro fisso 0,191819 a (uguale al valore storico a 16.000; e2e 5.442 s, picco 11,83
GiB), HU40 FAIL a lavoro fisso 0,289557 a (e2e 5.036 s, picco 11,83 GiB); certificatore 7,6-7,9 GB
di picco su HU20-HU40. Revisione statica del codice candidato (agente Opus 5) senza errori di
compilazione; build pulita alle 02:40; sei suite di test PASS dopo l'emendamento del test dello
storage (C non supera il criterio per cella preregistrato: divergenza di traiettoria dalla
seconda iterazione, documentata nel protocollo 7.2); identita' bit per bit di A con la baseline
PASS su HU10 (40 iterazioni) e HU20 (100 iterazioni: stato `fnv1a64:81695fcdc36df274`, policy
`fnv1a64:437892420c1b3714`). Sonda HU20 a 100 iterazioni: picco del trainer 7,94 GiB contro 11,49
GiB. Diagnosi E: il certificatore teneva due tabelle della policy durante il caricamento
(segnaposto uniforme piu' tabella caricata): corretto in `preflop_blueprint_certify.cpp`, stesso
certificato sulla sonda HU10 e picco 1,14 GB contro 2,01 GB. Liste delle azioni legali per i
sette scenari in `benchmarks/suite/actions/`. Coda sequenziale avviata alle 03:40: A (ripetizione
1 su HU10-HU40), poi B, C e le ripetizioni 2-3 di baseline e A.
Risultati finali 2026-09-23 23:38 (48 run: 4 versioni x 4 benchmark x 3 ripetizioni, uniformita'
PASS per ogni versione; report in `BENCHMARK_SUITE_REPORT_2026-09-23.md` e sezione 5 di
`MEMORY_TIME_OPTIMIZATION_2026-09-21.md`): candidata A bit-identica alla baseline su tutti i
benchmark (stesse policy e certificati), picco di commit -31 % ovunque (HU10 3,07 -> 2,11 GiB,
HU20 11,49 -> 7,94, HU30/HU40 11,83 -> 8,18), CPU del trainer -13/-23 %, training x1,14-1,30 e
e2e x1,11-1,29 sulle ripetizioni pulite; certificatore 7,6-7,9 GB -> 3,7-3,8 GiB (correzione E).
B (somme float32) -45/-46 % e C (float32) -59/-61 % con delta di EV e max gain dell'ordine di
1e-5 a, entro le tolleranze preregistrate su tutti gli scenari; C non supera il criterio per
cella (divergenza di traiettoria, emendamento 7.2). HU30 e HU40 restano FAIL a lavoro fisso con
ogni versione (baseline uguale allo storico). Fallimenti e limiti: tempi diurni contaminati
dall'uso interattivo del PC (core effettivi 4,1-5,2 contro 6,0-6,5), gestiti con l'indicatore di
contesa e le mediane sulle ripetizioni pulite; il test dello storage float32 ha richiesto
l'emendamento del criterio per cella; due incidenti di automazione (driver non terminato,
file della coda con CRLF) senza effetto sui risultati. Decisione: A come implementazione unica
(`double`), `mixed` consigliato quando la memoria e' il vincolo, `float32` sperimentale.
Prossimo passo: revisione del codice candidato e commit su branch dedicato (nessun commit
eseguito), eventuale riduzione dei timestamp del discount lazy (0,8 GiB) e valutazione di HU10
completo con storage narrow (34 GiB baseline: con `float32` circa 17 GiB, eseguibile).

### 2026-09-21 — pilot HU40 `history7` entro 12 GiB

Il pilot HU40 richiesto ha completato 500 iterazioni con DCFR alternato,
batch 32, otto thread, partizione 64 e `lazy-discount-v2-hybrid`. Il training
ha richiesto 96,489 s, pari a 0,192977 s/iterazione; la valutazione diagnostica
su otto flop ha richiesto 18,517 s. Il processo è terminato normalmente in
130,956 s con `PREFLOP_BLUEPRINT_TRAIN=ITERATION_LIMIT`.

Il picco di working set osservato dal monitor esterno è 12.728.139.776 byte,
11,854 GiB: restano 156.762.112 byte, circa 149,5 MiB, rispetto al tetto
temporaneo di 12 GiB. Il margine è sufficiente per questo pilot ma non qualifica
il layout per il limite di prodotto da 8 GiB. La stima campionata a 500
iterazioni è max gain 1,39651 ante, semiampiezza 0,305243 ante; è un punto
iniziale su otto flop, non una BR esatta né una misura di convergenza finale.
Log: `out/hu40_pilot/hu40_history7_lazy_v2_pilot500.jsonl`.

### 2026-09-21 — budget 8 GiB e protocollo unico dei benchmark

Il requisito di prodotto è ora un picco di processo non superiore a **8 GiB**.
I 25 GiB restano il tetto usato dall'audit cap 23, non una configurazione
candidabile. HU20 `history7` conserva il PASS matematico a 0,0279995887 ante,
ma usa circa 11,44 GiB e quindi fallisce il nuovo requisito di memoria. Cap 23
HU30 usa 24,67 GiB ed è escluso anche senza considerare il tempo.

HU10, HU20, HU30 e HU40 devono usare un solo solver automatico. I benchmark
possono variare soltanto stack, numero di size preflop e numero di size
postflop. Iterazioni, batch, capacità, feature, clustering, arresto e
certificazione non possono essere regolati per fare passare una fixture. Le
fixture correnti non sono ancora uniformi nelle percentuali: HU10 ridotto usa
66 % postflop, mentre HU20/HU30/HU40 di test usano 100 %. I confronti restano
diagnostici finché il profilo condiviso non viene riallineato.

La prossima ipotesi documentata è una rappresentazione universale
`[equity, hand strength, draw potential, nut potential, blockers, future distribution]`.
L'audit precedente non la dimostra: esclude soltanto le collisioni flop
esattamente identiche come causa primaria. La geometria fra osservazioni
vicine e le feature di turn e river restano da isolare. Il requisito temporale
resta almeno il 50 % in meno del riferimento HU20 da 74m54s, quindi massimo
37m27s end-to-end sullo stesso hardware e con la stessa BR fisica.

### 2026-09-21 — audit causale: numero di bucket sì, clustering no, cap `history7` plausibile

Su richiesta dell'utente è stato eseguito un audit senza cambiare il solver e
senza avviare training. Report completo:
[HU30_BUCKET_CAUSAL_AUDIT_2026-09-21.md](../../archive/history7-suite-2026-09/HU30_BUCKET_CAUSAL_AUDIT_2026-09-21.md).

La policy HU30 `fnv1a64:e52d2f110dbd2b34` a 32.000 è stata valutata su 16
flop fissati dal seed `20260921`, otto nodi flop e tutti i futuri esatti. La
perdita causata da collisioni fra feature flop identiche va da `2,06e-8` a
`0,000391` ante. La partizione corrente da 200 bucket perde invece da
`0,011660` a `0,057677` ante. La partizione da 500 bucket recupera fra il
31,6% e il 57,3% di questa perdita locale. Il controllo coincide con il
diagnostico C++ esistente entro `1,02e-14`.

Il limite di 25 iterazioni del clustering non è una causa materiale. A parità
di feature, capacità, seed e riavvii, il flop converge a 45 iterazioni con una
riduzione d'inerzia dello 0,0225%. Il turn era già convergente a 8. Un passo
Lloyd esatto aggiuntivo sul river riduce l'inerzia dello 0,0243% e cambia lo
0,163% del peso. Nessuna tabella prodotta dall'audit è stata salvata per il
solver.

La memoria resta una causa plausibile ma non isolata. `history7` cap 7 riduce
le righe river da 4.248.476 a 1.539.270, cioè del 63,77%. Cap 23 ne conserva
3.398.989 e riduce la distorsione geometrica dell'88,42%, ma il solo pilot a
2.000 è sotto-allenato e non è un confronto causale. La storia completa
richiede 31.138.638.936 byte per regret, strategy sum e policy, oltre 25 GiB
prima di mappe e temporanei.

Classificazione finale: feature flop non supportate come causa primaria;
numero di bucket contributo locale dimostrato; mancata convergenza del
clustering esclusa come causa primaria; cap river di `history7` contributo
forte ma non certificato; combinazione numero di bucket più cap river è la
spiegazione meglio sostenuta. Nessuna di queste misure dimostra una soluzione
che superi il gate.

### 2026-09-21 — HU30 a 32k: il limite dominante è l'astrazione, non la 3-bet

Il run HU30 `history7`, batch 32, otto thread, partizione 64 e DCFR alternato è
stato ripreso da 16.000 a 32.000 iterazioni. La BR fisica esatta su 573 flop e
605.088 board misura max gain **0,16761912899822407 a**, NashConv
**0,23267577680425217 a**, quindi FAIL rispetto alla soglia 0,03. A 16.000 il
max gain era 0,191818521 a: il raddoppio ha ridotto il massimo del 12,6 %.

La BR vincolata a `history7` è stata prima confrontata con `FiniteGame` su un
gioco ridotto. La misura su otto flop non è un estimatore non distorto del gioco
completo: risolve il gioco ristretto ai flop estratti e serve soltanto per
confronti appaiati. La valutazione esatta successiva ha enumerato tutti i 573
flop e 605.088 board in 3.194,826 s. Guadagni astratti:
**[0,03487409101410591, 0,01879786537775447] a**. Il massimo astratto manca il
gate di 0,004874091 a; il massimo fisico lo manca di 0,137619129 a. Per il
giocatore peggiore, la differenza fisico-astratto è **0,132745038 a**, il 79,2 %
del guadagno fisico. Altre iterazioni possono chiudere il piccolo residuo
astratto, ma non spiegano né rimuovono il divario fisico.

HU20 e HU30 condividono ruleset, range, apertura a 5 ante, size postflop pot più
all-in e gate. Non hanno lo stesso albero preflop: a 30 ante la risposta 3-bet a
17 ante resta distinta dallo shove ed è obbligatoria nel prodotto. Non è però
la rotta scelta dalla BR responsabile del massimo: la deviazione dominante passa
da `CO_call_BTN_check_chance`; la risposta 3-bet ha probabilità zero nella BR e
probabilità media 1,75e-5 nel profilo certificato. Il piatto limpato passa da SPR
4,75 su HU20 a SPR 7,25 su HU30. Lo stack maggiore aumenta il costo delle mani
fisiche fuse nella stessa informazione astratta, soprattutto al river.

La copertura a 32.000 non indica tabelle abbandonate: righe regret flop/turn/river
100/100/99,1 %, righe di strategia 99,8/98,0/87,9 %. Tra 16.000 e 32.000 la TV
media pesata della policy cresce per street: 1,08 % preflop, 2,35 % flop, 4,37 %
turn e 6,27 % river. Le strategie profonde restano più mobili, ma la BR astratta
esatta mostra che questo è il problema minore.

Il limite RAM è stato alzato da 12 a **25 GiB**. Sono state censite mappe con cap
river 8/12/16/23/24/28/30/32. Cap 28 ha raggiunto 25,56 GiB e cap 24 25,043 GiB:
entrambi sono stati fermati come `RESOURCE_LIMIT`. Cap 23 conserva 3.398.989
righe river, ha un picco osservato di 24,67 GiB e riduce l'errore quadratico
medio dei centroidi da 171.840.689 a 19.898.848. Nel pilot simultaneo a 2.000
iterazioni richiede 394,354 s, contro 320,833 s del cap 7, e la BR astratta sullo
stesso gioco ristretto a otto flop peggiora da 0,496262101 a **0,726731922 a**.
Il numero di board è rimasto 64.000 mentre le righe river sono più che
raddoppiate: il pilot è sotto-allenato. Non dimostra che cap 23 abbia un limite
asintotico peggiore, ma esclude l'aumento uniforme del cap come soluzione rapida.

Artefatti principali: `out/history7_optimized/hu30_history7_lazy_v2_auto_certificate.json`,
`hu30_history7_lazy_v2_t32000_abstract_br_exact.json`,
`history_cap23_candidate.json`, `hu30_cap23_simultaneous_t2000.jsonl` e
`hu30_cap23_simultaneous_t2000_abstract_br_sample8.json`.

Validazione della build corrente: kernel PASS, 1.633.676 asserzioni; trainer
PASS, 1.581.361 asserzioni. Il vecchio prototipo `recall_full` conferma che lo
storage compatto della storia completa entra in memoria (circa 12,92 GiB), ma
il suo eseguibile compila un albero HU30 obsoleto da 637 nodi invece dei 604
attuali. Il pilot da 26,79 s/100 vale quindi solo come misura di risorse e non
come confronto di convergenza. Prima di usarlo occorre integrare lo storage
compatto nell'albero corrente e ripetere gli oracoli.

`Esatta` qualifica la valutazione, non la policy. La BR astratta enumera tutto il
gioco ma vincola il deviatore agli information set della mappa `history7`; la BR
fisica enumera lo stesso gioco e permette al deviatore di distinguere le hole
card, senza conoscere carte future. Entrambe mantengono l'albero discreto delle
puntate. La prima misura l'errore di ottimizzazione entro l'astrazione; la
seconda include anche l'errore della rappresentazione ed è l'unico gate.

Il requisito di generalità è ora esplicito nel documento del goal. Nessun ramo
può dipendere da `HU30`, dallo stack 30 o dal fingerprint della fixture. La
stessa regola deve conservare HU10 e HU20, mantenere la 3-bet quando produce uno
stato distinto e qualificare HU30 e HU40. Iterazioni, memoria e raffinamento
restano scelte interne derivate dalla soglia sul piatto e dalle risorse locali.

### 2026-09-20 — HU20 `history7` ottimizzato: PASS esatto in 74m54s

Il run batch 32 precedente è stato fermato a 3.000 iterazioni. Il tempo medio
era salito da 0,2231 s/iter nelle prime 500 a 0,2862 s/iter; la proiezione era
76m19s di training più circa 15m05s di BR. Board e RAM restavano lineari. La
telemetria ogni 250 iterazioni ha localizzato la crescita nel refresh policy:
17,900, 22,479, 26,418 e 34,785 secondi per blocco, mentre preparazione board e
traversata restavano quasi piatte.

La causa era `materialize_row()`: il catch-up DCFR delle righe rare eseguiva
una moltiplicazione per ogni iterazione saltata, per ogni azione, sia sui regret
sia sulle somme strategiche. Il costo cresceva con l'età della riga. La nuova
modalità `lazy-discount-v2-hybrid` conserva l'ordine originale sui regret
positivi, usa `ldexp` per il fattore negativo 0,5 e applica alle sole somme
strategiche il rapporto fra prodotti prefissi. Il trainer usa una nuova
identità, quindi non carica checkpoint v1 con la nuova associazione numerica.

La variante che cumulava anche i regret positivi è stata rimossa: dopo 25
iterazioni produceva differenza regret 0,0110507 e differenza policy 1,0. La
versione conservata misura differenza regret 0, strategy sum `8,32667e-17` e
policy media `4,44089e-16`; determinismo tra thread e resume PASS.

Sul confronto a 3.000 iterazioni, la v1 richiedeva 858,616 s e la v2 680,659 s:
20,73 % in meno. Il run definitivo ha completato 16.000 iterazioni e 1.024.000
board in 3.469,540 s di training. Le fasi sono: refresh 1.436,170 s,
preparazione board 387,171 s, traversata 1.646,170 s e discount 0,017 s.
Inizializzazione, materializzazione, checksum e scrittura portano il trainer a
3.570,710 s, 59m31s. Working set finale: 12.285.771.776 byte, circa 11,44 GiB.

La BR fisica esatta ha enumerato 573 flop canonici e 605.088 board in 903,558
s, più 19,827 s di preparazione. Certificato: max gain
0,027999588711995 ante, NashConv 0,0299431715909233 ante, soglia 0,03, PASS.
Il tempo end-to-end è 4.494,095 s, 74m54s. Policy
`fnv1a64:362045ee45623b7a`, stato trainer `fnv1a64:449bbb9b17798709`.

Artefatti e SHA-256 sono in
[HISTORY7_TIME_AUDIT_2026-09-20.md](../../archive/history7-suite-2026-09/HISTORY7_TIME_AUDIT_2026-09-20.md).
Regressioni finali: trainer PASS 21.310.536 assertion, kernel PASS 1.636.010,
certificatore PASS 146.545. HU20 è riproducibile e qualificato; il prossimo
bersaglio sequenziale è HU30.

### 2026-09-20 — audit del tempo `history7`; run 8k fermato e ipotesi corretta

Su richiesta dell'utente ogni training è stato fermato. Il tentativo
`history7` batch 64 avviato verso 8.000 iterazioni è terminato a 1.800:
865,789 s di training, 230.400 board, 0,480994 s/iterazione e working set
12.416.487.424 byte. Non sono stati scritti checkpoint o policy. Considerare
8.000 come possibile chiusura sotto 90 minuti era scorretto: il solo punto
qualificato è 16.000 con BR fisica 0,0279995887 a.

Il profilo pulito di 100 iterazioni, stato
`fnv1a64:505f4de576707bf5`, divide 43,2937 s di training in: discount lazy
0,000184 s, refresh policy 11,3242 s, preparazione board 8,38124 s e
traversata CFR 23,5881 s. Le quote sono 0,0004 %, 26,16 %, 19,36 % e
54,48 %. La proiezione sostenuta a 16.000 è 2 h 08 min 16 s di solo
training; aggiungendo i 933,681 s della certificazione fisica, il limite
inferiore supera 2 h 23 min prima dell'I/O.

Sono state rimosse tre ottimizzazioni non sufficienti: cache netta all-in
(circa 1,5 % ma traiettoria numerica diversa), cache policy locale copiata
(0,399115 s/iter) e cache policy locale calcolata direttamente
(0,395803 s/iter e circa 135 MB in più). Ordinamento radix, mark
generazionali, buffer piccoli persistenti e `/arch:AVX2` non hanno prodotto
un guadagno ripetibile. Una partizione target 8 ha peggiorato il profilo a
0,531683 s/iter. Restano discount lazy, preparazione board parallela, pool
persistente, bitmap delle righe attive, otto thread e partizione 32.

Il programma evita ora di riscrivere lo stesso checkpoint più volte alla
stessa iterazione. La modifica riduce solo I/O e non è ancora benchmarkata.
Il log e gli artefatti completi del vecchio 16k non sono presenti in `out`:
restano verificabili il PASS, il max gain, le iterazioni, circa 11,48 GiB e
la durata riferita di circa tre ore, non la scomposizione delle sue fasi.

Audit completo, inclusi tentativi falliti, budget temporale e opzioni:
[HISTORY7_TIME_AUDIT_2026-09-20.md](../../archive/history7-suite-2026-09/HISTORY7_TIME_AUDIT_2026-09-20.md).

### 2026-09-20 — gerarchia compatta su censimento esatto e arresto automatico

L'audit successivo a `history7` conferma due fatti distinti. HU20 a 16.000
iterazioni passa la certificazione fisica esatta con max gain
**0,0279995887 a**. HU30 allo stesso checkpoint resta sfruttabile anche nel
gioco astratto: max gain campionato 0,154603646 a e max gain fisico esatto
0,191818521 a. `history7` risolve quindi la perdita di memoria astratta di
HU20, ma costa circa 11,48 GiB e non generalizza: sul gioco HU10 completo la
proiezione è 34,10 GiB per le maggiori colonne d'azione.

È stata aggiunta una gerarchia fissa e serializzata `GTOSDHR2`. Mantiene tutte
le coppie classe/bucket flop; al turn raggruppa soltanto figli dello stesso
padre con distanza CDF-L1 e mediana pesata; al river raggruppa soltanto figli
del turn già compresso con distanza L2 quadratica e media pesata. La mappa
nasce dall'intero censimento di 3.506.025.600 osservazioni fisiche pesate. La
lookup al turn non riceve il bucket river e ogni riga ha un solo padre.

Con otto figli massimi per padre il risultato è
**7.585 / 60.097 / 474.047** righe, fingerprint
`fnv1a64:e1b177635b19fe48`, file da 34.178.340 byte. Una cache diretta HR2
di turn e river porta la mappa residente a 306.966.340 byte senza cambiare
file o fingerprint; elimina le ricerche binarie dall'hot path. Il layout HU20 contiene
142.896.501 celle per tabella: regret, somme e policy richiedono
3.429.516.024 byte. HU30 e HU40 richiedono 3.533.539.656 byte. Costruzione e
round-trip: 40,10 s. Report:
`out/hierarchy32/history_rows_v2_cap8.json`.

Due prove sono state invalidate e fermate. `32/32` significava erroneamente
32 figli, mentre nel vecchio nome `recall32` il numero indicava indici a 32
bit e il prototipo usava otto figli: la mappa ottenuta avrebbe richiesto
22,32 GB su HU20. La prima mappa `8/8` usava erroneamente L2 anche al turn;
il run HU20 è stato interrotto a 500 prima della valutazione. File e
checkpoint sono prefissati `INVALID_L2_TURN_` e non costituiscono evidenza.

Il trainer accetta ora soltanto la soglia sul piatto come controllo di
convergenza, `--target-pot-percent`, default 1. In modalità automatica usa
batch 64, al massimo otto thread disponibili, DCFR lazy e refresh
selettivo; valuta checkpoint 250/500/1.000/2.000 e successivi raddoppi sugli
stessi otto flop. Quando il max gain stimato scende sotto quattro volte la
soglia, il trainer passa direttamente ai 573 flop canonici con BR fisica
esatta, nello stesso processo; se l'esatto fallisce, il training riprende
dal checkpoint successivo. Quattro
checkpoint con meno del 5 % di miglioramento producono `PLATEAU`, mai un
PASS. Il certificatore esatto riceve la stessa percentuale e scrive
`passes_target`. `--iterations` resta un override esclusivamente di ricerca.

Il primo screening senza cache è stato interrotto a 250 dopo oltre 13 minuti
di sola valutazione ancora incompleta; il log è prefissato
`INCOMPLETE_SLOW_LOOKUP_` e non contiene una misura di exploitability. Un
secondo tentativo con cache, ancora su 64 flop e quattro thread, è rimasto
incompleto dopo oltre 14 minuti. La cache costa 272.788.000 byte oltre alla
mappa serializzata. Sulle prime dieci iterazioni il training passa da 0,6831
a 0,6699 s/iterazione.

Il primo screening completo usa otto flop e otto thread al checkpoint 500:
8.448 board completi in 14,9321 s, max gain stimato 0,301785 a, limite
inferiore 0,0125834 a e semilarghezza 0,172672 a. Il valore centrale è dieci
volte la soglia 0,03 a. Il vecchio gate usava anche il limite inferiore e ha
avviato per errore la certificazione esatta: un campione piccolo può avere un
intervallo largo anche quando il valore centrale è lontano dalla soglia. La
certificazione è stata fermata dopo 32/573 flop, il checkpoint 500 era già
salvo, e il gate ora richiede max gain stimato <= quattro volte la soglia.

La curva successiva sugli stessi otto flop scende a 0,268395 a a 1.000,
0,238778 a a 2.000, 0,224014 a a 4.000 e 0,219175 a a 8.000. I quattro
miglioramenti relativi sono 11,1 %, 11,0 %, 6,2 % e 2,2 %. Il BR richiede
13,4-14,9 s per 8.448 board completi.
Il training usa ora quattro thread, mentre il BR ne usa otto: su questa CPU
la traversata CFR è limitata dalla banda memoria e passa da circa 0,72 a 0,69
s/iterazione; il parallelismo aggiuntivo resta utile sui flop indipendenti.

Al checkpoint 8.000 la BR vincolata alla stessa astrazione e agli stessi otto
flop misura max gain 0,154556 a. La BR fisica è 0,219175 a: la differenza
osservata è 0,064619 a, ma non è una decomposizione additiva dell'errore.
Poiché anche la BR astratta supera di oltre cinque volte la soglia, il punto
è ancora undertrained nel gioco astratto. Il checkpoint 8.000 viene quindi
esteso a 16.000 prima di decidere se aumentare la capacità della gerarchia.

L'estensione è stata fermata su decisione dell'utente prima di 16.000. Il
percorso `8/8` è **FAIL** rispetto al nuovo requisito operativo: almeno il 50
% di tempo in meno di `history7`, con la stessa soglia fisica di 0,03 a. A
8.000 richiede già 2 h 32 min di lavoro complessivo includendo diagnosi e
tentativi interrotti; proseguire fino a 16.000 avrebbe superato il tempo del
run `history7`. Il checkpoint 8.000, la policy e le due BR restano come
evidenza. Il prossimo intervento deve ridurre il costo dominante del trainer
o il numero di traversate richiesto; una sola riduzione della memoria non
soddisfa il requisito.

Build MSVC Release `/W4 /WX` PASS. Suite trainer deep, inclusi formato HR1,
HR2, determinismo, causalità, round-trip e soglia configurabile: PASS con
1.581.361 asserzioni. Il run automatico HU20 è ripreso dal checkpoint 500
con PID 9640; nessun limite di tempo, RAM o iterazioni è stato fornito al
processo.

### 2026-09-20 — stima esatta del layout HU10 `history7`, nessun solve avviato

Su richiesta dell'utente e stato calcolato il layout di HU10 senza allocare gli array e senza
avviare il training. Il benchmark esistente ha contato per street nodi decisionali e colonne di
azione con capacita unitarie; il report applica poi le capacita della mappa `history7`
`7.585/222.865/1.539.270` (`fnv1a64:3c9ee76ca6aad23b`).

HU10 completo (1.501 nodi, 584 decisioni) richiede 1.425.343.821 celle per tabella: policy
10,62 GiB, checkpoint R+S 21,24 GiB, tre tabelle 31,86 GiB e timestamp lazy 2,24 GiB. Stato
trainer a 2.000 iterazioni: **34,10 GiB**; picco di processo proiettato dal margine osservato su
HU20: 34,35-34,38 GiB. Non entra nel limite di 12 GiB. HU10 ridotto (193 nodi, 80 decisioni)
richiede invece 116.534.101 celle: stato trainer **2,81 GiB**, checkpoint 1,74 GiB, policy
0,87 GiB e picco proiettato 3,06-3,08 GiB; entra nel limite. Il layout completo e 3,04 volte lo
stato HU20 `history7`, perche le tre size postflop portano le colonne d'azione per riga da 512 a
1.396. Report: `out/hu_goal/hu10_history7_layout_estimate.json`.

Nessun training HU10 e stato avviato. Un confronto `class`/`history7` entro 12 GiB deve quindi
usare inizialmente HU10 ridotto oppure cambiare esplicitamente rappresentazione/storage; non si
chiama `history7` una mappa con cap diverso.

### 2026-09-19 — HU20: BR astratta, batch più grande e discount lazy

Il pilot history7 a 2.000 iterazioni termina su 128.000 board. Media
`fnv1a64:195f8c83b3582c3b`, stato `fnv1a64:dd5ce5f6230f5b4e`; training
3.153,17 s, totale 3.545,12 s. Sul campione fissato di 64 flop, seed 20260919,
il max gain fisico e 0,221344071 a. Il valore scende da 0,595749411 a a 500,
ma resta lontano dal gate.

La BR vincolata all'astrazione history7 e stata implementata con la ricorrenza
preregistrata e confrontata con una BR FiniteGame indipendente: errore massimo
1e-9 sul gioco ridotto. Sullo stesso campione a 2.000, il max gain astratto e
0,142510257 a. Quindi il profilo e ancora sfruttabile dentro history7; l'errore
di ottimizzazione e sostanziale prima ancora della perdita dovuta
all'astrazione. La misura e campionata e non sostituisce il gate fisico.

Il tentativo batch 512 per 125 iterazioni e stato interrotto al working set
13.154.013.184 byte, oltre 12 GiB. La correzione batch 320 per 200 iterazioni
resta sotto il limite, 12.484.063.232 byte, ma a pari 128.000 board produce
max gain astratto 0,172034656 a: peggio di 0,142510257. Ridurre il numero di
update e discount non risolve il transitorio; il ramo non viene esteso.

Il discount DCFR lazy evita le scansioni dense e conserva l'ordine esatto dei
fattori per ogni riga. Una prima versione con prodotti cumulativi e fallita:
una differenza di 3,47e-17 ha cambiato il regret matching di una riga quasi
nulla e poi la traiettoria. La versione corretta applica gli stessi fattori
nello stesso ordine dell'eager. Dopo righe inattive per più iterazioni,
regret, strategy sums e policy sono bit-identici; checkpoint/resume passa.
Suite completa: 21.310.122 asserzioni PASS.

Profilo HU20 reale, 10 iterazioni: stato dichiarato 12.037.793.016 byte,
picco working set 12.305.924.096 e memoria privata 12.329.254.912 byte
(11,48 GiB). Training 5,527 s, contro 10,269 s dell'eager alle prime 10
iterazioni del pilot precedente. Il training esplorativo a 8.000 e avviato
solo su HU20, con arresto automatico oltre 12 GiB.

### 2026-09-19 — pilot history7 HU20: transitorio a 500, continuazione a 2.000

I test ASAN di trainer, certificatore ed export passano: 1.057,65, 511,17 e
560,36 secondi. Il codice del pilot e identificato dal commit locale `17041a8`;
SHA256 trainer `5aa6b71c53441d688261f61dc636403c6cb1f1bf9504c812a868ef22bb0c430a`
e certificatore `aeaeaa1da7a0139d1a348056fa8023087836d45b0247095be44288b01ee57db1`.

HU20 history7 a 500: media `fnv1a64:b88c44f1d9642cdf`, stato
`fnv1a64:a887da709d4f75e6`, 32.000 board. Training 754,891 s; totale
945,459 s incluso I/O. Il discount globale richiede 576,296 s, il refresh
selettivo 27,148 s, la preparazione 71,420 s e le traversate 80,027 s.
Picchi: working set 11.475.062.784 byte, memoria privata 11.496.087.552 byte,
entrambi sotto 12 GiB. Checkpoint SHA256
`f2a10ef2f664bc6300e52115b293e15ec24b162416cffb84d3345013f4f301ea`;
policy SHA256
`4bd38b2072b044b327a5442939b594e2af33277c2d035bb66a8fd5f3072f46d9`.

Sul campione preregistrato di 64 flop, seed 20260919, max gain 0,595749411 a,
lower bound 0,053709052 a e semiampiezza 0,069883159 a. Il class a 2.000 sullo
stesso campione vale 0,113256300 a. Il transitorio e molto peggiore, ma il
protocollo vieta di arrestare il pilot sulla sola misura a 500. Checkpoint
conservato in `hu20_history7_t500_ckpt.bin`; ripresa a 2.000 avviata con
identita, capacità e seed invariati. Nessun run HU30 o HU40 concorrente.

### 2026-09-19 — HU20 class a 8.000: FAIL, difetto congiunto persistente

Certificazione completa terminata: 573 flop, 605.088 board, max gain
**0,049244481988271999 ante**, P1 0,0030199656767189442; gate 0,03 non passato.
Media `fnv1a64:36e9290be79d23ab`, albero invariato `f5b432de223744cc`.
Certificato `out/hu_goal/hu20_t8000_full.json`, 933,681 s; durata del processo
938,669 s, picco working set 463.699.968 byte, eseguibile SHA256
`1fa84464b5d1346e5db3c04d84f9e995e6ec7ac3b3ee01762e4e451cf1d0f3f1`.

La curva 2.000/4.000/8.000 vale 0,060957434/0,053314792/0,049244482.
L'ultimo raddoppio migliora del 7,63%, senza dimostrare un limite asintotico.
P0: sola deviazione preflop 0,0013142326812759994; postflop a preflop fissato
0,0023188694998061907. Limp/check resta raro nella media (0,0020397999829581195)
e frequente nella BR congiunta (0,23590873211877225); recupero postflop su
questa rotta 0,10183680487752461 ante. Nessuna localizzazione causale ulteriore
e dedotta da questi soli valori.

Si applica il protocollo gia dichiarato: dopo ASAN, pilot history7 solo HU20,
checkpoint 500/2.000, stesso seed, stesso algoritmo e stesso albero. Non si
estende ancora class a un altro numero arbitrario di iterazioni.

### 2026-09-19 — HU20 a 8.000 completato; certificazione esclusiva

Terminata la continuazione class da 4.000 a 8.000: 512.000 board cumulativi,
media `fnv1a64:36e9290be79d23ab`, stato `fnv1a64:ae2ecc4ad3ba6dfe`.
Training della continuazione 2.470,04 s, totale 2.538,09 s, quattro thread.
Il vecchio eseguibile divide il tempo per le 8.000 iterazioni cumulative:
il costo corretto delle 4.000 nuove iterazioni e 0,61751 s/iterazione.
Il denominatore e gia corretto nella CLI corrente.

Checkpoint conservato in `out/hu_goal/hu20_class_t8000_ckpt.bin`.
Avviata certificazione completa su 573 flop, otto thread, senza altri
training o benchmark concorrenti. Output atteso `hu20_t8000_full.json`;
log e misura del processo in `hu20_t8000_full.log` e
`hu20_t8000_full.runtime.json`. Nessun esito di convergenza ancora disponibile.

### 2026-09-19 — priorita richiesta: HU20 prima, poi HU30 e HU40

L'utente chiede perche si stiano testando piu cose insieme e propone di
controllare prima solo HU20. Si adotta l'ordine sequenziale: completare
HU20 class a 8.000 e certificarlo; se non passa, pilot della rappresentazione
gerarchica su HU20. HU30 e HU40 seguiranno una soluzione verificata su HU20.
Non sono stati avviati nuovi training HU30; su HU40 history7 sono state
eseguite soltanto le 10 iterazioni di profiling e i controlli di persistenza.
La concorrenza tra lavoro HU20 e controlli HU40 contendeva la CPU e limitava
la comparabilita dei tempi. L'obiettivo finale dei tre giochi resta invariato.

### 2026-09-19 — HU20 migliora a 4.000; candidata con storia completa fino al turn

HU20 class riprodotto a 2.000 con fingerprint identico al riferimento storico.
A 4.000, certificazione integrale: max gain **0,053314791870190045 a**, P1
0,0050854663726125487, lower bound a preflop fissato 0,0037933522150657498.
Policy `fnv1a64:93dcdf50196772ae`, stesso albero `f5b432de223744cc`.
Artefatto `out/hu_goal/hu20_t4000_full.json`, 1.599,315 s con altri lavori
CPU attivi. Il calo rispetto a 2.000 e del 12,54%; gate ancora FAIL.
Continuazione a 8.000 in corso, stesso seed e algoritmo.

Controllo corrente/media sullo stesso campione di 64 flop: corrente peggiore
sia su HU40 a 10.000 (0,736903 contro 0,576612) sia su HU20 a 2.000
(0,145985 contro 0,113256). Nessun reset della media proposto. Sono stime,
con selezione preflop sul campione, non certificazioni integrali.

Il censimento completo conta 7.585/222.865/4.248.476 righe mantenendo classe
e tutti i bucket precedenti. Stato numerico completo HU40: 30.278.673.480 byte;
i soli regret e accumuli superano 12 GiB. Candidata `history7`: conserva tutti
i bucket flop e turn e raggruppa solo river sotto lo stesso genitore turn,
con massimo 7 figli scelto dal limite di memoria (8 richiede 12,08 GiB).
Supporto fisico integrale, centroidi river gia esistenti, pesi esatti;
inizializzazione deterministica e Lloyd pesato, senza dati del training.
Mappa `fnv1a64:3c9ee76ca6aad23b`, 7.585/222.865/1.539.270 righe,
52.829.432 byte. Stato HU40 previsto 11.552.641.608 byte. Artefatti
`out/hu_goal/history7.bin` e `history7.json`; costruzione e roundtrip 36,585 s.
La distanza aggiunta fra centroidi non e una misura di exploitability.

Oracolo integrato e regressioni PASS, 21.309.860 asserzioni: regret e media
contro FiniteGame, BR fisica contro gioco lossless e perfect recall verificato
per entrambi i giocatori sul gioco ridotto gerarchico. Profiling HU40 history7:
10 iterazioni, 94,716 s; refresh policy 73,027 s, discount 12,102 s,
preparazione board 3,041 s, traversate 6,547 s. Working set osservato massimo
11.805.118.464 byte, circa 11,0 GiB. Dati con altri run CPU attivi.
Si verifica ora il refresh delle sole righe del batch, mantenendo snapshot
prima di ogni passaggio e discount globale invariati; si richiede identita
bit per bit. Checkpoint ed export passano a I/O streaming per evitare copie
integrali extra, preservando formato e checksum. Nessun risultato del nuovo
training e ancora disponibile.

GPU rilevata con `nvidia-smi`: RTX 3050, 6 GiB totali, 4.968 MiB liberi al
controllo. Non introdotta: lo stato non entra integralmente in VRAM e il
profiling ha prima individuato lavoro CPU evitabile. La roadmap permette
un esperimento GPU con misure di velocita e qualita; non occorre una deroga.

### 2026-09-19 — nuovo limite di memoria: 12 GiB

Decisione esplicita dell'utente durante il goal HU20/HU30/HU40: il limite
precedente di 4 GiB non vale piu, il nuovo limite e 12 GiB. Il censimento
preliminare si era arrestato dopo 108 flop, con lower bound 4,02 GiB per
regret+accumuli HU20. Si completa ora il conteggio per distinguere gli schemi
che entrano realmente nel nuovo budget. Precisione float64 e gate 0,03 invariati.

### 2026-09-19 — mandato persistente HU20/HU30/HU40

L'utente assegna come unico goal il superamento dei tre giochi e chiede di
non fermarsi prima. Si continua oltre gli esiti intermedi inconcludenti,
registrandoli, senza cambiare gate, size o regole. Protocollo e stato in
[HU20_HU30_HU40_GOAL_2026-09-19.md](../../archive/history7-suite-2026-09/HU20_HU30_HU40_GOAL_2026-09-19.md).
Base `c319218`, worktree isolato invariato. HU40 corrisponde alla fixture
heads-up CO40 storica; le tre fixture di test sono congelate.

Class entra nel trainer ordinario come risorsa immutabile con identità:
oracolo indipendente e regressioni PASS (81,60 s). Estratta la corrente dal
checkpoint HU40 t=10.000 dopo checksum e uguaglianza di tutte le 16.332.395
celle della media con la policy salvata. In corso confronto corrente/media
sugli stessi 64 flop campionati, seed 20260919, nessun nuovo training HU40.

### 2026-09-19 — P9 — esito del confronto congiunto e delle traiettorie lunghe

Correzione dell'arresto MILP registrato sotto: i default HiGHS ammettevano un
errore incompatibile con la tolleranza dichiarata. Il controllo riproduce
8,903134e-7 ante di discordanza sulla BR lossless. Impostare solo il gap assoluto
a zero non basta; con fattibilita MIP 1e-9 e gap assoluto zero l'oracolo coincide.
La soglia di accettazione del report resta invariata; il caso ha un test Python
permanente. I vecchi risultati MILP sono superati dai file `*_strict_bounds.json`.

Risposte MILP finali chiuse numericamente: controllo 72/72, corpus mirato 71/72,
policy storica nel corpus ristretto 6/6, prolungamento 49/54. Sei ricerche restano
incomplete entro 30 s; i bound restano visibili. Il certificato fisico del
corpus resta un limite superiore indipendente alla deviazione rappresentabile.
Le risposte ripetute nel prolungamento non sono esperimenti indipendenti.

Nel corpus mirato a stack40, class campionata aveva massimo guadagno fisico
0,213964932 a a 2.500 iterazioni, di cui 0,198554035 a gia rappresentabili.
Proseguendo gli stessi tre seed fino a 100.000, il peggiore scende a
0,000046978 a; history a 0,000003344, lossless a 0,000031747. I nove primi
checkpoint riproducono esattamente le policy precedenti. La corrente ha
NashConv fisica zero in tutti i 27 checkpoint osservati. Si tratta di un
transitorio della media, non di un pavimento dimostrato. Il training di questa
prova e il riferimento Linear MCCFR su FiniteGame: non il trainer vettoriale
CO40 completo. Non promuoviamo history o un reset dell'averaging sulla sua base.

Il replay completo CO40 mantiene 0,5001806189608786 a. La nuova traccia segue
il preflop della stessa BR fisica: CO perde 0,135760481 a cambiando soltanto
quel preflop, poi guadagna 0,635941100 a cambiando il postflop. Contributi delle
rotte limp/check e limp/bet4/call: 0,461761958 e 0,174179142 a. Identita verificata,
non quota causale e non BR congiunta a bucket nel gioco completo.

Validazione finale: sette gruppi Release PASS (127,30 s); tre ASAN del nuovo
oracolo/riferimenti/toy PASS (80,43 s, precedenti alla traccia fisica); sei test
Python PASS (4,51 s), Black/Ruff PASS. Confronti lossless indipendenti verificano
la traccia e la metrica fisica; nessuna modifica alla policy storica. Report,
protocolli condizionali, dati e limiti in
[CONSTRAINED_BR_AUDIT_2026-09-19.md](../../archive/preflop-blueprint-research-2026-09/CONSTRAINED_BR_AUDIT_2026-09-19.md).
Esito sulla causa dominante: INCONCLUSIVE; il gate CO40 non e superato.

### 2026-09-19 — P9 — controllo MILP sul corpus mirato: arresto per discordanza

Il corpus mirato termina il training. La certificazione MILP passa class e
history a stack20, poi il confronto lossless/esatto a 25 iterazioni per BTN
non coincide entro la tolleranza: la procedura si ferma e conserva le prime
25 risposte nel file parziale. Prima di usare questi risultati per una
diagnosi si deve spiegare la discordanza fra MILP e BR C++ lossless. Non si
rilassa la soglia e non si presenta il confronto come completato.

### 2026-09-19 — P9 — best response globale vincolata: nuovo protocollo autorizzato

L'utente autorizza con «Procedi» il seguito della diagnosi. Base `9b9d427`.
Il [protocollo congiunto](../../archive/preflop-blueprint-research-2026-09/CONSTRAINED_BR_AUDIT_2026-09-19.md) registra dominio,
limiti, controlli analitici e confronto Short Deck prima del relativo training.

Errore riprodotto: la vecchia BR generica restituisce 0 su un gioco con memoria
imperfetta il cui ottimo globale enumerato e 1. Il controllo di perfect recall
ora rifiuta quel dominio. Due test del trainer falliscono perche chiamavano
quella API sulla partizione a bucket; i confronti di regret e strategy sum
passano. Correzione: oracolo fisico lossless con stessa policy sollevata,
controllo della conservazione di EV e rifiuto esplicito delle certificazioni
astratte non supportate. Le sei suite Release passano dopo la correzione
(75,70 s, trainer 71,17 s). Il certificatore fisico CO40 non e modificato.

L'enumerazione globale C++ passa i confronti analitici, Kuhn, valutatore
ricorsivo indipendente, DAG, scala, limiti e rifiuto dell'absent-mindedness.
Il protocollo analitico produce 108 righe: con memoria cancellata l'esatto
mantiene un guadagno rappresentabile di 0,75 dopo 100.000 iterazioni; il
campionato dipende dal seed. La partizione blind converge internamente ma
perde 1 rispetto al gioco informato. Sono meccanismi, non una diagnosi CO40.

Il conteggio delle politiche Short Deck ridotte supera 64 bit. Aggiunto un
diagnostico MILP locale con bound numerico, verificato contro otto BR C++
enumerate; cinque test Python passano. Primo tentativo FAIL nel solo report
del giocatore senza decisioni: SciPy non fornisce un MIP bound quando non ci
sono variabili intere. Gestito il valore unico noto direttamente. Nessun
risultato incompleto viene dichiarato ottimo. Ora in esecuzione il confronto
class/history/lossless sul corpus ridotto con stack 20/40 e sizing uguali.

### 2026-09-19 — P9 — chiusura dell'audit e verifiche finali

Completato il piano condizionale con esito INCONCLUSIVE sulla causa dominante.
La metrica temporale non supera il controllo decisionale: nessun candidato
promosso, nessuna nuova tabella o traiettoria di training. CO40 resta a 0,500181
ante e non supera il gate 0,03. Questa e una chiusura della diagnosi concordata,
non una dichiarazione di convergenza o impossibilita.

Commit: `17984a9` normalizzazione; `04ba03a` diagnostici e risultati. ASAN:
kernel, trainer, certificatore, export, decision-gap e distanza temporale PASS,
sei suite senza fallimenti, 3.123,45 s complessivi. Il toy e l'oracolo dei vicini
sono PASS in Release; il secondo copre anche input invalidi dopo la revisione.
Sette riepiloghi JSON validati, sorgenti formattati, diff controllato. Le durate
includono concorrenza e non sono benchmark di throughput. Checkout principale
pulito; nessuna scrittura sulle policy e risorse storiche.

L'[audit completo](../../archive/preflop-blueprint-research-2026-09/NASH_AUDIT_2026-09-19.md) contiene formule, comandi, pesi,
risultati, fallimenti e motivazione dell'arresto. L'eventuale protocollo seguente
deve misurare la deviazione congiunta vincolata ai bucket, partendo da un gioco
enumerabile; il presente audit non avvia ulteriori esperimenti ad hoc.

### 2026-09-19 — P9 — copertura completa e mancata promozione del candidato

Fatto: completati tutti i 573 flop ai nodi 4, 226, 448, 7.585 righe ciascuno,
con runout futuri enumerati. Tempo 3.307,9656516 s, comprensivo della concorrenza
con verifiche ASAN e uno screening HU20. Non e un benchmark di throughput isolato.
Errore di strategia comune e perdita di separazione restano entrambi presenti;
nessuno domina in tutti i nodi. I guadagni delle singole decisioni alla radice
sono piccoli rispetto alla BR globale. Dati in `CO40_FULL_BUCKET_AUDIT_2026-09-19.json`.

Il replay dei 573 flop certificati di class riproduce esattamente i valori
globali storici: guadagni CO/BTN 0,500181 / 0,194539. Con preflop dell'eroe
congelato: 0,180378 / 0,148785; con continuazione media congelata: 0,007291 /
0,003115. La differenza non e una decomposizione causale additiva. Corretto un
commento che dichiarava l'uguaglianza del lower bound e della BR libera con
copertura completa: resta il diverso vincolo sul preflop.

Il controllo delle feature trasferisce azioni fra flop differenti su 2.711
osservazioni supportate per nodo (29,7–34,0% della massa a seconda della vista).
La distanza temporale peggiora i nodi 4 e 226, migliora poco il 448, in entrambe
le viste. L'export riproduce esattamente le metriche dello screening precedente.
Oracoli di trasporto, trasferimento e toy PASS; le verifiche del trainer e del
certificatore sotto ASAN sono PASS, export ASAN ancora in corso alla registrazione.

Decisione: INCONCLUSIVE sulla causa dominante dopo l'unico ampliamento previsto;
nessuna evidenza sufficiente per promuovere la metrica temporale. Non si creano
bucket nuovi e non si lanciano i tre seed del candidato. Non e una bocciatura
di ogni possibile astrazione temporale. Non e stato ottenuto un miglioramento
Nash in questo audit; CO40 resta non qualificato. Nessun divieto documentale
impedisce una soluzione gia sostenuta dai dati: e il gate sperimentale a non
essere superato. Si completano documentazione e verifiche senza cambiare ipotesi
per cercare un risultato favorevole.

### 2026-09-19 — P9 — oracoli temporali e protocollo dei controlli

Fatto: l'assegnamento fra istogrammi condizionati supera 96 confronti con tutte
le permutazioni. Le feature di 152 stati fisici ricostruiscono esattamente le
marginali flop; 154/168 coppie hanno distanza temporale maggiore. Il conteggio
non misura un miglioramento decisionale, perche la distanza temporale domina
quella marginale per costruzione. Dati in `TEMPORAL_SCREEN_2026-09-19.json`.

Il toy con informazione rivelata prima/dopo la scelta di investimento ha valore
analitico S/8 nel gioco originale e zero con scelta iniziale condivisa. Linear
CFR a 8.000 iterazioni converge internamente, ma la policy condivisa perde
0,125 / 1,25 / 5 per S=1/10/40. La rappresentazione distinta perde meno di
7,421e-7 anche per S=40. Questo andamento lineare e imposto dai payoff del toy;
non e una previsione quantitativa sugli stack Short Deck. Test PASS, 1,14 s.
Fallimento precedente: fixture con terminali creati ma irraggiungibili, rifiutato
da `validate_finite_game`; CFR non era stato avviato. Corretto il costruttore
creando soltanto i terminali raggiungibili e rieseguito il test da nuova build.

Il controllo successivo delle feature trasferisce l'azione del vicino piu
prossimo nella stessa riga, escludendo l'intero flop della query. Si includono
tutte le osservazioni dei 16 flop ai nodi 4, 226, 448. Pareggi di distanza e di
azione sono mediati, copertura e pesi sono espliciti. L'oracolo con Q sintetici
verifica una perdita nota di 0,5 per entrambi i metodi, massa coperta 1,85/2 e
una riga senza vicini esclusa dal confronto: PASS. Nessun clustering nuovo.
La misura CO40 su tutti i 573 flop e ancora in corso; i nuovi eseguibili non
modificano il processo attivo. Prossimo passo: leggere l'ampliamento completo.

### 2026-09-19 — P9 — verifica delle feature sui conflitti osservati

Fatto: lo screening CO40 su 16 flop ha identificato stati fisici con preferenze
opposte dentro righe `class`. La verifica su tutti i flop dei nodi 4, 226, 448 e
in corso. Si prepara un controllo delle feature degli esempi, senza costruire
nuovi cluster: trasporto esatto fra i 31 istogrammi condizionati ai turn.
Il solver di assignment viene confrontato con enumerazione delle permutazioni
su casi piccoli; la marginale dei turn deve ricostruire l'istogramma flop esistente.
Fallimenti: prima build di `preflop_blueprint_temporal_witness.cpp`, C2039/C3861
su `combo_index`: manca `combinatorics.hpp`; gli errori di `std::copy` sono successivi
alla dichiarazione mancante. La verifica completa CO40 usa un eseguibile distinto
e continua. Nessun risultato del nuovo controllo e ancora dichiarato valido.
Prossimo passo: correggere l'include e completare i test prima di valutare le feature.

### 2026-09-19 — P9 — import verificabile delle policy class

Fatto: per diagnosticare le policy `class` salvate occorre ricostruire la mappa
storica (classe preflop, bucket corrente). Il codice relativo era in `out/`, con
sostituzione di object al link; viene portato in un oggetto immutabile esplicito,
con controllo di capacita e fingerprint. Nessuna modifica al training in questa fase.
Fallimenti: prima compilazione di `class_bucket_rows.cpp`, C2039 su `combo_table`:
manca `showdown_counts.hpp`. Registrato prima della correzione dell'include.
Gate: import class NOT_RUN, diagnostica dei conflitti NOT_RUN.
Prossimo passo: test di equivalenza e confronto con le valutazioni storiche salvate.

### 2026-09-19 — P9 — normalizzazione validata indipendentemente

Fatto: aggiunti probabilita dell'ingresso e guadagno condizionato opzionale;
campi storici e metriche globali invariati. Il fattore 630 della tabella storica
"Le quattro ipotesi cadute" e confermato per i range uniformi completi.
Le perdite corrette CO sono 1,172575 / 1,372247 / 1,421924 / 0,730602 ante
per ingresso sotto il prior del diagnostico. Non sono una decomposizione della root.

Comandi: CTest `windows-release` per certifier, trainer ed export; ricalcolo
CO40 dal checkpoint del certificatore su una copia locale dei 573 flop salvati.
Risultati: certifier PASS (138.974 assert, 42,46 s), trainer PASS (76,66 s),
export PASS (48,80 s). CO40 ricalcolato in 1,4004197 s, max_gain identico
0,82717651588212859. [Report e artefatto](../../archive/preflop-blueprint-research-2026-09/NASH_AUDIT_2026-09-19.md).
Fallimenti: l'include del nuovo test e stato corretto prima della build verificata;
il precedente CTest su eseguibile obsoleto non e contato in questi risultati.
Dubbi: rapporti condizionati per range ristretti o cataloghi parziali non validati,
quindi `conditional_gain` e `null` in quei casi. Nessuna nuova qualificazione CO40.
Prossimo passo: separare perdita della strategia comune e costo dell'aggregazione.

### 2026-09-19 — P9 — audit della normalizzazione e piano causale autorizzato

Fatto: l'utente ha assegnato come unico goal della giornata il piano in cinque fasi:
normalizzazione indipendente, diagnostica dei conflitti nei bucket, verifica enumerabile
del meccanismo, candidato potential-aware offline solo se sostenuto dai risultati,
confronti matched HU20/CO40. Worktree isolato `nash-convergence-audit/GTO-Solver`,
branch `codex/nash-convergence-audit`, base `744113c69342a82f3b920add498106af2b763d52`.
Il checkout principale e le risorse precalcolate restano invariati.

Risultati iniziali: il campo `opponent_reach` somma pesi su combo e non e una probabilita.
Il ricalcolo dei certificati indica un fattore 630 nella tabella del 2026-09-18
"Le quattro ipotesi cadute". La verifica indipendente con codice di test e ancora in corso.
Il rapporto condizionato sara esposto solo con catalogo completo e range uniformi completi;
per range ristretti la distribuzione dei board richiede una verifica separata.

Fallimenti: prima compilazione del nuovo test, MSVC C2039/C2065 sul simbolo
`ca::preflop_hand_classes`: manca l'include che lo dichiara. Il successivo CTest,
avviato prima di controllare l'esito della compilazione, usa il vecchio eseguibile:
il suo risultato non valida i nuovi test. Correggere l'include e ricompilare prima del nuovo CTest.

Gate: normalizzazione indipendente NOT_RUN; nuova astrazione NOT_RUN; nessuna nuova
qualificazione o affermazione di convergenza.
Prossimo passo: completare la compilazione e confrontare il diagnostico con l'enumerazione
indipendente delle coppie disgiunte.

Formato di ogni voce:

```text
### AAAA-MM-GG — Px — titolo breve
Fatto: ...
Comandi: ...
Risultati: numeri, tempi, memoria, fingerprint
Fallimenti: cosa, causa identificata o ipotesi, cosa si è provato
Dubbi: ...
Prossimo passo: ...
```

### 2026-09-19 - la capacita dei bucket non e la leva: tre assi esauriti, il divario e della famiglia

Fatto: costruite per la prima volta le tabelle a **500/1.000/2.000**, il secondo candidato che la
roadmap nomina ("confronto matched su 3 seed") e che non era mai stato eseguito - su disco esisteva
una sola cartella di tabelle. Misurato `class` su HU20, il bersaglio piu vicino al gate.

## La costruzione

41 minuti, stessi parametri di clustering del report P3 (10 riavvii, 10 iterazioni di screening, 25
massime, campione 500.000), stesso seed di partizione, stesse feature e stesse distanze: l'unica
variabile e il numero di gruppi.

| Street | Capacita | Distanza media dal centroide | vs 200/500/1.000 |
|---|---:|---:|---|
| Flop | 500 | 119,0 | era 150,7, **-21 %** |
| Turn | 1.000 | 6,12 | era 8,12, **-25 %** |

Il clustering e genuinamente piu fine, non solo piu numeroso. Nessun bucket vuoto; il turn ha
converso in 11 iterazioni invece di fermarsi al limite di 25.

## Il confondente, escluso prima della misura

Il rischio dichiarato era il sotto-allenamento: `classprev1` era esploso a 2,43 con 765.243 righe
di river. Le righe di `class` con la capacita nuova sono **58.160** al river (71.196 -> 99.904 in
totale), cioe **1,4x**, non 2,5x come la capacita grezza, perche `class` conta solo le coppie
(classe preflop, bucket) realizzate. `recall32` si era allenato bene a 184.528 righe con lo stesso
budget, quindi 58.160 e comodamente dentro la zona allenabile e l'esito misura l'astrazione.

## L'esito

| | 200/500/1.000 | 500/1.000/2.000 |
|---|---:|---:|
| HU20 con `class`, esatta | 0,060957 | **0,059870** |
| Guadagno | - | **1,8 %** |
| x il gate (0,03) | 2,03 | 2,00 |

Criterio fissato prima della misura: sotto 0,045 la capacita e una leva e si prova 1.000/2.000/4.000;
sopra 0,058 non lo e. **0,059870 sta sopra 0,058.** Refutata.

## Tre assi indipendenti, tutti esauriti

| Asse | Escursione provata | Resa migliore |
|---|---|---:|
| Memoria della chiave | 1.000 -> 765.243 righe river | +1,7 % (`recall32`) |
| Capacita dei bucket | 200/500/1.000 -> 500/1.000/2.000 | **+1,8 %** |
| Iterazioni | 1.000 -> 10.000 | banda del 7 %, minimo interno a 2.000 |

Ogni parametro della famiglia, spazzato in entrambe le direzioni, rende qualche punto percentuale.
HU20 richiede **2,00x**, CO40 **16,7x**. La regolarita e troppo consistente per essere casuale: il
divario e **strutturale alla famiglia di astrazione** - k-means su istogrammi di equity al flop e
al turn, OCHS al river, chiave a memoria imperfetta - e non a un suo parametro.

Fallimenti: nessuno nuovo. L'ipotesi era dichiarata con un criterio quantitativo prima della misura
e il criterio ha deciso contro di essa.
Dubbi: (1) Resta non provata l'unica famiglia alternativa: feature diverse. La stima e bassa
(l'EMD su istogrammi di equity e gia la scelta potential-aware standard, non una svista) e il costo
e circa due ore per variante. (2) La variante per percentile del river resta non provata. Contro un
divario di 2,00x su HU20 e un candidato piu serio di quanto fosse contro il 16,7x di CO40, ma il
bilancio su questa linea e cinque ipotesi su cinque cadute.
Prossimo passo: decisione dell'utente. Le misure non indicano piu un parametro da girare. Le
opzioni sono: investire in una famiglia di astrazione diversa (progetto di ricerca, non un
pomeriggio), accettare la frontiera a 10 ante e definire lo scopo del prodotto, o rivedere il gate.

### 2026-09-18 (notte) - la frontiera: `class` su HU20 e HU30, e il gate stretto all'1 % del piatto

Fatto: misurato `class` su HU20 e HU30, che erano stati certificati solo sulla baseline. Serviva a
stabilire fin dove il prodotto qualifica, invece di continuare a tentare ipotesi su CO40.
Certificazioni esatte, 605.088 board, 2000 iterazioni, stesso trainer e stesso certificatore che
hanno prodotto lo 0,500181 di CO40. Controllo passato: `class` riporta 7585/21638/41973 righe,
identiche a prima della variante `rankriver`, quindi i quattro numeri sono confrontabili.

| Gioco | Stack | baseline | `class` | Guadagno | % del piatto | **x gate (0,03)** |
|---|---:|---:|---:|---:|---:|---:|
| HU10 | 10 a | 0,003981 | - | - | 0,13 % | **passa** |
| HU20 | 20 a | 0,128849 | **0,060957** | 52,7 % | 2,03 % | 2,03x |
| HU30 | 30 a | 0,571773 | **0,324082** | 43,3 % | 10,80 % | 10,8x |
| CO40 | 40 a | 0,827177 | 0,500181 | 39,5 % | 16,67 % | 16,7x |

Il guadagno di `class` **decresce con la profondita**: 52,7 -> 43,3 -> 39,5 %. Monotono sui tre
giochi.

## Il gate e cambiato: D27

Durante la lettura delle soglie e emerso che D2 (0,1 a) era dichiarata "soglia fisica **iniziale**"
e "**da stringere** quando l'astrazione migliora", mentre l'1 % del piatto e lo standard con cui e
stato accettato il prodotto postflop. L'utente ha deciso il 2026-09-18 di stringere il gate a
**0,03 a, l'1 % del piatto** (D27). La colonna di destra della tabella e contro quel gate.

## Verdetto del criterio fissato in anticipo

Prima di vedere HU30 era stato scritto: sotto ~0,15 un ulteriore 2x lo porta sotto 0,1 e il lavoro
sull'astrazione si ripaga; sopra ~0,3 nemmeno un raddoppio basta. **0,324082 sta sopra 0,3.** Un 2x
lascerebbe HU30 a 0,162, che falliva perfino il vecchio D2, e contro il gate nuovo servono 10,8x.

## La frontiera

La frontiera del prodotto sta **fra 10 e 20 ante**, e HU20 e l'unico bersaglio raggiungibile:
manca il gate di **2,03x**, che e la taglia di un salto gia ottenuto una volta sullo stesso gioco -
da baseline a `class` sono 2,11x. HU30 (10,8x) e CO40 (16,7x) chiedono piu di quanto qualunque
cambio di rappresentazione abbia mai reso in questo programma.

Fallimenti: (1) Previsto HU30 a 0,271 estrapolando il guadagno di HU20; il valore vero e 0,324
perche il guadagno di `class` decresce con la profondita, cosa che i tre punti mostrano
chiaramente e che una previsione a un punto solo ignorava.
Dubbi: (1) HU20 e certificato sulla **fixture di test**: una sola size postflop (100 % del piatto).
`hu20_full_v1` con le tre size non e mai stata costruita ne misurata. Il precedente di HU10
(ridotto 0,003981, completo 0,0039949, +0,4 %) e evidenza debole, perche a SPR 0,42 le size non
possono contare mentre a SPR 1,25 si. (2) Le 2000 iterazioni sono il minimo della curva **di
CO40**; la curva di HU20 non e mai stata misurata, e HU20 ha piu campioni per riga a parita di
iterazioni, quindi il suo minimo potrebbe cadere piu avanti. Parte del 2,03x potrebbe venire dal
solo punto sull'asse delle iterazioni.
Prossimo passo: misurare la curva iterazioni/exploitability di HU20 (un training con dump ai
raddoppi piu quattro certificazioni esatte, circa un'ora) e costruire `hu20_full_v1`. Sono le due
cose che non richiedono di indovinare un meccanismo.

### 2026-09-18 (notte, correzione) - D3 non e un gate: e la regola di arresto del training

Rilevato dall'utente. D2 e D3 sono stati usati come se fossero due soglie di accettazione. Non lo
sono, e la roadmap li distingue esplicitamente (sezione 2.1):

| | Definizione nella roadmap | Cosa e |
|---|---|---|
| **D2** | "Soglia fisica: massimo guadagno per giocatore <= 0,1 ante per mano" | **gate di accettazione** |
| **D3** | "Arresto del training: massimo guadagno <= 1 % del pot iniziale, stimato sui board campionati" | **regola di arresto**, implementata come `stima + semiampiezza <= 0,03a` |

D3 e ancorata al piatto e indipendente dallo stack, che e il modo standard di esprimere la
convergenza Nash. Non dice se un risultato sia accettabile: dice quando smettere di iterare.

## Cosa va corretto nelle voci precedenti

Le voci restano, come impone la regola del diario. Correzioni:

1. **"16,7 volte D3" come misura del fallimento** (voci del 2026-09-17 e del 2026-09-18 sera e
   notte, e la tabella della matrice con la colonna `x D3`). Il fallimento si misura contro **D2**:
   `class` a 0,500181 e **5,0 volte** il gate. Il rapporto con 0,03 resta un numero vero ma non e
   un verdetto.
2. **"Rivedere la soglia D3 per gli stack profondi"**, proposta come una delle due strade rimaste
   in tre voci. Proposta priva di senso: allentare una regola di arresto fa smettere di allenare
   prima, non fa qualificare niente. Ritirata. Le strade sono: lavorare sull'astrazione, rivedere
   **D2**, o restringere lo scopo del prodotto.
3. **"D3 si irrigidisce di quattro volte con la profondita"** (voce del 2026-09-18 sera, ripetuta
   all'utente). Il conto era 0,03 a rapportato allo **stack** - 0,3 % a 10 ante contro 0,075 % a 40.
   Ma il criterio e rapportato al **piatto**, che in questa struttura vale sempre 3 ante, quindi non
   si irrigidisce niente. Problema inventato misurando contro un metro che il progetto non usa.

## Posizione corretta, contro D2

| Gioco | Miglior misura esatta | D2 = 0,1 a | Fattore |
|---|---:|---|---:|
| HU10 | 0,003981 | **passa** | 25x sotto |
| HU20 | 0,128849 (baseline) | fallisce | 1,29x |
| HU30 | 0,571773 (baseline) | fallisce | 5,7x |
| CO40 | 0,500181 (`class`) | fallisce | **5,0x** |

HU20 manca il gate del 29 % sulla sola baseline, quindi `class` puo portarlo sotto: e la misura in
corso.

## Discrepanza fra regola scritta e codice

La roadmap dice che D3 va valutata "**nel gioco astratto**". `Trainer::meets_stop_rule` e applicata
al risultato di `estimate_exploitability`, che e il valutatore **fisico** su flop campionati.

Conseguenza concreta: la curva del 2026-09-18 sera mostra che l'exploitability fisica di CO40 non
si avvicina mai a 0,03 e risale dopo 2.000 iterazioni, quindi la regola di arresto implementata
**non puo scattare** su questo gioco. Tutti i log di training di oggi finiscono con
`PREFLOP_BLUEPRINT_TRAIN=ITERATION_LIMIT`, nessuno con `CONVERGED`. Se fosse applicata come
scritta, D3 sarebbe esattamente la misura che il 2026-09-18 si e tentato di costruire con il
regret residuo e poi abbandonata.

Fallimenti: (1) Due soglie con nomi simili usate come sinonimi per un'intera giornata, senza mai
aprire la definizione. (2) Su quella confusione e stata costruita e proposta all'utente una strada
d'azione inesistente ("rivedere D3"), e un problema inventato sulla rigidita della soglia con la
profondita. Nessuno dei due errori sarebbe sopravvissuto alla lettura di una riga di roadmap.
Dubbi: (1) La discrepanza fra D3 scritta ("nel gioco astratto") e implementata (fisica campionata)
non e risolta: non e chiaro se sia una scelta deliberata mai annotata o una deriva. Serve una
decisione dell'utente, perche D3 e fra le soglie non modificabili senza di essa (roadmap 2.1).
Prossimo passo: invariato, misurare `class` su HU20 e HU30 per stabilire la frontiera contro D2.

### 2026-09-18 (notte) - P9 - il river per rango relativo al board fallisce, e spiega cosa faceva il clustering

Fatto: censita l'occupazione dei bucket per board su tutti i board canonici, e provata una variante
`rankriver` che al river usa la posizione di rango della mano sul proprio board invece dell'indice
del cluster globale. Refutata: 0,986028 contro 0,500181 di `class`, peggio anche della baseline.

## Il censimento (misura valida, resta)

| Street | Capacita | Bucket occupati per board (mediana) | Quota usata | Mani per bucket, pesate |
|---|---:|---:|---:|---:|
| Flop | 200 | 50 | 24,5 % | 19,3 |
| Turn | 500 | 47 | 9,4 % | 20,7 |
| River | 1000 | 18 | 1,8 % | 53,0 |

Al river una mano e giocata identica ad altre 53 sullo stesso board, e il 98,2 % della capacita non
viene toccata. Entrambi i numeri sono esatti, su tutti i 19.998 river canonici.

## L'esperimento e il suo esito

Tenuto fisso tutto tranne la chiave del river: stesso albero, stesse fixture, stesse tabelle di
flop e turn, stesso schema, 2000 iterazioni, stesso batch e thread.

| | base | `class` | `rankriver` |
|---|---:|---:|---:|
| Exploitability esatta | 0,827177 | **0,500181** | **0,986028** |
| NashConv | - | 0,694720 | 1,471640 |
| EV di CO | - | -0,146476 | -0,137628 |
| Righe river | 1.000 | 41.973 | 37.665 |
| Stato | - | 392 MB | 362 MB |
| Secondi per iterazione | - | 0,432 | 1,206 (**2,79x**) |

## Il difetto, che e nel codice della variante

Assegnato l'indice **ordinale** del gruppo di rango (0 per il piu debole, poi 1, 2, ...) invece del
percentile. Il numero di gruppi distinti varia da 1 (board con scala servita, tutti pareggiano) a
circa 465. I nuts finiscono quindi sull'indice 17 su un board e sul 299 su un altro, e la stessa
riga di strategia serve i nuts su uno e una mano mediocre su un altro. La chiave e cieca al board,
quindi l'errore non e correggibile a valle.

## Cosa faceva il clustering, e che era stato letto al contrario

La rietichettatura per forza crescente e fatta **su tutti i board insieme**: per questo il bucket
999 e i nuts su board di texture opposta (verificato su tre). Quell'allineamento - stesso indice,
stessa forza assoluta, ovunque - e cio che rende usabile una chiave che non vede il board. La
capacita non toccata su un singolo board ne e il prezzo, non uno spreco.

Il 2,79x conferma l'altra faccia: con il river raggruppato le 465 mani vive di un board stanno in
una ventina di righe adiacenti e il kernel vettoriale legge poche linee di cache; per rango stanno
in fino a 465 righe sparse. La compressione comprava anche velocita.

Fallimenti: (1) Quinta ipotesi caduta, e la prima costruita su una misura invece che su
un'intuizione: il censimento era corretto, l'interpretazione no. (2) Strumento del regret residuo
scritto e poi abbandonato: il bound assoluto somma rumore una volta per riga e scala col numero di
righe (HU10, gioco risolto a 0,003981 fisico, riporta 0,0553 con tutte le 23.924 righe positive).
Prima della calibrazione stavo per puntarlo su CO40, dove avrebbe dato un numero grande e la
conclusione opposta a quella giusta. (3) La misura e stata proposta come decisiva senza verificare
che il suo esito cambiasse una decisione: la curva iterazioni/exploitability gia mostrava che
convergere di piu peggiora, quindi nessuno dei due esiti avrebbe cambiato il seguito. Fermata su
richiesta dell'utente. (4) Stima di 14 minuti per il training di `rankriver` presa da `class` senza
chiedersi se il costo per iterazione fosse lo stesso: sono stati 40. (5) Criterio del censimento
enunciato al contrario ("sotto la cinquantina di bucket occupati la compressione e trascurabile":
pochi bucket occupati significano piu compressione, non meno).
Dubbi: (1) La variante per **percentile** - indice = quota di mani battute, scalata alla capacita -
non e stata provata, ed e diversa da quella refutata: allinea i nuts sull'indice massimo di ogni
board e usa tutti gli indici ovunque. Il difetto identificato e specifico, ma il bilancio di
giornata su questa linea di ragionamento e cinque ipotesi su cinque. (2) Il costo di localita del
2,79x colpirebbe anche la variante per percentile, quindi anche riuscendo andrebbe pesato.
Prossimo passo: decisione dell'utente. La variante per percentile costa circa un'ora (10 minuti di
modifica, 40 di training, 25 di certificazione). In alternativa restano le due strade gia sul
tavolo: cambiare le feature del river, o rivedere la soglia D3 per gli stack profondi.

### 2026-09-18 (sera) - P9 - la curva exploitability/iterazioni ha un minimo interno: l'astrazione ha un pavimento

Fatto: misurata l'exploitability fisica **esatta** della stessa traiettoria a 1.000, 2.000, 4.000,
8.000 e 10.000 iterazioni. Nessun campionamento: 573 flop canonici per tutti i runout, 605.088
board, `max_gain_half_width` = 0 su tutti e cinque i punti. Le differenze non sono rumore.

| Iterazioni | Exploitability CO | BTN | NashConv | EV di CO |
|---:|---:|---:|---:|---:|
| 1.000 | 0,529728 | 0,204116 | 0,733843 | -0,149135 |
| 2.000 | **0,500181** | **0,194539** | **0,694720** | -0,146476 |
| 4.000 | 0,502202 | 0,201370 | 0,703572 | -0,145398 |
| 8.000 | 0,528748 | 0,211783 | 0,740532 | -0,144610 |
| 10.000 | 0,535882 | 0,217001 | 0,752882 | -0,144386 |

Due andamenti opposti sulla **stessa traiettoria**, ed e il risultato centrale:

- l'**EV migliora monotonicamente** su tutti e cinque i punti, senza mai invertire;
- l'**exploitability fisica ha un minimo interno** a 2.000-4.000 e poi peggiora monotonicamente.

CFR sta funzionando: converge nel proprio gioco astratto, e il valore lo dimostra. La soluzione di
quel gioco pero non e la soluzione del gioco fisico, e avvicinarsi alla prima allontana dalla
seconda. E la patologia dell'astrazione, mostrata qui **sull'asse delle iterazioni dentro un solo
run**, non piu confrontando rappresentazioni diverse.

Il pavimento e **0,500181**: cinque volte la soglia D2 (0,1) e 16,7 volte la D3 (0,03). Il bacino
del minimo e largo e piatto (2.000 e 4.000 distano lo 0,4 %), quindi non esiste un budget di
iterazioni da cercare meglio: nessun punto della curva si avvicina al gate. Entrambi i giocatori
peggiorano insieme dopo il minimo (CO 0,5002 -> 0,5359, BTN 0,1945 -> 0,2170), coerente con la
simmetria dell'astrazione gia registrata.

## Il test della deriva, e perche non decideva

Prima della curva, la domanda era se la media avesse converso. Salvate le policy ai quattro
raddoppi (determinismo verificato: la policy a 2.000 ha fingerprint `fnv1a64:be84f6b45d37b5b8`,
identico bit per bit a quella del run del 2026-09-17, due run indipendenti).

| Finestra | Tabella intera | Solo entries gia vive | **Root** |
|---|---:|---:|---:|
| 1.000 -> 2.000 | 0,053143 | 0,050801 | 0,012486 |
| 2.000 -> 4.000 | 0,046537 (x0,876) | 0,044507 (x0,876) | 0,007617 (**x0,610**) |
| 4.000 -> 8.000 | 0,038546 (x0,828) | 0,037603 (x0,845) | 0,005636 (**x0,740**) |

Il test dava risposte **opposte a seconda del peso**. Sulla tabella intera la deriva decade a
0,85 per raddoppio, piu lentamente dello 0,707 che avrebbe il puro rumore campionario attorno a un
punto fisso: sembra non convergere. Sul root decade a 0,61 e 0,74, cioe attorno o sotto quel
riferimento, e con ampiezza **quattro volte minore**: converge. La tabella pesa allo stesso modo
tutte le 16,3 M entries, quindi il suo numero e dominato da bucket di river quasi mai raggiunti.

Confondente esaminato e escluso: una entry mai visitata vale esattamente `1/actions`
(`trainer.cpp:844`), quindi la prima visita produce deriva che non parla di equilibrio. Vale il
10,8 %, 7,6 % e 3,8 % del totale nelle tre finestre, e toglierla non cambia il tasso di decadimento
(0,876 e 0,845 contro 0,876 e 0,828). Nota di validazione dello strumento: la quota di entries
ancora uniformi **si dimezza** a ogni raddoppio (6,65 -> 3,41 -> 1,41 %), quindi la misura rileva
un dimezzamento quando c'e; semplicemente non lo trova nel movimento della strategia.

## Difetto trovato nel driver di benchmark

`--checkpoint` e silenziosamente un no-op quando `--eval-every 0`: il salvataggio sta dentro il
ramo che scatta solo dopo una valutazione (`benchmarks/preflop_blueprint_train.cpp:291`). Il
`--resume` non ha quindi trovato nulla e i quattro passi si sono riallenati da zero, 15.000
iterazioni invece di 8.000. Non corretto: tocca un driver del prodotto e la decisione e dell'utente.

Fallimenti: (1) Il test della deriva e stato proposto come decisivo e non lo era: pesa a peso
uniforme entries di rilevanza diversissima, e la radice - l'unico blocco il cui offset si conosce
senza mappa dei nodi - si muoveva poco, cosa **gia misurata** prima di proporlo. La conclusione
"la media non si sta assestando", data all'utente a meta pomeriggio, e stata corretta poche ore
dopo dalla misura sul root. (2) Mezz'ora di calcolo persa per il no-op del checkpoint, non
verificato prima di lanciare la sequenza.
Dubbi: (1) Resta non misurata l'exploitability **dentro l'astrazione**: servirebbe una best
response ristretta ai bucket, che il certificatore non fa. Non e piu rilevante per la decisione -
il pavimento vale 0,50 al minimo e il limite a t->infinito e peggiore - ma il "CFR converge nel suo
gioco" resta un'inferenza da EV monotona e deriva del root, non una misura diretta. (2) Il root e
un nodo su 604 e i suoi rapporti sono due su un blocco di 324 slot: c'e spazio per il rumore. (3)
Il termine di interazione vale il 65 % dell'exploitability, quindi "il root converge" non implica
"converge dove conta".
Prossimo passo: **decisione dell'utente**. Il capitolo convergenza e chiuso: nessun budget di
iterazioni qualifica CO40 sotto questa astrazione. Le alternative sono cambiare famiglia di
feature (mai misurata) oppure rivedere la soglia D3 per gli stack profondi.

### 2026-09-18 — P9 — sweep sulla profondita, decomposizione della perdita, e quattro ipotesi cadute

Fatto: su richiesta dell'utente, verificato se il problema di CO40 si presenti anche a stack piu
bassi, e poi cercato il meccanismo. Il risultato utile e la sequenza di ipotesi falsificate: sono
state generate tutte prima di avere le misure giuste, e tutte e quattro sono cadute contro misure
che si potevano fare prima.

## Fixture nuove

`preflop_blueprint_hu20_test_v1.json` e `preflop_blueprint_hu30_test_v1.json` condividono
ruleset, range, open a 5 ante e astrazione postflop della variante di test CO40, ma non hanno lo
stesso albero preflop. Aggiunte al test di validazione dello schema. Su indicazione dell'utente
HU20 ha `response_target_units: []` come HU10: a 20 ante un 3bet a 17 a
lascia 3 a dietro in un piatto da 36 e non e distinguibile dallo shove a 19 a. Il suo albero ha
quindi 571 nodi e la stessa parte preflop di HU10 (22 nodi, 8 decisioni, 3 ingressi).

Vincolo del modello trovato per strada: l'all-in preflop e soggetto a `all_in_threshold`, il
1000 % del piatto dopo il call. Alla radice il piatto e 4 a, quindi lo shove sparisce dall'albero
sopra i **41 ante** (verificato: presente a 40 e 41, assente a 42 e 45). La finestra in cui questa
struttura e confrontabile e **18-41 ante**: sotto i 18 la risposta a 17 a supera lo stack e il
loader rifiuta la fixture, sopra i 41 l'albero cambia forma.

## Lo sweep

| Gioco | Stack | SPR dopo open+call | Nodi | **Max gain esatto** | Open di CO |
|---|---:|---:|---:|---:|---:|
| HU10 ridotto | 10 a | 0,42 | 193 | 0,003980549728196586 | 44,9 % |
| HU20 | 20 a | 1,25 | 571 | 0,12884900000000000 | 6,5 % |
| HU30 | 30 a | 2,08 | 604 | 0,57177300000000000 | 2,5 % |
| CO40 | 40 a | 2,92 | 604 | 0,82717651588212859 | 1,6 % |

L'exploitability e concentrata su CO: a HU20 il rapporto CO/BTN e **11,6 a 1**.

## Strumenti di misura aggiunti al prodotto

Tre aggiunte additive, nessuna tocca trainer, albero, policy o fingerprint. Dopo ognuna, suite
21/21 e regressione HU10 che riproduce `0.003980549728196586` alla cifra.

1. `best_response_preflop` / `gain_preflop`: la quarta casella del 2x2 che il codice gia
   calcolava per tre quarti. Deviazione **solo preflop**, con il postflop tenuto a quello del
   blueprint. Invariante nel test: `ev <= best_response_preflop <= best_response`.
2. `best_response_preflop_mix`: la strategia preflop **scelta dalla best response**, per ogni
   nodo decisionale dell'eroe, aggregata sulle 81 classi. Il vettore `choice` esisteva gia e
   veniva buttato via. `split_classes` verifica che le combo della stessa classe scelgano la
   stessa azione, come impone la simmetria dei semi: esce zero ovunque.
3. `postflop_entry_loss`: la perdita dentro ogni ingresso postflop **a reach fissato**, media a
   peso uniforme sulle combo vive invece che pesata col reach del blueprint. Serve perche
   `gain_lower` e cieco proprio dove il blueprint non va: un blueprint che evita un sottoalbero
   sembra giocarlo bene.

## Le misure

**Decomposizione in tre.** Nessuno dei due livelli e sbagliato da solo:

| Gioco | Totale | Solo preflop | Solo postflop | Interazione |
|---|---:|---:|---:|---:|
| HU20 | 0,128849 | 0,002747 | 0,008327 | 0,117776 (**91 %**) |
| HU30 | 0,571773 | 0,005802 | 0,130850 | 0,435121 (**76 %**) |
| CO40 | 0,827177 | 0,006229 | 0,282118 | 0,538829 (**65 %**) |

Correggere un livello solo recupera fra il 9 e il 35 %. Il blueprint e in un **ottimo locale**:
ogni pezzo e ottimale dati gli altri.

**La best response di CO su CO40, per nodo:**

| Nodo | Blueprint | Best response |
|---|---|---|
| radice | fold 27,4 · limp 35,8 · open 1,6 · shove 35,1 | **limp 100** |
| ha limpato, BTN punta a 5 | fold 34,8 · call 39,3 · shove 25,9 | fold 3,7 · **call 91,4** · shove 4,9 |
| ha limpato, BTN spinge | fold 70,3 · call 29,7 | fold 70,4 · call 29,6 |
| ha aperto, BTN 3betta | fold 60,7 · call 19,5 · shove 19,9 | fold 51,9 · call 46,9 · shove 1,2 |
| ha aperto, BTN spinge | fold 64,3 · call 35,7 | fold 64,2 · call 35,8 |

Dove la decisione e fold-o-call contro uno shove i due coincidono alla prima cifra: e una
decisione di sola equity, senza postflop dentro, e il blueprint la prende bene. Dove invece si
tratta di entrare in un piatto giocabile, divergono. La best response **non apre mai**: prende
flop economici con tutto. Attenzione, e uno **sfruttamento** di un BTN congelato che dopo il limp
checka il 63,7 % e non punisce mai, non una strategia di equilibrio.

**Perdita postflop a reach fissato su CO40**, per mano (diviso per il reach avversario, che e una
normalizzazione dell'analisi e non un'unita nativa del certificato):

| Ingresso | Piatto | CO | BTN |
|---|---:|---:|---:|
| limp-check | 4 | 0,001861 | 0,001797 |
| limp-bet-call | 12 | 0,002178 | 0,001818 |
| open-call | 12 | 0,002257 | 0,001800 |
| open-3bet-call | 36 | 0,001160 | 0,001337 |

HU10 per confronto: 0,000559 / 0,000087 / 0,000038.

## Le quattro ipotesi cadute

1. **L'astrazione postflop corrompe i valori preflop.** Refutata: `class` migliora
   l'exploitability del 39,5 % e la strategia preflop non si muove di un decimale
   (limp 35,8 -> 35,4 %, shove 35,1 -> 35,0 %).
2. **Lo shove cresce con la profondita.** Refutata: decresce, 59,9 -> 45,7 -> 35,1 %.
3. **La strategia media e incoerente con i propri EV.** Non supportata: solo 4 discordanze su 15
   superano un errore standard.
4. **Il postflop e giocato male, percio CO evita di entrarci.** Refutata quantitativamente: la
   perdita postflop e **piatta** fra gli ingressi (l'ingresso piu profondo e quello dove si perde
   meno), **uguale per i due giocatori** benche le loro exploitability differiscano di 2,5 volte,
   e vale circa 0,002 ante per mano contro divari di EV fra azioni preflop di 0,24. Due ordini di
   grandezza di distanza.

Perche 1 sembrava reggere e non reggeva: il valore del gioco per CO e **quasi invariante** rispetto
alla rappresentazione. Su otto rappresentazioni l'exploitability si muove fra 0,12 e 1,80 mentre
l'EV di CO si muove fra 0,00009 e 0,08; `class` cambia l'exploitability di 0,327 e l'EV di
0,00058, un rapporto di 1 a 564. L'astrazione e **simmetrica**: peggiora entrambi i giocatori, e
due giocatori handicappati uguale raggiungono all'incirca il valore giusto. Costa pochissimo in
valore e moltissimo in exploitability, e CFR ottimizza il valore.
Fallimenti: (1) Quattro ipotesi formulate prima di avere le misure che le avrebbero decise.
(2) Una frase scritta nel riassunto all'utente — "il blueprint non sa giocare a poker" — che
assumeva la qualita del postflop senza averla misurata, e che la misura successiva ha smentito.
(3) Un run HU20 scartato perche girava sulla fixture con il 3bet degenere. (4) Percentuali di BTN
citate da HU20 mentre si discuteva CO40.
Dubbi: (1) Non esiste un meccanismo che leghi i fatti sopravvissuti. Non ne viene proposto un
quinto. (2) Non esiste un riferimento esterno per CO40 sul nuovo albero, quindi "limpare il 36 %"
e giudicato assurdo senza uno standard. (3) La normalizzazione per reach avversario nella tabella
degli ingressi e una costruzione dell'analisi; i confronti robusti sono quelli interni allo stesso
certificato, cioe la piattezza fra ingressi e l'uguaglianza fra i due giocatori.
Prossimo passo: misurare se CFR abbia converso **nel proprio gioco astratto**, cosa mai fatta.
Tutte le misure di questo programma sono exploitability **fisiche**; se il regret medio residuo
fosse alto, il blueprint non sarebbe un equilibrio nemmeno della propria astrazione e tutto il
resto sarebbe a valle di quello.

### 2026-09-17 — P9 — il braccio Linear refuta l'ipotesi del discount: il muro e della rappresentazione

Fatto: ultimo esperimento della matrice. P9 registrava come aperta l'ipotesi che il discount DCFR
fosse responsabile del degrado sulle righe rare: con beta zero un regret negativo si dimezza a
ogni iterazione **globale**, anche quando la riga non compare nel batch, e venti iterazioni di
assenza lo riducono di un fattore un milione. Con 765.243 righe river e 64.000 board in 2.000
iterazioni quasi ogni riga e rara, quindi l'ipotesi prevedeva che Linear — che pesa gli incrementi
dell'iterazione t per t e non sconta i regret memorizzati — salvasse `classprev1`.

Cambiata **solo** la pesatura: update alternati, stesso albero, stesso protocollo, stessa passata
esatta. Max gain esatto su 573 flop canonici e 605.088 board:

| Rappresentazione | Righe river | DCFR | Linear | Rapporto |
|---|---:|---:|---:|---:|
| `class` | 41.973 | 0,50018061896087860 | 0,66222655337014890 | 1,324 |
| `classprev1` | 765.243 | 2,43078387921045060 | 2,62717999011574980 | 1,081 |

**L'ipotesi e refutata.** Linear non avvicina `classprev1` a `class`: resta a 2,63 contro 2,43,
cioe leggermente **peggiore**, e cinque volte peggio di `class` con entrambi gli schemi. Se il
discount fosse stato la causa, il rapporto Linear/DCFR sarebbe dovuto crollare sulla
rappresentazione fine; invece passa da 1,324 a 1,081.

Onesta sul residuo: quel calo del rapporto va nella direzione prevista dall'ipotesi — Linear e
relativamente meno penalizzato dove le righe sono rare. Ma e un effetto del 20 % su un divario di
cinque volte: esiste e non spiega il fenomeno. **Il muro e della rappresentazione**: con 64.000
board non si allenano 765.000 righe, e nessuno schema di pesatura lo compensa. Questo chiude
l'ipotesi aperta di P9 e spiega retroattivamente `recall_full`, che con 4.248.476 righe falliva
per lo stesso motivo e non per la precisione float32 o per il formato dello stato.

## Tabella finale della matrice

Nove rappresentazioni, stesso albero `fnv1a64:18d08f453034ac0f`, stesso protocollo
(2.000 iterazioni, batch 32, 8 thread, update alternati), stessa passata esatta.

| Rappresentazione | Righe F/T/R | Stato test | Stato `co40_v1` | **Max gain esatto** | % piatto | x D3 |
|---|---|---:|---:|---:|---:|---:|
| `base` (produzione) | 200/500/1.000 | 9 MB | 514 MB | 0,82717651588212859 | 27,6 % | 27,6 |
| `classf` | 7.585/500/1.000 | 22 MB | 663 MB | 0,70787939444806370 | 23,6 % | 23,6 |
| `class` @ 50/100/200 | 3.210/7.139/14.340 | 128 MB | 7,20 GB | 0,59405448398659820 | 19,8 % | 19,8 |
| `classft` | 7.585/21.638/1.000 | 104 MB | 3,19 GB | 0,58624021099899270 | 19,5 % | 19,5 |
| **`class`** | 7.585/21.638/41.973 | 374 MB | 21,13 GB | **0,50018061896087860** | 16,7 % | **16,7** |
| `class` @ 10.000 it. | idem | 374 MB | 21,13 GB | 0,53588168707754600 | 17,9 % | 17,9 |
| `class`, Linear | idem | 374 MB | 21,13 GB | 0,66222655337014890 | 22,1 % | 22,1 |
| `classprev1` | 7.585/222.865/765.243 | 5,78 GB | 361,99 GB | 2,43078387921045060 | 81,0 % | 81,0 |
| `classprev1`, Linear | idem | 5,78 GB | 361,99 GB | 2,62717999011574980 | 87,6 % | 87,6 |

Generata da `out/matrix/summary.py` leggendo i certificati.

## Conclusione

1. **Il trainer non e il collo di bottiglia.** Con perfect recall il CFR scende a 0,000011 a sul
   gioco ridotto; con la chiave di produzione si ferma a 0,027933 a. Il pavimento e l'astrazione.
2. **Dentro l'astrazione, la leva e la memoria, non la risoluzione.** Ricordare la classe preflop
   vale -39,5 % sul gioco vero; quadruplicare i bucket vale il 4,7 % e dimezzarli costa il 19 %.
3. **La memoria satura e poi collassa.** Ogni street che ricorda la classe compra una fetta simile
   (-0,119 flop, -0,122 turn, -0,086 river), ma oltre le circa 42.000 righe river la
   rappresentazione non e piu allenabile con questo budget di board e peggiora di cinque volte.
4. **Non e l'algoritmo.** Ne DCFR ne Linear cambiano il quadro; allenare cinque volte tanto
   peggiora del 7,1 %.
5. **Nessuna configurazione della famiglia si avvicina all'obiettivo.** Il campo va da 0,500 a
   2,627 contro una soglia D3 di 0,03 a. L'ottimo e `class` a **16,7 volte D3** e 5 volte D2.

Il vincolo di memoria posto dall'utente restringe ulteriormente: `class` costa 21,13 GB su
`co40_v1`, quindi la configurazione migliore della matrice non e nemmeno deployabile sul bersaglio
finale. Il miglior compromesso deployabile e `classft`, 0,586240 a a 3,19 GB.
Fallimenti: nessuno nuovo.
Dubbi: (1) La saturazione fra 42.000 e 765.000 righe river e stata osservata a 2.000 iterazioni e
64.000 board; non e noto dove si sposti aumentando i board per iterazione invece delle iterazioni,
che e l'unica variabile del campionamento non ancora toccata. (2) La matrice esplora una sola
famiglia: bucket di carte piu classe preflop. Feature diverse (equity contro range, potential-aware
al turn) restano non misurate ed erano l'opzione D del piano, mai avviata.
Prossimo passo: decisione dell'utente fra cambiare famiglia di astrazione e rivedere la soglia D3
per gli stack profondi; nessuna delle due e una decisione dell'agent.

### 2026-09-17 — P9 — matrice delle rappresentazioni: il muro dell'allenabilita fra 42.000 e 765.000 righe

Fatto: matrice di rappresentazioni postflop su `co40_test_v1`, tutte con lo stesso albero
`fnv1a64:18d08f453034ac0f`, lo stesso protocollo (DCFR alternato, 2.000 iterazioni, batch 32,
8 thread) e la stessa passata **esatta** su 573 flop canonici e 605.088 board. È la prima tabella
del programma in cui le rappresentazioni sono confrontabili fra loro. Vincolo posto dall'utente:
la rappresentazione deve stare nei 32 GB della macchina.

| Rappresentazione | Riga postflop | Righe F/T/R | Stato su `co40_test_v1` | **Max gain esatto** | Su `co40_v1` |
|---|---|---|---:|---:|---:|
| `base` | bucket corrente | 200/500/1.000 | 9,4 MB | **0,82717651588212859** | 539 MB |
| `classf` | classe al flop | 7.585/500/1.000 | 23,5 MB | **0,70787939444806370** | 696 MB |
| `class` @ 50/100/200 | classe ovunque, tabelle grossolane | 3.210/7.139/14.340 | 134 MB | **0,59405448398659820** | 7,73 GB |
| `classft` | classe a flop e turn | 7.585/21.638/1.000 | 109 MB | **0,58624021099899270** | 3,42 GB |
| **`class`** | classe ovunque | 7.585/21.638/41.973 | 392 MB | **0,50018061896087860** | 22,7 GB |
| `class` @ 10.000 it. | idem | idem | idem | 0,53588168707754600 | — |
| `classprev1` | classe + bucket precedente | 7.585/222.865/765.243 | 6,2 GB | vedi sotto | 389 GB |

**Contributo di ogni street.** Tenere la classe al flop vale −0,119, al turn −0,122, al river
−0,086. Nessun salto e nessuna saturazione: ogni street compra una fetta simile. Il river, che
costa 1.000 → 41.973 righe e quindi 3,42 → 22,7 GB sull'albero vero, è quello che rende meno.
`classft` è il miglior rapporto della matrice.

**L'asse risoluzione è chiuso.** Con le tabelle 50/100/200 la chiave `class` peggiora del 19 %
(0,594054 contro 0,500181): abbassare la risoluzione non recupera margine. Alzarla non è
praticabile, perché `class` con 500/1.000/2.000 costa 31,5 GB su `co40_v1`, cioè l'intera memoria
della macchina. Resta che 4x bucket senza memoria compravano il 4,7 % sul corpus ridotto: la
risoluzione non è la leva, in nessuna delle due direzioni.

**Il muro dell'allenabilita.** `classprev1` a 2.000 iterazioni dà max gain campionato **2,4710 a**
con limite inferiore non distorto **0,6757 a**: cinque volte peggio di `class` e **tre volte peggio
del baseline**. Il limite inferiore esclude che sia rumore dello stimatore. Anche il costo per
iterazione esplode, 3,2408 s contro 0,3941. Quindi fra **41.973 e 765.243 righe river** la
rappresentazione smette di essere allenabile con 2.000 iterazioni e 64.000 board, e aggiungere
memoria non smette semplicemente di pagare: **distrugge il risultato**. È coerente con
`recall_full` (4.248.476 righe, 1,3475 a campionato) e risponde alla domanda che P9 teneva aperta.

Comandi: `out/matrix/run_variant.py <nome> <variante> <buckets-dir> {train|certify}` e
`out/matrix/run_prev.py {train|certify}`. Probe nuovi: `out/class_probe.cpp` (famiglia
class/classft/classf, mappa densa per street) e `out/prev_probe.cpp` (mappa concatenata
(classe, bucket precedente, bucket corrente) enumerata sui cataloghi canonici).
Fallimenti: (1) Il runner `run_prev.py` scritto via heredoc ha perso i backslash doppi e non
compilava; riscritto con lo strumento di scrittura file. Un solo tentativo di riparazione, come
da regola concordata con l'utente.
Decisioni prese in autonomia, nel mandato dell'utente del 2026-09-17 per le 18 ore senza
supervisione: (1) **`coarse32` cancellato**, come da regola concordata, perché girava solo se
`classprev1` o `coarse8` avessero migliorato; con 2.793.223 righe river è ben oltre il muro.
(2) **`coarse8` declassato e braccio Linear promosso**: con 902.272 righe river `coarse8` sta
dallo stesso lato del muro e costerebbe 2-3 ore per un esito prevedibile, mentre il braccio
Linear è diventato l'esperimento a più alto valore informativo, perché testa se il crollo dipenda
dal discount DCFR. Lo scostamento dall'ordine concordato è motivato dal valore informativo, non
dal costo.
Dubbi: (1) Il migliore della matrice resta 0,500181 a, cioè **16,7 volte D3**. Nessuna variante
cambia l'ordine di grandezza: si muovono tutte fra 0,50 e 0,83. (2) Non è noto se il muro sia una
proprietà della rappresentazione o un artefatto dell'algoritmo: con beta zero DCFR dimezza i
regret negativi a ogni iterazione globale anche sulle righe non campionate, e con 765.243 righe
su 64.000 board la maggior parte delle righe è rara. È esattamente ciò che il braccio Linear
misura. (3) `classprev1` costerebbe 389 GB su `co40_v1` e non sarebbe comunque portabile.
Prossimo passo: Linear su `class` (riferimento) e su `classprev1` (test dell'ipotesi del discount).

### 2026-09-17 — P9 — conversione a 32 bit delle righe, e l'estensione a 10.000 iterazioni non aiuta

Fatto: due cose, su indicazione dell'utente che ha posto il vincolo di memoria dei 32 GB della
macchina e ha chiesto perché non passare direttamente a indici a 32 bit.

**Conversione a 32 bit.** Le capacità delle righe postflop erano `uint16_t` in `StateLayout`,
`PolicyInfo`, `TrainerConfig` e `Certificate`, più le firme di `layout_state` e `rows_for`.
Gli offset erano già a 64 bit, quindi si è mosso solo il tipo dell'indice. **Il formato su disco
non cambia**: `policy_file.cpp` scriveva già le capacità con `append_little32` e le troncava solo
in memoria. Tolta la troncatura, sparisce anche un difetto latente: una policy con 765.243 righe
river veniva riletta come 41.915 senza errori, producendo certificati plausibili e falsi — lo
stesso genere di problema del certificatore del probe, ma silenzioso, e sarebbe scattato esatto
al primo run di `classprev1`. La conversione è stata poi estesa ai probe (`lossless_probe_row`,
le righe della best response), che erano rimasti a 16 bit.

Regressione richiesta esplicitamente dall'utente, HU10 non deve rompersi. Ricertificate le policy
HU10 **esistenti**, non riallenate, con i binari nuovi:

| Fixture | Ricertificato | Registrato | Esito |
|---|---|---|---|
| HU10 ridotto | 0,003980549728196586 | 0,003980549728196586 | identico |
| HU10 completo | 0,0039948972150156414 | 0,0039948972150156414 | identico |

Identici anche NashConv e i fingerprint di policy e albero. Suite completa **76/76 PASS**, senza
adattare alcun valore atteso.

**Estensione a 10.000 iterazioni (punto A del piano).** Ripreso da checkpoint il run `class`:

| Iterazione | Max gain campionato | Limite inferiore non distorto |
|---:|---:|---:|
| 2.000 | 0,5393 | 0,1811 |
| 3.000 | 0,5827 | 0,1822 |
| 5.000 | 0,5592 | 0,1840 |
| 6.000 | 0,5167 | 0,1865 |
| 8.000 | 0,5486 | 0,1928 |
| 10.000 | 0,6007 | 0,1988 |

Nessuna tendenza al ribasso: la stima oscilla fra 0,52 e 0,65 e il **limite inferiore peggiora in
modo monotono**, da 0,1811 a 0,1988. Il limite inferiore è quello senza selezione (strategia media
al preflop, best response esatta dal flop in poi), quindi non è rumore dello stimatore naive: è la
strategia media che si allontana. È la stessa patologia del baseline, che fra 2.000 e 10.000
iterazioni era passato da 0,657 a 0,841 a sull'albero precedente.

**Conseguenza sul protocollo:** la matrice delle rappresentazioni si misura a **2.000 iterazioni**
e nessuna variante viene estesa. Training 5.359,6 s per le 8.000 iterazioni aggiuntive; policy
`fnv1a64:a83d66470e4793ac`. La certificazione **esatta** del punto a 10.000 conferma la
lettura senza passare per le stime: **0,535881687077546 a**, NashConv 0,7528823179600452 a,
17,86 % del piatto, limite inferiore 0,2002673400156187 a. Contro 0,50018061896087860 a del
punto a 2.000, allenare cinque volte tanto **peggiora del 7,1 %**. Il limite inferiore, che
non ha bias di selezione, sale da 0,18037784090724046 a 0,2002673400156187: la strategia
media si allontana davvero. File `out/class_20260917/cert10k.json`.
Fallimenti: (1) Il link del probe è fallito con `LNK1104` perché il run A teneva aperto
`co40_train_class_probe.exe`; risolto linkando la famiglia di varianti a un eseguibile distinto,
`co40_train_class_family.exe`. (2) Gli object dei probe erano stale rispetto alle firme nuove e
il link ha dato `LNK2019`: vanno ricompilati insieme, ed è stato aggiunto allo script di build.
Dubbi: (1) L'oscillazione fra 0,52 e 0,65 su stime a 20 flop ha semilarghezza circa 0,08, quindi i
singoli punti non sono distinguibili fra loro; la tendenza del limite inferiore sì. (2) Resta non
verificato se la patologia dipenda dal discount DCFR sulle righe rare: è il braccio Linear del
piano.
Prossimo passo: matrice delle rappresentazioni a 2.000 iterazioni, dalla più economica.

### 2026-09-17 — P9 — la chiave `class` su CO40 intero: 0,500181 a esatti, -39,5 % dal baseline

Fatto: portata la chiave `class` — riga postflop `(classe preflop, bucket della street corrente)`
invece del solo bucket — sul gioco intero e certificata in modo esatto contro il baseline dello
stesso albero. Il probe `out/class_probe.cpp` costruisce la mappa densa scandendo le tre tabelle
bucket, non dipende da un corpus dichiarato e quindi lascia il trainer campionare i board
normalmente. Protocollo identico al baseline: DCFR alternato, 2.000 iterazioni, batch 32,
8 thread, valutazione ogni 500 iterazioni su 20 flop.

| | baseline (`base`) | `class` |
|---|---:|---:|
| Righe F/T/R | 200 / 500 / 1.000 | 7.585 / 21.638 / 41.973 |
| Stato | 9.364.488 B | 391.977.480 B |
| Secondi per iterazione | 0,2288 | 0,3941 |
| Stima campionata a 2.000 it. | 0,8418 a | 0,5393 a |
| **Max gain esatto** | **0,82717651588212859 a** | **0,50018061896087860 a** |
| NashConv | 1,1558581520489282 a | 0,69471958737876530 a |
| Quota del piatto | 27,6 % | 16,7 % |
| Limite inferiore dal flop | 0,282118 a | 0,180378 a |
| EV | ∓0,147056 a | ∓0,146476 a |

Passata esatta su 573 flop canonici e 605.088 board in 787,9 s. Policy
`fnv1a64:be84f6b45d37b5b8`, capacità dichiarate nel certificato `[7585, 21638, 41973]`,
albero `fnv1a64:18d08f453034ac0f` uguale al baseline. File `out/class_20260917/cert.json`.

**Risultato: -39,5 %.** È il miglior valore mai ottenuto sul gioco a 40 ante con una
rappresentazione portabile in produzione. Il prototipo `recall32` aveva dato 0,491631 a, ma su
un albero diverso (`fnv1a64:9066044f8c0f0f59`, quindi non confrontabile alla cifra) e costando
1,57 GB più una mappa gerarchica da versionare e serializzare, contro 392 MB e una chiave che
in produzione è la concatenazione della classe al bucket.

**Ma il corpus ridotto aveva sovrastimato la leva.** Là la classe portava il pavimento da
0,027933 a a 0,000010 a, cioè lo azzerava; sul mazzo intero ne toglie il 39,5 %. Il dubbio
registrato nella voce precedente era esattamente questo e va considerato confermato: con un solo
flop canonico e 16 classi preflop il bucket flop era quasi costante, quindi la classe faceva un
lavoro che sul mazzo intero, con 573 flop canonici e 169 classi, il bucket flop svolge già in
parte. **La graduatoria delle varianti misurata sul corpus ridotto non è trasferibile.**

Il risultato resta **16,7 volte sopra D3** (0,03 a) e **5 volte sopra D2** (0,1 a): verdetto
REJECTED come tutte le passate esatte a 40 ante. Nessun Nash certificato.

Comandi: `out/co40_train_class_probe.exe --config benchmarks/fixtures/preflop_blueprint_co40_test_v1.json
--resources-dir out/preflop_blueprint_resources --buckets-dir out/preflop_blueprint_buckets_200_500_1000
--iterations 2000 --batch 32 --threads 8 --eval-flops 20 --eval-every 500
--checkpoint out/class_20260917/ckpt.bin --policy-out out/class_20260917/policy.bin`;
`out/co40_certify_class_probe.exe ... --threads 8 --chunk 16`.
Fallimenti: (1) Il certificatore del probe crashava con access violation senza stampare nulla:
non costruisce mai un `Trainer`, quindi nessuno chiamava `recall_initialize` e ogni risoluzione
di riga leggeva una mappa vuota. Serve anche `layout_state` con le capacità del probe invece di
quelle delle tabelle: sono le due modifiche che il prototipo `recall32` aveva già fatto al
proprio certificatore. (2) La prima correzione è stata cancellata da un `copy /y` nello script
di build che rigenerava il file appena patchato; il main del certificatore è ora mantenuto in
`out/` e la copia è stata rimossa dallo script. Senza accorgersene si sarebbe valutata la chiave
a bucket contro una policy allenata con la chiave `class`, ottenendo un numero plausibile e privo
di significato. Il cablaggio è verificato su due segnali: il certificatore stampa
7.585/21.638/41.973 righe e accetta la policy senza rifiutarla per capacità incompatibili.
Dubbi: (1) La curva di `class` stava ancora scendendo a 2.000 iterazioni (0,7053 / 0,5828 /
0,5669 / 0,5393 sulle stime campionate), mentre il baseline era piatto. Il certificato fotografa
quella traiettoria, non il suo asintoto. Attenzione però: sul baseline proseguire da 2.000 a
10.000 iterazioni **peggiorava** (0,657 a 0,841 a sull'albero precedente), quindi l'esito del
proseguimento è informativo in entrambi i sensi. (2) Non è noto se il residuo di 0,500 a sia
memoria ancora mancante (i bucket di flop e turn restano dimenticati) o risoluzione dei bucket
sul mazzo intero. Separarlo richiede di rifare lo sweep sul gioco vero con `classprev1` o
`coarse8`, che costano 6,2 e 6,6 GB e sono eseguibili su questa macchina.
Prossimo passo: proseguire il run `class` da checkpoint per distinguere plateau da traiettoria
(circa 20 minuti), e ripetere lo sweep delle varianti sul mazzo intero per attribuire il residuo.

### 2026-09-17 — P9 — quanta storia serve: basta la classe preflop, e costa 392 MB

Fatto: stabilito che il pavimento è memoria e non risoluzione, resta da capire **quanta**
storia serve, perché conservarla tutta su CO40 costa 30,3 GB e il run `recall_full` a 2.000
iterazioni aveva dato 1,3475 a campionato, peggio del baseline, per righe troppo rare.
Il probe è stato reso parametrico (`GTOSD_MEMORY_VARIANT` in `out/memory_probe.cpp`) e si
parte dalla chiave completa togliendo distinzioni, invece di partire dai bucket aggiungendone.
In parallelo `out/variant_prefix_count.cpp` conta le righe della stessa chiave sull'intero
mazzo, così ogni variante ha insieme la exploitability e il costo.

Varianti della riga postflop, tutte con le tabelle di produzione 200/500/1.000:

| Variante | Riga postflop | Plateau | Capacità su CO40 | Stato R+S+policy |
|---|---|---:|---|---:|
| `lossless` | per mano, perfect recall | 0,000011 a | — | — |
| `coarse8` | classe + bucket precedenti in 8 bande + corrente | **0,000007 a** | 7.585/87.952/902.272 | 6,6 GB |
| `classprev1` | classe + bucket della street precedente + corrente | **0,000009 a** | 7.585/222.865/765.243 | 6,2 GB |
| **`class`** | **classe preflop + corrente** | **0,000010 a** | **7.585/21.638/41.973** | **392 MB** |
| `full` | classe + tutti i bucket precedenti + corrente | 0,000011 a | 7.585/222.865/4.248.476 | 30,3 GB |
| `prev1` | bucket della street precedente + corrente, senza classe | 0,009939 a | 200/34.141/162.417 | 1,26 GB |
| `base` (produzione) | solo bucket della street corrente | 0,027933 a | 200/500/1.000 | 9,4 MB |
| `fine` | solo bucket corrente, capacità 500/1.000/2.000 | 0,026628 a | 500/1.000/2.000 | ~37 MB |

Il plateau è il minimo delle ultime cinque valutazioni su 2.000 iterazioni, valutazione esatta
sul corpus di 96 board. Log in `out/abstraction/mem_*_40.log`.

**Due risultati.** Primo: **ricordare la sola classe preflop basta**. `class` arriva a
0,000010 a, cioè il valore della rappresentazione lossless, e costa 392 MB contro i 30,3 GB
della storia completa: un settantasettesimo, per lo stesso risultato. Secondo: **è la classe a
portare l'informazione, non il bucket della street precedente**. `prev1`, che ricorda il bucket
precedente ma dimentica la classe, si ferma a 0,009939 a: tre volte meglio del baseline ma mille
volte peggio di `class`. Coerente con il testimone del 2026-09-17 sul flop `7c Tc Ac`, dove nella
stessa riga finivano `6c 7d` che chiama alla radice con probabilità 0,00055 e `8c 8d` con 0,98986:
la distinzione persa è quella che il giocatore aveva già usato nel preflop.

Comandi: `GTOSD_MEMORY_VARIANT=<variante> out/co40_train_memory_probe.exe --config
out/recall32/stack_40.json --iterations 2000 --eval-every 100 --eval-flops 24 --batch 32
--threads 2 --no-stop`; `GTOSD_MEMORY_VARIANT=<variante> out/variant_prefix_count.exe
benchmarks/fixtures/preflop_blueprint_co40_test_v1.json out/preflop_blueprint_buckets_200_500_1000`.
Fallimenti: nessuno nuovo.
Dubbi: (1) **Il corpus ridotto ha un solo flop canonico** (`6s 7d 8c` sotto le 24 permutazioni)
e 16 classi preflop invece di 169. Su quel corpus il bucket flop è quasi costante, quindi la
classe fa un lavoro che sull'intero mazzo potrebbe essere in parte già svolto dal bucket flop.
La graduatoria fra le varianti non è trasferibile così com'è: il conteggio delle righe è
sull'intero mazzo ed è reale, la exploitability no. (2) Il risultato non dice che `class` porti
CO40 sotto 0,03 a: dice che su un gioco dove il pavimento è 0,027933 a la classe lo rimuove.
L'errore di astrazione dei bucket sull'intero mazzo resta da misurare separatamente.
(3) `coarse8` e `classprev1` fanno marginalmente meglio di `class` ma costano sedici volte
tanto; la differenza fra 0,000007 e 0,000010 a è irrilevante rispetto alla soglia di 0,03 a.
Prossimo passo: portare la chiave `class` su CO40 intero e certificarla in modo esatto contro
il baseline 0,82717651588212859 a. Serve un probe che enumeri le righe sull'intero mazzo, come
fa `variant_prefix_count.cpp`, e le mappi durante il training con board campionati.

### 2026-09-17 — P9 — attribuzione del pavimento: è la memoria, non la risoluzione dei bucket

Fatto: esperimento che separa le due cause possibili del pavimento dell'astrazione misurato
stamattina. Quattro bracci sullo stesso albero (`fnv1a64:abe35f9a259e8571`), stesso corpus
dichiarato, stesso seed, stessa traiettoria, 2.000 iterazioni DCFR alternato, batch 32,
valutazione **esatta** sui 96 board del corpus. Cambia solo la riga informativa postflop.
Il braccio `memory` è nuovo (`out/memory_probe.cpp`): rimpiazza `lossless_probe.obj` al link,
così trainer e best response risolvono la riga allo stesso modo.

| Braccio | Riga postflop | Capacità | Righe usate F/T/R | Plateau |
|---|---|---|---|---:|
| `lossless` | per mano, perfect recall | — | 528 / 992 / 1.860 | **0,000011 a** |
| `memory` | classe preflop + tutti i bucket precedenti + corrente | 200/500/1.000 | 98 / 129 / 129 | **0,000011 a** |
| `bucket` | solo bucket della street corrente | 200/500/1.000 | 42 / 31 / 4 | **0,027933 a** |
| `fine` | solo bucket della street corrente | 500/1.000/2.000 | — | **0,026628 a** |

Il plateau è il minimo delle ultime cinque valutazioni; l'ultimo punto di `memory` è
0,000013 a, di `lossless` 0,000011 a. Fingerprint di stato: `lossless` `613c93cfcd0c78ad`,
`memory` `1814010557dfe66d`, `bucket` `e6e2a1e43755744c`, `fine` `9bf5757293e856bd`.

**Conclusione.** Con le **stesse** tabelle bucket di produzione, conservare la storia nella
riga porta la exploitability da 0,027933 a a 0,000011 a, cioè sul valore della rappresentazione
lossless: un fattore 2.500. Quadruplicare le capacità senza memoria la porta da 0,027933 a
0,026628 a, cioè il 4,7 %. La risoluzione dei bucket non è il collo di bottiglia; la memoria
imperfetta lo è, e da sola spiega praticamente tutto il pavimento.

Il conteggio delle righe lo mostra in modo diretto: sul corpus la chiave di produzione usa
**4 righe distinte al river**, quella con memoria 129. Non è che i bucket river siano pochi —
sono 1.000 — è che tutte le storie che arrivano allo stesso bucket river collassano insieme.

Comandi: `out/co40_train_memory_probe.exe` e `out/co40_train_private_corpus_baseline.exe` con
`--config out/recall32/stack_40.json --iterations 2000 --eval-every 50 --eval-flops 24
--batch 32 --threads 4 --no-stop`, il secondo anche con
`--buckets-dir .../preflop_blueprint_buckets_500_1000_2000`. Log in `out/abstraction/`.
Fallimenti: (1) La prima versione di `memory_probe.cpp` enumerava le righe con
`BoardContext::combo_ids()`, che elenca solo le mani vive al **river**: al flop restavano senza
riga tutte le mani uccise da turn o river, il trainer indicizzava con `no_bucket` e il processo
moriva con access violation `0xC0000005`. Corretta interrogando le tabelle bucket per board
parziale, con la maschera delle sole carte visibili a quella street.
Dubbi: (1) Il risultato vale sul corpus ridotto, dove la chiave con memoria costa 98/129/129
righe. Su CO40 intero la stessa chiave è l'enumerazione completa dei prefissi:
7.585 / 222.865 / 4.248.476 righe, 31 GB in float64, e il run `recall_full` a 2.000 iterazioni
ha dato 1,3475 a campionato, cioè **peggio** del baseline, perché le righe sono troppo rare per
essere allenate. Quindi la leva è identificata ma il problema si sposta: conservare la storia
**senza** far esplodere il numero di righe. È esattamente ciò che tentava `recall32`, che aveva
portato 0,830 a a 0,492 a. (2) Questo non dimostra che una rappresentazione con memoria
raggiunga 0,03 a su CO40 intero: dimostra che l'astrazione delle carte non è la causa e che i
bucket attuali sono abbastanza fini, non che il problema di allenabilità sia risolvibile.
Prossimo passo: cercare una chiave che conservi le distinzioni utili della storia restando
allenabile, misurando su CO40 intero contro il baseline esatto 0,82717651588212859 a.

### 2026-09-17 — P9 — baseline CO40 sull'albero nuovo: 0,827177 a, il cambio di size non sposta nulla

Fatto: su richiesta dell'utente, che ha scelto di concentrarsi sul solo gioco a 40 ante e di
sospendere le onde a 100 e 300 ante della curva dell'errore di astrazione, è stato rifatto il
numero di riferimento sull'albero preflop modificato oggi. Serviva perché tutti i certificati
CO40 precedenti valgono per l'albero `fnv1a64:9066044f8c0f0f59` e non per quello attuale.
Protocollo identico a quello storico, senza nessuna modifica: DCFR alternato, 2.000 iterazioni,
batch 32, 8 thread, valutazione ogni 500 iterazioni su 20 flop, poi passata esatta.

Comandi: `gtosd_preflop_blueprint_train --config benchmarks/fixtures/preflop_blueprint_co40_test_v1.json
--resources-dir out/preflop_blueprint_resources --buckets-dir out/preflop_blueprint_buckets_200_500_1000
--iterations 2000 --batch 32 --threads 8 --eval-flops 20 --eval-every 500
--checkpoint out/baseline_20260917/ckpt.bin --policy-out out/baseline_20260917/policy.bin`;
`gtosd_preflop_blueprint_certify --policy out/baseline_20260917/policy.bin --threads 8 --chunk 16
--state out/baseline_20260917/cert_state.bin --output out/baseline_20260917/cert.json`.

Risultati. Albero 604 nodi, 242 decisioni, stato 9.364.488 byte, fingerprint
`fnv1a64:18d08f453034ac0f`. Training 457,6 s (0,2288 s per iterazione), `converged: false`.
Stima campionata a 20 flop all'iterazione 2.000: 0,8418 a con semilarghezza 0,12 e limite
inferiore 0,2775 a. Certificato **esatto** su 573 flop canonici e 605.088 board:

| Grandezza | Valore |
|---|---:|
| Max gain esatto | **0,82717651588212859 a** |
| NashConv | 1,1558581520489282 a |
| Quota del piatto | 27,6 % |
| Quota dello stack | 2,07 % |
| Limite inferiore dal flop | 0,28211828155994961 a |
| EV | −0,14705579374932654 / +0,14705579374932351 a |

Policy `fnv1a64:c41b0fba6be10f18`, certificazione 554,8 s (9,2 min, contro i 23 min storici:
l'albero è sceso da 1.129 a 604 nodi). File: `out/baseline_20260917/cert.json`.

Confronto con il baseline corretto sull'albero precedente, stesso protocollo e stesse tabelle:
**0,83020566987928368 a** contro **0,82717651588212859 a**, cioè una differenza dello 0,4 %.
La previsione fatta prima del run era che il cambio delle size preflop non avrebbe spostato il
risultato, perché il pavimento misurato è l'astrazione postflop; il numero la conferma. Restano
valide entrambe le letture solo nel senso che il gioco è cambiato poco in exploitability, non che
i due certificati siano confrontabili come misure dello stesso gioco.

Verdetto D2 (soglia 0,1 a): **REJECTED**, come tutte le passate esatte a 40 ante. D3 (0,03 a) è
lontana di un fattore 27. Nessun Nash certificato esiste per questo gioco; l'unico gioco del
programma che raggiunge le soglie resta HU10, con max gain esatto 0,0039948972150156414 a
(0,13 % del piatto) su `r3_cert_full.json`.
Fallimenti: nessuno nuovo.
Dubbi: (1) Il baseline usa le tabelle 200/500/1.000; la misura di oggi dice che il loro pavimento
su un gioco ridotto è 0,027933 a contro 0,000011 a della rappresentazione lossless, ma non dice
quanto di questi 0,827 a sia risoluzione e quanto memoria imperfetta. Le due leve richiedono
interventi diversi. (2) Le onde 100 e 300 ante della curva sono state interrotte su richiesta
dell'utente: i punti a quegli stack non esistono e la domanda sul transfer resta aperta.
Prossimo passo: scelta dell'utente fra la leva della memoria e quella della risoluzione; ogni
intervento si misura contro 0,82717651588212859 a con lo stesso protocollo e la stessa passata
esatta.

### 2026-09-17 — P9 — albero preflop CO40: risposta 17 a e ramo limpato con re-raise solo all-in (richiesta dell'utente)

Fatto: l'utente ha chiesto due modifiche all'albero preflop CO40 e ha autorizzato
esplicitamente la modifica di **entrambe** le fixture, compresa quella principale
finora protetta come riferimento del gate P9. Ha inoltre chiesto di registrare
l'autorizzazione nella roadmap, fatto con un erratum alla sezione delle fixture e
una precisazione al «Da non fare» di P9.

Prima domanda dell'utente: «quando limpa CO, BTN raise perché è 4? Dovrebbe essere 6».
Verifica sullo stato pubblico del motore, non sulla prosa: l'etichetta dell'export conta
le fiche **aggiunte**, quindi `bet_4` significa che BTN aggiunge 4 a sopra il suo blind da
1 a e **arriva a 5 a**. Il livello di aggressione sale solo sulle azioni aggressive
(`compiled_game.cpp:164`), quindi dopo un limp BTN è ancora a livello 0 e riceve
`open_targets`. Con la formula esatta del rilancio di un piatto intero,
`P + 2B - c` (P piatto prima dell'azione, B puntata da eguagliare, c fiche già versate dal
rilanciante), il limp-raise vale `4 + 2 - 1 = 5`: il motore era già corretto. Il 6 viene
dalla scorciatoia `3 x last bet + pot`, che vale solo per `c = 0`. L'utente ha scelto la
formula esatta.

La verifica ha però trovato un errore vero: la risposta della fixture di test a **13 a**
non è un full pot sotto nessuna delle due convenzioni. L'esatta dà 17 a (BTN deve 4 a, il
piatto dopo il call è 12 a, quindi 1 + 4 + 12). Il 13 a proviene dal calcolo registrato
nella voce del 2026-09-16 («BTN paga 3 a per chiamare l'apertura, piatto 10 a»): quella
voce è sbagliata, lo stato pubblico dice 4 a da chiamare e piatto 12 a. Questa voce la
corregge.

Seconda richiesta: dopo «CO limpa, BTN rilancia» il limper deve avere solo fold, call e
all-in. I due rami raggiungono stati pubblici identici a meno di quale posto tiene quale
impegno, e `acted_players_mask` viene azzerato a ogni raise (`libs/core/src/game.cpp:558`),
quindi il ramo non è deducibile dallo stato. Il flag viene propagato dal compilatore:
`CompiledNode::limped_pot`, acceso da un call al livello 0 preflop. Non entra nel
fingerprint dell'albero, perché la differenza di comportamento è già nel fingerprint della
configurazione.

Comandi: `out\dump_preflop_tree.exe` (diagnostico nuovo, stampa la parte preflop con
piatto e impegni); `gtosd_preflop_blueprint_game --config ...`;
`gtosd_preflop_blueprint_game_tests`.
Risultati. Campo nuovo `limp_response_target_units`: opzionale, indicizzato sugli open,
assente = comportamento storico, vuoto = solo all-in. Serializzato solo quando presente,
così le configurazioni che lo precedono mantengono fingerprint e artefatti.

| Fixture | Albero | Preflop | Ingressi | Fingerprint |
|---|---:|---|---:|---|
| CO40 test (risposta 17 a, limp con re-raise solo all-in) | 604 nodi | 28 nodi, 10 decisioni | 4 | `fnv1a64:18d08f453034ac0f` |
| CO40 principale (limp con re-raise solo all-in, size convertite) | 26.878 nodi | 28 nodi, 10 decisioni | 4 | `fnv1a64:d6c10723d35b9503` |
| HU10 completo (non toccato) | 1.501 nodi | 22 nodi, 8 decisioni | 3 | `fnv1a64:bc9e7b35ad8c021d` |

Il fingerprint HU10 coincide con quello registrato negli artefatti esistenti
(`r3_chart_hu10_full.json`): le policy e i certificati HU10 restano validi. La parte
preflop CO40 non riproduce più l'albero legacy `fnv1a64:a68337fa567aa2d9`; il test congela
ora `fnv1a64:c2169c4295026609` come guardia di regressione, non come equivalenza al legacy.
Conteggi postflop CO40 principale dopo la conversione delle size: 26.854 nodi rappresentati,
9.948 decisioni, 25.852 archi (prima 27.012 / 10.060 / 25.944). Suite del modello di gioco
PASS con 797.826 asserzioni; suite completa del blueprint 19 test su 19 PASS, dopo aver
copiato nel checkout le tabelle bucket, che mancavano e facevano fallire cinque smoke per
un motivo indipendente da questa modifica.
Fallimenti: (1) La voce del 2026-09-16 che deriva la risposta a 13 a contiene un errore
aritmetico mai verificato contro lo stato pubblico; le size della fixture di test ne
dipendevano. (2) Tutte le policy, i checkpoint e i certificati CO40 esistenti sono
invalidati dal cambio di fingerprint, comprese le misure della diagnosi P9 sulla variante
di test.
Seguito, stessa giornata: l'utente ha deciso di convertire alla formula esatta anche le size
della fixture principale. I due open Monker erano entrambi decisi alla radice, dove la formula
dà 5 a, quindi collassano in una sola size; la risposta diventa 17 a. Le due fixture CO40 hanno
ora la stessa parte preflop e differiscono solo nelle size postflop (tre size 33/66/120 %
contro una sola del 100 %). Il rilancio di BTN sul limp resta 5 a, già esatto in quel nodo.
Dubbi: (1) Con le size convertite la fixture principale non corrisponde più all'albero del
riferimento Monker: il comparatore confronta a parità di albero, quindi il confronto con Monker
previsto da D1/D4 non è disponibile finché non esiste un riferimento esterno sul nuovo albero.
L'utente è stato informato di questa conseguenza prima di decidere. (2) A 300 ante l'all-in
sparisce dai nodi poco profondi perché la spinta supera la soglia `all_in_threshold` di 100.000
punti base sul piatto dopo il call: è comportamento preesistente del modello, non introdotto
qui, ma cambia la forma dell'albero fra i tre stack della curva.
Prossimo passo: rifare la curva dell'errore di astrazione a 40/100/300 ante sull'albero
definitivo.

### 2026-09-17 — P9 — errore di astrazione a 40 ante: il trainer converge, i bucket no

Fatto: misura diretta dell'errore di astrazione, mai fatta a 40 ante. Due bracci con lo
stesso albero, lo stesso corpus dichiarato, lo stesso seed e la stessa traiettoria; l'unica
differenza è la chiave postflop. Braccio lossless: righe per mano a perfect recall
(528 / 992 / 1.860 righe), con controllo esplicito che nessuna riga fonda genitori diversi.
Braccio bucket: le tabelle 200/500/1.000 di produzione. Corpus: flop `6s 7d 8c`, due turn,
due river, tutte le 24 permutazioni dei semi (96 board), 120 combo per giocatore sui ranghi
T/J/Q/K, valutazione **esatta** sul corpus. Rispetto alle due prove del 2026-09-16 è stato
aggiunto `--no-stop`: entrambe si erano fermate a 100 iterazioni sulla soglia D3, quindi il
plateau non era visibile.

Comandi: `out\co40_train_lossless_private_probe.exe` e
`out\co40_train_private_corpus_baseline.exe` con `--config out\recall32\stack_40.json
--iterations 2000 --eval-every 50 --eval-flops 24 --batch 32 --threads 4 --no-stop`.
Risultati (max gain in ante, valutazione esatta):

| Iterazione | lossless | bucket |
|---:|---:|---:|
| 50 | 0,047439 | 0,049161 |
| 100 | 0,007673 | 0,027625 |
| 650 | 0,000104 | 0,028285 (massimo) |
| 1.000 | 0,000042 | 0,027340 |
| 2.000 | **0,000007** | **0,026053** |

Il braccio lossless scende di quattro ordini di grandezza e continua a scendere; quello a
bucket sale fino all'iterazione 650 e poi scende lentamente verso 0,026 a. A 40 ante, su
questo gioco, **l'errore residuo è quasi interamente astrazione**: il rapporto fra i due
plateau è circa 3.700. Il primo punto di entrambi i bracci riproduce alla sesta cifra le
prove del 2026-09-16, quindi l'harness è deterministico.
Nella stessa sessione è stata completata la valutazione del run `recall_full` v3 (storia
completa dei bucket, 2.000 iterazioni, conclusa alle 01:42): max gain campionato su 32 flop
seed 123 **1,3475 ± 0,2536 a**, lower 0,3847 a, contro 3,3970 a di v1 a 250 iterazioni.
Migliora di 2,5 volte ma resta sopra il baseline corretto (0,8302 a esatto) e sopra
`recall32` (0,4916 a esatto). Il confronto non è omogeneo: 1,3475 è campionato e distorto
verso l'alto, gli altri due sono esatti su 573 flop. File:
`out/recall_full/co40_2000_v3_sample32_DIAGNOSTIC_ONLY.json`.
Fallimenti: (1) Il primo braccio bucket a 40 ante è morto con stack overflow
(`0xC00000FD`) all'iterazione 300 mentre il certificatore esatto occupava 10,6 GB e
paginava fuori gli altri processi; rilanciato senza `--checkpoint` e arrivato a 2.000.
Log conservato in `out/abstraction/bucket_40_crashed_at_300.log`. (2) Le onde 100 e 300
ante sono state fermate su richiesta dell'utente per cambiare prima l'albero preflop.
(3) La certificazione esatta di v3 è stata interrotta per liberare memoria; riprendibile
dal suo `--state`.
Dubbi: (1) Il corpus è ristretto (96 board, 120 combo per giocatore): il numero assoluto
0,026 a non è l'errore di astrazione di CO40 sull'intero mazzo, perché i bucket sono
costruiti sul mazzo completo e qui sono relativamente più grossolani. Ciò che si legge è il
confronto fra i due bracci, non la scala. (2) Il segnale sul transfer a 100 e 300 ante
richiede gli altri due punti della curva, non ancora misurati.
Prossimo passo: rifare i tre punti della curva sull'albero preflop definitivo.

### 2026-09-17 — P9 — precisione del prototipo con storia completa

Il primo run fisico con storia completa raggiunge 250 iterazioni, ma la
stima su 32 flop resta 3,396952 a ± 0,256657 a (lower 1,024487 a).
Non è una qualificazione. Il confronto su 1.000 iterazioni campionate di
un corpus CO40 ridotto trova inoltre un errore relativo nelle somme di
strategia di 2,80616e-5, oltre il limite di prova 1e-5. Accumulare i delta
per batch in float64 lo riduce a 1,30942e-5, ancora insufficiente.

La revisione v3 conserva `sum(k^gamma * deltaS_k)` invece di applicare
ogni volta il discount alle celle float32. È la stessa media DCFR dopo
normalizzazione. A 1.000 iterazioni, rispetto al trainer float64 con
discount esplicito: errore regret 6,61243e-7, somme 1,30343e-6 in scala
relativa, frequenze 2,22214e-7. Il controllo passa con la tolleranza
originaria; passano anche le 335.327 asserzioni CO40, la ripresa e i
confronti tra thread count. Risultati in `out/recall_full/precision_comparison.json`
e `out/recall_full/tests_co40_weighted_v3.log`.

Avviato da zero il run fisico v3 a 2.000 iterazioni. Primo checkpoint
completato a 500; log `out/recall_full/co40_2000_v3.log`, checkpoint
`out/recall_full/co40_v3.bin`. Stato con identità distinta dalle versioni
precedenti. La policy e il certificatore del prototipo restano diagnostici.

### 2026-09-17 — P9 — memoria delle street e prova senza ulteriore clustering

Il certificatore dedicato a `recall32` completa tutti i 573 flop canonici:
max gain esatto 0,49163135910804856 a e NashConv 0,73687471380326819 a,
contro max gain 0,83020566987928368 a del baseline corretto a 2.000
iterazioni. Il risultato resta sopra D3 (0,03 a); una singola traiettoria
non separa l'effetto della memoria da quello del nuovo clustering.
Certificato: `out/recall32/co40_2000_exact_DIAGNOSTIC_ONLY.json`.

Il diagnostico sulla policy baseline trova 20.397 celle flop/bucket con
reach preflop differenti fra le combo fuse. Sul flop `7c Tc Ac`, nel bucket
127, `6c 7d` chiama alla radice con probabilità 0,000552 e `8c 8d` con
0,989862. La distinzione già usata nel preflop viene dimenticata. Il report
P9 distingue questa prova strutturale dalle differenze locali di EV e
dall'exploitability del gioco intero.

L'enumerazione completa dei prefissi dei bucket 200/500/1000 produce
7.585/222.865/4.248.476 righe. Il prototipo `out/recall_full` conserva
tutte queste distinzioni, usando snapshot delle sole righe del batch,
R/S in float32, valori in float64, discount per riga e I/O progressivo.
Controlli solo CO40 su un corpus dichiarato: 335.327 asserzioni PASS,
scarto massimo regret DCFR 2,68461e-6, somme 3,09764e-7. Linear CFR ha
scarto assoluto regret 5,61522e-4 e somme 1,52588e-5, entro la tolleranza
relativa 1e-5. Best response fisica contro l'oracolo float64 entro 1e-9;
stato bit-identico a 1/2/4/8 thread, ripresa bit-identica, policy media
salvata progressivamente uguale alla versione in memoria. Il riferimento
scalare DCFR applica esplicitamente il discount t-1 del trainer prima
della traversata; la variante nominale del solver scalare usa una diversa
convenzione temporale e non era un confronto diretto valido.

Log: `out/recall_full/tests_co40_v2.log`. Avviata la prova fisica CO40 con
checkpoint; nessuna modifica del prodotto per questi prototipi, nessun
nuovo test a stack 100 o 300. La convergenza CO40 resta aperta.

### 2026-09-17 — P9 — gap residuo CO40 certificato esattamente

Completata la certificazione della policy a 2.000 iterazioni con campioni
indipendenti: 573 flop canonici, 605.088 board, max gain esatto
0,83020566987928368 a, NashConv 1,1585784190956665 a. File:
`out/co40_corrected_exact.json`. Il difetto di campionamento è corretto,
ma il problema CO40 resta aperto anche secondo la metrica esatta.

La nuova prova con memoria conserva tutte le coppie classe/bucket flop,
poi partiziona turn e river all'interno del genitore. Produce
7.585/58.221/184.528 righe e 1.572.896.088 byte di stato su CO40.
A 500 iterazioni: max gain campionato 0,839891 a, semilarghezza 0,155998 a,
lower 0,221515 a. Non è una qualificazione. Run ripreso fino a 2.000
iterazioni, con checkpoint `out/recall32/co40.bin` e log
`out/recall32/co40_2000.log`.

Per rendere praticabile la prova, il prototipo ricalcola la policy solo
sulle righe lette dal batch. Profiling eseguito prima della modifica;
equivalenza bit per bit dopo cinque iterazioni CO40 e confronto scalare
CO40 entro 9,09e-13. Codice sperimentale confinato in `out/recall32`.
Nessun nuovo test a 100 o 300 ante e nessuna modifica delle size.

### 2026-09-16 — P9 — ambito CO40 confermato: full pot più all-in

L'utente conferma una sola size postflop del 100% del piatto, con all-in
separato come nella fixture attuale. Il caso da risolvere è
`preflop_blueprint_co40_test_v1.json`; il passaggio al CO40 a tre size non
fa parte di questa attività. Questa indicazione aggiorna il piano delle
voci precedenti che prevedevano la qualificazione successiva a tre size.
La correzione deve restare generale rispetto a stack e albero delle azioni.
Con una precisazione successiva, l'utente sospende i test a 100 e 300 ante:
le prove attive e i prossimi controlli si concentrano esclusivamente su CO40.

### 2026-09-16 — P9 — campionamento alternato corretto, problema CO40 ancora aperto

Mandato aggiornato dell'utente: trovare e risolvere il problema con una regola
generale, valida anche per stack futuri di 100 e 300 ante, senza richiedere
parametri diversi all'utente finale. Nessun cambiamento delle size o delle soglie.

Correzione applicata: il secondo aggiornamento alternato campionato usa un
batch indipendente. Il precedente riuso del campione dava regret
condizionalmente distorti: scarto 2,58391 contro un riferimento esatto a due
board; dopo la correzione, 4,44e-16. Checkpoint versione 2. Oracoli su alberi
a 40, 100 e 300 ante verificano aggiornamenti ed EV/best response entro `1e-9`.

La correzione non chiude CO40 test: a 2.000 iterazioni, max gain campionato
0,918223 a (8 flop, semilarghezza 0,134932), max gain lower 0,303665 a.
Anche simultaneo e bucket più fini restano sopra soglia. Questi risultati non
escludono ogni effetto del budget o dello schema: le conclusioni categoriche
della precedente voce di diagnosi non sono dimostrate.

La chiave postflop dimentica le informazioni precedenti. Un controesempio
esatto ora riproducibile nel test `test_forgotten_information_witness` mostra
CFR fermo a gap 0,75 con tale fusione, contro 2,50e-8 conservando la memoria.
Questo dimostra una limitazione generale della rappresentazione; non prova
che spieghi da sola tutto il gap osservato in CO40.

Una gerarchia sperimentale con quattro figli per livello conserva la memoria
ma perde troppa risoluzione: max gain campionato 3,59519 a a 2.000 iterazioni.
Scartata come sostituzione del modello. Un'altra prova conserva i bucket e
separa le classi preflop: 7.585/21.638/41.973 righe, 405.658.776 byte di stato,
max gain campionato 0,729284 a a 500 iterazioni. Il miglioramento rispetto al
run base a 500 iterazioni non è conclusivo con otto flop di valutazione.
Entrambe le prove restano escluse dal prodotto.

Report, limiti e artefatti: [P9_CONVERGENCE_DIAGNOSIS.md](../../archive/preflop-blueprint-research-2026-09/P9_CONVERGENCE_DIAGNOSIS.md).
Nessuna convergenza o qualificazione dichiarata.

### 2026-09-16 — P9 (diagnosi) — CO40 di test con una sola apertura full pot: la riduzione delle size non basta

Fatto: su richiesta dell'utente la fixture **di test** CO40 passa da due aperture (6 a / 10 a) e due
risposte (10,5 a / 14,5 a) a una sola apertura full pot e una sola risposta full pot. Calcolo delle
size con la stessa regola di HU10: piatto iniziale 3 a (due ante da 1 a più il blind del bottone da
1 a), CO paga 1 a per vedere (piatto 4 a) e rilancia di un piatto intero, quindi **apertura 5 a**;
BTN paga 3 a per chiamare l'apertura (piatto 10 a) e rilancia di un piatto intero, quindi
**risposta 13 a**. Lo stack (40 a) non entra nel calcolo: per questo l'apertura coincide con quella
di HU10. La fixture CO40 principale non è toccata (le size Monker restano il riferimento del gate
P9, che la roadmap vieta di cambiare, e i conteggi di P4 §4 e P5 §3 le citano). Training,
certificazione esatta ed export con lo stesso protocollo.
Comandi: `gtosd_preflop_blueprint_game --config …co40_test_v1.json`; `train …co40_test_v1.json
--iterations 2000 --batch 32 --threads 8 --eval-flops 20 --eval-every 500 --policy-out out/policy_co40t1.bin`;
`certify --policy out/policy_co40t1.bin --threads 8 --chunk 16 --output out/co40t1_cert.json`;
`export --certificate out/co40t1_cert.json --eval-flops 60 --threads 8 --postflop-tree …`.
Risultati: albero 637 nodi (256 decisioni), preflop 34 nodi / 12 decisioni /
5 entry postflop (prima 1.129 nodi, 456 decisioni, preflop 58 / 20 / 9); stato 9.7 MB;
training 7 min (0.205 s per iterazione), stima a 20 flop
all'iterazione 2.000 0,6668 ± 0,1075 a; certificato esatto
**0,6516 a** (21,7 % del piatto, 1,63 % dello stack), nashconv 1,0257 a, limite
inferiore dal flop 0,2638 a, EV di CO -0,1429 a, certificazione 12 min
(1.3 s per flop). Confronto con le due aperture (stesso protocollo, 2.000 iterazioni):
0,6569 a. Quindi **la riduzione non risolve**: da 0,6569 a 0,6516 a, cioè lo stesso ordine di grandezza, con un albero quasi dimezzato (637 nodi contro 1.129) e una parte preflop di 12 decisioni contro 20. Radice CO: all in 34.8 %, fold 33.0 %, call 31.1 %, raise 5 1.1 %; EV di radice -0,1397 a;
12 nodi preflop; albero postflop 5 entry, 244 nodi
decisionali, 560 archi; export 86 s.
Per confronto HU10 completo (1.501 nodi, stessa astrazione, stesso protocollo): 0,0040 a, 0,13 % del piatto.
Nota sulle etichette: nell'export e nel viewer la risposta compare come `raise_12` perché l'etichetta conta le fiche aggiunte dall'attore e BTN ha già 1 a di blind; la puntata raggiunta è 13 a, verificata sullo stato pubblico (dopo il call piatto 28 a e 26 a dietro a testa). Le aperture coincidono con il target perché CO non ha nulla nella puntata.
Fallimenti: nessuno.
Dubbi: (1) L'esperimento non separa le due ipotesi residue perché la risposta configurata a 13 a
tiene attivo il ramo `response_targets[index]`, che non ha oracolo esatto (scelta dell'utente fra
le due opzioni proposte). Per separarle servirebbe la variante senza risposta, con la parte
preflop identica a HU10 e l'unica differenza nello stack. (2) Con l'albero sceso a 637 nodi,
cioè meno della metà di HU10 completo (1.501), la dimensione dell'albero è definitivamente
esclusa come causa.
Prossimo passo: decisione dell'utente fra l'oracolo esatto a 40 a (punto 2 del piano) e la misura
dell'errore di astrazione a 40 a (punto 4).

### 2026-09-16 — P9 (diagnosi) — mappa della copertura: non esiste un oracolo esatto su CO40

Fatto: risposta alla domanda dell'utente "non c'è l'oracolo esatto in CO40?". Verificata riga per
riga la copertura dei test: la risposta è **no**, e la parte mancante è esattamente quella dove il
trainer produce i numeri sbagliati. Nessuna modifica al codice; note aggiunte a P4 §3, P5 §2,
P6 §3 e P7 §4.
Comandi: lettura di `tests/preflop_blueprint_{trainer,oracle,kernel,game,certifier}_tests.cpp`,
`tests/preflop_blueprint_test_support.hpp` (`oracle_boards`, `oracle_subsets`),
`libs/preflop_blueprint/src/game_model.cpp` (`action_config_at`); report del gioco sulle due
fixture (`gtosd_preflop_blueprint_game --config …`).
Risultati. Copertura attuale:

| Componente | Confronto con una sorgente indipendente | Gioco su cui gira | Tolleranza |
|---|---|---|---|
| Albero e payoff (P4) | ogni nodo decisionale contro `legal_actions` / `apply_action` del core, ogni figlio e ogni payoff | **CO40**, HU10 completa, HU10 ridotta, 3-way | uguaglianza esatta |
| Kernel per board (P5) | valori per combo contro il solver postflop (`ProductionDcfr`, solo come oracolo di test, D5) | **CO40**, tre sottogiochi **river** dalla radice pubblica (57 / 117 / 57 nodi, al massimo 3 rilanci) | `1,4·10⁻¹⁴` a |
| Policy a bucket contro la stessa strategia per mano (P5) | traversata completa dell'albero | **CO40**, un board casuale | `1e-12` |
| CFR vettoriale: regret cumulati, somme di strategia, strategia media (P6) | `solve_finite_game` sul gioco ridotto costruito come `FiniteGame` | **solo HU10 ridotta** (3 board, 6 combo per giocatore, 25 iterazioni Linear) | `1,7·10⁻¹³` / `1,4·10⁻¹⁴` / `1e-9` |
| Best response fisica non chiaroveggente (P6) | `calculate_nash_conv` del `FiniteGame` lossless | **solo HU10 ridotta** | `1e-9` |
| Passata esatta per immagini d'orbita (P7) | enumerazione fisica dei 5.984 flop per combo | **solo HU10** | `1e-12` |
| Errore di astrazione (policy a bucket contro policy per mano) | exploitability fisica delle due policy | **solo HU10** | misurato: 1–3 millesimi di ante |

Le prime tre righe girano su CO40, le altre quattro no. Ma le prime tre verificano il *gioco* e i
*kernel per board*, non il CFR: l'unica verifica dei valori del CFR vettoriale, della media e della
best response è l'oracolo `FiniteGame`, e gira solo su HU10 ridotta. Codice attraversato da CO40 e
mai confrontato con una sorgente esatta:

1. **Più di una size di apertura.** `action_config_at` al livello 0 passa l'intera lista di open a
   `target_config`; l'oracolo ne ha una sola (5 a), CO40 ne ha due (6 a e 10 a).
2. **Risposta indicizzata sull'open scelto.** Il ramo livello 1 che cerca `state.current_bet` fra gli
   open e usa `response_targets[index]` non è nel gioco dell'oracolo: dalla modifica delle fixture
   del 2026-09-16 (pomeriggio) HU10 ha `response_target_units: []` e il livello 1 passa da
   `all_in_config`. **Prima di quella modifica l'oracolo copriva questo ramo** (open 3 a / 5 a,
   risposte 6 a / 8 a): la copertura è stata persa come effetto collaterale, non dichiarato allora.
3. **Rilancio incompleto configurato.** `allow_configured_incomplete_raise` è `true` in entrambe le
   fixture, ma ha effetto solo insieme a una response target: nell'oracolo attuale non ha effetto.
4. **Livello ≥ 2 dopo un rilancio configurato.** Su CO40 è il nodo in cui l'apertore affronta la
   risposta a 10,5 a e può solo foldare, chiamare o spingere. Nell'oracolo il livello 2 esiste solo
   dopo un all-in, cioè per il ramo `facing_all_in` → `passive_config`, che è codice diverso.
5. **Profondità dei rilanci.** Il gioco dell'oracolo ha `maximum_raise_count = 1`, CO40 ne ha 4: la
   ricorsione del CFR vettoriale con più rilanci nella stessa street non è mai stata confrontata
   con un solver esatto.
6. **Postflop profondo.** Nell'oracolo, dopo open e call restano 5 a su un piatto di 11 a: una sola
   decisione effettiva per street. Su CO40 restano 33 a su un piatto di 15 a con tre street.
   L'aggregazione non chiaroveggente flop → turn → river (decisione 30) è verificata esattamente
   solo nella forma piatta di HU10.
7. **Nessuna seconda strada su CO40.** Il certificatore condivide con il trainer albero compilato,
   contesto di board e kernel: un difetto comune ai due non produce una discrepanza, produce solo
   una exploitability alta, che è quello che si osserva. Su HU10 la seconda strada esiste ed è il
   `FiniteGame` lossless.
8. **Errore di astrazione mai misurato a 40 a.** Le tabelle bucket sono costruite dalle sole carte e
   non dipendono dallo stack; la loro adeguatezza con 34 a dietro è un'assunzione, non una misura.
   Su HU10 la misura esiste (1–3 millesimi di ante) e la procedura per farla è quella di P6.

Da qui la struttura del problema: il certificato dice "questa strategia è sfruttabile per 0,657 a";
non dice se la strategia è sbagliata perché il CFR ha calcolato male i regret su quei rami (punti
1–6) o perché ha calcolato bene dentro un'astrazione troppo grossolana per 40 a (punto 8). Le due
cause richiedono interventi opposti e nessuna delle due è esclusa dai dati attuali.
Fallimenti: (1) La modifica delle fixture HU10 del 2026-09-16 (pomeriggio, richiesta dell'utente)
ha ridotto la copertura dell'oracolo esatto: i rami "seconda size di apertura" e "risposta
indicizzata", prima inclusi, ora non sono più in nessun test di valore. Va registrato come costo
non dichiarato di quella modifica: HU10 resta il gioco di validazione, ma ora valida meno.
Dubbi: (1) L'oracolo a 40 a va costruito come quello di P6 (gioco ridotto `FiniteGame` con pochi
board e poche combo) ma con la struttura preflop di CO40; la dimensione cresce con i rilanci
(`maximum_raise_count = 4`) e va misurata prima di scriverlo. (2) Se l'oracolo a 40 a passasse,
resterebbe il punto 8 e servirebbe la misura dell'errore di astrazione a 40 a, che è un secondo
esperimento indipendente.
Prossimo passo: nessuno finché l'utente non decide; il piano resta quello della voce precedente,
con i punti 2 (oracolo esatto a 40 a) e 1/4 (astrazione) come alternative da separare.

### 2026-09-16 — P9 (diagnosi) — perché CO40 test non converge e piano per la convergenza

Fatto: analisi delle tre certificazioni esatte di CO40 test (voce precedente) per rispondere
all'utente sul perché non si raggiunge l'1 % del piatto; lanciato il primo esperimento
discriminante (tabelle 500/1.000/2.000, stesso protocollo, certificazione esatta). Nessuna
modifica al codice.
Comandi: `gtosd_preflop_blueprint_train …co40_test_v1.json --buckets-dir out/preflop_blueprint_buckets_500_1000_2000
--iterations 2000 --batch 32 --threads 8 --eval-flops 20 --eval-every 500 --policy-out out/policy_co40t_b500.bin`;
`certify … --buckets-dir …500_1000_2000 --policy out/policy_co40t_b500.bin --threads 8 --chunk 16`.
Risultati (evidenze già disponibili): (1) budget escluso: da 2.000 a 10.000 iterazioni la
exploitability esatta sale (0,657 → 0,841 a); con un CFR corretto la strategia media si avvicina
all'equilibrio del gioco astratto come `1/√T`. (2) Schema di aggiornamento escluso: Linear
simultaneo, la variante verificata contro l'oracolo esatto su HU10, dà 1,086 a ed è piatto fra
500 e 2.000 iterazioni. (3) La perdita è distribuita: con il preflop fisso e best response solo
dal flop restano 0,26–0,36 a (9–12 % del piatto); il preflop è assurdo: CO limpa il 31 % (AA
97 %) e spinge 40 a con il 33 % delle mani; BTN chiama lo shove di 13 piatti con il 29 % del
range e rilancia all-in sull'open 6 a con il 26 % (sull'open 10 a 26 %): entrambi preferiscono
chiudere la mano preflop, come se il postflop valesse poco o fosse valutato male. (4) Su HU10
lo stesso codice e le stesse tabelle danno 0,13 % del piatto a 2.000 iterazioni.
Ipotesi residue: (A) **astrazione**: tabelle 200/500/1.000 identiche a HU10, dove perdono 1–3
millesimi di ante; a 40 a il postflop pesa molto di più (34 a dietro dopo l'open, tre street,
due rilanci) e la best response fisica sfrutta ogni mano nel bucket sbagliato; la exploitability
fisica può salire mentre quella astratta scende (patologia dell'astrazione, Waugh et al. 2009).
(B) **difetto del trainer sulle strutture proprie di CO40** (due open con risposte indicizzate,
rilancio incompleto, nodi di livello 2 con solo all-in, payoff fino a ±40 a): l'oracolo esatto
del trainer (P6 §3) copre solo HU10 ridotto; i test P4 su CO40 verificano albero e payoff, non i
valori del CFR vettoriale; il certificatore misura nello stesso gioco compilato, quindi un errore
nei regret di quei nodi produrrebbe esattamente questo quadro.
Piano per la convergenza (proposta, in ordine di costo):
1. Esperimento in corso: CO40 test con 500/1.000/2.000, 2.000 iterazioni, certificazione
   esatta (≈ 35 min). Discesa netta → (A); invariata → (B).
2. Oracolo esatto a 40 a: gioco ridotto `FiniteGame` con la struttura preflop di CO40 (due
   open, risposte, rilancio incompleto, livello 2) e postflop minimo; confronto di regret e
   strategia media con `solve_finite_game` come in P6 §3. È il test che manca; costa un test
   nuovo e qualche ora.
3. Fixture intermedia (stack 20 a, stesse size) per misurare come cresce la exploitability con
   la profondità e separare le ipotesi anche sul preflop.
4. Se (A): tabelle per stack profondi — più bucket (punto 1), feature diverse (distribuzione
   dell'equity contro range invece che contro mano casuale, "potential-aware" al turn), river
   senza astrazione dove la memoria lo consente; poi capacità e batch più grandi e regola di
   arresto con il certificatore esatto (22 min sull'albero di test).
5. Se (B): correzione del trainer, ripetizione di P6 con l'oracolo a 40 a, poi il protocollo
   HU10 su CO40 test.
6. Solo dopo: CO40 completo (tre size), certificazione 10,2 h a 8 thread (≈ 2 h su EPYC 7443).
Fallimenti: nessuno nuovo.
Dubbi: la variante di test (una size postflop) è più facile del CO40 completo: se non converge
questa, il completo non convergerà con lo stesso trainer e le stesse tabelle.
Prossimo passo: decisione dell'utente sull'ordine dei punti 1–5; nessun codice viene scritto
fino a quella decisione.
Nota (2026-09-16, sera): l'esperimento 1 è stato interrotto dall'utente durante il training
(nessun risultato); l'utente ha indicato che non è necessario scrivere codice. I punti 1–5
restano proposte.

### 2026-09-16 — P8 — soluzione CO40 a una size nel viewer, DCFR contro Linear su CO40, risposte su exploitability/tempo e sui nodi fuori percorso

Fatto: (1) L'utente si aspettava nel viewer anche CO40 a una size postflop: la variante di test
è stata risolta con il protocollo HU10 (DCFR alternato, 2.000 iterazioni, `B = 32`, 8 thread,
valutazione ogni 500 iterazioni su 20 flop), certificata esatta ed esportata (`out/co40t_*`).
Vista la exploitability alta e la stima campionata crescente, lo stesso run è stato proseguito a
10.000 iterazioni (`--resume`, `out/co40t10k_*`) e, come primo punto del confronto P9.1, è stato
addestrato anche Linear simultaneo per 2.000 iterazioni (`out/co40t_linear_*`), entrambi
certificati esatti ed esportati. Viewer rigenerato con tre sorgenti (HU10 completo, HU10 ridotto,
CO40 test: DCFR alternato, 2.000 iterazioni, la migliore delle tre); la navigazione postflop resta quella di
HU10 completo: generatore e server accettano un solo albero e una sola policy (decisione 48).
(2) Domanda sull'aumento della exploitability per ridurre il tempo: risposta nei dubbi qui sotto
e all'utente, senza modifiche al codice. (3) Domanda sul nodo `CO call → BTN all-in → CO` con una
strategia mentre alla radice `call` vale 0,0 %: la frequenza di limp alla radice è 3,5·10⁻⁶ di
range (massimo 5,6·10⁻⁵ per 87s); il nodo è praticamente irraggiungibile ma CFR aggiorna ogni
information set e la strategia media vi è definita (AA call 100 %, J6o fold 91 %); gli EV sono
condizionati al nodo (BTN spinge il 67 % delle mani dopo il limp). Nessuna modifica al codice.
(4) Note datate aggiunte a P4 §4 e P6 §5 sul cambio delle fixture HU10.
Comandi: `gtosd_preflop_blueprint_train …co40_test_v1.json --iterations 2000 --batch 32 --threads 8
--eval-flops 20 --eval-every 500 --policy-out out/policy_co40t_dcfr_200.bin`; `… --resume --checkpoint
out/ckpt_co40t_10k.bin --iterations 10000 --eval-every 1000 --policy-out out/policy_co40t_10k.bin`;
`… --scheme linear --update simultaneous --iterations 2000 --policy-out out/policy_co40t_linear.bin`;
`certify --threads 8 --chunk 16`; `export --certificate … --eval-flops 60 --postflop-tree …`;
`generate_chart_data.py --blueprint … (tre export) --postflop-tree out/r3_postflop_tree_hu10_full.json`.
Risultati: CO40 test (1129 nodi, 456 decisioni; 0.234 s per iterazione DCFR,
0.790 s Linear):

| Run | Training | Stima a 20 flop | Exploitability esatta | % piatto | % stack | nashconv | Limite inferiore dal flop | EV CO | Certificazione |
|---|---|---|---|---|---|---|---|---|---|
| DCFR alternato, 2.000 it. | 8 min | 0,6784 ± 0,1055 a | **0,6569 a** | 21,9 % | 1,64 % | 1,0278 a | 0,2622 a | -0,1448 a | 23 min |
| DCFR alternato, 10.000 it. (proseguimento) | 38 min | 0,9366 ± 0,1178 a | **0,8406 a** | 28,0 % | 2,10 % | 1,2467 a | 0,2978 a | -0,1459 a | 23 min |
| Linear simultaneo, 2.000 it. | 26 min (in parallelo a una certificazione) | 1,0783 ± 0,1072 a | **1,0862 a** | 36,2 % | 2,72 % | 1,5226 a | 0,3559 a | -0,1460 a | 23 min |

con DCFR alternato la exploitability esatta sale fra 2.000 e 10.000 iterazioni (la strategia media peggiora); Linear simultaneo a 2.000 iterazioni è peggiore: lo schema non è la causa principale. Confronto descrittivo con il riferimento Monker CO40 (comparatore su DCFR 2.000, verdetto `REJECTED`, contratto esterno incompleto): alla radice variazione totale media di classe 26.2 pp, errore massimo per azione 23.8 pp, differenza di EV di radice 0,159 a: CO limpa e spinge 40 a con frequenze che Monker non ha (AA limp 97 %, T9s all-in 82 %).. Per confronto HU10 completo a 2.000 iterazioni DCFR: 0,0040 a (0,13 % del piatto).
Radice CO (DCFR alternato, 2.000 iterazioni): fold 33,0 %, all in 32,6 %, call 31,4 %, raise 10 2,5 %, raise 6 0,6 %; EV di radice -0,1411 a; 20 nodi preflop; albero postflop
9 entry, 436 nodi decisionali, 992 archi. Verdetto D2 (soglia 0,1 a): REJECTED per la variante
di test con questi run; il gate P9 resta sul CO40 completo.
Fallimenti: (1) La stima del mattino "2.000–10.000 iterazioni" per CO40 era una supposizione:
con il protocollo HU10 la variante di test resta lontana dall'equilibrio (tabella sopra).
(2) La stima campionata a 20 flop di DCFR alternato cresce con le iterazioni (0,68 a a 2.000,
0,94 a a 10.000): su HU10 non era successo.
Dubbi: (1) Exploitability e tempo: il costo del training si riduce fermandosi prima (su HU10
l'1 % del piatto arriva a circa 50 iterazioni, 10 s), ma la certificazione esatta costa lo stesso
qualunque sia la soglia (35 min su HU10 completo, 22 min su CO40 test, 10,2 h su CO40 completo
a 8 thread); la regola D3 campionata con `M = 1.000` costa più della passata esatta e, per il
bias dello stimatore, di fatto richiede una exploitability vera intorno allo 0,1–0,2 % del piatto
per dichiarare l'1 %. Per fermarsi davvero all'1 % servirebbe usare il certificatore esatto come
regola di arresto (economico sugli alberi piccoli) o un limite inferiore senza bias. Su CO40 il
problema è opposto: con il protocollo HU10 non si scende sotto il 22 % del piatto.
(2) Nel viewer la classe è segnata "fuori percorso" sotto `1e-6` di reach proprio: al nodo dopo
il limp molte classi restano fra `1e-6` e `6e-5` e non sono segnate; una soglia sul reach del
nodo intero sarebbe più leggibile (non implementata: nessuna modifica richiesta). (3) La parte
postflop di CO40 test vale da sola 0,2622 a di exploitability nel run migliore: con 40 a di
stack i bucket 200/500/1.000 costruiti per HU10 possono essere un limite; P9.1 (Linear contro
DCFR, tre seed, capacità 500/1.000/2.000) e la diagnosi astrazione/algoritmo/budget restano da fare.
Prossimo passo: merge e push; P9 secondo l'indicazione dell'utente, partendo da questa diagnosi.

### 2026-09-16 — P8 — contro l'open 5a solo fold/call/all-in, scaling sui thread, curva exploitability/iterazioni, erratum sulle proiezioni del certificatore

Fatto: (1) Domande dell'utente: come essere certi della convergenza a Nash (la exploitability
esatta certificata da P7 è la distanza da un equilibrio nel gioco fisico con questo albero:
nessuna strategia guadagna più di 0,0041 a per mano contro il blueprint, 0,14 % del piatto),
differenza fra le due sorgenti del viewer (stesso algoritmo DCFR alternato, alberi diversi: tre
size postflop contro una), tempi su un AMD EPYC 7443, costo di una exploitability dell'1 % del
piatto, stato della documentazione. (2) Regola dell'utente: in HU10 contro l'open 5 a BTN ha solo
fold, call e all-in. Il loader accetta una lista di risposte vuota (nessuna size di rilancio
sopra un open: al livello 1 solo fold/call/all-in, decisione 46), schema con `minItems: 0`,
validatore Python delle fixture aggiornato, fixture HU10 con `response_target_units: []`, test
dello scaffolding (lista vuota accettata, casi di rifiuto spostati sulla fixture CO40). Alberi:
HU10 completo 1.501 nodi (584 decisioni; preflop 22 nodi, 8 decisioni, 3 entry postflop),
ridotto 193 nodi (80 decisioni). Nell'export precedente BTN usava il rilancio a 8 a contro
l'open 5 a per il 6 % del range (fold 20 %, call 19 %, all-in 55 %). (3) Scaling sui thread su
HU10 completo (i3-10100F, 4 core / 8 thread): training di 100 iterazioni a 1/2/4/8 thread;
certificatore su 8 flop con `--chunk 1` e con `--chunk 8`. (4) Curva della exploitability esatta
in funzione delle iterazioni (albero ridotto: 50, 100, 200, 400, 700, 1.000, 2.000; albero
completo: 2.000 più i punti attorno all'1 % del piatto), run di riferimento a 2.000 iterazioni
certificati ed esportati (`out/r3_*`, `out/policy3_*`), viewer rigenerato. Report:
[P8_EXPORT.md](P8_EXPORT.md) §10.
Comandi: `ctest -L "p0|p4|p6|p7|p8"`; `gtosd_preflop_blueprint_train …hu10_full_v1.json --iterations 100
--batch 32 --threads T --eval-every 0`; `gtosd_preflop_blueprint_certify …hu10_full_v1.json --uniform
--threads T --chunk 8 --flop-limit 8`; per N in 50…1000: `train …hu10_reduced_v1.json --iterations N
--eval-every 0 --policy-out out/curve_red_policy_N.bin` e `certify --policy … --threads 8 --chunk 16`;
run di riferimento come nella voce precedente (`policy3_*`, `r3_cert_*`, `r3_chart_*`).
Risultati: test 15/15 PASS (172 s). Scaling del training: 0,433 / 0,276 / 0,211 / 0,194 s per
iterazione a 1/2/4/8 thread (2,2× a 8 thread; frazione seriale ≈ 0,32 con 30 unità e 10 nodi in
alto seriali). Certificatore con `--chunk 8`: 13,3 / 7,3 / 4,2 / 3,2 s per flop (4,2× a 8 thread,
1,32× dai thread SMT); con `--chunk 1`: 13,4 s per flop a qualsiasi numero di thread, perché il
parallelismo è sui flop dello stesso chunk. Curva (P8 §10): ridotto: 0,0191 a (0,64 % del piatto) a 20 iterazioni, training 3 s; completo: 0,0111 a (0,37 % del piatto) a 200 iterazioni, training 37 s; a 2.000 iterazioni 0,0040 a (ridotto) e 0,0040 a (completo). Run di riferimento con la nuova regola: completo 1.501 nodi, training 7 min (+ 5 min di valutazioni), certificato esatto **0,0040 a** in 35 min, EV di radice 0,1325 a; ridotto 193 nodi, training 5 min, certificato esatto 0,0040 a in 3 min; albero postflop 3 entry, 576 nodi decisionali, 1396 archi. CO40 completo con chunk 8 e 8 thread: 64 s per flop canonico misurati su 8 flop con chunk 8 e 8 thread, passata esatta ≈ 10,2 h (era 22,4 h).
Fallimenti: (1) Il validatore Python delle fixture imponeva liste di open e di risposte della
stessa lunghezza anche con risposte vuote: test dello schema FAIL, corretto. (2) **Erratum sulle
proiezioni del certificatore**: le misure con `--chunk 1` (CO40-TEST, voce precedente: 8,5 s per
flop, passata esatta 1,35 h) e con `--chunk 2` (CO40 completo, P7 §5: 141 s per flop, 22,4 h)
avevano rispettivamente uno e due thread attivi, non otto; a 8 thread con chunk ≥ 8 il costo è
circa 4,2 (da chunk 1) e 2,3 (da chunk 2) volte minore. CO40-TEST: ≈ 2,0 s per flop, passata
esatta ≈ 20 min. CO40 completo: 64 s per flop canonico misurati su 8 flop con chunk 8 e 8 thread, passata esatta ≈ 10,2 h (era 22,4 h) (decisione 47). (3) Lo script di
interruzione della catena ha terminato anche il proprio lanciatore (il pattern sul nome dello
script compariva nella sua riga di comando): catena rilanciata separatamente.
Dubbi: (1) Il training scala poco (2,2× su 4 core): la parte alta seriale e le 30 unità di HU10
limitano il parallelismo; su CO40 completo (211 unità) la frazione parallela è maggiore ma non
misurata. (2) La stima per l'EPYC 7443 assume una velocità per core simile all'i3-10100F (Zen 3 a
3,6–4,0 GHz con IPC maggiore contro Comet Lake a 4,1–4,3 GHz) e scaling del certificatore
lineare sui flop con chunk ≥ thread: va verificata sulla macchina. (3) La curva exploitability /
iterazioni è misurata su HU10: su CO40 la forma può essere diversa (albero più profondo).
Prossimo passo: merge del branch di fase nell'integrazione e in `main`, push dei branch; P9 sul CO40 completo (passata esatta ≈ 10 h a 8 thread su questa macchina) o sulla variante di test, secondo l'utente.

### 2026-09-16 — P8 — EV condizionato corretto fuori dalla radice, size preflop HU10 solo 5a/8a, CO40 di test

Fatto: (1) L'utente ha chiesto perché `J6o` compare nel nodo `CO raise 5a → BTN all-in → CO`
del viewer se CO non rilancia mai J6o. L'export riporta la strategia media di tutte le 81 classi a
ogni nodo: alla radice CO rilancia a 5 a J6o con frequenza 3,1·10⁻⁹ (residuo delle prime
iterazioni nella media DCFR), che è il reach mostrato dal viewer (3,12·10⁻⁷ %); la riga è quindi
attesa. L'analisi ha però trovato un errore negli EV fuori dalla radice: l'EV di classe usava il
valore controfattuale `v_a[h]` (già moltiplicato per la reach avversaria `D[h]`) pesato di nuovo
con `D[h]`, invece di `v_a[h] / D[h]`; alla radice `D[h] = 1` e il test di ricostruzione
passava, ai nodi interni l'EV era scalato per `D[h]` (fold −3,39 a per J6o e −3,14 a per AA nello
stesso nodo invece della perdita costante). Correzione in `preflop_action_values` (valore e serie
per flop dell'errore standard, decisione 43) e nuovo test: a ogni nodo interno con arco di fold
l'EV di fold di ogni classe è uguale al payoff di fold entro `1e-9`. (2) Decisione dell'utente:
le size preflop di HU10 diventano solo full pot (open 5 a, risposta 8 a) in entrambe le fixture
(completa e ridotta); test dello scaffolding e del gioco, smoke di query (`raise_5,call`) e
fixture aggiornati. (3) Decisione dell'utente: variante CO40 solo per i test con una size postflop
(100 % del piatto) più all-in, fixture `preflop_blueprint_co40_test_v1.json`
(`PREFLOP-BLUEPRINT-CO40-TEST-001`, stesse size preflop Monker 6 / 10 a e 10,5 / 14,5 a,
decisione 44), misure di tempo su 20 iterazioni e 2 flop canonici. (4) Riaddestramento HU10
(completo e ridotto) con le nuove size, certificazione esatta, export delle chart e dell'albero
postflop, rigenerazione del viewer. (5) Viewer: le classi con reach proprio lungo la history sotto
`1e-6` restano nella griglia ma desaturate, con il reach nel tooltip (decisione 45, commit
`21617ef` del viewer). Report: [P8_EXPORT.md](P8_EXPORT.md) §9.
Comandi: `cmake --build … --target <16 eseguibili blueprint>` e `ctest -L "p0|p4|p6|p7|p8"`;
`gtosd_preflop_blueprint_game --config …co40_test_v1.json`; `gtosd_preflop_blueprint_train …co40_test_v1.json
--iterations 20 --batch 32 --threads 8 --eval-every 0`; `gtosd_preflop_blueprint_certify
…co40_test_v1.json --uniform --threads 8 --chunk 1 --flop-limit 2`; per HU10 completo e ridotto:
`train --iterations 2000 --batch 32 --threads 8 --eval-flops 20 --eval-every 500 --policy-out out/policy2_*.bin`,
`certify --threads 8 --chunk 16 --state out/r2_state_*.bin`, `export --certificate out/r2_cert_*.json
--eval-flops 60 --threads 8` (chart e albero postflop); `generate_chart_data.py --blueprint out/r2_chart_hu10_full.json
--blueprint out/r2_chart_hu10_reduced.json --postflop-tree out/r2_postflop_tree_hu10_full.json`.
Risultati: test 15/15 PASS (191 s). CO40-TEST: 1.129 nodi (456 decisioni, 20 preflop) contro
27.012 (10.060) di CO40; stato 16,9 MB con 200/500/1.000; training 0,223 s per iterazione
(`B = 32`, 8 thread; CO40 completo ≈ 3 s, HU10 completo 0,27 s); certificazione 8,5 s per flop
canonico (CO40 completo 141 s): passata esatta su 573 flop ≈ 81 min (1,35 h) contro 22,4 h;
valutazione campionata D3 con `M = 1.000` ≈ 2,4 h (più della passata esatta, come su HU10), con
`M = 200` ≈ 28 min. Stima della risoluzione di CO40-TEST: 2.000 iterazioni 7,5 min, 10.000
iterazioni 37 min, più valutazioni periodiche (`M = 20` ≈ 3 min ciascuna) e certificazione esatta
di 1,35 h: fra 1,5 e 2,5 h in totale contro circa un giorno per CO40 completo (non è noto quante
iterazioni servano a CO40 per D3: HU10 ne ha richieste 2.000). HU10 con le nuove size:
completo 1.567 nodi, training 7 min (+ 4 min di valutazioni), certificato esatto **0,0041 a** in 31 min; ridotto 259 nodi, training 5 min, certificato esatto 0,0040 a in 4 min; EV di radice 0,1322 a (completo) e 0,1321 a (ridotto); export con badge `CERTIFIED_EXACT`; Esempio (export completo, nodo `CO raise 5a → BTN all-in → CO`): fold −6,000 a per J6o e −6,000 a per AA (il payoff di fold, costante), call −2,642 a per J6o e 4,528 a per AA. Albero postflop esportato: 600 nodi decisionali, 1.444 archi, 5 entry.
Fallimenti: (1) `LNK1104` su `gtosd_preflop_blueprint_export.exe`: il worker `--serve` del viewer
teneva aperto l'eseguibile; server fermato prima della build. (2) Lo script di build della catena
non ricompilava `gtosd_preflop_blueprint_scaffold_tests` (elenco parziale di target) e il test
vecchio falliva sulle nuove fixture: lo script costruisce ora tutti i 16 eseguibili blueprint.
(3) L'errore dell'EV non era coperto dai test: il controllo di ricostruzione era solo alla radice,
dove `D[h] = 1`; gli export `p8_chart_hu10_*` e la verifica del viewer di P8 §4–§5 hanno EV
fuori dalla radice sbagliati (frequenze, EV di radice, badge e verdetti corretti): superati dagli
export `r2_*` (P8 §9).
Dubbi: (1) Il "da non fare" di P9 (non cambiare albero o size) riguarda la qualificazione: la
variante CO40-TEST serve alle misure di tempo e alle prove; il gate P9 resta sull'albero CO40
completo salvo diversa indicazione dell'utente. (2) Con una sola size postflop l'albero CO40 di
test ha 24 volte meno nodi del completo: le proiezioni non si trasferiscono linearmente alle tre
size. (3) Le size preflop HU10 dell'utente (solo 5 a / 8 a) rendono i risultati HU10 di P6–P8 non
confrontabili con i nuovi: i vecchi restano nei report come storia.
Prossimo passo: merge del branch di fase nell'integrazione e in `main` (autorizzazione del
2026-09-16), push dei branch; P9 sul CO40 completo o sulla variante di test secondo l'utente.

### 2026-09-16 — P8 — viewer aggiornato e domande risolte, gate PASS

Fatto: l'utente ha risposto alle domande aperte: Q1 branch pubblicati su origin (10 branch
`feature/preflop-blueprint*` e 3 tag), Q2/Q3 merge dell'integrazione in `main` eseguito nel suo
working tree (`97d8121`, tag `preflop-blueprint-p3-abstraction` su `981361e`,
`preflop-blueprint-p6-hu10` su `22e1015`, `preflop-blueprint-p8-export` su `9360836`; `main` non
pushato: non richiesto), Q4 viewer modificabile. Nel repository: export dell'albero pubblico
postflop nello schema del viewer, sonda per nodo nel valutatore, worker di query postflop
(`--serve`) con frequenze, reach ed EV per classe (esatti a turn e river, runout campionati al
flop), test e smoke. Nel repository del viewer (branch `feature/preflop-blueprint-p8-export`,
modifiche locali preesistenti conservate in `56cdf31`, aggiornamento `4edbb45`): generatore con
sorgenti blueprint e badge, frontend con azioni e gioco dinamici, server con backend blueprint,
validatori generici, README. Report: [P8_EXPORT.md](P8_EXPORT.md) §5.
Comandi: `ctest -L p8 -V`; export dell'albero HU10; `generate_chart_data.py --blueprint … --postflop-tree …`;
`serve_viewer.py --backend blueprint …`; verifica nel browser integrato.
Risultati: `ctest -L p8` 7/7; worker: EV di classe al flop uguale al valutatore entro `1e-9`,
flop 0,4–1,3 s (16–64 runout), turn 0,5 s esatto, river istantaneo; viewer: "HU 10a Chart Viewer",
badge `CERTIFIED EXACT · exploitability 0.0042a (0.14% pot)`, 20 nodi preflop e albero postflop
(9 entry, 792 nodi) navigabili, query sul board `Ac Kd Qh` con frequenze ed EV per classe.
Fallimenti: (1) `reference?.ev !== null` nel viewer con riferimento assente: eccezione nel render
della matrice, guardia aggiunta. (2) Il flop con tutti i 1.056 runout costa 17,6 s nel worker
(sonda sequenziale sui turn): il viewer usa runout campionati; parallelizzazione rinviata.
(3) Le commit `b41cee2` e `9d05224` sono nate direttamente sul branch di integrazione
invece che sul branch di fase (checkout non tornato sul branch dopo il merge): il branch di
fase è stato riallineato a `9d05224` con fast-forward e la deviazione da D21 è registrata qui.
Dubbi: (1) Le size preflop di HU10 (open 3 a / 5 a, risposte 6 a / 8 a) vengono dal fixture di
calibrazione legacy `hu_preflop_hu10_calibration_v1.json` (D6, commit `04aa687`) e non sono state
scelte in questo programma: HU10 è solo il gioco di validazione, CO40 usa il contratto Monker
(6 a / 10 a, 10,5 a / 14,5 a). (2) La stima di 22 h per la certificazione esatta di CO40 riguarda
solo il certificatore P7 (605.088 board a 1,07 s per board per thread, dominati dai kernel all-in
di flop e turn): il training CO40 costa circa 3 s per iterazione (10 volte HU10), quindi ore, non
giorni; le riduzioni possibili sono descritte in P7 §5.
Prossimo passo: P9 sul branch `feature/preflop-blueprint-p9-co40`.

### 2026-09-16 — P8 — export, query, comparatore, viewer, gate PASS con riserva (viewer INCONCLUSIVE)

Fatto: id stabili di azione e di nodo (`action_labels`), query della policy per history + combo +
board senza ricalcolo di feature (`policy_query`), valori per azione ai nodi preflop con EV
condizionato per classe ed errore standard sui flop campionati
(`BestResponseEvaluator::preflop_action_values`), export `gtosd.preflop_blueprint_chart.v1` con
schema JSON, quattro fingerprint, badge `ESTIMATED` / `CERTIFIED_EXACT` e checksum nel layout
letto dal generatore del viewer, comparatore con verdetto sulla exploitability fisica (D2) e
distanze Monker descrittive (`EXTERNAL_CONTRACT_INCOMPLETE`), validatore statico registrato in
CTest, eseguibili `gtosd_preflop_blueprint_export` (export e query) e
`gtosd_preflop_blueprint_compare`, smoke a catena tramite fixture CTest. Report:
[P8_EXPORT.md](P8_EXPORT.md).
Comandi: `ctest -L p8 -V`; export di HU10 ridotto e completo dalle policy P6 con i certificati
P7 (60 flop, 8 thread); export senza certificato e baseline Linear; query di esempio;
comparatore candidato/baseline/riferimento; validatore; suite `preflop_blueprint`.
Risultati: test 20.380 asserzioni PASS (812 nodi decisionali etichettati, 160 cammini di query
su tutte le strade uguali al `BoardContext`, EV di radice ricostruito dalle classi entro `1e-9`,
verdetti del comparatore, parser Monker a distanza zero su un riferimento sintetico); export HU10
completo certificato: 20 nodi, 1.620 righe, badge `CERTIFIED_EXACT` con 0,0042 a esatti, 239 s
per 60 flop; comparatore `QUALIFIED` per il candidato certificato, `INCONCLUSIVE_ESTIMATE` per lo
stesso senza certificato (stima 0,061 ± 0,05 a contro 0,1 a), `STALE_TREE` fra alberi diversi,
DCFR contro Linear sullo stesso albero confrontati su 20 nodi; validatore PASS sui tre export.
Suite `preflop_blueprint`: PASS: 23/23 in 505 s (`ctest -L preflop_blueprint`, Release, dopo la correzione del check di isolamento).
Fallimenti: (1) helper JSON `quoted` in conflitto con `std::quoted` per ADL; (2) target
`nlohmann_json` non visibile nella directory dei test; (3) cammini casuali del test di query
troppo brevi per il river; (4) smoke con `DEPENDS` non eseguiti fuori etichetta: fixture CTest.
Dubbi: (1) l'EV per azione è condizionato al nodo (diviso per la reach avversaria): nei nodi
profondi con reach piccola gli errori standard sono grandi a 60 flop; il viewer dovrebbe mostrare
la reach. (2) La strategia corrente non è nel file di policy: se il viewer la vuole, va aggiunta
al formato `GTOSDPOL` (opzionale, diagnostica). (3) Il riferimento Monker CO40 non è confrontabile
con HU10 (azioni diverse); il confronto ha senso solo in P9.
Prossimo passo: decisione dell'utente su Q4 (viewer) e Q2/Q3 (merge in `main`); poi P9 sul
branch `feature/preflop-blueprint-p9-co40` con la passata esatta CO40 pianificata come lavoro a
chunk ripristinabile (22 h stimate) o ridotta con le ottimizzazioni indicate in P7.

### 2026-09-16 — P7 — certificatore board-major, gate PASS

Fatto: valutatore di best response riorganizzato in due stadi (`BestResponseEvaluator`: valori
per flop, aggregazione preflop) con immagini di orbita: ogni flop canonico valutato una volta con
tutti i runout e sommato su tutte le sue immagini nei semi (la strategia è simmetrica: valore di
`h` su `σ(F)` = valore di `σ⁻¹(h)` su `F`), 573 flop canonici per 7.140 fisici e 605.088 board;
certificatore a chunk paralleli con stato ripristinabile (record per flop con checksum, header con
fingerprint di albero, policy e catalogo), passate parziali per la misura, comando campionato (lo
stimatore P6), certificato JSON `gtosd.preflop_blueprint_certificate.v1`; file di policy
`GTOSDPOL` scritto dal trainer (`--policy-out`) e letto dal certificatore; helper binari
condivisi; header di test condiviso. Report: [P7_CERTIFIER.md](P7_CERTIFIER.md).
Comandi: `ctest -L p7 -V`; esportazione delle policy dai checkpoint P6 (HU10 ridotto e completo,
DCFR 200/500/1.000, 2.000 iterazioni); `gtosd_preflop_blueprint_certify` esatto su HU10 ridotto e
completo (8 thread, chunk 16, stato su file); parziale su CO40 (policy uniforme, 4 flop) per la
proiezione; campionato a 60 flop su HU10 completo; suite `preflop_blueprint`.
Risultati: test 373.068 asserzioni PASS (orbite = enumerazione fisica entro `1e-12` per combo e
in aggregato; ripresa bit-identica; comando campionato = trainer entro `1e-12`; round trip della
policy). Passata esatta: HU10 ridotto EV CO 0,136084 a, guadagni 0,003031 /
0,004200 a, nashconv 0,007231 a, limite inferiore 0,001379 / 0,000905 a, 453 s (0,79 s per flop
canonico, 8 thread), 251 MB; HU10 completo EV CO 0,136090 a, guadagni 0,003118 / 0,004228 a,
nashconv 0,007346 a, 2.220 s (ripresa da 16 flop dopo l'interruzione della sessione), 324 MB;
`EV_CO + EV_BTN = 0` entro `1e-16`. Exploitability vera del blueprint HU10: 0,0042 a = 0,14 % del
piatto (D3 1 %, D2 0,1 a). Stimatore P6 sugli stessi checkpoint: naive a 1.000 flop 0,0187 ±
0,0079 a, limite inferiore 0,0014 a: il valore esatto sta nell'intervallo e il bias della naive
(0,0145 a) è `0,46/√1000` come misurato in P6. CO40 parziale (4 flop canonici, 40 fisici, 4.224
board, policy uniforme): 563 s, 141 s per flop canonico, 1,07 s per board per thread, proiezione
22,4 h per la passata esatta con 8 thread. Suite `preflop_blueprint`: 18/18 PASS in 506 s.
Fallimenti: (1) `Result<T,E>` richiede `T` costruibile per default: `load_policy` restituisce un
`unique_ptr`. (2) Invariante "EV a somma zero" applicato a un sottoinsieme di flop: vale solo sul
catalogo completo perché i due giocatori condizionano su conteggi di flop compatibili diversi;
test limitato alla passata esatta. (3) Script di refactoring degli helper binari lasciato a metà
(parentesi residua in `trainer.cpp`): corretto al primo build.
Dubbi: (1) Su CO40 il costo per board (1,07 s) è 12 volte la traversata P5 per i
terminali all-in di flop e turn (kernel showdown per terminale e per board): le riduzioni
(kernel all-in per (flop, turn), simmetrie sotto lo stabilizzatore, `float`) sono rinviate dopo
P9 per §3.4, ma la passata esatta CO40 va pianificata come lavoro notturno con ripresa. (2)
L'invariante "EV a somma zero" vale solo sul catalogo completo: sulle stime campionate i due
giocatori condizionano su insiemi di flop compatibili diversi; da tenere presente nel viewer P8
quando mostra EV campionati.
Prossimo passo: merge di P7 nell'integrazione; P8 (export, query, comparatore, viewer) sul
branch `feature/preflop-blueprint-p8-export`, con il certificato P7 come fonte del numero
dichiarato e il file di policy come formato di scambio.

### 2026-09-16 — P6 — trainer con campionamento del board, gate PASS

Fatto: trainer CFR vettoriale con campionamento pubblico del board (batch `B`, un passaggio per
giocatore, snapshot della strategia per passaggio, Linear/DCFR una volta per iterazione,
partizione dell'albero in unità indipendenti dal numero di thread con un solo scrittore per
cella, hook esatti per liste di board pesate e sottoinsiemi di mani, checkpoint atomico con
checksum e identità, telemetria); oracolo `FiniteGame` a bucket sul gioco ridotto; valutatore di
best response fisica **non chiaroveggente** (`best_response.hpp`): valori aggregati sulle carte
future prima del massimo, campionamento di `M` flop con tutti i 33 × 32 runout, errore standard
sui flop, stima naive più limite inferiore senza selezione (strategia media al preflop, best
response esatta dal flop in poi); test contro il `FiniteGame` lossless; eseguibile con
`--eval-flops`, `--eval-only`, `--eval-seed` dopo il caricamento, `--fixed-boards`,
`--permute-suits`, default DCFR alternato. Report: [P6_TRAINER.md](P6_TRAINER.md).
Comandi: `ctest -L p6 -V`; `gtosd_preflop_blueprint_train` su HU10 completo e ridotto (2.000
iterazioni, `B = 32`, 8 thread, 20 flop ogni 250 iterazioni) con DCFR alternato e Linear
simultaneo e con le tabelle 50/100/200, 200/500/1.000, 500/1.000/2.000; `--fixed-boards 1/4/8/64`
sul ridotto (valutazione esatta sulla lista); `--eval-only` sui checkpoint finali con 60 flop,
scansione `M = 5…160` sul ridotto e `M = 1.000` per il gate; suite `preflop_blueprint` completa.
Risultati: oracolo regret entro `1,7·10⁻¹³`, strategia media entro `1e-9`; bit-identità 1/2/4/8
thread e partizioni 1/8/58 unità; ripresa identica; best response fisica = `calculate_nash_conv`
del `FiniteGame` lossless entro `1e-9` (nashconv 0,0931106). HU10 completo, DCFR alternato,
200/500/1.000: 0,31 s per iterazione, valutazione a 20 flop 93–122 s (21.120 board, circa 42 ms
per board per thread), memoria 245 MB; massimo guadagno naive 0,099 / 0,108 / 0,084 / 0,091 /
0,108 / 0,137 / 0,122 / 0,085 a alle iterazioni 250…2.000 (semiampiezza 0,04–0,10 a), EV CO
0,136 a. Stesse iterazioni e stessi flop: HU10 ridotto 0,100…0,085 a; 50/100/200 0,098…0,087 a;
500/1.000/2.000 0,104…0,087 a; Linear simultaneo 0,18…0,24 a. Scansione di `M` sul checkpoint
ridotto: naive 0,239 / 0,171 / 0,093 / 0,066 / 0,045 / 0,037 a per `M = 5…160` (`naive · √M` ≈
0,4–0,5 a costante), limite inferiore 0,0005–0,0013 a. Checkpoint completi a `M = 60`: naive
0,045 / 0,048 / 0,046 a (± 0,019) per 200/500/1.000, 50/100/200, 500/1.000/2.000 con limite
inferiore 0,0013 / 0,0019 / 0,0010 a; Linear simultaneo naive 0,143 a, limite 0,0031 a. Board
fissi (valutazione esatta sulla lista, DCFR): 1 board 0,014 a a 100 iterazioni (regola D3
soddisfatta), 4 board 0,014 a, 8 board 0,025 a, 64 board 0,111 a, piatti fra 100 e 500
iterazioni (chiaroveggente: `1·10⁻⁴` / 0,105 / 0,163 / 0,473 a). Gate a `M = 1.000` flop
(1.056.000 board): massimo guadagno naive 0,0187 a + semiampiezza
0,0079 a = 0,0266 a ≤ 0,03 a (guadagni [0,0108, 0,0187] a, limite inferiore [0,0014, 0,0009] a,
4.796 s, 98 MB): `PREFLOP_BLUEPRINT_TRAIN=CONVERGED`, gate PASS. Suite `preflop_blueprint`: 16/16 PASS in 318 s.
Fallimenti: (1) lo stimatore P6.3 come prescritto (massimo per board) era chiaroveggente sopra
il river: plateau di circa 1 a su HU10 diagnosticato prima come pavimento dell'astrazione, poi
smentito dal confronto di capacità e dal test a semi ruotati; circa tre ore di esperimenti da
scartare, erratum alla roadmap §5, decisioni 30–31. (2) La stima naive sui flop campionati
sceglie e valuta le azioni preflop sugli stessi flop: bias `≈ 0,4/√M` a; la cross-fit provata
era negativa a ogni `M` e va scartata; aggiunto il limite inferiore senza selezione (decisione
32). (3) Semiampiezza non nulla sulle valutazioni esatte per lista: corretta a zero. (4)
`--eval-only` con `--eval-seed` diverso respinto per identità: il seme ora riavvia l'RNG dopo il
caricamento (decisione 33). (5) Errori del test di partizione (batch, identità, RNG) e link
mancante di `gtosd::best_response`: vedi report.
Dubbi: (1) tre capacità e due alberi danno curve naive identiche alla terza cifra perché la
stima a 20 flop è dominata dal rumore di selezione comune; la capacità si vede solo nel limite
inferiore (1,9 → 1,3 → 1,0 millesimi di ante), tutto sotto D3. (2) Il gioco ristretto a 64 board
fissi ha best response 0,111 a con un solo runout per flop: non misura l'astrazione del gioco
completo, dove il limite inferiore è di millesimi; la modalità a board fissi serve solo per la
convergenza. (3) La valutazione costa più del training (882 s contro 618 s su 2.000 iterazioni)
e la stima campionata per D3 richiede `M ≈ 1.000` flop (circa 95 min su HU10 completo): la
passata esatta di P7 su 573 flop canonici (605.088 board, circa 50 min) è più economica ed
esatta; conviene usare la stima campionata con `M` piccolo solo per la curva e certificare con P7.
(4) Nella stima campionata il giocatore più "sfruttabile" è il BTN, nel gioco a 64 board il CO:
entrambi effetti del rumore/della restrizione, non della strategia.
Prossimo passo: merge di P6 nell'integrazione; il gate P6 prevede anche il merge dell'integrazione
in `main` con tag `preflop-blueprint-p6-hu10` (Q3, stesso blocco di Q2); P7 sul branch
`feature/preflop-blueprint-p7-certifier` con la stessa aggregazione non chiaroveggente su tutti i
flop canonici.

### 2026-09-15 — P5 — kernel vettoriale HU, gate PASS

Fatto: contesto di board (465 mani vive, rank dalla tabella P2, ordine per rank, classi, bucket
P3 via permutazione canonica, incidenza per carta); kernel fold `D = S − C[h1] − C[h2] + r[h]`,
showdown a due passate con correzione dei blocker e gruppi di pari rank, cache all-in preflop
per board dalla tabella esatta; interfaccia `ShowdownKernel` a N reach (D13) con implementazione
HU; traversata dei valori senza allocazioni con policy a bucket e per mano, potatura a reach nulla
e modalità best response; compilazione di sottogiochi da uno stato arbitrario; test contro i
riferimenti pairwise, contro la ricorsione per coppia e contro il solver postflop `ProductionDcfr`
(solo test, D5); eseguibile di misura. Report: [P5_VECTOR_KERNELS.md](P5_VECTOR_KERNELS.md).
Comandi: build dei target P5; `ctest -L p5 -V`; `gtosd_preflop_blueprint_traversal` su CO40 e
HU10; configure `windows-asan` (RelWithDebInfo, `/fsanitize=address /bigobj`) e
`ctest -R "gtosd_preflop_blueprint_(kernel|game|oracle)_tests"`; suite CTest completa (65 test)
sull'integrazione dopo il merge di P3.
Risultati: kernel 1.636.010 asserzioni PASS in 7,7 s (200 board × 3 pattern di reach entro
`1e-12`; traversata contro ricorsione per coppia con errore massimo `6,5·10⁻¹³` ante; bucket contro
mano entro `1e-12`); oracolo postflop 179.342 asserzioni PASS: tre sottogiochi river (57, 117, 57
nodi) con errore massimo `1,4·10⁻¹⁴` ante sui valori condizionali per combo; ASan PASS senza
diagnostiche (gioco 7,0 s, kernel 43,3 s, oracolo 6,6 s). Tempo per board CO40 a macchina libera:
contesto 0,06 ms, cache all-in 2,05 ms, traversata 89,8 ms (policy a bucket) / 70,7 ms (per
mano), best response 72,5 ms; HU10 completa 9,4 ms. Suite completa sull'integrazione (build
completa 21 min, test 1.278 s): 61/65 PASS; i 4 test legacy `gtosd_river_*_preflight/smoke`
falliscono con "frozen v1 regression manifest fingerprint mismatch" perché il worktree ha
`benchmarks/fixtures/river_bucket_qualification_corpus_v1.json` in CRLF (`core.autocrlf=true`)
mentre il fingerprint congelato è sul testo LF del checkout dell'utente; con il file in LF i 4
test passano (65/65). Nessuna relazione con il codice del programma.
Fallimenti: (1) primo run dell'oracolo respinto dal solver postflop (`invalid_configuration`):
opzioni `ProductionDcfr` costruite a mano; sostituite da `resolve_postflop_production_options`.
(2) Build ASan dell'oracolo fallita per C1128 (troppe sezioni) in `postflop_solver.cpp`, libreria
fuori perimetro: risolto aggiungendo `/bigobj` ai flag del configure ASan, senza modifiche al
repository. (3) Le prime misure di tempo (300 ms per traversata) erano contaminate dalla suite in
esecuzione; rimisurate a macchina libera.
Dubbi: (1) il costo della traversata è dominato dai 15.922 terminali di showdown; ottimizzazioni
(float, vettorizzazione) rinviate dopo P9 per §3.4. (2) Il fingerprint dei manifest legacy è
sensibile ai fine riga: fragilità del legacy da segnalare, non da correggere in questo programma.
Prossimo passo: P6 sul branch `feature/preflop-blueprint-p6-trainer`.

### 2026-09-15 — P4 — modello di gioco e albero compilato, gate PASS

Fatto: layer di regole N-player (`game_model`): stato preflop a N giocatori (ante morte, button
blind vivo del BTN, primo posto ad agire), abstraction delle azioni come funzione del livello di
aggressione della street e del "facing all-in", transizioni HU delegate al core e fold
generalizzato per N > 2, avanzo di street con il primo giocatore attivo non all-in. Albero
compilato (`compiled_game`): un solo array in preordine con sottoalberi contigui, nodi Decision /
Chance / TerminalFold / TerminalShowdown, archi nell'ordine di `legal_actions`, payoff per ogni
sottoinsieme di vincitori settled una volta con `settle_terminal`, statistiche, fingerprint,
layout dello stato `(nodo, classe o bucket, azione)`. Test (`preflop_blueprint_game_tests`) e
eseguibile di report (`preflop_blueprint_game`). Report: [P4_GAME_MODEL.md](P4_GAME_MODEL.md).
Comandi: build dei target P4; `ctest -L p4 -V`; `gtosd_hu_preflop_tree` (legacy) per il confronto;
`ctest -L preflop_blueprint`.
Risultati: 836.981 asserzioni PASS in 0,6 s; controllo di isolamento PASS su 26 sorgenti;
regressione `preflop_blueprint` P0–P4 11/11 PASS (223 s). CO40 parte preflop 58 nodi, 20
decisioni, 9 ingressi, 19 fold, 10 all-in; fingerprint
legacy `fnv1a64:a68337fa567aa2d9` riprodotto dalla parte preflop dell'albero compilato; scheletro
postflop 27.012 nodi rappresentati, 10.060 decisioni (372 flop, 2.100 turn, 7.588 river), 25.944
archi azione, 1.059 frontiere chance, 7.942 fold, 6.715 showdown, 1.236 runout all-in, massimo 4
raise per street, compilazione 0,026 s; stato R+S in double: 356.617.872 B con 200/500/1.000 e
714.889.872 B con 500/1.000/2.000 (preflop 4.617 celle, flop 216.000, turn 2.796.000, river
19.272.000 con la baseline). HU10 completa 2.059 nodi (812 decisioni), HU10 ridotta 571 nodi
(236 decisioni). Albero 3-way (UTG, CO, BTN, 40a) solo preflop: 580 nodi, 234 decisioni, 75
ingressi, 115 fold, 156 runout all-in; il fold generalizzato restituisce l'eccesso non chiamato
(UTG raise 6a, due fold: UTG +3a, CO −1a, BTN −2a).
Fallimenti: (1) primo run del test fallito sul conteggio atteso 30.324 / 11.308 della roadmap;
il benchmark legacy `gtosd_hu_preflop_tree` sul codice attuale misura 27.012 / 10.060 / 25.944,
identici all'albero compilato classe per classe: il valore della roadmap era documentazione
stale. Costanti attese corrette (decisione 20). (2) Il controllo di isolamento ha rifiutato un
commento dell'header che citava il costruttore HU legacy per nome (pattern `hu_preflop`);
commento riformulato.
Dubbi: la roadmap cita anche una profondità massima 15 dallo scheletro legacy; l'albero compilato
misura 17 dalla radice preflop (2 livelli in più per il tratto preflop fino all'ingresso). Il
postflop multiway non è compilato in P4 (P10); la "call per meno" a N > 2 è rifiutata perché con
stack uguali non si presenta e i side pot non sono modellati.
Prossimo passo: P5 sul branch `feature/preflop-blueprint-p5-kernel`.

### 2026-09-15 — P3 — clustering e tabelle bucket, gate PASS

Fatto: k-means intero con k-means++ e riavvii su campione sistematico; EMD esatta (L1 delle
cumulate, centroidi mediane pesate) per flop e turn, L2 sui vettori OCHS al river; tabelle
`uint16` per (board canonico, combo) con centroidi, parametri e fingerprint incorporati; lookup a
tempo costante da board fisico e mano; diagnostica di occupazione, inerzia e distanza media;
gruppi avversari da ranking per test e smoke; eseguibile con report JSON, salvataggio e verifica
di ricaricamento. Report: [P3_BUCKET_TABLES.md](P3_BUCKET_TABLES.md).
Comandi: build dei target P3; `ctest -L p3 -V`; costruzione completa con
`--flop 200 --turn 500 --river 1000 --restarts 10 --screening-iterations 10 --max-iterations 25
--screening-sample 500000` dalle risorse P2; `ctest -L preflop_blueprint`.
Risultati: 16.431.981 asserzioni PASS in 47,85 s (flop K=32, turn e river K=64, indipendenza dai
thread 1/3/8, invarianza ai semi, persistenza, rifiuto dei file corrotti); smoke 21,7 s PASS.
Tabelle 200/500/1.000 a 8 thread: flop 25 iterazioni, inerzia 5,68·10⁸, distanza media 150,7
(2,2 % del massimo), occupazione 449–3.035 righe, 55,5 s; turn 8 iterazioni (convergenza),
inerzia 9,49·10⁸, distanza media 8,12 (1,8 %), occupazione 2.399–102.648, 416 s; river 25
iterazioni, inerzia 1,68·10¹⁶, RMS per coordinata 0,050 di equity, occupazione 1.239–326.951,
814 s; nessun bucket vuoto; 43,3 MB in tre file; ricaricamento verificato; totale 1.294 s.
Fingerprint flop `fnv1a64:33f06cf437f8f26d`, turn `fnv1a64:51814338fcf1236c`, river
`fnv1a64:2e59aa76f59c0fcd`. Regressione `preflop_blueprint` P0–P3: 9/9 PASS (223 s con i test
P4 in corso; il controllo di isolamento è fallito una volta su un commento del codice P4, non su
P3, ed è stato ripetuto dopo la correzione).
Fallimenti: (1) accesso ai membri privati dal builder tramite classe derivata: non compila,
sostituito dal pattern attorney. (2) Lancio in background tramite il wrapper Visual Studio:
messaggio non fatale su `vswhere.exe` e log apparentemente vuoto mentre il processo girava; un
rilancio diretto ha fallito per il lock del log; un terzo lancio ha creato un processo duplicato,
terminato dopo 30 s. Il run originale è arrivato a PASS. Regola adottata: controllare i processi
con `Get-Process` prima di rilanciare.
Dubbi: flop e river si fermano al limite di 25 iterazioni (il turn converge in 8); per le tabelle
finali di P9 misurare 50 e 100 iterazioni. Il merge in `main` previsto da D21 al gate P3 non può
essere eseguito dall'agent senza toccare il working tree dell'utente: domanda Q2.
Prossimo passo: P4 sul branch `feature/preflop-blueprint-p4-game-model`.

### 2026-09-15 — P2 — risorse esatte, gate PASS

Fatto: tabella di rank ordinali a 16 bit derivata dall'oracolo esatto a 5 carte (1.404 rank
distinti); kernel di conteggio degli esiti con blocker in n log n più riferimento pairwise;
tabella all-in preflop esatta per le 176.715 coppie disgiunte; 8 gruppi avversari per equity;
istogrammi esatti flop (465 runout) e turn (30 river) e equity river per gruppi, per board
canonico e combo nel frame canonico; contenitore di risorse con checksum; eseguibile con report,
verifica oracolo, scrittura e ricaricamento. Report: [P2_EXACT_RESOURCES.md](P2_EXACT_RESOURCES.md).
Comandi: build dei target P2; `ctest -L p2 -V`; eseguibile con `--output-dir` e
`--verify-oracle 200000`; `ctest -L preflop_blueprint`.
Risultati: 9.868.560 asserzioni PASS in 84 s; 0 discrepanze con l'oracolo su 200.000 campioni;
tempi con 8 thread: rank 1,4 s, all-in 53 s, flop 2,1–2,4 s, turn 3,3–4,1 s, river 4,7–4,8 s;
7 file per 404.579.533 B scritti e ricaricati; regressione P0–P2 7/7 in 171 s; equity AA contro
mano casuale 0,7308; masse dei gruppi `78, 74, 84, 78, 76, 78, 88, 74`.
Fallimenti: (1) costante attesa delle coppie disgiunte errata (156.240 invece di 176.715: C(32,2)
al posto di C(34,2)); il test l'ha rifiutata, corretta. (2) Soglia di plausibilità dell'equity di
AA calibrata sul mazzo intero (> 0,80) mentre nello Short Deck vale 0,7308; sostituita da un
controllo strutturale più un intervallo largo. Nessun errore nel codice di calcolo.
Dubbi: il costo della tabella all-in (53 s) è il più alto delle risorse; accettabile come una
tantum, da non ricalcolare a ogni build.
Prossimo passo: P3 sul branch `feature/preflop-blueprint-p3-clustering`.

### 2026-09-15 — P1 — canonicalizzazione e cataloghi, gate PASS

Fatto: indice combinatorio colex con inversa e indice di combo compatibile con `all_combos()`;
canonicalizzazione dei semi di flop, flop+turn, board a cinque carte e board history con
permutazione e orbita esposte; cataloghi con molteplicità e riferimenti incrociati; PRNG
deterministico indipendente dalla piattaforma; sampler fisico e canonico; persistenza con
checksum; test e eseguibile di report. Report: [P1_CANONICAL_BOARDS.md](P1_CANONICAL_BOARDS.md).
Comandi: build dei target P1; `ctest -L p1 -V`; `ctest -L preflop_blueprint`.
Risultati: 4.062.607 asserzioni PASS in 3,6 s; conteggi 573 / 13.761 / 19.998 / 369.072 con somme
fisiche 7.140 / 235.620 / 376.992 / 7.539.840; costruzione dei cataloghi 2,56 s (history 2,03 s);
catalogo 13.970.940 B; fingerprint `fnv1a64:51879f40626cb7dd`; regressione P0 3/3.
Fallimenti: (1) prima build fallita per `deck_cards` non dichiarata in `canonical_boards.cpp`
(include mancante di `combinatorics.hpp`), corretta al secondo tentativo. (2) Prevenuti prima
della build: `-bound` su unsigned (C4146 con `/WX`) sostituito da `0U - bound`; scrittura del
magic con tipo a 8 bit; `<cmath>` mancante nel test.
Dubbi: il conteggio dei flop+turn canonici (13.761) era noto solo come limite inferiore
(9.818); ora è fissato come costante attesa. Il test del sampler usa una soglia a sei sigma per
classe: è un controllo di sanità della cumulata, non un test statistico formale.
Prossimo passo: P2 sul branch `feature/preflop-blueprint-p2-resources`.

### 2026-09-15 — P0 — scaffolding completato, gate PASS

Fatto: verificato il tag di sicurezza; creati il branch di integrazione e il branch di fase in un
worktree separato; aggiunti i target `gtosd_card_abstraction` e `gtosd_preflop_blueprint` con
l'opzione `GTOSD_BUILD_PREFLOP_BLUEPRINT`; scritti schema `gtosd.preflop_blueprint_game.v1`, tre
fixture (HU10 completa, HU10 ridotta, CO40), loader C++ con validazione e fingerprint, identità
dell'astrazione, test di scaffolding, controllo di dipendenza CMake, validatore Python dello
schema. Report: [P0_SCAFFOLDING.md](P0_SCAFFOLDING.md).
Comandi: configure Release con Ninja e MSVC riusando i pacchetti vcpkg di
`out/build/windows-release-current`; build dei tre target; `ctest -L preflop_blueprint`.
Risultati: configure 15,5 s; build 19 passi senza warning con `/WX`; test 3/3 PASS
(73 asserzioni, 4 sorgenti guardati, 3 fixture valide); guardia negativa su albero sintetico:
link proibito rifiutato, include legacy rifiutato, albero pulito accettato.
Fallimenti: (1) il nome di branch `feature/preflop-blueprint/p0-scaffolding` previsto da D21 è
rifiutato da git perché esiste il ref `feature/preflop-blueprint`; risolto con il trattino,
D21 e roadmap §9 allineati. (2) Il primo wrapper per l'ambiente Visual Studio lanciato da Git
Bash convertiva `/c` in un percorso; risolto disattivando la conversione dei percorsi MSYS.
(3) La rimozione dei worktree legacy con scratch e la cancellazione dei file `.bin` erano già
state bloccate dal classificatore di sicurezza prima dell'avvio di P0 (registro, A8 e D25);
nessun impatto su P0.
Dubbi: nessuno bloccante. `maximum_postflop_sizes = 3` è un limite di scaffolding da rivedere in
P4 insieme al layout delle azioni compilate.
Prossimo passo: P1 sul branch `feature/preflop-blueprint-p1-canonical` dopo il merge di P0
nell'integrazione.

### 2026-09-15 — P0 — creazione del diario

Fatto: creato il template del diario insieme alla roadmap. Nessun codice scritto.
Comandi: nessuno.
Risultati: nessuno.
Fallimenti: nessuno.
Dubbi: nessuno.
Prossimo passo: P0.

## 4. Domande per l'utente

| # | Data | Domanda | Stato | Risposta |
|---|---|---|---|---|
| Q1 | 2026-09-15 | I branch di fase vengono uniti nell'integrazione con merge locali `--no-ff`; per aprire pull request su GitHub servirebbe il push dei branch su origin. Si pubblicano i branch su origin oppure restano merge locali fino ai gate di `main`? Nel frattempo si procede con merge locali. risolta 2026-09-16 | pubblicare i branch: eseguito, 10 branch e 3 tag su origin (`main` non pushato) |
| Q2 | 2026-09-15 | D21 prevede il merge dell'integrazione in `main` al gate P3 con tag. `main` è il branch checked-out nel working tree dell'utente (`C:/Users/GoryNickel/Documents/GitHub/GTO-Solver`): git non permette di farne il checkout in un secondo worktree e spostarne il ref da fuori lascerebbe il working tree dell'utente in uno stato incoerente. Comandi proposti, da eseguire nel working tree dell'utente con `main` pulito: `git merge --no-ff feature/preflop-blueprint -m "merge(preflop-blueprint): P0-P3 card abstraction, gate P3 PASS"` poi `git tag -a preflop-blueprint-p3-abstraction -m "P3 gate PASS"`. Suite CTest completa eseguita sull'integrazione dopo il merge di P3: 65/65 PASS (4 test legacy passano solo con il manifest v1 in LF, vedi voce P5 del diario; nel checkout dell'utente il file è in LF). In alternativa l'utente può autorizzare l'agent a eseguire i due comandi nel suo working tree. Nel frattempo P4 e P5 sono proceduti sull'integrazione. risolta 2026-09-16 | applicare il merge: eseguito nel working tree dell'utente (`97d8121`), tag `preflop-blueprint-p3-abstraction` su `981361e` |
| Q3 | 2026-09-16 | D21 prevede al gate P6 il merge dell'integrazione in `main` con tag `preflop-blueprint-p6-hu10`; stesso blocco di Q2 (`main` è il working tree dell'utente). Comandi proposti nel working tree dell'utente con `main` pulito: `git merge --no-ff feature/preflop-blueprint -m "merge(preflop-blueprint): P0-P6 trainer and physical best response, gate P6 PASS"` poi `git tag -a preflop-blueprint-p6-hu10 -m "P6 gate PASS: HU10 D3 with the sampled estimator at 1000 flops"`. Da eseguire dopo (o insieme a) Q2. Nel frattempo P7 procede sull'integrazione. risolta 2026-09-16 | come Q2: tag `preflop-blueprint-p6-hu10` su `22e1015` e `preflop-blueprint-p8-export` su `9360836` |
| Q4 | 2026-09-16 | P8 prevede l'aggiornamento del viewer `tools/hu_preflop_chart_viewer` (repository separato, D18, escluso da git in questo repository) perché legga il nuovo export e mostri i badge `ESTIMATED` / `CERTIFIED_EXACT` / `EXTERNAL_REFERENCE`; il gate P8 richiede "viewer navigabile con un export HU10". L'agent lavora nel worktree e non modifica né il repository del viewer né il working tree dell'utente. Proposta: l'agent produce in questo repository l'export nel formato che il generatore del viewer già legge (`preflop_nodes` con `history`, `strategy`, `action_ev` per classe), lo schema e un validatore statico registrato in CTest; la modifica del generatore (vincoli fissi su 20 nodi, fingerprint CO40 e path Monker da rendere generici; badge di stato dal certificato P7) va fatta nel repository del viewer: la esegue l'utente, oppure l'utente autorizza l'agent a modificare `tools/hu_preflop_chart_viewer` nel suo working tree. Fino alla risposta il criterio "viewer navigabile" del gate P8 resta INCONCLUSIVE e le altre parti di P8 procedono. risolta 2026-09-16 | il viewer può essere modificato dall'agent: fatto sul branch `feature/preflop-blueprint-p8-export` del viewer (`4edbb45`), modifiche locali preesistenti conservate in `56cdf31` |

## 5. Decisioni prese dall'agent

| # | Data | Fase | Decisione | Motivazione |
|---|---|---|---|---|
| 1 | 2026-09-15 | P0 | Branch di fase con trattino: `feature/preflop-blueprint-pN-nome` | git rifiuta `feature/preflop-blueprint/pN-nome` perché il ref `feature/preflop-blueprint` esiste |
| 2 | 2026-09-15 | P0 | `maximum_postflop_sizes = 3` nel loader | fold/check/call più size più all-in restano entro le sei azioni del motore esistente; da rivedere in P4 |
| 3 | 2026-09-15 | P0 | `button_blind_units` esplicito e strettamente positivo | D9; evita l'identità implicita con l'ante del formato legacy |
| 4 | 2026-09-15 | P0 | Riuso dei pacchetti vcpkg installati nella build principale (`VCPKG_MANIFEST_INSTALL=OFF`) | evita una nuova installazione delle dipendenze nel worktree; riproducibile |
| 5 | 2026-09-15 | P0 | Test dello schema con `SKIP_RETURN_CODE 77` se `jsonschema` manca | non fallire su macchine senza il pacchetto; il loader C++ applica comunque le regole |
| 6 | 2026-09-15 | P0 | Merge locali `--no-ff` nell'integrazione, nessun push su origin | il push pubblica contenuti; in attesa della risposta a Q1 il lavoro non si ferma |
| 7 | 2026-09-15 | P1 | PRNG proprio (xoshiro256** seminato da splitmix64, draw limitati con il metodo di Lemire) al posto di `std::mt19937_64` e `std::uniform_int_distribution` | le distribuzioni standard sono implementation-defined; un seed deve identificare gli stessi board su ogni piattaforma |
| 8 | 2026-09-15 | P1 | Canonicalizzazione per minimo su 24 permutazioni di un codice a 6 bit per carta, cataloghi per enumerazione esaustiva | verificabile con la dimensione dell'orbita; 2,6 s di costruzione |
| 9 | 2026-09-15 | P1 | Conteggio dei flop+turn canonici fissato a 13.761; il caricamento del catalogo è fail-closed sui quattro conteggi | misura ottenuta dall'enumerazione; sostituisce il limite inferiore della roadmap |
| 10 | 2026-09-15 | P1 | Il file del catalogo non viene distribuito | ricostruzione in 2,6 s; il file salvato serve come identità verificabile con checksum |
| 11 | 2026-09-15 | P2 | Rank ordinali a 16 bit derivati dall'oracolo a 5 carte invece di caricare la tabella R3 a 32 bit | stesso ordine dei `HandValue`, metà memoria, 1,4 s di costruzione, nessun file esterno; l'oracolo resta l'unico evaluator |
| 12 | 2026-09-15 | P2 | Kernel sweep per flop e turn, pairwise per il river | il river richiede conteggi per gruppo avversario; il pairwise è un controllo indipendente del kernel |
| 13 | 2026-09-15 | P2 | File delle feature (365 MB) come artefatti offline non distribuiti; equity river in virgola fissa a 16 bit | servono solo al clustering P3; dimensione dimezzata rispetto a float32 con errore 1/131070 |
| 14 | 2026-09-15 | P2 | Tabella all-in triangolare con voci vuote per le coppie sovrapposte | indirizzamento O(1) senza mappa |
| 15 | 2026-09-15 | P3 | k-means intero: istogrammi e cumulate come conteggi, centroidi come mediane pesate (EMD) o medie arrotondate (L2), partizione statica del lavoro | nessuna dipendenza dall'ordine di riduzione in virgola mobile: risultato identico a 1, 3 e 8 thread (D14) |
| 16 | 2026-09-15 | P3 | Riavvii valutati su un campione sistematico di 500.000 osservazioni per 10 iterazioni, solo il migliore rifinito sull'intero insieme | 10 riavvii completi costerebbero dieci volte il river (814 s); il campione sistematico è deterministico e copre tutte le righe |
| 17 | 2026-09-15 | P3 | Bucket rietichettati per forza crescente del centroide dopo la convergenza | id confrontabili fra costruzioni e leggibili nei report; l'assegnazione non cambia |
| 18 | 2026-09-15 | P4 | Regole di fase come funzione del livello di aggressione della street (0 apertura, 1 risposta, 2+ solo fold/call/all-in) e del "facing all-in" letto dallo stato, non della macchina a stadi HU | stesso albero HU (fingerprint legacy riprodotto) e regole valide per N giocatori |
| 19 | 2026-09-15 | P4 | Transizioni HU delegate a `gtosd::apply_action`/`advance_street`; per N > 2 fold generalizzato e avanzo di street nella libreria blueprint, nessuna modifica al core in P4 | il core gestisce il fold solo a due giocatori; il costruttore N-player nel core è previsto da P10 (§2.2) |
| 20 | 2026-09-15 | P4 | Conteggi attesi dello scheletro postflop CO40 corretti a 27.012 nodi / 10.060 decisioni / 25.944 archi (misurati con `gtosd_hu_preflop_tree` sul codice legacy attuale) | i 30.324 / 11.308 / 29.112 della roadmap provengono da documenti anteriori alle regole di puntata correnti e non sono riprodotti nemmeno dal codice legacy |
| 21 | 2026-09-15 | P5 | Cache per board delle probabilità esatte di vittoria e pareggio di ogni coppia viva (2 matrici 465×465 in double, 3,5 MB) invece di leggere la tabella P2 a ogni terminale | 10 terminali all-in preflop per traversata: costruzione una volta per board, poi prodotti matrice-vettore; la massa di sconfitta deriva da `D − W − T` con `D` esatto dal kernel fold |
| 22 | 2026-09-15 | P5 | Interfaccia `Policy` per (nodo, mano) con due implementazioni: `BucketPolicy` sul layout P4 e `HandPolicy` per mano | la traversata non conosce l'astrazione; gli oracoli e i test prescrivono strategie per combo, il trainer userà i bucket |
| 23 | 2026-09-15 | P5 | Oracolo postflop confrontato sul valore condizionale `v[h] / D[h]` invece che sul valore controfattuale grezzo | il solver postflop normalizza la reach avversaria con una costante interna; il rapporto elimina la costante e resta un confronto esatto per combo |
| 24 | 2026-09-15 | P5 | Tabella all-in e rank caricate dalla directory `out/preflop_blueprint_resources` quando presente, altrimenti ricostruite nel test | i test restano autosufficienti su altre macchine (circa un minuto di costruzione) e rapidi su quella di riferimento |
| 25 | 2026-09-15 | P6 | Nel regret `R += w_B · P(h) · P(o\|h) · (v_a − v)` il fattore `cf_reach` di §5 è già dentro `v` (valori dei kernel con la reach avversaria); non viene moltiplicato di nuovo | formula del CFR vettoriale standard; uguaglianza entro `1e-13` con `solve_finite_game` sul gioco ridotto |
| 26 | 2026-09-15 | P6 | Modalità di aggiornamento `Simultaneous` (uno snapshot per iterazione, entrambi i giocatori) come default e `Alternating` come opzione; Linear default, DCFR 1,5/0/2 opzione | la modalità simultanea riproduce esattamente `solve_finite_game` Linear (oracolo P6); DCFR alternato converge più in fretta nelle prove e resta lo sfidante di §3.4 |
| 27 | 2026-09-15 | P6 | Partizione dell'albero in unità di `max(256, nodi/128)` nodi, indipendente dal numero di thread; parte alta seriale; incrementi scritti direttamente nelle celle (un solo scrittore) senza buffer di delta | bit-identità per qualsiasi numero di thread e di unità verificata (0 celle diverse fra 1, 8 e 58 unità); nessuna copia dello stato (D14, roadmap P6.2) |
| 28 | 2026-09-15 | P6 | Lo stimatore D3 misura la best response fisica contro la strategia media a bucket, come prescrive P6.3. **Corretta dalla decisione 30**: il massimo per mano *su ogni board* è chiaroveggente sopra il river | la best response del gioco astratto con recall imperfetto non è calcolabile per board; quella fisica la domina ed è la misura che P7 certificherà |
| 29 | 2026-09-15 | P6 | Boards del batch pesati `1/B`; RNG di training e di valutazione separati e salvati nel checkpoint | costanti comuni alle iterazioni non cambiano il regret matching; la valutazione non perturba il training e la ripresa è bit-identica |
| 30 | 2026-09-15 | P6 | Best response fisica **non chiaroveggente**: ai nodi dell'eroe i valori delle azioni sono aggregati sulle carte future prima del massimo (turn: somma sui river; flop: somma sui turn; preflop: somma sui flop); il massimo per board resta solo al river. Erratum aggiunto alla roadmap §5 | la best response per board di P6.3/§5 dava un responder che conosce turn e river: plateau di circa 1 a su HU10 e pavimenti crescenti con il numero di board fissi (0,105/0,163/0,473 a per 4/8/64 board) non dovuti all'astrazione; l'aggregazione corretta coincide con `calculate_nash_conv` del `FiniteGame` lossless entro `1e-9` |
| 31 | 2026-09-15 | P6 | Valutazione campionata per flop: `M` flop campionati dal catalogo con tutti i 33 × 32 runout enumerati, errore standard sui gruppi di flop; le liste esplicite sono raggruppate per flop | l'aggregazione non chiaroveggente al flop e al turn richiede tutti i runout del prefisso; board completi indipendenti non bastano |
| 32 | 2026-09-16 | P6 | Lo stimatore campionato riporta la stima naive (scelta e valore preflop sugli stessi `M` flop, distorta verso l'alto come `1/√M`) e un limite inferiore senza selezione (strategia media al preflop, best response esatta dal flop in poi); il gate D3 usa la naive più semiampiezza con `M = 1.000` flop; la cross-fit provata è stata scartata | sul checkpoint HU10 ridotto `naive · √M` è costante (≈ 0,4–0,5 a) per `M = 5…160` mentre il limite inferiore è ≈ 0,001 a: la stima P6.3 a `M` piccolo misura solo rumore di selezione; la cross-fit era negativa a ogni `M` |
| 33 | 2026-09-16 | P6 | Default dell'eseguibile di training: DCFR 1,5/0/2 con update alternato; la libreria mantiene Linear simultaneo come default (l'oracolo `FiniteGame` lo richiede). `--eval-seed` riavvia l'RNG di valutazione dopo il caricamento del checkpoint invece di entrare nell'identità | su HU10 completo il Linear simultaneo resta 2–3 volte sopra il DCFR alternato a parità di flop di valutazione (0,143 contro 0,045 a a `M = 60`); una rivalutazione su flop freschi non deve cambiare l'identità del run ripreso |
| 34 | 2026-09-16 | P7 | La passata esatta valuta ogni flop canonico una volta con tutti i runout fisici e somma sulle immagini della sua orbita nei semi (`FlopValues.images`); `aggregate(exact)` verifica che ogni combo sia compatibile con 5.984 flop fisici | la strategia media è simmetrica nei semi per costruzione; verificato contro l'enumerazione fisica entro `1e-12` per combo; costo 573 flop invece di 7.140 |
| 35 | 2026-09-16 | P7 | Formato di policy `GTOSDPOL` (fingerprint dell'albero, capacità, sorgente, tabella densa, checksum) scritto dal trainer e letto dal certificatore; il certificato porta i fingerprint di regole, albero, catalogo, tabelle bucket e policy | il certificatore non deve ricostruire il trainer (identità, semi) per leggere una strategia; P8 esporta dallo stesso file |
| 36 | 2026-09-16 | P7 | Stato del certificatore accodato per chunk con checksum per record; ripresa dall'header (albero, policy, catalogo); il numero dichiarato nei certificati è quello esatto, la regola D3 campionata resta la regola di arresto del training | passata ripresa bit-identica; su CO40 la passata esatta costa 22 h e va spezzata; la stima campionata sovrastima di `≈ 0,46/√M` |
| 37 | 2026-09-16 | P8 | Export `gtosd.preflop_blueprint_chart.v1` nel layout `preflop_nodes` → `{history, strategy, action_ev}` già letto dal generatore del viewer, con id di azione compatibili con le chart legacy (`raise_6`, `call`, `fold`, `all_in`) e id di nodo `CO_raise_3_BTN` | il viewer richiede solo la rimozione dei vincoli fissi CO40 e i badge (Q4); nessun secondo formato da mantenere |
| 38 | 2026-09-16 | P8 | EV per azione condizionato al nodo: valore controfattuale diviso per la reach avversaria data la combo, media di classe pesata con la reach, errore standard sui flop campionati; alla radice coincide con l'EV del gioco. **Implementazione corretta dalla decisione 43**: fino al commit `46084f0` la divisione per `D[h]` mancava e i valori fuori dalla radice erano scalati per `D[h]` | è la semantica delle chart (EV dell'azione nello spot); verificata entro `1e-9` alla radice |
| 39 | 2026-09-16 | P8 | Verdetto del comparatore sulla sola exploitability fisica dichiarata (D2, soglia 0,1 a): `QUALIFIED` / `REJECTED` con certificato esatto, `PROMISING` / `INCONCLUSIVE_ESTIMATE` / `REJECTED` (limite inferiore sopra soglia) con stima campionata; distanze Monker descrittive con `EXTERNAL_CONTRACT_INCOMPLETE` | D1/D2/D4 della roadmap; la stima campionata ha bias di selezione e non può qualificare da sola |
| 40 | 2026-09-16 | P8 | Albero pubblico postflop esportato nello schema del viewer (`gtosd.hu_postflop_public_tree.v1`) leggendo lo stato pubblico conservato per nodo dal compilato; id compilati nel file per indirizzare il worker | il viewer già navigava quello schema; nessun secondo formato |
| 41 | 2026-09-16 | P8 | EV postflop del worker: valore controfattuale dell'azione con la strategia media diviso per la reach avversaria al nodo; esatto a turn (32 river) e river, medio su `samplesPerAction` runout campionati al flop (seme fisso), classi pesate con la reach avversaria, frequenze di range pesate anche con la reach dell'eroe | stessa semantica dell'export preflop; il flop completo (1.056 runout) costa 17,6 s e non è interattivo |
| 42 | 2026-09-16 | P8 | Viewer: generatore con sorgenti blueprint e badge, azioni e gioco dinamici, Monker solo a parità di albero, backend `--serve`; le modifiche locali preesistenti dell'utente sono conservate in un commit separato prima dell'aggiornamento | autorizzazione Q4; il repository del viewer non ha remote: i commit restano locali |
| 43 | 2026-09-16 | P8 | EV di classe fuori dalla radice: media di `v_a[h] / D[h]` pesata con `D[h]` (valore e serie per flop dell'errore standard); test del payoff di fold a ogni nodo interno | fino a `46084f0` la divisione per `D[h]` mancava e alla radice non era rilevabile (`D[h] = 1`); il payoff di fold è una costante nota a ogni nodo interno e verifica la semantica condizionata |
| 44 | 2026-09-16 | P8 | Variante CO40 di test come fixture separata con id `-TEST` (una size postflop 100 % più all-in), usata solo per misure di tempo e prove; la fixture CO40 completa resta il riferimento del gate P9 | richiesta dell'utente; il "da non fare" di P9 vieta di cambiare size per migliorare il risultato, non di misurare su un albero ridotto |
| 45 | 2026-09-16 | P8 | Nel viewer le classi con reach proprio lungo la history sotto `1e-6` restano visibili ma desaturate, con il reach nel tooltip | l'export riporta la strategia media di tutte le classi (residui ≈ 1e-9); togliere le righe cambierebbe la griglia 9×9; la soglia è sotto ogni frequenza di gioco significativa |
| 46 | 2026-09-16 | P8 | Lista di risposte vuota nella configurazione = nessuna size di rilancio sopra un open (livello 1: fold, call, all-in); con lista non vuota resta una risposta per open | regola dell'utente per HU10 (contro l'open 5 a BTN ha solo l'all-in); nessun secondo formato, la fixture CO40 non cambia |
| 47 | 2026-09-16 | P8 | Le misure di tempo del certificatore si fanno con `--chunk` ≥ numero di thread (il parallelismo è sui flop di uno stesso chunk); i tempi per flop nei report sono a 8 thread con chunk 16 salvo indicazione | le proiezioni di P7 §5 (chunk 2) e della voce CO40-TEST (chunk 1) avevano 2 e 1 thread attivi e sovrastimavano di 2,3 e 4,2 volte |
| 48 | 2026-09-16 | P8 | La soluzione della variante CO40 di test entra nel viewer come terza sorgente (chart preflop con badge e exploitability esatta dichiarata, il run migliore fra DCFR 2.000/10.000 e Linear 2.000); la navigazione postflop resta sull'albero e sulla policy HU10 completo | il generatore accetta un solo `--postflop-tree` e il server una sola policy; estenderli non era richiesto |
| 49 | 2026-09-21 | P9 | Separare sempre BR astratta esatta e BR fisica esatta; solo la seconda qualifica | su HU30 a 32.000 valgono 0,034874091 e 0,167619129 a: confonderle attribuirebbe alle iterazioni un errore di rappresentazione pari a 0,132745038 a |
| 50 | 2026-09-21 | P9 | Nessuna correzione specifica per stack, fixture o fingerprint; HU10/HU20/HU30/HU40 formano una matrice di regressione ordinata | il solver deve risolvere giochi configurati, non i benchmark usati per scoprirne i difetti |
| 51 | 2026-09-21 | P9 | Non promuovere l'aumento uniforme del cap river | cap 23 usa 24,67 GiB, costa il 22,9 % in più e a 2.000 iterazioni peggiora la BR astratta ristretta da 0,496262101 a 0,726731922 a; non è una prova asintotica, ma fallisce il filtro di tempo |
| 52 | 2026-09-21 | P9 | Valutare un raffinamento annidato automatico con trasferimento della policy; usare la storia completa compatta soltanto come controllo diagnostico finché non passa sull'albero corrente | il raffinamento annidato conserva la strategia precedente al momento dello split e può assegnare memoria agli information set con disaccordo fisico misurato; il prototipo full-recall corrente usa un albero obsoleto e conserva comunque i bucket base |
| 53 | 2026-09-21 | P9 | Fissare a 8 GiB il picco del solver di prodotto | HU20 `history7` a 11,44 GiB resta il riferimento matematico ma non soddisfa il requisito; cap 23 a 24,67 GiB è escluso |
| 54 | 2026-09-21 | P9 | Usare un solo protocollo automatico su HU10/HU20/HU30/HU40; i benchmark variano soltanto stack e numero di size preflop/postflop | i benchmark misurano il solver; non possono selezionare iterazioni, batch, cap o feature per una singola fixture |
| 55 | 2026-09-21 | P9 | Formalizzare e valutare lo stesso vettore ricco di feature su tutte le street e tutti gli stack | a parità di righe può migliorare la distanza strategica senza aumentare lo stato CFR; l'audit corrente non copre la geometria delle osservazioni vicine su turn e river |
