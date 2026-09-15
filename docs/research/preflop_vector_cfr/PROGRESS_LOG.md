# Diario dell'agent coder: solver preflop vettoriale

Roadmap: [PREFLOP_VECTOR_CFR_CODER_ROADMAP_2026-09-15.md](../PREFLOP_VECTOR_CFR_CODER_ROADMAP_2026-09-15.md)
Registro decisioni: [PREFLOP_ARCHITECTURE_DECISION_LOG.md](../PREFLOP_ARCHITECTURE_DECISION_LOG.md)

Regole del diario: le voci non si cancellano; una correzione è una nuova voce che rimanda alla
precedente. Un fallimento si registra prima di tentare la correzione. Aggiornare a fine di ogni
sessione, a ogni gate e a ogni dubbio bloccante.

## 1. Stato corrente

| Campo | Valore |
|---|---|
| Fase in corso | P5 (kernel vettoriale HU), in avvio |
| Ultimo gate | P4 PASS (2026-09-15) |
| Branch di integrazione | `feature/preflop-blueprint` |
| Branch di fase | `feature/preflop-blueprint-p5-kernel` (P0–P4 uniti nell'integrazione) |
| Worktree | `C:/tmp/gtosd-preflop-blueprint` |
| Commit di partenza | `main` a `55ed6ef`; il tag `preflop-legacy-es-2026-09-15` è su `04aa687` |
| Build | `out/build/windows-release` nel worktree (Release, MSVC 19.51, Ninja 1.13.2) |
| Merge su `main` | in attesa dell'utente (Q2): il gate P3 prevede il merge dell'integrazione in `main` con tag, ma `main` è il branch del working tree dell'utente e l'agent non lo tocca |
| Prossimo passo | P5: kernel vettoriale HU su un board fisso (fold, showdown con blocker in n log n, all-in preflop dalla tabella esatta), reach a N vettori, test contro il calcolo diretto 465×465 |

## 2. Registro dei gate

| Fase | Esito | Data | Commit | Report |
|---|---|---|---|---|
| P0 Contratto e scaffolding | PASS | 2026-09-15 | `ef6f691` | [P0_SCAFFOLDING.md](P0_SCAFFOLDING.md) |
| P1 Canonicalizzazione e cataloghi | PASS | 2026-09-15 | `ca80dab` | [P1_CANONICAL_BOARDS.md](P1_CANONICAL_BOARDS.md) |
| P2 Risorse esatte | PASS | 2026-09-15 | `9f8a6d3` | [P2_EXACT_RESOURCES.md](P2_EXACT_RESOURCES.md) |
| P3 Clustering e tabelle bucket | PASS | 2026-09-15 | `c7bb762` | [P3_BUCKET_TABLES.md](P3_BUCKET_TABLES.md) |
| P4 Modello di gioco e albero compilato | PASS | 2026-09-15 | `89f159e` | [P4_GAME_MODEL.md](P4_GAME_MODEL.md) |
| P5 Kernel vettoriale HU | NOT_RUN | | | |
| P6 Trainer con campionamento del board | NOT_RUN | | | |
| P7 Certificatore board-major | NOT_RUN | | | |
| P8 Export, query, comparatore, viewer | NOT_RUN | | | |
| P9 Qualificazione CO40 e archiviazione | NOT_RUN | | | |
| P10 Conteggio alberi 3-way | NOT_RUN | | | |

Esiti ammessi: `PASS`, `FAIL`, `INCONCLUSIVE`, `NOT_RUN`.

## 3. Diario

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
| Q1 | 2026-09-15 | I branch di fase vengono uniti nell'integrazione con merge locali `--no-ff`; per aprire pull request su GitHub servirebbe il push dei branch su origin. Si pubblicano i branch su origin oppure restano merge locali fino ai gate di `main`? Nel frattempo si procede con merge locali. | aperta | |
| Q2 | 2026-09-15 | D21 prevede il merge dell'integrazione in `main` al gate P3 con tag. `main` è il branch checked-out nel working tree dell'utente (`C:/Users/GoryNickel/Documents/GitHub/GTO-Solver`): git non permette di farne il checkout in un secondo worktree e spostarne il ref da fuori lascerebbe il working tree dell'utente in uno stato incoerente. Comandi proposti, da eseguire nel working tree dell'utente con `main` pulito: `git merge --no-ff feature/preflop-blueprint -m "merge(preflop-blueprint): P0-P3 card abstraction, gate P3 PASS"` poi `git tag -a preflop-blueprint-p3-abstraction -m "P3 gate PASS"`. Prima del merge l'agent esegue la suite CTest completa sull'integrazione e ne registra l'esito. In alternativa l'utente può autorizzare l'agent a eseguire i due comandi nel suo working tree. Nel frattempo P4 procede sull'integrazione. | aperta | |

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
