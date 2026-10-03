# P4 — Modello di gioco e albero compilato

> **Stato al 2026-10-03.** Questo è il report del gate di P4, di settembre 2026, ed è
> congelato. Il modulo che descrive è ancora in produzione nel solver preflop blueprint.
> I riferimenti a HU10, CO40, P9 e alla best response astratta come lavoro o gate correnti
> sono però storici:
>
> - P9 è in [`archive/preflop-blueprint-research-2026-09/`](../../archive/preflop-blueprint-research-2026-09/README.md);
> - history7 e la suite HU10-HU40 sono stati tolti il 2026-10-03, e l'ultimo albero che li
>   contiene è al tag `history7-final`.
>
> Lo stato corrente è nel [diario](PROGRESS_LOG.md) e nella
> [ricetta di MonkerSolver](MONKER_RECIPE_REPRODUCTION_2026-09-28.md).

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
| CO40: 58 nodi preflop, 20 decisioni, 9 ingressi, 19 fold, 10 all-in | PASS (fino al 2026-09-16; vedi nota sotto) |
| Fingerprint legacy `fnv1a64:a68337fa567aa2d9` ricalcolato nel test sulla parte preflop (stesso formato `gtosd.hu_preflop_tree.v2`, stessi stati e archi nello stesso ordine) | PASS, identico (fino al 2026-09-16; vedi nota sotto) |
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

> **Nota (2026-09-16).** Queste verifiche su CO40 sono strutturali: azioni legali, transizioni e
> payoff contro il core. Non dicono nulla sui valori calcolati dal CFR vettoriale su quella
> struttura, che non hanno un oracolo esatto (P6 §3.1).

> **Nota (2026-09-17, P9). Modifica autorizzata dall'utente.** L'utente ha chiesto che dopo
> "CO limpa, BTN rilancia" il limper abbia solo fold, call e all-in, e ha autorizzato la modifica
> su entrambe le fixture CO40. Il campo nuovo è `limp_response_target_units` (opzionale,
> indicizzato sugli open, assente = comportamento storico, vuoto = solo all-in). I due rami
> raggiungono stati pubblici identici a meno di quale posto tiene quale impegno e
> `acted_players_mask` viene azzerato a ogni raise, quindi il ramo non è deducibile dallo stato:
> il compilatore propaga il flag `CompiledNode::limped_pot`, che non entra nel fingerprint
> dell'albero (la differenza di comportamento è già nel fingerprint della configurazione).
>
> Su indicazione successiva dell'utente, sempre del 2026-09-17, anche le size della fixture
> principale passano alla formula esatta. La formula dà una sola size per spot e i due open
> Monker erano entrambi decisi alla radice, quindi collassano su 5 a; la risposta diventa 17 a.
> Le due fixture CO40 hanno ora la stessa parte preflop e differiscono solo nelle size postflop.
>
> Conteggi di `preflop_blueprint_co40_v1.json` dopo entrambe le modifiche: **28 nodi preflop,
> 10 decisioni, 4 ingressi, 9 fold, 5 all-in**; postflop **26.854 nodi rappresentati,
> 9.948 decisioni, 25.852 archi**, 998 frontiere di chance, 7.952 fold, 6.812 showdown,
> 1.144 runout all-in; albero completo 26.878 nodi, profondità massima 17, al più quattro raise
> per street, fingerprint `fnv1a64:d6c10723d35b9503`. La parte preflop non riproduce più
> l'albero legacy `fnv1a64:a68337fa567aa2d9`: il fingerprint congelato nel test è ora
> `fnv1a64:c2169c4295026609` e vale come guardia di regressione, non come equivalenza al legacy.
> La suite completa del blueprint passa: 19 test su 19, comprese le regressioni del trainer, del
> certificatore e dell'export.
>
> `preflop_blueprint_co40_test_v1.json` cambia anche le size: la risposta passa da 13 a a **17 a**
> (formula esatta `P + 2B - c`; il 13 a veniva da un calcolo errato del diario del 2026-09-16).
> Albero risultante 604 nodi, 28 preflop, 10 decisioni, 4 ingressi postflop,
> fingerprint `fnv1a64:18d08f453034ac0f`. Le fixture HU10 non cambiano: non hanno il campo nuovo,
> la loro serializzazione e quindi i loro fingerprint restano identici
> (HU10 completo `fnv1a64:bc9e7b35ad8c021d`, verificato contro gli artefatti esistenti).

## 4. Conteggi CO40

| Grandezza | Albero compilato | Analizzatore legacy (misurato il 2026-09-15) | Roadmap §P4 (documenti anteriori) |
|---|---|---|---|
| Nodi preflop / decisioni / ingressi | 58 / 20 / 9 | 58 / 20 / 9 | 58 / 20 / 9 |
| Nodi postflop rappresentati (ingressi inclusi) | 27.012 | 27.012 | 30.324 |
| Decisioni postflop (flop / turn / river) | 10.060 (372 / 2.100 / 7.588) | 10.060 | 11.308 |
| Archi azione postflop | 25.944 | 25.944 | 29.112 |

I tre valori misurati sopra valgono per l'albero fino al 2026-09-16. Dopo la modifica autorizzata
dall'utente il 2026-09-17 sono 26.308 / 9.780 (340 / 2.012 / 7.428) / 25.288: vedi la nota nella
sezione precedente.
| Frontiere chance | 1.059 | 1.059 | — |
| Fold / showdown / runout all-in | 7.942 / 6.715 / 1.236 | 7.942 / 6.715 / 1.236 | — |
| Raise massimi per street | 4 | 4 | 4 |
| Profondità massima | 17 dalla radice preflop | 15 dall'ingresso | 15 |

I valori della roadmap (30.324 / 11.308 / 29.112) provengono da `docs/archive/preflop-es-2026-09/HU_PREFLOP_CO40_BENCHMARK.md`
e da `docs/archive/legacy-postflop-2026-07-09/IMPLEMENTATION_STATUS.md`, scritti con regole di puntata anteriori; il codice legacy
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
le terne candidate stavano sotto i 4 GiB del paletto allora vigente in §3.4. D30 alzò a 25 GiB
il tetto degli audit del 2026-09-21; D31 fissa il picco del solver di prodotto a **8 GiB**.
Memoria fissa dell'albero CO40: stati
8,4 MB, nodi 0,87 MB, archi 0,65 MB. Compilazione 0,026–0,027 s.

> **Nota (2026-09-16).** Dal pomeriggio del 2026-09-16 le fixture HU10 hanno una sola size preflop
> (open 5 a) e nessuna size di risposta (contro l'open solo fold, call e all-in): gli alberi e i
> conteggi HU10 di questa sezione valgono per le fixture di allora (diario, P8 §9 e §10).

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
