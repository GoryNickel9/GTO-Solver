# Contratti R2-S: card abstraction e subgame DCFR

Stato: R2-S chiusa come fattibilità A0. S1 include traversal e checkpoint
postflop bucketizzati; S2 include il bridge river exact e bounded; S3 è testata
nel gioco originale. Nessun bucket coarse è promosso nel prodotto.

## 1. Confine comune

`ProductionDcfr` resta il motore di aggiornamento con parametri `1.5/0/3`.
Action abstraction, card abstraction e decomposizione del gioco sono tre
dimensioni indipendenti. Cambiare bucket non autorizza a cambiare size, regole,
range o chance outcome.

Il percorso exact senza bucketing resta l'oracolo. Un candidato è valutato nel
gioco originale dopo aver riportato la strategia agli infoset fisici. La
convergenza del solo gioco astratto non è un gate di qualità originale.

Il confine postflop riconosce due policy 1.0. `exact_identity` è il solo percorso
di prodotto: il mapping `i -> i` è implicito, non alloca un vettore per infoset
e conserva perfect recall, conteggi e identità del checkpoint.
`made_hand_value` è un percorso di laboratorio risolvibile in `float64`, con
fingerprint e checkpoint propri. Le riduzioni di semi e del DAG restano
canonicalizzazioni lossless precedenti al confine. Versioni o modalità
sconosciute vengono rifiutate.

## 2. Candidato A — strategy tying deterministico

Il formato `GTOSD_CARD_ABSTRACTION 1 0` identifica una policy con:

- `policy_id`, metrica e street scope non vuoti;
- versione major/minor esplicita;
- claim di perfect recall `identity_verified` oppure `not_claimed`;
- mapping completo `original_information_set -> abstract_information_set`;
- peso di aggregazione finito e strettamente positivo per ogni origine.

Tutti gli infoset nello stesso bucket devono avere lo stesso player e la stessa
lista ordinata di action ID. Duplicati, copertura parziale, versioni future e
bucket incompatibili vengono rifiutati. Soltanto il mapping identità può usare
`identity_verified`; il codice non deduce perfect recall da nomi o feature.

Il prototipo rinomina gli infoset ma conserva l'intero albero fisico. Ne segue
che probabilità chance, blocker, collisioni di carte, payoff e card removal non
cambiano. Il mapping lega le strategie delle mani nello stesso bucket: riduce
gli action-entry strategici, non i nodi attraversati. I pesi servono
all'aggregazione di una strategia fisica preesistente; non alterano le reach
chance durante DCFR.

La policy ha fingerprint FNV-1a sulla serializzazione canonica. Un checkpoint
astratto resta identificato dal fingerprint del gioco trasformato; non può
essere ripreso contro il gioco exact. La policy è un artefatto separato e deve
accompagnare checkpoint e soluzione prima di qualsiasi integrazione prodotto.

### Byte model A

Per `N` infoset originali e `K` bucket, il payload denso minimo previsto è:

`4*N mapping + 8*N aggregation_weight + 4*(K+1) bucket_offset` byte.

Il formato testuale, gli allocator e le stringhe sono conteggiati separatamente
come `serialized_policy_bytes`; non vengono nascosti nel payload minimo. Sul
Kuhn di test, `N=12`, `K=4`: il mapping coarse richiede almeno 164 byte e riduce
gli action-entry strategici da 24 a 8, ma non riduce le traversate.

Il layout postflop usa un modello diverso e dichiarato: ID bucket locale a 16
bit per infoset, offset a 64 bit per decisione e bucket, ID del nodo pubblico a
32 bit e indici membro a 16 bit. Non duplica i pesi dei range. Le capacità degli
allocator non sono incluse e restano fuori dal totale minimo pubblicato.

### Primo mapping postflop coarse

`made_hand_value` raggruppa soltanto mani private dello stesso giocatore nello
stesso nodo decisionale pubblico. La chiave è l'intero `HandValue` corrente:
categoria e kicker ordinati, calcolati sulle migliori cinque carte disponibili
su flop, turn o river. Il mapping è deterministico e non usa ID, board name o
risultati dei benchmark. Non unisce nodi pubblici, non elimina runout e non
modifica range, blocker, payoff o action abstraction.

La policy conserva ogni mano fisica nel traversal previsto, ma lega la sua
strategia al bucket. Questo può perdere informazione strategica dipendente dai
blocker anche quando il valore della mano coincide; perfect recall e qualità
nel gioco originale non sono quindi rivendicati. Ogni entry conserva il peso
iniziale del range in basis point per audit e lift. Durante il training il
regret deve usare pesi controfattuali correnti e l'average strategy la reach
propria corrente: il peso statico del manifest non può sostituirli.

Il kernel double-precision di riferimento applica un update ProductionDcfr a
un bucket: regret matching condiviso, somma pesata dei regret immediati,
discount signed una sola volta per azione e average strategy pesata con la
reach propria. Verifica identità a un solo membro, invarianza all'ordine dei
membri, separazione dei due pesi e rifiuto dei valori non finiti. Il traversal
postflop lo usa come oracolo campionato su 1.024 update e applica gli stessi
update direttamente allo stato bucket.

Misure Release A0 sul corpus di sviluppo/validazione D/V, a 32 iterazioni:

| Fixture | Infoset exact | Bucket | Mapping | Stato bucket | A0 operativo | Solve exact | NashConv A0/exact |
|---|---:|---:|---:|---:|---:|---:|---:|
| D-RIVER-CHECK-001 | 1.015.872 | 72.052 | 4.700.960 B | 1.152.832 B | 3,64483 s | 0,662169 s | ~0 / ~0 |
| V-RIVER-BET-001 | 1.997.952 | 141.328 | 9.242.624 B | 4.478.080 B | 7,89550 s | 1,01448 s | 0,00330981 / 0,00175170 |

I fingerprint risultano rispettivamente `fnv1a64:5d66ebe092d31392` e
`fnv1a64:c650ee934c5de4bb`. I tempi sono osservazioni singole del test e non
benchmark di qualificazione. Su V il massimo delta di profilo è `0,00590437`
ante e il massimo delta di BR è `0,0109969` ante.

### Gate A

1. Identità: stesso output DCFR del riferimento exact.
2. Persistenza: round trip e fingerprint stabili; versioni future rifiutate.
3. Mapping: copertura totale, firme action/player compatibili, pesi validi.
4. Qualità: lift completo e NashConv/BR calcolati nel gioco originale.
5. Postflop: traversal e checkpoint sono integrati nel laboratorio; nessuna
   promozione finché costo, UI/CLI, `.gtsd` e qualità su un corpus più ampio non
   superano i gate.

Il test coarse `FiniteGame` converge nel gioco astratto ma conserva NashConv
originale maggiore di `0.1`. Il mapping postflop D/V dimostra riduzione
strutturale e rende misurabile l'errore originale. A0 è più lenta dell'exact e
non è qualificata.

## 3. Candidato B — resolving con boundary CFV

Il subgame è definito da un public state stabile, non da un singolo nodo che
potrebbe separare informazioni private incompatibili. Il blueprint e il
resolver usano entrambi `ProductionDcfr`.

Il formato boundary `GTOSD_SUBGAME_BOUNDARY 1 1` contiene:

- versione, fingerprint del gioco, ruleset e action abstraction;
- identità e fingerprint del blueprint, algoritmo e iterazione certificata;
- public-state ID, history e street di ingresso;
- reach avversaria per ogni stato privato compatibile;
- CFV blueprint per ogni infoset privato dell'avversario al confine;
- reach controfattuale per ogni root history fisica, coerente con il totale
  dell'infoset privato;
- stato esplicito per reach zero;
- precisione, unità, normalizzazione, checksum e policy di sicurezza.

Un resolver safe deve offrire all'avversario, per ogni stato privato pertinente,
un'alternativa che preservi almeno il CFV del blueprint e deve derivare il
vincolo nel gioco costruito. Una verifica BR globale ex-post può bocciare il
risultato, ma non rende safe una costruzione priva del gadget/constraint.

Boundary mancanti, duplicati, non finiti, appartenenti a un altro fingerprint,
azioni off-tree e partizioni private incomplete vengono rifiutati. Reach zero
non autorizza a inventare valori: il record resta presente e marcato.

### Byte model B

Il record denso di progetto usa almeno 24 byte per stato privato: ID a 32 bit,
flag/status con padding, reach `float64` e CFV `float64`. Al totale si sommano
16 byte per ogni root history fisica (ID, padding e reach `float64`),
header/checksum, mapping public/private, stato DCFR del blueprint, stato del
resolver, strategia splice e buffer di BR. Il picco qualificante è la memoria
contemporanea realmente necessaria, non il solo file boundary o il solo
resolver.

Il tempo operativo è:

`blueprint + boundary extraction + tutti i resolve + splice + BR/verifica + I/O obbligatorio`.

### Gate B

1. Estrazione public-state chiusa rispetto agli infoset.
2. Boundary completi per range asimmetrici e reach zero.
3. Rifiuto di off-tree, fingerprint e precisione incompatibili.
4. Persistenza atomica, checksum e fallback esplicito.
5. BR globale su giochi ridotti: nessun aumento oltre la tolleranza derivata.
6. Contabilità end-to-end e picco contemporaneo.

La fattibilità è verificata per giochi finiti ridotti two-player zero-sum. Il
prototipo valida la partizione, deriva reach/CFV, costruisce il gadget opt-out,
lo risolve con DCFR e applica soltanto la strategia del resolving player. Su
Kuhn e Short Deck river toy la BR globale avversaria non aumenta oltre la
tolleranza `0.001`. Public state e boundary usano envelope con checksum e
sostituzione atomica; una corruzione viene rifiutata. Il bridge river postflop
materializza esattamente ogni deal privato pesato entro limiti espliciti e
importa il blueprint dal checkpoint exact. Sulla fixture a 5 deal e 46 nodi,
profile value e NashConv coincidono con l'oracolo a `1e-9`; la BR avversaria è
`-1,8` prima e dopo il resolve. Chance pubbliche interne, rake non zero nel gate
safe, limiti superati e checkpoint incompatibili vengono rifiutati.

## 4. Decisione S0

Il candidato A procede in S1 sul riferimento ridotto, mantenendo il percorso
postflop exact invariato. Il candidato B procede in S2 dal contratto
public-state/boundary e dai gadget Kuhn/Short Deck toy verso il porting
postflop; non verrà sostituito da un semplice solve locale con range
condizionati.

S3 è abilitata soltanto nel test bounded dopo i gate isolati. La fixture riduce
12 infoset a 4 e certifica nel gioco fisico exact, bucket-only e combinazione;
NashConv passa da `0,156251` a `0` dopo il resolve combinato. Il corpus holdout H
rimane sigillato fino alla conferma finale e non determina feature, K, street o
decomposizione.

## 5. Contratto sperimentale river bucket-native 1.0

Il contratto vale soltanto quando il board river è completo. Per ogni player il
builder valuta ogni combo legale e usa l'intero `HandValue` finale come bucket.
Per i bucket `i,j`, la massa chance è:

`M(i,j) = sum(weight_0(h0) * weight_1(h1))`

dove la somma include soltanto combo `h0,h1` compatibili fra loro e con il
board. La probabilità della coppia è `M(i,j) / sum(M)`. Questa aggregazione
conserva pesi, blocker e card removal nella distribuzione iniziale; non conserva
l'informazione privata che distingue due combo nello stesso bucket durante le
decisioni.

Il traversal usa un solo albero pubblico. Ogni riga strategica è identificata
da nodo decisionale, player e bucket del player attivo. Fold, call e showdown
usano payoff pubblici o l'esito costante della coppia di `HandValue`. Nessun deal
fisico viene visitato durante un'iterazione. Le combo fisiche restano disponibili
per query, lift e BR/NashConv nel gioco originale.

Vincoli 1.0:

- HU e river fisso; il settlement applica il rake configurato, mentre la
  qualifica numerica corrente copre zero-rake;
- ProductionDcfr `alpha=1.5`, `beta=0`, `gamma=3`, averaging delay zero;
- un thread e massimo 16 azioni per decisione;
- limiti espliciti per deal preprocessati, coppie bucket e oracle
  materializzato;
- nessuna dichiarazione di equivalenza exact o perfect recall del gioco
  originale.

Il checkpoint `GTOSD_RIVER_BUCKET_CHECKPOINT 1 0` include fingerprint source,
fingerprint dell'astrazione e fingerprint del gioco bucket. Il loader rifiuta
una diversa astrazione, un checkpoint exact, versioni incompatibili, trailing
data e payload oltre 256 MiB. Il fingerprint source copre la configurazione
completa, incluso il rake, oltre ad albero e range. La scrittura usa un file
temporaneo, checksum FNV-1a del payload e sostituzione atomica.

Il byte model separa metadata bucket, runtime, stato solver, mirror checkpoint,
scratch e `FiniteGame` opzionale. Il totale predefinito esclude l'oracolo
materializzato e l'overhead degli allocator; entrambi devono essere dichiarati
quando presenti.

### 5.1 Qualifica del contratto 1.0

Il corpus River v1 usa sette strati scelti senza output del solver, range
deterministici pesati e soglie fissate prima del solve. Ogni fixture richiede
congiuntamente qualità nel gioco fisico, speedup, riduzione dei nodi e byte
model. Il report Release 2026-09-06 conclude `REJECTED`, 0/7.

L'aggregazione conserva la massa iniziale corretta, ma il lift impone la stessa
strategia a combo con `HandValue` uguale e blocker diversi. La best response del
gioco fisico sfrutta questa informazione: bucket NashConv e delta rispetto
all'exact falliscono in tutti e sette gli strati. Inoltre il costo del builder
supera il risparmio di traversal quando restano molte coppie di bucket.

Il contratto 1.0 resta valido come formato sperimentale e regressione, ma non è
qualificato per il prodotto. Non va esteso a street precedenti. Un successore
deve avere una nuova identità di astrazione, includere feature blocker-aware e
dimostrare i propri gate su un nuovo holdout; il corpus v1 non può guidarne il
design.

## 6. Contratto sperimentale River exact-blocker 2.0

Per un player `p`, due combo attive `h` e `h'` appartengono alla stessa classe
2.0 soltanto se:

1. `HandValue(h, board) == HandValue(h', board)`;
2. per ogni combo attiva `o` dell'avversario,
   `compatible(h, o) == compatible(h', o)`.

La seconda condizione è memorizzata come bit vector ordinato sugli ID combo
avversari. A range e board fissati, i membri di una classe hanno lo stesso
supporto chance e lo stesso esito di showdown contro ogni stato privato
avversario. Legare la loro strategia è quindi lossless per il gioco River
costruito. La definizione non identifica automorfismi congiunti che permutano
anche le combo avversarie; è una relazione sufficiente, non la prova del
quoziente minimo possibile.

Il builder conserva gli stessi vincoli HU, River fisso, ProductionDcfr `1.5/0/3`,
un thread e massimo 16 azioni del contratto 1.0. `abstraction`, chiavi infoset e
fingerprint distinguono `made_hand_value_v1` da
`exact_blocker_signature_v2`. Lift e certificazione usano la partizione
effettiva, non ricostruiscono bucket dal solo valore della mano.

### 6.1 Gate e decisione

Il corpus v2 riusa il corpus v1 congelato come regressione e aggiunge cinque
holdout indipendenti. In tutte le 12 fixture la partizione produce una classe
per combo: 96/96 sulle regressioni e 112/112 sugli holdout. Di conseguenza
`bucket_pairs == physical_deals` e `bucket_nodes_per_pass ==
physical_nodes_per_pass`.

Il test full-range conferma lo stesso limite: 465 classi per player, 188.790
coppie compatibili e 6.620.596 B di modello nativo. La qualifica Release è
`REJECTED`, 0/12: nodi e speedup falliscono sempre; byte model passa 12/12 e i
gate di qualità passano 11/12. L'eccezione è una divergenza di convergenza a
iterazione fissata fra stato product quantizzato e stato v2 double sulla
fixture con rake, non una collisione astratta.

La versione 2.0 non è qualificata per il prodotto e non sostituisce exact. Il
contratto vieta l'estensione implicita a Turn o preflop. Un candidato lossless
successivo deve formalizzare automorfismi del grafo compatibilità/showdown; un
candidato che unisce firme diverse è approssimato e richiede un'altra identità
di formato e propri limiti d'errore.

## 7. Partizione equa pesata River 3.0 — solo fattibilità

Per un River HU fisso si costruisce il grafo bipartito fra combo attive. Un arco
esiste se le due combo non collidono. La partizione iniziale usa `(player,
HandValue)`; il raffinamento separa due combo quando differisce la massa totale
del range avversario compatibile in almeno una classe avversaria corrente.

La relazione stabile deve preservare:

- lo stesso payoff di showdown per ogni classe opposta;
- la stessa massa chance avversaria, inclusi blocker e card removal;
- lo stesso albero pubblico, action set, rake e utility;
- una strategia comune sollevabile alle combo fisiche.

Il peso della combo propria non è una feature di classe: scala reach e update
di quella combo, ma non cambia la sua risposta ottima quando payoff e
distribuzione avversaria coincidono. I pesi delle combo avversarie sono invece
parte della firma. Il costruttore deve fallire oltre il limite di deal e deve
verificare l'equità dopo la convergenza.

Questa relazione non autorizza una fusione approssimata. Sul corpus v2 produce
solo singleton e fallisce il gate minimo `1,25x`; perciò resta un analizzatore
di fattibilità e non definisce un formato solver/checkpoint 3.0.

## 8. Oracle CFV e memoria per astrazioni approssimate

Un candidato approssimato deve essere confrontato contro il profilo exact nella
stessa proiezione fisica. La strategia bucket viene sollevata alle combo
originali prima di calcolare questi indicatori:

1. NashConv e best response nel gioco originale;
2. distanza di variazione totale della strategia per infoset root;
3. errore assoluto della CFV di strategia, normalizzato sul pot iniziale;
4. errore massimo delle CFV d'azione, normalizzato sullo stesso pot.

Le CFV usano chance reach e reach dell'avversario, senza includere la reach del
player proprietario dell'infoset. La media root è pesata per counterfactual
reach. Il diagnostico deve ricostruire il valore del profilo alla radice e
rifiuta probabilità non normalizzate, action set differenti o infoset mancanti.

La memoria espone tre grandezze distinte: byte model dello stato, Peak RSS del
processo e incremento Peak RSS osservato nella singola fase con campionamento a
1 ms. Il delta di fase non è memoria posseduta dal solver; il baseline delle
fasi successive può includere pagine trattenute dall'allocator.

Il report v1 del 2026-09-07 applica questo oracle alle sette fixture River
congelate. `made_hand_value_v1` riduce i nodi da `8,34x` a `892,33x`, ma fallisce
7/7: NashConv fisica `0,5506%–5,3387%`, CFV media `0,39%–7,41%` del pot ed
errore massimo di una CFV d'azione `28,80%–88,76%`. Il Peak RSS dell'intero
workflow ridotto è `107.356.160 B`. Il contratto 1.0 resta una regressione
sperimentale e non è promosso.

## 9. Showdown distribution 3.0

La firma v3 preserva il `HandValue` River esatto. Per ogni combo hero calcola,
contro il range avversario attivo:

- massa compatibile in ciascuna delle nove `HandCategory`;
- massa degli esiti hero loss, tie e win.

Le dodici masse intere sono normalizzate sul peso avversario precedente al card
removal della combo hero e arrotondate al quantum dichiarato. La qualifica
iniziale fissa `500` basis point normalizzati. Il quantum deve appartenere a
`[1, 10000]` ed entra nel fingerprint dell'astrazione. Il mapping è
deterministico e dipende da board e range, non dall'identità della fixture.

Il corpus v3 riusa le sette regressioni v1 e aggiunge cinque holdout strutturali
congelati prima del primo solve. A 1.024 iterazioni la qualifica è `REJECTED`
12/12: quality gate completi 0/12, speedup 0/12, riduzione minima 5/12 e byte
model 12/12. Le coppie bucket `434–9.240` rendono il kernel da `7,6x` a `123,9x`
più lento dell'exact ottimizzato. NashConv originale massima `2,6527%`, TV root
massima `32,37%`, CFV media massima `4,07%` del pot.

Il profile-value gate passa 12/12 ma NashConv delta passa 2/12. Perciò la
somiglianza dell'EV aggregato non autorizza la fusione delle strategie. V3 resta
un esperimento River; non definisce bucket Flop/Turn, non viene usato dal solver
preflop e non cambia il prodotto.

## 10. Decomposizione HU preflop exact 1.0

Il confine Flop conserva una reach di sequenza `float64` per ciascuna delle 630
combo fisiche e per ciascun giocatore. Il board elimina le combo bloccate senza
rinormalizzare silenziosamente il range. La policy preflop resta definita sulle
81 classi perché tutte le combo della stessa classe condividono la decisione,
ma chance, compatibility mass e payoff rimangono fisici.

L'isomorfismo dei semi è lossless solo perché la policy preflop è suit-symmetric
per costruzione. Ogni rappresentante canonico registra la propria molteplicità;
la somma deve essere 7.140. Se una futura policy distingue i semi, il catalogo
va rigenerato con il gruppo di automorfismi che preserva quella policy oppure
disattivato.

Un boundary identifica tree, blueprint e continuazione tramite fingerprint.
Per ogni combo avversaria viva registra counterfactual reach, CFV condizionale
e stato di reach positivo/zero. La CFV esclude le azioni dell'avversario ma
include chance, reach di sequenza del resolving player e reach delle azioni
postflop del player di cui si sta accumulando il valore. Il validatore separa
le due reach, rifiuta probabilità fuori `[0,1]` e impedisce di attribuire a una
combo valore proveniente da una propria azione che non ha raggiunto la root.

Il boundary River estratto da `ProductionDcfr` può usare la strategia media del
checkpoint oppure la best response exact contro quella strategia. La
ricomposizione delle CFV pesate deve coincidere rispettivamente con il profile
value o con il best-response value autorevole entro `1e-9`. Range e checkpoint
devono avere lo stesso fingerprint. Boundary, accumulatore e aggregato portano
un tag di modalità; un contributo `ExactBestResponse` non può entrare nel
canale `AverageStrategy` o nell'assemblatore Flop del profilo. Lo scheduler, il
suo checkpoint e le due accumulazioni numeriche River sono implementati;
update delle street superiori, ricorsione BR Flop/Turn ed esecuzione iterativa
restano aperti.
Fino alla loro chiusura questo contratto non abilita un solve HU completo né
modifica il prodotto.

Blueprint e boundary Flop sono persistibili con schema 1.0; il decomposition
plan usa lo schema 1.2. Tutti usano checksum e sostituzione atomica. Il
fingerprint del piano include conteggi,
probabilità, byte model, reach e catalogo canonico; quello del boundary include
resolver, opponent, reach e CFV. Alterare uno di questi dati invalida il file.

La certificazione dell'intero gioco è distinta dalla validazione locale. Per il
CO40 richiede `2 * 5.157 = 10.314` boundary validi, copertura della massa
postflop e una best response globale exact legata agli stessi fingerprint.
Boundary completi con BR assente, campionata o sopra soglia non sono un
certificato di equilibrio. Questo requisito resta fail-closed anche quando il
profilo root appare vicino al benchmark.

Il piano espone separatamente le frontiere fisiche e canoniche. Per CO40 le
prime sono `9 × 7.140 = 64.260`; le seconde sono `9 × 573 = 5.157`. La
certificazione usa soltanto i `10.314` boundary canonici dei due player, ma le
molteplicità devono ricostruire esattamente la chance fisica. I rispettivi
payload densi sono `130.699.008 B` canonici e `1.628.605.440 B` fisici.

La materializzazione delle boundary River è vietata: `732.976.992` record per
lato richiederebbero `9.288.284.442.624 B`; corrispondono a `366.488.496`
stati pubblici, risolti una sola volta per ottenere entrambe le CFV. Il batch
plan 1.5 mantiene insieme ogni coppia di resolver e impone riduzione verso
lo stato Flop/Turn e ordina le root per entry, Flop canonico, history River,
runout canonico e resolver. Il root catalog registra 5.157 span contigui e la
molteplicità fisica di ogni runout. Ogni batch resta entro un solo span; un
checkpoint può avanzare soltanto sul
batch successivo e dopo che il fingerprint dell'accumulatore è cambiato.

Ogni record River 1.3 contiene 528 valori nelle coordinate del Flop
rappresentante. Il bridge enumera l'orbita del runout che stabilizza quel Flop,
permuta insieme le combo private e applica la molteplicità del Flop. La
moltiplicazione scalare delle sole 465 combo vive sul River rappresentante è
incompatibile con il contratto blocker-aware. Gli accumulatori e aggregati
persistiti usano schema v4; v1-v3 vengono rifiutati.

Per la ricorsione BR, i runout di ogni task sono inoltre partizionati per Turn
canonico osservato. Ogni gruppo conserva offset e conteggio dei board River,
massa fisica e una lista di span di root per ciascuna history River. L'unione
degli span copre ogni root del task esattamente una volta e mantiene adiacente
la coppia dei resolver. Questa vista consente uno scheduling Turn-major senza
duplicare il catalogo globale e impedisce di massimizzare una decisione Turn
dopo aver sommato osservazioni pubbliche differenti.

Le 3.792 history terminali prima del River hanno un ordinale stabile per task.
Il valutatore replaya ogni history sul board concreto, propaga separatamente la
reach delle azioni dei due player e calcola utility fold o showdown enumerando
tutti i runout residui. Flop fold e Flop all-in producono 812 runout ordinati
per deal privato compatibile; i terminali Turn ne producono 28 per ciascun
turn. L'assemblatore parte dall'aggregato River, accetta i terminali soltanto
nell'ordine del manifesto e finalizza due boundary Flop solo dopo aver provato,
per ogni combo viva, la conservazione esatta della massa
`molteplicità Flop × 812 × reach preflop compatibile`.
