# Performance

> **Ricerca postflop 2026-09-10.** L'analisi distingue il costo della
> ricodifica dello stato, la certificazione exact seriale e la traversata per
> coppie nel prototipo River. Il nuovo oracle algebrico dei blocker passa
> 1.530 confronti; non è uno speedup del prodotto. Algoritmi campionati e
> raffinamento dell'astrazione sono valutati con gate nel gioco originale.
> [Analisi, fonti e verifiche](../POSTFLOP_RESEARCH_DECISION_2026-09-10.md) e
> [roadmap operativa dell'agente](../POSTFLOP_AGENT_EXECUTION_ROADMAP_2026-09-10.md).

> **Chiusura ProductionDcfr product optimization — 2026-09-06.** R3 misura A0
> su cinque processi Release: mapping `56,53%/46,54%` e training
> `42,54%/50,62%` del wall D/V. Le prove R4-A1/B1 falliscono i kill gate
> predefiniti e sono state rimosse. La telemetria R6 non osserva finestre RBP
> utilizzabili e non esiste una derivazione lazy che preservi la traiettoria
> signed, i clock e i reset di `ProductionDcfr`. Esito:
> `BLOCKED_WITH_EVIDENCE`. Il prodotto resta exact, H resta sigillato e la
> comparabilità RAM GTO+ resta `NOT_EVALUATED_COMPARABILITY_UNRESOLVED`. La
> suite Release finale passa `35/35` in `260,92 s`. Evidenza:
> [`../PRODUCTION_DCFR_PRODUCT_OPTIMIZATION_EXECUTION_2026-09-05.md`](../PRODUCTION_DCFR_PRODUCT_OPTIMIZATION_EXECUTION_2026-09-05.md).

> **Correzione semantica memoria GTO+ — 2026-09-04.** I riferimenti
> `8/399/2.000 MB` sono il campo UI “Memory needed for solving”, non Peak RSS e
> non un limite desktop generale. La composizione interna GTO+ non è ancora
> identificata; il confronto memoria è
> `NOT_EVALUATED_COMPARABILITY_UNRESOLVED`. I Peak RSS misurati nel recheck
> 2026-09-03 (`7.790.592/363.569.152/1.534.152.704 B`) restano telemetria OS,
> ma il claim storico `3/3 PASS` è ritirato. Lo schema v4 ora separa memoria di
> processo, accounting solver-owned e riferimento GTO+; il benchmark non
> configura più il backend page-backed né un budget dal valore esterno. Piano:
> [`../GTO_PLUS_SOLVER_MEMORY_SEMANTICS_AND_GATE_CORRECTION_PLAN_2026-09-03.md`](../GTO_PLUS_SOLVER_MEMORY_SEMANTICS_AND_GATE_CORRECTION_PLAN_2026-09-03.md).

> **Production final-head — 2026-09-01, checkpoint di qualificazione.** La schedule comune
> qualificata e' `production_dcfr`: exact alternating signed DCFR `1.5/0/3`,
> reset one-based `1,2,5,17,65`, regret clock post-65 ritardato di una
> iterazione, delay zero, stato scaled uint16, massimo otto thread e nessuna
> logica fixture-specific. Cinque processi auditabili (`r2-r6`) con
> `certification_interval=20` riproducono AHK/TH/TST a `80/80/160` iterazioni,
> dEV `0,951423%/0,807956%/0,904505%` e mediana/p95 solver
> `0,758705/0,790918 s`, `19,948228/24,192260 s`,
> `184,095930/197,865030 s`. Peak RSS massimo TST `1.969.860.608 B`, dato
> diagnostico senza cap normativo; il full CTest Release disponibile a quel
> checkpoint passava `28/28` (`105,46 s`, 2026-09-04). Il precedente
> `1.5/0/2` e' ora comparator storico. La nuova schedule e' superiore alla
> Release, ma TH e TST restano sopra i limiti GTO+ rispettivamente del
> `1,661%` e `42,722%`; il parity gate non e' ancora superato. Evidenza:
> [`../DCFR_EPOCH_RESET_GAMMA3_FEASIBILITY_2026-09-01.md`](../DCFR_EPOCH_RESET_GAMMA3_FEASIBILITY_2026-09-01.md).

> **Schema memoria v4 — implementato.** `gto_plus_reference.solver_memory`
> conserva label, valore, unità, normalizzazione e semantica; i report v4 usano
> `process_memory`, `solver_memory_accounting` e `memory_comparison` separati.
> Gli input v3 sono letti soltanto come `legacy_metric_misclassified`; i loro
> vecchi PASS/FAIL non vengono propagati. Il precedente contratto 2 GiB è
> archiviato come evidenza:
> [`../archive/legacy-memory-gate/TWO_GIB_RESOURCE_CONTRACT_AND_FRONTIER_RECHECK_2026-09-01.md`](../archive/legacy-memory-gate/TWO_GIB_RESOURCE_CONTRACT_AND_FRONTIER_RECHECK_2026-09-01.md).

> **Ledger solver-owned — 2026-09-04.** Lo schema interno
> `gtosd.solver_memory_accounting.v2` riporta previsione pre-state, picco
> osservato di payload e capacità, backing e lifetime per categoria. Tree/DAG,
> board/rank metadata, regret, cumulative strategy, scale codec, mapping,
> worker/traversal arena e certification/BR sono contabilizzati nei punti di
> ownership; trimming e paging non modificano il valore logico. Il ledger è
> distinto da Peak RSS e non risolve da solo la comparabilità con “Memory
> needed for solving” di GTO+; il relativo gate resta `NOT_EVALUATED`.
> Nello smoke AHKHQH Release il predictor pre-state riporta payload/capacità
> `7.069.187/7.083.938 B`; il ledger osserva
> `7.334.731/7.349.482 B`. Il delta `265.544 B` è attribuito agli arena lazy di
> traversal; i due runner seriali di certification raggiungono `78.408 B`
> ciascuno ma non sono simultanei. Questi numeri verificano l'accounting, non
> costituiscono ancora confronto con gli `8 MB` mostrati da GTO+.

> **Research paths imported — 2026-09-02.** S6, Pure/Sync-PCFR and
> range-aware physical-orbit remain rejected for production. Their runners,
> probes and counterexample oracles are retained for reproducibility; the two
> algorithmic paths are compile-time gated and default `OFF`, while legacy strict-cap
> profiling is opt-in. GTO+ black-box observation remains only partially
> automatable and requires a manual marker. These paths do not change
> `production_dcfr 1.5/0/3`. Evidence:
> [`../S6_COMMON_PRODUCTION_QUALIFICATION_LOOP_2026-08-31.md`](../S6_COMMON_PRODUCTION_QUALIFICATION_LOOP_2026-08-31.md),
> [`../TST_STRICT_2GB_BOTTLENECK_ATTRIBUTION_AND_FEASIBILITY_LOOP_2026-08-31.md`](../archive/legacy-memory-gate/TST_STRICT_2GB_BOTTLENECK_ATTRIBUTION_AND_FEASIBILITY_LOOP_2026-08-31.md),
> [`../STRICT_2GB_EXACT_ALGORITHM_RECHECK_2026-09-01.md`](../archive/legacy-memory-gate/STRICT_2GB_EXACT_ALGORITHM_RECHECK_2026-09-01.md),
> [`../SYNC_PCFR_POSTFLOP_TRAJECTORY_GATE_2026-09-01.md`](../SYNC_PCFR_POSTFLOP_TRAJECTORY_GATE_2026-09-01.md),
> [`../RANGE_AWARE_PHYSICAL_ORBIT_ORACLE_2026-09-01.md`](../RANGE_AWARE_PHYSICAL_ORBIT_ORACLE_2026-09-01.md) and
> [`../GTO_PLUS_AUTONOMOUS_BLACK_BOX_DISCOVERY_AND_CHARACTERIZATION_2026-08-31.md`](../GTO_PLUS_AUTONOMOUS_BLACK_BOX_DISCOVERY_AND_CHARACTERIZATION_2026-08-31.md).

> **Precedenza storica.** I checkpoint e feasibility block datati 2026-08-31 o
> precedenti sotto questa nota restano evidenza storica. Le loro diciture
> `production resta 1.5/0/2`, `stato corrente` e five-process congelata sono
> superseded dal final-head 2026-09-01 sopra.

> **FD-FTRL/OMD feasibility — 2026-08-31.** Le practical `R` variants possono
> riusare esattamente i due payload action correnti, ma sono chiuse come
> **FD-FTRL/OMD LOCAL-COST BLOCKER**. Sul mix TST, direct small-N è migliore
> di sort/bisection ma misura ancora `24,577/23,107 ns` per infoset contro
> `6,996 ns` RM. Il boundary ottimistico, con certification congelata e
> scaling locale perfetto su otto thread, richiede target prima di
> `82,4768/84,2703` iterazioni. S6 è ancora `1,35433%` @120 e crossing circa
> @145; nessun tiny, curve o target-driven FD è stato autorizzato. Production
> resta `1.5/0/2`; S6 resta STRONG RESEARCH BASELINE. Protocollo e misure:
> [`../MEMORY_NEUTRAL_FD_FTRL_OMD_FEASIBILITY_LOOP_2026-08-31.md`](../MEMORY_NEUTRAL_FD_FTRL_OMD_FEASIBILITY_LOOP_2026-08-31.md).

> **Lazy-CFR feasibility — 2026-08-31; blocker memoria ritirato.** La famiglia
> era stata chiusa come **LAZY FAMILY RAM BLOCKER**. Un solo accumulatore
> `float32` per i `145.524.152` infoset TST aggiungerebbe `582.096.608 B` e
> portava il Peak RSS proiettato a `2.552.018.656 B`; le vere DS
> `alpha/alpha_hat/beta` per history/history-action sono maggiori. Il bitset
> nominalmente allocabile non rappresenta il residuo continuo del trigger.
> Nessun trace, oracle, solve o target-driven è stato eseguito. La dimensione
> addizionale resta evidenza utile, ma non può più essere confrontata con un cap
> desktop inesistente; la famiglia richiede una nuova valutazione dopo il ledger
> solver-owned. Production
> resta common `1.5/0/2`; S6 `1.5/0/5` resta STRONG RESEARCH BASELINE.
> Protocollo e lower bound:
> [`../EXACT_LAZY_CFR_FEASIBILITY_LOOP_2026-08-31.md`](../EXACT_LAZY_CFR_FEASIBILITY_LOOP_2026-08-31.md).

> **Predictive-CFR feasibility — 2026-08-31; blocker memoria ritirato.** PCFR+
> e PDCFR+ erano stati chiusi come **PREDICTIVE FAMILY EXHAUSTED UNDER THE
> FROZEN RAM/STATE CONTRACT**. La policy predittiva non è ricostruibile dal
> cumulative regret che deve sopravvivere all'update; la cumulative average è
> anch'essa necessaria. Il lower bound TST `uint16(actions-infosets)` porta il
> Peak RSS proiettato a `2.412.654.048 B` contro il cap allora assunto. Quel
> confronto non è più valido; il costo di stato resta misurato ma la famiglia
> richiede un nuovo pre-gate memoria. Nessun solve o target-driven era stato
> autorizzato. Production resta common `1.5/0/2`; S6 `1.5/0/5` resta
> STRONG RESEARCH BASELINE. Protocollo e state proof:
> [`../EXACT_PREDICTIVE_CFR_FEASIBILITY_LOOP_2026-08-31.md`](../EXACT_PREDICTIVE_CFR_FEASIBILITY_LOOP_2026-08-31.md).

> **Common schedule result — 2026-08-31.** **COMMON EXACT SCHEDULE SPACE
> EXHAUSTED** per le famiglie schedule/fixed exact studiate. S6 `1.5/0/5`
> migliora TST di circa 20–21% in iterazioni ma resta a `1,35433%` @120 e
> proietta il target circa @160 (`1,25–1,46x`). Non è production; B
> `1.5/0/2` rimane authority. Evidenza:
> [`../COMMON_EXACT_CONVERGENCE_ACCELERATION_LOOP_2026-08-31.md`](../COMMON_EXACT_CONVERGENCE_ACCELERATION_LOOP_2026-08-31.md).

> **Constraint governance gate — 2026-08-31; frontier memoria ritirata.** Il
> boundary temporale `>=1,615339x` resta una proiezione storica. Le alternative
> RAM e la pair da `3.432.437.888 B` dipendevano dal falso cap e non sono più
> requisiti correnti. Matrice e dominance storiche:
> [`../CONSTRAINT_GOVERNANCE_GATE_2026-08-31.md`](../CONSTRAINT_GOVERNANCE_GATE_2026-08-31.md).

> **Real-node joint lower bound — 2026-08-31.** Il corpus production bounded
> AHK/TH/TST è replay-fedele su code, scale e parent output. La precision
> frontier elimina regret 16–24 bit per drift multi-step; il precedente scarto
> RAM di float32 direct (`6–8 B/action`) è da rivalutare. Il producer
> materialization streaming vale soltanto
> ~`1,041x`. Le proiezioni TST sono `93,299454 s` ideal-state,
> `168,036868 s` ideal-producer e `89,624835 s` joint ideal. Poiché nessun
> nessun punto studiato passava insieme precisione e traversal
> `<=96,208089 s`; le conclusioni esclusivamente temporali restano storiche,
> mentre il memory kill gate è ritirato.
> Protocollo, frontier e ledger:
> [`../REAL_NODE_REPLAY_AND_PRODUCER_LOWER_BOUND_LOOP_2026-08-31.md`](../REAL_NODE_REPLAY_AND_PRODUCER_LOWER_BOUND_LOOP_2026-08-31.md).

> **New representation decision — 2026-08-31.** Le scale tile-local condivise
> fra tutte le actions sono matematicamente valide per regret matching, ma lo
> sweep reale `K=8/16/32/64` misura `0,948x–0,977x` nel fused shadow; per-hand
> è `0,874x` e TST usa 2.631.753.824 B. Power-of-two K32 è `0,972x`. Direct
> bfloat16 fallisce il probe AHK@20 (`84,2584%` dEV e ~2x traversal), mentre
> signed-float24/bfloat16 è solo `1,396x`. Nessuna representation è promossa e
> la baseline `ScaledUint16RegretStrategy` non cambia. Non riaprire queste
> famiglie nella stessa forma senza un real-node replay corpus che dimostri
> insieme precisione e throughput. Protocollo e ledger:
> [`../NEW_PRODUCTION_STATE_REPRESENTATION_LOOP_2026-08-31.md`](../NEW_PRODUCTION_STATE_REPRESENTATION_LOOP_2026-08-31.md).

> **Frozen exact-state decision — 2026-08-30.** Il pass
> `prequantized values -> node-global scale -> encode` di
> `ScaledUint16RegretStrategy` è strutturale sotto checkpoint byte-identico.
> Lower bound 32+31 bit/entry; shadow exact massimo `1,024x`, sotto `1,3x`.
> Non riaprire retain, recompute, provisional tile, sparse journal o delayed
> finalization senza nuova prova che superi i gate del report
> [`../EXACT_STATE_REPRESENTATION_FEASIBILITY_LOOP_2026-08-30.md`](../EXACT_STATE_REPRESENTATION_FEASIBILITY_LOOP_2026-08-30.md).
> Un codec o scale semantics diversi richiedono una baseline nuova esplicita.

> **Direzione architetturale 2026-08-30.** Il cumulative optimization loop è
> **EXHAUSTED con blocker architetturale**. Streaming/rank fusion, treelet,
> liveness/cache, batching locale e varianti state/update già respinte non sono
> il prossimo passo. Il feasibility loop ha falsificato bounded wavefront puro,
> compiled traversal e continuation exact disponibile rispetto ai threshold
> richiesti, lasciando production invariata. Ogni lavoro successivo deve prima
> dimostrare un nuovo state/dataflow ceiling exact e RAM-feasible; protocollo,
> byte model e risultati sono in
> [`ARCHITECTURAL_TRAVERSAL_FEASIBILITY_LOOP_2026-08-30.md`](../ARCHITECTURAL_TRAVERSAL_FEASIBILITY_LOOP_2026-08-30.md).

> **Revalidation production final-head 2026-08-30 — storico/superseded.** Sul
> binario Release da `6508bddd039d44ecb941acded4b5b16d39f4f7e8`, AHKHQH
> converge @80 in `0,670928 s`, TH7D6S @80 in `17,645055 s` e TSTC9D @202 in
> `208,111772 s`. I limiti sono rispettivamente `1,900000 s`, `19,622222 s` e
> `128,988889 s`: AHK/TH time PASS, TST time FAIL di `79,122883 s`
> (`1,613409x`, `+61,3409%`). TST traversal/certification sono
> `174,926380 / 32,780800 s`. Tutte e tre passano dEV e Root; stato e Peak RSS
> sono misure diagnostiche. Le classificazioni memoria storiche sono ritirate.
> La fonte completa è
> [`OBJECTIVE_DRIVEN_GATE_CLOSURE_2026-08-30.md`](../OBJECTIVE_DRIVEN_GATE_CLOSURE_2026-08-30.md).

> **Correzione root-analysis 2026-08-30.** Il Root FAIL AHKHQH della baseline
> normalizzata era diagnostico: l'overload prepared valutava il root sul layout
> fisico del browser. Dopo il dispatch al layout production canonico, AHK, TH e
> TST hanno Root PASS con contratto invariato. Vedere
> [`AHKHQH_PREPARED_ROOT_ANALYSIS_FIX_2026-08-30.md`](../AHKHQH_PREPARED_ROOT_ANALYSIS_FIX_2026-08-30.md).

> **Baseline production storica (2026-08-29/30; superseded).** AHKHQH, TH7D6S e TSTC9D
> usano ora lo stesso DCFR exact signed `alpha=1.5`, `beta=0`, `gamma=2`,
> `averaging_delay=0` e massimo otto thread. Golden, curve fixed e time-to-target
> sono in
> [`PRODUCTION_DCFR_NORMALIZATION_2026-08-29.md`](../PRODUCTION_DCFR_NORMALIZATION_2026-08-29.md).
> Le baseline DCFR+ e il profilo TST-specifico `1.9/0/3` riportati sotto sono
> storici/superseded per confronti production comuni. Il prerequisito root e'
> ora superato; l'audit RBP è chiuso Categoria C e non abilita pruning.

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
- memoria solver-owned prevista e osservata, quando il ledger sarà disponibile;
- transient workspace, mapping logico, Peak RSS e private bytes come metriche
  separate.

Il valore GTO+ “Memory needed for solving” è conservato con label, unità e
precisione visualizzata. Finché non è identificato il suo perimetro, nessuna
metrica GTOSD decide un PASS/FAIL memoria. Lo schema v4 lo conserva come
`gto_plus_reference.solver_memory`; `solver_state_bytes` resta un dato parziale
di attribuzione e i picchi solver-owned restano `null` fino al ledger dedicato.

## Fixture di parità

La suite comparativa corrente comprende `GTP-AHKHQH-101`,
`GTP-TH7D6S-101` e `GTP-TSTC9D-101`. La fixture v1 AHKHQH `003` resta
congelata come riferimento storico; le fixture generiche v4 permettono di
aggiungere scenari senza modificare il codice del runner.
Lo script `tools/run_gto_plus_convergence_benchmark.ps1` avvia processi
indipendenti e produce report versionati. Il confronto primario usa la mediana
di cinque run e pubblica anche p95 e ogni campione.

La specifica generica v4 (`gtosd.gto_plus_convergence_benchmark.v4`) permette
di registrare benchmark diagnostici di convergenza senza modifiche al codice: board,
range, stack-to-pot, sizing, raise depth e nodi di riferimento EV/frequenze
sono letti dalla fixture (`benchmarks/fixtures/gto_plus_ahkhqh_101.json` è la
validazione v4 dello scenario 003). Nuovi benchmark possono qualificare
correttezza e tempo; la componente memoria resta non valutata finché non viene
dimostrata la comparabilità.

Baseline storica AHKHQH documentata (re-baseline 2026-08-05, regola all-in
naturale):

- GTO+ operativo: 1,71 s a dEV 0,98%, memoria solver 8 MB;
- GTOSD: mediana 3,128 s, p95 3,271 s (cinque run, albero 165.774 nodi);
- GTOSD state `Float32`: 6.677.088 byte, misura parziale non confrontabile con
  il display GTO+ da 8 MB;
- gate tempo `<=1,900000 s`: FAIL;
- confronto memoria: `NOT_EVALUATED`;
- gate correttezza (`correctness_gate`, EV del nodo root): PASS dal 2026-08-02;
  gli EV BTN condizionali e le frequenze restano diagnostica (con il root lock
  F10.4 i delta BTN scendono a +0,0348 / +0,0366 ante,
  vedi GTO_PLUS_PARITY_JOURNEY.md).

Il tentativo GTO+ a target 0,10% è censurato a `>245 s` e non sostituisce il
riferimento operativo senza ridefinire l'intero protocollo.

## Checkpoint storico della suite — 2026-08-14

Checkpoint storico dello stato persistente: i tre scenari sono stati rieseguiti separatamente sul
build Release `out/build/windows-release-current` con il formato core packed
`Float13RegretFloat11Strategy` (3 byte/action, compute float64). La fixture v2
AHKHQH-101 è la validazione eseguibile dello scenario canonico 003; il vecchio
file v1 003 non è presente nel worktree. La chiusura del 2026-08-14 ha
ricompilato `gto_cli` e il riferimento GTO+ e ha eseguito quest'ultimo con esito
PASS; il 16/16 CTest del 2026-08-13 è evidenza precedente e non viene presentato
come nuova esecuzione sul checkpoint odierno.

| Benchmark | dEV GTOSD | Root EV GTOSD / GTO+ | Tempo / limite 90% | Solver state / display GTO+ non comparabile | Stato |
|---|---:|---:|---:|---:|---|
| `GTP-AHKHQH-101` (scenario 003) | 0,982960% @ 100 | 19,123322 / 19,15 | 4,970917 s / 1,900000 s | 2.503.908 B / 8.000.000 B | dEV/root PASS; memoria N/E; tempo FAIL |
| `GTP-TH7D6S-101` | 0,986976% @ 82 | 8,220073 / 8,22198 | 37,810434 s / 19,622222 s | 249.955.776 B / 399.000.000 B | dEV/root PASS; memoria N/E; tempo FAIL |
| `GTP-TSTC9D-101` | 0,983565% @ 200 | 8,498226 / 8,50165 | 690,307523 s / 120,600000 s | 1.747.903.656 B / 2.000.000.000 B | dEV/root PASS; memoria N/E; tempo FAIL; metadata incompleti |

Questi sono singoli run storici, non mediane temporali. La colonna memoria
accosta due metriche non equivalenti soltanto per preservare il dato originario;
non dimostra un gate. I due report TSTC9D citati misuravano Peak RSS di
3.166.359.552 e 3.173.212.160 byte: sono osservazioni del processo, non FAIL
rispetto al display GTO+. La sostituzione architetturale è
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

dEV `0,9857595%` e root EV `8,4925400` (delta `-0,0091100`) passano; Peak RSS
`1.968.537.600 B` e stato `1.472.605.376 B` sono diagnostici. Il tempo fallisce sia il
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

Questo checkpoint storico è superato dalla chiusura 2026-09-06 riportata in
testa al documento. La profilazione R3 è stata eseguita su D/V, i due candidati
R4 selezionati sono stati respinti e nessuna modifica prestazionale è stata
promossa. Una riapertura richiede nuova evidenza architetturale o matematica,
non una micro-ottimizzazione mirata a una fixture.

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
da 4 byte stima 1,888-1,918 GB. Questi valori restano stime architetturali; i
precedenti giudizi rispetto a 1,8/2 GB non sono gate correnti. Il report è
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
modifica i gate prestazionali del percorso standard. La roadmap corrente
inserisce R2-S prima delle ottimizzazioni profonde: strategy tying e subgame
vengono valutati come architetture sperimentali DCFR, senza riaprire il fallback
fisico range-aware già bloccato e senza sbloccare F11+.

## Baseline di prodotto 2026-09-05

La prima correzione del piano product non è uno speedup: centralizza il profilo
ProductionDcfr 1.0 e rimuove i fallback numerici nascosti dai caller. I tempi
smoke registrati nel documento di esecuzione dimostrano soltanto che solve e
resume operativi completano; non sono B0 e non sostituiscono cinque processi
indipendenti per fixture.

Il report benchmark misura ora, senza startup o parsing, `build_to_ready`,
`solve_to_consultable` e `build_to_consultable` sul gioco e sui range reali,
oltre a kernel, certificazione, peak RSS e `solver_state_bytes`. Il runner
aggregante richiede cinque processi e un preflight CPU/RAM prima di ciascuno. La
distribuzione B0 controllata è congelata in
`out/production-dcfr-product-b0-controlled-20260905`. Il gate RAM esterno resta
`NOT_EVALUATED_COMPARABILITY_UNRESOLVED` finché il display GTO+ e la misura del
processo non hanno perimetro equivalente.

La B0 controllata conserva 15 processi accettati. Le mediane/p95 solver sono
AHK `0,788373/0,817360 s`, TH `22,682420/22,975138 s` e TST
`245,082130/304,203644 s`; le mediane build-to-consultable sono rispettivamente
`0,829805`, `23,042815` e `247,841130 s`. Tutti i crossing e i gate root passano
a `80/80/160`; TH e TST falliscono il gate tempo. AHK/TH conservano inoltre
diagnostici EV/action GTO+ fuori tolleranza. R2 è una baseline riproducibile,
non una qualification.

L'audit dinamico `gtosd.production_dcfr_anti_specialization_audit.v1` verifica
rinomina/metadati, riserializzazione, permutazione globale dei semi e
perturbazioni di range, stack e sizing. Phase10 confronta inoltre i due lati del
dispatch di residency e richiede checkpoint materializzati byte-identici. H non
è usato da questo audit.

Il candidato A0 `made_hand_value` è stato eseguito in Release su D/V a 32
iterazioni. D riduce 1.015.872 infoset a 72.052, usa 4.700.960 B di mapping e
1.152.832 B di stato; V riduce 1.997.952 infoset a 141.328, usa 9.242.624 B di
mapping e 4.478.080 B di stato. Il profilo R3 a cinque processi misura wall
operativo mediano `3,67412 s` contro `0,713685 s` exact su D e `8,62473 s`
contro `1,13260 s` exact su V. Su V NashConv è `0,00330981` contro `0,00175170`
exact, con delta massimo di profilo `0,00590437` ante e di BR `0,0109969` ante.
A0 non è uno speedup né un candidato production qualificato.

## Benchmark grande TH7D6S — checkpoint storico 2026-08-08

I numeri seguenti documentano una tappa precedente e sono superati dal
checkpoint di suite del 2026-08-09 riportato sopra.

Il secondo benchmark comparativo è `GTP-TH7D6S-101`. I riferimenti GTO+ sono
17,66 s e il display “Memory needed for solving” da 399 MB; il gate tempo al
90% è `<=19,622222 s`, mentre non esiste ancora una soglia memoria GTOSD
comparabile.

- stato mixed lossless: regret float24, average strategy binary16, calcolo e
  certificazione float64;
- stato solver: **416.592.960 B**, dato parziale di attribuzione; Peak RSS è
  pubblicato separatamente e nessuno dei due decide il confronto GTO+;
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

## River bucket-native 2026-09-06

Il kernel sperimentale river elimina i deal fisici dal traversal dopo una
preparazione esatta di pesi e compatibilità. Su full range conta 188.790 deal,
611 coppie di bucket e 25 bucket per player. Il lavoro per passata passa da
1.699.110 nodi fisici a 5.499 nodi bucket; il modello nativo è 55.972 B senza
overhead allocator e senza materializzare il `FiniteGame` di validazione.

Cinque processi Release MSVC 18.8 x64, 128 iterazioni:

| Caso | Solve exact, mediana | Solve native, mediana | Riduzione nodi | NashConv originale exact/native |
|---|---:|---:|---:|---:|
| D | 7,2651 ms | 3,1269 ms | 77.761 → 811 | 0,000202927 / 0,00377002 |
| V | 8,0537 ms | 3,0401 ms | 77.761 → 631 | 0,0000145436 / 0,000511609 |

Questi numeri provano uno speedup sul micro-corpus, non una qualifica product.
Il confronto include solve-to-profile ma non startup; build e oracolo fisico
sono misurati separatamente. Il kernel è single-thread, river-only e non passa
da CLI/GUI. Il costo astratto è visibile nella NashConv originale.

### Qualifica River v1

Il corpus indipendente a sette fixture misura build più solve per entrambi i
percorsi, esclude startup e alterna l'ordine su cinque ripetizioni. La decisione
Release è `REJECTED`, 0/7. Lo speedup passa soltanto su monotone, straight board
e ace-low straight (`2,346x`, `4,063x`, `4,049x`); sugli altri casi varia da
`0,098x` a `0,721x`. La riduzione dei nodi varia da `8,34x` a `892,33x`, ma non
predice da sola il wall perché il builder enumera e aggrega i deal.

Bucket NashConv nel gioco originale varia da `0,00550591` a `0,05338713` e
supera la soglia `0,005` in 7/7. Il byte model resta fra 5.932 e 90.639 B e
passa sempre il limite di 1 MiB. Quindi il candidato riduce bene la
rappresentazione, ma non offre insieme qualità e tempo sufficienti.

Il timer bucket non include lift e BR nel gioco fisico, mentre il percorso
exact include le proprie certificazioni. È quindi un limite superiore favorevole
allo speedup del candidato, non un confronto del workflow completo. Poiché il
candidato fallisce già così, questa asimmetria non può trasformare il risultato
in un falso `REJECTED`; impedisce invece qualsiasi claim positivo sui tre casi
più veloci.

### Qualifica River exact-blocker v2

La firma v2 aggiunge al valore finale il vettore di compatibilità contro tutte
le combo avversarie. Sul corpus congelato non comprime: 12/12 fixture hanno
rapporto nodi `1,00x`. Le regressioni hanno 96 classi per 96 combo per player;
gli holdout 112/112. Sul full range sono 465/465 e il modello nativo sale a
6.620.596 B.

Cinque ripetizioni Release a 1.024 iterazioni producono speedup exact/native fra
`0,006709x` e `0,017259x`, cioè il candidato impiega circa 58–149 volte il tempo
del percorso exact. Il byte model del corpus resta fra 373.856 e 793.104 B e
passa il limite di 1 MiB, ma non compensa il lavoro invariato e il costo della
matrice di compatibilità.

La decisione è `REJECTED_FEASIBILITY`, 0/12. Il risultato non dimostra che ogni
quoziente lossless River sia impossibile: esclude questa relazione con etichette
avversarie fisse. Un nuovo tentativo deve dimostrare automorfismi congiunti del
grafo prima di aggiungere un altro kernel.

## River lossless: limite strutturale misurato

La partizione equa pesata separa il costo di rappresentazione dal costo del
kernel. Sul full range uniforme stima una riduzione potenziale delle righe di
`10,3333x` e delle coppie compatibili di `94,1596x`. Sul corpus v2 la riduzione
è `1,00x` in 12/12 fixture, quindi il kill gate `1,25x` fallisce prima di
misurare un solver.

Non viene riportato uno speedup v3: il kernel non esiste e dedurlo dal full
range sarebbe fuorviante. Il risultato prestazionale è un blocker
architetturale per range pesati asimmetrici, non una regressione del percorso
exact corrente.
