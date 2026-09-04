# Stato implementazione roadmap HU Short Deck

> **Correzione semantica memoria GTO+ 2026-09-04 — stato corrente.** I valori
> `8/399/2.000 MB` sono il campo UI “Memory needed for solving”, non Peak RSS e
> non un cap desktop generale. La loro composizione interna non è ancora
> identificata; il confronto memoria è quindi
> `NOT_EVALUATED_COMPARABILITY_UNRESOLVED`. I Peak RSS GTOSD osservati nel
> recheck 2026-09-03 (`7.790.592 B`, `363.569.152 B`, `1.534.152.704 B`) restano
> telemetria OS valida, ma il precedente claim `3/3 PASS` è ritirato. Anche il
> backend page-backed storicamente selezionato dal riferimento della fixture era
> una conseguenza del contratto errato e non costituisce parità memoria. Il
> benchmark v4 usa sempre vettori residenti e non imposta budget; il backend
> page-backed resta disponibile soltanto come opt-in esplicito e indipendente
> tramite `resident_working_set_budget_bytes`. Stato,
> dEV, root/layout, exact outcomes e test restano invariati. Piano e autorità:
> [`GTO_PLUS_SOLVER_MEMORY_SEMANTICS_AND_GATE_CORRECTION_PLAN_2026-09-03.md`](GTO_PLUS_SOLVER_MEMORY_SEMANTICS_AND_GATE_CORRECTION_PLAN_2026-09-03.md).

> **Production DCFR integration 2026-09-01 — stato corrente.** Il contratto
> comune AHK/TH/TST e' ora `production_dcfr`: exact alternating signed DCFR
> `1.5/0/3`, reset one-based `1,2,5,17,65`, regret clock post-65 ritardato di
> una iterazione, delay zero, `ScaledUint16RegretStrategy`, CPU-only e massimo
> otto thread. Cinque processi final-head auditabili (`r2-r6`) passano `15/15`
> solve con dEV `<1%`, correctness/layout/exact outcomes; il Peak RSS TST
> registrato resta diagnostico e non è sottoposto a un cap normativo.
> Iterazioni deterministiche AHK/TH/TST `80/80/160`; mediane solver
> `0,758705/19,948228/184,095930 s`; p95
> `0,790918/24,192260/197,865030 s`. Full CTest Release corrente
> `33/33 PASS` (`222,29 s`, 2026-09-04).
> La vecchia authority `1.5/0/2` e' ora il comparator Release storico. Il gate
> GTO+ resta non superato: la qualification storica fallisce i tempi TH/TST e
> la memoria non è valutabile finché la metrica non è equivalente. Report:
> [`DCFR_EPOCH_RESET_GAMMA3_FEASIBILITY_2026-09-01.md`](DCFR_EPOCH_RESET_GAMMA3_FEASIBILITY_2026-09-01.md).

> **Abstraction e subgame solving 2026-09-04 — nuova priorità di prodotto.**
> Implementati `gtosd::abstraction` e `gtosd::subgame`: feature postflop W/T/L
> exact-outcome, k-means deterministico per partizione, fingerprint/round-trip,
> riscrittura reale degli infoset CFR+, lift al gioco esatto, frontier
> reach-weighted e guard full-game exact-NashConv con fallback. Il test
> end-to-end riduce Kuhn da 12 a 8 infoset e certifica la policy rialzata; il
> resolving rifiuta un candidato sotto-allenato. Il bridge `DenseLayout` HU
> postflop è ora disponibile via API/CLI bucketed separate: CFR+ Float64
> seriale, delta combo aggregati reach-weighted prima della proiezione, lift
> combo-level, BR exact, checkpoint/resume e query bucket-aware. Il fixture CLI
> 8-bucket comprime 1.860→32 infoset e raggiunge `0,751763%` NashConv/pot a 200
> iterazioni. Restano aperti `.gtsd`, GUI, cache feature flop/turn, codec
> compresso e frontier merge postflop nativo. La parity GTO+ riprenderà dopo la
> scelta e qualifica delle granularità commerciali, non prima.

> **Schema memoria v4 implementato 2026-09-04.** Le fixture correnti dichiarano
> `gto_plus_reference.solver_memory`; report e summary separano
> `gto_plus_reference_memory`, `solver_memory_accounting` e `process_memory`.
> `memory_comparison` è `not_evaluated`, `passed` è `null` e nessun valore GTO+
> configura residenza o budget del processo. Il loader diretto accetta ancora
> v3 soltanto come `legacy_metric_misclassified`; il wrapper multiprocesso
> richiede v4.

> **Consolidamento ricerca 2026-09-02:** tooling e prove dei branch isolati
> sono ora versionati su `main`, senza modificare i default production. S6
> resta respinto. Pure/Sync-PCFR e range-aware physical-orbit sono oracle
> compile-time gated e default `OFF`; il profiling legacy strict-cap è opt-in. Il
> controesempio range-aware impedisce la promozione della famiglia
> physical-orbit con range asimmetrici. Il workflow black-box GTO+ è
> `PARTIALLY AUTOMATABLE` e richiede un marker manuale. Report:
> [`S6_COMMON_PRODUCTION_QUALIFICATION_LOOP_2026-08-31.md`](S6_COMMON_PRODUCTION_QUALIFICATION_LOOP_2026-08-31.md),
> [`TST_STRICT_2GB_BOTTLENECK_ATTRIBUTION_AND_FEASIBILITY_LOOP_2026-08-31.md`](archive/legacy-memory-gate/TST_STRICT_2GB_BOTTLENECK_ATTRIBUTION_AND_FEASIBILITY_LOOP_2026-08-31.md),
> [`STRICT_2GB_EXACT_ALGORITHM_RECHECK_2026-09-01.md`](archive/legacy-memory-gate/STRICT_2GB_EXACT_ALGORITHM_RECHECK_2026-09-01.md),
> [`SYNC_PCFR_POSTFLOP_TRAJECTORY_GATE_2026-09-01.md`](SYNC_PCFR_POSTFLOP_TRAJECTORY_GATE_2026-09-01.md),
> [`RANGE_AWARE_PHYSICAL_ORBIT_ORACLE_2026-09-01.md`](RANGE_AWARE_PHYSICAL_ORBIT_ORACLE_2026-09-01.md) e
> [`GTO_PLUS_AUTONOMOUS_BLACK_BOX_DISCOVERY_AND_CHARACTERIZATION_2026-08-31.md`](GTO_PLUS_AUTONOMOUS_BLACK_BOX_DISCOVERY_AND_CHARACTERIZATION_2026-08-31.md).

> **Precedenza storica.** Gli aggiornamenti datati 2026-08-31 e precedenti
> sotto questa sezione restano ledger storico. Ogni loro frase che presenta
> `1.5/0/2` come production corrente o la five-process come congelata e'
> superseded dal blocco 2026-09-01 sopra.

> **FD-FTRL/OMD decision 2026-08-31:** **FD-FTRL/OMD LOCAL-COST BLOCKER.**
> Il reuse byte-level `payload A = R'/Q'`, `payload B = linear average` passa
> il RAM pre-gate senza un terzo state; l'oracolo CFR/RM e CFR+/RM+ passa
> `18.670` asserzioni. Il kernel direct sul mix TST è però `3,513x` FTRL e
> `3,303x` OMD rispetto a RM. Un lower bound già favorevole lascia soltanto
> `82,4768/84,2703` iterazioni entro il limite, mentre S6 attraversa circa
> @145. Nessun solver path, enum o checkpoint FD è stato introdotto.
> Production resta common `1.5/0/2`; S6 `1.5/0/5` resta STRONG RESEARCH
> BASELINE. Report:
> [`MEMORY_NEUTRAL_FD_FTRL_OMD_FEASIBILITY_LOOP_2026-08-31.md`](MEMORY_NEUTRAL_FD_FTRL_OMD_FEASIBILITY_LOOP_2026-08-31.md).

> **Lazy-CFR decision 2026-08-31 — memory blocker ritirato.** La regola
> exact `m(I)>=B` richiede un accumulatore di reach pending distinto per
> infoset; il path pubblicato è ancora più grande perché usa state
> history/history-action. Il lower bound `float32/infoset` portava il Peak RSS
> TST da `1.969.922.048 B` a `2.552.018.656 B`; nessun trace, oracle o solver
> candidate era stato autorizzato. Il confronto con il falso cap è ritirato e
> richiede un nuovo pre-gate. Una composizione Lazy-DCFR/S6 non ha una
> derivazione primaria sound. Production resta `1.5/0/2`; S6 `1.5/0/5` resta
> STRONG RESEARCH BASELINE. Report:
> [`EXACT_LAZY_CFR_FEASIBILITY_LOOP_2026-08-31.md`](EXACT_LAZY_CFR_FEASIBILITY_LOOP_2026-08-31.md).

> **Predictive-CFR decision 2026-08-31 — memory blocker ritirato.** Il predictor
> PCFR+/PDCFR+ separa policy
> corrente predetta e cumulative regret; cumulative average occupa già il
> secondo payload production. Il lower bound TST aggiungeva `442.732.000 B` e
> proiettava `2.412.654.048 B` Peak RSS contro il cap allora assunto. Quel kill
> gate è ritirato; nessun solver candidate era stato autorizzato. Oracle formula
> 169/169 PASS; nessun enum/dispatch production.
> Production resta `1.5/0/2`, S6 `1.5/0/5` resta STRONG RESEARCH BASELINE.
> Report: [`EXACT_PREDICTIVE_CFR_FEASIBILITY_LOOP_2026-08-31.md`](EXACT_PREDICTIVE_CFR_FEASIBILITY_LOOP_2026-08-31.md).

> **Common schedule decision 2026-08-31:** **COMMON EXACT SCHEDULE SPACE
> EXHAUSTED.** S6 `1.5/0/5` è il migliore common schedule studiato ma proietta
> TST circa @160 e worst ratio `1,25–1,46`, quindi non è production. Il
> contratto globale resta signed DCFR `1.5/0/2`. Dettagli:
> [`COMMON_EXACT_CONVERGENCE_ACCELERATION_LOOP_2026-08-31.md`](COMMON_EXACT_CONVERGENCE_ACCELERATION_LOOP_2026-08-31.md).

> **Constraint governance 2026-08-31 — memoria superseded.** Il tempo e i costi
> misurati restano evidenza storica, ma la frontier che assumeva un cap RAM
> desktop non è più una decisione corrente e deve essere ricalcolata dopo la
> definizione della metrica solver-owned. Dettagli storici in
> [`CONSTRAINT_GOVERNANCE_GATE_2026-08-31.md`](CONSTRAINT_GOVERNANCE_GATE_2026-08-31.md).

> **Real-node replay decision 2026-08-31:** **JOINT STATE/PRODUCER
> LOWER-BOUND BLOCKER** nel contratto storico. Il corpus bounded AHK,
> TH e TST ha replay autorevole byte/bit-identico. Precisioni regret 16–24 bit
> mostrano drift/outlier multi-step; float32 regret è stabile ma le varianti
> direct `6–8 B/action` fallivano il cap allora assunto. Il producer streaming
> isolato ha ceiling misurato ~`1,041x` e proietta `168,036868 s`; nessun
> candidate supera insieme numerical, RAM e time gates. Non sono stati
> eseguiti solver probe candidate né target-driven. Il prossimo passo è un gate
> di governance sui vincoli. Vedere
> [`REAL_NODE_REPLAY_AND_PRODUCER_LOWER_BOUND_LOOP_2026-08-31.md`](REAL_NODE_REPLAY_AND_PRODUCER_LOWER_BOUND_LOOP_2026-08-31.md).

> **New state representation decision 2026-08-31:**
> **REPRESENTATION SPACE EXHAUSTED** per le famiglie obbligatorie studiate.
> Tile float/power-of-two e per-hand non superano il fused shadow/RAM; hybrid
> conserva un global barrier; direct bfloat16 fallisce AHK@20 (`84,2584%` dEV,
> traversal circa 2x più lento); signed-float24/bfloat16 resta sotto il gate
> `1,5x`. Ogni dispatch sperimentale è stato rimosso. Restano soltanto
> telemetria layout e benchmark/oracle generalizzabili; production e checkpoint
> sono invariati. Vedere
> [`NEW_PRODUCTION_STATE_REPRESENTATION_LOOP_2026-08-31.md`](NEW_PRODUCTION_STATE_REPRESENTATION_LOOP_2026-08-31.md).

> **Exact state decision 2026-08-30:** **EXACT REPRESENTATION BLOCKER PROVEN**
> per `ScaledUint16RegretStrategy` byte-identico. Retain, recompute, mixed,
> sparse e hierarchical non superano i gate shadow/economici; production resta
> invariata. Il prossimo passo è una task separata sul nuovo formato state e
> sulle scale semantics, non un'altra variante dello stesso dataflow. Vedere
> [`EXACT_STATE_REPRESENTATION_FEASIBILITY_LOOP_2026-08-30.md`](EXACT_STATE_REPRESENTATION_FEASIBILITY_LOOP_2026-08-30.md).

> **Architectural traversal loop 2026-08-30:** il cumulative objective loop è
> **EXHAUSTED con blocker architetturale**. Non va riaperta un'altra
> micro-ottimizzazione della rappresentazione node/action/value/state corrente.
> Il nuovo studio ha misurato batchability local/frontier/global, disgiunzione,
> byte traffic, RAM e tre famiglie architetturali. Il wavefront shadow è exact
> ma fallisce il gate sui workload mediani (`1,116x/1,327x` a width 4); un
> compiled plan elimina al massimo il `2,94%` del traversal; la continuation
> exact disponibile non elimina abbastanza materializzazione. Nessun percorso
> production è stato modificato. Evidenza e ledger:
> `ARCHITECTURAL_TRAVERSAL_FEASIBILITY_LOOP_2026-08-30.md`.

> **Revalidation final-head 2026-08-30:** le tre fixture sono state eseguite
> target-driven sullo stesso binario Release da
> `6508bddd039d44ecb941acded4b5b16d39f4f7e8`. AHKHQH chiude @80 in
> `0,670928 s`, TH7D6S @80 in `17,645055 s`, entrambi time PASS; TSTC9D chiude
> @202 in `208,111772 s` contro `128,988889 s`, time FAIL di `79,122883 s`
> (`+61,3409%`). Tutte passano dEV, Root, payoff-sum, layout, convergence e
> `solver_state_bytes`. Peak RSS resta un dato distinto; le classificazioni
> memoria allora pubblicate sono ora semanticamente invalide. Full CTest
> finale 21/21 PASS. La matrice e gli artifact sono in
> `OBJECTIVE_DRIVEN_GATE_CLOSURE_2026-08-30.md`.

> **Audit RBP 2026-08-30:** implementata soltanto telemetria read-only ai
> checkpoint, con stato solver byte-identico OFF/ON. AHKHQH, TH7D6S e TSTC9D
> hanno zero action entry che garantiscano almeno un'iterazione di pruning con
> la formula CFR originale. Esito **Categoria C**: RBP non e' applicabile in
> modo sound al DCFR production `1.5/0/2`; il pruning effettivo resta non
> implementato e nessun gate e' sbloccato. Evidenza in
> `RBP_READ_ONLY_AUDIT_2026-08-30.md`.

> **Aggiornamento root 2026-08-30:** corretto il dispatch del root prepared dal
> browser fisico al layout production canonico. AHK target @80 e' ora
> `19,108987 / 19,15`, delta `-0,041013`, Root PASS; TH e TST restano Root PASS,
> CTest Release 20/20. Il prerequisito root per l'audit RBP e' superato; il
> pruning RBP non e' implementato. Dettagli in
> `AHKHQH_PREPARED_ROOT_ANALYSIS_FIX_2026-08-30.md`.

> **Verifica documentale:** 2026-08-29, piano P0-P7 chiuso con Esito B.
> Questo file è la dashboard dello stato implementato; i report `PHASE_*` restano
> storici e il gate prestazionale è normato da `GTO_PLUS_PARITY_JOURNEY.md`.
> La build Release completa passa; il riferimento GTO+ passa 24 asserzioni,
> fallback asimmetrico e root lock, con differenziale seriale/parallelo nullo.
> La suite finale CTest è 21/21 PASS. Persistent scale, cache showdown e
> action-liveness sono stati misurati e rimossi perché regressivi; il report
> corrente è `OBJECTIVE_DRIVEN_GATE_CLOSURE_2026-08-30.md`, mentre
> `NEXT_OPTIMIZATION_RESULTS_2026-08-29.md` resta storico.

> **Aggiornamento 2026-08-29/30:** il contratto production e' congelato a DCFR
> exact signed `1.5/0/2`, delay zero e otto thread per AHK/TH/TST. La baseline
> normalizzata e' in `PRODUCTION_DCFR_NORMALIZATION_2026-08-29.md`. Il Root FAIL
> AHK e il blocco RBP della prima versione del report sono superseded dal fix
> prepared-root 2026-08-30. L'audit RBP read-only successivo ha Esito C e non
> modifica il motore production.

Aggiornato: 2026-09-01

Le specifiche tecniche canoniche sono indicizzate in
[`specifications/README.md`](specifications/README.md). Questo documento
riassume gate ed evidenza di implementazione.

## Stato sintetico dei gate

| Gate | Stato | Evidenza attuale | Lavoro residuo principale |
|---|---|---|---|
| F0 | **Completata** | Build riproducibile CMake/vcpkg; preset Debug, Release e ASan; 5/5 test verdi in ogni preset; benchmark e install tree verificati | Nessun residuo F0; resta da osservare la prima esecuzione della nuova workflow su GitHub Actions |
| F1 | **Completata** | 641.528 asserzioni, 240 combinazioni parametrizzate, 100.000 transizioni randomizzate, Debug/Release/ASan/UBSan verdi | Nessun residuo F1; le transizioni chance appartengono a F3 |
| F2 | **Completata** | Evaluator exact first-party, adapter `IHandEvaluator`, showdown 2–6 player, 9.801.957 asserzioni exhaustive e 1.000.000 di deal nightly con zero mismatch | Nessun residuo F2; il benchmark batch è una baseline misurata, non uno SLA |
| F3 | **Completata** | Modulo `gtosd::tree`, 13.191 asserzioni, 1.056 runout ordinati, snapshot/hash deterministico, Debug/Release/ASan/UBSan verdi | Nessun residuo del gate locale; confronto esterno GTO+ rinviato finché non viene fornita una configurazione di riferimento |
| F4 | **Completata** | Modulo `gtosd::isomorphism`, tutte le 24 permutazioni, mapping inverso, 7.140 flop fisici e 573 orbite, chance con molteplicità | Nessun residuo F4 |
| F5 | **Completata** | Moduli `gtosd::solver` e `gtosd::best_response`, cinque algoritmi, exact BR/NashConv, 79 asserzioni e sanitizer verdi | Cross-check OpenSpiel/sequence-form resta test-only futuro; non è un gate bloccante |
| F6 | **Completata** | Tre prototype report, nove preflight exact, parità EV/NashConv e probe RSS out-of-core | Nessun residuo del gate memoria; traversal poker production appartiene a F7 |
| F7 | **Completata** | Modulo `gtosd::postflop`, CFR+ exact, BR/NashConv, checkpoint/resume, query, PF-F1 a 0,741405%, layout range-aware, infoset canonici e public DAG lossless | La baseline naturale GTO+ usa 385.980 infoset, 834.636 action entry, 46.065 nodi pubblici canonici e 165.774 nodi fisici; la costruzione parte ancora dal tree fisico |
| F8 | **Completata** | Modulo `gtosd::storage`, `.gtsd` 1.0 chunked, Zstd, secretstream, random access, atomic save, migrazione, verifier, catalogo SQLite e round-trip byte-exact dello stato packed 13+11 | Le vecchie misure PF-F1 non sostituiscono i tre run di certificazione RAM correnti |
| F9 | **Completata localmente** | Qt/ImGui, 7/7 E2E, 19/19 regression, tre backend sopra 60 FPS, install tree verificato | Qualifica su hardware esattamente 4-core/2 GHz/16 GB resta release gate F10 |
| F10 | **Completata localmente** | `gto_gui` Qt, pannelli CO/OOP e BTN/IP, board visuale 3–5 carte, Target dEV, range quadrati paint-on-click/slider, pausa/cancel, memoria solver canonica separata dal peak RSS, chiavi locali trasparenti, log persistenti, recovery cifrato, albero orizzontale, selettore turn/river, heatmap 9×9 read-only ed E2E create→solve→save→reopen→navigate→resume | Qualifica personale e su hardware esattamente 4-core/2 GHz/16 GB restano gate distinti |
| GTO+ parity gate | **NON SUPERATO; temporaneamente posposto** | Production final-head exact conserva `15/15` correctness solve; TH/TST restano sopra i time gate e la memoria è `NOT_EVALUATED_COMPARABILITY_UNRESOLVED` | Riprendere la parity sul percorso bucketing/subgame dopo integrazione postflop; exact resta oracle |
| Backend di calcolo | **CPU/RAM only** | Contratto permanente: solver, CFR, best response e certificazione non usano GPU o acceleratori di calcolo | Conservare il confine anche nelle ottimizzazioni future; la GPU può soltanto renderizzare la GUI |
| Abstraction/subgame core | **Implementato; bridge HU postflop iniziale qualificato** | Moduli versionati, `DenseLayout` CFR+ bucketed, CLI solve/resume/query/certify, exact lift/BR guard, fallback e benchmark | Collegare `.gtsd`, GUI e frontier mid-tree; qualificare granularità reali |
| F11+ | **Riattivata dalla decisione 2026-09-04** | Card abstraction, safe subgame core e bridge postflop completati | Selezionare granularità commerciali e proseguire con persistenza/GUI prima della parity |

## Fase 0 — Fondazioni del repository

### Esito

Il gate F0 è completato localmente. Il repository dispone di un percorso di
build C++20 riproducibile, dipendenze bloccate da baseline vcpkg, runner reali
GoogleTest e Google Benchmark, controlli statici non mutanti, installazione
locale e workflow CI per Debug, Release e sanitizer.

Non viene dichiarato che la workflow remota sia già verde: il file CI è stato
implementato e validato staticamente, mentre l'esecuzione GitHub Actions potrà
essere osservata solo dopo un push.

### Copertura delle attività della roadmap

| # | Requisito F0 | Stato | Implementazione ed evidenza |
|---:|---|---|---|
| 1 | Root `CMakeLists.txt` | Completato | Progetto `gtosd` C++20, opzioni di build, target modulari, test, benchmark, install ed export CMake |
| 2 | Preset `windows-debug`, `windows-release`, `windows-asan` | Completato | Preset Ninja single-config in `CMakePresets.json`; compilatore e Ninja risolti da variabili dell'ambiente Visual Studio, senza path macchina codificati nel repository |
| 3 | Manifest vcpkg con versioni pinned | Completato | `vcpkg.json` usa la baseline immutabile `cd61e1e26a038e82d6550a3ebbe0fbbfe7da78e3` |
| 4 | `/W4 /permissive-` | Completato | Applicati tramite `gtosd_set_warnings()` a tutti i target first-party |
| 5 | `/WX` nei target core CI | Completato | `GTOSD_WARNINGS_AS_ERRORS=ON` è il default dei preset e della CI; le build locali finali non hanno prodotto warning first-party |
| 6 | GoogleTest e Google Benchmark | Completato | `GTest::gtest_main` con discovery CTest; `benchmark::benchmark` e `benchmark::benchmark_main` con benchmark `BM_EvaluateSeven` |
| 7 | clang-format e clang-tidy senza rewrite CI | Completato | Target `format-check` usa `--dry-run --Werror`; clang-tidy viene eseguito durante la compilazione e non modifica i sorgenti |
| 8 | GitHub Actions Windows x64 Debug/Release | Completato | Matrice `windows-debug`/`windows-release`, bootstrap vcpkg pinned, build, test, CLI smoke, install e benchmark |
| 9 | Sanitizer clang-cl dove supportato | Completato | Job Windows clang-cl ASan e job Linux UBSan; preset MSVC ASan locale; directory runtime del compilatore propagata ai test CTest |
| 10 | Policy `Result<T, Error>` | Completato | `Result` è `[[nodiscard]]`; policy degli errori, eccezioni e diagnostiche documentata in `ERROR_AND_VERSIONING_POLICY.md` |
| 11 | Semantic versioning file/API | Completato | API corrente `0.11.0` generata da CMake; major/minor espliciti per public tree, solution, checkpoint e card abstraction; incompatibilità major testata |
| 12 | `THIRD_PARTY_NOTICES.md` | Completato | Baseline, versioni risolte, licenze e distinzione dipendenze production/development registrate |

### Dipendenze risolte

| Pacchetto | Versione bloccata | Uso attuale |
|---|---:|---|
| Google Benchmark | 1.9.5 | Benchmark runner |
| GoogleTest | 1.17.0, port revision 2 | Test runner di infrastruttura |
| nlohmann/json | 3.12.0, port revision 2 | Pinned per moduli di configurazione futuri |
| spdlog | 1.17.0 | Pinned per logging futuro |
| fmt | 12.2.0 | Dipendenza transitiva di spdlog |

La fonte normativa delle licenze e delle condizioni di redistribuzione resta
`THIRD_PARTY_NOTICES.md`; questa tabella registra soltanto lo stato del gate.

### Verifiche eseguite il 2026-07-27

| Verifica | Configurazione | Risultato |
|---|---|---|
| Configure pulito | `windows-release --fresh`, CMake 4.4, Ninja, MSVC 19.51 | PASS |
| Build Release | `/W4 /permissive- /WX` | PASS, zero warning first-party |
| CTest Release | GoogleTest, core, F1, benchmark smoke | PASS, 5/5 in 2,90 s nella verifica finale |
| Benchmark Release | `BM_EvaluateSeven`, minimo 0,05 s | PASS, circa 716,8k valutazioni/s; misura smoke, non SLA |
| CLI smoke | `gto_cli self-check` | PASS, versione `0.1.0`, 36 carte, 630 combo, 81 classi e root HU coerenti |
| Install tree | `out/install/windows-release-final` | PASS, libreria, CLI, header pubblici, header versione generato ed export CMake |
| Configure pulito | `windows-debug --fresh` | PASS |
| Build e CTest Debug | `/W4 /permissive- /WX` | PASS, 5/5 in 25,55 s |
| Configure pulito | `windows-asan --fresh` | PASS |
| Build e CTest ASan | MSVC AddressSanitizer | PASS, 5/5 in 25,95 s |
| Suite F1 sotto ASan | 641.528 asserzioni e 100.000 transizioni | PASS, nessun errore sanitizer |
| clang-format | Tutti i file C++ first-party | PASS, dry-run senza riscrittura |
| clang-tidy | Target core first-party | PASS, nessun warning first-party |
| Path audit | CMake, preset, manifest, app, librerie, header, test, benchmark, CI | PASS, nessun riferimento a `F:\` |
| Install senza sorgenti esterne | Build e install eseguiti interamente dalla checkout corrente | PASS |

I tempi sono misure della macchina locale e non costituiscono una garanzia di
prestazioni. La prova ASan finale ha riusato i pacchetti già materializzati
nell'albero del preset dopo il configure pulito; non ha ridotto né escluso
alcun test.

### Comandi canonici Windows

Da Visual Studio Developer PowerShell:

```powershell
$env:VCPKG_ROOT = "C:\path\to\vcpkg"
$env:GTOSD_NINJA_EXE = (Get-Command ninja).Source

cmake --preset windows-release --fresh
cmake --build --preset windows-release --parallel
ctest --preset windows-release

.\out\build\windows-release\apps\gto_cli\gto_cli.exe self-check
.\out\build\windows-release\benchmarks\gtosd_benchmark_smoke.exe `
  --benchmark_filter=BM_EvaluateSeven `
  --benchmark_min_time=0.05s

cmake --install out/build/windows-release `
  --prefix out/install/windows-release
```

Per Debug e ASan si sostituisce `windows-release` rispettivamente con
`windows-debug` e `windows-asan`.

### Criteri del gate F0

| Criterio | Esito | Nota |
|---|---|---|
| Build Release pulita | PASS | Configure `--fresh`, dipendenze risolte dalla baseline pinned, compilazione completa |
| Test runner verde | PASS | 5/5 Release, 5/5 Debug, 5/5 ASan |
| Zero warning target first-party | PASS | Warning elevati a errori; build completate |
| Nessuna dipendenza da path assoluti | PASS | Solo variabili ambiente per toolchain; nessun path macchina salvato nei file di progetto |

## Fase 2 — Evaluator e showdown

### Esito

Il gate F2 è completato localmente. Il production path dispone di un evaluator
Short Deck exact best-five-of-seven, di un adapter sostituibile
`IHandEvaluator`, di valutazione scalar e batch e di showdown esatto per 2–6
player. Evaluator e showdown sono separati nel modulo CMake `gtosd::equity`;
la nuova API pubblica incrementa coerentemente la versione a `0.2.0`.

Il percorso exact non contiene Monte Carlo, fallback uniformi, dipendenze Python
o `-ffast-math`. Gli input non validi producono un `EquityError` tipizzato;
un'eccezione proveniente da un evaluator collegato viene tradotta in
`InternalEvaluatorFailure` senza inventare valori o winner.

### Copertura delle attività della roadmap

| # | Requisito F2 | Stato | Implementazione ed evidenza |
|---:|---|---|---|
| 1–2 | Provenienza e copia del codice autorizzato | Completato senza copia | La sorgente indicata dalla roadmap è stata identificata con hash SHA-256, ma non contiene licenza; `THIRD_PARTY_NOTICES.md` registra l'audit. Per eliminare il rischio di titolarità, nessun file esterno è stato copiato e l'evaluator production è first-party |
| 3 | Nessuna dipendenza Python production | Completato | `gtosd::equity` è C++20 puro e dipende soltanto da `gtosd::core` |
| 4 | Adapter `IHandEvaluator` | Completato | Interfaccia virtuale tipizzata e implementazione `ExactHandEvaluator` |
| 5 | Eliminare fallback silenziosi | Completato | Ogni funzione exact restituisce un valore esatto oppure `EquityError`; nessun path Monte Carlo |
| 6 | Separare evaluator ed equity/showdown | Completato | `evaluator.hpp/.cpp` valuta le mani; `showdown.hpp/.cpp` valida board e hole card e costruisce il winner mask |
| 7 | Winner mask multi-player | Completato | Winner unico e tie completo verificati per ogni player count da 2 a 6 |
| 8 | Tie split fixed-point | Completato | Winner mask integrato con `split_pot`; test a sei player verifica che nessuna unità venga persa |
| 9 | Disabilitare `-ffast-math` | Completato | Il flag non è presente nei target first-party; MSVC, ASan e GCC UBSan sono verdi |
| 10 | Benchmark scalar e batch | Completato | `BM_EvaluateSeven` e `BM_EvaluateSevenBatch` usano fixture deterministiche |

### Oracle e copertura test

L'oracle di test è un'implementazione first-party indipendente: valuta
direttamente i conteggi di rank, i mask di seme e i tie-breaker su cinque o
sette carte. Non richiama `evaluate_five`, `evaluate_seven` o lo showdown
production.

| Verifica | Copertura | Risultato |
|---|---:|---:|
| Enumerazione five-card | Tutte le `C(36,5) = 376.992` mani | Zero mismatch |
| Permutazioni globali dei semi | 24 per ogni mano five-card | Zero variazioni |
| Suite exhaustive F2 | 9.801.957 asserzioni | PASS |
| Categorie e tie-breaker | Wheel `A-6-7-8-9`, flush sopra full, quads, doppio tris, tre coppie, sei carte suited | PASS |
| Showdown | Winner unico e tie per 2, 3, 4, 5 e 6 player | PASS |
| Input malformati | Board incompleto, hole-card count errato, duplicati e overlap, player count non supportato | Errori tipizzati |
| Errore evaluator | Adapter che solleva eccezione | `InternalEvaluatorFailure` |
| Nightly deterministica | Seed `1040974684198`, 1.000.000 deal validi | Zero mismatch |

La workflow CI contiene un job schedulato e avviabile manualmente che compila
`GTOSD_BUILD_NIGHTLY_TESTS=ON` ed esegue la label CTest `nightly`. Come per gli
altri job CI, non viene dichiarato un esito remoto finché la workflow non sarà
eseguita dopo un push.

### Verifiche eseguite il 2026-07-28

| Verifica | Configurazione | Risultato |
|---|---|---|
| Build Release | MSVC 19.51, C++20, `/W4 /permissive- /WX` | PASS, zero warning first-party |
| CTest Release | Nightly abilitata localmente | PASS, 7/7 in 7,30 s |
| CTest Debug | Nightly esclusa | PASS, 6/6 in 39,33 s |
| CTest AddressSanitizer | Nightly esclusa | PASS, 6/6 in 49,41 s; nessun errore sanitizer |
| GCC UBSan | GCC 13.3, `-fno-sanitize-recover=all` | Suite exhaustive e million-deal PASS |
| clang-format | Tutti i file F2 | PASS, `--dry-run --Werror` |
| clang-tidy | Target `gtosd::core` e `gtosd::equity` | PASS, nessun warning first-party |
| Install tree | `out/install/windows-release-f2` | PASS, librerie e header esportati come `gtosd::core` e `gtosd::equity` |

### Benchmark Release

Macchina osservata da Google Benchmark: 8 logical CPU a 3,6 GHz, cache L3 da
6 MiB. Cinque ripetizioni, tempo minimo 0,2 s:

| Benchmark | Media | Mediana | Interpretazione |
|---|---:|---:|---|
| `BM_EvaluateSeven` | 750,6k mani/s | 749,6k mani/s | Baseline scalar |
| `BM_EvaluateSevenBatch` | 591,7k mani/s | 589,8k mani/s | Baseline API batch con materializzazione del vettore risultato |

Queste sono misure locali, non uno SLA. Il batch dimostra il contratto e rende
misurabile la futura ottimizzazione cache-aware; non viene dichiarato più veloce
del percorso scalar.

### Criteri del gate F2

| Criterio | Esito | Evidenza |
|---|---:|---|
| Zero mismatch contro oracle | PASS | Exhaustive five-card e un milione di deal seven-card |
| Nessun fallback Monte Carlo exact | PASS | Nessuna implementazione Monte Carlo nel modulo `equity` |
| Errore esplicito per input invalido | PASS | `EquityError` verificato per tutte le condizioni esprimibili dall'API tipizzata |

## Fase 3 — Public tree postflop senza isomorfismi

### Esito

Il gate locale F3 è completato. Il nuovo modulo pubblico `gtosd::tree`
costruisce un albero fisico, deterministico e ispezionabile dal flop al river.
Non applica canonicalizzazione dei semi: ogni carta pubblica legale è
materializzata come edge distinta con molteplicità fisica unitaria.

Il public tree non incorpora hole card o range. Conserva tutti i rami pubblici
fisici; durante il traversal, `condition_chance_edges` applica card removal
alle carte private/dead e rinormalizza esattamente il denominatore. Questo
evita di confondere probabilità pubbliche `33/32` con quelle condizionate HU
`29/28`.

### Copertura delle attività della roadmap

| # | Requisito F3 | Stato | Implementazione |
|---:|---|---|---|
| 1 | DTO configurazione | Completato | `PostflopTreeConfig`, configurazioni per street/player/scenario e rake tipizzato |
| 2 | JSON Schema | Completato | `schemas/postflop_tree_config.schema.json`, versione 1, campi chiusi e limiti numerici |
| 3 | Scenari CO/BTN | Completato | `Lead`, `AfterCheck`, `FacingBet` risolti dallo stato pubblico |
| 4 | Massimo tre size | Completato | Validazione DTO/parser e deduplicazione monetaria delegata al core F1 |
| 5 | Raise depth `0..4` | Completato | Configurazione per scenario; nessun quinto raise non all-in |
| 6 | Transizioni street | Completato | Check–check, bet–call e raise–call attraversano chance fino al river |
| 7 | Chance fisiche | Completato | 33 turn e 32 river per ogni turn pubblico, senza sampling |
| 8 | All-in runout | Completato | Flop all-in–call distribuisce turn e river; turn all-in–call distribuisce river |
| 9 | Terminal showdown | Completato | `resolve_showdown_terminal` integra `evaluate_showdown` F2 e `Settlement` F1 |
| 10 | Tree inspector CLI | Completato | `gto_cli tree-inspect <config.json> [maximum_nodes]` |
| 11 | Stima eager | Completato | Preflight esatto di nodi/edge/byte senza materializzare il vettore dei nodi |
| 12 | Hash betting tree | Completato | Snapshot deterministico versionato `fnv1a64` |

### Snapshot fisico approvato

Fixture: flop `As Qd 7c`, pot 10 ante, stack 20 ante, linee check-only.

| Metrica | Valore |
|---|---:|
| Nodi | 3.270 |
| Edge | 3.269 |
| Decision node | 2.180 |
| Chance node | 34 |
| Terminal showdown | 1.056 |
| Chance edge | 1.089 |
| Profondità massima | 8 |
| Stima eager | 1.360.264 byte |
| Hash | `fnv1a64:0d2cb83058ae7460` |

Le 1.056 board complete corrispondono a `33 × 32` runout ordinati. Con quattro
hole card HU disgiunte, il primo chance node viene condizionato da 33 a 29
turn; al turn, il denominatore condizionato sarà 28.

### Verifiche eseguite il 2026-07-28

| Verifica | Risultato |
|---|---|
| MSVC Release `/W4 /WX` | PASS, suite completa 7/7 in 4,70 s |
| MSVC Debug `/W4 /WX` | PASS, suite completa 7/7 in 41,60 s |
| MSVC AddressSanitizer | PASS, suite completa 7/7 in 65,46 s |
| GCC UBSan `-fno-sanitize-recover=all` | PASS, suite F3 senza undefined behavior |
| Suite F3 | PASS, 13.191 asserzioni |
| clang-format | PASS, dry-run `--Werror` |
| clang-tidy | PASS sul production target `gtosd::tree` |
| Install tree | `gtosd::tree`, header pubblici, schema JSON e CLI installabili |

La workflow CI è stata estesa affinché clang-cl ASan e Linux UBSan
materializzino anche `nlohmann-json` tramite la baseline vcpkg bloccata. Come
per F0–F2, non viene dichiarato un esito remoto prima di un push.

Il confronto manuale con GTO+ non è dichiarato eseguito: richiede una
configurazione e un node count di riferimento forniti dall'esterno. Il gate
locale usa snapshot first-party deterministici e copre tutte le invarianti
fisiche richieste.

## Fase 4 — Isomorfismo globale lossless

### Esito

Il gate locale F4 è completato. Il nuovo modulo pubblico
`gtosd::isomorphism` applica una sola permutazione globale a board, range,
private deal, dead card, carte future e nodelock. La chiave canonica è il
minimo lessicografico delle 24 rappresentazioni e conserva mapping diretto e
inverso per riportare strategie e nodelock ai semi fisici.

L'aggregazione chance conserva ogni outcome fisico e registra
`physical_outcome_count / total_legal_outcome_count`. Prima di
canonicalizzare un figlio, i range vengono condizionati rimuovendo le combo
bloccate dalla nuova carta pubblica. Non esistono sampling, bucketing o
fallback approssimati.

### Copertura delle attività della roadmap

| # | Requisito F4 | Stato | Implementazione |
|---:|---|---|---|
| 1 | 24 permutazioni | Completato | Enumerazione deterministica dell'intero gruppo `S4` |
| 2 | Canonical key | Completato | Minimo lessicografico versionato `GTOSD_ISO_1` |
| 3 | Range e nodelock | Completato | Trasformazione globale, validazione blocker e massa nodelock completa |
| 4 | Inverse mapping | Completato | Mapping canonico→fisico verificato con round-trip completo |
| 5 | Molteplicità chance | Completato | Raggruppamento per canonical key senza perdita di carte fisiche |
| 6 | Private deal | Completato | Ordine dei player preservato; semi trasformati globalmente |
| 7–8 | Cache e metriche | Completato | Query, hit, miss, collisioni hash e hit rate esposti |
| 9 | Audit CLI | Completato | `gto_cli isomorphism-audit <config.json>` stampa l'orbita completa |
| 10 | Confronto algoritmo | Completato per contratto | Azione globale e minimo di orbita conformi alla roadmap; nessun hand-index bucketing |

### Evidenza del gate

| Verifica | Risultato |
|---|---|
| Suite exhaustive Release | PASS, 351.930 asserzioni |
| Flop fisici | 7.140 su 7.140 |
| Coppie flop/permutazione | 171.360 |
| Orbite canoniche Short Deck | 573 |
| Golden globale | Board, range, private/dead/future e nodelock equivalenti condividono la chiave |
| Controesempio board-only | Chiave differente quando i blocker non seguono la permutazione |
| Chance monotone | 33 turn fisici aggregati in 15 figli canonici, somma molteplicità 33 |
| EV showdown | Hand value e winner mask identici per tutte le 24 permutazioni |
| Debug / ASan / UBSan | Suite F4 focalizzata PASS, nessuna diagnostica sanitizer |
| clang-format | PASS, `--dry-run --Werror` |
| clang-tidy | Modulo `gtosd::isomorphism` e CLI F4 senza warning |

### Benchmark Release

| Benchmark | Mediana |
|---|---:|
| Canonicalizzazione globale completa | 451.281 ns, 2.384 operazioni/s |
| Cache hit canonical key | 19.384 ns, 47.787 operazioni/s |

## Fase 5 — Solver laboratory

### Esito

Il gate locale F5 è completato. `gtosd::solver` implementa Vanilla CFR, CFR+,
Linear CFR, DCFR parametrico ed external-sampling MCCFR da laboratorio.
`gtosd::best_response` valuta strategie, calcola una BR exact infoset-aware e
produce NashConv anche per payoff general-sum.

I reference game sono Matching Pennies, Kuhn, Leduc e un river/rake toy che
usa board, combo ed evaluator Short Deck fisici. CFR+ è il primary del
laboratorio perché ha ottenuto NashConv inferiore a CFR e DCFR su Kuhn e
Leduc. DCFR resta il fallback exact parametrico; MCCFR non è autorizzato nel
percorso finale.

| Gate | Esito |
|---|---:|
| EV Matching/Kuhn entro `1e-6` | PASS |
| BR infoset-aware | PASS |
| NashConv general-sum con rake | PASS |
| Resume byte-equivalente | PASS |
| Selezione primaria riproducibile | PASS, CFR+ |

Build Release completa, Debug focalizzata, MSVC ASan, GCC UBSan,
clang-format, clang-tidy e install tree sono verdi. Il dettaglio, gli sweep e
le misure sono registrati in
[`PHASE_5_COMPLETION_REPORT.md`](PHASE_5_COMPLETION_REPORT.md).

## Fase 6 — Prototipi memoria exact

### Esito

Il gate locale F6 è completato. `gtosd::memory` confronta lazy in-RAM, street
decomposition e out-of-core sui benchmark versionati PF-F1/PF-F2/PF-F3.
I conteggi conservano tutti gli outcome fisici e tutte le combo private legali:
non vengono usati sampling o bucketing.

| Decisione | Esito |
|---|---|
| Primary PF-F1 | Lazy in-RAM, peak previsto 5,236 GiB |
| Fallback | Out-of-core, probe RSS PF-F1 16,918 MiB |
| Street decomposition | Corretta, non selezionata: +1,56% su PF-F1 con boundary lossless |
| Parità | Checkpoint byte-identico, delta EV/NashConv zero |
| PRE-FULL | Upper bound fisico pubblicato, 29,574–36,510 TiB |

Il dettaglio è in [`PHASE_6_COMPLETION_REPORT.md`](PHASE_6_COMPLETION_REPORT.md)
e nei tre report di prototipo.

## Fase 7 — HU postflop CLI production

### Esito

Il gate locale F7 è completato. `gtosd::postflop` integra il finite game
fisico Short Deck con CFR+ alternato, card removal, turn e river enumerati,
checkpoint atomico riprendibile, fallback out-of-core paginato, query per
combo fisica e certificazione tramite best response exact infoset-aware.

| Gate | Esito |
|---|---:|
| PF-F1 sotto 1% del pot | PASS, 0,741405% a 125 iterazioni |
| Turn e river enumerati | PASS, denominatori HU `29/28` |
| Checkpoint riprendibile | PASS, inline e out-of-core |
| Report con metriche | PASS, JSON e Markdown |
| Nessuna dichiarazione GTO senza BR | PASS, BR CO/BTN e NashConv pubblicati |

Build Release completa, Debug focalizzata, MSVC ASan, clang-format e
ricertificazione PF-F1 sono verdi. Il dettaglio è registrato in
[`PHASE_7_COMPLETION_REPORT.md`](PHASE_7_COMPLETION_REPORT.md).

### Ingresso completato

La Fase 8 è stata completata sopra le API query e checkpoint introdotte qui.

## Fase 8 — Storage della soluzione

### Esito

Il gate locale F8 è completato. `gtosd::storage` implementa il container
versionato `.gtsd` 1.0 con indice interno autenticato, compressione Zstandard
per chunk, cifratura XChaCha20-Poly1305 secretstream indipendente per chunk,
random access, verifica completa prima del commit e sostituzione atomica.
SQLite è usato esclusivamente come catalogo esterno `.gtsddb`; non sostituisce
l'indice binario interno necessario per aprire un singolo file.

| Gate | Esito |
|---|---:|
| Round-trip config/strategia/EV | PASS, lossless |
| Bit flip ciphertext | PASS, `AuthenticationFailed` |
| File troncato | PASS, `TruncatedFile` |
| Root senza full load | PASS, 676 B sul PF-F1 |
| Atomic save | PASS, vecchio file intatto su errore pre-commit |
| Migrazione | PASS, destinazione separata e sorgente preservata |
| Target 250 MB | PASS storage PF-F1 a una iterazione: 5.618.173 B |
| File fisico 250 MB simulato | PASS, 262.150.191 B aperti con 164 B |

Il benchmark PF-F1 storage usa la topologia completa da 66.756.096 azioni e
1.068.121.299 byte logici, ma una sola iterazione. Misura formato, compressione
e random access; non è una nuova certificazione di convergenza. Il risultato
F7 a 125 iterazioni resta la sola evidenza locale sotto l'1% del pot.

Debug completo, Release completa con F4 exhaustive verificata separatamente,
MSVC ASan focalizzato F8, clang-format, CLI end-to-end e install tree sono
verdi. Il dettaglio è registrato in
[`PHASE_8_COMPLETION_REPORT.md`](PHASE_8_COMPLETION_REPORT.md).

## Fase 9 — Prototipo e scelta GUI

### Esito

Il gate F9 è completato localmente. I prototipi Qt 6 Widgets e Dear ImGui
docking condividono fixture da 100.000 nodi, matrice Short Deck 9×9, apertura
lazy `.gtsd`, dieci workflow e tre scale DPI.

| Gate | Evidenza |
|---|---|
| Frame time | Qt raster 238,95 FPS; ImGui DX11 4.362,19 FPS; WARP 62,20 FPS, p95 tutti ≤16,666667 ms |
| E2E | 7/7 test F9; Qt e ImGui a 100/150/200% |
| Root lazy | 8 chunk totali, solo `CONFIG` caricato, strategy non caricata |
| Packaging | 21 artefatti verificati; smoke Qt/ImGui dall'install tree |
| Licenze | ImGui MIT; Qt dinamico con obblighi LGPLv3 oppure licenza commerciale |
| Regressioni | Release 19/19, focused MSVC ASan F9 1/1, format-check verde |

L'ADR [`ADR_0001_GUI_FRAMEWORK.md`](ADR_0001_GUI_FRAMEWORK.md) seleziona Qt 6
Widgets per la GUI prodotto. Dear ImGui resta disponibile per tooling
diagnostico. La misura usa quattro core fisici dell'i3-10100F a 3,6 GHz e
31,94 GiB: non è presentata come emulazione esatta del PC minimo 2 GHz/16 GB.
Il dettaglio è in
[`PHASE_9_COMPLETION_REPORT.md`](PHASE_9_COMPLETION_REPORT.md).

## Fase 10 — GUI HU postflop

### Esito

Il gate automatico locale F10 è completato. L'eseguibile prodotto `gto_gui`
integra configurazione visuale completa, board Short Deck visuale da tre a cinque
carte, pannelli di sizing separati CO/OOP e BTN/IP, editor range CO/BTN
paint-on-click/slider a basis point, Target dEV certificato a intervalli,
preflight e backend memoria automatici, solve CFR+ in worker separato, pausa,
annullamento e progresso per iterazione,
checkpoint/recovery `.gtsd`, save/open autenticato, albero azioni con frequenze,
reached range per nodo, equity exact, strategy matrix 9×9 e distribuzione del
valore mano.

| Gate | Evidenza |
|---|---|
| Crea→solve→salva→riapri→naviga→resume | PASS, E2E Qt sull'eseguibile reale e dall'install tree |
| Classe/combo | PASS, distribuzione azioni, equity exact, heatmap 9×9 reached-weighted e valore mano |
| Progress continuo | PASS, iteration counter indipendente dall'intervallo BR/NashConv |
| Nessun freeze solve | PASS, massimo gap heartbeat 12,0331 ms sulla fixture E2E installata |
| Range effettivi | PASS, reach CFR/BR, fingerprint, checkpoint e chunk `RANGES` condividono gli stessi 1.260 pesi |
| Regressioni | PASS, Release 22/22 (144,33 s); E2E prodotto aggiornato 30,75 s; precedenti gate Debug e MSVC ASan |
| Packaging | PASS, install tree pulito 75 file / 89.080.903 B e smoke E2E installato |

La misura E2E usa una fixture ridotta check-only da due iterazioni, poi ripresa
fino alla terza, e non dimostra convergenza. La certificazione solver resta
PF-F1 F7 a 0,741405%.
Il dettaglio, i limiti e i comandi di riproduzione sono in
[`PHASE_10_COMPLETION_REPORT.md`](PHASE_10_COMPLETION_REPORT.md).

## Prossimo ingresso

F10.4 resta un esperimento diagnostico, non node locking di prodotto. La
decisione 2026-09-04 rende prioritario il percorso astratto: l'aggregazione
reach-weighted nel `DenseLayout` e la CLI sono completate. Il prossimo ingresso
è una cache/manifest delle feature flop/turn con sweep di granularità e costo
separato; seguono persistenza `.gtsd`, GUI e frontier merge nativo. Il percorso
exact resta oracle con differenziale e best response; la parity GTO+ riprenderà
dopo questa qualifica. Il solving resta permanentemente CPU/RAM-only.

## Contratti poker già codificati

| Contratto | Valore |
|---|---:|
| Carte | 36 (`6..A`) |
| Combo fisiche | 630 |
| Classi preflop | 81 |
| Masse | pair `6`, suited `4`, offsuit `12` |
| Posizioni HU | CO primo, BTN secondo |
| Pot root | 3 ante |
| Call root CO | 1 ante |
| Precisione chip | 0,0001 ante |
| Ranking | colore sopra full, `A-6-7-8-9` valido |

Il dettaglio del gate F1 è registrato in
[`PHASE_1_COMPLETION_REPORT.md`](PHASE_1_COMPLETION_REPORT.md).
Il dettaglio del gate F5 è registrato in
[`PHASE_5_COMPLETION_REPORT.md`](PHASE_5_COMPLETION_REPORT.md).

Il risultato PF-F1 F7 è una soluzione HU postflop exact della configurazione
versionata e certificata tramite BR/NashConv. Non è una strategia preflop, non
copre configurazioni diverse da PF-F1 e non sostituisce i gate F9–F15.
