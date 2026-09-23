# Azioni legali per scenario

File generati da `gtosd_preflop_blueprint_game --config benchmarks/suite/fixtures/<scenario>.json --actions`
(build candidata del 2026-09-22, capacita' a 32 bit). Ogni file contiene il report JSON dell'albero
seguito dalla riga di stato `PREFLOP_BLUEPRINT_GAME=PASS`: `preflop_decisions` elenca per ogni nodo
di decisione preflop l'attore, il livello di aggressione, se il piatto e' limpato e le azioni legali
con importo in unita' (1 ante = 10.000 unita') e flag all-in; `postflop_action_signatures` conta le
firme delle azioni per street. Rigenerare con `python tools/preflop_suite/suite.py resolve` (fixture)
e con il comando sopra (liste).

| Scenario | Albero (fnv1a64) | Nodi | Decisioni | Decisioni preflop | Archi all-in preflop | Etichette preflop | Decisioni flop/turn/river | Firme postflop |
|---|---|---:|---:|---:|---:|---|---|---:|
| `HU10` | `d7b31d6f2cb759fc` | 193 | 80 | 8 | 4 | fold, call, check, all_in, bet_4, raise_5 | 16/24/32 | 12 |
| `HU10-FULL` | `bc9e7b35ad8c021d` | 1501 | 584 | 8 | 4 | fold, call, check, all_in, bet_4, raise_5 | 48/160/368 | 29 |
| `HU20` | `f5b432de223744cc` | 571 | 228 | 8 | 4 | fold, call, check, all_in, bet_4, raise_5 | 28/68/124 | 17 |
| `HU20-2` | `7b59c6cacc9f5da5` | 604 | 242 | 10 | 5 | fold, call, check, all_in, bet_4, raise_16, raise_5 | 32/72/128 | 18 |
| `HU30` | `17dc5c7d07ea30c2` | 604 | 242 | 10 | 5 | fold, call, check, all_in, bet_4, raise_16, raise_5 | 32/72/128 | 18 |
| `HU40` | `18d08f453034ac0f` | 604 | 242 | 10 | 5 | fold, call, check, all_in, bet_4, raise_16, raise_5 | 32/72/128 | 18 |
| `HU40-FULL` | `d6c10723d35b9503` | 26878 | 9958 | 10 | 5 | fold, call, check, all_in, bet_4, raise_16, raise_5 | 300/1988/7660 | 130 |
