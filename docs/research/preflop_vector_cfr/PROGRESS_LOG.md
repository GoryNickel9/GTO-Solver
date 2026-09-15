# Diario dell'agent coder: solver preflop vettoriale

Roadmap: [PREFLOP_VECTOR_CFR_CODER_ROADMAP_2026-09-15.md](../PREFLOP_VECTOR_CFR_CODER_ROADMAP_2026-09-15.md)
Registro decisioni: [PREFLOP_ARCHITECTURE_DECISION_LOG.md](../PREFLOP_ARCHITECTURE_DECISION_LOG.md)

Regole del diario: le voci non si cancellano; una correzione è una nuova voce che rimanda alla
precedente. Un fallimento si registra prima di tentare la correzione. Aggiornare a fine di ogni
sessione, a ogni gate e a ogni dubbio bloccante.

## 1. Stato corrente

| Campo | Valore |
|---|---|
| Fase in corso | P3 (clustering e tabelle bucket), in avvio |
| Ultimo gate | P2 PASS (2026-09-15) |
| Branch di integrazione | `feature/preflop-blueprint` |
| Branch di fase | `feature/preflop-blueprint-p3-clustering` (P0–P2 uniti nell'integrazione) |
| Worktree | `C:/tmp/gtosd-preflop-blueprint` |
| Commit di partenza | `main` a `55ed6ef`; il tag `preflop-legacy-es-2026-09-15` è su `04aa687` |
| Build | `out/build/windows-release` nel worktree (Release, MSVC 19.51, Ninja 1.13.2) |
| Prossimo passo | P3: k-means (EMD sugli istogrammi flop/turn, L2 sull'OCHS river), tabelle bucket per board canonico, lookup a tempo costante, diagnostica di occupazione e dispersione |

## 2. Registro dei gate

| Fase | Esito | Data | Commit | Report |
|---|---|---|---|---|
| P0 Contratto e scaffolding | PASS | 2026-09-15 | `ef6f691` | [P0_SCAFFOLDING.md](P0_SCAFFOLDING.md) |
| P1 Canonicalizzazione e cataloghi | PASS | 2026-09-15 | `ca80dab` | [P1_CANONICAL_BOARDS.md](P1_CANONICAL_BOARDS.md) |
| P2 Risorse esatte | PASS | 2026-09-15 | `9f8a6d3` | [P2_EXACT_RESOURCES.md](P2_EXACT_RESOURCES.md) |
| P3 Clustering e tabelle bucket | NOT_RUN | | | |
| P4 Modello di gioco e albero compilato | NOT_RUN | | | |
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
