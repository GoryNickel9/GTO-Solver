# P0 — Contratto, snapshot e scaffolding

Data: 2026-09-15
Esito del gate: **PASS**
Roadmap: [PREFLOP_VECTOR_CFR_CODER_ROADMAP_2026-09-15.md](../PREFLOP_VECTOR_CFR_CODER_ROADMAP_2026-09-15.md), fase P0

## Identità

| Campo | Valore |
|---|---|
| Base di partenza | `main` a `55ed6ef` (documentazione del riavvio committata) |
| Tag di sicurezza | `preflop-legacy-es-2026-09-15` su `04aa687`, già presente in locale e su origin; non ricreato |
| Branch di integrazione | `feature/preflop-blueprint` (da `main`) |
| Branch di fase | `feature/preflop-blueprint-p0-scaffolding` |
| Worktree | `C:/tmp/gtosd-preflop-blueprint` (il working tree dell'utente non è stato toccato) |
| Commit del codice P0 | `ef6f691` |

## Ambiente

| Campo | Valore |
|---|---|
| CPU | Intel Core i3-10100F, 4 core fisici, 8 thread logici, 3,60 GHz |
| RAM | 34.294.738.944 B fisici; 13.335.101.568 B liberi al rilevamento |
| Sistema | Windows 11 Home 10.0.26200 |
| Compilatore | MSVC 19.51.36248 per x64 (toolset 14.51.36231, Visual Studio 18 Community) |
| CMake / Ninja | 4.4.0 / 1.13.2 |
| Dipendenze | nlohmann_json 3.12.0 dai pacchetti vcpkg già installati in `out/build/windows-release-current/vcpkg_installed`, riusati con `VCPKG_MANIFEST_INSTALL=OFF` |
| Python | 3.13.2 con `jsonschema` 4.26.0 |
| Build | `out/build/windows-release` nel worktree, Release, `/W4 /permissive- /WX` |
| clang-format | non disponibile nell'ambiente: `format-check` non eseguito; stile LLVM a 100 colonne mantenuto a mano |

## Consegne

1. **Target CMake.** `gtosd_card_abstraction` e `gtosd_preflop_blueprint` dietro l'opzione
   `GTOSD_BUILD_PREFLOP_BLUEPRINT` (default `ON`), con alias `gtosd::`, `gtosd_set_warnings`,
   install/export condizionati. Catena di dipendenze: core, equity, tree, card_abstraction;
   nlohmann_json privato.
2. **Controllo di dipendenza.** `tests/verify_preflop_blueprint_isolation.cmake` rifiuta nei
   CMakeLists dei nuovi target ogni link a solver, best_response, memory, postflop,
   postflop_subgame, preflop legacy, storage, isomorphism, gui_prototype e, nei sorgenti e header
   nuovi, ogni include di `gtosd/postflop/`, `gtosd/preflop/`, `gtosd/solver/`, `gtosd/memory/`,
   `gtosd/storage/`, `gtosd/isomorphism/` e ogni riferimento a `hu_preflop`. Registrato come test
   `gtosd_preflop_blueprint_dependency_check`.
3. **Schema e fixture.** `schemas/preflop_blueprint_game.schema.json`
   (`gtosd.preflop_blueprint_game.v1`): N giocatori con posizioni in ordine di azione, button
   blind esplicito, liste di open e response target a lunghezza variabile e indicizzate,
   size postflop da 1 a 3. Fixture congelate: `preflop_blueprint_hu10_full_v1.json`
   (equivalente a `hu_preflop_hu10_calibration_v1.json`), `preflop_blueprint_hu10_reduced_v1.json`
   (una sola size postflop 66 % più all-in, D17) e `preflop_blueprint_co40_v1.json` (equivalente a
   `hu_preflop_co40_game_v1.json`).
4. **Loader C++** `gtosd::preflop_blueprint::parse_game_config_json` con validazione (giocatori
   2–6, posizioni distinte con BTN ultimo, target strettamente crescenti e sotto lo stack,
   response sopra il rispettivo open, size crescenti, all-in obbligatorio, rake disabilitata),
   serializzazione canonica e fingerprint FNV-1a.
5. **Identità dell'astrazione** `gtosd::card_abstraction::library_identity()`:
   `gtosd.card_abstraction/0.1|short_deck_36|<fingerprint del ruleset della tabella a 7 carte>`.
6. **Validatore Python** `tools/validate_preflop_blueprint_fixtures.py` (JSON Schema 2020-12 più
   controlli incrociati), registrato come test con `SKIP_RETURN_CODE 77` se `jsonschema` manca.
7. **Diario** avviato in `PROGRESS_LOG.md`.

## Verifiche

| Controllo | Esito | Evidenza |
|---|---|---|
| Configure dell'intero progetto con l'opzione `ON` | PASS | 15,5 s, nessun avviso CMake |
| Build Release dei target nuovi e delle loro dipendenze, warning come errori | PASS | 19 passi, nessun warning |
| `gtosd_preflop_blueprint_scaffold_tests` | PASS | 73 asserzioni, 0,06 s |
| `gtosd_preflop_blueprint_dependency_check` | PASS | 4 sorgenti/header guardati |
| `gtosd_preflop_blueprint_fixture_schema` | PASS | 3 fixture, 0,71 s |
| Guardia su albero sintetico con link a `gtosd::postflop` | rifiutato (exit 1) | riga 46 dello script |
| Guardia su albero sintetico con include di `gtosd/preflop/hu_preflop.hpp` | rifiutato (exit 1) | riga 58 dello script |
| Guardia su albero sintetico pulito | PASS | `sources=2` |

Il test di scaffolding copre: identità dell'astrazione; valori delle tre fixture (stack, ante,
button blind, target, size, flag); uguaglianza fra HU10 ridotta e completa a meno di id e size;
round trip serializzazione → parsing con fingerprint invariato; ventuno casi di rifiuto con codice
d'errore atteso (JSON invalido, schema legacy, campo mancante, 1 o 7 giocatori, posizioni
incoerenti o duplicate, BTN non ultimo, response non allineate o non superiori all'open, open non
crescenti o non sopra il button blind, response che raggiunge lo stack, zero o quattro size, size
non crescenti, all-in disattivato, rake attiva, revisione monetaria errata, stack come stringa).

## Decisioni prese dall'agent

| Decisione | Motivazione |
|---|---|
| Branch di fase `feature/preflop-blueprint-p0-scaffolding` (trattino) invece di `feature/preflop-blueprint/p0-scaffolding` | git non ammette un ref `X/Y` se esiste il ref `X`; il branch di integrazione `feature/preflop-blueprint` esiste. Il registro D21 e la roadmap §9 sono stati allineati. |
| `maximum_postflop_sizes = 3` | fold/check/call più size più all-in devono stare nelle stesse sei azioni massime del motore esistente; le fixture usano al più tre size. Da rivedere in P4 se il layout compilato ammette di più. |
| `button_blind_units` esplicito e strettamente positivo | D9 richiede il button blind; renderlo esplicito evita l'identità implicita con l'ante del formato legacy. |
| Riuso dei pacchetti vcpkg installati nella build principale | evita una nuova installazione delle dipendenze nel worktree; il configure resta riproducibile con `VCPKG_MANIFEST_INSTALL=OFF`. |
| Test dello schema con `SKIP_RETURN_CODE 77` | su una macchina senza `jsonschema` il test viene saltato, non fallito; il loader C++ applica comunque tutte le regole. |

## Limiti

- I target sono di scaffolding: nessuna funzionalità di solver. Il loader è l'unica logica.
- Configure dell'intero progetto eseguito; build completa non eseguita (solo target nuovi e loro
  dipendenze). La suite completa verrà eseguita ai gate di merge in `main`.
- `format-check` non eseguibile (clang-format assente).

## Comandi riproducibili

Dal Developer Command Prompt di Visual Studio (x64), nel worktree:

```text
cmake -S . -B out\build\windows-release -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_CXX_COMPILER=cl.exe "-DCMAKE_MAKE_PROGRAM=<ninja.exe>" ^
  "-DCMAKE_TOOLCHAIN_FILE=<VCPKG_ROOT>\scripts\buildsystems\vcpkg.cmake" ^
  -DVCPKG_TARGET_TRIPLET=x64-windows -DVCPKG_MANIFEST_INSTALL=OFF ^
  "-DVCPKG_INSTALLED_DIR=<repo>\out\build\windows-release-current\vcpkg_installed" ^
  -DGTOSD_WARNINGS_AS_ERRORS=ON
cmake --build out\build\windows-release --target gtosd_card_abstraction gtosd_preflop_blueprint gtosd_preflop_blueprint_scaffold_tests
ctest --test-dir out\build\windows-release -L preflop_blueprint --output-on-failure
```
