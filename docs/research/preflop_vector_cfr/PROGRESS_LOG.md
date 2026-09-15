# Diario dell'agent coder: solver preflop vettoriale

Roadmap: [PREFLOP_VECTOR_CFR_CODER_ROADMAP_2026-09-15.md](../PREFLOP_VECTOR_CFR_CODER_ROADMAP_2026-09-15.md)
Registro decisioni: [PREFLOP_ARCHITECTURE_DECISION_LOG.md](../PREFLOP_ARCHITECTURE_DECISION_LOG.md)

Regole del diario: le voci non si cancellano; una correzione è una nuova voce che rimanda alla
precedente. Un fallimento si registra prima di tentare la correzione. Aggiornare a fine di ogni
sessione, a ogni gate e a ogni dubbio bloccante.

## 1. Stato corrente

| Campo | Valore |
|---|---|
| Fase in corso | nessuna (roadmap congelata, implementazione non avviata) |
| Ultimo gate | — |
| Branch di lavoro | — |
| Commit di partenza | `main` dopo i commit di documentazione del 2026-09-15 (`bb45824` o successivo); il tag `preflop-legacy-es-2026-09-15` è su `04aa687` |
| Prossimo passo | P0: snapshot, tag, target CMake vuoti, fixture |

## 2. Registro dei gate

| Fase | Esito | Data | Commit | Report |
|---|---|---|---|---|
| P0 Contratto e scaffolding | NOT_RUN | | | |
| P1 Canonicalizzazione e cataloghi | NOT_RUN | | | |
| P2 Risorse esatte | NOT_RUN | | | |
| P3 Clustering e tabelle bucket | NOT_RUN | | | |
| P4 Modello di gioco e albero compilato | NOT_RUN | | | |
| P5 Kernel vettoriale HU | NOT_RUN | | | |
| P6 Trainer con campionamento del board | NOT_RUN | | | |
| P7 Certificatore board-major | NOT_RUN | | | |
| P8 Export, query, comparatore, viewer | NOT_RUN | | | |
| P9 Qualificazione CO40 e archiviazione | NOT_RUN | | | |
| P10 Conteggio alberi 3-way | NOT_RUN | | | |

Esiti ammessi: `PASS`, `FAIL`, `INCONCLUSIVE`, `NOT_RUN`.

## 3. Diario

Formato di ogni voce:

```text
### AAAA-MM-GG — Px — titolo breve
Fatto: ...
Comandi: ...
Risultati: numeri, tempi, memoria, fingerprint
Fallimenti: cosa, causa identificata o ipotesi, cosa si è provato
Dubbi: ...
Prossimo passo: ...
```

### 2026-09-15 — P0 — creazione del diario

Fatto: creato il template del diario insieme alla roadmap. Nessun codice scritto.
Comandi: nessuno.
Risultati: nessuno.
Fallimenti: nessuno.
Dubbi: nessuno.
Prossimo passo: P0.

## 4. Domande per l'utente

| # | Data | Domanda | Stato | Risposta |
|---|---|---|---|---|

## 5. Decisioni prese dall'agent

| # | Data | Fase | Decisione | Motivazione |
|---|---|---|---|---|
