# R1 — Isolamento del trainer preflop

## Stato

`PASS` per il gate R1 della roadmap del 10 settembre 2026.

R1 separa il nuovo trainer preflop dal solver postflop standalone. Il target autonomo usa i contratti condivisi di core, equity e tree, ma non include header, sorgenti o librerie del solver postflop. Il percorso storico di decomposizione resta disponibile attraverso un adapter esplicito.

L'audit richiesto prima di R2 ha trovato un falso positivo nel primo smoke test: l'eseguibile costruiva soltanto l'albero e non istanziava il percorso `solve_hu_preflop_sampled`. Il collegamento del solve esponeva cinque simboli ereditati dalla decomposizione legacy. R1 è stato corretto prima di procedere: le operazioni generiche sul blueprint sono ora nel core preflop, le tre bridge che consumano contratti postflop sono in `hu_preflop_legacy_bridge.cpp`, e il test di isolamento esegue un solve fisico completo minimo.

## Snapshot e confini

- R0 è stato committato in `39eb45b` (`feat-preflop-freeze-HU-CO40-R0-contract`).
- Il working tree conteneva già modifiche e file non tracciati; non sono stati ripuliti, ripristinati o inclusi nel commit R0.
- R1 modifica soltanto i confini preflop, i target CMake dedicati, il test legacy che consuma le API spostate e gli artefatti di questa directory.
- `libs/postflop/`, `include/gtosd/postflop/` e `libs/postflop_subgame/` non sono stati modificati per R1. Le modifiche preesistenti nel working tree restano separate.

## Implementazione

Il nuovo percorso è esposto da `gtosd/preflop/hu_preflop.hpp`. Le dichiarazioni che richiedono boundary values o configurazioni postflop sono state spostate in `gtosd/preflop/hu_preflop_legacy.hpp`.

Il target autonomo `gtosd_preflop_trainer` compila:

- `config.cpp`;
- `hu_preflop.cpp`;
- `hu_preflop_persistence.cpp`;
- `hu_preflop_solver.cpp`.

Le sue dipendenze sono `gtosd::core`, `gtosd::equity`, `gtosd::tree` e `nlohmann_json`. Non dipende da `gtosd_postflop`, `gtosd_postflop_preflop_experimental` o `gtosd_postflop_subgame`.

Il target legacy `gtosd_preflop` conserva decomposizione e valutazione campionata; il relativo adapter include ora esplicitamente le API postflop legacy. Il benchmark di decomposizione e `gtosd_hu_preflop_tests` continuano a usare quel percorso senza trascinarlo nel trainer autonomo.

Il test `gtosd_hu_preflop_trainer_isolation_tests` non si limita più al tree builder. Esegue due iterazioni di external sampling con rappresentazione `ExactPhysical`, valida il blueprint prodotto e fallisce al link se il solve reintroduce una dipendenza dalla decomposizione legacy.

## Verifiche

| Verifica | Esito | Evidenza |
|---|---|---|
| Build del trainer e del test di isolamento | `PASS` | Release build dei target `gtosd_hu_preflop_trainer` e `gtosd_hu_preflop_trainer_isolation_tests` |
| Esecuzione trainer | `PASS` | fingerprint `fnv1a64:0f9919d7d6030cf0`, 58 nodi, 20 decisioni, 9 entry postflop; solve `external_sampling_v2_opponent_pass_average`, 422 infoset |
| Test di isolamento | `PASS` | solve fisico minimo, blueprint valido, nessun simbolo legacy irrisolto |
| Verifica statica delle dipendenze | `PASS` | `HU_PREFLOP_TRAINER_DEPENDENCY_CHECK=PASS` |
| CTest R1 dopo la correzione | `PASS` | 3/3 in 0,13 s: trainer, isolamento, dipendenze |
| Build dei consumer legacy | `PASS` | `gtosd_hu_preflop_tests`, `gtosd_hu_preflop_decomposition` |
| Test legacy completo | `PASS` | 1/1, 483,32 s |

Comandi principali:

```text
cmake --build out\\build\\codex-release-20260907 --config Release --target gtosd_hu_preflop_trainer gtosd_hu_preflop_trainer_isolation_tests
ctest --test-dir out\\build\\codex-release-20260907 -C Release --output-on-failure -R trainer
ctest --test-dir out\\build\\codex-release-20260907 -C Release --output-on-failure -R hu_preflop_tests
```

Il build Release ha richiesto l'ambiente `VsDevCmd.bat -arch=x64`. La riconfigurazione CMake ha richiesto accesso al registry vcpkg locale; il primo tentativo non elevato è stato bloccato dai permessi del registry, mentre il build autorizzato è terminato correttamente.

## Limiti

R1 dimostra isolamento e compatibilità di compilazione/esecuzione. Non dimostra ancora correttezza dell'averaging external-sampling, convergenza, qualità della strategia, fattibilità CPU/RAM o qualificazione entro due ore. Questi aspetti appartengono a R2 e alle fasi successive.

## Prossima fase

R2 deve definire e correggere il contratto di sampling e averaging, usando il controesempio conservato e giochi ridotti prima di interpretare le strategie del trainer.
