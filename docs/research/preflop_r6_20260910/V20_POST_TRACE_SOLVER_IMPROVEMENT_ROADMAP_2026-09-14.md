# V20 — Roadmap dopo la root decision trace

Data: 2026-09-14  
Stato: `ROOT TRACE COMPLETE / NEXT EXTERNAL CONTRACT GUARD`  
Baseline congelata: `V17`  
Prerequisito: root decision trace per `JTo`, `QJo` e `J9s`

La roadmap è stata preregistrata prima dell'implementazione della trace. La trace è ora completa;
le modifiche elencate sotto restano da implementare nell'ordine indicato.

## Obiettivo

Questa roadmap ordina le modifiche da valutare dopo la root decision trace. Il contratto completo
del solve Monker non è recuperabile. Il suo export resta un confronto descrittivo e non può
certificare equivalenza del gioco. Il lavoro non punta a portare artificialmente la frequenza di
all-in CO dal 20–25% al 41%. Deve stabilire se le scelte del nostro solver derivano da continuation
value, risposta BTN, convergenza, astrazione o selezione fra azioni quasi indifferenti.

V17 resta la baseline finché un candidato supera i gate di EV, stabilità fra seed, errori
Call/Fold, tempo e memoria. Il CRN globale V19 resta research-only e disattivato.

## Evidenza che determina l'ordine

| Evidenza | Conseguenza |
| --- | --- |
| Equity all-in verificata contro PokerQuant con errore massimo `1,22124532708767e-15` | Non si riapre l'evaluator all-in senza nuova evidenza |
| Deficit all-in V17 quasi identico fra seed 1 e 2 | Il rumore del solo seed non spiega il divario |
| Circa `12,14 pp` del deficit positivo, su `18,36 pp`, ricadono entro `0,1a` dal miglior EV Monker arrotondato | WMAE e TV vanno affiancate da una metrica ponderata per perdita EV |
| V19 riduce WMAE media di `0,3124 pp`, ma peggiora TV media di `1,1687 pp` | Il CRN globale non viene promosso |
| Il contratto completo Monker non è recuperabile | La reference resta permanentemente descrittiva; nessuna differenza verso Monker certifica un errore del solver |
| I solve da 2M usano una best response campionata | `normalized_nashconv=0` non certifica convergenza |

## Ordine delle modifiche

| Priorità | Modifica | Condizione di ingresso | Gate di uscita | Stima |
| ---: | --- | --- | --- | ---: |
| 1 | Congelare il perimetro noto della reference Monker e marcarla permanentemente `EXTERNAL_CONTRACT_INCOMPLETE` | Export e dati già disponibili | Il comparatore continua a produrre WMAE/TV e segnala la confidenza; Monker non può essere l'unico gate di correttezza | 1–2 h |
| 2 | Costruire un holdout fisico paired per `all_in`, `raise_6`, `raise_10` e `call` sulle classi ad alto impatto | Root trace valida e policy V17 congelata | Intervallo al 95% abbastanza stretto da distinguere gap di `0,1a`; stessa coppia di deal per ogni azione | 1–2 giorni + 2–6 h di run |
| 3 | Calibrare la best response campionata contro una best response esatta su un gioco ridotto | Harness reduced-game enumerabile | Bias, varianza e copertura dell'intervallo documentati su seed congelati | 1–2 giorni |
| 4 | Collegare la best response streaming già presente a una certificazione esatta del gioco astratto V8 | Gate della priorità 3 superato | NashConv esatta V8, checksum degli input e ripresa da task intermedi | 2–5 giorni |
| 5 | Raffinare selettivamente le continuation che il holdout identifica come distorte | Bias fisico localizzato per street, history o bucket | Riduzione del bias fuori campione senza regressione di memoria, TV o Call/Fold | 2–5 giorni per candidato |
| 6 | Migliorare MCCFR soltanto se la trace mostra mancata convergenza: sampling stratificato mirato, allocazione adattiva dei rollout e averaging controllato | EV fisici coerenti, ma current/average strategy o regret restano incoerenti | Migliore NashConv o lower bound calibrato e TV inferiore a parità di budget | 2–4 giorni per candidato |
| 7 | Aggiungere loss ponderata per EV, regret evitabile e intervalli di confidenza ai comparatori | Holdout fisico disponibile | Report WMAE/TV ed EV-loss prodotti dallo stesso comando; WMAE/TV Monker restano diagnostiche | 1 giorno |
| 8 | Riallineare il viewer: V17 default, badge `estimated/unverified-external`, current/average e pannello causale | Schema della trace stabile | Validatore statico PASS e nessuna policy esterna o research presentata come baseline certificata | 2–4 h |

## Modifica condizionata dalla diagnosi

La root trace instrada il lavoro senza cambiare automaticamente il solver:

| Risultato osservato | Modifica autorizzata | Modifica da evitare |
| --- | --- | --- |
| Call perde EV nella rivalutazione fisica rispetto alla continuation V8 | Split selettivo di bucket/history lungo il ramo Call e retraining matched | Aumento globale dei bucket |
| BTN concede troppo dopo limp o risponde male agli all-in | Audit e correzione della policy BTN o del suo information state | Correzione manuale delle frequenze CO |
| Current EV preferisce All-in, average strategy preferisce Call | Audit dell'averaging, checkpoint temporali e budget aggiuntivo controllato | Tuning verso la frequenza Monker |
| Call e All-in restano entro l'intervallo di indifferenza con NashConv bassa | Conservare la strategia; il gap Monker resta descrittivo | Usare WMAE come unica funzione obiettivo |
| NashConv o best-response gain resta alto | Intervenire su sampling, regret update o precisione della policy | Raffinare l'astrazione prima di isolare la causa |

La trace V18 su `AA` con 10.000 deal per azione non replica il precedente vantaggio puntuale di
Raise 6. Call precede Raise 6 di `0,2267a` sulla continuation media; l'intervallo simultaneo
`[-0,1426a; +0,5960a]` resta inconclusivo. Questo caso non autorizza una modifica al trainer.

## Gate comuni per ogni candidato

Un candidato può sostituire V17 soltanto se soddisfa tutti questi controlli:

| Controllo | Soglia |
| --- | --- |
| WMAE media verso reference | Diagnostica secondaria; non blocca né autorizza la promozione |
| TV media fra seed | Riduzione di almeno il `10%` rispetto a V17 |
| Call/Fold con reach pubblica `>=1%` | Nessuna regressione rispetto ai 7 casi V17 |
| EV-loss fisica sul corpus holdout | Miglioramento con intervallo al 95% che non attraversa zero |
| Riproducibilità | Due seed matched, poi due seed indipendenti dopo il gate |
| Risorse | Meno di 60 minuti per seed e meno di 8 GiB sul profilo desktop congelato |

## Componenti previsti

| Area | File o modulo |
| --- | --- |
| Contratto esterno | `benchmarks/fixtures/hu_preflop_co40_reference_v1.json` |
| Holdout e query fisiche | `benchmarks/hu_preflop_postflop_query.cpp`, nuovo runner Phase F |
| Training e diagnostica | `libs/preflop/src/hu_preflop_solver.cpp`, `include/gtosd/preflop/hu_preflop.hpp` |
| Best response | infrastruttura HU preflop di decomposizione e task streaming già presente |
| Comparatori | `tools/analyze_hu_preflop_pair.py`, `tools/analyze_hu_preflop_seed_tv.py` |
| Viewer | `tools/hu_preflop_chart_viewer/` |
| Test | `tests/hu_preflop_telemetry_tests.cpp`, `tests/hu_preflop_tests.cpp` |

## Decisioni congelate

- V17 resta baseline fino al superamento dei gate comuni.
- V19 global CRN non riceve altri seed e non entra nel viewer.
- Il 41% di all-in Monker resta permanentemente un dato diagnostico.
- Le metriche di frequenza non sostituiscono EV-loss o NashConv.
- Ogni incremento di bucket o iterazioni richiede un confronto matched; non si eseguono sweep
  globali senza una causa localizzata.

## Primo lavoro dopo la trace

Marcare la reference esterna come `EXTERNAL_CONTRACT_INCOMPLETE` nella fixture, nello schema e
nel comparatore. Subito dopo, chiudere il contratto del corpus fisico paired e implementare il
runner Phase F sulle classi `JTo`, `QJo`, `A6o`, `T9o`, `AQo`, `AKo`, `J9s` e `KK`.

## Aggiornamento V21

La priorità 3 dispone ora di una calibrazione deterministica sul gioco Short Deck ridotto: Linear
MCCFR porta la NashConv esatta da `0,0014765486` a `0,0001654188` fra 20.000 e 60.000 iterazioni,
e il resume è byte-coerente. Restano da misurare bias, varianza e copertura della best response
campionata prima di chiudere l'intera priorità.

Per la priorità 4 è stato eseguito il primo probe sulla policy V17 reale. Lo snapshot immutabile
lega tree e policy; l'evaluator River vettoriale coincide con l'oracolo scalare entro `1e-10` e
riduce il tempo per root da `89,8287 s` a `0,8999 s`, pari a `99,82x`. La decomposizione espone
però 322.199.856 sottogiochi River canonici: la proiezione root-by-root resta circa 9,19 anni
seriali. Serve quindi riuso batch fra board e shape prima della certificazione CO40.

Protocollo ed esito sono in
[V21_WHOLE_GAME_NASHCONV_CALIBRATION_PROTOCOL_2026-09-14.md](V21_WHOLE_GAME_NASHCONV_CALIBRATION_PROTOCOL_2026-09-14.md)
e
[V21_WHOLE_GAME_NASHCONV_IMPLEMENTATION_REPORT_2026-09-14.md](V21_WHOLE_GAME_NASHCONV_IMPLEMENTATION_REPORT_2026-09-14.md).
