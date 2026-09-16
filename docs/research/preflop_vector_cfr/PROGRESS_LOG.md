# Diario dell'agent coder: solver preflop vettoriale

Roadmap: [PREFLOP_VECTOR_CFR_CODER_ROADMAP_2026-09-15.md](../PREFLOP_VECTOR_CFR_CODER_ROADMAP_2026-09-15.md)
Registro decisioni: [PREFLOP_ARCHITECTURE_DECISION_LOG.md](../PREFLOP_ARCHITECTURE_DECISION_LOG.md)

Regole del diario: le voci non si cancellano; una correzione è una nuova voce che rimanda alla
precedente. Un fallimento si registra prima di tentare la correzione. Aggiornare a fine di ogni
sessione, a ogni gate e a ogni dubbio bloccante.

## 1. Stato corrente

| Campo | Valore |
|---|---|
| Fase in corso | P8 chiusa (gate PASS, viewer incluso); correzione post-gate dell'EV e nuove size HU10 (2026-09-16); P9 da avviare |
| Ultimo gate | P8 PASS (2026-09-16) |
| Branch di integrazione | `feature/preflop-blueprint` |
| Branch di fase | `feature/preflop-blueprint-p8-hu10-sizes` (da `feature/preflop-blueprint`; P0–P8 uniti nell'integrazione) |
| Worktree | `C:/tmp/gtosd-preflop-blueprint` |
| Commit di partenza | `main` a `55ed6ef`; il tag `preflop-legacy-es-2026-09-15` è su `04aa687` |
| Build | `out/build/windows-release` nel worktree (Release, MSVC 19.51, Ninja 1.13.2) |
| Merge su `main` | eseguito dall'utente il 2026-09-16 (`97d8121`, tag P3/P6/P8); il completamento di P8 (viewer) è unito nell'integrazione e in `main` con lo stesso mandato; `main` non è pushato (non richiesto); correzione EV e size HU10 5a/8a unite in integrazione (`f047484`) e in `main` (`9c68a63`) il 2026-09-16, branch di fase e integrazione pushati |
| Prossimo passo | P9: qualificazione CO40 (albero completo: certificato esatto ≈ 10 h a 8 thread; variante di test a una size postflop: ≈ 20 min) secondo l'indicazione dell'utente; export e confronto Monker descrittivo |

## 2. Registro dei gate

| Fase | Esito | Data | Commit | Report |
|---|---|---|---|---|
| P0 Contratto e scaffolding | PASS | 2026-09-15 | `ef6f691` | [P0_SCAFFOLDING.md](P0_SCAFFOLDING.md) |
| P1 Canonicalizzazione e cataloghi | PASS | 2026-09-15 | `ca80dab` | [P1_CANONICAL_BOARDS.md](P1_CANONICAL_BOARDS.md) |
| P2 Risorse esatte | PASS | 2026-09-15 | `9f8a6d3` | [P2_EXACT_RESOURCES.md](P2_EXACT_RESOURCES.md) |
| P3 Clustering e tabelle bucket | PASS | 2026-09-15 | `c7bb762` | [P3_BUCKET_TABLES.md](P3_BUCKET_TABLES.md) |
| P4 Modello di gioco e albero compilato | PASS | 2026-09-15 | `89f159e` | [P4_GAME_MODEL.md](P4_GAME_MODEL.md) |
| P5 Kernel vettoriale HU | PASS | 2026-09-15 | `738e361` | [P5_VECTOR_KERNELS.md](P5_VECTOR_KERNELS.md) |
| P6 Trainer con campionamento del board | PASS | 2026-09-16 | `c5ccef1` | [P6_TRAINER.md](P6_TRAINER.md) |
| P7 Certificatore board-major | PASS | 2026-09-16 | `01b7ca4` | [P7_CERTIFIER.md](P7_CERTIFIER.md) |
| P8 Export, query, comparatore, viewer | PASS | 2026-09-16 | `cdd3481`, `b41cee2` | [P8_EXPORT.md](P8_EXPORT.md) |
| P9 Qualificazione CO40 e archiviazione | NOT_RUN | | | |
| P10 Conteggio alberi 3-way | NOT_RUN | | | |

Esiti ammessi: `PASS`, `FAIL`, `INCONCLUSIVE`, `NOT_RUN`.

## 3. Diario

Formato di ogni voce:

```text
### AAAA-MM-GG — Px — titolo breve
Fatto: ...
Comandi: ...
Risultati: numeri, tempi, memoria, fingerprint
Fallimenti: cosa, causa identificata o ipotesi, cosa si è provato
Dubbi: ...
Prossimo passo: ...
```

### 2026-09-16 — P8 — contro l'open 5a solo fold/call/all-in, scaling sui thread, curva exploitability/iterazioni, erratum sulle proiezioni del certificatore

Fatto: (1) Domande dell'utente: come essere certi della convergenza a Nash (la exploitability
esatta certificata da P7 è la distanza da un equilibrio nel gioco fisico con questo albero:
nessuna strategia guadagna più di 0,0041 a per mano contro il blueprint, 0,14 % del piatto),
differenza fra le due sorgenti del viewer (stesso algoritmo DCFR alternato, alberi diversi: tre
size postflop contro una), tempi su un AMD EPYC 7443, costo di una exploitability dell'1 % del
piatto, stato della documentazione. (2) Regola dell'utente: in HU10 contro l'open 5 a BTN ha solo
fold, call e all-in. Il loader accetta una lista di risposte vuota (nessuna size di rilancio
sopra un open: al livello 1 solo fold/call/all-in, decisione 46), schema con `minItems: 0`,
validatore Python delle fixture aggiornato, fixture HU10 con `response_target_units: []`, test
dello scaffolding (lista vuota accettata, casi di rifiuto spostati sulla fixture CO40). Alberi:
HU10 completo 1.501 nodi (584 decisioni; preflop 22 nodi, 8 decisioni, 3 entry postflop),
ridotto 193 nodi (80 decisioni). Nell'export precedente BTN usava il rilancio a 8 a contro
l'open 5 a per il 6 % del range (fold 20 %, call 19 %, all-in 55 %). (3) Scaling sui thread su
HU10 completo (i3-10100F, 4 core / 8 thread): training di 100 iterazioni a 1/2/4/8 thread;
certificatore su 8 flop con `--chunk 1` e con `--chunk 8`. (4) Curva della exploitability esatta
in funzione delle iterazioni (albero ridotto: 50, 100, 200, 400, 700, 1.000, 2.000; albero
completo: 2.000 più i punti attorno all'1 % del piatto), run di riferimento a 2.000 iterazioni
certificati ed esportati (`out/r3_*`, `out/policy3_*`), viewer rigenerato. Report:
[P8_EXPORT.md](P8_EXPORT.md) §10.
Comandi: `ctest -L "p0|p4|p6|p7|p8"`; `gtosd_preflop_blueprint_train …hu10_full_v1.json --iterations 100
--batch 32 --threads T --eval-every 0`; `gtosd_preflop_blueprint_certify …hu10_full_v1.json --uniform
--threads T --chunk 8 --flop-limit 8`; per N in 50…1000: `train …hu10_reduced_v1.json --iterations N
--eval-every 0 --policy-out out/curve_red_policy_N.bin` e `certify --policy … --threads 8 --chunk 16`;
run di riferimento come nella voce precedente (`policy3_*`, `r3_cert_*`, `r3_chart_*`).
Risultati: test 15/15 PASS (172 s). Scaling del training: 0,433 / 0,276 / 0,211 / 0,194 s per
iterazione a 1/2/4/8 thread (2,2× a 8 thread; frazione seriale ≈ 0,32 con 30 unità e 10 nodi in
alto seriali). Certificatore con `--chunk 8`: 13,3 / 7,3 / 4,2 / 3,2 s per flop (4,2× a 8 thread,
1,32× dai thread SMT); con `--chunk 1`: 13,4 s per flop a qualsiasi numero di thread, perché il
parallelismo è sui flop dello stesso chunk. Curva (P8 §10): ridotto: 0,0191 a (0,64 % del piatto) a 20 iterazioni, training 3 s; completo: 0,0111 a (0,37 % del piatto) a 200 iterazioni, training 37 s; a 2.000 iterazioni 0,0040 a (ridotto) e 0,0040 a (completo). Run di riferimento con la nuova regola: completo 1.501 nodi, training 7 min (+ 5 min di valutazioni), certificato esatto **0,0040 a** in 35 min, EV di radice 0,1325 a; ridotto 193 nodi, training 5 min, certificato esatto 0,0040 a in 3 min; albero postflop 3 entry, 576 nodi decisionali, 1396 archi. CO40 completo con chunk 8 e 8 thread: 64 s per flop canonico misurati su 8 flop con chunk 8 e 8 thread, passata esatta ≈ 10,2 h (era 22,4 h).
Fallimenti: (1) Il validatore Python delle fixture imponeva liste di open e di risposte della
stessa lunghezza anche con risposte vuote: test dello schema FAIL, corretto. (2) **Erratum sulle
proiezioni del certificatore**: le misure con `--chunk 1` (CO40-TEST, voce precedente: 8,5 s per
flop, passata esatta 1,35 h) e con `--chunk 2` (CO40 completo, P7 §5: 141 s per flop, 22,4 h)
avevano rispettivamente uno e due thread attivi, non otto; a 8 thread con chunk ≥ 8 il costo è
circa 4,2 (da chunk 1) e 2,3 (da chunk 2) volte minore. CO40-TEST: ≈ 2,0 s per flop, passata
esatta ≈ 20 min. CO40 completo: 64 s per flop canonico misurati su 8 flop con chunk 8 e 8 thread, passata esatta ≈ 10,2 h (era 22,4 h) (decisione 47). (3) Lo script di
interruzione della catena ha terminato anche il proprio lanciatore (il pattern sul nome dello
script compariva nella sua riga di comando): catena rilanciata separatamente.
Dubbi: (1) Il training scala poco (2,2× su 4 core): la parte alta seriale e le 30 unità di HU10
limitano il parallelismo; su CO40 completo (211 unità) la frazione parallela è maggiore ma non
misurata. (2) La stima per l'EPYC 7443 assume una velocità per core simile all'i3-10100F (Zen 3 a
3,6–4,0 GHz con IPC maggiore contro Comet Lake a 4,1–4,3 GHz) e scaling del certificatore
lineare sui flop con chunk ≥ thread: va verificata sulla macchina. (3) La curva exploitability /
iterazioni è misurata su HU10: su CO40 la forma può essere diversa (albero più profondo).
Prossimo passo: merge del branch di fase nell'integrazione e in `main`, push dei branch; P9 sul CO40 completo (passata esatta ≈ 10 h a 8 thread su questa macchina) o sulla variante di test, secondo l'utente.

### 2026-09-16 — P8 — EV condizionato corretto fuori dalla radice, size preflop HU10 solo 5a/8a, CO40 di test

Fatto: (1) L'utente ha chiesto perché `J6o` compare nel nodo `CO raise 5a → BTN all-in → CO`
del viewer se CO non rilancia mai J6o. L'export riporta la strategia media di tutte le 81 classi a
ogni nodo: alla radice CO rilancia a 5 a J6o con frequenza 3,1·10⁻⁹ (residuo delle prime
iterazioni nella media DCFR), che è il reach mostrato dal viewer (3,12·10⁻⁷ %); la riga è quindi
attesa. L'analisi ha però trovato un errore negli EV fuori dalla radice: l'EV di classe usava il
valore controfattuale `v_a[h]` (già moltiplicato per la reach avversaria `D[h]`) pesato di nuovo
con `D[h]`, invece di `v_a[h] / D[h]`; alla radice `D[h] = 1` e il test di ricostruzione
passava, ai nodi interni l'EV era scalato per `D[h]` (fold −3,39 a per J6o e −3,14 a per AA nello
stesso nodo invece della perdita costante). Correzione in `preflop_action_values` (valore e serie
per flop dell'errore standard, decisione 43) e nuovo test: a ogni nodo interno con arco di fold
l'EV di fold di ogni classe è uguale al payoff di fold entro `1e-9`. (2) Decisione dell'utente:
le size preflop di HU10 diventano solo full pot (open 5 a, risposta 8 a) in entrambe le fixture
(completa e ridotta); test dello scaffolding e del gioco, smoke di query (`raise_5,call`) e
fixture aggiornati. (3) Decisione dell'utente: variante CO40 solo per i test con una size postflop
(100 % del piatto) più all-in, fixture `preflop_blueprint_co40_test_v1.json`
(`PREFLOP-BLUEPRINT-CO40-TEST-001`, stesse size preflop Monker 6 / 10 a e 10,5 / 14,5 a,
decisione 44), misure di tempo su 20 iterazioni e 2 flop canonici. (4) Riaddestramento HU10
(completo e ridotto) con le nuove size, certificazione esatta, export delle chart e dell'albero
postflop, rigenerazione del viewer. (5) Viewer: le classi con reach proprio lungo la history sotto
`1e-6` restano nella griglia ma desaturate, con il reach nel tooltip (decisione 45, commit
`21617ef` del viewer). Report: [P8_EXPORT.md](P8_EXPORT.md) §9.
Comandi: `cmake --build … --target <16 eseguibili blueprint>` e `ctest -L "p0|p4|p6|p7|p8"`;
`gtosd_preflop_blueprint_game --config …co40_test_v1.json`; `gtosd_preflop_blueprint_train …co40_test_v1.json
--iterations 20 --batch 32 --threads 8 --eval-every 0`; `gtosd_preflop_blueprint_certify
…co40_test_v1.json --uniform --threads 8 --chunk 1 --flop-limit 2`; per HU10 completo e ridotto:
`train --iterations 2000 --batch 32 --threads 8 --eval-flops 20 --eval-every 500 --policy-out out/policy2_*.bin`,
`certify --threads 8 --chunk 16 --state out/r2_state_*.bin`, `export --certificate out/r2_cert_*.json
--eval-flops 60 --threads 8` (chart e albero postflop); `generate_chart_data.py --blueprint out/r2_chart_hu10_full.json
--blueprint out/r2_chart_hu10_reduced.json --postflop-tree out/r2_postflop_tree_hu10_full.json`.
Risultati: test 15/15 PASS (191 s). CO40-TEST: 1.129 nodi (456 decisioni, 20 preflop) contro
27.012 (10.060) di CO40; stato 16,9 MB con 200/500/1.000; training 0,223 s per iterazione
(`B = 32`, 8 thread; CO40 completo ≈ 3 s, HU10 completo 0,27 s); certificazione 8,5 s per flop
canonico (CO40 completo 141 s): passata esatta su 573 flop ≈ 81 min (1,35 h) contro 22,4 h;
valutazione campionata D3 con `M = 1.000` ≈ 2,4 h (più della passata esatta, come su HU10), con
`M = 200` ≈ 28 min. Stima della risoluzione di CO40-TEST: 2.000 iterazioni 7,5 min, 10.000
iterazioni 37 min, più valutazioni periodiche (`M = 20` ≈ 3 min ciascuna) e certificazione esatta
di 1,35 h: fra 1,5 e 2,5 h in totale contro circa un giorno per CO40 completo (non è noto quante
iterazioni servano a CO40 per D3: HU10 ne ha richieste 2.000). HU10 con le nuove size:
completo 1.567 nodi, training 7 min (+ 4 min di valutazioni), certificato esatto **0,0041 a** in 31 min; ridotto 259 nodi, training 5 min, certificato esatto 0,0040 a in 4 min; EV di radice 0,1322 a (completo) e 0,1321 a (ridotto); export con badge `CERTIFIED_EXACT`; Esempio (export completo, nodo `CO raise 5a → BTN all-in → CO`): fold −6,000 a per J6o e −6,000 a per AA (il payoff di fold, costante), call −2,642 a per J6o e 4,528 a per AA. Albero postflop esportato: 600 nodi decisionali, 1.444 archi, 5 entry.
Fallimenti: (1) `LNK1104` su `gtosd_preflop_blueprint_export.exe`: il worker `--serve` del viewer
teneva aperto l'eseguibile; server fermato prima della build. (2) Lo script di build della catena
non ricompilava `gtosd_preflop_blueprint_scaffold_tests` (elenco parziale di target) e il test
vecchio falliva sulle nuove fixture: lo script costruisce ora tutti i 16 eseguibili blueprint.
(3) L'errore dell'EV non era coperto dai test: il controllo di ricostruzione era solo alla radice,
dove `D[h] = 1`; gli export `p8_chart_hu10_*` e la verifica del viewer di P8 §4–§5 hanno EV
fuori dalla radice sbagliati (frequenze, EV di radice, badge e verdetti corretti): superati dagli
export `r2_*` (P8 §9).
Dubbi: (1) Il "da non fare" di P9 (non cambiare albero o size) riguarda la qualificazione: la
variante CO40-TEST serve alle misure di tempo e alle prove; il gate P9 resta sull'albero CO40
completo salvo diversa indicazione dell'utente. (2) Con una sola size postflop l'albero CO40 di
test ha 24 volte meno nodi del completo: le proiezioni non si trasferiscono linearmente alle tre
size. (3) Le size preflop HU10 dell'utente (solo 5 a / 8 a) rendono i risultati HU10 di P6–P8 non
confrontabili con i nuovi: i vecchi restano nei report come storia.
Prossimo passo: merge del branch di fase nell'integrazione e in `main` (autorizzazione del
2026-09-16), push dei branch; P9 sul CO40 completo o sulla variante di test secondo l'utente.

### 2026-09-16 — P8 — viewer aggiornato e domande risolte, gate PASS

Fatto: l'utente ha risposto alle domande aperte: Q1 branch pubblicati su origin (10 branch
`feature/preflop-blueprint*` e 3 tag), Q2/Q3 merge dell'integrazione in `main` eseguito nel suo
working tree (`97d8121`, tag `preflop-blueprint-p3-abstraction` su `981361e`,
`preflop-blueprint-p6-hu10` su `22e1015`, `preflop-blueprint-p8-export` su `9360836`; `main` non
pushato: non richiesto), Q4 viewer modificabile. Nel repository: export dell'albero pubblico
postflop nello schema del viewer, sonda per nodo nel valutatore, worker di query postflop
(`--serve`) con frequenze, reach ed EV per classe (esatti a turn e river, runout campionati al
flop), test e smoke. Nel repository del viewer (branch `feature/preflop-blueprint-p8-export`,
modifiche locali preesistenti conservate in `56cdf31`, aggiornamento `4edbb45`): generatore con
sorgenti blueprint e badge, frontend con azioni e gioco dinamici, server con backend blueprint,
validatori generici, README. Report: [P8_EXPORT.md](P8_EXPORT.md) §5.
Comandi: `ctest -L p8 -V`; export dell'albero HU10; `generate_chart_data.py --blueprint … --postflop-tree …`;
`serve_viewer.py --backend blueprint …`; verifica nel browser integrato.
Risultati: `ctest -L p8` 7/7; worker: EV di classe al flop uguale al valutatore entro `1e-9`,
flop 0,4–1,3 s (16–64 runout), turn 0,5 s esatto, river istantaneo; viewer: "HU 10a Chart Viewer",
badge `CERTIFIED EXACT · exploitability 0.0042a (0.14% pot)`, 20 nodi preflop e albero postflop
(9 entry, 792 nodi) navigabili, query sul board `Ac Kd Qh` con frequenze ed EV per classe.
Fallimenti: (1) `reference?.ev !== null` nel viewer con riferimento assente: eccezione nel render
della matrice, guardia aggiunta. (2) Il flop con tutti i 1.056 runout costa 17,6 s nel worker
(sonda sequenziale sui turn): il viewer usa runout campionati; parallelizzazione rinviata.
(3) Le commit `b41cee2` e `9d05224` sono nate direttamente sul branch di integrazione
invece che sul branch di fase (checkout non tornato sul branch dopo il merge): il branch di
fase è stato riallineato a `9d05224` con fast-forward e la deviazione da D21 è registrata qui.
Dubbi: (1) Le size preflop di HU10 (open 3 a / 5 a, risposte 6 a / 8 a) vengono dal fixture di
calibrazione legacy `hu_preflop_hu10_calibration_v1.json` (D6, commit `04aa687`) e non sono state
scelte in questo programma: HU10 è solo il gioco di validazione, CO40 usa il contratto Monker
(6 a / 10 a, 10,5 a / 14,5 a). (2) La stima di 22 h per la certificazione esatta di CO40 riguarda
solo il certificatore P7 (605.088 board a 1,07 s per board per thread, dominati dai kernel all-in
di flop e turn): il training CO40 costa circa 3 s per iterazione (10 volte HU10), quindi ore, non
giorni; le riduzioni possibili sono descritte in P7 §5.
Prossimo passo: P9 sul branch `feature/preflop-blueprint-p9-co40`.

### 2026-09-16 — P8 — export, query, comparatore, viewer, gate PASS con riserva (viewer INCONCLUSIVE)

Fatto: id stabili di azione e di nodo (`action_labels`), query della policy per history + combo +
board senza ricalcolo di feature (`policy_query`), valori per azione ai nodi preflop con EV
condizionato per classe ed errore standard sui flop campionati
(`BestResponseEvaluator::preflop_action_values`), export `gtosd.preflop_blueprint_chart.v1` con
schema JSON, quattro fingerprint, badge `ESTIMATED` / `CERTIFIED_EXACT` e checksum nel layout
letto dal generatore del viewer, comparatore con verdetto sulla exploitability fisica (D2) e
distanze Monker descrittive (`EXTERNAL_CONTRACT_INCOMPLETE`), validatore statico registrato in
CTest, eseguibili `gtosd_preflop_blueprint_export` (export e query) e
`gtosd_preflop_blueprint_compare`, smoke a catena tramite fixture CTest. Report:
[P8_EXPORT.md](P8_EXPORT.md).
Comandi: `ctest -L p8 -V`; export di HU10 ridotto e completo dalle policy P6 con i certificati
P7 (60 flop, 8 thread); export senza certificato e baseline Linear; query di esempio;
comparatore candidato/baseline/riferimento; validatore; suite `preflop_blueprint`.
Risultati: test 20.380 asserzioni PASS (812 nodi decisionali etichettati, 160 cammini di query
su tutte le strade uguali al `BoardContext`, EV di radice ricostruito dalle classi entro `1e-9`,
verdetti del comparatore, parser Monker a distanza zero su un riferimento sintetico); export HU10
completo certificato: 20 nodi, 1.620 righe, badge `CERTIFIED_EXACT` con 0,0042 a esatti, 239 s
per 60 flop; comparatore `QUALIFIED` per il candidato certificato, `INCONCLUSIVE_ESTIMATE` per lo
stesso senza certificato (stima 0,061 ± 0,05 a contro 0,1 a), `STALE_TREE` fra alberi diversi,
DCFR contro Linear sullo stesso albero confrontati su 20 nodi; validatore PASS sui tre export.
Suite `preflop_blueprint`: PASS: 23/23 in 505 s (`ctest -L preflop_blueprint`, Release, dopo la correzione del check di isolamento).
Fallimenti: (1) helper JSON `quoted` in conflitto con `std::quoted` per ADL; (2) target
`nlohmann_json` non visibile nella directory dei test; (3) cammini casuali del test di query
troppo brevi per il river; (4) smoke con `DEPENDS` non eseguiti fuori etichetta: fixture CTest.
Dubbi: (1) l'EV per azione è condizionato al nodo (diviso per la reach avversaria): nei nodi
profondi con reach piccola gli errori standard sono grandi a 60 flop; il viewer dovrebbe mostrare
la reach. (2) La strategia corrente non è nel file di policy: se il viewer la vuole, va aggiunta
al formato `GTOSDPOL` (opzionale, diagnostica). (3) Il riferimento Monker CO40 non è confrontabile
con HU10 (azioni diverse); il confronto ha senso solo in P9.
Prossimo passo: decisione dell'utente su Q4 (viewer) e Q2/Q3 (merge in `main`); poi P9 sul
branch `feature/preflop-blueprint-p9-co40` con la passata esatta CO40 pianificata come lavoro a
chunk ripristinabile (22 h stimate) o ridotta con le ottimizzazioni indicate in P7.

### 2026-09-16 — P7 — certificatore board-major, gate PASS

Fatto: valutatore di best response riorganizzato in due stadi (`BestResponseEvaluator`: valori
per flop, aggregazione preflop) con immagini di orbita: ogni flop canonico valutato una volta con
tutti i runout e sommato su tutte le sue immagini nei semi (la strategia è simmetrica: valore di
`h` su `σ(F)` = valore di `σ⁻¹(h)` su `F`), 573 flop canonici per 7.140 fisici e 605.088 board;
certificatore a chunk paralleli con stato ripristinabile (record per flop con checksum, header con
fingerprint di albero, policy e catalogo), passate parziali per la misura, comando campionato (lo
stimatore P6), certificato JSON `gtosd.preflop_blueprint_certificate.v1`; file di policy
`GTOSDPOL` scritto dal trainer (`--policy-out`) e letto dal certificatore; helper binari
condivisi; header di test condiviso. Report: [P7_CERTIFIER.md](P7_CERTIFIER.md).
Comandi: `ctest -L p7 -V`; esportazione delle policy dai checkpoint P6 (HU10 ridotto e completo,
DCFR 200/500/1.000, 2.000 iterazioni); `gtosd_preflop_blueprint_certify` esatto su HU10 ridotto e
completo (8 thread, chunk 16, stato su file); parziale su CO40 (policy uniforme, 4 flop) per la
proiezione; campionato a 60 flop su HU10 completo; suite `preflop_blueprint`.
Risultati: test 373.068 asserzioni PASS (orbite = enumerazione fisica entro `1e-12` per combo e
in aggregato; ripresa bit-identica; comando campionato = trainer entro `1e-12`; round trip della
policy). Passata esatta: HU10 ridotto EV CO 0,136084 a, guadagni 0,003031 /
0,004200 a, nashconv 0,007231 a, limite inferiore 0,001379 / 0,000905 a, 453 s (0,79 s per flop
canonico, 8 thread), 251 MB; HU10 completo EV CO 0,136090 a, guadagni 0,003118 / 0,004228 a,
nashconv 0,007346 a, 2.220 s (ripresa da 16 flop dopo l'interruzione della sessione), 324 MB;
`EV_CO + EV_BTN = 0` entro `1e-16`. Exploitability vera del blueprint HU10: 0,0042 a = 0,14 % del
piatto (D3 1 %, D2 0,1 a). Stimatore P6 sugli stessi checkpoint: naive a 1.000 flop 0,0187 ±
0,0079 a, limite inferiore 0,0014 a: il valore esatto sta nell'intervallo e il bias della naive
(0,0145 a) è `0,46/√1000` come misurato in P6. CO40 parziale (4 flop canonici, 40 fisici, 4.224
board, policy uniforme): 563 s, 141 s per flop canonico, 1,07 s per board per thread, proiezione
22,4 h per la passata esatta con 8 thread. Suite `preflop_blueprint`: 18/18 PASS in 506 s.
Fallimenti: (1) `Result<T,E>` richiede `T` costruibile per default: `load_policy` restituisce un
`unique_ptr`. (2) Invariante "EV a somma zero" applicato a un sottoinsieme di flop: vale solo sul
catalogo completo perché i due giocatori condizionano su conteggi di flop compatibili diversi;
test limitato alla passata esatta. (3) Script di refactoring degli helper binari lasciato a metà
(parentesi residua in `trainer.cpp`): corretto al primo build.
Dubbi: (1) Su CO40 il costo per board (1,07 s) è 12 volte la traversata P5 per i
terminali all-in di flop e turn (kernel showdown per terminale e per board): le riduzioni
(kernel all-in per (flop, turn), simmetrie sotto lo stabilizzatore, `float`) sono rinviate dopo
P9 per §3.4, ma la passata esatta CO40 va pianificata come lavoro notturno con ripresa. (2)
L'invariante "EV a somma zero" vale solo sul catalogo completo: sulle stime campionate i due
giocatori condizionano su insiemi di flop compatibili diversi; da tenere presente nel viewer P8
quando mostra EV campionati.
Prossimo passo: merge di P7 nell'integrazione; P8 (export, query, comparatore, viewer) sul
branch `feature/preflop-blueprint-p8-export`, con il certificato P7 come fonte del numero
dichiarato e il file di policy come formato di scambio.

### 2026-09-16 — P6 — trainer con campionamento del board, gate PASS

Fatto: trainer CFR vettoriale con campionamento pubblico del board (batch `B`, un passaggio per
giocatore, snapshot della strategia per passaggio, Linear/DCFR una volta per iterazione,
partizione dell'albero in unità indipendenti dal numero di thread con un solo scrittore per
cella, hook esatti per liste di board pesate e sottoinsiemi di mani, checkpoint atomico con
checksum e identità, telemetria); oracolo `FiniteGame` a bucket sul gioco ridotto; valutatore di
best response fisica **non chiaroveggente** (`best_response.hpp`): valori aggregati sulle carte
future prima del massimo, campionamento di `M` flop con tutti i 33 × 32 runout, errore standard
sui flop, stima naive più limite inferiore senza selezione (strategia media al preflop, best
response esatta dal flop in poi); test contro il `FiniteGame` lossless; eseguibile con
`--eval-flops`, `--eval-only`, `--eval-seed` dopo il caricamento, `--fixed-boards`,
`--permute-suits`, default DCFR alternato. Report: [P6_TRAINER.md](P6_TRAINER.md).
Comandi: `ctest -L p6 -V`; `gtosd_preflop_blueprint_train` su HU10 completo e ridotto (2.000
iterazioni, `B = 32`, 8 thread, 20 flop ogni 250 iterazioni) con DCFR alternato e Linear
simultaneo e con le tabelle 50/100/200, 200/500/1.000, 500/1.000/2.000; `--fixed-boards 1/4/8/64`
sul ridotto (valutazione esatta sulla lista); `--eval-only` sui checkpoint finali con 60 flop,
scansione `M = 5…160` sul ridotto e `M = 1.000` per il gate; suite `preflop_blueprint` completa.
Risultati: oracolo regret entro `1,7·10⁻¹³`, strategia media entro `1e-9`; bit-identità 1/2/4/8
thread e partizioni 1/8/58 unità; ripresa identica; best response fisica = `calculate_nash_conv`
del `FiniteGame` lossless entro `1e-9` (nashconv 0,0931106). HU10 completo, DCFR alternato,
200/500/1.000: 0,31 s per iterazione, valutazione a 20 flop 93–122 s (21.120 board, circa 42 ms
per board per thread), memoria 245 MB; massimo guadagno naive 0,099 / 0,108 / 0,084 / 0,091 /
0,108 / 0,137 / 0,122 / 0,085 a alle iterazioni 250…2.000 (semiampiezza 0,04–0,10 a), EV CO
0,136 a. Stesse iterazioni e stessi flop: HU10 ridotto 0,100…0,085 a; 50/100/200 0,098…0,087 a;
500/1.000/2.000 0,104…0,087 a; Linear simultaneo 0,18…0,24 a. Scansione di `M` sul checkpoint
ridotto: naive 0,239 / 0,171 / 0,093 / 0,066 / 0,045 / 0,037 a per `M = 5…160` (`naive · √M` ≈
0,4–0,5 a costante), limite inferiore 0,0005–0,0013 a. Checkpoint completi a `M = 60`: naive
0,045 / 0,048 / 0,046 a (± 0,019) per 200/500/1.000, 50/100/200, 500/1.000/2.000 con limite
inferiore 0,0013 / 0,0019 / 0,0010 a; Linear simultaneo naive 0,143 a, limite 0,0031 a. Board
fissi (valutazione esatta sulla lista, DCFR): 1 board 0,014 a a 100 iterazioni (regola D3
soddisfatta), 4 board 0,014 a, 8 board 0,025 a, 64 board 0,111 a, piatti fra 100 e 500
iterazioni (chiaroveggente: `1·10⁻⁴` / 0,105 / 0,163 / 0,473 a). Gate a `M = 1.000` flop
(1.056.000 board): massimo guadagno naive 0,0187 a + semiampiezza
0,0079 a = 0,0266 a ≤ 0,03 a (guadagni [0,0108, 0,0187] a, limite inferiore [0,0014, 0,0009] a,
4.796 s, 98 MB): `PREFLOP_BLUEPRINT_TRAIN=CONVERGED`, gate PASS. Suite `preflop_blueprint`: 16/16 PASS in 318 s.
Fallimenti: (1) lo stimatore P6.3 come prescritto (massimo per board) era chiaroveggente sopra
il river: plateau di circa 1 a su HU10 diagnosticato prima come pavimento dell'astrazione, poi
smentito dal confronto di capacità e dal test a semi ruotati; circa tre ore di esperimenti da
scartare, erratum alla roadmap §5, decisioni 30–31. (2) La stima naive sui flop campionati
sceglie e valuta le azioni preflop sugli stessi flop: bias `≈ 0,4/√M` a; la cross-fit provata
era negativa a ogni `M` e va scartata; aggiunto il limite inferiore senza selezione (decisione
32). (3) Semiampiezza non nulla sulle valutazioni esatte per lista: corretta a zero. (4)
`--eval-only` con `--eval-seed` diverso respinto per identità: il seme ora riavvia l'RNG dopo il
caricamento (decisione 33). (5) Errori del test di partizione (batch, identità, RNG) e link
mancante di `gtosd::best_response`: vedi report.
Dubbi: (1) tre capacità e due alberi danno curve naive identiche alla terza cifra perché la
stima a 20 flop è dominata dal rumore di selezione comune; la capacità si vede solo nel limite
inferiore (1,9 → 1,3 → 1,0 millesimi di ante), tutto sotto D3. (2) Il gioco ristretto a 64 board
fissi ha best response 0,111 a con un solo runout per flop: non misura l'astrazione del gioco
completo, dove il limite inferiore è di millesimi; la modalità a board fissi serve solo per la
convergenza. (3) La valutazione costa più del training (882 s contro 618 s su 2.000 iterazioni)
e la stima campionata per D3 richiede `M ≈ 1.000` flop (circa 95 min su HU10 completo): la
passata esatta di P7 su 573 flop canonici (605.088 board, circa 50 min) è più economica ed
esatta; conviene usare la stima campionata con `M` piccolo solo per la curva e certificare con P7.
(4) Nella stima campionata il giocatore più "sfruttabile" è il BTN, nel gioco a 64 board il CO:
entrambi effetti del rumore/della restrizione, non della strategia.
Prossimo passo: merge di P6 nell'integrazione; il gate P6 prevede anche il merge dell'integrazione
in `main` con tag `preflop-blueprint-p6-hu10` (Q3, stesso blocco di Q2); P7 sul branch
`feature/preflop-blueprint-p7-certifier` con la stessa aggregazione non chiaroveggente su tutti i
flop canonici.

### 2026-09-15 — P5 — kernel vettoriale HU, gate PASS

Fatto: contesto di board (465 mani vive, rank dalla tabella P2, ordine per rank, classi, bucket
P3 via permutazione canonica, incidenza per carta); kernel fold `D = S − C[h1] − C[h2] + r[h]`,
showdown a due passate con correzione dei blocker e gruppi di pari rank, cache all-in preflop
per board dalla tabella esatta; interfaccia `ShowdownKernel` a N reach (D13) con implementazione
HU; traversata dei valori senza allocazioni con policy a bucket e per mano, potatura a reach nulla
e modalità best response; compilazione di sottogiochi da uno stato arbitrario; test contro i
riferimenti pairwise, contro la ricorsione per coppia e contro il solver postflop `ProductionDcfr`
(solo test, D5); eseguibile di misura. Report: [P5_VECTOR_KERNELS.md](P5_VECTOR_KERNELS.md).
Comandi: build dei target P5; `ctest -L p5 -V`; `gtosd_preflop_blueprint_traversal` su CO40 e
HU10; configure `windows-asan` (RelWithDebInfo, `/fsanitize=address /bigobj`) e
`ctest -R "gtosd_preflop_blueprint_(kernel|game|oracle)_tests"`; suite CTest completa (65 test)
sull'integrazione dopo il merge di P3.
Risultati: kernel 1.636.010 asserzioni PASS in 7,7 s (200 board × 3 pattern di reach entro
`1e-12`; traversata contro ricorsione per coppia con errore massimo `6,5·10⁻¹³` ante; bucket contro
mano entro `1e-12`); oracolo postflop 179.342 asserzioni PASS: tre sottogiochi river (57, 117, 57
nodi) con errore massimo `1,4·10⁻¹⁴` ante sui valori condizionali per combo; ASan PASS senza
diagnostiche (gioco 7,0 s, kernel 43,3 s, oracolo 6,6 s). Tempo per board CO40 a macchina libera:
contesto 0,06 ms, cache all-in 2,05 ms, traversata 89,8 ms (policy a bucket) / 70,7 ms (per
mano), best response 72,5 ms; HU10 completa 9,4 ms. Suite completa sull'integrazione (build
completa 21 min, test 1.278 s): 61/65 PASS; i 4 test legacy `gtosd_river_*_preflight/smoke`
falliscono con "frozen v1 regression manifest fingerprint mismatch" perché il worktree ha
`benchmarks/fixtures/river_bucket_qualification_corpus_v1.json` in CRLF (`core.autocrlf=true`)
mentre il fingerprint congelato è sul testo LF del checkout dell'utente; con il file in LF i 4
test passano (65/65). Nessuna relazione con il codice del programma.
Fallimenti: (1) primo run dell'oracolo respinto dal solver postflop (`invalid_configuration`):
opzioni `ProductionDcfr` costruite a mano; sostituite da `resolve_postflop_production_options`.
(2) Build ASan dell'oracolo fallita per C1128 (troppe sezioni) in `postflop_solver.cpp`, libreria
fuori perimetro: risolto aggiungendo `/bigobj` ai flag del configure ASan, senza modifiche al
repository. (3) Le prime misure di tempo (300 ms per traversata) erano contaminate dalla suite in
esecuzione; rimisurate a macchina libera.
Dubbi: (1) il costo della traversata è dominato dai 15.922 terminali di showdown; ottimizzazioni
(float, vettorizzazione) rinviate dopo P9 per §3.4. (2) Il fingerprint dei manifest legacy è
sensibile ai fine riga: fragilità del legacy da segnalare, non da correggere in questo programma.
Prossimo passo: P6 sul branch `feature/preflop-blueprint-p6-trainer`.

### 2026-09-15 — P4 — modello di gioco e albero compilato, gate PASS

Fatto: layer di regole N-player (`game_model`): stato preflop a N giocatori (ante morte, button
blind vivo del BTN, primo posto ad agire), abstraction delle azioni come funzione del livello di
aggressione della street e del "facing all-in", transizioni HU delegate al core e fold
generalizzato per N > 2, avanzo di street con il primo giocatore attivo non all-in. Albero
compilato (`compiled_game`): un solo array in preordine con sottoalberi contigui, nodi Decision /
Chance / TerminalFold / TerminalShowdown, archi nell'ordine di `legal_actions`, payoff per ogni
sottoinsieme di vincitori settled una volta con `settle_terminal`, statistiche, fingerprint,
layout dello stato `(nodo, classe o bucket, azione)`. Test (`preflop_blueprint_game_tests`) e
eseguibile di report (`preflop_blueprint_game`). Report: [P4_GAME_MODEL.md](P4_GAME_MODEL.md).
Comandi: build dei target P4; `ctest -L p4 -V`; `gtosd_hu_preflop_tree` (legacy) per il confronto;
`ctest -L preflop_blueprint`.
Risultati: 836.981 asserzioni PASS in 0,6 s; controllo di isolamento PASS su 26 sorgenti;
regressione `preflop_blueprint` P0–P4 11/11 PASS (223 s). CO40 parte preflop 58 nodi, 20
decisioni, 9 ingressi, 19 fold, 10 all-in; fingerprint
legacy `fnv1a64:a68337fa567aa2d9` riprodotto dalla parte preflop dell'albero compilato; scheletro
postflop 27.012 nodi rappresentati, 10.060 decisioni (372 flop, 2.100 turn, 7.588 river), 25.944
archi azione, 1.059 frontiere chance, 7.942 fold, 6.715 showdown, 1.236 runout all-in, massimo 4
raise per street, compilazione 0,026 s; stato R+S in double: 356.617.872 B con 200/500/1.000 e
714.889.872 B con 500/1.000/2.000 (preflop 4.617 celle, flop 216.000, turn 2.796.000, river
19.272.000 con la baseline). HU10 completa 2.059 nodi (812 decisioni), HU10 ridotta 571 nodi
(236 decisioni). Albero 3-way (UTG, CO, BTN, 40a) solo preflop: 580 nodi, 234 decisioni, 75
ingressi, 115 fold, 156 runout all-in; il fold generalizzato restituisce l'eccesso non chiamato
(UTG raise 6a, due fold: UTG +3a, CO −1a, BTN −2a).
Fallimenti: (1) primo run del test fallito sul conteggio atteso 30.324 / 11.308 della roadmap;
il benchmark legacy `gtosd_hu_preflop_tree` sul codice attuale misura 27.012 / 10.060 / 25.944,
identici all'albero compilato classe per classe: il valore della roadmap era documentazione
stale. Costanti attese corrette (decisione 20). (2) Il controllo di isolamento ha rifiutato un
commento dell'header che citava il costruttore HU legacy per nome (pattern `hu_preflop`);
commento riformulato.
Dubbi: la roadmap cita anche una profondità massima 15 dallo scheletro legacy; l'albero compilato
misura 17 dalla radice preflop (2 livelli in più per il tratto preflop fino all'ingresso). Il
postflop multiway non è compilato in P4 (P10); la "call per meno" a N > 2 è rifiutata perché con
stack uguali non si presenta e i side pot non sono modellati.
Prossimo passo: P5 sul branch `feature/preflop-blueprint-p5-kernel`.

### 2026-09-15 — P3 — clustering e tabelle bucket, gate PASS

Fatto: k-means intero con k-means++ e riavvii su campione sistematico; EMD esatta (L1 delle
cumulate, centroidi mediane pesate) per flop e turn, L2 sui vettori OCHS al river; tabelle
`uint16` per (board canonico, combo) con centroidi, parametri e fingerprint incorporati; lookup a
tempo costante da board fisico e mano; diagnostica di occupazione, inerzia e distanza media;
gruppi avversari da ranking per test e smoke; eseguibile con report JSON, salvataggio e verifica
di ricaricamento. Report: [P3_BUCKET_TABLES.md](P3_BUCKET_TABLES.md).
Comandi: build dei target P3; `ctest -L p3 -V`; costruzione completa con
`--flop 200 --turn 500 --river 1000 --restarts 10 --screening-iterations 10 --max-iterations 25
--screening-sample 500000` dalle risorse P2; `ctest -L preflop_blueprint`.
Risultati: 16.431.981 asserzioni PASS in 47,85 s (flop K=32, turn e river K=64, indipendenza dai
thread 1/3/8, invarianza ai semi, persistenza, rifiuto dei file corrotti); smoke 21,7 s PASS.
Tabelle 200/500/1.000 a 8 thread: flop 25 iterazioni, inerzia 5,68·10⁸, distanza media 150,7
(2,2 % del massimo), occupazione 449–3.035 righe, 55,5 s; turn 8 iterazioni (convergenza),
inerzia 9,49·10⁸, distanza media 8,12 (1,8 %), occupazione 2.399–102.648, 416 s; river 25
iterazioni, inerzia 1,68·10¹⁶, RMS per coordinata 0,050 di equity, occupazione 1.239–326.951,
814 s; nessun bucket vuoto; 43,3 MB in tre file; ricaricamento verificato; totale 1.294 s.
Fingerprint flop `fnv1a64:33f06cf437f8f26d`, turn `fnv1a64:51814338fcf1236c`, river
`fnv1a64:2e59aa76f59c0fcd`. Regressione `preflop_blueprint` P0–P3: 9/9 PASS (223 s con i test
P4 in corso; il controllo di isolamento è fallito una volta su un commento del codice P4, non su
P3, ed è stato ripetuto dopo la correzione).
Fallimenti: (1) accesso ai membri privati dal builder tramite classe derivata: non compila,
sostituito dal pattern attorney. (2) Lancio in background tramite il wrapper Visual Studio:
messaggio non fatale su `vswhere.exe` e log apparentemente vuoto mentre il processo girava; un
rilancio diretto ha fallito per il lock del log; un terzo lancio ha creato un processo duplicato,
terminato dopo 30 s. Il run originale è arrivato a PASS. Regola adottata: controllare i processi
con `Get-Process` prima di rilanciare.
Dubbi: flop e river si fermano al limite di 25 iterazioni (il turn converge in 8); per le tabelle
finali di P9 misurare 50 e 100 iterazioni. Il merge in `main` previsto da D21 al gate P3 non può
essere eseguito dall'agent senza toccare il working tree dell'utente: domanda Q2.
Prossimo passo: P4 sul branch `feature/preflop-blueprint-p4-game-model`.

### 2026-09-15 — P2 — risorse esatte, gate PASS

Fatto: tabella di rank ordinali a 16 bit derivata dall'oracolo esatto a 5 carte (1.404 rank
distinti); kernel di conteggio degli esiti con blocker in n log n più riferimento pairwise;
tabella all-in preflop esatta per le 176.715 coppie disgiunte; 8 gruppi avversari per equity;
istogrammi esatti flop (465 runout) e turn (30 river) e equity river per gruppi, per board
canonico e combo nel frame canonico; contenitore di risorse con checksum; eseguibile con report,
verifica oracolo, scrittura e ricaricamento. Report: [P2_EXACT_RESOURCES.md](P2_EXACT_RESOURCES.md).
Comandi: build dei target P2; `ctest -L p2 -V`; eseguibile con `--output-dir` e
`--verify-oracle 200000`; `ctest -L preflop_blueprint`.
Risultati: 9.868.560 asserzioni PASS in 84 s; 0 discrepanze con l'oracolo su 200.000 campioni;
tempi con 8 thread: rank 1,4 s, all-in 53 s, flop 2,1–2,4 s, turn 3,3–4,1 s, river 4,7–4,8 s;
7 file per 404.579.533 B scritti e ricaricati; regressione P0–P2 7/7 in 171 s; equity AA contro
mano casuale 0,7308; masse dei gruppi `78, 74, 84, 78, 76, 78, 88, 74`.
Fallimenti: (1) costante attesa delle coppie disgiunte errata (156.240 invece di 176.715: C(32,2)
al posto di C(34,2)); il test l'ha rifiutata, corretta. (2) Soglia di plausibilità dell'equity di
AA calibrata sul mazzo intero (> 0,80) mentre nello Short Deck vale 0,7308; sostituita da un
controllo strutturale più un intervallo largo. Nessun errore nel codice di calcolo.
Dubbi: il costo della tabella all-in (53 s) è il più alto delle risorse; accettabile come una
tantum, da non ricalcolare a ogni build.
Prossimo passo: P3 sul branch `feature/preflop-blueprint-p3-clustering`.

### 2026-09-15 — P1 — canonicalizzazione e cataloghi, gate PASS

Fatto: indice combinatorio colex con inversa e indice di combo compatibile con `all_combos()`;
canonicalizzazione dei semi di flop, flop+turn, board a cinque carte e board history con
permutazione e orbita esposte; cataloghi con molteplicità e riferimenti incrociati; PRNG
deterministico indipendente dalla piattaforma; sampler fisico e canonico; persistenza con
checksum; test e eseguibile di report. Report: [P1_CANONICAL_BOARDS.md](P1_CANONICAL_BOARDS.md).
Comandi: build dei target P1; `ctest -L p1 -V`; `ctest -L preflop_blueprint`.
Risultati: 4.062.607 asserzioni PASS in 3,6 s; conteggi 573 / 13.761 / 19.998 / 369.072 con somme
fisiche 7.140 / 235.620 / 376.992 / 7.539.840; costruzione dei cataloghi 2,56 s (history 2,03 s);
catalogo 13.970.940 B; fingerprint `fnv1a64:51879f40626cb7dd`; regressione P0 3/3.
Fallimenti: (1) prima build fallita per `deck_cards` non dichiarata in `canonical_boards.cpp`
(include mancante di `combinatorics.hpp`), corretta al secondo tentativo. (2) Prevenuti prima
della build: `-bound` su unsigned (C4146 con `/WX`) sostituito da `0U - bound`; scrittura del
magic con tipo a 8 bit; `<cmath>` mancante nel test.
Dubbi: il conteggio dei flop+turn canonici (13.761) era noto solo come limite inferiore
(9.818); ora è fissato come costante attesa. Il test del sampler usa una soglia a sei sigma per
classe: è un controllo di sanità della cumulata, non un test statistico formale.
Prossimo passo: P2 sul branch `feature/preflop-blueprint-p2-resources`.

### 2026-09-15 — P0 — scaffolding completato, gate PASS

Fatto: verificato il tag di sicurezza; creati il branch di integrazione e il branch di fase in un
worktree separato; aggiunti i target `gtosd_card_abstraction` e `gtosd_preflop_blueprint` con
l'opzione `GTOSD_BUILD_PREFLOP_BLUEPRINT`; scritti schema `gtosd.preflop_blueprint_game.v1`, tre
fixture (HU10 completa, HU10 ridotta, CO40), loader C++ con validazione e fingerprint, identità
dell'astrazione, test di scaffolding, controllo di dipendenza CMake, validatore Python dello
schema. Report: [P0_SCAFFOLDING.md](P0_SCAFFOLDING.md).
Comandi: configure Release con Ninja e MSVC riusando i pacchetti vcpkg di
`out/build/windows-release-current`; build dei tre target; `ctest -L preflop_blueprint`.
Risultati: configure 15,5 s; build 19 passi senza warning con `/WX`; test 3/3 PASS
(73 asserzioni, 4 sorgenti guardati, 3 fixture valide); guardia negativa su albero sintetico:
link proibito rifiutato, include legacy rifiutato, albero pulito accettato.
Fallimenti: (1) il nome di branch `feature/preflop-blueprint/p0-scaffolding` previsto da D21 è
rifiutato da git perché esiste il ref `feature/preflop-blueprint`; risolto con il trattino,
D21 e roadmap §9 allineati. (2) Il primo wrapper per l'ambiente Visual Studio lanciato da Git
Bash convertiva `/c` in un percorso; risolto disattivando la conversione dei percorsi MSYS.
(3) La rimozione dei worktree legacy con scratch e la cancellazione dei file `.bin` erano già
state bloccate dal classificatore di sicurezza prima dell'avvio di P0 (registro, A8 e D25);
nessun impatto su P0.
Dubbi: nessuno bloccante. `maximum_postflop_sizes = 3` è un limite di scaffolding da rivedere in
P4 insieme al layout delle azioni compilate.
Prossimo passo: P1 sul branch `feature/preflop-blueprint-p1-canonical` dopo il merge di P0
nell'integrazione.

### 2026-09-15 — P0 — creazione del diario

Fatto: creato il template del diario insieme alla roadmap. Nessun codice scritto.
Comandi: nessuno.
Risultati: nessuno.
Fallimenti: nessuno.
Dubbi: nessuno.
Prossimo passo: P0.

## 4. Domande per l'utente

| # | Data | Domanda | Stato | Risposta |
|---|---|---|---|---|
| Q1 | 2026-09-15 | I branch di fase vengono uniti nell'integrazione con merge locali `--no-ff`; per aprire pull request su GitHub servirebbe il push dei branch su origin. Si pubblicano i branch su origin oppure restano merge locali fino ai gate di `main`? Nel frattempo si procede con merge locali. risolta 2026-09-16 | pubblicare i branch: eseguito, 10 branch e 3 tag su origin (`main` non pushato) |
| Q2 | 2026-09-15 | D21 prevede il merge dell'integrazione in `main` al gate P3 con tag. `main` è il branch checked-out nel working tree dell'utente (`C:/Users/GoryNickel/Documents/GitHub/GTO-Solver`): git non permette di farne il checkout in un secondo worktree e spostarne il ref da fuori lascerebbe il working tree dell'utente in uno stato incoerente. Comandi proposti, da eseguire nel working tree dell'utente con `main` pulito: `git merge --no-ff feature/preflop-blueprint -m "merge(preflop-blueprint): P0-P3 card abstraction, gate P3 PASS"` poi `git tag -a preflop-blueprint-p3-abstraction -m "P3 gate PASS"`. Suite CTest completa eseguita sull'integrazione dopo il merge di P3: 65/65 PASS (4 test legacy passano solo con il manifest v1 in LF, vedi voce P5 del diario; nel checkout dell'utente il file è in LF). In alternativa l'utente può autorizzare l'agent a eseguire i due comandi nel suo working tree. Nel frattempo P4 e P5 sono proceduti sull'integrazione. risolta 2026-09-16 | applicare il merge: eseguito nel working tree dell'utente (`97d8121`), tag `preflop-blueprint-p3-abstraction` su `981361e` |
| Q3 | 2026-09-16 | D21 prevede al gate P6 il merge dell'integrazione in `main` con tag `preflop-blueprint-p6-hu10`; stesso blocco di Q2 (`main` è il working tree dell'utente). Comandi proposti nel working tree dell'utente con `main` pulito: `git merge --no-ff feature/preflop-blueprint -m "merge(preflop-blueprint): P0-P6 trainer and physical best response, gate P6 PASS"` poi `git tag -a preflop-blueprint-p6-hu10 -m "P6 gate PASS: HU10 D3 with the sampled estimator at 1000 flops"`. Da eseguire dopo (o insieme a) Q2. Nel frattempo P7 procede sull'integrazione. risolta 2026-09-16 | come Q2: tag `preflop-blueprint-p6-hu10` su `22e1015` e `preflop-blueprint-p8-export` su `9360836` |
| Q4 | 2026-09-16 | P8 prevede l'aggiornamento del viewer `tools/hu_preflop_chart_viewer` (repository separato, D18, escluso da git in questo repository) perché legga il nuovo export e mostri i badge `ESTIMATED` / `CERTIFIED_EXACT` / `EXTERNAL_REFERENCE`; il gate P8 richiede "viewer navigabile con un export HU10". L'agent lavora nel worktree e non modifica né il repository del viewer né il working tree dell'utente. Proposta: l'agent produce in questo repository l'export nel formato che il generatore del viewer già legge (`preflop_nodes` con `history`, `strategy`, `action_ev` per classe), lo schema e un validatore statico registrato in CTest; la modifica del generatore (vincoli fissi su 20 nodi, fingerprint CO40 e path Monker da rendere generici; badge di stato dal certificato P7) va fatta nel repository del viewer: la esegue l'utente, oppure l'utente autorizza l'agent a modificare `tools/hu_preflop_chart_viewer` nel suo working tree. Fino alla risposta il criterio "viewer navigabile" del gate P8 resta INCONCLUSIVE e le altre parti di P8 procedono. risolta 2026-09-16 | il viewer può essere modificato dall'agent: fatto sul branch `feature/preflop-blueprint-p8-export` del viewer (`4edbb45`), modifiche locali preesistenti conservate in `56cdf31` |

## 5. Decisioni prese dall'agent

| # | Data | Fase | Decisione | Motivazione |
|---|---|---|---|---|
| 1 | 2026-09-15 | P0 | Branch di fase con trattino: `feature/preflop-blueprint-pN-nome` | git rifiuta `feature/preflop-blueprint/pN-nome` perché il ref `feature/preflop-blueprint` esiste |
| 2 | 2026-09-15 | P0 | `maximum_postflop_sizes = 3` nel loader | fold/check/call più size più all-in restano entro le sei azioni del motore esistente; da rivedere in P4 |
| 3 | 2026-09-15 | P0 | `button_blind_units` esplicito e strettamente positivo | D9; evita l'identità implicita con l'ante del formato legacy |
| 4 | 2026-09-15 | P0 | Riuso dei pacchetti vcpkg installati nella build principale (`VCPKG_MANIFEST_INSTALL=OFF`) | evita una nuova installazione delle dipendenze nel worktree; riproducibile |
| 5 | 2026-09-15 | P0 | Test dello schema con `SKIP_RETURN_CODE 77` se `jsonschema` manca | non fallire su macchine senza il pacchetto; il loader C++ applica comunque le regole |
| 6 | 2026-09-15 | P0 | Merge locali `--no-ff` nell'integrazione, nessun push su origin | il push pubblica contenuti; in attesa della risposta a Q1 il lavoro non si ferma |
| 7 | 2026-09-15 | P1 | PRNG proprio (xoshiro256** seminato da splitmix64, draw limitati con il metodo di Lemire) al posto di `std::mt19937_64` e `std::uniform_int_distribution` | le distribuzioni standard sono implementation-defined; un seed deve identificare gli stessi board su ogni piattaforma |
| 8 | 2026-09-15 | P1 | Canonicalizzazione per minimo su 24 permutazioni di un codice a 6 bit per carta, cataloghi per enumerazione esaustiva | verificabile con la dimensione dell'orbita; 2,6 s di costruzione |
| 9 | 2026-09-15 | P1 | Conteggio dei flop+turn canonici fissato a 13.761; il caricamento del catalogo è fail-closed sui quattro conteggi | misura ottenuta dall'enumerazione; sostituisce il limite inferiore della roadmap |
| 10 | 2026-09-15 | P1 | Il file del catalogo non viene distribuito | ricostruzione in 2,6 s; il file salvato serve come identità verificabile con checksum |
| 11 | 2026-09-15 | P2 | Rank ordinali a 16 bit derivati dall'oracolo a 5 carte invece di caricare la tabella R3 a 32 bit | stesso ordine dei `HandValue`, metà memoria, 1,4 s di costruzione, nessun file esterno; l'oracolo resta l'unico evaluator |
| 12 | 2026-09-15 | P2 | Kernel sweep per flop e turn, pairwise per il river | il river richiede conteggi per gruppo avversario; il pairwise è un controllo indipendente del kernel |
| 13 | 2026-09-15 | P2 | File delle feature (365 MB) come artefatti offline non distribuiti; equity river in virgola fissa a 16 bit | servono solo al clustering P3; dimensione dimezzata rispetto a float32 con errore 1/131070 |
| 14 | 2026-09-15 | P2 | Tabella all-in triangolare con voci vuote per le coppie sovrapposte | indirizzamento O(1) senza mappa |
| 15 | 2026-09-15 | P3 | k-means intero: istogrammi e cumulate come conteggi, centroidi come mediane pesate (EMD) o medie arrotondate (L2), partizione statica del lavoro | nessuna dipendenza dall'ordine di riduzione in virgola mobile: risultato identico a 1, 3 e 8 thread (D14) |
| 16 | 2026-09-15 | P3 | Riavvii valutati su un campione sistematico di 500.000 osservazioni per 10 iterazioni, solo il migliore rifinito sull'intero insieme | 10 riavvii completi costerebbero dieci volte il river (814 s); il campione sistematico è deterministico e copre tutte le righe |
| 17 | 2026-09-15 | P3 | Bucket rietichettati per forza crescente del centroide dopo la convergenza | id confrontabili fra costruzioni e leggibili nei report; l'assegnazione non cambia |
| 18 | 2026-09-15 | P4 | Regole di fase come funzione del livello di aggressione della street (0 apertura, 1 risposta, 2+ solo fold/call/all-in) e del "facing all-in" letto dallo stato, non della macchina a stadi HU | stesso albero HU (fingerprint legacy riprodotto) e regole valide per N giocatori |
| 19 | 2026-09-15 | P4 | Transizioni HU delegate a `gtosd::apply_action`/`advance_street`; per N > 2 fold generalizzato e avanzo di street nella libreria blueprint, nessuna modifica al core in P4 | il core gestisce il fold solo a due giocatori; il costruttore N-player nel core è previsto da P10 (§2.2) |
| 20 | 2026-09-15 | P4 | Conteggi attesi dello scheletro postflop CO40 corretti a 27.012 nodi / 10.060 decisioni / 25.944 archi (misurati con `gtosd_hu_preflop_tree` sul codice legacy attuale) | i 30.324 / 11.308 / 29.112 della roadmap provengono da documenti anteriori alle regole di puntata correnti e non sono riprodotti nemmeno dal codice legacy |
| 21 | 2026-09-15 | P5 | Cache per board delle probabilità esatte di vittoria e pareggio di ogni coppia viva (2 matrici 465×465 in double, 3,5 MB) invece di leggere la tabella P2 a ogni terminale | 10 terminali all-in preflop per traversata: costruzione una volta per board, poi prodotti matrice-vettore; la massa di sconfitta deriva da `D − W − T` con `D` esatto dal kernel fold |
| 22 | 2026-09-15 | P5 | Interfaccia `Policy` per (nodo, mano) con due implementazioni: `BucketPolicy` sul layout P4 e `HandPolicy` per mano | la traversata non conosce l'astrazione; gli oracoli e i test prescrivono strategie per combo, il trainer userà i bucket |
| 23 | 2026-09-15 | P5 | Oracolo postflop confrontato sul valore condizionale `v[h] / D[h]` invece che sul valore controfattuale grezzo | il solver postflop normalizza la reach avversaria con una costante interna; il rapporto elimina la costante e resta un confronto esatto per combo |
| 24 | 2026-09-15 | P5 | Tabella all-in e rank caricate dalla directory `out/preflop_blueprint_resources` quando presente, altrimenti ricostruite nel test | i test restano autosufficienti su altre macchine (circa un minuto di costruzione) e rapidi su quella di riferimento |
| 25 | 2026-09-15 | P6 | Nel regret `R += w_B · P(h) · P(o\|h) · (v_a − v)` il fattore `cf_reach` di §5 è già dentro `v` (valori dei kernel con la reach avversaria); non viene moltiplicato di nuovo | formula del CFR vettoriale standard; uguaglianza entro `1e-13` con `solve_finite_game` sul gioco ridotto |
| 26 | 2026-09-15 | P6 | Modalità di aggiornamento `Simultaneous` (uno snapshot per iterazione, entrambi i giocatori) come default e `Alternating` come opzione; Linear default, DCFR 1,5/0/2 opzione | la modalità simultanea riproduce esattamente `solve_finite_game` Linear (oracolo P6); DCFR alternato converge più in fretta nelle prove e resta lo sfidante di §3.4 |
| 27 | 2026-09-15 | P6 | Partizione dell'albero in unità di `max(256, nodi/128)` nodi, indipendente dal numero di thread; parte alta seriale; incrementi scritti direttamente nelle celle (un solo scrittore) senza buffer di delta | bit-identità per qualsiasi numero di thread e di unità verificata (0 celle diverse fra 1, 8 e 58 unità); nessuna copia dello stato (D14, roadmap P6.2) |
| 28 | 2026-09-15 | P6 | Lo stimatore D3 misura la best response fisica contro la strategia media a bucket, come prescrive P6.3. **Corretta dalla decisione 30**: il massimo per mano *su ogni board* è chiaroveggente sopra il river | la best response del gioco astratto con recall imperfetto non è calcolabile per board; quella fisica la domina ed è la misura che P7 certificherà |
| 29 | 2026-09-15 | P6 | Boards del batch pesati `1/B`; RNG di training e di valutazione separati e salvati nel checkpoint | costanti comuni alle iterazioni non cambiano il regret matching; la valutazione non perturba il training e la ripresa è bit-identica |
| 30 | 2026-09-15 | P6 | Best response fisica **non chiaroveggente**: ai nodi dell'eroe i valori delle azioni sono aggregati sulle carte future prima del massimo (turn: somma sui river; flop: somma sui turn; preflop: somma sui flop); il massimo per board resta solo al river. Erratum aggiunto alla roadmap §5 | la best response per board di P6.3/§5 dava un responder che conosce turn e river: plateau di circa 1 a su HU10 e pavimenti crescenti con il numero di board fissi (0,105/0,163/0,473 a per 4/8/64 board) non dovuti all'astrazione; l'aggregazione corretta coincide con `calculate_nash_conv` del `FiniteGame` lossless entro `1e-9` |
| 31 | 2026-09-15 | P6 | Valutazione campionata per flop: `M` flop campionati dal catalogo con tutti i 33 × 32 runout enumerati, errore standard sui gruppi di flop; le liste esplicite sono raggruppate per flop | l'aggregazione non chiaroveggente al flop e al turn richiede tutti i runout del prefisso; board completi indipendenti non bastano |
| 32 | 2026-09-16 | P6 | Lo stimatore campionato riporta la stima naive (scelta e valore preflop sugli stessi `M` flop, distorta verso l'alto come `1/√M`) e un limite inferiore senza selezione (strategia media al preflop, best response esatta dal flop in poi); il gate D3 usa la naive più semiampiezza con `M = 1.000` flop; la cross-fit provata è stata scartata | sul checkpoint HU10 ridotto `naive · √M` è costante (≈ 0,4–0,5 a) per `M = 5…160` mentre il limite inferiore è ≈ 0,001 a: la stima P6.3 a `M` piccolo misura solo rumore di selezione; la cross-fit era negativa a ogni `M` |
| 33 | 2026-09-16 | P6 | Default dell'eseguibile di training: DCFR 1,5/0/2 con update alternato; la libreria mantiene Linear simultaneo come default (l'oracolo `FiniteGame` lo richiede). `--eval-seed` riavvia l'RNG di valutazione dopo il caricamento del checkpoint invece di entrare nell'identità | su HU10 completo il Linear simultaneo resta 2–3 volte sopra il DCFR alternato a parità di flop di valutazione (0,143 contro 0,045 a a `M = 60`); una rivalutazione su flop freschi non deve cambiare l'identità del run ripreso |
| 34 | 2026-09-16 | P7 | La passata esatta valuta ogni flop canonico una volta con tutti i runout fisici e somma sulle immagini della sua orbita nei semi (`FlopValues.images`); `aggregate(exact)` verifica che ogni combo sia compatibile con 5.984 flop fisici | la strategia media è simmetrica nei semi per costruzione; verificato contro l'enumerazione fisica entro `1e-12` per combo; costo 573 flop invece di 7.140 |
| 35 | 2026-09-16 | P7 | Formato di policy `GTOSDPOL` (fingerprint dell'albero, capacità, sorgente, tabella densa, checksum) scritto dal trainer e letto dal certificatore; il certificato porta i fingerprint di regole, albero, catalogo, tabelle bucket e policy | il certificatore non deve ricostruire il trainer (identità, semi) per leggere una strategia; P8 esporta dallo stesso file |
| 36 | 2026-09-16 | P7 | Stato del certificatore accodato per chunk con checksum per record; ripresa dall'header (albero, policy, catalogo); il numero dichiarato nei certificati è quello esatto, la regola D3 campionata resta la regola di arresto del training | passata ripresa bit-identica; su CO40 la passata esatta costa 22 h e va spezzata; la stima campionata sovrastima di `≈ 0,46/√M` |
| 37 | 2026-09-16 | P8 | Export `gtosd.preflop_blueprint_chart.v1` nel layout `preflop_nodes` → `{history, strategy, action_ev}` già letto dal generatore del viewer, con id di azione compatibili con le chart legacy (`raise_6`, `call`, `fold`, `all_in`) e id di nodo `CO_raise_3_BTN` | il viewer richiede solo la rimozione dei vincoli fissi CO40 e i badge (Q4); nessun secondo formato da mantenere |
| 38 | 2026-09-16 | P8 | EV per azione condizionato al nodo: valore controfattuale diviso per la reach avversaria data la combo, media di classe pesata con la reach, errore standard sui flop campionati; alla radice coincide con l'EV del gioco. **Implementazione corretta dalla decisione 43**: fino al commit `46084f0` la divisione per `D[h]` mancava e i valori fuori dalla radice erano scalati per `D[h]` | è la semantica delle chart (EV dell'azione nello spot); verificata entro `1e-9` alla radice |
| 39 | 2026-09-16 | P8 | Verdetto del comparatore sulla sola exploitability fisica dichiarata (D2, soglia 0,1 a): `QUALIFIED` / `REJECTED` con certificato esatto, `PROMISING` / `INCONCLUSIVE_ESTIMATE` / `REJECTED` (limite inferiore sopra soglia) con stima campionata; distanze Monker descrittive con `EXTERNAL_CONTRACT_INCOMPLETE` | D1/D2/D4 della roadmap; la stima campionata ha bias di selezione e non può qualificare da sola |
| 40 | 2026-09-16 | P8 | Albero pubblico postflop esportato nello schema del viewer (`gtosd.hu_postflop_public_tree.v1`) leggendo lo stato pubblico conservato per nodo dal compilato; id compilati nel file per indirizzare il worker | il viewer già navigava quello schema; nessun secondo formato |
| 41 | 2026-09-16 | P8 | EV postflop del worker: valore controfattuale dell'azione con la strategia media diviso per la reach avversaria al nodo; esatto a turn (32 river) e river, medio su `samplesPerAction` runout campionati al flop (seme fisso), classi pesate con la reach avversaria, frequenze di range pesate anche con la reach dell'eroe | stessa semantica dell'export preflop; il flop completo (1.056 runout) costa 17,6 s e non è interattivo |
| 42 | 2026-09-16 | P8 | Viewer: generatore con sorgenti blueprint e badge, azioni e gioco dinamici, Monker solo a parità di albero, backend `--serve`; le modifiche locali preesistenti dell'utente sono conservate in un commit separato prima dell'aggiornamento | autorizzazione Q4; il repository del viewer non ha remote: i commit restano locali |
| 43 | 2026-09-16 | P8 | EV di classe fuori dalla radice: media di `v_a[h] / D[h]` pesata con `D[h]` (valore e serie per flop dell'errore standard); test del payoff di fold a ogni nodo interno | fino a `46084f0` la divisione per `D[h]` mancava e alla radice non era rilevabile (`D[h] = 1`); il payoff di fold è una costante nota a ogni nodo interno e verifica la semantica condizionata |
| 44 | 2026-09-16 | P8 | Variante CO40 di test come fixture separata con id `-TEST` (una size postflop 100 % più all-in), usata solo per misure di tempo e prove; la fixture CO40 completa resta il riferimento del gate P9 | richiesta dell'utente; il "da non fare" di P9 vieta di cambiare size per migliorare il risultato, non di misurare su un albero ridotto |
| 45 | 2026-09-16 | P8 | Nel viewer le classi con reach proprio lungo la history sotto `1e-6` restano visibili ma desaturate, con il reach nel tooltip | l'export riporta la strategia media di tutte le classi (residui ≈ 1e-9); togliere le righe cambierebbe la griglia 9×9; la soglia è sotto ogni frequenza di gioco significativa |
| 46 | 2026-09-16 | P8 | Lista di risposte vuota nella configurazione = nessuna size di rilancio sopra un open (livello 1: fold, call, all-in); con lista non vuota resta una risposta per open | regola dell'utente per HU10 (contro l'open 5 a BTN ha solo l'all-in); nessun secondo formato, la fixture CO40 non cambia |
| 47 | 2026-09-16 | P8 | Le misure di tempo del certificatore si fanno con `--chunk` ≥ numero di thread (il parallelismo è sui flop di uno stesso chunk); i tempi per flop nei report sono a 8 thread con chunk 16 salvo indicazione | le proiezioni di P7 §5 (chunk 2) e della voce CO40-TEST (chunk 1) avevano 2 e 1 thread attivi e sovrastimavano di 2,3 e 4,2 volte |
