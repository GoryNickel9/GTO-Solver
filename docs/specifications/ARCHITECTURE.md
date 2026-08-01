# Architettura

## Obiettivi

L'architettura mantiene separati regole, rappresentazione del gioco, solver,
persistenza e presentazione. Il core matematico deve funzionare senza Qt,
filesystem di prodotto o servizi remoti. GTOSD è C++20, locale e modulare.

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

1. Il config versionato viene parsato e validato.
2. Range e blocker vengono validati senza rinormalizzazione implicita.
3. Il public tree fisico viene costruito con azioni legali deterministiche.
4. L'isomorfismo globale e il canonical public DAG riducono duplicazioni senza
   perdere informazione privata.
5. `prepare_postflop_tree` crea il layout infoset/action e, se richiesto, gli
   indici analytics.
6. CFR+ aggiorna regret e strategy sum nel backend scelto.
7. A intervalli espliciti la best response certifica il profilo medio.
8. Checkpoint e soluzione possono essere salvati e ripresi solo con fingerprint
   compatibile.

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
inserire dipendenze nella GUI. Il root lock F10.4 pianificato è un esperimento
diagnostico test-only e non costituisce l'API di node locking di prodotto.

Il preflop richiederà action abstraction, decomposizione e stima risorse; il
multiway richiederà utility e metriche differenti. Nessuna delle due estensioni
può essere ottenuta reinterpretando silenziosamente il solver HU postflop.

## Toolchain e distribuzione

La build canonica usa CMake preset, vcpkg e MSVC su Windows. Le librerie
installano target CMake namespaced `gtosd::`; la GUI desktop è un'opzione di
build separata. Qt è confinato all'applicazione desktop. Test e benchmark sono
target distinti, così una build consumer non incorpora il laboratorio.
