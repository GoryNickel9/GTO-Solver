# Architettura CPU e RAM per avvicinare il benchmark preflop HU 40a in meno di due ore

## 1. Decisione proposta

Il percorso più promettente è una **blueprint astratta dell'intero gioco, conservata e aggiornata in RAM**, con preflop exact sulle 81 classi, rappresentazione postflop a dimensione limitata, evaluator tabellare exact e struttura delle puntate precompilata. L'algoritmo di aggiornamento resta una scelta sperimentale: i primi candidati sono external sampling con averaging corretto e Linear MCCFR, seguito da public chance sampling; DCFR/CFR+ su un gioco astratto sufficientemente piccolo costituisce un'alternativa reale.

La priorità è ottenere **range ed EV comparabili al riferimento entro 7.200 secondi sul PC locale**. La certificazione rigorosa della NashConv dell'intero gioco resta distinta. Il costo proiettato di 34,10 ore per una sweep di subgame River separati non è il costo obbligatorio di questo nuovo percorso.

La diagnosi del codice aggiunge cinque elementi rispetto alla precedente analisi:

1. L'evaluator a sette carte enumera 21 mani da cinque carte a ogni chiamata. Una tabella exact completa per il mazzo Short Deck richiede circa **31,84 MiB a 32 bit per valore**: è una delle opportunità più concrete per scambiare RAM con lavoro CPU.
2. I bucket correnti combinano categoria e una stima di equity molto rumorosa. Con otto campioni, i 32 intervalli River ricevono al massimo **17 valori diversi di equity**. Il numero nominale di intervalli non misura la qualità dell'astrazione.
3. La chiave astratta non conserva un identificatore separato del board o della sua struttura: accorpa anche situazioni pubbliche. La variante con memoria moltiplica le combinazioni delle street; quella memoryless perde informazione utile senza sostituirla con feature migliori.
4. Le cache dei bucket sono indicizzate da osservazioni fisiche canoniche e crescono senza budget. Una tabella dei regret piccola non garantisce quindi RAM limitata lungo un run.
5. Lo schema di averaging problematico è presente anche nel MCCFR del motore `FiniteGame`. Quest'ultimo non è un oracle indipendente per verificare quella formula.

Non ci sono dati pubblici sufficienti per identificare l'algoritmo del Monker privato. L'ipotesi tecnica più plausibile è una combinazione di **forte riduzione dello spazio strategico, calcoli precalcolati e aggiornamenti CFR efficienti**. La specifica variante di CFR, il sampling, i cluster e i criteri di arresto restano sconosciuti. Non serve ricostruirli esattamente per progettare un percorso competitivo.

## 2. Obiettivo misurabile e confine del confronto

Il gioco locale resta quello della fixture `hu_preflop_co40_game_v1.json`: 36 carte; stack effettivo 40 ante; CO con 1 ante; BTN con 1 ante più 1 button blind; call incrementale del CO pari a 1 ante; rake zero; colore sopra full house; A-6-7-8-9; root fold/call/raise-to 6/raise-to 10/all-in; continuazioni preflop dichiarate; postflop locale 33%, 66%, 120% e all-in, fino a esaurimento naturale dello stack. L'equivalenza dell'albero postflop Monker è in attesa di conferma. L'eccezione configurata per il raise incompleto è parte del gioco locale, non un parametro da cambiare per migliorare il risultato. [Fixture](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/benchmarks/fixtures/hu_preflop_co40_game_v1.json).

L'output richiesto è la strategia **media** per ogni classe, la sua EV root misurata nel gioco fisico e una soluzione consultabile. Non basta una matrice inserita manualmente, una policy addestrata a imitare le 405 frequenze del target, o un EV calcolato soltanto all'interno dell'astrazione.

| Metrica | Riferimento o soglia attuale |
|---|---:|
| Tempo totale specifico del caso | < 120 minuti |
| EV CO target / tolleranza | −0,30 ante / errore assoluto ≤ 0,05 ante |
| TV media per classe, pesata sulle combo | ≤ 2 punti percentuali |
| Errore massimo delle cinque frequenze root aggregate | ≤ 1 punto percentuale |
| P95 della TV per classe, pesato sulle combo | ≤ 5 punti percentuali |
| MAE action/class pesato | ≤ 1 punto percentuale |
| NashConv globale certificata | Obiettivo separato; non dichiararla disponibile |

Con cinque azioni, le formule del comparatore rendono `TV_media = 2,5 × MAE`. Pertanto il gate TV ≤ 2 implica già MAE ≤ 0,8. Sono due modi di riportare la stessa somma degli scarti, non due misure indipendenti. Gli altri gate controllano concentrazione degli errori e margini root. [Comparatore](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/benchmarks/hu_preflop_reference.cpp:299).

Il comparatore attuale include la NashConv certificata nel suo `qualifies`. Per il nuovo obiettivo serve in seguito uno stato separato, ad esempio `RANGE_EV_MATCH`, che non trasformi `nashconv_certified=false` in una certificazione. Questo rapporto non cambia fixture, checker o soglie.

Il riferimento arrotonda le frequenze a percentuali intere e alcune righe sommano a 101%; il confronto normalizza ogni riga, preservando l'originale. Strategie differenti possono avere EV quasi identica quando due azioni sono quasi indifferenti, oppure appartenere a equilibri diversi. Per diagnosticare gli scarti occorrono anche EV delle azioni e regret delle deviazioni; la sola distanza fra frequenze non identifica quale solver sia corretto.

## 3. Che cosa si può inferire sul Monker privato

Non sono disponibili CPU, RAM, tempo preciso, impostazioni dei bucket, EV per azione o risposte BTN del run privato. Il precedente dato «meno di 12 ore» non dimostra un run inferiore a due ore sul nostro hardware. Due ore è il nuovo obiettivo progettuale.

La documentazione pubblica di Monker descrive un solver con betting tree configurabile e astrazione delle mani in bucket. La pagina sull'astrazione mostra che diverse impostazioni possono ridurre molto il gioco e modificare i range ottenuti; la guida propone 15–30 strength bucket per street. Queste informazioni riguardano il prodotto pubblico, non identificano il motore privato Short Deck.[^1][^2]

| Ipotesi sul motore privato | Forza dell'evidenza | Conseguenza progettuale |
|---|---|---|
| Compressione strategica postflop mediante astrazione | Plausibile; documentata nel prodotto pubblico | È il primo meccanismo da studiare per ridurre il lavoro totale |
| Tabelle e strutture residenti in RAM riutilizzate fra iterazioni | Plausibile per un solver desktop di questa classe; non verificato sul privato | Il nostro motore dovrebbe evitare ricostruzioni per visita |
| Famiglia CFR con sampling o traversate aggregate | Ipotesi coerente con la letteratura, non identificazione | Confrontare varianti sullo stesso gioco astratto |
| External sampling, PCS, CFR+, DCFR o schedule proprietario specifico | Non determinabile dai range disponibili | Non fissare il progetto a una variante attribuita a Monker |
| Reti neurali, database di soluzioni già pronte, hardware particolare | Nessuna evidenza sul run | Non usarli per spiegare il tempo senza dati |

La possibilità di aprire tutti i rami nell'interfaccia non dimostra che ogni combinazione fisica abbia regret indipendenti o che ogni subgame sia stato risolto separatamente. Una policy astratta può essere applicata a qualsiasi mano fisica mediante la propria mappa di bucket.

Anche conoscere esattamente la matrice root non permetterebbe di ricostruire l'algoritmo: diverse procedure numeriche possono convergere a strategie simili, mentre astrazioni diverse possono alterare il risultato usando lo stesso CFR. La domanda operativa è quale rappresentazione conservi abbastanza informazione da riprodurre il comportamento preflop entro il budget.

## 4. Percorsi del codice e cosa riutilizzare

| Componente | Comportamento verificato | Valutazione |
|---|---|---|
| `hu_preflop.cpp` | Costruisce il trunk e analizza lo scheletro pubblico postflop | Riutilizzare legalità, unità, fingerprint e conteggi; l'analyzer è oggi un contatore memoizzato, non il layout del trainer |
| `hu_preflop_solver.cpp` | Sampler seriale, tabella hash sparse, DCFR 1.5/0/3 incorporato, bucket online | Contiene il percorso da rivedere per correttezza, riuso e dimensione dello stato |
| `hu_preflop_sampled_evaluation.cpp` | Valutazione River fisica scalare di una policy esportata | Oracle locale e diagnostica; non obbligatorio nel percorso rapido dei range |
| `hu_preflop_decomposition.cpp` | Cataloghi, reach, boundary, BR, checkpoint e tabella all-in | Riutilizzare i contratti e le tabelle pertinenti; evitare la sweep esaustiva come training principale |
| `postflop_solver.cpp` | Kernel vettoriali per mani, somme per rank e rimozione dei blocker | Riutilizzare concetti e kernel con contratti compatibili, non costruire un solver completo per ogni River |

Il sampler preflop non usa automaticamente le ottimizzazioni del solver postflop. Il fatto che il secondo disponga di kernel vettoriali non rende vettoriale il primo. Per esempio, il kernel postflop aggrega reach per rank e per carta prima di produrre i valori delle mani; il sampler richiama invece il valutatore di showdown per il deal estratto nei diversi terminali. [Kernel postflop](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/libs/postflop/src/postflop_solver.cpp:13117), [terminale del sampler](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/libs/preflop/src/hu_preflop_solver.cpp:683).

Lo scheletro conta 30.324 nodi rappresentati, 11.308 decisioni e 29.112 archi d'azione. Il suo analyzer usa memoizzazione per calcolare i conteggi, ma il trainer continua a generare `legal_actions` e a usare `apply_action`, che rigenera le azioni per validarle. Una struttura precompilata deve conservare distinti i nodi con history strategiche diverse anche quando pot e stack coincidono: si possono condividere transizioni meccaniche, non presumere uguali le policy o i range.

### Correttezza dell'averaging: due implementazioni condividono il problema

Il sampler accumula `reach[actor] × strategy` durante entrambi i passaggi dei giocatori. Nell'external sampling parte della reach è già incorporata nella probabilità di visita. Il controesempio razionale conservato nell'analisi precedente produce pesi attesi `x(x+y)` invece di `x`, alterando la media. [Sampler preflop](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/libs/preflop/src/hu_preflop_solver.cpp:740), [controesempio](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/docs/research/preflop_12h_evidence_20260910/averaging_counterexample.json).

La stessa formula compare in `traverse_external_sampling` del motore `FiniteGame`, riga 354. Il test Kuhn campionato controlla una NashConv inferiore a 0,12 dopo 100.000 iterazioni; non verifica l'aspettativa dell'accumulatore. Non può escludere questo difetto, e confrontare i due sampler fra loro non darebbe indipendenza. [Secondo sampler](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/libs/solver/src/solver.cpp:327), [test](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/tests/phase5_tests.cpp:132).

La verifica necessaria è un piccolo gioco con più decisioni dello stesso giocatore, probabilità di reach variabili e enumerazione esatta degli esiti del sampling. L'averaging deve poi essere definito insieme ad alternanza, peso temporale e batching. OpenSpiel offre un riferimento indipendente per la convenzione HU, ma il test deve essere derivato dalla formula, non dalla somiglianza fra implementazioni.[^3]

Il discount negativo `0,5^Δt` su un infoset non visitato e il prefisso lungo O(numero di iterazioni) sono altri punti da riesaminare. Non è ancora misurato quanto il discount peggiori la qualità; la sua presenza non va scambiata per una spiegazione già dimostrata. La scelta 1.5/0/3 del preflop può cambiare senza modificare il contratto del motore postflop.

## 5. La prima opportunità CPU/RAM è un evaluator exact tabellare

`evaluate_seven_exact` rimuove a turno due delle sette carte e valuta tutti i 21 sottoinsiemi. `evaluate_seven_batch` ripete lo stesso procedimento per ogni mano: il nome batch non introduce qui una valutazione vettoriale o una tabella. [Evaluator](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/libs/equity/src/evaluator.cpp:57).

Un bucket con otto campioni valuta hero e opponent a sette carte per ogni campione: **16 valutazioni a sette carte, quindi 336 valutazioni a cinque carte**, oltre al calcolo della categoria visibile. Quel lavoro si ripete per molte osservazioni nuove. Il profilo precedente misura infatti 26,09 secondi nel calcolo dei bucket su 56,68 secondi complessivi della sonda memoryless da 100.000 iterazioni.

Quel blocco rappresenta circa il 46% del tempo della sonda strumentata. Eliminandone idealmente tutto il costo e lasciando invariato il resto, lo speedup sarebbe soltanto circa 1,85×. La tabella dell'evaluator può aiutare anche altri blocchi, ma questo conto chiarisce perché l'ottimizzazione dei bucket, da sola, non risolve il problema della qualità delle continuazioni.

Per un mazzo fisso di 36 carte il numero di insiemi non ordinati è piccolo abbastanza da considerare una tabella completa:

| Carte | Combinazioni | Payload di un valore a 32 bit |
|---|---:|---:|
| 5 | 376.992 | 1.507.968 byte |
| 6 | 1.947.792 | 7.791.168 byte |
| 7 | 8.347.680 | 33.390.720 byte, **31,84 MiB** |
| Tutte e tre le tabelle | 10.672.464 | **40,71 MiB** |

Sono conteggi combinatori, non dimensioni di file già prodotti o benchmark eseguiti. La categoria e i cinque kicker correnti possono essere impacchettati senza perdita in un intero a 32 bit mantenendo il confronto lessicografico. Un rank ordinale validato potrebbe usare 16 bit e dimezzare il payload; la versione a 32 bit basta già a rendere trascurabile la capacità richiesta rispetto alla RAM del PC.

L'indice può essere una numerazione combinatoria delle carte ordinate, con coefficienti binomiali precalcolati. Il lookup non richiede canonicalizzare 24 permutazioni dei semi. La tabella resta specifica di mazzo e ranking e deve essere accompagnata da versione, checksum e confronto esaustivo con l'evaluator oracle. La costruzione ingenua di quella a sette carte richiederebbe 175.301.280 valutazioni di sottoinsiemi da cinque; pretabellare prima le cinque carte cambia quel costo. Il tempo di preparazione non è stato misurato.

Questa tabella **non è card abstraction**: conserva l'esito esatto di ogni showdown. Per riusare la stessa tabella in giochi differenti bastano lo stesso mazzo e ranking; stack, sizings e range non cambiano il valore di una mano. Costruzione e caricamento vanno comunque contabilizzati distinguendo un primo avvio da un run con tabella già disponibile.

Resta utile anche la cache del vincitore per deal, già misurata nella precedente sonda con un taglio del tempo del 33,72% nella modalità fisica. Le due ottimizzazioni eliminano lavoro in punti diversi, ma **i loro speedup non si moltiplicano senza misura**. Il nuovo limite potrebbe diventare accesso alla memoria, indicizzazione o visita dei nodi.

### Riuso del board e forza River esatta

Su un River ci sono 465 mani private compatibili con il board e, fissata la mano hero, 406 mani avversarie compatibili. Valutare una volta le 465 mani e ordinarle permette di ottenere forza relativa e correzioni dei blocker tramite somme aggregate. Si può così evitare di stimare la forza River contro un range uniforme con soli otto showdown.

Per il solo ranking finale si può anche ignorare l'ordine delle street: i 376.992 board finali si raggruppano in 19.998 classi sotto le permutazioni dei semi. Una tabella di rank a 16 bit per 465 mani per board avrebbe 18.598.140 byte di payload, circa 17,74 MiB, più indici e mapping. Il conteggio delle classi deriva da Burnside: `(376.992 + 6×16.560 + 8×450)/24`. Questa è un'alternativa alla tabella diretta, non un ulteriore speedup garantito.

Il riuso dei rank non autorizza a confondere le history strategiche: una carta uscita al Turn e una al River hanno effetti diversi sulle decisioni già prese. I rank di showdown possono condividere dati; le chiavi della policy devono conservare la cronologia scelta dall'astrazione.

## 6. Perché l'astrazione corrente non è un buon punto d'arrivo

### Quantizzazione di rumore

Ogni campione di equity aggiunge 0, 0,5 o 1. Con otto campioni, la media può assumere al massimo 17 valori, separati da 6,25 punti percentuali. I 32 intervalli River larghi 3,125 punti non aggiungono informazione: molti indici non possono essere raggiunti. Con campioni indipendenti limitati a [0,1], l'errore standard massimo della media a otto campioni è circa 17,68 punti percentuali, un'indicazione della scala del rumore, non un errore misurato di ogni bucket.

Il seed deterministico rende riproducibile questa assegnazione rumorosa. Non la rende precisa. Aumentare i campioni migliorerebbe la feature, ma il costo cresce proprio nel blocco già dominante; serve un diverso modo di calcolarla e riusarla. [Funzione dei bucket](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/libs/preflop/src/hu_preflop_solver.cpp:394).

### Perdita della struttura pubblica

La history astratta codifica entry, puntate e avanzamenti di street, senza includere le carte pubbliche effettive. In modalità astratta `physical_cards` rimane al valore sentinella. Due osservazioni su board diversi, con stessa history e stessi bucket, condividono quindi la strategia; nella variante memoryless condividono anche senza ricordare la classe preflop. [Chiave](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/libs/preflop/src/hu_preflop_solver.cpp:615).

Questo non introduce carte illegali: il deal fisico continua a rispettare card removal. Introduce invece **perdita di informazione strategica**. Categoria ed equity media non distinguono bene board paired, monotone, connessi, nut advantage, blocker e distribuzione della forza avversaria. Il problema è la qualità del raggruppamento, non il fatto che raggruppi board in assoluto.

### Memoria delle street e crescita delle cache

La variante con memoria trattiene classe preflop e storia dei bucket. La combinazione di etichette Flop/Turn/River moltiplica lo spazio; i precedenti istogrammi mostrano che gran parte degli stati tardivi riceve una sola visita. La memoryless evita il prodotto, ma ricompra capacità rinunciando a informazioni. Serve una selezione migliore di ciò che si conserva, non soltanto un booleano memoryless.

`bucket_cache_[player][street]` aggiunge ogni osservazione fisica canonica incontrata e non ha limite o eviction. Anche con pochi cluster, il cache key appartiene allo spazio grande. Un run lungo può dunque consumare memoria per milioni di osservazioni che non ricompaiono. Un disegno a RAM limitata deve coprire **regret, somme, mapping, cache e scratch**, non soltanto gli infoset.

Il seed di training alimenta inoltre la definizione dei bucket. Va separato dal seed della partizione e da quello di valutazione: altrimenti una prova multi-seed cambia insieme sampling e astrazione, rendendo difficile attribuire gli scarti.

## 7. Quali astrazioni conviene confrontare

La letteratura offre motivi concreti per superare l'equity scalare. Johanson e colleghi confrontano astrazioni basate su distribuzioni e su aspettative, descrivono l'OCHS al River e mostrano vantaggi empirici della memoria imperfetta a parità di risorse. Riportano anche patologie: raffinare un'astrazione non garantisce una policy migliore nel gioco originale. Sono risultati su altri giochi di poker, non una misura dello Short Deck corrente.[^4] Il lavoro di Ganzfried e Sandholm tratta esplicitamente l'evoluzione della forza e astrazioni potential-aware con memoria imperfetta.[^5]

La seguente selezione è una proposta per il nostro motore. Le dimensioni finali vanno scelte misurando il compromesso fra errore e tempo.

| Metodo | Informazione conservata | Impiego proposto e limite |
|---|---|---|
| Isomorfismo esatto dei semi | Equivalenze reali sotto permutazione dei semi | Fondamenta; non elimina da solo l'esplosione del gioco |
| Equity media / E[HS²] | Livello medio e un momento della distribuzione | Baseline economica; separazione insufficiente di molti draw e blocker |
| Istogrammi della forza futura | Probabilità di terminare in diverse fasce di forza | Primo candidato Flop/Turn; confronto con distanza di Wasserstein/EMD e clustering deterministico |
| Transizioni fra fasce di forza | Come e quando una mano migliora o peggiora | Candidato più ricco per distinguere mani fatte, draw e redraw; costo di preparazione maggiore |
| OCHS e feature dei blocker | Vettore di equity contro gruppi avversari, anziché un solo range uniforme | Primo candidato River; costo e partizioni specifici dello Short Deck |
| Cluster pubblici più bucket privati | Texture del board e forza della mano condizionata alla texture | Evita fusioni pubbliche troppo grossolane; va limitato il prodotto delle due dimensioni |
| Memoria imperfetta controllata | Informazioni correnti più un riassunto selettivo del passato | Spende la RAM su distinzioni utili; nessuna garanzia generale di CFR nel gioco originale |
| Raffinamento adattivo | Suddivisione dei gruppi con valori o azioni incompatibili | Fase successiva; trasferire regret e media fra partizioni richiede un contratto esplicito |

Per la prima candidata manterrei le 81 classi preflop e tutta la history delle puntate. Nel postflop userei cluster distinti per street, ottenuti da feature congiunte di mano e board: distribuzione dei runout al Flop/Turn, forza contro gruppi avversari al River, texture e blocker rilevanti. Il numero di etichette deve comprendere anche il riassunto di memoria eventualmente conservato. Separare cluster pubblici e privati è un'alternativa da confrontare, non un obbligo: una feature congiunta sufficientemente ricca può rappresentare entrambi.

Gli istogrammi devono descrivere la distribuzione delle carte ancora sconosciute, condizionata alle sole carte visibili. Non devono usare il runout già estratto nel deal di training come se fosse osservabile dal giocatore. Analogamente, un bucket non può dipendere dalle carte effettive dell'avversario.

Le feature OCHS dovrebbero partire da partizioni avversarie stabili, dichiarate e indipendenti dalla matrice target. Ricalcolare i cluster contro il range corrente dopo ogni aggiornamento cambierebbe il significato degli infoset durante il training. Una successiva fase range-aware è possibile, ma deve versionare la partizione e verificare come trasferire lo stato.

Il generatore iniziale può campionare osservazioni fisiche per costruire i centroidi. Non serve catalogare preventivamente ogni osservazione possibile. Occorre però contabilizzare questo costo, rendere deterministica l'assegnazione di un'osservazione mai vista e limitare la cache delle feature. Una cache di board con rank e statistiche riutilizzabili è più controllabile di una mappa illimitata per ogni coppia mano/street incontrata.

### Card removal e probabilità congiunte

Il primo trainer astratto dovrebbe continuare a estrarre **deal fisici legali**, traducendo poi l'osservazione di ciascun giocatore nel relativo bucket. Questo conserva la distribuzione congiunta delle carte nel sampling e permette di usare payoff fisici esatti. La perdita introdotta è la condivisione della strategia fra osservazioni diverse.

Un motore che opera direttamente sui bucket richiede invece probabilità congiunte e transizioni compatibili con board, history e blocker. Il prodotto delle due frequenze marginali dei bucket non basta. Né basta scegliere una mano rappresentativa per bucket: si perderebbero correlazioni decisive. Questo lavoro aggiuntivo distingue il candidato full-traversal dalla variante MCCFR su deal fisici.

Per raffinare una partizione sceglierei gruppi con grande dispersione dei valori delle azioni o forte disaccordo fra le azioni preferite dalle mani. La priorità dovrebbe considerare reach counterfactual ed esplorazione, oltre alla reach self-play: un ramo poco giocato può contenere una deviazione redditizia. La distanza dalla matrice del benchmark resta una misura esterna, non la funzione con cui costruire i bucket.

### Altre riduzioni possibili

Ridurre le size postflop può servire a un avvio grossolano. Prima del confronto finale vanno però reinserite le azioni della fixture e addestrate le risposte corrispondenti; trasferire una policy iniziale non rende equivalenti i due giochi. Per questo partirei dalla riduzione delle carte mantenendo le size attuali.

Il depth-limited solving potrebbe sostituire continuazioni lunghe con valori dipendenti dai range o con un insieme di policy di continuazione. Il lavoro sulle foglie multi-valued spiega perché una continuazione strategica unica non rappresenti tutte le risposte possibili.[^6] Qui mancano ancora valori di frontiera sufficientemente affidabili. Congelare il postflop e allenare solo il preflop, come fa il refinement corrente, conserva l'eventuale errore delle continuazioni.

Deep CFR sostituisce tabelle con approssimatori e costituisce un'alternativa documentata.[^7] Per il limite locale di due ore non abbiamo però dati sul costo CPU di raccolta dei campioni, training e inferenza. Non lo sceglierei prima di aver verificato il candidato tabellare compatto. Lo stesso criterio vale per portfolio di policy, PSRO e altre forme di compressione: il costo per costruire continuazioni attendibili deve entrare nel conto.

## 8. Scelta dell'algoritmo: confronto sullo stesso spazio strategico

Il risultato di MCCFR riguarda stimatori e condizioni di sampling precisi.[^8] Cambiare discount, averaging, esplorazione o modalità di fusione fra thread richiede verifiche corrispondenti. Il nome «MCCFR» da solo non identifica una procedura.

| Candidato | Vantaggio da verificare | Rischio o costo | Priorità |
|---|---|---|---|
| External sampling con averaging corretto | Riferimento semplice, deal fisici e stato compatto | Varianza e visite sparse | Prima baseline |
| Linear MCCFR con baseline di riduzione della varianza | Peso crescente dei campioni e informazioni riusate fra visite | Formula dei pesi e baseline da verificare | Primo candidato prestazionale |
| Public chance sampling / public outcome sampling | Riutilizzo di un board per molte mani; accessi regolari | Più lavoro per visita e possibili aggiornamenti duplicati dello stesso bucket | Challenger CPU |
| CFR+ o DCFR con traversata completa dell'astrazione | Aggiornamenti meno rumorosi su un gioco piccolo | Operatori chance e payoff astratti corretti; costo di ogni sweep | Alternativa da misurare |
| DCFR con schedule variabile / PCFR+ | Possibile riduzione delle iterazioni | Risultati dipendenti dal dominio e dalla traversata | Esperimento successivo |
| Outcome sampling | Visite individuali economiche | Varianza elevata, correzioni di importanza e averaging | Secondario |
| CFR-D con tutti i subgame fisici | Decomposizione compatibile con obiettivi teorici più forti | Il lavoro totale delle continuazioni resta enorme | Non adatto al primo obiettivo di due ore |
| LP in sequence form / metodi di primo ordine | Riferimento alternativo per astrazioni molto piccole | Rappresentazione e prodotti fra operatori da costruire | Oracle ridotto, non prima architettura |

Linear MCCFR è un candidato distinto dal DCFR attuale 1.5/0/3. Il lavoro di Brown e Sandholm considera anche varianti campionate e mostra che i vantaggi del discount dipendono dal regime; non giustifica trasferire automaticamente uno schedule deterministico al nostro sampler.[^9] Le baseline di Schmid e colleghi e di Davis e colleghi mirano a ridurre la varianza degli aggiornamenti senza sostituire il payoff del gioco con una previsione non corretta.[^10][^11]

PCS merita un confronto perché il motore postflop possiede già aggregazioni per mano e blocker. Esiste però un limite rilevante proprio per i bucket: nel lavoro originale il chance sampling può essere più rapido nelle astrazioni piccole, dove molte mani condividono lo stesso infoset; PCS diventa competitivo al crescere della granularità.[^12] Quindi il confronto deve misurare **tempo per qualità raggiunta**, non soltanto nodi al secondo o varianza per iterazione.

La versione 2026 di *Faster Game Solving via Hyperparameter Schedules* valuta schedule DCFR variabili, anche su endgame di poker.[^13] È un candidato per il trainer deterministico, non una dimostrazione di un solve preflop Short Deck in due ore. Anche PCFR+ non è una sostituzione universalmente migliore: il lavoro originario riporta risultati poker in cui DCFR resta competitivo o superiore.[^14]

La memoria imperfetta limita comunque le garanzie: un regret basso nell'astrazione non fornisce automaticamente una NashConv bassa nel gioco fisico. Per isolare le cause, prima si confrontano gli algoritmi sulla stessa partizione; poi si confrontano le partizioni con algoritmo e budget fissati. Cambiare entrambi nello stesso esperimento impedisce di attribuire il miglioramento.

## 9. Come usare CPU e RAM senza ricreare milioni di subgame

### Layout e budget della memoria

La proposta usa uno scheletro di betting compilato con ID interi, azioni e transizioni già validate. Per ogni nodo decisionale, lo stato numerico è un array indicizzato dall'etichetta astratta dell'osservazione. Questo evita hash della history, ricostruzione delle azioni e piccoli oggetti dinamici nella visita frequente.

Con regret e somme strategiche a 64 bit, il solo payload è:

`M_numerico = 16 × Σ_h [numero_azioni(h) × numero_contesti_astratti(h)] byte`.

Usando i 29.112 archi d'azione dello scheletro e, soltanto per stimare l'ordine di grandezza, lo stesso numero K di contesti a ogni decisione:

| K totale per history | Payload numerico |
|---:|---:|
| 256 | 0,111 GiB |
| 1.024 | 0,444 GiB |
| 2.048 | 0,888 GiB |
| 8.192 | 3,554 GiB |
| 16.384 | 7,107 GiB |

K comprende tutte le distinzioni conservate: bucket privato, eventuale cluster pubblico e memoria selettiva. Non è un numero di bucket privati da moltiplicare successivamente per migliaia di board. In un layout reale le street avrebbero budget diversi e il preflop le sue 81 classi; la tabella non è una misura della RAM del processo né una prova di accuratezza.

Vanno aggiunti mapping, cache, baseline di varianza, metadati, scratch, esportazione e buffer dei thread. Tabelle dense di transizioni o payoff possono introdurre prodotti quadratici fra bucket; il sampling fisico iniziale evita di costruirle integralmente. Anche per questo una stima basata solo sui regret sarebbe incompleta.

Il PC rilevato è un i3-10100F, quattro core fisici e otto thread logici, con 32 GiB installati. Lo snapshot precedente mostrava circa 13,36 GiB disponibili: è un dato transitorio. Un primo limite del processo nell'intervallo 8–12 GiB è una proposta prudente da verificare contro la disponibilità effettiva, non RAM già riservata. Non occorre iniziare dalla quantizzazione; gli array double permettono prima di separare errore di astrazione ed errore numerico.

### Parallelismo e riuso

Otto thread logici non equivalgono a uno speedup 8×. I candidati vanno misurati a 1, 2, 4 e 8 thread, registrando banda di memoria, sincronizzazione e qualità a pari secondi. Un batching deterministico può far leggere ai worker una policy coerente e fondere delta limitati; dimensione del batch e aggiornamenti ritardati influenzano la dinamica e vanno validati.

Replicare una tabella da 3,55 GiB per quattro worker, oltre allo stato principale, supera già 17 GiB prima del resto. Servono buffer sparsi limitati o partizionamento del lavoro, evitando copie integrali e lock globali nel percorso frequente. Nel candidato PCS, board batch e vettori di mani offrono una diversa unità di parallelismo.

Gli all-in preflop offrono un ulteriore riuso. Una matrice di equity 81×81 in double occupa 52.488 byte, circa 51,26 KiB, ma la sua costruzione esatta può essere costosa. Il modulo di decomposizione possiede già cataloghi, conteggi compatibili e valutazione dei terminali all-in. [Builder](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/libs/preflop/src/hu_preflop_decomposition.cpp:5448), [uso dei pesi compatibili](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/libs/preflop/src/hu_preflop_decomposition.cpp:5670).

La matrice è utilizzabile con distribuzioni e molteplicità coerenti con le classi, includendo le collisioni fra carte. Non si può sostituire indiscriminatamente il payoff di una coppia fisica estratta con l'equity media della coppia di classi: cambia il condizionamento. I terminali fold sono già deterministici; integrare analiticamente chance e azioni note, dove possibile, riduce la varianza. La costruzione delle tabelle all-in e il loro caricamento devono essere misurati separatamente e inclusi nel rispettivo scenario temporale.

## 10. Che cosa dicono i risultati già disponibili

### Le 34,10 ore descrivono una specifica sweep

La proiezione storica moltiplica `366.488.496 root × 0,0026794 secondi` e divide per otto worker ideali. Il risultato è 272,77 ore seriali, oppure 34,10 ore idealizzate. Il tempo unitario proviene da una traversata del subgame River peggiore, applicato a tutte le root. Non è il tempo misurato dell'intera sweep, non è un limite inferiore, e non rappresenta un solve convergente: successive iterazioni e altre street aggiungerebbero lavoro. [Origine del calcolo](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/docs/HU_PREFLOP_CO40_BENCHMARK.md:422).

La nuova architettura aggiorna uno stato astratto condiviso fra molti deal. Non deve visitare separatamente tutte quelle root a ogni iterazione. Il lavoro viene ridotto accettando un errore di rappresentazione dichiarato, che deve essere misurato sul gioco fisico. Questo è il motivo per cui quella proiezione non vieta un tentativo da due ore.

### Velocità già sufficiente a produrre output, qualità ancora distante

| Risultato storico nel repository | Tempo riportato | TV media | Errore EV | Scarto all-in root |
|---|---:|---:|---:|---:|
| Memoryless 500k v1 | 281,37 s | 42,675 pp | 0,28289 ante | −25,532 pp |
| Memoryless suit-invariant 100k v2 | 45,31 s | 50,864 pp | 0,66021 ante | −25,288 pp |
| 100k + 400k di refinement preflop | 139,09 s | 51,247 pp | 0,60601 ante | −32,161 pp |

Fonti locali: [candidato 500k](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/benchmarks/results/hu_preflop_co40_memoryless_500k_candidate_v1.json), [confronto 500k](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/benchmarks/results/hu_preflop_co40_memoryless_500k_comparison_v1.json), [candidato 100k v2](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/benchmarks/results/hu_preflop_co40_memoryless_suit_invariant_100k_candidate_v2.json), [confronto 100k v2](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/benchmarks/results/hu_preflop_co40_memoryless_suit_invariant_100k_comparison_v2.json), [candidato refinement](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/benchmarks/results/hu_preflop_co40_memoryless_100k_plus_400k_preflop_refinement_candidate_v1.json), [confronto refinement](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/benchmarks/results/hu_preflop_co40_memoryless_100k_plus_400k_preflop_refinement_comparison_v1.json).

Questi report non sono un esperimento A/B controllato: compaiono fingerprint diversi del gioco e del riferimento, e `REFERENCE_CONFIG_INCOMPLETE` segnala informazioni mancanti sul solve esterno. Un hash diverso da solo non dimostra un cambiamento delle regole: possono cambiare anche schema e metadati. I numeri mostrano comunque che i precedenti output valutati dal rispettivo comparatore restano lontani dal target. Non permettono di estrapolare una curva di convergenza fra versioni.

Per il risultato 500k, alcuni scarti sono visibili senza aggregazioni:

| Classe | Distribuzione target rilevante | Distribuzione candidata rilevante |
|---|---|---|
| AA | Call 28%; raise 6: 24%; raise 10: 48% | Call 83,90%; raise 6: 7,20%; raise 10: 4,51% |
| AKo | All-in 100% | All-in 16,38%; raise 10: 73,74% |
| A6o | Fold 0%; all-in 50%; call 26% | Fold 57,56%; all-in 5,55%; call 27,87% |
| JTs | All-in 78%; call 10% | All-in 7,98%; call 35,58% |

Ogni riga mostra solo le azioni citate, non una distribuzione completa. Le frequenze del riferimento sono riportate nella [fixture](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/benchmarks/fixtures/hu_preflop_co40_reference_v1.json).

La frequenza all-in aggregata target è circa 40,779%, contro circa 15,247% del candidato 500k. Vale quindi la pena controllare prima i valori relativi fra all-in e continuazioni non all-in: chiamate apparentemente redditizie contro un postflop debole possono ridurre troppo lo shove. È un'ipotesi causale da verificare tramite EV per azione e risposte BTN, non una diagnosi dimostrata dal solo scarto.

Il run con rake 5% che avvicina l'EV non risolve il problema: cambia la utility rispetto alla fixture senza rake e resta distante nelle frequenze. Regole, rake, size e target non devono diventare parametri con cui adattare artificialmente il benchmark.

### Evidenza diagnostica del codice corrente

La sonda fisica lossless precedente, a 100.000 iterazioni, registrava 8.996.964 infoset. Dei 3.575.493 infoset River, 3.575.469 erano stati visitati una sola volta. La quantità di stato creata non corrisponde quindi a una strategia tardiva ben appresa. La memoryless riutilizza molto più stato, ma introduce le perdite descritte sopra. [Statistiche conservate](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/docs/research/preflop_12h_evidence_20260910/analysis_summary.json).

Le tre ripetizioni della sonda fisica da 20.000 iterazioni davano mediane 8,47394 s senza cache del vincitore e 5,61611 s con cache: speedup 1,509×. Sono misure precedenti su copie diagnostiche, con stesso seed e confronto dell'output root e dell'EV. Non provano equivalenza di tutta la policy o raggiungimento del benchmark; non costituiscono una modifica production effettuata in questa analisi. [Run e comandi](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/docs/research/preflop_12h_evidence_20260910/runs.json).

## 11. Misurare l'EV entro il budget

Una policy veloce da addestrare può essere costosa da valutare se ogni query ricostruisce chiavi, bucket e legal actions. `query_hu_preflop_postflop_policy` ripercorre la history e ricalcola le feature; il percorso di esportazione costruisce e ordina una seconda rappresentazione della policy. Il sampler può inoltre produrre solo il risultato root quando l'esportazione postflop è disabilitata. Il budget finale deve includere una policy interrogabile e il relativo costo di accesso. [Query](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/libs/preflop/src/hu_preflop_solver.cpp:1222), [estrazione](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/libs/preflop/src/hu_preflop_solver.cpp:1054).

Il parametro `solve_seconds` comprende training e diverse valutazioni interne, ma parte dopo la costruzione di alcuni oggetti e non comprende la scrittura del file finale. Non basta quindi a certificare il requisito temporale. L'orologio del nuovo benchmark deve comprendere tutto il lavoro specifico del caso: preparazione, training, valutazione, esportazione e verifica di rilettura. Avvio generico dell'applicazione e risorse riutilizzabili già presenti vanno riportati separatamente, dichiarando sempre scenario cold o warm.

### Il numero di deal va scelto dalla varianza

La sonda fisica da 100.000 iterazioni, valutata su 20.000 deal, aveva errore standard EV di circa 0,16385 ante. Una semilarghezza normale al 95% sarebbe circa 0,321 ante, molto maggiore della tolleranza di 0,05. Se varianza e indipendenza restassero uguali, servirebbero circa 825.000 campioni per una semilarghezza di 0,05, o 5,16 milioni per 0,02. Sono proiezioni statistiche indicative, non una garanzia: cambiano con la policy e con lo stimatore. [Risultato della sonda](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/docs/research/preflop_12h_evidence_20260910/profile_exact_100000_0.json).

Il criterio pratico proposto, per un checkpoint finale scelto prima della valutazione indipendente, è:

`|EV_stimata − EV_target| + semilarghezza_IC95 ≤ 0,05 ante`.

Questo criterio è più prudente del solo confronto della media e non aggiunge precisione al target esterno, riportato a una cifra decimale. Un campione fisso dimensionato con un pilot indipendente evita di interpretare come intervallo al 95% una sequenza di controlli ripetuti con arresto opportunistico. In alternativa serve un metodo statistico valido per arresto sequenziale.

Per ridurre il costo, valuterei integrazione analitica dei terminali e delle azioni note, stratificazione dei deal e control variate. AIVAT è una fonte primaria pertinente per stimare valori con varianza ridotta sfruttando policy note.[^15] Va derivato lo stimatore per le nostre utility e verificata l'assenza di bias; il fattore di riduzione osservato in altri lavori non è trasferibile al nostro caso. Campioni comuni fra due candidate aiutano soprattutto la precisione della loro differenza.

La valutazione misura l'EV del profilo fisico indotto dalle due policy astratte. Non è una best response e non certifica un equilibrio. Le risposte apprese attuali, con training limitato e nella stessa astrazione, non sono un upper bound sull'exploitability. Anche il valore zero ottenuto troncando una stima negativa non dimostra NashConv nulla.

Servono due misure di incertezza distinte: rumore dei deal usati per valutare una policy fissa e variabilità fra training indipendenti. Una strategia che cambia poco negli ultimi minuti può aver stabilizzato un'astrazione sbagliata. Per questo, oltre alle metriche del benchmark, manterrei controlli con policy avversarie differenti, audit fisici su sotto-giochi ridotti e confronto con una partizione più fine. Il benchmark root disponibile non determina da solo la correttezza dell'intero gioco.

## 12. Esperimento da progettare e criteri di decisione

### Budget di un singolo run candidato

| Intervallo proposto | Lavoro | Condizione |
|---|---|---|
| 0–10 minuti | Caricamento o costruzione delle risorse, partizione e layout | Misurare separatamente cold/warm; se il setup supera il budget, la candidata cold fallisce |
| 10–90 minuti | Training con controlli intermedi leggeri | Budget comune fra algoritmi; stato e cache entro il limite RAM |
| 90–110 minuti | Valutazione fisica finale su dati indipendenti | IC sufficientemente stretto; altrimenti risultato inconcludente sull'EV |
| 110–115 minuti | Esportazione, rilettura e confronto delle frequenze | Soluzione consultabile, fingerprint coerenti |
| 115–120 minuti | Riserva | Consegna pianificata entro 115 minuti; fallimento temporale se si raggiungono 120 |

Questa è una ripartizione progettuale di 120 minuti, non un benchmark ottenuto. Non comprende lo sviluppo del motore né un'intera ricerca su molte candidate. Se sono richieste risorse precomputate, il risultato deve dichiararlo: una modalità warm riuscita non dimostra un primo solve cold inferiore a due ore.

Non fisserei oggi un numero di bucket «vincente». Una prima scala di budget totali per history, per esempio K=256, 1.024 e 4.096 con allocazione differente per street, serve a misurare se l'errore scende spendendo più capacità. Non equivale a imporre questi tre numeri a ogni street. Il budget della tabella e quello delle cache devono essere congelati prima del run.

### Ordine delle attività dopo questa analisi

1. **Correggere e verificare averaging e stimatori.** Gioco minimo multi-decisione, enumerazione esatta delle aspettative, confronto indipendente e controlli Kuhn/Leduc. Senza questo passaggio le curve successive non isolano l'errore di astrazione.
2. **Preparare il percorso CPU compatto.** Evaluator tabellare, cache del payoff per deal, scheletro compilato, array indicizzati e cache limitate; verificare equivalenza fisica e costo cold/warm.
3. **Confrontare le rappresentazioni.** Baseline corrente corretta, feature distribuzionali Flop/Turn, OCHS River e memoria selettiva, con seed separati e medesime size.
4. **Confrontare gli algoritmi e i thread.** External sampling/Linear MCCFR, PCS e full DCFR sul rispettivo spazio compatibile; misurare tempo per qualità, scaling e costo di valutazione.
5. **Eseguire il run finale entro due ore.** Partizione e configurazione congelate, campioni finali indipendenti, tutte le metriche, IC EV e soluzione rileggibile. La qualificazione resta limitata a range/EV.

La matrice sperimentale deve restare piccola e sequenziale: prima eliminiamo i difetti di correttezza, poi scegliamo una rappresentazione sostenibile, infine confrontiamo le varianti di aggiornamento. I checkpoint a 5, 15, 30, 60 e 80 minuti di training possono mostrare il progresso, ma non vanno scambiati per cinque repliche indipendenti.

Se il regret dell'astrazione e le strategie si stabilizzano mentre lo scarto fisico resta elevato, il prossimo intervento riguarda rappresentazione o differenze di gioco. Se cambiando seed varia molto il risultato, il limite può essere rumore o training insufficiente. Se più iterazioni abbassano regolarmente gli errori a partizione fissa, ottimizzare throughput e varianza può essere sufficiente. Per separare queste ipotesi servono EV per azione, diagnostica intra-bucket e policy BTN, anche quando il riferimento esterno non le fornisce.

Una prova da due ore che non raggiunge il target deve restituire il migliore checkpoint, errori e costo per componente. Non deve aumentare tolleranze, modificare rake o ridefinire la metrica a posteriori. Se la configurazione esterna resta incompleta, il rapporto potrà attestare vicinanza numerica ai dati disponibili, non parità dimostrata con il solve privato.

## 13. Verifiche svolte, limiti e materiali

Questa analisi ha letto i percorsi del sampler, del motore generico, dell'evaluator, delle regole d'azione, della decomposizione, dei kernel postflop e del comparatore. Ha ricalcolato capacità delle tabelle e budget numerici, esaminato fixture e risultati conservati e consultato fonti primarie e codice pubblico. **In questa fase è stato creato soltanto questo Markdown: nessuna modifica ai sorgenti, nessun nuovo test o benchmark del solver eseguito.** Le sonde e il controesempio richiamati provengono dalla fase precedente della stessa indagine.

| Affermazione | Stato |
|---|---|
| L'evaluator corrente ripete 21 valutazioni a cinque carte | Verificata nel codice |
| Una tabella completa a sette carte ha payload di 31,84 MiB a 32 bit | Conteggio combinatorio; costruzione e lookup non misurati |
| Averaging problematico presente in entrambi i sampler | Verificato nel codice e nel controesempio algebrico conservato |
| Bucket con otto campioni, perdita di informazione pubblica, cache senza budget | Verificati nel codice |
| Una nuova astrazione può essere rappresentata in pochi GiB | Modello di capacità condizionato alla dimensione scelta; non misura del processo |
| Il candidato proposto raggiunge i gate entro due ore | **Non dimostrato** |
| Il Monker privato usa una specifica variante CFR | **Non determinabile dalle informazioni disponibili** |

Il working tree contiene modifiche preesistenti e file non tracciati. Il solo commit non identifica il codice esaminato. I manifest e le misure precedenti sono conservati in [preflop_12h_evidence_20260910](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/docs/research/preflop_12h_evidence_20260910/source_manifest.json); il rapporto precedente conserva il contesto della certificazione e delle prove in [HU_PREFLOP_12H_FEASIBILITY_2026-09-10.md](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/docs/research/HU_PREFLOP_12H_FEASIBILITY_2026-09-10.md). Il presente documento adotta l'obiettivo aggiornato: range ed EV entro due ore, con libertà di algoritmo e astrazione, soltanto CPU e RAM.

Il prossimo intervento ad alta priorità è la verifica indipendente dell'averaging external sampling, prima di investire in nuovi run lunghi.

## Fonti

Fonti primarie consultate il 10 settembre 2026. I risultati prestazionali dei paper si riferiscono ai rispettivi giochi e hardware; non sono misure del repository. La consultazione GitHub ha fornito un riferimento indipendente di implementazione, non informazioni sul codice proprietario Monker.

[^1]: MonkerWare, [Abstraction](https://www.monkerware.com/compare.html). Evidenza pubblica dell'uso di astrazioni e del loro impatto sui risultati.
[^2]: MonkerWare, [Guide](https://www.monkerware.com/guide.html). Impostazioni pubbliche del tree e dei bucket; non documenta il motore privato Short Deck.
[^3]: Google DeepMind, [OpenSpiel: external_sampling_mccfr.py](https://github.com/google-deepmind/open_spiel/blob/master/open_spiel/python/algorithms/external_sampling_mccfr.py). Convenzioni di sampling e averaging da confrontare con una derivazione indipendente.
[^4]: Johanson, Burch, Valenzano, Bowling, 2013, [Evaluating State-Space Abstractions in Extensive-Form Games](https://poker.cs.ualberta.ca/publications/AAMAS13-abstraction.pdf). Astrazioni distribuzionali, OCHS e memoria imperfetta.
[^5]: Ganzfried, Sandholm, 2014, [Potential-Aware Imperfect-Recall Abstraction with Earth Mover's Distance in Imperfect-Information Games](https://www.cs.cmu.edu/~sandholm/potential-aware_imperfect-recall.aaai14.pdf). Feature delle evoluzioni future e clustering.
[^6]: Brown, Sandholm, Amos, 2018, [Depth-Limited Solving for Imperfect-Information Games](https://arxiv.org/abs/1805.08195). Continuazioni multi-valued alle frontiere.
[^7]: Brown, Lerer, Gross, Sandholm, 2019, [Deep Counterfactual Regret Minimization](https://proceedings.mlr.press/v97/brown19b.html). Approssimazione dei regret mediante reti.
[^8]: Lanctot, Waugh, Zinkevich, Bowling, 2009, [Monte Carlo Sampling for Regret Minimization in Extensive Games](https://www.cs.cmu.edu/~kwaugh/publications/nips09b.pdf). Fondamenti degli stimatori MCCFR.
[^9]: Brown, Sandholm, 2019, [Solving Imperfect-Information Games via Discounted Regret Minimization](https://arxiv.org/pdf/1809.04040). DCFR e varianti di pesatura, incluso il regime campionato.
[^10]: Schmid, Burch, Lanctot, Moravčík, Kadlec, Bowling, 2019, [Variance Reduction in Monte Carlo Counterfactual Regret Minimization](https://arxiv.org/pdf/1809.03057). Baseline per aggiornamenti campionati.
[^11]: Davis, Schmid, Bowling, 2020, [Low-Variance and Zero-Variance Baselines for Extensive-Form Games](https://proceedings.mlr.press/v119/davis20a.html). Riduzione della varianza e public outcome sampling.
[^12]: Johanson, Bard, Lanctot, Gibson, Bowling, 2012, [Efficient Nash Equilibrium Approximation through Monte Carlo Counterfactual Regret Minimization](https://poker.cs.ualberta.ca/publications/AAMAS12-pcs.pdf). PCS e dipendenza dalla dimensione dell'astrazione.
[^13]: Zhang, McAleer, Sandholm, versione 2026, [Faster Game Solving via Hyperparameter Schedules](https://arxiv.org/html/2404.09097v2). Schedule variabili e confronti sperimentali.
[^14]: Farina, Kroer, Sandholm, 2021, [Faster Game Solving via Predictive Blackwell Approachability: Connecting Regret Matching and Mirror Descent](https://arxiv.org/pdf/2007.14358). PCFR+ e confronto con DCFR.
[^15]: Burch, Schmid, Moravčík, Morrill, Bowling, 2018, [AIVAT: A New Variance Reduction Technique for Agent Evaluation in Imperfect Information Games](https://poker.cs.ualberta.ca/publications/aaai18-burch-aivat.pdf). Stima dell'EV con control variate e policy note.
