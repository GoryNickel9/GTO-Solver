# Analisi: algoritmo e astrazione per il preflop HU Short Deck

Data: 2026-09-15
Snapshot analizzato: `04aa687` (working tree pulito)
Stato: `ANALISI / PROPOSTA DI MODIFICA — nessun sorgente modificato`
Decisioni successive e punti aperti: [PREFLOP_ARCHITECTURE_DECISION_LOG.md](PREFLOP_ARCHITECTURE_DECISION_LOG.md)
(le domande della sezione 7 sono in gran parte risolte lì).

Questo documento risponde a tre domande: quale algoritmo usare per il preflop Short Deck con
astrazione, come convergere a Nash in modo misurabile, e quali regole restano valide nel
multiway. Le affermazioni sui documenti storici sono state riverificate sul codice; dove un
documento e il codice divergono, vale il codice.

## 0. Sintesi

Il percorso V1–V23 non converge per cause strutturali, non per mancanza di iterazioni:

1. **I bucket postflop sono dominati dal rumore.** La feature principale è un'equity Monte Carlo
   a 8 campioni contro una mano uniforme (`compute_distributional_strength_bucket`,
   [hu_preflop_solver.cpp:1276](../../libs/preflop/src/hu_preflop_solver.cpp)). Con 8 campioni
   l'errore standard è circa `17,7 pp`; al Flop la V8 conserva 3 bit di equity, cioè bin da
   `12,5 pp`. Il bin è più stretto dell'errore: due mani con equity vera `0,45` e `0,55` finiscono
   in ordine invertito con probabilità vicina a quella di finire in ordine corretto. L'astrazione
   di fatto conserva la categoria e poco altro. Lo studio architetturale del 10 settembre lo aveva
   già identificato (§6, "quantizzazione di rumore"); non è mai stato corretto.
2. **External sampling su un deal fisico alla volta ha una varianza incompatibile con azioni quasi
   indifferenti.** Per `JTo` la trace V20 mostra errori standard di `0,10–0,30a` sugli EV d'azione
   con 2.000 deal per azione, mentre i gap EV che decidono la frequenza sono `≤ 0,1a`. Il regret
   cumulativo è rumore più segnale dello stesso ordine: la TV fra seed di `11 pp` è la
   conseguenza aritmetica, non un difetto correggibile con CRN o stratificazione.
3. **Il criterio di qualità è sbagliato.** WMAE e TV contro le frequenze Monker non misurano la
   convergenza: astrazione e albero postflop esterni sono ignoti e il `94,39 %` della TV fra seed
   ricade su azioni con gap EV `≤ 0,1a` (V18). Venti versioni sono state respinte contro un
   proxy più rumoroso dell'effetto che dovevano rilevare.
4. **Il certificatore usa la decomposizione sbagliata.** V21/V22 valutano `322·10^6` sottogiochi
   River separati a `0,24–0,31 s` ciascuno (`2,5–3,2 anni`). Una traversata vettoriale
   board-major dell'intero albero postflop costa `≈ 0,05 s` per board e copre tutte le 873 shape
   River insieme: la best response esatta nel gioco fisico è dell'ordine di **un'ora su 8 thread**,
   non di anni. Il kernel necessario (somme prefisse per rank con blocker) esiste già in
   [postflop_solver.cpp:8750](../../libs/postflop/src/postflop_solver.cpp).
5. **V23 certifica un gioco diverso da quello da certificare.** Il `FiniteGame` con `K` deal per
   classe è un gioco a chance empirica di 648 deal su `7,1·10^10`; il profile EV CO cambia di
   `0,23a` fra `K=2` e `K=4`. Il certificato è esatto ma privo di trasferibilità, e la
   materializzazione esplode (`18·10^6` nodi stimati per HU40).

**Raccomandazione.** Sostituire il trainer a deal singolo con **CFR vettoriale a public chance
sampling** (campiona il board, enumera tutte le mani, regret indicizzati per bucket, reach per
combo fisica) su un'**astrazione precalcolata con feature esatte e clustering**. Certificare con
una **best response esatta nel gioco fisico della strategia sollevata**, calcolata con la stessa
traversata board-major. Abbandonare il gate di frequenze Monker come criterio di convergenza.
Queste tre scelte coincidono con il ramo della roadmap R6 ("PCS come challenger", "full
CFR+/DCFR sull'astrazione") che non è mai stato eseguito.

## 1. Stato verificato nel codice

| Componente | Stato verificato | Riferimento |
|---|---|---|
| Averaging external sampling | Corretto dopo R2: peso `1·w_t` solo nel passaggio dell'avversario del traverser | [external_sampling.hpp](../../include/gtosd/core/external_sampling.hpp), [hu_preflop_solver.cpp:3718](../../libs/preflop/src/hu_preflop_solver.cpp) |
| Chance | Deal fisico uniforme (hole+hole+board) per traversata | [hu_preflop_solver.cpp:944](../../libs/preflop/src/hu_preflop_solver.cpp) |
| Chiave postflop V8 | Hash della history pubblica + bucket della street corrente; classe preflop e bucket precedenti dimenticati; board fisico non nella chiave | [hu_preflop_solver.cpp:3440](../../libs/preflop/src/hu_preflop_solver.cpp) |
| Feature bucket | MC8 vs mano uniforme; 16 bin di equity (riga 1368); V8 conserva 3/4/4 bit di equity su Flop/Turn/River (riga 1427); "profilo" e "texture" quasi mai rappresentati alle capacità `32/128/512` | [hu_preflop_solver.cpp:1204–1470](../../libs/preflop/src/hu_preflop_solver.cpp) |
| Census V17 | 10.060 contesti decisionali, 1,57 M information set; River `174,7` righe medie per contesto su 512 possibili | V22 fase 1 |
| Media con massa zero | `average_strategy` restituisce la strategia corrente (regret-matched) quando `strategy_sum` è nulla: la "media" esportata di un infoset visitato solo come traverser è in realtà la policy corrente | [hu_preflop_solver.cpp:186–205](../../libs/preflop/src/hu_preflop_solver.cpp) |
| Default algoritmo del trainer | `DiscountedMccfr1503` resta il default delle opzioni | [hu_preflop.hpp:1424](../../include/gtosd/preflop/hu_preflop.hpp) |
| Tabella 7 carte esatta | Implementata e verificata (R3), 8.347.680 voci | [seven_card_table.cpp](../../libs/equity/src/seven_card_table.cpp) |
| Kernel showdown vettoriale | Somme prefisse per rank e per carta, AVX2, nel solver postflop | [postflop_solver.cpp:8724–8825](../../libs/postflop/src/postflop_solver.cpp) |
| Certificatore | Root-by-root River (V21), board-batched (V22): `0,31 s` per sottogioco | V22 report |
| Gioco astratto V23 | `FiniteGame` esplicito con corpus di `K` deal per classe CO | [hu_preflop_abstract_game.cpp](../../libs/preflop/src/hu_preflop_abstract_game.cpp) |

Il census V17 mostra che l'astrazione V8 è **quasi saturata**: la mancata convergenza non dipende
da information set mai visitati ma dalla qualità della partizione e dal rumore di ogni update.

## 2. Diagnosi dettagliata

### 2.1 L'astrazione conserva la categoria e un lancio di moneta

Ogni campione MC vale 0, ½ o 1. Con `n=8` la media assume 17 valori a passo `6,25 pp` e ha errore
standard `sqrt(p(1-p)/8) ≤ 17,7 pp`. Nella V8:

| Street | Bit equity | Larghezza bin | Errore standard / bin |
|---|---:|---:|---:|
| Flop | 3 | 12,5 pp | ≈ 1,4 bin |
| Turn | 4 | 6,25 pp | ≈ 2,8 bin |
| River | 4 | 6,25 pp | ≈ 2,8 bin |

Al River le mani avversarie compatibili sono 406: l'equity esatta costa 406 lookup e viene invece
stimata con 8 campioni. Lo screen "MC8 vs MC32" che ha chiuso la questione confrontava un solo seed
a 100.000 iterazioni, con un effetto di `0,6 pp` e una variabilità fra seed di `7–9 pp`: non era in
grado di decidere.

Il mapping non è un clustering: è una composizione di bit (`categoria | equity | profilo`) con
punteggi ad hoc (`2·g0 + 3·g1 + 2·g2 + 2·var + 2·dom + texture`). Non esiste una distanza fra
osservazioni né centroidi appresi dai dati. Le astrazioni usate dai solver di riferimento
(distribution-aware con EMD, potential-aware, OCHS) sono tutte cluster su feature esatte.

### 2.2 La varianza per update è il limite, non il numero di iterazioni

Con external sampling ogni traversata aggiorna una sola mano del traverser. A 2 M iterazioni una
classe da 12 combo riceve circa `2·10^6 · 12/630 ≈ 3,8·10^4` visite alla root; ogni campione ha
deviazione `10–20a` per la presenza degli all-in a 40a. Rumore del regret cumulativo
`≈ sqrt(3,8·10^4)·15a ≈ 3·10^3 a`; segnale per un gap di `0,1a`: `3,8·10^3 a`. Sono dello stesso
ordine, e i pesi lineari (`t`) amplificano il rumore tardivo. Ogni information set River riceve in
media poche decine di contributi nell'intero run.

Con una traversata vettoriale per board, ogni board campionato aggiorna tutti i nodi pubblici per
tutte le 465 mani vive, con l'attesa esatta sulle 406 mani avversarie: resta solo la varianza del
board. A parità di tempo il numero di contributi per information set cresce di tre–quattro ordini
di grandezza (si veda §4).

### 2.3 Le frequenze Monker non sono un criterio di convergenza

Due equilibri diversi, o due astrazioni diverse dello stesso gioco, producono frequenze diverse
sulle azioni quasi indifferenti e lo stesso EV. La decomposizione V18 lo quantifica: il `94,39 %`
della TV fra seed cade su gap EV `≤ 0,1a`. La roadmap V20 aveva già degradato Monker a dato
descrittivo; il gate operativo dei protocolli V6–V19 è però rimasto WMAE/TV. Il criterio corretto
è l'exploitability della strategia sollevata nel gioco fisico, con EV e intervallo.

### 2.4 La certificazione è fattibile: è la decomposizione a essere sbagliata

Il costo V22 è `322·10^6 sottogiochi × 0,31 s`. Il conteggio nasce dal prodotto
`369.072 board history canoniche × 873 shape River`, ognuna valutata separatamente con matrici
`465×465`. Una traversata board-major visita l'albero pubblico postflop (30.324 nodi) una volta per
board history con vettori di 465 mani, calcola gli showdown con somme prefisse per rank
(`O(n log n)` per board, `O(n)` per nodo) e copre tutte le shape nello stesso passaggio:

| Grandezza | Valore |
|---|---:|
| Board history canoniche (con molteplicità) | 369.072 |
| Nodi pubblici postflop | 30.324 |
| Mani vive per board | 465 |
| Operazioni per board (stima) | `≈ 10^8` |
| Costo per board single-thread (stima) | 0,03–0,1 s |
| Passata completa, 8 thread (stima) | 1–2 h |

È una stima da misurare, ma il fattore rispetto ai `3,2 anni` è `≥ 10^4`. La stessa passata, con
regret update, è un'iterazione **esatta** di CFR sull'astrazione; con campionamento dei board
diventa il trainer.

### 2.5 Il gioco astratto giusto non è il `FiniteGame` a corpus

Il gioco da risolvere è: chance fisica esatta, mani fisiche per card removal, **strategie legate
per bucket**. Non va materializzato: vive nella traversata vettoriale, con la tabella dei regret
indicizzata da `(nodo pubblico, bucket)` e i vettori di reach per combo. Il `FiniteGame` a `K`
deal per classe è utile come oracle su HU10 per verificare formule e resume; non deve essere il
percorso HU40.

### 2.6 Metodologia sperimentale

Gli screen V6–V19 usano uno o due seed a 100k–250k iterazioni e soglie di `0,5 pp` con una
variabilità fra seed di `25–35 pp` a 250k. Decisioni prese sotto la soglia del rumore sono casuali.
Il fatto che quasi tutti i challenger siano risultati "peggiori di poco" e quasi nessuno "migliore
di molto" è coerente con questo.

## 3. Architettura raccomandata

### 3.1 Astrazione: feature esatte precalcolate e clustering

Lo Short Deck rende esatte e precalcolabili tutte le feature. Conteggi canonici (limite inferiore
con 24 permutazioni dei semi) e costo con la tabella a 7 carte:

| Street | Osservazioni canoniche `(mano, board)` | Feature esatta | Lookup per osservazione | Lookup totali |
|---|---:|---|---:|---:|
| Flop | `630·C(34,3)/24 ≈ 1,6·10^5` | istogramma dell'equity River sui 465 runout, 406 avversari | `1,9·10^5` | `3·10^10` |
| Turn | `630·C(34,3)·31/24 ≈ 4,9·10^6` | istogramma dell'equity River sui 30 river, 378 avversari | `1,1·10^4` | `5,5·10^10` |
| River | `630·C(34,5)/24 ≈ 7,3·10^6` | equity esatta vs 406 avversari, per 8 gruppi avversari (OCHS) | 406 | `3·10^9` |

Totale `≈ 9·10^10` lookup: **10–30 minuti una sola volta**, indipendenti da stack, sizing e
albero; il risultato si salva su disco (decine di MB) con fingerprint di mazzo, ranking, versione
feature, capacità e seed. Le 81 classi preflop restano esatte.

Clustering: k-means con distanza EMD sugli istogrammi (Flop/Turn), L2 sui vettori OCHS (River),
inizializzazione k-means++ e più restart a seed fisso. Memoria imperfetta: la chiave postflop è
`(history pubblica, bucket della street corrente)`, come oggi. Capacità da esplorare per street:
`200 / 500 / 1.000` e `500 / 1.000 / 2.000`; la scelta va fatta sull'exploitability fisica a pari
tempo, non sulle frequenze.

Vincoli da conservare: feature dipendenti solo dalle carte visibili, seed di partizione separato
da training e valutazione, mappa totale e deterministica, nessun uso della matrice Monker.

### 3.2 Algoritmo: CFR vettoriale con public chance sampling

Per ogni iterazione:

1. campionare un batch di `B` board completi (canonici, con molteplicità, oppure fisici uniformi);
2. per ogni board, dalla root preflop, propagare i vettori di reach per giocatore sulle 630 combo
   (465 vive dopo il board), leggendo la strategia dalla classe preflop (81) e, nel postflop, dal
   bucket `(mano, board)` della street corrente;
3. ai terminali: fold e all-in dal ledger; showdown con ordinamento dei rank delle 465 mani e somme
   prefisse con correzione dei blocker; all-in preflop opzionalmente da tabella esatta `630×630`;
4. risalendo, valori controfattuali per mano; per ogni nodo del giocatore aggiornato, regret del
   bucket `b` sommato sulle mani `h∈b` pesato con la reach controfattuale avversaria; strategia
   media pesata con la reach propria;
5. riduzione deterministica dei delta del batch nell'ordine dei board; aggiornamento alternato dei
   due giocatori.

Questo è il gioco astratto ibrido richiesto dalla roadmap R5 (deal fisici, strategie legate), con
l'enumerazione delle mani al posto del campionamento. Non richiede operatori chance astratti né
prodotti di marginali.

### 3.3 Regret minimizer e averaging

| Variante | Ruolo | Note |
|---|---|---|
| Linear CFR su update campionati per board (peso `t` su regret e media) | Baseline | Stessa convenzione già verificata in R2; varianza per update molto minore di ES |
| DCFR `1,5 / 0 / 2` applicato per iterazione (non per board) su batch `B ≥ 32` | Challenger principale | Con update quasi esatti per board lo sconto dei regret negativi è utile; verificare a pari wall time |
| CFR+ con media lineare | Challenger secondario | Noto per soffrire il campionamento; provare solo con `B` grande |
| Pesi lineari solo in warm-up, poi costanti | Opzione | Riduce l'amplificazione del rumore tardivo |

Da non trasferire: il pruning per-mano di Pluribus (non si applica a update vettoriali), lo schedule
`ProductionDcfr` con reset dell'average (nato per traversate esatte).

### 3.4 Parallelismo

L'unità di lavoro è il board: `B` board per batch distribuiti su 8 worker, delta sparsi per
`(nodo, bucket)`, riduzione nell'ordine dei board. Nessuna replica della tabella; la policy è
congelata dentro il batch. Riproducibilità bit-identica fra 1 e 8 thread come già ottenuto in R6.

### 3.5 Certificazione: best response esatta nel gioco fisico

La strategia sollevata assegna a ogni combo fisica la strategia del suo bucket. La best response
nel gioco fisico ha perfect recall e non è vincolata ai bucket: per ogni board history canonica,
una traversata vettoriale calcola `max_a` per mano e nodo; i valori pesati per molteplicità danno
`gain_CO`, `gain_BTN`, `NashConv` e l'EV del profilo. È l'unica quantità che certifica qualcosa sul
gioco reale, ed è la metrica standard della letteratura per strategie astratte.

Durante il training, la stessa traversata su un sottoinsieme di board fornisce una stima con
intervallo; la passata completa si esegue ai checkpoint dichiarati e alla fine.

Da non fare: una best response vincolata ai bucket con memoria imperfetta (problema difficile e
privo di significato sul gioco fisico), o un certificato sul `FiniteGame` a corpus.

### 3.6 Che cosa significa "convergere a Nash con astrazione"

Si può dimostrare: (i) regret medio e `ε`-Nash **nel gioco astratto** se l'astrazione ha perfect
recall; (ii) con memoria imperfetta CFR non ha garanzia formale generale, ma converge in pratica
e ha garanzie per classi ristrette (giochi well-formed). Non si può dimostrare a priori quanto
l'`ε` astratto si trasferisca al gioco fisico: i bound noti sono larghi e raffinare l'astrazione
può peggiorare l'exploitability reale (patologie note). Il claim onesto e verificabile è:

> `ε`-Nash misurata nel gioco fisico CO40 tramite best response esatta della strategia sollevata:
> `gain_CO`, `gain_BTN` in ante, normalizzati sul pot iniziale (3a) e sullo stack (40a).

La riduzione dell'`ε` misurato è la funzione obiettivo con cui scegliere capacità, feature e
algoritmo. Il confronto con Monker resta descrittivo.

### 3.7 Estensione multiway: cosa resta valido

| Regola | HU | Multiway |
|---|---|---|
| Reach per giocatore su combo fisiche, regret per `(nodo pubblico, bucket)` | sì | sì, un vettore di reach per giocatore |
| Astrazione carte per street precalcolata su feature esatte vs avversario uniforme | sì | sì, identica |
| Public chance sampling board-major | sì | sì |
| Showdown con somme prefisse `O(n log n)` | sì | **no**: con 3+ giocatori la decomposizione a coppie non è esatta per il card removal. Servono mani avversarie campionate (una per avversario) oppure enumerazione `O(n^2)` per il 3-way |
| Linear CFR / DCFR con update alternati | sì | sì (nessuna garanzia di equilibrio per `n > 2`; in pratica funziona) |
| Certificazione con BR esatta per giocatore | sì | computazionalmente sì; concettualmente è la somma dei deviation gain, non una prova di Nash |
| Albero delle puntate | 58 nodi preflop | cresce in modo esponenziale: l'action abstraction va ridotta prima di tutto |

Progettare da subito il trainer con `N` vettori di reach e un'interfaccia di showdown sostituibile
(esatta HU, campionata multiway) evita una riscrittura.

## 4. Stima dei costi

Hardware: i3-10100F, 4 core / 8 thread, 32 GiB. Sono stime da verificare con un prototipo; nessuna
è un benchmark eseguito.

| Voce | Stima |
|---|---:|
| Precalcolo feature + clustering (una tantum, indipendente dal gioco) | 10–30 min |
| Tabella all-in preflop `630×630` esatta (una tantum) | 10–20 min, ≈ 1,3 MB |
| Costo per board della traversata vettoriale | 0,03–0,1 s single-thread |
| Board per ora su 8 thread | `1–3·10^5` |
| Contributi per information set River per ora (capacità 512) | `≈ 10^5`, contro `≈ 30` nell'intero run V17 |
| Stato numerico `(11.308 nodi × bucket × 4 azioni × 16 B)` | 185 MB (256), 370 MB (512), 740 MB (1.024) |
| Mappe bucket su disco | ≈ 25 MB |
| BR esatta nel gioco fisico, passata completa, 8 thread | 1–2 h |
| BR campionata (2.000 board) | secondi |

## 5. Modifiche proposte

### 5.0 Cosa fermare

- nuovi run della serie V con external sampling a deal singolo e gate WMAE/TV Monker;
- estensione del `FiniteGame` a corpus (V23) a HU20/HU40 come percorso di qualificazione;
- certificatore root-by-root e board-batched per sottogioco River (V21/V22);
- decomposizione exact Flop/River (CFR-D) come motore di training.

Tutto il materiale resta come evidenza e oracle; HU10 a corpus resta il test di correttezza del
`FiniteGame`.

### 5.1 Modulo di astrazione esatta (`libs/preflop` o nuovo `libs/abstraction`)

- Enumerazione canonica delle osservazioni `(mano, board)` per street; indice combinatorio e
  canonicalizzazione dei semi condivisi.
- Feature esatte via `SevenCardLookupTable`: istogrammi Flop/Turn, OCHS River; costruzione
  multi-thread; file versionato con checksum e fingerprint.
- k-means (EMD / L2) con seed di partizione; mappa `osservazione → bucket` totale e
  deterministica; query `O(1)` a partire dal board canonico e dalla combo.
- Test: determinismo, invarianza ai semi, copertura totale, nessuna lettura di carte non visibili,
  equivalenza feature vs oracle scalare su campione, rifiuto di file corrotti.

### 5.2 Trainer vettoriale board-major (`hu_preflop_vector_trainer.cpp`)

- Board sampler canonico con molteplicità; vettori di reach su 630 combo; tabella
  `(nodo pubblico, bucket) → regret, strategy_sum` densa.
- Kernel showdown per rank con blocker: estrarre la parte neutra del kernel postflop in una utility
  condivisa (`libs/equity` o `libs/core`) senza toccare `ProductionDcfr`.
- Terminali all-in preflop da tabella esatta; all-in postflop su board campionato (non distorto).
- Update alternati, batch di board, riduzione deterministica, 1–8 worker.
- Test: sullo stesso gioco astratto piccolo, strategia media uguale (a meno dell'ordine di
  riduzione) all'oracolo denso `FiniteGame`; conservazione della massa di reach; bit-identità
  1/8 thread; Kuhn/Leduc già presenti nel laboratorio.

### 5.3 Regret minimizer intercambiabile

- Linear, DCFR `1,5/0/2`, CFR+ come schedule sulla stessa tabella; opzione pesi lineari solo in
  warm-up.
- Confronto a pari wall time su CO40 con la metrica di §3.5, tre seed, board comuni fra candidati.

### 5.4 Certificatore board-major

- BR esatta della strategia sollevata: iterazione sulle 369.072 board history canoniche con
  molteplicità, `max_a` per mano; output `gain_CO`, `gain_BTN`, EV, normalizzazioni; resume per
  intervalli di board.
- Versione campionata con intervallo per il monitoraggio.
- Test: sulla fixture Short Deck a quattro street di V21 coincide con `calculate_nash_conv` del
  gioco fisico entro `1e-10`; profile EV a somma zero; gain non negativi.

### 5.5 Nuovi gate e report

- Sostituire WMAE/TV come gate con: exploitability fisica (ante, `/pot`, `/stack`), EV CO con
  intervallo, stabilità fra seed espressa come EV-loss e non come TV.
- Monker: solo metrica EV-loss ponderata (già proposta in V20 §7), stato
  `EXTERNAL_CONTRACT_INCOMPLETE` permanente.

### 5.6 Metodologia

- Nessun gate con soglia inferiore a due volte la deviazione fra seed misurata; almeno tre seed;
  stesso budget di tempo; board comuni fra candidati.

### 5.7 Governance del confine preflop/postflop

- Rivedere il divieto di riuso dei kernel postflop per le utility neutre (rank prefix,
  canonicalizzazione, tabella 7 carte): estrarle in libreria condivisa con test su entrambi i
  consumatori. `ProductionDcfr`, schedule e storage postflop restano intatti.

### 5.8 Correzioni minori al trainer corrente (se resta come baseline)

- `average_strategy` con massa nulla: restituire uniforme e contare il caso, non la strategia
  corrente ([hu_preflop_solver.cpp:205](../../libs/preflop/src/hu_preflop_solver.cpp)).
- Default `sampling_algorithm` da `DiscountedMccfr1503` a `LinearMccfr`
  ([hu_preflop.hpp:1424](../../include/gtosd/preflop/hu_preflop.hpp)).
- River: equity esatta su 406 mani al posto di MC8, a costo quasi nullo.

## 6. Ordine di esecuzione proposto

1. 5.1 astrazione esatta + 5.4 certificatore su fixture ridotta (oracle V21) — correttezza prima
   di tutto.
2. 5.2 trainer vettoriale su HU10; confronto con il `FiniteGame` a corpus come oracle.
3. 5.3 scelta del regret minimizer a pari tempo su CO40; 5.4 passata esatta finale.
4. 5.5–5.6 report e gate; solo dopo, sweep di capacità e feature.

## 7. Domande aperte

1. **Gate.** Si accetta di sostituire il gate di frequenze Monker (`≤ 1–2 pp`) con exploitability
   fisica misurata più EV con intervallo? Le frequenze restano descrittive.
2. **Normalizzazione.** I documenti usano sia `1 %` del pot iniziale (`0,03a`, V23) sia `1 %`
   dello stack (`0,4a`, benchmark CO40): quale è il gate di prodotto? Proposta: report di entrambe,
   gate iniziale sullo stack.
3. **Risorse warm.** Una precomputazione una tantum (10–30 min, decine di MB, indipendente da
   stack e sizing) può stare fuori dal budget di 2 h del singolo solve?
4. **Confine postflop.** Si autorizza l'estrazione dei kernel neutri in libreria condivisa?
5. **Primo target.** HU10 per validare la pipeline in giornata, poi CO40? Oppure direttamente CO40?
6. **Multiway.** Quale formato (3-way, 6-max) e quale action abstraction? Decide se il kernel di
   showdown campionato serve nella prima versione.

## 8. Riferimenti

- Johanson, Bard, Lanctot, Gibson, Bowling, 2012 — Efficient Nash Equilibrium Approximation
  through Monte Carlo CFR (public chance sampling).
- Johanson, Burch, Valenzano, Bowling, 2013 — Evaluating State-Space Abstractions in
  Extensive-Form Games (distribution-aware, OCHS, imperfect recall).
- Ganzfried, Sandholm, 2014 — Potential-Aware Imperfect-Recall Abstraction with EMD.
- Johanson, Waugh, Bowling, Zinkevich, 2011 — Accelerating Best Response Calculation in Large
  Extensive Games (BR nel gioco reale di strategie astratte).
- Brown, Sandholm, 2019 — Solving Imperfect-Information Games via Discounted Regret Minimization
  (DCFR, Linear CFR, regime campionato).
- Waugh, Schnizlein, Bowling, Szafron, 2009 — Abstraction Pathologies in Extensive Games.
- Lanctot, Gibson, Burch, Zinkevich, Bowling, 2012 — No-Regret Learning in Extensive-Form Games
  with Imperfect Recall.
- Documenti interni storici: studio CPU/RAM, README R6, roadmap V20, fase V22 e
  report HU10 V23. Sono stati rimossi dal working tree il 2026-09-15 e restano
  nel tag `preflop-legacy-es-2026-09-15`; percorsi e comandi di recupero sono
  nell'[indice legacy](PREFLOP_LEGACY_INDEX.md).
  Questi documenti sono stati rimossi dal working tree il 2026-09-15 e restano leggibili al tag
  `preflop-legacy-es-2026-09-15` (`git show preflop-legacy-es-2026-09-15:docs/research/<percorso>`);
  vedi [PREFLOP_LEGACY_INDEX.md](PREFLOP_LEGACY_INDEX.md). Le citazioni di codice
  (`libs/preflop/...`) restano valide finché il legacy è in build.
