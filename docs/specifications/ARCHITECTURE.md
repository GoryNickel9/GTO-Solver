# Architettura

## Obiettivi

L'architettura mantiene separati regole, rappresentazione del gioco, solver,
persistenza e presentazione. Il core matematico deve funzionare senza interfaccia
grafica, filesystem di prodotto o servizi remoti. GTOSD è C++20, locale e modulare.

## Modello di esecuzione CPU/RAM

Il solver usa esclusivamente CPU e RAM di sistema. Non esiste e non verrà
introdotto un backend GPU per costruzione dell'albero, canonicalizzazione,
traversal CFR, regret/strategy update, best response, certificazione o analisi
della soluzione. Sono vietati CUDA, ROCm, OpenCL, Vulkan Compute,
DirectCompute, compute shader e deleghe equivalenti ad acceleratori.

Un'interfaccia (oggi la web UI) può usare una GPU soltanto per disegnare. Tale
rendering è esterno al core matematico, non riceve workload del solver e non può
essere incluso in un benchmark di solving. Tutte le stime di capacità e i gate
prestazionali del solver devono quindi essere soddisfatti con CPU e RAM.

## Moduli correnti

```text
core -> equity -> tree -> isomorphism
  \        \        \          \
   \        +--------+-----------+-> postflop -> storage
    +-----------------> solver -> best_response       \
                         memory ------------------------+-> gto_cli
```

Le frecce indicano dipendenze concettuali; CMake applica i link effettivi.

| Modulo | Responsabilità |
|---|---|
| `gtosd_core` | carte, denaro, range, stato, azioni, rake e settlement |
| `gtosd_equity` | evaluator Short Deck e showdown |
| `gtosd_tree` | config JSON e public tree fisico |
| `gtosd_isomorphism` | permutazioni globali lossless dei semi |
| `gtosd_solver` | giochi finiti di riferimento e varianti CFR |
| `gtosd_best_response` | profile value, best response e NashConv |
| `gtosd_memory` | layout e backend di memoria |
| `gtosd_postflop` | solver exact HU range-aware e analytics |
| `gtosd_storage` | container `.gtsd`, cifratura, compressione e catalogo |
| `gto_cli` | automazione, solve, inspect e benchmark |
| altri moduli | ricerca, preflop legacy e preflop blueprint: vedi "Altri moduli" |

Le API pubbliche vivono sotto `include/gtosd`; le implementazioni sotto
`libs/*`. Le applicazioni possono dipendere dalle librerie, mai il contrario.

## Flusso del solve postflop

> **Migrazione architetturale completata per il percorso production corrente.**
> [`ADR_0002_MEMORY_BOUNDED_EXACT_SOLVER.md`](../ADR_0002_MEMORY_BOUNDED_EXACT_SOLVER.md)
> definisce il backend memory-bounded iniziale; il traversal production usa il
> layout canonico lossless e lo stato node-scaled qualificato. La schedule
> corrente e' definita in `SOLVER_ALGORITHMS.md`, non nell'ADR storico.

1. Il config versionato viene parsato e validato.
2. Range e blocker vengono validati senza rinormalizzazione implicita.
3. Le azioni del public tree sono generate deterministicamente; il solve
   production le compila in streaming nel grafo canonico, mentre il browser
   può richiedere la materializzazione fisica completa.
4. L'isomorfismo lossless riduce infoset duplicati; il target migrato usa un
   canonical chance tree senza unificare history arbitrarie.
5. `prepare_postflop_tree` crea il layout infoset/action e, se richiesto, gli
   indici analytics.
6. L'algoritmo selezionato aggiorna regret e strategy sum; le fixture production
   usano `ProductionDcfr`, mentre CFR+ resta oracle/fallback.
7. A intervalli espliciti la best response certifica il profilo medio.
8. Checkpoint e soluzione possono essere salvati e ripresi solo con fingerprint
   compatibile.

Il target approvato costruisce direttamente un canonical chance tree con
coordinate private player-local, mapping inversi dei CFV e subtree disgiunti.
Non unifica history arbitrarie in un DAG globale. Il traversal owner-computes
senza delta `O(worker * actions)` e exact BR streaming. CFR-D è un livello successivo
per i giochi che non rispettano il preflight dopo la riduzione lossless; non è
un sostituto implicito dell'algoritmo postflop corrente.

La preparazione è riutilizzabile: benchmark e CLI possono eseguire più tranche
senza ricostruire la topologia. L'analytics è opt-in per non gonfiare il path di
solving quando non serve.

Lo stato scaled-uint16 usa normalmente vettori residenti. Un target di working
set non nullo autorizza invece una mapping locale OS-page-backed quando il
modello `RSS corrente + stato logico` supera il target. La decisione è basata
soltanto sulle risorse, non sulla fixture; la rappresentazione e l'ordine degli
update restano identici. Persistenza ed export richiedono materializzazione
esplicita e il report deve rendere visibile la residenza scelta.

## Confini e invarianti

- `core` non conosce solver, interfacce o storage.
- `equity` non decide frequenze o azioni.
- `tree` contiene solo stato pubblico; le combo private entrano negli infoset.
- `isomorphism` applica una permutazione globale, mai una canonicalizzazione
  board-only che rompa i blocker.
- `postflop` non campiona e non bucketizza nel percorso exact.
- `postflop`, `solver` e `best_response` non delegano calcolo a GPU o altri
  acceleratori: CPU e RAM sono l'unico backend autorizzato.
- `storage` non modifica semantica o precisione del solve.
- Le interfacce e la CLI non ricalcolano metriche con formule divergenti dal core.

## Altri moduli

Aggiornato il 2026-10-03.

| Modulo | Responsabilità | Stato |
|---|---|---|
| `gtosd_postflop_subgame` | bridge exact e bounded da un river postflop a `FiniteGame`, boundary e splice (R2-S); dipende da `postflop` e `solver` | ricerca, compilato e testato |
| `gtosd_solver_validation` | validazione TRE su tre corpus chance (training, risposta, valutazione); dipende da `best_response` | ricerca, compilato e testato |
| `gtosd_preflop_trainer`, `gtosd_preflop`, `gtosd_preflop_certifier` (`libs/preflop`) | preflop external sampling R0-R6 | legacy: programma chiuso il 2026-09-15, codice compilato e testato |
| `gtosd_card_abstraction` | risorse esatte del preflop blueprint: rank, tabelle all-in e 3-way, board canonici, feature esatte, tabelle bucket | prodotto |
| `gtosd_preflop_blueprint` | solver preflop blueprint: modello di gioco, kernel HU e multiway, trainer, best response, certificatore, export, chart e query | prodotto |

`gtosd_card_abstraction` dipende da `core` ed `equity`; `gtosd_preflop_blueprint`
da `core`, `equity`, `tree` e `card_abstraction`. Le due librerie si compilano
con l'opzione `GTOSD_BUILD_PREFLOP_BLUEPRINT` (default `ON`). La guardia
`tests/verify_preflop_blueprint_isolation.cmake` (CTest
`gtosd_preflop_blueprint_dependency_check`) rifiuta nei loro CMakeLists i link a
`solver`, `best_response`, `solver_validation`, `isomorphism`, `memory`,
`postflop`, `postflop_subgame`, al preflop legacy e a `storage`. Nei loro
sorgenti e nelle CLI `benchmarks/preflop_blueprint_*.cpp` rifiuta gli include
di `gtosd/postflop/`, `gtosd/preflop/`, `gtosd/solver/`, `gtosd/memory/`,
`gtosd/storage/` e `gtosd/isomorphism/`. I test possono usare le librerie
postflop come oracolo.

Questa specifica non descrive il progetto del preflop blueprint. I documenti
sono i report di modulo P0-P8 e il diario in `docs/research/preflop_vector_cfr/`.

## Persistenza e sicurezza

Checkpoint e `.gtsd` hanno ruoli distinti. Il checkpoint contiene lo stato
necessario al resume; `.gtsd` è il contenitore autenticato e indicizzato per il
prodotto. Il writer usa compressione Zstandard, secretstream
XChaCha20-Poly1305, verifica prima del replace e scrittura atomica.

La chiave è fornita dal chiamante e non viene salvata nel file. Il catalogo
SQLite esterno indicizza le soluzioni, ma non è necessario per aprirle.

## Estensioni previste

Il node locking deve aggiungere moduli o contratti senza far dipendere il core
dalle interfacce. Il root lock F10.4 implementato è un esperimento diagnostico
test-only e non costituisce l'API di node locking di prodotto.

Il preflop e il multiway non reinterpretano il solver HU postflop. Sono in una
libreria separata, `gtosd_preflop_blueprint`, con action abstraction, card
abstraction e campionamento dei board propri (vedi "Altri moduli"). Gli
interventi previsti su `gto_cli` sono solve con i range dell'utente, eventi di
avanzamento JSONL e un worker `serve`. Sono comandi nuovi della CLI e non
cambiano i contratti esistenti.

## Toolchain e distribuzione

La build canonica usa CMake preset, vcpkg e MSVC su Windows. Le librerie
installano target CMake namespaced `gtosd::`. La GUI desktop Qt, le sue opzioni
di build e la feature vcpkg `gui-prototypes` sono state tolte il 2026-10-02: il
progetto non dipende più da Qt. Test e benchmark sono target distinti, così una
build consumer non incorpora il laboratorio.
