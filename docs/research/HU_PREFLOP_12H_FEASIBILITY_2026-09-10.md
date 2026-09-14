# Preflop HU Short Deck: colli di bottiglia e fattibilità entro 12 ore

**Data:** 10 settembre 2026. **Esito:** obiettivo non dimostrato; il percorso corrente non è qualificato per training più certificazione rigorosa dell'intero gioco entro 12 ore. Non è stata dimostrata l'impossibilità del problema: sono stati individuati difetti e costi del percorso attuale, e un esperimento capace di decidere la fattibilità di un'architettura diversa.

## 1. Decisione tecnica

Il problema non si risolve aumentando il numero di iterazioni MCCFR. Ci sono quattro ostacoli distinti, in ordine di dipendenza:

1. **L'averaging corrente usa un estimatore scorretto per external sampling.** Prima di confrontare algoritmi o prolungare il training va corretta e verificata questa parte matematica.
2. **La rappresentazione fisica sparse produce soprattutto nuovi infoset.** A 100.000 iterazioni, 3.575.469 dei 3.575.493 infoset River ricevono una sola visita. Il campionamento riduce il lavoro per traversata, ma non rende piccolo lo spazio da apprendere.
3. **Ogni visita ripete lavoro evitabile:** valutazione dello stesso showdown, costruzione delle azioni, transizioni, canonicalizzazione e, nella modalità astratta, calcolo online dei bucket. Una sonda isolata dello showdown dà già un fattore 1,51 sul tempo del percorso fisico, senza risolvere il punto precedente.
4. **Il certificatore è un secondo collo di bottiglia autonomo.** Una singola root River poco profonda richiede 13,09 s per valutare il profilo e 16,11 s per la best response exact nel percorso corrente. Anche un training molto rapido lascerebbe aperto questo problema.

La direzione sostenuta dalle fonti è: **MCCFR con averaging verificato, controllo della dimensione dello stato, campionamento pubblico e riduzione della varianza; valutazione della policy con vettori di mani e best response che riutilizza il lavoro sul public tree.** Per rispettare una scadenza rigida potrebbe servire un certificato conservativo su un albero parziale, con limiti validi per tutti i rami omessi. La letteratura ne stabilisce la possibilità in alcune classi di giochi, non il successo entro 12 ore su questa fixture.

Non sono stati modificati file di produzione. Le modifiche compilate appartengono esclusivamente a sonde diagnostiche separate. [Dati e script](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/docs/research/preflop_12h_evidence_20260910/README.md).

## 2. Che cosa deve significare «completato in meno di 12 ore»

La precisazione dell'utente richiede **certificazione rigorosa dell'errore sull'intero gioco**, non soltanto una matrice preflop stabile o vicina a Monker. Uso la fixture HU CO40 esistente, senza modificarne le regole: 36 carte, 630 combo, 81 classi preflop con masse 6/4/12; stack 40 ante; CO con 1 ante; BTN con 1 ante più 1 button blind; call incrementale del CO pari a 1 ante; rake zero; colore sopra full house; scala A-6-7-8-9. Root fold/call/raise-to 6/raise-to 10/all-in e continuazioni preflop come dichiarati nel contratto. Il candidato locale usa postflop 33/66/120% e all-in, compresa l'eccezione esplicita per il raise incompleto configurato; l'equivalenza dell'albero postflop Monker è in attesa di conferma. [Contratto locale](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/docs/HU_PREFLOP_CO40_BENCHMARK.md:12).

«Intero gioco» indica qui **tutte le carte e continuazioni del gioco con queste size discrete**. Non certifica le puntate escluse dall'action abstraction, altri stack, altre strutture di ante o il multiway.

Per il profilo medio esportato σ, nel gioco HU zero-sum:

\[
NC(\sigma)=\sum_{i=0}^1\left[\max_{\tau_i}u_i(\tau_i,\sigma_{-i})-u_i(\sigma)\right].
\]

Poiché le due utility del profilo sommano a zero, la NashConv è anche la somma dei due valori di best response. Il gate locale è `NC / 40 ≤ 0,01`, cioè **NC ≤ 0,4 ante**. Con la convenzione HU `exploitability = NC/2`, equivale a **0,2 ante**. Il riferimento alla normalizzazione è indispensabile: «1%» senza denominatore non definisce l'accuratezza.

Un successo richiede un upper bound valido su questa quantità, legato a una policy immutabile, al fingerprint del gioco e a un margine numerico documentato. Una BR ottenuta enumerando il gioco è exact rispetto allo spazio di decisione; usare `double` non costituisce, da solo, una prova dell'errore di arrotondamento. Per la lettura stretta di «rigoroso» occorrono limiti numerici conservativi o un'analisi dell'errore verificata.

Il cronometro deve includere preparazione specifica del gioco, training, estrazione della policy, certificazione finale e salvataggio consultabile. L'avvio dell'applicazione può restare escluso. Eventuali tabelle generiche già disponibili vanno dichiarate, con costo di costruzione separato; la preparazione specifica del caso non può essere nascosta fuori dalle 12 ore.

### Il confronto con Monker non certifica questa condizione

Il repository registra un run inferiore a 12 ore su una versione modificata di Monker, ma non ne conosce versione precisa, stopping rule o NashConv. La FAQ pubblica di Monker dichiara di non avere le risorse per calcolare l'exploitability dell'intera strategia e riconosce l'incertezza sulla propagazione dell'errore postflop al preflop. Questo non descrive necessariamente la versione modificata dell'utente; dimostra che la documentazione pubblica non sostiene l'equivalenza fra quel tempo e il requisito di certificazione scelto qui. [FAQ Monker](https://www.monkerware.com/faq.html).

La guida pubblica propone bucket e una regola empirica sul rapporto iterazioni/nodi; avverte anche che grandi alberi preflop possono richiedere giorni. Sono indicazioni operative, non upper bound sull'errore. Il limite richiesto di 12 ore resta il nostro vincolo, ma va misurato sul risultato più forte ora richiesto. [Guida Monker](https://monkerware.com/guide.html).

## 3. Provenienza delle misure

| Voce | Condizione verificata |
|---|---|
| Hardware | Intel Core i3-10100F, 4 core fisici e 8 processori logici; nominalmente 32 GiB RAM |
| Memoria fisica visibile / disponibile all'inizio | 34.294.738.944 / 14.350.233.600 byte; disponibilità istantanea, non limite della macchina |
| Sistema e compilazione | Windows x64, MSVC 19.51.36248.0, Release `/O2 /Ob2 /DNDEBUG /MD`, C++20 |
| Checkout | HEAD `ffb208a0dad018dc537288c42239322ff7dfde6d`, working tree già modificato dall'utente |
| Librerie usate | `out/build/codex-release-20260907`; il vecchio build `windows-release-current` non è stato usato |
| Esecuzione | 16 processi di training sequenziali; un thread di training; un seed fisso; nessuna concorrenza fra le sonde |
| Confronto cache | 3 repliche per variante e modalità, ordine A/B invertito nella replica intermedia |
| Profilazione | 4 run con timer e contatori di visite; overhead aggiuntivo di 8 byte per infoset |
| Valutazione nei run brevi | 20.000 deal del profilo; 5.000 iterazioni per risposta appresa; 10.000 deal per risposta |

La specifica Intel conferma che otto thread logici non equivalgono a otto core fisici. Nessuna stima usa 8× come speedup misurato. [Intel](https://www.intel.com/content/www/us/en/products/sku/203473/intel-core-i310100f-processor-6m-cache-up-to-4-30-ghz/specifications.html).

Gli hash dei sorgenti e delle librerie sono nel [manifest](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/docs/research/preflop_12h_evidence_20260910/source_manifest.json); sono necessari perché il solo SHA Git non identifica questo working tree. Comandi, tempi e memoria sono in [runs.json](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/docs/research/preflop_12h_evidence_20260910/runs.json). Le statistiche derivate sono in [analysis_summary.json](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/docs/research/preflop_12h_evidence_20260910/analysis_summary.json).

`solve_seconds` parte dentro `SampledSolver::solve`, dopo la costruzione della tabella e del prefisso di discount. Include training, valutazione campionata, training delle risposte apprese e loro valutazione; include l'estrazione opzionale della policy se richiesta. Non include costruzione iniziale, serializzazione su file e lifecycle completo. I confronti A/B usano lo stesso perimetro. Il campo `wall_seconds` registra invece il processo della sonda, compreso avvio e polling. Nessuno dei due è un tempo di certificazione globale.

I timer strumentati servono a localizzare il lavoro, non a promettere percentuali esatte di CPU in produzione. `bucket_nested` e `canonical_nested` sono inclusi in `key_inclusive` e non vanno sommati nuovamente. La memoria osservata dal processo comprende più del payload delle tabelle; non è confrontata con la memoria mostrata da GTO+.

## 4. Prima anomalia: l'averaging di external sampling

Nelle traversate preflop e postflop, ogni visita a un nodo non bloccato esegue:

```cpp
information->strategy_sum[action] += reach[actor] * strategy[action];
```

L'aggiornamento avviene sia quando l'attore è il traverser, sia nel passaggio dell'altro giocatore. Lo stesso schema compare nel refinement preflop. [Sorgente, riga 740](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/libs/preflop/src/hu_preflop_solver.cpp:740), [postflop, riga 858](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/libs/preflop/src/hu_preflop_solver.cpp:858).

La media comportamentale corretta pesa la strategia all'infoset con la reach **del suo giocatore**. Nell'external sampling HU, però, parte di quella reach è già incorporata nella probabilità di visitare l'infoset. Moltiplicarla di nuovo senza correggere la probabilità di inclusione cambia la media.

Controesempio esatto: P0 entra con probabilità `x`, P1 continua con probabilità `y`, poi P0 sceglie A/B. Nel passaggio P0 il nodo è incluso con probabilità `y` e accumula `x·σ`; nel passaggio P1 è incluso con probabilità `x` e accumula ancora `x·σ`. In totale, l'accumulatore atteso usa **`x(y+x)·σ`**, anziché `x·σ` a meno di una costante comune nel tempo.

| Profili successivi | Media corretta di A | Normalizzazione degli accumulatori attesi locali |
|---|---:|---:|
| `(x,y,σA)=(0,1;0,5;1)` e `(0,9;0,5;0)`, pesi uniformi | 10% | 4,54545% |
| Stessi profili, pesi relativi `(t+1)^3` del discount corrente | 3,18725% | 1,39130% |

Il calcolo usa frazioni razionali, senza floating point, chance o astrazione. È un controesempio alla formula dell'accumulatore, **non un benchmark di convergenza del poker**. La tabella normalizza valori attesi degli accumulatori; non afferma che il rapporto abbia la stessa aspettativa in un numero finito di campioni. [Script e asserzioni](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/docs/research/preflop_12h_evidence_20260910/averaging_counterexample.py).

L'implementazione di riferimento OpenSpiel documenta e applica per HU il semplice averaging sui soli nodi dell'avversario del traverser, accumulando la policy senza questa moltiplicazione; offre anche un percorso separato di averaging completo. Il confronto sostiene il difetto, non impone di copiare quella libreria. Con discount e update alternati va definita e verificata l'esatta convenzione temporale scelta. [OpenSpiel, external sampling](https://github.com/google-deepmind/open_spiel/blob/master/open_spiel/python/algorithms/external_sampling_mccfr.py).

Il problema non altera direttamente il peso relativo alla root dove entrambe le reach sono 1, ma altera le continuazioni usate per apprendere e valutare il preflop. La vicinanza delle frequenze root non lo esclude. Gli smoke test locali del sampler controllano validità, serializzazione e numeri finiti; non costituiscono il test indipendente di aspettativa dell'averaging che serve qui.

### Discount: rischio separato ancora da misurare

`DcfrTable::touch` applica ai regret negativi `0,5^Δt`, con `Δt` espresso in iterazioni globali, anche per infoset raramente visitati. Dopo dieci iterazioni senza visita il fattore è 0,0009765625; dopo cento circa 7,89×10⁻³¹. In uno spazio dominato da visite isolate, il trattamento asimmetrico del rumore merita una verifica specifica. È un **rischio algoritmico osservabile nel codice**, non una causa quantitativa dimostrata da questi run.

Il lavoro originale DCFR contiene risultati campionati per Linear MCCFR con discount periodico di regret e media in funzione di blocchi di nodi visitati. Non è una validazione automatica del trapianto `1.5/0/3` in questa implementazione sparse. Confrontare vanilla external sampling, Linear MCCFR e la variante corrente richiede prima averaging corretto e poi errore contro tempo, con stessa astrazione e semi distinti per training e partizione. [Brown e Sandholm, 2019](https://arxiv.org/pdf/1809.04040).

## 5. Collo di bottiglia strutturale: visite, stato e astrazione

Il campionamento delle carte private rende piccola una traversata; non assicura che la traversata successiva riutilizzi gli stessi parametri. Le misure mostrano questa distinzione.

| Modalità strumentata | Iterazioni | Infoset complessivi | Infoset River | River con una visita | Tempo sonda |
|---|---:|---:|---:|---:|---:|
| Bucket con memoria delle street | 20.000 | 1.396.374 | 574.555 | 99,9643% | 15,77 s |
| Fisica, isomorfismo lossless | 20.000 | 1.699.846 | 643.474 | 100% | 10,05 s |
| Fisica, isomorfismo lossless | 100.000 | 8.996.964 | 3.575.493 | 99,9993% | 43,73 s |
| Bucket memoryless | 100.000 | 670.341 | 464.857 | 33,03% | 56,68 s |

Nella modalità fisica a 100.000 iterazioni, anche il Turn ha il 99,9648% di singleton; il Flop il 95,7833%. I 1.620 infoset preflop sono invece riutilizzati molte volte. Le decisioni preflop apprendono quindi contro continuazioni che in gran parte non hanno ancora ricevuto aggiornamenti ripetuti. L'average policy di un infoset visitato una sola volta resta uniforme, perché l'accumulo avviene prima dell'aggiornamento dei regret.

Durante la valutazione separata della policy fisica da 100.000 iterazioni, 12.668 delle 14.789 query postflop non trovano l'infoset e ricadono sulla policy uniforme: **85,66%**. Contare anche le query preflop nasconderebbe il problema. [Log della sonda fisica](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/docs/research/preflop_12h_evidence_20260910/profile_exact_100000_0.stderr.log).

La stessa sonda osserva 1.898.389.504 byte di peak working set, circa 1,77 GiB, già a 100.000 iterazioni. Il payload corrente senza strumentazione è 144 byte per chiave e stato; la sonda ne usa 152. Restano fuori da quel payload bucket dell'hash map, allocator, tabelle delle risposte, cache e vettore di discount lungo O(T). Un'estrapolazione lineare della RAM fino a 12 ore non sarebbe affidabile: crescita e saturazione dipendono dalla rappresentazione. È però già dimostrato che l'attuale vantaggio di velocità per visita si accompagna a un riuso insufficiente.

La modalità memoryless riutilizza più stato, ma cancella informazione ricordata dal giocatore e introduce imperfect recall. La riduzione della tabella non è una prova di accuratezza. I bucket correnti usano categoria ed equity Monte Carlo con 8 campioni e granularità diverse per street; mani con equity media simile possono avere blocker, distribuzioni di esiti e opzioni future molto diverse. Inoltre il seed di training viene usato anche per costruire i bucket: cambiare seed cambia contemporaneamente traiettoria e gioco astratto. Le due sorgenti vanno separate prima di misurare variabilità o convergenza.

La letteratura potential-aware propone distribuzioni e transizioni fra street, non soltanto un valore medio di equity. Offre un criterio per migliorare la partizione, non un certificato gratuito sul gioco originale. [Ganzfried e Sandholm, 2014](https://www.cs.cmu.edu/~sandholm/potential-aware_imperfect-recall.aaai14.pdf). Le garanzie di trasferimento fra gioco astratto e originale richiedono ipotesi e limiti d'errore espliciti; non seguono dalla sola convergenza nel gioco piccolo. [Kroer e Sandholm, 2017](https://proceedings.neurips.cc/paper/7342-a-unified-framework-for-extensive-form-game-abstraction-with-bounds.pdf).

Le 81 classi preflop possono restare exact. L'eventuale compressione postflop deve essere versionata, riproducibile e valutata nel gioco fisico. Se il certificatore calcola direttamente un upper bound sulla NashConv originale della policy astratta sollevata alle combo fisiche, quell'upper bound include già l'effetto dell'astrazione: non occorre sommare una seconda volta un generico «abstraction error».

## 6. Collo di bottiglia CPU: lavoro ripetuto verificato

Nella sonda fisica da 100.000 iterazioni, i timer cumulativi dell'intero `solve` individuano:

| Blocco | Secondi strumentati | Lettura |
|---|---:|---|
| Terminal payoff | 14,35 | Lo stesso deal completo viene rivalutato in molti rami di betting |
| Costruzione chiave | 8,69 | Include 8,21 s di canonicalizzazione dei semi |
| `apply_action` | 7,57 | Valida di nuovo le azioni legali già generate dalla traversata |
| Accesso/creazione/discount stato | 6,72 | Hash map sparse, allocazioni e aggiornamenti |
| `legal_actions` esplicita | 2,79 | Oltre alle chiamate interne ad `apply_action` |
| Campionamento dei deal | 0,084 | Non è il principale costo misurato |

Nella modalità memoryless, la chiave costa 27,72 s su 56,68 s, di cui 26,09 s nel calcolo dei bucket. La rappresentazione più compatta può dunque essere più lenta per visita, se la costruzione dei suoi indici resta nell'hot path.

### Un'ottimizzazione isolata misurata

Una copia diagnostica del solver memorizza il winner mask del deal completo già estratto e lo riusa nei diversi rami. Il settlement continua a usare pot, contributi, fold e rake dello stato corrente. Nessun nuovo numero casuale, sizing o payoff è introdotto.

| Modalità, 20.000 iterazioni | Mediana baseline | Mediana con cache | Fattore | Riduzione tempo |
|---|---:|---:|---:|---:|
| Bucket con memoria | 13,073 s | 10,833 s | 1,207× | 17,14% |
| Fisica lossless | 8,474 s | 5,616 s | 1,509× | 33,72% |

Tre repliche per riga; stessa fixture e stesso seed. Strategie root, EV, standard error, conteggio infoset e metrica di risposta risultano identici fra baseline, cache e sonda strumentata. Questo verifica l'output confrontato, non tutta la policy né la correttezza globale del solver. Non è stato eseguito un test di convergenza prima/dopo. La variante non è stata promossa in produzione.

Le altre modifiche plausibili, ancora senza speedup locale dimostrato, sono:

1. **Betting skeleton precompilato:** indici di nodo e transizioni già validate al posto di `legal_actions` e `apply_action` ripetute. Conservare history, arrotondamenti, incomplete raise, stack e rake.
2. **Indici di carte per deal e street:** riusare fino a sei osservazioni canoniche, due giocatori per tre street. L'indicizzazione incrementale può sostituire enumerazioni ripetute dei 24 semi.
3. **Bucket precalcolati o memoizzati fuori dalla visita:** budget e invalidazione espliciti per ruleset, street, feature e seed della partizione.
4. **Storage compatto per ID:** separare struttura pubblica e stato numerico, misurare SoA/arena/reserve; evitare hash e allocazioni evitabili. Compressione numerica solo dopo confronto con oracle.

Il lavoro di Waugh fornisce un'implementazione di indicizzazione per round adatta a studiare il secondo punto. Va adattata al mazzo Short Deck e verificata sui blocker e sull'ordine delle street; non si possono usare indici di Hold'em a 52 carte senza adattamento. [hand-isomorphism](https://github.com/kdub0/hand-isomorphism).

## 7. Certificazione: il costo attuale e il cambio di algoritmo necessario

La risposta appresa durante i run brevi **non è la best response exact**. Usa anch'essa uno spazio di policy limitato e training finito. Il valore atteso di una risposta legale fornisce un lower bound al massimo avversario; il suo valore stimato su campioni ha anche errore statistico e non è, da solo, un lower bound rigoroso. Il codice somma le due stime e applica `max(0, ...)`: tutti i run brevi qui riportano zero senza certificazione.

Esempio della sonda fisica a 100.000 iterazioni: le due risposte apprese hanno stime −0,15175 e −0,84193 ante; la somma negativa viene troncata a zero. Non significa che il profilo sia un equilibrio. Il campo `nashconv_certified=false` è corretto e va conservato. Anche LBR può scoprire debolezze e fornire evidenza inferiore dell'exploitability, non dimostrarne un upper bound piccolo. [Lisy e Bowling](https://arxiv.org/abs/1612.07547).

### Misura del percorso exact disponibile

La sonda del certificatore usa una blueprint preflop uniforme e una policy postflop uniforme, rappresentazione fisica. Nel primo task canonico seleziona la shape River con minore stack residuo; non pretende che sia la root minima dell'intero catalogo. Root ordinal 16.800, stack residuo 16.208 unità monetarie, cioè 1,6208 ante, history di sette azioni.

| Fase | Tempo | Risultato locale |
|---|---:|---|
| Costruzione tree, piano e catalogo | 3,9566 s | Root individuata |
| Valutazione del profilo | 13,0935 s | 465 righe per lato; valori +4,7974 / −4,7974 ante |
| Best response exact locale | 16,1053 s | 465 righe per lato; valori 19,1896 / 9,60915 ante |

Errore di ricomposizione riportato zero in entrambe le valutazioni. È un controllo interno, non una prova indipendente della BR. Questi valori sono condizionati a quella root: **non sono la NashConv dell'intero gioco**. [Codice della sonda](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/docs/research/preflop_12h_evidence_20260910/river_certificate_probe.cpp), [output completo](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/docs/research/preflop_12h_evidence_20260910/river_certificate_probe.stdout.log).

Il motivo del costo è visibile in `SampledRiverPolicyEvaluator`: per ogni mano del giocatore ricostruisce il vettore dell'avversario e traversa di nuovo il betting tree. Nei nodi avversari interroga la policy per ogni combo e azione. Ogni query della policy ripercorre l'action history, ricostruisce stati e azioni, canonicalizza e ricerca lo stato; per le policy astratte può anche ricostruire il bucket. Al terminale confronta nuovamente ogni coppia compatibile. [Evaluator](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/libs/preflop/src/hu_preflop_sampled_evaluation.cpp:234), [query](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/libs/preflop/src/hu_preflop_solver.cpp:1222).

Il lavoro su **Accelerating Best Response Calculation** descrive precisamente le alternative utili: traversata del public tree con vettori delle mani, riuso delle probabilità, terminali accelerati, isomorfismi e parallelismo. Occorre calcolare la policy una volta per osservazione e history, propagare vettori di reach e fare il massimo della BR per infoset legale. Non si può massimizzare separatamente per carta privata dell'avversario o board futuro non osservabile. [Johanson et al., 2011](https://www.cs.cmu.edu/~kwaugh/publications/johanson11.pdf).

Al River, dopo aver ordinato le mani per forza, somme prefisse e correzioni di card removal possono sostituire i confronti quadratici fra tutte le coppie, preservando esattamente le combo compatibili. È anche il tipo di kernel valorizzato dal public chance sampling. Il vantaggio deriva dal riuso fra mani e rami, non dal semplice spostamento del loop in otto thread.

Il certificatore globale esistente richiede coverage completa, evidenza di BR exact e soglia soddisfatta prima di impostare `certified`. È una buona barriera. L'infrastruttura di checkpoint consente ripresa, ma non rende economico ogni leaf. Non va rilassato quel gate per etichettare come certificata una risposta appresa. [Gate](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/libs/preflop/src/hu_preflop_decomposition.cpp:2230).

## 8. Dimensioni reali e lettura corretta delle proiezioni

La documentazione locale e il catalogo del gioco separano quantità spesso confuse:

| Quantità | Conteggio | Conseguenza |
|---|---:|---|
| Tree preflop | 58 nodi, 20 decisioni, 9 entry postflop | La parte pubblica preflop è piccola |
| Scheletro pubblico postflop | 30.324 nodi, 11.308 decisioni | Le size configurate hanno un costo significativo, ma la struttura è riusabile |
| Flop fisici / canonici | 7.140 / 573 | Le nove entry generano 5.157 task canonici |
| History pubbliche di board fino al River | 7.539.840 fisiche / 369.072 canoniche | Flop, Turn e River conservano la cronologia osservata |
| Root pubbliche River del catalogo | 366.488.496 | Non confondere con i due lati del resolver |
| Boundary per lato | 732.976.992 | Memorizzarle tutte richiederebbe 9.288.284.442.624 byte nel modello corrente |

Indipendentemente dal catalogo, ci sono `C(36,2)·C(34,2) = 353.430` coppie ordinate di mani private compatibili. Aggiungere un board finale non ordinato dà 71.172.319.680 assegnazioni. Nel gioco le street sono osservate in momenti diversi: distinguere il Flop dal Turn e dal River moltiplica per 20, portando a **1.423.446.393.600** assegnazioni. Non sono altrettanti infoset da allocare: il conteggio serve a mostrare perché un algoritmo che ripeta il lavoro per ogni assegnazione privata spreca molto rispetto a uno che fattorizza il public tree.

Il batching delle boundary a 64 MiB limita la memoria residente, ma lascia il lavoro totale. File-backed e memory mapping spostano il problema di capacità verso accessi e I/O; non fanno convergere in meno traversate.

La stima già presente nel repository usa 0,0026794 s, misurati su **una traversata del subgame River peggiore**, per ciascuna delle 366.488.496 root: 272,77 ore seriali, 34,10 ore con speedup ideale 8×, per una sola sweep. Questa proiezione spiega perché quel percorso non è una scelta convincente. **Non è un lower bound matematico né una misura dello sweep:** attribuisce a tutte le root il costo della peggiore. Non può dimostrare che ogni algoritmo exact sia impossibile entro 12 ore. [Misure storiche e perimetro](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/docs/HU_PREFLOP_CO40_BENCHMARK.md:422).

La nuova misura di 16,11 s riguarda un altro algoritmo: la BR della policy interrogata dal valutatore scalare, non la traversata del solver vettoriale usata dalla vecchia stima. I due tempi non vanno mescolati. Il divario motiva un confronto fra implementazioni sugli stessi ingressi e la rimozione delle query ripetute.

Per stimare seriamente il nuovo percorso occorre stratificare per entry, shape di betting, SPR, struttura del board e supporto dei range; misurare i costi dei gruppi; moltiplicarli per le rispettive molteplicità del catalogo. Cache condivise, riuso e parallelismo devono poi essere misurati su batch completi, non dedotti da un solo leaf.

## 9. Che cosa suggerisce davvero la letteratura MCCFR

| Fonte primaria | Risultato pertinente | Applicazione e limite in questo progetto |
|---|---|---|
| [Lanctot et al., 2009, MCCFR](https://www.cs.cmu.edu/~kwaugh/publications/nips09b.pdf) | Update campionati con correttezza in aspettativa e garanzie di regret nelle condizioni del metodo | Fondazione del sampler. Servono corretti pesi, supporto del sampling e ipotesi di recall; il nome MCCFR non certifica qualsiasi variante |
| [Johanson et al., 2012, Public Chance Sampling](https://poker.cs.ualberta.ca/publications/AAMAS12-pcs.pdf) | Campionare eventi pubblici e trattare congiuntamente le mani private permette riuso e terminali più efficienti | Candidato principale da confrontare con external sampling: più lavoro per iterazione può dare meno tempo per unità di errore. Non elimina da solo lo storage delle history pubbliche |
| [Schmid et al., 2019, variance reduction](https://arxiv.org/pdf/1809.03057) | Baseline come control variates riducono la varianza preservando l'aspettativa con la correzione appropriata | Ridurre il numero di campioni necessari, dopo aver corretto l'averaging. I grandi fattori sperimentali su giochi piccoli non sono promesse per HU CO40 |
| [Davis et al., 2020, low/zero-variance baselines](https://proceedings.mlr.press/v119/davis20a/davis20a.pdf) | Baseline predittive e Public Outcome Sampling; condizioni per annullare componenti della varianza | Studiare baseline per public state e tutte le mani. Il risultato zero-variance richiede condizioni sulla baseline/copertura, non si ottiene automaticamente da pochi rollout |
| [Burch et al., 2014, CFR-D](https://poker.cs.ualberta.ca/publications/aaai2014-cfrd.pdf) | Decomposizione con trattamento corretto delle boundary | Riduce stato simultaneo e consente subgame solving; non cancella il costo dei subgame né autorizza EV scalari indipendenti dai range |
| [Brown e Sandholm, 2015, regret-based pruning](https://papers.nips.cc/paper/5910-regret-based-pruning-in-extensive-form-games.pdf) | Condizioni per non attraversare temporaneamente rami senza compromettere il regret | È utile dopo avere update validati. «Regret negativo, quindi salta» non trasferisce automaticamente quelle condizioni al sampler con discount |

Una baseline campionata deve aggiungere il valore di controllo e correggere il residuo per la probabilità di sampling, ad esempio `b(a) + 1{a campionata}/q(a) · (v(a)−b(a))` nel caso elementare pertinente. Sostituire semplicemente una foglia con una previsione neurale o un'EV media introduce bias; la certificazione originale dovrebbe comunque coprirne gli effetti.

PCS e POS non sono sinonimi: il primo campiona la chance pubblica; il secondo campiona esiti pubblici includendo azioni secondo il proprio schema e percorre gli stati privati compatibili. Non conviene scegliere per acronimo. Il confronto corretto è `upper bound/errore versus wall time`, a parità di gioco e limite RAM, includendo costo e memoria delle baseline.

Pluribus è una prova importante di efficacia pratica di MCCFR con astrazione, pruning e ingegnerizzazione. Il paper riporta per la blueprint otto giorni su 64 core fisici e meno di 512 GB di memoria. Sono risorse e obiettivi diversi dal desktop presente; inoltre la forza dimostrata nel multiway non equivale a una certificazione HU della NashConv del gioco completo. [Paper](https://noambrown.github.io/papers/19-Science-Superhuman.pdf), [supplemento tecnico](https://noambrown.github.io/papers/19-Science-Superhuman_Supp.pdf).

La ricerca recente non giustifica promettere ordini di grandezza senza una prova locale. Per esempio, il preprint di luglio 2026 sul correlated chance sampling riporta esperimenti su Kuhn e Leduc: è una variante da considerare in seguito, non un'evidenza che chiuda il vincolo CO40. [Correlated Chance Sampling for MCCFR](https://arxiv.org/abs/2607.27035).

### Parallelizzazione e GPU

Il sampler attuale è seriale. Va misurato lo scaling reale a 1/2/4/8 thread, includendo convergenza, RAM duplicata e costo delle riduzioni. Worker con delta locali e riduzioni definite sono un punto di partenza verificabile; aggiornamenti concorrenti non sincronizzati su tabelle o floating point non lo sono. Il numero di iterazioni a parità di secondo può aumentare mentre l'efficacia di ciascun update diminuisce.

Le misure indicano prima lavoro ridondante e accessi irregolari, non un kernel GPU già pronto. La priorità è regolarizzare public tree, vettori di mani e terminali sulla CPU. GPU e cloud non fanno parte della soluzione proposta per questo vincolo locale.

## 10. GitHub: componenti utili, non prove di fattibilità

Sono stati letti sorgenti o documentazione dei progetti seguenti. Non sono stati eseguiti benchmark di questi repository né importato loro codice nel motore.

| Repository e revisione | Evidenza letta | Uso concreto / limite |
|---|---|---|
| [OpenSpiel, external sampling C++](https://github.com/google-deepmind/open_spiel/blob/master/open_spiel/algorithms/external_sampling_mccfr.cc) e Python, consultati il 10/09/2026 | Implementazione e commenti dell'averaging HU; alternativa full averaging | Oracle concettuale e test indipendenti su giochi piccoli. Il framework generico non è un motore Short Deck ottimizzato |
| [kdub0/hand-isomorphism](https://github.com/kdub0/hand-isomorphism), consultato il 10/09/2026 | Indici incrementali delle osservazioni per round | Studiare indici senza canonicalizzazione ripetuta. Adattamento a 36 carte e test obbligatori |
| [DecisionHoldem, `a9ea9a5`](https://github.com/AI-Decision/DecisionHoldem/blob/a9ea9a545c7bb24f4e657bc6d1f75af66aa1bb51/PokerAI/BlueprintMCCFR.h) | Struttura precompilata, cluster e pruning; [coordinamento](https://github.com/AI-Decision/DecisionHoldem/blob/a9ea9a545c7bb24f4e657bc6d1f75af66aa1bb51/PokerAI/Multi_Blureprint.h) configurato per molti thread | Esempio di separazione fra betting tree e stato. Una funzione chiamata MCCFR enumera anche azioni avversarie: non identificarne la variante dal nome. Nessuna prova letta di certificazione completa in 12 ore su questo hardware |
| [antsolver, `e5d3b7c`](https://github.com/SlappyPenguin/antsolver/blob/e5d3b7cf9116167eb3eb8b4e9e038407c8850f33/src/blueprint/trainer.cpp) | Deal/winner/bucket precalcolati, children indicizzati, averaging nel passaggio avversario | Conferma che spostare lavoro fuori dalla visita è una scelta concreta. Il sorgente dichiara race condition: non adottare quel parallelismo come riferimento di correttezza |
| [ozzi7/Poker-MCCFRM](https://github.com/ozzi7/Poker-MCCFRM), README | Progetto C# dismesso, astrazione imperfect recall e raccomandazione di RAM elevata | Documentazione indicativa delle risorse; nessun codice eseguito e nessun upper bound full-game verificato |

L'evidenza comune è architetturale: separare la struttura del gioco dai numeri aggiornati, riusare gli indici e ridurre la varianza. Non emerge un'implementazione pubblica già verificata che soddisfi insieme Short Deck HU CO40, questo PC, meno di 12 ore e il certificato globale richiesto.

## 11. Una certificazione rigorosa deve necessariamente enumerare tutto?

**Non necessariamente.** È possibile coprire matematicamente rami non espansi attraverso limiti validi. Questo è diverso dal campionarli e assumere che il resto assomigli ai campioni.

Il lavoro sui piccoli certificati di equilibrio costruisce pseudogiochi con intervalli di payoff alle foglie troncate. Un certificato può essere molto più piccolo del gioco se gli intervalli bastano a escludere deviazioni utili. Non garantisce che un certificato piccolo esista per ogni gioco: il caso peggiore può richiedere l'espansione sostanziale dell'albero. [Zhang e Sandholm, 2020](https://proceedings.neurips.cc/paper/2020/file/4fbe073f17f161810fdf3dab1307b30f-Paper.pdf).

Per una policy congelata nel nostro gioco zero-sum, se `U0` e `U1` sono upper bound validi dei due valori di BR, allora `NC ≤ U0+U1`. Più in generale si possono sottrarre lower bound dei valori del profilo. Le bound devono preservare infoset e chance; non consentire al deviatore di conoscere carte nascoste. Espandendo i rami che contribuiscono maggiormente all'incertezza si può tentare di chiudere il gap senza risolvere ogni subgame separatamente.

In questo schema ogni ramo è coperto da valutazione esatta o intervallo conservativo. Un ramo raro nel self-play non è automaticamente trascurabile: il deviatore può cambiare la propria reach. Si può saltare un ramo con reach controfattuale avversaria/chance nulla, oppure usarne un limite pesato dimostrato. Pruning basato soltanto sulla frequenza di visita della policy corrente sarebbe scorretto per la BR.

Le tecniche Cert-MCCFR per giochi black-box e i risultati sugli stimatori di regret mostrano anche vie con garanzie probabilistiche. Richiedono confidence sequence/bound uniformi appropriati, non il solo errore standard dell'EV. Gli esperimenti pubblicati non costituiscono una validazione del nostro sampler con DCFR e imperfect recall. La richiesta attuale è trattata nel senso deterministico conservativo; passare a un certificato con probabilità `1−δ` richiederebbe dichiarare e concordare quel contratto. [Zhang e Sandholm, 2021](https://arxiv.org/pdf/2009.07384), [Farina, Kroer e Sandholm, 2020](https://proceedings.mlr.press/v119/farina20a/farina20a.pdf).

Questo percorso richiederebbe una **nuova versione esplicita del certificato**, con bound, ipotesi, policy, gioco e margine numerico. Non può essere inserito nel campo `exact_best_response=true` dell'attuale formato senza distinguerne il significato. L'evaluator exact su giochi ridotti rimane l'oracle: l'intervallo deve sempre contenerne il risultato.

## 12. Piano di decisione per le 12 ore

Un budget di progetto possibile, **non una previsione di prestazione**, è:

| Fase | Budget |
|---|---:|
| Preparazione specifica e cache | 0,5 ore |
| Training MCCFR | 8 ore |
| Certificazione globale finale | 3 ore |
| Esportazione, checkpoint e margine | 0,5 ore |
| Totale massimo | 12 ore |

Se si mantenesse una valutazione separata per tutte le 366.488.496 root River, tre ore richiederebbero **33.934 root pubbliche al secondo aggregate**, includendo i due lati. Persino con otto worker ideali sono 0,236 ms per root per worker. Dedicando tutte le 12 ore alla sola certificazione diventerebbero 0,943 ms. Questi sono requisiti aritmetici del particolare schema, non throughput ottenuti né limiti universali per algoritmi che condividono lavoro fra root.

Il certificatore scalare misurato è lontano da tale ordine di grandezza. Nessuna ottimizzazione locale della cache degli showdown del trainer può compensarlo. La certificazione deve quindi essere progettata e misurata **prima di investire in un run lungo**.

### Cinque gate, in ordine

1. **Correttezza del sampler.** Test indipendente dell'aspettativa di regret e averaging sullo stesso nucleo usato dal preflop, poi Kuhn/Leduc e gioco Short Deck enumerabile. Coprire reach zero/frazionaria, alternanza, discount, strategia media, campionamento e ripresa. Correggere averaging e separare i seed di partizione/training. Non basta che il motore didattico separato passi già Kuhn.
2. **Costo della certificazione.** Prototipo di public-tree BR vettoriale con policy preindicizzata e showdown con blocker. Confronto exact con l'evaluator scalare su ingressi identici, inclusi range asimmetrici. Profilazione stratificata e batch completi per proiettare lavoro/RAM. Verificare anche se bound conservativi restringono davvero il gap su giochi ridotti.
3. **Rappresentazione e tempo per errore.** Confrontare external sampling, PCS/POS e baseline a varianza ridotta, con la stessa partizione fissata e più seed di training. Misurare visite, miss della policy, byte per stato, errore contro oracle e upper bound disponibile. Mantenere 81 classi preflop exact; eventuale astrazione postflop esplicita. Non scegliere il candidato soltanto per iterazioni/s.
4. **Ingegnerizzazione e scaling.** Integrare solo cambiamenti sostenuti da misure: cache deal, skeleton, indici, storage e riduzioni parallele. Test a 1/2/4/8 thread, precisione e checkpoint completo di regret, somme, RNG e discount. L'export della sola strategia media non riprende il training; il checkpoint BR già esistente risolve un problema diverso.
5. **Qualifica finale.** Congelare regole, partizione, algoritmo e policy; eseguire il budget completo, includere serializzazione e certificato nel tempo, verificare `NC_upper/40 ≤ 0,01`, RAM e integrità. Se il bound resta largo alla scadenza, esito **FAIL/NOT_CERTIFIED**, anche se root EV e frequenze assomigliano al target.

I gate non autorizzano di per sé una modifica di produzione: costituiscono l'ordine tecnico raccomandato dalla presente analisi. Per il codice futuro, i punti principali sono `hu_preflop_solver.cpp`, l'evaluator della policy e il provider della BR nel modulo di decomposizione; il core postflop validato va riusato dove il contratto coincide, senza cambiare le sue semantiche per questa fixture.

Non propongo di allungare il run exact sparse corrente, ridurre di nascosto le size, aggiungere rake, assegnare EV postflop fisse o riusare una soluzione soltanto perché pot e stack coincidono. Struttura e kernel sono riusabili; policy e CFV dipendono anche da history e range. Una decomposizione corretta deve conservare tali dipendenze.

## 13. Validazione svolta e limiti residui

| Verifica | Esito e portata |
|---|---|
| Preflight fixture esterna | PASS: 81 classi, 630 combo, cinque azioni, rake disabilitata; fingerprint `fnv1a64:bcd91f8001ef744f` |
| Compilazione sonde Release | Completata; copie diagnostiche e librerie locali identificate nel manifest |
| 16 processi diagnostici | Completati con exit code 0; risultati conservati |
| Confronto cache/baseline/strumentazione | Output root ed EV confrontati identici a seed fissato; non equivalenza dell'intera policy |
| Controesempio averaging | Asserzioni razionali PASS; dimostra il difetto dell'accumulatore atteso |
| Probe River | Completato; 465 righe per lato, ricomposizione interna zero; nessuna certificazione globale |
| Suite completa CTest | Non rieseguita per questa analisi senza modifiche production; i PASS storici nel repository non sono presentati come nuovi risultati |
| Hardware counters | Cache miss, branch miss, bandwidth e contention non misurati; i timer sono strumentazione di funzioni |
| Run completo di 12 ore / upper bound globale | Non eseguito / non disponibile |

Resta non determinato il miglior rapporto tra dimensione dell'astrazione, varianza del sampling e gap certificabile su CO40. Mancano inoltre il costo misurato del nuovo certificatore, lo scaling reale e un bound numerico formalizzato. Sono le incognite che impediscono una promessa responsabile delle 12 ore.

L'analisi individua un difetto matematico concreto, una causa strutturale misurata e lavoro CPU eliminabile, e distingue questi risultati dal costo della certificazione. **La prima attività successiva è correggere e validare l'averaging del sampler con un test indipendente della sua aspettativa.** Il primo esperimento prestazionale dopo quel gate deve riguardare la BR vettoriale, perché decide se l'obiettivo completo può rientrare nel budget.
