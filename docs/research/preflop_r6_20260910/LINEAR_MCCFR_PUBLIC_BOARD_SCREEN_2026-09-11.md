# Linear MCCFR: screening del campionamento public-board

Data: 2026-09-11

## Obiettivo e contratto

Confrontare `independent_physical_deal_v1` con `public_board_stratified` su due seed indipendenti. Gioco, Linear MCCFR, 500.000 iterazioni, batch `32`, partizione `32/128/512`, evaluator, feature, cache e otto worker restano congelati.

Non è stato modificato il codice di produzione. I due nuovi solve usano il suffisso `public_board`, con stdout, stderr e confronto `.comparison.json` associati.

## Risultati

| Sampling | Seed | WMAE (pp) | TV media (pp) | P95 TV (pp) | Errore max root (pp) | EV CO (ante) | SE EV | Solve (s) | Picco private campionato (B) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Fisico indipendente | 1 | **19,344159** | **48,360397** | 95,508322 | **26,387556** | -0,096547 | **0,117304** | 87,922163 | **340.541.440** |
| Public board | 1 | 19,390189 | 48,475472 | **95,258541** | 28,727678 | -0,159639 | 0,118947 | **86,158908** | 344.436.736 |
| Fisico indipendente | 2 | **19,756852** | **49,392130** | 95,958384 | 28,989995 | +0,124768 | **0,112607** | 88,554543 | **342.720.512** |
| Public board | 2 | 21,250540 | 53,126351 | **95,916781** | **28,530789** | -0,128518 | 0,117835 | **86,748276** | 344.666.112 |

Sui due seed il public-board porta:

- WMAE media da `19,550505` a `20,320365 pp`: `+0,769860 pp`;
- TV media da `48,876264` a `50,800911 pp`: `+1,924647 pp`;
- P95 medio da `95,733353` a `95,587661 pp`: `-0,145692 pp`;
- errore max root medio da `27,688775` a `28,629234 pp`: `+0,940459 pp`;
- tempo medio da `88,238353` a `86,453592 s`: `-2,02%`.

La WMAE fra le due policy passa da `12,075768` a `12,562132 pp`, quindi la stabilità peggiora di `0,486364 pp`.

## Validazione e gate

| Controllo | Esito | Evidenza |
|---|---|---|
| Due seed, unica variabile sampling | PASS | configurazioni e log congelati |
| Solve completati | PASS | entrambi `HU_PREFLOP_SOLVE=PASS`; stderr vuoti |
| 81 classi e strategie valide | PASS | probabilità finite, non negative e normalizzate |
| WMAE non peggiore su entrambi i seed | FAIL | seed 1 `+0,046030 pp`; seed 2 `+1,493688 pp` |
| Stabilità fra seed migliorata | FAIL | `+0,486364 pp` |
| Costo tempo/RAM | PASS | tempo `-2,02%`; RAM media circa `+0,85%` |
| Equivalenza Monker | NOT EVALUATED | configurazione esterna incompleta |

## Decisione

`public_board_stratified` è scartato dalla candidata. Il guadagno di tempo non compensa il peggioramento strategico e non riduce la dispersione fra seed.

Il risultato riporta l'attenzione sulla capacità dell'astrazione: `32/128/512` aveva migliorato entrambi i seed rispetto a `64/256/1024`. Il prossimo punto controllato è `16/64/256`, sempre a 500.000 iterazioni e su due seed. Un ulteriore miglioramento coerente autorizzerà il confronto delle capacità; un peggioramento individuerà il minimo locale osservato.
