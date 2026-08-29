# Architettura

## Obiettivi

L'architettura mantiene separati regole, rappresentazione del gioco, solver,
persistenza e presentazione. Il core matematico deve funzionare senza Qt,
filesystem di prodotto o servizi remoti. GTOSD è C++20, locale e modulare.

## Modello di esecuzione CPU/RAM

Il solver usa esclusivamente CPU e RAM di sistema. Non esiste e non verrà
introdotto un backend GPU per costruzione dell'albero, canonicalizzazione,
traversal CFR, regret/strategy update, best response, certificazione o analisi
della soluzione. Sono vietati CUDA, ROCm, OpenCL, Vulkan Compute,
DirectCompute, compute shader e deleghe equivalenti ad acceleratori.

La GUI può utilizzare una GPU soltanto per disegnare l'interfaccia. Tale
rendering è esterno al core matematico, non riceve workload del solver e non può
essere incluso in un benchmark di solving. Tutte le stime di capacità e i gate
prestazionali del solver devono quindi essere soddisfatti con CPU e RAM.

## Moduli correnti

```text
core -> equity -> tree -> isomorphism
  \        \        \          \
   \        +--------+-----------+-> postflop -> storage
    +-----------------> solver -> best_response       \
                         memory ------------------------+-> CLI / Qt GUI
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
| `gto_gui` | workflow desktop Qt 6 Widgets |

Le API pubbliche vivono sotto `include/gtosd`; le implementazioni sotto
`libs/*`. Le applicazioni possono dipendere dalle librerie, mai il contrario.

## Flusso del solve postflop

> **Migrazione architetturale accettata e avviata.**
> [`ADR_0002_MEMORY_BOUNDED_EXACT_SOLVER.md`](../ADR_0002_MEMORY_BOUNDED_EXACT_SOLVER.md)
> sostituisce come target il flusso fisico descritto sotto. Il compilatore
> layout-only è implementato; finché il traversal non supera i gate
> differenziali, questo paragrafo continua a descrivere il
> codice production corrente e non la capacità futura.

1. Il config versionato viene parsato e validato.
2. Range e blocker vengono validati senza rinormalizzazione implicita.
3. Il public tree fisico viene costruito con azioni legali deterministiche.
4. L'isomorfismo lossless riduce infoset duplicati; il target migrato usa un
   canonical chance tree senza unificare history arbitrarie.
5. `prepare_postflop_tree` crea il layout infoset/action e, se richiesto, gli
   indici analytics.
6. CFR+ aggiorna regret e strategy sum nel backend scelto.
7. A intervalli espliciti la best response certifica il profilo medio.
8. Checkpoint e soluzione possono essere salvati e ripresi solo con fingerprint
   compatibile.

Il target approvato costruisce direttamente un canonical chance tree con
coordinate private player-local, mapping inversi dei CFV e subtree disgiunti.
Non unifica history arbitrarie in un DAG globale. Il traversal owner-computes
senza delta `O(worker * actions)` e exact BR streaming. CFR-D è un livello successivo
per i giochi che non rispettano il preflight dopo la riduzione lossless; non è
un sostituto implicito dell'algoritmo postflop corrente.

La preparazione è riutilizzabile: benchmark e GUI possono eseguire più tranche
senza ricostruire la topologia. L'analytics è opt-in per non gonfiare il path di
solving quando non serve.

## Confini e invarianti

- `core` non conosce solver, GUI o storage.
- `equity` non decide frequenze o azioni.
- `tree` contiene solo stato pubblico; le combo private entrano negli infoset.
- `isomorphism` applica una permutazione globale, mai una canonicalizzazione
  board-only che rompa i blocker.
- `postflop` non campiona e non bucketizza nel percorso exact.
- `postflop`, `solver` e `best_response` non delegano calcolo a GPU o altri
  acceleratori: CPU e RAM sono l'unico backend autorizzato.
- `storage` non modifica semantica o precisione del solve.
- GUI e CLI non ricalcolano metriche con formule divergenti dal core.

## Persistenza e sicurezza

Checkpoint e `.gtsd` hanno ruoli distinti. Il checkpoint contiene lo stato
necessario al resume; `.gtsd` è il contenitore autenticato e indicizzato per il
prodotto. Il writer usa compressione Zstandard, secretstream
XChaCha20-Poly1305, verifica prima del replace e scrittura atomica.

La chiave è fornita dal chiamante e non viene salvata nel file. Il catalogo
SQLite esterno indicizza le soluzioni, ma non è necessario per aprirle.

## Estensioni previste

Node locking, preflop HU e multiway devono aggiungere moduli o contratti senza
inserire dipendenze nella GUI. Il root lock F10.4 implementato è un esperimento
diagnostico test-only e non costituisce l'API di node locking di prodotto.

Il preflop richiederà action abstraction, decomposizione e stima risorse; il
multiway richiederà utility e metriche differenti. Nessuna delle due estensioni
può essere ottenuta reinterpretando silenziosamente il solver HU postflop.

## Toolchain e distribuzione

La build canonica usa CMake preset, vcpkg e MSVC su Windows. Le librerie
installano target CMake namespaced `gtosd::`; la GUI desktop è un'opzione di
build separata. Qt è confinato all'applicazione desktop. Test e benchmark sono
target distinti, così una build consumer non incorpora il laboratorio.
