# Algoritmi del solver

## Framework di riferimento

`gtosd_solver` supporta su giochi finiti validabili:

- Vanilla CFR;
- CFR+;
- Linear CFR;
- Discounted CFR (DCFR);
- ProductionDcfr con schedule bounded `1.5/0/3`;
- External Sampling MCCFR;
- Linear MCCFR.

Questo laboratorio valida formule, checkpoint, averaging e best response su
Kuhn, Leduc e giochi ridotti. Non implica che ogni algoritmo sia disponibile
nel workflow postflop di prodotto.

## Percorso HU postflop production

Il percorso production corrente usa `ProductionDcfr`, una schedule DCFR signed
exact-outcome con aggiornamenti alternati su tutte le combo e chance
compatibili. Il contratto e' `alpha=1.5`, `beta=0`, `gamma=3`, regret signed e
average immediato (`averaging_delay=0`). L'average viene azzerato alle
iterazioni one-based `1,2,5,17,65`; dopo 65 l'ultima epoca viene mantenuta. Non
usa sampling o bucketing nel percorso product. Espone `exact_identity` 1.0,
che nomina il mapping identità senza cambiare il gioco o allocare stato, e
`made_hand_value` 1.0 come mapping sperimentale risolvibile soltanto dal probe
R2-S `float64`, con checkpoint separato. L'isomorfismo globale e il DAG
canonico sono riduzioni lossless; lo stato cumulativo production usa il codec
node-scaled uint16 dichiarato nella specifica di precisione.

L'intero percorso, inclusi exact BR e certificazione, viene eseguito su CPU con
stato e workspace in RAM. Un backend GPU non fa parte delle varianti ammesse e
non è un'estensione pianificata: ottimizzazioni future devono restare CPU-only.

Per ogni giocatore una traversata calcola i valori counterfactuali e produce
delta separati. Nel production backend, all'iterazione `t`, il clock positivo
e' `r=t-1` fino a 65 e `r=t-2` dopo 65; i regret positivi precedenti sono
moltiplicati per `r^alpha/(r^alpha+1)` e quelli non positivi per `1/2`.
All'interno di ogni epoca l'average strategy accumula con peso cubico
`(k+1)^3`, dove `k` parte da zero al reset. Questa forma additiva e' equivalente
fino a scala comune al discount ricorsivo gamma 3. La strategia esposta deriva
dall'accumulatore reach-weighted.

## Varianti

| Algoritmo | Regret | Averaging | Uso corrente |
|---|---|---|---|
| Vanilla CFR | somma integrale | uniforme | laboratorio |
| CFR+ | cumulativo troncato a zero | con delay | oracle/fallback exact |
| Linear CFR | peso crescente con iterazione | lineare | laboratorio |
| DCFR | discount separato positivo/negativo/strategy | parametrico `1.5/0/2` di default | laboratorio/comparator |
| Production DCFR | DCFR signed, reset bounded e pesi cubici | contratto fisso `1.5/0/3` | postflop production |
| External Sampling MCCFR | stima campionata | dipende dal campione | laboratorio |
| Linear MCCFR | external sampling con peso `t` su regret e average | lineare | preflop V23 e laboratorio |

DCFR continua a esporre `alpha=1.5`, `beta=0`, `gamma=2` come variante
parametrica versionata. `ProductionDcfr` ha identità distinta sia nel checkpoint
postflop (`11`) sia nel laboratorio `FiniteGame`; non accetta una gamma
alternativa. Il resume generico attraverso il confine d'epoca è byte-identico
al run continuo. MCCFR registra seed e non può essere chiamato exact.

Il percorso campionato del `FiniteGame` calcola la strategia corrente soltanto per gli
information set visitati e applica delta sparse. Gli information set non visitati non ricevono
né regret né massa media, quindi questa riduzione non cambia l'algoritmo. Sul gioco HU10 V23
`K=2`, 512 iterazioni producono lo stesso checkpoint SHA-256 del percorso denso; il tempo di
training scende da 64,2828711 s a 1,7173435 s. Il lookup precompilato nodo→information set e i
delta indicizzati dal buffer riducono inoltre K=8/100k da 22,5572053 s a 12,5287590 s, con
checkpoint SHA-256 invariato. Costruzione della strategia media completa, validazione e best
response restano operazioni dense eseguite ai checkpoint dichiarati.

## Parallelismo

`resolve_postflop_production_options` è l'unico confine autorizzato per nuovi
solve e resume di prodotto. Il profilo versionato 1.0 risolve algoritmo,
precisione, delay, target stretto, certificazione, isomorfismo, canonical DAG e
policy di parallelismo. I caller possono scegliere target, limite iterazioni e
numero di thread entro il massimo di otto; non possono sostituire schedule,
codec o backend. Un resume eredita lo stato ProductionDcfr compatibile e viene
rifiutato se identifica CFR+ o metadati ambigui.

Il postflop può parallelizzare action subtree fino a una profondità configurata.
I worker producono buffer separati e una riduzione deterministica. Thread count,
profondità e impatto sulla convergenza fanno parte del benchmark. Nessun lock
globale deve entrare nell'hot path.

Il parallelismo è esclusivamente tra thread CPU. Non sono ammessi offload GPU,
kernel compute o riduzioni numeriche eseguite da acceleratori esterni.

## Certificazione

La best response è separata dall'update CFR. La certificazione registra profile
value, best-response value, NashConv, NashConv/pot e payoff sum. Il criterio di
arresto viene valutato solo a intervalli di certificazione dichiarati; una
certificazione finale è obbligatoria.

Nel percorso HU preflop decomposto, la BR globale identifica anche il profilo
composito delle boundary postflop. Un fingerprint basato sul solo blueprint
preflop non è sufficiente: il certificatore rifiuta una BR calcolata contro
continuazioni diverse, anche se tree e strategia preflop coincidono.

La riduzione canonica delle boundary River trasforma insieme board e combo
privata. Per ogni runout rappresentante enumera l'orbita che stabilizza il
Flop, rimappa la combo nelle coordinate del public root e applica una sola
volta la molteplicità del Flop. Moltiplicare una CFV per-combo per la sola
dimensione dell'orbita senza questa rimappatura non è un quoziente lossless.

Le reach che alimentano una root River vengono ottenute riproducendo l'intera
history Flop/Turn. A ogni azione si moltiplica soltanto la reach del player che
agisce per la probabilità della strategia media della sua combo; chance e
blocker vengono applicati quando la street avanza. Il provider della strategia
è separato da questo propagatore e deve restituire probabilità finite in
`[0,1]`.

La frequenza della certificazione cambia il tempo osservato e deve essere
pubblicata. Non è lecito confrontare il solo traversal GTOSD con il tempo
end-to-end GTO+.

### Completamento e copertura della policy

Una policy portata su un altro corpus può non definire tutti gli information set del gioco
target. `complete_strategy_profile` offre due contratti: `reject_missing` rifiuta la policy;
`uniform_unseen_v1` completa le sole chiavi mancanti con una distribuzione uniforme sulle azioni
legali. Le chiavi sorgente estranee al target vengono contate ma non copiate nel profilo completo.
Una voce malformata viene respinta anche quando non compare nel gioco target.

L'audit usa due metriche complementari. Se `I` è l'insieme delle chiavi richieste e `M` quello
delle chiavi presenti e compatibili:

```text
exact_key_coverage = |M| / |I|
```

Sia `r(v)` la probabilità on-policy di raggiungere il nodo decisionale `v`, incluse chance e
azioni di entrambi i giocatori. Indicando con `U` i nodi la cui chiave è stata completata:

```text
reach_weighted_coverage = 1 - sum(v in U, r(v)) / sum(v decisionale, r(v))
```

La seconda quantità misura la quota delle visite decisionali attese coperta dalla policy appresa;
non è una counterfactual reach e non sostituisce `exact_key_coverage`. Entrambe sono riportate
anche per giocatore. La versione corrente opera sul `FiniteGame` HU; l'estensione multiway
richiede prima la generalizzazione delle utility e degli indici giocatore del core.

### Protocollo training/response/evaluation

Il runner HU10 usa tre campioni indipendenti della stessa definizione astratta:

1. T addestra la strategia media con Linear MCCFR;
2. R completa e congela la policy T, quindi calcola una best response esatta per giocatore nel
   gioco finito R;
3. E completa la policy e le due risposte con `uniform_unseen_v1`, poi valuta baseline e deviazione
   sugli stessi outcome;
4. il confronto paired usa `gain_i(x) = u_i(response_i, sigma_-i; x) - u_i(sigma; x)`;
5. le medie per classe CO vengono ricomposte con `class_mass / 630`.

La best response su R produce una NashConv esatta della policy completata nel solo gioco finito R.
Su E la risposta resta congelata: ottimizzarla di nuovo su E contaminerebbe il campione di
valutazione. Un gain negativo su E è ammesso e segnala che la risposta R non generalizza; non viene
troncato a zero.

Il piano adattivo raddoppia K mantenendo il prefisso deterministico di ogni strato. La correzione
Bonferroni copre i cinque intervalli usati dallo stop e tutti i look dichiarati. Gli intervalli
restano basati su un'approssimazione normale; K piccolo serve solo allo smoke engineering. Il gain
della risposta candidata non è un upper bound dell'exploitability fisica, perché R non enumera
tutte le chance e il completamento uniforme interviene sulle chiavi non osservate.

`RbpReadOnlyTelemetry` può osservare checkpoint DCFR legacy e ProductionDcfr,
ma non cambia la traversata. Sul profilo product D/V alle iterazioni 20 e 32 non
trova candidati che superino neppure il proxy CFR originale per una singola
iterazione. Quel proxy non costituisce comunque un bound valido per gli sconti
signed, i clock e i reset della media ProductionDcfr. R6 non ha quindi introdotto
pruning o lazy update nel percorso product.

## Checkpoint e resume

Il checkpoint conserva iterazioni, delay, precisione e accumulatori. Il resume
continua la stessa traiettoria solo con fingerprint e configurazione compatibili.
Cambiare algoritmo, action layout o significato delle utility richiede un nuovo
solve o una migrazione esplicita.

## Node locking

Il node locking globale di prodotto non è ancora implementato. F10.4 prevede un
root lock esterno combo-per-combo limitato a test diagnostico: il root non viene
aggiornato, le continuation restano libere e la certificazione riguarda il gioco
vincolato. Questo percorso non deve essere presentato come equilibrio del gioco
originale.

## Algoritmi futuri

### Common Random Numbers root sperimentale

Il trainer HU preflop espone `root_common_random_numbers` come controllo di varianza opzionale e
disattivato per default. Nei batch congelati, le azioni del traverser alla root usano lo stesso
seed di continuazione. Ogni valore d'azione conserva la propria distribuzione marginale; cambia
soltanto la correlazione fra valori stimati nello stesso update.

La modalità non aggiunge rollout e non altera pesi, reach o regret matching. L'ID algoritmo la
registra separatamente. Il suo beneficio non è garantito: una covarianza positiva riduce la
varianza delle differenze fra azioni, mentre una covarianza negativa può peggiorarla. Per questo
resta una modalità di ricerca soggetta a replica su due seed, gate temporale e diagnostiche
pairwise.

Outcome Sampling, continual resolving e depth-limited solving non sono capacità
production correnti. R2-S fornisce due primitive di laboratorio: traversal
postflop bucketizzato con lift/certificazione originale e resolving safe con
boundary CFV. Il bridge river postflop materializza esattamente ogni deal
privato pesato entro limiti espliciti, importa il blueprint ProductionDcfr e
verifica il gadget opt-out con BR globale. S3 combina le primitive e certifica
il risultato nel gioco fisico. Queste capacità restano bounded e sperimentali:
A0 è più lenta su D/V, V perde qualità e il prodotto resta exact/no bucketing.
Il percorso HU preflop selezionato usa una decomposizione exact per root
pubblica Flop. I subgame River sono risolti con `ProductionDcfr`; boundary CFV,
terminali anticipati e reach delle azioni vengono ridotti verso una boundary
Flop task-local. Il livello superiore riusa queste continuation durante gli
update preflop/Flop/Turn. Non è MCCFR: carte, blocker, fold e showdown sono
enumerati. La qualifica finale richiede best response e NashConv globali exact.
Il loop convergente delle street superiori e la BR globale sono ancora da
implementare, quindi la decomposizione corrente non è ancora un solver HU
preflop qualificato.

Il motore postflop espone separatamente CFV root per combo sotto strategia
media e sotto best response exact. Entrambi i report ricompongono il valore
autorevole del checkpoint entro `1e-9`. Il bridge applica a entrambi lo stesso
lift orbitale blocker-aware e li accumula in canali River task-local distinti.
Il tag di modalità entra nei fingerprint e impedisce di usare una BR come
boundary della strategia media. Questo chiude le foglie River del futuro
calcolo BR globale decomposto; non costituisce ancora la scelta ottima alle
decisioni Flop/Turn o preflop.

La futura ricorsione upper-street usa una vista Turn-major del catalogo River.
Per ogni task Flop raggruppa i runout per Turn canonico e, dentro il gruppo,
espone uno span per ciascuna history River. Il valore viene quindi ridotto e
massimizzato separatamente dopo ogni osservazione Turn; sommare prima tutti i
Turn e scegliere poi l'azione non sarebbe una best response valida.

Multiway richiederà una nozione di soluzione e metriche separate; NashConv HU
zero-sum non viene trasferita per assunzione.

## Target memory-bounded approvato

L'ADR
[`ADR_0002_MEMORY_BOUNDED_EXACT_SOLVER.md`](../ADR_0002_MEMORY_BOUNDED_EXACT_SOLVER.md)
selezionava DCFR standard (`alpha=1.5`, `beta=0`, `gamma=2`) come regret
minimizer production iniziale del nuovo kernel canonico, con CFR+ come oracle e
fallback. La decisione successiva del 2026-09-01 promuove la schedule comune
`ProductionDcfr` descritta sopra; l'ADR resta la motivazione del backend
memory-bounded, non l'autorita' della schedule corrente. Le tre fixture
AHKHQH, TH7D6S e TSTC9D applicano lo stesso contratto e non esiste selezione per
benchmark. `DcfrPlus` conserva una proiezione non-negativa custom e un
identificatore distinto.

## ProductionDcfr river bucket-native sperimentale

Il kernel river-native riusa la schedule ProductionDcfr `1.5/0/3` ma cambia il
gioco risolto. La chance iniziale seleziona una coppia di bucket con massa
derivata dai deal fisici compatibili; il traversal alternato percorre soltanto
l'albero pubblico. Regret e strategy sum sono `float64` e indicizzati per
decisione e bucket del player attivo.

La modalità è enumerata, non MCCFR: non campiona chance outcome. È però
astratta, perché tutte le combo con lo stesso `HandValue` finale condividono la
strategia. Il lift sul gioco fisico rende misurabile questa perdita tramite BR
e NashConv originale. Il solver accetta un solo thread e checkpoint 1.0
separati; non è un backend production e non cambia il contratto exact.

### Variante exact-blocker v2

Il kernel supporta anche `exact_blocker_signature_v2`. La chance resta
enumerata e ProductionDcfr conserva formula e schedule; cambia soltanto la
partizione delle mani. Due combo condividono stato strategico quando hanno lo
stesso valore finale e la stessa compatibilità contro tutte le combo avversarie
attive.

Questa partizione è lossless per il River costruito, ma sul corpus qualificante
degenera nell'identità: una classe per combo e una coppia per deal in 12/12
fixture. Il candidato non riduce quindi traversate o stato strategico utile e
viene respinto per fattibilità. Non è un algoritmo production alternativo e non
autorizza un'estensione a street precedenti.

### Analizzatore weighted-equitable

L'analizzatore successivo non è un solver. Calcola la partizione stabile più
grossolana ottenibile conservando `HandValue` e massa avversaria compatibile
per classe. In questo modo valuta una famiglia più ampia di quozienti lossless
rispetto alla firma v2 con etichette fisiche fisse.

Il full range uniforme ammette 45 classi per player; i 12 range pesati del
corpus v2 non ammettono fusioni. Il pre-gate chiude quindi il candidato prima
dell'allocazione di regret e strategy sum. ProductionDcfr e la sua traiettoria
numerica non sono stati modificati.
