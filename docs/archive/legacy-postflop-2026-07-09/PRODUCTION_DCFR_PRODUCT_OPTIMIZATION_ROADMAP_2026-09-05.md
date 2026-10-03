# Roadmap operativa: ottimizzazione ProductionDcfr e diramazioni

> **Nuovo mandato postflop — 2026-09-10.** La richiesta dell'utente riapre
> algoritmi alternativi, sampling e astrazione. Per questa nuova ricerca
> seguire la [roadmap dell'agente](POSTFLOP_AGENT_EXECUTION_ROADMAP_2026-09-10.md).
> I vincoli successivi di solo ProductionDcfr, stessa traiettoria e tetti di
> iterazioni appartengono alla campagna precedente; non vietano gli esperimenti
> ora autorizzati. Restano validi integrità delle evidenze, qualità nel gioco
> originale e confronto omogeneo delle risorse. Questa nota non promuove un
> backend e non modifica le attività preflop descritte nel documento.

Data: 2026-09-05. Versione: 16. Stato: **R9-C IN CORSO — decomposizione HU preflop exact: gate strutturale, boundary CFV, EV di profilo, streaming e best response con resume, certificatore globale e identità della continuation PASS; solve completo non ancora certificato; nessun default di prodotto modificato**.

**Sequenza v5 concordata:** fondamenta e baseline → progettazione e valutazione di astrazione/bucketing e subgame con ProductionDcfr → implementazione isolata delle tecniche fattibili e verifica della combinazione → ottimizzazione dell'architettura risultante → qualificazione finale congiunta. Non occorre raggiungere prima la parità di tempo, iterazioni e RAM per studiare e implementare le tecniche che potrebbero consentirla. L'esecuzione corrente applica questa sequenza.

Aggiornamento risorse v4: **la parità RAM con GTO+ è obbligatoria**, non un obiettivo opzionale. Il requisito è memoria non superiore a GTO+ sullo stesso gioco e sullo stesso perimetro di misura. Tempo migliore non compensa RAM peggiore. Comparabilità irrisolta impedisce la chiusura dell'obiettivo congiunto, pur consentendo ricerca e risultati intermedi esplicitamente parziali.

Aggiornamento temporale v3: per decisione dell'utente, l'avvio dell'applicazione/processo non conta nel confronto. Ogni riferimento successivo a wall nei gate indica il wall operativo senza startup, non la durata completa del processo. I vecchi tempi di processo rimangono solo diagnostici.

Aggiornamento decisionale: solo ProductionDcfr nei percorsi operativi del prodotto; niente ripresa CFR+ nel prodotto. Astrazione, bucketing e subgame sono compatibili con DCFR e non richiedono di sostituirlo. I rami relativi ad altri algoritmi restano soltanto alternative archiviate, non attività da eseguire sotto questo mandato.

Documento di analisi, audit e fonti: [piano di ottimizzazione](PRODUCTION_DCFR_PRODUCT_OPTIMIZATION_PLAN_2026-09-05.md). Questa roadmap ne specifica ordine, dipendenze, deliverable e decisioni; non promuove algoritmi e non riapre automaticamente funzionalità rimosse dal revert.

## 1. Obiettivo, confini e regole decisionali

Obiettivo finale: diminuire tempo necessario e iterazioni per raggiungere la stessa accuratezza e pareggiare o migliorare la RAM di GTO+, attraverso miglioramenti disponibili nel prodotto, non solo nel runner benchmark. Nessuno scambio implicito fra velocità, accuratezza e RAM.

Vincoli invarianti:

1. Correttezza, regole, range, sizings, payoff, card removal, target e metrica non si indeboliscono per ottenere un PASS.
2. ProductionDcfr resta il profilo di riferimento: signed alternating `1.5/0/3`, reset bounded `1,2,5,17,65`, codec corrente e clock documentati nel piano.
3. La qualificazione del prodotto si svolge a otto thread solver. I confronti diagnostici seriali servono solo a verificare correttezza/scaling; non sostituiscono questa qualificazione. Rilevare il totale effettivo dei worker nelle diverse fasi.
4. Nessun dispatch per ID, percorso fixture, board riconosciuto, dimensione esatta riconducibile al test o soluzione precalcolata. Specializzazione per proprietà strutturali lecite solo se generalizzata e verificata su corpus indipendente.
5. Nessun peggioramento di iterazioni, mediana solver e mediana wall su ciascuna regressione obbligatoria. Nessuna compensazione fra benchmark.
6. Ottimizzare il costo senza ridurre le iterazioni è un risultato intermedio utile, non il completamento dell'obiettivo congiunto.
7. Nessuna sostituzione di ProductionDcfr è prevista. Profili numerici non equivalenti e astrazioni richiedono identità separata e qualificazione esplicita. Le alternative ad altri algoritmi possono essere riaperte solo da una nuova decisione dell'utente.
8. Il benchmark misura il prodotto; le sue opzioni non devono essere un profilo privilegiato irraggiungibile da CLI/GUI.
9. Preservare checkpoint, progetti, evidenze storiche e modifiche locali dell'utente. Nessun push o distribuzione remota previsto.

### Soglie storiche obbligatorie

| Caso | Iterazioni massime | Limite mediana solver (s) | Mediana storica processo, solo diagnostica (s) |
|---|---:|---:|---:|
| AHKHQH | 80 | 0,758705 | 6,231883 |
| TH7D6S | 80 | 19,948228 | 35,208170 |
| TSTC9D | 160 | 184,095930 | 208,403423 |

Sono misure storiche della qualificazione gamma3, non nuovi risultati. p95, memoria, target e provenienza completa sono nel piano. Rimangono vincolanti i limiti di iterazioni e solver a perimetro invariato. La colonna processo NON è un limite del nuovo gate: serve una baseline contemporanea del wall operativo senza startup. Non inventare valori corretti sottraendo startup stimato o mediane di timer diversi.

### Tempo totale, tempo solver e riferimento GTO+

Il wall è tempo reale trascorso fra due eventi espliciti, non la somma dei tempi CPU dei thread. Qui il **wall operativo** parte da Build Tree con applicazione già pronta e configurazione disponibile e termina alla soluzione consultabile, senza pause dell'utente. Startup e chiusura del processo sono esclusi. La mediana è il valore centrale dei campioni ordinati; il limite riguarda quella mediana, non il massimo di ogni singola esecuzione.

Per il primo benchmark, **AHKHQH**, la [fixture corrente](../benchmarks/fixtures/gto_plus_ahkhqh_101.json) registra GTO+ **1,71 s**, target dEV 1%, stessa macchina, GTO+ v1.6.9 64-bit. Il perimetro dichiarato è: click su Run Solver, **albero già preparato**, fino a soluzione completa consultabile. È un riferimento registrato, non una nuova misura eseguita oggi e non una mediana di cinque processi dichiarata dalla fixture.

I nostri riferimenti storici AHK sono **0,758705 s solver** e **6,231883 s wall del processo**. Non confrontare direttamente 6,231883 con 1,71 come se includessero lo stesso lavoro. Il valore solver storico inferiore a 1,71 non dimostra da solo che la GUI consegni una soluzione consultabile prima di GTO+.

R2 deve produrre tre misure a confini espliciti: **build-to-ready**, **solve-to-consultable** con albero pronto e **build-to-consultable** (wall operativo complessivo). Applicazione già pronta in entrambi i prodotti; niente pause umane incluse. Registrare preparazione cold, training, certificazione, finalizzazione e analisi/UI necessarie. Tutte le attività richieste per rendere consultabile la soluzione devono rientrare nel relativo intervallo, non essere nascoste dietro la fine del timer. Esportazioni opzionali separate; salvataggi obbligatori inclusi.

Il primo benchmark ha, secondo quanto riferito dall'utente in questa conversazione, build tree GTO+ inferiore a un secondo. Associato al solve registrato di 1,71 s, indica un totale build+solve inferiore a 2,71 s, assumendo stesso caso e stesso perimetro di completamento. È un limite derivato da dichiarazione utente, non una nuova misura strumentata né una mediana certificata: registrare i campioni omogenei prima di dichiarare la parity completa.

L'esclusione dello startup non autorizza a precalcolare fuori timer dati del gioco richiesto. Inizializzazione generale dell'applicazione esclusa; costruzione tree, rank e cache specifici del caso inclusi nel build/prepare, anche se lazy. Per il runner CLI si possono usare processi indipendenti, ma i timer interni devono iniziare dopo l'inizializzazione generale, prima del lavoro specifico del caso; non usare il tempo esterno del processo come sostituto.

Obiettivi distinti: rispettare i limiti storici iterazioni/solver e la nuova baseline wall operativo; raggiungere o migliorare **1,71 s** per AHK sul perimetro solve-to-consultable GTO+ e confrontare anche build+solve senza startup. Eventuali soglie di tolleranza del parity gate, come 1,90 s nei report storici, non sono il tempo misurato GTO+ e non significano averlo eguagliato.

## 2. Mappa delle dipendenze

### Gate RAM obbligatorio e preliminare di comparabilità

Riferimenti attualmente registrati nelle fixture, campo GTO+ **Memory needed for solving**:

| Caso | Valore GTO+ | Normalizzazione decimale della fixture |
|---|---:|---:|
| AHKHQH | 8 MB | 8.000.000 B |
| TH7D6S | 399 MB | 399.000.000 B |
| TSTC9D | 2.000 MB | 2.000.000.000 B |

Sono valori dichiarati per solving, non misure certificate di Peak RSS totale. Non diventano automaticamente cap del processo GTOSD; in particolare 2.000 MB non significa 2 GiB. Il nuovo requisito di parità non modifica retroattivamente la semantica di questi dati.

**R0-M, prerequisito del PASS finale:** definire e verificare scope equivalente. Inventariare tree, stato, scale, rank, scratch worker, BR, finalizzazione e cache; per ciascuno registrare inclusione/esclusione, lifetime, backing, payload/capacità e modalità di osservazione. Usare il ledger solver-owned già disponibile senza presumere che coincida con il display GTO+.

Due confronti distinti: (a) memoria per solving confrontabile con il campo GTO+; (b) RAM effettiva dei due processi misurata con lo stesso strumento e protocollo, a parità di fase e stato dell'applicazione. Raccogliere baseline dell'app pronta e picchi di build, solve/certificazione e soluzione consultabile. Riportare sia valori assoluti sia eventuali delta, senza sottrarre arbitrariamente memoria per ottenere il PASS. Il display GTO+ non sostituisce una misura della RAM effettiva.

Il contratto deve dichiarare quali confronti sono dimostrati, unità, risoluzione del display e incertezza; nessuna tolleranza inventata. Se il display resta opaco, si può qualificare separatamente la RAM effettiva con misure omogenee, ma non chiamarla equivalenza del display. La chiusura complessiva richiede il gate RAM concordato e dimostrato, con ogni scope irrisolto esplicitamente dichiarato.

Diramazioni: scope equivalente e memoria GTOSD non superiore → PASS del confronto specifico; superiore → FAIL RAM; scope/strumentazione incerti → `NOT_EVALUATED_COMPARABILITY_UNRESOLVED`, obiettivo congiunto aperto. R0-M parte insieme all'inventario, non soltanto dopo le ottimizzazioni. R4/R6 possono avanzare con stime e limiti dichiarati, ma non essere presentati come soluzione completa.

Non ridurre artificialmente RSS mediante trimming, paging, spostamento su disco o esclusione di buffer necessari: registrare commitment, backing e I/O, con costo incluso nei timer operativi. Un incremento rispetto alla nostra baseline è valutabile solo se resta entro la parità GTO+ dimostrata e i vincoli hardware; nessuna promozione finale fondata su una semplice promessa di RAM futura.

```text
R0  Contratto, inventario e manifest
 |
R1  Profilo comune API / CLI / GUI / benchmark
 |   incompatibilità resume/backend -> R1-C, poi ripetere R1
R2  Baseline riproducibile + audit anti-specializzazione
 |   correttezza FAIL -> ramo FIX, niente ottimizzazioni
 |   ambiente/report inconcludente -> ramo DIAG, ripetere R2
R2-S Astrazione/bucketing e subgame con ProductionDcfr
 |   specifica e fattibilità -> prototipi isolati -> combinazione verificata
 |   nessuna tecnica fattibile -> blocker e decisione, niente promozione forzata
R3  Nuovo profilo dei costi sull'architettura risultante
 |
 +--> R4-A Preparazione, riuso, cache immutabili
 +--> R4-B Codec, dati, scratch, memoria
 +--> R4-C Scheduling a 8 thread
 +--> R4-D Certificazione, analisi, persistenza
 |          ogni candidato -> GATE -> scarto / inconcludente / qualificato
 |
R5  Composizione dei soli candidati compatibili e qualificati
 |
 +--> obiettivo congiunto raggiunto -> R7 conferma e integrazione
 |
 +--> tempi migliori, iterazioni invariate -> risultato parziale
 |       R6 fattibilità convergenza, con algoritmo production invariato
 |         +--> pruning rigoroso / lazy
 |         +--> predictive / OMD: archiviato, fuori mandato solo DCFR
 |         +--> schedule diversa: archiviata, richiede nuova decisione
 |         +--> nessun candidato fattibile -> blocker documentato
 |
R7  Conferma finale, compatibilità, profilo effettivo nel prodotto
 |
R8  Ripresa del parity gate, separato dal successo rispetto a noi stessi
 |
R9  Eventuale estensione preflop e altri ambiti futuri
     astrazione/bucketing e subgame postflop sono già anticipati in R2-S
```

I rami R4 non sono una prescrizione di implementarli tutti: R3 sceglie il primo in base al costo totale evitabile e al rischio, dopo R2-S. Prima di R2-S sono ammesse correzioni di correttezza, strumenti di misura e profiling necessario alla fattibilità, non un ciclo di ottimizzazioni profonde del vecchio layout. Attività documentali indipendenti possono sovrapporsi; build e benchmark pesanti non si eseguono contemporaneamente. Nessuna stima di giorni o speedup è affidabile prima delle misure: pianificazione per milestone, non promesse di calendario.

## 3. R0 — Congelare contratto, provenienza e corpus

**Ingresso:** piano letto, checkout identificato; baseline descritta dal piano `ffb208a`/tree `c26e1f8`, da verificare nuovamente all'avvio dell'implementazione.

Attività:

- Inventariare branch, commit, diff, file non tracciati, build Release, cache CMake, hash eseguibile e flag research. Non includere accidentalmente artefatti locali nel commit.
- Elencare entry point solve, prepare, resume, diagnostica, API e GUI con le opzioni effettive. Separare default libreria e profilo applicativo.
- Formalizzare unità dei timer, dEV massimo e NashConv somma, iterazione alternata completa, update e traversate equivalenti. Il singolo pass giocatore non deve diventare un'iterazione solo nel candidato.
- Congelare i requisiti congiunti: tempo/iterazioni senza regressione e RAM non superiore a GTO+ su perimetro equivalente. Avviare R0-M; finché la comparabilità manca, il gate RAM non può passare.
- Congelare corpus di sviluppo D, validazione V e conferma H. AHK/TH/TST rimangono regressioni obbligatorie e non set di tuning.
- Predisporre registro dei candidati e registro dei blocchi storici con motivo preciso: correttezza, costo, memoria, comparabilità o mancata generalizzazione.

**Artefatti proposti:** manifest ambiente/baseline, matrice caller, manifest D/V/H, registro candidati. Collocazione suggerita: documenti in `docs/`, campioni grezzi in una directory di evidenze dedicata sotto `out/`; nomi definitivi al momento dell'implementazione.

**Uscita R0:** ogni misura può essere ricondotta a binario, configurazione risolta e corpus. Nessun risultato precedente viene presentato come misurato sul nuovo HEAD.

**Diramazioni:** checkout differente → aggiornare inventario; dirty sovrapposto → preservare/isolare o chiedere direzione; contratto matematico ambiguo → risolvere prima di scrivere il kernel; nessun ambiente Release valido → correggere la build prima delle misure.

## 4. R1 — Allineare il prodotto

**Perché precede le ottimizzazioni:** l'audit precedente rileva benchmark ProductionDcfr contro default CFR+ nei percorsi ordinari. La roadmap assume questo come punto di partenza documentato, da riconfermare sui caller prima della patch.

### R1-A — Risoluzione centralizzata delle opzioni

1. Definire un profilo production versionato e una funzione condivisa di risoluzione delle opzioni.
2. Distinguere input utente, default applicativi e opzioni risolte; emettere un riepilogo verificabile.
3. Applicare ProductionDcfr ai nuovi solve CLI e GUI. Il benchmark deve usarlo attraverso lo stesso percorso, senza sovrascrivere segretamente precisione, schedule o thread. Nessun selettore CFR+ nei percorsi operativi del prodotto.
4. Gli algoritmi reference eventualmente necessari ai test restano isolati dai percorsi operativi; la loro presenza non autorizza solve o resume CFR+ nel prodotto. Non cambiare indiscriminatamente i default delle strutture usate dagli oracoli.
5. Separare identità matematica, profilo numerico e policy di esecuzione: un nuovo numero di thread non deve diventare un nuovo algoritmo; un nuovo codec non deve essere nascosto come scheduling.

**File da esaminare/modificare:** `include/gtosd/postflop/postflop_solver.hpp`, `libs/postflop/src/postflop_solver.cpp`, `apps/gto_cli/main.cpp`, `apps/gto_gui/product_window.cpp`, moduli di progetto/checkpoint individuati nell'inventario e rispettivi test.

### R1-B — Contratto dei nuovi solve

Test su configurazioni identiche via API, CLI e funzione di configurazione del worker GUI: algoritmo, codec, averaging, target, strictness, intervallo di certificazione, backend e thread risolti devono coincidere. Verificare risultati e traiettoria, non solo stringhe di log. Aggiungere smoke GUI quando disponibile; un test headless del resolver non prova da solo l'interazione grafica.

### R1-C — Compatibilità e recupero

| Situazione | Azione richiesta |
|---|---|
| Checkpoint ProductionDcfr compatibile | Ripristinare clock, averaging epoch e stato; confronto con run continuo |
| Vecchio checkpoint CFR+ | Rifiutare la ripresa nel prodotto con errore esplicito; consentire eventualmente importazione della sola configurazione per un nuovo solve ProductionDcfr da zero, senza riusare regret, averaging o iterazioni |
| Metadati legacy insufficienti | Usare una migrazione documentata solo se l'identità è ricostruibile senza ambiguità; altrimenti errore recuperabile |
| Backend non supporta codec/profilo | Errore o alternativa esplicita; nessun fallback nascosto |
| Progetto legacy senza algoritmo | Ricostruire l'identità solo se non ambigua; altrimenti niente resume. Eventuale importazione della configurazione avvia un nuovo solve ProductionDcfr, dichiarando il riavvio |
| File corrotto, checksum errato, write interrotto | Rifiuto del file o recupero previsto dal formato; originale preservato |

Test pause/resume e save/load attorno alle iterazioni `1,2,5,17,65,66`, poi oltre l'ultimo reset. Verificare anche cancel durante certificazione/checkpoint, cambio risorse e richiesta di parametri incompatibili.

**Uscita R1:** matrice caller coerente, migrazioni esplicite, test pertinenti e Release completati. Questa fase è una correzione di disponibilità del profilo nel prodotto, non uno speedup rispetto al benchmark già configurato correttamente.

## 5. R2 — Baseline, report e indipendenza dalle fixture

### R2-A — Misurazione riproducibile

Eseguire smoke di correttezza, poi almeno cinque processi indipendenti per caso. Congelare hardware, configurazione, stato cold/warm, profilo e intervallo20. Salvare tutti i campioni, anche quelli sfavorevoli. Preflight di CPU, RAM libera e processi concorrenti; niente build durante il timing.

Tre riferimenti da non confondere:

- **SH:** soglie storiche iterazioni/solver del piano, esclusi i vecchi wall di processo (distinte dal corpus holdout H);
- **B0:** baseline contemporanea ProductionDcfr;
- **Bk:** ultimo prodotto promosso, per valutare le modifiche cumulative.

Ogni candidato si confronta con B0/Bk e resta vincolato da SH. Non aggiornare la baseline per assorbire una regressione.

B0/Bk devono contenere build-to-ready, solve-to-consultable e wall operativo senza startup. Fino alla misurazione di questi intervalli, il relativo gate è NOT_EVALUATED: i 6,231883/35,208170/208,403423 secondi storici non lo sostituiscono. Le nuove baseline vengono misurate sul prodotto di riferimento, non scelte a posteriori dai tempi del candidato.

### R2-B — Audit dinamico anti-specializzazione

Test obbligatori: rinomina ID/percorso, file JSON semanticamente equivalente, modifica dei metadati senza cambiare il gioco, perturbazioni di range/stack/sizings, permutazioni lecite dei semi, casi vicino a soglie di dispatch. L'identità testuale non deve selezionare il kernel.

Un'ottimizzazione per due azioni è ammissibile se vale per tutti i nodi a due azioni e ha fallback corretto. Una soglia di memoria può dipendere dal budget disponibile; non dalla dimensione esatta di TST. Un fingerprint per invalidare una cache è lecito; per restituire una strategia precalcolata della fixture no.

### R2-C — Decisione

| Esito | Diramazione |
|---|---|
| Correttezza o checkpoint falliscono | FIX: correggere, aggiungere regressione, ripetere R1/R2; timing non interpretabile |
| Report incompleto o tempi sovrapposti | DIAG: correggere accounting/timer prima del confronto |
| Iterazioni diverse con contratto apparentemente identico | Confrontare stato per certificazione, codec, clock, input e ordine delle riduzioni |
| Iterazioni uguali ma secondi peggiori | Analizzare ambiente e fasi: non attribuire automaticamente il problema alla convergenza |
| Dipendenza illegittima dall'identità della fixture | Rimuovere la dipendenza, test metamorfico permanente, rifare baseline |
| B0 supera SH | Classificare e pubblicare la regressione; SH non viene rilassato |
| Baseline riproducibile e audit superato | R3 |

**Uscita:** report B0, matrice di correttezza, elenco completo dei casi mancanti e audit con perimetro dichiarato. Non chiamare esaustiva una sola ricerca di stringhe.

## 5-bis. R2-S — Architettura DCFR con astrazione, bucketing e subgame

**Posizione obbligatoria:** dopo R0/R1/R2 e prima di R3–R5. R0-M deve essere avviato e le misure RAM interne attendibili; la comparabilità GTO+ ancora aperta non vieta i prototipi, ma impedisce la qualificazione complessiva. La baseline non deve già battere GTO+ per entrare qui.

### S0 — Contratti e fattibilità

- Conservare il percorso non astratto ProductionDcfr come riferimento verificabile, non come unico profilo finale obbligatorio.
- Separare action abstraction già configurata e card abstraction: bucketing è una forma di quest'ultima. Non cambiare sizings/regole delle fixture per ridurre l'albero e rivendicare un confronto omogeneo.
- Definire policy globale/versionata, metrica di similarità, street, clustering deterministico, pesi, trattamento dei blocker e della storia informativa. Dichiarare se si conserva perfect recall; nessuna garanzia automatica per un'astrazione che lo perde.
- Definire public state, blueprint, boundary CFV, vincoli di sicurezza e contratto del subgame; DCFR deve essere il motore sia del blueprint sia del resolver. Usare CFR+ non è un prerequisito.
- Calcolare memoria completa, costo costruzione feature/bucket/boundary, traversate e verifica finale. Nessuna promessa di meno iterazioni basata soltanto su meno stati.
- Identificare moduli e formati separati per mapping bucket, boundary e checkpoint; non reintrodurre automaticamente i sorgenti del precedente revert.

**Uscita S0:** specifiche, byte model e test attesi per due candidati indipendenti. Se la fattibilità è negativa, documentare il vincolo e fermare quel ramo prima di implementarlo.

### S1 — Astrazione/bucketing isolato

Implementare prima su giochi ridotti: mapping deterministico/versionato, aggregazione e disaggregazione delle strategie, pesi e card removal corretti, identificazione dell'astrazione nel progetto e checkpoint. Verificare il caso identità, con un bucket per ogni infoset privato pertinente, contro il percorso di riferimento; poi confrontare granularità diverse su D/V, mai scegliere K per ID benchmark.

Misurare convergenza nel gioco astratto **e** qualità della strategia riportata nel gioco originale. Un dEV inferiore all'1% solo nel gioco astratto non soddisfa il gate originale. L'errore totale non va ottenuto sommando stime eterogenee senza un bound valido: usare BR originale dove fattibile e distinguere stime negli altri casi.

Diramazioni: errore eccessivo → affinamento generale o scarto; feature/build troppo costosi → valutare costo cold e riuso dichiarato; impossibilità di verificare qualità → non qualificato; correttezza e fattibilità positive → candidato per S3, non ancora production.

### S2 — Subgame isolato

Implementare su riferimento non astratto prima della combinazione: estrazione del subgame, blueprint DCFR, boundary compatibili con le informazioni private, vincoli del resolver, splice e persistenza. Testare zero reach, range asimmetrici, azioni fuori albero e boundary incompleti/incompatibili; rifiutare input non supportati invece di produrre una falsa garanzia safe.

Confrontare piccoli giochi con BR globale prima/dopo e tolleranze giustificate per risoluzione approssimata. Una guardia globale ex-post serve alla validazione ma non sostituisce la derivazione della sicurezza; blueprint o boundary stimati richiedono garanzie dichiarate.

Contare blueprint + boundary + tutti i resolve + verifica + I/O. Se migliora soltanto il singolo river ma non il workflow richiesto, non dichiarare speedup del solve completo.

Diramazioni: sicurezza non dimostrata → correggere o scartare; costo globale dominante → rivedere decomposizione o scartare; verifica positiva → candidato per S3. S1 e S2 non hanno dipendenza algoritmica reciproca: ordine dei prototipi scelto da fattibilità e costo, con misure pesanti non concorrenti.

### S3 — Combinazione e scelta dell'architettura

Confrontare quattro configurazioni: riferimento DCFR; DCFR con bucket; DCFR con subgame; DCFR con entrambi. Eseguire la quarta solo dopo verifiche isolate dei componenti. Controllare compatibilità dei mapping al confine, errori combinati, memoria contemporanea e identità dei file; i benefici non si sommano automaticamente.

| Esito | Diramazione |
|---|---|
| Entrambe le tecniche corrette e combinazione fattibile | Candidato combinato, poi R3 |
| Una sola tecnica fattibile | Documentare il limite dell'altra; proporre architettura con la sola tecnica valida, senza dichiarare entrambe completate |
| Componenti validi, combinazione errata | Correggere l'interazione; nessuna promozione della combinazione |
| Candidato corretto con margine di costo plausibile ma target finali non ancora raggiunti | Stato sperimentale; R3/R4 ottimizzano la nuova architettura, non serve già il PASS finale |
| Nessun margine plausibile o errore non accettabile | Blocker e decisione sull'architettura; non forzare astrazione o peggiorare i gate |

**Uscita R2-S:** architettura candidata documentata con correttezza verificata, errori dichiarati, implementazioni isolate e combinazione testata quando applicabile. Si conserva B0 e si registra una baseline sperimentale A0 della nuova architettura; A0 non sostituisce i limiti originali e non è un prodotto promosso.

Il gate comune resta obbligatorio per la promozione finale. Un prototipo corretto ma ancora lento può proseguire alle ottimizzazioni solo con un modello credibile del margine; un candidato che fallisce correttezza non prosegue. R2-S non attende la chiusura del parity gate e non sblocca automaticamente preflop o altre fasi F11+.

## 6. R3 — Profiling dell'architettura risultante e scelta del ramo

**Ingresso:** esito R2-S documentato. Ripetere l'attribuzione dei costi: i profili del vecchio layout non descrivono automaticamente bucket, blueprint e resolver. Includere i loro costi di preparazione e verifica nei timer operativi.

Misurare preparation/build, training, certificazione e finalizzazione consultabile, distinguendo analisi/persistenza obbligatorie e opzionali; specificare quali componenti sono inclusi in `solver_seconds`. Startup, se registrato, resta diagnostica esclusa da ranking e gate. Se le attività si sovrappongono, misurare intervalli di wall operativo e critical path: i tempi CPU dei worker non si sommano come wall.

Contatori richiesti: nodi e action-entry aggiornati, passate sullo stato, scale cambiate, conversioni, allocazioni, memoria per lifetime, task utili/attese, showdown/chance/BR, page fault. Distinguere contatori disponibili da stime. Profiling e run finale non strumentato restano separati.

Per ciascuna ipotesi compilare:

1. quota attuale del wall aggredibile;
2. limite ottimistico di miglioramento totale, incluso overhead introdotto;
3. memoria aggiuntiva a regime e al picco, non solo payload finale;
4. impatto previsto su iterazioni e precisione;
5. differenza concreta rispetto agli esperimenti già respinti;
6. costo del più piccolo esperimento falsificabile;
7. criterio di scarto stabilito prima del risultato.

**Ranking:** correttezza dimostrabile → applicabilità generale → guadagno totale plausibile → rischio RAM/compatibilità → costo di verifica. Non scegliere in base alla migliore percentuale di un microbenchmark.

**Diramazioni:** setup dominante → R4-A; codec/bandwidth dominante → R4-B; worker inattivi → R4-C; BR/analisi/I/O dominante → R4-D. Se nessun candidato offre margine oltre il rumore, passare alla fattibilità R6 o documentare il blocker; non ripetere micro-ottimizzazioni già falsificate.

Il corpus H finale non va usato per scegliere il ramo o i parametri: profiling su D/V. Se H viene esaminato per la scelta, diventa parte dello sviluppo e occorre un nuovo H di conferma. Questo precisa la separazione operativa del piano iniziale.

## 7. R4 — Rami per ridurre il costo del prodotto

### R4-A — Preparazione, riuso e cache

**Ipotesi:** lavoro immutabile duplicato o ricostruito inutilmente.

Attività: tracciare ownership e riuso di prepared tree, layout e rank; eliminare duplicazione solo se rilevata; stimare validazione e caricamento di una cache. La chiave deve coprire tutte le dipendenze del dato memorizzato: ruleset, evaluator, board, range quando rilevanti, tree/sizings, versione numerica e formato.

Test: cold senza cache, warm valida, cache vecchia, checksum corrotto, input cambiato, due processi concorrenti, scrittura interrotta. Fallimento della cache deve lasciare possibile la ricostruzione corretta.

Diramazioni:

- Riuso già presente → chiudere l'ipotesi, non aggiungere un'altra cache.
- Warm migliora ma cold peggiora → non promuovere come speedup cold; valutare una funzione opzionale soltanto se utile al workflow prodotto e approvata.
- Cache occupa più risorse del beneficio → scarto o diversa granularità giustificata dai dati.
- Cold e warm passano → gate comune, includendo I/O e spazio disco.

### R4-B — Stato, codec e scratch

Due sottorami distinti:

- **B1, semantica invariata:** allocazioni/lifetime, copie e scratch realmente evitabili, medesimo codec e update. Dimostrare equivalenza promessa, compresi parent output e checkpoint.
- **B2, nuovo profilo numerico:** Float32 direct o altra rappresentazione motivata dal collo di bottiglia misurato. Pre-gate RAM e approvazione prima di cambiare il prodotto; enum/formato distinti quando necessario.

Sequenza: modello byte → replay real-node multistep → precisione e throughput → piccolo solve → D/V → gate comune. Misurare costo encode/decode e scale globale insieme al producer; non isolare il solo kernel favorevole.

Diramazioni: drift BR/strategie → scarto numerico; più RAM del budget reale → inapplicabile a quel profilo hardware; solo il vecchio falso cap escludeva il candidato → rivalutazione legittima; throughput insufficiente → scarto anche se preciso; velocità con più RAM → compromesso esplicito, non miglioramento simultaneo delle risorse.

### R4-C — Scheduling e parallelismo

Attività: misurare sbilanciamento, granularità task, sincronizzazione e bandwidth; disegnare policy strutturale basata su lavoro stimato; conservare alternanza dei giocatori e ownership dello stato. Contare anche thread di BR e attività accessorie per evitare oversubscription nascosta.

Test: otto thread come gate, stress di scheduling ripetuto, confronti diagnostici a meno thread, differenziali numerici e race-check quando supportato dalla toolchain. Dichiarare eventuali tool non disponibili e alternative utilizzate.

Diramazioni: bandwidth già satura → tornare a B, altri task non aiutano; overhead maggiore nei piccoli casi → nuova policy strutturale generale oppure scarto; drift da riduzioni → ripristinare ordine o qualificare esplicitamente il nuovo comportamento numerico; miglioramento solo di TST con regressione AHK → nessuna promozione.

### R4-D — Certificazione, analisi e persistenza

Attività: individuare lavoro ripetuto nella singola certificazione, dati immutabili riutilizzabili e analisi richieste realmente dall'utente. Preservare BR esatta, intervallo20 e output obbligatori. La certificazione è già parallelizzata: l'ipotesi deve essere più precisa di “aggiungere thread”.

Test: BR indipendente, rake zero/non zero, range asimmetrici, snapshot coerente durante save, pause/cancel, output finali e checkpoint atomici. Non spostare il lavoro oltre la fine del timer se il prodotto deve comunque eseguirlo per consegnare la soluzione.

Diramazioni: costo inevitabile dominante → quantificare il limite; eliminazione output diagnostici opzionali → miglioramento solo nel profilo che non li richiede, non confronto falsato; certificazione adattiva → esperimento separato, non comparabile al gate intervallo20; BR fra strategie diverse → nessun riuso senza nuova valutazione corretta.

## 8. Gate comune di ogni candidato

Ordine vincolante:

1. Specifica minima e pre-gate teorico/costo/memoria.
2. Unit, proprietà e differential pertinenti.
3. Replay e piccoli giochi indipendenti.
4. Fixed-iteration D/V: costo e traiettoria.
5. Target-driven D/V e regressioni obbligatorie.
6. Conferma H e cinque processi per fixture sullo stesso HEAD finale.
7. Verifica effettiva dei percorsi prodotto, checkpoint e documentazione.

| Evidenza | Stato e prossimo passo |
|---|---|
| Test matematico fallito | `REJECTED_CORRECTNESS`; correzione separata, ripartire dai test |
| Ceiling insufficiente | `REJECTED_FEASIBILITY`; non avviare solve lunghi |
| Più iterazioni oppure solver/wall peggiore in un caso obbligatorio | `REJECTED_REGRESSION` |
| Beneficio entro il rumore | `INCONCLUSIVE`; protocollo aggiuntivo prefissato, non retry opportunistici |
| RAM superiore a GTO+ nel confronto omogeneo | `REJECTED_RAM_PARITY`, anche se più veloce |
| Scope RAM non confrontabile o misure mancanti | `NOT_EVALUATED_COMPARABILITY_UNRESOLVED`; solo risultato parziale, niente chiusura complessiva |
| V passa, H fallisce | `REJECTED_GENERALIZATION`; se si modifica il candidato, H è consumato |
| Tempi migliori, iterazioni uguali | `QUALIFIED_COST_ONLY`; progresso intermedio, obiettivo iterazioni aperto |
| Iterazioni inferiori e tempi non peggiori | `QUALIFIED_CONVERGENCE`; verificare costo totale e lavoro equivalente |
| Requisiti completi e integrazione verificata | `PROMOTED` dopo decisione prevista |

Prima delle misure fissare numero campioni iniziale, eventuale lotto supplementare, stima mediana/p95 e regola statistica. Con margine di non-regressione zero, l'assenza di una differenza significativa non dimostra equivalenza: se i dati non consentono la conclusione, mantenere `INCONCLUSIVE`. Non introdurre una tolleranza prestazionale dopo aver visto il risultato. p95 resta indicatore di coda esplicito, non una garanzia robusta basata su cinque soli campioni.

Annullamento, crash, OOM o timeout non producono una mediana PASS sui soli run completati. Registrare il fallimento; un'interruzione ambientale motivata resta visibile e si ripete l'intera coppia di confronto prevista dal protocollo.

## 9. R5 — Comporre i miglioramenti senza perdere la baseline

Ingresso: almeno un candidato qualificato. Integrare una modifica coerente per volta; conservare manifest e possibilità di revert del singolo intervento.

Per A e B qualificati singolarmente eseguire anche A+B: non sommare gli speedup teorici. Le modifiche possono competere per bandwidth, aggiungere memoria simultanea, invalidare una cache o cambiare il scheduling.

Diramazioni:

- A+B supera tutti i gate → nuova Bk, nuova attribuzione R3.
- A+B regredisce → isolare interazione con A/B/base; conservare solo combinazione qualificata.
- Costi migliorati ma iterazioni invariate → pubblicare risultato parziale; R6 è ancora necessario per l'obiettivo congiunto.
- Obiettivo congiunto dimostrato su matrice completa → R7.
- Nessun margine residuo plausibile → blocker quantitativo, non un'ulteriore riscrittura del core.

## 10. R6 — Diramazioni per convergenza e lavoro evitabile

Questa fase non sostituisce ProductionDcfr. La fattibilità può concludersi con “non conviene”. R6-A/B sono valutabili solo dimostrando compatibilità con il contratto DCFR richiesto; se richiedono un altro algoritmo, fermarsi. R6-C/D sono alternative archiviate, non rami eseguibili del mandato corrente.

### R6-A — Pruning rigoroso

Usare prima `RbpReadOnlyTelemetry`: frequenza, durata e peso computazionale dei candidati. Derivare condizioni di skip e riattivazione per il discount effettivo; includere averaging, reach e contributi da ricostruire.

Se la prova vale solo per CFR non scontato o beta diverso, non applicarla a ProductionDcfr. Se richiede cambiare schedule, classificare come nuovo algoritmo. Un'azione con probabilità corrente zero non implica regret futuro irrilevante.

Uscita positiva: oracolo su piccoli giochi, bound documentato, stato aggiuntivo noto e guadagno netto. Uscita negativa: skip troppo brevi, bound non dimostrato o overhead eccessivo → scarto, valutare lazy.

### R6-B — Lazy updates

Modellare residui per history/history-action, clock e aggregazione degli update saltati. Distinguere “stesso gioco senza sampling” da “stessa traiettoria ProductionDcfr”: Lazy-CFR non implica automaticamente equivalenza della traiettoria.

Misurare iterazioni nominali, aggiornamenti locali, visite e lavoro equivalente. Se la RAM completa non è sostenibile, documentare quali accumulatori dominano; non sostituirli con un bitset che perde valori continui. Se il collo di bottiglia è quasi tutto in nodi sempre attivi, stop prima del solve completo.

### R6-C — Predictive/PDCFR+/OMD

**Fuori mandato solo DCFR:** descrizione conservata come alternativa, nessuna implementazione o promozione prevista senza nuova decisione.

Derivare stato minimo, precisione dei predittori e costo locale; confronto con studi FD-FTRL/OMD già presenti. Calcolare quante iterazioni deve risparmiare per pagare il costo aggiuntivo, inclusi BR e inizializzazione.

Sequenza: formule → giochi giocattolo → real-node → D/V → H. Predittore e average strategy hanno ruoli distinti; niente aliasing che distrugga informazione. Garanzie e limiti con rake devono essere dichiarati.

Se vince solo in iterazioni ma perde wall, respingere per questo obiettivo. Se vince solo su alcune strutture, studiare generalizzazione su D/V; non creare un selettore per i benchmark. Qualunque selettore automatico deve essere un profilo di prodotto verificabile e non un modo di scegliere il vincitore ex-post per ogni run.

### R6-D — Schedule dinamiche o apprese

**Fuori mandato ProductionDcfr invariato:** nessuna modifica della schedule o controller appreso prevista senza nuova decisione.

Prerequisito: nuova ipotesi rispetto a S6/HsDcfr30/reset già studiati. Niente sweep sui crossing 80/160.

Definire feature lecite, dati di training distinti, controller deterministico, seed, costo inferenza, stato checkpoint e fallimenti gestiti. Una policy globale può reagire a proprietà del problema: “globale” significa regola comune e versionata, non necessariamente stesso parametro numerico per qualunque input.

Se la politica necessita di informazioni del risultato futuro o della baseline specifica del test, è inammissibile. Se l'holdout perde, scarto o nuovo ciclo di ricerca con nuovo holdout. Se funziona, richiede una propria identità algoritmica e una decisione esplicita di promozione.

### R6-E — Tutte le famiglie falliscono

Consegnare un blocker con migliori candidati, costi, vincolo violato e condizioni che potrebbero cambiare la fattibilità. Proporre una sola decisione motivata: diverso budget RAM, nuova architettura, oppure mantenimento della baseline. Non alleggerire target, accuracy o soglie in autonomia. Le ottimizzazioni di costo già promosse restano valide, ma l'obiettivo “meno iterazioni” resta aperto.

## 11. R7 — Conferma finale e integrazione nel prodotto

Checklist di chiusura:

- [ ] Un solo HEAD finale, build Release verificata, flag research non attivi accidentalmente.
- [ ] Unit/integration/regressioni pertinenti e suite completa eseguiti; risultati e casi indisponibili espliciti.
- [ ] API, CLI e GUI risolvono il profilo previsto; test dei nuovi progetti e dei progetti legacy.
- [ ] Tutte le fixture e H sul medesimo profilo e binario, dati grezzi conservati.
- [ ] Iterazioni, solver, wall, p95, lavoro equivalente e memoria riportati separatamente.
- [ ] Gate RAM GTO+ superato su perimetro documentato; display e RAM effettiva non confusi. Nessun PASS complessivo con comparabilità richiesta ancora irrisolta.
- [ ] Cold/warm, preprocessing e persistenza dichiarati; nessun costo nascosto.
- [ ] Startup escluso dai gate; build-to-ready, solve-to-consultable e build-to-consultable misurati con confini omogenei. Nessun lavoro specifico del caso spostato prima del timer.
- [ ] Checkpoint/resume e recupero verificati; nessuna conversione algoritmica implicita.
- [ ] Audit anti-specializzazione superato nel perimetro dichiarato.
- [ ] Documentazione matematica, numerica, CLI/GUI, formati, performance e stato aggiornati.
- [ ] Decisione di promozione e rollback del cambiamento documentati.

Rollback: revert mirato della modifica problematica, conservando evidenze e file utente; per formati nuovi spiegare quali versioni possano ancora leggerli. Revert del codice non garantisce downgrade dei file. Se l'algoritmo precedente non può leggere il nuovo stato, conservare il lettore compatibile o dichiarare chiaramente il limite.

Esiti finali distinti: `PRODUCT_COST_IMPROVED`, `PRODUCT_CONVERGENCE_IMPROVED`, `JOINT_OBJECTIVE_MET`, `BLOCKED_WITH_EVIDENCE`. `JOINT_OBJECTIVE_MET` richiede anche la parità RAM GTO+ dimostrata; risultati di costo/convergenza senza tale gate sono parziali. Non confondere una milestone con la chiusura dell'intero obiettivo.

## 12. R8 — Riprendere la parity

Il successo contro la nostra baseline non equivale al superamento dei limiti GTO+. Riprendere il [parity journey](GTO_PLUS_PARITY_JOURNEY.md) con la configurazione congelata e metriche omogenee.

Diramazioni: correttezza passa ma timing no → nuovo R3 con il divario residuo; memoria esterna non comparabile → mantenere `NOT_EVALUATED_COMPARABILITY_UNRESOLVED`, non inventare un PASS; evidenza esterna stale/incompleta → raccogliere evidenza valida prima di concludere. F11+ non viene dichiarata sbloccata da questa roadmap.

## 13. R9 — Estensioni future oltre l'architettura postflop

Ramo separato per preflop e ulteriori ambiti, attivabile dopo decisione e prerequisiti di roadmap del prodotto. Astrazione/bucketing e subgame postflop sono stati spostati in R2-S: non attendono R8 o il successo prestazionale finale. Non è il ripristino automatico delle modifiche eliminate.

**Architettura prevista: ProductionDcfr come motore anche per gioco astratto, bucket e subgame.** Questi componenti determinano rispettivamente rappresentazione e decomposizione del gioco; DCFR determina gli aggiornamenti iterativi. Non esiste una dipendenza da CFR+. Le note R9-A/B sotto restano riferimenti ai requisiti ora eseguiti in R2-S, non fasi successive alla parity.

Prima dell'implementazione scegliere esplicitamente fra: percorso non astratto corrente; gioco astratto risolto da DCFR; blueprint DCFR con subgame DCFR; combinazione dei due. Configurazione e file devono identificare sia algoritmo sia astrazione/boundary. Non trasferire automaticamente le garanzie del gioco non astratto al gioco bucketizzato, soprattutto se l'astrazione perde perfect recall; la sicurezza del resolver dipende anche dai vincoli al confine e dall'accuratezza raggiunta.

### R9-A — Requisiti subgame trasferiti a R2-S/S2

Sequenza: specifica public state e boundary → piccoli giochi con BR globale → blueprint versionato → boundary CFV compatibili con informazioni private/range → resolver con vincoli → splice → verifica globale → persistenza → costo end-to-end.

Gestire zero reach, azioni off-tree, blueprint approssimato e limiti di sicurezza. Se i boundary sono stime, dichiarare l'errore e le garanzie effettive. Se verifica globale e costruzione del blueprint annullano il beneficio, non vendere il tempo del solo subgame come speedup root.

### R9-B — Requisiti astrazione/bucketing trasferiti a R2-S/S1

Nella fase anticipata R2-S/S1: specificare metrica, clustering, street, policy globale e versione. Testare card removal e correlazioni fra history/private hands; la semplice somma dei pesi non prova equivalenza al gioco originale.

Gate distinti: errore d'astrazione, convergenza interna, BR/dEV nel gioco non astratto dove fattibile, tempo totale, RAM. Risultati “più veloci” perché risolvono un gioco meno dettagliato sono un diverso compromesso di prodotto, non automaticamente parity.

### R9-C — Preflop e altri algoritmi

Preflop: resource estimator → piccoli giochi enumerabili → action abstraction dichiarativa → decomposizione → riuso postflop → storage/checkpoint di grandi soluzioni. Non assumere che la qualifica postflop dimostri prestazioni preflop.

MCCFR/reti di valore richiedono campionamento, seed, varianza e garanzie dichiarate: confronti separati dal solver enumerato. GPU e multiway non sono scorciatoie di questa roadmap CPU HU; richiedono autorizzazione e specifiche autonome.

## 14. Scheda obbligatoria per attività e handoff

Ogni attività deve registrare:

| Campo | Contenuto obbligatorio |
|---|---|
| ID e fase | Nome strutturale, mai il nome della fixture come selettore |
| Stato | Non avviato / in corso / inconcludente / respinto / qualificato / promosso |
| Prerequisiti | Gate e commit da cui dipende |
| Ipotesi e alternativa | Costo eliminato e perché gli studi precedenti non bastano |
| Autorità | Modifica consentita o decisione ancora necessaria |
| File e API | Superficie minima coinvolta, caller e formato impattati |
| Invarianti | Matematica, precisione, determinismo, compatibilità |
| Byte model e ceiling | Picco, regime, costo evitabile, overhead |
| Esperimento | Manifest, corpus D/V/H, seed, thread, campioni e stop rule |
| Risultati | Dati grezzi, errori, tutti i fallimenti, confronto SH/B0/Bk e conferma sul corpus H |
| Decisione | Motivo del PASS/FAIL/INCONCLUSIVE, senza media fra fixture |
| Recupero | Revert mirato e compatibilità dei file prodotti |
| Prossimo passo | Una sola attività prioritaria con criterio di completamento |

## 15. Stato iniziale e prima milestone

| Milestone | Stato alla stesura | Evidenza necessaria per chiuderla |
|---|---|---|
| Audit e piano iniziale | Documentati nel file collegato | Audit mirato, limiti e fonti già riportati |
| R0 | Chiuso | Manifest e contratto congelati |
| R1 | Chiuso | Profilo condiviso e compatibilità testata |
| R2 | Chiuso come baseline; parity fallita | Baseline contemporanea e audit |
| R2-S | Chiusa come fattibilità A0; S1/S2 isolate e S3 combinata verificate | Candidato non promosso: più lento su D/V, errore V dichiarato, product exact invariato |
| R3 | Chiusa | Mapping 46–57% e training 43–51% del wall A0; contatori e timer pubblicati |
| R4–R5 | R4-A1/B1 respinte; R5 non applicabile | Pre-gate D falliti e composizione ancora 3,47–4,07x più lenta dell'exact |
| R6 | Chiusa con R6-E | RBP senza finestre e senza prova DCFR; lazy incompatibile con la traiettoria ProductionDcfr |
| R7–R8 | Non raggiunte | Nessun candidato per H/R7; tempo TH/TST e comparabilità RAM restano aperti |
| R9 | Futuro per preflop e ulteriori ambiti; A/B trasferiti a R2-S | Specifica e decisione dedicate alle estensioni |

**Esito finale: `BLOCKED_WITH_EVIDENCE`. Il prodotto resta ProductionDcfr exact;
H non è stato aperto perché nessun candidato ha superato D/V.**

Validazione finale 2026-09-06: build Release PASS, phase7 `216` asserzioni,
phase10 bucket `763.823`, phase10 R6-RBP `8` e CTest completo `33/33` in
`270,43 s`. I due runner phase10 passano anche sotto ASan senza diagnostiche.

## 16. Avanzamento dell'implementazione

Il registro verificabile è
[`PRODUCTION_DCFR_PRODUCT_OPTIMIZATION_EXECUTION_2026-09-05.md`](PRODUCTION_DCFR_PRODUCT_OPTIMIZATION_EXECUTION_2026-09-05.md).
Al 2026-09-06 R0 è completata con il corpus H v2 deterministico, disgiunto e
sigillato; R1 è implementata e verificata su API, CLI, persistenza, benchmark e
GUI Qt Release/E2E. R2 ha una baseline controllata di 15 processi: correttezza e
crossing interni passano; TH e TST falliscono il gate tempo e il gate RAM resta
non valutabile. I timer product sono esposti sia nello schema
report 2 sia nel benchmark v4 con range reali. R2-B è chiusa con
riserializzazione, rinomina, permutazione dei semi, perturbazioni di
range/stack/sizing e soglia di residency. Tutte le run B0 accettate hanno
superato il preflight CPU/RAM. S0 è chiusa dal contratto versionato per mapping
bucket e boundary CFV. S1 include ora il traversal postflop ProductionDcfr
bucketizzato, la certificazione nel gioco originale e un checkpoint 1.0
checked/atomico con resume byte-identico. Il mapping `made_hand_value` riduce gli
infoset di circa 14,1x e usa 4.700.960 B su D e 9.242.624 B su V. S2 include un
bridge exact e bounded dai river postflop al contratto `FiniteGame`, con deal
privati pesati, boundary CFV, gadget opt-out, splice e BR globale. S3 combina
mapping e resolve: la fixture riduce 12 infoset a 4 e certifica la strategia
sollevata nel gioco originale. A0 non è production-qualified: D/V sono più
lenti dell'exact e V ha NashConv `0,00330981` contro `0,00175170` exact, oltre a
delta di profilo e BR. Il percorso product resta exact e invariato.

R3 attribuisce il costo A0 a mapping e training; R4-A1/B1 falliscono i kill gate
predefiniti e sono state rimosse. R6-A/B sono respinte perché non preservano o
non dimostrano la traiettoria ProductionDcfr. R7/R8 non sono state raggiunte e
la RAM GTO+ resta non comparabile.

## 17. River bucket-native — follow-up separato

Stato: **`FEASIBILITY_ONLY`, implementato e validato localmente**. Questo ramo
nasce dopo la chiusura R6 su autorizzazione esplicita e non cambia l'esito della
roadmap exact. Riduce il gioco attraversato accettando card abstraction.

1. `build_fixed_river_bucket_game` raggruppa ogni range per `HandValue` finale
   e conserva la massa congiunta delle sole coppie di combo compatibili.
2. Il kernel dedicato esegue ProductionDcfr `1.5/0/3` sul prodotto fra coppie
   di bucket e albero pubblico, senza deal fisici nell'hot path.
3. Query e lift riportano la strategia alle combo; l'oracolo fisico calcola BR
   e NashConv nel gioco originale.
4. `GTOSD_RIVER_BUCKET_CHECKPOINT 1 0` separa source, astrazione e identità del
   gioco; checksum, save atomico e resume continuo/segmentato sono verificati.
5. Il `FiniteGame` materializzato è soltanto un oracle opzionale e non rientra
   nel percorso o nel byte model nativo predefinito.

Evidenza Release MSVC 18.8 x64: D riduce 8.640 deal a 90 coppie e impiega una
mediana di 3,1269 ms contro 7,2651 ms exact; V riduce gli stessi deal a 70
coppie e impiega 3,0401 ms contro 8,0537 ms exact. La NashConv originale sale
rispettivamente da 0,000202927 a 0,00377002 e da 0,0000145436 a 0,000511609.
Il test mirato passa 313 asserzioni; i tre target correlati passano sia Release
sia AddressSanitizer, dove la riconferma finale del target river termina in
211,36 s senza
diagnostiche.

Il CTest Release completo sullo stato di quel checkpoint passa 33/33 in
279,25 s. Dopo i qualificatori v1 e v2, lo stato corrente passa 35/35 in
260,92 s.

### Qualifica indipendente v1

Il corpus River v1 è stato congelato prima del primo solve, con sette strati e
SHA-256
`7E542407BCCF598BA5E3A0CB5A3F8ECCEBF0DE3D8DB70A79E99D1DDFE73F2619`.
Le soglie richiedono per ogni fixture: exact NashConv <= `0,001`, bucket
NashConv <= `0,005`, delta NashConv/profile/BR <= `0,005`, speedup operativo >=
`1,25x`, riduzione nodi >= `10x` e byte model <= `1 MiB`.

Decisione: **`REJECTED`, 0/7 fixture qualificate**. Bucket NashConv e delta
NashConv falliscono 7/7; speedup passa 3/7; riduzione nodi 6/7; profile value e
byte model passano 7/7. L'exact supera il proprio gate in 6/7: il caso rake
resta anche `ORACLE_NOT_CONVERGED` a 256 iterazioni, senza annullare gli altri
fallimenti della stessa fixture. Release e AddressSanitizer producono gli
stessi valori matematici e nessuna diagnostica sanitizer.

Kill gate chiuso: il kernel `made_hand_value` non viene promosso, collegato a
CLI/GUI/`.gtsd` o esteso a Turn/preflop. Un nuovo candidato richiede bucket
blocker-aware, costi di costruzione più bassi e un nuovo holdout indipendente.
Il corpus v1 resta congelato come regressione e non viene usato per scegliere
feature o soglie.

## 18. River v2 blocker-aware lossless — decisione

Stato: **`REJECTED_FEASIBILITY`, implementato e qualificato senza promozione**.
Il candidato v2 conserva una strategia comune soltanto fra combo con identico
`HandValue` finale e identica compatibilità contro tutte le combo avversarie
attive. Il kernel, i fingerprint e le chiavi infoset hanno identità distinta da
`made_hand_value_v1`; il default v1 e il percorso exact non cambiano.

Il manifest v2, congelato prima del solve, comprende le sette regressioni v1 e
cinque nuovi holdout. SHA-256:
`F3E262337D5481651D80101C6AD9A09AD8BBC146379FE10D794B169ACED95B14`;
fingerprint runner: `fnv1a64:812e7edf4d682820`.

| Gate | Risultato Release |
|---|---:|
| Classi e nodi ridotti | FAIL 12/12; rapporto `1,00x` |
| Speedup operativo exact/native | FAIL 12/12; `0,006709x`–`0,017259x` |
| Byte model `<= 1 MiB` sul corpus | PASS 12/12; massimo 793.104 B |
| Gate di qualità nel gioco fisico | PASS 11/12 |
| Decisione congiunta | `REJECTED`, 0/12 |

La fixture con rake fallisce il gate exact a 1.024 iterazioni e il confronto BR;
le altre undici rispettano profile value, BR, NashConv exact/candidato e delta.
Il test full-range mostra il limite strutturale senza dipendere dal corpus: 465
classi per 465 combo per player e 188.790 coppie per 188.790 deal.

Validazione finale: test River Release PASS con 338 asserzioni; ASan PASS in
209,04 s; preflight v2 Release e ASan PASS; CTest Release completo 35/35 in
260,92 s. Report Release SHA-256:
`B872A274662FA485A121D75E6795505C10BCBF7EF0DEF9C9DE715D17F449F459`.

Il ramo River lossless è chiuso su questa relazione. Non viene esteso a Turn o
preflop e non sblocca F11+. Exact resta ProductionDcfr di prodotto. Una futura
proposta deve partire da automorfismi congiunti provati oppure dichiararsi
approssimata e usare un nuovo holdout.

## 19. River lossless generale — gate conclusivo

Stato: **`BLOCKED`, analisi completata senza implementare un nuovo kernel**.
La partizione equa pesata è il quoziente lossless più permissivo valutato per
il River fisso: preserva il valore finale e raffina le classi finché ogni combo
vede la stessa massa avversaria compatibile in ogni classe opposta.

| Evidenza | Full range uniforme | Corpus realistico v2 |
|---|---:|---:|
| Combo attive per player | 465 | 96 oppure 112 |
| Classi stabili per player | 45 | 96 oppure 112 |
| Deal fisici | 188.790 | invariato per fixture |
| Coppie di classi | 2.005 | uguali ai deal fisici |
| Riduzione di coppia | 94,1596x | 1,00x in 12/12 |

Il full range conferma che l'algoritmo trova simmetrie quando i range le
possiedono. Il corpus configurabile mostra che pesi e blocker asimmetrici le
distruggono. Il kill gate `>= 1,25x` fallisce 12/12, quindi un kernel v3 non
avrebbe margine strutturale e non viene scritto. Il ramo River exact compresso è
chiuso; un River approssimato resta una decisione di prodotto separata.

R9-C può ora iniziare con specifica HU preflop, resource estimator e scelta
esplicita fra gioco exact bounded, decomposizione e astrazione misurata. Questo
risultato River non dimostra la fattibilità preflop e non va riutilizzato come
bucketing preflop implicito.

Validazione finale corrente: CTest Release `36/36` PASS in `267,95 s`; test
postflop più preflight equo ASan `2/2` PASS in `198,94 s`.

## 20. Avvio R9-C — unico benchmark HU preflop

Il riferimento `GTP-HU-PREFLOP-CO40-001` sostituisce ogni ipotesi di suite
preflop esterna: stack 40a, root CO, raise totali 6a/10a, all-in, call, fold,
strategia sulle 81 classi ed EV `-0,3a`. `PRE-TINY` resta un test matematico e
non un benchmark di prodotto.

Il 9 settembre 2026 l'utente ha identificato la sorgente come una versione
modificata di MonkerSolver, ha confermato il ranking GTOSD
`colore > full house` con scala `A-6-7-8-9` e un tempo di solve certamente
inferiore a 12 ore. Versione, iterazioni, criterio di arresto e NashConv del
run esterno non sono disponibili.

Le continuazioni confermate sono ora una configurazione separata e
dichiarativa: raise-to 10,5a contro 6a, raise-to 14,5a contro 10a, struttura
speculare dopo limp e 33/66/120/all-in su ogni street. La rake è zero. Il
raise-to 14,5a è una deroga esplicita alla minimum raise standard; il default
del core resta invariato.

Il resource gate passa. L'albero ha 58 nodi preflop e nove ingressi postflop;
lo scheletro postflop aggregato contiene 30.324 nodi. La profondità massima è
15, il massimo osservato è quattro raise e nessun ramo raggiunge il limite di
sicurezza 63. Lo stack termina quindi il betting senza un cap di prodotto.

Il primo solve HU preflop è stato eseguito. Il candidato R9-C v1 usa 81 classi
exact preflop, deal fisici, bucket postflop categoria/equity, external-sampling
DCFR `1.5/0/3`, showdown exact e rake zero. Il run 20k termina in 13,810 s con
1.444.270 infoset; il run 100k termina in 75,141 s con 6.602.077 infoset.

Il byte model misura 136 B minimi per infoset, escluso l'overhead della hash
table. Il payload blueprint cresce da 196.420.720 B a 20k a 897.882.472 B a
100k; blueprint più risposte richiedono almeno 206,45 MiB e 949,06 MiB.

Decisione: **`REJECTED`**. A 100k l'EV CO è `+0,5958a ±0,1027a` contro
`-0,3a`; la MAE action/class è `21,763 pp` e la TV media è `54,407 pp`.
La NashConv non è certificata: il comparatore richiede ora anche
`nashconv_certified=true`, impedendo che un lower bound campionato pari a zero
superi il gate.

R9-C v1 completa il milestone “primo solve” ma non qualifica il solver. R9-C v2
deve ridurre la crescita degli infoset, sostituire i bucket equity-MC8 con una
rappresentazione postflop misurata e produrre una BR convergente. Un run più
lungo dello stesso v1 è respinto dai dati 20k/100k.

Validazione finale del milestone: `39/39` test Release PASS in `251,60 s`;
parser, albero, resource gate, solve smoke e preflight passano `3/3` sotto
AddressSanitizer in `3,57 s` senza diagnostiche.

## 21. R9-C v2 — oracle ridotto CFV/RSS

Il primo gate di R9-C v2 è chiuso: il bucket postflop v1 è stato confrontato
con exact su sette giochi River ridotti, sollevando il profilo bucket nello
stesso gioco fisico. Il runner misura strategia root, CFV di strategia e
d'azione, BR/NashConv originale, byte model e Peak RSS campionato a 1 ms.

Esito: **`REJECTED` 7/7**. La riduzione dei nodi è `8,34x–892,33x`, ma la
NashConv bucket nel gioco fisico è `0,5506%–5,3387%`; la TV root media arriva
al `37,26%`, l'errore CFV medio al `7,41%` del pot e quello massimo d'azione
all'`88,76%`. Il Peak RSS del workflow ridotto è `107.356.160 B`, separato dal
byte model e non trasferibile al gioco preflop completo.

Il risultato impedisce di usare `made_hand_value_v1` come base di R9-C v2. Il
prossimo candidato deve modellare distribuzioni future, blocker e transizioni,
oppure decomporre il gioco con boundary CFV verificabili. Exact resta l'oracolo
e nessun default di prodotto cambia.

Validazione finale del gate: test mirati `2/2` PASS; suite Release completa
`40/40` PASS in `245,99 s`; smoke AddressSanitizer PASS in `22,27 s` senza
diagnostiche.

## 22. R9-C v2 — showdown distribution v3

Il candidato successivo sostituisce la sola categoria/equity con `HandValue`
River esatto, distribuzione pesata sulle nove categorie avversarie e masse
loss/tie/win. Il quantum 5% e cinque holdout sono stati congelati prima del
primo solve. Mapping, quantum e partizione hanno identità versionata.

La qualifica conclude **`REJECTED` 12/12**, inclusi 0/5 holdout. Profile-value e
byte model passano 12/12, ma NashConv bucket passa 7/12, delta NashConv 2/12,
best response 6/12, riduzione nodi 5/12 e speedup 0/12. Il range osservato è:
riduzione `1,18x–18,57x`, speedup `0,008x–0,131x`, NashConv fisica fino al
`2,6527%`, TV root fino al `32,37%` e CFV media fino al `4,07%` del pot.

V3 non entra nel solver HU preflop: retuning del quantum sul corpus congelato è
vietato e un'altra firma marginalmente più grossolana ripeterebbe il trade-off
fra errore e troppe coppie. R9-C prosegue soltanto con una specifica di
decomposizione: public root Flop, range condizionati, boundary CFV, budget RAM e
certificazione dell'intero gioco.

Validazione: suite Release `42/42` PASS in `240,17 s`; target postflop e smoke
v3 AddressSanitizer `2/2` PASS in `302,91 s` senza diagnostiche. Report:
`benchmarks/results/river_showdown_distribution_qualification_2026-09-07.json`.

## 23. R9-C v2 — decomposizione exact a boundary River

Il primo contratto di decomposizione è implementato senza card abstraction.
Una policy preflop densa sulle 81 classi propaga reach `float64` alle 630 combo
fisiche. Ogni task lega fingerprint del tree, blueprint, entry node, flop
canonico e molteplicità fisica. Il catalogo contiene 573 flop canonici e 5.157
entry/flop task contro 64.260 root fisiche.

Il piano Flop-only è respinto: il massimo ingresso richiede 7.843.579.392
action entry, 31.374.317.568 B di stato e 32.528.500.840 B di picco modellato.
La decomposizione annidata al River conserva 262.408.000 B di stato Flop/Turn
e porta il lower bound del working set attivo a 637.032.404 B. Il massimo
subgame River contiene 260.400 action entry e 1.041.600 B di stato compresso.

Il boundary oracle postflop usa la strategia media ProductionDcfr e restituisce
CFV condizionali per combo con counterfactual reach separata. Ricompone il
profile value dello smoke River con errore `6,49e-15`; un test asimmetrico
frazionario copre la mappa player-local del DAG canonico e il rifiuto di un
checkpoint con range differenti.

Decisione corrente: **proseguire con River nesting exact**. Il risultato è un
gate RAM, non una qualifica del solver. Restano obbligatori orchestrazione e
checkpoint dei task, aggiornamento delle street superiori, solve convergente,
BR/NashConv globale e confronto con `GTP-HU-PREFLOP-CO40-001`. La singola
iterazione River con NashConv normalizzata `3,0992` non è evidenza di
convergenza.

La persistenza 1.0 di blueprint, piano e boundary è implementata con checksum,
scrittura atomica e fingerprint semantici. Un certificatore globale conta
10.314 boundary, verifica copertura di massa e accetta soltanto evidenza di best
response globale exact. Con il catalogo incompleto restituisce
`INCOMPLETE_BOUNDARY_COVERAGE`; una BR campionata non può superare il gate.

Il controllo alternativo external-sampling fisico lossless è respinto. A 100k
iterazioni visita 8.996.964 infoset, richiede almeno 1.295.562.816 B di payload,
produce EV CO `+0,7437a` ed errore medio strategico `21,627 pp`. Eliminare i
bucket senza decomporre non crea abbastanza riuso degli infoset. Non verrà
esteso con più iterazioni dello stesso schema.

Validazione corrente: Release `42/42` PASS in `153,10 s`; ASan mirato `3/3`
PASS in `487,21 s`, con riconferma HU dopo il certificatore PASS in `9,13 s`.

## 24. R9-C v2 — rappresentabilità file-backed e scheduler River

Il backend exact `ScaledUint16RegretStrategy` può ora usare, solo con percorso
e budget espliciti, uno stato temporaneo mappato su file. Il default resident
del prodotto non cambia. Il probe Turn alloca logicamente `238.611.936 B`,
completa in `2,87843 s` e raggiunge `18.939.904 B` di Peak RSS. Il probe Flop
peggiore rappresenta `7.843.579.392` action entry e `31.426.437.952 B` di stato
logico. Una singola traversata richiede `1.285,694435 s`; solve più
certificazione richiedono `9.719,15 s`. Il Peak RSS è `608.006.144 B`, mentre
il ledger massimo solver-owned è `32.032.088.249 B` perché include il backing
non residente. Il file temporaneo viene eliminato alla chiusura.

Il probe dimostra rappresentabilità entro la RAM, non fattibilità temporale né
convergenza: dopo una sola iterazione la NashConv normalizzata è `4,26804`.
Il percorso Flop monolitico resta quindi respinto. Il bug che ometteva la
dispatch specializzata dei nodi River a sei azioni è stato corretto e coperto
da regressione.

Il work estimator River conta `369.072` board history canoniche, `993` betting
history e `732.976.992` boundary resolver considerando entrambi i player.
Materializzarle richiederebbe `9.288.284.442.624 B`; il piano lo vieta. Lo
scheduler usa payload da 64 MiB: massimo `5.294` boundary per batch, `141.687`
batch task-aligned e `836` boundary nell'ultimo batch. Ogni batch conserva
intera la coppia dei resolver: le boundary per lato sono `732.976.992`, ma gli
stati pubblici River da risolvere una sola volta sono `366.488.496`. Il catalogo conserva la molteplicità di
tutte le `7.539.840` board history fisiche.

L'ordine v3 è `entry/flop/river-history/runout/resolver-pair`. Produce `5.157` span
contigui, uno per task canonico preflop→Flop, così l'esecutore può ridurre e
rilasciare un accumulatore upper-street prima del task successivo. I boundary
Flop persistiti sono `10.314` e richiedono `130.699.008 B`; il precedente
valore `1.628.605.440 B` descrive invece le `64.260` frontiere fisiche senza
isomorfismo. Entrambe le misure restano esposte con nomi distinti.

L'accumulatore numerico River è ora task-local e usa somme compensate. Riduce
in ordine deterministico reach e `reach × CFV`, copre le 528 combo vive al
Flop e persiste sia lo stato parziale sia l'aggregato con fingerprint, checksum
e scrittura atomica. L'aggregato rappresenta però soltanto la continuazione
River: non è ancora una boundary Flop completa.

Restano aperti la generazione delle reach alle history River, i contributi
terminali di Flop e Turn, l'esecuzione convergente dei task e la best response
globale exact. Nessuna metrica locale o singola iterazione può chiudere questi
gate.

Il benchmark Release misura `0,0026794 s` di sola traversata per una iterazione
sul subgame River peggiore. L'estrapolazione diagnostica sui `366.488.496`
stati pubblici è `981.969 s`, cioè `11,37 giorni` seriali o `1,42 giorni` anche
ipotizzando scaling ideale su otto worker, prima di preparazione, riduzione e
iterazioni successive. Non è un tempo certificato dell'intero sweep, ma basta
a respingere il solve exact indipendente di ogni root come percorso di
produzione: il riferimento modificato MonkerSolver ha completato lo spot in
meno di 12 ore. Questo confronto boccia la granularità root-per-root locale,
non l'exact in sé. Scheduler e accumulatore restano oracle bounded-memory; il
primo solve HU 40a deve riusare informazione fra root tramite una struttura
condivisa, sampling o approssimazione esplicita e poi essere confrontato con le
boundary exact.

## 25. R9-C v2 — certificazione globale streaming

Il certificatore non richiede più la materializzazione contemporanea delle
10.314 boundary Flop. Un ledger versionato conserva un mask a due bit logici e
la probabilità fisica per ciascuno dei 5.157 task canonici. Ogni boundary viene
validata, registrata e può essere rilasciata subito. La probabilità del task
entra nella copertura soltanto dopo la ricezione dei lati CO e BTN.

Il piano di decomposizione 1.2 espone quattro misure separate. I mask occupano
`5.157 B`, le probabilità `41.256 B` e il ledger `46.413 B`; con una boundary
da `12.672 B`, il payload vivo della certificazione è `59.085 B`. Il confronto
corretto resta `59.085 B` streaming contro `130.699.008 B` per tutte le
boundary canoniche e `1.628.605.440 B` per le frontiere fisiche non ridotte.
Queste misure escludono overhead di vector, stringhe e allocator.

Il checkpoint `gtosd.hu_preflop_whole_game_coverage.v1` registra identità di
tree, blueprint e piano, target NashConv, mask, probabilità, contatori, massa
coperta, hash incrementale dello stato e catena dei contributi. Il file usa
checksum e sostituzione atomica. Il resume viene validato di nuovo contro il
catalogo canonico prima della finalizzazione.

Il fingerprint del profilo di continuazione combina posizione canonica e
fingerprint di ogni boundary, quindi non dipende dall'ordine con cui i worker
consegnano i risultati. La best response globale 1.1 deve legarsi a questo
fingerprint oltre che a tree e blueprint. Una BR prodotta su un diverso insieme
di continuazioni viene rifiutata prima di valutare NashConv.

Il test Release percorre tutte le `10.314/10.314` boundary, completa
`5.157/5.157` task e ricompone l'intera probabilità postflop. La finalizzazione
resta `GLOBAL_BEST_RESPONSE_MISSING` senza BR globale e
`GLOBAL_BEST_RESPONSE_NOT_EXACT` con evidenza campionata. La copertura completa
non viene quindi confusa con convergenza. Il test copre anche BR exact sopra e
sotto soglia; soltanto copertura completa, identità coincidente e NashConv entro
target restituiscono `CERTIFIED`. Test HU più preflight: `2/2` PASS in
`30,13 s`. Il benchmark di decomposizione passa da solo in `3,71 s` e dentro
CTest in `3,42 s`; la suite Release completa passa `42/42` in `255,69 s`.
Il target HU preflop passa sotto AddressSanitizer in `300,37 s` senza
diagnostiche. Fingerprint piano: `fnv1a64:55a80db03369199d`.

Restano aperti la produzione delle boundary Flop complete dai contributi
terminali Flop/Turn e dagli aggregati River, il solve che riusa informazione fra
root e la best response globale exact. Nessun default production è cambiato.

## 26. R9-C v2 — lift orbitale delle combo e reach replayabile

La molteplicità di un board canonico non può essere applicata a una CFV
per-combo lasciando invariato l'indice privato. Il caso `flop=0,1,2`,
`turn=3`, `river=4` lo rende visibile: la molteplicità globale è 12, composta
dall'orbita del Flop e da tre runout che ne stabilizzano il rappresentante. I
tre runout bloccano semi diversi della stessa classe privata.

Il bridge River 1.1 solleva ora ogni runout nell'orbita dello stabilizzatore del
Flop, applica la stessa permutazione alle combo private e soltanto dopo applica
la molteplicità del Flop. Ogni boundary intermedia espone 528 righe nelle
coordinate del Flop rappresentante; le 465 righe del solver River restano
corrette solo prima del lift. L'accumulatore non moltiplica più una seconda
volta la boundary già sollevata.

L'oracolo unitario attraversa tutte le orbite River del primo task. Per ognuna
delle 528 combo vive verifica esattamente
`molteplicità Flop × 31 × 30 × C(29,2)`, con `C(29,2)=406`. Il test impedisce
che una futura ottimizzazione conservi la massa totale ma alteri i blocker per
seme.

Le history che raggiungono Turn e River contengono ora l'intera sequenza di
azioni, non soltanto un hash. Il replay ricostruisce lo stato pubblico e
rifiuta una path manomessa. Un propagatore separato moltiplica la probabilità
della strategia media solo nella reach del giocatore che agisce, applica i
blocker sul Flop e sul Turn e rifiuta valori fuori `[0,1]`. Questo chiude il
contratto di generazione delle reach. Il provider convergente della strategia
upper-street resta da collegare; i terminali Flop/Turn sono chiusi nella fase
27.

Il catalogo terminale upper-street enumera 3.792 history replayabili: 612
terminano sul Flop e 3.180 sul Turn; 2.388 finiscono con fold e 1.404 con
all-in runout. Questi conteggi costituiscono il manifesto strutturale coperto
dall'assemblatore della fase 27.

In questa fase il cambio era incompatibile con gli accumulatori River v1/v2:
batch plan 1.5, root boundary 1.2 e checkpoint accumulator/aggregate v3 li
rifiutavano. La fase 29 sostituisce poi root boundary 1.2 e checkpoint v3 con
1.3/v4 per distinguere profilo e BR. Il record denso sale da 11.160 a 12.672 B; la proiezione di
materializzazione sale a 9.288.284.442.624 B. Con 64 MiB, il piano usa 5.294
boundary per batch, 141.687 batch e 836 boundary finali. I test mirati Release
passano `3/3` in `38,93 s`; la suite Release completa passa `42/42` in
`258,86 s`. Il target HU preflop passa sotto AddressSanitizer in `362,36 s`
senza diagnostiche. Nessun default di produzione è cambiato.

## 27. R9-C v2 — terminali exact e assemblatore Flop task-local

La riduzione River include ora due fattori distinti: counterfactual reach
dell'altro player e reach delle azioni postflop del player di cui si accumula
la CFV. La versione precedente ometteva il secondo fattore e avrebbe
sovrastimato i rami raggiunti con frequenza mista. Batch plan 1.5, root boundary
1.2 e accumulator/aggregate v3 rendono il cambio fail-closed.

Il valutatore upper-street replaya le 3.792 history del manifesto e calcola
utility exact per fold e all-in. I fold Flop e gli all-in Flop rappresentano
812 runout ordinati per deal privato; sul Turn vengono enumerati i 28 river
compatibili. Board, blocker, ranking Short Deck, rake e molteplicità orbitale
restano fisici.

L'assemblatore task-local parte da un aggregato River validato, consuma ogni
terminale secondo l'ordinale stabile e usa somme compensate. Non finalizza se
manca una entry, se l'ordine cambia o se una combo non conserva esattamente la
massa `molteplicità Flop × 812 × reach preflop compatibile`. Al successo emette
le due boundary Flop richieste dal ledger globale.

Il test HU Release passa con 8.965 asserzioni; la suite Release completa passa
`42/42` in `317,59 s` e lo stesso target passa sotto AddressSanitizer senza
diagnostiche. Restano aperti il provider convergente della strategia Flop/Turn,
l'iterazione fra upper game e subgame e la best response globale exact. Nessun
default di produzione è cambiato.

## 28. R9-C v2 — CFV best response exact per combo

La certificazione postflop aggregata non bastava per decomporre la BR globale:
servono i valori della deviazione per ciascuna combo fisica. Il solver espone
ora un report `ExactBestResponse` distinto dal report `AverageStrategy`. Per
entrambi i player applica la best response contro la strategia media del
checkpoint, conserva counterfactual reach e combo fisica e ricompone il valore
BR autorevole entro `1e-9`.

Il bridge River della strategia media accetta soltanto il report
`AverageStrategy`; passargli il report BR fallisce chiuso. Questo impedisce di
abbassare artificialmente NashConv mescolando valori di profilo e valori di
deviazione. I test mirati Release HU preflop e phase10 passano `2/2` in
`126,01 s`; `phase10` passa anche sotto AddressSanitizer con 776.083 asserzioni
e nessuna diagnostica.

Restano da implementare il lift orbitale e l'accumulazione dedicata delle CFV
BR, la scelta ottima alle decisioni Flop/Turn e la ricorsione best-response nel
tree preflop. Nessun default di produzione è cambiato.

## 29. R9-C v2 — lift e accumulazione River della best response

Il bridge River accetta ora entrambi i report postflop: `AverageStrategy` ed
`ExactBestResponse`. Applica in entrambi i casi la stessa enumerazione
dell'orbita che stabilizza il Flop, permuta board e combo private insieme e
produce 528 valori nelle coordinate del Flop rappresentante. Non viene usata
una moltiplicazione scalare che perderebbe i blocker per seme.

Boundary root River, accumulatore e aggregato dichiarano
`HuPreflopContinuationValueMode`. Modalità e fingerprint devono coincidere a
ogni contributo; il canale di profilo rifiuta una boundary BR e viceversa. Gli
accumulatori e aggregati persistiti passano allo schema v4, che serializza il
tag e rifiuta i payload v3 privi di modalità. Il batch plan resta 1.5 perché
ordine, memoria e scheduling non cambiano; la root boundary passa a 1.3.

Il percorso BR viene finalizzato come aggregato River task-local. Non entra
nell'assemblatore Flop della strategia media: prima serve una ricorsione BR che
massimizzi alle decisioni del player rispondente e segua la strategia media
alle decisioni avversarie. Confondere l'aggregato BR con una continuation di
profilo abbasserebbe artificialmente la NashConv e viene quindi rifiutato.

Il test HU Release passa con 8.971 asserzioni; insieme al benchmark di
decomposizione i test mirati passano `2/2` in `99,29 s`. La suite Release
completa passa `42/42` in `318,07 s`; il target HU passa sotto AddressSanitizer
in `1.115,26 s` senza diagnostiche. Nessun default CLI, GUI, ProductionDcfr o
formato `.gtsd` è cambiato.

## 30. R9-C v2 — vista Turn-major per la ricorsione BR

Il catalogo River conserva l'ordine compatto esistente, ma può ora derivare per
un singolo task i gruppi di runout che condividono lo stesso Turn canonico.
Ogni gruppo registra offset, numero di board e massa fisica. Per ciascuna
history River genera uno span di ordinali che mantiene adiacente la coppia dei
resolver.

L'unione degli span Turn-major copre ogni root del task esattamente una volta.
La vista non copia boundary e non aggiunge stato globale: permette di elaborare
un Turn, ridurne tutte le continuazioni e liberarlo prima del successivo. È
necessaria perché il player può scegliere azioni diverse dopo Turn pubblici
diversi; massimizzare soltanto dopo la loro somma produrrebbe una falsa best
response.

Il test HU Release passa con 8.975 asserzioni e rifiuta cataloghi, task e
fingerprint di gruppo incompatibili; la suite completa passa `42/42` in
`333,03 s`. Restano da aggiungere le foglie terminali BR per Turn e il riduttore
`somma avversario / max rispondente`. Nessun default production è cambiato.

## 31. R9-C v2 — terminali preflop exact e best response completa del tree

La ricorsione `somma avversario / max rispondente` copre ora l'intero tree
preflop. Consuma nove valutazioni postflop, i 19 terminali fold e i dieci
terminali all-in. Ogni foglia conserva la reach della strategia avversaria,
esclude quella del player che devia e usa la stessa unità chance:
`C(32,3) × 29 × 28 = 4.027.520` runout ordinati per deal privato.

I fold usano il settlement exact del nodo. Gli all-in condividono una tabella
showdown exact per le 81 × 81 classi preflop: 19.998 board completi canonici
rappresentano tutti i 376.992 board fisici e 1.423.446.393.600 esiti
deal/runout. La tabella occupa 157.464 B; catalogo, tabella e scratch portano il
payload vivo modellato a 726.168 B. Il build Release è sceso da 633,252 s a
63,6706 s senza cambiare il fingerprint
`fnv1a64:fe73211ffab94a00`.

L'assemblatore rifiuta foglie mancanti, duplicate o prodotte con tree,
blueprint, continuazione, responder o iterazione differenti. Il primo test
d'integrazione ha individuato un errore nella durata dell'identità della
continuazione: la stringa veniva spostata prima delle verifiche dei terminali.
Il provider ora conserva una copia immutabile dell'identità durante tutta la
ricorsione. Il target HU Release passa con 9.073 asserzioni.

## 32. R9-C v2 — evidenza BR globale e calcolo NashConv

`HuPreflopGlobalBestResponseEvidence` passa allo schema 1.2. Registra
iterazione del blueprint, denominatore chance, valori BR dei due player, valori
del profilo e guadagni di deviazione. Il costruttore accetta soltanto due
valutazioni exact, una per player, con massa root completa e la stessa identità
di continuazione. Calcola
`g_i = BR_i - V_i(profile)`, rifiuta guadagni negativi oltre la tolleranza,
somma i due guadagni per NashConv e normalizza sullo stack effettivo.

Il certificatore ricostruisce tutte le relazioni numeriche dello schema 1.2 e
rifiuta un'evidenza incoerente anche se il fingerprint è stato ricalcolato. I
test coprono entrambi i responder, la massa fisica completa, il responder
duplicato e un valore di profilo superiore alla best response.

Questo chiude il percorso strutturale fino all'evidenza globale, ma non
certifica ancora il benchmark HU 40a. Mancano le nove valutazioni postflop
prodotte da un checkpoint convergente e i valori exact dello stesso profilo.
La fase 33 implementa il secondo punto per ogni continuation già disponibile;
senza checkpoint convergenti la NashConv del benchmark resta non disponibile.
La misura della fase 32 stimava 637.032.404 B di picco annidato e 960.053 s per
una singola sweep seriale di tutti i root River indipendenti. Nessun default CLI,
GUI, ProductionDcfr o `.gtsd` è cambiato.

## 33. R9-C v2 — EV exact del profilo e certificazione anti-manomissione

Il ledger globale passa allo schema 1.1 e conserva due contributi utility per
ognuno dei 5.157 task entry/Flop. La boundary del resolver 0 contribuisce al
valore del player 1; quella del resolver 1 contribuisce al valore del player 0.
Ogni contributo usa molteplicità fisica del Flop, reach della sequenza
avversaria, counterfactual reach e CFV del profilo. Lo stato usa un hash
indipendente dall'ordine di arrivo e lega tutte le boundary alla stessa
iterazione del blueprint.

Il piano di decomposizione 1.3 conta 82.512 B di utility, oltre a 5.157 B di
mask e 41.256 B di probabilità. Il ledger completo occupa quindi 128.925 B; con
una boundary da 12.672 B il payload vivo massimo è 141.597 B. Il nuovo
fingerprint del piano è `fnv1a64:8a0f8bf6b11f12ff`.

`HuPreflopExactProfileEvaluation` somma le 10.314 boundary e valuta i 19 fold e
i dieci all-in nelle stesse unità chance della best response globale. Il
validatore ricalcola i terminali dal catalogo e dalla tabella showdown exact:
un chiamante non può alterare il valore del profilo e ottenere una prova valida
rigenerando soltanto il fingerprint. La global BR 1.2 accetta l'artefatto solo
se tree, blueprint, piano, continuation, tabella all-in e iterazione coincidono.

La validazione Release passa con 9.076 asserzioni HU preflop, 776.083
asserzioni `phase10` e 42/42 test complessivi. Il target HU passa sotto
AddressSanitizer in 2.651,66 s senza diagnostiche. La validazione unica della
tabella all-in elimina il costo ripetuto per ogni terminale: il target HU passa da circa
quattro minuti a circa un minuto sulla stessa build. Il benchmark di
decomposizione è PASS, ma stima 1.037.860 s per una sweep seriale di tutti i root
River indipendenti. Resta quindi da produrre una continuation postflop
convergente con riuso fra root e certificare la NashConv del benchmark
`GTP-HU-PREFLOP-CO40-001`. Nessun default CLI, GUI, ProductionDcfr o `.gtsd` è
cambiato.

## 34. R9-C v2 — streaming task-atomico e identità del checkpoint

Il ledger globale passa allo schema 1.3. Dal primo boundary registra il
fingerprint del checkpoint di continuation e rifiuta ogni boundary successiva
che dichiari un checkpoint diverso, anche quando tree, blueprint e numero di
iterazioni coincidono. Il rifiuto non modifica mask, conteggi, utility o
fingerprint del ledger. I payload 1.1 vengono rifiutati perché non contengono
questa identità; i payload 1.2 vengono rifiutati perché non possono provare
che i due resolver dello stesso task condividano l'assemblaggio locale.

Il controllo usa due campi distinti. `continuation_fingerprint` identifica
l'artefatto assemblato del singolo task e può cambiare fra flop;
`continuation_checkpoint_fingerprint` identifica la strategia postflop comune
e deve restare uguale nell'intero gioco. La root boundary River fornisce
quest'ultima identità; accumulatore e aggregato River v5, terminali Flop/Turn
1.1, assemblatore Flop 1.1 e boundary Flop 1.1 la propagano senza sostituirla
con un fingerprint locale.

`stream_hu_preflop_whole_game_boundaries` percorre i 5.157 task senza
materializzarne il catalogo. Per ogni task salta i resolver già presenti,
richiede esattamente quelli mancanti, valida l'intera risposta e ordina i due
lati prima dell'accumulazione. Il task entra nel ledger soltanto dopo che il
checkpoint sink ha accettato il candidato. Se un provider o il sink fallisce,
rimane valido l'ultimo task già committato e il solve può ripartire dal ledger
persistito.

Il piano di decomposizione passa allo schema 1.5. Il ledger numerico usa
170.181 B, inclusi 41.256 B di hash locali; il picco transazionale conserva
ledger corrente, candidato e due boundary, per 365.706 B. Stringhe, metadati
dei container e callback sono esclusi. Il lower bound annidato sale a
637.398.110 B. Cinque processi Release
consecutivi del benchmark passano con fingerprint
`fnv1a64:fd74e36ab417d1da`; la proiezione diagnostica di una sweep a una
iterazione varia fra 783.076 e 930.075 s, mediana 834.311 s.

Il test HU Release passa con 9.090 asserzioni. Verifica il rifiuto di checkpoint
misti e risposte incomplete, il resume da un solo resolver, la persistenza del
task già committato dopo un errore successivo e la copertura completa di
10.314 boundary. I terminali BR Flop/Turn 1.1 includono il fingerprint della
continuation usata dal provider avversario; un terminale internamente valido ma
appartenente a un altro checkpoint viene rifiutato. La suite Release completa
passa 42/42 in 787,22 s; il target HU impiega 506,16 s. Lo stesso target passa
sotto AddressSanitizer con 9.090 asserzioni in 4.793,91 s, senza diagnostiche.
Il provider di continuation convergenti e la
NashConv del benchmark `GTP-HU-PREFLOP-CO40-001` restano aperti. Nessun default
CLI, GUI, ProductionDcfr o `.gtsd` è cambiato.

## 35. R9-C v2 — resume della best response exact

La pipeline BR può ora salvare e riprendere il lavoro dentro un leaf River,
non soltanto fra task Flop. Il checkpoint v1 conserva la query completa, il
manifest ordinato dei root River, l'indice del prossimo root, 630 accumulatori
compensati e le identità di tree, decomposizione, catalogo, blueprint,
continuation e iterazione. Un resume completo finalizza senza richiamare il
provider; un resume parziale riparte dal primo root non committato.

Il protocollo resta transazionale: il resolver costruisce un candidato, lo
passa al checkpoint sink e aggiorna lo stato corrente solo se il sink risponde
con successo. Sono persistibili anche la valutazione BR del task, l'accumulatore
delle entry e la valutazione completa dell'entry. I quattro formati JSON v1
usano checksum, limiti espliciti e sostituzione atomica; dati corrotti, schema
precedente, history incoerente o identità incompatibili vengono rifiutati.

Il target HU Release passa con 9.099 asserzioni in 462,24 s e stderr vuoto.
Verifica roundtrip parziale e completo, corruzione, salvataggio atomico,
interruzione dopo il primo root e resume dal secondo senza replay. La
persistenza riduce il lavoro perso, ma non la quantità di lavoro: lo sweep
River indipendente resta proiettato fra 783.076 e 930.075 s per una sola
iterazione. Restano aperti il provider postflop convergente con riuso fra root,
ASan sulla revisione corrente e quindi la NashConv del benchmark. La suite
Release completa passa 42/42 in 752,22 s; nel run integrale il target HU
impiega 484,88 s. Nessun default di produzione è cambiato.
