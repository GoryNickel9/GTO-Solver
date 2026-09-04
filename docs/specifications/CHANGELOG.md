# Changelog delle specifiche

Questo changelog registra modifiche ai contratti in `docs/specifications`, non
sostituisce la cronologia Git né i report di fase.

## 2026-09-04

### Corretto

- Riclassificati `8 MB`, `399 MB` e `2.000 MB` come valori del campo GTO+
  “Memory needed for solving”, non come Peak RSS del processo.
- Eliminata dalla documentazione normativa l'esistenza di un cap desktop
  indipendente `<2 GiB`: il valore `2.000 MB` appartiene esclusivamente alla
  fixture TSTC9D.
- Sospeso il confronto memoria GTO+ con stato
  `NOT_EVALUATED_COMPARABILITY_UNRESOLVED`; Peak RSS, private bytes e stato
  persistente restano telemetrie separate senza PASS/FAIL comparativo.
- Marcati come semanticamente invalidi i PASS/FAIL memoria prodotti dagli
  schema v3 precedenti; correttezza, convergenza e tempo conservano la
  propria validità indipendente.
- Archiviati, senza cancellarne i dati, i report il cui oggetto principale era
  il falso gate Peak RSS/2 GB; i report misti conservano le evidenze con una
  errata corrige in apertura.

### Implementato

- Aggiunti i moduli installabili `gtosd::abstraction` e `gtosd::subgame`.
  Il primo produce feature W/T/L/equity postflop con runout esatti, esegue
  bucketing k-means deterministico per partizione, misura l'errore, serializza
  e riscrive gli infoset realmente consumati da CFR+.
- Aggiunto resolving su frontier reach-weighted e infoset-closed, con modalità
  unsafe esplicita e guard full-game exact-NashConv. Un candidato peggiore
  viene rifiutato mantenendo il blueprint invariato.
- Portato il default production del minimizer subgame CFR+ a otto thread; il
  checkpoint registra il thread count e il seriale resta un oracle esplicito.
- Nel percorso composto bucketed/subgame, candidato e blueprint vengono
  rialzati e confrontati sul gioco esatto; il solo NashConv astratto non può
  autorizzare il deploy.
- Aggiornata la decisione di prodotto: bucketing e decomposizione precedono la
  ripresa della parity; il percorso exact resta oracle, non requisito del
  default commerciale per alberi grandi.
- Collegata `CardAbstraction` al `DenseLayout` HU postflop con API opt-in
  prepare/solve/estimate/query/certify. Chance, payoff e BR restano combo-level;
  regret e average strategy sono bucketed con aggregazione reach-weighted e
  singola proiezione CFR+ per bucket.
- Aggiunti comandi CLI separati `solve-bucketed`, `resume-bucketed`,
  `query-bucketed` e `certify-bucketed`. Report e query espongono fingerprint,
  granularità, compression ratio, weighted MSE e membership; i comandi exact
  mantengono il proprio dispatch e rifiutano checkpoint astratti.
- Limitato esplicitamente il primo bridge nativo qualificato a CFR+ Float64,
  otto thread e canonical DAG lossless. DCFR, codec compressi, più di otto
  thread e diagnostici incompatibili vengono rifiutati.
- Aggiunto il manifest `GTOSD_CARD_ABSTRACTION_FEATURE_CACHE 1 0`: ordine
  canonico, bit IEEE esatti, fingerprint sorgente/cache, load validato e
  scrittura atomica. È indipendente dal bucket count e riusabile tra sweep.
- Aggiunti API e CLI `postflop build-feature-cache`; solve/resume/query/certify
  bucketed accettano la cache opzionale e pubblicano fingerprint, flag di riuso
  e tempi separati di feature preparation e clustering.
- Qualificata l'equivalenza bit-identica tra percorso diretto e cache, il
  rifiuto di sorgenti stale e lo sweep K=1/2/3/6/12 con errore non crescente.
- Aggiunto il preflight bucketing layout-only con upper bound per K, limite
  preventivo di 2.000.000 osservazioni, accounting di stato Float64, mapping,
  transienti, otto worker, scrittura atomica e spazio page-backed.
- Parallelizzato il builder exact-feature su otto partizioni indipendenti con
  merge canonico deterministico. Il percorso CLI bucketed e la qualification
  usano sette worker più il thread chiamante; il cap API è otto thread totali.
- Aggiunti i comandi GTO+ di preflight/cache/qualification e il runner a cinque
  processi con oracle seriale separato. Il gate richiede exact BR, NashConv
  `<1%`, RAM esplicita, determinismo e delta parallelo/seriale `<=1e-4`.
- Collegato il resolving mid-tree al `DenseLayout` postflop bucketed. Il path
  propaga range blueprint, chance/card removal e automorfismi; solo gli action
  slot del frontier sono resettati e sottoposti a snapshot entro budget.
- Aggiunti `prepared_postflop_edges`, `resolve_postflop_subgame` e i comandi
  CLI `edges-bucketed`/`resolve-bucketed`. Il solve locale usa CFR+ a otto
  thread e il deploy è autorizzato soltanto da exact-NashConv full-game; il
  fallback ripristina il blueprint byte-per-byte e l'output non sovrascrive la
  sorgente.
- Aggiunto il test CLI end-to-end del resolver e un test nativo che attraversa
  un chance node, verifica il prior privato condizionato, il budget dello
  snapshot e la persistenza del checkpoint distribuito.
- Chiuso il prerequisito abstraction/subgame del prodotto HU postflop e
  riaperto il lavoro di parity GTO+. Il futuro solver preflop resta Fase 14 e
  non viene presentato come già implementato.

- Migrati template e fixture correnti a
  `gtosd.gto_plus_convergence_benchmark.v4`, con oggetto
  `gto_plus_reference.solver_memory` tipizzato e validato.
- Emessi run `gtosd.gto_plus_convergence_run.v4` e summary v4 con
  `gto_plus_reference_memory`, `solver_memory_accounting`, `process_memory` e
  `memory_comparison` separati; rimossi i falsi gate memoria.
- Impedito al benchmark di trasformare il riferimento GTO+ in un budget o in
  una selezione page-backed. L’eventuale backend page-backed resta opt-in
  generico tramite `resident_working_set_budget_bytes`.
- Rimossi i target predefiniti 1,8/2,0 GB dall’estimatore del layout canonico;
  un budget viene valutato soltanto se dichiarato come user-configured o
  experiment.
- Conservata la lettura diretta v1/v2/v3 con conversione v4 esplicita; una v3
  viene marcata `legacy_metric_misclassified`. Il wrapper multiprocesso accetta
  soltanto fixture v4.
- Implementato `SolverMemoryLedger` generico e thread-safe con categorie,
  backing, lifetime, payload/capacità correnti e massimi, prediction pre-state
  e high-water runtime; il report usa
  `gtosd.solver_memory_accounting.v2` senza riattivare il gate GTO+.
- Verificata l'osservazionalità del ledger: disabilitarlo non modifica
  fingerprint, checkpoint, strategia o EV exact.

### Pianificato, non ancora implementato

- Ricostruzione black-box della formula GTO+ e promozione dell'eventuale
  comparatore restano Fasi D/E separate. Fino ad allora il confronto memoria
  rimane `NOT_EVALUATED`.

### Verificato

- Build MSVC Release dei target modificati completata senza errori.
- CTest Release completo `35/35 PASS` in `208,99 s`, inclusi ledger,
  CardAbstraction, SubgameSolver, bridge/CLI postflop bucketed, CFR+, test
  esaustivi, oracle e riferimento GTO+.
- Benchmark Release dedicato a 1.000 iterazioni: CFR+ Kuhn exact `78,045 ms`,
  CFR+ Kuhn 2-bucket `79,383 ms`, subgame Leduc exact-guarded `2.645,542 ms`;
  il benchmark Leduc esercita tutti gli otto thread; valori
  registrati come baseline funzionale, non come speedup qualificato.
- Benchmark nativo Release, cinque ripetizioni: cache turn `21,289 ms` mediana
  per 66 partizioni/744 osservazioni; fixed-river a 1.000 iterazioni da K=1 a
  K=12 usa `128/256/384/768/1.536 B`, compression `12/6/4/2/1x`, weighted MSE
  `0,294077/0,069559/0,026860/0,002066/0` e wall mediano
  `8,146/8,798/9,250/9,443/10,414 ms`. Exact misura `12,673 ms` e `1.536 B`.
  La fixture ridotta qualifica il trade-off, non uno speedup commerciale.
- AHKHQH flop K=16, cinque processi a otto thread: NashConv deterministico
  `0,6029051267%`, wall mediano `9,936001 s`, Peak RSS mediano `25.227.264 B`;
  oracle seriale `0,6099145321%`, `31,051574 s`, delta `7,0094e-5` e speedup
  sulla mediana parallela `3,13x`. Il builder AHK passa da circa `4,15 s` a `1,57246 s`
  conservando il fingerprint. Il builder TH completa 280.308 osservazioni in
  `169,398 s`/`314.556.416 B`, contro il seriale incompleto oltre nove minuti.
- TH7D6S K=128, cinque processi a otto thread: NashConv deterministico
  `0,8816749004%`, wall mediano `435,349473 s`, Peak RSS mediano
  `694.796.288 B`; oracle seriale `0,8770330545%` in `1.493,513047 s`, delta
  `4,64185e-5` e speedup `3,43x`. K=16 è respinto anche a 800 iterazioni.
- Corretto il preflight cache: upper bound testuale sempre sopra il file reale,
  doppia copia atomica inclusa nel budget disco e limite osservazioni verificato
  prima di avviare i worker. RAM e disco sono parametri obbligatori del runner.
- Tutti i nuovi file C++ passano `clang-format --dry-run --Werror`. Il target
  globale continua a segnalare debito di formato in sorgenti preesistenti e non
  e' stato usato per produrre una riscrittura meccanica fuori scope.
- Rimosso dal manifest di install il riferimento stantio ai quattro JSON Schema
  eliminati in precedenza dal repository; install tree e consumer esterno 0.11
  PASS verificano anche gli export CMake `gtosd::abstraction` e
  `gtosd::subgame`. Il consumer collega anche `gtosd::postflop`, verifica la
  dipendenza pubblica dall'astrazione e invoca build/validation della feature
  cache dall'install tree.
- Test manuali dei runner black-box e user-configured process-memory budget
  PASS; parsing di 14 fixture JSON e 22 script PowerShell PASS.
- Smoke AHKHQH v4 a cinque processi completato a 80 iterazioni e dEV
  `0,951423%` in ogni run; essendo stato eseguito su worktree dirty, non è una
  certificazione o una promozione prestazionale.

## 2026-09-03

### Aggiornato

- Aggiunto un compilatore streaming del public tree per il layout canonico
  production, senza materializzare il secondo albero fisico durante il solve.
- Sovrapposti gli offset mutuamente esclusivi del decision layout e rimossi gli
  accumulatori showdown del tipo scalare non usato da ogni traversal.
- Storicamente aggiornato lo stato Peak RSS per-fixture; la successiva
  correzione del 2026-09-04 invalida la classificazione comparativa 3/3 ma non
  le misure OS grezze.
- Compattati i record canonici node/edge/outcome a `16/8/8` byte e rimossi
  indici temporanei e hash table non necessari dal compilatore streaming.
- Aggiunto un backend exact OS-page-backed selezionato soltanto da un target di
  working set esplicito, con residenza e materializzazione dichiarate nel
  report; target zero conserva i vettori residenti.

### Verificato

- Oracle streamed-vs-materialized `24.121` assert PASS e riferimento GTO+
  `24/24` PASS, inclusi ISO asimmetrico e prepared-root differential.
- Build Release completa e CTest `28/28 PASS` in `198,25 s`.
- Phase 10 `10.660` assert, riferimento GTO+ `24/24` e controesempio orbit
  asimmetrico `11/11` PASS.
- Peak RSS finale osservato: AHKHQH max cinque processi `7.790.592 B`, TH7D6S
  max cinque processi `363.569.152 B`, TSTC9D full-convergence
  `1.534.152.704 B`. Sono dati diagnostici; il claim storico “gate memoria 3/3”
  è superseded dalla correzione del 2026-09-04. Correttezza 3/3 resta PASS.

## 2026-09-02

### Aggiornato

- Registrato allora un presunto cap desktop comune `< 2 GiB`, separato dai
  riferimenti per fixture. La correzione del 2026-09-04 ne ritira interamente la
  natura normativa; restano valide soltanto le misure OS grezze.
- Consolidati su `main` tooling e report S6, strict-cap, Pure/Sync-PCFR,
  range-aware physical-orbit e black-box GTO+ senza promuovere percorsi
  respinti o default-off.
- Resa robusta la sostituzione atomica Windows contro errori transitori e
  isolati i file temporanei dei test storage.

### Verificato

- Build Release completa e CTest `27/27 PASS` in `218,43 s`.
- Test storage aggiornato `10/10 PASS`, incluso il lock Windows deterministico.

## 2026-09-01

### Aggiornato

- Promossa la schedule comune `production_dcfr` (`1.5/0/3`, reset
  `1,2,5,17,65`, regret clock post-65 ritardato di una iterazione).
- Allineate le tre fixture production e mantenuta l'identita' checkpoint `11`.
- Registrata la qualificazione final-head a cinque processi: `15/15` solve con
  dEV `<1%`, correctness/layout/exact outcomes. L’allora claim `cap desktop
  PASS` è ritirato dalla correzione del 2026-09-04.
- Pubblicati mediana/p95 AHK `0,758705/0,790918 s`, TH
  `19,948228/24,192260 s`, TST `184,095930/197,865030 s`.
- Corretto lo stato del parity gate: AHK time PASS; TH e TST time FAIL
  rispettivamente del `1,661%` e `42,722%` sulla mediana.
- Reso `ScaledUint16RegretStrategy` il formato benchmark production dichiarato;
  “exact outcomes” non implica identita' numerica con Float64.

### Verificato

- CTest Release `26/26 PASS`.
- Contratto fixture, resume byte-equivalent, PowerShell parse e diff check PASS.

## 2026-08-14

### Aggiornato

- Allineato il checkpoint della suite GTO+ ai report correnti: dEV/root/RAM
  PASS su 3/3 e tempo FAIL su 3/3.
- Separati esplicitamente riferimenti temporali GTO+ grezzi, limiti al 90%,
  `solver_state_bytes`, transient workspace e peak RSS.
- Registrati l'assenza di iteration cap, il confronto stretto `Target dEV < 1%`
  e il freeze F11+ fino al superamento con modifiche del solo core generale.
- Documentato il fallback fisico necessario per range asimmetrici e il ritiro
  dell'esperimento isomorfico non lossless.
- Reso permanente il contratto di solving CPU/RAM-only: nessun backend GPU o
  acceleratore di calcolo presente o futuro; l'eventuale GPU è rendering GUI.
- Ordinato il piano tempo: fast path generale del fallback fisico, DAG
  player-local, isomorfismo street-local e soltanto dopo layout/scheduling/SIMD.
- Marcato il candidato DAG presente nel worktree come non verificato e non
  promosso; i benchmark correnti non sono stati aggiornati.

### Verificato

- Build Release di `gto_cli` e `gtosd_gto_plus_reference_tests`.
- Riferimento GTO+ PASS con 24 asserzioni, fallback asimmetrico a differenza
  zero e root lock esterno PASS. La suite CTest completa non è stata rieseguita
  in questa chiusura.

## 2026-08-02

### Aggiunto

- Corpus canonico separato per architettura, matematica, regole, algoritmi,
  precisione, tree, soluzione, validazione, testing, performance, GUI, CLI e
  limitazioni.
- Indice e regola di precedenza tra specifiche, parity journey, roadmap e report
  storici.
- Semantica esplicita dei posteriori privati e degli EV condizionali.
- Contratto del futuro esperimento F10.4 controlled-posterior, marcato come
  diagnostico e non implementato.

### Corretto

- La parità GTO+ non richiede più EV BTN condizionali uguali quando i posteriori
  combo-per-combo sono differenti.
- Il root EV AhKhQh è registrato come PASS: delta `+0,005491179 ante`.
- La baseline resta FAIL sul tempo e PASS sulla memoria.
- Il tentativo GTO+ a target 0,10% è registrato come censurato `>245 s`, con dEV
  osservato circa 0,11%.
- La soglia automatica all-in segue la frazione dello stack impegnata e distingue
  `Add` da `Go`.

### Chiarito

- Nessun bucketing, sampling o smoothing è usato nella fixture di parità.
- `Float32` riguarda lo stato cumulativo; traversal e certificazione restano in
  precisione più alta.
- La quantizzazione `uint16` è sperimentale e lossy.
- Node locking globale, preflop HU e multiway restano non supportati.

## Cronologia precedente

Le decisioni e misure precedenti sono conservate in:

- [ROADMAP_HU_SHORT_DECK_GTO_SOLVER.md](../ROADMAP_HU_SHORT_DECK_GTO_SOLVER.md);
- [IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md);
- [GTO_PLUS_PARITY_JOURNEY.md](../GTO_PLUS_PARITY_JOURNEY.md);
- report `PHASE_*_COMPLETION_REPORT.md` nella directory `docs`.
