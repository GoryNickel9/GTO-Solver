# ADR 0001 — Framework GUI desktop

Stato: **Accettato**
Data: 2026-07-29

## Contesto

La GUI HU deve navigare alberi grandi senza materializzarli, modificare la
matrice Short Deck 9×9, aprire container `.gtsd` a chunk e restare usabile su
Windows 10 con quattro core e 16 GB. Il core matematico e lo storage non devono
dipendere dal toolkit.

Questa decisione riguarda esclusivamente la GUI. Un backend grafico può usare
la GPU per disegnare, ma non autorizza né pianifica calcolo solver sulla GPU:
tree, CFR, regret/strategy, best response e certificazione restano CPU/RAM-only.

F9 confronta Qt 6 Widgets e Dear ImGui docking sullo stesso
`gtosd::gui_prototype`: fixture da 100.000 nodi, 81 classi, dieci workflow,
apertura autenticata del solo chunk `CONFIG`, DPI 100/150/200 e rendering con
un logical processor per ciascuno di quattro core fisici.

## Alternative

### Qt 6 Widgets

Vantaggi:

- modello/view nativo adatto alla virtualizzazione gerarchica;
- docking, focus, tastiera, High DPI e automazione widget disponibili nel
  toolkit;
- infrastruttura di accessibilità Windows esposta dai widget standard;
- rendering raster software disponibile senza dipendere dalla GPU;
- packaging Windows supportato da `windeployqt`.

Costi:

- runtime e pacchetto sensibilmente più grandi;
- build della dipendenza più lenta;
- per prodotto proprietario, LGPLv3 richiede linking dinamico e disciplina di
  packaging/relinking, oppure una licenza Qt commerciale;
- una matrice ad alte prestazioni richiede un widget custom.

### Dear ImGui docking

Vantaggi:

- runtime piccolo, integrazione diretta Direct3D 11 e licenza MIT;
- frame loop e disegno di grandi liste semplici da controllare;
- `ImGuiListClipper` evita di generare le righe fuori viewport;
- WARP fornisce un fallback D3D11 riproducibile.

Costi:

- il branch docking è dichiarato stabile e mantenuto, ma resta separato dal
  ramo principale;
- accessibilità desktop e semantica assistive-technology non sono fornite dal
  backend standard;
- controlli, stato persistente, automazione semantica e integrazione desktop
  richiedono più codice first-party;
- il modello immediate-mode sposta sul prodotto la responsabilità di molti
  comportamenti già presenti nei widget Qt.

## Decisione

Entrambi i candidati hanno superato i gate di frame time ed E2E. È selezionato
**Qt 6 Widgets** per la GUI di prodotto F10. Dear ImGui resta ammesso per
strumenti diagnostici e profiler interni, non come toolkit della GUI utente.

La decisione privilegia verificabilità dei workflow, navigazione tastiera,
accessibilità e costo totale di implementazione. Il vantaggio di throughput di
ImGui non compensa l'assenza di una superficie accessibile e automatizzabile
equivalente. Qt raster ha misurato 238,95 FPS con p95 5,49 ms; entrambi i
backend ImGui hanno superato lo stesso gate, quindi la scelta non nasconde un
fallimento prestazionale del candidato scartato.

## Vincoli conseguenti

1. Il core continua a esporre modelli C++ senza tipi Qt.
2. Le viste usano query/windowing; non copiano strategy e regret completi.
3. Il prodotto distribuisce Qt tramite DLL, mai staticamente sotto LGPL.
4. Il packaging installa avvisi e testi di licenza e permette la sostituzione
   delle librerie coperte.
5. Ogni controllo custom critico espone nome, ruolo, stato e focus accessibili.
6. La pipeline F10 mantiene E2E headless con il plugin `minimal` a 100%, 150%
   e 200%.

## Evidenza

- protocollo: [`PHASE_9_BENCHMARK_PROTOCOL.md`](PHASE_9_BENCHMARK_PROTOCOL.md);
- report di completamento:
  [`PHASE_9_COMPLETION_REPORT.md`](PHASE_9_COMPLETION_REPORT.md);
- metriche raw: `out/phase9/*.json`, artefatti di test non versionati;
- licenze: [`THIRD_PARTY_NOTICES.md`](../../../THIRD_PARTY_NOTICES.md).

## Fonti primarie

- [Qt licensing](https://doc.qt.io/qt-6/licensing.html)
- [Qt LGPL obligations](https://www.qt.io/development/open-source-lgpl-obligations)
- [Qt for Windows deployment](https://doc.qt.io/qt-6/windows-deployment.html)
- [Qt accessibility](https://doc.qt.io/qt-6/accessible.html)
- [Dear ImGui repository and MIT license](https://github.com/ocornut/imgui)
- [Dear ImGui docking documentation](https://github.com/ocornut/imgui/wiki/Docking)
- [Dear ImGui accessibility design discussion](https://github.com/ocornut/imgui/issues/7892)
