# Card abstraction e subgame solving

## Stato e confine semantico

Decisione di prodotto: 2026-09-04. GTOSD supporta una modalità astratta
esplicita per giochi che non sono eseguibili sul desktop senza decomposizione e
bucketing. Il percorso non astratto resta l'oracolo di correttezza e di parity;
non è il default commerciale obbligatorio per alberi preflop o postflop grandi.

`gtosd::abstraction` e `gtosd::subgame` sono librerie C++20 installabili e
indipendenti dalla GUI. Sono integrate end-to-end con `FiniteGame`, CFR+,
checkpoint, best response e NashConv. Il collegamento al `DenseLayout` nativo
HU postflop è ancora un'attività distinta: finché non è completato, il comando
`postflop solve` continua a dichiarare correttamente `uses_bucketing=false`.

Una soluzione bucketed è approssimata rispetto al gioco di carte originale.
`exact outcomes` può ancora indicare l'enumerazione completa della chance nel
gioco astratto, ma non annulla l'errore di astrazione.

## Contratto `CardAbstraction`

La configurazione `1.0` dichiara:

- `kind`: `exact_identity` oppure `equity_feature_kmeans`;
- `buckets_per_partition`;
- `maximum_iterations` di Lloyd;
- `feature_schema_id`, che identifica significato e scala delle feature.

Ogni osservazione rappresenta un information set privato esatto e contiene
`information_set`, `partition`, player, combo fisica, board mask, reach weight e
vettore di feature. La partizione identifica insieme public state, history,
street, attore e action schema. Il clustering tra partizioni differenti è
vietato. Player o dimensionalità differenti nella stessa partizione producono
un errore.

Una combo omessa è assente o bloccata. Una combo presente con peso zero resta
nel risultato ed è distinta dall'omissione. La sovrapposizione tra hole cards e
board viene rifiutata prima del clustering; i pesi non sono rinormalizzati
silenziosamente.

### Feature postflop esatte

`build_exact_postflop_equity_observations` costruisce, per flop, turn o river,
il vettore:

```text
[P(loss), P(tie), P(win), P(win) + 0.5 P(tie)]
```

contro il range pesato avversario. Tutti i runout mancanti sono enumerati e
rispettano card removal; non viene usato sampling. Il costo è preparatorio e va
misurato separatamente dal traversal CFR. Preflop e feature più ricche possono
essere fornite tramite lo stesso contratto versionato, con uno
`feature_schema_id` differente.

### Clustering e metrica

Per ogni partizione il k-means deterministico minimizza:

```text
sum_i w_i ||x_i - centroid(bucket(i))||^2
```

L'inizializzazione è una segmentazione quantile stabile delle feature ordinate;
i pareggi mantengono l'assegnazione precedente. Gli stati a peso zero non
alterano i centroidi pesati, ma ricevono un bucket deterministico. Il report
pubblica:

- information set esatti e astratti;
- compression ratio;
- weighted mean squared error;
- massimo errore L2;
- `uses_lossy_bucketing`.

Configurazione, osservazioni e assegnazioni producono un fingerprint stabile e
indipendente dall'ordine dell'input. La serializzazione testuale 1.0 conserva i
bit IEEE delle feature e dei pesi e ricostruisce il fingerprint al load.

`apply_card_abstraction` riscrive realmente gli information set consumati da
CFR. Se due stati raggruppati hanno player o azioni differenti, la validazione
del gioco fallisce. Il fingerprint del gioco astratto include quello
dell'astrazione, quindi un checkpoint exact non può essere riaperto come
bucketed o viceversa.

## Contratto `SubgameSolver`

Un frontier è un insieme di radici non sovrapposte. Tutti gli information set
decisionali raggiungibili dal frontier devono essere interamente interni: un
taglio che lascia occorrenze dello stesso information set dentro e fuori viene
rifiutato. `derive_reach_weighted_subgame_roots` propaga chance e strategie di
entrambi i player dal blueprint e si ferma al frontier.

Il sottogioco preserva nodi, azioni, payoff e information set e aggiunge, quando
necessario, una chance root con probabilità proporzionale al reach del
blueprint. La sua identità include nodi e pesi di frontiera. CFR+ è il minimizer
predefinito; algoritmo, seed, delay e iterazioni restano nel checkpoint.

Sono disponibili due modalità:

- `unsafe_isolated`: deploy esplicito del candidato senza garanzia di
  non-regressione e senza eseguire best response sul gioco completo; i campi
  di certificazione restano assenti, non valorizzati artificialmente a zero;
- `exact_nash_conv_guard`: best response esatta sul gioco completo prima e
  dopo il merge.

Il candidato viene distribuito soltanto se:

```text
NashConv(candidate_full) <= NashConv(blueprint_full) + safety_tolerance
```

Altrimenti `BlueprintFallback` restituisce il blueprint invariato. Nei giochi
two-player zero-sum questo controlla anche l'exploitability perché
`exploitability = NashConv / 2`. Con rake viene controllata NashConv, non viene
inventata un'exploitability zero-sum.

## Composizione bucketing + resolving

`solve_abstract_subgame` impone questo ordine:

1. materializza il gioco bucketed;
2. esegue il subgame solve con CFR+;
3. rialza candidato e blueprint su ogni information set esatto;
4. calcola le best response sul gioco esatto originale;
5. decide candidate/fallback usando esclusivamente le metriche esatte rialzate.

Una metrica migliore nel solo gioco astratto non può quindi autorizzare il
deploy. Per giochi che non consentono una best response esatta, questa modalità
non scala ancora: servirà un gadget safe con boundary counterfactual values e
un bound dichiarato. Una stima campionata non potrà essere chiamata garanzia
esatta.

## Gate di promozione al postflop nativo

Prima di attivare bucketing nel `DenseLayout` HU postflop sono obbligatori:

1. aggregazione reach-weighted dei regret per bucket, senza update collision;
2. feature/fingerprint persistiti nella soluzione;
3. query combo che esponga bucket e metrica di errore;
4. confronto contro il percorso non astratto su giochi enumerabili;
5. card removal e range asimmetrici;
6. resume continuo/segmentato;
7. benchmark RAM, tempo e NashConv su più granularità;
8. UI/CLI che non abiliti bucketing implicitamente.

Finché questi gate non sono verdi, i nuovi moduli sono il percorso di
composizione production per giochi finiti e il fondamento preflop, non una
reinterpretazione silenziosa dell'attuale solver postflop.
