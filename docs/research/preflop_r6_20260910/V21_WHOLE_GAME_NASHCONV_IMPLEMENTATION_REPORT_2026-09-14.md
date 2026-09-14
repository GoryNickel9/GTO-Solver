# V21 — Report di implementazione NashConv whole-game

Data: 2026-09-14  
Stato: `CALIBRATION PASS / CO40 NOT CERTIFIED`

## Risultato

Il percorso di validazione `MCCFR -> strategia media -> best response esatta -> NashConv` passa
su una fixture Short Deck enumerabile che include preflop, flop, turn e river. La fixture usa
carte fisiche, card removal, showdown Short Deck, fold, call, raise, all-in e rake configurabile.

Questo risultato valida le formule e il flusso di certificazione sul gioco ridotto. Non certifica
la NashConv del checkpoint CO40 V17, V18 o V19. Sul checkpoint reale V17 è stato completato un
probe esatto di un singolo sottogioco River; manca ancora la riduzione di tutti i contributi
preflop e postflop del gioco.

## Calibrazione ridotta

Il gioco contiene 789 nodi, 149 chance node, 300 nodi decisionali e 150 information set. Il
training usa Linear MCCFR con seed fisso e strategia media. Ogni checkpoint è valutato con best
response enumerata.

| Iterazioni | NashConv | NashConv / piatto iniziale |
| ---: | ---: | ---: |
| 20.000 | 0,0014765486497216873 | 0,0004921828832405625 |
| 40.000 | 0,0003716098854660288 | 0,0001238699618220096 |
| 60.000 | 0,0001654188395427969 | 0,0000551396131809323 |

L'errore massimo di normalizzazione è `1,1102230246251565e-16`. Il resume da 5.000 a 10.000
iterazioni produce lo stesso checkpoint byte per byte del run continuo. Il fingerprint del gioco
è `fnv1a64:c5550b399fed070a`; i fingerprint dei tre checkpoint sono rispettivamente
`fnv1a64:d76a3778ed0b51b6`, `fnv1a64:ff663972ec5c4606` e
`fnv1a64:943f7c83d5a515a9`.

## Snapshot della policy reale

Il valutatore accetta ora uno snapshot immutabile che lega policy, albero, iterazioni e modalità
di lookup. La validazione avviene una volta alla creazione dello snapshot; le query successive non
possono sostituire silenziosamente uno degli input.

Il formato della policy V17 dichiara una strategia uniforme quando un information set non è stato
visitato durante il training. Questa fallback completa la strategia e può essere inclusa nella
valutazione matematica. La modalità strict resta un controllo di copertura: segnala gli information
set mancanti, ma la loro presenza non rende da sola indefinita la NashConv.

Input del probe V17 seed 1:

- tree fingerprint: `fnv1a64:a68337fa567aa2d9`;
- policy fingerprint: `fnv1a64:3821fd86bf83ad5f`;
- iterazioni: 2.000.000;
- abstraction: V8;
- policy entries: 1.566.290.

## Probe esatto su un sottogioco River V17

Il probe seleziona il primo task e la root River con stack rimanente minimo, ordinal 16.800 ed
entry 3. Le quantità seguenti sono esatte per quel sottogioco pubblico e per la policy completa
con fallback uniforme dichiarata. Non sono la NashConv globale CO40.

| Giocatore | EV del profilo | EV best response | Deviation gain |
| --- | ---: | ---: | ---: |
| CO | 9,713873536126114a | 15,735573892678937a | 6,021700356552824a |
| BTN | -9,713873536126115a | 2,6718732516741555a | 12,38574678780027a |

La somma locale dei deviation gain è `18,407447144353092a`. È un segnale diagnostico forte su
questa root River, non un certificato whole-game: la scelta best response deve restare coerente
fra tutte le history dello stesso information set e i risultati devono essere pesati dalla reach
prima dell'osservazione pubblica.

## Ottimizzazione del valutatore River

Il primo evaluator scalare interrogava la policy per ogni coppia di combo. Il nuovo evaluator
propaga una matrice di reach `465 x 465`, interroga la strategia avversaria una volta per combo e
stato, e mantiene il massimo best-response separato per mano privata del responder.

| Implementazione | Profilo + BR per root | Rapporto |
| --- | ---: | ---: |
| Scalare | 89,8287418 s | 1,00x |
| Vettoriale | 0,8999151 s | 99,82x |

Il test differenziale richiede uguaglianza fra evaluator scalare e vettoriale entro `1e-10`.

## Limite misurato sul gioco completo

La decomposizione corrente contiene:

- 5.157 task entry/flop;
- 369.072 history pubbliche canoniche;
- 873 forme River;
- 322.199.856 sottogiochi pubblici River canonici;
- 644.399.712 lati resolver.

Applicare il probe vettoriale separatamente a ogni root richiederebbe, per proiezione lineare,
circa 9,19 anni seriali o 1,15 anni con otto worker ideali. È una stima di throughput, non un
benchmark whole-game. Il prossimo executor deve riusare il lavoro fra board, shape e boundary;
non deve lanciare centinaia di milioni di valutazioni indipendenti.

## Gate

| Controllo | Esito |
| --- | --- |
| Fixture Short Deck quattro street valida | PASS |
| Best response almeno pari al profilo | PASS |
| NashConv finita, non negativa e decrescente | PASS |
| Resume MCCFR byte-coerente | PASS |
| Evaluator River scalare e vettoriale entro `1e-10` | PASS |
| Oracolo monolitico e decomposizione sullo stesso whole game | PENDING |
| NashConv globale CO40 V17/V18/V19 | NOT CERTIFIED |

## Artefatti e validazione

- [Calibrazione MCCFR/NashConv](v21_mccfr_nashconv_calibration_v1.json): schema, seed,
  checkpoint, EV, best response, NashConv e fingerprint del gioco ridotto.
- [Probe V17 seed 1](v21_v17_seed1_nashconv_probe_v1.json): input reali, root selezionata,
  copertura strict, fallback dichiarata, valori locali e proiezione di throughput.
- `gtosd_external_sampling_tests`: PASS, 116 asserzioni.
- `gtosd_hu_preflop_tests`: PASS, 15.954 asserzioni.
- build Release dei tre target V21 e `git diff --check`: PASS.

`clang-format` non è installato nell'ambiente Visual Studio corrente; la build usa comunque i
warning come errori. Il controllo di formattazione resta l'unico gate meccanico non eseguito.

## Decisione

Non si avvia un'enumerazione root-by-root. La priorità è un reducer batch che condivida le query
della policy e le transizioni fra sottogiochi equivalenti, conservi i fingerprint e aggreghi la
best response prima di massimizzare per information set. Soltanto l'output completo di quel
reducer potrà riportare `nashconv_certified=true` per una strategia CO40.
