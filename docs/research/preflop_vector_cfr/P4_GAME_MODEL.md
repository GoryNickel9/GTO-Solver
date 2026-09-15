# P4 — Modello di gioco e albero compilato

Data: 2026-09-15. Branch di fase: `feature/preflop-blueprint-p4-game-model`. Esito del gate: **PASS**.

## 1. Cosa è stato prodotto

| Componente | File | Contenuto |
|---|---|---|
| Regole N-player | `include/gtosd/preflop_blueprint/game_model.hpp`, `libs/preflop_blueprint/src/game_model.cpp` | stato preflop a N giocatori (`make_preflop_state`: ante morte nel pot iniziale, button blind vivo dell'ultimo posto, primo posto ad agire); abstraction delle azioni come funzione dello stato (`action_config_at`); transizioni (`apply_action_at`: HU delegato a `gtosd::apply_action`, per N > 2 fold generalizzato); avanzo di street con il primo giocatore attivo non all-in (`advance_to_next_street`) |
| Albero compilato | `include/gtosd/preflop_blueprint/compiled_game.hpp`, `libs/preflop_blueprint/src/compiled_game.cpp` | `CompiledGame::compile(config, options)`: un solo array di nodi in preordine con sottoalberi contigui (`subtree_end`), archi nell'ordine di `legal_actions`, stati pubblici conservati, payoff per giocatore settled con `settle_terminal` per ogni sottoinsieme non vuoto di vincitori, statistiche, fingerprint `gtosd.preflop_blueprint_game_tree.v1`; `layout_state`: offset contigui `(nodo, riga, azione)` con 81 classi al preflop e la capacità della street postflop |
| Eseguibile | `benchmarks/preflop_blueprint_game.cpp` | compila una fixture e stampa il report JSON `gtosd.preflop_blueprint_game_report.v1` con conteggi, byte dello stato per due terne di capacità e tempo |
| Test | `tests/preflop_blueprint_game_tests.cpp` | 836.981 asserzioni; registrato con label `p4` insieme al report CO40 |

## 2. Regole dell'abstraction preflop guidate dallo stato

Il livello di aggressione `level` conta bet, raise e all-in di rilancio già fatti sulla street lungo
il percorso (il core non conta i bet in `raise_count_this_street`, quindi il compilatore porta il
contatore). Lo stato dice se il bet corrente è di un giocatore all-in (`facing_all_in`).

| Situazione | Azioni | Equivalente legacy HU |
|---|---|---|
| facing all-in | fold, call | `FacingAllIn` |
| `level = 0` (nessuna aggressione) | fold o check, call, raise ai target di apertura (nessun rilancio incompleto), all-in | `Root`, `LimpOption` |
| `level = 1` con bet corrente = target di apertura `i` | fold, call, raise al target di risposta `i` (incompleto se configurato), all-in | `FacingSmallOpen`, `FacingLargeOpen` |
| `level >= 2` | fold, call, all-in | `FinalResponse` |
| postflop | fold o check, call, bet/raise 33/66/120 % del pot dopo il call, all-in sempre presente | `PostflopSkeletonAnalyzer` |

Le regole non nominano il numero di giocatori: la stessa tabella produce l'albero HU e quello 3-way.
Il minimo bet preflop è il button blind (uguale all'ante nelle fixture; il legacy usava l'ante).

## 3. Verifiche eseguite

| Verifica | Esito |
|---|---|
| Stato preflop HU uguale a `make_hu_preflop_state` del core (CO40 e HU10) | PASS |
| CO40: 58 nodi preflop, 20 decisioni, 9 ingressi, 19 fold, 10 all-in | PASS |
| Fingerprint legacy `fnv1a64:a68337fa567aa2d9` ricalcolato nel test sulla parte preflop (stesso formato `gtosd.hu_preflop_tree.v2`, stessi stati e archi nello stesso ordine) | PASS, identico |
| Ogni nodo decisionale: azioni compilate uguali a `legal_actions` in numero, ordine e contenuto; ogni figlio uguale a `apply_action` del core (HU) o alla transizione multiway (N > 2) | PASS su CO40, HU10 completa, HU10 ridotta, 3-way preflop |
| Ogni nodo chance: un solo arco, figlio uguale a `advance_street` del core (HU) | PASS |
| Preordine: figli consecutivi, sottoalberi contigui, `parent`/`depth` coerenti, ingressi postflop corretti | PASS |
| Payoff di ogni terminale uguali a `settle_terminal` per ogni sottoinsieme di vincitori; somma zero; vincitore unico > 0, perdente attivo < 0 | PASS |
| Conteggi dello scheletro postflop CO40 uguali all'analizzatore legacy (`gtosd_hu_preflop_tree`) | PASS, vedi §4 |
| Layout: offset contigui, totali per street, 81 righe preflop, capacità postflop; capacità maggiori richiedono più stato | PASS |
| HU10 ridotta più piccola della completa; stessa parte preflop; compilazione deterministica | PASS |
| 3-way (UTG, CO, BTN, stack 40a) solo preflop: compila; i fold terminali lasciano un giocatore; gli ingressi hanno almeno due giocatori; il flop parte dal primo attivo non all-in; UTG raise 6a e due fold → UTG +3a, CO −1a, BTN −2a con 5a restituiti | PASS |
| `ctest -L p4`: test e report CO40 | PASS (0,6 s + 0,5 s) |
| Controllo di isolamento (`gtosd_preflop_blueprint_dependency_check`) | PASS, 26 sorgenti guardati |

## 4. Conteggi CO40

| Grandezza | Albero compilato | Analizzatore legacy (misurato il 2026-09-15) | Roadmap §P4 (documenti anteriori) |
|---|---|---|---|
| Nodi preflop / decisioni / ingressi | 58 / 20 / 9 | 58 / 20 / 9 | 58 / 20 / 9 |
| Nodi postflop rappresentati (ingressi inclusi) | 27.012 | 27.012 | 30.324 |
| Decisioni postflop (flop / turn / river) | 10.060 (372 / 2.100 / 7.588) | 10.060 | 11.308 |
| Archi azione postflop | 25.944 | 25.944 | 29.112 |
| Frontiere chance | 1.059 | 1.059 | — |
| Fold / showdown / runout all-in | 7.942 / 6.715 / 1.236 | 7.942 / 6.715 / 1.236 | — |
| Raise massimi per street | 4 | 4 | 4 |
| Profondità massima | 17 dalla radice preflop | 15 dall'ingresso | 15 |

I valori della roadmap (30.324 / 11.308 / 29.112) provengono da `docs/HU_PREFLOP_CO40_BENCHMARK.md`
e da `docs/IMPLEMENTATION_STATUS.md`, scritti con regole di puntata anteriori; il codice legacy
attuale non li riproduce. L'albero compilato coincide con l'analizzatore legacy attuale classe per
classe, quindi i conteggi attesi sono stati corretti (diario, decisione 20). Nodi totali dell'albero
compilato: 27.061 (58 preflop + 27.012 postflop − 9 ingressi contati una volta), 27.060 archi.

## 5. Byte dello stato (R e S in double)

| Configurazione | Capacità flop/turn/river | Celle | Preflop | Flop | Turn | River | Byte R+S |
|---|---|---|---|---|---|---|---|
| CO40 | 200 / 500 / 1.000 | 22.288.617 | 4.617 | 216.000 | 2.796.000 | 19.272.000 | 356.617.872 (340 MiB) |
| CO40 | 500 / 1.000 / 2.000 | 44.680.617 | 4.617 | 540.000 | 5.592.000 | 38.544.000 | 714.889.872 (682 MiB) |
| HU10 completa | 200 / 500 / 1.000 | 1.420.217 | 4.617 | 45.600 | 278.000 | 1.092.000 | 22.723.472 |
| HU10 completa | 500 / 1.000 / 2.000 | 2.858.617 | 4.617 | 114.000 | 556.000 | 2.184.000 | 45.737.872 |
| HU10 ridotta | 200 / 500 / 1.000 | 308.217 | 4.617 | 21.600 | 78.000 | 204.000 | 4.931.472 |
| HU10 ridotta | 500 / 1.000 / 2.000 | 622.617 | 4.617 | 54.000 | 156.000 | 408.000 | 9.961.872 |

Il river pesa l'86 % dello stato CO40: 7.588 decisioni × 1.000 bucket × 2,5 azioni medie. Entrambe
le terne candidate stanno sotto i 4 GiB del paletto §3.4. Memoria fissa dell'albero CO40: stati
8,4 MB, nodi 0,87 MB, archi 0,65 MB. Compilazione 0,026–0,027 s.

Altri alberi: HU10 completa 2.059 nodi (2.010 postflop, 812 decisioni), HU10 ridotta 571 nodi
(236 decisioni); 3-way solo preflop 580 nodi, 234 decisioni, 75 ingressi, 115 fold, 156 runout
all-in (fingerprint `fnv1a64:806ec416fe01d164`).

## 6. Osservazioni e dubbi

1. Le strutture sono pronte per N giocatori (maschere, payoff per sottoinsieme di vincitori, N
   stack); il postflop multiway non è stato compilato: con tre giocatori i sottoalberi crescono
   molto e il conteggio è il compito di P10. La transizione "call per meno del dovuto" per N > 2
   è rifiutata esplicitamente: con stack uguali non si presenta e i side pot non sono modellati.
2. `maximum_actions = 8` sostituisce il limite di scaffolding a sei azioni: fold/check, call,
   fino a quattro target o tre size, all-in.
3. Il fingerprint legacy viene ricalcolato nel test e non nella libreria: il controllo di
   isolamento vieta ogni riferimento al trainer legacy nei sorgenti della libreria.

## 7. Comandi

```text
cmake --build out/build/windows-release --target gtosd_preflop_blueprint gtosd_preflop_blueprint_game_tests gtosd_preflop_blueprint_game
ctest --test-dir out/build/windows-release -L p4 --output-on-failure -V
gtosd_preflop_blueprint_game --config benchmarks/fixtures/preflop_blueprint_co40_v1.json
gtosd_hu_preflop_tree
```
