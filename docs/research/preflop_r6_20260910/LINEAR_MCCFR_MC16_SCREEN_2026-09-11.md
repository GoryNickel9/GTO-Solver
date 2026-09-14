# Linear MCCFR: screening MC8 contro MC16

Data: 2026-09-11

## Obiettivo e contratto

Verificare se raddoppiare da 8 a 16 i campioni equity usati nelle feature dei bucket riduce l'errore e la variabilità. Il confronto usa due seed a 500.000 iterazioni, partizione `32/128/512`, batch `32`, sampling fisico indipendente e otto worker.

Non è stato modificato il codice di produzione. I nuovi artefatti usano il suffisso `mc16` e includono JSON, stdout, stderr e confronto Monker.

## Risultati

| Feature | Seed | WMAE (pp) | TV media (pp) | P95 TV (pp) | Errore max root (pp) | EV CO (ante) | SE EV | Solve (s) | Picco private campionato (B) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| MC8 | 1 | 19,344159 | 48,360397 | 95,508322 | 26,387556 | -0,096547 | 0,117304 | **87,922163** | **340.541.440** |
| MC16 | 1 | **19,219461** | **48,048652** | **93,933983** | **26,237432** | +0,015227 | **0,115534** | 131,479327 | 380.129.280 |
| MC8 | 2 | **19,756852** | 49,392130 | 95,958384 | **28,989995** | +0,124768 | **0,112607** | **88,554543** | **342.720.512** |
| MC16 | 2 | 19,743204 | **49,358011** | **94,854120** | 29,637952 | -0,039517 | 0,113280 | 132,901683 | 381.337.600 |

Medie sui due seed:

- WMAE: MC8 `19,550505`, MC16 `19,481332 pp`, miglioramento `0,069173 pp`;
- TV: MC8 `48,876264`, MC16 `48,703331 pp`, miglioramento `0,172933 pp`;
- P95: MC8 `95,733353`, MC16 `94,394052 pp`, miglioramento `1,339301 pp`;
- errore max root: MC8 `27,688775`, MC16 `27,937692 pp`, peggioramento `0,248917 pp`;
- solve: MC8 `88,238353`, MC16 `132,190505 s`, costo `+49,81%`;
- picco private: MC8 `341.630.976`, MC16 `380.733.440 B`, costo `+11,45%`.

## Stabilità

| Feature | WMAE fra policy seed 1/2 (pp) |
|---|---:|
| MC8 | **12,075768** |
| MC16 | 14,616508 |

MC16 aumenta la distanza fra seed di `2,540739 pp`, pari al `21,04%`.

## Validazione e gate

| Controllo | Esito | Evidenza |
|---|---|---|
| Due seed, unica variabile equity samples | PASS | MC8/MC16 con contratto altrimenti identico |
| Solve e log | PASS | entrambi i nuovi solve passano; stderr vuoti |
| Strategie valide | PASS | 81 classi e probabilità normalizzate |
| Miglioramento WMAE medio almeno 0,5 pp | FAIL | `0,069173 pp` |
| Stabilità fra seed non peggiore | FAIL | `+21,04%` |
| Errore max root non peggiore | FAIL | media `+0,248917 pp` |
| Costo tempo massimo +15% | FAIL | `+49,81%` |
| Equivalenza Monker | NOT EVALUATED | configurazione esterna incompleta |

## Decisione

MC16 è scartato. MC8 resta selezionato: permette più iterazioni nello stesso budget e produce policy più stabili fra seed.

La configurazione candidata per il gate lungo è quindi Linear MCCFR, MC8, batch `32`, partizione `32/128/512`, sampling fisico indipendente e nessuna opponent-value baseline. Il prossimo controllo è a 4 milioni di iterazioni su due seed; non si procede a 8M senza valutarne qualità e stabilità.
