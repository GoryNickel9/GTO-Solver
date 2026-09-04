# Card abstraction e subgame solving

## Stato e confine semantico

Decisione di prodotto: 2026-09-04. GTOSD supporta una modalità astratta
esplicita per giochi che non sono eseguibili sul desktop senza decomposizione e
bucketing. Il percorso non astratto resta l'oracolo di correttezza e di parity;
non è il default commerciale obbligatorio per alberi preflop o postflop grandi.

`gtosd::abstraction` e `gtosd::subgame` sono librerie C++20 installabili e
indipendenti dalla GUI. Sono integrate end-to-end con `FiniteGame`, CFR+,
checkpoint, best response e NashConv. Il solver HU postflop dispone inoltre di
un percorso nativo opt-in nel `DenseLayout`: `solve_postflop_abstracted` e i
comandi CLI `solve-bucketed`, `resume-bucketed`, `query-bucketed` e
`certify-bucketed`. Le API e i comandi exact esistenti non selezionano mai
questo percorso implicitamente.

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

### Cache delle feature esatte

La generazione delle feature è separata dal clustering. Il manifest
`GTOSD_CARD_ABSTRACTION_FEATURE_CACHE 1 0` conserva osservazioni canoniche,
schema, fingerprint della sorgente e fingerprint semantico proprio. Il
fingerprint della sorgente lega la cache a config, range e layout combo-level;
il load rifiuta versione, schema, contenuto corrotto o sorgente differente.
Scrittura e sostituzione del file sono atomiche.

La cache non contiene il numero di bucket: la stessa enumerazione esatta può
quindi alimentare sweep `K=1..N` senza rieseguire i runout. Percorso diretto e
percorso cache devono produrre la stessa astrazione, lo stesso fingerprint di
gioco e checkpoint bit-identici. I report separano
`feature_preparation_seconds`, `abstraction_clustering_seconds`, fingerprint e
flag di riuso; il tempo di costruzione iniziale della cache non viene nascosto
nel traversal CFR+.

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

Il default production del minimizer è CFR+ con otto thread; `thread_count=1`
resta configurabile come oracle differenziale. Su un sottogioco giocattolo con
meno di otto rami chance indipendenti il pool usa soltanto i rami disponibili,
senza creare lavoro fittizio.

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

## Integrazione HU postflop nativa

Il percorso nativo conserva chance, card removal, payoff terminali, valori per
combo e best response sul gioco completo. Soltanto cumulative regret e average
strategy sono indicizzate per bucket. Per ogni update CFR+:

1. calcola action value e current value per combo esatta;
2. aggrega nel bucket il regret controfattuale pesato per il reach privato
   iniziale della combo;
3. aggrega l'average strategy con il reach corrente della combo;
4. applica una sola proiezione CFR+ `max(0, R + delta)` per bucket/azione.

Proiettare prima ogni delta combo e poi sommarlo cambierebbe l'algoritmo ed è
vietato. Le partizioni board/player determinano la mappa combo→bucket; ogni
decision node conserva comunque uno stato strategico distinto, quindi history
e action schema non vengono fusi.

La prima configurazione qualificata è intenzionalmente stretta:

- CFR+ alternato;
- stato `Float64` residente o out-of-core;
- otto thread solver (`parallel_action_depth=7`: sette worker più il thread
  chiamante);
- canonical public DAG e isomorfismo lossless attivi;
- nessun root lock, replay diagnostico, Pure-CFR o street decomposition.

DCFR, `ScaledUint16RegretStrategy` e i diagnostici incompatibili vengono
rifiutati, non eseguiti con semantica non validata. Più di otto thread vengono
rifiutati dall'API. Il fingerprint del layout
include versione, feature schema, parametri e tutte le assegnazioni
deterministiche; resume/query/certification con configurazione exact o bucket
count differente falliscono con checkpoint mismatch. La query combo espone
`abstraction_bucket` e `abstraction_bucket_size`; il report CLI persiste
fingerprint, granularità, compression ratio, weighted MSE e provenienza/tempi
della feature cache. Il fingerprint della cache è telemetria di provenienza,
non parte dell'identità dell'astrazione: dati diretti e dati cache equivalenti
devono restare semanticamente intercambiabili.

## Evidenza di qualifica iniziale

Il test nativo usa range frazionari e asimmetrici, card removal, confronto
exact/abstracted, fingerprint ripetibile, round-trip checkpoint, resume
continuo/segmentato bit-identico, lift combo-level e certificazione full-game
exact-NashConv. Verifica inoltre che le API prepared exact e abstracted non
siano intercambiabili.

Sul fixture CLI river fixed-board a range uniformi, 8 bucket riducono 1.860
infoset exact a 32 infoset solver (`58,125x`) e 200 iterazioni raggiungono
`NashConv/pot = 0,751763%` sotto certificazione exact. Il microbenchmark
end-to-end a 1.000 iterazioni riduce lo stato Float64 da 1.536 a 384 byte
(`4x`) e misura `12,673 ms` exact contro `9,250 ms` bucketed. K=3 resta però a
`5,339393%` NashConv/pot: un wall inferiore su questo gioco minuscolo non è una
prova di speedup utile o di qualità production.

La qualifica Release multi-granularità usa cinque ripetizioni e mediana su 8
logical CPU a 3,6 GHz. La cache turn (66 partizioni, 744 osservazioni) richiede
`21,289 ms` wall. Sul fixture fixed-river da 48 infoset esatti, lo sweep CFR+
da 1.000 iterazioni riusa una sola cache e produce:

| Bucket/partizione | Infoset astratti | Stato Float64 | Compressione | Weighted MSE | Max L2 | Wall mediano | NashConv/pot |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 4 | 128 B | 12x | 0,294077 | 0,869201 | 8,146 ms | 15,151551% |
| 2 | 8 | 256 B | 6x | 0,069559 | 0,395394 | 8,798 ms | 10,315783% |
| 3 | 12 | 384 B | 4x | 0,026860 | 0,236189 | 9,250 ms | 5,339393% |
| 6 | 24 | 768 B | 2x | 0,002066 | 0,078730 | 9,443 ms | 0,001636% |
| 12 | 48 | 1.536 B | 1x | 0 | 0 | 10,414 ms | 0,001968% |

L'errore delle feature è monotono su questo sweep ed è nullo alla granularità
identità; NashConv a iterazioni fisse non è invece una metrica monotona di
errore di astrazione. La fixture è ridotta e qualifica i contratti, non sceglie
una granularità commerciale universale.

## Preflight e qualifica a otto thread

`preflight-bucketing-gto-plus` enumera soltanto la forma canonica e, per ogni
K richiesto, pubblica upper bound di infoset, action entries e stato CFR+
Float64. Stima separatamente cache logica/serializzata, doppio spazio della
scrittura atomica, transienti del clustering, scratch degli otto thread e
disco page-backed. L'out-of-core non riceve un vantaggio RSS non misurato: il
bound conservativo assume che tutte le pagine mappate possano diventare
residenti. Il formato rifiuta oltre 2.000.000 osservazioni prima di enumerare.
L'upper bound serializzato include entrambi gli identificatori, le due
occorrenze del combo id e i bit pattern decimali completi di peso e feature;
il gate disco combinato conserva anche entrambe le copie della sostituzione
atomica mentre esiste lo state page-backed.

Il builder exact-feature usa otto worker su partizioni board/player
indipendenti. Ogni worker scrive in uno slot preassegnato e il merge segue
l'ordine canonico, quindi il fingerprint non dipende dallo scheduling. Su
AHKHQH flop il fingerprint resta `e0179a28bd72d999`; il wall scende da circa
`4,15 s` seriali a `1,57246 s`, con Peak RSS `23.076.864 B`. Su TH7D6S il
builder a otto worker completa 280.308 osservazioni in `169,398 s`, Peak RSS
`314.556.416 B`, mentre il vecchio percorso seriale era ancora incompleto
dopo oltre nove minuti.

La qualifica K=16 AHKHQH flop esegue cinque processi indipendenti, ciascuno con
otto thread, cache condivisa, CFR+ Float64, delay zero e best response esatta
combo-level. Tutti passano NashConv strettamente sotto 1% e RAM richiesta:
NashConv deterministico `0,6029051267%`, wall mediano `9,936001 s`, Peak RSS
mediano `25.227.264 B`. L'oracolo seriale separato misura `0,6099145321%`; la
differenza assoluta `7,0094e-5` in frazione è sotto il gate `1e-4`. Questa è
equivalenza numerica quantificata, non identità bitwise sul fixture grande.

TH7D6S richiede una granularità differente: K=16 resta a `5,924317%` dopo 800
iterazioni, mentre K=128 raggiunge `0,8816749004%` a 400. Cinque processi a
otto thread producono la stessa metrica, wall mediano `435,349473 s` e Peak RSS
mediano `694.796.288 B`. L'oracolo seriale misura `0,8770330545%` in
`1.493,513047 s`; delta `4,64185e-5` e speedup mediano `3,43x`.

## Limiti aperti

- La cache esatta evita di rigenerare i runout tra granularità e il preflight
  ne stima transienti e spazio disco, ma il formato testuale non è ancora
  compresso o memory-mapped.
- Il container `.gtsd` non ha ancora un chunk `ABSTRACTION`; checkpoint e
  report CLI sono persistibili, ma il packaging prodotto completo richiede
  config/range/abstraction esterni identici e verificati dal fingerprint.
- La GUI non espone ancora il selettore di granularità.
- `SubgameSolver` safe è production-installabile sul contratto `FiniteGame`;
  il taglio e merge arbitrario di un frontier nel `DenseLayout` postflop resta
  da collegare. Un solve postflop bucketed dalla root non va chiamato resolving
  mid-tree.
- Lo sweep multi-granularità ridotto è qualificato; servono fixture
  flop/turn/preflop commercialmente rappresentative prima di scegliere default.
  L'exact resta l'oracolo per questi gate e per la successiva parity GTO+.
