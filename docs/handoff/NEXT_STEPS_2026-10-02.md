# Handoff per un agent coder: tutto quello che resta da fare (2 ottobre 2026)

Scritto fra le 23:50 del 01/10 e le 00:10 del 02/10, in sola lettura (nessun run, build o test toccato); controllato e
corretto alle 00:40 del 02/10 da una verifica indipendente (riquadro qui sotto, sezioni 4, T1-T4, T9, T10, T15, 6, 7). È un documento
autosufficiente per chi non ha mai visto il progetto: leggere le sezioni 1-4 prima di toccare qualunque cosa, poi seguire
i lavori della sezione 5 nell'ordine dato.

**Legenda.** **[VERIFIED: file]** = letto in quel file o misurato con un comando alla scrittura. **[INFERRED]** = ragionamento,
stima o proposta, non misurata. Le decisioni che spettano all'utente sono segnate **DECISIONE UTENTE** e raccolte nella
sezione 7.

> **Stato alle 00:35 del 02/10 (aggiornato).** Il run 3-way 1 **è partito**. Alle 00:00:17 il gate della memoria era chiuso
> (17.392.736 KB liberi contro i 18.874.368 richiesti) e lo era ancora alle 00:08; si è aperto alle 00:09:29 con 20.549.132 KB
> liberi, e alle 00:09:30 è partito il runner del round 1 [VERIFIED: `OUT1/queue.log`]. Il trainer
> `gtosd_preflop_blueprint_train.exe` è vivo dalle 00:09:30 (pid Windows 22792, circa 13,8 GB di working set) [VERIFIED:
> `Get-CimInstance Win32_Process` alle 00:30].
> - Le prime 500 iterazioni hanno preso 870,6 s di training, cioè **1,74 s per iterazione**, contro gli 1,5 s dello smoke
>   [VERIFIED: evento `training_progress` in `OUT1/train.jsonl`]. Gli orari di T1 sono ricalcolati con questa velocità.
> - L'impronta dell'albero nell'evento `start` di `OUT1/train.jsonl` è `fnv1a64:71abeabaab92fe56`, uguale a quella registrata
>   nel referee: il punto "impronta" di T3 è già soddisfatto [VERIFIED].
> - La decisione U1 (liberare memoria) non serve più.
> - Dopo la scrittura, alle 00:23-00:25, in `WT` sono comparsi 4 commit nuovi su `feat/threeway-step2` (rimozione delle GUI
>   desktop legacy, sezione 4). La parte A resta non committata [VERIFIED: `git log`, `git status` in `WT` alle 00:30].
> - Gli script di pulizia esistono e i loro dry run sono stati verificati: comandi esatti in T15.

---

## 1. Il progetto in un minuto

- **Cos'è.** GTO-Solver è un solver C++ (CMake, Ninja, MSVC, vcpkg) per il poker **short deck**: 36 carte, rank 6-A, 81 classi
  preflop, 630 combo. Il prodotto in sviluppo è un *preflop blueprint*. Riproduce le chart di MonkerSolver. Lo scopo
  finale è il preflop **6-way** su un server da 52 core / 104 thread e 256 GB [VERIFIED: `MEM/preflop-product-target.md`].
- **Due passi.** Il passo 1 è il "checkdown": l'albero preflop con showdown esatto sul runout. Il passo 2 è il postflop sparso con
  bucket per board (CFR vettoriale, campionamento pubblico del caso, DCFR alternato).
- **Lo standard HU.** HU50 al passo 2, con righe per classe di board (`--board-class-rows`, mappa delle texture TX2).
  La batteria di correttezza HU è chiusa con 0 FAIL [VERIFIED: `DOCS/MONKER_RECIPE_REPRODUCTION_2026-09-28.md` §10.13].
- **Il 3-way.** Il passo 2 a tre giocatori è la "fase 3", con la specifica in `DOCS/threeway/PHASE3_SPEC_2026-09-30.md`.
  - La fase 3a (kernel, percorso a 3 seggi del trainer, cache delle classi, CLI) è fatta, con il gate verde e il merge `238a41e`.
  - Il primo run 3WAY50 è partito alle 00:09:30 del 02/10 (coda lanciata alle 18:11:54 del 01/10 con partenza dalle 00:00).
  - La fase 3b è la valutazione con la policy fissa ("parte A"), il V11 e il test del blocco. È scritta ma non compilata.
  - La fase 3c (parte B, migliore risposta completa) richiede un server.
- **history7.** Le righe di bucket per storia (`HistoryBucketRows`) e la migliore risposta astratta esatta: l'utente ha deciso
  di eliminarle il 01/10 [VERIFIED: `MEM/history7-retire-decision.md`]. La rimozione è pronta in una sandbox, non applicata.
- **Diario e documenti di riferimento.**
  - `DOCS/PROGRESS_LOG.md`: tabella di stato e diario. Le voci non si cancellano: una correzione è una voce nuova.
  - `DOCS/MONKER_RECIPE_REPRODUCTION_2026-09-28.md`: ricetta, risultati, sezione 9 (3-way) e sezione 10 (batteria HU).
  - La specifica della fase 3.

## 2. Abbreviazioni dei percorsi

| Sigla | Percorso | Cosa contiene |
|---|---|---|
| `M` | `C:/Users/GoryNickel/Documents/GitHub/GTO-Solver` | checkout principale, branch `feat/monker-step1-checkdown` |
| `WT` | `C:/Users/GoryNickel/Documents/GitHub/GTO-Solver-phase3` | worktree della fase 3, branch `feat/threeway-step2` |
| `B` | `WT/out/build/windows-release-suite` | build Release della suite nel worktree (MSVC, /W4 /WX) |
| `SP` | `C:/Users/GORYNI~1/AppData/Local/Temp/claude/C--Users-GoryNickel-Documents-GitHub-GTO-Solver/94e439a5-4749-4983-9219-1f8b6fb59a5f/scratchpad` (in Git Bash `/c/Users/GORYNI~1/...`) | cartella temporanea della sessione principale: script, rapporti, log |
| `P3A`, `P3B` | `SP/threeway/phase3a`, `SP/threeway/phase3b` | script e rapporti delle fasi 3a e 3b |
| `H7` | `P3B/history7_removal` | artefatti della rimozione di history7 |
| `OUT1` | `M/out/monker/step2_3way/3WAY50_15x4_rake25cap2` | cartella del run 3-way 1 |
| `MONKER3` | `C:/Users/GoryNickel/Documents/GitHub/GTO-Chart-Browser/ranges/Short Deck/Symmetrical Chart/3-way/50a` | le 54 chart 3-way 50a di MonkerSolver (sola lettura) |
| `DOCS` | `M/docs/research/preflop_vector_cfr` | diario, ricetta, specifiche |
| `MEM` | `C:/Users/GoryNickel/.claude/projects/C--Users-GoryNickel-Documents-GitHub-GTO-Solver/memory` | note di memoria con le regole dell'utente |

## 3. Regole da rispettare sempre

| # | Regola | Come si applica | Fonte |
|---|---|---|---|
| R1 | **Finestre della macchina** | Scrivere codice, documenti e specifiche è sempre ammesso, a qualunque ora. Run 00:00-20:00, build e test fino alle 21:00. **02/10: nessun limite** (giorno libero). **03/10 di nuovo normale**, salvo nuove indicazioni: la coda mette in pausa alle 19:40 e riprende alle 00:00. Non dare per scontato che un'estensione valga anche per un altro giorno: chiedere | [VERIFIED: `MEM/machine-windows.md`] |
| R2 | **Subagent solo Opus 5.5** | Ogni Agent e ogni agente di Workflow passa `model: "opus"`. Le eccezioni per Fable del 01/10 sono revocate | [VERIFIED: `MEM/subagent-model-opus.md`] |
| R3 | **Script congelati** | Un run lungo esegue copie congelate (`M/out/frozen/...`, eseguibili in `M/out/monker/bin_*`). Non modificare mai uno script che bash sta leggendo. `out/frozen` si estende solo con file **nuovi**, mai sovrascritti | [VERIFIED: `MEM/freeze-running-scripts.md`] |
| R4 | **Mai cancellare dati senza l'utente** | Questo vale anche per sandbox, policy, checkpoint e run su F:. Le cancellazioni le fa l'utente (sezione T15). Una junction si toglie con `cmd /c rmdir <junction>`, **mai** con `Remove-Item -Recurse` | [VERIFIED: `MEM/run-storage-ssd-then-f.md`; `docs/solver-ui/WEB_UI_PROTOTYPE_PROMPT.md` §5.10] |
| R5 | **Identità byte HU (V1)** | Ogni modifica della libreria deve dare `V1_COMPARE=IDENTICAL` sulle fixture g1, rake e hu10 contro `P3A/v1_baseline`, prima del commit della libreria | [VERIFIED: `P3B/after_run.md` B.1, C.2; `MEM/history7-retire-decision.md`] |
| R6 | **Niente SendMessage ad agenti di workflow in esecuzione** | Un SendMessage avvia una copia parallela. Per aggiungere requisiti si aspetta la fine e si fa un passo successivo | [VERIFIED: `MEM/no-sendmessage-to-workflow-agents.md`] |
| R7 | **TaskStop non uccide gli script in coda** | Si cancella con il file CANCEL dello script, oppure con `Get-CimInstance Win32_Process` e `Stop-Process` sul PID, verificando dopo | [VERIFIED: `MEM/taskstop-orphans.md`] |
| R8 | **Memoria con un training in corso** | Build a `-j 2`, un test alla volta, almeno 4 GB liberi, niente sopra 2 GB. Il trainer 3-way vuole 18.874.368 KB liberi e nessun altro trainer per partire | [VERIFIED: `P3B/after_run.md` sezione B; `M/out/frozen/queue_3way50_15x4.sh`] |
| R9 | **Roadmap esplicita** | Mai riordinare in silenzio. Se un passo cambia posto, va detto all'utente: cosa, da dove a dove, perché | [VERIFIED: `MEM/roadmap-changes-explicit.md`] |
| R10 | **Git** | Commit con percorsi espliciti. Mai `add -A`, `commit -a`, `stash`, `reset`, `rebase`. **Push solo quando l'utente lo chiede**. Se esiste `index.lock`, aspettare | [VERIFIED: `H7/apply_to_worktree.sh`; diario 01/10] |
| R11 | **Impostazioni di MonkerSolver** | Non chiederle mai: l'utente non le conosce. Si deducono dai dati | [VERIFIED: `MEM/monker-settings-unknown.md`] |
| R12 | **Archivio su F:** | I run si allenano su C:. Finiti e valutati, vanno su `F:\GTO-Solver-out` con `SP/archive_run.ps1`, che lascia una junction. Un solo script per cartella. Solo fra le 20:00 e le 24:00 e mai durante un salvataggio | [VERIFIED: `MEM/run-storage-ssd-then-f.md`; `PHASE3_SPEC` §9, D7] |
| R13 | **Build da Git Bash** | MSVC passa dal wrapper `SP/vsdev.cmd`, con `GTOSD_WORKDIR` impostata e i percorsi in forma 8.3 senza virgolette. La riga `vswhere.exe not recognized` è innocua | [VERIFIED: `MEM/msvc-build-from-git-bash.md`] |
| R14 | **Testo con backslash o apostrofi** | Patch Python e prosa italiana si scrivono con lo strumento Write, non con un heredoc. I file letti da bash vanno scritti con LF | [VERIFIED: `MEM/bash-heredoc-backslashes.md`] |
| R15 | **Multiway** | L'esattezza non è richiesta oltre l'HU: campionamento e astrazione sono legittimi e si giudicano con misure | [VERIFIED: `MEM/multiway-exactness-not-required.md`] |
| R16 | **Rake** | Il rake 2,5 % / cap 2a serve solo a riprodurre MonkerSolver. Nelle sue soluzioni l'utente sceglierà il proprio rake | [VERIFIED: `MEM/phase3-decisions-2026-10-01.md`] |

## 4. Stato alla scrittura (00:00-00:05 del 02/10)

| Cosa | Stato |
|---|---|
| `M`, `feat/monker-step1-checkdown` | HEAD `88118a6`, pulito. `origin/feat/monker-step1-checkdown` = `88118a6`. `main` = `df4ca99`, 135 commit dietro; `origin/main` = `77677b2` [VERIFIED: `git rev-parse`, `git rev-list`]. Dopo la scrittura HEAD è `dafcee2` (il commit di questo documento, non pushato), più la sua correzione |
| Branch già cancellati | Alle 23:49 del 01/10 la sessione principale ha cancellato 13 branch locali uniti e 14 branch remoti uniti. Restano in locale `main`, `feat/monker-step1-checkdown`, `feat/threeway-step2`, `feat/solver-ui` e i 7 branch dei worktree da cancellare (T15); su origin restano `main` e `feat/monker-step1-checkdown` [VERIFIED: `git branch -a` alle 00:30; per i remoti si vedono solo i riferimenti locali] |
| Tag | `history7-final` = `88118a6`: l'ultimo albero con history7 [VERIFIED: `git rev-parse history7-final`] |
| `WT`, `feat/threeway-step2` | HEAD `2aa24d8`: `88118a6` più il commit del referee 3-way, non ancora nel branch principale. Parte A **non committata**: `trainer.hpp` +151, `trainer.cpp` +742/−1, `benchmarks/CMakeLists.txt` +7, `tests/CMakeLists.txt` +36. Non tracciati: `benchmarks/preflop_blueprint_policy_values.cpp` (1.354 righe), `tests/preflop_blueprint_policy_values_tests.cpp` (713), `tools/monker_compare/part_a_values.py` (226) [VERIFIED: `git status`, `git diff --stat`, `wc -l` in `WT`]. **Dopo la scrittura** (00:23-00:25 del 02/10) un'altra sessione ha aggiunto 4 commit: `921f424`, `1591a10`, `797a7d4`, `c2e9138`. Tolgono le GUI desktop legacy (`apps/gto_gui`, `apps/gui_qt_prototype`, `apps/gui_imgui_prototype`, preset `windows-gui-release`, feature vcpkg, due script F9) e collegano `tests/install_consumer` a `gtosd::storage`. HEAD è `c2e9138`; la parte A resta non committata e intatta [VERIFIED: `git log`, `git diff --stat 2aa24d8 c2e9138`, `git status` alle 00:30]. Il messaggio di `921f424` cita una decisione dell'utente del 02/10 sulla GUI [INFERRED: decisione non trovata nei file letti] |
| Run 3-way 1 | Coda congelata `M/out/frozen/queue_3way50_15x4.sh` (sha256 `7581f0f8…`), lanciata alle 18:11:54 del 01/10. Alle 00:03 del 02/10 aspettava la memoria; il gate si è aperto alle 00:09:29 e il round 1 è partito alle 00:09:30 (riquadro in alto) [VERIFIED: `OUT1/queue.log`] |
| Catene della fase 3b | `P3B/chain.sh` (build, v11, v1, cli) e `H7/h7_chain.sh` (configure, build, tests, smokes, v1). Annullate alle 23:37-23:38 del 01/10 con i file `P3B/chain.CANCEL` e `H7/h7.CANCEL`; nei log, "cancelled" alle 23:38:30 e 23:38:05 [VERIFIED: log e file]. Le ha annullate la sessione principale per proteggere il run [INFERRED: dal brief del task] |
| Sandbox history7 | `WT/out/laneH/src` (= `2aa24d8` più la rimozione, albero LF) e `WT/out/laneH/base`. **`WT/out/laneH/build` non esiste**: la configure non è mai partita [VERIFIED: `ls`] |
| Script di pulizia | `SP/cleanup/delete_data.ps1` e `SP/cleanup/post_delete.sh` **non esistono** alle 00:00 del 02/10 [VERIFIED: `find`]. Scritti alle 00:09-00:11; dry run ricontrollati alle 00:20 da una verifica indipendente: comandi in T15 [VERIFIED] |

---

## 5. I lavori, in ordine

Ogni lavoro ha obiettivo, percorsi, comandi, accettazione, vincoli ed eventuali decisioni dell'utente. L'ordine è quello concordato:
1. run 1;
2. fase 3b (parte A, V11, gate 3b);
3. parte A campionata sul run 1, prima dell'archivio;
4. test del blocco.

La rimozione di history7 si costruisce e si prova dopo la parte A [VERIFIED: `DOCS/MONKER_RECIPE_REPRODUCTION_2026-09-28.md` §9.7 "Cosa resta";
`MEM/history7-retire-decision.md`]. Il calendario proposto per incastrarli è nella sezione 6.

### T1. Sorvegliare il run 3-way 1 (02/10, in corso)

- **Obiettivo.** Sapere quando parte, se è sano e quando finisce, senza toccarlo.
- **Configurazione** [VERIFIED: `M/out/frozen/queue_3way50_15x4.sh`; `OUT1/queue.log`]:
  - gioco: `3WAY50_donk_rake25cap2` (3 giocatori, 50a, rake 2,5 % / cap 2a, no flop no drop);
  - astrazione: bucket `out/monker/buckets_15x4`, mappa TX2, tabelle in double;
  - training: 8 thread, DCFR alternato, batch 32;
  - chart ogni 4.000 iterazioni;
  - arresto quando le 18 chart non all-in cambiano meno di 0,008 fra due snapshot; minimo 16.000 iterazioni, tetto 48.000;
  - checkpoint e snapshot della policy ogni 16.000;
  - eseguibili `M/out/monker/bin_3way_step2` (build di `12fe441`), runner congelato in `M/out/frozen/threeway_step2_12fe441`.
- **Fine attesa** [INFERRED], a 1,74 s per iterazione, la velocità delle prime 500 iterazioni del run [VERIFIED:
  `OUT1/train.jsonl`, 870,6 s]. Lo smoke a 15 × 4 aveva dato 1,5 s [VERIFIED: `MONKER_RECIPE` §9.7]. Si conta dalla partenza
  delle 00:09:30:
  - 16.000 iterazioni (primo checkpoint) in circa 7,7 ore, verso le 08:00 del 02/10;
  - arresto atteso fra 24.000 e 40.000, cioè in 11,6-19,3 ore: il 02/10 fra le 11:45 e le 19:30 circa;
  - tetto di 48.000 in circa 23 ore, verso le 23:30 del 02/10.

  Il costo dei checkpoint (1,3-4,5 minuti ciascuno secondo la specifica) non è misurato.
- **Comandi in sola lettura** (Git Bash). Non usare `tail -f`: un lettore aperto ha già bloccato un archivio [VERIFIED:
  `MONKER_RECIPE` §10.13].
  ```bash
  OUT1=/c/Users/GoryNickel/Documents/GitHub/GTO-Solver/out/monker/step2_3way/3WAY50_15x4_rake25cap2
  tail -n 15 "$OUT1/queue.log"   # gate della memoria, "round N: runner start", uscita del runner, "run finished (...)"
  tail -n 5 "$OUT1/run.log"      # una riga per snapshot: cambio, stop metric non_all_in, distanza da MonkerSolver
  ls "$OUT1/charts"              # it_4000, it_8000, ...
  grep -o 'PREFLOP_BLUEPRINT_TRAIN=[A-Z_]*' "$OUT1/train.jsonl" | tail -n 1   # stato del trainer (scritto a fine round)
  powershell -NoProfile -Command "Get-Process gtosd_preflop_blueprint_train -ErrorAction SilentlyContinue | Select-Object Id,StartTime,WorkingSet64"
  powershell -NoProfile -Command "(Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory"
  ```
  L'eseguibile di training può bufferizzare lo stdout fino all'uscita [VERIFIED: `MEM/msvc-build-from-git-bash.md`]. Il
  progresso affidabile sono `run.log` e le cartelle `charts/it_N`.
- **Accettazione.**
  - `queue.log`: "memory gate open", poi "round 1: runner start". Fatto alle 00:09:29-00:09:30 [VERIFIED].
  - `run.log`: una riga ogni 4.000 iterazioni, cioè circa ogni 115 minuti a 1,74 s [INFERRED].
  - Alla fine: "run finished (PREFLOP_BLUEPRINT_TRAIN=STOPPED): queue done", oppure `ITERATION_LIMIT` al tetto, e `OUT1/policy.bin` presente.
- **Comandi manuali, solo con l'OK dell'utente** (fermare il run cambia la roadmap):
  - `touch "$OUT1/QUEUE_CANCEL"`: la coda tiene presente `PAUSE`, il trainer scrive il checkpoint ed esce `PAUSED`, la coda termina.
    Finché `QUEUE_CANCEL` resta, un nuovo lancio si rifiuta (exit 2) [VERIFIED: commento in testa allo script].
  - Non creare a mano `STOP` o `PAUSE`. Li gestiscono il runner e la coda.
- **Vincoli.** Durante il run 1 nessun altro training e nessuna build o test sulla macchina, salvo un OK esplicito
  dell'utente: le catene sono state annullate apposta (sezione 4). Scrivere codice e documenti va bene. Il 02/10 la coda
  non mette in pausa (FREE_DAYS = 2026-10-02). Dal 03/10 tiene `PAUSE` dalle 19:40 e riparte alle 00:00 [VERIFIED: script
  della coda].

### T2. Se il run 1 fallisce o il PC si riavvia

1. **Diagnosi.** In `OUT1` leggere `queue.log`, `run.log`, `train.stderr.log` e `runner_round<N>.log`. Un errore chiude la
   coda senza nuovi tentativi ("runner failed: queue ends without retry") [VERIFIED: script della coda]. Se l'errore è
   deterministico (due volte lo stesso), non rilanciare in loop: riferire all'utente con i log.
2. **Controllare che non sia vivo niente:**
   ```powershell
   Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -like '*queue_3way50_15x4*' -or $_.Name -like 'gtosd_preflop_blueprint_train*' } | Select-Object ProcessId,Name,CommandLine
   ```
3. **Lock vecchio.** Se `OUT1/queue.lock/` esiste e nessuna coda è viva (dopo un riavvio esisterà), toglierlo con
   `rm -r "$OUT1/queue.lock"`. Contiene il PID MSYS della coda (1644), che non è un PID Windows: il bash della coda era il
   Windows pid 32328 [VERIFIED: `P3B/cleanup_audit.md` "State at audit time"]. Lo script dice di toglierlo a mano solo dopo
   aver controllato che il PID non esiste più.
4. **Annullamento rimasto.** Se esiste `OUT1/QUEUE_CANCEL` e l'utente vuole riprendere, toglierlo.
5. **Checkpoint.** Controllare `ls -la "$OUT1/state.ckpt"`. Il checkpoint si scrive a 16.000 e 32.000, a ogni pausa e alla
   fine. Il runner passa `--resume` solo se `state.ckpt` esiste, e conta come già fatti gli snapshot presenti [VERIFIED: runner
   congelato, righe 101-102 e 119]. Si perdono al massimo 16.000 iterazioni, circa 7,7 ore a 1,74 s [INFERRED]. **Se il crash
   avviene prima di 16.000 iterazioni (prima delle 08:00 circa del 02/10) non c'è `state.ckpt`**: non rilanciare da solo e
   chiedere all'utente. Un rilancio
   ripartirebbe da 0 nella stessa cartella, sopra le chart già scritte, e questo caso non è stato provato [INFERRED].
6. **Memoria.** Servono almeno 18.874.368 KB liberi e nessun trainer, altrimenti la coda aspetta e lo scrive nel log ogni 10
   minuti. Dopo un riavvio le applicazioni dell'utente possono tenere chiuso il gate: chiedergli di chiuderle.
7. **Prova a secco.** Esegue tutti i controlli e non crea nulla. Atteso `DRY_RUN_OK` e la riga "output folder exists with
   queue.log: a restart resumes it (…state.ckpt)" [VERIFIED: script].
   ```bash
   Q_DRY_RUN=1 bash /c/Users/GoryNickel/Documents/GitHub/GTO-Solver/out/frozen/queue_3way50_15x4.sh
   ```
8. **Rilancio.** Rilanciare **lo stesso script congelato**, staccato dalla sessione: in Claude Code con il Bash in
   background, da terminale con
   `nohup bash .../out/frozen/queue_3way50_15x4.sh > SP/queue_relaunch_<data>.out 2>&1 &`. `START_AT` è già passato,
   quindi parte appena il gate si apre. Non modificare la coda né il runner: hanno sha256 fissate, e una copia modificata
   fallisce i controlli. Gli override `Q_*` sono solo per prove ("never for the real run") [VERIFIED: script].
9. **Atteso dopo il rilancio.** In `queue.log`, "round N: runner start". In `run.log`, "step 2 continuous start: ... --resume".
10. **Prevenzione [INFERRED].** Proporre all'utente di sospendere Windows Update nei giorni dei run: il checkpoint ogni 16.000
    è stato scelto proprio per un riavvio di Windows Update [VERIFIED: diario 01/10 sera].

### T3. Fine del run 1: risultato, controllo dell'albero, impronta (pochi minuti, nessuna build)

- **Risultato.** Da `OUT1/run.log`: iterazione dell'arresto, le tre medie (54 chart, 36 all-in, 18 non all-in), distanza e
  differenza di range dalle 54 chart di MonkerSolver (`charts/it_N/vs_monker.json`) [VERIFIED: runner].
  - Da confrontare con i riferimenti della specifica [VERIFIED: `PHASE3_SPEC` §1.2-1.3]: il passo 1 3-way è a 0,245-0,288; l'attesa per il passo 2 è 0,06-0,12 [INFERRED nella specifica].
  - Misure da riportare, non gate: preferenza suited (UTG 0,204 / CO 0,255 in MonkerSolver; script `SP/suited_pref.py`), mix di primo ingresso alla radice UTG e del CO dopo il fold di UTG.
- **Controllo dell'albero (condizione 2 di G3)** con l'eseguibile congelato:
  ```bash
  M=/c/Users/GoryNickel/Documents/GitHub/GTO-Solver
  mkdir -p $OUT1/part_a
  $M/out/monker/bin_3way_step2/gtosd_preflop_blueprint_monker_tree.exe --config $M/benchmarks/monker/3WAY50_donk_rake25cap2.json \
    --charts $OUT1/charts/it_<finale> --manifest $M/benchmarks/monker/3WAY50_tree_manifest.tsv --json $OUT1/part_a/tree_check.json
  ```
  Atteso PASS su 54 chart su 54 [VERIFIED: forma del comando in `MONKER_RECIPE` §6; V12 a 15 × 4 in §9.7].
- **Impronta dell'albero (after_run A.2).** Il referee `WT/tools/independent/sd_referee.py` fissa `fnv1a64:71abeabaab92fe56`
  per `3WAY50_donk_rake25cap2`. Bisogna confermare che l'evento di partenza in `OUT1/train.jsonl` riporti la stessa
  impronta. Se diverge, correggere `RECORDED_TREE_FINGERPRINTS` e committare in `WT` [VERIFIED: `P3B/after_run.md` A.2].
  **Già fatto alle 00:30:** la prima riga di `OUT1/train.jsonl` (`"event": "start"`) riporta
  `"tree_fingerprint": "fnv1a64:71abeabaab92fe56"`, uguale al valore registrato; nessun commit serve [VERIFIED].
- **Accettazione.** Run finito con `STOPPED`, oppure con `ITERATION_LIMIT` riportato come tale; albero 54/54; impronta confermata.

### T4. Fase 3b, parte A: build e verifiche (gate 3b, prima metà)

- **Obiettivo.** Compilare e provare la parte A. È `Trainer::evaluate_policy_values` con la CLI
  `gtosd_preflop_blueprint_policy_values`, i test V11 e `part_a_values.py`, descritti in `P3B/partA.md` §1. Il codice non
  è mai stato compilato [VERIFIED: `P3B/review_partA.md` D4].
- **Quando.** Dopo la fine del run 1, a macchina libera: il 02/10 senza limiti, il 03/10 entro le 21:00.
- **Comandi** (Git Bash; in tutti i blocchi seguenti
  `SPU=/c/Users/GORYNI~1/AppData/Local/Temp/claude/C--Users-GoryNickel-Documents-GitHub-GTO-Solver/94e439a5-4749-4983-9219-1f8b6fb59a5f/scratchpad`):
  1. Togliere l'annullamento della catena: `rm "$SPU/threeway/phase3b/chain.CANCEL"`.
  2. Build completa a `-j 4`: `bash $SPU/threeway/phase3a/final_locked.sh build3b final_build.cmd`, con log in
     `P3A/final_build3b.log` e `BUILD_OK` atteso. Prende il lock condiviso `P3A/build.lock` e aspetta 4 GB liberi.
     - La build rilancia CMake da sola, perché i `CMakeLists.txt` sono cambiati [INFERRED].
     - Gli errori di compilazione si correggono solo nei file della parte A.
     - In alternativa c'è la catena a `-j 2`: `REQUIRE_TRAINER=0 bash $SPU/threeway/phase3b/chain.sh build v11 v1 cli`. Senza
       `REQUIRE_TRAINER=0` la catena aspetta per sempre un trainer vivo [VERIFIED: `P3B/chain.sh`, funzione `gate`].
  3. V11, con `POLICY_VALUES_TESTS=PASS` atteso e i casi v11hucd, v11three, v11hu, v11smoke e v11state entro 1e-9 oppure
     identici bit per bit [VERIFIED: `P3B/partA.md` §1.4]:
     ```bash
     WT=C:/Users/GoryNickel/Documents/GitHub/GTO-Solver-phase3; B=$WT/out/build/windows-release-suite
     $B/tests/gtosd_preflop_blueprint_policy_values_tests.exe --resources-dir $WT/out/preflop_blueprint_resources \
       --buckets-dir $WT/out/preflop_blueprint_buckets_200_500_1000 --scratch-dir $WT/out/phase3b_scratch --threads 4
     ```
  4. V1 completo (B.1). Circa 3 minuti; 5 GB per ciascuna fixture HU50. Atteso `V1_COMPARE=IDENTICAL`, 13 campi × g1/rake/hu10:
     ```bash
     bash $SPU/threeway/phase3a/v1_baseline.sh $B/benchmarks $SPU/threeway/phase3b/v1_full 4
     python $SPU/threeway/phase3a/v1_summary.py $SPU/threeway/phase3b/v1_full --compare $SPU/threeway/phase3a/v1_baseline
     ```
  5. ctest completa (B.2): `bash $SPU/threeway/phase3a/final_locked.sh ctest_3b final_ctest.cmd`, log
     `P3A/final_ctest_3b.log`. Sono attesi 102 test (i 101 del gate 3a più `policy_values_tests`), più quelli eventualmente
     registrati in più; il criterio è **0 failed** [VERIFIED: `P3B/after_run.md` B.2].
  6. Gamba esatta HU di V11 (B.3), 15-30 minuti, meno di 1 GB:
     `bash $SPU/threeway/phase3a/final_locked.sh long3b final_ctest.cmd -R gtosd_preflop_blueprint_policy_values_long_tests`.
     Atteso: entro 1e-9 dall'aggregato esatto di `BestResponseEvaluator`.
  7. Referee nella ctest (A.1). Atteso 2/2 (`SD_REFEREE=PASS`, `SD_REFEREE_SELFTEST=PASS`):
     `bash $SPU/threeway/phase3a/final_locked.sh ref3b final_ctest.cmd -R gtosd_preflop_blueprint_independent_referee -j 1`.
     Prima controllare che il test del referee sia registrato con la famiglia 3WAY50 (serve la riconfigurazione di CMake del
     passo 2):
     ```bash
     grep '^add_test("gtosd_preflop_blueprint_independent_referee"' $B/tests/CTestTestfile.cmake | grep -c 3WAY50_donk.json   # atteso 1
     ```
     **Correzione.** `P3B/after_run.md` A.1 (e la versione precedente di questo documento) diceva che
     `grep -c 3WAY50_donk.json $B/tests/CTestTestfile.cmake` deve dare 1. Non distingue niente: dà già 1 oggi, perché
     `3WAY50_donk.json` compare anche negli argomenti del test `gtosd_preflop_blueprint_fixture_schema`. Dopo la
     riconfigurazione darà 2. Il comando sopra dà 0 oggi e 1 a registrazione fatta [VERIFIED: `CTestTestfile.cmake` del
     01/10 13:46 e `tests/CMakeLists.txt` righe 458 e 1086 in `WT`].
  8. Smoke della CLI: `REQUIRE_TRAINER=0 bash $SPU/threeway/phase3b/chain.sh cli`. Fa la parte A campionata su 2 flop con
     arresto e ripresa, poi `monker_in_our_game.py` e `part_a_values.py --check`, che devono uscire con 0. Log in
     `P3B/chain.log` [VERIFIED: `P3B/chain.sh`].
- **Accettazione.** `BUILD_OK` senza warning, V11 PASS, V1 IDENTICAL, ctest con 0 failed, `--long` PASS, referee 2/2, smoke
  della CLI con exit 0.
- **Vincoli.** R1, R5, R8. Il V1 completo è obbligatorio: la parte A aggiunge due rami con guardia di nullità nel percorso
  caldo HU [VERIFIED: `P3B/review_partA.md` D5].

### T5. Review rinviata della parte A: errori standard, guardie, mutazioni

- **D.2, errori standard (rilievo D1, gravità bassa).** Riguarda solo gli errori standard: le SE delle tre stime "pooled"
  omettono il termine del denominatore. La patch `python $SPU/threeway/phase3b/review_partA/patch_partA_se.py apply` fa
  una sostituzione di testo esatta sulla CLI, ed esiste anche `revert`.
  - **Raccomandato [INFERRED]**: applicarla dopo il primo V11 verde e **prima** del commit (T6) e della valutazione del run 1
    (T7), poi ricompilare la CLI e ripetere V11. Le stime puntuali non cambiano.
  - Senza la patch, l'EV si legge come `ev_direct ± ev_direct_standard_error`, che è esatto [VERIFIED: `P3B/review_partA.md` §3].
- **D3, una lacuna di V11 (proposta).** In `v11three` e `v11smoke` aggiungere tre controlli sui valori pooled, entro 1e-9
  [VERIFIED: `P3B/review_partA.md` D3]:
  - `opponent_reach` alla radice di UTG vale 1 su ogni combo;
  - le reach dei nodi top del CO sommano a 1;
  - per il BTN, l'identità esistente.
- **D.1, cinque mutazioni.** I passi [VERIFIED: `P3B/after_run.md` D.1]:
  - Lavorare in una copia sandbox del commit della parte A (`WT/out/review3b/{src,head}` da `git archive`), mai nel worktree.
  - Strumenti: `P3B/review_partA/review_partA_mutations.py`, con i comandi `--src --head apply|restore|status`.
  - Mutazioni: pm1 rake in ogni passata, pm2 reach non disgiunta, pm3 scala HU, pm4 massa di showdown persa, pm5 buffer del rake stantio.
  - Ognuna deve far fallire il caso di test indicato.
  - Se pm2 passa `v11three`, la reach a 3 seggi non ha guardia: aggiungere D3.
  - Costo: la prima build della sandbox richiede 20-40 minuti, poi 3-5 minuti per mutazione. Può girare a `-j 2` durante il
    test del blocco (R8).
- **Accettazione.** Patch delle SE applicata e V11 di nuovo PASS. D3 aggiunto. Esito delle 5 mutazioni registrato nel diario.

### T6. Commit della parte A e congelamento della CLI

- **Commit nel worktree**, con percorsi espliciti:
  ```bash
  git -C $WT add include/gtosd/preflop_blueprint/trainer.hpp libs/preflop_blueprint/src/trainer.cpp benchmarks/CMakeLists.txt \
    tests/CMakeLists.txt benchmarks/preflop_blueprint_policy_values.cpp tests/preflop_blueprint_policy_values_tests.cpp \
    tools/monker_compare/part_a_values.py
  git -C $WT commit -m "feat(preflop-blueprint): phase 3b part A, fixed-policy values per seat, V11" -m "<attribuzione della sessione>"
  ```
- **Congelamento** [VERIFIED: regola in `MEM/freeze-running-scripts.md`]. Copiare
  `B/benchmarks/gtosd_preflop_blueprint_policy_values.exe` in una cartella **nuova**, per esempio
  `M/out/monker/bin_3way_partA/`, con un `SOURCE.txt` (commit e sha256). Le valutazioni lunghe girano da quella copia.
- **Accettazione.** `git -C $WT status --short` non mostra più i file della parte A. L'eseguibile congelato ha uno sha256 registrato.

### T7. Parte A sul run 1 (B.4) e verdetto del gate 3b

- **Quando.** Dopo T3 e T6, a macchina libera. Pesa circa 7,3 GB [INFERRED] e richiede 0,9-2 ore a 8 thread [INFERRED: specifica §6.1].
  È ripristinabile con `--state`: un rilancio dello stesso comando continua.
- **Comando** [VERIFIED: `P3B/after_run.md` B.4; l'eseguibile è la copia congelata di T6]:
  ```bash
  M=C:/Users/GoryNickel/Documents/GitHub/GTO-Solver; OUT=$M/out/monker/step2_3way/3WAY50_15x4_rake25cap2
  MONKER="C:/Users/GoryNickel/Documents/GitHub/GTO-Chart-Browser/ranges/Short Deck/Symmetrical Chart/3-way/50a"
  mkdir -p $OUT/part_a
  $M/out/monker/bin_3way_partA/gtosd_preflop_blueprint_policy_values.exe \
    --config $M/benchmarks/monker/3WAY50_donk_rake25cap2.json \
    --resources-dir $M/out/preflop_blueprint_resources --buckets-dir $M/out/monker/buckets_15x4 \
    --board-class-rows --board-texture-map $M/benchmarks/monker/textures/texture_map_TX2_recommended.txt \
    --policy $OUT/policy.bin --threads 8 --flops 64 --seed 20261002 \
    --state $OUT/part_a/state_64.bin --out $OUT/part_a/values_64.json \
    --series-out $OUT/part_a/values_64.series.json --charts "monker=$MONKER" --check \
    > $OUT/part_a/cli_64.log 2> $OUT/part_a/cli_64.progress.log
  python $WT/tools/monker_compare/part_a_values.py $OUT/part_a/values_64.json --monker "$MONKER" \
    --ours $OUT/charts/it_<finale> --json $OUT/part_a/report_64.json --label "run 1 final" --check
  python $WT/tools/monker_compare/monker_in_our_game.py $OUT/part_a/values_64.json --monker "$MONKER" --check
  ```
  Facoltativo: le curve appaiate sugli snapshot `charts/it_16000/policy.bin` e `it_32000`, con lo stesso `--seed` e altri
  `--state` e `--out`.
- **Accettazione, cioè il gate G3** [VERIFIED: `PHASE3_SPEC` §1.3; `P3B/partA.md` §1.2]:
  - run finito per la regola di arresto o per il tetto;
  - albero 54/54 (T3);
  - identità del rake: `estimate.rake_identity_residual_antes` ≤ 1e-9;
  - guadagno della migliore risposta solo preflop, `gain_preflop_antes[s]`, **≤ 0,04 a per seggio**. È una stima per eccesso: il massimo su valori rumorosi;
  - `checks_failed` vuoto e tutte le `--check` con exit 0.
- **Rapporto da scrivere.**
  - Per seggio: EV ± SE, rake atteso, guadagni con le loro SE, le chart di MonkerSolver nel nostro gioco (perdita ± SE,
    righe di ripiego, scomposizione per nodo).
  - Etichetta "provvisorio, campionato su 64 flop fisici" (specifica §1.4, punto 3).
  - Tempi (B.7): secondi per flop e picco del working set (`evaluation.process_peaks`).
- **Attenzione** [VERIFIED: `P3B/review_partA.md` D2]: l'identità del rake è un controllo di coerenza, cieco a una massa di
  kernel sbagliata. Non va presentata come prova dei kernel; quella è V2, V8, V13 e i casi del class solver.

### T8. Archivio del run 1 su F:

- **Quando.** Dopo T3 e T7, ed eventualmente dopo le curve appaiate. Fra le 20:00 e le 24:00, mai durante il salvataggio di un
  altro run (R12).
- **Comando** (da `M`, PowerShell):
  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File <SP>\archive_run.ps1 out\monker\step2_3way\3WAY50_15x4_rake25cap2
  ```
  Lo script fa robocopy /MOVE, verifica file e byte, crea la junction e la verifica. Prende un lock per cartella e salta le
  cartelle con `NO_ARCHIVE` [VERIFIED: `MEM/run-storage-ssd-then-f.md`].
- **Dimensioni [INFERRED].** Stato di 14,32 GB più circa 6,8 GB per policy (snapshot e finale): 35-50 GB. F: aveva 156 GB
  liberi alle 16:42 del 01/10 [VERIFIED: `MONKER_RECIPE` §10.13]. A circa 37 MB/s servono 15-25 minuti.
- **Accettazione.** Rc 0, junction presente, `OUT1/run.log` leggibile attraverso la junction.

### T9. Rimozione di history7 (lane H): verifica, applicazione, commit

- **Decisione.** L'utente l'ha decisa il 01/10 verso le 19:30 [VERIFIED: `MEM/history7-retire-decision.md`].
- **Cosa si toglie** [VERIFIED: `P3B/removal_note.md`]:
  - la famiglia `HistoryBucketRows` (GTOSDHR1/HR2);
  - la migliore risposta astratta esatta;
  - gli eseguibili `abstract_br`, `history_rows` e `history_census`;
  - l'opzione `--history-rows` delle CLI;
  - la suite HU10-HU40 (`benchmarks/suite/*` tranne `fixtures/`, `tools/preflop_suite/*`);
  - due script di ricerca;
  - i test di history.

  In tutto circa 5.400 righe.
- **Cosa resta di proposito.** La modalità di valutazione a policy fissa (`fixed_policy_evaluation_`), su cui poggia la parte
  A. Le 7 fixture `benchmarks/suite/fixtures/*.json`, lette da `test_fingerprints_without_the_cap` del game test.
- **Review.** Approvata a lettura, con un difetto (le fixture) corretto negli artefatti. Build, test e V1 sono ancora da fare
  [VERIFIED: `P3B/review_removal.md`].
- **Artefatti in `H7`.**
  - `patch_removal.py`: modifiche a testo esatto su un albero a `2aa24d8`. Si ferma senza scrivere se il testo è cambiato.
  - `removal_full.patch`: 47 sezioni.
  - `apply_to_worktree.sh`: passi 1, 2, 3 e check.
  - `dryrun/{CMakeLists.txt,trainer.hpp,trainer.cpp}.patch`: hunk da HEAD alla sandbox, percorsi `a/base/...`, quindi `-p2`.
  - Script di build e test: `h7_configure.cmd`, `h7_build.cmd`, `h7_ctest.cmd`, `h7_junctions.cmd`, `h7_watchdog.ps1`, `h7_chain.sh`.

**9a. Verifica nella sandbox** (la rimozione da sola, senza parte A):
```bash
H7=$SPU/threeway/phase3b/history7_removal
rm "$H7/h7.CANCEL"
touch "$H7/h7.NO_TRAINER_OK"     # solo se NESSUN trainer è vivo; senza, il gate aspetta un trainer
bash "$H7/h7_chain.sh" junctions configure build tests smokes v1    # log $H7/chain.log
```
- **Atteso** nei log di `H7` (`configure.log`, `build.log`, `test_*.log`, `smokes.log`, `v1_hu10_compare.txt`):
  `LANEH_CONFIGURE_OK`, `BUILD_OK`, i test kernel, trainer, certifier e texture `=PASS`, gli smoke tutti passati, V1 hu10 "13
  SAME, 0 DIFFERENT".
- **Come gira la catena.** Usa `-j 2`, il lock `P3A/build.lock` e un watchdog che uccide i processi della sandbox sopra 2 GB.
  Il primo build della libreria richiede circa 30-40 minuti [INFERRED: la build completa a `-j 2` della fase 3a ha preso 29,9
  minuti]. Può girare durante il test del blocco (R8): in quel caso **senza** `h7.NO_TRAINER_OK`.
- **Poi, a macchina libera** [VERIFIED: `P3B/after_run.md` C.3, C.2]:
  - ctest completa sulla sandbox a `-j 4` (comando C.3). Deve dare 0 failed, compreso `gtosd_preflop_blueprint_game_tests`,
    l'unico che usa le fixture della suite [VERIFIED: `P3B/review_removal.md` §10].
  - V1 completo con gli eseguibili della sandbox: g1 e rake coprono il percorso delle righe per classe di board, toccato dal
    rinomino di `BoardContext::abstract_rows_`.
    ```bash
    bash $SPU/threeway/phase3a/v1_baseline.sh $WT/out/laneH/build/benchmarks $H7/v1_full_sandbox 4
    python $SPU/threeway/phase3a/v1_summary.py $H7/v1_full_sandbox --compare $SPU/threeway/phase3a/v1_baseline   # IDENTICAL
    ```

**9b. Applicazione al worktree. TRAPPOLA da evitare.**

`apply_to_worktree.sh` è scritto per la parte A **non committata**. Per `trainer.hpp`, `trainer.cpp` e
`benchmarks/CMakeLists.txt` (`stage_from_sandbox`) mette nell'indice la versione della sandbox, cioè `2aa24d8` più la
rimozione, **senza la parte A**. Il codice della funzione: `git hash-object -w --path` sul file della sandbox, poi
`update-index --cacheinfo` [VERIFIED: `H7/apply_to_worktree.sh`]. Se la parte A è già committata (T6), lanciarlo così com'è
fa sì che i commit 1 e 3 **cancellino la parte A** da quei tre file [INFERRED dalla lettura]. Due modi corretti:

- **(i) Parte A già committata: segue l'ordine concordato.** Worktree pulito sul commit della parte A. Si mantiene la
  divisione in tre commit dello script:
  1. eseguibili history-only, suite tranne `fixtures/`, script di ricerca, `benchmarks/CMakeLists.txt`;
  2. le 4 CLI;
  3. libreria e test.

  Al posto di `stage_from_sandbox`:
  - applicare nel worktree i tre patch di prova, dalla radice di `WT`. Sono già stati provati su copie dei file della parte
    A: il risultato è uguale a sandbox più parte A [VERIFIED: `P3B/review_removal.md` §7].
    ```bash
    patch -p2 -N -F 3 < $H7/dryrun/CMakeLists.txt.patch   # benchmarks/CMakeLists.txt
    patch -p2 -N -F 3 < $H7/dryrun/trainer.hpp.patch
    patch -p2 -N -F 3 < $H7/dryrun/trainer.cpp.patch
    ```
    poi `git add` dei tre file nel commit giusto;
  - `git rm` dei 20 file in `WT/out/laneH/src/REMOVAL_DELETED_FILES.txt` [VERIFIED: 20 righe];
  - per gli altri file modificati, copia dalla sandbox con `git add`: sono le liste `copy_and_add` dei passi 2 e 3, che la
    parte A non tocca.

  Conviene scriverlo come una **copia** dello script nello scratchpad, senza modificare l'originale.
- **(ii) Rimozione prima, parte A dopo.** È l'uso per cui lo script è scritto:
  1. con la parte A ancora non committata, applicare i tre patch sopra;
  2. `bash $H7/apply_to_worktree.sh 1` e commit, poi `2` e commit, poi `3` e commit;
  3. `bash $H7/apply_to_worktree.sh check`: il diff del worktree deve essere solo la parte A;
  4. infine il commit della parte A.

  Cambia l'ordine concordato, quindi va **detto all'utente prima** (R9).

**Comune a (i) e (ii):**
- **I 4 commit GUI del 02/10 (sezione 4).** La sandbox `WT/out/laneH/src` resta a `2aa24d8`, senza quei commit. I file toccati
  dai commit GUI e quelli della rimozione di history7 sono disgiunti, quindi `copy_and_add` e `stage_from_sandbox` non
  annullano la rimozione della GUI [VERIFIED: elenco dei file di `removal_full.patch` e di `REMOVAL_DELETED_FILES.txt` contro
  `git diff --stat 2aa24d8 c2e9138`]. `apply_to_worktree.sh check` però mostrerà 5 differenze attese, dovute ai commit GUI e
  non alla rimozione:
  - `DIFFERS` per `tests/install_consumer/CMakeLists.txt`, `tests/install_consumer/main.cpp` e
    `tools/run_production_dcfr_anti_specialization_audit.ps1`;
  - `tools/run_f9_benchmarks.ps1` e `tools/verify_f9_install.ps1` fra i file della sandbox assenti in HEAD.

  Sono le sole differenze ammesse in più [INFERRED: dalla lettura del ramo `check` dello script]. Se nel frattempo `WT` riceve
  altri commit, rifare il confronto dei file.
- Prima di ogni commit, il grep dei riferimenti pendenti (schema in `P3B/review_removal.md` §5) sul worktree deve essere vuoto.
- Se `patch` fallisce per i fine riga (il checkout è CRLF per alcuni file, i patch sono LF), fermarsi e applicare a mano
  [INFERRED].

**9c. Verifica dell'insieme (parte A più rimozione) nel worktree.**
- `bash $SPU/threeway/phase3a/final_locked.sh h7_rebuild final_build.cmd` a `-j 4`.
- V1 completo (C.2), con `IDENTICAL` atteso.
- `bash $SPU/threeway/phase3a/final_locked.sh ctest_h7 final_ctest.cmd`, con 0 failed.
- V11 di nuovo PASS.
- `git diff <commit parte A>..HEAD -- libs/preflop_blueprint/src/trainer.cpp` deve mostrare solo rimozioni di codice history,
  con `evaluate_policy_values` intatta.

**9d. Review indipendente** dei tre commit, in sola lettura, con un subagent Opus 5.5 (R2).

**9e. Formati di uscita cambiati.** Vanno registrati nel diario e nella documentazione delle CLI (T14). Spariscono:
- `history_map_resident_bytes` dall'evento di partenza del train, dal memory breakdown e dal JSON di certify;
- `"history_map"` dal JSON delle chart;
- `historyMapFingerprint` dalla riga ready di `export --serve`.

Nessun consumatore trovato. La fixture `apps/solver-ui/fixtures/runs/V1L/train.jsonl` del worktree dell'utente contiene
ancora la chiave come dato: non toccarla, dirlo all'utente [VERIFIED: `P3B/review_removal.md` §3, §10].

**9f. Cancellazione della sandbox `WT/out/laneH`** (C.4): solo con l'OK dell'utente (R4).

- **Accettazione.** Sandbox con test PASS e V1 hu10 SAME. ctest completa con 0 failed. V1 completo IDENTICAL sulla sandbox e
  sul worktree. Grep vuoto. Review approvata. Parte A intatta.

### T10. Merge in `feat/monker-step1-checkdown`

- **Prerequisiti.** T4-T7 e T9 verdi, oppure solo T4-T7 per un primo merge di parte A e referee: si può fare in due merge.
- **Comando** (precedente: il merge `238a41e` della fase 3a [VERIFIED: `git log`]):
  ```bash
  git -C $M status --short     # deve essere vuoto
  git -C $M merge --no-ff feat/threeway-step2 -m "Merge phase 3b: part A (V11), 3-way referee, history7 removal"
  git -C $M diff feat/threeway-step2 HEAD -- libs include benchmarks tests tools   # deve essere vuoto
  ```
- **Contenuto del merge.** Oggi `feat/threeway-step2` porta anche i 4 commit della rimozione delle GUI desktop
  (`921f424`..`c2e9138`, sezione 4) [VERIFIED: `git log`]. Il messaggio del merge deve citarli, e l'utente deve saperlo
  prima del merge, perché per il resto del codice legacy ha detto "non ancora" (T15).
- **Vincoli.**
  - Il merge è sicuro anche con un training attivo, perché i run usano copie congelate [INFERRED: R3].
  - **Niente push** finché l'utente non lo chiede (R10).

### T11. Test del blocco 3-way (ambito ridotto, specifica §6.3, after_run B.5)

- **Obiettivo.** Bloccare il preflop delle 54 chart 3-way di MonkerSolver e allenare il nostro postflop contro i suoi
  range. Poi misurare con la parte A il guadagno preflop per seggio.
- **Parametri** [VERIFIED: `PHASE3_SPEC` §6.3; `P3B/after_run.md` B.5]:
  - stesso gioco e astrazione del run 1;
  - `--lock-charts <dir> --lock-nodes all`;
  - `THRESHOLD=0`, `MAX=24000`, policy ogni 8.000 (servono `it_16000` e `it_24000`).

  L'evento di partenza deve riportare righe 2.681, `outside_range_rows` 1.688 e `fallback_rows` 5. Le righe di ripiego si
  allenano.
- **Costo [INFERRED].** 24.000 × 1,5 s ≈ 10 ore (`MONKER_RECIPE` §9.7 "Cosa resta" 3), con picco di 14,8 GB. Alla velocità
  delle prime 500 iterazioni del run 1 (1,74 s) sono circa 11,6 ore. La specifica stimava 13-30 ore prima della misura.
- **Trappola [VERIFIED].** Il runner congelato espande `$TRAIN_ARGS` **senza virgolette** (`run_step2_continuous.sh` congelato,
  riga del comando del trainer), e `MONKER3` contiene spazi ("Short Deck", "Symmetrical Chart"). Un `--lock-charts "<MONKER3>"`
  dentro `TRAIN_ARGS` verrebbe spezzato. Il comando di B.5 (`--lock-charts \"$MONKER\"`) va quindi corretto: i test del
  blocco HU usavano la copia senza spazi `out/monker_lock/charts_50a` [VERIFIED: `MONKER_RECIPE` §6]. Copiare le 54 chart 3-way
  (sorgente in sola lettura) in una cartella nuova, per esempio `M/out/monker_lock/charts_3way_50a/{UTG,CO,BTN}`, e
  verificare gli sha256.
- **Coda.** Uno script **nuovo** derivato da `out/frozen/queue_3way50_15x4.sh`, scritto prima in `SP`:
  - `OUT=M/out/monker/step2_3way/3WAY50_15x4_rake25cap2_lock_all`;
  - `R_THRESHOLD=0`, `R_MAX=24000`, `R_POLICY_SNAPSHOT_EVERY=8000`;
  - `R_TRAIN_ARGS="--board-texture-map $MAP --lock-charts <copia senza spazi> --lock-nodes all"`;
  - `START_AT` e `FREE_DAYS` secondo l'utente;
  - stesse sha256 fissate. Il trainer `bin_3way_step2` (`12fe441`) ha già il blocco a 3 seggi: V12 PASS a 15 × 4 [VERIFIED:
    `MONKER_RECIPE` §9.7].

  Poi `Q_DRY_RUN=1` fino a `DRY_RUN_OK`, il congelamento come file nuovo `M/out/frozen/queue_3way50_15x4_lock.sh` (mai
  sovrascrivere) e il lancio staccato.
- **Valutazione.** La parte A (comando di T7) su `charts/it_16000/policy.bin` e `it_24000`, con lo stesso `--seed 20261002`.
  Riportare in parallelo [VERIFIED: `PHASE3_SPEC` §1.2, §6.3]:
  - i guadagni per seggio in % del piatto di 4a;
  - quelli del run 1;
  - i test del blocco HU: CO 0,92-1,80 %;
  - il blocco del passo 1: UTG / CO / BTN 3,65-5,46 / … / 1,40-1,64 %.
- **Accettazione.** Run finito con `ITERATION_LIMIT` a 24.000, controlli della parte A verdi, tabella scritta.
- **DECISIONE UTENTE.** Ora di partenza: la sera del 02/10, giorno libero, oppure le 00:00 del 03/10. Chiedere (R1).

### T12. Proposta della batteria di correttezza 3-way (solo documenti)

- **Richiesta dell'utente** del 01/10 mattina: proporla prima dei run della fase 3. Tre componenti: un oracolo esatto
  contro un CFR indipendente, regole e payoff indipendenti, guadagni di deviazione per giocatore [VERIFIED:
  `MEM/phase3-decisions-2026-10-01.md`]. Non è ancora scritta [VERIFIED: diario 01/10 sera, "Cosa resta"].
- **Già coperto.**
  - Regole e payoff: il referee 3-way `2aa24d8`, 5 configurazioni PASS, 8 mutazioni del motore prese [VERIFIED: `P3B/referee3.md`,
    `P3B/review_partA.md` §4].
  - V2-V13 del gate 3a.
  - Guadagni preflop per seggio: la parte A.
- **Manca [INFERRED].**
  - Giochi 3-way piccoli senza perdita, risolti da un CFR indipendente in Python come oracolo.
  - Il guadagno completo per seggio: la parte B, su un server.
  - Una config che eserciti la regola del donk con 3 seggi (C9), oggi scoperta [VERIFIED: `P3B/referee3.md` §3].
- **Criteri.** Con 3 o più giocatori non c'è garanzia di Nash: i criteri sono l'accordo con l'implementazione indipendente e i
  guadagni per seggio riportati, non "tendono a 0".
- **Consegna.** `DOCS/threeway/CORRECTNESS_BATTERY_3WAY_PROPOSAL_<data>.md`, con costi e tempi. **DECISIONE UTENTE** per
  eseguirla.

### T13. "Fatto" della fase 3 e fase 3c

- **"Fatto"** [VERIFIED: `PHASE3_SPEC` §1.4]:
  1. 3a unita: fatto;
  2. un run che soddisfa G3: T3 e T7;
  3. la parte A su quel run, campionata e quindi provvisoria: T7;
  4. il test del blocco allenato e valutato: T11;
  5. diario e viewer 3-way aggiornati: T14.
- **Fuori ambito ridotto (D5: niente server)** [VERIFIED: `PHASE3_SPEC` §10]:
  - parte B (migliore risposta completa, NashConv, `--exploit`);
  - run 30 × 4;
  - secondo rake 5 % / cap 0,75a;
  - seed 2;
  - parte A esatta.

  Sull'i3, la parte A esatta del run 1 (B.6, `--all-flops`) richiede 8-18 ore ripristinabili fra le finestre [INFERRED].
  **DECISIONE UTENTE**: server a noleggio, parte A esatta sull'i3, oppure rinvio.

### T14. Documenti e diario dopo ogni passo

Dopo ogni lavoro: una voce nuova in `DOCS/PROGRESS_LOG.md`, con fatti segnati [V] e [I] come nel resto del diario, e
l'aggiornamento della tabella di stato §1. Le voci vecchie non si toccano.

- **Manca la voce del 01/10 sera dopo le 18:15** [VERIFIED: l'ultima voce è "2026-10-01 sera (16:30-18:15)"]. Deve coprire:
  - il codice della parte A (`P3B/partA.md`);
  - il referee 3-way (`P3B/referee3.md`, commit `2aa24d8`);
  - la decisione su history7 e il tag `history7-final`;
  - la sandbox della rimozione e le due review (`P3B/removal_note.md`, `review_removal.md`, `review_partA.md`);
  - l'audit di pulizia (`P3B/cleanup_audit.md`);
  - le catene annullate alle 23:38;
  - il gate della memoria chiuso alle 00:00 del 02/10 e aperto alle 00:09:29, con la partenza del run 1 alle 00:09:30;
  - la pulizia (T15) e i 4 commit GUI in `WT` (sezione 4).
- **Tabella di stato.**
  - La riga "Gate di accettazione" parla ancora di "best response esatta dentro l'astrazione <= 0,03 a" [VERIFIED:
    `PROGRESS_LOG.md` §1]: va aggiornata con T16.
  - Vanno aggiornate anche "Prossimo passo", "Build" (nuovi `bin_*`) e "Branch".
- **`MONKER_RECIPE_REPRODUCTION_2026-09-28.md`.**
  - Una sezione 9.8 per la fase 3b (run 1, parte A, gate 3b) e una 9.9 per il test del blocco.
  - Una nota in §1 sul ritiro del gate astratto.
  - L'aggiornamento di §8, punto 9 (decisioni).
- **`PHASE3_SPEC`.** Le date dal 01/10 in poi vanno rilette con un giorno di ritardo [VERIFIED: diario 01/10, "Calendario"].
- **Documenti che citano gli strumenti rimossi.** L'elenco è in `P3B/review_removal.md` §9: `BENCHMARK_SUITE_*`,
  `MEMORY_TIME_OPTIMIZATION_2026-09-21.md`, `PROGRESS_LOG.md`, `docs/solver-ui/WEB_UI_PROTOTYPE_PROMPT.md`
  (`tools/preflop_suite`, `--history-rows`). Aggiungere una nota "esiste solo al tag `history7-final`", senza riscrivere la storia.
- **Viewer 3-way.** È un artifact privato dell'utente, "Short Deck 3-way 50a", con il generatore
  `SP/threeway/viewer/build_viewer_3way.py`. Va esteso con le fonti del passo 2 (snapshot, cambio, parte A) e ripubblicato
  sullo stesso artifact [VERIFIED: `PHASE3_SPEC` §8.1; `MONKER_RECIPE` §9.5].
- **Facoltativo** [VERIFIED: `P3B/review_removal.md` §10.4]: spostare le 7 fixture in `benchmarks/fixtures/` e aggiornare i
  percorsi del game test.

### T15. Pulizia (la fa l'utente) e decisioni che restano

- **Audit.** `P3B/cleanup_audit.md`, in sola lettura, con classi KEEP / DELETE CANDIDATE / ASK. Candidati principali
  [VERIFIED: `P3B/cleanup_audit.md` §4-5]:
  - circa 234 GB su F:: `hierarchy32`, `history7_optimized`, `hu40_history7_solve`, `matrix`, `suite`, `monker/correctness`, gli `smoke_*`;
  - il worktree Codex: 73,6 GB su C:;
  - i build vecchi: circa 13 GB;
  - 20 branch locali: 17 uniti e 3 di ricerca archiviati dai tag `archive/research/*`.
- **Decisioni dell'utente** del 01/10 verso le 23:48, più "Procedi" verso le 23:50 per i binari delle varianti [INFERRED: dal
  brief della sessione principale, non trovate nei file]:
  - F:, cartelle intere: history7 e suite (`hierarchy32`, `history7_optimized`, `hu40_history7_solve`, `matrix`, `suite`), i
    test di correttezza (`monker/correctness`), i vecchi smoke (`monker/smoke_continuous`, `smoke_policy_snapshots`,
    `smoke_policy_snapshots_g1`, `monker/step2/smoke`, `monker/step2/smoke2`);
  - F:, varianti: in ogni run di `monker/variants` tranne `HU50_m30x4_rake25`, `HU50_g1_rake` e `HU50_lock_all_m30x4_rake25`
    (tenuti interi), solo i binari: `*.ckpt`, `policy.bin` e gli altri `*.bin` sopra 1 MB. Chart, json, txt e log restanti
    rimangono;
  - C:: il worktree Codex e i build e le cartelle vecchie (circa 25 GB). Sono 24 alberi `out/build/*` stantii, i 4 worktree in
    `C:/tmp`, i 2 worktree `wf_da8b89e2-b70-*`, `M/.tmp`, 29 cartelle legacy e P9 e i 1.712 file sciolti in `M/out`;
  - branch: 13 locali e 14 remoti già cancellati alle 23:49 (sezione 4). Restano i 7 branch dei worktree, per `post_delete.sh`.
- **Script**, in `SP/cleanup`, ricontrollati alle 00:20 del 02/10 da una verifica indipendente:
  - **`delete_data.ps1`, lo esegue l'utente.**
    - Contiene 1.888 percorsi espliciti misurati alle 00:05:15. Prima di cancellare, ogni elemento viene ricontrollato:
      radice ammessa, nessun percorso protetto, nessuna junction attraversata, dimensione e data invariate, nessun file
      tracciato da git, nessun processo che lo nomina.
    - Dry run con `-UnlinkInnerJunctions`: 1.848 elementi OK, 445,34 GB in unità di 2^30 byte (F: 349,01, C: 96,33), 0
      rifiutati.
    - I 40 elementi di `benchmarks/results` (6,8 GB) restano fuori salvo `-IncludeBenchmarkResults`.
    - I 64 binari delle varianti coincidono, per percorso e dimensione, con un elenco indipendente di F:; i tre run tenuti e
      `step2/HU50` non compaiono [VERIFIED].
  - **Il worktree Codex contiene 2 junction** verso `M/out/preflop_blueprint_resources` (usata dal run 1) e
    `M/out/preflop_blueprint_buckets_200_500_1000`.
    - Senza `-UnlinkInnerJunctions` lo script rifiuta quell'elemento.
    - Con l'opzione toglie prima le 2 junction con `cmd /c rmdir` (senza `/s`). Poi controlla che le destinazioni esistano
      ancora, altrimenti si ferma. Cancella la cartella solo se non resta nessuna junction [VERIFIED: lettura dello script e
      dry run].
  - **`post_delete.sh`, lo esegue la sessione principale** dopo la cancellazione dell'utente. Non cancella dati:
    - toglie con `cmd /c rmdir` le 34 junction di `M/out` che puntano alle cartelle cancellate su F:, solo se la destinazione
      non esiste più e F: è collegato;
    - lancia `git worktree prune` solo se i worktree da potare sono esattamente i 7 attesi;
    - cancella i 7 branch: `-d` per i 4 uniti, `-D` per i 3 di ricerca solo se il tag `archive/research/*` punta allo stesso
      commit.

    Dry run delle 00:19: 34 junction tenute (destinazioni ancora presenti), 0 worktree da potare, 7 branch bloccati dai loro
    worktree. È l'esito atteso prima della cancellazione [VERIFIED].
- **Procedura.**
  1. L'utente, in Windows PowerShell: prima il dry run, poi la cancellazione, che chiede di scrivere `YES`. Log in
     `SP/cleanup/delete_data.log`.
     ```powershell
     powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Users\GORYNI~1\AppData\Local\Temp\claude\C--Users-GoryNickel-Documents-GitHub-GTO-Solver\94e439a5-4749-4983-9219-1f8b6fb59a5f\scratchpad\cleanup\delete_data.ps1" -WhatIf -UnlinkInnerJunctions
     powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Users\GORYNI~1\AppData\Local\Temp\claude\C--Users-GoryNickel-Documents-GitHub-GTO-Solver\94e439a5-4749-4983-9219-1f8b6fb59a5f\scratchpad\cleanup\delete_data.ps1" -UnlinkInnerJunctions
     ```
     Aggiungere `-IncludeBenchmarkResults` solo se l'utente vuole cancellare anche `benchmarks/results`, che non rientra nei
     "circa 25 GB" approvati [INFERRED]. `-SkipGroup <nome>` salta un gruppo.
  2. Un elemento modificato dopo le 00:05:15 viene rifiutato. In quel caso la sessione principale rilancia `measure.ps1`
     (sola lettura) e `python gen.py`, poi di nuovo il dry run. Lo stesso vale per una cartella cancellata solo in parte.
  3. Dopo la cancellazione, la sessione principale lancia `post_delete.sh`, prima con `-n`:
     ```bash
     bash /c/Users/GORYNI~1/AppData/Local/Temp/claude/C--Users-GoryNickel-Documents-GitHub-GTO-Solver/94e439a5-4749-4983-9219-1f8b6fb59a5f/scratchpad/cleanup/post_delete.sh -n
     bash /c/Users/GORYNI~1/AppData/Local/Temp/claude/C--Users-GoryNickel-Documents-GitHub-GTO-Solver/94e439a5-4749-4983-9219-1f8b6fb59a5f/scratchpad/cleanup/post_delete.sh
     ```
  4. **Quando.** Nessun elemento della lista è letto o scritto dal run 1 [VERIFIED: percorsi protetti nello script e controllo
     dei processi nel dry run]. Si può lanciare durante il run, mai durante un archivio su F:, e meglio fuori dai minuti del
     checkpoint a 16.000 (verso le 08:00) [INFERRED].
  5. Dopo, verificare con l'utente:
     - `git worktree list`;
     - il riepilogo di `post_delete.sh`, con le junction tolte e nessuna riga `UNLISTED`;
     - lo spazio libero su C: e F:;
     - il viewer HU50, che legge ancora `charts/**`.

     Per le junction non usare `cmd /c dir /AL /S` su `M/out`: può attraversare le junction e leggere tutto F:
     [INFERRED].
- **Decisioni che restano (DECISIONE UTENTE).**
  1. **Binari di `out/monker/step2/HU50` su F:**: 7,3 GB di `state.ckpt` e `policy.bin`. Servono solo a riprendere o
     rivalutare il run; il viewer legge solo `charts/` [VERIFIED: audit §4.1].
  2. **Fast-forward di `main` e `origin/main`.** `main` (`df4ca99`) era 135 commit dietro il branch di integrazione alla
     scrittura (di più dopo i commit di questo documento),
     `origin/main` è `77677b2` [VERIFIED: `git rev-list`]. Il fast-forward locale si fa con l'OK; quello su origin è un push
     dell'utente.
  3. **Rimozione del codice legacy** (libs `isomorphism`, `solver`, `postflop`, ..., `apps/gto_*`, test e strumenti legacy;
     audit §3.1-3.5): l'utente ha detto **"non ancora"** [INFERRED: dal brief del task, non trovato nei file]. Non toccarlo.
     Eccezione già avvenuta: le GUI desktop, tolte in `WT` dai 4 commit del 02/10 (sezione 4), secondo il messaggio di
     `921f424` su decisione dell'utente.
  4. **`docs/specifications/`** (17 file), il README e la ROADMAP del prodotto legacy. Descrivono il contratto "HU postflop
     esatto" che l'utente mantiene [VERIFIED: audit §3.5]. Chiedere.
  5. **Le sandbox `WT/out/laneH` e `WT/out/review3b`**, dopo T9 e T5 [INFERRED].
  6. **`benchmarks/results`** (6,8 GB, non tracciato e ignorato da git [VERIFIED: `git ls-files`]): fuori dalla pulizia salvo
     `-IncludeBenchmarkResults`.

### T16. Criteri di accettazione del prodotto (proposta da far approvare)

- **Contesto.** Il gate "migliore risposta astratta ≤ 0,03 a" è ritirato con history7, perché non c'è più una migliore
  risposta esatta dentro l'astrazione [VERIFIED: `P3B/removal_note.md`, "What is retired"]. Il gate dichiarato non era
  calcolabile sulle righe per classe di board del prodotto [VERIFIED: `SP/history7/recommendation.md` §0, §3].
- **Proposta [INFERRED], da scrivere in un documento breve e da far approvare:**
  1. **Correttezza (pass/fail).**
     - HU: la batteria HU, chiusa con 0 FAIL, si ripete in parte quando cambia il motore. V1 vale per ogni modifica della libreria.
     - 3-way: i test V del gate 3a, il referee, V11 e la batteria di T12.
  2. **Migliore risposta esatta sulle carte reali, misurata e riportata.**
     - HU: `gtosd_preflop_blueprint_monker_values --all-flops` (NashConv fisica, `gain_lower`, `gain_preflop`).
     - 3-way: la parte A solo preflop per seggio, con G3 ≤ 0,04 a per seggio; più tardi la parte B.
     - La NashConv fisica di HU50 non può fare da soglia: G1 è a circa 0,78 a, il 26 % del piatto, il pavimento dell'astrazione
       [VERIFIED: `SP/history7/recommendation.md` §3].
  3. **Confronto con MonkerSolver.** Con un criterio di somiglianza in EV: la perdita delle chart di MonkerSolver nel nostro
     gioco e i test del blocco. La distanza resta descrittiva, con la sua soglia di rumore. Il criterio in EV è ancora una
     decisione HU aperta [VERIFIED: `MONKER_RECIPE` §8, punto 2].
- **DECISIONE UTENTE** sulle soglie. Poi aggiornare la riga "Gate di accettazione" del diario.

### T17. Altri lavori in sospeso

1. **Distorsione del calcolatore di equity dell'utente.** `src/lib/multiwaySimulation.ts` in
   `C:/Users/GoryNickel/Documents/GitHub/equity-calculator-web-app` (multiway con range, 3-6 giocatori, Monte Carlo)
   campiona i range in sequenza. È distorto quando i range si bloccano, come si vede su AK+KQ contro AA contro JJ rispetto a
   un campionamento per rifiuto [VERIFIED: `MONKER_RECIPE` §9.2; `MEM/user-equity-calculator.md`].
   - Correzione proposta [INFERRED]: campionamento per rifiuto dell'intero deal, oppure pesi esatti, con un test contro
     l'enumerazione esatta C++ (`runTrueEnumeration`).
   - È il **repository dell'utente**: si lavora solo con il suo OK e dopo aver riletto il codice. La DLL resta in sola lettura.
2. **Decisioni HU aperte** [VERIFIED: `MONKER_RECIPE` §8, punti 1, 2, 4, 8, 9, 10; `PROGRESS_LOG.md` §1]:
   - configurazione HU di riferimento (rake e astrazione), con candidato 30 × 4 + TX2 (o TXM2) + donk, rake 2,5 % / cap 2a;
   - il run TXM2: nessuna cartella `HU50_m30x4_txm2_rake25` in `M/out/monker/variants` [VERIFIED: `ls`], quindi non è girato
     [INFERRED];
   - lo stato dell'estensione del 30 × 4 fino a 64.000 non è nel diario dopo le 14:40 del 30/09: da leggere in
     `out/monker/variants/HU50_m30x4_rake25/run.log`, su F: attraverso la junction;
   - il criterio di somiglianza in EV;
   - l'opzione A (algoritmo: Linear CFR, DCFR un board per batch): pronta, aspetta l'utente;
   - il test della radice del CO (open a 5a al 7 % contro lo 0,5 % di MonkerSolver): varianti dell'albero solo nei piatti
     rilanciati;
   - il test mancante "migliore risposta preflop contro un postflop risolto esattamente": costo da stimare;
   - l'ottimizzazione dei 35 minuti di HU40, in pausa.
   **DECISIONI UTENTE**, da riproporre in un unico elenco.
3. **Web UI.** Le decisioni D1-D8 di `docs/solver-ui/WEB_UI_PROTOTYPE_PROMPT.md` §0.1 hanno la colonna "Risposta" vuota
   [VERIFIED]:
   - D1 posizione del codice;
   - D2 worktree e branch;
   - D3 set di binari congelati;
   - D4 lettura di F:;
   - D5 build JS come "build" o come "codice";
   - D6 convivenza con i run a mano;
   - D7 ordine M0 → M1 → M2;
   - D8 dipendenze.

   L'utente ha già il worktree `C:/Users/GoryNickel/Documents/GitHub/GTO-Solver-solver-ui`, branch `feat/solver-ui`,
   `99e492a`, 4 commit suoi [VERIFIED: `git log`; audit §1.1]. Probabilmente ha già scelto alcune risposte [INFERRED].
   Chiedere e non toccare il suo worktree. Dopo T9 il prompt va corretto (riferimenti a `tools/preflop_suite` e `--history-rows`).
4. **Studio di fattibilità 4-6-way, dopo il 3-way.** Albero 6-way a regole HU: 0,47 M nodi (30a) - 1,5 M (40a). history7 è
   impossibile lì, e ora rimossa. Il solo bucket 500/1000/2000 darebbe 4,5-16 GiB a 6-way [VERIFIED:
   `MEM/preflop-product-target.md`].
   - Consegna [INFERRED]: una specifica come `PHASE3_SPEC` per N = 4..6. Contenuto: kernel a N seggi (inclusione-esclusione),
     cache delle classi a N giocatori, memoria, s/iterazione sul server NUMA, campionamento delle mani avversarie (R15),
     criteri di qualità per seggio.
   - Si parte dopo il "fatto" della fase 3, salvo diversa indicazione dell'utente.
5. **Push.** Di `feat/monker-step1-checkdown` dopo i merge, e di `feat/threeway-step2` se l'utente lo vuole: solo su richiesta.
6. **Facoltativi** [VERIFIED: `MONKER_RECIPE` §9.7, `P3B/referee3.md` §3]:
   - V7 statistico (distanza harness-HU contro due seed HU);
   - una config 3-way che eserciti C9;
   - 3WAY100;
   - la seconda passata facoltativa di §6.4 (EV degli altri seggi con le righe di s sostituite), sul server.

---

## 6. Calendario proposto [INFERRED, da annunciare all'utente prima di applicarlo, R9]

| Quando | Macchina | Agente, senza macchina |
|---|---|---|
| 02/10, dalla partenza del run 1 (00:09:30) alla fine (circa 11,6-19,3 ore dopo, fra le 11:45 e le 19:30 circa; tetto verso le 23:30) | Solo il run 1 (T1) | T12 (proposta della batteria), bozza di T16, voce del diario del 01/10 sera (T14), copia senza spazi delle chart e bozza della coda del blocco (T11), variante (i) di `apply_to_worktree` (T9b) |
| Fine del run 1 (02/10) | T3 (minuti), T4 (circa 1-1,5 ore), T5 D.2 e D3 con nuova build e V11, T6, T7 (0,9-2 ore) | Rapporto del gate 3b |
| Dopo T7 (02/10 sera, giorno libero, se l'utente è d'accordo) | Lancio del test del blocco (T11, circa 10 ore) | — |
| Durante il test del blocco (`-j 2`, almeno 4 GB, niente sopra 2 GB) | T9a (sandbox: configure, build, test, V1 hu10), T5 D.1 (mutazioni) | T14 |
| 02/10, 20:00-24:00 | T8 (archivio del run 1), mai durante un salvataggio del run del blocco | — |
| 03/10 (finestra normale: run fino alle 19:40, build e test fino alle 21:00) | Fine del blocco [INFERRED: 10-11,6 ore dopo la partenza a 1,5-1,74 s per iterazione, cioè circa 02:00-09:40 se partito fra le 16 e le 22], parte A sul blocco (2 × 1-2 ore), T9a ctest completa e V1 completo, T9b-c, T10 | Rapporti e diario |

Se l'utente preferisce chiudere history7 prima del test del blocco, T9 passa prima di T11: va detto in modo esplicito.

## 7. Decisioni dell'utente da raccogliere

| # | Decisione | Lavoro |
|---|---|---|
| U1 | ~~Liberare circa 1,5 GB di RAM perché il run 1 parta~~: non serve più, il gate si è aperto alle 00:09:29 | stato in alto, T1 |
| U2 | Ora di partenza del test del blocco (sera del 02/10 oppure 00:00 del 03/10) | T11 |
| U3 | Ordine di history7 rispetto al test del blocco, e variante (i) o (ii) dei commit | T9 |
| U4 | Parte A esatta del run 1 sull'i3 (8-18 ore) oppure su un server; server a noleggio per la fase 3c | T13 |
| U5 | Criteri di accettazione del prodotto e loro soglie | T16 |
| U6 | Eseguire la batteria di correttezza 3-way proposta | T12 |
| U7 | Pulizia: esecuzione degli script; binari HU50 del passo 2; `main`/`origin/main`; codice legacy ("non ancora"); `docs/specifications`; sandbox | T15 |
| U8 | Push dei branch | T10, T17 |
| U9 | Decisioni HU aperte: configurazione di riferimento, TXM2, criterio in EV, opzione A, radice del CO, test della migliore risposta contro un postflop esatto, HU40 | T17 |
| U10 | Decisioni D1-D8 della web UI | T17 |
| U11 | Correzione del calcolatore di equity nel suo repository | T17 |
| U12 | Partenza dello studio 4-6-way | T17 |

## 8. Trappole note

1. **`apply_to_worktree.sh` dopo il commit della parte A** la cancellerebbe da tre file (T9b).
2. **Percorsi con spazi in `TRAIN_ARGS`**: il runner non li quota (T11).
3. **Gate delle catene.**
   - `chain.sh` aspetta un trainer vivo, salvo `REQUIRE_TRAINER=0`.
   - `h7_chain.sh` aspetta un trainer vivo, salvo `h7.NO_TRAINER_OK`.
   - Con `*.CANCEL` presente escono al primo gate.
   - Condividono `P3A/build.lock`. Un lock rimasto da un crash blocca tutte le build: leggere `owner` e toglierlo solo se
     nessuna build è viva.
4. **Coda del run.** Un `queue.lock` vecchio dopo un riavvio va tolto a mano. Un `QUEUE_CANCEL` rimasto fa rifiutare il riavvio.
   Il gate vuole 18.874.368 KB liberi e zero trainer.
5. **TaskStop non uccide gli script bash in coda** (R7).
6. **`SP` sta in `%TEMP%`.** Windows potrebbe ripulirla [INFERRED]. Contiene script che non sono nel repository: `after_run.md`,
   le catene, `apply_to_worktree.sh`, `v1_baseline.sh`, `v1_summary.py` con `v1_baseline/`, `vsdev.cmd`, `archive_run.ps1`,
   il generatore del viewer. Se manca qualcosa, chiedere alla sessione principale prima di ricostruirlo.
7. **Build MSVC da Git Bash**: solo attraverso `vsdev.cmd` (R13). Un server di viewer attivo blocca il link dell'eseguibile di
   export (LNK1104).
8. **Fine riga.** Alcuni file del worktree sono CRLF, la sandbox è LF. Un file letto da bash con CRLF rompe i `read`.
9. **Lettori aperti.** `tail -f` o editor aperti su file di un run fanno fallire `archive_run.ps1`.
10. **Numeri della parte A.** Il guadagno campionato è una stima per eccesso. Le SE pooled sono corrette solo con la patch D1.
    L'identità del rake non prova i kernel.
11. **Memoria della macchina.** Le applicazioni dell'utente (Brave, Claude, ChatGPT, Steam, ...) possono tenere chiuso il gate
    dei 18,87 GB. È successo il 01/10 alle 16:50 e il 02/10 alle 00:00 (gate aperto alle 00:09:29).
12. **`grep -c` sul `CTestTestfile.cmake`.** Il conteggio dei file di config sull'intero file non prova la registrazione del
    referee 3-way, perché lo stesso file compare nel test dello schema delle fixture (T4, punto 7).

## 9. Fonti lette per questo documento

- `MEM/MEMORY.md` e le note `machine-windows`, `subagent-model-opus`, `freeze-running-scripts`,
  `no-sendmessage-to-workflow-agents`, `phase3-decisions-2026-10-01`, `history7-retire-decision`, `preflop-blueprint-program`,
  `preflop-product-target`, `run-storage-ssd-then-f`, `user-equity-calculator`, `taskstop-orphans`, `msvc-build-from-git-bash`,
  `multiway-exactness-not-required`, `roadmap-changes-explicit`, `monker-charts-rake`, `monker-settings-unknown`,
  `bash-heredoc-backslashes`.
- `P3B/{partA.md, review_partA.md, after_run.md, referee3.md, removal_note.md, review_removal.md, cleanup_audit.md, chain.sh,
  chain.log, locked.sh, build.cmd}`, `H7/{h7_chain.sh, chain.log, apply_to_worktree.sh, patch_removal.py (testa),
  dryrun/*.patch (testa), h7_*.cmd}`, `P3B/review_partA/{patch_partA_se.py, review_partA_mutations.py}` (testa),
  `P3A/{final_locked.sh, final_build.cmd, final_ctest.cmd, v1_baseline.sh}`.
- `SP/history7/recommendation.md`.
- `DOCS/MONKER_RECIPE_REPRODUCTION_2026-09-28.md` §8, §9.2, §9.5, §9.7, §10 (10.1-10.4, 10.10-10.13);
  `DOCS/PROGRESS_LOG.md` §1-2 e le voci del 01/10; `DOCS/threeway/PHASE3_SPEC_2026-09-30.md` §1, §6.3-6.5, §9, §10.
- `docs/solver-ui/WEB_UI_PROTOTYPE_PROMPT.md` §0.1, §1, §2.
- In sola lettura: `M/out/frozen/queue_3way50_15x4.sh`, `M/out/frozen/threeway_step2_12fe441/tools/monker_compare/run_step2_continuous.sh`,
  `OUT1/queue.log`; `git status`, `git log`, `git rev-parse` e `git worktree list` in `M` e in `WT`.
- Verifica delle 00:15-00:40 del 02/10: `SP/cleanup/*` (script, manifest, log, dry run rieseguiti),
  `P3B/cleanup_audit.md`, `OUT1/{queue.log,train.jsonl}`, `WT/tests/CMakeLists.txt`,
  `WT/out/build/windows-release-suite/tests/CTestTestfile.cmake`, `H7/{apply_to_worktree.sh,removal_full.patch}`,
  `git log 2aa24d8..c2e9138` e `git branch -a`.
