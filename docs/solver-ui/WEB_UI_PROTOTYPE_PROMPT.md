# Prompt per l'agent coder: prototipo della web UI di comando di GTO-Solver

Versione del 30/09/2026, revisione 2. Questa revisione incorpora le correzioni di una revisione critica fatta sul codice.

Il destinatario è un agent coder che non ha mai visto questo progetto. Il documento è autosufficiente: le appendici contengono i fatti d'interfaccia del solver, raccolti con ricerche in sola lettura nel repository. Lo stato di riferimento è il branch `feat/monker-step1-checkdown` del 30/09/2026 sera. Il branch è attivo: altre sessioni committano in parallelo, quindi numeri di riga e dettagli possono essersi spostati.

**Per l'utente, prima della consegna.** Compila la colonna "Risposta" della sezione 0.1. Una risposta lasciata vuota vale come "chiedimelo": l'agent la chiederà prima di iniziare.

**Legenda**
- **[V]** = verificato nel codice o nei dati (file:riga, oppure file letto).
- **[I]** = dedotto e non provato. Verificalo prima di costruirci sopra e annota l'esito in `DECISIONS.md`.
- Se il codice contraddice questo documento, vale il codice. Segnala la discrepanza all'utente.

**Percorsi**
- `REPO` = `C:/Users/GoryNickel/Documents/GitHub/GTO-Solver`: il checkout principale. Altre sessioni ci lavorano e ci committano in parallelo.
- `CHARTS` = `C:/Users/GoryNickel/Documents/GitHub/GTO-Chart-Browser`: la web app di chart dell'utente. Non va modificata.
- I percorsi senza prefisso sono relativi a `REPO`.

---

## 0. Come procedere

1. Leggi tutto il documento, appendici comprese, prima di scrivere codice.
2. Controlla la tabella 0.1.
   - Se manca una risposta, fai all'utente **tutte** le domande mancanti insieme, una sola volta, prima di iniziare.
   - Finché non hai le risposte puoi solo leggere il repository.
   - Registra risposte e scelte in `<app>/docs/DECISIONS.md`.
3. Crea branch e worktree come descritto nella sezione 11. Poi procedi per milestone, da M0 a M3 (sezione 10).
4. Alla fine di ogni milestone consegna quattro cose:
   - una demo su fixture o in mock mode;
   - l'elenco dei test verdi;
   - le domande che servono per la milestone successiva (sezione 13.2);
   - i fatti [I] che hai verificato o smentito.
5. Lingua:
   - L'utente scrive in italiano: rispondigli in italiano.
   - Anche i testi della UI sono in italiano.
   - Codice, identificatori, messaggi di commit e commenti sono in inglese.

### 0.1 Decisioni dell'utente (bloccanti)

| # | Domanda | Opzioni e contesto | Default proposto | Risposta |
|---|---|---|---|---|
| D1 | Dove sta il codice dell'app? | `tools/solver-ui/`, oppure `apps/solver-ui/`. Nel secondo caso il glob CMake del target `format-check` scansiona la cartella a ogni configure. In più, `apps/` contiene già le GUI desktop Qt/ImGui (sezione 4.6). | `tools/solver-ui/` | |
| D2 | Da quale branch parte il worktree e dove lo metto? | Il worktree non tocca il checkout principale. | HEAD corrente di `feat/monker-step1-checkdown`; worktree in `C:/Users/GoryNickel/Documents/GitHub/GTO-Solver-solver-ui`, branch `feat/solver-ui` | |
| D3 | Bin set frozen: come rendere lanciabili step 1, tree check e validazione? | Nessun set contiene tutti i tool (Appendice G). `bin_correct/{base,c123}` hanno `game`, `monker_buckets`, `monker_values` e `train`, con README e SHA256SUMS. `checkdown` esiste solo in `bin_rake`; `checkdown_classes` e `monker_tree` solo in `bin_3way_step1`. Nessuno di questi due ha README o SHA256SUMS. **(a)** L'utente, in una finestra build, fa costruire un set frozen completo con tutti i tool usati dalla UI (`train`, `monker_values`, `game`, `checkdown`, `checkdown_classes`, `monker_tree`, `monker_buckets`), con README e SHA256SUMS. **(b)** I set vecchi si registrano con hash calcolati alla registrazione e confermati dall'utente; i tool si risolvono per tool e si registra il commit, se noto. | (a). Finché il set completo non esiste, M2 lancia solo `step2` (con `c123`) e `validate`; gli altri kind restano in mock. | |
| D4 | Posso leggere le run archiviate su `F:` (sottocartelle di `out/monker/variants`, `out/monker/step2`, `out/monker/smoke_*`, tutte junction)? | Solo lettura di file di testo, mai scansioni ricorsive all'avvio. Senza questa lettura VIEW RESULTS non ha nessuna run HU50 step 2 da confrontare con Monker: stanno tutte su `F:`. I giochi di correttezza su `C:` non hanno un riferimento Monker. | Sì, in sola lettura | |
| D5 | Build JS/Python, installazione delle dipendenze e suite di test in mock (pytest, Vitest, Playwright) contano come "build/test", ammessi solo fino alle 21:00, oppure come "scrivere codice", sempre ammesso? | La tua regola dice: scrivere codice è sempre ammesso; le build e i test del solver solo fino alle 21:00 (salvo estensioni del giorno). | Finché non rispondi: solo fino alle 21:00. Dopo le 21:00 si scrive soltanto codice. | |
| D6 | Convivenza con le run lanciate a mano, da te o da altre sessioni Claude | Le code a mano ignorano la UI, quindi il budget di thread della UI non può impedire l'oversubscription. **(a)** Fasce orarie separate (per esempio: la UI lancia solo in certe ore). **(b)** La UI conta i processi esterni e aspetta, senza garanzie. **(c)** Da una data in poi solo la UI lancia run. Sotto-domanda: la UI può scrivere `STOP`/`CANCEL` in run esterne, con conferma, oppure le mostra soltanto? | (b); le run esterne si mostrano soltanto e non si toccano mai | |
| D7 | Ordine delle milestone: VIEW RESULTS (M0), poi MONITOR (M1), poi LAUNCH con stop/resume (M2) | È diverso dall'ordine della richiesta, dove LAUNCH era il primo. Motivo nella sezione 1. | Va bene così | |
| D8 | Posso aggiungere dipendenze npm e pip nuove? | Versioni pinnate, lockfile committati, pip in un venv dedicato, licenze permissive, nessuna telemetria | Sì | |

---

## 1. Obiettivo e utenti

**Utente.** C'è un solo utente, il proprietario del progetto. Non servono account né ruoli. Comanda il solver dal browser del proprio PC.

**Macchina del prototipo.** Solver e UI girano sullo stesso PC di sviluppo:
- Windows 11, i3-10100F con 4 core / 8 thread, 32 GB di RAM;
- SSD `C:` con circa 300 GB liberi;
- `F:`, un disco USB lento usato come archivio;
- Git Bash + PowerShell, build MSVC.

**Macchina di produzione (futura).** È pianificata con 52 core / 104 thread, 256 GB di RAM e 2 nodi NUMA. **L'OS non è deciso** e non va chiesto: progetta per Windows e Linux, implementa e testa prima Windows. L'utente la comanderà dal suo PC via browser.

**Funzioni del primo prototipo**
1. **LAUNCH**: creare e accodare run. Si scelgono gioco, astrazione, iterazioni, snapshot, valutazioni e priorità.
2. **MONITOR**: vedere la coda, il progresso, l'ETA, CPU/RAM, i log e le curve di convergenza e di exploitability. Da qui si fanno anche stop, resume ed extend.
3. **VIEW RESULTS**:
   - i chart per nodo, su una matrice **9x9** delle 81 classi di mano;
   - il confronto con i chart di MonkerSolver;
   - l'exploitability.

**Fuori scope**
- la gestione dello storage (archiviazione su `F:`, spostamenti, pulizia);
- la multi-macchina e gli account;
- la build del C++ dalla UI;
- **qualunque cancellazione di dati**, anche automatica.

**Ordine di consegna (cambio esplicito rispetto alla richiesta).** L'utente ha elencato LAUNCH per primo. Il piano consegna:
- M0: VIEW RESULTS;
- M1: MONITOR in sola lettura;
- M2: LAUNCH, con stop, resume ed extend;
- M3: completamenti.

Il motivo è la sicurezza. Il lancio è la parte rischiosa: processi lunghi, finestre orarie, RAM, dati. Ha bisogno di parser, adapter, tail degli eventi e mock già collaudati. VIEW RESULTS e MONITOR in sola lettura non possono fare danni e si testano subito su dati reali.

Stop e resume, che l'utente ha messo in MONITOR, arrivano in M2 insieme a LAUNCH, perché richiedono il supervisor dei processi. Tutte e tre le funzioni restano nel primo prototipo. La decisione D7 conferma quest'ordine; se l'utente lo cambia, segui la sua scelta.

**Nota sulla matrice.** Nel brief compariva "13x13", ma lo short deck ha 9 rank (6..A): la matrice è 9x9 = 81 classi.
- Il README di GTO-Chart-Browser dice "13x13" (`CHARTS/README.md:15`).
- Il suo codice però disegna 9x9 [V] (`CHARTS/app/src/components/HandMatrix.tsx:82`, `app/src/lib/constants.ts:1-17`).

---

## 2. Il progetto in breve (per chi non l'ha mai visto)

**Il solver.** GTO-Solver è un solver C++ (CMake + Ninja + MSVC, vcpkg) per il poker short deck. Il prodotto attuale è un *preflop blueprint* che riproduce i chart di MonkerSolver. L'obiettivo finale è il preflop 6-way.

**Carte e classi.** Lo short deck ha 36 carte, con rank 6..A, e quindi 81 classi di mano:
- 9 coppie, 36 suited e 36 offsuit;
- 630 combo in tutto: 6 per ogni coppia, 4 per ogni suited, 12 per ogni offsuit.

**Soldi**
- 1 ante (simbolo **a**) = 10.000 *units*.
- Ogni giocatore mette un ante "morto". Il BTN mette anche un *button blind* vivo, pari a 1 a in tutte le config attuali.
- Pot iniziale = ante × giocatori + button blind: 3 a in HU, 4 a in 3-way.
- Gli stack sono espressi in ante: HU50 = 50 a = 500.000 units.

**Posizioni.** Sono elencate in ordine d'azione e l'ultima è sempre `BTN`:
- HU: `CO, BTN`;
- 3-way: `UTG, CO, BTN`;
- 6-way: `UTG, UTG+1, MP, HJ, CO, BTN`.

**Due step di risoluzione**
- **Step 1 ("checkdown").**
  - È l'albero preflop, in cui ogni continuazione postflop è risolta come showdown sul runout completo. È esatto.
  - In HU lavora a livello di combo: `gtosd_preflop_blueprint_checkdown`.
  - Con 2-3 giocatori lavora a livello di classe: `gtosd_preflop_blueprint_checkdown_classes`.
  - Dura minuti e usa meno di 0,5 GB.
  - Non ha checkpoint.
- **Step 2.**
  - È il gioco HU completo. Il betting postflop usa un'astrazione di carte: bucket table + classi di texture del board.
  - L'algoritmo è vector CFR con chance sampling pubblico, DCFR (1.5, 0, 2) e *lazy discount*.
  - Eseguibile: `gtosd_preflop_blueprint_train`.
  - **Solo heads-up** [V `libs/preflop_blueprint/src/trainer.cpp:472-476`].
  - Dura da 30 minuti a 4 ore e usa da 1 a 16 GB di RAM.
  - Ha checkpoint e resume.
  - La UI lo lancia sempre con `--eval-every 0`. In quel caso il trainer **non termina mai `CONVERGED`**: finisce con `ITERATION_LIMIT`, `STOPPED` oppure `FAIL`.
- **Oltre l'HU**: 3-way step 2 e 6-way **non sono addestrabili oggi**. La UI deve essere estendibile per "run kind".

**Misure di qualità**
- **Exploitability**
  - È il guadagno del best response per giocatore, in ante per mano. **NashConv** è la somma dei guadagni.
  - Il target è l'1 % del pot iniziale, cioè 0,03 a in HU.
  - Per lo step 2 esiste solo dal valutatore separato `gtosd_preflop_blueprint_monker_values --all-flops`. È esatto sui 573 flop canonici (7.140 flop fisici, 605.088 board).
  - `gain_lower` conta solo le deviazioni postflop e tende al "pavimento" dell'astrazione.
  - `gain_preflop` conta solo le deviazioni preflop.
- **Step 1 3-way**: si mostra il gain per seat. Con 3 giocatori CFR non ha garanzia di Nash, quindi NashConv non va mostrato come misura di equilibrio.
- **Confronto con MonkerSolver**: ci sono tre misure:
  - la distanza tra le strategie (total variation);
  - la differenza di range;
  - la perdita che i chart Monker subiscono se giocati nel nostro gioco.

**Impostazioni di Monker**
- Le impostazioni con cui sono stati risolti i chart Monker (rake, bucket, size) **non sono note e l'utente non le conosce: non chiederle mai**. Si deducono dai dati.
- L'utente ritiene che i chart siano stati risolti con rake 5 %, cap 3 a, no flop no drop.

**Documenti di riferimento**
- `docs/research/preflop_vector_cfr/MONKER_RECIPE_REPRODUCTION_2026-09-28.md`: ricetta, risultati e incidenti.
- `docs/research/preflop_vector_cfr/PROGRESS_LOG.md`.
- `benchmarks/monker/correctness/README.md`: giochi di correttezza e tree fingerprint attesi.
- `out/monker/correctness/results_2026-09-30.md`.

---

## 3. Regole operative dell'utente (vincoli non negoziabili)

| # | Regola | Conseguenza per backend e UI |
|---|---|---|
| R1 | **Finestre macchina.** Le run girano 00:00-20:00. Build e test sono ammessi fino alle 21:00. Le estensioni valgono solo per il singolo giorno (per esempio il 30/09 la macchina era disponibile fino alle 22:00). | Le finestre sono un'impostazione per host, in **ora locale dell'host**, con override per data nel file di config. Nessun processo solver gestito gira fuori finestra. **Una run non parte se non può finire dentro la finestra** (regole esatte in 5.5). Sul server di produzione la finestra può essere `off`. |
| R2 | Le run eseguono **copie frozen** di eseguibili e script, mai il build tree. Mai modificare uno script che bash sta leggendo: bash legge per offset, e una modifica a metà run ne ha rotta una il 29/09. | Il backend lancia **direttamente** gli eseguibili, senza bash. Li prende da *bin set* registrati e immutabili, con SHA256 verificato prima di ogni spawn. Rifiuta ogni percorso sotto `out/build`. Congela nella run dir config, texture map e lock chart; gli script Python si congelano sotto `data_root`. |
| R3 | Checkpoint e resume a segmenti. | Ogni processo trainer riceve `--checkpoint` e `--stop-file`. Extend = un nuovo segmento con `--resume` e un target più alto. |
| R4 | Un file `CANCEL` ferma un driver prima del segmento successivo. | Il backend controlla `<run>/CANCEL` prima di ogni step, che sia stato creato a mano o dalla UI. |
| R5 | Le run si addestrano su SSD e poi si archiviano in `F:\GTO-Solver-out`, lasciando delle junction; un solo archiver per cartella. | L'archiviazione è fuori scope. Però: `runs_root` sta su `C:`; non si scrive mai attraverso una junction; ogni run dir della UI non ancora in uno stato finale contiene un file `NO_ARCHIVE` (l'archiver dell'utente lo rispetta); se una run dir della UI diventa una junction, la run è `archived`: sola lettura, niente resume, niente scritture. Le cartelle su `F:` si mostrano come "disco lento" o "non raggiungibile". |
| R6 | **Mai cancellare dati senza l'OK dell'utente.** | Il prototipo non ha endpoint di delete e **nessuna cancellazione automatica**, nemmeno delle `policy.bin` degli snapshot. Le sole operazioni ammesse sulle run dir della UI sono le rinomine della sezione 5.8 (snapshot "superseded") e la rimozione del proprio marker `NO_ARCHIVE`. |
| R7 | Una singola run deve restare sotto circa **16 GB** sul PC attuale. | Prima dell'avvio c'è un admission check sulla RAM stimata. Il limite è configurabile per host. |
| R8 | La macchina è condivisa con altre sessioni (agent Claude) che lanciano run a mano. | I processi `gtosd_*` non gestiti dalla UI contano nei budget di thread e RAM. Non vanno mai toccati, salvo quanto deciso in D6. |
| R9 | Un kill deve fermare l'intero albero di processi, con verifica per PID. Un "TaskStop" una volta ha lasciato vivi 5 script, che sono partiti a mezzanotte. | Il supervisor uccide solo processi che ha nel DB, identificati da (pid, create_time, exe), insieme ai loro discendenti. **Mai kill per pattern di command line**: le scansioni per pattern servono solo a mostrare processi. Dopo il kill si verifica con un nuovo scan. Un job cancellato non deve **mai** partire, nemmeno dopo un restart del backend. |
| R10 | I cambi di roadmap vanno resi espliciti. | Non riordinare milestone o scope in silenzio: annuncia cosa si sposta e perché. |

---

## 4. Architettura

### 4.1 Componenti

```
 PC dell'utente                                Solver host (oggi: lo stesso PC)
 ┌──────────────────────┐   localhost, oppure  ┌───────────────────────────────────────────────┐
 │ Browser              │   SSH tunnel o rete  │ solver-host service (backend)                 │
 │  Frontend SPA        │   privata (Tailscale │  - REST API + SSE per gli aggiornamenti live  │
 │  (build statica      │ ───────────────────▶ │  - auth a token, CSRF, allowlist Host/Origin  │
 │   servita dal        │    / WireGuard)      │  - catalog: bin set, bucket, texture, config, │
 │   backend)           │                      │    chart Monker, lock chart                   │
 └──────────────────────┘                      │  - job store (SQLite) + scheduler             │
                                               │  - supervisor dei processi (spawn diretto)    │
                                               │  - tailer di log/eventi, sampler CPU/RAM/disco│
                                               │  - adapter delle run dir (4 layout esistenti) │
                                               │  - mock solver (sviluppo e test)              │
                                               └──────────┬─────────────────────┬──────────────┘
                                                          │ argv validati       │ letture file
                                                          ▼                     ▼
                                  eseguibili frozen (out/monker/bin_*)   run dir, chart Monker, config
```

- Il backend gira **sulla macchina del solver**. Il frontend è una SPA statica che il backend stesso serve, sulla stessa origin.
- C'è un solo backend per host. Il frontend non deve cablare un host specifico: usa l'origin da cui è stato servito.

### 4.2 Rete e accesso remoto (sintesi; i dettagli sono nella sezione 6)

- **Bind di default**: `127.0.0.1:8765` (porta configurabile).
- **Accesso remoto**, solo attraverso una rete privata (la scelta concreta è la domanda Q15):
  - **SSH tunnel**: `ssh -N -L 8765:127.0.0.1:8765 utente@host`;
  - **Tailscale**: `tailscale serve`, che inoltra a localhost e lascia il bind su loopback;
  - **WireGuard**: bind esplicito sull'indirizzo dell'interfaccia WG.
- **Vietato**:
  - esporre il servizio sull'internet pubblico: niente port forwarding sul router e niente `tailscale funnel`;
  - fare bind su `0.0.0.0` o su un indirizzo non loopback, salvo con `server.allow_non_loopback = true` più un indirizzo esplicito.
- **Autenticazione obbligatoria anche su localhost**: altri processi locali o pagine web aperte nel browser potrebbero chiamare l'API.

### 4.3 Cross-platform

- Nel percorso di esecuzione delle run non ci sono bash, PowerShell o `.cmd`: il backend fa lo spawn diretto degli eseguibili, con una lista argv.
- I nomi degli eseguibili si risolvono per piattaforma (`.exe` su Windows). I percorsi usano sempre il formato nativo: mai `/c/...`.
- Il controllo dei processi passa per un'interfaccia con due implementazioni.
  - **Windows**:
    - Lo spawn usa `CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW`, più `CREATE_BREAKAWAY_FROM_JOB` quando il backend gira dentro un job che lo permette.
    - Finché il backend è vivo, ogni step può stare in un Job Object senza nome, **senza** `KILL_ON_JOB_CLOSE` (altrimenti le run muoiono quando muore il backend). Serve per il kill dell'albero e per l'accounting.
    - Dopo un restart del backend il job non è più raggiungibile [I: un job con nome sparisce quando si chiude l'ultimo handle]. Il kill passa allora dall'albero dei processi letto con psutil, a partire da (pid, create_time) verificati.
  - **POSIX**: `start_new_session=True`, poi `killpg` del process group verificato.
- La telemetria di CPU e RAM viene **dall'OS** (per esempio psutil), non dal trainer. Il trainer riporta `process_bytes` e i picchi solo su Windows: altrove valgono 0 [V `trainer.cpp:2560-2587`].
- Gli strumenti Python si invocano con l'interprete configurato (`paths.python`), oppure si importano da copie congelate (sezione 5.7).
- Test:
  - Oggi i test del supervisor girano su Windows.
  - Il ramo POSIX va almeno coperto da unit test con i processi mockati. Il repo non ha CI [V].
  - Se esiste un ambiente Linux o WSL (domanda Q18), usalo anche per il ramo POSIX.

### 4.4 File di configurazione

Il backend legge un unico file: `solver-ui.toml` (oppure JSON). Il percorso si passa con `--config` o con la variabile `SOLVER_UI_CONFIG`. Tutti i percorsi e i limiti stanno lì, non nel codice.

**Mock e reale non si mescolano mai.**
- `mode = "mock"` è il default. Usa i propri `data_root`, `runs_root` e DB, e rifiuta di lanciare bin set reali.
- `mode = "real"` richiede che l'utente abbia dato l'OK. In questa modalità i bin set mock non esistono.
- Il DB registra la propria modalità e rifiuta di aprirsi nell'altra.

```toml
mode = "mock"                   # "mock" | "real" (reale solo con OK dell'utente)

[server]
bind = "127.0.0.1"
port = 8765
allowed_hosts = ["localhost:8765", "127.0.0.1:8765"]   # anti DNS-rebinding; con la porta
allowed_origins = ["http://localhost:8765", "http://127.0.0.1:8765"]
dev_origins = ["http://localhost:5173"]                # solo con --dev, mai in remoto
allow_non_loopback = false
session_days = 7

[auth]
token_file = "out/solver-ui/secrets/token"    # token generato; niente password

[paths]                         # relativi a repo_root se non assoluti
repo_root   = "C:/Users/GoryNickel/Documents/GitHub/GTO-Solver"
data_root   = "out/solver-ui-mock"            # reale: "out/solver-ui"
runs_root   = "out/solver-ui-mock/runs"       # reale: "out/solver-ui/runs" (SSD)
python      = "python"
bin_sets    = ["out/monker/bin_correct/*", "out/monker/bin_*", "out/monker/bin"]
resources   = "out/preflop_blueprint_resources"
bucket_sets = ["out/monker/buckets_*", "out/monker/correctness/buckets/*"]
texture_maps = ["benchmarks/monker/textures/*.txt"]
game_configs = ["benchmarks/monker/*.json", "benchmarks/monker/correctness/*.json", "out/solver-ui/configs/*.json"]
lock_charts = ["out/monker_lock/charts_50a", "benchmarks/monker/correctness/lock_limp_check", "benchmarks/monker/correctness/lock_b0m"]
monker_library = "C:/Users/GoryNickel/Documents/GitHub/GTO-Chart-Browser/ranges/Short Deck"
analysis_tools = "tools/monker_compare"
archive_read_roots = ["F:/GTO-Solver-out"]   # destinazione reale delle junction, sola lettura

[[browse_roots]]                # run esistenti, sola lettura
path = "out/monker/correctness"
[[browse_roots]]
path = "out/monker/step1"
[[browse_roots]]
path = "out/monker/step1_3way"
[[browse_roots]]
path = "out/monker/variants"    # sottocartelle = junction verso F: (USB lento)
enabled = false                 # true solo se D4 = sì
slow = true

[limits]
threads_total = 8               # budget macchina, include i processi non gestiti
threads_per_run_max = 8
ram_per_run_gb = 16
ram_reserve_gb = 4              # RAM lasciata libera al sistema
disk_reserve_gb = 20
max_parallel_runs = 2
validate_max_nodes = 5000000    # --max-nodes del game tool nei job di validazione
short_job_timeout_s = 300

[windows]
enabled = true                  # false = server dedicato senza finestra
timezone = "local"              # ora locale dell'host; la UI mostra il fuso
runs  = "00:00-20:00"
tests = "00:00-21:00"
stop_margin_min = 15            # STOP graceful a fine finestra meno 15 min
kill_margin_min = 2             # kill a fine finestra meno 2 min
min_useful_minutes = 20         # non avviare un segmento continuous se restano meno minuti
[[windows.overrides]]
date = "2026-09-30"
runs = "00:00-22:00"

[mock]
speed = 50
```

### 4.5 Scelta tecnologica: la decide l'agent, entro questi vincoli

**Vincoli**
1. Deve girare oggi su Windows 11 e domani, forse, su Linux. Non deve dipendere da Docker.
2. A runtime non usa servizi esterni: niente cloud, niente CDN, nessuna telemetria. Tutti gli asset sono inclusi nella build.
3. Strumenti presenti sul PC il 30/09 [V]: Node v24.19.0, npm 11.1.0, pnpm 10.28.1, Python 3.13.2. psutil 7.1.0 e FastAPI 0.136.3 sono installati globalmente, ma usa comunque un venv con dipendenze pinnate.
4. I lockfile vanno committati e le versioni pinnate.

**Stack preferito** (per coerenza con le altre app dell'utente; la scelta finale spetta a te, motivata)

| Livello | Preferito | Perché |
|---|---|---|
| Backend | Python 3.13, FastAPI + uvicorn, pydantic v2 | Gli strumenti di analisi sono in Python stdlib (`compare_charts.py`, `monker_in_our_game.py`): importarne una copia congelata dà numeri identici a quelli che l'utente conosce. Nessuna compilazione. |
| Processi e telemetria | psutil; ctypes o pywin32 per i Job Object | psutil è il modo cross-platform più solido per leggere albero dei processi, cmdline, create_time, CPU e RAM. |
| Store | SQLite (stdlib `sqlite3`, WAL) in `data_root` | È transazionale e sopravvive ai restart. Niente file di testo parsati da bash: un CRLF ha già rotto una coda. |
| Live | SSE (EventSource) + REST | Il flusso server→client basta. Funziona attraverso SSH tunnel e Tailscale, e si riconnette da solo con `Last-Event-ID`. |
| Frontend | React 19 + Vite + TypeScript strict + Tailwind 4 + TanStack Query | È lo stack delle app dell'utente (GTO-Chart-Browser): si possono riusare HandMatrix, getCellBackground, costanti, FreqBar e la logica di ActionPathBuilder (Appendice J). |
| Grafici | uPlot (serie lunghe) oppure Recharts | A tua scelta. |
| Tipi dell'API | OpenAPI generato da FastAPI, poi `openapi-typescript` | Una sola fonte per i tipi. |
| Test | pytest con fake clock; Vitest + Testing Library; Playwright per gli e2e | |
| Serving | Il backend serve `frontend/dist`; in sviluppo Vite fa da proxy per `/api` | Stessa origin, quindi niente CORS. |

**Alternative e dettagli**
- Un backend Node/TypeScript (per esempio Fastify) è accettabile se lo motivi: avresti i tipi condivisi con il frontend. Però gli strumenti Python diventano subprocess, e su Windows leggere cmdline e albero dei processi richiede dipendenze in più.
- Ogni scelta diversa dallo stack preferito va scritta in `DECISIONS.md`, con il motivo, e comunicata all'utente.
- SSE e autenticazione: EventSource non può impostare header, quindi l'autenticazione passa dal cookie di sessione.

### 4.6 Posizione nel repo e layout

**Posizione (D1).** Il default raccomandato è `tools/solver-ui/`, per tre motivi [V]:
- `CMakeLists.txt:64-69` esegue `file(GLOB_RECURSE GTOSD_FORMAT_FILES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/apps/*.cpp ...)` per il target `format-check`. Sotto `apps/`, `node_modules` e i venv verrebbero riscansionati a ogni build, e ogni `.cpp` dentro un pacchetto entrerebbe nel controllo clang-format.
- `apps/` contiene già le app desktop `gto_cli`, `gto_gui`, `gui_qt_prototype` e `gui_imgui_prototype` (`CMakeLists.txt:104-117`). Non c'entrano con questo lavoro e non vanno toccate.
- Nessun glob scansiona `tools/`.

Non modificare `CMakeLists.txt`.

Struttura proposta:

```
<app>/
  README.md                  setup, avvio, modello di sicurezza, mock mode
  .gitignore                 node_modules/, dist/, .venv/, coverage, test-results/ ...
  docs/DECISIONS.md          risposte dell'utente e scelte fatte
  config/solver-ui.example.toml
  backend/                   pacchetto solver_ui (API, scheduler, supervisor, adapter, parser)
  frontend/                  SPA
  mock/                      mock degli eseguibili + replay
  fixtures/                  piccoli campioni reali + casi sintetici (MAI checkpoint o policy)
```

Il `.gitignore` di root [V] contiene `/out/`, `*.bin`, `__pycache__/` e altro, ma **non** `node_modules`, `dist` né `.venv`: servono `.gitignore` annidati.

**Dati di runtime** (fuori da git, perché `/out/` è ignorato) in `REPO/out/solver-ui/` (reale) oppure `REPO/out/solver-ui-mock/` (mock):
- `solver-ui.db`;
- `runs/`, `configs/`, `cache/`, `tools/`, `secrets/`, `logs/`.

**Worktree.** Se lavori in un worktree separato (sezione 11), `out/` è per-worktree. Per questo la config deve puntare `repo_root` al checkout principale, dove stanno bin set, bucket, risorse e run.

---

## 5. Modello dei job

### 5.1 Entità

| Entità | Cos'è | Campi chiave |
|---|---|---|
| **BinSet** | Cartella di eseguibili frozen, immutabile. Esempio: `out/monker/bin_correct/c123`, con 4 exe, `README.txt` e `SHA256SUMS.txt`. | id, path, file con sha256, testo del README (commit, build, test), capacità per tool, origine degli hash (`SHA256SUMS.txt` oppure "calcolati alla registrazione e confermati"), `verified_at`, `mock` |
| **Asset** | Input in sola lettura: resources, bucket set, texture map, game config, set di chart Monker, set di lock chart | id, kind, path, hash/fingerprint, metadati (bucket: `capacity` per street da `monker_buckets_report.json`; texture: `name`, `river-key`, numero di classi turn; set Monker: ha colonne `*_EV`, fine riga) |
| **Run** | Unità di lunga durata: run dir + spec + piano. Può attraversare più processi, giorni e finestre | id **opaco generato dal server**, kind, label, descrizione, tag, spec validata, run_dir, tool risolti (bin set + file + sha256 per tool), input congelati, stato, priorità, timestamp |
| **Step** | Un'esecuzione di processo: segmento trainer, valutazione, tree check, validazione, solve step 1 | run_id, tipo, argv, cwd, file stdout/stderr, pid, create_time, exe_path, start/end, exit code, riga di verdetto, intervallo di iterazioni, `stop_requested` |
| **Snapshot** | `charts/it_<N>` di una run | iteration, charts_dir, policy_fingerprint, has_policy, eval_dir, has_values, has_vs_monker, has_vs_previous, secondi di training cumulativi, `superseded` |
| **Evaluation** | Il `values.json` di uno snapshot o della policy finale | mode (exact / partial / sampled), esito (vedi A.5), estimate, secondi |
| **QueueEntry** | La posizione di una run in coda | priority, paused; `not_before` e `after` sono "should" (M3) |
| **HostSample** | Serie temporale di CPU, RAM, disco e thread in uso | |
| **AuditEntry** | Registro di ogni azione che modifica qualcosa | |

**Registrazione dei bin set**
- `SHA256SUMS.txt` ha righe `<hex> *<file>`; l'asterisco indica la modalità binaria.
- Per i set senza `SHA256SUMS.txt` vale la decisione D3:
  - con (a) non si registrano;
  - con (b) il backend calcola gli hash, li mostra all'utente e registra la sua conferma e il commit, se noto.
- Nessun eseguibile ha `--help` o `--version` [V]. Il catalogo delle opzioni sta quindi nel codice della UI (Appendice A), e ogni bin set dichiara le capacità di ogni tool. Esempi:
  - `c123` non ha `--dump-nodes`;
  - `bin_correct/base` ignora `postflop_betting_streets` [V README della correctness].
- Le capacità si registrano a mano nella config, oppure si scoprono con probe innocui in finestra (domanda Q12).

### 5.2 Kind di run

| Kind | Eseguibili | Giocatori | Checkpoint / resume | Stop graceful | Output principali | Milestone |
|---|---|---|---|---|---|---|
| `step2` | `train`, poi `monker_values` e confronto con Monker per ogni snapshot valutato | 2 | sì | `--stop-file` | `train.jsonl`, `state.ckpt`, `charts/it_N/`, `evals/it_N/` | M2 |
| `step1_hu` | `checkdown` | 2 | no | no (è breve: si fa kill) | `charts/`, `summary.json`, log di testo | M2 (richiede D3) |
| `step1_classes` | `checkdown_classes` | 2-3 | no | no | `charts/`, `summary.json`, log di testo | M2 (richiede D3) |
| `evaluation` | `monker_values --all-flops` su uno snapshot o sulla policy finale | 2 | no (riparte da zero) | no | `values.json`, `values.log` | M2 |
| `chart_eval_step1` | `checkdown_classes --iterations 0 --lock-charts DIR --lock-nodes all` | 2-3 | no | no | `summary.json` con oggetto `lock` (i chart Monker valutati nel nostro step 1) | M3 (should) |

**Job brevi** (non sono run, ma passano comunque dallo scheduler):
- **`validate`**: game tool (`--config`, più `--checkdown` per lo step 1, più `--actions`).
- **`tree_check`**: `monker_tree --config --charts`.

Entrambi rispettano finestra, budget di thread e RAM, un timeout (`short_job_timeout_s`) e il kill dell'albero. Il game tool compila fino a `--max-nodes` nodi (default del tool 50 milioni); una config 6-way potrebbe usare diversi GB [I]. Per questo la validazione passa `--max-nodes = validate_max_nodes`. Fuori finestra questi job restano in coda, e la UI lo dice.

**Confronto dei chart** (`compare`): non è un processo solver.
- Il backend chiama `compare()` da una copia congelata di `compare_charts.py`, in-process o in un subprocess Python.
- Per i set Monker con colonne `*_EV` usa una copia ripulita (sezione 5.14).

**4-6 giocatori**: si possono validare e fare la preview con il game tool, ma non lanciare. La UI mostra "non addestrabile oggi".

**Bucket table** (`monker_buckets`): fuori dal prototipo.

**Il kind `step2` ha due strategie di segmentazione.**
- **`segmented`**, lo stile di `run_correctness.sh` (M2):
  - lancia un processo trainer per ogni target di una lista crescente (per esempio `250 500 1000 2000 ...`);
  - dopo ogni segmento può valutare lo snapshot.
  - Le fini di segmento materializzano i lazy discount. Due liste di target diverse danno quindi risultati diversi a livello di arrotondamento, mentre la stessa lista è riproducibile [V README della correctness].
- **`continuous`**, lo stile di `run_step2_continuous.sh` (M2, senza stop rule):
  - lancia un solo processo fino a `max`, con `--checkpoint-every` e `--chart-every STEP`;
  - dopo ogni snapshot fa il confronto contro Monker e contro lo snapshot precedente.
  - La *stop rule* (scrivere `STOP` quando la variazione media rispetto allo snapshot precedente scende sotto `THRESHOLD`) è "should" in M3.

**Politica di valutazione degli snapshot**
- Valori: `between_segments` (default nel `segmented`), `final_only`, `none`.
- In `continuous` si può valutare uno snapshot ogni K, con `--policy-snapshot-every`.
- `--policy-snapshots` (una `policy.bin` per snapshot: 0,46 GB su V1L, circa 1 GB su HU50 30x4) si passa **solo ai segmenti il cui snapshot va valutato**. Il flag non fa parte dell'identità del checkpoint.
- `concurrent`, cioè valutare mentre il training della stessa run prosegue, non è richiesto: raddoppia la RAM.

### 5.3 Macchina a stati della run

```
draft ─▶ queued ─▶ waiting(reason) ─▶ running(substep) ─▶ done
                     ▲   │               │   │
                     │   │               │   ├─▶ pausing ─▶ paused ──(resume/extend)──▶ queued
                     │   │               │   ├─▶ failed
                     │   └─▶ cancelled   │   └─▶ interrupted (processo sparito senza verdetto)
                     └───────────────────┘
                     (qualunque stato non attivo) ─▶ archived (la run dir è diventata una junction)
```

**Stati della run**
- `waiting.reason` vale uno tra `window`, `budget_threads`, `budget_ram`, `budget_disk`, `queue_paused` (in M3 anche `dependency` e `not_before`). La UI mostra la ragione in chiaro, per esempio: "in attesa: finestra chiusa, riapre alle 00:00".
- `blocked` è diverso da `waiting`: la stima supera un limite dell'host (per esempio `ram_per_run_gb`). Serve cambiare parametri o alzare il limite in config.
- `running.substep` vale uno tra `training`, `evaluating`, `comparing`, `solving`.
- `paused` si raggiunge in due casi: stop dell'utente o fine finestra. Una run in `paused` è sempre ripartibile dal checkpoint.
- `interrupted` si raggiunge quando il processo sparisce senza riga di verdetto: crash, riavvio della macchina, oppure kill esterno. Le guardie di altre sessioni uccidono per pattern: vedi la sezione 11. È ripartibile se esiste un checkpoint; il trainer lo valida da sé.
- `done`, `failed` e `cancelled` sono stati finali. Da `done` e `paused` si può fare **extend** (nuovo target), che riporta la run in `queued`.
- `archived`: sola lettura.
- **Badge "non bit-identica"**: si mostra su una run che ha avuto uno STOP, un kill o un crash a metà segmento. Il risultato è valido, ma non è riproducibile bit per bit rispetto alla stessa run senza interruzioni (sezione 5.8).

**Stati dello step**: `pending`, `starting`, `running`, `stopping`, `exited_ok`, `exited_fail`, `killed`, `lost`.

### 5.4 Coda

- L'ordine è per priorità (intero, più alto = prima) e FIFO a parità di priorità. L'utente può riordinare, mettere in pausa e riprendere l'intera coda.
- I passi interni di una run (segmento, valutazione, confronto) sono accodati dallo scheduler. Ereditano la priorità della run e non compaiono come voci separate nella coda utente.
- Una run cancellata viene rimossa in modo transazionale. Dopo un restart non deve mai ripartire: c'è un test di regressione dedicato (incidente dei 5 script orfani).
- "Should" (M3): `not_before` (data e ora) e `after` (la run parte solo dopo che le run indicate sono finite con successo).

### 5.5 Finestre orarie

**Definizione**
- Per host sono definite due finestre, in ora locale dell'host: `runs` (processi solver) e `tests` (test d'integrazione con il solver reale).
- Gli override per data stanno nel file di config. Un editor degli override in UI è "should" (M3).
- Con `enabled = false` (server dedicato) la finestra è sempre aperta.
- La UI mostra sempre l'ora e il fuso dell'host.
- **Ora legale**: in Italia finisce il 25/10/2026, quando la notte dura 25 ore. Lo scheduler calcola le finestre in ora locale con un fuso vero (per esempio `zoneinfo`), mai con offset fissi. I test con fake clock coprono quel giorno.

**Admission di uno step** (regola dell'utente: una run non parte se non può finire dentro la finestra)

| Tipo di step | Parte solo se |
|---|---|
| Segmento `segmented` | preparazione + training fino al target + scrittura finale ≤ tempo rimasto − `stop_margin`. Altrimenti la run aspetta la finestra successiva (`waiting(window)`). Nessuno STOP a metà segmento è pianificato. Lo STOP di fine finestra resta solo come rete di sicurezza, se l'ETA era sbagliata; in quel caso la run riceve il badge "non bit-identica". |
| Trainer `continuous` | tempo rimasto ≥ `min_useful_minutes` + `stop_margin` + preparazione + scrittura attese. Può essere fermato con STOP a fine finestra e ripreso alla successiva. Ogni stop materializza i lazy discount, quindi il risultato non è bit-identico a quello di una run senza interruzioni: va scritto nel README e mostrato in UI. |
| Step senza stop graceful (valutazione, step 1, validazione, tree check) | l'ETA completa sta nella finestra meno `stop_margin`. |

Quando l'ETA non è nota (primo segmento di un gioco nuovo) usa la misura di una run simile, oppure una stima prudente. Mostra all'utente la fonte della stima.

**Fine finestra**
1. A `fine − stop_margin` il backend scrive `STOP` nelle run gestite che hanno un trainer attivo. Il trainer finisce l'iterazione, salva checkpoint e policy, ed esce con `STOPPED`. La run passa a `paused(window)`.
2. A `fine − kill_margin` il backend uccide gli step gestiti ancora vivi (per esempio una valutazione) e verifica. Quello step è da rifare.
3. I processi non gestiti non si toccano mai.

**Riapertura**: le run in `paused(window)` ripartono in automatico con `--resume` (default da confermare: domanda Q11).

**Tempi da considerare nella stima dello stop**. La scrittura del checkpoint va da 24-30 s (HU50, 5,2 GB) a 121 s durante un trasferimento USB concorrente [V]. `stop_margin` deve coprire almeno il doppio del `write_seconds` osservato.

### 5.6 Budget e admission check

Prima di ogni step il backend verifica tre budget: thread, RAM e disco.

**Thread**
- Si sommano i `--threads` di tutti i processi gestiti, più una stima per i processi `gtosd_*` non gestiti. Il totale deve restare ≤ `threads_total`.
- Per i processi non gestiti, il valore si legge da `--threads` nella cmdline. Se manca, si usa il default del tool:

  | Tool | Thread di default |
  |---|---|
  | `train` | 1 in modalità manuale (con `--iterations`); min(8, thread hardware) in modalità automatica |
  | `monker_values` | 1 |
  | `checkdown_classes` | 1 |
  | `checkdown` | 1 (single-thread) |
  | `game`, `monker_tree` | 1 |
  | `monker_buckets` | min(8, thread hardware) |

- Se psutil solleva `AccessDenied`, conta un valore prudente (`threads_per_run_max`).
- L'oversubscription degrada tutto: con 7 run attive un'iterazione passa da 55-65 ms a 100-200 ms [V `out/monker/correctness/results_2026-09-30.md`].

**RAM stimata**, dalla fonte migliore disponibile:
1. **Resume**: lo `state_bytes` dell'evento `start` precedente della stessa run, più il margine misurato (`memory_breakdown.process.peak_private_commit_bytes − state_bytes` dei segmenti precedenti).
2. **Run nuova `step2`**: la formula del trainer [V `trainer.cpp:2093-2098`], riprodotta nel backend: tabelle regret + strategy (16 B per cella con storage double), più la compact policy (8 B per elemento di capacità), più 2 B per riga (`discount_iterations`), più i prefissi di discount (8 B ciascuno).
   - Celle e righe si ricavano dal game tool chiamato con le capacità reali per street (`--flop/--turn/--river`, uint32; Appendice H.2). Esempio: HU50 30x4 con map identità → 68760 / 1651320 / 412830.
   - Il `state_bytes` del game tool è **solo** 16 B × entries [V `compiled_game.hpp:194-196`], quindi sottostima. Su V1L: 0,919 GB dal game tool, 0,978 GB di `state_bytes` del trainer, 1,07 GB di picco. Con storage mixed o float32, invece, il game tool sovrastima.
   - Se il game tool non riporta tutto ciò che serve alla formula, dillo all'utente invece di inventare.
   - Calibra il rapporto `peak_private_commit_bytes / state_bytes` su almeno 3 run reali. Fino ad allora aggiungi 10 % + 0,15 GB.
3. **Evaluator**: 8 B × celle della policy + circa 0,12 GB (V1L misurato: 0,58 GB).
4. **Step 1 3-way**: circa 0,5 GB.

Lo step parte solo se valgono entrambe le condizioni:
- `stima ≤ ram_per_run_gb`. Altrimenti la run è `blocked`, non in attesa.
- `stima + ram_reserve_gb ≤ memoria disponibile (OS) − stime degli step appena avviati e non ancora allocati`.

**Disco**: lo spazio libero su `runs_root` deve essere almeno `2 × checkpoint + policy finale + policy degli snapshot previsti + disk_reserve_gb`. Il fattore 2 serve perché il `.tmp` convive con il vecchio file durante la scrittura. Le policy non si cancellano mai: la UI mostra quanto spazio occupano e l'admission rifiuta quando manca.

**Ragioni del rifiuto**: ogni rifiuto riporta numeri e fonte, per esempio: "stima 5,5 GB da evento start del 30/09; disponibili 4,1 GB − riserva 4 GB".

### 5.7 Congelamento (frozen) per run

| Cosa | Come |
|---|---|
| Eseguibili | Riferimento, per tool, a un BinSet registrato. **Prima di ogni spawn** si verifica lo SHA256; se non corrisponde, lo step si rifiuta. Un percorso sotto `out/build` si rifiuta sempre. I tool restano fissi per tutta la run; cambiarli richiede un'azione esplicita di "upgrade", con un avviso. |
| Config di gioco | Copia in `<run>/inputs/config.json`: il trainer riceve questa copia. Si salva anche l'hash dell'originale. |
| Texture map | Copia in `<run>/inputs/texture_map.txt`. |
| Lock chart | Copia in `<run>/inputs/lock_charts/`, ripulita dalle colonne `*_EV` se presenti (sezione 5.14). Il fingerprint del lock è calcolato sulle righe, non sul percorso [V `trainer.cpp:755-811`], quindi la copia non cambia l'identità. |
| Bucket set | Non si copiano (circa 42 MB ciascuno). Il loro fingerprint fa parte dell'identità del checkpoint, quindi il trainer lo controlla da sé al resume. Registra comunque il fingerprint dal report dei bucket e dall'evento `start`. |
| Resources | Non si copiano (circa 426 MB). **Non fanno parte dell'identità del checkpoint** [V `trainer.cpp:689-742`]: `rank_table_v1.bin` e `preflop_all_in_v1.bin` non vengono controllati al resume. Il backend calcola lo sha256 alla registrazione e ricontrolla dimensione e mtime a ogni resume. Se cambiano, blocca il resume e chiede all'utente. |
| Script Python di analisi | Copia di `compare_charts.py` e `monker_in_our_game.py` in `data_root/tools/<sha>/`, con sha256 e commit del checkout di origine. La run registra quale copia usa. I driver di oggi li chiamano dal tree live, e questo è un difetto da non replicare. |
| Chart Monker di riferimento | Solo percorso e hash del set (i file non vengono modificati). |

### 5.8 Segmenti, checkpoint, resume, extend

**Resume**: usa gli stessi argomenti, più `--resume` e un `--iterations` maggiore dell'iterazione corrente.

**Identità del checkpoint.** Il checkpoint porta un hash di: fingerprint dell'albero; fingerprint della config (compreso il suo `id`); fingerprint dei bucket e del catalogo; capacità; `batch`; `scheme`; `update`; parametri DCFR; seed; lazy o eager discount; modalità sampled; fingerprint delle board-class-rows (texture compresa); storage (se non double); lock preflop. Un resume con identità diversa fallisce con `checkpoint rejected: integrity_failure` [V `trainer.cpp:689-742`].

**Campi fissi per tutta la run**. La UI li blocca dopo il primo segmento:
- tutti i campi d'identità, marcati "identity" nell'Appendice A.2;
- `--partition-target`: non è nell'identità, ma cambia l'arrotondamento;
- `--checkpoint-every`: ogni salvataggio del checkpoint materializza i lazy discount pendenti [V `trainer.cpp:2268-2272`], come una fine segmento;
- `--lazy-discount-epoch`: decide dove i discount vengono ribasati [V `trainer.cpp:2377-2386`];
- in `segmented`, la lista dei target già eseguiti.

**Campi liberi**:
- `--threads`: risultati bit-identici con qualunque numero di thread [V `trainer.hpp:31-33`];
- la cadenza dei chart (gli snapshot sono in sola lettura [V `train.cpp:178-180, 869-876`]);
- `--progress-every`;
- `--policy-snapshots`.

**Interruzioni e bit-identità.** Uno STOP, compreso quello di fine finestra, salva un checkpoint e quindi materializza i lazy discount. Una run fermata e ripresa non è bit-identica alla stessa run senza interruzioni: va documentato e mostrato con il badge.

**Resume senza training.** Un resume con `--iterations` ≤ iterazione corrente non addestra nulla, ma riscrive checkpoint e policy finale. È il modo di esportare `policy.bin` da un checkpoint [V].

**Seed.** Un seed diverso richiede una run dir nuova.

**Contatori**
- `iteration` e `boards_processed` sono cumulativi.
- `training_seconds`, `seconds_per_iteration` e `boards_distinct` ripartono a ogni processo.
- L'asse "tempo di training cumulativo" lo ricostruisce il backend, sommando i segmenti.

**Snapshot "superseded" (protezione dei dati).** Prima di rinominare `it_<N>.tmp` in `it_<N>`, il trainer esegue `remove_all(chart_dir/it_N)` [V `train.cpp:915-919`], cancellando la cartella esistente e tutto quello che contiene.

Succede quando una run riparte da un checkpoint più vecchio del suo snapshot più recente. Esempio: una run `continuous` uccisa o andata in crash tra due salvataggi `--checkpoint-every`.

Regola, prima di **ogni** resume o riavvio di un trainer in una run dir che contiene già snapshot:
1. Sposta ogni `charts/it_N` con N maggiore dell'ultima iterazione di checkpoint certa in `<run>/superseded/<timestamp>/charts/it_N`. L'iterazione certa è l'`iteration` dell'ultimo evento `end`, oppure 0 se non c'è nessun `end` o nessun checkpoint. Sposta allo stesso modo le relative `evals/it_N`.
2. Quando arriva l'evento `start`, il suo `resumed_iteration` (R) dice da dove riparte davvero il trainer (0 senza `--resume`). Riporta al loro posto gli snapshot con N ≤ R, purché il trainer non li abbia già ricreati.
3. Non si cancella mai nulla, e non si usa mai il suffisso `.tmp`: il trainer lo tratta come una cartella propria.

Gli snapshot spostati restano visibili in UI come "superseded", legati alla traiettoria abbandonata.

### 5.9 Stop, cancel, kill

| Azione UI | Meccanismo | Effetto | Perdita |
|---|---|---|---|
| **Stop** (graceful) | Registra l'intento nel DB (`stop_requested`) e crea `<run>/STOP`. Il trainer controlla il file dopo ogni iterazione [V `train.cpp:954-960`]. | Evento `stop_file`, salvataggio di checkpoint e policy, uscita 0 con `PREFLOP_BLUEPRINT_TRAIN=STOPPED`. La run va in `paused`. | Nulla. Latenza: un'iterazione più la scrittura (da 27 s a 2 min). |
| **Cancel** | Toglie la run dalla coda. Se la run è attiva, niente nuovi step: scrive `CANCEL` e fa uno Stop sul trainer attivo. | La run va in `cancelled`, con il checkpoint conservato. | Nulla. |
| **Kill** (conferma esplicita; offerto solo se Stop non ha effetto entro un timeout, o per step senza stop graceful) | Terminazione dell'albero dello step: Job Object o process group finché il backend è vivo; dopo un restart, discendenti psutil di (pid, create_time, exe) verificati. **Mai per pattern.** | Poi **verifica**: rescan per (pid, create_time, exe), con retry. Esito mostrato in UI. | Tutto dall'ultimo checkpoint. Può restare un `state.ckpt.tmp`, che si ignora. |

**Dettagli [V]**
- **Uno STOP scritto durante l'avvio del trainer si perde.** Il trainer cancella un file di stop "stale" prima di caricare qualunque cosa [V `train.cpp:408-412`]. Per questo:
  - l'intento di stop vive nel DB;
  - quando compare l'evento `start` di uno step con `stop_requested`, il backend riscrive `STOP`;
  - un `STOP` rimasto da uno stop precedente è innocuo, perché il trainer lo rimuove all'avvio.
- Il trainer non ha gestori di segnale: un kill è sempre brutale.
- Il driver `run_correctness.sh` **non** passa `--stop-file`. Il backend invece lo passa sempre.

### 5.10 Mai cancellare dati

- Nel prototipo non esiste delete. "Nascondi" è solo un flag nel DB della UI.
- Nessuna cancellazione automatica: né `policy.bin` degli snapshot, né checkpoint, né cartelle. Lo spazio si controlla con la politica di valutazione (5.2) e con l'admission sul disco. La pulizia la fa l'utente a mano, fuori dalla UI.
- Le uniche operazioni distruttive ammesse sono la rimozione del proprio marker `NO_ARCHIVE` quando la run raggiunge uno stato finale, e le rinomine "superseded" della sezione 5.8.
- Mai `Remove-Item -Recurse` su percorsi che possono contenere junction: può seguirle dentro `F:`.
- Mai scrivere dentro run dir che non sono state create dalla UI (eccezione: decisione D6).

### 5.11 Restart del backend

**Spawn**
- Lo stdout e lo stderr di ogni step vanno **direttamente su file** nella run dir (`train.jsonl`, `train.stderr.log`, ...), **mai su pipe** verso il backend. Con una pipe, un restart del backend romperebbe l'output del processo.
- Il backend legge i file in tail, salvando l'offset.
- Nel DB si salvano, in modo atomico: pid, create_time, exe_path, argv, cwd e percorsi dei log.
- Il backend verifica entro pochi secondi che lo step sia vivo e che abbia scritto il primo output (per il trainer: l'evento `start`). Un lancio che fallisce in silenzio ha tenuto ferma la macchina 1,5 h il 25/09.

**All'avvio**, per ogni step in `starting`, `running` o `stopping`:
1. Se esiste un processo con lo stesso (pid, create_time, exe_path), il backend si **ri-aggancia**: riprende il tail dall'offset salvato e ricalcola lo stato.
2. Altrimenti legge i file:
   - con una riga di verdetto (`PREFLOP_BLUEPRINT_*=...`) chiude lo step con quell'esito;
   - senza verdetto marca lo step `lost` e la run `interrupted`, con resume offerto se c'è un checkpoint.
3. Per un processo ri-agganciato che non è figlio del backend, l'exit code non è leggibile: l'esito si deduce dalla riga di verdetto e dai file di stderr.

**Test esplicito**: se il backend gira dentro un Job Object con `KILL_ON_JOB_CLOSE` (per esempio lanciato da un terminale o da un tool di agent), i figli muoiono con lui, a meno che il job permetta `CREATE_BREAKAWAY_FROM_JOB`. Prova il caso e documenta come avviare il backend in modo persistente (domanda Q13).

### 5.12 Layout della run dir gestita dalla UI

```
<runs_root>/<run_id opaco>/
  meta.json            schema "gtosd.solver_ui.run.v1": label, descrizione, tag, kind, spec completa,
                       tool risolti + sha256, hash degli input, copie degli script usate, creato/aggiornato
  NO_ARCHIVE           presente finché la run non è in uno stato finale
  inputs/              config.json, texture_map.txt, lock_charts/ (copie congelate)
  run.log              log umano del backend (una riga per evento, con timestamp)
  train.jsonl          stdout del trainer, appeso segmento dopo segmento
  train.stderr.log
  state.ckpt           (mai letto dalla UI)
  policy.bin           (mai letto dalla UI)
  charts/it_<N>/       <POS>/*_strategy.txt, policy.bin? (scritti SOLO dal trainer)
  evals/it_<N>/        values.json, values.log, vs_monker.json/.txt, vs_previous.json/.txt
  superseded/<ts>/     snapshot e valutazioni di una traiettoria abbandonata (5.8)
  STOP | CANCEL        file di controllo
```

- **Il run id è opaco** (per esempio `r-20261001-7f3a`): non deve contenere nomi di config né testo scelto dall'utente. Label e descrizione stanno solo in `meta.json` e nel DB. Motivo: le guardie di altre sessioni uccidono i processi la cui command line contiene certi nomi di config (sezione 11).
- Le valutazioni stanno in `evals/`, fuori da `charts/`, perché il trainer sostituisce le cartelle `charts/it_N` senza chiedere.

**Curva di convergenza.** Il backend costruisce le righe della curva da `evals/it_N/values.json` e `vs_monker.json`, con gli stessi campi di `convergence.json` (Appendice E.3). **Non usare `convergence_curve.py`** nel percorso di esecuzione, per quattro trappole [V]:
- `--bin` ha come default il build tree `out/build/windows-release-suite/benchmarks` (`:123-125`);
- lo script fa `chdir` su `parents[2]` del proprio percorso (`:52`, `:134`). Per una copia congelata fuori dal repo quella cartella è arbitraria, quindi ogni argomento dovrebbe essere assoluto;
- se l'exe manca termina con `SystemExit` prima di guardare la cache (`:145-147`);
- una riga in cache richiede la stessa modalità e gli stessi flop, un chart set `monker` quando si passa `--monker`, e `self_checks.passed` (`:94-107`).

Aggiungi un **golden test**: le righe costruite dal backend a partire dai `values.json` della fixture V1L devono coincidere con le righe del `convergence.json` di V1L.

### 5.13 Riferimento Monker per una run

- Suggerimento automatico: `<giocatori: 2→HU, 3→3-way, ...>/<effective_stack_units/10000>a` sotto `Symmetrical Chart`, per esempio 2 giocatori e 50 a → `Symmetrical Chart/HU/50a`.
- **"Nessun riferimento" è un valore valido.** I giochi di correttezza (HU6, HU8, HU19) non hanno un set Monker corrispondente. Non indovinare mai: se il set suggerito non esiste, la run non ha riferimento.
- L'utente conferma il set. Il tree check (`monker_tree`) conferma che gli alberi coincidono.

### 5.14 Run esistenti (non gestite), set Monker con EV: discovery e adapter

**Quattro layout** (Appendice E). Un adapter li normalizza in un unico modello `Run`, con `managed = false`.

**Riconoscimento del kind**
- dalla prima riga di `run.log`:
  - `correctness start:` → `correctness`;
  - `step 2 continuous start:` → `step2_continuous`;
- dallo schema di `summary.json` → `step1_hu` oppure `step1_classes`;
- dal suffisso `_monker_eval` → valutazione dei chart Monker.

**Stato di una run non gestita**, dedotto da più fonti:
- l'ultima riga di `run.log`;
- la riga di verdetto in `train.jsonl`;
- un processo `gtosd_*` vivo la cui cmdline contiene la run dir;
- la presenza di `CANCEL` o `STOP`;
- cartelle `it_*.tmp`.

**Regole di lettura**
- **Mai** aprire `state.ckpt` (0,9-2 GB) o `policy.bin` (circa 1 GB), nemmeno per calcolarne l'hash. Usa solo dimensione e mtime.
- Gli snapshot sono solo le cartelle `it_<cifre>`: ignora `it_*.tmp` e tutto il resto.
- Tollera i file mancanti e i JSON vecchi senza le chiavi di range.
- Metti in cache il parsing (per mtime e size) sotto `data_root/cache`.
- Le sottocartelle di `out/monker/variants/`, `out/monker/step2` e `out/monker/smoke_*` sono **junction verso `F:`**, un disco USB lento [V]. Si leggono solo se D4 = sì, mai con scansioni ricorsive all'avvio. Se `F:` manca, la run appare "archiviata, non raggiungibile".
- Le run esterne attive cambiano mentre le leggi: tollera file a metà scrittura e JSON non ancora completi (riprova più tardi).

**Set Monker con colonne `*_EV`: nessuno strumento del repo li legge** [V].
- `compare_charts.read_chart` tratta `Call_EV` e le altre colonne EV come azioni (`compare_charts.py:43-51`). Il test d'appartenenza al range le somma (`:138-139`), e gli EV sono negativi. Ogni classe risulta allora fuori range, e `mean_distance` è `null`.
- Il `read_chart` C++ rifiuta i valori negativi con "invalid frequency" (`monker_chart_format.hpp:282-291`). Per questo `monker_values --charts`, `monker_tree`, il lock del trainer e `checkdown_classes --lock-charts` falliscono su questi set.
- Nessun file in `benchmarks/` o `tools/monker_compare/` gestisce `_EV`.
- **Regola**:
  - il parser della UI legge le colonne `*_EV` e le usa come EV nei tooltip;
  - per gli strumenti, il backend genera una **copia ripulita** (senza colonne `*_EV`, con `Total` conservato) sotto `data_root/cache/monker_noev/<hash del set>/`, e passa quella;
  - gli originali non si modificano mai.
- La regola "le metriche coincidono con `compare_charts.py`" vale solo su set senza EV, oppure sulla copia ripulita.

---

## 6. Sicurezza

### 6.1 Rete e sessione

**Bind**: di default su loopback. Le altre modalità sono in 4.2.

**Autenticazione: un solo token generato, niente password né account.**
- Al primo avvio il backend genera un token casuale a 256 bit in `data_root/secrets/token`. Il file sta fuori da git ed è leggibile solo dall'utente (ACL su Windows, 0600 su POSIX). Il token si stampa una volta sola in console.
- La pagina di login riceve il token e crea un cookie di sessione `HttpOnly`, `SameSite=Strict`, `Secure` quando il servizio è in HTTPS, con scadenza configurabile.
- C'è un endpoint di logout.

**Allowlist di `Host` e `Origin`**
- Attraverso un SSH tunnel l'header `Host` è `localhost:8765`, **porta compresa**.
- Con `tailscale serve`, `Host` e `Origin` sono il nome tailnet della macchina [I].
- Le allowlist devono contenere entrambe le forme configurate.
- Il dev server di Vite resta su localhost e non si usa mai per l'accesso remoto. La sua origin è ammessa solo in modalità dev.

**Protezioni delle richieste**
- **CSRF**: ogni richiesta che modifica qualcosa (POST, PUT, PATCH) richiede l'header `X-CSRF-Token`, legato alla sessione, e un `Origin` presente nell'allowlist. Le GET non modificano mai niente.
- **CORS**: disabilitato.
- **Header di sicurezza**: CSP `default-src 'self'` senza script inline, `frame-ancestors 'none'`, `Referrer-Policy: no-referrer`, `X-Content-Type-Options: nosniff`.
- **Login**: tentativi limitati nel tempo, confronto a tempo costante. Mai segreti nelle URL, nelle query string o nei log.

**Contenuti non fidati.** File chart, log, label e descrizioni si trattano come testo non fidato. Niente `dangerouslySetInnerHTML` e niente HTML costruito da stringhe.

**Audit log** di ogni azione che modifica qualcosa: chi (id di sessione), cosa, argomenti ed esito. La vista in UI è "should" (M3); il registro no, serve da subito.

### 6.2 Esecuzione di processi

- **Niente shell arbitraria.** Lo spawn usa sempre una lista argv, con `shell=False`.
- L'eseguibile deve avere un nome nella **whitelist** del catalogo (Appendice A) e stare dentro un BinSet registrato e verificato.
- Gli argomenti sono costruiti **solo** da builder tipizzati per kind, a partire da campi validati (tipo, range, enum, dipendenze tra opzioni).
- Non c'è un campo "argomenti extra". Il `TRAIN_ARGS` dei driver attuali non va replicato. Un'opzione nuova si aggiunge al catalogo nel codice.
- I numeri si validano nella UI:
  - il trainer e il game tool parsano gli interi con `std::stoull` in base 10, che accetta un `-` iniziale e va in wrap-around (`--threads -1` diventerebbe circa 4 miliardi) [I];
  - `monker_values` parsa gli interi in **base 0** (`010` = 8, `0x10` = 16) [V `monker_values.cpp:148-150`].
  - Il backend scrive sempre interi decimali senza zeri iniziali.
- Il JSON di config lo **scrive il backend**, a partire dai campi validati. Un JSON caricato dall'utente passa dallo stesso validatore, e le **chiavi sconosciute si rifiutano**: il solver le ignora in silenzio [V].
- **L'input dell'utente non diventa mai un nome di file.** Config `id`, label e tag non compaiono nei percorsi: file e cartelle hanno id generati dal server.

### 6.3 File system (sandbox dei percorsi)

**Input del client.** Il client non manda mai percorsi liberi. Manda un id di catalogo, oppure un percorso relativo a una root configurata.

**Risoluzione.**
- Il backend risolve il percorso (realpath) e verifica il prefisso contro le root consentite.
- Su Windows deve gestire maiuscole/minuscole, nomi 8.3 (`GORYNI~1`), junction, UNC e nomi di device.
- Rifiuta `..` e i percorsi assoluti fuori dalle root.

**Junction in lettura.** Il realpath di `out/monker/variants/<run>` punta a `F:\GTO-Solver-out\...`. Per questo le destinazioni degli archivi (`archive_read_roots`) sono root di **sola lettura** esplicite. Senza, il controllo del prefisso rifiuterebbe letture legittime.

**Scritture.**
- Solo sotto `data_root` e `runs_root`.
- **Nessun reparse point** (junction, symlink) in nessun componente del percorso: controlla ogni componente.

**Lettura e serving di file**
- Whitelist di estensioni: `.txt .json .jsonl .log .tsv .md`.
- Tetto di dimensione, con letture a range o in tail per i log.
- Negati sempre `state.ckpt*`, `*.bin` e `*.tmp`.
- Il backend apre i file delle run in sola lettura.

---

## 7. API (proposta, adattabile)

| Metodo | Percorso | Scopo | Milestone |
|---|---|---|---|
| POST | `/api/auth/login`, `/api/auth/logout` | Sessione | M0 |
| GET | `/api/session` | Token CSRF, modalità (mock/reale), versione, info host (ora e fuso) | M0 |
| GET | `/api/host` | CPU, RAM, dischi per root, stato finestra e countdown, budget usati, processi `gtosd_*` non gestiti | M0 (statico), M1 (live) |
| GET | `/api/catalog` | Bin set (verificati, capacità per tool), bucket set, texture map, config, set Monker (con flag EV), lock chart | M0 / M2 |
| GET | `/api/runs?kind&state&root` | Run normalizzate, gestite e scoperte | M0 |
| GET | `/api/runs/{id}` | Dettaglio: spec, step, stato, ultime metriche | M0 |
| GET | `/api/runs/{id}/events?after=<offset>` | Eventi di `train.jsonl` già parsati | M1 |
| GET | `/api/runs/{id}/logs/{name}?tail=N` | Tail di log in whitelist | M1 |
| GET | `/api/runs/{id}/snapshots` | Elenco degli snapshot (compresi i superseded, marcati) | M0 |
| GET | `/api/runs/{id}/snapshots/{it}/charts` | Chart parsati di tutti i nodi | M0 |
| GET | `/api/runs/{id}/snapshots/{it}/values` | Sottoinsieme di `values.json` (estimate, class_ev, chart_sets, preflop_response) | M0 |
| GET | `/api/runs/{id}/curve` | Righe di convergenza, con asse del tempo cumulativo | M0 |
| GET | `/api/compare?a=<ref>&b=<ref>` | Confronto snapshot contro set Monker (M0); snapshot contro snapshot e run contro run (M3) | M0 / M3 |
| GET | `/api/monker/sets`, `/api/monker/sets/{id}/charts` | Libreria Monker, indicizzata in modo lazy per cartella tavolo/stack | M0 |
| POST | `/api/configs/validate` | Validazione per campo (sincrona), poi un job breve col game tool (fingerprint dell'albero, nodi, entries, stime); il risultato arriva via SSE o `GET /api/short-jobs/{id}` | M2 |
| POST | `/api/configs` | Salva una config nuova in `data_root/configs` (versionata, mai sovrascritta) | M2 |
| POST | `/api/runs/preview` | Dry run: argv di ogni step, stime di RAM, disco e tempo, esito dell'admission | M2 |
| POST | `/api/runs` | Crea e accoda la run (dopo la conferma della preview) | M2 |
| POST | `/api/runs/{id}/stop`, `/cancel`, `/kill`, `/resume`, `/extend`, `/priority`, `/evaluate?it=N` | Controllo (kill con conferma) | M2 |
| POST | `/api/queue/pause`, `/api/queue/resume`; PUT `/api/queue/order` | Coda | M2 |
| GET | `/api/stream` (SSE) | Eventi `run.updated`, `run.progress`, `step.updated`, `host.metrics`, `queue.updated`, `log.append`, `audit` | M1 |
| GET/PUT | `/api/settings/windows` | Editor degli override per data | M3 (should) |

---

## 8. Schermate

Tutti i testi sono in italiano.
- Le unità sono in **ante** (`a`) e in % del pot iniziale, mai in "bb". GTO-Chart-Browser etichetta gli EV come "bb" (`spot-format.ts:44-50`), ma nello short deck sono ante.
- I numeri mostrano sempre la fonte (file e iterazione).
- Un badge "MOCK" è sempre visibile in modalità mock.

### 8.1 Launch (M2)

**1. Scelta del kind** (sezione 5.2). La UI mostra subito i vincoli:
- `step2` ed `evaluation` solo HU;
- `step1_classes` 2-3 giocatori;
- da 4 a 6 giocatori solo validazione e preview;
- i kind senza tool registrati (D3) sono disabilitati, con il motivo.

**2. Gioco**
- Si parte da una config esistente, dal catalogo, oppure da un form nuovo.
- **Il form**:
  - replica la validazione di `game_config.cpp` campo per campo e mostra l'errore sul campo (tabella completa nell'Appendice C);
  - riceve gli importi in ante decimali e li converte in units intere (× 10.000, rifiutando i non interi);
  - riceve le percentuali in % e le converte in basis points;
  - mostra il pot iniziale calcolato;
  - ha un selettore di modalità: "target" (`open_target_units`) oppure "pot" (`preflop_open_sizes_basis_points`), mutuamente esclusivi.
- **Dopo la validazione locale**: il bottone "Valida con il solver" accoda un job breve col game tool (`--config`, `--actions`, `--max-nodes`; più `--checkdown` per lo step 1). Mostra `tree_fingerprint`, nodi, decisioni, profondità e l'elenco delle decisioni preflop. Fuori finestra il job resta in coda e la UI lo dice.
- **Tree check contro Monker** (quando `monker_tree` è registrato):
  - il set si sceglie come in 5.13, usando la copia ripulita se il set ha EV;
  - si esegue `monker_tree --config --charts`;
  - con PASS i nodi coincidono; altrimenti la UI elenca i file mancanti e quelli in più.

**3. Astrazione** (solo `step2`)
- **Bucket set**: la UI mostra livelli, tier e capacità per street da `monker_buckets_report.json`.
- **Texture map**: la UI mostra nome, `river-key` e numero di classi turn.
- **Lock** preflop: set di chart più nodi, oppure `all`.
- **Opzioni avanzate** (storage, seed, batch, partition, checkpoint-every, lazy-discount-epoch), con l'etichetta "identity" o "fisso per la run" dove serve.

**4. Iterazioni e snapshot**
- Segmentazione: `segmented`, con la lista dei target e un preset "raddoppio 250..N", oppure `continuous`, con `max`, `STEP` e `checkpoint-every`.
- Poi: politica di valutazione (quali snapshot ricevono `--policy-snapshots`), thread (con il budget visibile), cadenza del progresso.

**5. Metadati**: label, descrizione, tag, "variante di" (un'altra run). Sostituiscono le etichette cablate nei viewer attuali.

**6. Coda**: priorità. `not_before` e dipendenze arrivano in M3.

**7. Preview e conferma**
- La UI mostra:
  - l'argv esatto di ogni step previsto;
  - i tool usati, con bin set e sha256;
  - le stime di RAM, disco (checkpoint, policy, snapshot con policy) e tempo per segmento e totale, con il calendario delle finestre;
  - l'esito dell'admission, con i numeri;
  - le note di riproducibilità (per esempio: "`continuous` con stop di fine finestra: non bit-identica").
- La UI **non avvia nulla** senza questa conferma.

**Stima delle capacità di un run step 2** (Appendice H.2), da verificare contro almeno 3 eventi `start` reali prima di fidarsi:

| Street | Capacità |
|---|---|
| flop | 573 × capacità flop del bucket set |
| turn | classi turn della texture map × capacità turn |
| river | (classi turn se `river-key turn`, oppure 19.998 se `river-board`) × capacità river |

### 8.2 Monitor (M1 in sola lettura; azioni in M2)

**Pannello host**
- CPU % totale e per processo; RAM usata e disponibile; spazio libero per root (`C:`, `F:` se presente).
- Thread in uso rispetto al budget.
- Stato della finestra, con ora e fuso dell'host e con il countdown a stop e kill.
- Processi `gtosd_*` non gestiti: PID, eseguibile, run dir dedotta, thread (stimati come in 5.6), RAM.

**Coda** (M2): le run in `queued`, `waiting` e `blocked`, con la ragione. Si possono riordinare, mettere in pausa e cancellare.

**Elenco delle run**: filtri per stato, kind, gioco e tag. Le run non gestite hanno un badge "non gestita, sola lettura". Le metriche in evidenza nell'elenco si decidono con la domanda Q17.

**Dettaglio della run**
- **Riepilogo**: spec, argv degli step, tool con commit e SHA, input congelati, piano dei segmenti con lo stato di ognuno.
- **Progresso**: iterazione / target, iterazioni al secondo su una finestra mobile, **ETA** con il dettaglio (training, scrittura, preparazione, valutazioni, pause di finestra) e l'ora di fine prevista, finestre comprese.
- **Risorse**: CPU e RAM nel tempo, sia dall'OS sia da `process_bytes` e dai picchi del trainer; `state_bytes`; `memory_breakdown`.
- **Grafici**:
  - iterazione nel tempo e secondi per iterazione in funzione dell'iterazione;
  - **exploitability** in funzione dell'iterazione e del tempo di training cumulativo: NashConv in a e in % del pot su scala log; `gain`, `gain_lower` e `gain_preflop` per giocatore; linea del target (1 % del pot);
  - distanza e differenza di range contro Monker per snapshot (se c'è un riferimento);
  - variazione rispetto allo snapshot precedente.
- **Log**: tail live di `run.log`, `train.stderr.log`, `values.log` e degli eventi parsati, con un filtro per tipo d'evento.
- **Azioni** (M2): Stop, Extend (nuovo target), Resume, Cancel, Kill (con conferma), "Valuta ora questo snapshot" (solo se lo snapshot ha `policy.bin`).

**ETA** (formule nell'Appendice D.3)
- Il rate si calcola su una **finestra mobile** degli ultimi eventi `training_progress`, perché la velocità cambia con il carico.
- Per l'evaluator si usa il suo `eta_seconds`.
- Con la stop rule (M3) la UI mostra "al più entro ..." e il trend della variazione verso la `THRESHOLD`.

### 8.3 Results (M0; confronti A/B in M3)

**Navigatore dei nodi**
- È un breadcrumb dei passi (giocatore e azione), come `ActionPathBuilder` di GTO-Chart-Browser.
- Si costruisce dai nomi dei file (Appendice F.2), **ricostruendo i fold impliciti**. Esempio 3-way: `BTN/CO_Call_BTN_strategy.txt` vuol dire UTG fold, CO call (limp), BTN agisce. I fold impliciti si mostrano tratteggiati.
- Deve reggere set grandi: il 6-way 50a di Monker ha 5.600 file. Serve un indice lazy per cartella, messo in cache.
- I rami con frequenza 0 non si potano in silenzio: si mostrano come "mai giocato".

**Tre griglie 9x9 affiancate**: Nostra, Monker e Differenza.
- **Disposizione**: righe e colonne A..6; coppie sulla diagonale; suited in alto a destra; offsuit in basso a sinistra.
- **Cella**: barre impilate proporzionali alle frequenze delle azioni.
  - Per il disegno, la riga si normalizza per il suo totale.
  - L'opacità vale `0,15 + 0,85 × reach`.
  - Le celle fuori range (totale < 0,5) sono tratteggiate.
  - La griglia Differenza mostra la distanza TV (da 0 a 1) come colore di calore, calcolata come in F.4, **senza** normalizzare le righe.
- **Colori** (da GTO-Chart-Browser): Fold `#607d8b`, Call `#2e7d32`, Check `#00838f`, AllIn `#c62828`; raise in ordine crescente di size `#ef6c00`, `#7b1fa2`, `#1565c0`.
- **Etichette**: `Call` in un nodo non rilanciato si mostra come "Limp"; `5.0ante` si mostra come "Raise 5a".
- **Tooltip**: frequenze ed **EV per azione**.
  - Per le nostre run l'EV viene da `values.json` → `heroes[].nodes[].class_ev`. Esiste solo per gli snapshot valutati. È in ante, al netto dell'ante messo: Fold alla root vale −1,0.
  - Per Monker l'EV viene dalle colonne `*_EV`, presenti in **tutti** i file dei set HU/40a, 3-way/40a, 3-way/60a, 4-way/40a e 5-way/40a.
  - Dove l'EV non esiste si mostra "n/d", mai 0.

**Pannello del nodo**
- Frequenze aggregate, pesate per combo e reach, anche per coppie, suited e offsuit.
- Numero di mani in range e combo effettive, nostre e di Monker.
- Distanza media e differenza di range.
- Classi con la distanza più grande.

**Dettaglio mano**: le due strategie, la distanza e le combo effettive.

**Tabella dei nodi**, ordinabile per ordine dell'albero, distanza, gain locale (step 1) e combo effettive.

**Metriche della run** (devono coincidere con `compare_charts.py`, Appendice F.4): `overall_mean_distance`, `overall_range_difference`, `same_main_action_share`.

**Exploitability**
- **Step 2**:
  - `estimate` di `values.json`: EV, `gain`, `gain_lower` e `gain_preflop` per giocatore, `max_gain`, NashConv in a e in % del pot, e le chiavi di rake quando presenti;
  - `chart_sets`: i chart Monker giocati nel nostro gioco, con la perdita per giocatore in a e in % e `within_target`;
  - `preflop_response`.
  - Nelle run con lock va evidenziato `gain_lower`: il gain pieno include la deviazione preflop vietata (circa 1,0 a in V1L) [V].
  - Con il rake il gioco non è a somma zero: mostra `ev_sum_antes` ed `expected_rake_antes`.
- **Step 1**: gain per seat in a e in % del pot, EV, rake atteso, traiettoria, gain locale per nodo. Nessun NashConv con 3 giocatori.
- **Valutazione dei chart Monker nel nostro step 1** (`*_monker_eval`, già esistenti): la stessa tabella, per "nostra" contro "Monker bloccato".

**Confronti**
- M0: snapshot contro il set Monker di riferimento.
- M3: snapshot contro snapshot (A/B, oppure contro il precedente) e run contro run.
- I chart si abbinano per percorso relativo, e la UI elenca `only_ours` e `only_theirs`.

**"Should" (M3)**
- curve di convergenza per nodo;
- preferenza suited alla root (formula in F.4);
- viste di `exploit.json` e dei lock test (`lock_check.json`);
- copia del range nel formato `[50.0]AKs-AQs[/50.0]`;
- download dei file di una cartella chart come zip.

---

## 9. Mock mode

**Obiettivo.** Sviluppare e testare tutta la UI senza far girare il solver. Fuori dalle finestre macchina è l'unico modo ammesso per esercitare il supervisor (vedi anche D5).

**Isolamento.** Il mock usa i propri `data_root`, `runs_root` e DB (sezione 4.4). Non scrive mai altrove.

**Cosa serve a ogni milestone**
- M0: solo fixture.
- M1: fixture + lo strumento di replay.
- M2: gli eseguibili mock completi.

**1. Mock degli eseguibili (M2)**
- È uno script (per esempio `mock/mock_gtosd.py <tool> <args...>`) che implementa le CLI di `train`, `monker_values`, `game`, `checkdown`, `checkdown_classes` e `monker_tree`.
- Un **mock bin set** (`mock: true`) associa a ogni tool il prefisso argv `[python, mock_gtosd.py, <tool>]`. Supervisor e builder restano identici; cambia solo il prefisso. Non usare shim `.cmd`: l'escaping degli argomenti dei batch è insicuro.
- **Messaggi di errore**: il mock rifiuta gli argomenti con **gli stessi messaggi `FAIL`** del solver reale. **Estraili dal sorgente** (`benchmarks/preflop_blueprint_train.cpp:45-58, 220-441` e i punti equivalenti degli altri tool), non dalle tabelle di questo documento. Aggiungi un test che confronta la lista del mock con i `throw std::runtime_error(...)` del sorgente.
- **Comportamento del trainer mock**:
  - scrive `train.jsonl` con gli stessi campi e con la **stessa incoerenza di spaziatura** (`"event": "start"` accanto a `"event":"charts"`; `memory_breakdown` con chiavi ordinate ed `event` non per primo);
  - cancella il file di stop stale all'avvio, prima di "caricare", come il reale;
  - onora `--stop-file` a ogni iterazione finta;
  - scrive un checkpoint finto (header `MOCKCKPT` + iterazione), via `.tmp` + rename, e lo rilegge con `--resume`;
  - scrive gli snapshot via `it_<N>.tmp` + rename, **con lo stesso `remove_all` della cartella esistente**, a partire da chart di fixture perturbati che convergono;
  - termina con la riga di verdetto;
  - ha una velocità configurabile (`speed`).
- **Evaluator mock**:
  - scrive progress JSON su stderr, con `eta_seconds`;
  - scrive un `values.json` di fixture scalato con l'iterazione, con exploitability circa c/√it;
  - riproduce anche il caso reale "`=FAIL self-checks N` **su stdout**, exit 1, `values.json` scritto lo stesso".
- **Iniezione di guasti** (via variabile d'ambiente o id di config speciale):
  - `FAIL` all'avvio;
  - crash a metà senza verdetto;
  - scrittura del checkpoint lenta;
  - avvio lento (per il test dello STOP perso);
  - `policy_snapshot_skipped` per disco;
  - `charts_failed`;
  - riga non JSON a metà file;
  - processo figlio e nipote, per testare il kill dell'albero;
  - rifiuto di un resume con `integrity_failure`.

**2. Replay (M1)**: `mock_gtosd.py replay --source <train.jsonl reale> --speed 50` riproduce eventi reali, riscalando i tempi.

**3. Fixture** in `<app>/fixtures/`
- Piccoli file reali copiati **solo da cartelle su `C:`**. Mai checkpoint o policy.
- **Copiali solo da snapshot `it_N` completi**, quando la run è ferma, e verifica che ogni JSON si parsi. `V1L` e `V2L` vengono estese dal 01/10 alle 00:00 (con 2 thread ciascuna, fino alle 19:40/19:58): mentre lavori, `convergence.*` e `charts/` di quelle run cambiano, e un `convergence.json` può essere a metà scrittura.
- Cosa copiare:
  - `out/monker/correctness/V1L/`: `run.log`, `train.jsonl`, `convergence.txt/.json`, `curve.log`, `charts/it_250/`, `charts/it_500/` e `charts/it_64000/` (chart, `values.json` di 275 KB, `values.log`);
  - `out/monker/step1_3way/3WAY50_rake_dead/`: `summary.json`, `charts/` (54 file), `vs_monker.json`, `tree_check.txt`, il `.log` e `_monker_eval/summary.json`;
  - `out/monker/step1/HU50/`: `summary.json`, `charts/`, `compare.json` (formato vecchio).
- Una run step 2 continuous sta su `F:`. Copiane i file di testo solo se D4 = sì; altrimenti generali con il mock.
- **Chart Monker**: non committarli senza chiedere (domanda Q9). Sono output del software dell'utente, gitignored nell'altro repo. Per i test usa chart sintetici, compresi casi con colonne `*_EV`, fine riga CRLF e LF, e size come `5.98ante`. Per lo sviluppo usa il percorso locale della libreria.

---

## 10. Milestone, criteri di accettazione e test

**Regole generali**
- Ogni milestone è completa solo con i test verdi e una demo.
- I test con il solver reale si fanno **solo in finestra** e **con l'OK dell'utente**.
- L'ordine M0 → M3 è un cambio rispetto alla richiesta (sezione 1, D7).

### M0: VIEW RESULTS completo, in sola lettura (run esistenti e libreria Monker)

**Mock necessario**: solo fixture.

**Scope**
- Scheletro del backend, config, auth a token, CSRF, allowlist `Host`/`Origin`, sandbox dei percorsi.
- Adapter dei 4 layout.
- Parser di chart, `train.jsonl`, `run.log`, `values.json`, `convergence.json`, `summary.json`, `vs_monker.json` e del `compare.json` vecchio.
- Indice lazy della libreria Monker e copie ripulite dei set con EV.
- Confronto con la copia congelata di `compare()`.
- UI: elenco run, dettaglio statico, snapshot, navigatore, tre griglie 9x9, pannello del nodo, dettaglio mano con EV, tabella dei nodi, metriche della run, pannello exploitability (step 2 e step 1), curva da `convergence.json`, pannello host statico.

**Accettazione**
1. **Fixture V1L**:
   - tutti gli snapshot sono visibili;
   - la curva coincide con `convergence.txt` riga per riga;
   - i 4 chart sono navigabili;
   - il pannello exploitability di `it_64000` riporta l'`estimate` di `values.json`, con `gain_lower` in evidenza (run con lock);
   - la run mostra "nessun riferimento Monker".
2. **Fixture `3WAY50_rake_dead`**:
   - 54 nodi, con i fold impliciti corretti;
   - gain per seat da `summary.json`;
   - `overall_mean_distance` e `overall_range_difference` contro Monker 3-way/50a uguali a `vs_monker.json` entro **5e-5**. `compare_charts.py` arrotonda con `round(x, 4)` (`:110-111, 166-167, 174-175, 191`). Chiamando la copia congelata di `compare()` i valori sono identici.
3. **Libreria Monker**:
   - HU/40a mostra gli EV delle colonne `*_EV` nei tooltip;
   - un confronto contro un set con EV (tramite la copia ripulita) dà un `mean_distance` non nullo;
   - il 6-way 50a (5.600 file) si apre con indice lazy: prima apertura < 10 s, poi in cache.
4. **Nessuna scrittura** fuori da `data_root`: il test verifica hash e mtime su **copie statiche** delle fixture. **Nessuna apertura** di `state.ckpt` o `policy.bin`: il test usa un file sentinella che fallisce se viene aperto.
5. **Auth e rete**:
   - senza sessione la risposta è 401;
   - una richiesta mutante senza CSRF o con un `Origin` estraneo è 403;
   - un `Host` non in allowlist è 400;
   - il bind è su 127.0.0.1.
6. **Se D4 = sì**: una run HU50 step 2 su `F:` (per esempio `HU50_m30x4_rake25`) è visibile in sola lettura. Il suo ultimo snapshot contro Monker HU/50a dà le metriche del suo `vs_monker.json`. Nessuna scansione ricorsiva all'avvio.

**Test**
- Unit test dei parser: CRLF e LF mescolati, `Total` assente, colonne `*_EV`, righe a zero, size come `5.98ante`, abbinamento delle colonne per token e non per posizione.
- Unit test del parser dei nomi (HU, 3-way, 6-way con `UTG+1`).
- **Golden test** delle metriche contro `compare_charts.py` su almeno 3 coppie reali, di cui una con un set EV (ripulito).
- Test della sandbox: `..`, junction, 8.3, UNC, percorsi assoluti, lettura tramite `archive_read_roots`, rifiuto di scritture attraverso reparse point.
- Test dell'auth.
- E2E Playwright su fixture: la griglia ha 81 celle, con le etichette al posto giusto.

### M1: MONITOR delle run lanciate fuori dalla UI (sola lettura) e pannello host live

**Mock necessario**: fixture + replay.

**Scope**
- Scanner dei processi `gtosd_*` (pid, cmdline, create_time, CPU, RAM, thread stimati come in 5.6), con l'abbinamento alla run dir.
- Tail incrementale di `train.jsonl`, `run.log`, `values.log` e dei log dello step 1.
- Progresso, rate, ETA.
- SSE; serie temporali dell'host; stato della finestra, con ora e fuso; badge "non gestita".

**Accettazione**
1. Replay mock di `V1L/train.jsonl` a 50x: il progresso si aggiorna in ≤ 2 s. ETA e rate coincidono con le formule dell'Appendice D.3, con unit test sugli eventi registrati.
2. Righe parziali, righe non JSON, `nan`/`inf` e segmenti appesi (`resumed_iteration`) sono gestiti correttamente.
3. Dopo un restart del backend la vista riprende dall'offset salvato.
4. Su copie statiche delle fixture i file restano invariati (hash). Il backend apre i file delle run solo in lettura: c'è un test che intercetta le aperture.
5. Un processo osservato che sparisce senza verdetto (kill esterno) fa apparire la run come "sparita senza verdetto".
6. **Solo in finestra e con OK** (domanda Q10): osserva in sola lettura le estensioni di V1L/V2L del 01/10. Iterazione e RAM devono coincidere con Task Manager entro il 5 %.

### M2: LAUNCH, coda, stop, resume ed extend

**Mock necessario**: eseguibili mock completi.

**Scope**
- Catalogo con verifica SHA256 e risoluzione dei tool secondo D3.
- Form della config con validatore e job breve di validazione.
- Preview dell'argv, stime, admission.
- Job store e scheduler: priorità, finestre con override da config, budget, regole di 5.5.
- Supervisor: spawn diretto, log su file, stop/cancel/kill, ri-aggancio.
- Kind `step2` (`segmented` e `continuous` senza stop rule) ed `evaluation`. `step1_hu`, `step1_classes` e `tree_check` solo se i tool sono registrati (D3); altrimenti restano in mock.
- Input e script congelati per run; snapshot superseded; `NO_ARCHIVE`; audit log; pausa di fine finestra con auto-resume.

**Accettazione, in mock**
1. Ciclo completo `queued → running → evaluating → done`.
2. Stop → `STOPPED` con checkpoint. Extend → l'iterazione riprende dal valore giusto.
3. **Uno STOP premuto durante l'avvio** (mock con avvio lento) non si perde: il backend lo riscrive dopo l'evento `start`.
4. Kill → figlio e nipote morti, verificato con uno scan. Nessun kill per pattern: un test statico lo controlla.
5. **Una run cancellata in coda non parte mai, nemmeno dopo un restart.**
6. Backend ucciso a metà run → al riavvio la run è ri-agganciata, oppure marcata `interrupted` con resume offerto.
7. Test del backend avviato dentro un job con `KILL_ON_JOB_CLOSE`: comportamento documentato (sezione 5.11).
8. Con fake clock:
   - `STOP` a `fine − stop_margin`, kill a `fine − kill_margin`, auto-resume alla riapertura;
   - un segmento `segmented` che non ci sta non parte;
   - l'override per data è rispettato;
   - il 25/10/2026 (fine dell'ora legale) funziona.
9. I budget bloccano con la ragione giusta, contando i processi non gestiti con i thread di default di 5.6.
10. Resume da un checkpoint più vecchio dell'ultimo snapshot → gli `it_N` successivi finiscono in `superseded/`, nulla viene cancellato, e le valutazioni restano leggibili.
11. `monker_values` mock con `=FAIL` su stdout ed exit 1 → lo step è fallito anche se `values.json` esiste.
12. Nessuna chiamata con shell. Un test statico cerca `shell=True` e l'uso di `os.system`.
13. **Golden test dell'argv** con fixture di argv attesi. Le differenze rispetto ai driver sono documentate: `--stop-file`, copie in `inputs/`, percorsi assoluti, cadenza del progresso, `--policy-snapshots` solo sui segmenti valutati.
14. Il validatore della config accetta tutte le config del repo (`benchmarks/monker/*.json`, `benchmarks/monker/correctness/*.json`) e rifiuta ogni mutazione non valida della tabella dell'Appendice C.

**Accettazione con il solver reale** (in finestra, con OK)
1. `step1_hu` su HU50 con 5.000 iterazioni: circa 75 s. Richiede `checkdown` registrato (D3).
2. Controllo incrociato del validatore: accetta e rifiuta esattamente gli stessi casi del game tool reale, su un campione di mutazioni.
3. **Riproduzione di V1L**, con bin `c123`.
   - Parametri, uguali a quelli di V1L:
     - config `benchmarks/monker/correctness/HU6_V1_flopturn.json`;
     - bucket `out/monker/correctness/buckets/v1_flopturn_exact`;
     - map `identity_texture_map.txt`;
     - lock `lock_limp_check` sui nodi `CO/CO_strategy.txt,BTN/CO_Call_BTN_strategy.txt`;
     - 2 thread, partition 4;
     - target `250 500`, entrambi valutati (quindi entrambi con `--policy-snapshots`).
   - **Precondizione, al primo evento `start`**:
     - `trainer_identity` = `fnv1a64:d8161b50af8c502b`;
     - fingerprint del lock = `ae10d154d2190a64`;
     - `tree_fingerprint` = `fnv1a64:da5c6942354ad5ad`.
     - L'identità dipende dal contenuto dei file, non dai percorsi [V `trainer.cpp:689-742, 755-811`], quindi le copie in `inputs/` non la cambiano.
     - Se un valore differisce, fai STOP e riporta la differenza: `benchmarks/monker/correctness/*` è in modifica da parte di altre sessioni.
   - Criteri:
     - il `policy_fingerprint` degli eventi `charts` a 250 e 500 è uguale a quello della fixture di `V1L/train.jsonl`;
     - i file chart sono identici byte per byte alle fixture `it_250` e `it_500`.
   - Costo: circa 4-5 min e 1,1 GB.
   - [I] Se non risultano identici, **non forzare nulla**: riporta la differenza all'utente.
4. Stop durante il segmento 500 → `STOPPED`, poi resume fino a 500 → iterazione finale 500. I fingerprint possono differire da quelli senza interruzione (5.8): documentalo e verifica che compaia il badge "non bit-identica".

### M3: completamenti e "should"

**Scope**
- Confronti A/B (snapshot contro snapshot, run contro run).
- Curve per nodo.
- Kind `chart_eval_step1`.
- Stop rule della modalità `continuous`.
- `not_before` e dipendenze `after`.
- Preferenza suited.
- Viste di `exploit.json` e dei lock test.
- Copia del range e download zip.
- Vista dell'audit log.
- Editor degli override delle finestre.
- Rifinitura delle prestazioni.

**Accettazione**
1. Tutti i numeri coincidono con gli strumenti esistenti: `compare_charts.py` (entro 5e-5, o identici con `compare()`), `values.json`, `convergence.txt` e `monker_in_our_game.py`, sui fixture e su V1L e 3WAY50.
2. La preferenza suited coincide con la formula dell'Appendice F.4.
3. Una griglia si rende in < 100 ms per nodo. Navigare nel 6-way 50a di Monker è fluido.
4. La stop rule scrive `STOP` quando `overall_mean_distance(vs_previous) < THRESHOLD`, con test in mock.

---

## 11. Regole di ingegneria per l'agent

Non vedi la memoria del progetto: le regole che ti servono sono tutte qui.

1. **Git**
   - Lavora in un **worktree separato**, con un branch nuovo, dalla base scelta in D2.
   - Nel checkout principale **non** fare mai `checkout`, `switch`, `stash`, `reset` o `clean`. Lì lavorano altre sessioni, e le run vive chiamano script Python dal suo tree.
2. **Commit**
   - Piccoli, con messaggi nello stile del repo (per esempio `ui(solver): ...`).
   - Per la riga di attribuzione in fondo ai commit segui le regole del tuo harness.
   - Aggiungi i file con percorsi espliciti, mai `git add -A`.
   - Se esiste `.git/index.lock`, aspetta e riprova.
   - **Mai push, mai PR** senza chiedere.
3. **Subagent**: se ne usi, falli girare sempre con il modello **opus**, mai con Fable 5.1 (regola dell'utente).
4. **Solo lettura**: non modificare codice o librerie del solver:
   - `libs/`, `include/`, `benchmarks/*.cpp/hpp`, `tests/`, `CMakeLists.txt`, `CMakePresets.json`, `apps/`;
   - gli script esistenti (`tools/monker_compare/*`, `benchmarks/monker/correctness/run_correctness.sh`, `tools/preflop_suite/*`);
   - `out/frozen/`, `out/monker/bin*`, i bucket, le resources, le config in `benchmarks/`;
   - GTO-Chart-Browser. Se ne copi componenti, indica l'origine in un commento.
   - Se il solver ha bisogno di una modifica (per esempio `--version`, un gestore di segnali, la telemetria RAM su Linux, colonne `*_EV` nei reader), scrivi una proposta e chiedi.
5. **Le run in corso non si toccano**
   - Mai scrivere in una run dir non creata dalla UI.
   - Mai uccidere processi che la UI o tu non avete avviato.
   - Mai modificare script in esecuzione.
   - **Guardie di altre sessioni**: uccidono per pattern di command line. Esempio: il 01/10 alle 19:58 `stop_ext_1958.ps1` uccide ogni processo la cui command line contiene (senza distinzione di maiuscole) `run_correctness_lock.sh`, `queue_lossless_ext.sh`, `HU6_V1_flopturn` o `HU6_V2_river`. I tuoi processi, run id e percorsi non devono contenere nomi di config. Il backend deve gestire i kill esterni (stato `interrupted`).
6. **Finestre macchina**
   - Qualunque eseguibile solver reale, anche il game tool, gira solo in finestra: run 00:00-20:00, test fino alle 21:00, salvo override comunicati dall'utente per quel giorno.
   - Prima di lanciarlo controlla l'ora e che la macchina non sia già piena (budget thread).
   - Build JS/Python e suite di test in mock seguono la decisione D5.
   - Scrivere codice è sempre ammesso.
7. **Build C++**: nessuna. Se serve un bin set nuovo (D3, oppure `--dump-nodes`), chiedi all'utente.
8. **Dati**
   - Mai cancellare, spostare o rinominare nulla fuori da `data_root` e `runs_root`.
   - Mai scrivere in `out/monker/variants`, `out/monker/step2`, `out/monker/smoke_*` o su `F:`.
   - Mai leggere `state.ckpt` o `policy.bin`.
   - Mai `Remove-Item -Recurse` su percorsi che possono contenere junction. Per rimuovere una junction: `cmd /c rmdir <junction>`, e comunque solo con OK.
9. **Processi**
   - Non lasciare processi orfani (dev server, mock, test). Alla fine verifica per PID che siano morti.
   - `TaskStop` o la chiusura di un terminale non uccidono i figli: usa kill per PID e verifica.
   - Da Git Bash, `tasklist` tronca i nomi immagine a 25 caratteri: cerca per prefisso o usa `Get-CimInstance Win32_Process`.
10. **Tool di shell** (se sei un agent Claude Code):
    - Il tool Bash rimuove un livello di backslash negli heredoc: scrivi gli script con lo strumento di scrittura file.
    - Da Git Bash, per argomenti con `/` diretti a exe Windows, usa `MSYS_NO_PATHCONV=1`.
    - Nel backend usa solo percorsi nativi.
11. **Dipendenze**: secondo D8. Pinnate, lockfile committati, nessuna telemetria, licenze permissive. Il frontend si compila in statico e non chiama nulla fuori dal backend.
12. **Decisioni**:
    - Tutto ciò che questo documento non copre si chiede all'utente.
    - Gli spostamenti di roadmap o di milestone si annunciano esplicitamente.
    - Ogni fatto [I] va verificato prima di costruirci sopra, e il risultato va annotato in `DECISIONS.md`.
13. **Documentazione**: `README.md` dell'app (setup, avvio, config, sicurezza, mock, riproducibilità) e `DECISIONS.md`, sempre aggiornati.

---

## 12. Trappole note (checklist)

Il dettaglio è nell'Appendice I.

**`train.jsonl`**
- Parsa ogni riga come JSON e salta quelle non JSON: la riga di verdetto finale è testo semplice.
- Mai grep di stringhe letterali: la spaziatura è incoerente.
- `evaluation`, `exact_certification`, `coverage` ed `end` non hanno flush esplicito e possono comparire in ritardo [I].
- Un `nan` o un `inf` renderebbe la riga JSON non valida [I]: gestiscilo.

**Contatori**: `training_seconds` riparte a ogni processo; anche il `train_s` di `convergence.txt` è per processo.

**Guardia sull'albero**
- Confronta il `tree_fingerprint` dell'evento `start` con quello del game tool sulla stessa config, in modalità full.
- Il confronto è autorevole solo se game tool e trainer vengono dalla **stessa build**. Con tool di build diverse (D3 opzione b) è solo un avviso.

**Solo HU**: `--chart-every`, lock del trainer, trainer stesso ed evaluator. Step 1 classes: 2-3 giocatori.

**Modalità del trainer**
- Sempre manuale, con `--iterations` ed `--eval-every 0`. La modalità automatica esegue un valutatore non validato con `--board-class-rows` [I].
- Con `--eval-every 0` non esiste `CONVERGED`: la UI non deve aspettarlo.

**Progress**: `--progress-every` di default è 0 (spento). Passalo sempre, altrimenti non arrivano eventi di progresso. Un valore basso (per esempio 100) dà ETA più fini e non è nell'identità.

**Stop file**
- Un file di stop stale viene cancellato all'avvio del trainer (con un avviso su stderr).
- Per lo stesso motivo, uno STOP scritto durante l'avvio si perde (5.9).

**Snapshot**
- Ignora `it_<N>.tmp`.
- **Un `it_<N>` esistente viene cancellato e sostituito dal trainer**, con tutto il suo contenuto. Vedi 5.8.

**`monker_values`**
- Successo = exit 0 **e** riga `PASS mode=` **e** `self_checks.passed == true`.
- I fallimenti di self-check e di scrittura stampano `=FAIL` su **stdout** e scrivono comunque `values.json`.
- Gli interi si parsano in base 0.

**Chart Monker**
- Fine riga **mescolate**: HU/50a, HU/100a, 3-way/50a e 6-way/50a usano CRLF; HU/40a, 3-way/40a, 3-way/60a e 4-way/40a usano LF. Gestisci entrambe.
- Colonne `*_EV` in tutti i file di alcuni set; nessuno strumento del repo le regge (5.14).
- Le size vanno lette come float.

**Metriche**: `compare_charts.py` **non** normalizza le righe per il totale nel calcolo della distanza (F.4) e arrotonda a 4 decimali nel JSON.

**Resources**: non sono nell'identità del checkpoint; controllale tu (5.7).

**Tempi**: in un fixture preso da una run condivisa, la velocità dipende dal carico (da 0,06 a 0,2 s per iterazione).

**Rake**: il gioco non è a somma zero; `--expected-rake` richiede una config con rake.

**Ora legale**: 25/10/2026.

---

## 13. Domande aperte

### 13.1 Bloccanti

Sono nella sezione 0.1 (D1-D8). Se qualcuna è senza risposta, chiedile tutte insieme prima di iniziare.

### 13.2 Da chiedere alla milestone indicata

| # | Quando | Domanda | Default |
|---|---|---|---|
| Q9 | M0 | Posso committare dei chart Monker come fixture di test? | No; uso chart sintetici |
| Q10 | M1 | Posso osservare in sola lettura le estensioni di V1L/V2L del 01/10 (o altre run vive) per il test di accettazione 6? | Sì, sola lettura |
| Q11 | M2 | A fine finestra: pausa graceful e ripresa automatica alla finestra successiva, per le run `continuous`? | Sì |
| Q12 | M2 | In quali finestre posso far girare i test col solver reale (riproduzione di V1L, step 1 HU50)? Posso sondare le capacità dei bin set con probe innocui? | Test fino alle 21:00, su tua conferma giornaliera |
| Q13 | M2 | Come deve partire il backend in modo persistente: a mano, all'accesso (Task Scheduler) o come servizio? | A mano con uno script, per il prototipo |
| Q14 | M2 | La valutazione di una run può girare in parallelo al training di un'altra, se il budget lo consente? | Sì, se passa l'admission |
| Q15 | Prima dell'accesso remoto | Come raggiungerai l'host: SSH tunnel, `tailscale serve`, WireGuard o solo LAN? | Bind su localhost; SSH tunnel oppure `tailscale serve` |
| Q16 | Prima della produzione | Budget sul server di produzione (thread per run e totali, RAM per run)? C'è una finestra macchina? | Da config; finestra `off` |
| Q17 | M0 | Quali metriche devono stare in evidenza nella lista delle run? | Distanza, differenza di range, NashConv in % del pot |
| Q18 | M2 | Esiste un ambiente Linux o WSL per provare il ramo POSIX del supervisor? | No: unit test con processi mockati |

### 13.3 Già deciso (non chiedere)

- Le impostazioni di MonkerSolver (bucket, size, rake) sono ignote e l'utente non le conosce: non chiederle mai. Le si deduce dai dati.
- L'esattezza è richiesta solo per il contratto HU postflop. Il multiway può usare sampling o astrazione.
- Fuori scope: archiviazione e gestione dello storage, multi-macchina, account, cancellazioni.
- L'OS della macchina di produzione non è deciso: progetta per entrambi, testa prima Windows.
- Autenticazione: un solo token generato. Niente password.
- La tecnologia la sceglie l'agent. Lo stack di GTO-Chart-Browser è quello preferito per coerenza, non un obbligo.

---

# Appendici: interfaccia del solver (riferimento)

## Appendice A: eseguibili e opzioni CLI

### A.1 Convenzioni comuni [V]

**Sintassi e output**
- Le opzioni sono coppie `--nome valore`, più qualche flag booleano. Non esistono `--help` né `--version`: `--help` fallisce con `missing value for --help` ed exit 1.
- **Successo**: l'ultima riga di stdout è `PREFLOP_BLUEPRINT_<TOOL>=PASS ...`, con exit 0.
- **Errore**: di norma una sola riga su stderr, `PREFLOP_BLUEPRINT_<TOOL>=FAIL <messaggio>`, con exit 1. **Eccezione**: `monker_values` (A.5).
- Stati di successo del trainer: `CONVERGED | PLATEAU | STOPPED | ITERATION_LIMIT`, tutti con exit 0. Con `--eval-every 0` solo `STOPPED` o `ITERATION_LIMIT`.
- I driver bash aggiungono l'exit 2 (uso sbagliato) e l'exit 3 (cancellato).

**Messaggi `FAIL`: estraili dal sorgente** (`benchmarks/preflop_blueprint_train.cpp:45-58, 220-441, 432-555`), non da questo elenco. Alcuni esempi reali del trainer:
- argomenti:
  - `missing value for X`, `unknown argument X`;
  - `invalid number: X`: un intero con caratteri in coda. Un valore del tutto non numerico dà invece il testo dell'eccezione della libreria standard di `std::stoull`, che dipende dal compilatore;
  - `invalid decimal: X`;
  - `unknown scheme X`, `unknown update mode X`, `unknown table storage X`;
- vincoli di range:
  - `--lazy-discount-epoch must be in 1..65535`;
  - `--target-pot-percent must be in (0, 100]`;
  - `--policy-snapshot-reserve-gb must be in [0, 1e9]`;
- dipendenze tra opzioni:
  - `--config, --resources-dir and --buckets-dir are required`;
  - `--board-texture-map requires --board-class-rows`;
  - `--resume requires an existing checkpoint`;
  - `--checkpoint-every requires --checkpoint`;
  - `--lock-charts and --lock-nodes go together`;
  - `--chart-every and --chart-dir go together`;
  - `--policy-snapshots requires --chart-every and --chart-dir`;
  - un messaggio su `--policy-snapshot-every`, che richiede `--policy-snapshots` e un multiplo di `--chart-every`;
  - `--board-class-rows requires --eval-every 0 and no certificate`;
- config e albero: `configuration rejected: <invalid_json|unsupported_schema|missing_field|invalid_value|invalid_structure>`, `compile failed`, `--chart-every writes heads-up charts only`;
- risorse e creazione: `resources or bucket tables missing`, `trainer creation failed: <...>`, `board texture map F rejected: <...>`;
- checkpoint: `checkpoint rejected: <integrity_failure|unsupported_version|io_failure>`;
- scrittura: `periodic checkpoint write failed`, `final checkpoint write failed: ...`, `policy write failed: ...`.

**Percorsi**: sono usati così come vengono passati, relativi alla cwd del processo. Il backend passa percorsi assoluti e imposta cwd = run dir.

### A.2 `gtosd_preflop_blueprint_train` (step 2, solo HU)

Fonte: `benchmarks/preflop_blueprint_train.cpp` (parse `:220-379`, controlli `:380-441`).
- **Modalità manuale**: si attiva con `--iterations N`. Addestra fino all'iterazione assoluta N.
- **Modalità automatica**: si attiva senza `--iterations`. **Non usarla.**

Colonna "Identity": **sì** = nell'hash del checkpoint [V `trainer.cpp:689-742`]; **fissa** = non nell'hash, ma va tenuta fissa per tutta la run (5.8); **no** = libera.

| Opzione | Default | Vincoli ed effetto | Identity |
|---|---|---|---|
| `--config PATH` | obbligatoria | JSON del gioco (Appendice C) | sì (fingerprint di config e albero) |
| `--resources-dir DIR` | obbligatoria | `rank_table_v1.bin`, `preflop_all_in_v1.bin` | **no**: la UI ne controlla hash, dimensione e mtime (5.7) |
| `--buckets-dir DIR` | obbligatoria | `flop\|turn\|river_buckets_v1.bin` | sì (fingerprint dei bucket e del catalogo) |
| `--iterations N` | 100 | target assoluto; attiva la modalità manuale | no |
| `--threads N` | **1** | 0 → `invalid_configuration`; risultati identici con qualunque numero di thread | no |
| `--batch N` | 32 | board per giocatore per iterazione (64 per iterazione con update alternating); > 0 | **sì** |
| `--partition-target N` | 0 = max(256, nodi/128) | cambia l'arrotondamento; i driver usano 4 (correctness) o 64 (HU50) | fissa |
| `--scheme dcfr\|linear` | `dcfr` | `linear` insieme a `--lazy-discount` viene rifiutato | **sì** |
| `--update alternating\|simultaneous` | `alternating` | | **sì** |
| `--table-storage double\|mixed\|float32` | `double` | | sì, se non double |
| `--seed N` | predefinito | un seed nuovo richiede una run dir nuova | **sì** |
| `--eval-seed N` | — | si applica dopo il caricamento; la UI non lo usa | no |
| `--eval-every N` | **10** | deve essere **0** con `--board-class-rows` | no |
| `--eval-flops N` | 8 | | no |
| `--target-pot-percent X` | 1 | (0, 100] | no |
| `--progress-every N` | **0 = spento** | passalo sempre | no |
| `--checkpoint PATH` | — | checkpoint a ogni `--checkpoint-every`, dopo ogni valutazione programmata, e **sempre alla fine** (anche con `STOPPED`) | — |
| `--checkpoint-every N` | 0 | richiede `--checkpoint`; ogni salvataggio materializza i lazy discount | fissa |
| `--resume` | flag | richiede che il file checkpoint esista | — |
| `--policy-out PATH` | — | policy media finale (8 B per cella) | no |
| `--current-policy-out PATH` | — | policy corrente (regret matching) | no |
| `--chart-every N` + `--chart-dir DIR` | — | vanno insieme; solo HU; scrive `DIR/it_<N>` via `.tmp` + rename, **cancellando un `it_<N>` esistente** | no |
| `--policy-snapshots` | flag | scrive anche `it_<N>/policy.bin`; richiede `--chart-every` e `--chart-dir` | no |
| `--policy-snapshot-every N` | — | richiede `--policy-snapshots`; multiplo di `--chart-every` | no |
| `--policy-snapshot-reserve-gb X` | 1 | lo snapshot si salta (evento `policy_snapshot_skipped`) se lo spazio libero è < policy + checkpoint + policy finale + X | no |
| `--stop-file PATH` | — | un file stale si cancella all'avvio; controllo dopo ogni iterazione | no |
| `--lock-charts DIR` + `--lock-nodes a,b\|all` | — | solo HU; le righe bloccate non imparano; niente colonne `*_EV` nei chart | **sì** |
| `--board-class-rows` | flag | righe stile Monker; richiede `--eval-every 0` | **sì** |
| `--board-texture-map FILE` | — | richiede `--board-class-rows`; la map identità equivale ad assenza di map | **sì** |
| `--lazy-discount` | off | serve per la velocità (da 0,75 a circa 0,07 s per iterazione su HU50); richiede DCFR | **sì** |
| `--lazy-discount-epoch N` | 65535 | 1..65535; decide dove i discount si ribasano | fissa |
| `--batch-policy-refresh` | flag | no-op di compatibilità, passato dai driver | no |
| `--prefetch-refresh N` / `--prefetch-update N` | 4 / 8 | | no |

- **Opzioni da non esporre**: `--class-rows`, `--history-rows`, `--certificate-out`, `--coverage-out`, `--eval-only`, `--fixed-boards`, `--permute-suits`, `--profile-traversal`.
- **DCFR**: alpha/beta/gamma sono fissi a 1.5/0/2 e non si cambiano da CLI.
- **Output**: stdout = JSON Lines (Appendice D) + la riga finale `PREFLOP_BLUEPRINT_TRAIN=<STATUS>`. Stderr contiene solo l'avviso sul file di stop stale e la riga `FAIL`.
- **`convergence_status` dell'evento `end`**: `CERTIFIED_EXACT | PLATEAU | CERTIFIED_FAIL | ESTIMATED | NOT_REACHED`.

### A.3 `gtosd_preflop_blueprint_checkdown` (step 1 HU, livello combo, esatto)

Esiste solo in `out/monker/bin_rake` (senza README né SHA256SUMS): vedi D3.

| Opzione | Default | Note |
|---|---|---|
| `--config`, `--resources-dir`, `--output-dir` | obbligatorie | solo HU; usa `preflop_all_in_v1.bin` |
| `--iterations N` | 20000 | |
| `--report-every N` | 1000 (minimo 1) | |
| `--alpha/--beta/--gamma X` | 1.5 / 0 / 2 | |

- Single-thread, senza checkpoint.
- **Stderr**: una riga per report, `iteration N gain g0 g1 a (P % of the pot) ev e0 e1 seconds S`.
- **Stdout**: una riga `rake: ...` opzionale, poi `PREFLOP_BLUEPRINT_CHECKDOWN=PASS charts=N max_gain_pot_percent=X`.
- **Output**: `<out>/charts/<POS>/*_strategy.txt` e `<out>/summary.json` (Appendice E.5).
- **Durata**: HU50 con 5.000 iterazioni ≈ 75 s.

### A.4 `gtosd_preflop_blueprint_checkdown_classes` (step 1, 2-3 giocatori, livello classe)

Esiste solo in `out/monker/bin_3way_step1` (senza README né SHA256SUMS): vedi D3.

| Opzione | Default | Note |
|---|---|---|
| `--config`, `--resources-dir`, `--output-dir` | obbligatorie | 2 o 3 giocatori; il 3-way usa `preflop_three_way_v1.bin` (0,49 GB) |
| `--iterations N` | 20000 | 0 solo se tutti i nodi sono bloccati (valuta un set di chart) |
| `--report-every N` | 1000 | |
| `--alpha/--beta/--gamma` | 1.5 / 0 / 2 | |
| `--threads N` | 1 | 1..256; i risultati non dipendono dal numero di thread |
| `--folded-cards dead\|ignore` | `dead` | solo 3-way |
| `--lock-charts DIR` / `--lock-nodes all\|a,b` | — / `all` | qualsiasi numero di seat; niente colonne `*_EV` nei chart |
| `--reference DIR`, `--ev-tolerance`, `--chart-tolerance` | 1e-6 / 1e-3 | solo HU: equivalenza con `checkdown` |
| `--expect-summary FILE` / `--expect-tolerance X` | 1e-3 | |

- **Log** (stdout + stderr): righe `iteration N gain g0, g1, g2 a (p % of the pot) ev e0, e1, e2 seconds S`, poi `EV [...]`, `gain [...]`, `largest local gains: ...`.
- **Verdetto**, per esempio: `PREFLOP_BLUEPRINT_CHECKDOWN_CLASSES=PASS seats=3 charts=54 max_gain_pot_percent=2.15e-05`.
- **Durata**: circa 17 ms per iterazione con 8 thread su 3WAY50; 10.000 iterazioni ≈ 172-174 s.

### A.5 `gtosd_preflop_blueprint_monker_values` (exploitability esatta e valore dei chart; solo HU)

| Opzione | Default | Vincoli |
|---|---|---|
| `--config`, `--resources-dir`, `--buckets-dir`, `--policy` | obbligatorie | la sorgente della policy deve portare lo stesso suffisso d'astrazione |
| `--charts NAME=DIR` | almeno una, ripetibile | per esempio `ours=<run>/charts/it_N`, `monker=<copia ripulita>`; niente colonne `*_EV` |
| `--board-class-rows` / `--board-texture-map FILE` | | come nel training |
| `--all-flops` | flag | pass esatto sui 573 flop canonici (modalità `exact`) |
| `--flop-limit K` | 0 | solo con `--all-flops` (modalità `partial`) |
| `--flops N` / `--seed N` | 64 / predefinito | modalità `sampled` |
| `--threads N` | **1** | |
| `--target-pot-percent X` | 1 | solo riportato (`target_antes`) |
| `--river-engine joint\|reference` | `joint` | |
| `--no-combo-values` | flag | JSON più piccolo (275 KB contro 1,47 MB) |
| `--expected-rake` | flag | richiede una config con rake |
| `--out FILE` | — | `values.json` (Appendice E.2) |
| `--exploit NAMES\|all`, `--exploit-heroes`, `--exploit-streets`, `--exploit-out`, `--exploit-summary` | off | best response contro un set di chart (`exploit.json`) |

- **Interi in base 0** [V `monker_values.cpp:148-150`]: `010` = 8, `0x10` = 16. Il backend scrive sempre decimali semplici.
- **Stderr**: JSON Lines. Esempi reali:
  - `{"event": "start", "mode": "exact", "flops": 573, "threads": 2, "policy_entries": 57437559, "load_seconds": 6.84685}`
  - `{"event": "progress", "flops_done": 28, "flops_total": 573, "elapsed_seconds": 2.5385, "seconds_per_flop": 0.0906605, "eta_seconds": 49.41}`
  - Esistono anche `expected_rake_pass`, `exploit_pass` ed `exploit_progress`.
- **Stdout**: testo, poi `PREFLOP_BLUEPRINT_MONKER_VALUES=PASS mode=exact|sampled|partial`.
- **Fallimenti** [V `monker_values.cpp:1589-1602`; `convergence_curve.py:187-189`]:
  - `=FAIL self-checks N` e `=FAIL cannot write F` vanno su **stdout**, non su stderr, con exit 1;
  - `values.json` viene scritto lo stesso;
  - gli errori di argomenti seguono la convenzione di A.1 [I: verifica].
- **Successo** = exit 0 **e** riga `PASS mode=` **e** `self_checks.passed == true`. L'esistenza del file non prova nulla.
- **Costo**: V1L circa 45-80 s con 2 thread e 0,58 GB; HU50 30x4 279 s; G4 84 min.

### A.6 `gtosd_preflop_blueprint_game` (valida una config; dimensioni dell'albero e dello stato)

| Opzione | Default | Note |
|---|---|---|
| `--config PATH` | obbligatoria | |
| `--preflop-only` / `--checkdown` | flag | modalità di compilazione; il `tree_fingerprint` dipende da questa (full = albero del trainer; checkdown = albero dello step 1) |
| `--actions` | flag | aggiunge `preflop_decisions` (nodo, path id, attore, livello, `limped_pot`, `actions[{label, amount_units, all_in}]`) |
| `--flop/--turn/--river N` | 200/500/1000 | capacità di `layout_baseline` (uint32): passa le capacità reali per street (H.2) |
| `--alt-flop/--alt-turn/--alt-river N` | 500/1000/2000 | capacità di `layout_alternative` |
| `--max-nodes N` | 50.000.000 | la UI passa `validate_max_nodes` |
| `--dump-nodes FILE` | — | **nuovo, assente nel bin frozen `c123`** |

- **Stdout**: un JSON su più righe, poi `PREFLOP_BLUEPRINT_GAME=PASS`.
- **Chiavi del JSON** (schema `gtosd.preflop_blueprint_game_report.v1`):
  - `config_id`, `config_fingerprint`, `player_count`, `tree_fingerprint`;
  - conteggi di nodi, archi, decisioni, chance e terminali;
  - `maximum_depth`, `maximum_raise_count`;
  - `layout_baseline` e `layout_alternative`, ciascuno con `{flop_capacity, turn_capacity, river_capacity, entries, table_bytes, state_bytes}`.
- **Attenzione**: il `state_bytes` del game tool è solo 16 B × entries [V `compiled_game.hpp:194-196`]. Per la RAM del trainer usa la formula di 5.6.

### A.7 `gtosd_preflop_blueprint_monker_tree` (albero contro una cartella di chart Monker)

Esiste solo in `out/monker/bin_3way_step1`: vedi D3.

- **Opzioni**: `--config` più `--charts DIR` (copia ripulita se il set ha EV) e/o `--manifest TSV`; `--write-manifest TSV`; `--json FILE`.
- **Stdout**: righe di errore `<check>: <text>`, un riepilogo, poi uno dei due verdetti:
  - `PREFLOP_BLUEPRINT_MONKER_TREE=PASS nodes=N files=N`;
  - `=FAIL missing=.. extra=.. actions=.. unknown_tokens=.. classes=.. pot_rule=.. manifest=..`.
- **Manifest**: `benchmarks/monker/{HU50,3WAY50,3WAY100}_tree_manifest.tsv`.

### A.8 Altri eseguibili (fuori dal prototipo)

- `monker_buckets`: costruisce le bucket table.
- `monker_charts`: produce chart da una policy.
- `resources` e `three_way_table`: risorse una tantum.
- `export`, `compare` e `abstract_br`: percorso P8 per le policy history-rows. Non capiscono le board-class-rows [I].

---

## Appendice B: argv canonici (riferimento per i builder)

I builder della UI partono da queste righe. Le differenze volute sono documentate nelle fixture di argv attesi (M2, criterio 13).

### B.1 Segmento trainer `step2` (stile `run_correctness.sh`, `segmented`) [V `run_correctness.sh:65-100`]

```
<bin>/gtosd_preflop_blueprint_train(.exe)
  --config <run>/inputs/config.json --resources-dir <res> --buckets-dir <buckets>
  --board-class-rows --board-texture-map <run>/inputs/texture_map.txt
  --threads <T> --table-storage double --eval-every 0 --batch 32 --partition-target <P>
  --scheme dcfr --update alternating --batch-policy-refresh --lazy-discount
  --progress-every <K> --iterations <N>
  --checkpoint <run>/state.ckpt --chart-every <N> --chart-dir <run>/charts
  [--policy-snapshots]                           # solo se questo segmento sarà valutato
  --stop-file <run>/STOP                         # aggiunto dalla UI: il driver non lo passa
  [--lock-charts <run>/inputs/lock_charts --lock-nodes <list|all>] [--seed <S>] [--resume]
```

- `--resume` si aggiunge quando `state.ckpt` esiste, dopo la procedura "superseded" di 5.8.
- Il driver usa `--partition-target 4` e `--progress-every 1000`. Con target piccoli (250) serve un `K` più basso per vedere il progresso.
- Stdout va appeso a `train.jsonl`, stderr a `train.stderr.log`.
- Dopo ogni segmento:
  1. guardia sull'albero (`tree_fingerprint` dell'ultimo `start`);
  2. valutazione dello snapshot, se prevista.
- **Il driver cancella `charts/it_N/policy.bin` dopo la valutazione: la UI no.**

### B.2 Trainer `step2` `continuous` (stile `run_step2_continuous.sh`) [V]

```
... --board-class-rows [--board-texture-map M] --threads 8 --table-storage double --eval-every 0 --batch 32
    --partition-target 64 --scheme dcfr --update alternating --batch-policy-refresh --lazy-discount
    --progress-every 500 --iterations <MAX> --checkpoint <out>/state.ckpt --checkpoint-every 20000
    --policy-out <out>/policy.bin --chart-every <STEP=4000> --chart-dir <out>/charts --stop-file <out>/STOP
    [--policy-snapshots --policy-snapshot-every <K×STEP>] [--lock-charts ... --lock-nodes ...] [--seed ...] [--resume]
```

- Dopo ogni snapshot il backend chiama `compare()` (copia congelata):
  - contro il set Monker (copia ripulita se serve), scrivendo `evals/it_N/vs_monker.json/.txt`;
  - contro `it_<prev>`, scrivendo `evals/it_N/vs_previous.json/.txt`.
- Stop rule (M3): se `overall_mean_distance(vs_previous) < THRESHOLD`, il backend crea `STOP`. Default dei driver: `THRESHOLD` 0,01; poi 0,005; 0 = mai.

### B.3 Valutazione di uno snapshot [V `convergence_curve.py:174-183`, adattato]

```
<bin>/gtosd_preflop_blueprint_monker_values --config C --resources-dir RES --buckets-dir B --board-class-rows
  [--board-texture-map M] --policy <run>/charts/it_N/policy.bin --charts ours=<run>/charts/it_N
  [--charts monker=<copia ripulita del set>] --all-flops --threads <T> --no-combo-values [--expected-rake]
  --out <run>/evals/it_N/values.json
```

- Stdout e stderr vanno in `<run>/evals/it_N/values.log`.
- Esito secondo A.5 (tripla condizione).
- La valutazione finale delle run HU50 esistenti usa `--policy policy.bin --charts monker=<dir> --charts ours=charts/it_<last>`, **con** i combo values, e scrive `monker_values_exact.json`.

### B.4 Step 1

```
gtosd_preflop_blueprint_checkdown --config C --resources-dir RES --output-dir OUT --iterations 5000
gtosd_preflop_blueprint_checkdown_classes --config C --resources-dir RES --output-dir OUT
  --iterations 10000 --report-every 1000 --threads 8 [--folded-cards dead|ignore]
  [--lock-charts DIR --lock-nodes all [--iterations 0]]
```

---

## Appendice C: config di gioco JSON (`gtosd.preflop_blueprint_game.v1`)

**Fonti**: parser `libs/preflop_blueprint/src/game_config.cpp:372-625`, validazione `:256-370`.

**Regole generali**
- Gli importi sono **interi** in units (10.000 per ante). `5.0` si rifiuta.
- Le percentuali sono in basis points (10000 = 100 %).
- **Le chiavi sconosciute sono ignorate in silenzio** [V]: la UI le rifiuta.
- Gli errori del solver riportano solo il codice (`invalid_json | unsupported_schema | missing_field | invalid_value | invalid_structure`), mai la chiave. Per questo la UI valida da sé campo per campo e poi conferma con il game tool.
- `id` entra nei fingerprint e nell'identità del checkpoint. Non entra mai in un nome di file o in una command line della UI.

| Chiave | Obbl. | Tipo | Validazione |
|---|---|---|---|
| `schema` | sì | string | esattamente `gtosd.preflop_blueprint_game.v1` |
| `monetary_contract_revision` | sì | int | = 2 |
| `ante_accounting` | sì | const | `"dead_initial_pot_contribution"` |
| `preflop_target_basis` | sì | const | `"live_commitment_excluding_dead_ante"` |
| `raise_termination` | sì | const | `"natural_stack"` |
| `rake_mode` | sì | string | `"disabled"` \| `"enabled"` |
| `id` | sì | string | non vuoto |
| `player_count` | sì | int | 2..6 |
| `positions` | sì | string[] | lunghezza = `player_count`, valori distinti e non vuoti, in ordine d'azione, **ultimo = `"BTN"`** |
| `effective_stack_units` | sì | int | > 0 e > button blind |
| `ante_units` | sì | int | > 0 |
| `button_blind_units` | sì | int | > 0 e < stack |
| `postflop_minimum_bet_units` | sì | int | > 0 |
| `open_target_units` | sì | int[] | strettamente crescenti, ognuno in (button blind, stack), al massimo 4; **`[]` in pot mode**; esattamente uno tra questo (non vuoto) e `preflop_open_sizes_basis_points` |
| `response_target_units` | sì | int[] | `[]` = solo re-raise all-in; altrimenti uno per ogni open target, ognuno in (open, stack); `[]` in pot mode |
| `allow_configured_incomplete_raise` | sì | bool | |
| `include_all_in` | sì | bool | deve essere `true` |
| `postflop_sizes_basis_points` | sì | int[] | da 1 a 3 valori, ognuno in 1..100000, strettamente crescenti |
| `limp_response_target_units` | no | int[] | assente = riusa `response_target_units`; `[]` = fold/call/all-in; non vuoto vietato in pot mode |
| `preflop_open_sizes_basis_points` | no | int[] | pot mode (alberi Monker): da 1 a 3 valori, > 0, ≤ 100000, crescenti; `[]` → `invalid_structure` |
| `postflop_donk_bets` | no | bool | default `true` |
| `postflop_all_in_max_pot_basis_points` | no | int | 1..100000 (50000 = all-in al massimo 5 volte il pot); assente = all-in ovunque |
| `rake_basis_points` | se rake | int | 1..10000 |
| `rake_cap_units` | se rake | int | > 0 |
| `rake_no_flop_no_drop` | se rake | bool | |
| `rake_minimum_pot_units` | se rake | int | ≥ 0 |
| `postflop_betting_streets` | no | string[] | non vuoto, distinti, in ordine, da `flop, turn, river` (le altre street sono solo check). `bin_correct/base` lo ignora: guardia sull'albero. |

Con `rake_mode: "disabled"`, qualunque chiave di rake dà `invalid_value`. Con `"enabled"`, servono tutte e quattro.

**Esempi reali**

```json
{"schema": "gtosd.preflop_blueprint_game.v1", "id": "MONKER-HU50-STEP2-2SIZE-DONK-ALLIN5X-001",
 "monetary_contract_revision": 2, "ante_accounting": "dead_initial_pot_contribution",
 "preflop_target_basis": "live_commitment_excluding_dead_ante", "player_count": 2, "positions": ["CO", "BTN"],
 "effective_stack_units": 500000, "ante_units": 10000, "button_blind_units": 10000,
 "open_target_units": [50000], "response_target_units": [], "allow_configured_incomplete_raise": true,
 "postflop_sizes_basis_points": [5000, 10000], "postflop_minimum_bet_units": 10000, "include_all_in": true,
 "raise_termination": "natural_stack", "rake_mode": "disabled", "postflop_donk_bets": true,
 "postflop_all_in_max_pot_basis_points": 50000}
```
Fonte: `benchmarks/monker/HU50_step2_2size_donk_allin5x.json`.

```json
{"schema": "gtosd.preflop_blueprint_game.v1", "id": "MONKER-3WAY50-DONK-RAKE-001", "monetary_contract_revision": 2,
 "ante_accounting": "dead_initial_pot_contribution", "preflop_target_basis": "live_commitment_excluding_dead_ante",
 "player_count": 3, "positions": ["UTG", "CO", "BTN"], "effective_stack_units": 500000, "ante_units": 10000,
 "button_blind_units": 10000, "open_target_units": [], "preflop_open_sizes_basis_points": [10000],
 "response_target_units": [], "allow_configured_incomplete_raise": false, "postflop_sizes_basis_points": [10000],
 "postflop_minimum_bet_units": 10000, "include_all_in": true, "raise_termination": "natural_stack",
 "rake_mode": "enabled", "rake_basis_points": 500, "rake_cap_units": 30000, "rake_no_flop_no_drop": true,
 "rake_minimum_pot_units": 0, "postflop_donk_bets": true}
```
Fonte: `benchmarks/monker/3WAY50_donk_rake.json` (pot mode, rake 5 %, cap 3 a).

**Catalogo esistente** (usalo per i test del validatore)
- HU50: base, rake, step2, step2_2size, allin5x, donk, donk_rake*.
- 3WAY50 e 3WAY100: `donk*`.
- Correttezza: HU6_all, HU6_V0_flop, HU6_V1_flopturn, HU6_V2_river (varianti rake), HU19_B0_flop, HU19_B0M_flop, HU19_B2_river, HU8_B1_flopturn. Nessuno di questi ha un riferimento Monker.
- I tree fingerprint attesi sono in `benchmarks/monker/correctness/README.md`.
- Alcune config sono in modifica da parte di altre sessioni: copia le fixture del validatore e non leggerle dal tree live durante i test.

---

## Appendice D: eventi di `train.jsonl`

### D.1 Ordine per processo [V]

1. `start`
2. `memory_breakdown` (stage `after_initialization`)
3. `memory_breakdown` (stage `after_first_iteration`)
4. ripetuti: `training_progress`, `charts` / `charts_failed` / `policy_snapshot_skipped` / `policy_snapshot_failed`, `stop_file` (e `evaluation` e `exact_certification*` solo se `eval-every > 0`)
5. `end`
6. la riga di testo `PREFLOP_BLUEPRINT_TRAIN=<STATUS>`

Il file si appende segmento dopo segmento: contiene più blocchi `start ... end`.

| Evento | Campi utili |
|---|---|
| `start` | `config_id`, `tree_fingerprint`, `trainer_identity`, `nodes`, `decisions`, `capacities[3]`, `board_texture` (null oppure `{name, fingerprint, classes[3]}`; assente nelle run precedenti al 28/09 sera), `state_bytes`, `threads`, `batch`, `scheme`, `lazy_discount`, `table_storage`, `update`, **`resumed_iteration`**, `initial_pot_antes`, `preflop_lock{charts, files[], rows, outside_range_rows, fingerprint}`, `preparation_seconds` |
| `memory_breakdown` | byte per tabella, `accounted_total_bytes`, `process{working_set_bytes, peak_working_set_bytes, private_commit_bytes, peak_private_commit_bytes, page_faults}`, `stage`. Chiavi ordinate: `event` non è la prima |
| `training_progress` | `iteration` (assoluta), `training_seconds` (**solo questo processo**), `discount_seconds`, `policy_refresh_seconds`, `board_prepare_seconds`, `traversal_seconds`, `boards_processed` (cumulativo), `process_bytes` (0 fuori da Windows) |
| `charts` | `iteration`, `training_seconds`, [`policy_fingerprint`, `policy_seconds`] se è stata scritta una policy di snapshot |
| `policy_snapshot_skipped` | `iteration`, `reason: "disk_space"`, `available_bytes`, `required_bytes` |
| `policy_snapshot_failed` / `charts_failed` | `iteration`, `error` |
| `stop_file` | `iteration` |
| `end` | `iteration`, `policy_fingerprint` (`""` senza `--policy-out`), `boards_processed`, `converged`, `plateau`, `convergence_status`, `preparation_seconds`, `training_seconds`, `write_seconds`, `seconds_per_iteration` (di questo processo), `state_fingerprint`, `process_bytes`, `process_after_training{...}`, `process_final{...}`, `total_seconds` |

### D.2 Righe reali [V `out/monker/correctness/V1L/train.jsonl`, segmento 48000→64000]

```
{"event": "start", "config_id": "CORRECTNESS-HU6-V1-FLOPTURN-001", "tree_fingerprint": "fnv1a64:da5c6942354ad5ad", "trainer_identity": "fnv1a64:d8161b50af8c502b", "nodes": 31, "decisions": 14, "capacities": [302544, 6825456, 206415], "board_texture": {"classes":[573,13761,13761],"fingerprint":"","name":"identity"}, "state_bytes": 977619252, ..., "threads": 2, "scheme": "dcfr", "lazy_discount": true, "table_storage": "double", "update": "alternating", "resumed_iteration": 48000, "initial_pot_antes": 3, "preflop_lock": {"charts":"benchmarks/monker/correctness/lock_limp_check","files":["CO/CO_strategy.txt","BTN/CO_Call_BTN_strategy.txt"],"fingerprint":"ae10d154d2190a64","outside_range_rows":0,"rows":162}, "preparation_seconds": 10.0911}
{"accounted_total_bytes":1063425885,...,"process":{"page_faults":374005,"peak_private_commit_bytes":1070874624,...},"stage":"after_initialization",...}
{"event":"training_progress","iteration":64000,"training_seconds":988.637,"discount_seconds":0.0245918,"policy_refresh_seconds":109.777,"board_prepare_seconds":47.548,"traversal_seconds":831.274,"boards_processed":4096000,"process_bytes":1082556416}
{"event":"charts","iteration":64000,"training_seconds":988.637,"policy_fingerprint":"fnv1a64:a87257734a6e63dd","policy_seconds":3.06026}
{"event": "end", "iteration": 64000, "policy_fingerprint": "", ..., "convergence_status": "NOT_REACHED", "training_seconds": 988.637, "write_seconds": 4.33771, "seconds_per_iteration": 0.0617898, "state_fingerprint": "fnv1a64:0cdfd19ef6f20207", ..., "total_seconds": 1007.86}
PREFLOP_BLUEPRINT_TRAIN=ITERATION_LIMIT
```

Un esempio reale di stop: `{"event":"stop_file","iteration":20057}` seguito da `PREFLOP_BLUEPRINT_TRAIN=STOPPED`.

### D.3 Progresso ed ETA

- **Rate**: `(iteration − resumed_iteration) / training_seconds`, calcolato su una finestra mobile degli ultimi eventi `training_progress` del processo corrente, usando le differenze tra eventi.
- **ETA del segmento**: `(target − iteration) / rate` + scrittura finale. Per la scrittura si usa il `write_seconds` degli `end` precedenti; su HU50 sono 24-30 s.
- **ETA della run**, sommando per ogni segmento restante:
  - la preparazione (`preparation_seconds`: 8-15 s sui giochi piccoli, circa 25 s su HU50);
  - il training;
  - la scrittura;
  - una valutazione per snapshot valutato (durate precedenti, oppure `eta_seconds` dell'evaluator in corso);
  - le pause delle finestre.
- **Velocità reali**:
  - V1L: 0,062 s per iterazione da solo; da 0,1 a 0,2 s con 7 run attive.
  - HU50 30x4: da 0,077 a 0,092 s per iterazione.
- **Exploitability dello step 2**: non compare in `train.jsonl`. Viene solo dalle valutazioni `monker_values`.

---

## Appendice E: altri output

### E.1 Layout delle run esistenti

| Layout | Dove | Contenuto |
|---|---|---|
| Correctness | `out/monker/correctness/<RUN>/` (SSD; V1L occupa 880 MB) | `run.log`, `train.jsonl`, `train.stderr.log`, `state.ckpt`, `curve.log`, `convergence.txt/.json`, `charts/it_N/{CO,BTN}/`, `values.json`, `values.log`, [`policy.bin`] |
| Step 2 continuous | `out/monker/variants/<run>/`, `out/monker/step2/<run>/` (**junction verso F:**) | `run.log`, `train.jsonl`, `state.ckpt` (2,06 GB), `policy.bin` (1,03 GB), `charts/it_N/{CO,BTN, vs_monker.*, vs_previous.*, [policy.bin], [values.*]}`, `monker_values_exact.json/.log`, `monker_in_our_game.json/.txt`, `br_split.txt`; opzionali `exploit.json`, `lock_check.json`, `convergence.*`, `STOP`, `NO_ARCHIVE` |
| Step 1 HU | `out/monker/step1/HU50/` | `summary.json`, `charts/{CO,BTN}/` (8 chart), `compare.json` (formato vecchio: solo `charts, only_ours, only_theirs, overall_mean_distance, overall_same_main_action_share`) |
| Step 1 classes | `out/monker/step1_3way/` | `<NAME>.log`, `<NAME>/{summary.json, charts/{UTG,CO,BTN}/ (54), vs_monker.json/.txt, tree_check.txt}`, `<NAME>_monker_eval/{summary.json, charts/}` |

**Log di coda globale**: `out/monker/variants/chain.log`, con righe `YYYY-MM-DD HH:MM:SS start|end|evaluated|archive <run> ...` e note libere. File di cancel di coda: `out/monker/variants/CANCEL_<NAME>`.

**Grammatica di `run.log` (correctness)**:
```
2026-09-30 17:45:54 correctness start: config <cfg> buckets <dir> map <map> threads 2 eval 2 partition 4 bin <bin> lock <dir|none> <nodes> tree <fnv1a64:...|unchecked> targets 250 500 1000 ...
17:46:46 segment 250 rc 0 (52 s)
17:48:07 evaluation 250 rc 0 (80 s)
HH:MM:SS done | HH:MM:SS cancelled before <N> | tree <x> is not the expected <y>: stop
```

**Grammatica di `run.log` (step 2 continuous)**:
```
2026-09-28 18:29:39 step 2 continuous start: config <cfg> buckets <dir> step 300 max 3000 threshold 1.0 <TRAIN_ARGS> [--resume]
2026-09-28 18:30:12 iteration 300: elapsed 31 s, change vs previous na, vs monker distance 0.2496 range difference 0.7328
STABLE at N (change X < T): stopping the trainer
trainer finished: PREFLOP_BLUEPRINT_TRAIN=<status>, last snapshot N, stop event ...
trainer failed (exit rc)
```

La riga `stop event` legge tutto il `train.jsonl`: dopo un resume mostra lo stop **vecchio**.

**Stato inferito dai viewer attuali** (parole chiave di `run.log`):
- `STABLE` → stabile;
- `trainer failed` → interrotto;
- `trainer finished` → finito senza soglia;
- altrimenti in corso / avviato / in attesa.

### E.2 `values.json` (`gtosd.preflop_blueprint_monker_values.v1`, solo HU)

```
schema, config_id, [rake{basis_points, cap_antes, no_flop_no_drop, minimum_pot_antes}], tree_fingerprint,
policy_fingerprint, policy_source ("<trainer id>|iteration=N|abstraction=...|preflop-lock=..."),
abstraction ("board-class-rows-v2|flop=120|turn=120|river=30|texture=...|classes=573/4482/4482|river-key=turn"),
board_texture{name, fingerprint, classes[3]}|null, fingerprints{catalog, flop_table, turn_table, river_table},
capacities[3], initial_pot_antes (3.0), target_pot_percent (1.0), target_antes (0.03), value_scope (testo),
evaluation{mode: exact|partial|sampled, flops, physical_flops, boards, seed|null, threads, river_engine,
           seconds{load, stage_one, aggregate, values[, expected_rake]}, process_after_load{..}, process_peaks{..}},
estimate{ev_antes[2], ev_standard_error_antes[2], best_response_antes[2], gain_antes[2], gain_lower_antes[2],
         best_response_preflop_antes[2], gain_preflop_antes[2], max_gain_antes, nashconv_antes,
         [ev_sum_antes, expected_rake_antes, expected_rake_by_hero_antes[2]]},
combos{labels[630], class[630]}, classes[81]  (ordine degli id: AA,KK..66, poi suited AKs.., poi offsuit),
heroes[2]{hero, position, ev_antes, root_value_mean_antes, top[..], [root_values[630]],
  nodes[]{chart ("CO/CO_strategy.txt"), node, path_id, tokens[], columns[], parent: null|{chart, action},
          next{token: [chart figli dello stesso hero]}, strategy{class:{token:freq}},
          class_ev{class:{token:{ev, se|null}}}, class_weight{class: combos x reach avversario},
          [opponent_reach[630], combo_values{token:[630]}]}},
preflop_response[2]{hero, position, gain_per_class_antes, gain_per_combo_antes, aggregate_gain_preflop_antes,
                    pot_percent, choices{chart:{class: best token}}},
chart_sets[]{name, directory, players[2]{hero, position, ev_ours_antes, ev_charts_antes, loss_antes, loss_pot_percent,
  loss_standard_error_antes, loss_recursive_antes, positive_loss_antes, negative_loss_antes, fallback_reach_combos,
  within_target, nodes[]{chart, node, path_id, columns, loss_antes, loss_pot_percent, reach_combos_ours,
  reach_combos_charts, largest[[class, loss] x10], largest_negative[[class, loss] x5],
  classes{class:{combos, reach_ours, reach_charts, opponent_reach, row, strategy_ours{}, strategy_charts{},
          best_action, local_loss_antes, regret_ours_antes, loss_antes}}}}},
[exploitation{...}], self_checks{passed, failures[]}
```

- `class_ev`: EV dell'azione in ante, condizionato alla classe e alla storia pubblica. È netto, con gli ante messi inclusi: Fold alla root vale −1,0.
- `se` è non nullo solo in modalità `sampled`.
- Una valutazione vale solo se `self_checks.passed` è vero (A.5).

### E.3 `convergence.txt` / `convergence.json` (`tools/monker_compare/convergence_curve.py`)

Formato di riferimento: il backend produce righe con gli stessi campi (5.12), ma non esegue lo script.

**TSV reale** [V V1L]; i valori mancanti sono `na`:
```
iteration	train_s	gain_CO	gain_BTN	gain_lower_CO	gain_lower_BTN	gain_pre_CO	gain_pre_BTN	nashconv_a	nashconv_%pot	distance	range_diff
250	30	0.93084	1.07500	0.27332	0.34266	0.92911	1.07089	2.00584	66.861	na	na
```
Con `--monker` ci sono anche le colonne `monker_loss_<P0> monker_loss_<P1>`.

**JSON**:
- top level: `{schema: "gtosd.preflop_blueprint_convergence_curve.v1", run, config, buckets, texture_map, monker, pass, flop_limit, positions, initial_pot_antes, value_scope, rows[], failures[]}`;
- ogni riga: `iteration, training_seconds, mode, flops, policy_fingerprint, policy_source, ev_antes[2], gain_antes[2], gain_lower_antes[2], gain_preflop_antes[2], max_gain_antes, nashconv_antes, nashconv_pot_percent, distance, range_difference, monker_loss_antes[2]|null, values_json, evaluation_seconds`, più le chiavi di rake.

**Attenzione**: `train_s` è per processo, **non cumulativo**.

### E.4 Output di `compare_charts.py`

**Uso**: `python tools/monker_compare/compare_charts.py <ours dir> <theirs dir> [--top 5] [--json out.json]`, oppure `compare(ours, theirs, top)` importato.

**Chiavi per chart**:
- `chart`, `actions_ours`, `actions_theirs`, `classes`;
- `outside_both_ranges[]`, `in_range_only_ours[]`, `in_range_only_theirs[]`;
- `mean_distance`, `same_main_action_share`, `largest[{class, distance, ours{}, theirs{}}]`;
- `range_combos_ours`, `range_combos_theirs`, `range_difference`, `restricted_range`, `range_common_combos`, `range_union_combos`.

**Chiavi di top level**:
- `charts[]`, `only_ours[]`, `only_theirs[]`;
- `overall_mean_distance`, `overall_same_main_action_share`;
- `overall_range_difference`, `overall_range_combos_ours`, `overall_range_combos_theirs`, `distinct_restricted_ranges`.

**Arrotondamenti**: i valori del JSON sono arrotondati con `round(x, 4)` (`:110-111, 166-167, 174-175, 191`).

**Percorsi**: la funzione `native_path` converte i percorsi `/c/...`.

### E.5 `summary.json` dello step 1

**`gtosd.preflop_blueprint_checkdown.v1` (HU)**:
- `config_id`, `tree_fingerprint`, `all_in_table_fingerprint`, `nodes`, `iterations`, `dcfr`;
- `initial_pot_antes`, `ev_antes[2]`, [`rake`, `expected_rake_antes`, `expected_rake_by_reach_antes`];
- `gain_antes[2]`, `max_gain_pot_percent`, `seconds`, `charts`;
- `trajectory[{iteration, gain[2], seconds}]`.

**`gtosd.preflop_blueprint_checkdown_classes.v1`**:
- `seats`, `terminal_source{file, fingerprint, heads_up_file, folded_cards}`, `threads`, `nodes`, `terminals`, `iterations`, `dcfr`;
- `initial_pot_antes`, `ev_antes[n]`, [`rake`], `expected_rake_antes`, `expected_rake_by_reach_antes`;
- `gain_antes[n]`, `gain_pot_percent[n]`, `max_gain_pot_percent`;
- `seconds`, `load_seconds`, `iteration_seconds`, `seconds_per_iteration`, `charts`;
- [`lock{charts_dir, files, chart_nodes, rows, chart_rows, outside_range_rows, fallback_rows, fallback_reach_combos}`];
- `chart_nodes[{chart, seat, own_reach_combos, local_gain_antes}]`, `trajectory[]`.

### E.6 Altri file (opzionali, da tollerare se mancano)

- **`monker_in_our_game.json`**, per giocatore:
  - `ev_ours_rows_antes`, `ev_monker_antes`, `loss_antes`, `loss_pot_percent`;
  - `gain_best_response_antes`, `gain_best_response_pot_percent`;
  - `best_response_choices`, `fallback_rows`;
  - `nodes[{chart, loss_antes, reach_combos_reference, reach_combos_alternative, largest, largest_negative}]`.
- **`exploit.json`** (`--exploit`): `cases[]` con `chart_set`, `chart_player`, `exploiter`, `exploitation{charts_antes, charts_pot_percent, ours_antes, ours_pot_percent, extra_antes, extra_pot_percent, extra_gain_lower_antes, extra_gain_preflop_antes}`, `difference`, `streets`, `seconds`.
- **`lock_check.json`**: `{iteration, per_node{chart: distance}, btn_iso_range_difference|co_limp_range_difference, co_root_mix, converged|explained, kind}`.
- **`tree_check.txt`**: termina con la riga di verdetto `PREFLOP_BLUEPRINT_MONKER_TREE=...`.

---

## Appendice F: chart, nomi dei nodi, libreria Monker, metriche

### F.1 Formato dei chart (il nostro e quello di Monker coincidono, salvo le colonne EV) [V `benchmarks/monker_chart_format.hpp`]

```
Combination<TAB>AllIn<TAB>Call<TAB>Fold<TAB>Total
66<TAB>0.000<TAB>1.000<TAB>0.000<TAB>1.000
76o<TAB>...
```

**Header e colonne**
- L'header è `Combination`, poi una colonna per azione, poi `Total` (che può mancare).
- I valori hanno 3 decimali.
- Ordine delle colonne: `AllIn`, raise in size crescente, `Call`, `Check`, `Fold`. **Leggi sempre per token, mai per posizione.**

**Righe**
- Sono 81, in ordine ASCII lessicografico delle etichette: `66, 76o, 76s, 77, 86o, ...`.
- Etichette: coppie `AA`, suited `AKs`, offsuit `AKo`, carta alta per prima, rank `6789TJQKA`.
- **Fuori range**: una riga con totale < 0,5. Il nostro writer azzera le classi con reach propria < 5e-4.
- **Normalizzazione**: il reader C++ normalizza le righe in range per il loro totale [V `format.hpp:308-348`]. `compare_charts.py` **no**: nel calcolo della distanza usa i valori grezzi (F.4). La UI normalizza solo per disegnare.

**Token d'azione**
- `Fold | Check | Call | AllIn | <impegno totale sulla street>ante`.
- Noi scriviamo le size con 1 decimale (`5.0ante`). Monker può scrivere `5.5ante`, `5.98ante`, `7.25ante`: leggile come float.

**Fine riga: mescolate** [V, scansione dei file]
- Noi scriviamo LF.
- Nei set Monker: HU/50a, HU/100a, 3-way/50a e 6-way/50a usano CRLF; HU/40a, 3-way/40a, 3-way/60a e 4-way/40a usano LF.
- Gestisci entrambe, sempre.

**Colonne EV** [V]
- Alcuni set Monker hanno colonne `<action>_EV` intercalate **in tutti i loro file**, non solo nelle root: HU/40a (20 su 20), 3-way/40a (54 su 54), 3-way/60a (54 su 54), 4-way/40a (236 su 236), e 5-way/40a.
- Esempio di header: `Combination AllIn AllIn_EV 6.0ante 6.0ante_EV 10.0ante 10.0ante_EV Call Call_EV Fold Fold_EV Total`.
- Unità: ante, al netto dell'ante messo (Fold alla root HU CO vale −1,000).
- HU/50a, HU/100a e 3-way/50a non hanno colonne EV.
- I nostri chart non hanno mai EV: stanno in `values.json`.
- **Nessuno strumento del repo regge le colonne EV** (5.14): usa copie ripulite.

### F.2 Nomi dei file = linea d'azione [V `monker_chart_format.hpp:113-192`]

- **Forma**: `<ACTOR>/<P1>_<t1>_<P2>_<t2>_..._<ACTOR>_strategy.txt`, nella cartella di chi agisce. La root del primo giocatore di mano è `<POS>/<POS>_strategy.txt`.
- **Fold impliciti**: un fold che è la prima azione di un giocatore **viene omesso** dal nome. I fold successivi si scrivono (`..._UTG_Fold_CO`). Esempi 3-way (`UTG, CO, BTN`):
  - `BTN/CO_Call_BTN_strategy.txt` = UTG fold, CO call, BTN agisce;
  - `BTN/UTG_6.0ante_BTN_strategy.txt` = UTG rilancia a 6 a, CO fold, BTN agisce.
- **Tokenizzazione**: per separare posizioni e azioni servono le posizioni del gioco (`UTG+1` contiene `+`). La tokenizzazione usa l'ordine d'azione (config, oppure la lista standard `UTG, UTG+1, MP, HJ, CO, BTN` troncata a N) per ricostruire i fold impliciti.
- **Genitori** (decisioni precedenti dello stesso attore): si leggono dal nome. Per esempio, per `BTN/CO_Call_BTN_5.0ante_CO_AllIn_BTN_strategy.txt` la reach di BTN è la sua frequenza `5.0ante` in `BTN/CO_Call_BTN_strategy.txt`. `values.json` dà anche `parent` e `next` in modo esplicito (solo HU).
- **Id interni** (`path_id` in `values.json`): per esempio `CO_call_BTN_all_in_CO`.
  - Etichette: `fold, check, call, all_in, bet_<antes>, raise_<antes>`.
  - I decimali si scrivono con `_` (`raise_4_5`); i duplicati ricevono un suffisso `_bp<basis points>`.
- **Conteggi**: HU50 8 chart; 3-way 50a 54 (UTG 16, CO 18, BTN 20); correctness HU6 4.

### F.3 Libreria Monker (locale, gitignored) [V]

**Percorso**: `CHARTS/ranges/Short Deck/{Symmetrical Chart,Asymmetrical Chart}/<tavolo>/<stack>/<POS>/*_strategy.txt`.

| Set | File | Colonne EV | Fine riga |
|---|---|---|---|
| Symmetrical HU | 40a: 20; 50a: 8; 75a: 8; 100a: 9; 150a: 10; 200a: 11; 300a: 9 | 40a sì | 40a LF; 50a e 100a CRLF; altri da verificare |
| Symmetrical 3-way | 40a, 50a, 60a, 100a: 54 ciascuno | 40a e 60a sì | 40a e 60a LF; 50a CRLF |
| Symmetrical 4-way | 706 (40a: 236) | 40a sì | 40a LF |
| Symmetrical 5-way | 5.114 | 40a sì | da verificare |
| Symmetrical 6-way | 30a: 664; 40a: 5.632; 50a: 5.600; 100a: 5.117 | — | 50a CRLF |
| Symmetrical, totale | 23.124 | | |
| Asymmetrical | 34.421 file, cartelle stack come `100 100 60` | da verificare | da verificare |

La colonna EV si rileva per file (dall'header), non per set.

### F.4 Metriche (devono coincidere con `compare_charts.py`) [V `tools/monker_compare/compare_charts.py`]

| Metrica | Definizione |
|---|---|
| combos(classe) | coppia 6, suited 4, offsuit 12 |
| azioni di un chart | tutte le colonne dell'header tranne `Combination` e `Total` (`:43-51`). Per questo le colonne `*_EV` rompono lo script: vanno tolte prima. |
| classe in range (per la distanza) | somma di **tutti** i valori della riga (senza `Total`) ≥ 0,5 (`:138-139`) |
| distanza di classe | ½ Σ_a \|f_nostra(a) − f_loro(a)\| sull'unione delle azioni, con i **valori grezzi, non normalizzati per il totale**; un'azione assente vale 0 (`:134-148`) |
| `mean_distance` di un chart | media delle distanze pesata per combo, sulle classi **in range da entrambe le parti** |
| `overall_mean_distance` | media semplice sui chart presenti da entrambe le parti |
| `same_main_action_share` | quota di combo con la stessa azione principale; l'azione principale è il massimo sulle azioni ordinate alfabeticamente, quindi a parità vince la prima in ordine |
| reach(classe, chart) | 1 se la riga della classe **nel chart stesso** è in range (somma ≥ 0,5), altrimenti 0; poi, per ogni decisione precedente dello stesso attore, si moltiplica per f(azione)/totale della riga del chart genitore, oppure per 0 se quella riga ha totale < 0,5 (`:76-96`). Qui sì che si normalizza, ma solo nei genitori. |
| chart genitore mancante | le metriche di range del chart sono `null` (`:100-103`) |
| dimensione del range | Σ combos × reach |
| `range_difference` | 1 − common/union, con common = Σ combos × min(reach_nostra, reach_loro) e union = Σ combos × max(...) [V `:108-109`]; `null` se union = 0 |
| `overall_range_difference` | common e union sommati sui **range ristretti distinti** (chart raggiunti con le stesse azioni proprie contano una volta), poi 1 − rapporto |
| arrotondamenti | `round(x, 4)` nel JSON: un'implementazione indipendente coincide entro 5e-5 |
| preferenza suited (solo nel JS dei viewer; M3) | alla root del primo attore, per ognuna delle 36 coppie di rank non appaiate con entrambe le versioni in range: P(entrare al postflop con Call o raise), suited meno offsuit; media delle 36 differenze. 0 = uguali, > 0 = suited giocate di più |

---

## Appendice G: inventario dei percorsi (PC attuale) [V]

| Cosa | Dove |
|---|---|
| Bin set frozen | Vedi la tabella sotto |
| Driver frozen | `out/frozen/run_correctness{,_lock,_v2}.sh`, `out/frozen/run_step2_continuous{,_abd,_lock,_rake,_algo}.sh` (solo riferimento: la UI non li usa) |
| Build tree (vietato) | `out/build/windows-release-suite` |
| Resources | `out/preflop_blueprint_resources/` (circa 426 MB) |
| Bucket set | `out/monker/buckets_{15x4,30x1,30x4,30x8,60x8,flopexact_15x4}/` (circa 42 MB ciascuno; `monker_buckets_report.json` → `flop/turn/river.capacity`, per esempio 120/120/30 per 30x4), `out/monker/correctness/buckets/*` |
| Texture map | `benchmarks/monker/textures/*.txt`. Header `gtosd-board-texture-v1`, `name <X>`, `river-key turn\|river-board`, `flop 573`, ... Classi turn: identity 13.761, TX0 8.217, TX1 6.768, TX2 4.482, TX3 2.680, TXM 1.899, TXM2 2.151. Alcune sono in modifica da parte di altre sessioni: la UI le congela per run. |
| Config | `benchmarks/monker/*.json` (20 file), `benchmarks/monker/correctness/*.json` |
| Manifest degli alberi | `benchmarks/monker/{HU50,3WAY50,3WAY100}_tree_manifest.tsv` |
| Lock chart | `out/monker_lock/charts_50a`, `benchmarks/monker/correctness/lock_limp_check`, `lock_b0m` |
| Script di analisi | `tools/monker_compare/{compare_charts,convergence_curve,monker_in_our_game,monker_tree_file,monker_tree_oracle}.py` (solo stdlib) |
| Run su SSD | `out/monker/correctness/*` (9,3 GB in tutto), `out/monker/step1`, `step1_rake`, `step1_3way`, `probe_2size` |
| Run su F: (junction) | le sottocartelle di `out/monker/variants/`, poi `out/monker/step2`, `out/monker/smoke_continuous`, `smoke_policy_snapshots*` |
| Spazio libero (30/09 sera) | C: 301 GB su 894; F: 215 GB su 1,9 TB (pieno all'89 %) |

**Bin set** [V `ls out/monker/bin*`]:

| Cartella | Tool | README / SHA256SUMS |
|---|---|---|
| `bin_correct/c123` | `game`, `monker_buckets`, `monker_values`, `train` (commit 591724c, dc34131, 68cf367; ctest 86/86) | sì / sì |
| `bin_correct/base` | `game`, `monker_buckets`, `monker_values`, `train` (ignora `postflop_betting_streets`) | sì / sì |
| `bin_rake` | `checkdown`, `monker_buckets`, `monker_values`, `train` | no / no |
| `bin_3way_step1` | `checkdown_classes`, `monker_tree` | no / no |
| `bin`, `bin_abd` | `monker_buckets`, `monker_values`, `train` | no / no |
| `bin_allin`, `bin_lock`, `bin_texture` | `monker_values`, `train` | no / no |
| `bin_threeway` | `three_way_table` | no / no |

Nessun set contiene tutti i tool della UI: vedi D3.

---

## Appendice H: dimensioni e tempi

### H.1 Misure [V]

| Run | RAM | Disco | Velocità e durata |
|---|---|---|---|
| V1L correctness (HU6, 2 thread) | stato 0,98 GB, picco 1,07-1,08 GB | checkpoint 0,92 GB, policy circa 0,46 GB | 0,062 s per iterazione; 64k iterazioni ≈ 6.500 s su 12 segmenti; valutazione 45-80 s |
| HU50 30x4 + TX2 + rake 2,5 % | 2,17 GB / picco 2,29 GB | 2,88 GB (checkpoint 2,06, policy 1,03) | 0,077-0,092 s per iterazione; 32k iterazioni in 52 min; valutazione esatta 279 s |
| HU50 30x4, map identità | stato 5,48 GB | checkpoint 5,2 GB, policy 2,6 GB | 0,065-0,079 s per iterazione; salvataggio 24-30 s; resume circa 26 s |
| E 30x8 / F 60x8 / D flop esatto | picco 3,82 / 7,53 / 1,54 GB | circa 1,5 × stato [I] | 0,074 / 0,105 / 0,076 s per iterazione |
| G4 (due size postflop, 8.599 nodi) | picco 15,67 GB | circa 23 GB [I] | 0,617 s per iterazione; 3 h 26 min, più 84 min di valutazione |
| Step 1 HU checkdown | piccola | piccolo | 5.000 iterazioni in 75 s |
| Step 1 3-way classes (8 thread) | 0,49 GB | piccolo | 17 ms per iterazione |
| 3-way step 2 (pianificato, non esiste) | circa 14,8 GB con 15x4; 27 GB con 30x4 | — | — |

### H.2 Regole pratiche [V, aritmetica verificata su V1L]

- **Checkpoint** = entries × 16 B + 136 B (storage double). V1L: 57.437.559 × 16 + 136 = 919.001.080 B, esatto.
- **Policy** (finale o snapshot) = 8 B per cella.
- **RAM del trainer**: `state_bytes` = tabelle regret e strategy + capacità della compact policy × 8 B + `discount_iterations` × 2 B + prefissi di discount × 8 B [V `trainer.cpp:2093-2098`]. Il picco di commit è un po' più alto: V1L 1,07 GB contro 0,978 GB di stato.
- **`state_bytes` del game tool** = solo 16 B × entries: sottostima il trainer.
- **RAM dell'evaluator** ≈ policy + circa 0,12 GB.
- **Capacità**:
  - flop = 573 × capacità flop;
  - turn = classi turn × capacità turn;
  - river = (classi turn con `river-key turn`, oppure 19.998 con `river-board`) × capacità river.
  - Verifiche: HU50 30x4 con map identità → [68.760, 1.651.320, 412.830]; V1L → [573×528, 13.761×496, 13.761×15].

---

## Appendice I: guasti visti e regola che ne deriva [V]

| Quando | Cosa è successo | Regola per il backend |
|---|---|---|
| 29/09 | Uno script in esecuzione è stato modificato: il runner è morto con un syntax error | Niente shell layer; si lanciano solo eseguibili frozen |
| 29/09 | Una copia del runner fuori dal repo è fallita all'avvio (ricavava la root dal proprio percorso) | Percorsi espliciti e assoluti |
| 30/09 00:00 | 5 script "fermati" con TaskStop erano ancora vivi; a mezzanotte hanno lanciato run duplicate nella stessa cartella | Un job esiste solo nello store; cancel transazionale; kill dell'albero con verifica, solo per PID |
| 30/09 | Due code hanno archiviato la stessa cartella in contemporanea | Un solo proprietario per run dir; `NO_ARCHIVE` nelle run attive |
| 30/09 | L'archiver ha saltato la cartella ma ha restituito 0, e il chiamante ha registrato un successo | Esiti distinti per "saltato" e "fatto"; successo solo con verdetto esplicito |
| 25/09 | Un lancio inline con `cmd start` è fallito in silenzio: macchina ferma per 1,5 h | Il backend verifica entro pochi secondi che lo step sia vivo e abbia scritto `start` |
| 25/09 | L'app desktop Claude usava 0,3-0,5 core e falsava i tempi | Registrare la CPU di sistema durante le run |
| 22/09 | Un CRLF scritto da Python in un file letto da bash ha rotto una coda | Store strutturato (SQLite) |
| 28/09 | Un percorso `/c/...` passato a Python Windows ha dato "vs monker na" | Solo percorsi nativi |
| 16/09 | Un viewer in esecuzione teneva bloccato un exe e il link successivo è fallito (LNK1104) | Le build non toccano mai gli exe usati dalle run |
| 30/09 sera | 7+ processi solver concorrenti: da 100 a 200 ms per iterazione invece di 60 | Budget globale di thread |
| 21-22/09 | Baseline HU30 con 4,84 GB di swap | Admission sulla RAM libera, con la formula del trainer |
| sempre | `bin_correct/base` ignora `postflop_betting_streets` e addestra un albero diverso in silenzio | Guardia sul `tree_fingerprint` |
| codice | Il trainer cancella un `it_<N>` esistente prima di scrivere lo snapshot nuovo | Valutazioni fuori da `charts/`; procedura "superseded" prima di ogni resume |
| codice | Il trainer cancella il file di stop all'avvio | Intento di stop nel DB, riscritto dopo `start` |
| codice | `monker_values` scrive `values.json` anche quando fallisce | Successo = exit 0 + `PASS` + `self_checks.passed` |
| codice | Nessuno strumento regge le colonne `*_EV` dei chart Monker | Copie ripulite in cache |

---

## Appendice J: GTO-Chart-Browser, parti riusabili [V]

**Stack**: React 19, Vite 8, TypeScript 5 strict, Tailwind 4, Vitest (`CHARTS/app/package.json`). Si copia, non si modifica.

**File riusabili**
- **`src/lib/constants.ts`**:
  - `RANKS` (A..6);
  - `getMatrixLabel(row, col)`: coppie sulla diagonale, suited in alto a destra, offsuit in basso a sinistra;
  - `MATRIX_COMBOS`;
  - colori delle azioni; `getActionColorInSpot`; `POSITION_ORDER`.
- **`src/components/HandMatrix.tsx`**:
  - props `{strategy, title, expanded?}`;
  - griglia CSS 9x9;
  - tooltip con % ed EV;
  - legenda con `summary` %, `summaryEv`, `totalEv`.
- **`src/lib/range-utils.ts`**:
  - `getCellBackground` (gradient con le frequenze);
  - `buildGrid()`;
  - `copyRangeForAction` (formato `[63.0]AKo[/63.0]`).
- **`src/components/FreqBar.tsx`**.
- **`src/components/ActionPathBuilder.tsx`** e **`src/lib/action-tree.ts`**:
  - breadcrumb, undo, troncamento, anteprima di un passo;
  - chiavi `${player}_${action}`.
  - Dipendono dalla loro API Vercel: alimentali dal backend locale.
- **`src/lib/spot-format.ts`**:
  - `displayActionName` (`Raise 5a`), `formatSpot`;
  - `formatEV` etichetta come "bb": correggilo in "a".
- **`scripts/build-ranges.ts`**: esempio di parser.
  - Legge CRLF e LF.
  - Ignora `*_EV` come azioni.
  - Normalizza per `Total`.
  - **Pota i rami con frequenza 0** e non rappresenta i fold impliciti. La nostra UI non deve potare in silenzio: mostra i rami a 0 come "mai giocato".
