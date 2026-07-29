# Stato implementazione roadmap HU Short Deck

Aggiornato: 2026-07-29

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
| F7 | **Completata** | Modulo `gtosd::postflop`, CFR+ exact, BR/NashConv, checkpoint/resume, query, PF-F1 a 0,741405%, layout range-aware, infoset canonici e public DAG lossless | Il benchmark GTO+ `AhKhQh` usa 125.352 infoset, 250.704 action entry e 14.673 nodi pubblici canonici; la costruzione parte ancora dal tree fisico |
| F8 | **Completata** | Modulo `gtosd::storage`, `.gtsd` 1.0 chunked, Zstd, secretstream, random access, atomic save, migrazione, verifier, catalogo SQLite e 309 asserzioni | La quantizzazione resta sperimentale; la misura PF-F1 storage usa una iterazione e non sostituisce la certificazione F7 |
| F9 | **Completata localmente** | Qt/ImGui, 7/7 E2E, 19/19 regression, tre backend sopra 60 FPS, install tree verificato | Qualifica su hardware esattamente 4-core/2 GHz/16 GB resta release gate F10 |
| F10 | **Completata localmente** | `gto_gui` Qt, pannelli CO/OOP e BTN/IP, board visuale 3–5 carte, Target dEV, range quadrati paint-on-click/slider, pausa/cancel, stima risorse range-aware conservativa, chiavi locali trasparenti, log persistenti, recovery cifrato, heatmap 9×9 ed E2E create→solve→save→reopen→navigate→resume | Qualifica personale e su hardware esattamente 4-core/2 GHz/16 GB restano gate distinti; la stima preventiva non sottrae ancora le orbite canoniche effettive |
| F11+ | Non iniziata | — | Nodelock globale e milestone successive |

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
paint-on-click/slider a basis point, Target dEV certificato a ogni iterazione,
preflight e backend memoria automatici, solve CFR+ in worker separato, pausa,
annullamento e progresso per iterazione,
checkpoint/recovery `.gtsd`, save/open autenticato, albero fisico e strategy
matrix per classe e combo.

| Gate | Evidenza |
|---|---|
| Crea→solve→salva→riapri→naviga→resume | PASS, E2E Qt sull'eseguibile reale e dall'install tree |
| Classe/combo | PASS, heatmap 9×9 combo-weighted e query batch delle combo fisiche legali |
| Progress continuo | PASS, iteration counter indipendente dall'intervallo BR/NashConv |
| Nessun freeze solve | PASS, massimo gap heartbeat 12,0331 ms sulla fixture E2E installata |
| Range effettivi | PASS, reach CFR/BR, fingerprint, checkpoint e chunk `RANGES` condividono gli stessi 1.260 pesi |
| Regressioni | PASS, Release 21/21; Debug F10; MSVC ASan core e GUI; clang-format |
| Packaging | PASS, install tree pulito 75 file / 89.080.903 B e smoke E2E installato |

La misura E2E usa una fixture ridotta check-only da due iterazioni, poi ripresa
fino alla terza, e non dimostra convergenza. La certificazione solver resta
PF-F1 F7 a 0,741405%.
Il dettaglio, i limiti e i comandi di riproduzione sono in
[`PHASE_10_COMPLETION_REPORT.md`](PHASE_10_COMPLETION_REPORT.md).

### Prossimo ingresso

La milestone successiva è **Fase 11 — Nodelock globale**.

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
