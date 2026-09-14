# Piano di ottimizzazione del prodotto: tempo e iterazioni

Data: 2026-09-05. Stato al 2026-09-09: **R9-C in corso; decomposizione HU preflop exact, resource model, boundary CFV, valutazione del profilo, best response riprendibile e certificazione globale sono implementati e validati sul percorso strutturale. Il solve convergente del benchmark non è ancora certificato e nessun default di prodotto è cambiato**.

**Ordine eseguito, roadmap v7:** P0 profilo comune → P1 baseline e verifiche → P1-S astrazione/bucketing e subgame con ProductionDcfr → profiling R3 → pre-gate R4 → fattibilità R6. Nessun ramo ha prodotto un candidato D/V, quindi qualificazione H/R7 e gate finali R8 non sono stati avviati. Restano invariati l'esclusione dello startup, il vincolo solo DCFR e la parità RAM obbligatoria su scope equivalente.

**Requisito RAM aggiornato, roadmap v4:** pareggiare o migliorare GTO+ anche nella memoria, sullo stesso gioco e perimetro di misura, è obbligatorio per chiudere l'obiettivo. Un beneficio temporale non compensa il mancato gate RAM. I riferimenti display sono 8/399/2.000 MB; non sono cap RSS automaticamente applicabili. Il nuovo pre-gate R0-M della roadmap definisce la verifica di comparabilità e la misura omogenea della RAM effettiva dei due prodotti. Scope irrisolto significa risultato complessivo non ancora qualificato, non esenzione dal requisito.

**Contratto temporale aggiornato per decisione dell'utente:** l'avvio dell'applicazione/processo è escluso dai confronti e dai gate prestazionali. In questo piano, il wall operativo è il tempo da Build Tree a soluzione consultabile, con applicazione già pronta e configurazione disponibile, senza pause dell'utente. Il tempo da Run Solver con albero pronto è una seconda misura distinta. Il wall storico dell'intero processo resta solo evidenza diagnostica, non una soglia operativa.

Esecuzione e diramazioni: [roadmap operativa dettagliata](PRODUCTION_DCFR_PRODUCT_OPTIMIZATION_ROADMAP_2026-09-05.md).

**Decisione successiva, roadmap v2:** solo ProductionDcfr nel prodotto, inclusi i resume compatibili. Checkpoint CFR+ non riprendibili: eventuale recupero della configurazione avvia un nuovo solve DCFR da zero. Le alternative algoritmiche descritte sotto restano ricerca archiviata fuori dal mandato corrente. Astrazione, bucketing e subgame possono invece essere progettati intorno a DCFR, con gate dedicati. Il primo riferimento GTO+ AHK è 1,71 s da Run Solver con albero pronto, non il wall dell'intero processo: vedere la distinzione dei timer nella roadmap.

Baseline del codice ispezionato: `ffb208a`, revert con tree equivalente a `c26e1f8`.
Ambito: solver desktop offline, CPU, qualificazione a otto thread, ottimizzazioni condivise dal prodotto. Non ottimizzare AHK, TH o TST come casi speciali.

## 1. Decisione e obiettivo

Manteniamo ProductionDcfr. Prima di cambiare ulteriormente la matematica dobbiamo rendere coerenti i percorsi applicativi, misurare il costo attuale e separare due problemi:

1. **Costo per iterazione e costo esterno al training:** layout, inizializzazione, codec, traversata, certificazione, scheduling, analisi e persistenza.
2. **Iterazioni necessarie alla stessa accuratezza:** dipendono dalla dinamica algoritmica, dalla strategia media e dalla metrica; non diminuiscono automaticamente rendendo il kernel più veloce.

Il vincolo richiesto è congiunto: nessun benchmark di regressione deve peggiorare in iterazioni, mediana solver o mediana wall. Per una vera accelerazione della convergenza vogliamo meno iterazioni, senza nascondere lavoro aggiuntivo in una singola iterazione. Un intervento infrastrutturale può essere utile con iterazioni invariate, ma non va presentato come riduzione delle iterazioni.

Non è dimostrato oggi che un candidato soddisfi contemporaneamente tutti questi obiettivi. I risultati dei paper sono ipotesi da verificare sul prodotto, non prestazioni trasferibili.

## 2. Contratto attuale e baseline storica

Autorità: [algoritmi](specifications/SOLVER_ALGORITHMS.md), [qualificazione gamma3](DCFR_EPOCH_RESET_GAMMA3_FEASIBILITY_2026-09-01.md), [performance](specifications/PERFORMANCE.md), [parity journey](GTO_PLUS_PARITY_JOURNEY.md).

Il percorso qualificato usa:

- `ProductionDcfr`, enum persistito `11`, signed alternating DCFR `alpha=1.5`, `beta=0`, `gamma=3`;
- reset dell'averaging alle iterazioni one-based `1,2,5,17,65`, nessun reset successivo;
- clock regret `t-1` fino a 65, `t-2` successivamente; peso della media cubico nel relativo epoch;
- combo e chance outcome enumerati, isomorfismi solo lossless compatibili con range e board;
- `ScaledUint16RegretStrategy`, certificazione esatta ogni 20 iterazioni, target stretto dEV/pot `<1%` per le fixture correnti;
- configurazione benchmark `parallel_action_depth=7`, massimo otto thread.

Questa non è la schedule DCFR originale `1.5/0/2`. Non è neppure identica alla schedule upstream di b-inary per ogni iterazione. Cambiarla richiede identificazione e compatibilità checkpoint esplicite.

I moduli di bucketing/resolving sono stati reintrodotti come primitive sperimentali versionate e isolate. Non sono collegati al percorso product, che resta exact. CFR+ di riferimento preesistente resta disponibile: la sua presenza non significa che debba tornare algoritmo del percorso production.

### Valori da non superare

Cinque processi final-head storici `r2-r6`, riportati nel documento gamma3. **Non sono benchmark rieseguiti sul revert il 5 settembre.** I tempi includono le rispettive definizioni originali di solver e processo: non sono intercambiabili.

| Benchmark | Iterazioni | dEV/pot | Solver mediana / p95 (s) | Wall processo storico, solo diagnostico, mediana / p95 (s) | Stato (B) | Peak RSS massimo (B) |
|---|---:|---:|---:|---:|---:|---:|
| AHKHQH | 80 | 0,951423% | 0,758705 / 0,790918 | 6,231883 / 6,468465 | 5.300.664 | 166.645.760 |
| TH7D6S | 80 | 0,807956% | 19,948228 / 24,192260 | 35,208170 / 41,015117 | 334.452.416 | 799.043.584 |
| TSTC9D | 160 | 0,904505% | 184,095930 / 197,865030 | 208,403423 / 221,888253 | 1.472.605.376 | 1.969.860.608 |

Restano vincolanti i tetti storici di iterazioni e solver a parità di perimetro. I valori wall dell'intero processo restano visibili ma non sono più tetti di accettazione, perché includono attività escluse dal nuovo contratto. Rilevare una baseline operativa B0 senza startup sul medesimo hardware, con binario baseline e candidato misurati agli stessi confini. Non ricavare il nuovo tempo sottraendo una stima dell'avvio o due mediane storiche. Finché manca B0, il confronto wall operativo è NOT_EVALUATED, non PASS. Eventuali regressioni vanno attribuite prima di cambiare la baseline.

Per ciascun giocatore `g_i = BR_i - V_i(profile)`. dEV normalizzato usa il massimo guadagno; NashConv normalizzato usa la somma. Non usare una conversione costante fra le due metriche. Con guadagni non negativi, `max(g_i) <= sum(g_i) <= 2 max(g_i)`. Rake e giochi non zero-sum richiedono la propria interpretazione: non trasferire automaticamente garanzie teoriche HU zero-sum.

La RAM va separata in stato, payload/capacità solver-owned, scratch, memoria del processo e persistenza. Gli `8/399/2000 MB` GTO+ non sono Peak RSS né un cap desktop. Confronto memoria esterno: `NOT_EVALUATED_COMPARABILITY_UNRESOLVED`. Per nuovi candidati riportare incrementi rispetto alla baseline e stimare la fattibilità sul computer disponibile; nessun limite RAM arbitrario introdotto qui.

## 3. Audit: esistono ottimizzazioni specifiche per benchmark?

### Risultato circoscritto

La ricerca di ID, nomi board e riferimenti fixture in `libs`, `include`, `apps`, seguita dalla lettura dei punti individuati e del runner PowerShell, **non ha individuato dispatch del core sui tre ID/nomi benchmark**. Non equivale a una prova esaustiva di indipendenza: costanti, profili numerici e configurazioni differenti possono introdurre specializzazione senza contenere un nome.

| Punto verificato sul codice ripristinato | Evidenza | Valutazione |
|---|---|---|
| `libs/postflop/src/postflop_solver.cpp:3249` | Commento TH sulla memoria evitata; condizione basata su layout/isomorfismi e automorfismi dei range | Ottimizzazione strutturale generale, non selezione per board |
| Stesso file, `:7233` | Commento TH; `add_strategy` evita update quando `value == 0.0` | Condizione numerica generale; verificare invarianti anche fuori fixture |
| Stesso file, `:18023` | Delta differito allocato se necessario al percorso isomorphic/non-direct | Riduzione di memoria applicabile a tutti i layout aventi quella proprietà |
| Stesso file, `:9341` | Esperimento simultaneous respinto, dentro `#if 0` | Evidenza storica, non percorso production attivo |
| `apps/gto_cli/main.cpp:2033` | Runner legacy v1 vincolato a `GTP-AHKHQH-003` e vecchi parametri | Specializzazione del validatore legacy, non un fast path del solver; non usarlo per qualificare il prodotto corrente |
| `tools/run_gto_plus_convergence_benchmark.ps1` | Fixture di default, validazione schema v4, più processi e aggregazione | Selezione di test legittima; non deve decidere l'algoritmo in base all'ID |
| `production_dcfr_schedule`, core `:16497` | Limite numerico globale dell'epoch a 64 | Nessun ID; resta una scelta empirica da validare su holdout, non una prova di generalizzazione |

### Problema reale: benchmark e prodotto non usano ancora lo stesso profilo

L'allineamento dichiarato nei documenti non è uniforme fra gli entry point:

- `include/gtosd/postflop/postflop_solver.hpp:267`: i default di `PostflopSolveOptions` sono **CfrPlus, Float64, parallel_action_depth=0**.
- `apps/gto_cli/main.cpp:286`, solve ordinario: costruisce tali opzioni senza selezionare ProductionDcfr o il profilo compresso/parallelo del benchmark.
- `apps/gto_gui/product_window.cpp:1432`: il percorso GUI imposta averaging delay 20, profondità parallela 5, Float32 oppure Float64/out-of-core e non assegna l'algoritmo; eredita CFR+.
- `apps/gto_cli/main.cpp:1106`, runner benchmark: assegna esplicitamente algoritmo, codec, esponenti e profondità dalla specifica. Le fixture correnti selezionano ProductionDcfr e profondità 7.

Questo è un **disallineamento di prodotto confermato staticamente**, non la prova che il core contenga una scorciatoia per TH/TST. Né basta a spiegare tempi di due benchmark che passano entrambi dal medesimo runner. Va corretto prima di sostenere che un miglioramento della suite raggiunga anche l'utente desktop. Non cambiare tutti i default della libreria alla cieca: toy test, strumenti diagnostici, backend e resume hanno contratti diversi.

### Audit da rendere permanente

1. Inventariare tutti i call site di solve/prepare/resume, le variabili `GTOSD_*`, i flag compile-time e ogni selettore algoritmo/precisione. I percorsi research devono restare espliciti, separati e non attivati accidentalmente dall'ambiente.
2. Cercare anche board mask, fingerprint, dimensioni esatte degli alberi, soglie hardcoded, nomi di file e directory. Distinguere un controllo di integrità da un selettore di prestazioni.
3. Rinominare ID e percorso a parità di configurazione: kernel, opzioni risolte e risultati matematici devono restare uguali. I metadati del report possono cambiare.
4. Variare board, range pesati/asimmetrici, sizings e stack intorno alle fixture. I rami ottimizzati devono essere spiegabili da proprietà del gioco, non da una firma del test.
5. Testare permutazioni lecite dei semi e riordinamenti di input; confrontare risultati rimappati con tolleranze dichiarate. Un hash stabile può servire a validare una cache, mai a riconoscere una soluzione precalcolata.
6. Registrare nel report il profilo effettivo risolto dal prodotto, non soltanto i parametri richiesti dal runner.

## 4. Letteratura e codice open source: cosa aggiungono davvero

Fonti primarie consultate il 2026-09-05: paper completi accessibili, sezioni algoritmiche/sperimentali pertinenti e file sorgenti indicati sotto. Nessun solver esterno è stato compilato o benchmarkato in questo audit. I link `master` sono mobili: prima di un confronto eseguibile fissare SHA, hash dei file, toolchain e licenze nel manifest sperimentale; questo documento non attribuisce loro un commit non verificato.

### 4.1 DCFR, schedule e predizione

- **Brown e Sandholm, DCFR (AAAI 2019):** discount separato dei regret e pesatura della media sono già parte del nostro motore. Il paper mostra che la compatibilità pratica con pruning dipende anche da beta; non giustifica applicare a beta=0 ogni regola di pruning per CFR classico. Fonte: [paper DCFR](https://arxiv.org/html/1809.04040v3).
- **DDCFR, ICLR 2024:** apprende una politica di discount da informazioni runtime e valuta generalizzazione a giochi non usati nel training. Possibile linea di ricerca, ma introduce training, policy, feature e costo di inferenza da contabilizzare. Non addestrare sui tre benchmark di accettazione. Fonte: [paper completo](https://proceedings.iclr.cc/paper_files/paper/2024/file/a89331e2ecbbd9d9618bc94717a91155-Paper-Conference.pdf), [repository degli autori](https://github.com/rpSebastian/DDCFR).
- **Faster Game Solving via Hyperparameter Schedules:** propone schedule variabili senza la stessa pipeline appresa. Nel nostro header esiste già `HsDcfr30`: non è una funzionalità da annunciare come nuova. Prima leggere le prove locali di qualificazione e capire quali varianti sono realmente mancanti. Fonte: [paper](https://arxiv.org/html/2404.09097v2).
- **Stable-Predictive Optimistic CFR (ICML 2019):** distingue miglioramento teorico e vantaggio pratico; negli esperimenti poker non supera DCFR. Un rate asintotico migliore non garantisce wall inferiore a 80–160 iterazioni. Fonte: [paper completo](https://proceedings.mlr.press/v97/farina19a/farina19a.pdf).
- **Weighted regret con optimistic OMD / PDCFR+:** combinare predizione e pesatura è una possibilità per diminuire le iterazioni, ma cambia algoritmo e stato; verificare memoria e costo per update prima del solve completo. Fonte: [paper completo](https://arxiv.org/pdf/2404.13891).

Decisione: niente nuovo sweep indiscriminato di gamma/reset sulle tre fixture. DDCFR/predittivi sono candidati separati, non modifiche silenziose a ProductionDcfr.

### 4.2 Fare meno lavoro utile: lazy, pruning e subgame

- **Lazy-CFR, ICLR 2020:** aggiorna selettivamente segmenti di iterazioni, accumulando informazione fra gli update. Non consiste nel saltare una visita senza conservarne il contributo. Misurare update/nodi e traversate equivalenti oltre alle iterazioni nominali. Fonte: [paper e algoritmo](https://ml.cs.tsinghua.edu.cn/~jun/pub/lazy-cfr.pdf).
- **Reduced Space and Faster Convergence via Pruning, ICML 2017:** pruning e risparmio di stato richiedono condizioni matematiche, non una soglia arbitraria sul regret. Nel prodotto c'è telemetria `RbpReadOnlyTelemetry`; osservare candidati non autorizza a saltarli. Fonte: [paper degli autori e PDF collegato](https://proceedings.mlr.press/v70/brown17a.html).
- **Safe and Nested Subgame Solving, NeurIPS 2017:** un subgame non si risolve in sicurezza usando soltanto range locali; servono vincoli/valori al confine e una strategia di riferimento appropriata. Blueprint, costruzione dei boundary e certificazione contribuiscono al costo totale. Fonte: [paper completo](https://noambrown.github.io/papers/17-NIPS-Safe.pdf).

Decisione: astrazione/bucketing e subgame entrano nella fase architetturale dopo la baseline, prima delle ottimizzazioni profonde. Non promettono automaticamente meno iterazioni del solve root e non comportano ripristino automatico dei sorgenti eliminati dal revert. Lazy/pruning restano ulteriori candidati soggetti a fattibilità e compatibilità DCFR.

### 4.3 Due implementazioni reali ispezionate

**b-inary/postflop-solver (Rust).** Il [README](https://raw.githubusercontent.com/b-inary/postflop-solver/master/README.md) e il [solver](https://raw.githubusercontent.com/b-inary/postflop-solver/master/src/solver.rs) mostrano DCFR gamma3, reset a potenze di quattro, update alternati, codec signed/unsigned a 16 bit, trattamento degli isomorfismi, somme chance in float64 e allocator opzionale per scratch. Molte idee hanno già corrispettivi locali: non basta portarne i nomi. Il limite locale dei reset a 65 è una differenza da documentare. La frequenza esterna di certificazione e la metrica exploitability non vanno importate come se fossero il nostro gate dEV.

**TexasSolver (C++).** Nel [DiscountedCfrTrainable.cpp](https://raw.githubusercontent.com/bupticybee/TexasSolver/master/src/trainable/DiscountedCfrTrainable.cpp), `getcurrentStrategyNoCache` normalizza la parte positiva; `updateRegrets` aggiunge il regret istantaneo e poi sconta in base al nuovo segno. L'averaging usa propri coefficienti e il parametro reach non è moltiplicato nella riga attiva mostrata. È un motivo per confrontare convenzioni e ordine delle operazioni, non per copiare la formula come equivalente alla nostra. Ispezione limitata a questo componente: nessuna certificazione della correttezza dell'intero solver.

Entrambi i file LICENSE consultati contengono GNU AGPL v3: [b-inary](https://raw.githubusercontent.com/b-inary/postflop-solver/master/LICENSE), [TexasSolver](https://raw.githubusercontent.com/bupticybee/TexasSolver/master/LICENSE). Prima di incorporare codice serve una verifica di compatibilità delle licenze. Qui prendiamo spunti architetturali e non importiamo sorgenti.

## 5. Non ripetere gli esperimenti già chiusi

La cronologia in [PERFORMANCE](specifications/PERFORMANCE.md) distingue:

| Famiglia | Evidenza locale | Conseguenza per il prossimo lavoro |
|---|---|---|
| Decode/update/global-scale/encode byte-identico | Il passaggio di scala globale è strutturale; piccoli guadagni degli shadow già insufficienti | Non riproporre fusion/streaming senza un ceiling nuovo e un replay reale |
| Tile-local scale, bfloat16, rappresentazioni alternative | Alcuni candidati più lenti, altri con drift numerico | Il ritiro del cap RAM non sana né la lentezza né l'errore |
| S6 / schedule precedenti | Qualificazione condivisa non superata | Non promuovere una curva favorevole isolata |
| FD-FTRL/OMD | Costo locale misurato maggiore del regret matching | Richiede abbastanza iterazioni risparmiate per compensarlo; modello aggiornato prima del solve |
| Lazy-CFR | Accumulatore float32 minimo TST stimato a 582.096.608 B; struttura completa più grande | Esclusione per il vecchio cap ritirata: ricalcolare lo stato completo, non dichiarare impossibilità generale |
| Predictive-CFR | Stato predittivo aggiuntivo non eliminabile conservando gli altri payload | Rivalutare con ledger e risorse reali, senza presumere RAM invariata |
| Range-aware physical orbit | Controesempi e gate research separati | Non riattivare per guadagnare tempo senza risolvere la correttezza |

Riferimenti: [lazy](EXACT_LAZY_CFR_FEASIBILITY_LOOP_2026-08-31.md), [predictive](EXACT_PREDICTIVE_CFR_FEASIBILITY_LOOP_2026-08-31.md), [FD-FTRL/OMD](MEMORY_NEUTRAL_FD_FTRL_OMD_FEASIBILITY_LOOP_2026-08-31.md), [rappresentazione](NEW_PRODUCTION_STATE_REPRESENTATION_LOOP_2026-08-31.md), [replay](REAL_NODE_REPLAY_AND_PRODUCER_LOWER_BOUND_LOOP_2026-08-31.md).

## 6. Piano operativo prioritizzato

### P0 — Un solo profilo production realmente condiviso

Interventi proposti, non effettuati in questo documento:

1. Introdurre una funzione di risoluzione del profilo production condivisa da CLI, GUI e benchmark. Può risiedere nel modulo postflop; nome/API da definire dopo inventario dei caller.
2. Rendere espliciti algoritmo, precisione, averaging, certificazione, target e policy thread. Registrare i valori risolti nei report e nei progetti. Il runner deve invocare la stessa configurazione del prodotto.
3. Per solve nuovi, selezionare il profilo qualificato ProductionDcfr. Riprendere soltanto checkpoint ProductionDcfr compatibili, conservando stato e clock salvati. Rifiutare la ripresa CFR+; eventuale importazione della configurazione avvia un solve DCFR da zero senza conversione dello stato.
4. Verificare backend: se il codec/profilo non è supportato, errore o scelta esplicita dell'utente, mai fallback nascosto. GUI deve esporre la distinzione fra profilo production e riferimento/ricerca.
5. Unificare la policy a otto thread per qualificazione. Verificare thread effettivi e oversubscription, anche durante BR e checkpoint. La profondità di traversal non è da sola una misura dei core attivi.

File coinvolti: header/core postflop, `apps/gto_cli/main.cpp`, `apps/gto_gui/product_window.cpp`, serializzazione progetto/checkpoint e relativi test. Evitare un cambiamento globale dei default che rompa i caller di riferimento.

**Exit gate:** configurazione equivalente produce la stessa traiettoria matematica attraverso API, CLI e percorso GUI; resume riproduce il solve non interrotto; test backend e input incompatibili. Questo corregge il prodotto, ma non conta come accelerazione rispetto alla baseline benchmark che già usa ProductionDcfr.

### P1 — Rebaseline e attribuzione causale

Prima un processo diagnostico per configurazione; poi baseline a cinque processi indipendenti senza build o altri solve in parallelo. Registrare commit, hash binario/fixture, compiler e flag, CPU, RAM libera, carico, seed, thread, backend e variabili diagnostiche.

Strumentare intervalli non sovrapposti per ottenere:

`T_wall_operativo = T_build_prepare + T_training + T_certification + T_finalize_consultable`.

Il timer parte con applicazione pronta e configurazione disponibile, al comando Build Tree; termina alla soluzione consultabile. Escludere startup, attese dell'utente e chiusura del processo. Includere tutto il lavoro dipendente dal gioco richiesto, anche se eseguito in modo lazy: non spostare preparazione/rank/cache del caso prima del timer per nasconderne il costo. Se persistenza o analisi sono necessarie alla soluzione consultabile, appartengono al totale; export/salvataggi opzionali si misurano separatamente. Misurare inoltre build e solve-to-consultable separatamente. La scomposizione additiva richiede fasi non sovrapposte; in caso di overlap usare l'intervallo reale end-to-end. Lo startup può essere registrato solo come diagnostica separata, mai incluso nel gate.

Definire poi esattamente quali termini siano inclusi nel timer pubblico `solver_seconds`: non sommare fasi già incluse. La differenza fra due mediane non è la mediana di una fase.

Rilevare: visite/update effettivi, byte letti/scritti stimati e misurati dove possibile, scale changed/unchanged, conversioni, showdown, chance, lavoro utile dei worker, attese, allocazioni, page fault, RSS e ledger. Profiling strumentato separato dalla misura finale non strumentata.

Per ogni candidato applicare il modello Amdahl `S <= 1 / ((1-f)+f/s)` usando la frazione attuale `f`. Per convergenza usare `T_new = T_fixed_new + N_new*C_new + T_cert_new`: un metodo con update doppiamente costoso non vince dimezzando soltanto una parte del lavoro. Non ereditare percentuali dei vecchi profili senza aggiornarle.

**Exit gate:** tabella di attribuzione per tutte le fixture e per almeno un corpus holdout; una sola ipotesi dominante scelta tramite costo totale e rischio.

### P1-S — Architettura: astrazione/bucketing e subgame con DCFR

Equivalente a R2-S nella roadmap; segue P0/P1 e precede P2/P3. Usare la baseline per scegliere metrica, granularità, decomposizione e budget di errore su corpus D/V, non per richiedere preventivamente che il motore attuale abbia già chiuso i gate prestazionali.

1. **Specificare e modellare:** distinguere action abstraction e bucketing delle carte; definire policy globale/versionata, blocker, pesi, storia informativa/perfect recall e identità dei file. Per il subgame definire blueprint DCFR, boundary CFV, vincoli e sicurezza. Misurare costo e RAM di costruzione, non solo del training.
2. **Implementare separatamente il bucketing:** partire dai giochi ridotti, verificare mapping identità contro riferimento non astratto, poi granularità differenti. Valutare la strategia nel gioco originale: convergenza all'1% nel solo gioco astratto non è accettazione.
3. **Implementare separatamente il subgame:** partire dal riferimento non astratto, verificare estrazione, boundary, resolve DCFR, splice, zero reach e persistenza; BR globale su piccoli giochi e costo totale di blueprint/resolve/verifica.
4. **Combinare solo dopo le prove isolate:** confrontare DCFR base, bucket soltanto, subgame soltanto e combinazione. Misurare errori e memoria contemporanea; non sommare guadagni teorici. L'ordine dei due prototipi dipende dalla fattibilità, non vi è dipendenza obbligatoria fra loro.
5. **Scegliere l'architettura candidata e riprofilare:** se corretta e con margine plausibile ma ancora fuori target, proseguire sperimentalmente a P2. Nessuna promozione fino ai gate finali. Se una tecnica fallisce, dichiararla incompleta/scartata e motivare l'alternativa; se nessuna è fattibile, riportare il blocker e chiedere una decisione invece di indebolire accuratezza o RAM.

La comparabilità RAM GTO+ deve essere investigata già in P1; se irrisolta non blocca i prototipi, ma impedisce il PASS complessivo. Conservare la baseline originaria e registrare separatamente quella sperimentale, senza cambiare i tetti per assorbire regressioni. Nuovi profili espliciti nel prodotto, checkpoint versionati e nessuna scelta K per benchmark.

### P2 — Ottimizzare il costo dell'architettura risultante

Prerequisito: esito P1-S documentato. Le ipotesi sotto si applicano soltanto ai costi ancora presenti nel profilo aggiornato; non ottimizzare profondamente un layout destinato a essere sostituito prima della valutazione architetturale. DCFR resta il motore, mentre rappresentazione e decomposizione sono dichiarate nei profili.

| Ipotesi | Esperimento minimo | Invarianti e motivo di stop |
|---|---|---|
| Preparazione/analisi duplicata fra prodotto e solver | Tracciare `PostflopPreparedTree`, layout analisi e dati rank; contare build duplicate | Riutilizzo solo con identità completa di ruleset, tree e range. Stop se già riusato o costo irrilevante |
| Cache persistente di metadati immutabili | Prototipo limitato a evaluator/board/rank o layout verificati, checksum e invalidazione | Separare cold e warm; includere creazione/caricamento. Nessuna cache di risultati precalcolati delle fixture |
| Float32 direct al posto della quantizzazione globale | Riaprire solo il pre-gate memoria ormai corretto; replay real-node e multi-step | È un nuovo profilo numerico, non byte-identico. Misurare RAM e drift prima di un eventuale test completo; nessuna promozione implicita |
| Minori allocazioni/scratch e passate ridondanti | Profilo allocation/lifetime; nuova ipotesi non già falsificata dagli studi di fusion | Riduzioni deterministiche, lifetime corretti, nessuno stato aggiuntivo nascosto. Stop se il ceiling non paga il costo |
| Migliore distribuzione del lavoro a otto thread | Distribuzione strutturale per quantità di lavoro, non nomi board; trace worker | Stesso update alternato e riduzioni controllate; contare task e overhead. Non ripetere batching/treelet già bocciati senza nuova evidenza |
| Certificazione e analisi meno costose | Profilare runner BR/profile e confrontare output indipendenti | Certificazione già parallela e con ottimizzazioni zero-sum: non proporre semplicemente di parallelizzarla. Nessuna cache di BR fra strategie diverse |

La linea Float32 può ridurre tempo aumentando RAM: non soddisfa da sola un obiettivo di RAM inferiore. Decisione esplicita su un profilo di prodotto alternativo, dopo stima delle risorse. Non riattivare il vecchio paging soltanto per far apparire inferiore il RSS.

Una frequenza adattiva di certificazione può essere un'opzione di prodotto futura; non usarla nel confronto principale a intervallo20. Controllare più spesso può anticipare l'iterazione riportata senza migliorare la dinamica; controllare meno spesso può peggiorarla. Riportare separatamente crossing osservato, intervallo e costo BR.

### P3 — Seconda linea: meno visite o meno iterazioni

Questa fase richiede approvazione prima di sostituire o ampliare gli algoritmi production, coerentemente con il revert richiesto.

Ordine di fattibilità proposto:

1. **Pruning compatibile con ProductionDcfr:** usare telemetria read-only su corpus vario; stimare massa di lavoro eliminabile, finestre e metadati. Derivare un bound conservativo con discount signed e ricostruzione degli update saltati. Beta0 attenua rapidamente i regret negativi: il guadagno non è scontato. Senza prova del bound o con copertura insufficiente, fermarsi prima di implementare skip.
2. **Lazy updates:** costruire il modello completo di residui/history/history-action, non solo un bitset o un float per infoset. Dimostrare la corrispondenza degli update aggregati sul gioco giocattolo e l'adattamento alla schedule; riportare lavoro equivalente, non solo iterazioni nominali.
3. **Predictive/PDCFR+ o OMD:** rivalutare prima byte e ns/update. Se il ceiling è positivo, prototipo su Kuhn/Leduc e piccoli Short Deck, poi corpus indipendente. Non riusare la cumulative strategy come scratch distruggendo informazioni necessarie.
4. **DDCFR/schedule generalizzabili:** soltanto se i precedenti candidati non bastano e vi è una motivazione nuova rispetto agli sweep già chiusi. Training/discovery e holdout separati; feature strutturali/runtime, nessun ID/board fingerprint; checkpoint include stato del controller. Costo di training separato e inferenza inclusa nel solve.

Ogni algoritmo sperimentale ha identità distinta, opzione disattivata per default, formula documentata e test propri. Nessun risultato è promosso per una sola fixture favorevole.

### P4 — Requisiti architetturali anticipati in P1-S e ambiti futuri

I requisiti subgame e bucketing di questa sezione si eseguono in P1-S, non dopo P2/P3 o la parity. Solo le estensioni preflop e gli altri algoritmi restano future/separate.

Safe subgame solving può ridurre la quantità di gioco mantenuta/calcolata contemporaneamente. Servono: blueprint, boundary CFV per le informazioni private corrette, vincoli di sicurezza, gestione delle reach zero, card removal, estrazione/splice e persistenza versionata. Testare piccoli giochi con BR globale prima/dopo; un controllo globale ex-post è una validazione, non sostituisce un metodo safe per costruzione e può annullare il guadagno di scala.

Il conto deve includere `blueprint + boundary + tutti i resolve + verifica + I/O`. Non confrontare un singolo river risolto con l'intero flop. Il subgame postflop entra dopo la baseline; l'estensione preflop rimane distinta e non viene sbloccata automaticamente.

Bucketing viene progettato e implementato con DCFR in P1-S, con policy globale/versionata, card removal corretto e certificazione nel gioco non astratto dove fattibile; mai K scelto per l'ID del benchmark. Non reinserire automaticamente l'implementazione rimossa. MCCFR e reti di valore restano fuori dal mandato solo DCFR e richiederebbero gate e decisioni distinti. Nessun requisito di automazione durante gioco live.

## 7. Protocollo anti-overfitting e promozione

### Corpus

- Conservare AHK/TH/TST come regressioni obbligatorie, non come spazio di ricerca dei parametri.
- Congelare un corpus di sviluppo e un holdout separato: flop monotone/rainbow/paired, turn/river, range asimmetrici e pesi frazionari, sizings differenti, stack/SPR diversi, rake zero e non zero, differenti gruppi di automorfismi e casi con card removal pesante.
- Dichiarare manifest e seed prima dei test. Non scegliere solo casi simili alle tre fixture. I casi grandi richiedono stima preventiva delle risorse.
- Ogni nuova selezione di parametri dopo aver visto l'holdout consuma quell'holdout: crearne uno nuovo per la conferma finale.

### Ordine delle verifiche

1. Unit e differential: regret matching, discount, averaging/reset, payoff/rake, BR, codec, zero reach, acting-player `player_local`, node locking e checkpoint/resume.
2. Kuhn/Leduc e piccoli Short Deck enumerabili; confronto con riferimento indipendente. Le implementazioni esterne non sono oracoli senza controllarne convenzioni e regole.
3. Replay real-node multistep, non soltanto errore locale di un encode. Bitwise quando rivendicato; altrimenti tolleranze giustificate prima della misura.
4. Diagnostica fixed-iteration omogenea per costo e curva; non è certificazione del target.
5. Target-driven completo, senza limiti che trasformino timeout/non-convergenza in PASS.
6. Suite Release e cinque processi indipendenti per ciascuna fixture e ogni configurazione di conferma, tutti sullo stesso HEAD finale. Ordine baseline/candidato bilanciato e carico controllato; niente processi pesanti concorrenti.

### Criteri di accettazione

- Correttezza, target stretto, root EV/layout/outcomes e integrità dei report devono passare separatamente dal tempo.
- Iterazioni non superiori ai tetti storici `80/80/160` nel confronto omogeneo; per algoritmi diversi aggiungere traversate equivalenti e numero di valutazioni/update.
- Solver mediano non superiore al tetto storico omogeneo e alla baseline contemporanea; wall operativo e solve-to-consultable non peggiori delle rispettive baseline senza startup. I vecchi wall di processo non sono soglie. Migliorare una misura non compensa peggiorare l'altra. Nessuna media aggregata fra fixture che nasconda una regressione.
- Riportare anche p95 e tutti i campioni. Una differenza vicina al rumore è **INCONCLUSIVE**, non PASS per tolleranza inventata. Aumentare i campioni; non continuare a misurare soltanto finché esce il run favorevole. Fissare in anticipo la regola statistica e la convenzione del p95, poco stabile con cinque processi.
- Memoria solver/processo sempre pubblicata. Parità RAM GTO+ obbligatoria su scope equivalente: FAIL RAM respinge il candidato per l'obiettivo congiunto anche se più veloce. Comparabilità irrisolta impedisce il PASS complessivo, non esenta dal vincolo. Aumento rispetto alla nostra baseline valutabile soltanto entro il requisito GTO+ dimostrato e le risorse disponibili; mai nasconderlo tramite una diversa etichetta.
- Holdout senza regressioni note non dichiarate; profilo realmente raggiungibile dal prodotto. Se manca il percorso CLI/GUI, il risultato è un prototipo, non una promozione production.

Un microbenchmark serve a respingere rapidamente un candidato, non a promuoverlo. Una candidata che riduce iterazioni ma aumenta wall viene respinta per questo obiettivo.

## 8. Artefatti e registro delle decisioni

Per ciascun candidato produrre una scheda con: ID indipendente dalle fixture, ipotesi, invarianti, riferimenti scientifici, differenza rispetto ai tentativi precedenti, file coinvolti, byte model, ceiling, metrica primaria, kill gate, commit, configurazione effettiva, test, campioni grezzi, mediana/p95, esito e motivazione.

Stati consentiti: `PROPOSED`, `FEASIBILITY_ONLY`, `REJECTED`, `INCONCLUSIVE`, `QUALIFIED`, `PROMOTED`. `QUALIFIED` richiede la matrice completa; `PROMOTED` anche integrazione del profilo nel prodotto e compatibilità dei file.

Ciclo: **misura → modello → ipotesi → ranking → test → promozione/rifiuto → nuova baseline → ricalcolo dell'obiettivo**. Un blocker numerico resta valido dopo la correzione del cap RAM; un blocker esclusivamente basato su quel cap va rivalutato. Nessuna riapertura per il solo fatto che un paper menzioni la stessa tecnica.

Aggiornare a valle dell'implementazione: `SOLVER_ALGORITHMS`, `NUMERICAL_PRECISION`, `PERFORMANCE`, `TESTING`, `CLI`, documentazione GUI, formati progetto/checkpoint, `IMPLEMENTATION_STATUS` e parity journey. Questo piano non sostituisce quei contratti né dichiara chiuso il parity gate.

## 9. Validazione di questo lavoro e prossimo passo

Le sezioni 1–8 fissano l'audit e i criteri iniziali. Lo stato eseguibile è nel
registro della sezione 10: il profilo condiviso è implementato, la suite Release
è stata rieseguita e la baseline R2 controllata è congelata. Nessuno speedup è stato
promosso e nessun solver esterno è stato eseguito.

R3 ha attribuito il wall A0: mapping 56,53%/46,54% e training 42,54%/50,62%
su D/V. R4-A1 e R4-B1 hanno fallito i kill gate predefiniti; anche combinate
restavano 3,47–4,07 volte più lente dell'exact. R6-A non trova finestre RBP e
non dispone di un bound sound per ProductionDcfr; R6-B non può ricostruire la
stessa traiettoria signed dai reward collassati del Lazy-CFR pubblicato.

**Esito finale: `BLOCKED_WITH_EVIDENCE`.** Nessuno speedup è promosso, il
percorso product resta ProductionDcfr exact e H v2 resta sigillato. Una nuova
attività richiede una prova ProductionDcfr per pruning/lazy, una diversa
architettura exact che riduca il lavoro fisico, oppure autorizzazione esplicita
a valutare un altro algoritmo.

## 10. Stato di esecuzione 2026-09-05

L'implementazione è tracciata in
[`PRODUCTION_DCFR_PRODUCT_OPTIMIZATION_EXECUTION_2026-09-05.md`](PRODUCTION_DCFR_PRODUCT_OPTIMIZATION_EXECUTION_2026-09-05.md).
P0/R1 è implementata nei caller API, CLI, benchmark e nel worker GUI. La build
Qt Release e l'E2E product passano. R0 è chiusa dal corpus H v2 deterministico,
disgiunto e sigillato. R2 è chiusa come baseline, non come parity: TH/TST
falliscono il gate tempo e la comparabilità RAM resta irrisolta. R2-S è chiusa
come fattibilità sperimentale. S1 esegue direttamente il traversal postflop
ProductionDcfr sui bucket `made_hand_value`, usa stato `float64`, verifica un
oracolo scalare campionato e salva un checkpoint 1.0 distinto da quello exact.
Resume continuo e segmentato sono byte-identici. Il mapping usa ID locali a 16
bit e legge i pesi dai range canonici: D richiede 4.700.960 B e V 9.242.624 B,
con fingerprint invariati. S2 proietta in modo esatto e bounded i river
postflop, conserva tutti i deal privati pesati, deriva boundary, costruisce il
gadget opt-out e certifica lo splice nel gioco fisico. S3 comprime 12 infoset in
4 e certifica exact, bucket-only e combinazione nel gioco originale. A0 resta
non qualificata perché è più lenta su D/V e V perde qualità; UI, CLI e formato
`.gtsd` continuano a usare soltanto `exact_identity`.
R3–R6 sono chiuse nel registro di esecuzione: timer e contatori restano nel
probe, la telemetria RBP read-only supporta il profilo ProductionDcfr e nessun
pruning è entrato nel traversal. R7/R8 non sono state avviate perché manca un
candidato D/V; la comparabilità RAM GTO+ resta irrisolta.

## 11. Follow-up autorizzato: gioco river bucket-native

Il 2026-09-06 è stato implementato un follow-up limitato al river già completo.
Non riapre il ramo exact chiuso con `BLOCKED_WITH_EVIDENCE`: valuta un diverso
compromesso di prodotto, esplicitamente astratto. Il percorso ProductionDcfr
exact resta l'oracolo e il default.

Il candidato enumera una sola volta le combo fisiche per applicare range,
blocker e card removal. Poi aggrega la massa dei deal compatibili per coppia di
`HandValue` finale e attraversa un solo albero pubblico compatto. Sul caso full
range del test, 188.790 deal diventano 611 coppie di bucket; il lavoro per
passata scende da 1.699.110 a 5.499 nodi. Il modello nativo conta 55.972 B,
inclusi stato, mirror checkpoint e scratch, ma escluso l'overhead degli
allocator.

Due river pesati D/V costituiscono l'oracolo originale. A 128 iterazioni:

| Caso | Deal → coppie bucket | Nodi exact → bucket | Mediana solve exact → native, 5 processi | NashConv originale exact → native |
|---|---:|---:|---:|---:|
| D | 8.640 → 90 | 77.761 → 811 | 7,2651 ms → 3,1269 ms | 0,000202927 → 0,00377002 |
| V | 8.640 → 70 | 77.761 → 631 | 8,0537 ms → 3,0401 ms | 0,0000145436 → 0,000511609 |

Il candidato era `FEASIBILITY_ONLY`: riduce tempo e rappresentazione sui test,
ma perde informazione sui blocker privati all'interno dello stesso bucket. Non
è collegato a CLI, GUI o `.gtsd`, accetta solo river fisso HU e un thread, e
non è collegato al percorso product. Il settlement applica il rake configurato.

## 12. Qualifica River v1: decisione

Il corpus indipendente
`benchmarks/fixtures/river_bucket_qualification_corpus_v1.json` è stato
selezionato prima del primo solve e congelato con SHA-256
`7E542407BCCF598BA5E3A0CB5A3F8ECCEBF0DE3D8DB70A79E99D1DDFE73F2619`.
Contiene sette strati River, range pesati deterministici, rake zero e non zero,
SPR e sizing diversi. Ogni fixture doveva superare tutte le soglie preventive
a 256 iterazioni e cinque ripetizioni alternate exact/native.

Esito: **`REJECTED`, 0/7 fixture qualificate**. Il byte model nativo e il delta
del profile value passano 7/7, ma bucket NashConv e relativo delta falliscono
7/7. Lo speedup operativo passa soltanto 3/7; il caso high-card two-tone non
raggiunge neppure la riduzione minima di 10x. La fixture double-paired con rake
fallisce anche il gate di convergenza exact, quindi non può attribuire tutto il
delta all'astrazione, ma fallisce comunque soglie proprie del candidato.

Conseguenza: `made_hand_value` River resta un esperimento respinto per
integrazione product. Exact resta default e oracolo. Non si estende questa
rappresentazione a Turn o preflop; un eventuale nuovo candidato deve preservare
informazione di blocker e usare un nuovo holdout, senza tarare il corpus v1.
Il report completo è
`benchmarks/results/river_bucket_qualification_2026-09-06.json`.

## 13. Follow-up River v2 blocker-aware lossless

Il successore `exact_blocker_signature_v2` usa una relazione di equivalenza
più stretta. Due combo dello stesso player condividono una classe soltanto se
hanno lo stesso `HandValue` finale e lo stesso vettore di compatibilità contro
ogni combo attiva del range avversario. La relazione conserva quindi showdown,
card removal e supporto chance contro qualunque strategia avversaria sul river
fisso. Non rivendica di essere il quoziente lossless più piccolo rispetto ad
automorfismi congiunti di semi e range.

Il corpus v2 è stato congelato prima del primo solve. Riusa le sette fixture v1
come regressione e aggiunge cinque holdout scelti senza output del solver. Il
manifest ha SHA-256
`F3E262337D5481651D80101C6AD9A09AD8BBC146379FE10D794B169ACED95B14` e
fingerprint runner `fnv1a64:812e7edf4d682820`.

Il preflight trova rapporto `1,00x` in tutte le 12 fixture: 96 classi per 96
combo nei casi v1 e 112 classi per 112 combo negli holdout. Il test full-range
conferma 465 classi per 465 combo per player, 188.790 coppie compatibili e
188.790 deal fisici. Il modello nativo full-range richiede 6.620.596 B contro
55.972 B del candidato coarse v1.

La qualifica Release a 1.024 iterazioni conclude **`REJECTED_FEASIBILITY`,
0/12**. Riduzione nodi e speedup falliscono 12/12; lo speedup exact/native varia
da `0,006709x` a `0,017259x`. Il byte model passa 12/12 e i gate di qualità
passano 11/12. La fixture double-paired con rake conserva il problema già noto:
l'oracolo exact quantizzato ha NashConv `0,0131932` alla quota fissata e produce
anche il solo delta BR/NashConv fuori soglia. Questo non altera il blocker
strutturale, perché anche quella fixture ha una classe per combo e rapporto
nodi `1,00x`.

Conseguenza: v2 resta un kernel sperimentale e un oracolo di equivalenza. Non
entra in CLI, GUI, `.gtsd`, Turn o preflop. Exact resta il percorso product. Un
ulteriore River lossless richiede una riduzione basata su automorfismi congiunti
dimostrati sul grafo di compatibilità; un River approssimato richiede invece un
nuovo contratto e un budget d'errore esplicito.

## 14. Chiusura River lossless: partizione equa pesata

L'ultimo pre-gate elimina il limite principale di v2 senza introdurre
approssimazione. Parte da classi separate per player e `HandValue`, poi raffina
con la massa totale del range avversario compatibile in ciascuna classe
avversaria. Una classe stabile contiene quindi soltanto combo con lo stesso
showdown e la stessa distribuzione chance pesata verso ogni classe strategica.
Il peso della combo propria non entra nel colore iniziale: moltiplica lo stesso
update controfattuale; i pesi avversari entrano invece nella firma perché
determinano la chance condizionale e i blocker.

Sul full range uniforme il metodo trova simmetria reale: `465 -> 45` classi per
player e `188.790 -> 2.005` coppie di classi compatibili, pari a una riduzione
potenziale del lavoro di coppia di `94,1596x`. Sul corpus v2, che contiene range
asimmetrici e pesi frazionari, tutte le 12 fixture convergono invece
all'identità: `96 -> 96` oppure `112 -> 112` classi, riduzione `1,00x`, gate
minimo `1,25x` fallito 12/12.

Decisione: **`BLOCKED` prima del kernel**. La simmetria del full range non è
rappresentativa del prodotto configurabile. Non si implementa un terzo solver
River che avrebbe lo stesso stato fisico dell'exact; ProductionDcfr exact resta
il prodotto. Il report è
`benchmarks/results/river_joint_equitable_feasibility_2026-09-06.json`, SHA-256
`9E75FD396393FD4EB49C23881F8233237CDE22004ED660E0E4322BC287565C69`.

## 15. Estensione R9-C: decomposizione HU preflop exact

R9-C usa public root al Flop, range condizionati alle combo fisiche e nesting
al River. Il percorso exact copre ora le nove entry postflop, i 19 fold e i
dieci all-in del tree HU 40a. La best response preflop massimizza soltanto alle
decisioni del player che devia e somma le azioni avversarie con la reach del
blueprint.

Il gate RAM resta positivo: il lower bound annidato modellato è 637.398.110 B,
incluso il payload transazionale della certificazione. Il gate temporale resta
negativo per solve River indipendenti. Cinque processi Release consecutivi
proiettano una sweep seriale fra 783.076 e 930.075 s, con mediana 834.311 s.
È una proiezione da una sola iterazione sul subgame peggiore, non una misura di
convergenza. Il riferimento proviene da una versione modificata di MonkerSolver
e, secondo conferma dell'utente del 9 settembre, ha completato questo spot in
meno di 12 ore. La proiezione locale supera quindi il limite esterno già con
una sola sweep: prova che il solve indipendente root-per-root è
architetturalmente inadatto, non che l'exact o la decomposizione siano
intrinsecamente troppo lenti. Scheduler, checkpoint e riduttori restano oracle
bounded-memory; non diventano un default di prodotto.

Il ledger globale 1.3 conserva anche il contributo EV di profilo di entrambi i
resolver per ciascuno dei 5.157 task. Occupa 170.181 B: 5.157 B di mask,
41.256 B di probabilità, 82.512 B di utility `float64` e 41.256 B per
l'identità locale del task. Lo streaming
task-atomico mantiene il ledger corrente, il candidato e al massimo due
boundary da 12.672 B: il payload vivo modellato è 365.706 B. Stringhe,
metadati dei container e callback sono esclusi. Il piano 1.5 ha fingerprint
`fnv1a64:fd74e36ab417d1da`.

La catena distingue due identità. Il fingerprint locale descrive
l'assemblaggio di un singolo task e cambia fra flop; il fingerprint globale
identifica il checkpoint postflop che ha prodotto CFV e probabilità. Dal primo
boundary il ledger lega tutti i task al secondo e alla stessa iterazione del
blueprint. Una boundary proveniente da un checkpoint diverso viene rifiutata
senza modificare la copertura. Gli accumulatori e aggregati River v5 propagano
questa identità fino all'assemblatore Flop 1.1; anche i terminali Flop/Turn 1.1
devono dichiarare lo stesso checkpoint.
L'orchestratore costruisce un solo task alla volta, richiede al provider
soltanto i resolver mancanti e salva il candidato prima del commit. Un errore
del provider o del checkpoint conserva l'ultimo task già accettato.

La valutazione exact del profilo ricompone i contributi delle 10.314 boundary,
i 19 fold e i dieci all-in. Il validatore ricalcola i terminali dalla tabella
showdown exact: un EV alterato viene rifiutato anche se il fingerprint viene
rigenerato. L'evidenza BR 1.2 deriva quindi
`g_i = BR_i - V_i(profile)` e la NashConv dallo stesso blueprint, dalla stessa
iterazione e dallo stesso fingerprint di continuazione.

La certificazione del benchmark richiede ancora:

1. nove valutazioni postflop da checkpoint convergenti;
2. best response exact dei due player su tutte le foglie;
3. NashConv globale entro la soglia dichiarata;
4. confronto con `GTP-HU-PREFLOP-CO40-001`.

Il test HU Release passa con 9.090 asserzioni. Copre checkpoint misti,
risposte incomplete del provider, resume da un solo resolver,
checkpoint-before-commit e copertura completa dei 10.314 boundary. I terminali
BR Flop/Turn 1.1 dichiarano la continuation usata dal provider avversario e il
dispatcher rifiuta un terminale di un checkpoint differente prima della
ricorsione. La suite Release completa passa 42/42 in 787,22 s; il target HU
impiega 506,16 s. Lo stesso target passa sotto AddressSanitizer con 9.090
asserzioni in 4.793,91 s, senza diagnostiche.
Questi test provano il percorso strutturale e l'integrità
dell'evidenza; non trasformano lo smoke a una iterazione in prova di
convergenza. Nessun default CLI, GUI, ProductionDcfr o `.gtsd` è cambiato.

La BR exact dispone inoltre di checkpoint a granularità River leaf, task ed
entry. Il leaf salva il manifest ordinato dei root, l'indice del prossimo root,
le 630 somme compensate e tutte le identità della query e della continuation.
Il commit avviene solo dopo il successo del sink; il resume non ricalcola i
root già accettati. Task evaluation, entry accumulator ed entry evaluation
hanno payload JSON v1 separati, con checksum, limiti di lettura e scrittura
atomica. Il test HU Release di questa revisione passa con 9.099 asserzioni in
462,24 s e stderr vuoto. La suite Release completa passa 42/42 in 752,22 s;
nel run integrale il target HU impiega 484,88 s. È un miglioramento di
affidabilità, non di velocità o convergenza; la proiezione dello sweep River
resta invariata.
