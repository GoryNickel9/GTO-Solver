# Gate monetario HU CO40 — ante morte e target live

Data: 2026-09-11  
Stato: `PASS`.

## Contratto validato

- Ogni giocatore versa `1a` di ante morta. Le due ante sono registrate in `initial_pot` e attribuite separatamente in `initial_pot_contributions`.
- BTN versa inoltre `1a` di button blind live. Alla root i commitment live sono CO `0a`, BTN `1a`; il pot è `3a` e il CO chiama `1a`.
- I target preflop `6a`, `10a`, `10,5a` e `14,5a` sono commitment live e non includono l'ante.
- L'all-in raggiunge `39a` live più `1a` di ante morta, cioè `40a` totali per giocatore.
- Il fold EV è il negativo di ante morta più commitment live già investito, al netto di eventuali restituzioni uncalled.

## Implementazione

1. `make_hu_preflop_state` separa le ante morte dal button blind live senza cambiare pot root, stack residui o costo del call.
2. `ActionConfig::aggressive_targets` rappresenta target monetari assoluti. Il tree builder non approssima più le size preflop tramite percentuali intere del pot.
3. Le re-raise `6a → 10,5a` e `10a → 14,5a` sono entrambe eccezioni incomplete esplicite. Conservano rispettivamente gli incrementi pieni precedenti di `5a` e `9a`.
4. Le fixture dichiarano `monetary_contract_revision=2`, `ante_accounting=dead_initial_pot_contribution` e `preflop_target_basis=live_commitment_excluding_dead_ante`.
5. Reference preflight, schema e validatore distinguono `target_live_commitment_ante` da `target_total_contribution_ante`.

## Ledger congelato dei 20 nodi decisionali

Le colonne `CO live` e `BTN live` escludono sempre l'ante morta. `Fold EV` è riferito al giocatore che deve agire.

| # | Action path | To act | CO live | BTN live | Fold EV |
|---:|---|---|---:|---:|---:|
| 1 | Root | CO | 0a | 1a | -1a |
| 2 | CO All-in | BTN | 39a | 1a | -2a |
| 3 | CO Call | BTN | 1a | 1a | — |
| 4 | CO Raise 10a | BTN | 10a | 1a | -2a |
| 5 | CO Raise 6a | BTN | 6a | 1a | -2a |
| 6 | CO Call → BTN All-in | CO | 1a | 39a | -2a |
| 7 | CO Call → BTN Raise 10a | CO | 1a | 10a | -2a |
| 8 | CO Call → BTN Raise 6a | CO | 1a | 6a | -2a |
| 9 | CO Raise 10a → BTN All-in | CO | 10a | 39a | -11a |
| 10 | CO Raise 10a → BTN Raise 14,5a | CO | 10a | 14,5a | -11a |
| 11 | CO Raise 6a → BTN All-in | CO | 6a | 39a | -7a |
| 12 | CO Raise 6a → BTN Raise 10,5a | CO | 6a | 10,5a | -7a |
| 13 | CO Call → BTN Raise 10a → CO All-in | BTN | 39a | 10a | -11a |
| 14 | CO Call → BTN Raise 10a → CO Raise 14,5a | BTN | 14,5a | 10a | -11a |
| 15 | CO Call → BTN Raise 6a → CO All-in | BTN | 39a | 6a | **-7a** |
| 16 | CO Call → BTN Raise 6a → CO Raise 10,5a | BTN | 10,5a | 6a | -7a |
| 17 | CO Raise 10a → BTN Raise 14,5a → CO All-in | BTN | 39a | 14,5a | -15,5a |
| 18 | CO Raise 6a → BTN Raise 10,5a → CO All-in | BTN | 39a | 10,5a | -11,5a |
| 19 | CO Call → BTN Raise 10a → CO Raise 14,5a → BTN All-in | CO | 14,5a | 39a | -15,5a |
| 20 | CO Call → BTN Raise 6a → CO Raise 10,5a → BTN All-in | CO | 10,5a | 39a | -11,5a |

Per ogni nodo il test verifica anche:

- `pot = 2a` di ante morte `+ CO live + BTN live`;
- `stack residuo + ante morta + commitment live = 40a` per entrambi;
- settlement rake-free a somma zero;
- esistenza del fold solo quando l'attore affronta una puntata live.

## Validazione

| Controllo | Risultato |
|---|---|
| Ledger dedicato | `PASS`, 295 assertion |
| Contratto albero CO40 | `PASS`, 25 assertion |
| Test HU completo | `PASS`, 9.653 assertion |
| Test config dichiarativa revision 2 | `PASS`, incluso rifiuto revision 1 |
| Core + Phase 1 | `2/2 PASS`, 2,90 s |
| Reference preflight | `PASS`, 81 classi, 630 combo, 5 azioni |
| Validatore JSON R0 | `PASS` |
| Sintassi schema e fixture JSON | `PASS`, 4 documenti |
| Albero | `PASS`, 58 nodi, 20 decisioni, 19 fold, 10 all-in |
| Fingerprint riferimento | `fnv1a64:f78898249087ee6d` |
| Fingerprint albero | `fnv1a64:a68337fa567aa2d9` |
| CTest label `preflop` | `13/13 PASS`, 509,45 s |

Il resource estimator è stato rivalidato sul nuovo albero. I resolver root canonici sono `322.199.856`; il piano River da 64 MiB usa `125.271` batch e mantiene il limite massimo di `67.085.568` byte.

## Artefatti obsoleti e sostituzione

I quattro solve V6/V7 da 2M originari usano l'albero `fnv1a64:0f9919d7d6030cf0` e restano obsoleti. Il validatore degli export li rifiuta con `STALE_TREE`. Il viewer usa ora quattro sostituti a 2M con l'albero corrente; risultati e gate sono in [V6_V7_2M_MONETARY_V2_RERUN_2026-09-11.md](V6_V7_2M_MONETARY_V2_RERUN_2026-09-11.md).

Gli EV non-root esportati da Monker restano dati esterni, ma la loro convenzione monetaria non è documentata e alcuni valori non coincidono con questo ledger. Solo la strategia root e il suo EV aggregato hanno oggi un confronto contrattuale esplicito.

Il runtime locale non include un motore JSON Schema Draft 2020-12. La sintassi dei due schema è stata verificata e i vincoli monetari sono coperti dal validatore R0 e dalla preflight C++; non viene dichiarata una validazione Draft 2020-12 completa.

## Decisione

Il contratto monetario precedente è invalidato. I confronti correnti accettano soltanto solve costruiti sul fingerprint `fnv1a64:a68337fa567aa2d9`.
