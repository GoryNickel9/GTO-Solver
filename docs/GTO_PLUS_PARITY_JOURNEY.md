# GTO+ parity journey — suite postflop

Aggiornato: 2026-08-31
Benchmark ID: `GTP-AHKHQH-003`, `GTP-TH7D6S-101`, `GTP-TSTC9D-101`
Stato del gate: **BLOCCANTE — NON SUPERATO**

> **Constraint governance gate 2026-08-31.** L'optimization research è in
> pausa al governance gate. RAM-only e certification-only sono insufficienti;
> la minimum-change frontier è un hardware resource change con speedup
> effettivo traversal+BR `>=1,615339x`, oppure RAM raw
> `>=3.432.437.888 B` più exact final BR `<=8,604297 s`. Entrambe sono soltanto
> `POTENTIALLY SUFFICIENT — NEEDS NEW STUDY`; nessun contratto è stato scelto
> o implementato. Vedere
> [`CONSTRAINT_GOVERNANCE_GATE_2026-08-31.md`](CONSTRAINT_GOVERNANCE_GATE_2026-08-31.md).

> **Real-node replay e producer lower bound 2026-08-31.**
> **JOINT STATE/PRODUCER LOWER-BOUND BLOCKER.** Il replay production è fedele
> su `288` decision node reali (`162.317` action entry): regret/strategy code,
> scale e parent value autorevoli sono byte/bit-identici. Lo sweep mostra che
> regret sotto float32 sviluppa outlier di policy nel replay multi-step; i
> punti float32 abbastanza stabili richiedono `6–8 B/action` e superano il cap
> desktop TST. Lo streaming della sola materializzazione producer misura circa
> `1,041x`, insufficiente; l'ideal producer proietta `168,036868 s`, mentre
> solo il joint ideal perfetto raggiunge `89,624835 s`. Nessun candidate e
> nessun target-driven sono stati autorizzati. Production e baseline restano
> invariati; il prossimo passo è un gate esplicito sui vincoli, non un altro
> codec. Evidenza:
> [`REAL_NODE_REPLAY_AND_PRODUCER_LOWER_BOUND_LOOP_2026-08-31.md`](REAL_NODE_REPLAY_AND_PRODUCER_LOWER_BOUND_LOOP_2026-08-31.md).

> **New production state representation loop 2026-08-31.**
> **REPRESENTATION SPACE EXHAUSTED** per tile-local float/power-of-two,
> per-hand, hybrid, adaptive e direct compact studiati. Il fused shadow finale
> misura `0,874x–0,977x` per le scale locali; il direct bfloat16 arriva a
> `1,465x` nello shadow ma, integrato sperimentalmente, AHK@20 regredisce da
> `8,24777%` a `84,2584%` dEV e da `0,169024` a `0,341450 s`. Il path è stato
> rimosso. Signed-float24/bfloat16 è numericamente migliore ma solo `1,396x`.
> Production, checkpoint e baseline restano invariati; il nuovo blocker è la
> combinazione producer whole-vector + costo local-scale/direct packing.
> Evidenza: [`NEW_PRODUCTION_STATE_REPRESENTATION_LOOP_2026-08-31.md`](NEW_PRODUCTION_STATE_REPRESENTATION_LOOP_2026-08-31.md).

> **Exact state representation loop 2026-08-30.** **EXACT REPRESENTATION
> BLOCKER PROVEN.** Il codec node-global byte-identico richiede almeno 32 bit
> per regret e 31 per strategy prima della scala finale; il saving ideale sui
> due `float` è 1,5625%. Gli shadow exact misurano al massimo `1,024x` (recompute
> `0,896x`), sotto `1,3x`; TST@202 cambia ancora l'83,018% dei regret code.
> Nessuna integrazione production o rebaseline. Il prossimo studio deve essere
> separato e può valutare un nuovo formato non byte-identico. Evidenza:
> [`EXACT_STATE_REPRESENTATION_FEASIBILITY_LOOP_2026-08-30.md`](EXACT_STATE_REPRESENTATION_FEASIBILITY_LOOP_2026-08-30.md).

> **Architectural traversal/dataflow loop 2026-08-30 — stato operativo
> corrente.** Il precedente cumulative loop è **EXHAUSTED con blocker
> architetturale**; non è corretto continuare con micro-ottimizzazioni dello
> stesso layout. La telemetria generale trova su TST 335.984 river work unit e
> batchability byte strict width-4 `0,589% / 53,839% / 98,831%` per
> local/frontier/global. Il wavefront shadow su subtree reali è byte-exact ma i
> mediani width-4 sono solo `1,116x/1,327x`, sotto il gate `1,50x`; il traversal
> compilato ha ceiling end-to-end `2,94%`; la continuation exact disponibile è
> chiusa dal global node scale e dall'ordine di update. Nessuna integrazione
> production o rebaseline è stata autorizzata. Vedere
> [`ARCHITECTURAL_TRAVERSAL_FEASIBILITY_LOOP_2026-08-30.md`](ARCHITECTURAL_TRAVERSAL_FEASIBILITY_LOOP_2026-08-30.md).

> **Checkpoint final-head omogeneo 2026-08-30 — prevale su tutti i checkpoint
> sottostanti per lo stato corrente.** I tre benchmark target-driven sono stati
> rieseguiti in tre processi separati, uno alla volta, sul binario
> Release da `6508bddd039d44ecb941acded4b5b16d39f4f7e8`, contratto comune exact
> alternating DCFR signed `alpha=1.5`, `beta=0`, `gamma=2`, delay zero e otto
> thread massimi.
>
> | Fixture | Iter / dEV | Root / riferimento | Payoff-sum | Solver / limite | Peak RSS | State | Esito |
> |---|---:|---:|---:|---:|---:|---:|---|
> | AHKHQH | 80 / 0,655665% | 19,108984 / 19,15 | 0 | 0,670928 / 1,900000 s | 166.510.592 B | 5.300.664 B | correctness/dEV/root/state/time PASS |
> | TH7D6S | 80 / 0,806385% | 8,221632 / 8,22198 | -4,44e-16 | 17,645055 / 19,622222 s | 798.371.840 B | 334.452.416 B | correctness/dEV/root/state/time PASS |
> | TSTC9D | 202 / 0,991863% | 8,494698 / 8,50165 | 3,22e-15 | 208,111772 / 128,988889 s | 1.969.922.048 B | 1.472.605.376 B | correctness/dEV/root/state PASS; time FAIL |
>
> TST traversal è `174,926380 s` e certification `32,780800 s`. Il gap finale
> è `79,122883 s`, rapporto `1,613409x`, `+61,3409%`. Il precedente
> `252,534707 s` era pre-ottimizzazione certification e non rappresenta più il
> final HEAD. Tutte le fixture rientrano nel cap desktop 2 GB e nello
> `solver_state_bytes` fixture; il distinto `memory_gate` peak-RSS-vs-GTO+
> resta FAIL per AHK e TH, PASS per TST. Non usare quindi `RAM PASS` senza
> nominare il gate. Full CTest 21/21 PASS; five-process ancora congelata.
> Evidenza dettagliata:
> [`OBJECTIVE_DRIVEN_GATE_CLOSURE_2026-08-30.md`](OBJECTIVE_DRIVEN_GATE_CLOSURE_2026-08-30.md).

> **Root-analysis fix 2026-08-30.** Il precedente Root FAIL AHKHQH era un bug
> di dispatch: con browser fisico preparato, il root non usava il layout
> production canonico autorevole. Dopo il fix, AHK target @80 misura
> `19,108987 / 19,15` (delta `-0,041013`, PASS); TH e TST restano PASS e CTest
> e' 20/20. Vedere
> [`AHKHQH_PREPARED_ROOT_ANALYSIS_FIX_2026-08-30.md`](AHKHQH_PREPARED_ROOT_ANALYSIS_FIX_2026-08-30.md).
> Il parity gate prestazionale complessivo resta non superato; e' superato il
> solo prerequisito root necessario per poter riprendere l'audit RBP.

> **Baseline production comune 2026-08-29/30 (prevale per i nuovi confronti).**
> Le tre fixture usano DCFR exact signed `alpha=1.5`, `beta=0`, `gamma=2`,
> average immediato e massimo otto thread. Il profilo TST `1.9/0/3` e i percorsi
> DCFR+ sotto restano storici/superseded. Risultati e golden sono in
> [`PRODUCTION_DCFR_NORMALIZATION_2026-08-29.md`](PRODUCTION_DCFR_NORMALIZATION_2026-08-29.md).
> Il Root FAIL AHK e il conseguente blocco RBP pubblicati nella prima versione
> del report erano diagnostici pre-fix e sono superseded dalla correzione sopra.

> **Aggiornamento riferimento GTO+ 2026-08-28 (prevale sui riferimenti temporali
> TSTC9D storici sotto).** La nuova curva GTO+ contiene 24 punti fra 5,00 s e
> 116,09 s. Il primo punto strettamente sotto `Target dEV < 1%` è `0,91%`
> (`0,146 ante`) a `116,09 s`; il punto precedente è `1,13%` (`0,181 ante`) a
> `111,40 s`. Il riferimento grezzo TSTC9D è quindi `116,09 s` e il limite con
> margine concordato del 90% è `128,988889 s`. Il vecchio `108,54 s` resta
> registrato nella fixture come misura superata, ma non è più il tempo di
> completamento certificato. La traccia integrale è versionata in
> `benchmarks/fixtures/gto_plus_tstc9d_101.json`.

> **Checkpoint operativo TSTC9D 2026-08-29 (prevale sul checkpoint TSTC9D
> 2026-08-14 sotto).** La fixture dispone ora degli importi monetari GTO+
> osservati ed è marcata `metadata_complete=true`. Il percorso resta exact,
> CPU-only, senza sampling/bucketing/GPU, con otto thread, stato signed
> `ScaledUint16RegretStrategy`, DAG pubblico canonico e aritmetica del
> traversal in `float` come previsto dal codec performance; best response e
> gate esterni restano separati.
>
> Il miglior checkpoint lungo controllato della sessione usa DCFR
> `alpha=1,9`, `beta=0`, `gamma=3`, 160 iterazioni e una sola certificazione
> finale (`out/tstc9d_tail_river_alpha1_9_iter160.json`): traversal
> **130,3729854 s**, certificazione **5,4393643 s**, elapsed solver
> **136,2380205 s**, dEV **1,0784262679%**, root EV **8,4906667401 ante**
> (delta `-0,0109832599`), `solver_state_bytes` **1.472.605.376 B**, peak RSS
> **1.968.742.400 B** e 245.146.564 nodi visitati. Root e memoria passano;
> dEV e tempo grezzo GTO+ `116,09 s` falliscono. È un singolo run, non una
> mediana promossa.
>
> Profilo aggregato della traversata: produzione valori showdown `27,4%`,
> value/update `22,0%`, accumulo rank/card `19,3%`, regret matching `13,6%`,
> costruzione prefix `9,1%`, chance/board `8,3%`, reach `0,3%`. La famiglia
> showdown complessiva vale circa il `55,8%`; il secondo limite è lo
> scheduling, perché l'utilizzo CPU medio del run lungo è `86,1%` e le
> strategie più dense rendono il costo per iterazione crescente. Il valore
> breve migliore (`~0,733 s/iter` a 20 iterazioni) non rappresenta quindi il
> costo medio del solve lungo (`~0,815 s/iter`).
>
> Ottimizzazioni lossless mantenute: specializzazione compile-time del
> terminale fold/showdown accoppiato; slot flop compatibili con la chance
> precalcolati; `/favor:INTEL64` in Release senza `/fp:fast`; split river
> soltanto nella coda quando la queue è vuota e ci sono worker inattivi, con
> accumulo finale nello stesso ordine canonico. Quest'ultimo ha ridotto il
> traversal a 160 iterazioni da `132,257 s` a `130,373 s` (`-1,42%`) senza
> cambiare root EV, dEV o nodi nel differenziale a 20 iterazioni.
>
> Esperimenti misurati e respinti: pinning thread; normalizzazione one-pass
> dell'average; prefix showdown sparso; scratch chance riusabile; nested
> river indiscriminato; PGO; stride showdown board-local 32; accumulo
> cell-major; split parallelo dei tre rami root (più lento e non identico nel
> DAG condiviso); DCFR+ (`174,729 s`, dEV `1,541924%` a 135); HS-DCFR 3.0
> (`124,764 s`, dEV `2,265369%` a 135); DCFR beta 1 (`109,452 s` ma dEV
> `4,075340%`); gamma 5 (`122,184 s`, dEV `1,263294%`) e gamma 2
> (`125,727 s`, dEV `1,532120%`) a 135. Nessun risultato respinto è presente
> nel percorso finale, salvo infrastruttura dormiente non attivata.
>
> **Stato:** goal di chiusura sospeso su richiesta dell'utente. F11+ resta
> congelata. Il benchmark finale Release
> `out/tstc9d_session_final_alpha1_9_gamma3_iter170.json` converge a 170
> iterazioni: elapsed **153,1763512 s**, traversal **147,0631715 s** (`96,01%`),
> certificazione **5,5051254 s** (`3,59%`), inizializzazione **0,5738847 s**
> (`0,37%`), regret application **0,0001314 s** e finalizzazione
> **0,0015453 s**. Il wall con preparazione tree è **179,7229091 s**, quindi
> la preparazione esterna al timer solver pesa **26,5465579 s**. dEV
> **0,9857595195%**, root **8,4925400350** (delta `-0,0091099650`), peak RSS
> **1.968.537.600 B** e stato **1.472.605.376 B** passano; il time gate
> `116,09 s` e il limite 90% `128,988889 s` falliscono. Cinque processi
> indipendenti restano rinviati finché il singolo run non supera tutti i gate.
> Verifica finale Release: `gtosd_gto_plus_reference_tests` PASS, 24
> asserzioni; differenziale seriale/parallelo con massimo delta regret e
> strategy pari a zero; `git diff --check` senza errori (soli avvisi EOL del
> worktree). La suite CTest completa non è stata rieseguita.

> **Policy prestazionale generale 2026-08-29.** Le ottimizzazioni devono
> appartenere al motore condiviso e restare applicabili a board, range,
> sizings e profondità differenti; non sono ammessi percorsi privilegiati per
> TSTC9D o per qualsiasi altro nome di fixture. Un miglioramento su TSTC9D è
> quindi soltanto un risultato locale finché non viene verificato anche sui
> carichi AHKHQH e TH7D6S. Il prossimo gate è una profilazione omogenea a
> iterazioni fisse sui tre benchmark, con lo stesso binario Release, CPU-only
> e numero di thread dichiarato. Devono essere separati costo per iterazione,
> numero di iterazioni fino alla soglia e overhead di certificazione; il run
> target-driven ufficiale e la certificazione a cinque processi vengono dopo.
> La policy completa, inclusi i criteri di promozione e non-regressione, è in
> `docs/specifications/PERFORMANCE.md`.

> **Checkpoint operativo 2026-08-14 (prevale su tutti i checkpoint sotto).**
> Il core supporta uno stato packed generale di 3 byte/action: regret CFR+
> unsigned float13 `E8M5` e strategy sum unsigned float11 `E5M6`, con traversal
> e payoff in float64. Non esistono branch per benchmark. I tre run Release
> passano dEV strettamente sotto 1%, root EV e RAM. Sono run singoli di chiusura
> RAM/correttezza; la certificazione temporale a cinque processi è rinviata.
>
> | Benchmark | Report | Iter / dEV | Root EV GTOSD / GTO+ | Tempo / limite 90% | `solver_state_bytes` / GTO+ | Gate |
> |---|---|---:|---:|---:|---:|---|
> | `GTP-AHKHQH-101` | `out/ram_final_ahkhqh.json` | 100 / 0,982960% | 19,123322 / 19,15 | 4,970917 / 1,900000 s | 2.503.908 / 8.000.000 B | dEV/root/RAM PASS; tempo FAIL |
> | `GTP-TH7D6S-101` | `out/compact_no_average_simd3_th7d6s.json` | 82 / 0,986976% | 8,220073 / 8,22198 | 37,810434 / 19,622222 s | 249.955.776 / 399.000.000 B | dEV/root/RAM PASS; tempo FAIL |
> | `GTP-TSTC9D-101` | `out/compact_no_average_simd3_tstc9d.json` | 200 / 0,983565% | 8,498226 / 8,50165 | 690,307523 / 120,600000 s | 1.747.903.656 / 2.000.000.000 B | dEV/root/RAM PASS; tempo FAIL |
>
> I riferimenti temporali GTO+ grezzi sono rispettivamente 1,71 s, 17,66 s e
> 108,54 s; i limiti mostrati concedono il margine concordato del 90%. I limiti
> RAM sono invece i byte GTO+ diretti, non maggiorati. Peak RSS resta separato
> (`208.031.744 B`, `477.130.752 B`, `3.166.359.552 B`) e non sostituisce lo
> stato solver. Tutti i time gate restano FAIL: la pressione della macchina non
> viene usata per giustificarli e il residuo è trattato come problema del core.
>
> **Worktree sperimentale successivo, non promosso.** Sono state avviate
> modifiche generali per il DAG con range asimmetrici, ma la build è stata
> interrotta e non esistono ancora differenziale, riferimento o benchmark
> validi. Il checkpoint della tabella resta quindi l'unica evidenza corrente.
> L'audit ha inoltre rilevato un intervento precedente e più circoscritto: nel
> fallback fisico TST le action base sono già dirette, ma la selezione del path
> resta legata al numero di automorfismi. Il primo candidato da verificare è
> `!uses_isomorphic_infosets || automorphisms.size() <= 1`, così da riusare
> `PlayerIndexed`, regret immediati e nessun buffer differito senza cambiare il
> gioco. Solo dopo viene il DAG player-local e poi l'isomorfismo street-local.
> Tutto il solving, inclusi BR e certificazione, è CPU/RAM-only; la GPU è
> esclusa permanentemente dal percorso di calcolo.
>
> Il 2026-08-14, dopo il ritiro dell'ultimo esperimento asimmetrico, la build
> Release di `gto_cli` e `gtosd_gto_plus_reference_tests` è PASS. Il riferimento
> eseguibile è PASS con 24 asserzioni; fallback fisico asimmetrico e root lock
> esterno sono PASS. Non è stata rieseguita la suite CTest completa in questa
> chiusura: il precedente 16/16 del 2026-08-13 resta evidenza storica distinta.
>
> **Checkpoint strutturale precedente 2026-08-13.**
> Due schermate dell'albero GTO+ hanno chiuso il differenziale strutturale
> iniziale di `TSTC9D`: GTO+ arrotonda `5,28` a `5,3`, quindi mostra `Raise 14`;
> dopo la prima bet non aggiunge il push perché vale circa `280,83%` del pot
> dopo il call, mentre dopo `Raise 14` aggiunge `Raise 80` perché il push vale
> `150%`, conservando anche il raise regolare al 75% (`Raise 47`). Il contratto
> generale è quindi `(stack-call)/(pot+call) < soglia`, modalità `Add`, con
> size `[33,75]` al primo raise e `[75]` ai raise successivi.
>
> Il core, il tree config e il parser benchmark sono stati riallineati in modo
> generale: calendario di size per `raise_count`, `Add`/`Go` distinti e soglia
> stretta sul pot dopo il call. Non esistono eccezioni per ID benchmark. Le
> fixture precedenti senza calendario riusano la lista uniforme a ogni
> profondità; AHKHQH e TH7D6S hanno all-in automatico disabilitato.
>
> Il vecchio run TST (`8,576672`, `114,614952 s`, `752.202.000 B`) è ora
> **storico e non certificabile**, perché usava `[33,75]` a tutte le profondità,
> modalità `Go` e la base `stack/current_pot`. Layout, fingerprint, RAM, tempo,
> dEV e root EV TST devono essere rigenerati. Il rounding è ora una policy
> generale, piecewise, validata e serializzata nel core: TST seleziona target
> sotto 10 ante al decimo e target successivi all'ante intera, senza branch per
> ID benchmark. Il catalogo risultante è `Bet 5,3`, `Raise 14`, poi
> `Call 8,7 / Raise 47 / Raise 80`, con fingerprint
> `fnv1a64:c51f0921903117bf`,
> 2.791.872 nodi fisici, 231.129.064 infoset, 582.634.552 azioni e
> 2.913.172.760 B di stato solver. Il solver core non riceve un massimo di
> iterazioni. Il core ora evita le certificazioni periodiche prima che inizi
> l'averaging, perché la strategia media non contiene ancora campioni. Nell'A/B
> Release le certificazioni passano da `20..180` a `140,160,180`: a 160 il dEV
> è 1,346488% e il solver continua; a 180 certifica 0,958803% e si ferma. Root
> EV 8,496602 contro 8,50165 PASS; solver time 471,901781 s (-10,37% dal
> baseline 526,492253 s), RAM allora ancora FAIL; è superato dal checkpoint
> packed sopra.
> `metadata_complete=false` e F11+ resta congelata.

> **Checkpoint storico 2026-08-11 (non più operativo per TSTC9D).**
> I tre benchmark superano già il target dEV e il gate `solver_state_bytes`;
> `AHKHQH` e `TH7D6S` falliscono ancora il tempo; `TSTC9D` passa il tempo ma
> fallisce il root EV.
> Le misure sotto sono singoli processi Release, non la mediana finale di cinque.
> La sessione di ottimizzazione è stata chiusa su richiesta dell'utente dopo
> l'aggiornamento documentale: non sono stati avviati ulteriori full o CTest.
> `TH7D6S` e `TSTC9D` sono stati ricertificati in
> `out/build/windows-release-current`; `AHKHQH` è l'ultimo candidato conservato
> e deve ancora essere rieseguito nello stesso build tree pulito.
>
> | Benchmark | dEV | Root EV GTOSD / GTO+ | Tempo GTOSD / limite | Stato solver | Gate |
> |---|---:|---:|---:|---:|---|
> | `AHKHQH` | 0,981301% | 19,103999 / 19,15 | 2,237744 / 1,900000 s | 6.677.088 B | dEV, root EV, RAM PASS; tempo FAIL |
> | `TH7D6S` | 0,987345% | 8,220498 / 8,22198 | 27,896048 / 19,622222 s | 416.592.960 B | dEV, root EV, RAM PASS; tempo FAIL |
> | `TSTC9D` | 0,963255% | 8,576672 / 8,50165 | 114,614952 / 120,600000 s | 752.202.000 B | dEV, RAM e tempo PASS; root EV FAIL |
>
> Il delta root `TSTC9D` è `+0,075022 ante`, quindi eccede la tolleranza
> assoluta `0,05` di `0,025022 ante`. La fixture è marcata
> `metadata_complete=false`: manca ancora la conferma degli importi effettivi
> prodotti da GTO+ per i sizing 33%/75% al root e nelle sequenze bet/raise.
> Il root CO esterno **8,50165** è invece confermato dall'utente.
>
> Il runner pubblica ora `initial_street_action_catalog`, che separa la label
> storica dall'importo monetario esatto. La diagnostica TST mostra che la label
> root `bet_5` rappresenta in realtà **5,28 ante** (33% di 16), mentre
> `bet_12` rappresenta 12 ante. Le size successive conservano a loro volta i
> pot frazionari (per esempio 21,28 e 35,3248 ante). Questo non dimostra ancora
> la causa del delta EV: occorre confrontare il catalogo con gli importi
> effettivamente usati da GTO+, che potrebbero essere arrotondati diversamente.
> Le label non sono quindi una fonte sufficiente per certificare la fixture.
>
> I nuovi checkpoint usano overlap lossless dei sottoalberi al root e fan-out
> chance annidato, ristretto ai soli rami root accodati. Sul build tree pulito
> corrente TH chiude a 27,896048 s a iterazione 79 e TST a 114,614952 s a
> iterazione 140; TST conserva quindi il PASS tempo. Restano misure singole,
> non mediane finali di cinque processi.
>
> **Handoff di chiusura.** Il prossimo ingresso ad alta priorità è una misura
> AHK isolata sul tree Release pulito, seguita da profiling strutturale del
> doppio calcolo delle strategie per iterazione. La campagna finale da cinque
> processi resta rinviata finché i singoli run AHK/TH non passano il tempo e la
> fixture TST non dispone degli importi monetari GTO+ mancanti. F11+ resta
> congelata.

> **Checkpoint storico TH7D6S (2026-08-08; superato dal checkpoint 2026-08-14
> sopra):** `GTP-TH7D6S-101` usa **416.592.960 B** di stato solver contro il
> limite **443.333.333 B** (gate RAM 90% PASS). Il miglior full credibile è
> `out/loop_policy_half_avx_full.json`: **86,1059943 s**, traversal
> **79,7679045 s**, certificazione **5,7897606 s**, iterazione 140, dEV
> **0,9260452878479758%**, root EV **8,219182737421068 ante** e gate
> correctness/layout PASS. Il limite tempo **19,622222 s** resta FAIL di circa
> 4,39 volte. Le ottimizzazioni successive zero-sum e fold/showdown sono
> mantenute perché positive in A/B controllati, ma i loro full sono avvenuti
> sotto carico e non sostituiscono questo best. Vedi
> `speed_optimization_journey.md` §§8.24-8.28. La mediana finale di cinque
> processi non è ancora stata eseguita e F11+ resta congelata.

> Checkpoint prestazionale parallelo: il benchmark grande `GTP-TH7D6S-101` ha ora
> memoria solver 416.592.960 B (gate 90% PASS) ma un run completo singolo da
> 89,2088938 s contro il limite 19,622222 s (gate tempo FAIL). Correttezza root, layout,
> dEV 0,9260452878479734% e precisione dichiarata passano. Dettagli e limiti della
> misura singola sono registrati in `speed_optimization_journey.md` §8.16; questo
> checkpoint non sblocca F11+ e non sostituisce il gate canonico `GTP-AHKHQH-003`.
> Le schedule sperimentali HS-DCFR+(15/30) sono state misurate e rifiutate:
> 65,0266614 s e 95,0473936 s rispettivamente. Il codice produttivo resta CFR+
> canonico; risultati e trasformazione dei pesi sono registrati nel §8.17.

## 1. Scopo

Questo documento è il registro canonico del percorso verso la parità con GTO+
per il test `AhKhQh`. Deve essere aggiornato nello stesso cambiamento che
introduce qualsiasi implementazione, benchmark o decisione relativa a questo
gate.

Non si avviano F11 o fasi successive finché GTOSD non raggiunge almeno il 90%
dei benchmark obbligatori, senza modificare il gioco e senza perdere
correttezza matematica.

## 2. Fixture immutabile

| Campo | Valore |
|---|---|
| Gioco | Heads-up Short Deck postflop |
| Mazzo | 36 carte, `6..A` |
| Flop | `Ah Kh Qh` |
| CO/OOP | `AA-QQ,AKs-AQs,KQs,AKo-AQo,KQo` |
| BTN/IP | `AA-QQ,AKs-AQs,KQs,AKo-AQo,KQo` |
| Combo fisiche dopo i blocker | 36 per giocatore |
| Pot iniziale | 40 ante |
| Stack effettivo | 100 ante |
| Bet size | 50% pot per entrambi, flop/turn/river |
| Raise | 50% pot, massimo uno per street |
| All-in automatico | Regola naturale: all-in solo quando la bet size supera lo stack rimanente (correzione 2026-08-05; la soglia `150%` era un'inferenza mai confermata ed è stata rimossa) |
| Smoothing due bet finali GTO+ | Disabilitato, confermato dall'utente |
| Rake | 0% |
| Precisione GTOSD | stato `float32`, calcolo e certificazione `float64` |
| Card abstraction | Nessuna |
| Sampling | Nessuno |
| Isomorfismo | Solo lossless |

Le fixture `GTP-AHKHQH-001` e `002` sono ritirate: la prima non riproduceva le
size, la seconda applicava la soglia all-in a `(stack-call)/(pot+call)` e
generava `All-in 100` dove GTO+ mostra `Raise 60`. Qualsiasi ulteriore variazione
genera un benchmark ID differente e non può sostituire questo gate.

## 3. Riferimento GTO+

I valori seguenti sono misure esterne fornite dall'utente e sono accettati come
target del progetto:

| Metrica | GTO+ |
|---|---:|
| Tempo fino alla convergenza | **1,71 s** |
| Memory needed for solving | **8 MB** |
| Target dEV del run sorgente | **1% del pot iniziale** |
| EV flop CO root | **19,15 ante** |
| EV flop BTN dopo check CO | **21,65 ante** |
| EV flop BTN dopo bet 20 CO | **17,51 ante** |
| Frequenze CO root | **check 80,3%; bet 20 19,7%** |
| Frequenze BTN dopo bet 20 | **fold 37,4%; call 62,6%; raise 60 0,0%** |

Il riferimento operativo da `1,71 s` è stato successivamente esportato con
maggiore precisione: dEV `0,390 ante` (`0,98%`), CO root EV `19,1588`, BTN dopo
check `21,6597`, BTN dopo bet `17,5107`. Un secondo run GTO+ a dEV `0,078 ante`
(`0,19%`, `4,20 s`) riporta rispettivamente `19,1581 / 21,6682 / 17,1176`.
Questi export sono diagnostici e non sostituiscono il target prestazionale da
`1,71 s`.

Il tentativo con vero Target `0,10%` non è sceso sotto `0,11%` in circa
`245 s`; il relativo tempo di convergenza è quindi soltanto limitato inferiormente
a `>245 s`, non misurato.

Confermato dall'utente:

- stesso hardware per GTO+ e GTOSD;
- Target dEV `1%` nel run da `1,71 s`;
- timer dal click su `Run Solver`, con albero già preparato, fino alla soluzione
  completa consultabile;
- unità della memoria GTO+: **MB decimali** (8 MB = 8.000.000 byte), quindi il
  confronto in byte con lo stato solver GTOSD è diretto.

Resta da registrare, senza invalidare il target operativo:

- turn e river dell'action tree GTO+ (il livello flop è registrato: 4 nodi con
  action set, frequenze ed EV combo-per-combo in
  `docs/specifications/gtoplus_specs.md`).

Finché questo campo non è allineato, una misura GTOSD può essere
diagnostica, ma non può essere dichiarata comparazione scientifica definitiva.

## 4. Definizione del gate 90%

Per una metrica in cui un valore inferiore è migliore:

```text
score = riferimento_GTO+ / misura_GTOSD × 100
```

Ne derivano le soglie:

| Gate obbligatorio | Formula | Soglia GTOSD |
|---|---|---:|
| Velocità di convergenza | `1,71 / tempo_GTOSD ≥ 0,90` | **≤ 1,900000 s** |
| Memoria solver | `8 / memoria_GTOSD ≥ 0,90` | **≤ 8,888889 MB** |

Entrambi devono essere superati nello stesso commit. Non è ammessa una media
che compensi il fallimento di una metrica con l'altra.

### Gate di correttezza non negoziabili

- stessa fixture e stesse azioni legali;
- range fisici e card removal identici;
- nessun sampling, bucketing o astrazione lossy;
- strategia media normalizzata;
- nessun NaN o infinito;
- parità del percorso canonico con quello fisico entro `1e-11`;
- suite Release completa verde;
- metrica di convergenza registrata e confrontabile;
- EV del gioco al root entro la tolleranza versionata, nella stessa convenzione
  e a convergenza dichiarata;
- EV condizionali e frequenze dei nodi osservati confrontati come gate soltanto
  quando le reach private/posteriori combo-per-combo sono uguali; altrimenti
  restano diagnostici;
- almeno cinque processi indipendenti; decisione sulla mediana, con tutti i run
  pubblicati.

Un'ottimizzazione che supera il benchmark ma fallisce uno di questi controlli
viene rifiutata.

## 5. Baseline GTOSD corrente

Implementazione: F10.4 completata come root-lock diagnostico; benchmark v1
congelato e benchmark v2 parametrico disponibili.

| Metrica | GTOSD corrente | Score | Stato |
|---|---:|---:|---|
| Tempo `Run Solver` a dEV GTO+ ≤1%, 5 processi | mediana documentata 3,128 s; p95 3,271 s | sotto la soglia `<=1,900000 s` | **FAIL** |
| Convergenza deterministica | iterazione 80; dEV 0,695544%; NashConv/Pot 1,268598% | — | **PASS** |
| Regret + average strategy `float32` | 6.677.088 byte | entro `<=8.888.889 byte` | **PASS** |
| EV flop CO root, probe accurato | 19,163591 vs 19,1581; delta +0,005491 | — | **PASS** |
| BTN dopo check, posteriori diversi | 22,418551 vs 21,6682; delta +0,750351 | — | Diagnostico |
| BTN dopo bet 20, posteriori diversi | 15,996175 vs 17,1176; delta -1,121425 | — | Diagnostico |
| CO root bet 20 | 24,6348% vs 18,0194% | — | Diagnostico: causa posteriori diversi |
| BTN raise 60 dopo bet 20 | GTOSD 3,0438% vs GTO+ 0,0% nel run accurato | — | Diagnostico |
| Nodi pubblici fisici | 165.774 | — | Informativo |
| Nodi pubblici canonici | 46.065 | — | Informativo |
| Infoset canonici | 385.980 | — | Informativo |
| Action entry canoniche | 834.636 | — | Informativo |
| Delta regret transient | pubblicato separatamente in ogni run | — | Diagnostico, separato dallo stato solver |
| Peak RSS | pubblicato in ogni run | — | Diagnostico, non equivalente agli 8 MB |

Il benchmark pubblica separatamente stato solver, transient workspace e peak
RSS. La modalità `float32` non cambia payoff o enumerazione: tutte le operazioni
del traversal e la best response restano `float64`.

### Perché la misura corrente non supera ancora la parità

Il benchmark automatico usa la semantica GTO+ corretta: massimo guadagno di
deviazione unilaterale diviso per il pot iniziale, target `1%`. Il timer continuo
parte dopo la preparazione del layout e include inizializzazione, CFR+,
averaging, certificazione exact BR finale e finalizzazione. I cinque run sono
processi distinti e pubblicano ogni tempo.

Il gate memoria passa e il gate velocità corrente fallisce. Il probe GTOSD a
1.000 iterazioni e dEV `0,011334%` produce CO root EV `19,163591`, contro
`19,1581` del run GTO+ a dEV `0,078 ante`: delta `+0,005491 ante`, pari allo
`0,0137%` del pot. Il valore del gioco è quindi allineato entro `0,0055 ante` e
non mostra un errore di normalizzazione o payoff.

I due EV BTN non rappresentano lo stesso subgame privato: dopo un'azione root,
il peso CO è proporzionale a `range_iniziale × strategia_root_combo × blocker`.
GTO+ betta il `18,0194%`, GTOSD il `24,6348%`, con allocazioni combo-per-combo
diverse. I posteriori CO dopo bet/check sono quindi diversi e possono generare
EV condizionali distanti anche quando il root EV coincide. Lo stesso GTO+
sposta il bet root di `1,7084` punti percentuali tra dEV `0,98%` e `0,19%`, ma
il root EV cambia soltanto di `0,0007 ante`; questo indica un equilibrio molto
piatto o selezioni numeriche differenti tra mix quasi indifferenti.

Lo smoothing resta escluso e non è stato usato bucketing. Prima di attribuire
il residuo al tree builder occorre l'esperimento controllato della fase F10.4,
che rende identica la strategia root GTO+ e lascia libere le continuation.

## 6. Protocollo di misura obbligatorio

Ogni candidato al gate deve registrare:

1. commit e worktree pulito;
2. build Release e compiler/flags;
3. CPU, RAM, sistema operativo e thread;
4. fingerprint completa della fixture;
5. algoritmo e precisione;
6. soglia di convergenza e valore finale osservato;
7. esclusione del layout già preparato e inclusione di inizializzazione,
   solving, certificazione e finalizzazione;
8. cinque processi indipendenti, senza riuso involontario di cache o
   checkpoint;
9. tempi individuali, mediana e p95;
10. memoria persistente, transient buffer e peak RSS separati;
11. suite di correttezza e differenziale physical/canonical.

Il timer primario “tempo alla convergenza” parte prima dell'inizializzazione
necessaria al solve e termina quando la soglia allineata è certificata. Eventuali
misure kernel-only restano secondarie.

## 7. Backlog ordinato del journey

| Priorità | Attività | Ipotesi misurabile | Stato |
|---:|---|---|---|
| 1 | Allineare e automatizzare il criterio di convergenza GTO+/GTOSD | Rende misurabile il gate tempo end-to-end | **Completato** |
| 2 | Profilare separatamente layout, CFR+, averaging e BR | Ha identificato traversal e certificazioni ripetute | **Completato** |
| 3 | Allineare il timer al tree già preparato | Replica il click `Run Solver` GTO+ | **Completato** |
| 4 | Compattare il traversal alle combo attive | Riduce 630 slot a 36 senza abstraction | **Completato** |
| 5 | Storage `float32` con calcolo `float64` | Dimezza i buffer senza abstraction | **Completato, differenziale richiesto nel gate finale** |
| 6 | Parallelismo deterministico per action subtree | Usa fino a 6 thread con delta separati e join | **Completato** |
| 7 | Worker persistenti e riuso layout GUI | Elimina creazione thread e rebuild ripetuti | **Completato** |
| 8 | Dimostrare la parità del root EV e ricomporre i rami BTN | Separa valore del gioco da EV condizionali dipendenti dal posteriore | **Completato: delta root +0,005491 ante** |
| 9 | **F10.4 — Root lock diagnostico GTO+ combo-per-combo** | Con posteriori root identici, misura se i due delta BTN scendono entro ±0,5 ante | **Completato: PASS diagnostico** |
| 10 | Classificare il residuo dopo root lock | Il residuo downstream principale è CO dopo check-bet, delta +0,0925 ante | **Completato come diagnosi; nessun unlock** |
| 11 | Acquisire e confrontare l'action tree GTO+ downstream | Necessario solo dopo una decisione esplicita sul differenziale residuo | **Condizionato** |
| 12 | Profilare il percorso standard, senza lock | Ridurre la mediana documentata da 3,128 s a ≤1,900000 s | **Prossimo lavoro; gate bloccante** |

L'ordine può cambiare solo sulla base di profiling registrato qui.

### F10.4 — Controlled-posterior con root lock GTO+

**Obiettivo.** Stabilire causalmente se la distanza degli EV BTN deriva dalla
diversa strategia CO root o da una divergenza downstream. Non è node locking di
prodotto e non modifica il comportamento predefinito del solver.

**Input immutabile del test.** Le 36 righe combo-per-combo `Bet 20 / Check`
esportate da GTO+ v1.6.9 nel run a dEV `0,078 ante`, insieme ai riferimenti
`CO root 19,1581`, `BTN dopo check 21,6682` e `BTN dopo bet 17,1176`. Il source
dEV deve essere conservato nel report: il riferimento non è un equilibrio
esatto e non autorizza aspettative bit-per-bit.

**Comportamento richiesto.** Al solo nodo CO root, ogni combo usa esattamente la
probabilità esterna corrispondente; CFR+ non aggiorna i regret del root e
accumula/esporta la strategia locked. Tutti i nodi successivi restano liberi.
Le reach dopo bet/check devono essere costruite normalmente dal motore,
includendo compatibilità tra le due hole card, board blocker e pesi iniziali.

**Isolamento e sicurezza semantica.** Il percorso deve essere esplicitamente
denominato `diagnostic_external_root_lock`; deve rifiutare combo duplicate,
bloccate o mancanti, azioni inesistenti, probabilità non finite/negative e
somme diverse da uno. Resume, certificazione, salvataggio o browser non devono
poter perdere il vincolo in modo silenzioso. Il report deve distinguere la
convergenza del gioco vincolato dall'exploitability del gioco originale.

**Validazione obbligatoria.** Il test permanente verifica:

1. copertura esatta delle 36 combo e riproduzione delle probabilità locked;
2. nessun cambiamento a conteggi, azioni legali e fingerprint della fixture
   standard non vincolata;
3. ricomposizione zero-sum del root e posteriori Bet/Check generati dal lock;
4. solve downstream ad accuratezza almeno pari al probe GTOSD già usato;
5. EV BTN locked, delta rispetto a `21,6682 / 17,1176` e frequenze downstream;
6. differenziale physical/canonical oppure una limitazione esplicita se le
   asimmetrie numeriche tra semi dell'export GTO+ impediscono la condivisione
   lossless degli infoset locked;
7. suite Release senza regressioni del percorso standard.

**Decisione al termine.** Se entrambi gli EV BTN sono entro `±0,5 ante`, F10.4
classifica il mismatch storico come principalmente dovuto alla selezione del
mix root/posteriore; la soglia non certifica parità bit-per-bit. Se almeno uno
resta fuori, il prossimo lavoro è il differenziale downstream, partendo dal
primo nodo con reach già uguali. In entrambi i casi il lock resta test-only.

**Definition of Done.** Implementazione, fixture esterna versionata, test degli
input invalidi, run accurato, report JSON riproducibile, aggiornamento di questo
journey e del benchmark, build Release e suite completa PASS. Al 2026-08-05 la
fase è **COMPLETATA** — vedi la voce `2026-08-05 — Regola all-in corretta e
F10.4 completata` nel registro: con la regola all-in naturale e il root locked
i delta BTN sono `+0,0348` / `+0,0366` (entro `±0,05 ante`).

### 2026-08-05 — Regola all-in corretta e F10.4 completata

- **Correzione della regola all-in**: la soglia `Go all-in if remaining stack <
  150% current pot` era un'inferenza non confermata (interpretazione A dello
  sweep 2026-08-02). L'utente dichiara che GTO+ non ha tale regola: all-in solo
  quando la bet size supera lo stack rimanente (regola naturale). Applicata a
  tutte le strade (flop/turn/river), stesse size 50%.
- Impatto sull'albero (con range): `165.774` nodi fisici, `46.065` canonici,
  `385.980` infosets canoniche, `834.636` action entry, stato `6.677.088` byte
  (più vicino agli 8 MB dichiarati da GTO+ dei `4.214.976` precedenti).
  Fingerprint di gioco: `fnv1a64:9e42ca23963f718b`.
- Re-baseline completo: fixture `003` (v1, contratto aggiornato), `101`, `103`,
  `104` (v2) ri-parametrizzate con la regola naturale e nuovi `expected_layout`;
  `make_gto_plus_parity_config` e il config del diagnostic allineati.
- Numeri dopo la correzione (80 iterazioni, senza lock): dEV `0,674155%`,
  EV CO root `19,1129` (gate PASS), BTN dopo check `22,0833` (delta `+0,433`),
  BTN dopo bet `17,3600` (delta `-0,150` — prima era `-0,74`): la regola all-in
  da sola spiega gran parte del mismatch BTN storico.
- **F10.4 completata**: implementato il root lock diagnostico
  `diagnostic_external_root_lock` (36 righe combo-per-combo Bet 20/Check del
  run operativo GTO+ 0,98%, provenienza in `gtoplus_specs.md`, source dEV
  conservato nel report). Validazione permanente nel reference test:
  1. copertura esatta delle 36 combo e riproduzione delle probabilità locked
     (delta massimo `2,2e-16`);
  2. fingerprint del gioco invariato rispetto alla fixture non vincolata;
  3. ricomposizione zero-sum del root e posteriori Bet/Check dal lock;
  4. convergenza del gioco vincolato (dEV `0,203%` a 200 iterazioni, probe
     non vincolato `0,674%`).
- Risultato con root locked (200 iterazioni, gioco vincolato convergente):
  CO root `19,1232` (delta `-0,0356`), BTN dopo check `21,6945` (delta
  `+0,0348`), BTN dopo bet `17,5473` (delta `+0,0366`), CO dopo check-bet
  `11,9884` (delta `+0,0925`); frequenze BTN call `64,6%` vs `62,6%` GTO+,
  fold `34,8%` vs `37,4%`; CO dopo check-bet call `55,2%` vs `56,3%`,
  raise `4,5%` vs `3,75%`.
- **Decisione F10.4: PASS**. Con la regola all-in corretta e il mix root CO
  locked, entrambi gli EV BTN sono entro `±0,05 ante` (non solo `±0,5`):
  il mismatch storico era principalmente (1) la regola all-in errata e (2) la
  selezione del mix root/posteriore CO. Il nodo CO dopo check-bet (`+0,0925`)
  resta il primo candidato del differenziale downstream residuo.
- Evidenza: `out/f104-root-lock.json`, `out/_nat-101.json`, report dei run
  re-baseline, `EXTERNAL_ROOT_LOCK_TEST=PASS` nella suite.

## 8. Registro delle implementazioni

### 2026-08-02 — Root EV in parità e apertura F10.4

- Acquisiti due export GTO+ v1.6.9 combo-per-combo: run operativo a dEV
  `0,390 ante` (`0,98%`, `1,71 s`) e run più accurato a dEV `0,078 ante`
  (`0,19%`, `4,20 s`).
- Il vero Target `0,10%` non è stato raggiunto: dopo circa `245 s` GTO+ era a
  dEV `0,045 ante` (`0,11%`). Il tempo a target è censurato a `>245 s`.
- Nel run accurato GTO+: CO root EV `19,1581`, bet root `6,487/36 = 18,0194%`,
  BTN dopo check `21,6682`, BTN dopo bet `17,1176`.
- Nel probe GTOSD a 1.000 iterazioni: CO root EV `19,163591179`, bet root
  `24,6348407%`, BTN dopo check `22,418551039`, BTN dopo bet `15,996174547`.
- Delta root GTOSD−GTO+ `+0,005491179 ante` (`0,0137%` del pot): **PASS**.
- Ricomposizione GTOSD verificata esattamente:
  `0,246348407 × 15,996174547 + 0,753651593 × 22,418551039 = 20,836408821`;
  sommando il CO si ottiene `40`.
- Ricomposizione GTO+ da `1,71 s`, entro gli arrotondamenti esportati:
  BTN root `20,8411945`; CO + BTN `39,9999945`.
- La precedente deduzione “EV BTN diversi ⇒ gioco diverso” è ritirata. Gli EV
  BTN sono condizionati da posteriori CO differenti perché le strategie root
  combo-per-combo sono differenti. Restano diagnostici finché tali posteriori
  non vengono controllati.
- Frequenze GTO+ instabili ma valore stabile: tra i due export il bet root passa
  da `19,7278%` a `18,0194%`, il root EV cambia solo di `-0,0007 ante` e l'EV
  BTN dopo bet cambia di `-0,3931 ante`. Evidenza compatibile con mix quasi
  indifferenti/equilibrio piatto, non con bucketing.
- Aperta **F10.4 — Root lock diagnostico GTO+ combo-per-combo**. Il lock varrà
  soltanto per il test, fisserà le 36 strategie CO root del run a dEV
  `0,078 ante`, salterà gli aggiornamenti regret al root e lascerà libere tutte
  le continuation.
- Invarianti F10.4: nessuna modifica all'albero o alla fixture standard; niente
  sampling/bucketing; blocker e card removal esatti; probabilità valide per
  tutte le 36 combo; soluzione e checkpoint marcati come gioco vincolato, mai
  presentati come equilibrio del gioco originale.
- Criterio diagnostico: se entrambi i delta BTN scendono entro `±0,5 ante`, la
  causa dominante è la selezione root/posteriore; altrimenti si apre il
  differenziale downstream su action tree, payoff e chance.
- Nessun codice è stato modificato per F10.4 in questo aggiornamento: è una
  fase pianificata, non implementata né validata.
- Decisione: **ROOT VALUE PARITY PASS; GATE COMPLESSIVO ANCORA BLOCCATO** per
  velocità e per la diagnostica controlled-posterior non ancora eseguita.

### 2026-08-01 — Verifica BTN, soglia all-in corretta e fixture `003`

- Ispezionata la soluzione GTO+ v1.6.9: root bet 20 `19,7%`; BTN dopo bet 20
  usa fold `37,4%`, call `62,6%`, raise 60 `0,0%`; non espone all-in 100.
- Corretto il trigger: stack completo / pot prima dell'azione, non
  `(stack-call)/(pot+call)`. Aggiunto regression test al nodo esatto.
- Ritirata `002`; `003` ha `112.848` nodi fisici, `31.461` canonici,
  `250.704` infoset, `526.872` action entry e fingerprint
  `fnv1a64:001a19fa48cd8b1e`.
- Cinque run Release: `2,8812251`, `2,7197086`, `2,8186997`, `2,6386802`,
  `2,6308951 s`; mediana `2,7197086 s`, p95 `2,8812251 s`.
- Speed score `62,874383%` **FAIL**; stato `4.214.976 byte`, memory score
  `189,799420%` **PASS**.
- A dEV `0,695544%`: EV `19,121570 / 22,201691 / 16,770667`; i due BTN
  falliscono anche la richiesta ±0,5 ante.
- Probe a 1.000 iterazioni e dEV `0,011334%`: EV BTN
  `22,418551 / 15,996175`; la divergenza cresce, quindi non è convergenza.
- Build completa `windows-gui-release` e suite Release `22/22` PASS, inclusi
  E2E Qt, legal-action regression e differenziale physical/canonical.
- Aggiornamento utente: “smoothly” era disabilitato; non spiega il residuo.
- Rerun Release dopo aver versionato il flag come disabilitato: iterazione 80,
  dEV `0,695544%`, EV `19,121570 / 22,201691 / 16,770667`, identici al run
  precedente; il solo tempo del processo è `3,0260982 s`.
- Varianti diagnostiche con `raise_depth` 2 e 3: stessi `112.848` nodi fisici,
  `31.461` canonici, `250.704` infoset, `526.872` action entry, dEV
  `0,695544%` ed EV bit-per-bit identici a `raise_depth` 1. La fingerprint di
  configurazione cambia, ma non l'albero materializzato: “più raise in GTO+” è
  escluso come causa per questa fixture.
- Sweep controllato delle quattro letture della soglia `<150%`: solo
  `stack/current_pot` conserva al primo nodo BTN `fold / call 20 / raise 60`.
  Le altre tre — `(stack-call)/(pot+call)`, `stack/(pot+call)` e
  `(stack-call)/current_pot` — producono tutte `fold / call 20 / all-in 100` e
  sono identiche tra loro: `63.474` nodi fisici, `288.900` action entry, dEV
  `0,904789%`, EV `19,128823 / 22,219692 / 16,196783`.
- La sola interpretazione A produce `112.848` nodi fisici, `526.872` action
  entry, dEV `0,695544%` ed EV `19,121570 / 22,201691 / 16,770667`; supera il
  gate dell'action set immediato ma non la tolleranza EV BTN. Nessuna delle
  quattro formule semplici ricostruisce quindi l'intero gioco GTO+.
- La semantica `Go all-in` resta da verificare sui nodi successivi, ma non può
  essere risolta sostituendo globalmente numeratore o denominatore con una
  delle tre alternative bocciate dallo stesso nodo GTO+ osservato.
- Decisione: **INCONCLUSIVE/REJECT**. La formula immediata è corretta, ma manca
  ancora il differenziale completo dell'action tree GTO+.
- Evidenza: `out/gto-plus-convergence/gtp003-five-runs/summary.json` e
  `out/diagnostics/all-in-semantics-sweep-summary.json`.

### 2026-08-01 — Baseline 1,71 s / 8 MB e nuovo gate EV

- Aggiornati i riferimenti esterni GTO+ a `1,71 s` e `8.000.000 byte`.
- Aggiunti tre EV flop condizionali obbligatori nella convenzione visuale GTO+:
  `19,15`, `21,65`, `17,51`, tolleranza assoluta `0,05 ante`.
- Ogni run pubblica riferimento, misura, delta e stato; il wrapper conserva
  tutti e cinque i report anche quando il gate EV fallisce.
- Run: `1,9034193`, `1,7017160`, `1,8440822`, `1,8186019`, `1,7689969 s`;
  mediana `1,8186019 s`, p95 `1,9034193 s`.
- Speed score `94,028275%` PASS; memory score `346,140533%` PASS.
- EV CO root `19,128823` PASS; EV BTN dopo check `22,219692` FAIL; EV BTN dopo
  bet 20 `16,196783` FAIL.
- La GUI espone gli EV condizionali GTO+ per nodo e permette di scegliere il
  turn/river nei nodi chance; il percorso turn è coperto dall'E2E Qt.
- Decisione: **REJECT**. La soluzione non è ancora allineata a GTO+ nonostante
  il superamento dei gate prestazionali.
- Evidenza: `out/gto-plus-convergence/ev-gate-1_71s-8mb/summary.json`.

### 2026-08-01 — Fixture corretta, GUI allineata e gate memoria PASS

- Ritirata `GTP-AHKHQH-001`: non conteneva il raise osservato in GTO+.
- Introdotta `GTP-AHKHQH-002`: bet/raise 50%, massimo un raise e policy
  `Go all-in if push < 150% pot`; BTN affrontando bet 20 espone fold, call e
  `All-in 100`.
- La GUI usa Target dEV GTO+, averaging delay 20 e certificazione ogni 20;
  usa stato `float32` con aritmetica/certificazione `float64`, ma conserva la
  precisione dei checkpoint storici durante il resume;
  conserva il layout tra preflight, solve e browser e non ricostruisce più
  l'albero a ogni click.
- Il traversal parallelizza anche i nodi a tre azioni, riusa mapping compatti
  per blocker/isomorfismi e usa Release MSVC AVX2/LTCG senza `/fp:fast`.
- Navigazione resa orizzontale e cache delle analisi nodo; matrice strategia
  9×9 resa read-only.
- Cinque run: `2,4507398`, `2,4276519`, `3,4393542`, `2,6085426`,
  `2,9743763 s`; mediana `2,6085426 s`, p95 `3,4393542 s`.
- Tutti i run: iterazione 80, dEV `0,904789%`, NashConv/Pot `1,588940%`;
  stato solver `2.311.200 byte`.
- Speed score `31,435178%` **FAIL**; memory score `112,495673%` **PASS**.
- Correttezza benchmark, differential physical/canonical, build Release, GUI
  E2E e suite completa 22/22 PASS. Gate complessivo ancora **FAIL**.
- Evidenza: `out/gto-plus-convergence/final-rebuilt-candidate/summary.json`.

### 2026-08-01 — Candidato ritirato: fixture `001` non allineata

- Corretto il riferimento a Target dEV `1%` e timer dal click `Run Solver` su
  albero già preparato fino alla soluzione consultabile, sullo stesso hardware.
- Profilo iniziale a 1%: layout `2,27 s`, traversal `8,32 s`, certificazioni
  `15,63 s`, finalizzazione `<0,001 s`.
- Certificazione portata alla sola iterazione finale 40; averaging delay CFR+
  impostato a 10 dopo confronto misurato.
- Traversal lossless specializzato sulle 36 combo attive, scratch riusabili,
  payoff terminali precomputati e normalizzazione completa spostata fuori
  dall'hot path (`2,22e-16` massimo errore).
- Parallelismo deterministic action-subtree fino a 6 thread; ogni worker usa
  delta regret separati, fusi soltanto dopo il join.
- Stato performance `float32` con aritmetica e certificazione `float64`:
  `2.005.632 byte`; checkpoint consultabile e ricertificabile.
- Cinque run finali: `0,8648449`, `0,8912323`, `0,9653215`, `0,7757150`,
  `0,8542203 s`; mediana `0,8648449 s`, p95 `0,9653215 s`.
- Tutti i run: iterazione 40, dEV `0,982194%`, NashConv/Pot `1,724106%`.
- Speed score `94,814689%`, memory score `129,634948%`: entrambi i gate
  prestazionali PASS.
- Build Release completa PASS, build GUI Release PASS e suite `15/15` PASS.
- Stato storico: **RITIRATO**. Il PASS non è valido per il gate perché mancava
  l'azione raise/all-in presente nel riferimento GTO+.
- Evidenza locale: `out/gto-plus-convergence/final-candidate/summary.json`.

### 2026-08-01 — Baseline diagnostica superata (target allora assunto 0,5%)

- Questa voce è conservata come storia del profiling ma non è confrontabile col
  run GTO+ da `0,82 s`, successivamente confermato a Target dEV `1%` e con tree
  già preparato.
- Distinto formalmente `Target dEV GTO+ = max(BR[p] - EV[p]) / pot` da
  `NashConv/Pot`, che somma i due guadagni.
- Aggiunto arresto exact su massimo guadagno normalizzato senza modificare il
  percorso NashConv esistente.
- Aggiunti fixture/schema versionati, comando CLI single-process e runner
  PowerShell con minimo cinque processi, mediana e p95 nearest-rank.
- Scope timer GTOSD: layout, inizializzazione, CFR+, averaging e certificazioni
  exact BR; esclusi startup processo e scrittura JSON.
- Run Release: `40,4711801`, `39,9082154`, `35,8920193`, `34,3246333`,
  `34,0701124 s`; mediana `35,8920193 s`, p95 `40,4711801 s`.
- Tutti i run: iterazione 70, dEV `0,4906437527%`, NashConv/Pot
  `0,8507773499%`, fingerprint `fnv1a64:e6b50494a9986bb2`, storage solver
  `4.011.264 byte`.
- Score velocità `2,284630%` e memoria `64,817474%`: gate entrambi FAIL.
- Correttezza/reproducibilità del run PASS; build Release completa e suite
  `15/15` PASS; comparabilità scientifica completa PENDING per worktree sporco
  e metadati GTO+ incompleti.
- Evidenza macchina: `out/gto-plus-convergence/five-runs/summary.json` (artefatto
  locale non versionato).

### 2026-07-29 — Baseline e gate formalizzato

- Accettato il nuovo riferimento GTO+ di `0,82 s` fino alla convergenza.
- Conservato il riferimento memoria GTO+ di `2,6 MB`.
- Formalizzate le soglie bloccanti: `≤0,911111 s` e `≤2,888889 MB`.
- Baseline GTOSD: `4,011264 MB`, 125.352 infoset, 250.704 action entry,
  14.673 nodi pubblici canonici.
- La misura GTOSD da `2,192 s` resta diagnostica perché rappresenta una
  iterazione più certificazione, non una convergenza allineata.
- F11 e tutte le fasi successive sono congelate.

## 9. Template per i prossimi aggiornamenti

Ogni nuova voce deve contenere:

```text
Data e commit:
Ipotesi:
File/componenti modificati:
Correttezza e test:
Configurazione hardware:
Run tempo [1..5]:
Mediana / p95:
Convergenza finale:
Memoria solver / transient / peak RSS:
Score velocità:
Score memoria:
Decisione: ACCEPT / REJECT / INCONCLUSIVE
Prossimo esperimento singolo:
```

## 10. Condizione di sblocco

Il freeze delle fasi viene rimosso soltanto quando una voce del registro
dimostra contemporaneamente:

```text
speed_score >= 90%
memory_score >= 90%
correctness_gates = PASS
release_suite = PASS
reproducibility = PASS
```

Fino ad allora il solo lavoro autorizzato sul percorso principale è
benchmarking, profiling, correttezza o ottimizzazione direttamente collegata a
`GTP-AHKHQH-003`.
