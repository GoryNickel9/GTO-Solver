# P8 — Export, query, comparatore, viewer

Data: 2026-09-16. Branch di fase: `feature/preflop-blueprint-p8-export`. Esito del gate:
**PASS** (§6): test PASS, export HU10 completo e ridotto certificati `CERTIFIED_EXACT`, query,
comparatore e validatore verificati; il viewer, aggiornato nel suo repository dopo
l'autorizzazione dell'utente (Q4, 2026-09-16), naviga l'export HU10 preflop e postflop con i
badge di stato (§5).

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
| Albero postflop per il viewer | `include/gtosd/preflop_blueprint/postflop_tree_export.hpp`, `libs/preflop_blueprint/src/postflop_tree_export.cpp` | `gtosd.hu_postflop_public_tree.v1`: entry con history preflop, nodi decisionali postflop con id contigui e stato pubblico in ante (pot, to call, stack, commitment, raise), azioni con commitment obiettivo, pagamento e target (decisione, chance con la strada distribuita, terminale con stato), conteggi; id compilati conservati per il worker |
| Worker di query postflop | `include/gtosd/preflop_blueprint/query_worker.hpp`, `libs/preflop_blueprint/src/query_worker.cpp`; `NodeProbe` / `probe_node` in `best_response.hpp` | per una entry, un percorso di azioni e il board visibile: combo vive, reach dell'eroe lungo il percorso, frequenze per classe, EV per azione (esatti a turn e river con tutti i river enumerati, medi su runout campionati al flop), frequenze di range; protocollo JSON a righe (`--serve`), risposta letta dal server del viewer |
| Test | `tests/preflop_blueprint_export_tests.cpp` (20.380 asserzioni più test del worker), smoke CTest a catena (train → export / albero postflop → validatore / query / comparatore tramite fixture) | etichette, query, export, comparatore, albero postflop, worker |

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

> **Erratum (2026-09-16).** Fino al commit `46084f0` l'implementazione non divideva per `D[h]`: i
> valori fuori dalla radice erano scalati per `D[h]` (alla radice `D[h] = 1` e il test passava).
> Corretto con un test sul payoff di fold a ogni nodo interno; export rifatti in §9.

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
| Smoke a catena (`ctest -L p8`): train (policy) → export (badge `ESTIMATED`, 20 nodi) → validatore (`nodes=20 rows=1620`) → query (`PREFLOP_BLUEPRINT_QUERY=OK`) → albero postflop → comparatore (`PROMISING` con soglia 100) | PASS (7/7) |
| Albero postflop e worker (HU10 completo, policy pseudo-casuale): 792 nodi decisionali e 1.876 archi uguali al compilato, id contigui, target validi; query al flop con 16 runout campionati (81 classi vive, frequenze a somma uno, EV finiti), con tutti i 1.056 runout esatta e con EV di classe uguale al valutatore entro `1e-9`; turn esatto sui 32 river; river esatto; entry non valida e board incompleto respinti | PASS |
| Suite `preflop_blueprint` P0–P8 | PASS: 23/23 in 505 s (`ctest -L preflop_blueprint`, Release, dopo la correzione del check di isolamento) |

## 4. Export di HU10

> **Superato (2026-09-16).** Gli `action_ev` dei nodi interni in questa sezione e in §5 sono
> affetti dall'erratum di §2; inoltre le fixture HU10 hanno ora una sola size preflop (5 a / 8 a).
> I valori validi sono in §9. Frequenze, EV di radice, badge e verdetti restano corretti.

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

Il viewer `tools/hu_preflop_chart_viewer` è un repository separato (D18), presente nel working
tree dell'utente; l'utente ha autorizzato la modifica (Q4). Le sue modifiche locali non
committate sono state conservate in un commit dedicato (`56cdf31`) sul branch
`feature/preflop-blueprint-p8-export` del viewer; l'aggiornamento è il commit `4edbb45`:

- generatore (`generate_chart_data.py`): sorgenti da export blueprint (`--blueprint`, ripetibile,
  con etichette), badge `CERTIFIED_EXACT` / `ESTIMATED` dal blocco di stato, gioco per sorgente
  (posizioni, stack), ordine e colori delle azioni derivati dagli export (`raise_3`, `bet_2_64`,
  …), riferimento Monker opzionale (`EXTERNAL_REFERENCE`, confrontato solo a parità di albero),
  albero postflop da `--postflop-tree` con controllo del fingerprint; senza argomenti riproduce il
  payload legacy CO40;
- frontend preflop (`app.js`, `index.html`, `styles.css`): nodo radice dalla history, confronti con
  il riferimento solo con lo stesso albero, badge con colore e tooltip, campi di configurazione
  dinamici (stack 10 ante, "HU 10a Chart Viewer");
- frontend postflop (`postflop-app.js`): backend blueprint senza il vincolo dei 2M iterazioni,
  indicazione "EV esatti" / "runout campionati", reach della classe nel dettaglio;
- server (`serve_viewer.py`): `--backend blueprint` avvia `gtosd_preflop_blueprint_export --serve`
  (config, risorse, tabelle, policy) e ne espone la handshake; `--backend legacy` conserva il
  worker V18;
- validatori: chart blueprint e alberi postflop di qualsiasi gioco (conteggi dal file).

Verifica nel browser (server locale con la policy HU10 DCFR certificata): pagina "HU 10a Chart
Viewer", badge `CERTIFIED EXACT · exploitability 0.0042a (0.14% pot)`, 20 nodi preflop navigabili
con frequenze e EV per classe (radice: all-in 32,0 %, raise 5a 42,1 %, raise 3a 0,2 %, fold
25,7 %; AA: all-in 40,6 % EV +3,94 a, raise 5a 58,9 % EV +3,86 a), tre sorgenti selezionabili;
vista postflop con l'albero HU10 (9 entry, 792 nodi), stato pubblico corretto (pot 4 a, stack
8 a / 8 a dopo limp e check) e query sul board `Ac Kd Qh`: CO al flop check 33,8 %, bet 1,32 a
60,2 %, EV per classe (AA +3,50 a). Tempi del worker su HU10 completo: flop 0,4–1,3 s con 16–64
runout campionati, turn 0,5 s esatto, river istantaneo; il flop con tutti i 1.056 runout costa
17,6 s (sonda sequenziale sui turn: parallelizzabile in seguito).

## 6. Esito del gate

| Criterio (roadmap P8) | Esito |
|---|---|
| Test PASS (`ctest -L p8`, 20.380 asserzioni più 5 smoke a catena) | PASS |
| Round trip della policy; query uguale prima e dopo l'export; validatore statico PASS; schema JSON accettato; comparatore su un candidato con exploitability sopra soglia restituisce `REJECTED` | PASS (§3) |
| Viewer navigabile con un export HU10 | PASS: viewer aggiornato nel suo repository (Q4 autorizzata dall'utente), preflop e postflop HU10 navigati nel browser con badge `CERTIFIED_EXACT` (§5) |
| Report | questo documento |

Gate P8 PASS. Le domande Q1–Q4 sono state risolte dall'utente il 2026-09-16 (branch pubblicati su
origin, merge dell'integrazione in `main` con i tag `preflop-blueprint-p3-abstraction`,
`preflop-blueprint-p6-hu10`, `preflop-blueprint-p8-export`, viewer modificabile). P9
(qualificazione CO40) è la fase successiva: richiede la passata esatta di 22 h stimata in P7 o le
sue ottimizzazioni; le tabelle bucket di CO40 sono già disponibili.

## 7. Fallimenti registrati

1. `quoted` come nome dell'helper JSON entrava in conflitto con `std::quoted` per ADL: rinominato.
2. Il target `nlohmann_json` non era visibile nella directory dei test: `find_package` locale.
3. Cammini casuali del test di query troppo brevi per raggiungere il river: cammini mirati per
   strada con preferenza per le azioni passive.
4. Gli smoke a catena con `DEPENDS` non eseguivano il produttore fuori dall'etichetta `p8`:
   sostituiti con fixture CTest (`FIXTURES_SETUP` / `FIXTURES_REQUIRED`).
5. Nel viewer il confronto con Monker assumeva un riferimento sempre presente (`reference?.ev`
   con `reference` indefinito lanciava un'eccezione e interrompeva il render della matrice):
   guardia aggiunta; il riferimento è usato solo a parità di albero.
6. (2026-09-16) EV di classe fuori dalla radice non diviso per la reach avversaria: il test di
   ricostruzione copriva solo la radice (`D[h] = 1`); corretto con il test del payoff di fold
   a ogni nodo interno (§9).

## 8. Comandi

```text
ctest --test-dir out/build/windows-release -L p8 --output-on-failure -V
gtosd_preflop_blueprint_export --config benchmarks/fixtures/preflop_blueprint_hu10_full_v1.json --resources-dir out/preflop_blueprint_resources --buckets-dir out/preflop_blueprint_buckets_200_500_1000 --policy out/policy_full_dcfr_200.bin --certificate out/p7_cert_full.json --eval-flops 60 --threads 8 --iterations 2000 --output out/p8_chart_hu10_full.json
gtosd_preflop_blueprint_export ... --policy out/policy_full_dcfr_200.bin --query-history raise_3,call --query-hand AhKs --query-board 7s8d9c
gtosd_preflop_blueprint_export ... --policy out/policy_full_dcfr_200.bin --postflop-tree out/p8_postflop_tree_hu10_full.json
gtosd_preflop_blueprint_export ... --policy out/policy_full_dcfr_200.bin --threads 8 --iterations 2000 --serve
python tools/hu_preflop_chart_viewer/generate_chart_data.py --blueprint out/p8_chart_hu10_full.json --blueprint out/p8_chart_hu10_full_linear.json --blueprint out/p8_chart_hu10_reduced.json --postflop-tree out/p8_postflop_tree_hu10_full.json
python tools/hu_preflop_chart_viewer/serve_viewer.py --port 4173 --backend blueprint --executable <gtosd_preflop_blueprint_export.exe> --config benchmarks/fixtures/preflop_blueprint_hu10_full_v1.json --resources-dir out/preflop_blueprint_resources --buckets-dir out/preflop_blueprint_buckets_200_500_1000 --policy out/policy_full_dcfr_200.bin
gtosd_preflop_blueprint_compare --candidate out/p8_chart_hu10_full.json --baseline out/p8_chart_hu10_reduced.json --reference benchmarks/fixtures/hu_preflop_co40_reference_v1.json --output out/p8_compare_full_vs_reduced.json
python tools/validate_preflop_blueprint_chart.py out/p8_chart_hu10_full.json
```

## 9. Erratum e riesecuzione con le size preflop 5a/8a (2026-09-16)

**Erratum.** L'EV di classe fuori dalla radice (§2) era implementato senza la divisione per la
reach avversaria `D[h]`: gli `action_ev` dei 19 nodi interni negli export di §4 e nella verifica
del viewer di §5 erano scalati per `D[h]` (nel nodo `CO raise 5a → BTN all-in → CO` il fold
valeva −3,39 a per J6o e −3,14 a per AA invece della perdita costante). Frequenze, EV di radice,
certificati, badge e verdetti del comparatore non sono toccati. Correzione nel commit `46084f0`
(`preflop_action_values`: valore e serie per flop dell'errore standard) con un nuovo test: a ogni
nodo interno con un arco di fold l'EV di fold di ogni classe è uguale al payoff di fold entro
`1e-9`.

**Nuove size.** Per decisione dell'utente (2026-09-16) le fixture HU10 (completa e ridotta) hanno
una sola size preflop: open 5 a (full pot), risposta 8 a (superata da §10: nessuna size di
risposta, contro l'open solo fold, call e all-in). L'albero cambia, quindi policy,
certificati ed export HU10 di P6–P8 sono superati da quelli di questa sezione (`out/r2_*`,
`out/policy2_*`), prodotti con lo stesso protocollo: 2.000 iterazioni DCFR alternato con
`B = 32`, 8 thread, valutazione ogni 500 iterazioni su 20 flop; certificazione esatta P7; export
con 60 flop campionati per gli EV per azione.

| Gioco | Badge | Nodi / decisioni | Nodi preflop | EV radice (60 flop) | Exploitability esatta (573 flop, 605.088 board) | Stima a 20 flop (iterazione 2.000) | Training 2.000 it. | Certificazione | Export | Albero |
|---|---|---|---|---|---|---|---|---|---|---|
| HU10 completo (33/66/120 %) | `CERTIFIED_EXACT` | 1.567 / 612 | 12 | 0,1322 a | **0,0041 a** (0,14 % del piatto), nashconv 0,0075 a, limite inferiore 0,0014 a | 0,0905 ± 0,0609 a | 7 min + 4 min valutazioni | 31 min (3,2 s per flop) | 3 min | `fnv1a64:50bef9c82ac9c2d3` |
| HU10 ridotto (66 %) | `CERTIFIED_EXACT` | 259 / 108 | 12 | 0,1321 a | **0,0040 a** (0,13 % del piatto), nashconv 0,0070 a, limite inferiore 0,0013 a | 0,0906 ± 0,0609 a | 5 min + 30 s valutazioni | 4 min (0,4 s per flop) | 26 s | `fnv1a64:db42c9d8716d28a9` |

Esempio (export completo, nodo `CO raise 5a → BTN all-in → CO`): fold −6,000 a per J6o e −6,000 a per AA (il payoff di fold, costante), call −2,642 a per J6o e 4,528 a per AA. Albero postflop esportato: 600 nodi decisionali, 1.444 archi, 5 entry.

Il viewer è stato rigenerato con i due export (`generate_chart_data.py --blueprint … --postflop-tree …`)
e il server avviato sulla policy `policy2_full_dcfr_200.bin`; le classi con reach proprio lungo la
history sotto `1e-6` sono desaturate con il reach nel tooltip (commit `21617ef` del viewer).
Verifica nel browser (server locale sulla policy `policy2_full_dcfr_200.bin`, handshake
`Backend blueprint pronto · 2000 iterazioni · policy fnv1a64:245747207e0a6518`): badge
`CERTIFIED EXACT · exploitability 0.0041a (0.14% pot)` per il completo e `0.0040a (0.13% pot)` per
il ridotto; 12 nodi preflop (radice CO: all-in 32,2 %, raise 5a 42,0 %, call 0,0 %, fold 25,8 %;
AA: all-in 42,0 % EV +3,94 a, raise 5a 58,0 % EV +3,87 a); nodo `CO raise 5a → BTN all-in → CO`:
range call 99,7 % / fold 0,3 %, J6o segnata "quasi mai su questo percorso" (reach proprio
1,8·10⁻⁷ %) con call 97,56 % EV −2,642 a e fold 2,44 % EV −6,000 a (payoff di fold, uguale per
tutte le classi); vista postflop con l'albero nuovo (5 entry, 600 nodi decisionali, 1.444 archi)
e query sul board `Ac Kd Qh` dopo call e check: CO check 49,2 %, bet 1,32 a 40,8 %, bet 2,64 a
4,5 %, bet 4,8 a 4,7 %, all-in 0,8 %; AA EV +3,92 a su 32 runout campionati.

## 10. Contro l'open 5a solo fold/call/all-in, scaling e curva exploitability/iterazioni (2026-09-16)

**Regola dell'utente.** In HU10 contro l'open 5 a BTN ha solo fold, call e all-in: le fixture HU10
hanno `response_target_units: []` (decisione 46), l'albero completo passa a 1.501 nodi (584
decisioni; preflop 22 nodi, 8 decisioni, 3 entry postflop), il ridotto a 193 nodi. Gli export di
§9 sono superati da quelli di questa sezione (`out/r3_*`, `out/policy3_*`), stesso protocollo.

| Gioco | Badge | Nodi / decisioni | Nodi preflop | EV radice (60 flop) | Exploitability esatta | Stima a 20 flop (it. 2.000) | Training 2.000 it. | Certificazione | Export | Albero |
|---|---|---|---|---|---|---|---|---|---|---|
| HU10 completo (33/66/120 %) | `CERTIFIED_EXACT` | 1.501 / 584 | 8 | 0,1325 a | **0,0040 a** (0,13 % del piatto), limite inferiore 0,0017 a | 0,0931 ± 0,0632 a | 7 min + 5 min valutazioni | 35 min (3.7 s per flop) | 3 min | `fnv1a64:bc9e7b35ad8c021d` |
| HU10 ridotto (66 %) | `CERTIFIED_EXACT` | 193 / 80 | 8 | 0,1324 a | **0,0040 a** (0,13 % del piatto), limite inferiore 0,0016 a | 0,0932 ± 0,0636 a | 5 min + 26 s valutazioni | 3 min (0.3 s per flop) | 22 s | `fnv1a64:cc5c2f8eea9aac57` |

Albero postflop esportato: 3 entry. 576 nodi decisionali. 1.396 archi.

**Scaling sui thread** (HU10 completo, i3-10100F 4 core / 8 thread; training 100 iterazioni con
`B = 32`; certificatore 8 flop con `--chunk 8`):

| Thread | Training s/iterazione | Speedup | Certificatore s/flop | Speedup |
|---|---|---|---|---|
| 1 | 0,433 | 1,00 | 13,3 | 1,00 |
| 2 | 0,276 | 1,57 | 7,3 | 1,83 |
| 4 | 0,211 | 2,05 | 4,2 | 3,15 |
| 8 | 0,194 | 2,23 | 3,2 | 4,16 |

Con `--chunk 1` il certificatore resta a 13,4 s per flop a qualsiasi numero di thread (il
parallelismo è sui flop dello stesso chunk): le proiezioni misurate con chunk 1 (CO40-TEST, §9:
1,35 h) e chunk 2 (CO40 completo, P7 §5: 22,4 h) vanno corrette di 4,2 e 2,3 volte:
CO40-TEST ≈ 2,0 s per flop (≈ 20 min per la passata esatta), CO40 completo 64 s per flop canonico misurati su 8 flop con chunk 8 e 8 thread, passata esatta ≈ 10,2 h (era 22,4 h)
(decisione 47).

**Curva exploitability esatta / iterazioni** (DCFR alternato, `B = 32`, 8 thread; certificazione
esatta su 573 flop canonici):

| Albero | Iterazioni | Training (8 thread) | Exploitability esatta | % piatto | nashconv | Certificazione |
|---|---|---|---|---|---|---|
| HU10 ridotto | 10 | 2 s | 0,0470 a | 1,57 % | 0,0628 a | 3 min |
| HU10 ridotto | 20 | 3 s | 0,0191 a | 0,64 % | 0,0358 a | 3 min |
| HU10 ridotto | 30 | 5 s | 0,0167 a | 0,56 % | 0,0314 a | 3 min |
| HU10 ridotto | 50 | 9 s | 0,0131 a | 0,44 % | 0,0245 a | 3 min |
| HU10 ridotto | 100 | 17 s | 0,0103 a | 0,34 % | 0,0187 a | 3 min |
| HU10 ridotto | 200 | 32 s | 0,0071 a | 0,24 % | 0,0128 a | 3 min |
| HU10 ridotto | 400 | 63 s | 0,0058 a | 0,19 % | 0,0114 a | 3 min |
| HU10 ridotto | 700 | 109 s | 0,0050 a | 0,17 % | 0,0091 a | 3 min |
| HU10 ridotto | 1000 | 3 min | 0,0042 a | 0,14 % | 0,0077 a | 3 min |
| HU10 ridotto | 2000 | 5 min | 0,0040 a | 0,13 % | 0,0074 a | 3 min |
| HU10 completo | 50 | 9 s | 0,0302 a | 1,01 % | 0,0464 a | 32 min |
| HU10 completo | 200 | 37 s | 0,0111 a | 0,37 % | 0,0180 a | 33 min |
| HU10 completo | 2000 | 7 min | 0,0040 a | 0,13 % | 0,0076 a | 35 min |


Verifica nel browser (server locale sulla policy `policy3_full_dcfr_200.bin`): sorgente
"Blueprint HU10 (open 5a, risposta solo all-in) - DCFR alternato, certificazione esatta", 8 nodi
preflop; radice CO: raise 5a 43,3 %, all-in 31,0 %, fold 25,7 % (AA: raise 5a 58,6 % EV +3,86 a,
all-in 41,4 % EV +3,94 a); nodo `CO raise 5a → BTN`: solo all-in 57,3 %, call 22,6 %, fold 20,1 %
(AA: all-in 88,8 % EV +4,47 a, call 11,2 % EV +4,11 a, fold EV −2,000 a); vista postflop con
l'albero nuovo (3 entry, 576 nodi decisionali, 1.396 archi) e query sul board `Ac Kd Qh` dopo
call e check: CO check 46,2 %, bet 1,32 a 43,3 %, bet 2,64 a 8,5 %, bet 4,8 a 0,6 %, all-in 1,4 %;
AA EV +3,64 a su 32 runout campionati.
