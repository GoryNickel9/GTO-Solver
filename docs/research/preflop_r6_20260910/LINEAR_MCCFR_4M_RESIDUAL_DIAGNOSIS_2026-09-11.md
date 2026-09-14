# Linear MCCFR 4M: diagnosi del residuo strategico

Data: 2026-09-11

## Obiettivo

Separare il rumore del training dal bias strutturale dopo il gate 4M. La diagnosi usa la media delle due policy root, la loro distanza e il riferimento Monker normalizzato. Non modifica codice, fixture o strategie.

## Errore per azione

| Azione | MAE per combo della policy media (pp) | Differenza media fra seed (pp) | Frequenza Monker aggregata | Frequenza Solver aggregata | Delta aggregato |
|---|---:|---:|---:|---:|---:|
| All-in | 23,3362 | 8,2484 | 40,7790% | 17,7305% | **-23,0485 pp** |
| Raise 6 | 11,2369 | 9,3552 | 2,4078% | 11,8643% | +9,4565 pp |
| Raise 10 | 10,2231 | 6,6294 | 4,7194% | 6,9188% | +2,1994 pp |
| Call | 21,8123 | 14,3127 | 7,5667% | 28,7854% | **+21,2187 pp** |
| Fold | 10,4655 | 5,4536 | 44,5270% | 34,7010% | -9,8259 pp |

La policy locale sostituisce soprattutto all-in Monker con call e, in parte, raise-to 6. Il bilancio aggregato non è una conseguenza di AA o di una singola classe marginale.

## Stabilità del bias con le iterazioni

| Iterazioni | Azione | Delta aggregato medio | Semirange fra seed |
|---:|---|---:|---:|
| 500k | All-in | -27,6888 pp | 1,3012 pp |
| 4M | All-in | **-23,0485 pp** | **0,0013 pp** |
| 500k | Call | +25,1413 pp | 0,5127 pp |
| 4M | Call | **+21,2187 pp** | 1,8067 pp |
| 500k | Raise 6 | +8,3233 pp | 1,4567 pp |
| 4M | Raise 6 | +9,4564 pp | 0,3329 pp |
| 500k | Raise 10 | +6,6956 pp | 1,2216 pp |
| 4M | Raise 10 | +2,1993 pp | 0,2412 pp |
| 500k | Fold | -12,4715 pp | 1,0236 pp |
| 4M | Fold | -9,8258 pp | 1,8998 pp |

L'all-in aggregato dei due seed converge praticamente allo stesso valore, ma resta oltre 23 punti sotto Monker. Questa è evidenza diretta contro l'ipotesi che il residuo principale sia soltanto rumore MCCFR.

## Combo con residuo sistematico maggiore

`TV bias` confronta la policy media con Monker. `TV seed` confronta le due policy locali. I valori Monker per combo provengono dalla tabella normalizzata a due decimali; servono alla diagnosi, non sostituiscono il comparatore ufficiale.

| Combo | TV bias (pp) | TV seed (pp) | Azione col bias maggiore |
|---|---:|---:|---|
| QJo | 96,14 | 6,48 | All-in |
| J8o | 87,79 | 3,49 | Fold |
| T8o | 90,51 | 11,36 | Fold |
| K9o | 79,81 | 0,16 | Call |
| T9o | 86,93 | 15,29 | All-in |
| K6s | 85,29 | 20,45 | Fold |
| JTs | 87,51 | 28,41 | All-in |
| T8s | 84,51 | 32,85 | All-in |
| Q7s | 73,53 | 11,77 | Fold |
| KK | 82,11 | 30,91 | Raise 6 |

QJo, J8o e K9o sono i segnali più forti: errore enorme e differenza fra seed piccola. Aumentare semplicemente le iterazioni rende questi valori più stabili senza garantire che si avvicinino a Monker.

## Interpretazione

Le cause ancora compatibili con i dati sono:

1. memoria imperfetta della rappresentazione `current_observation`, che può fondere history con valori d'azione diversi;
2. feature/bucket che non preservano abbastanza bene il valore relativo di all-in e call;
3. albero postflop Monker diverso da quello locale, informazione esterna ancora mancante;
4. altra astrazione history-dependent o metodo di bucketing Monker ignoto.

Le regole root, i contributi forzati, il rake zero e l'albero preflop sono già confermati. Il fold spurio di AA e l'all-in spurio di 76o sono stati eliminati da Linear MCCFR, quindi non spiegano più lo scarto globale.

## Gate diagnostico

| Ipotesi | Esito |
|---|---|
| Rumore MCCFR come causa principale | RESPINTA per il delta all-in aggregato |
| Più iterazioni come prossimo intervento | RESPINTO: costo alto e bias stabile |
| Bias strutturale della continuazione/astrazione | SUPPORTATO, causa specifica non ancora isolata |
| Equivalenza col postflop Monker | NOT EVALUATED |

## Decisione

Non eseguire 8M con la stessa rappresentazione. Il prossimo test cambia una sola proprietà strutturale: conserva la history dei bucket postflop, prima su scala breve e con budget esplicito. Se riduce il bias all-in/call senza esplodere RAM, viene replicato; altrimenti si prova il perfect recall soltanto su un caso ridotto.
