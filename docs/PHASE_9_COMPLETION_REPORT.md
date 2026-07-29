# Fase 9 — Completion report

Data: 2026-07-29
Versione: 0.9.0
Stato: **completata localmente**

## 1. Esito

F9 confronta due prototipi Windows costruiti sullo stesso modello first-party:

- Qt 6 Widgets con `QAbstractItemModel`, dock widget e raster software;
- Dear ImGui docking con Win32, Direct3D 11 e fallback WARP forzabile.

Entrambi usano una fixture gerarchica da 100.000 nodi generata on demand, la
matrice Short Deck 9×9 esatta e l'apertura autenticata lazy di `.gtsd`. Tutti
i gate locali sono verdi e Qt 6 Widgets è selezionato per F10.

## 2. Implementazione

### Modello condiviso

`gtosd::gui_prototype` contiene:

- `VirtualTreeFixture`, con lookup deterministico `O(depth)` e pagine
  materializzate a richiesta;
- `RangeMatrixModel`, 81 classi e masse fisiche pair/suited/offsuit 6/4/12;
- blocker sul board senza rinormalizzazione silenziosa;
- navigazione tastiera a coordinate stabili;
- apertura `.gtsd` che legge indice e solo chunk `CONFIG`;
- probe first-party D3D11 hardware/WARP e inventario hardware;
- contratto comune dei dieci workflow E2E.

### Qt 6 Widgets

Il prototipo usa tre `QDockWidget`, un `QTreeView` virtualizzato, un widget
custom per la matrice e interfacce accessibili interrogate in test. L'E2E
renderizza realmente il widget tree con il plugin headless `minimal` a 100%,
150% e 200%. Il benchmark rasterizza 300 frame su `QImage` dopo 30 frame di
warm-up.

### Dear ImGui

Il prototipo usa il branch docking, `ImGuiListClipper`, backend Win32/DX11 e
fallback `D3D_DRIVER_TYPE_WARP`. Il benchmark attende una query
`D3D11_QUERY_EVENT` a ogni frame: misura il completamento del rendering e non
soltanto l'invio asincrono dei comandi. Il device è single-thread e
l'antialias delle primitive rettangolari è disabilitato nella configurazione
misurata.

## 3. Gate

| Gate F9 | Stato | Evidenza |
|---|---|---|
| 60 FPS / interazione fluida | PASS locale | Qt 238,95 FPS; DX11 4.362,19 FPS; WARP 62,20 FPS, tutti con p95 ≤ 16,666667 ms |
| Root senza full load | PASS | container reale, 8 chunk totali, 1 caricato, strategy non caricata |
| Workflow E2E automatizzabile | PASS | 10 workflow × 3 scale DPI × 2 prototipi |
| Licenza commerciale compatibile | PASS condizionato | ImGui MIT; Qt DLL LGPLv3 con obblighi di packaging, oppure licenza commerciale |

## 4. Risultati prestazionali

Macchina:

| Campo | Valore |
|---|---|
| CPU | Intel Core i3-10100F |
| Frequenza nominale | 3.600 MHz |
| Core fisici / logical processor | 4 / 8 |
| Affinità benchmark | 1 logical processor per ognuno dei 4 core fisici |
| RAM fisica | 34.294.738.944 B (31,94 GiB) |
| Windows build | 26200 |

| Prototipo | Backend | Mean frame | p95 | FPS | Gate |
|---|---|---:|---:|---:|---|
| Qt 6 Widgets | raster software | 4,184911 ms | 5,4879 ms | 238,954 | PASS |
| Dear ImGui | Direct3D 11 hardware | 0,229243 ms | 0,4096 ms | 4.362,190 | PASS |
| Dear ImGui | Direct3D 11 WARP | 16,078154 ms | 16,2958 ms | 62,196 | PASS |

L'affinità usa quattro core fisici senza sibling SMT, ma non simula frequenza,
IPC, cache, RAM o GPU di un PC differente. La misura certifica questo host e
il fallback software WARP; non viene presentata come esecuzione su una CPU
2 GHz con 16 GB. La qualificazione su hardware minimo esatto resta un gate di
release F10, mentre Qt conserva un margine locale ampio.

## 5. Validazione

| Controllo | Risultato |
|---|---|
| Test first-party F9 | PASS, 44 asserzioni |
| Qt E2E DPI 100/150/200 | PASS, 3/3 |
| ImGui E2E DPI 100/150/200 | PASS, 3/3 |
| D3D11 hardware + WARP forzato | PASS nel test comune |
| Benchmark con GPU synchronization | PASS, 3/3 backend |
| Install tree GUI e notice | PASS, 21 artefatti e smoke installato Qt/ImGui |
| Regression suite Release | PASS, 19/19 inclusa F4 exhaustive |
| Focused MSVC ASan F9 | PASS, 1/1 |
| Format check / `git diff --check` | PASS / PASS |

Identità della verifica:

- commit base: `98cb961bc9d3e93fc85c09f90cbd3b9674b9ff6e`;
- 35 file modificati o non tracciati nel manifest di verifica;
- SHA-256 del manifest `BASE + (git blob, path)`, escluso questo report:
  `ec55f9cdb46a651273048cb4e86b3c0690c4486f9fa263abf26f4ffc754b55ee`.

## 6. Licenze e packaging

Dear ImGui 1.92.8#1 è MIT. Qt Base 6.11.1 viene distribuito come DLL:
l'uso proprietario sotto LGPLv3 richiede notice, testi, possibilità di
sostituzione/relinking e source offer quando applicabile; una licenza
commerciale Qt è alternativa. Il packaging copia i notice autorevoli vcpkg di
Qt, ImGui e delle dipendenze runtime e verifica DLL, `qwindows`, `qminimal` e
smoke E2E dall'install tree. Il pacchetto vcpkg non include cataloghi di
traduzione Qt; il prototipo non usa stringhe tradotte.

## 7. Decisione

L'ADR [`ADR_0001_GUI_FRAMEWORK.md`](ADR_0001_GUI_FRAMEWORK.md) è accettato:
Qt 6 Widgets è il framework F10. ImGui resta limitato a strumenti diagnostici.

## 8. Limiti

- il prototipo non è la GUI prodotto F10;
- non avvia un solve e non implementa home, recenti o range editor completo;
- l'albero è una fixture logica; l'apertura `.gtsd` usa però formato,
  autenticazione e config reali;
- l'accessibilità del widget custom Qt è verificata al livello di interfaccia,
  nome e focus; audit screen-reader completi appartengono alla GUI prodotto;
- non viene attribuita a ImGui una superficie accessibile equivalente.
- la misura locale non sostituisce una qualifica su macchina esattamente
  4-core/2 GHz/16 GB.

## 9. Riproduzione

```powershell
cmake --preset windows-gui-release
cmake --build --preset windows-gui-release
ctest --preset windows-gui-release -L phase9
.\tools\run_f9_benchmarks.ps1
.\tools\verify_f9_install.ps1
```
