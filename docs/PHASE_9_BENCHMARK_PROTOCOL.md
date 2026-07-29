# Fase 9 — Protocollo di confronto GUI

Aggiornato: 2026-07-29

## 1. Obiettivo

Il confronto Qt 6 Widgets / Dear ImGui docking usa lo stesso modello dati e
gli stessi workflow. Il benchmark non misura il solver e non modifica la
strategia: misura esclusivamente apertura, virtualizzazione, input e rendering
del prototipo F9.

## 2. Fixture comune

Il preset `windows-gui-release` usa `C:\tmp\gtosd-gui-build`. Il percorso
corto evita che i sorgenti generati di Qt superino i limiti di path ancora
incontrati da `cl.exe`; gli altri preset continuano a costruire sotto
`out/build`.

| Campo | Valore |
|---|---:|
| Nodi logici | 100.000 |
| Branching sintetico | 3 |
| Finestra materializzata | 10.000 righe |
| Matrice | 9×9, 81 classi |
| Combo fisiche | 630 |
| Viewport | 1280×800 |
| Warm-up | 30 frame |
| Frame misurati | 300 |
| Affinità processo | un logical processor per ciascuno di massimo 4 core fisici |

`VirtualTreeFixture` genera una riga deterministica in `O(depth)` senza
allocare i 100.000 nodi. Qt usa `QAbstractItemModel`; Dear ImGui usa
`ImGuiListClipper`. Il test comune materializza anche una finestra contigua da
10.000 righe per impedire che il gate sia superato con una fixture nominale ma
mai attraversata.

## 3. Percorsi grafici

| Candidato | Percorso primario | Fallback verificato |
|---|---|---|
| Qt 6 Widgets | paint/raster di `QWidget` su `QImage` | Raster software |
| Dear ImGui | Win32 + Direct3D 11 | Direct3D 11 WARP |

Il probe first-party prova D3D11 hardware e, separatamente, forza WARP. Un
fallback non viene dichiarato disponibile soltanto perché esiste nell'API:
deve creare realmente device e context durante i test.

Il prototipo ImGui usa un device `D3D11_CREATE_DEVICE_SINGLETHREADED`, perché
il frame loop è seriale, e disabilita l'antialias delle primitive UI
rettangolari. Testo, matrice, docking e focus non cambiano; la scelta riduce
l'overhead del fallback WARP ed è parte della configurazione misurata.

## 4. Metriche e gate

Ogni processo produce JSON con:

- framework e backend;
- nodi logici e celle;
- frame misurati;
- tempo medio per frame;
- p95 del tempo per frame;
- frame al secondo;
- scala DPI;
- esito del limite di affinità.

Il gate conservativo del prototipo è:

```text
fps >= 60
p95_frame_ms <= 16,666667
```

Il gate viene applicato a Qt raster, Dear ImGui D3D11 hardware e Dear ImGui
WARP. L'affinità sceglie un logical processor per ciascuno di quattro core
fisici, evitando di scambiare due sibling SMT per due core. Non simula
frequenza, IPC o GPU di un'altra macchina; hardware e sistema operativo della
misura finale devono quindi essere riportati nel completion report.

## 5. DPI, tastiera e automazione

Entrambi i candidati eseguono gli stessi dieci workflow a 100%, 150% e 200%:

1. apertura fixture;
2. visualizzazione root;
3. espansione root;
4. finestra virtuale da 10.000 righe;
5. focus matrice;
6. modifica peso;
7. navigazione da tastiera;
8. espansione combo con blocker;
9. render DPI;
10. esportazione metriche.

Qt esegue un render reale offscreen del widget tree; Dear ImGui genera e
invia il draw data al backend D3D11. I report E2E sono file JSON indipendenti
per ogni scala.

## 6. Apertura lazy `.gtsd`

Il test crea un container autenticato reale e apre soltanto indice e chunk
`CONFIG`. Registra:

- byte residenti dell'indice;
- byte logici totali;
- byte del solo config;
- chunk totali e caricati;
- flag `strategy_loaded`.

Il gate passa soltanto con un chunk caricato e strategia non caricata. La
rigenerazione del public tree dalla configurazione è distinta dal caricamento
dei grandi chunk strategy/regret.

## 7. Riproduzione

```powershell
cmake --preset windows-gui-release
cmake --build --preset windows-gui-release
ctest --preset windows-gui-release -L phase9
.\tools\run_f9_benchmarks.ps1
.\tools\verify_f9_install.ps1
```

I risultati misurati appartengono a `out/phase9` e non sono sorgenti
versionati. Il completion report conserva i valori necessari alla
riproducibilità e l'hash del commit o del worktree verificato.
