# Performance

> **Baseline production corrente (2026-08-29/30).** AHKHQH, TH7D6S e TSTC9D
> usano ora lo stesso DCFR exact signed `alpha=1.5`, `beta=0`, `gamma=2`,
> `averaging_delay=0` e massimo otto thread. Golden, curve fixed e time-to-target
> sono in
> [`PRODUCTION_DCFR_NORMALIZATION_2026-08-29.md`](../PRODUCTION_DCFR_NORMALIZATION_2026-08-29.md).
> Le baseline DCFR+ e il profilo TST-specifico `1.9/0/3` riportati sotto sono
> storici/superseded per confronti production comuni. Il gate resta bloccato dal
> root EV AHKHQH signed/packed; RBP non e' stato avviato.

Analisi trasversale corrente del motore generale, con profiling fixed-iteration,
scaling thread, confronto algoritmico e limiti di telemetria:
[`PERFORMANCE_ANALYSIS_2026-08-29.md`](../PERFORMANCE_ANALYSIS_2026-08-29.md).

## Principio di misura

Le prestazioni vengono ottimizzate solo dopo profiling. Ogni misura registra
hardware, OS, compiler, flags, commit, thread, precisione, fixture, seed e
criterio di convergenza. Un miglioramento che altera gioco o accuratezza viene
rifiutato.

## Metriche

Il solver pubblica almeno:

- tempo layout, inizializzazione, traversal, apply regret, certificazione,
  finalizzazione e totale;
- iterazioni e nodi attraversati;
- physical/canonical nodes, infoset e actions;
- normalized NashConv o maximum deviation;
- byte di regret e strategy sum;
- transient workspace e peak RSS come metriche separate.

Peak RSS non sostituisce la memoria solver dichiarata da GTO+. Cache e layout
preparato non possono essere inclusi da un lato e esclusi dall'altro.

## Fixture di parità

La suite comparativa corrente comprende `GTP-AHKHQH-003`,
`GTP-TH7D6S-101` e `GTP-TSTC9D-101`. La fixture v1 di AHKHQH resta congelata;
le fixture generiche v2 permettono di aggiungere scenari senza modificare il
codice del runner.
Lo script `tools/run_gto_plus_convergence_benchmark.ps1` avvia processi
indipendenti e produce report versionati. Il confronto primario usa la mediana
di cinque run e pubblica anche p95 e ogni campione.

La specifica generica v2 (`gtosd.gto_plus_convergence_benchmark.v2`) permette
di registrare nuovi benchmark di convergenza senza modifiche al codice: board,
range, stack-to-pot, sizing, raise depth e nodi di riferimento EV/frequenze
sono letti dalla fixture (`benchmarks/fixtures/gto_plus_ahkhqh_101.json` è la
validazione v2 dello scenario 003). Ogni nuovo benchmark mantiene il proprio
riferimento GTO+ e i propri gate.

Baseline storica AHKHQH documentata (re-baseline 2026-08-05, regola all-in
naturale):

- GTO+ operativo: 1,71 s a dEV 0,98%, memoria solver 8 MB;
- GTOSD: mediana 3,128 s, p95 3,271 s (cinque run, albero 165.774 nodi);
- GTOSD state `Float32`: 6.677.088 byte (più vicino agli 8 MB GTO+ del
  precedente 4.214.976 byte);
- gate tempo `<=1,900000 s`: FAIL;
- gate memoria `<=8.888.889 byte`: PASS;
- gate correttezza (`correctness_gate`, EV del nodo root): PASS dal 2026-08-02;
  gli EV BTN condizionali e le frequenze restano diagnostica (con il root lock
  F10.4 i delta BTN scendono a +0,0348 / +0,0366 ante,
  vedi GTO_PLUS_PARITY_JOURNEY.md).

Il tentativo GTO+ a target 0,10% è censurato a `>245 s` e non sostituisce il
riferimento operativo senza ridefinire l'intero protocollo.

## Checkpoint corrente della suite — 2026-08-14

Stato di chiusura RAM: i tre scenari sono stati rieseguiti separatamente sul
build Release `out/build/windows-release-current` con il formato core packed
`Float13RegretFloat11Strategy` (3 byte/action, compute float64). La fixture v2
AHKHQH-101 è la validazione eseguibile dello scenario canonico 003; il vecchio
file v1 003 non è presente nel worktree. La chiusura del 2026-08-14 ha
ricompilato `gto_cli` e il riferimento GTO+ e ha eseguito quest'ultimo con esito
PASS; il 16/16 CTest del 2026-08-13 è evidenza precedente e non viene presentato
come nuova esecuzione sul checkpoint odierno.

| Benchmark | dEV GTOSD | Root EV GTOSD / GTO+ | Tempo / limite 90% | Solver state / GTO+ | Stato |
|---|---:|---:|---:|---:|---|
| `GTP-AHKHQH-101` (scenario 003) | 0,982960% @ 100 | 19,123322 / 19,15 | 4,970917 s / 1,900000 s | 2.503.908 B / 8.000.000 B | dEV/root/RAM PASS; tempo FAIL |
| `GTP-TH7D6S-101` | 0,986976% @ 82 | 8,220073 / 8,22198 | 37,810434 s / 19,622222 s | 249.955.776 B / 399.000.000 B | dEV/root/RAM PASS; tempo FAIL |
| `GTP-TSTC9D-101` | 0,983565% @ 200 | 8,498226 / 8,50165 | 690,307523 s / 120,600000 s | 1.747.903.656 B / 2.000.000.000 B | dEV/root/RAM PASS; tempo FAIL; metadata incompleti |

Questi sono singoli run storici, non mediane temporali. La colonna RAM misura
soltanto `solver_state_bytes` e non dimostra più il gate memoria: l'utente ha
chiarito che il valore GTO+ copre l'intero solve. Il contratto corrente usa
quindi `peak_rss_bytes` per `memory_gate` e pubblica separatamente lo stato. I
due report TSTC9D citati misuravano peak RSS di 3.166.359.552 e 3.173.212.160
byte e devono essere letti come RAM FAIL. La sostituzione architetturale è
definita in
[`ADR_0002_MEMORY_BOUNDED_EXACT_SOLVER.md`](../ADR_0002_MEMORY_BOUNDED_EXACT_SOLVER.md).

Per TSTC9D le schermate GTO+ confermano `Bet 5,3`, `Raise 14` e, al nodo
successivo, `Fold / Call / Raise 47 / Raise 80`. La soglia `Add all-in` usa il
push sopra il call diviso per il pot dopo il call: circa `280,83%` dopo la
prima bet (nessun push) e `150%` dopo il primo raise (push aggiunto). Le size
sono `[33,75]` al primo raise e `75%` ai successivi. Il contratto del motore è
stato riallineato in modo generale. Il rounding dei target aggressivi è una
policy piecewise del core, non una correzione del runner. Il run target-driven
non ha iteration cap: 1,346488% a 160 non lo arresta, 0,958803% a 180 sì.
La fixture corrente incorpora gli importi monetari confermati dall'utente e usa
`metadata_complete=true`. I vecchi numeri TST non sono una certificazione
finale del nuovo contratto peak RSS.

Il core non esegue certificazioni periodiche prima dell'inizio effettivo
dell'averaging: in quell'intervallo la strategia media non contiene campioni e
non può sostenere un claim di convergenza. Pausa, cancellazione e termine di un
run finito continuano a forzare la certificazione. L'A/B Release TSTC9D ha
ridotto le certificazioni da 9 (`20..180`) a 3 (`140,160,180`) preservando
bit per bit dEV, root EV, fingerprint e layout: solver time 526,492253 ->
471,901781 s (-10,37%), certification 45,181916 -> 15,546513 s. È una singola
misura controllata, non la certificazione temporale finale su cinque processi.

Il probe Release del 2026-08-13 conferma nel catalogo exact
`5,3 -> 14 -> call 8,7 / raise payment 41,7 (raise-to 47) / all-in 74,7`, senza il
vecchio raise 33%. Fingerprint e conteggi della fixture sono stati rigenerati
dal report. Il nuovo run e' una singola misura, non la mediana di cinque.

Questi tempi sono gli ultimi checkpoint comparabili disponibili, non una nuova
mediana promossa. Il confronto usa i riferimenti GTO+ grezzi 1,71/17,66/116,09
s e i limiti concordati al 90% 1,90/19,622222/128,988889 s. Per TSTC9D il
nuovo riferimento deriva dalla curva temporale completa: il primo punto
strettamente sotto 1% è 0,91% (0,146 ante) a 116,09 s e sostituisce il dato
isolato precedente di 108,54 s. La pressione della
macchina non viene accettata come spiegazione del delta: GTO+ è stabile anche
sotto pressione e ogni FAIL temporale resta attribuito al percorso core finché
una modifica generale non lo elimina. La promozione finale richiede cinque
processi indipendenti, ma solo dopo che il singolo run passa.

## Checkpoint TSTC9D corrente — 2026-08-29

Il checkpoint corrente non è una promozione del gate. È un singolo run Release
CPU-only a otto thread con DCFR `alpha=1,9`, `beta=0`, `gamma=3`, stato
`ScaledUint16RegretStrategy`, 160 iterazioni e certificazione finale unica:

| Fase/metrica | Valore |
|---|---:|
| Inizializzazione | 0,464064 s |
| Traversal | 130,372985 s |
| Certificazione exact BR | 5,439364 s |
| Finalizzazione | ~0,002 s |
| Elapsed solver | 136,238021 s |
| Costo medio traversal | 0,814831 s/iter |
| dEV finale | 1,078426% |
| Root EV / delta | 8,490667 / -0,010983 ante |
| Nodi visitati | 245.146.564 |
| `solver_state_bytes` | 1.472.605.376 B |
| Peak RSS | 1.968.742.400 B |

Il profilo attribuisce circa `55,8%` del traversal alla famiglia showdown
(`27,4%` produzione valori, `19,3%` rank/card, `9,1%` prefix), `22,0%` a
value/update, `13,6%` a regret matching, `8,3%` a chance/board e `0,3%` a
reach. L'utilizzo CPU medio del run lungo è `86,1%`: il costo breve vicino a
`0,733 s/iter` cresce quando le strategie diventano dense e non può essere
estrapolato come tempo del solve completo.

Le modifiche mantenute sono lossless: specializzazione terminale paired,
precalcolo degli slot chance compatibili, `/favor:INTEL64` senza fast-math e
scheduling river limitato alla coda con riduzione canonica seriale. I tentativi
con diverso ordine di somma, root action concurrency non indipendente, layout
32-card, accumulo cell-major, pinning, PGO e schedule DCFR/CFR+ peggiori sono
respinti. Il dettaglio numerico è nel checkpoint 2026-08-29 di
`GTO_PLUS_PARITY_JOURNEY.md`.

Il run finale successivo a 170 iterazioni ha raggiunto la soglia dEV ed è il
riferimento di chiusura della sessione:

| Fase/metrica | Valore | Quota elapsed solver |
|---|---:|---:|
| Inizializzazione | 0,573885 s | 0,37% |
| Traversal | 147,063172 s | 96,01% |
| Regret application | 0,000131 s | <0,01% |
| Certificazione exact BR | 5,505125 s | 3,59% |
| Finalizzazione | 0,001545 s | <0,01% |
| **Elapsed solver** | **153,176351 s** | **100%** |
| Preparazione tree fuori timer | 26,546558 s | — |
| Wall con preparazione | 179,722909 s | — |

dEV `0,9857595%`, root EV `8,4925400` (delta `-0,0091100`), peak RSS
`1.968.537.600 B` e stato `1.472.605.376 B` passano. Il tempo fallisce sia il
riferimento grezzo `116,09 s` (`+37,086351 s`, `+31,95%`) sia il limite con
margine `128,988889 s` (`+24,187462 s`, `+18,75%`). Report:
`out/tstc9d_session_final_alpha1_9_gamma3_iter170.json`.

## Profiling

Le aree misurate separatamente sono traversal, certificazioni exact BR,
inizializzazione/riduzione dei buffer, allocazioni, cache, contention e I/O.
Ogni ottimizzazione significativa registra prima/dopo sullo stesso gate.

## Policy di ottimizzazione generale — aggiornamento 2026-08-29

Il target di ottimizzazione è il motore condiviso, non una fixture specifica.
Board, range, sizings, profondità, algoritmo e numero di giocatori sono input
del programma reale e non possono determinare branch o percorsi privilegiati
nel codice di produzione. Un'ottimizzazione è quindi promossa soltanto se è
applicabile a ogni configurazione valida, preserva la semantica matematica e
non introduce una regressione patologica su un'altra classe di albero.

Un miglioramento osservato su TSTC9D non è sufficiente per concludere che il
motore sia stato ottimizzato in modo generale. Il guadagno percentuale può
variare tra benchmark perché cambiano il peso relativo di showdown, chance,
accessi alla memoria, sincronizzazione e numero di iterazioni necessarie. La
differenza deve però essere spiegata dal profilo del carico, non da una
selezione basata sul nome o sulla configurazione della fixture.

Ogni modifica prestazionale deve superare questa sequenza:

1. baseline sullo stesso binario Release, CPU-only, con numero di thread
   dichiarato;
2. confronto diagnostico a iterazioni fisse su AHKHQH, TH7D6S e TSTC9D, per
   isolare il costo di una iterazione dalla velocità di convergenza;
3. profiling della stessa suddivisione: layout/setup, traversal, showdown,
   chance, regret update, strategy averaging, certificazione, scheduling,
   attesa dei worker e memoria;
4. differenziale matematico e di layout contro la baseline, con risultato
   invariato entro la tolleranza dichiarata;
5. verifica su board e range differenti, inclusi range asimmetrici e alberi
   piccoli, medi e grandi;
6. solo dopo il superamento del test diagnostico, run ufficiale target-driven
   e cinque processi indipendenti con dEV, root EV, tempo, stato solver e peak
   RSS riportati separatamente.

Il benchmark a iterazioni fisse è diagnostico e non sostituisce il gate GTO+:
misura `tempo_per_iterazione`, `nodi_per_secondo` e la ripartizione delle fasi.
Il benchmark ufficiale misura invece il tempo fino alla prima certificazione
con dEV strettamente sotto soglia. La sua durata dipende anche dal numero di
iterazioni richieste dalla dinamica CFR/DCFR; non è lecito attribuire un
miglioramento del kernel a una convergenza ottenuta con meno iterazioni.

Lo stato corrente è pertanto: le ottimizzazioni lossless già mantenute sono
globali al binario e non contengono branch per benchmark, ma la loro efficacia
non è ancora stata dimostrata con una profilazione comparativa omogenea sui
tre carichi. Il prossimo intervento prioritario è questa misura comparativa;
non una nuova ottimizzazione mirata a TSTC9D.

## Piano prestazionale CPU/RAM-only

Il calcolo del solver usa esclusivamente thread CPU e memoria RAM. GPU e
acceleratori di calcolo non sono una leva presente o futura: non sono ammessi
backend CUDA, ROCm, OpenCL, Vulkan Compute, DirectCompute o equivalenti per
tree building, traversal CFR, update di regret/strategy, best response,
certificazione o analisi della soluzione. L'eventuale accelerazione grafica
della GUI riguarda soltanto il rendering.

Il checkpoint numerico della tabella sopra resta l'unica baseline promossa.
Il preflight layout-only del 2026-08-27 non è un benchmark di solving ma
fornisce un conteggio strutturale verificato per TSTC9D: 1.758.624 nodi
canonici, 145.524.152 infoset e 366.890.152 action entry. Il modello da 3
byte/action stima 1,516-1,546 GB complessivi per 1-8 worker; quello `i16/u16`
da 4 byte stima 1,888-1,918 GB. Entrambi restano sotto 2 GB, ma solo il primo
rispetta il target interno di 1,8 GB. Il report è
`out/tstc9d_canonical_layout.json`; le cifre non sostituiscono il peak RSS del
solve completo.

L'ordine degli interventi generali è:

1. correggere la selezione del fast path quando il fallback usa infoset fisici:
   `uses_direct_action_bases = !uses_isomorphic_infosets ||
   automorphisms.size() <= 1`; in quel layout ogni blocco azione è già unico e
   diretto, quindi sono applicabili `PlayerIndexed`, update immediato dei regret
   e rimozione dei buffer differiti;
2. migrare il traversal sul canonical chance tree con trasformazioni di reach,
   mapping inverso dei CFV e ownership disgiunta; i range dei player possono
   essere diversi, purché ogni automorfismo li preservi separatamente;
3. valutare un isomorfismo street-local basato sullo stabilizzatore del board
   pubblico corrente. È distinto dalla canonicalizzazione del solo board già
   respinta: deve preservare carte private, reach e mapping inverso;
4. solo dopo le riduzioni strutturali, riprofilare layout SoA, scheduling CPU,
   cache, bandwidth e SIMD. La best response esatta è una fase separata e non
   può compensare il costo dominante del traversal.

Ogni punto richiede differenziale fisico/canonico entro `1e-11`, riferimento
GTO+, suite completa e poi un singolo benchmark isolato. Le cinque esecuzioni
indipendenti vengono effettuate soltanto quando il singolo run supera il gate.

Le ottimizzazioni lossless del percorso production corrente includono combo attive, public DAG,
isomorfismo globale, stato `Float32` con calcolo `Float64`, parallelismo per
action subtree e riuso del prepared tree. La validazione differenziale resta
obbligatoria.

## Benchmark non comparativi

La suite interna misura evaluator, tree build, CFR traversal, best response,
checkpoint, storage, scaling multicore e GUI. Questi benchmark individuano
regressioni, ma non dimostrano parità GTO+ se fixture o timer differiscono.

## Regole di decisione

- confrontare distribuzioni, non un solo best run;
- non mediare tempo e memoria in un punteggio compensatorio;
- non riutilizzare involontariamente checkpoint o cache tra processi;
- non cambiare il certification interval per nascondere il costo; è invece
  lecito eliminare nel core certificazioni per le quali la strategia media è
  matematicamente priva di campioni, dichiarando e testando la regola;
- non chiamare “convergenza” una singola iterazione più certificazione;
- conservare report grezzi insieme al riepilogo.

Il percorso root-lock diagnostico ha ridotto il mismatch downstream, ma non
modifica i gate prestazionali del percorso standard. Il prossimo intervento è
il fast path generale del fallback fisico descritto sopra, seguito dal
differenziale sul chance tree asimmetrico; F11+ resta congelata finché tutti e tre i
benchmark non superano i rispettivi gate.

## Benchmark grande TH7D6S — checkpoint storico 2026-08-08

I numeri seguenti documentano una tappa precedente e sono superati dal
checkpoint di suite del 2026-08-09 riportato sopra.

Il secondo gate comparativo corrente è `GTP-TH7D6S-101`. I riferimenti GTO+
sono 17,66 s e 399 MB; per raggiungere almeno il 90% servono quindi tempo
`<=19,622222 s` e stato solver `<=443.333.333 B`.

- stato mixed lossless: regret float24, average strategy binary16, calcolo e
  certificazione float64;
- stato solver: **416.592.960 B**, quindi gate RAM PASS; peak RSS è pubblicato
  separatamente e non sostituisce questa metrica;
- miglior full comparabile corrente: **86,1059943 s**, traversal
  **79,7679045 s**, certificazione **5,7897606 s**, iterazione 140;
- dEV **0,9260452878479758%**, root EV **8,219182737421068 ante**,
  correctness/layout PASS;
- gate tempo FAIL; la mediana prescritta di cinque processi è rinviata finché
  il singolo run non è vicino alla soglia.

Sono mantenuti i kernel AVX2 bit-exact per update regret float24 e
normalizzazione binary16. Anche il max/sum SIMD della policy, il riuso zero-sum
di profile EV P1 quando rake=0 e la fusione fold/showdown fratelli restano in
produzione. Le ultime due modifiche sono positive in A/B controllati, ma i full
successivi erano sotto carico e non vengono usati per dichiarare un nuovo wall
best. Fonte completa: `speed_optimization_journey.md` §§8.24-8.28.
