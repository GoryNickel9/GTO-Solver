# V23 — Protocollo NashConv astratta con perfect recall

Data: 2026-09-14
Stato: `PREREGISTERED_BEFORE_IMPLEMENTATION / PHASE_2 HU10 TARGET MET / K SENSITIVITY UNSTABLE`

## Emendamento A — chance corpus finito

Data: 2026-09-15
Stato: `PREREGISTERED_BEFORE_CHANCE-COMPILER_IMPLEMENTATION`

Il primo census HU 10a ha misurato 20 decisioni preflop e 792 decisioni postflop pubbliche. Il
prodotto cartesiano V23, che include classe preflop e history completa `32/128/512`, ha un limite
superiore di 80.255.517.012 information set. Questo non è un conteggio di stati raggiungibili, ma
esclude la materializzazione ingenua del prodotto.

La calibrazione V23 userà quindi un'ulteriore astrazione chance esplicita: un corpus deterministico
di deal fisici completi. Per ogni classe CO vengono campionati `K` deal condizionati; ogni deal
riceve peso `class_mass / (630 * K)`. Il marginale delle 81 classi CO resta esatto, mentre hole BTN,
board e correlazioni blocker sono una distribuzione empirica congelata.

Il deal completo viene scelto da chance all'inizio, ma le information key rivelano soltanto le
carte osservabili nella street corrente. La scelta anticipata del runout non concede informazione
futura ai giocatori.

Il significato del risultato è limitato:

- `calculate_nash_conv` è esatta per il gioco finito identificato dal fingerprint del corpus;
- non certifica la distribuzione fisica completa;
- trainer Linear MCCFR e certificatore devono consumare lo stesso `FiniteGame` e lo stesso
  fingerprint;
- il report deve usare `CERTIFIED_ABSTRACT_EMPIRICAL_CHANCE`, non `CERTIFIED_PHYSICAL`;
- la stabilità si misura su `K = 2, 4, 8` e almeno due seed del corpus prima di aumentare lo stack.

Il primo gate HU 10a usa `K = 2` per correttezza e resume. `K = 8` è il primo candidato di
qualificazione. Un certificato non si trasferisce a un altro `K`, seed, stack o action abstraction.

## Esito HU10 K=8

Il run MC8 con 5.000.000 iterazioni ha raggiunto `normalized_dev=0,0081173483`. Il target
`<0,01` è stato attraversato al checkpoint da 4.500.000 iterazioni. Il certificato identifica il
corpus `fnv1a64:139ff63a167164ef` e il gioco finito `fnv1a64:7567a4a5a10b9393`.

Un secondo corpus, seed 23173, raggiunge `normalized_dev=0,0094551018` a 5.000.000 iterazioni.
Entrambi i corpus K=8 superano quindi il gate.

La sensibilità K=2/4/8 è stata eseguita con seed corpus 23171. I tre giochi finiti raggiungono il
target rispettivamente a 1,5M, 3M e 5M iterazioni, ma il profile EV CO è `+0,0863017485`,
`-0,1436597714` e `-0,2985411462` ante. La convergenza interna è confermata; la stima del valore
fisico non è stabile rispetto a K. L'esito non autorizza la certificazione fisica o il passaggio
automatico a HU40.

## Decisione

V23 introdurrà un gioco astratto condiviso dal trainer Linear MCCFR e dal certificatore
NashConv. La prima configurazione conserverà il mapping postflop V8 e le capacità
`32/128/512`, ma ricorderà la classe preflop e tutti i bucket osservati nelle street
precedenti.

V17 resta la baseline congelata. La sua policy può inizializzare un candidato V23, ma non può
ricevere retroattivamente un certificato perfect-recall: V17 ha addestrato decisioni unite dagli
information set V8 originali.

La certificazione fisica CO40 non è un requisito di V23. Il benchmark V22 ha chiuso quel percorso
sul kernel corrente con stato `INFEASIBLE_EXACT_BOARD_BATCHED`. V23 certificherà il gioco astratto
identificato dal proprio fingerprint.

## Obiettivo

Produrre, per una strategia media HU preflop:

- EV esatto del profilo nel gioco astratto;
- best response astratta esatta per CO;
- best response astratta esatta per BTN;
- deviation gain per entrambi i giocatori;
- NashConv, exploitability heads-up e `dEV` normalizzati;
- fingerprint riproducibile di gioco, policy e certificato.

Il certificatore deve funzionare con stack diversi senza modifiche al codice. Un nuovo stack
richiede una nuova compilazione del gioco e una nuova certificazione.

## Contratti distinti

V23 separa quattro identificatori:

| Identificatore | Contenuto |
| --- | --- |
| `rules_fingerprint` | Mazzo, ranking, ante, blinds, rake, stack e utility |
| `tree_fingerprint` | Range iniziali, azioni legali, sizing e public history |
| `abstraction_fingerprint` | Feature, capacità, mapping e modello di recall |
| `policy_fingerprint` | Strategia media, fallback, iterazioni e seed |

Il certificato è valido soltanto per la combinazione esatta dei quattro fingerprint.

## Astrazione V23 iniziale

### Carte

Il mapping conserva feature, seed di partizione e capacità V8:

| Street | Capacità |
| --- | ---: |
| Flop | 32 |
| Turn | 128 |
| River | 512 |

La chiave privata astratta ricorda tutte le osservazioni precedenti:

| Street corrente | Informazioni private ricordate |
| --- | --- |
| Preflop | classe preflop |
| Flop | classe preflop, bucket flop |
| Turn | classe preflop, bucket flop, bucket turn |
| River | classe preflop, bucket flop, bucket turn, bucket river |

La `public_history` conserva l'intera sequenza ordinata di azioni e chance pubbliche prevista dal
tree corrente.

### Azioni

V23 usa senza modifiche l'action abstraction della configurazione CO40 v2. Non aggiunge sizing,
non rimuove azioni e non traduce una size in un'altra durante la certificazione.

### Riduzioni lossless

Canonicalizzazione dei semi, riuso tra board equivalenti, batching e cache non cambiano il gioco.
Ogni percorso ottimizzato deve coincidere con l'oracolo scalare sui giochi ridotti entro `1e-10`.

## Definizione matematica

Sia `sigma_bar` la strategia media completa, inclusa la fallback serializzata. Per ogni giocatore
`i`:

```text
profile_ev_i = u_i(sigma_bar)
br_ev_i      = max_sigma_i u_i(sigma_i, sigma_bar_-i)
gain_i       = br_ev_i - profile_ev_i
```

La massimizzazione della best response è vincolata agli stessi information set, alle stesse
azioni e alle stesse osservazioni astratte usate dal training V23.

```text
nashconv_ante        = gain_co + gain_btn
exploitability_ante  = nashconv_ante / 2
normalized_nashconv  = nashconv_ante / initial_pot_ante
normalized_dev       = max(gain_co, gain_btn) / initial_pot_ante
```

Per CO40 v2 il piatto iniziale è `3a`. Il gate comparabile con il criterio postflop richiede:

```text
normalized_dev < 0.01
```

equivalente a un guadagno unilaterale massimo inferiore a `0.03a`.

## Perché il 10a non certifica il 40a

HU 10a e HU 40a sono due giochi estensivi differenti. Cambiare lo stack modifica:

- azioni legali e punti in cui compare l'all-in;
- rapporto stack-to-pot nelle continuation;
- numero e profondità delle sequenze di raise;
- payoff terminali e costo delle deviazioni;
- range raggiunti nei nodi postflop sotto la strategia addestrata.

Un certificato HU 10a dimostra che trainer e certificatore funzionano sul fingerprint HU 10a.
Non impone alcun limite alla NashConv HU 40a e non autorizza `nashconv_certified=true` sul
fingerprint HU 40a.

Il 10a resta utile perché può verificare formule, aggregazione degli information set, card
removal, resume e determinismo con un albero più piccolo. Il codice validato e le cache indipendenti
dallo stack possono essere riutilizzati nel 40a; il valore della NashConv non si trasferisce.

## Scala di esecuzione

### Fase 1 — Audit V8

Il checker visita gli information set V8 raggiungibili e cerca due history unite che differiscono
per un'osservazione privata precedente o per una sequenza di azioni personali. Ogni violazione
produce un witness serializzato.

Gate:

- determinismo con seed fisso;
- nessuna collisione non spiegata dal contratto V8;
- almeno un witness se il modello dichiara `imperfect_recall` e la collisione è raggiungibile;
- test negativo sulla chiave V23, che non deve riprodurre lo stesso witness.

### Fase 2 — Gioco HU 10a

Il 10a è il primo benchmark end-to-end della nuova infrastruttura:

```text
stack: 10a
ante: 1a
open: 3a, 5a, all-in
response: 6a sul piccolo open, 8a sul grande open, all-in
postflop: 33%, 66%, 120%, all-in
rake: disabilitato
```

La configurazione è congelata in
`benchmarks/fixtures/hu_preflop_hu10_calibration_v1.json`. I sizing HU 10a formano un gioco di
calibrazione autonomo; non sono una proiezione della policy CO40.

1. compilazione dell'`AbstractGameDefinition`;
2. training Linear MCCFR;
3. congelamento della strategia media;
4. profile EV e due best response;
5. certificato e verifica del resume.

Il gate 10a richiede:

- `gain_co >= -1e-12` e `gain_btn >= -1e-12`;
- uguaglianza fra oracolo e reducer entro `1e-10` sulle fixture enumerabili;
- NashConv finita e non negativa oltre il solo rumore numerico;
- trend decrescente ai checkpoint preregistrati;
- run ripetuto con fingerprint identici;
- memoria inferiore a `8 GiB`.

Il superamento del gate abilita il benchmark 20a, non la certificazione 40a.

### Fase 3 — Gioco HU 20a

Il 20a misura la crescita di nodi, information set, memoria e tempo. Il run viene interrotto prima
del training completo se la proiezione supera i limiti desktop.

Il report deve pubblicare:

- rapporto di crescita `10a -> 20a`;
- tempo di compilazione;
- tempo per profile EV e per ciascuna best response;
- picco di memoria;
- cardinalità raw e dopo il riuso;
- proiezione misurata per 40a.

### Fase 4 — Gioco HU 40a

Il 40a parte soltanto se la proiezione misurata rispetta:

- meno di `8 GiB` di RAM;
- meno di `60 minuti` per seed da 2M;
- certificazione riprendibile;
- nessuna proiezione superiore a `60 minuti` per il certificato finale.

Si eseguono due run matched:

```text
V23 seed 1, 2M iterazioni
V23 seed 2, 2M iterazioni
```

Configurazione congelata:

```text
Linear MCCFR
batch 32
8 worker
MC8
bucket 32/128/512
stesso tree e stessi sizing CO40 v2
```

## Gate di promozione rispetto a V17

V23 sostituisce V17 soltanto se supera tutti i controlli seguenti:

| Controllo | Soglia |
| --- | --- |
| Certificato | fingerprint completi e `CERTIFIED_ABSTRACT` |
| Deviazione | `normalized_dev < 0.01` |
| Stabilità | TV fra seed ridotta almeno del 10% rispetto a V17 |
| Call/Fold | nessuna regressione sui sette casi V17 |
| EV-loss fisica | differenza matched non peggiore; intervallo al 95% dichiarato |
| Risorse | meno di 60 minuti per seed e meno di 8 GiB |

WMAE e TV verso Monker restano diagnostiche perché il contratto esterno è incompleto. Non possono
da sole promuovere o respingere una soluzione.

## Artefatti

L'implementazione deve produrre:

- manifest del gioco astratto;
- census degli information set;
- report dell'audit perfect recall;
- checkpoint atomici del compilatore e del certificatore;
- profile EV e best-response witness;
- certificato JSON con formule, unità e fingerprint;
- confronto matched contro V17;
- benchmark `10a`, `20a` e `40a`.

Stati terminali ammessi:

```text
CERTIFIED_ABSTRACT_EMPIRICAL_CHANCE_TARGET_MET
CERTIFIED_ABSTRACT_EMPIRICAL_CHANCE_TARGET_NOT_MET
FAILED_RECALL_AUDIT
FAILED_CORRECTNESS_GATE
INFEASIBLE_RESOURCE_GATE
ESTIMATED_ONLY
INTERRUPTED_RESUMABLE
```

## Modifiche previste

| Area | Modifica |
| --- | --- |
| Contratto | nuovo `AbstractGameDefinition` versionato |
| Policy key | modalità V23 perfect-recall |
| Trainer | consumo dello stesso contratto del certificatore |
| Best response | reducer vincolato agli information set V23 |
| Persistenza | hash distinti per regole, tree, astrazione e policy |
| Test | recall, oracolo ridotto, determinismo, resume e corruzione |
| Benchmark | scala `10a -> 20a -> 40a` |

## Versioni successive

V23 non cambia capacità, sizing o continuation model. Le modifiche successive restano separate:

| Versione pianificata | Unica variabile ammessa |
| --- | --- |
| V24 | split adattivo dei bucket ad alta dispersione EV |
| V25 | aggiunta mirata di sizing nei nodi ad alto reach |
| V26 | correzione delle continuation se il holdout fisico localizza il bias |

Ogni versione richiede un nuovo fingerprint, un nuovo training e un nuovo certificato astratto.
Una policy precedente può fornire un warm start, ma il suo certificato non si trasferisce.

## Condizioni di arresto

Il lavoro si ferma senza dichiarare successo quando:

- trainer e certificatore non consumano lo stesso fingerprint;
- una collisione perfect-recall resta raggiungibile;
- una best response risulta inferiore al profile EV oltre `1e-12`;
- oracolo e percorso ottimizzato differiscono oltre `1e-10`;
- il resume cambia il risultato o il fingerprint;
- il gate desktop viene superato;
- manca uno dei due responder.

## Riferimenti

- [Protocollo V8](DISTRIBUTIONAL_STREET_ADAPTIVE_V8_PROTOCOL_2026-09-11.md)
- [Roadmap successiva alla trace V20](V20_POST_TRACE_SOLVER_IMPROVEMENT_ROADMAP_2026-09-14.md)
- [Implementazione whole-game V21](V21_WHOLE_GAME_NASHCONV_IMPLEMENTATION_REPORT_2026-09-14.md)
- [Protocollo del certificatore V22](V22_GENERIC_NASHCONV_CERTIFIER_PROTOCOL_2026-09-14.md)
- [Benchmark reducer board-batched V22](V22_CROSS_ROOT_BOARD_BATCHED_REDUCER_REPORT_2026-09-14.md)
