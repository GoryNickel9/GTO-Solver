# Censimento della suite di benchmark del solver preflop blueprint

Data: 2026-09-21. Stato del repository: branch `codex/fix-preflop-deep-stack-convergence`,
HEAD `ba93c75` (working tree pulito). Documento del milestone "riduzione RAM e tempi su tutta
la suite di benchmark", parte 1 (censimento) e rapporto sulle differenze originarie.

Fonti esaminate: codice (`libs/preflop_blueprint`, `libs/card_abstraction`, `benchmarks/*.cpp`,
`tests/`), fixture (`benchmarks/fixtures/*.json`), script (`scripts/research/*.ps1`,
`out/*.cmd`), documentazione di ricerca (`docs/research/**`), diario
(`preflop_vector_cfr/PROGRESS_LOG.md`), log e certificati in `out/` del checkout principale,
del worktree `C:/tmp/gtosd-preflop-blueprint` e del worktree Codex
`C:/Users/GoryNickel/.codex/worktrees/nash-convergence-audit/GTO-Solver`.

## 1. Famiglie trovate e perimetro

| Famiglia | Solver | Benchmark | Stato nel milestone |
|---|---|---|---|
| A. Preflop blueprint (CFR vettoriale con campionamento pubblico dei board, `libs/preflop_blueprint`) | `gtosd_preflop_blueprint_train` + `gtosd_preflop_blueprint_certify` | HU10 ridotto, HU10 completo, HU20, HU30, HU40 (= CO40 test), CO40 completo | **Suite del milestone**: sezioni 2-5 |
| B. Preflop external sampling (programma R0-R6 / V1-V23, chiuso il 2026-09-15, tag `preflop-legacy-es-2026-09-15`) | `gtosd_hu_preflop_solve` e affini (`libs/preflop`) | CO40 Monker (`hu_preflop_co40_game_v1.json`, open 6a/10a, risposte 10,5a/14,5a), HU10 calibrazione (`hu_preflop_hu10_calibration_v1.json`, 3a/5a, 6a/8a), sonde V23 su HU20 (`.tmp/v23_hu20_*`) | Censiti (sezione 6), **non confrontabili**: algoritmo e astrazione diversi, gate sulle frequenze Monker abbandonato (D1-D4, D25) |
| C. Solver postflop (`ProductionDcfr`, `libs/postflop*`) | `gto_cli`, `gtosd_river_bucket_qualification` | `gto_plus_ahkhqh_*`, `gto_plus_th7d6s_*`, `gto_plus_tstc9d_*`, corpus river | Fuori perimetro: altro solver, altro gioco (postflop) |
| D. Micro-benchmark Google Benchmark (`gtosd_benchmark_smoke`, showdown fusion, treelet, codec) | kernel isolati | smoke CTest | Fuori perimetro: non sono run del solver |

Il milestone riguarda la famiglia A. "HU40" nel goal del 2026-09-19 e nel diario indica la
fixture `preflop_blueprint_co40_test_v1.json` (heads-up CO contro BTN a 40 ante): i due nomi
sono conservati nella tabella dei nomi (sezione 4).

## 2. Benchmark della famiglia A

Le grandezze comuni a tutti i benchmark della famiglia: Short Deck 36 carte, due giocatori CO e
BTN, ante morta 1a per giocatore, button blind viva 1a del BTN (piatto iniziale 3a), rake
disabilitato, `allow_configured_incomplete_raise = true`, `include_all_in = true`,
`raise_termination = natural_stack`, contratto monetario revisione 2. Le unita' monetarie sono
10.000 per ante. L'astrazione delle carte e' la stessa per tutti i run con `history7`:
81 classi preflop, tabelle bucket 200/500/1.000 (`fnv1a64:33f06cf437f8f26d`,
`51814338fcf1236c`, `2e59aa76f59c0fcd`) e mappa `history7` (`fnv1a64:3c9ee76ca6aad23b`,
7.585 / 222.865 / 1.539.270 righe). I run che non usano `history7` sono indicati.

Regole di lettura: "Completo" = training portato al numero di iterazioni dichiarato e BR fisica
esatta su 573 flop / 605.088 board; "Profilo diagnostico" = run breve (10-3.000 iterazioni,
batch o thread non canonici, valutazione campionata) che non va confrontato con un run completo.

### 2.1 HU20 (canonico: `HU20`)

| Campo | Valore |
|---|---|
| Identificazione | Nome storico "HU20 test"; fixture `benchmarks/fixtures/preflop_blueprint_hu20_test_v1.json` (id `PREFLOP-BLUEPRINT-HU20-TEST-001`, SHA-256 `73135e90...c913edb0`); lancio manuale da PowerShell (voce del diario 2026-09-20) |
| Parametri dello scenario | stack 20a; 1 size preflop (open 5a, nessuna 3-bet configurata per decisione dell'utente del 2026-09-19); 1 size postflop |
| Size effettive | preflop: open a 5a, poi fold/call/all-in (`response_target_units: []`); postflop: 100 % del piatto piu' all-in; albero 571 nodi, 228 decisioni (8 preflop, 28/68/124 flop/turn/river), profondita' 14, fingerprint `fnv1a64:f5b432de223744cc` |
| Implementazione | `gtosd_preflop_blueprint_train.exe` (Trainer con `lazy-discount-v2-hybrid`, refresh delle sole righe del batch, pool di thread persistente) + `gtosd_preflop_blueprint_certify.exe` (BR fisica esatta board-major); build `out/build/windows-release-main-integration` (SHA-256 non registrato per il run del 2026-09-20; l'eseguibile presente oggi e' `4e3a2ddf...` del 2026-09-21 00:13, `29d7faa5...` per il certificatore del 2026-09-20 22:12) |
| Astrazione | `history7` (sopra); layout 467.201.821 celle per tabella; tre tabelle double 11,21 GB + timestamp lazy 0,82 GB = 12,04 GB di stato dichiarato |
| Esecuzione | 16.000 iterazioni, batch 32 (64 board per iterazione con update alternati), 8 thread, partizione 64, DCFR 1,5/0/2 alternato, seed di default, `--eval-every 0`, checkpoint e policy finali |
| Certificazione | `gtosd_preflop_blueprint_certify --threads 8 --chunk 16 --target-pot-percent 1`: BR fisica esatta, `exact = true`, `passes_target = max_gain <= 0,03 a` |
| Risultati disponibili | training 3.469,54 s (refresh 1.436,17, board 387,17, traversata 1.646,17, discount 0,017); trainer 3.570,71 s; certificatore 19,83 + 903,56 s; end-to-end 4.494,10 s (74m54s); working set finale del training 12.285.771.776 B (11,44 GiB), picco non campionato; certificatore 3.962.064.896 B; max gain **0,027999588711995 a**, NashConv 0,0299431715909233 a; policy `fnv1a64:362045ee45623b7a`, stato `fnv1a64:449bbb9b17798709`. File: `out/history7_optimized/hu20_history7_lazy_v2_16k_20260920_{train.jsonl,certificate.json,policy.bin,checkpoint.bin,cert_state.bin}`; audit `HISTORY7_TIME_AUDIT_2026-09-20.md` |
| Stato | **Completato, PASS** (unico run qualificato di HU20). Non rispetta il budget di prodotto di 8 GiB (D31) |

Run storici dello stesso scenario, non canonici:

| Run | Astrazione / protocollo | Esito | Riferimenti |
|---|---|---|---|
| class 2.000 / 4.000 / 8.000 | `class-major` 7.585/21.638/41.973, batch 32, 4 thread, eager | 0,060957 / 0,053315 / 0,049244 a, FAIL | worktree Codex `out/hu_goal/hu20_class_t*`, `hu20_t4000_full.json`, `hu20_t8000_full.json` |
| history7 pilot 500 / 2.000 | eager, batch 32 | campionato 64 flop 0,5957 / 0,2213 a | `out/hu_goal/hu20_history7_t{500,2000}_*` (Codex) |
| history7 lazy v1 8.000 / 16.000 | lazy v1, batch 32 | 16.000: 0,0279995887 a PASS (durata circa 3 h, scomposizione non conservata) | `out/hu_goal/hu20_history7_lazy_t{8000,16000}_*` (Codex) |
| batch 320 x 200, batch 512 x 125 | lazy, batch non canonico | astratta 0,172 a; batch 512 oltre 12 GiB | `hu20_history7_b320_t200_*`, `hu20_history7_b512_t125_*` (Codex) |
| gerarchia HR2 `8/8` 8.000 | mappa `fnv1a64:e1b177635b19fe48`, 3,43 GB | fisica 0,219175 a, astratta 0,154556 a | `out/hierarchy32/hu20_cap8_iter8000_*` |
| profili 100-3.000 iterazioni (batch 32/64, partizione 8/24/32/64/96, AVX2, radix, cache) | diagnostici | tempi per fase | `out/history7_optimized/hu20_history7_*_100.jsonl`, `*_1000.jsonl`, `*_3000.jsonl`, `out/hierarchy32/hu20_profile_*` |

### 2.2 HU30 (canonico: `HU30`)

| Campo | Valore |
|---|---|
| Identificazione | "HU30 test"; fixture `preflop_blueprint_hu30_test_v1.json` (id `PREFLOP-BLUEPRINT-HU30-TEST-001`, SHA-256 `5d0929dd...fb01cc216`); script `out/hu_goal/run_hu30_lazy_t16000.ps1` e `run_hu30_resume_t32000.ps1` (worktree Codex) |
| Parametri dello scenario | stack 30a; 2 size preflop (open 5a, 3-bet 17a); 1 size postflop |
| Size effettive | open 5a; risposta 17a sopra l'open; nel piatto limpato re-raise solo all-in (`limp_response_target_units: []`); postflop 100 % piu' all-in; albero 604 nodi, 242 decisioni (10 preflop, 32/72/128), fingerprint `fnv1a64:17dc5c7d07ea30c2` |
| Implementazione | stessi eseguibili di HU20 (build main-integration), **modalita' automatica** (`--target-pot-percent 1` senza `--iterations`): checkpoint geometrici 250/500/1.000/.../16.000 con valutazione campionata su 8 flop e certificazione esatta nello stesso processo; poi ripresa esplicita a 32.000 |
| Astrazione | `history7`; 481.360.067 celle per tabella; stato dichiarato 12,41 GB |
| Esecuzione | batch 32, 8 thread, partizione 64, DCFR alternato lazy v2; 16.000 iterazioni nel run automatico (con 7 valutazioni campionate e una certificazione esatta in-process), poi 16.000 iterazioni di ripresa fino a 32.000 |
| Certificazione | in-process a 16.000 (`hu30_history7_lazy_v2_auto_t16000_certificate.json`, chunk 32, 1.045 s) e a 32.000 (`hu30_history7_lazy_v2_auto_certificate.json`, 1.141 s); BR astratta esatta a 32.000 (3.195 s, `hu30_history7_lazy_v2_t32000_abstract_br_exact.json`) |
| Risultati disponibili | 16.000: max gain 0,191818521 a FAIL; 32.000: **0,167619129 a FAIL**, NashConv 0,232676 a, astratta 0,034874 a; training 16.500 iterazioni di ripresa in 3.933,73 s a 32.500 (0,238 s/iter); working set 12,65-12,68 GB; policy 32k `fnv1a64:e52d2f110dbd2b34`. File in `out/history7_optimized/hu30_history7_lazy_v2_auto_*` e `hu30_history7_lazy_v2_t32000_*` |
| Stato | **Completato, FAIL** al gate; il tempo non e' comparabile con HU20 perche' il run include valutazioni e certificazione in-process (processo diverso) |

Run non canonici: profilo 100 iterazioni (`hu30_history7_lazy_v2_profile_100.jsonl`); batch
32/64/128 con board uguali a 500-2.000 iterazioni (`hu30_equal_boards_*`); DCFR simultaneo batch
32 2.000 (`hu30_dcfr_simultaneous_batch32_t2000.jsonl`); cap river 23 simultaneo 2.000
(`hu30_cap23_simultaneous_t2000.jsonl`, picco 24,67 GiB, BR astratta ristretta peggiore); censimenti
di mappe cap 8/12/16/23/24/28/30/32 (`history_cap*_candidate.json`). Tutti diagnostici.

### 2.3 HU40 (canonico: `HU40`; nome storico "CO40 test")

| Campo | Valore |
|---|---|
| Identificazione | fixture `preflop_blueprint_co40_test_v1.json` (id `PREFLOP-BLUEPRINT-CO40-TEST-001`, SHA-256 `b0f5eaaf...5fae5577e69ddd7e7`); script `scripts/research/run_hu40_history7_solve.ps1` (build main-integration, monitor esterno del working set con limite 12 GiB) e `finalize_hu40_t37000.ps1` |
| Parametri dello scenario | stack 40a; 2 size preflop (5a, 17a); 1 size postflop |
| Size effettive | come HU30 con stack 40a: 604 nodi, 242 decisioni, fingerprint `fnv1a64:18d08f453034ac0f` (stessa struttura di HU30, importi diversi) |
| Implementazione | stessi eseguibili (main-integration `4e3a2ddf...` train, `29d7faa5...` certify, `50c3f4c8...` abstract BR); iterazioni esplicite |
| Astrazione | `history7`; 481.360.067 celle; stato 12,41 GB |
| Esecuzione | 16.000 iterazioni (3.471,39 s, 0,217 s/iter), ripresa a 32.000 (3.392,63 s), ripresa a 37.000 (5.000 iterazioni); batch 32, 8 thread, partizione 64, DCFR alternato lazy v2, `--eval-every 0` |
| Certificazione | BR astratta esatta dopo ogni blocco (3.062 / 3.441 / 3.098 s, picco 7,87 GB); BR fisica esatta finale a 37.000: chunk 16, 962,45 s di valutazione, 967,07 s totali, picco working set 7.844.061.184 B (monitor esterno) |
| Risultati disponibili | astratta 0,102033 / 0,063999 / 0,058519 a a 16k/32k/37k; **fisica 0,248745 a FAIL** a 37k, NashConv 0,377301 a; picco working set del training 12.678.443.008 B (11,81 GiB); policy 37k `fnv1a64:6caea24c57414392`. File in `out/hu40_history7_solve/` (`solve_state.json`, `hu40_history7_t*_train.jsonl`, `*_abstract_br_exact.json`, `hu40_history7_final_physical_certificate.json`, `hu40_history7_final_coverage.json`) |
| Stato | **Completato, FAIL** al gate; le tre tranche di training furono eseguite come processi separati con ripresa da checkpoint |

Run non canonici dello stesso scenario: pilot history7 500 iterazioni (`out/hu40_pilot/`); profilo
history7 10 iterazioni (`out/hu_goal/hu40_class_profile100.log`, Codex); class 10.000 iterazioni
(0,500181 a esatti, 4 thread, checkpoint `hu40_legacy_current.bin`); nell'era P8/P9 (albero con
open 6a/10a e risposte 10,5a/14,5a, poi 5a/13a, fingerprint `2d2711aa8697e2b7` e
`9066044f8c0f0f59`, bucket 200/500/1.000 senza storia) DCFR 2.000 = 0,656927 a, DCFR 10.000 =
0,840618 a, Linear 2.000 = 1,086169 a, DCFR 2.000 a una size 5a/13a = 0,651556 a
(`C:/tmp/gtosd-preflop-blueprint/out/co40t*_cert.json`). Non confrontabili: albero diverso.

### 2.4 HU10 ridotto (canonico: `HU10`, con riallineamento delle size)

| Campo | Valore |
|---|---|
| Identificazione | "HU10 ridotto"; fixture `preflop_blueprint_hu10_reduced_v1.json` (id `PREFLOP-BLUEPRINT-HU10-REDUCED-001`, SHA-256 `1f77056c...03eaff365a0`); comandi in `out/hu10_regression/red.cmd` e nel diario (P8, 2026-09-16) |
| Parametri dello scenario | stack 10a; 1 size preflop (open 5a; a 10a una 3-bet a 17a supera lo stack e il loader la rifiuta); 1 size postflop |
| Size effettive | open 5a, poi fold/call/all-in; postflop **66 %** del piatto piu' all-in (non 100 %); albero 193 nodi, 80 decisioni (8 preflop, 16/24/32), fingerprint `fnv1a64:cc5c2f8eea9aac57` |
| Implementazione | eseguibili del worktree `C:/tmp/gtosd-preflop-blueprint/out/build/windows-release` del 2026-09-16 (prima di lazy discount, refresh selettivo, pool persistente e righe a 32 bit) |
| Astrazione | **bucket 200/500/1.000 diretti** (nessuna mappa di classe o di storia); 102.901 celle per tabella, stato 2,47 MB |
| Esecuzione | 2.000 iterazioni, batch 32, 8 thread, DCFR alternato eager, partizione automatica, valutazione campionata su 20 flop ogni 500 iterazioni (25,8 s) |
| Certificazione | esatta, 8 thread, chunk 16 (`r3_cert_red.json`, ricontrollo `out/hu10_regression/red.json`) |
| Risultati disponibili | training 312,11 s (0,156 s/iter), totale 351,66 s, processo 98 MB; certificato **0,0039805 a PASS** (0,13 % del piatto), NashConv 0,0073519 a, 171-183 s, 142-145 MB; policy `fnv1a64:dd9bf1029eec784c`, file `C:/tmp/gtosd-preflop-blueprint/out/policy3_red_dcfr_200.bin`, `r3_train_red.log`, `r3_cert_red.json` |
| Stato | **Completato, PASS** con protocollo diverso (2.000 iterazioni, astrazione diversa, size diversa): non confrontabile con HU20; il layout `history7` stimato e' 2,81 GiB di stato (`hu10_history7_layout_estimate.json`), mai eseguito prima di questo milestone |

### 2.5 HU10 completo (canonico: `HU10-FULL`)

| Campo | Valore |
|---|---|
| Identificazione | fixture `preflop_blueprint_hu10_full_v1.json` (id `PREFLOP-BLUEPRINT-HU10-FULL-001`, SHA-256 `73e5270b...fabad1b491`); `out/hu10_regression/full.cmd` |
| Parametri dello scenario | stack 10a; 1 size preflop; 3 size postflop |
| Size effettive | open 5a poi fold/call/all-in; postflop 33 / 66 / 120 % piu' all-in; albero 1.501 nodi, 584 decisioni (8, 48/160/368), profondita' 15, fingerprint `fnv1a64:bc9e7b35ad8c021d` |
| Implementazione / astrazione / esecuzione | come HU10 ridotto: bucket 200/500/1.000 diretti, 2.000 iterazioni, batch 32, 8 thread, eager; 1.094.101 celle, stato 26 MB |
| Certificazione | esatta, 8 thread, chunk 16 |
| Risultati disponibili | training 392,83 s (0,196 s/iter) piu' 279,12 s di valutazioni, totale 685,57 s, 123 MB; certificato **0,0039949 a PASS**, NashConv 0,0075699 a, 2.108-2.207 s, 211 MB; policy `fnv1a64:8d5a662a93dc11fd` (`policy3_full_dcfr_200.bin`, `r3_cert_full.json`, `out/hu10_regression/full.json`) |
| Stato | **Completato, PASS** con protocollo e astrazione diversi. Con `history7` il layout richiede 1.425.343.821 celle per tabella: 34,1 GiB di stato, oltre la RAM fisica (32 GiB) e oltre il budget di 8 GiB: **memoria insufficiente** per la baseline uniforme |

### 2.6 CO40 completo (canonico: `HU40-FULL`)

| Campo | Valore |
|---|---|
| Identificazione | fixture `preflop_blueprint_co40_v1.json` (id `PREFLOP-BLUEPRINT-CO40-001`, SHA-256 `65041746...20800751`); usata dai test CTest `gtosd_preflop_blueprint_game_report` e `gtosd_preflop_blueprint_traversal_report` |
| Parametri dello scenario | stack 40a; 2 size preflop (5a, 17a); 3 size postflop |
| Size effettive | 33 / 66 / 120 % piu' all-in; albero 26.878 nodi, 9.958 decisioni (10 preflop, 300/1.988/7.660), profondita' 17, fino a 4 raise per street, fingerprint `fnv1a64:d6c10723d35b9503` |
| Implementazione | mai addestrato sull'albero corrente; sull'albero precedente (27.012 nodi, size 6a/10a) solo un certificato parziale con policy uniforme (8 flop, `co40_full_chunk8_cert.json`) e le misure di traversata di P5 |
| Astrazione | con `history7`: 773.649.883 celle per tabella, 18,6 GB per tre tabelle, oltre 21 GB con i timestamp: **memoria insufficiente** rispetto al budget di 8 GiB; con bucket diretti 200/500/1.000: 22.456.987 celle (359 MB per due tabelle), astrazione diversa |
| Esecuzione / certificazione | nessuna; proiezione della sola BR esatta: 64 s per flop canonico a 8 thread (10,2 h) |
| Stato | **Mai eseguito** (ne' training ne' certificazione completa); censito come scenario della suite con esito `RESOURCE_LIMIT` per l'astrazione uniforme |

### 2.7 Scenario derivato `HU20-2`

Stack 20a con 2 size preflop (open 5a e 3-bet 17a) dal catalogo comune. Il motore lo compila
con 604 nodi come HU30 (fingerprint `fnv1a64:7b59c6cacc9f5da5`): a 20 ante la 3-bet a 17a resta
un'azione distinta dallo shove (lascia 3a dietro). Nessun run storico. Serve solo a rendere
esplicito che `HU20` con una sola size preflop e' una scelta dichiarata dello scenario e non un
effetto del cap allo stack (sezione 3.5).

## 3. Rapporto sulle differenze originarie

I benchmark storici non formano una suite uniforme. Le differenze, oltre a stack e numero di
size, sono le seguenti.

### 3.1 Astrazione delle carte (tre rappresentazioni diverse)

| Benchmark | Rappresentazione | Righe flop/turn/river | Celle per tabella |
|---|---|---|---|
| HU10 ridotto, HU10 completo | bucket diretti 200/500/1.000 | 200 / 500 / 1.000 | 102.901 / 1.094.101 |
| HU20 class (2k-8k), HU40 class 10k | `class-major-v1` (classe preflop x bucket) | 7.585 / 21.638 / 41.973 | 16,3 M (HU40) |
| HU20 16k, HU30 16k-32k, HU40 16k-37k | `history7` (storia fino al turn, river cap 7) | 7.585 / 222.865 / 1.539.270 | 467,2 M / 481,4 M / 481,4 M |

Conseguenza: i PASS di HU10 (0,0040 a) e di HU20 (0,0280 a) sono stati ottenuti con
astrazioni diverse e non misurano lo stesso solver.

### 3.2 Protocollo di training

| Benchmark | Iterazioni | Batch | Thread | Discount | Refresh policy | Valutazioni durante il training |
|---|---:|---:|---:|---|---|---|
| HU10 ridotto/completo | 2.000 | 32 | 8 | DCFR eager | denso | 20 flop ogni 500 iterazioni |
| HU20 class | 2.000 -> 8.000 | 32 | 4 | eager | denso | nessuna |
| HU20 history7 16k | 16.000 | 32 | 8 | lazy v2 | righe del batch | nessuna |
| HU30 history7 | 16.000 automatico + 16.000 ripresa | 32 | 8 | lazy v2 | righe del batch | 8 flop a 250/500/.../16.000 e certificazione in-process |
| HU40 history7 | 16.000 + 16.000 + 5.000 | 32 | 8 | lazy v2 | righe del batch | nessuna (BR astratta fra i blocchi, processi separati) |

### 3.3 Size e albero

- HU10 ridotto usa il 66 % postflop, HU20/HU30/HU40 di test il 100 %: il contratto "una size
  postflop" era applicato con griglie diverse.
- HU20 non configura la 3-bet a 17a, HU30 e HU40 si'. A 20 ante il motore compilerebbe la
  3-bet come azione distinta (sezione 2.7): l'assenza e' una decisione dell'utente del 2026-09-19
  registrata nel commit `6462dfa`, non la conseguenza della regola del cap allo stack. Il goal
  del 2026-09-19 la descrive come "fusa con lo shove secondo la regola corrente": la frase non
  corrisponde al codice ed e' corretta da questo censimento.
- Le size preflop di HU40 sono cambiate due volte nella storia (6a/10a Monker, poi 5a/13a,
  poi 5a/17a): i certificati CO40 test del 2026-09-16 riguardano alberi diversi da quello attuale.

### 3.4 Eseguibili e build

Non esiste un fingerprint dell'eseguibile per il run HU20 di riferimento (74m54s): il diario
registra solo l'identita' del trainer (`fnv1a64:90d07de511eb9968`) e gli SHA-256 dei file di
output. I run HU10 usano una build del 2026-09-16 senza le ottimizzazioni successive; i run
HU30/HU40 usano la build main-integration del 2026-09-20/21. Gli eseguibili attuali di quella
directory non coincidono per dimensione con la build pulita dell'HEAD (`571.904` contro
`606.208` byte per il trainer), quindi la loro corrispondenza con un commit non e' dimostrabile.

### 3.5 Certificazione

Tutti i certificati esatti usano lo stesso certificatore (BR fisica esatta su 573 flop e 605.088
board) con 8 thread; il chunk varia (16 per HU20/HU40/HU10, 32 per HU30 a 16.000 e in-process a
32.000). Il chunk non cambia il risultato numerico ma cambia il tempo; la modalita' in-process di
HU30 rende il tempo del training non separabile senza leggere gli eventi del log.

### 3.6 Misure di memoria

Le misure storiche riportano grandezze diverse: working set finale interno (`process_bytes` a
fine training, dopo il rilascio della policy), picco di working set del monitor esterno (HU40),
memoria privata (alcuni pilot). Nessun run registra insieme picco di private commit e picco di
working set del ciclo completo training + certificazione.

### 3.7 Conclusione

I confronti storici fra HU10, HU20, HU30 e HU40 sono diagnostici. Per il milestone e' stata
costruita una baseline canonica rieseguita (`baseline-ba93c75`): stessa build dell'HEAD,
stessa astrazione `history7`, stesso protocollo (16.000 iterazioni, batch 32, 8 thread,
partizione 64, DCFR alternato lazy v2, refresh delle righe del batch, checkpoint e policy finali,
BR fisica esatta chunk 16), stessa griglia di size. Il protocollo e il catalogo delle size sono
in `benchmarks/suite/preflop_blueprint_suite.json`; le fixture generate in
`benchmarks/suite/fixtures/`; il controllo di uniformita' e' `tools/preflop_suite/suite.py check`.

## 4. Corrispondenza fra nomi

| Nome canonico | Nomi storici | Fixture storica | Fixture canonica | Tree fingerprint |
|---|---|---|---|---|
| `HU10` | HU10 ridotto, "HU10" nel goal | `preflop_blueprint_hu10_reduced_v1.json` (66 %) | `benchmarks/suite/fixtures/HU10.json` (id `PREFLOP-BLUEPRINT-HU10-POT-001`, 100 %) | storico `cc5c2f8eea9aac57`, canonico `d7b31d6f2cb759fc` |
| `HU10-FULL` | HU10 completo | `preflop_blueprint_hu10_full_v1.json` | identica (`HU10-FULL.json`) | `bc9e7b35ad8c021d` |
| `HU20` | HU20 test, HU20 | `preflop_blueprint_hu20_test_v1.json` | identica (`HU20.json`) | `f5b432de223744cc` |
| `HU30` | HU30 test, HU30 | `preflop_blueprint_hu30_test_v1.json` | identica (`HU30.json`) | `17dc5c7d07ea30c2` |
| `HU40` | CO40 test, CO40-TEST, HU40 | `preflop_blueprint_co40_test_v1.json` | identica (`HU40.json`) | `18d08f453034ac0f` |
| `HU40-FULL` | CO40, CO40 completo | `preflop_blueprint_co40_v1.json` | identica (`HU40-FULL.json`) | `d6c10723d35b9503` |
| `HU20-2` | nessuno | nessuna | `HU20-2.json` | `7b59c6cacc9f5da5` |

Le fixture canoniche di `HU10-FULL`, `HU20`, `HU30`, `HU40` e `HU40-FULL` sono byte per byte
uguali alle fixture storiche (verificato dal resolver): policy e certificati storici restano
caricabili. `HU10` cambia size postflop e quindi fingerprint: i risultati storici di HU10 ridotto
restano come storia e non entrano nel confronto.

## 5. Stato della suite prima del milestone

| Scenario | Astrazione uniforme `history7` | Protocollo comune (16k, batch 32, 8 thread) | Esito storico |
|---|---|---|---|
| HU10 | mai eseguito (stimato 2,81 GiB di stato) | mai eseguito | PASS con bucket diretti e 2.000 iterazioni (non comparabile) |
| HU10-FULL | memoria insufficiente (34,1 GiB) | non eseguibile | PASS con bucket diretti (non comparabile) |
| HU20 | si' | si' | PASS 0,0280 a, 74m54s, 11,44 GiB |
| HU30 | si' | processo diverso (automatico, in-process) | FAIL 0,1676 a a 32.000 |
| HU40 | si' | si' (in tre tranche) | FAIL 0,2487 a a 37.000 |
| HU40-FULL | memoria insufficiente (oltre 18,6 GB solo di tabelle) | non eseguibile | mai eseguito |

## 6. Famiglia B: benchmark del programma external sampling (censiti, non confrontabili)

| Benchmark | Fixture | Contenuto | Risultati | Riferimenti |
|---|---|---|---|---|
| CO40 Monker | `benchmarks/fixtures/hu_preflop_co40_game_v1.json` | stack 40a, open 6a/10a, risposte 10,5a/14,5a, postflop 33/66/120 %, riferimento Monker `hu_preflop_co40_reference_v1.json` | candidati V-series in `benchmarks/results/hu_preflop_co40_*_candidate_v1.json` con confronti Monker (WMAE 13-18 pp, nessun candidato qualificato) | `PREFLOP_LEGACY_INDEX.md`, tag `preflop-legacy-es-2026-09-15` |
| HU10 calibrazione | `hu_preflop_hu10_calibration_v1.json` | stack 10a, open 3a/5a, risposte 6a/8a | oracolo `FiniteGame` con NashConv esatta (V21-V23) | `libs/preflop`, `gtosd_hu_preflop_abstract_nashconv` |
| Sonde V23 HU20 | `.tmp/v23_hu20_stack_only_probe.json` | stack 20a con le size CO40 Monker; abstract game V23 perfect recall (32/128/512) | solo censimento degli information set, nessuna NashConv certificabile | `.tmp/v23_hu20_*.json` |
| Sensibilita' rake CO40 | `hu_preflop_co40_rake5_cap3_sensitivity_v2.json` | rake 5 %, cap 3 | non usato dal blueprint (rake disabilitato per schema) | idem |

Sono programmi con algoritmo (external sampling MCCFR su deal fisici), astrazione (bucket
MC8/MC4) e gate (frequenze Monker) diversi; non possono entrare nel confronto di questo
milestone. Restano nella storia git.
