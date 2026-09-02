# Performance

> **Production final-head — 2026-09-01, stato corrente.** La schedule comune
> qualificata e' `production_dcfr`: exact alternating signed DCFR `1.5/0/3`,
> reset one-based `1,2,5,17,65`, regret clock post-65 ritardato di una
> iterazione, delay zero, stato scaled uint16, massimo otto thread e nessuna
> logica fixture-specific. Cinque processi auditabili (`r2-r6`) con
> `certification_interval=20` riproducono AHK/TH/TST a `80/80/160` iterazioni,
> dEV `0,951423%/0,807956%/0,904505%` e mediana/p95 solver
> `0,758705/0,790918 s`, `19,948228/24,192260 s`,
> `184,095930/197,865030 s`. Peak RSS massimo TST `1.969.860.608 B`, sotto il
> cap stretto `<2 GiB` (`2.147.483.648 B`); full CTest Release corrente
> `27/27 PASS` (`218,43 s`, 2026-09-02). Il precedente
> `1.5/0/2` e' ora comparator storico. La nuova schedule e' superiore alla
> Release, ma TH e TST restano sopra i limiti GTO+ rispettivamente del
> `1,661%` e `42,722%`; il parity gate non e' ancora superato. Evidenza:
> [`../DCFR_EPOCH_RESET_GAMMA3_FEASIBILITY_2026-09-01.md`](../DCFR_EPOCH_RESET_GAMMA3_FEASIBILITY_2026-09-01.md).

> **2 GiB resource contract — 2026-09-01.** Il gate comune del processo è
> `peak_rss_bytes < 2.147.483.648 B` e non coincide con i riferimenti esterni
> `solver_memory_bytes`. Il probe GTO+ TST ha peak `2.061.889.536 B`; il nuovo
> cap rende RAM-feasible K8/K16/K32 ma non supera i loro gate di throughput.
> Nessun candidato è promosso e production resta `production_dcfr 1.5/0/3`.
> Protocollo:
> [`../TWO_GIB_RESOURCE_CONTRACT_AND_FRONTIER_RECHECK_2026-09-01.md`](../TWO_GIB_RESOURCE_CONTRACT_AND_FRONTIER_RECHECK_2026-09-01.md).

> **Research paths imported — 2026-09-02.** S6, Pure/Sync-PCFR and
> range-aware physical-orbit remain rejected for production. Their runners,
> probes and counterexample oracles are retained for reproducibility; the two
> algorithmic paths are compile-time gated and default `OFF`, while strict-cap
> profiling is opt-in. GTO+ black-box observation remains only partially
> automatable and requires a manual marker. These paths do not change
> `production_dcfr 1.5/0/3`. Evidence:
> [`../S6_COMMON_PRODUCTION_QUALIFICATION_LOOP_2026-08-31.md`](../S6_COMMON_PRODUCTION_QUALIFICATION_LOOP_2026-08-31.md),
> [`../TST_STRICT_2GB_BOTTLENECK_ATTRIBUTION_AND_FEASIBILITY_LOOP_2026-08-31.md`](../TST_STRICT_2GB_BOTTLENECK_ATTRIBUTION_AND_FEASIBILITY_LOOP_2026-08-31.md),
> [`../STRICT_2GB_EXACT_ALGORITHM_RECHECK_2026-09-01.md`](../STRICT_2GB_EXACT_ALGORITHM_RECHECK_2026-09-01.md),
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

> **Lazy-CFR feasibility — 2026-08-31.** La famiglia pubblicata è chiusa sotto
> il contratto corrente come **LAZY FAMILY RAM BLOCKER**. Un solo accumulatore
> `float32` per i `145.524.152` infoset TST aggiungerebbe `582.096.608 B` e
> porterebbe il peak ottimistico a `2.552.018.656 B`; le vere DS
> `alpha/alpha_hat/beta` per history/history-action sono maggiori. Il bitset
> nominalmente allocabile non rappresenta il residuo continuo del trigger.
> Nessun trace, oracle, solve o target-driven è stato eseguito. Production
> resta common `1.5/0/2`; S6 `1.5/0/5` resta STRONG RESEARCH BASELINE.
> Protocollo e lower bound:
> [`../EXACT_LAZY_CFR_FEASIBILITY_LOOP_2026-08-31.md`](../EXACT_LAZY_CFR_FEASIBILITY_LOOP_2026-08-31.md).

> **Predictive-CFR feasibility — 2026-08-31.** PCFR+ e PDCFR+ sono chiusi
> sotto il contratto corrente come **PREDICTIVE FAMILY EXHAUSTED UNDER THE
> FROZEN RAM/STATE CONTRACT**. La policy predittiva non è ricostruibile dal
> cumulative regret che deve sopravvivere all'update; la cumulative average è
> anch'essa necessaria. Il lower bound TST `uint16(actions-infosets)` porta il
> peak proiettato a `2.412.654.048 B` contro 2 GB. Nessun solve o target-driven
> è stato autorizzato. Production resta common `1.5/0/2`; S6 `1.5/0/5` resta
> STRONG RESEARCH BASELINE. Protocollo e state proof:
> [`../EXACT_PREDICTIVE_CFR_FEASIBILITY_LOOP_2026-08-31.md`](../EXACT_PREDICTIVE_CFR_FEASIBILITY_LOOP_2026-08-31.md).

> **Common schedule result — 2026-08-31.** **COMMON EXACT SCHEDULE SPACE
> EXHAUSTED** per le famiglie schedule/fixed exact studiate. S6 `1.5/0/5`
> migliora TST di circa 20–21% in iterazioni ma resta a `1,35433%` @120 e
> proietta il target circa @160 (`1,25–1,46x`). Non è production; B
> `1.5/0/2` rimane authority. Evidenza:
> [`../COMMON_EXACT_CONVERGENCE_ACCELERATION_LOOP_2026-08-31.md`](../COMMON_EXACT_CONVERGENCE_ACCELERATION_LOOP_2026-08-31.md).

> **Constraint governance gate — 2026-08-31.** L'optimization research è
> sospesa in attesa di una decisione esplicita. Il minimum resource-only
> boundary è `>=1,615339x` effective speedup su traversal+exact BR; RAM-only
> resta insufficiente, mentre la pair 8 B/action richiede cap raw
> `>=3.432.437.888 B` e exact final certification `<=8,604297 s`. Questi sono
> boundary E1/E2, non una promozione production né una modifica del benchmark.
> Matrice, dominance e decisione richiesta:
> [`../CONSTRAINT_GOVERNANCE_GATE_2026-08-31.md`](../CONSTRAINT_GOVERNANCE_GATE_2026-08-31.md).

> **Real-node joint lower bound — 2026-08-31.** Il corpus production bounded
> AHK/TH/TST è replay-fedele su code, scale e parent output. La precision
> frontier elimina regret 16–24 bit per drift multi-step e float32 direct per
> RAM (`6–8 B/action`); il producer materialization streaming vale soltanto
> ~`1,041x`. Le proiezioni TST sono `93,299454 s` ideal-state,
> `168,036868 s` ideal-producer e `89,624835 s` joint ideal. Poiché nessun
> punto realizzabile passa precisione, cap desktop e traversal
> `<=96,208089 s`, la famiglia è chiusa come strada primaria e non autorizza
> altri codec o full target-driven. Il confronto peak-RSS-vs-GTO+ non cambia.
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
> `174,926380 / 32,780800 s`. Tutte e tre passano dEV, Root,
> `solver_state_bytes` e cap desktop 2 GB; il distinto `memory_gate`
> peak-RSS-vs-GTO+ fallisce per AHK/TH e passa per TST. La fonte completa è
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

## Checkpoint storico della suite — 2026-08-14

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
