# V22 — Report fase 1 del certificatore NashConv generico

Data: 2026-09-14  
Stato: `PLANNING GATE PASS / EXACT PHYSICAL ROOT-BY-ROOT INFEASIBLE`

## Risultato

Il probe NashConv è ora indipendente dallo stack hardcoded: riceve configurazione, candidato e
policy, misura il lavoro e applica una stop rule prima del run exact. Lo stesso comando può quindi
analizzare un futuro solve 50a senza modifiche al codice.

Il checkpoint V17 non supera il gate di esecuzione exact con il percorso root-by-root. Dopo il
batching delle query, la cache delle chiavi private e il riuso dell'inizializzazione fra profilo e
BR, il costo proiettato resta `2,46 anni` seriali, contro un limite operativo di sette giorni.
An'idealizzazione lineare su otto worker richiederebbe comunque `112,36 giorni`. Il runner emette
`INFEASIBLE_EXACT_ROOT_BY_ROOT` e non presenta il probe locale come NashConv globale.

## Contratto della best response

La metrica primaria resta la best response fisica lifted. La policy media V8 è congelata, ma il
responder conserva le proprie combo e le osservazioni pubbliche fisiche. Una best response
vincolata ai bucket V8 sarà eventualmente una seconda metrica, con nome e certificato separati.

La fallback uniforme dichiarata dalla policy completa la strategia. La modalità strict misura la
copertura delle sole righe addestrate e continua a segnalare `STRICT_COVERAGE_INCOMPLETE`; questo
non autorizza a eliminare le history raggiungibili dal calcolo.

## Census della policy V17

| Street | Information set addestrati | Contesti decisionali | Righe medie per contesto |
| --- | ---: | ---: | ---: |
| Flop | 10.353 | 372 | 27,83 |
| Turn | 230.012 | 2.100 | 109,53 |
| River | 1.325.925 | 7.588 | 174,74 |
| Totale | 1.566.290 | 10.060 | 155,69 |

Ogni contesto contiene da 23 a 206 righe addestrate. Queste cardinalità dimostrano che le query
della policy possono essere riusate. Non misurano una riduzione equivalente di card removal,
reach o showdown, che dipendono ancora dalle carte fisiche.

## Batching e riuso locale

La vecchia API ricostruiva history, bucket e lookup una volta per ogni azione dello stesso nodo.
La nuova API restituisce l'intera strategia in ordine di azione legale; l'evaluator vettoriale la
calcola una volta per combo e decisione.

| Versione | Profilo + BR per root | Proiezione seriale | EV locali |
| --- | ---: | ---: | --- |
| Prima del batching | 0,8421401 s | 8,60 anni | riferimento |
| Strategia completa per lookup | 0,6323322 s | 6,46 anni | identici |
| Chiavi private River preparate | 0,3919397 s | 4,00 anni | identici |
| Profilo e BR con inizializzazione condivisa | 0,2410329 s | 2,46 anni | identici |

Il guadagno complessivo misurato è `3,49x`. La NashConv locale del probe resta
`18,407447144353092a`; profile EV, BR EV e deviation gain non cambiano. La modalità strict
continua a fallire sulla prima riga assente, come previsto per una policy sparsa.

## Integrazione nei solve futuri

Il runner di solve serializza ora `nashconv_validation`. Finché manca l'executor exact, il blocco
riporta:

- stato `ESTIMATED_LOWER_BOUND_ONLY`;
- policy target `average`;
- somma dei valori delle due risposte MCCFR;
- errore standard combinato e intervallo normale al 95%;
- scope dell'intervallo, condizionato alle risposte campionate congelate;
- limite esplicito: il campionamento non fornisce un upper bound sull'errore di ottimizzazione.

Questo dato può dimostrare che una strategia è sfruttabile quando il lower bound è materialmente
positivo. Non può certificare che la strategia sia vicina a Nash quando il valore è basso.

Uno smoke solve Linear MCCFR a una iterazione ha verificato la serializzazione del contratto. Il
payload contiene `certified: false`, stato `ESTIMATED_LOWER_BOUND_ONLY`, somma delle due risposte,
errore standard, intervallo normale al 95% e limite del metodo. I numeri dello smoke non misurano
la qualità di una strategia addestrata.

## Validazione

- build Release warning-clean dei target probe, solve e test HU;
- suite HU Release: `PASS`, `15.956` asserzioni;
- confronto scalare, vettoriale e paired per profilo e BR entro `1e-10`;
- probe V17: EV locali invariati e gate exact `INFEASIBLE_EXACT_ROOT_BY_ROOT`;
- parsing JSON riuscito per manifest V22 e smoke solve.

## Artefatto

Il manifest [v22_v17_seed1_nashconv_execution_plan_v1.json](v22_v17_seed1_nashconv_execution_plan_v1.json)
contiene il census, i fingerprint, il probe invariato, la proiezione e la decisione automatica.

## Decisione

Non si avvia l'executor root-by-root. La fase successiva deve preparare una sola volta bucket e
showdown per board fisico e consumare insieme tutte le River shape compatibili. Il gate resta lo
stesso: un risultato campionato non può impostare `certified=true`.

Questa fase è stata eseguita. Il [report board-batched](V22_CROSS_ROOT_BOARD_BATCHED_REDUCER_REPORT_2026-09-14.md)
misura `3,2053 anni` seriali per il solo River e chiude il gate con
`INFEASIBLE_EXACT_BOARD_BATCHED`.
