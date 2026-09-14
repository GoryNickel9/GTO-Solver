# R6 — Decisione dopo V18 e gate della prossima candidata

Data: 2026-09-13  
Stato: `V18_SCIENTIFIC_REJECTED / V17_BASELINE_RESTORED / NEXT_RUN_NOT_AUTHORIZED`

## Decisione

V18 non prosegue come candidata principale. I file, le policy e gli audit restano disponibili
come evidenza riproducibile, ma il miglioramento esterno non compensa la regressione fra seed e
nell'audit EV.

| Metrica | V15/V17 | V18 | Variazione V18 |
| --- | ---: | ---: | ---: |
| WMAE media contro Monker | 13,9472 pp | 13,3838 pp | +4,04% |
| TV media contro Monker | 34,8679 pp | 33,4596 pp | +4,04% |
| TV fra seed, strategia media | 10,9453 pp | 13,0159 pp | −18,92% |
| Audit esatto Call/Fold, reach pubblica ≥1% | 7 casi | 8 casi | −14,29% |
| Perdita EV pesata sul deal root | 0,81330a | 0,83194a | −2,29% |

Un segno positivo indica un miglioramento. V18 non raggiunge il `10%` in TV o WMAE e peggiora
entrambe le misure interne scelte per stabilità ed EV. La seconda fase isolata ottiene inoltre
`16,6728 pp` di TV fra seed, contro `13,0159 pp` della miscela finale. Altre iterazioni con lo
stesso refinement K=1 non sono autorizzate.

## Baseline corrente

V17 torna a essere la baseline di ricerca. La sua strategia media è bit-identica a V15, mentre il
formato V17 espone anche la policy corrente completa. Questo permette di confrontare nello stesso
artefatto:

- strategia media e policy corrente;
- TV fra seed di entrambe;
- EV per azione e relativo errore standard;
- casi materialmente inferiori e perdita EV pesata.

La baseline non è una soluzione qualificata: non dispone di NashConv globale e il contratto
postflop Monker resta incompleto.

## Gate obbligatorio della prossima candidata

La prossima candidata dovrà superare tutti i controlli seguenti. Un miglioramento WMAE non potrà
compensare una TV fra seed peggiore.

| Controllo | Baseline V17 | Requisito |
| --- | ---: | ---: |
| TV fra seed, strategia media | 10,9453 pp | ≤9,8508 pp, miglioramento ≥10% |
| WMAE media contro Monker | 13,9472 pp | ≤13,9472 pp, nessuna regressione |
| TV fra seed, policy corrente | 11,1472 pp | ≤11,1472 pp, nessuna regressione |
| Audit esatto Call/Fold, reach pubblica ≥1% | 7 casi | ≤7 casi |
| Perdita EV pesata sul deal root | 0,81330a | ≤0,81330a |

Build, test, integrità, normalizzazione, contratto monetario e due run indipendenti da almeno 2M
restano condizioni necessarie. Smoke e giochi ridotti verificano la matematica, ma non decidono
la qualità della candidata.

## Proposta tecnica non autorizzata

La proposta successiva riparte dal trainer V17 e applica Common Random Numbers a tutte le azioni
dei nodi preflop durante il training principale. Ogni confronto d'azione usa lo stesso deal e la
stessa sequenza casuale compatibile, mentre il postflop continua ad apprendere. Non usa il
refinement congelato V18 e non consulta le frequenze Monker durante il training.

La proposta deve prima dimostrare sui giochi ridotti che ogni azione conserva la distribuzione
marginale corretta e che l'accoppiamento riduce la varianza delle differenze senza introdurre bias.
Se questi test passano, la decisione scientifica richiede due run complete da 2M con gli stessi
seed della baseline. Non sono autorizzati run V19, modifiche al codice V19 o promozioni nel viewer
senza nuovo consenso dell'utente.

## Viewer

Lo stato scientifico desiderato è:

- V17 disponibile e selezionata come baseline;
- V18 disponibile soltanto come `REJECTED / ARCHIVED EVIDENCE`;
- nessuna V19 visibile prima delle due run e del gate.

Questa decisione aggiorna la documentazione. Il viewer eseguibile non viene modificato in questa
attività e continua temporaneamente ad aprire V18 finché non viene sincronizzato separatamente.
