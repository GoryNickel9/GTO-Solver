# Stato implementazione roadmap HU Short Deck

> **V23 T/R/E validator 2026-09-15 — ENGINEERING PASS; QUALIFICA NON ESEGUITA.** Il runner
> `gtosd_hu_preflop_tre_validation` addestra su T, cerca due best response esatte sul gioco finito
> R e valuta policy e risposte congelate su E con confronti paired. E cresce per raddoppio; gli
> intervalli usano pesi fisici delle 81 classi CO e Bonferroni su cinque metriche e tutti i look.
> Lo smoke `T=K1/R=K1/E=K2→K4`, 16 iterazioni, completa due look in 58,38 s e passa lo schema v1.
> Il target di precisione volutamente impossibile non viene raggiunto. La copertura T→E K4 è
> `0,02094` per chiavi e `0,69393` per reach; i gain candidati sono negativi. Questi numeri
> verificano reporting e isolamento dei corpus, non la qualità della soluzione. Il certificato
> dichiara `physical_nashconv_certified=false`. Report:
> [`V23_TRE_VALIDATION_IMPLEMENTATION_2026-09-15.md`](research/preflop_r6_20260910/V23_TRE_VALIDATION_IMPLEMENTATION_2026-09-15.md).

> **V23 NashConv astratta 2026-09-15 — HU10 K8 TARGET RAGGIUNTO.** La chiave V23
> conserva classe preflop e history bucket `32/128/512`; l'audit V8 espone sei omissioni, quello
> V23 nessuna. La fixture HU10 compila un gioco finito con corpus chance stratificato e fingerprint
> condiviso fra Linear MCCFR e best response. Il candidato K=8/MC8 contiene 1.334.233 nodi e
> 490.050 information set. A 5.000.000 iterazioni la best response esatta misura
> `normalized_dev=0,00811735`, sotto il target `<0,01`; il gate viene attraversato a 4.500.000.
> Il resume è byte-identico al run continuo e l'hot path sparse conserva lo stesso SHA-256 del
> percorso denso. `physical_nashconv_certified=false`: il certificato vale soltanto per il corpus
> finito `fnv1a64:139ff63a167164ef`. Un secondo corpus K=8 raggiunge
> `normalized_dev=0,00945510` a 5.000.000 iterazioni e verifica l'early-stop. V17 resta baseline
> mentre proseguono i benchmark 20a/40a. La sensibilità K=2/4/8 è completata: tutti i giochi
> raggiungono `normalized_dev < 0,01`, rispettivamente a 1,5M/3M/5M iterazioni, ma il profile EV
> CO passa da `+0,08630` a `-0,14366` e `-0,29854` ante. L'esito è quindi
> `NOT_STABLE_FOR_PHYSICAL_VALUE_ESTIMATION`. Il nuovo lookup diretto nodo→information set
> conserva lo SHA-256 del checkpoint e riduce il training K=8/100k da 22,56 s a 12,53 s.
> `PolicyCompletion` introduce i contratti `reject_missing` e `uniform_unseen_v1`; il coverage
> audit misura chiavi e reach on-policy mancanti, anche per giocatore, e il certificato V23 v2 ne
> persiste i risultati. Il test cross-corpus K=1 misura `exact_key_coverage=0,149305`,
> `reach_weighted_coverage=0,765321` e 55.450 chiavi mancanti: il cambio di chance seed espone
> supporto non appreso invece di nasconderlo dietro un lookup implicito. Il runner T/R/E ora usa
> lo stesso contratto per validare fuori campione risposte congelate.
> Protocollo e report:
> [`V23_ABSTRACT_PERFECT_RECALL_NASHCONV_PROTOCOL_2026-09-14.md`](research/preflop_r6_20260910/V23_ABSTRACT_PERFECT_RECALL_NASHCONV_PROTOCOL_2026-09-14.md) e
> [`V23_HU10_ABSTRACT_NASHCONV_REPORT_2026-09-15.md`](research/preflop_r6_20260910/V23_HU10_ABSTRACT_NASHCONV_REPORT_2026-09-15.md).

> **V20 trace paired V18 AA 2026-09-14 — PASS; EV LOCALE INCONCLUSIVO.** La replica V18 seed 1
> coincide bit per bit per strategia, regret, vantaggi root, algoritmo e root EV. Con 10.000 deal
> `AA` per azione, Call precede Raise 6 di `0,2267a` sulla continuation media, con IC simultaneo
> `[-0,1426a; +0,5960a]`; sulla continuation corrente Raise 6 precede Call di `0,1052a`, ancora
> senza separazione. Il precedente vantaggio di Raise 6 su 188 campioni non si replica. La best
> response resta campionata e `normalized_nashconv=0` non certifica convergenza. Report:
> [`V20_V18_AA_PAIRED_TRACE_REPORT_2026-09-14.md`](research/preflop_r6_20260910/V20_V18_AA_PAIRED_TRACE_REPORT_2026-09-14.md).

> **V20 root decision trace 2026-09-14 — ENGINEERING PASS; ACTION EV INCONCLUSIVE.**
> La trace post-training forza le cinque azioni root sugli stessi deal fisici e valuta sia la
> continuation media sia quella corrente senza mutare policy, regret o strategy sum. Nel V17
> seed 1 da 2M, Call è l'unica azione con regret cumulativo positivo per `JTo`, `QJo` e `J9s`:
> questo produce il 100% Call nella policy corrente. I sei confronti paired contro la migliore
> alternativa osservata non superano però l'intervallo simultaneo al 95%. Build Release, test
> mirato con 4.537 asserzioni e suite HU 8/8 passano. Il contratto completo Monker è
> permanentemente indisponibile; WMAE e TV verso Monker restano diagnostiche. Report e roadmap:
> [`V20_ROOT_DECISION_TRACE_REPORT_2026-09-14.md`](research/preflop_r6_20260910/V20_ROOT_DECISION_TRACE_REPORT_2026-09-14.md) e
> [`V20_POST_TRACE_SOLVER_IMPROVEMENT_ROADMAP_2026-09-14.md`](research/preflop_r6_20260910/V20_POST_TRACE_SOLVER_IMPROVEMENT_ROADMAP_2026-09-14.md).

> **HU preflop R6 2026-09-14 — V18 E V19 RESPINTE; V17 BASELINE.** V18 migliora la WMAE media
> del `4,04%`, ma peggiora la TV fra seed del `18,92%`. V19 aggiunge telemetria action-conditioned
> opt-in e CRN globale sperimentale, con non-mutazione, fixture paired e riproducibilità 1/8 worker.
> La coppia V19 C2 da `2M + 2M` migliora la WMAE da `13,9472 pp` a `13,6347 pp`, ma peggiora la TV
> da `10,9453 pp` a `12,1140 pp` e gli errori Call/Fold raggiunti da 7 a 10. Il Gate C fallisce.
> I run corretti restano sotto 44,1 minuti e 2,81 GB private per seed; il vecchio `memory_failure`
> usava per errore perfect recall e non era matched. Nessun default, viewer o policy pubblicata cambia. Registri:
> [`V18_REJECTION_AND_NEXT_GATE_DECISION_2026-09-13.md`](research/preflop_r6_20260910/V18_REJECTION_AND_NEXT_GATE_DECISION_2026-09-13.md) e
> [`V19_FASE_A_AUDIT_2026-09-13.md`](research/preflop_r6_20260910/V19_FASE_A_AUDIT_2026-09-13.md),
> [`V19_FASE_B_CRN_AUDIT_2026-09-14.md`](research/preflop_r6_20260910/V19_FASE_B_CRN_AUDIT_2026-09-14.md) e
> [`V19_COMPLETION_REPORT_2026-09-14.md`](research/preflop_r6_20260910/V19_COMPLETION_REPORT_2026-09-14.md).

> **R9-C decomposizione exact 2026-09-09 — IN CORSO.** Blueprint preflop denso,
> reach sulle 630 combo fisiche, 573 flop canonici/5.157 task, resource estimator,
> boundary CFV ProductionDcfr e scheduler River task-local sono implementati. Il piano
> Flop-only è respinto dal picco modellato di 32.528.500.840 B; il nesting River
> porta il lower bound attivo a 637.398.110 B. Lo smoke River ricompone l'EV con
> errore `6,49e-15`, ma una iterazione non dimostra convergenza. Valutazione del
> profilo, BR exact e NashConv globale sono collegate; manca il provider che
> produca continuation postflop convergenti per l'intero benchmark. Nessun
> percorso di prodotto è stato modificato.
>
> Blueprint, piano, boundary, checkpoint scheduler e ledger di copertura sono
> persistibili con checksum e scrittura atomica. Il certificatore consuma in
> streaming le 10.314 boundary canoniche: il ledger usa `170.181 B` e il payload
> transazionale usa `365.706 B`, invece di materializzare `130.699.008 B`. Lo
> schema 1.3 rifiuta boundary di checkpoint diversi. Il fingerprint locale del
> task è distinto dall'identità globale del checkpoint: River v5, terminali e
> assemblatore Flop 1.1 e boundary Flop 1.1 propagano la seconda. Il test completo copre
> 10.314/10.314 boundary e tutta la massa postflop, poi restituisce
> `GLOBAL_BEST_RESPONSE_MISSING` senza BR e
> `GLOBAL_BEST_RESPONSE_NOT_EXACT` con una risposta campionata. L'oracle sparse fisico
> lossless è stato misurato e respinto: 8.996.964 infoset/1.295.562.816 B minimi
> a 100k, EV `+0,7437a`, errore strategico medio `21,627 pp`. Release `42/42` e
> ASan mirato `3/3` passano. Le 64.260 frontiere fisiche sono `9 × 7.140`, non
> flop distinti; senza isomorfismo i boundary occuperebbero `1.628.605.440 B`.
>
> Il catalogo River contiene 5.157 span task-local, 366.488.496 stati pubblici
> e 732.976.992 boundary per lato, divise in 141.687 batch task-aligned da
> massimo 64 MiB. Ogni batch mantiene unita la coppia dei resolver. L'accumulatore
> numerico task-local e la sua persistenza atomica sono implementati. La materializzazione da 9,29 TB è
> vietata. Il probe Flop file-backed ha rappresentato 31,43 GB logici con
> 608,01 MB Peak RSS, ma una traversata dura 1.285,69 s e solve più certificazione
> 9.719,15 s; il percorso monolitico resta respinto per tempo e convergenza.
> Anche lo sweep River exact separato è respinto: cinque processi Release
> proiettano fra 783.076 e 930.075 s per una sola iterazione sui 366.488.496
> stati pubblici, con mediana 834.311 s. L'exact resta oracle; il primo solve
> deve riusare informazione fra root e misurare l'errore introdotto. Il target
> HU Release passa con 9.090 asserzioni. I terminali BR Flop/Turn 1.1
> dichiarano la continuation usata dal provider avversario e il dispatcher
> rifiuta checkpoint misti prima della ricorsione. La suite Release completa
> passa 42/42 in 787,22 s; il target HU impiega 506,16 s. Lo stesso target passa sotto
> AddressSanitizer con 9.090 asserzioni in 4.793,91 s, senza diagnostiche. La BR globale lega il
> fingerprint composito delle boundary alla stessa continuation globale, pur
> consentendo fingerprint locali diversi fra task. Evidenza
> calcolata su continuation diverse fallisce prima del gate NashConv.
> La BR exact è ora persistibile e riprendibile anche dentro il singolo leaf
> River e ai livelli task, entry accumulator ed entry evaluation. Il sink salva
> il candidato prima del commit; dopo un'interruzione il provider riparte dal
> primo root non completato. Roundtrip, checksum, incompatibilità di schema e
> resume senza replay passano nel target HU Release: 9.099 asserzioni in
> 462,24 s, stderr vuoto. La stessa revisione passa l'intera suite Release
> 42/42 in 752,22 s; nel run integrale il target HU impiega 484,88 s. Questo
> incremento protegge il lavoro già calcolato, ma non modifica la proiezione di
> 783.076–930.075 s né prova convergenza.
>
> La riduzione River ora solleva ogni runout canonico nella sua orbita fisica e
> permuta insieme le combo private nelle coordinate del Flop rappresentante.
> Una boundary River contiene 528 righe, non le sole 465 combo vive sul board
> rappresentante. L'oracolo unitario verifica per tutte le combo la massa esatta
> `molteplicità Flop × 31 × 30 × 406`. I checkpoint accumulator/aggregate v1-v4
> sono rifiutati; i nuovi payload usano lo schema v5. Le history Flop/Turn
> conservano inoltre le azioni complete, sono replayabili e alimentano un
> propagatore di reach che applica blocker e probabilità per il solo actor. La
> riduzione River include ora anche la reach delle azioni proprie del player di
> cui si accumula la CFV; ometterla sovrastimava i rami mixed-strategy.
> Il manifesto terminale contiene 3.792 history: 612 Flop, 3.180 Turn, 2.388
> fold e 1.404 all-in runout. Il valutatore exact e l'assemblatore task-local
> sono implementati: sommano continuation River, fold e showdown, rifiutano
> ordinali mancanti/fuori ordine e provano 812 runout per combo compatibile
> prima di emettere le due boundary Flop.
> Il postflop espone ora anche best-response CFV exact per combo, ricomposte
> contro la certificazione entro `1e-9`. Il bridge solleva ora anche tali valori
> sull'orbita fisica del River e li accumula in un canale task-local distinto.
> Boundary, accumulatore e aggregato BR non possono essere mescolati con quelli
> di profilo; l'assemblatore Flop average-policy rifiuta l'aggregato BR. Restano
> da implementare la scelta ottima alle decisioni Flop/Turn e la ricorsione BR
> preflop. I test Release mirati passano `2/2` in `99,29 s` e il test HU conta
> 8.971 asserzioni. La suite Release completa passa `42/42` in `318,07 s`.
> Il target HU passa sotto AddressSanitizer in `1.115,26 s` senza diagnostiche.
> Nessun default production è cambiato.
> La vista successiva per la BR upper-street partiziona ogni task per Turn
> canonico e genera span River per history senza duplicare il catalogo. Il test
> Release copre ogni root una volta e passa con 8.975 asserzioni; la suite
> completa passa `42/42` in `333,03 s`. ASan deve ancora essere rieseguito dopo
> questo incremento.

> **R9-C v2 showdown distribution 2026-09-07 — `REJECTED` 12/12.** La firma
> River v3 conserva `HandValue` e aggiunge masse avversarie per categoria e
> loss/tie/win, con quantum 5% e cinque holdout congelati. Profile value e byte
> model passano 12/12, ma speedup 0/12, riduzione minima 5/12, delta NashConv
> 2/12 e best response 6/12. Il native è `7,6x`–`123,9x` più lento; NashConv
> fisica massima `2,6527%`. Suite Release `42/42` PASS; ASan mirato `2/2` PASS.
> V3 resta benchmark-only e non entra nel preflop o nel prodotto. R9-C passa
> alla specifica di decomposizione con boundary CFV.
>
> **Oracle River CFV/RSS 2026-09-07 — bucket v1 `REJECTED` 7/7.** Exact e
> profilo bucket sollevato sono ora confrontati nello stesso gioco fisico per
> strategia root, CFV di strategia/azione e NashConv certificata. La riduzione
> nodi `8,34x`–`892,33x` non compensa NashConv `0,5506%`–`5,3387%`, CFV media
> fino al `7,41%` del pot ed errore CFV d'azione fino all'`88,76%`. Peak RSS del
> workflow ridotto: `107.356.160 B`, distinto dal byte model. Nessun default di
> prodotto cambia; R9-C v2 non riusa `made_hand_value_v1`. Test mirati `2/2` e
> suite Release completa `40/40` PASS in `245,99 s`; smoke AddressSanitizer
> PASS in `22,27 s` senza diagnostiche.
>
> **River lossless generale 2026-09-06 — `BLOCKED`.** La partizione equa
> pesata riduce il full range da 465 a 45 classi per player e da 188.790 a
> 2.005 coppie, ma sui 12 range realistici v2 produce solo singleton e rapporto
> `1,00x`. Il gate `1,25x` fallisce 12/12: nessun kernel v3 viene implementato,
> exact resta product e il ramo River exact compresso è chiuso.
>
> **River exact-blocker v2 2026-09-06 — `REJECTED_FEASIBILITY`.** La firma
> lossless unisce solo combo con uguale `HandValue` e identica compatibilità
> contro ogni combo avversaria attiva. Sette regressioni v1 e cinque holdout
> producono una classe per combo: riduzione nodi `1,00x` e speedup
> exact/native `0,006709x`–`0,017259x`. Memoria passa 12/12 e qualità 11/12,
> ma nessuna fixture supera tutti i gate. Exact resta il prodotto; nessuna
> estensione a Turn o preflop. Release 35/35 in 260,92 s; kernel e preflight
> passano anche sotto ASan.

> **River bucket-native 2026-09-06 — qualifica v1 `REJECTED`.** Un kernel isolato
> raggruppa i deal di un river completo per coppia di `HandValue`, attraversa
> un solo albero pubblico con ProductionDcfr e riporta la strategia alle combo
> per BR/NashConv originale. Full range: 188.790 deal → 611 coppie e
> 1.699.110 → 5.499 nodi per passata, byte model nativo 55.972 B. Sui due casi
> pesati D/V la mediana di cinque solve scende da 7,2651 a 3,1269 ms e da
> 8,0537 a 3,0401 ms; NashConv originale sale però a 0,00377002 e 0,000511609.
> Il corpus indipendente v1, congelato prima del solve, qualifica 0/7 fixture:
> bucket NashConv e delta falliscono 7/7; lo speedup operativo passa 3/7, mentre
> byte model e profile-value delta passano 7/7. Test Release 313 asserzioni e
> percorso ASan completo passano senza diagnostiche. Il percorso è river-only,
> single-thread e non collegato a CLI/GUI/`.gtsd`: exact resta il prodotto
> predefinito; questa rappresentazione non prosegue su Turn o preflop. Il CTest
> Release corrente passa 35/35 in 260,92 s.

> **ProductionDcfr product optimization 2026-09-06 — `BLOCKED_WITH_EVIDENCE`.** R0/R1/R2/R2-S/R3/R4/R6 sono chiuse. Nuovi
> solve API/CLI e worker GUI, resume e benchmark passano da un resolver
> production condiviso: algoritmo `ProductionDcfr`, stato scaled uint16,
> averaging delay 0, certificazione 20, strict target e profondità parallela 7.
> Checkpoint nativo v2 e metrics archive v4 persistono identità e parametri;
> checkpoint CFR+ e backend incompatibili sono rifiutati senza fallback. Il
> corpus H v2 è deterministico, disgiunto e sigillato. I timer product sono
> disponibili nel CLI e nel benchmark con range reali; l'audit dinamico copre
> identità, semi, range, stack, sizing e soglia di residency. La suite GUI
> Release GUI passa `38/38` in 203,93 s; il preset Release corrente, con i
> nuovi gate R2-S/R3/R6 e River, passa `35/35` in 260,92 s nella verifica finale del
> 2026-09-06. B0 ha
> cinque run controllati per AHK, TH e TST. TH/TST falliscono il gate tempo e la
> RAM GTO+ resta non comparabile: R2 è una baseline chiusa, non una parity.
> S0 definisce mapping bucket e boundary CFV; S1 implementa identità-oracle,
> strategy tying pesato, persistenza e BR/NashConv nel gioco originale sul
> motore finito. Le policy postflop 1.0 includono `exact_identity`, integrata
> con mapping implicito a zero byte ed equivalenza esatta di fingerprint, stato
> e checkpoint, e `made_hand_value`, mapping coarse deterministico misurato su
> D/V. Il traversal coarse ProductionDcfr, il checkpoint 1.0 e il resume
> byte-identico sono implementati; l'oracolo scalare controlla 1.024 update. S2
> valida public-state chiusi, deriva reach/CFV, costruisce il
> gadget opt-out zero-sum e verifica lo splice del solo resolving player con BR
> globale su Kuhn e Short Deck toy. Persistenza con checksum/sostituzione atomica
> e rifiuto della corruzione passano. Un bridge river exact e bounded porta il
> postflop nel resolver; S3 comprime 12 infoset in 4 e certifica la combinazione
> nel gioco originale. A0 non è promossa: è più lenta su D/V e V perde qualità.
> R3 attribuisce quasi tutto il wall a mapping e training. R4-A1/B1 falliscono i
> kill gate e sono state rimosse. R6 non trova finestre RBP su D/V e non dispone
> di una derivazione lazy compatibile con la traiettoria ProductionDcfr signed.
> H resta sigillato; nessun candidato raggiunge R7. Il prodotto resta exact.
> Registro:
> `PRODUCTION_DCFR_PRODUCT_OPTIMIZATION_EXECUTION_2026-09-05.md`.

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

> **Production DCFR integration 2026-09-01 — checkpoint di qualificazione.** Il contratto
> comune AHK/TH/TST e' ora `production_dcfr`: exact alternating signed DCFR
> `1.5/0/3`, reset one-based `1,2,5,17,65`, regret clock post-65 ritardato di
> una iterazione, delay zero, `ScaledUint16RegretStrategy`, CPU-only e massimo
> otto thread. Cinque processi final-head auditabili (`r2-r6`) passano `15/15`
> solve con dEV `<1%`, correctness/layout/exact outcomes; il Peak RSS TST
> registrato resta diagnostico e non è sottoposto a un cap normativo.
> Iterazioni deterministiche AHK/TH/TST `80/80/160`; mediane solver
> `0,758705/19,948228/184,095930 s`; p95
> `0,790918/24,192260/197,865030 s`. Il CTest Release disponibile a quel
> checkpoint passava `28/28` (`105,46 s`, 2026-09-04).
> La vecchia authority `1.5/0/2` e' ora il comparator Release storico. Il gate
> GTO+ resta non superato: la qualification storica fallisce i tempi TH/TST e
> la memoria non è valutabile finché la metrica non è equivalente. Report:
> [`DCFR_EPOCH_RESET_GAMMA3_FEASIBILITY_2026-09-01.md`](DCFR_EPOCH_RESET_GAMMA3_FEASIBILITY_2026-09-01.md).

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
| GTO+ parity gate | **NON SUPERATO; TH/TST time blocker in scope** | Production final-head `production_dcfr` exact signed `1.5/0/3`, reset `1,2,5,17,65`: cinque processi auditabili, `15/15` solve PASS. AHK `0,951423% @80`, root `19,118978`, mediana/p95 `0,758705/0,790918 s`; TH `0,807956% @80`, root `8,226793`, `19,948228/24,192260 s`; TST `0,904505% @160`, root `8,495661`, `184,095930/197,865030 s`, stato `1.472.605.376 B`, peak massimo `1.969.860.608 B`. TH/TST superano le rispettive mediane limite del `1,661%/42,722%`. Correctness/layout/exact outcomes PASS; la memoria resta `NOT_EVALUATED_COMPARABILITY_UNRESOLVED`. Full CTest corrente 35/35 PASS | La nuova production e' qualificata e la five-process non e' piu' congelata. F11+ resta congelata finche' TH e TST non superano insieme il gate tempo GTO+; nessuna parità memoria è dichiarata |
| Backend di calcolo | **CPU/RAM only** | Contratto permanente: solver, CFR, best response e certificazione non usano GPU o acceleratori di calcolo | Conservare il confine anche nelle ottimizzazioni future; la GPU può soltanto renderizzare la GUI |
| F11+ | **Congelata dal parity gate** | — | Nessuna fase successiva prima del superamento documentato in `GTO_PLUS_PARITY_JOURNEY.md` |

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
| 11 | Semantic versioning file/API | Completato | API corrente `0.10.0` generata da CMake; major/minor espliciti per formati public tree, solution e checkpoint; incompatibilità major testata |
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

F10.4 è completata come esperimento diagnostico e non è node locking di
prodotto. Il prossimo lavoro autorizzato è ridurre il tempo dei tre benchmark
intervenendo soltanto sul core generale e mantenendo dEV, root EV e RAM. Il
profilo corrente indica che micro-ottimizzazioni isolate non coprono il gap.
Prima si deve abilitare il fast path fisico generale quando il fallback non usa
infoset isomorfi: action base dirette, layout `PlayerIndexed`, regret immediati
e nessun workspace differito. Seguono DAG lossless per range asimmetrici con
reach/molteplicità player-local e isomorfismo street-local. Ogni candidato deve
passare il differenziale `1e-11` prima dei benchmark. Il solving resta
permanentemente CPU/RAM-only e F11+ resta congelata.

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

## Ricerca River blocker-aware 2026-09-06

Il kernel sperimentale supporta ora due identità: `made_hand_value_v1` ed
`exact_blocker_signature_v2`. V2 unisce soltanto combo con uguale valore finale
e uguale compatibilità contro ogni combo attiva avversaria. Test mirati
verificano equivalenza fisica, separazione dei blocker, lift, BR/NashConv e il
caso full-range.

La qualifica v2 è chiusa con `REJECTED_FEASIBILITY`, 0/12. Su sette regressioni
congelate e cinque holdout la partizione produce una classe per combo, non
riduce i nodi ed è 58–149 volte più lenta dell'exact. CTest Release passa 35/35
in 260,92 s; il target River e il preflight v2 passano anche sotto ASan.

Nessuna superficie prodotto cambia: CLI, GUI, `.gtsd`, Turn e preflop usano
ancora il percorso ProductionDcfr exact. Il kernel v2 resta isolato come prova
del limite della firma lossless con etichette avversarie fisse.

## Fattibilità River lossless generale 2026-09-06

`analyze_fixed_river_equitable_partition` calcola la partizione equa pesata
coarsest del grafo di compatibilità River entro un cap esplicito di deal. Il
test controllato prova una fusione lossless; il full range trova 45 classi per
player e 2.005 coppie contro 188.790 deal. Il corpus v2 mostra però zero
compressione in 12/12 fixture asimmetriche.

Il runner e il report sono diagnostici. Non esistono dispatch CLI/GUI, nuova
identità `.gtsd`, stato di regret o checkpoint v3. L'analisi chiude il ramo
River exact senza modificare ProductionDcfr product.

Validazione finale: CTest Release `36/36` PASS in `267,95 s`; target postflop e
preflight equo ASan `2/2` PASS in `198,94 s`.

## HU preflop CO 40a 2026-09-06

È congelata la fixture esterna unica `GTP-HU-PREFLOP-CO40-001`. Il preflight
verifica 81 classi, 630 combo, masse `6/4/12`, cinque azioni root e percentuali
arrotondate. La strategia marginale fisica è 40,7789% all-in, 2,40789% raise
6a, 4,71957% raise 10a, 7,56675% call e 44,5269% fold.

Il primo solver HU preflop campionato è implementato. La configurazione
dichiarativa fissa raise-to 10,5a/14,5a, struttura speculare dopo limp,
33/66/120/all-in postflop fino alla terminazione naturale e rake zero. Il
raise-to 14,5a è ammesso tramite un override locale; il motore standard continua
a rifiutarlo come non-full raise non all-in.

Il preflight pubblica anche il lower bound della chance: 353.430 deal privati,
1.753.012.800 frontiere private+flop e almeno 73.042.200 rappresentanti anche
nel caso ideale di orbite globali da 24 elementi. Questo indirizza il prossimo
prototipo verso decomposizione/on-demand, non verso la materializzazione del
full game.

Il tree/resource gate misura 58 nodi preflop e 30.324 nodi nello scheletro
postflop. Il massimo è quattro raise per street e deriva dallo stack, non da un
cap a quattro. Il limite interno 63 non viene raggiunto.

Il candidato v1 conserva le 81 classi exact al preflop e usa bucket
postflop categoria/equity MC8 con perfect recall astratta. External-sampling
DCFR `1.5/0/3` visita deal fisici e risolve gli showdown con l'evaluator exact.
Il solve 20k termina in 13,810 s; il run 100k termina in 75,141 s. Il payload
minimo è 136 B per infoset: 196.420.720 B per il blueprint 20k e 897.882.472 B
per quello 100k, esclusi hash table, allocator e Peak RSS.

Il comparatore conclude `REJECTED`: a 100k la MAE action/class è 21,763 pp,
la TV media 54,407 pp, l'errore root massimo 27,896 pp e il delta EV 0,8958a.
La risposta campionata non certifica NashConv. Il gate ora controlla
`nashconv_certified`, quindi il lower bound zero non può produrre un falso
PASS.

Il 7 settembre 2026 la rake del benchmark è stata confermata a `0%`. Il
comparatore dispone ora di un gate rake esplicito: il run 5%/cap 3a resta una
sensibilità diagnostica e non è un candidato valido, indipendentemente dalla
vicinanza del solo EV.

Validazione corrente: build Release warning-clean, albero/resource gate PASS,
solve smoke PASS, due solve riproducibili completati, CTest Release `39/39`
PASS in `251,60 s` e AddressSanitizer mirato `3/3` PASS in `3,57 s`.

## Calibrazione NashConv whole-game V21 — 2026-09-14

Il percorso `MCCFR -> strategia media -> best response esatta -> NashConv` è validato su una
fixture Short Deck enumerabile di quattro street. Linear MCCFR riduce la NashConv esatta da
`0,0014765486` a `0,0001654188` fra 20.000 e 60.000 iterazioni; la best response domina il
profilo e il resume è byte-coerente.

La policy postflop può ora essere fissata in uno snapshot immutabile associato ai fingerprint del
tree e del checkpoint. Il nuovo evaluator River vettoriale conserva i valori dell'oracolo scalare
entro `1e-10` e, sul probe V17 seed 1, riduce il costo di profilo più BR da `89,8287 s` a
`0,8999 s` per root.

La strategia CO40 non ha ancora NashConv globale certificata. Il catalogo completo contiene
322.199.856 sottogiochi River canonici; una valutazione indipendente root-by-root richiederebbe
circa 9,19 anni seriali per proiezione. Il prossimo componente richiesto è un reducer batch che
riusi policy query e transizioni fra board e shape, quindi aggreghi la best response per
information set prima della massimizzazione.

Protocollo e risultati: [V21 whole-game NashConv](research/preflop_r6_20260910/V21_WHOLE_GAME_NASHCONV_IMPLEMENTATION_REPORT_2026-09-14.md).

## Certificatore NashConv generico V22 — 2026-09-14

Il probe accetta configurazione, candidato e policy senza dipendere dallo stack: una futura
soluzione HU 50a richiede un nuovo run, non nuovo codice. Il contratto primario resta la best
response fisica lifted; la policy media V8 è congelata e la fallback uniforme dichiarata fa parte
della strategia valutata.

Il census V17 trova 1.566.290 infoset addestrati in 10.060 contesti decisionali. Batching delle
query, chiavi private River preparate e inizializzazione condivisa riducono profilo più BR per il
root campione da `0,8421401 s` a `0,2410329 s`, con EV invariati. La proiezione root-by-root scende
da `8,60` a `2,46 anni` seriali, ancora oltre la stop rule di sette giorni; il manifest riporta
quindi `INFEASIBLE_EXACT_ROOT_BY_ROOT`, non una NashConv globale.

Ogni nuovo solve serializza inoltre una stima separata con stato
`ESTIMATED_LOWER_BOUND_ONLY`, errore standard e intervallo al 95%. Il campo resta
`certified: false`: due risposte MCCFR fattibili possono dimostrare sfruttabilità, ma non fornire
un upper bound sull'errore. Build Release e suite HU passano con `15.956` asserzioni.

Protocollo e risultati: [V22 NashConv generico](research/preflop_r6_20260910/V22_GENERIC_NASHCONV_CERTIFIER_PHASE1_REPORT_2026-09-14.md).

Il follow-up board-batched condivide combo vive, showdown e chiavi private fra tutte le 633 River
shape dello stesso board. L'enumerazione strided riduce la materializzazione del campione da circa
`493 s` a `0,814670 s`, ma la traversata profile+BR richiede ancora `198,725931 s`, pari a
`0,313943019 s` per sottogioco. La proiezione più rappresentativa sale a `3,2053 anni` seriali o
`146,34 giorni` ideali su otto worker. Il gate è `INFEASIBLE_EXACT_BOARD_BATCHED`: il prossimo
salto richiesto è il riuso dei prefissi pubblici e del reach fra history, non altra cache del board.

Dettagli: [V22 reducer board-batched](research/preflop_r6_20260910/V22_CROSS_ROOT_BOARD_BATCHED_REDUCER_REPORT_2026-09-14.md).

## Riavvio del programma preflop — 2026-09-15

Il programma preflop external sampling (R0–R6, V1–V23) è chiuso senza candidato qualificato. I
suoi documenti sono stati rimossi dal working tree e restano al tag
`preflop-legacy-es-2026-09-15`; l'indice è in
[PREFLOP_LEGACY_INDEX.md](research/PREFLOP_LEGACY_INDEX.md). I link delle sezioni precedenti di
questo file verso `research/preflop_r*` si risolvono a quel tag.

Il nuovo programma è definito da tre documenti: l'[analisi](research/HU_PREFLOP_ALGORITHM_AND_ABSTRACTION_ANALYSIS_2026-09-15.md)
(diagnosi e architettura: astrazione precalcolata con feature esatte, CFR vettoriale con
campionamento del board, best response esatta nel gioco fisico), il
[registro delle decisioni](research/PREFLOP_ARCHITECTURE_DECISION_LOG.md) e la
[roadmap P0–P10](research/PREFLOP_VECTOR_CFR_CODER_ROADMAP_2026-09-15.md). Lo stato di
avanzamento è nel [diario dell'agent](research/preflop_vector_cfr/PROGRESS_LOG.md). Il codice
legacy in `libs/preflop/` resta in build come oracolo fino allo stadio 1 dell'archiviazione (D20).
