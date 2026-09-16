# P8 — Export, query, comparatore, viewer

Data: 2026-09-16. Branch di fase: `feature/preflop-blueprint-p8-export`. Esito del gate:
**PASS con riserva** (§6): test PASS, export HU10 completo e ridotto certificati `CERTIFIED_EXACT`,
query, comparatore e validatore verificati; il criterio "viewer navigabile con un export HU10"
resta **INCONCLUSIVE** perché il viewer è un repository separato fuori dal perimetro dell'agent
(Q4 nel diario).

## 1. Cosa è stato prodotto

| Componente | File | Contenuto |
|---|---|---|
| Etichette e id | `include/gtosd/preflop_blueprint/action_labels.hpp`, `libs/preflop_blueprint/src/action_labels.cpp` | id di azione stabili `fold`, `check`, `call`, `all_in`, `raise_<ante>`, `bet_<ante>` (decimali con `_`, come gli id `raise_6` / `raise_10_5` delle chart legacy; suffisso `_bp<punti base>` solo in caso di collisione), id di nodo preflop `CO`, `CO_raise_3_BTN`, …, passi di history `(posizione, azione)`, enumerazione dei nodi decisionali preflop |
| Query | `include/gtosd/preflop_blueprint/policy_query.hpp`, `libs/preflop_blueprint/src/policy_query.cpp` | data una history legale (id di azione), una combo e il board distribuito, la distribuzione d'azione della policy al nodo raggiunto: riga = classe di mano al preflop, bucket della combo sul board dopo (lookup del catalogo + tabella, nessuna feature ricalcolata); consumo delle carte del board alle transizioni di strada; errori tipizzati |
| Valori per azione | `BestResponseEvaluator::preflop_action_values` in `best_response.hpp/.cpp` | per ogni nodo preflop dell'eroe e ogni azione: valore controfattuale medio per combo sui flop campionati, reach avversaria al nodo per combo, EV condizionato per classe (pesi = reach avversaria) con errore standard sui flop |
| Export chart | `include/gtosd/preflop_blueprint/chart_export.hpp`, `libs/preflop_blueprint/src/chart_export.cpp`, `schemas/preflop_blueprint_chart.schema.json` | `gtosd.preflop_blueprint_chart.v1`: per ogni nodo preflop `history`, `strategy` (81 classi × azioni) e `action_ev` (EV in ante, errore standard, campioni) nel layout letto dal generatore del viewer; quattro fingerprint (regole, albero, astrazione = catalogo + tre tabelle, policy); blocco `status` con badge `ESTIMATED` (stima campionata sugli stessi flop: naive, limite inferiore, semiampiezza) o `CERTIFIED_EXACT` (certificato P7 della stessa policy e albero); `root_ev_ante`, `strategy` e `root_action_ev` di radice per compatibilità; checksum FNV-1a del testo; scrittura atomica |
| Comparatore | `include/gtosd/preflop_blueprint/comparator.hpp`, `libs/preflop_blueprint/src/comparator.cpp` | verdetto sulla exploitability fisica dichiarata (D2, soglia 0,1 a): `QUALIFIED` / `REJECTED` con certificato esatto, `PROMISING` / `INCONCLUSIVE_ESTIMATE` / `REJECTED` con stima campionata (limite inferiore sopra soglia = respinto); baseline (altro export) confrontata nodo per nodo con le metriche legacy (WMAE e variazione totale per classe pesate con le masse 6/4/12, p95, errore massimo di radice), `STALE_TREE` se l'albero differisce; riferimento Monker `gtosd.hu_preflop_reference.v1` (parser delle range string `[p]classe[/p]`) confrontato alla radice, stato `EXTERNAL_CONTRACT_INCOMPLETE`, mai influente sul verdetto; output `gtosd.preflop_blueprint_comparison.v1` |
| Validatore | `tools/validate_preflop_blueprint_chart.py` | schema, fingerprint, badge, 81 righe per nodo, somme a uno, history coerente con gli id, radice replicata, checksum ricalcolato; registrato in CTest (`p8;schema`) sull'export dello smoke |
| Eseguibili | `benchmarks/preflop_blueprint_export.cpp` (export e `--query-*`), `benchmarks/preflop_blueprint_compare.cpp` | export con `--certificate`, `--eval-flops`, `--iterations`; query con `--query-history`, `--query-hand`, `--query-board` (JSON `gtosd.preflop_blueprint_query.v1`); comparatore con `--candidate`, `--baseline`, `--reference`, `--max-gain` (uscita 2 se `REJECTED`) |
| Test | `tests/preflop_blueprint_export_tests.cpp` (20.380 asserzioni), smoke CTest a catena (train → export → validatore / query / comparatore tramite fixture) | etichette, query, export, comparatore |

## 2. Definizioni

**EV per azione (scope).** Per il nodo `n` dell'attore, l'azione `a` e la combo `h`, il valore
controfattuale `v_a[h]` (avversario con la strategia media prima e dopo, entrambi con la
strategia media dopo l'azione, media sui flop campionati con tutti i runout, terminali preflop
esatti) è diviso per la probabilità `D[h]` che l'avversario raggiunga il nodo con una mano
disgiunta da `h`; l'EV della classe è la media delle combo pesata con `D[h]`; l'errore standard è
quello della serie per flop dei valori di classe. Alla radice `D[h] = 1` e la somma sulle classi
(masse 6/4/12, frequenze della strategia) restituisce esattamente l'EV del gioco (test entro
`1e-9`). La strategia corrente non è esportata (`has_current_strategy = false`): il file di
policy contiene solo la media.

**Badge.** `CERTIFIED_EXACT` richiede un certificato P7 `exact = true` con la stessa fingerprint
di policy e di albero; altrimenti `ESTIMATED` con la stima campionata (naive, che sovrastima come
`≈ 0,4/√M`, e limite inferiore) sugli stessi flop degli EV. `EXTERNAL_REFERENCE` è lo stato del
riferimento Monker nel viewer, non un badge dell'export.

**Verdetto.** Solo la exploitability fisica decide (D1, D2, D4): le distanze da Monker sono
descrittive perché il contratto postflop esterno è incompleto.

## 3. Verifiche eseguite

| Verifica | Esito |
|---|---|
| Etichette: 812 nodi decisionali di HU10 completo con etichette uniche per nodo nel vocabolario; radice `CO` con `fold call raise_3 raise_5 all_in`; 20 nodi preflop con id unici e history di lunghezza pari al path | PASS |
| Query: 160 cammini casuali (26/28/23/19 per strada preflop/flop/turn/river) con board casuale: nodo, id, riga uguale a quella del `BoardContext` P5 e probabilità uguali alla riga della policy; history terminale, azione ignota, mano sul board e board mancante respinti | PASS |
| Export HU10 ridotto (policy di 3 iterazioni, 3 flop): 20 nodi, 81 righe per nodo con tutte le azioni, frequenze a somma uno e uguali alla policy, campi EV, EV di radice ricostruito dalle classi entro `1e-9`; badge `ESTIMATED` senza certificato, `CERTIFIED_EXACT` con certificato coerente, certificato con fingerprint diversa ignorato e segnalato; query identica prima e dopo il round trip della policy | PASS |
| Comparatore: candidato esatto sopra soglia `REJECTED`, sotto soglia `QUALIFIED`; stima entro soglia `PROMISING`, sopra soglia `INCONCLUSIVE_ESTIMATE`, limite inferiore sopra soglia `REJECTED`; baseline uguale a se stesso distanza zero su tutti i nodi, albero diverso `STALE_TREE`; riferimento sintetico in formato Monker costruito dalla radice del candidato distanza zero, riferimento CO40 contro HU10 `ACTION_SET_MISMATCH` | PASS |
| Smoke a catena (`ctest -L p8`): train (policy) → export (badge `ESTIMATED`, 20 nodi) → validatore (`nodes=20 rows=1620`) → query (`PREFLOP_BLUEPRINT_QUERY=OK`) → comparatore (`PROMISING` con soglia 100) | PASS (6/6) |
| Suite `preflop_blueprint` P0–P8 | PASS: 23/23 in 505 s (`ctest -L preflop_blueprint`, Release, dopo la correzione del check di isolamento) |

## 4. Export di HU10

Export dalle policy dei checkpoint P6 (2.000 iterazioni, 200/500/1.000) con i certificati esatti di
P7 dove disponibili, 60 flop campionati (63.360 board) per gli EV per azione, 8 thread.

| Export | Badge | Nodi | EV radice (60 flop) | Exploitability dichiarata | Stima campionata sugli stessi flop | Tempo | Byte |
|---|---|---|---|---|---|---|---|
| HU10 ridotto, DCFR alternato | `CERTIFIED_EXACT` | 20 | 0,1320 a | 0,0042 a esatta (certificato P7) | naive 0,0605 ± 0,0434 a, limite inferiore 0,0016 a | 63 s | 722,124 |
| HU10 completo, DCFR alternato | `CERTIFIED_EXACT` | 20 | 0,1320 a | 0,0042 a esatta (certificato P7) | naive 0,0607 ± 0,0435 a, limite inferiore 0,0017 a | 239 s | 722,116 |
| HU10 completo, DCFR, senza certificato | `ESTIMATED` | 20 | 0,1320 a | stima: naive 0,0607 ± 0,0435 a, limite inferiore 0,0017 a | (la stessa) | 236 s | — |
| HU10 completo, Linear simultaneo (baseline) | `ESTIMATED` | 20 | 0,1300 a | stima: naive 0,1333 ± 0,0457 a, limite inferiore 0,0035 a | (la stessa) | 238 s | — |

Esempio (radice, CO, classe AA, export completo certificato): `fold` 0,000 (EV -1,000 ± 0,000 a), `call` 0,000 (EV 3,713 ± 0,064 a), `raise_3` 0,005 (EV 3,877 ± 0,041 a), `raise_5` 0,589 (EV 3,864 ± 0,068 a), `all_in` 0,406 (EV 3,939 ± 0,000 a).

Query `raise_3,call` con `AhKs` su `7s8d9c`: nodo `CO_raise_3_BTN_call_CO` (flop, attore
CO, bucket 62), distribuzione `all_in` 0,208, `bet_2_64` 0,190, `bet_5_28` 0,596, `check` 0,006.

Comparatore. (1) Candidato HU10 completo certificato, baseline HU10 ridotto, riferimento Monker
CO40: verdetto `QUALIFIED` (esatto, 0,0042 a ≤ 0,1 a); baseline `STALE_TREE` (l'albero
ridotto ha un fingerprint diverso: nessun confronto per nodo); riferimento
`EXTERNAL_CONTRACT_INCOMPLETE`, `ACTION_SET_MISMATCH` (le chart Monker CO40 hanno azioni diverse da
HU10). (2) Candidato DCFR alternato contro baseline Linear simultaneo, stesso albero: baseline
`COMPARED` su 20 nodi, variazione totale media di classe alla radice 10,79 pp (WMAE
4,32 pp), peggiore fra i nodi 45,28 pp, differenza di EV di radice 0,0020 a: le due
strategie medie differiscono di pochi punti percentuali nonostante la exploitability tre volte
maggiore del Linear (P6). (3) Candidato non certificato: verdetto `INCONCLUSIVE_ESTIMATE` (stima
0,0607 + 0,0435 a contro la soglia 0,1 a, limite inferiore
0,0017 a): senza certificato la stima campionata a 60 flop non basta a qualificare né a
respingere, come previsto dal bias di selezione. Validatore: PASS file=p8_chart_hu10_reduced.json nodes=20 rows=1620 badge=CERTIFIED_EXACT; PASS file=p8_chart_hu10_full.json nodes=20 rows=1620 badge=CERTIFIED_EXACT; PASS file=p8_chart_hu10_full_estimated.json nodes=20 rows=1620 badge=ESTIMATED.

## 5. Viewer

Il viewer `tools/hu_preflop_chart_viewer` è un repository separato (D18), escluso da git in
questo repository e presente solo nel working tree dell'utente. Il suo generatore
(`generate_chart_data.py`) legge candidati con `preflop_nodes` → `{id, player, history,
strategy, action_ev, ev_scope}`, esattamente il layout dell'export P8, ma impone tre vincoli
fissi di CO40 (fingerprint dell'albero `fnv1a64:a68337fa567aa2d9`, 20 nodi con le stesse history
delle chart Monker, 2 milioni di iterazioni postflop) e non conosce i badge. Per rendere
navigabile un export HU10 servono nel repository del viewer: (1) lettura dell'albero e dei nodi
dall'export invece dei vincoli fissi; (2) badge di stato da `status.badge` (`ESTIMATED`,
`CERTIFIED_EXACT`) e `EXTERNAL_REFERENCE` per Monker; (3) nessuna policy di ricerca legacy come
baseline. L'agent non modifica quel repository né il working tree dell'utente: domanda Q4 del
diario. Il criterio "viewer navigabile con un export HU10" del gate resta INCONCLUSIVE fino alla
risposta; tutto il resto del gate è verificato.

## 6. Esito del gate

| Criterio (roadmap P8) | Esito |
|---|---|
| Test PASS (`ctest -L p8`, 20.380 asserzioni più 5 smoke a catena) | PASS |
| Round trip della policy; query uguale prima e dopo l'export; validatore statico PASS; schema JSON accettato; comparatore su un candidato con exploitability sopra soglia restituisce `REJECTED` | PASS (§3) |
| Viewer navigabile con un export HU10 | INCONCLUSIVE: l'export è nel layout letto dal generatore del viewer e il validatore lo accetta, ma il generatore del viewer (repository separato, D18) impone i vincoli fissi di CO40 e non ha i badge; la modifica spetta all'utente o va autorizzata (Q4) |
| Report | questo documento |

Il programma si ferma al gate P8 per la decisione fuori perimetro sul viewer (Q4) e per le
decisioni già aperte sui merge in `main` (Q2, Q3). P9 (qualificazione CO40) resta da avviare:
richiede la passata esatta di 22 h stimata in P7 o le sue ottimizzazioni, e le tabelle bucket di
CO40 già disponibili.

## 7. Fallimenti registrati

1. `quoted` come nome dell'helper JSON entrava in conflitto con `std::quoted` per ADL: rinominato.
2. Il target `nlohmann_json` non era visibile nella directory dei test: `find_package` locale.
3. Cammini casuali del test di query troppo brevi per raggiungere il river: cammini mirati per
   strada con preferenza per le azioni passive.
4. Gli smoke a catena con `DEPENDS` non eseguivano il produttore fuori dall'etichetta `p8`:
   sostituiti con fixture CTest (`FIXTURES_SETUP` / `FIXTURES_REQUIRED`).

## 8. Comandi

```text
ctest --test-dir out/build/windows-release -L p8 --output-on-failure -V
gtosd_preflop_blueprint_export --config benchmarks/fixtures/preflop_blueprint_hu10_full_v1.json --resources-dir out/preflop_blueprint_resources --buckets-dir out/preflop_blueprint_buckets_200_500_1000 --policy out/policy_full_dcfr_200.bin --certificate out/p7_cert_full.json --eval-flops 60 --threads 8 --iterations 2000 --output out/p8_chart_hu10_full.json
gtosd_preflop_blueprint_export ... --policy out/policy_full_dcfr_200.bin --query-history raise_3,call --query-hand AhKs --query-board 7s8d9c
gtosd_preflop_blueprint_compare --candidate out/p8_chart_hu10_full.json --baseline out/p8_chart_hu10_reduced.json --reference benchmarks/fixtures/hu_preflop_co40_reference_v1.json --output out/p8_compare_full_vs_reduced.json
python tools/validate_preflop_blueprint_chart.py out/p8_chart_hu10_full.json
```
