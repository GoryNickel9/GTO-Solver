# Riproduzione della ricetta MonkerSolver (2026-09-28)

Documento del programma avviato il 28 settembre: riprodurre il modo in cui MonkerSolver costruisce
le chart preflop, verificarlo contro le chart MonkerSolver che l'utente possiede, e usarlo come base
del solver multiway. Branch `feat/monker-step1-checkdown` (da `feat/preflop-phase1-time`), commit
`e576396` (passo 1) e `7ff711b` (passo 2).

## 1. Decisioni dell'utente del 28 settembre

| Tema | Decisione |
|---|---|
| Obiettivo del prodotto | Solver preflop short deck multiway, fino al 6-way |
| Macchina di produzione | 2 x Xeon Gold 6230R (52 core, 104 thread, due nodi NUMA, AVX-512), 256 GB ECC; test su macchine a noleggio prima dell'acquisto |
| Limiti di 8 GiB e 35 minuti | Valgono solo per la suite di benchmark HU10-HU40 sul PC di sviluppo, non sono regole del prodotto |
| Criterio di accettazione | Solo best response esatta dentro l'astrazione <= 0,03 a (1 % del piatto); il limite del 5 % sul certificato fisico è tolto (protocollo, commit `a0331d6`) |
| Direzione | Seguire la ricetta di MonkerSolver (passo 1 e passo 2); niente stile HRC e niente history7 per ora |
| Confronto | Chart MonkerSolver short deck dell'utente (`GTO-Chart-Browser/ranges/Short Deck/Symmetrical Chart`, HU e 3-way a 50a e altri stack); obiettivo chart simili, non identiche |
| Albero | Identico a quello delle chart MonkerSolver; postflop non noto, quindi bet e raise al 100 % del piatto, all-in sempre disponibile, niente donk bet (consiglio ufficiale di MonkerSolver). **Cambiato il 29 settembre**: la configurazione HU di riferimento usa i donk bet (5.7) |
| Classifica short deck | La scala batte il tris anche nel MonkerSolver delle chart (stessa classifica nostra); confermato il 30 settembre dagli EV del set 3-way a 60a (5.9) |
| Arresto del passo 2 | Distanza media sotto 0,01 fra due salvataggi consecutivi delle chart (ogni 4.000 iterazioni) |
| In pausa | Fasi 1 e 2 dei 35 minuti di HU40 sull'i3; la best response astratta veloce è progettata (piano del 28 mattina) ma non implementata |

## 2. Cosa fanno gli altri solver (ricerche del 28 settembre)

Tre ricerche con agenti e verifica avversaria sulle fonti primarie (33 affermazioni controllate, 10
corrette). Sintesi:

| Solver | Postflop dentro il preflop | Bucket | Cosa dichiara come convergenza |
|---|---|---|---|
| MonkerSolver | Albero sparso aggiunto al preflop con un filtro | 15-30 livelli di forza per board (guida ufficiale); flop e turn divisi anche in 4 livelli di potenziale (decodifica di terzi), 120 gruppi per flop; strategie separate per ogni flop, turn e river fusi in classi di "texture" con regola non pubblica | Nessuna exploitability: le FAQ dicono che non è calcolabile e che l'errore viene soprattutto dall'astrazione postflop (affermazione non misurata); regola pratica di iterazioni per nodo |
| HRC | Modelli postflop; quello avanzato a bucket | Gruppi globali su equity e realizzazione (famiglia Johanson 2013, come i nostri), 1.024/256/256 di default, fino a 16.384; memoria imperfetta dichiarata | Indicatore euristico di quanto cambia la strategia |
| Simple Preflop Holdem | Bucket per street | 10.000/10.000/10.000 nei pacchetti short deck HU (GTOSims), massimo 10.000/100.000/100.000 | "Moltissime iterazioni" |
| PioSOLVER Edge | Postflop esatto su un sottoinsieme di flop pesati | Nessuna astrazione delle carte | Exploitability esatta del gioco ridotto |
| GTO Wizard | Libreria preflop calcolata con MonkerSolver (30/30/30); GTO Wizard AI con reti neurali | | Numeri sul gioco vero solo per il postflop |

Esempio ufficiale MonkerSolver (Hold'em, forza 30, texture Perfect al flop e Large a turn e river):
210.600 righe per nodo al flop (1.755 flop x 120), 1.073.040 al turn (8.942 classi x 120), 110.310 al
river (3.677 classi x 30), 12,75 GB stimati. Il consiglio ufficiale per il multiway
(monkerware.com/trees.html): costruire il preflop con postflop vuoto (passo 1), poi aggiungere un
postflop sparso con un filtro (passo 2): una sola size relativamente grande, niente cold call quando
restano altri giocatori da agire (al turn arrivano al massimo in due), niente donk bet, via le
opzioni preflop poco importanti; poi un solo calcolo su tutto l'albero.

Stima del costo di una riscrittura completa del solver: 3-4 settimane (8-15 giorni di codice più 5-8
di validazione), senza evidenze di convergenza più rapida; nessuna tecnica nota dà più di 2-3 volte in
un contesto campionato e astratto come il nostro.

## 3. Dimensioni del multiway (misure del 28 settembre)

Alberi costruiti con le regole di azione di HU30/HU40 (open 5a, 3-bet 17a, una size postflop), posizioni
del registro delle decisioni D10. Memoria del trainer in float32 (8 byte per cella; double il doppio),
senza costi fissi; TiB = 1.024 GiB.

| Formato (30a / 40a) | Nodi | history7 | Tipo preflop + bucket | Bucket 500/1000/2000 | Bucket 4096/1024/1024 | Bucket 10.000 | Monker fine (turn e river per turn) |
|---|---|---|---|---|---|---|---|
| HU | 604 | 3,6 GiB | 0,1 GiB | trascurabile | trascurabile | trascurabile | 3 GiB |
| 3-way | 5.959 / 7.597 | 36 / 49 GiB | 1 / 2 GiB | 0,1 GiB | 0,1 GiB | 0,4 / 0,5 GiB | 31 / 39 GiB |
| 4-way | 25.705 / 50.497 | 147 / 323 GiB | 5 / 11 GiB | 0,2 / 0,5 GiB | 0,3 / 0,5 GiB | 1,8 / 3,5 GiB | 134 / 271 GiB |
| 5-way | 113.680 / 296.005 | 0,63 / 1,84 TiB | 23 / 64 GiB | 1,1 / 3,1 GiB | 1,2 / 3,0 GiB | 8 / 21 GiB | 0,58 / 1,59 TiB |
| 6-way | 470.647 / 1.515.037 | 2,56 / 9,49 TiB | 93 / 332 GiB | 4,5 / 16 GiB | 5 / 16 GiB | 32 / 108 GiB | 2,40 / 8,26 TiB |

Il costruttore dell'albero gestisce già 3-6 giocatori; trainer, best response, certificatore ed export
sono solo HU. Con le regole sparse di MonkerSolver (cold call, donk) l'albero multiway si riduce molto;
la riduzione per il nostro gioco va misurata quando il costruttore avrà quelle regole.

## 4. Passo 1: preflop con postflop vuoto (checkdown)

**Albero.** Configurazione `benchmarks/monker/HU50.json`: open 5a, 3-bet solo all-in, isolation sul
limp a 5a. Con l'all-in sempre disponibile (nuovo `ActionConfig::all_in_unconditional`, impostato dal
modello di gioco: la vecchia soglia del 1000 % del piatto toglieva l'open-shove e lo shove sul limp da
42a in su) l'albero ha esattamente gli 8 nodi di decisione delle chart MonkerSolver. Gli alberi della
suite HU10-HU40 restano identici (impronte invariate).

**Solver.** `CompileOptions::checkdown_at_flop` chiude ogni ingresso al flop con uno showdown sull'intero
runout; `gtosd_preflop_blueprint_checkdown` risolve l'albero (22 nodi) con DCFR vettoriale esatto, i
valori terminali dalla tabella esatta degli all-in preflop, best response esatta: nessuna astrazione
delle carte. 5.000 iterazioni in 75 s, distanza dall'equilibrio 3e-5 % del piatto.

**Confronto con le chart MonkerSolver HU50** (`tools/monker_compare/compare_charts.py`: distanza = metà
della somma delle differenze di frequenza per mano, pesata sulle combinazioni, solo sulle mani in range
da entrambe le parti):

| Nodo | Stessa azione principale | Distanza | Mani in entrambi i range |
|---|---:|---:|---:|
| CO alla radice | 53,0 % | 0,453 | 81 |
| BTN contro il limp | 68,6 % | 0,398 | 81 |
| BTN contro l'open a 5a | 44,1 % | 0,429 | 81 |
| BTN contro lo shove del CO | 98,1 % | 0,029 | 81 |
| CO contro l'isolation a 5a | 58,5 % | 0,406 | 31 |
| CO contro lo shove del BTN sul limp | 95,3 % | 0,041 | 31 |
| BTN contro lo shove del CO sull'isolation | 87,5 % | 0,079 | 6 |
| CO contro lo shove del BTN sull'open | non confrontabile (il nostro CO non apre mai a 5a) | | 0 |
| **Media sui 7 nodi confrontabili** | **72,2 %** | **0,262** | |

Frequenze alla radice (pesate sulle combinazioni): MonkerSolver all-in 33,1 %, open 0,5 %, limp 29,0 %,
fold 37,4 %; passo 1 all-in 29,0 %, open 0,0 %, limp 62,6 %, fold 8,3 %.

**Lettura.** Dove la decisione dipende solo dall'equity (call o fold contro un all-in) coincidiamo al
87,5-98 % (95-98 % sui due nodi con molte mani in range): stesse regole, stessa classifica, stessi calcoli sugli all-in. Dove conta il gioco postflop
(limp, isolation, call contro un raise) il passo 1 si discosta: senza postflop limpare e chiamare valgono
troppo. Il passo 1 da solo non basta, come nella ricetta di MonkerSolver.

## 5. Passo 2: postflop sparso con bucket per board

**Albero.** `benchmarks/monker/HU50_step2.json` = HU50 più `"postflop_donk_bets": false` (nuova chiave,
scritta nella configurazione solo quando è false, quindi le impronte esistenti non cambiano). Regola:
prima di qualsiasi bet su una street postflop, un giocatore non può puntare se l'ultimo aggressore del
giro precedente è ancora in mano, non è all-in e parla dopo di lui. Bet e raise al 100 % del piatto,
all-in sempre disponibile. 493 nodi (571 con i donk bet), 11 nodi con il solo check.

**Bucket per board** (`gtosd_preflop_blueprint_monker_buckets`, tabelle in `out/monker/buckets_30x4`,
costruite in 13 s): per ogni board canonico e ogni mano viva, equity al river contro una mano a caso
(conti interi, pareggi esatti), media E e varianza V sui runout (465 dal flop, 30 dal turn).
Livello di forza = quantile di E sul board (30 livelli, le mani con la stessa E nello stesso livello);
livello di potenziale = quartile di V sul board (4 livelli, sul board intero come nelle tabelle
decodificate di MonkerSolver); id = 4 x forza + potenziale su flop e turn (120), forza sul river (30).
Gruppi distinti per board (minimo / mediana / massimo): flop 41 / 64 / 93, turn 11 / 52 / 70, river
1 / 25 / 30. Controlli del tool: ogni mano ha un id, orbite di simmetria dei semi coerenti su tutti i
board, equity del river uguale alla feature esistente, 10.000 board fisici letti come nel contesto del
board, salvataggio e ricarica identici.

**Righe** (`BoardClassRows`, opzione `--board-class-rows` del trainer): riga = classe del board x gruppi
+ id; classi: ogni flop canonico (573), ogni flop+turn canonico (13.761); il river condivide la classe
del suo turn (regola nostra, perché quella di MonkerSolver non è pubblica). Righe per nodo 68.760 /
1.651.320 / 412.830; memoria imperfetta (né classe preflop né gruppi delle street precedenti).
325 milioni di celle, 4,84 GiB di tabelle in double (stato del trainer 5,10 GiB con i timestamp dello
sconto lazy, picco 5,2 GiB). Valutazione durante il training e certificato non supportati
ancora con queste righe (il trainer li rifiuta).

**Training** (`tools/monker_compare/run_step2.sh`): trainer attuale con le opzioni della suite (DCFR
1,5/0/2 alternato, batch 32, sconto lazy), blocchi da 4.000 iterazioni con ripresa dal checkpoint, chart
esportate a ogni blocco (`gtosd_preflop_blueprint_monker_charts`), confronto con il blocco precedente e
con MonkerSolver, arresto sotto 0,01. Costo misurato nel run: 0,065-0,079 s per iterazione (0,067 nella
prova breve, di cui 0,061 di attraversamento), salvataggio 24-30 s e ripresa circa 26 s per blocco:
blocchi da 5,4-7,0 minuti. Senza lo sconto lazy il costo era 0,75 s per iterazione (0,66 di sconto
dell'intera tabella). Prova breve a 500 iterazioni:
distanza da MonkerSolver 0,237, stessa azione principale 78 %.

**Risultato.** Run lanciato il 28 settembre alle 17:10 (`out/monker/step2/HU50`), **stabile a 24.000
iterazioni** (17:45, cambiamento 0,0095 < 0,01). Distanza da MonkerSolver per salvataggio: 0,161 (4.000),
0,105, 0,084, 0,076, 0,0727, **0,0725** (24.000); cambiamento sul blocco precedente 0,068, 0,031, 0,019,
0,0127, 0,0095. Differenza di range sulle combo effettive (5.1): **0,383** (passo 1: 0,729).

| Nodo | Distanza | Differenza di range | Combo effettive (MonkerSolver / nostre) |
|---|---:|---:|---:|
| CO alla radice | 0,145 | 0 | 630 / 630 |
| BTN contro il limp | 0,124 | 0 | 630 / 630 |
| BTN contro l'open a 5a | 0,079 | 0 | 630 / 630 |
| BTN contro lo shove del CO | 0,021 | 0 | 630 / 630 |
| CO contro l'isolation a 5a | 0,174 | 0,274 | 182,8 / 182,1 |
| CO contro lo shove del BTN sul limp | 0,020 | stesso range del nodo sopra | 182,8 / 182,1 |
| CO contro lo shove del BTN sull'open | 0,003 | 0,948 | 3,1 / 58,9 |
| BTN contro lo shove del CO sull'isolation | 0,014 | 0,327 | 152,6 / 156,1 |

Frequenze alla radice del CO (combo effettive su 630): MonkerSolver all-in 33,1 %, open 0,5 %, limp 29,0 %,
fold 37,4 %; passo 2 all-in 26,8 %, open 9,3 %, limp 28,9 %, fold 35,0 %. Il nostro CO usa l'open a 5a
soprattutto con AA (83,5 %) e JJ (43,8 %), in parte con KK (46,5 %), KJs (41 %) e KQs (27 %), e con mani
non premium (JTs 34,6 %, QJo 33,9 %, J9o 32,3 %, A7o 27 %); MonkerSolver limpa AA (93 %) e shova KQs
(99 %) e KK (75 %).

### 5.1 Misure di confronto (decisioni dell'utente del 28 settembre sera)

- **Distanza media**: per ogni mano metà della somma delle differenze assolute fra le frequenze delle
  azioni (la parte di strategia da spostare), media pesata sulle combinazioni delle mani in range da
  entrambe le parti, poi media semplice sugli 8 nodi. Esempio: AA alla radice, noi open 83,5 % / limp
  16,4 % / all-in 0,1 %, MonkerSolver open 6,6 % / limp 93,4 % -> 0,77. Pesare ogni mano anche per
  quanto spesso arriva al nodo cambia poco (0,069 contro 0,072): si tiene la pesatura per combinazioni.
- **Differenza di range**: misura quali mani arrivano al nodo, che la distanza non vede. Combo effettive
  di una mano = combinazioni × frequenza con cui arriva (prodotto delle azioni precedenti dello stesso
  giocatore, lette dalle chart dei nodi padre). Su tutto l'albero: 1 − Σ parte comune / Σ unione, in
  combo effettive, sui **range ristretti distinti** (il range del CO dopo il limp compare in due nodi e
  conta una volta). La prima versione era una media per nodo e contava due volte quel range (0,456 invece
  di 0,383): corretta su richiesta dell'utente ("Non deve calcolare la media, ma le combo effettive, sia
  per monker che per il nostro").
- **Stessa azione principale: abbandonata** (decisione dell'utente): nasconde le strategie miste (51/49
  contro 49/51 conta come diverso, 100/0 contro 51/49 come uguale) e con range diversi dà 100 % anche dove
  i range non si somigliano (CO contro lo shove dopo l'open: 3,1 contro 58,9 combo effettive).
- **Preferenza suited alla radice** (diagnostica): media sulle 36 coppie di ranghi diversi di (limp + open
  della suited) − (limp + open della offsuit). MonkerSolver 0,224, passo 2 0,399.
- **Soglia di rumore**: due run identici con seed diverso (5.2) danno chart distanti 0,007 / 0,034 e
  valori contro MonkerSolver che differiscono di 0,0004 / 0,006. Una variante conta se sposta la distanza
  da MonkerSolver di almeno circa 0,005 o la differenza di range di almeno circa 0,02. Sul gioco G1
  (astrazione compatta + donk bet) il rumore misurato il 29 settembre è di 0,0088 / 0,0425 fra le chart
  a 16.000 iterazioni (quello del passo 2 è a 24.000: la differenza può venire dalle iterazioni; una sola
  coppia di seed per gioco); è la soglia per lo spostamento fra le chart delle varianti di G1 (5.7). Sulla
  distanza da MonkerSolver i due seed di G1 differiscono di 0,0009 / 0,0026 e le soglie restano 0,005 /
  0,02.

### 5.2 Varianti del passo 2 (una modifica alla volta, 28 settembre sera)

Varianti con il runner continuo (`tools/monker_compare/run_step2_continuous.sh`, chart dal trainer vivo
ogni 4.000 iterazioni, arresto sotto 0,01), tutte stabili a 24.000 iterazioni. Il passo 2 è stato
calcolato con il runner a blocchi (`run_step2.sh`, salvataggio e ripresa a ogni blocco): i suoi 36
minuti non sono confrontabili (la stessa configurazione con il runner continuo, seed 2, ne ha presi 31).

| Run | Cosa cambia | Distanza da MonkerSolver | Differenza di range | Preferenza suited | Distanza / range dalle chart del passo 2 | Picco di memoria | Durata |
|---|---|---:|---:|---:|---:|---:|---:|
| Passo 2 | — | 0,0725 | 0,383 | 0,399 | — | 5,61 GB | 36 min |
| Seed 2 | solo il seed del campionamento dei board | 0,0720 | 0,388 | 0,399 | 0,007 / 0,034 (rumore) | 5,61 GB | 31 min |
| 15 livelli di forza | bucket 15 × 4 (livelli a 30 uniti a coppie) | 0,0723 | 0,378 | 0,364 | 0,009 / 0,055 | 2,86 GB | 28 min |
| Potenziale a 1 livello | bucket 30 × 1 | 0,0755 | 0,391 | 0,349 | 0,018 / 0,105 | 2,75 GB | 27 min |
| **Donk bet ammessi** | `HU50_step2_donk.json`, 571 nodi | **0,0667** | 0,376 | 0,372 | 0,018 / 0,136 | 6,62 GB | 33 min |
| Size 75 % | tolta dall'utente prima del run (1.036 nodi, circa 11,7 GB stimati) | — | — | — | — | — | — |

- **15 livelli**: chart vicine a quelle del passo 2 (0,009 / 0,055: sulla distanza entro il rumore, sul
  range un po' sopra), preferenza suited 0,399 -> 0,364; valori contro MonkerSolver ed EV (5.5) invariati;
  metà memoria; blocchi circa il 10 % più rapidi del seed 2 sullo stesso runner, dentro la variabilità dei
  tempi. Risparmio di memoria senza perdita misurabile.
- **Potenziale a 1 livello**: sposta le chart più del rumore (0,018 / 0,105) ma non verso MonkerSolver;
  riduce la preferenza suited, non abbastanza. Non è neutro sulle chart (in EV sì, 5.5).
- **Donk bet**: l'unica variante che avvicina le chart a MonkerSolver oltre il rumore (−0,0058), nei nodi
  del piatto limp-isolation-call dove senza donk il CO può solo fare check: CO contro l'isolation 0,174 ->
  0,141, BTN contro il limp 0,124 -> 0,111. L'analisi preventiva con tre analisti (5.3) prevedeva uno
  spostamento lontano da MonkerSolver. Indizio che le chart dell'utente siano state calcolate con i donk
  bet, contro la ricetta ufficiale.

### 5.3 Dove stanno le differenze (analisi con tre analisti indipendenti e un arbitro)

- Le mani **offsuit coincidono** con MonkerSolver entro 0,03 dopo il limp (CO contro l'isolation
  fold/call/all-in 0,54/0,42/0,04 contro 0,54/0,44/0,03; BTN contro il limp) ma non del tutto alla radice
  (fold/limp/open/all-in 0,47/0,19/0,00/0,33 contro 0,46/0,16/0,08/0,31: il nostro CO apre anche offsuit).
  Le differenze maggiori sono nelle **suited e nelle coppie** del CO (radice: suited limp/open/all-in
  42/0/36 % in MonkerSolver contro 55/8/20 % nostro; coppie open 2 % contro 27 %, all-in 22 % contro 9 %).
  Ipotesi dell'analisi, non misurata: il nostro postflop dà più valore alle suited di chi è fuori
  posizione nei piatti da 12a.
- **Esclusi dai dati**: "tris batte scala" (MonkerSolver chiamerebbe 99, che folda) e button blind da 2a
  (chiamerebbe KJo, JTo, 99). **Rake: sfavorita, non esclusa** (le soglie degli all-in di MonkerSolver
  ammettono da 0 a circa 1-2 a di rake su 100 a di piatto). **Correzione del 30 settembre (5.9)**: il rake
  non è più sfavorito. L'utente (29/09 alle 23:13) dice che è "molto probabile" che le chart siano state
  calcolate con rake 5 %, cap 3 ante, no flop no drop (il 30/09 alle 01:40 precisa: 5 % sul postflop, sul
  preflop solo per gli all-in, fino a 3a; alle 02:21 "abbastanza sicuro, non certo"); gli EV del set 3-way a 60a
  si spiegano con un rake piatto di circa 2 a sugli all-in, e sul set 3-way a 50a una stima indiretta dà
  circa 0,7 a. Sui piatti all-in da circa 100 a l'ipotesi 5 % / cap 3a toglie 3 a, sopra la fascia 0-2 a
  ammessa qui; decidono i test 6, 6b e 6c (5.9). **Esito del 30 settembre mattina (5.10)**: con tutto il
  preflop di MonkerSolver bloccato nessuna delle quattro ipotesi provate (5 % con cap 3a, 2a o 0,75a; 2,5 % con
  cap 2a) chiude lo scarto (guadagno del CO 0,92-1,13 % del piatto contro 1,44 % senza rake); nei nodi di call
  contro un all-in, che dipendono solo da equity e rake, il cap di 0,75a è l'unico senza deviazioni misurabili.
- **Più iterazioni, stima**: estrapolando la convergenza da due salvataggi (a + b/t su 12.000 e 24.000)
  le chart si muovono ancora di circa 0,027 e la distanza sale leggermente (0,079): peggiorano CO contro
  l'isolation (0,174 -> 0,202), BTN contro il limp (0,124 -> 0,150) e BTN contro l'open (0,079 -> 0,092),
  migliorano di poco i nodi contro gli all-in. **Correzione del 29 settembre (5.7)**: misurato su G1 con
  seed 2 raddoppiando le iterazioni (16.000 -> 32.000), le chart si muovono ancora (0,0155 / 0,0838) ma la
  distanza da MonkerSolver scende (0,0650 -> 0,0631) invece di salire; la stima era su un altro gioco (il
  passo 2) e su due soli salvataggi.
- **Leve residue, misurate dopo**: la variante dell'albero provata (donk bet, 0,018 / 0,136) ha spostato
  le chart quanto quelle dell'astrazione (potenziale a 1 livello 0,018 / 0,105, texture TX2 0,020 /
  0,115); solo i 15 livelli restano vicini al rumore. La seconda size (50 % + 100 %, 2.429 decisioni
  postflop, circa 16 volte memoria e tempo) è secondo l'analisi la leva probabilmente più grande, ma non
  è misurata e la direzione è incerta; richiede una macchina con circa 80 GB (server affittato o la
  macchina di produzione). **Correzione del 29 settembre (5.6, 5.7)**: gli 80 GB erano una stima per
  l'astrazione del passo 2 (70,7 GB con il calcolo esatto dal layout); con l'astrazione compatta e i donk
  bet il gioco G4 è girato sul PC di sviluppo (picco 15,67 GB). Misurata, la seconda size è, alla pari con
  i donk bet (G0c contro G1: 0,0212 / 0,1342), la leva più grande sulle chart fra quelle provate (0,0219 /
  0,1285 da G1) ma sposta **lontano** da MonkerSolver (distanza 0,0641 -> 0,0663).
- **Affidabilità dell'analisi**: delle tre previsioni poi verificate due sono risultate sbagliate (donk
  bet: prevista lontano da MonkerSolver, misurata verso; potenziale a 1 livello: indicata come la più
  mirata verso MonkerSolver, misurata lontano) e una giusta sull'entità (seconda size: la leva più grande,
  alla pari con i donk bet, 5.7; la direzione non era prevista). Le altre valutazioni di questa sezione
  vanno lette come ipotesi.

### 5.4 Le chart di MonkerSolver giocate nel nostro gioco

Domanda: quanti ante perde un giocatore che usa le chart preflop di MonkerSolver invece delle nostre,
con tutto il resto fermo alla nostra strategia (il nostro postflop per entrambi, il preflop
dell'avversario)? Valutazione **esatta** su tutti i 573 flop canonici (605.088 board, 4 minuti, 8 thread,
`gtosd_preflop_blueprint_monker_values --all-flops`, poi `tools/monker_compare/monker_in_our_game.py`):
valori controfattuali di ogni azione preflop per combo, ricorsione esatta sui nodi del giocatore
(i valori controfattuali si sommano), controlli di identità alla radice, somma degli EV nulla.

| Gioco | Giocatore | EV nostre chart | EV chart MonkerSolver | **Perdita di MonkerSolver** | Nostro scarto dalla migliore risposta preflop |
|---|---|---:|---:|---:|---:|
| Passo 2 | CO | −0,20887 | −0,20909 | **0,00022 a (0,007 % del piatto)** | 0,0058 a (0,19 %) |
| Passo 2 | BTN | +0,20887 | +0,20797 | **0,00090 a (0,030 %)** | 0,0038 a (0,13 %) |
| Donk bet | CO | −0,20876 | −0,20831 | −0,00045 a (MonkerSolver meglio) | 0,0070 a (0,23 %) |
| Donk bet | BTN | +0,20876 | +0,20813 | 0,00063 a (0,021 %) | 0,0029 a (0,10 %) |
| Turn in texture TX2 (5.5) | CO | −0,20818 | −0,20973 | 0,00155 a (0,052 %) | 0,0038 a (0,13 %) |
| Turn in texture TX2 (5.5) | BTN | +0,20818 | +0,20755 | 0,00063 a (0,021 %) | 0,0016 a (0,05 %) |
| 15 livelli + TX2 (5.5) | CO | −0,20814 | −0,20980 | 0,00166 a (0,055 %) | 0,0035 a (0,12 %) |
| 15 livelli + TX2 (5.5) | BTN | +0,20814 | +0,20745 | 0,00069 a (0,023 %) | 0,0015 a (0,05 %) |

**Conclusione.** Nel nostro gioco le chart di MonkerSolver valgono quanto le nostre: nel gioco del passo 2
perdono da 33 a 138 volte meno della soglia dell'1 % del piatto (0,03 a), in tutti i giochi della tabella
almeno 18 volte meno, e meno del nostro stesso scarto dalla migliore risposta preflop. La perdita netta
somma guadagni e perdite per classe (CO: +0,0035 e −0,0033 a); anche la sola parte positiva resta piccola
(0,12 % del piatto). Nel nostro gioco le chart di MonkerSolver sono quindi quasi una migliore risposta
quanto le nostre: le differenze fra le chart (distanza 0,07) stanno fra azioni di valore quasi uguale. La
misura va in un solo verso: non prova le nostre chart nel gioco di MonkerSolver, che non abbiamo. La policy del passo 2, cancellata dal vecchio runner, è stata
riesportata da una copia del checkpoint con l'impronta originale (`fnv1a64:ff9741d496d73bc5`).

**Precisazione del 30 settembre (test 5, 5.9).** Questa equivalenza è locale: le chart di MonkerSolver sono
giocate contro la nostra strategia (il nostro preflop e un postflop addestrato su di esso). Quando tutto il
preflop dei due giocatori è bloccato alle chart di MonkerSolver e il postflop si addestra contro quei range,
una migliore risposta preflop guadagna l'1,44 % del piatto al CO e lo 0,54 % al BTN (stabile da 24.000 a
64.000 iterazioni del postflop): il preflop di MonkerSolver, con il postflop che il nostro CFR impara contro i
suoi range, non è un equilibrio del nostro gioco. Non prova che nessun postflop lo renda un equilibrio: metà
del guadagno del CO sta nell'open a 5a, una linea rara nelle chart (5.9). Il rake (test 6-6d) abbassa il guadagno
del CO allo 0,92-1,13 % del piatto, un postflop più fine (test 5M-5D) lo alza all'1,60-1,80 % (5.10).

### 5.5 Turn fusi in classi di texture (notte del 28-29 settembre)

Regole progettate sui 13.761 flop+turn canonici (mappa `gtosd-board-texture-v1`, opzione
`--board-texture-map`, commit 6de08ae; mappe e generatore in `benchmarks/monker/textures/`): la classe del
turn diventa una texture (texture del flop × relazione di rango del turn × colore possibile × effetto
sulle scale); il river segue il turn; i bucket non cambiano. TX2 (consigliata): 4.482 classi di turn
invece di 13.761, 108 milioni di celle invece di 325 (stato 1,8 GB invece di 5,5); TX1 (riserva) 6.768
classi. Esempio K♠9♥6♦: 33 turn -> 8 classi (A | K | Q | J | T | 9 | 8+7 | 6). Con la mappa identità
il trainer è identico bit per bit a oggi.

Risultati su HU50 (stesso runner, arresto sotto 0,01; valori finali):

| Run | Classi di turn | Iterazioni | Distanza da MonkerSolver | Differenza di range | Preferenza suited | Distanza / range dalle chart del passo 2 | Picco di memoria |
|---|---:|---:|---:|---:|---:|---:|---:|
| Passo 2 | 13.761 | 24.000 | 0,0725 | 0,383 | 0,399 | — | 5,61 GB |
| TX1 (riserva) | 6.768 | 20.000 | 0,0710 | 0,367 | 0,372 | 0,015 / 0,083 | 2,86 GB |
| TX2 | 4.482 | 20.000 | 0,0710 | 0,360 | 0,386 | 0,020 / 0,115 | 1,96 GB |
| **15 livelli + TX2** (astrazione compatta) | 4.482 | 20.000 | **0,0706** | 0,3625 | 0,353 | 0,023 / 0,125 (dal TX2: 0,008 / 0,048) | **1,03 GB** |

- **TX1 e TX2 danno chart vicine** (distanti fra loro 0,007 / 0,047: sulla distanza come due seed, sul
  range un po' sopra il rumore). Il TX1, meno aggressivo, si sposta un po' meno dal passo 2 (0,015 / 0,083
  contro 0,020 / 0,115; asintoti 0,019 / 0,10 contro 0,025 / 0,14): lo spostamento viene in gran parte
  dalla fusione dei turn in sé, in parte dall'aggressività. A effetto simile TX2 risparmia di più.
- **Sulle chart non è neutra**: confronto finale contro finale (20.000 contro 24.000 iterazioni, come
  prescritto dal piano), TX2 supera la soglia (0,018 / 0,105) e TX1 è nella zona grigia; a iterazioni
  uguali (20.000) anche il TX1 la supera (0,0215 / 0,121, seed 0,0064 / 0,035). Gli asintoti stimati (a +
  b/t su due salvataggi) confermano uno spostamento reale (TX2 0,025 / 0,14 contro 0,0125 / 0,060 fra i
  seed). Lo spostamento va leggermente **verso MonkerSolver** (asintoti 0,0714 contro 0,0788), che fonde
  anch'esso turn e river.
- **In EV è neutra**: le chart TX2 giocate nel gioco del passo 2 perdono −0,00065 a (CO) e −0,00017 a (BTN),
  cioè fanno appena meglio delle chart del passo 2 (probabilmente perché più convergenti: nei loro giochi lo
  scarto dalla migliore risposta preflop è minore, 0,0038 / 0,0016 a contro 0,0058 / 0,0038); le chart di
  MonkerSolver nel gioco TX2 perdono 0,0016 a (0,052 % del piatto) e 0,0006 a.
- **Tempo**: le chart convergono prima (distanza da MonkerSolver 0,073 già a 8.000 iterazioni) ma il tempo
  per iterazione resta quello del passo 2 (0,071 s sull'intero run, uguale al passo 2; il compatto 0,064):
  il guadagno è di memoria.

- **Astrazione compatta (15 livelli + TX2)**: aggiungere i 15 livelli al TX2 sposta poco le chart (dal
  TX2 0,008 / 0,048: distanza al livello del rumore, range un po' sopra, come i 15 livelli da soli);
  memoria 5,4 volte più piccola del passo 2 (1,03 GB); distanza da MonkerSolver
  0,0706; le sue chart nel gioco del passo 2 perdono −0,00082 / −0,00028 a (appena meglio); le chart di
  MonkerSolver nel gioco compatto perdono 0,0017 a (0,055 % del piatto, CO) e 0,0007 a (BTN).

Chart di tutte le varianti giocate nel gioco del passo 2 (esatto, 573 flop; CO / BTN, negativo = meglio
delle chart del passo 2): seed 2 −0,00002 / −0,00000 a, 15 livelli −0,00037 / −0,00015, potenziale a 1
livello +0,00014 / −0,00001, TX1 −0,00046 / −0,00017, TX2 −0,00065 / −0,00017, 15 livelli + TX2 −0,00082 /
−0,00028, donk bet (chart di un gioco diverso) +0,00036 / +0,00044: tutte equivalenti in EV entro lo
0,03 % del piatto. Con questa misura, nel gioco del passo 2, le chart del passo 2 e di tutte le varianti
sono intercambiabili; la distanza fra chart resta utile per vedere dove si spostano, meno per giudicarle.

### 5.6 Giochi sull'astrazione compatta (notte del 29 settembre)

Decisioni dell'utente: all-in postflop solo fino a 5 volte il piatto (opzione
`postflop_all_in_max_pot_basis_points`, commit 410a380: spariscono 6 all-in, tutti nelle linee limp-check
del piatto da 4a con 48a dietro; il preflop resta identico), tabelle in double, criterio per scegliere il
gioco di G4: il più vicino a MonkerSolver sulla distanza, a parità di EV. **Decisione sull'all-in revocata
il 29 settembre (5.7)**: la configurazione di riferimento tiene l'all-in postflop senza limite; l'opzione
resta disponibile. Tutti i giochi usano l'astrazione compatta (15 livelli + TX2), arresto sotto 0,01, e
sono valutati con MonkerSolver giocato nel gioco di ciascun run.

| Gioco | Configurazione | Iterazioni | Distanza da MonkerSolver | Differenza di range | Preferenza suited | Dalle chart G0c | Perdita di MonkerSolver nel gioco del run (CO / BTN) | Picco |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| G0c | `HU50_step2.json` | 20.000 | 0,0706 | 0,3625 | 0,353 | — | 0,0017 / 0,0007 a | 1,03 GB |
| **G1** | `HU50_step2_donk.json` | 16.000 | **0,0641** | **0,350** | 0,365 | 0,021 / 0,134 | 0,0011 / 0,0005 a | 1,20 GB |
| G2 | `HU50_step2_allin5x.json` | 16.000 | 0,0750 | 0,383 | 0,381 | 0,013 / 0,073 | 0,0019 / 0,0006 a | 1,00 GB |
| G3 | `HU50_step2_donk_allin5x.json` | 16.000 | 0,0658 | 0,360 | 0,381 | 0,023 / 0,138 | 0,0020 / 0,0004 a | 1,17 GB |

- **I donk bet avvicinano a MonkerSolver anche sull'astrazione compatta** (G1 −0,0065 da G0c); sommati
  alla compatta danno il risultato migliore finora (0,0641 / 0,350).
- **L'all-in fino a 5 volte il piatto allontana un po'** (G2 +0,004 da G0c, G3 +0,0017 da G1), ai limiti
  del rumore; in EV tutti i giochi restano equivalenti (perdita di MonkerSolver sotto lo 0,07 % del
  piatto).
- **Scelta per G4**: G1 (compatta + donk) + due size (bet e raise al 50 % e al 100 % del piatto),
  `benchmarks/monker/HU50_step2_2size_donk.json`: 8.599 nodi, 3.220 decisioni postflop, 15,6 GB in double
  (calcolo esatto dal layout, verificato sui run precedenti; due size senza donk bet sull'astrazione del
  passo 2: 70,7 GB, non gli 80 stimati il 28; prova su 2 size senza donk in float32: 6,29 GB misurati, 0,44 s
  per iterazione). Con 12,5 GiB liberi su 32 (llama-server dell'utente 5 GB) il run
  finirebbe nel file di paging: lanciato solo dopo la chiusura di llama-server.

### 5.7 Test del 29 settembre (dalle 10 alle 18:38): G4, E, F, A, B, D

Tutti i test partono da G1 (astrazione compatta 15 livelli × 4 + TX2 + donk bet, `HU50_step2_donk.json`)
e cambiano una cosa sola; stesso runner, arresto sotto 0,01, MonkerSolver giocato nel gioco di ciascun run
(esatto, 573 flop). Eseguibili: `out/monker/bin_allin` per G4, E ed F, `out/monker/bin_abd` (commit
`5578ab8`) per A, B e D. Orari: G4 dalle 10:02 (arresto alle 13:29, valutazione esatta fino alle 15:02),
E 15:03-15:36, F 15:36-16:27, A 16:28-17:10 (con la curva di convergenza), B 17:10-17:31, D 17:32-18:02,
A fino a 32.000 iterazioni 18:02-18:38.

| Run | Cosa cambia rispetto a G1 | Iterazioni | Distanza da MonkerSolver | Differenza di range | Preferenza suited | Dalle chart di G1 | Perdita di MonkerSolver nel gioco del run (CO / BTN) | Nostro scarto dalla migliore risposta preflop (CO / BTN) | Picco di memoria | Secondi per iterazione |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| **G1** (riferimento) | — | 16.000 | 0,0641 | 0,350 | 0,365 | — | 0,00114 / 0,00046 a | 0,0042 / 0,0018 a | 1,20 GB | 0,105 (run della notte; A 0,074) |
| A | seed 2 | 16.000 | 0,0650 | 0,352 | 0,352 | 0,0088 / 0,0425 (rumore) | 0,00122 / 0,00046 a | 0,0047 / 0,0017 a | 1,20 GB | 0,074 |
| A continuato | seed 2, ripreso fino a 32.000 senza arresto | 32.000 | 0,0631 | 0,337 | 0,337 | 0,0163 / 0,0840 | 0,0027 / 0,0003 a (curva) | 0,0028 / 0,0012 a (curva) | 1,20 GB | 0,068 |
| E | bucket 30 × 8 (240 id al flop e al turn, 30 al river) | 20.000 | 0,0625 | 0,339 | 0,394 | 0,0111 / 0,0626 | 0,00145 / 0,00026 a | 0,0041 / 0,0018 a | 3,82 GB | 0,074 |
| F | bucket 60 × 8 (480 id al flop e al turn, 60 al river) | 20.000 | 0,0625 | 0,3395 | 0,395 | 0,0136 / 0,0694 | 0,00106 / 0,00025 a | 0,0044 / 0,0020 a | 7,53 GB | 0,105 |
| D | flop esatto (un id per orbita dei semi, capacità 528), turn e river come G1 | 20.000 | 0,0621 | 0,342 | 0,387 | 0,0109 / 0,0551 | 0,00140 / 0,00035 a | 0,0040 / 0,0017 a | 1,54 GB | 0,076 |
| G4 | bet e raise al 50 % e al 100 % del piatto (8.599 nodi) | 20.000 | 0,0663 | 0,373 | 0,432 | 0,0219 / 0,1285 | 0,00097 / 0,00067 a | 0,0050 / 0,0018 a | 15,67 GB | 0,617 |

La perdita di MonkerSolver e lo scarto di A sono misurati sulla policy di fine run (16.190 iterazioni);
quelli di "A continuato" vengono dalla curva di convergenza (policy del salvataggio a 32.000). Nei run
fermati a 0,01 MonkerSolver perde al massimo lo 0,048 % del piatto (E, CO), in A a 32.000 lo 0,09 %: in EV
tutti i giochi restano equivalenti.

**G4 (due size): G1 resta la configurazione di riferimento.** Stabile a 20.000 iterazioni (cambiamento
0,0085). Alla pari con i donk bet (G0c contro G1: 0,0212 / 0,1342), la seconda size è la leva più grande
sulle chart fra quelle provate (0,0219 / 0,1285 da G1, 2,5 / 3 volte il rumore del seed; da G0c 0,0312 /
0,172), come previsto in 5.3 sull'entità, ma sposta **lontano** da MonkerSolver: distanza 0,0641 ->
0,0663, range 0,350 -> 0,373, preferenza suited 0,365 -> 0,432 (MonkerSolver 0,224); alla radice il CO apre
a 5a ancora di più (9,4 %). In EV resta equivalente: MonkerSolver nel gioco G4 perde 0,00097 a
(0,032 % del piatto, CO) e 0,00067 a (0,022 %, BTN); il nostro scarto dalla migliore risposta preflop è
0,00500 / 0,00182 a. Costo: picco 15,67 GB (la stima dal layout, 15,6 GB, era giusta), 0,617 s per
iterazione (8,4 volte A, cioè G1 a una size: 0,617 contro 0,0736 s; 5,9 volte il run di G1 della notte,
0,105 s), 3 ore e 26 minuti di training (12.349 s) e 5.067 s (84 minuti) di valutazione esatta, contro
4-6 minuti nei giochi a una size.

**Rumore del seed su G1 (A).** A è G1 con `--seed 2`, stabile a 16.000 iterazioni come G1: distanza da
MonkerSolver 0,0650 contro 0,0641, range 0,3522 contro 0,3496, preferenza suited 0,352 contro 0,365;
MonkerSolver nel gioco A perde 0,00122 / 0,00046 a (in G1 0,00114 / 0,00046). Le chart di A e di G1 distano
**0,0088 / 0,0425** a 16.000 iterazioni, più del rumore del passo 2 (0,007 / 0,034 a 24.000, 5.1: la
differenza può venire dalle iterazioni). Le varianti fermate a 20.000 (D, E, F, G4, G0c) contengono anche
4.000 iterazioni in più di G1: G1 a 16.000 contro A a 20.000 dà 0,0099 / 0,0497 (1,1 / 1,2 volte il
rumore), riferimento per il loro spostamento da G1. L'ultima colonna confronta le chart a iterazioni
uguali (16.000 per entrambe). Spostamento delle chart di ogni variante, da G1 e da A (a 16.000):

| Variante | Da G1 | Da A | Multipli del rumore (da G1) | Da G1, 16.000 iterazioni per entrambe |
|---|---:|---:|---:|---:|
| A a 20.000 iterazioni (seed + 4.000 iterazioni) | 0,0099 / 0,0497 | 0,0067 / 0,0385 | 1,1 / 1,2 | — |
| D (flop esatto) | 0,0109 / 0,0551 | 0,0120 / 0,0699 | 1,2 / 1,3 | 0,0115 / 0,0606 |
| E (30 × 8) | 0,0111 / 0,0626 | 0,0118 / 0,0775 | 1,3 / 1,5 | 0,0106 / 0,0646 |
| G3 (donk + all-in fino a 5 volte il piatto) | 0,0135 / 0,0717 | 0,0150 / 0,0823 | 1,5 / 1,7 | 0,0135 / 0,0717 |
| F (60 × 8) | 0,0136 / 0,0694 | 0,0140 / 0,0814 | 1,5 / 1,6 | 0,0139 / 0,0765 |
| A a 32.000 iterazioni | 0,0163 / 0,0840 | 0,0155 / 0,0838 | 1,9 / 2,0 | — |
| G0c (senza donk bet) | 0,0212 / 0,1342 | 0,0239 / 0,1371 | 2,4 / 3,2 | 0,0203 / 0,1429 |
| G4 (due size) | 0,0219 / 0,1285 | 0,0229 / 0,1306 | 2,5 / 3,0 | 0,0259 / 0,1664 |

Le raffinature dell'astrazione delle carte (D, E, F) spostano le chart di 1,2-1,6 volte il rumore, poco
sopra il riferimento con 4.000 iterazioni in più (1,1 / 1,2); il limite all'all-in (G3, anch'esso un cambio
dell'albero) di 1,5 / 1,7; i donk bet (G0c) e la seconda size (G4) di 2,4-3,2 volte (2,3-3,9 volte a 16.000
iterazioni per entrambe); il raddoppio delle iterazioni di circa 2 volte.

**Bucket più fini (E, F).** 30 × 8 (`out/monker/buckets_30x8`) e 60 × 8 (`out/monker/buckets_60x8`) invece
di 15 × 4 (60 id al flop e al turn, 15 al river), stabili a 20.000 iterazioni. Distanza da MonkerSolver
0,0625 per entrambi (G1 0,0641: −0,0016, poco più della differenza fra i due seed, 0,0009), range 0,339 /
0,3395, preferenza suited peggiore (0,394 / 0,395 contro 0,365). **F coincide con E** (0,0043 / 0,0245, sotto
il rumore): oltre 30 × 8 i bucket non cambiano più le chart. In EV equivalenti (MonkerSolver perde 0,00145 /
0,00026 a in E, 0,00106 / 0,00025 a in F; scarto dalla migliore risposta preflop 0,00409 / 0,00183 e
0,00440 / 0,00199 a). Memoria: stato 3,70 e 7,40 GB, picco 3,82 e 7,53 GB (3,2 e 6,3 volte G1).
Conclusione: il numero di bucket non è la leva; l'astrazione compatta resta.

**Flop esatto (D).** Bucket del flop con un id per ogni orbita dei semi delle combo vive (`--flop-exact`,
`out/monker/buckets_flopexact_15x4`: da 150 a 528 id per flop, mediana 321, contro 24-53 dei 15 × 4), turn e
river identici a G1 (stesse impronte delle tabelle). Stabile a 20.000 iterazioni: distanza 0,0621, range
0,342, preferenza suited 0,387; dalle chart di G1 0,0109 / 0,0551 (1,2 / 1,3 volte il rumore), da E 0,0072 /
0,0443. MonkerSolver nel gioco D perde 0,00140 / 0,00035 a; scarto dalla migliore risposta preflop 0,00399 /
0,00173 a. Stato 1,41 GB (+30 % su G1, 1,08 GB), picco 1,54 GB; 0,076 s per iterazione, circa il 3 % più di
A sullo stesso tratto (1.212 contro 1.177 s per le prime 16.000 iterazioni). Il flop esatto non avvicina le
chart a MonkerSolver oltre il rumore.

**Curva di convergenza (A).** Con la policy salvata a ogni salvataggio delle chart (`--policy-snapshots`),
`tools/monker_compare/convergence_curve.py` valuta ogni salvataggio con la migliore risposta esatta a carte
vere sullo stesso albero delle azioni (573 flop, circa 4 minuti per salvataggio; piatto iniziale 3 a).
Dopo l'arresto a 16.000 A è stato ripreso dal checkpoint di fine run (16.190 iterazioni) con soglia 0 fino
a 32.000 (18:02-18:21; curva completata alle 18:38). NashConv = somma dei guadagni delle migliori risposte
complete dei due giocatori; "solo preflop" = migliore risposta sul solo preflop con il nostro postflop
fermo; "solo postflop" = migliore risposta sul solo postflop con il nostro preflop fermo.

| Iterazioni | NashConv | % del piatto | Solo preflop (CO / BTN) | Solo postflop (CO / BTN) | Distanza da MonkerSolver | Differenza di range | Perdita di MonkerSolver (CO / BTN) | Cambiamento sul salvataggio precedente |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 4.000 | 1,903 a | 63,4 | 0,0120 / 0,0054 | 0,460 / 0,394 | 0,0908 | 0,503 | −0,0038 / −0,0003 | — |
| 8.000 | 1,225 a | 40,8 | 0,0091 / 0,0029 | 0,327 / 0,275 | 0,0682 | 0,385 | −0,0022 / 0,0004 | 0,0351 |
| 12.000 | 1,001 a | 33,4 | 0,0062 / 0,0021 | 0,282 / 0,239 | 0,0637 | 0,354 | 0,0004 / 0,0005 | 0,0155 |
| 16.000 | 0,899 a | 30,0 | 0,0048 / 0,0017 | 0,262 / 0,223 | 0,0650 | 0,352 | 0,0012 / 0,0005 | 0,0092 |
| 20.000 | 0,844 a | 28,1 | 0,0044 / 0,0016 | 0,252 / 0,215 | 0,0644 | 0,347 | 0,0018 / 0,0004 | 0,0067 |
| 24.000 | 0,810 a | 27,0 | 0,0032 / 0,0014 | 0,245 / 0,210 | 0,0640 | 0,345 | 0,0022 / 0,0004 | 0,0053 |
| 28.000 | 0,790 a | 26,3 | 0,0031 / 0,0013 | 0,240 / 0,206 | 0,0634 | 0,341 | 0,0025 / 0,0004 | 0,0044 |
| 32.000 | 0,777 a | 25,9 | 0,0028 / 0,0012 | 0,236 / 0,204 | 0,0631 | 0,337 | 0,0027 / 0,0003 | 0,0033 |

- **Il preflop converge**: il guadagno solo preflop scende da 0,0120 / 0,0054 a (4.000) a 0,0048 / 0,0017 a
  (16.000) e 0,0028 / 0,0012 a (32.000, 0,09 % / 0,04 % del piatto).
- **Il postflop scende ancora**: solo postflop 0,262 / 0,223 a a 16.000, 0,236 / 0,204 a a 32.000; NashConv
  0,899 -> 0,777 a. È la NashConv del gioco a carte vere, quindi comprende l'errore dell'astrazione e non va
  a zero. Stime a + b/t del pavimento: 0,59 dalla coppia 12.000-16.000 (0,57 da 8.000-16.000), 0,63-0,68
  dalle coppie fra 16.000 e 32.000 (0,655-0,685 da quelle che finiscono a 32.000); le stime salgono con i
  punti più tardi (la curva si appiattisce più in fretta di 1/t), quindi sono probabilmente stime per
  difetto: il pavimento è probabilmente sopra 0,685 e la NashConv a 32.000 (0,777) gli sta sopra di meno
  del 13,5 % (sopra le stime: dal 13,5 al 24 %).
- **Le chart si muovono ancora**: cambiamento per blocco di 4.000 iterazioni dopo 16.000 0,0067, 0,0053,
  0,0044, 0,0033; A a 32.000 contro A a 16.000 0,0155 / 0,0838 (1,8-2 volte il rumore del seed), in lenta
  deriva verso MonkerSolver (distanza 0,0650 -> 0,0631, range 0,352 -> 0,337, preferenza suited 0,352 ->
  0,337); alla radice l'open a 5a del CO scende da 7,7 % a 6,7 %.
- **La perdita di MonkerSolver dipende dalla convergenza del nostro gioco**: prima di 8.000-12.000 iterazioni
  le chart di MonkerSolver fanno meglio delle nostre (CO −0,0038 a a 4.000, −0,0022 a a 8.000); poi la
  perdita del CO cresce fino a 0,0027 a a 32.000 (0,09 % del piatto, 11 volte sotto la soglia di 0,03 a,
  appena sotto il nostro guadagno solo preflop, 0,0028); quella del BTN resta fra −0,0003 e 0,0005 a.
- **Regola di arresto**: la soglia 0,01 basta per il preflop (a 16.000 il guadagno solo preflop è già 0,0048 /
  0,0017 a) ma non fissa la posizione finale delle chart, che raddoppiando le iterazioni si spostano ancora di
  circa 2 volte il rumore. Proposta per il 3-way: soglia 0,005 (in A raggiunta a 28.000, cambiamento 0,0044)
  oppure il doppio delle iterazioni dell'arresto a 0,01.

**Migliore risposta contro il preflop delle chart (B, solo su G1).** `gtosd_preflop_blueprint_monker_values
--exploit all` (esatto, 573 flop, 21 minuti): un giocatore gioca al preflop le chart di MonkerSolver (o, come
controllo, le nostre chart esportate) con il nostro postflop; l'avversario usa la migliore risposta esatta
(preflop e postflop). NashConv della nostra policy 0,897 a (29,9 % del piatto); 0,875 a con il preflop
del CO di MonkerSolver, 0,922 a con il preflop del BTN di MonkerSolver.

Sfruttabilità = valore della migliore risposta dell'avversario; la sua differenza è la differenza di
NashConv, perché la migliore risposta di chi gioca le chart non cambia. La differenza dei guadagni (guadagno
= migliore risposta − EV dello sfruttatore con la nostra policy) è la differenza della migliore risposta meno
la perdita delle chart nel nostro gioco (0,0011 a per il CO, 0,0005 a per il BTN). Le ultime due colonne sono
differenze di guadagno con le migliori risposte parziali, che non si sommano a quella completa (per la
nostra policy: CO 0,618 a contro 0,004 + 0,263): non sono parti della differenza.

| Preflop giocato (con il nostro postflop) | Chi sfrutta | Migliore risposta contro le nostre | Migliore risposta contro le chart | **Differenza (sfruttabilità)** | Differenza dei guadagni | Differenza del guadagno con la sola migliore risposta preflop | Differenza del guadagno con la sola migliore risposta postflop |
|---|---|---:|---:|---:|---:|---:|---:|
| CO con le chart di MonkerSolver | BTN | 0,4866 a | 0,4644 a | **−0,0221 a (−0,74 % del piatto)** | −0,0233 | +0,0109 | −0,0259 |
| BTN con le chart di MonkerSolver | CO | 0,4108 a | 0,4358 a | **+0,0250 a (+0,83 %)** | +0,0245 | +0,0181 | +0,0050 |
| CO con le nostre chart esportate (controllo) | BTN | 0,4866 a | 0,4865 a | −0,00011 a (−0,004 %) | −0,00014 | −0,00004 | −0,00015 |
| BTN con le nostre chart esportate (controllo) | CO | 0,4108 a | 0,4105 a | −0,00031 a (−0,010 %) | −0,00030 | −0,00005 | −0,00009 |

- Il CO con il preflop di MonkerSolver (e il nostro postflop) è **meno** sfruttabile del nostro: il BTN
  guadagna di più con la sola migliore risposta preflop (+0,0109) ma molto meno con la sola postflop
  (−0,0259), soprattutto perché il CO di MonkerSolver non apre quasi mai a 5a: sulla linea open a 5a e call
  il BTN guadagna 0,0558 a contro il nostro CO e 0,0045 a contro MonkerSolver (probabilità della linea
  0,0649 -> 0,0045).
- Il BTN con il preflop di MonkerSolver è **più** sfruttabile del nostro, soprattutto al preflop (guadagno
  solo preflop del CO 0,0042 -> 0,0222 a; solo postflop +0,0050).
- Le due differenze hanno segno opposto e stanno sotto l'1 % del piatto, ma sono circa 20 volte (CO) e 55
  volte (BTN) la perdita delle chart nel nostro gioco; il controllo con le nostre chart esportate resta entro
  0,0003 a (0,010 % del piatto): l'arrotondamento delle chart non pesa.
- Scelte di classe della migliore risposta preflop che cambiano: 7 del BTN in 4 nodi e 6 del CO in 3 nodi,
  tutte marginali; in maggioranza call o fold contro un all-in (TT, T9s, 99, KJo; KTs, QJs, KQo, A9s, ATo),
  più AKs dall'isolation al check dietro il limp, Q7o e J7o da fold a call contro l'open a 5a, 76o da fold a
  limp alla radice (elenco in `out/monker/variants/HU50_c_donk/exploit.txt`).
- Decisione dell'utente: il test C (ricalcolare il postflop con il preflop di MonkerSolver fermo) non si fa.

**Radice del CO: l'open a 5a.** Frequenze pesate sulle combinazioni (tutte le mani):

| Chart | Iterazioni | Open 5a | All-in | Limp | Fold | AA (open / limp) |
|---|---:|---:|---:|---:|---:|---:|
| MonkerSolver | | 0,5 % | 33,1 % | 29,0 % | 37,4 % | 0,07 / 0,93 |
| Passo 1 (postflop vuoto) | 5.000 | 0,0 % | 29,0 % | 62,6 % | 8,3 % | 0 / 1,00 |
| G0c | 20.000 | 7,5 % | 27,7 % | 30,6 % | 34,2 % | 0,76 / 0,24 |
| G1 | 16.000 | 7,5 % | 27,8 % | 30,8 % | 33,8 % | 0,72 / 0,28 |
| A | 16.000 | 7,7 % | 27,8 % | 30,6 % | 33,9 % | 0,76 / 0,24 |
| A | 20.000 | 7,3 % | 27,7 % | 31,5 % | 33,5 % | 0,73 / 0,27 |
| A | 32.000 | 6,7 % | 28,1 % | 32,3 % | 32,9 % | 0,68 / 0,32 |
| E | 20.000 | 7,0 % | 27,7 % | 31,9 % | 33,5 % | 0,66 / 0,34 |
| F | 20.000 | 7,2 % | 27,5 % | 31,7 % | 33,6 % | 0,66 / 0,34 |
| D | 20.000 | 7,4 % | 27,4 % | 31,5 % | 33,7 % | 0,70 / 0,30 |
| G2 | 16.000 | 8,6 % | 26,4 % | 31,4 % | 33,5 % | 0,86 / 0,14 |
| G3 | 16.000 | 8,1 % | 26,8 % | 32,2 % | 33,0 % | 0,79 / 0,21 |
| G4 | 20.000 | 9,4 % | 25,8 % | 30,5 % | 34,3 % | 0,87 / 0,13 |

L'open alla radice scende con le iterazioni in tutti i run: A 7,7 / 7,3 / 7,0 / 6,8 / 6,7 % a 16.000 /
20.000 / 24.000 / 28.000 / 32.000; G4 12,0 / 10,6 / 9,4 % a 12.000 / 16.000 / 20.000; G0c 8,2 % a 16.000.
Va quindi confrontato a iterazioni uguali.

- Nessuna modifica provata porta l'open vicino allo 0,5 % di MonkerSolver. Le astrazioni del 28 (livelli,
  potenziale, texture; valori finali a 20.000-24.000) lo tengono fra il 7,5 % (TX2) e il 10,7 % (potenziale a
  1 livello); D, E ed F a 20.000 fra il 7,0 e il 7,4 %, entro 0,3 punti da A allo stesso punto (7,3 %); il
  seed lo sposta di 0,14 punti (G1 7,54 %, A 7,68 % a 16.000). I donk bet lo spostano poco e non sempre nello
  stesso verso (a 16.000 G0c 8,2 % contro G1 7,5 %; a 24.000 sull'astrazione del passo 2 9,3 % senza e 9,5 %
  con). L'all-in limitato lo alza di circa 0,5 punti a iterazioni uguali (G2 8,6 % contro G0c 8,2 %, G3 8,1 %
  contro G1 7,5 %, tutti a 16.000); la seconda size di 2-3 punti (G4 9,4 % contro A 7,3 % a 20.000, 10,6 %
  contro G1 7,5 % a 16.000), ancora in calo di 1,2 punti nelle ultime 4.000 iterazioni. Il raddoppio delle
  iterazioni lo abbassa (A da 7,7 % a 6,7 %, ancora in calo di circa 0,15 punti ogni 4.000; una stima a + b/t
  fra 16.000 e 32.000 dà un asintoto intorno al 5,6 %). Solo il postflop vuoto del passo 1 lo azzera. AA apre
  il 66-76 % nei giochi a una size senza limite all'all-in (MonkerSolver limpa il 93 %).
- Nel piatto rilanciato a 5a (12 a di piatto, 44 a dietro, SPR circa 3,7) la decisione vera è al flop (bet
  al 100 % e poi all-in al turn): D toglie l'astrazione delle carte al flop (turn 15 × 4 con TX2 e river 15
  livelli restano come in G1, e al turn si decide l'all-in) e la radice non si muove.
- **Ipotesi**: le chart dell'utente sono state calcolate da MonkerSolver con un albero postflop diverso nei
  piatti rilanciati e/o portano l'errore dell'astrazione di MonkerSolver stesso; da parte nostra pesa anche
  la convergenza incompleta (l'open scende ancora con le iterazioni, ma la stima dell'asintoto resta lontana
  dallo 0,5 %). Prossimo test proposto: varianti dell'albero delle azioni solo nei piatti rilanciati (size al
  flop più piccola, solo check o all-in a SPR basso), oppure le impostazioni dell'albero MonkerSolver
  dell'utente, se disponibili (chieste all'utente). **Correzione del 30 settembre (5.9)**: l'utente non
  conosce le impostazioni con cui sono state calcolate le chart e ha chiesto di non domandarle più. Con il
  preflop di MonkerSolver bloccato e il postflop addestrato contro i suoi range (test 5), la migliore risposta
  del CO vuole ancora aprire a 5a; ma le chart raggiungono quella linea lo 0,5 % delle volte e lì il postflop
  del BTN risponde a quel range ristretto, quindi il test non separa il gioco dal modo in cui il postflop è
  appreso (5.9). **Aggiornamento del 30 settembre mattina (5.10)**: la spinta verso l'open a 5a resta con un
  postflop più fine (0,021 -> 0,022-0,025 a) e con il rake 2,5 % / cap 2a (0,014 a); con il rake 5 % scende a
  0,007-0,010 a e la migliore risposta vuole soprattutto più all-in.

**Decisioni dell'utente del 29 settembre.**

| Tema | Decisione |
|---|---|
| Configurazione HU di riferimento | G1: astrazione compatta (15 livelli × 4 + TX2) con donk bet (decisione dell'utente); una size: G4 non adottato perché più lontano da MonkerSolver (criterio della notte, 5.6) |
| All-in postflop fino a 5 volte il piatto | No: revoca la decisione della notte (5.6); G2 e G3 un po' più lontani da MonkerSolver, e il limite toglie solo 6 all-in |
| Test B | Solo su G1 |
| Test C (postflop ricalcolato con il preflop di MonkerSolver fermo) | Non si fa |
| A | Continuato fino a 32.000 iterazioni |
| 3-way | Rinviato (decisione delle 20:00): si parte quando il problema della radice e del piatto limpato è capito meglio, probabilmente dopo le 3 del 30 settembre. **Cambiato nella notte**: fasi 1 e 2 del 3-way avviate subito, le impostazioni della fase 3 aspettano i risultati HU (5.9, 9) |
| Test della radice bloccata | Codice la sera del 29 (anche dopo le 21), test in partenza automatica alle 00:00 del 30; il secondo test (tutto il CO preflop bloccato) solo se il primo non converge verso le chart del BTN di MonkerSolver. **Nei fatti** il test 1 è partito alle 20:28 su richiesta dell'utente, il test 2 alle 21:07 (5.9) |

**Incidenti.** Il runner di G4 è morto con un errore di sintassi dopo l'arresto del trainer (13:36, codice 2),
perché un agente di codice ha modificato `run_step2_continuous.sh` mentre bash lo stava leggendo; training e
valutazione non ne sono toccati (policy scritta dal trainer, valutazione esatta completata). Una copia
congelata del runner messa nella cartella temporanea è fallita alla partenza (la radice del repository è
calcolata dal percorso dello script): primo lancio di E fallito alle 15:02 e rilanciato subito. Le copie
congelate stanno ora in `out/frozen/` (due livelli sotto la radice del repository).

### 5.8 Sera del 29 settembre: G1+, scomposizione dello scarto, test della radice bloccata

**G1+** (`out/monker/variants/HU50_g1plus`): E (G1 con bucket 30 × 8) ripreso dal checkpoint di 20.000
fino alla soglia di arresto 0,005, raggiunta a 28.000 (cambiamento 0,0043). Distanza 0,0622, differenza di
range 0,338 (G1 0,0641 / 0,350), preferenza suited 0,380; chart spostate da G1 di 0,0148 / 0,075 (circa 1,7
volte il rumore); open a 5a del CO al 6,9 %. MonkerSolver nel gioco G1+: perdita 0,00225 a al CO (0,075 % del
piatto) e 0,00022 a al BTN; nostro scarto dalla migliore risposta preflop 0,0031 / 0,0015 a (G1 0,0042 /
0,0018): più convergente, e come in A la perdita di MonkerSolver al CO cresce con la convergenza.

**Dove sta lo scarto da MonkerSolver (G1 a 16.000; A a 32.000 quasi identico).**

| Nodo | Distanza da MonkerSolver | Quota della distanza | Rumore del seed nel nodo |
|---|---:|---:|---:|
| CO contro l'isolation dopo il suo limp | 0,147 | 28,6 % | 0,021 |
| Radice del CO | 0,140 | 27,3 % | 0,016 |
| BTN contro il limp | 0,107 | 21,0 % | 0,013 |
| BTN contro l'open a 5a | 0,075 | 14,6 % | 0,013 |
| Gli altri quattro nodi (all-in) | 0,001-0,018 | 8,5 % | ≤ 0,004 |

| Range ristretto | Combo effettive nostre / MonkerSolver | Quota della differenza di range |
|---|---|---:|
| Limp del CO | 194,1 / 182,8 (stesse dimensioni, composizione diversa) | 39,5 % |
| Isolation del BTN dietro al limp | 144,5 / 152,5 | 31,5 % |
| Open a 5a del CO | 47,5 / 3,1 | 29,0 % |

Lo scarto sta per circa il 70 % nel piatto limpato (chi limpa, chi isola, come il CO risponde all'isolation) e
per circa il 30 % nell'open a 5a: la frase "lo scarto principale è l'open a 5a" detta all'utente nel
pomeriggio era sbagliata ed è stata corretta. Ipotesi: la radice sposta le mani forti fra i piatti (AA limp
0,93 in MonkerSolver, open circa 0,7 da noi) e quindi cambia la composizione del range di limp, la risposta
all'isolation e il range di isolation del BTN; la radice sarebbe la causa comune dei quattro nodi.

**Test della radice bloccata** (richiesto dall'utente, codice la sera del 29): opzione del trainer che blocca
nodi preflop alle righe di un set di chart (strategia corrente e media uguali alla chart, nessun regret),
mentre tutto il resto impara normalmente. Test 1: radice del CO bloccata alla chart di MonkerSolver, gioco G1,
soglia di arresto 0,005, partenza automatica alle 00:00 del 30. Criterio fissato in anticipo: converge se il
BTN contro il limp (oggi 0,107) e il BTN contro l'open (0,075) scendono sotto circa 0,026 (due volte il
rumore del nodo) e il range di isolation del BTN si avvicina a quello di MonkerSolver. Se non converge,
test 2: tutti e quattro i nodi preflop del CO bloccati (nessun codice in più); se anche così il BTN resta
diverso, la causa sta nel postflop (albero delle azioni, astrazione di MonkerSolver, regole del gioco) o in
equilibri quasi indifferenti. Circa 40 minuti di macchina per test.

**Correzione del 30 settembre (5.9).** Il criterio era sbagliato in principio: con i nodi di un giocatore
fermi il CFR non cerca un equilibrio, e i nodi liberi convergono a una migliore risposta alle chart bloccate.
Anche se le chart di MonkerSolver fossero un equilibrio del nostro gioco, i nodi del BTN non dovrebbero
coincidere con i suoi; quindi il fallimento dei test 1 e 2 non dice nulla sul postflop né sugli equilibri
quasi indifferenti. Il test 5 misura se il preflop di MonkerSolver è stabile contro il postflop appreso sui
suoi range (con un limite simile: il postflop appreso contro range fissi non ha motivo di scoraggiare le
deviazioni preflop, 5.9). Il test 1 è partito alle 20:28, in anticipo su richiesta dell'utente, non alle 00:00.

### 5.9 Notte del 29-30 settembre (dalle 20:28 alle 03:55): preflop bloccato, rake, G1 con rake

Cronologia in `out/monker/variants/chain.log`; risultati nelle cartelle `out/monker/variants/HU50_lock_*`,
`HU50_g1_rake` e `out/monker/step1_rake/HU50`. I run del passo 2 usano l'astrazione di G1 (15 livelli × 4 +
TX2, donk bet), con o senza rake, e valutazioni esatte su 573 flop; fanno eccezione il passo 1 con rake (senza
postflop) e i test 5M-5D (altri bucket; in coda alle 03:55, fatti la mattina: risultati nel 5.10).

**Blocco dei nodi preflop** (commit `c7d6ba0` delle 20:43; eseguibili `out/monker/bin_lock`, costruiti alle
20:27 dal codice poi committato come `c7d6ba0` a revisione conclusa). Opzioni del trainer
`--lock-charts DIR --lock-nodes FILE[,FILE...]` oppure `--lock-nodes all`: una riga bloccata gioca la chart
(strategia corrente e media, e ogni export), non riceve scritture di regret né di somma delle strategie; tutto
il resto impara normalmente. L'impronta del blocco fa parte dell'identità del trainer (un checkpoint non
riprende sotto un altro blocco); le classi che la chart lascia vuote restano libere solo se il loro reach
attraverso i nodi bloccati è nullo. Chart di MonkerSolver per il blocco in `out/monker_lock/charts_50a`. Suite
`preflop_blueprint` 47/47 e test di base PASS, revisione indipendente senza difetti del CFR. **Soglia di
arresto con nodi bloccati**: il cambiamento medio fra salvataggi conta come 0 le chart bloccate, quindi il
cambiamento dei nodi liberi è quello misurato × 8/7 con un nodo bloccato e × 2 con quattro. Il test 2 usa la
soglia 0,0025 (0,005 sui nodi liberi); il test 1 è partito con 0,005 non scalata (0,0057 sui nodi liberi): il
suo arresto a 0,004 vale 0,0046 sui nodi liberi, comunque sotto 0,005.

**Test 1 e 2 (radice del CO bloccata; tutti i nodi del CO bloccati).** Criterio del 5.8: BTN contro il limp e
BTN contro l'open sotto circa 0,026 e range di isolation del BTN vicino a quello di MonkerSolver (differenza
sotto 0,072); file `lock_check.json` e `monker_in_our_game.txt` di ciascun run.

| Run | Nodi bloccati | Soglia | Arresto (cambiamento) | BTN contro il limp | BTN contro l'open a 5a | Differenza del range di isolation del BTN | CO contro l'isolation | Guadagno della migliore risposta preflop (CO / BTN) |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| G1 (riferimento) | — | 0,01 | 16.000 (0,0096) | 0,107 | 0,075 | 0,280 | 0,147 | 0,0042 / 0,0018 a (0,14 % / 0,06 %) |
| Test 1, `HU50_lock_root` (20:28-21:07) | radice del CO | 0,005 | 28.000 (0,004) | 0,118 | 0,117 | 0,338 | 0,152 | 0,0813 / 0,0008 a (2,71 % / 0,027 %) |
| Test 2, `HU50_lock_co_all` (21:07-21:46) | i 4 nodi del CO | 0,0025 | 28.000 (0,002) | 0,123 | 0,226 | 0,338 | bloccato | 0,2839 / 0,0007 a (9,46 % / 0,023 %) |
| Limite del criterio | | | | 0,026 | 0,026 | 0,072 | | |

Entrambi **non convergono** secondo il criterio: i due nodi del BTN si allontanano da MonkerSolver invece di
avvicinarsi (contro l'open 0,075 -> 0,117 -> 0,226). La distanza media sugli 8 nodi (0,0588 e 0,0460) conta i
nodi bloccati come 0 e non è confrontabile con quella di G1.

**Lezione: il criterio era sbagliato in principio.** Con i nodi di un giocatore fermi il CFR non cerca un
equilibrio: i nodi liberi convergono a una migliore risposta alle chart bloccate, non a una strategia
bilanciata. Anche se le chart di MonkerSolver fossero un equilibrio del nostro gioco, i nodi del BTN non
dovrebbero coincidere con i suoi (una migliore risposta a una strategia fissa non è in genere la strategia di
equilibrio). Il guadagno del CO (2,71 % e 9,46 %; 1,44 % con tutto il preflop di MonkerSolver nel test 5,
0,14 % in G1) viene soprattutto dall'open a 5a (0,052 a su 0,081 nel test 1, 0,219 su 0,284 nel test 2,
`decompose_br.py`): la radice bloccata raggiunge quella linea lo 0,5 % delle volte, e il BTN vi gioca una
risposta adattata a quel range ristretto, quindi sfruttabile. Errore mio della sera del 29, riconosciuto
con l'utente; corretto nel 5.8.

**Test 5 (`HU50_lock_all`): tutto il preflop dei due giocatori bloccato alle chart di MonkerSolver**, il
postflop impara; 24.000 iterazioni (22:03-22:36), poi esteso fino a 64.000 (00:00-01:49, policy conservata a
40.000). Misura: il guadagno di una migliore risposta preflop esatta con quel postflop fermo, cioè quanto il
preflop di MonkerSolver, insieme a quel postflop, è lontano da un equilibrio del nostro gioco (non contro ogni
postflop possibile); scomposto per nodo e per azione di
destinazione con `decompose_br.py` (`br_split.txt`). File `monker_in_our_game_24k.txt`,
`monker_values_exact_40k.json`, `monker_in_our_game.txt` (64.000).

| Iterazioni del postflop | Guadagno del CO | Guadagno del BTN | Radice del CO (totale; verso open a 5a / limp / all-in) | CO contro l'isolation | BTN contro il limp (totale; verso check / isolation a 5a) |
|---:|---:|---:|---|---:|---|
| 24.000 | 0,0432 a (1,44 %) | 0,0161 a (0,54 %) | 0,0386; 0,0209 / 0,0115 / 0,0062 | 0,0064 | 0,0153; 0,0123 / 0,0026 |
| 40.000 | 0,0429 a (1,43 %) | 0,0153 a (0,51 %) | 0,0381; 0,0205 / 0,0121 / 0,0055 | 0,0069 | 0,0145; 0,0111 / 0,0029 |
| 64.000 | 0,0436 a (1,45 %) | 0,0147 a (0,49 %) | 0,0387; 0,0216 / 0,0119 / 0,0052 | 0,0073 | 0,0140; 0,0103 / 0,0031 |

- Il guadagno sta quasi tutto alla radice del CO (89 % a 64.000): la migliore risposta vuole più open a 5a
  (circa 0,021 a), più limp (circa 0,012 a) e più all-in (circa 0,005 a); al BTN quasi tutto contro il limp,
  verso il check dietro (circa 0,010 a).
- **Stabile con più training del postflop** (CO 1,44 -> 1,43 -> 1,45 %): non è un artefatto delle
  iterazioni. Il preflop di MonkerSolver, con il postflop che il nostro CFR impara contro i suoi range, non è
  un equilibrio del nostro gioco (a bucket, senza rake): il CO guadagna l'1,44 % deviando al preflop, 10 volte
  il nostro scarto dalla migliore risposta preflop in G1 (0,0042 a), oltre la soglia di 0,03 a.
- **Non prova che nessun postflop lo renda un equilibrio.** Con il preflop bloccato il postflop converge a un
  equilibrio del gioco bloccato e non ha motivo di scoraggiare le deviazioni preflop; i valori di sottogioco delle
  mani fuori da un range non sono unici (lo stesso meccanismo dei test 1 e 2). Metà del guadagno del CO (0,0216 a
  su 0,0436 a con 64.000 iterazioni, `br_split.txt`) sta nell'open a 5a, una linea che le chart raggiungono lo
  0,5 % delle volte (3,1 combo), dove il postflop del BTN risponde a quel range ristretto. Le parti sulle linee
  frequenti (limp del CO 0,012 a, check del BTN dietro il limp 0,010 a) sono meno esposte. Quanto viene
  dall'astrazione o dal rake lo dicono i test in coda (sotto). **Risultati del mattino (5.10)**: il rake abbassa il
  guadagno del CO da un quinto a un terzo, un postflop più fine lo alza; nessuna variante lo porta al livello del
  nostro scarto dalla migliore risposta preflop (circa 0,1 % del piatto).
- Test 3 e 4 (bloccato solo il BTN contro l'open, o solo contro il limp): pianificati, sospesi dopo il test 5.

**Incidente delle 00:00 del 30.** Alle 00:00 sono partiti, oltre all'estensione del test 5, due copie del
test 3 (`HU50_lock_btn_open`) e un secondo test 5: venivano da script in coda (`run_lock34.sh` ×2,
`run_lock5.sh` ×2, `run_lock.sh`) che TaskStop non aveva fermato (TaskStop chiude l'involucro del tool, non lo
script bash figlio). Uccisi per PID alle 00:06; `HU50_lock_btn_open` (non richiesto) cancellato; la
valutazione a 24.000 del test 5 (file `*_24k`, 22:40) è intatta; il secondo test 5 si è fermato subito (già a
24.000). Regola da allora: uno script in coda si ferma per PID e si verifica che sia fermo; gli script in coda
controllano un file di annullamento.

**Rake** (commit `3ec4027`, eseguibili `out/monker/bin_rake`). L'utente, 29/09 alle 23:13 dopo il test 5: è
"molto probabile" che le chart siano state calcolate con rake 5 % e cap 3 ante, no flop no drop; il 30/09 alle
01:40 la precisazione: 5 % sul postflop (sul preflop solo per gli all-in), fino a 3a; alle 02:21 "abbastanza
sicuro, non certo". Il motore aveva già
`RakeConfig` e `settle_terminal`: rake = minimo fra percentuale del piatto chiamato e cap, cap per mano, nullo
senza flop con no flop no drop; il flop conta come distribuito se la street non è il preflop oppure se la mano
finisce in un all-in con runout (`flop_dealt = street != Preflop || AllInRunout`), quindi gli all-in preflop
pagano il rake. Chiavi della configurazione del gioco: `rake_mode` `"enabled"`, `rake_basis_points`,
`rake_cap_units` (1 ante = 10.000 unità), `rake_no_flop_no_drop`, `rake_minimum_pot_units`, scritte solo con il
rake attivo (identità e impronte esistenti invariate). Le foglie del checkdown del passo 1 sono ora saldate
come showdown al river e pagano il rake; il gioco non è più a somma zero (somma degli EV = −rake atteso,
`gtosd_preflop_blueprint_monker_values --expected-rake`). Fixture: `benchmarks/monker/HU50_rake.json` (passo 1),
`HU50_step2_donk_rake.json` (G1 con rake, `MONKER-HU50-STEP2-DONK-RAKE-001`),
`benchmarks/fixtures/preflop_blueprint_hu10_reduced_rake_v1.json` (HU10 con un cap di 0,5 a che morde).
Suite `preflop_blueprint` 54/54 e test di base PASS; senza rake ogni output è identico byte per byte;
revisione indipendente senza difetti. Configurazioni delle ipotesi, non committate:
`HU50_step2_donk_rake5cap2.json` (5 %, cap 2a) e `HU50_step2_donk_rake25cap2.json` (2,5 %, cap 2a); dal mattino anche
`HU50_step2_donk_rake5cap075.json` (5 %, cap 0,75a, test 6d, 5.10).

**Passo 1 con rake** (`out/monker/step1_rake/HU50/summary.json`): 5.000 iterazioni, guadagno massimo 9,3e-5 %
del piatto, rake atteso 0,328 a per mano, EV CO −0,280 a, BTN −0,048 a. Distanza da MonkerSolver 0,252,
differenza di range 0,701 (senza rake 0,262 / 0,729). Radice del CO (combo, `root_mix.py`):

| Chart | All-in | Open 5a | Limp | Fold |
|---|---:|---:|---:|---:|
| MonkerSolver | 33,1 % | 0,5 % | 29,0 % | 37,4 % |
| Passo 1 senza rake | 29,0 % | 0,0 % | 62,6 % | 8,3 % |
| Passo 1 con rake | 30,7 % | 0,0 % | 55,0 % | 14,3 % |

Col rake il limp scende di 7,6 punti e il fold sale di 6: la direzione di MonkerSolver, ancora lontana.

**G1 con rake** (`HU50_g1_rake`, `HU50_step2_donk_rake.json`, stessa astrazione di G1, soglia 0,005), dalle
01:41. Arresto per regola a 32.000 (02:51, cambiamento 0,0049); alle 02:46 l'utente chiede di continuare oltre
l'arresto: estensione ripresa dallo stato salvato alle 03:03, dopo la valutazione esatta dell'arresto; alle
03:13 l'utente chiede di fermarla e di avviare i test 6, 6b e 6c: fermata alle 03:15 a 37.850 iterazioni
(ultime chart a 36.000), non rivalutata.

| Iterazioni | Distanza da MonkerSolver | Differenza di range | Cambiamento | Radice del CO: all-in / open 5a / limp / fold |
|---:|---:|---:|---:|---|
| 4.000 | 0,1211 | 0,710 | — | 32,1 / 15,6 / 6,0 / 46,3 % |
| 8.000 | 0,1152 | 0,706 | 0,0280 | 34,1 / 13,3 / 5,5 / 47,1 % |
| 12.000 | 0,1104 | 0,687 | 0,0148 | 34,9 / 12,1 / 6,0 / 46,9 % |
| 16.000 | 0,1050 | 0,651 | 0,0136 | 35,9 / 10,5 / 7,4 / 46,2 % |
| 20.000 | 0,0985 | 0,602 | 0,0122 | 37,2 / 8,0 / 9,6 / 45,2 % |
| 24.000 | 0,0916 | 0,544 | 0,0119 | 37,9 / 5,7 / 12,3 / 44,0 % |
| 28.000 | 0,0873 | 0,510 | 0,0069 | 38,5 / 4,6 / 13,7 / 43,3 % |
| **32.000 (arresto)** | 0,0849 | 0,490 | 0,0049 | 38,9 / 3,8 / 14,5 / 42,8 % |
| 36.000 | 0,0837 | 0,481 | 0,0033 | 39,1 / 3,5 / 14,7 / 42,6 % |
| MonkerSolver | | | | 33,1 / 0,5 / 29,0 / 37,4 % |
| G1 senza rake (16.000) | 0,0641 | 0,350 | | 27,8 / 7,5 / 30,8 / 33,8 % |

- **Radice**: il limp sale e l'open scende con le iterazioni, rallentando (+2,7, +1,4, +0,8, +0,2 punti di
  limp ogni 4.000 iterazioni da 20.000 a 36.000). L'open a 5a (3,5 % a 36.000) è il più vicino allo 0,5 % di
  MonkerSolver fra i giochi con il postflop (6,7-10,7 % senza rake; il passo 1, senza postflop, ha 0,0 %); il
  limp (14,7 %) resta a metà di quello di
  MonkerSolver (29,0 %) e l'all-in (39,1 %) sopra (33,1 %). AA a 36.000: open 0,49 / limp 0,51 (MonkerSolver
  0,07 / 0,93).
- **Distanza**: scende ancora a ogni salvataggio, ma a 36.000 (0,0837 / 0,481) le chart con rake 5 % / cap 3a
  sono più lontane da MonkerSolver di quelle di G1 senza rake (0,0641 / 0,350). Preferenza suited 0,148
  all'arresto (0,147-0,152 da 16.000 a 36.000; MonkerSolver 0,224, G1 0,365).
- **Valutazione esatta all'arresto** (`monker_in_our_game_stop.txt`): le chart di MonkerSolver giocate nel
  gioco con rake perdono 0,00083 a al CO (0,028 % del piatto) e 0,00082 a al BTN (0,027 %), contro 0,075 % /
  0,007 % in G1+ senza rake; il nostro scarto dalla migliore risposta preflop è 0,00266 a (0,089 %) / 0,00074 a
  (0,025 %); rake atteso 0,338 a per mano; EV CO −0,370 a, BTN +0,032 a.

**Cosa vuol dire "corretto"** (discussione con l'utente nella notte). Tre domande diverse:

1. **La nostra strategia nel nostro gioco**: misurata con la migliore risposta esatta. Il guadagno solo
   preflop è piccolo (A a 32.000: 0,0028 / 0,0012 a; G1 con rake all'arresto 0,0027 / 0,0007 a), sotto la
   soglia di 0,03 a. La migliore risposta completa a carte vere vale circa 0,78 a (NashConv 0,777 a, 25,9 % del
   piatto, A a 32.000, `convergence.json`), dominata dal postflop (solo postflop 0,236 / 0,204 a): è
   soprattutto l'errore dell'astrazione delle carte del passo 2 (bucket per board, turn in texture), con un
   resto di convergenza (pavimento stimato sopra 0,685 a, 5.7); l'astrazione delle azioni (una size) non entra
   in questa misura, che usa lo stesso albero. Resta molto sopra il vecchio limite di 0,15 a del certificato
   fisico (tolto dal criterio il 28 settembre, sezione 1). La migliore risposta dentro l'astrazione, il
   criterio di accettazione, non è calcolata con le righe per classe di board.
2. **Se il nostro gioco è quello giusto**: si misura con i test con il preflop bloccato contro MonkerSolver
   (test 5 e seguenti, con il limite detto sopra: il postflop appreso contro range fissi non scoraggia le
   deviazioni preflop) e con le raffinature dell'astrazione. La distanza dalle chart di MonkerSolver non è un
   test di correttezza (gli equilibri non sono unici e le azioni quasi indifferenti si mescolano in modi
   diversi); lo sono i confronti in EV (5.4, B, test 5).
3. **Test che manca**: la migliore risposta preflop contro un postflop risolto esattamente (per ogni flop,
   con i range che ci arrivano), che separerebbe l'errore del nostro postflop astratto da quello del preflop.
   Non costruito; costo da stimare (unico riferimento, per un calcolo simile su HU30 e HU40: 19-29 ore per
   passata, diario del 28 settembre).

**Convenzioni di MonkerSolver dagli EV del set 3-way a 60a** (workflow con un agente e un verificatore
indipendente, circa 01:00-02:20; file in `scratchpad/threeway/probe/`: `summary.txt`, `results.txt`,
`verify_*`). Il set 3-way a 60a (esportato ad agosto 2026) ha le colonne Call_EV e Fold_EV; 18 nodi in cui un
giocatore affronta un all-in e il terzo ha già foldato (showdown a due), 241 classi con frequenza di call
positiva (1.458 in tutto). Range di chi shova e di chi folda dal prodotto delle chart lungo il percorso,
rimozione delle carte congiunta ed esatta, enumerazione esatta dei board. Fold_EV = −(ante + fiches vive
messe) su tutti i 18 nodi; call EV = eq × (piatto − rake) − 60 (stack di 60a compreso l'ante).

| Convenzione (scala > tris) | RMS del residuo (241 classi) | Media del residuo |
|---|---:|---:|
| carte foldate morte, rake 5 % / cap 3a (3 a su questi piatti) | 0,546 a | −0,53 |
| carte foldate morte, senza rake | 1,036 a | +1,02 |
| carte foldate ignorate, rake 5 % / cap 3a | 0,702 a | −0,53 |
| carte foldate ignorate, senza rake | 1,141 a | +1,02 |
| **carte foldate morte, rake piatto 2 a** | **0,092 a** | −0,015 |
| carte foldate ignorate, rake piatto 2 a | 0,463 a | −0,010 |

- **Carte di chi folda: morte** (tolte dal mazzo con il range del giocatore che ha foldato), non ignorate.
  Con un adattamento libero di piatto e costo per nodo le carte morte danno un costo di 60,00 ± 0,10 e un
  piatto dopo il rake uguale al piatto meno 2 a (RMS complessivo 0,088 a contro 0,44 per le carte ignorate).
  Il verificatore ha riscritto il motore da zero (27 casi su 4 nodi uguali entro 2e-12) e ha controllato che
  vince la rimozione congiunta, non quella sequenziale (RMS a 2 a su `CO_AllIn_BTN` / `UTG_AllIn_BTN`: 0,025 /
  0,017 congiunta, 0,085 / 0,064 sequenziale, 0,34 / 0,18 ignorate), con il range di chi folda lungo tutto il
  suo percorso.
- **Rake**: un rake piatto di circa 2 a (1,97 a sull'insieme, 1,8-2,1 a per nodo, piatti da 121 a 128 a);
  5 % / cap 3a e nessun rake sono respinti. Su questi piatti il 5 % supera sempre il cap, quindi la
  percentuale non si può identificare.
- **Classifica**: scala sopra tris confermata (tris sopra scala dà un RMS di almeno 1,84 a: da 1,84 a 2,65
  secondo la convenzione; carte morte 1,84 con 5 % / cap 3a, 1,94 con il rake piatto di 2 a, 2,48 senza rake).
- **Residuo di 0,09 a**: non viene dall'arrotondamento delle chart (0,005 a); cresce sui nodi rari (0,02 a sui
  nodi frequenti, 0,18-0,21 a sui due più rari). Probabilmente rumore o convergenza incompleta degli EV di
  MonkerSolver sui nodi poco visitati (inferenza, non provata).
- **Limiti**: il set 3-way a 60a è di agosto 2026, quello a 50a di settembre 2025: le impostazioni possono
  essere diverse. Il set a 50a non ha colonne di EV; una stima indiretta dall'indifferenza delle classi miste
  fra call e fold (tarata sul 60a: mediana 1,99 a) dà sul 3-way a 50a (13 classi, stack di 50a compreso l'ante)
  carte morte coerenti (mediana 0,71 a, scarto 0,27; le ignorate si disperdono, scarto 1,0) e un rake di circa
  **0,7 a**, non 3 a: con 3 a quelle call miste perderebbero circa 1,1 a rispetto al fold, senza rake
  guadagnerebbero circa 0,35 a. Stima indicativa (presuppone che MonkerSolver sia convergente su quei nodi).
  **Precisazione del 30 settembre mattina**: la verifica finale (`verify_50a_out.txt`, 10 classi con call mista su
  piatti di 101-102 a) dà con le carte morte una mediana di 0,745 a (media 0,739, scarto 0,195; carte ignorate:
  mediana 1,10, scarto 1,12); da qui il test 6d (5 %, cap 0,75a, 5.10).
  Conclusione del verificatore: nessuno dei due set coincide con 5 % / cap 3a; la fixture 3WAY50 con rake
  (5 %, cap 3a) non è confermata dalle chart.
- Dalla critica della fase 1 (9.1): le cartelle 3-way "40a" e "60a" sono identiche byte per byte (11/08/2026)
  e i loro EV corrispondono a uno stack di 60a: la cartella "40a" contiene il calcolo a 60a e non va usata come
  riferimento a 40a.

**Test in coda e in corso alle 03:55** (risultati allora non disponibili). **Aggiornamento delle 08:50**: tutti
fatti, più il test 6d (5 %, cap 0,75a) aggiunto alle 04:35; risultati nel 5.10.

| Test | Cartella | Cosa | Scopo |
|---|---|---|---|
| 6 | `HU50_lock_all_rake` | test 5 nel gioco con rake 5 %, cap 3a (training finito alle 03:48, valutazione esatta in corso) | quanto il preflop di MonkerSolver, con il postflop appreso sui suoi range, è lontano da un equilibrio sotto ciascuna ipotesi di rake: un guadagno molto più basso del test 5 (1,44 / 0,54 %) indica il rake usato |
| 6b | `HU50_lock_all_rake5cap2` | stesso con 5 %, cap 2a | come sopra |
| 6c | `HU50_lock_all_rake25cap2` | stesso con 2,5 %, cap 2a | come sopra |
| B con rake | `HU50_g1_rake` (`exploit.json`) | il test B del 5.7 sul gioco con rake: policy a 37.850 iterazioni, nostre chart a 36.000 | sfruttabilità del preflop di MonkerSolver contro quella del nostro, nel gioco con rake |
| 5M | `HU50_lock_all_m30x4` | test 5 con bucket 30 × 4 (i conteggi di default di MonkerSolver, 30 livelli × 4 al flop e al turn e 30 al river, dalle schermate delle impostazioni mandate dall'utente; aggiunto per primo alle 03:47) | il preflop di MonkerSolver diventa meno sfruttabile quando il nostro postflop si fa più fine? (domanda dell'utente: l'astrazione diversa di MonkerSolver può spiegare il fallimento?) |
| 5E, 5F, 5D | `HU50_lock_all_e30x8`, `_f60x8`, `_dflop` | test 5 con i bucket di E (30 × 8), F (60 × 8) e D (flop esatto) | come sopra |

I test 5M-5D partono dopo il B con rake (coda `run_lock_abstr2.sh`, che alle 03:47 ha sostituito la coda senza
M). Osservazione: su un piatto all-in da circa 100 a le tre ipotesi tolgono 3 a (6) e 2 a (6b, 6c); nessuna
riproduce gli 0,7 a stimati sul set 3-way a 50a (per questo alle 04:35 è stato aggiunto il test 6d). L'utente non
conosce le impostazioni con cui sono state calcolate le chart (30/09: da non chiedere più); le schermate danno i
valori di default di MonkerSolver, non quelli usati per le chart (5.10).

**Decisioni dell'utente della notte.**

| Tema | Decisione |
|---|---|
| Test con il preflop bloccato | Ordine: test 1, test 2 solo se il primo fallisce; test 1 anticipato alle 20:28; test 5 con priorità ed esteso a 64.000 iterazioni; test 3 e 4 sospesi |
| Rake | 5 %, cap 3a, no flop no drop, "molto probabile" (29/09 23:13); 5 % sul postflop, sul preflop solo per gli all-in, fino a 3a (30/09 01:40); poi "abbastanza sicuro, non certo" (02:21): codificato (`3ec4027`); test 6, 6b e 6c approvati |
| G1 con rake | Continuato oltre l'arresto (02:46), poi fermato a 36.000 per i test 6, 6b e 6c (richiesta alle 03:13, fermo alle 03:15); test B ripetuto con il rake (domanda dell'utente alle 03:25, coda scritta alle 03:27) |
| Astrazione | Test 5 con i bucket di E, F e D in coda dopo il B con rake; M (conteggi di default di MonkerSolver) aggiunto per primo alle 03:47 |
| Carte di chi folda | Convenzione da verificare sugli EV di MonkerSolver: fatto, morte |
| 3-way | Fasi 1 e 2 avviate subito (revoca il rinvio delle 20:00); le impostazioni della fase 3 aspettano l'HU (sezione 9) |
| Impostazioni di MonkerSolver | L'utente non le conosce; non chiederle più |
| Aperti | Regola di arresto 0,005 per il 3-way (proposta); configurazione HU di riferimento e scelta del rake dopo i test (ancora aperti alle 08:50, sezione 8) |

### 5.10 Mattina del 30 settembre (dalle 03:55 alle 08:50): rake, B con rake, astrazione, impostazioni di MonkerSolver

Cronologia in `out/monker/variants/chain.log`; risultati nelle cartelle `out/monker/variants/HU50_lock_all_*` e in
`HU50_g1_rake/exploit.txt` (3-way nella sezione 9.5). Ordine dei run, training e valutazione: test 6 (03:15-03:58), 6b
(03:58-04:39), 6c (04:39-05:13), B con rake (05:13-05:41), 5M (05:42-06:13), 5E (06:13-06:45), 5F (06:45-07:23), 5D
(07:23-07:54), passo 1 3-way (07:55-08:06, 9.5), 6d (08:06-08:39), un passo 1 3-way in più (08:40-08:43). I test con
il preflop bloccato ripetono il test 5 (5.9) con una sola modifica: gli 8 nodi preflop dei due giocatori bloccati alle
chart di MonkerSolver HU 50a, il postflop impara per 24.000 iterazioni (soglia 0), valutazione esatta su 573 flop,
guadagno della migliore risposta preflop scomposto per nodo e per azione di destinazione con `decompose_br.py`
(`br_split.txt`: guadagni locali, cioè deviare in un solo nodo e seguire le chart dopo; indicano dove sta la
deviazione ma non ne sono parti esatte: per il BTN la loro somma resta entro il 2 % del guadagno completo, per il CO lo
supera del 4-5 % senza rake (5, 5M-5D), del 9 % con il 2,5 % / cap 2a (6c) e del 18-22 % con il 5 % (6, 6b, 6d); gli
importi sotto 0,00005 a non sono stampati). Eseguibili `out/monker/bin_rake` (6-6d, B con rake) e
`out/monker/bin_lock` (5M-5D).

**Test 6-6d: il preflop di MonkerSolver bloccato nei giochi con rake** (albero e astrazione di G1, no flop no drop, gli
all-in preflop pagano il rake; 25-33 minuti di training e 7-12 di valutazione per test). Il test 6d (5 %, cap 0,75a,
`HU50_step2_donk_rake5cap075.json`, `MONKER-HU50-STEP2-DONK-RAKE5-CAP075-001`, non committata) è stato aggiunto alle
04:35, con l'utente via: la verifica delle convenzioni sul set 3-way a 50a, dello stesso periodo delle chart HU50, ricava
dalle call miste contro gli all-in circa 0,75 a di rake su piatti di circa 100 a (carte foldate morte: mediana 0,745 a su
10 classi, indicativa; 5.9), un valore che 6, 6b e 6c (3 e 2 a su quei piatti) non coprono. File `br_split.txt`,
`monker_in_our_game.txt` e `monker_values_exact.log` (rake atteso) di ciascuna cartella.

| Test | Cartella | Rake | Guadagno del CO | Guadagno del BTN | Somma | Rake atteso per mano | EV CO / BTN |
|---|---|---|---:|---:|---:|---:|---:|
| 5 (riferimento, 24.000) | `HU50_lock_all` | nessuno | 0,0432 a (1,44 %) | 0,0161 a (0,54 %) | 0,0593 a (1,98 %) | 0 | −0,211 / +0,211 a |
| 6 | `HU50_lock_all_rake` | 5 %, cap 3a | 0,0281 a (0,94 %) | 0,0190 a (0,63 %) | 0,0471 a (1,57 %) | 0,373 a | −0,387 / +0,014 a |
| 6b | `HU50_lock_all_rake5cap2` | 5 %, cap 2a | 0,0298 a (0,99 %) | 0,0167 a (0,56 %) | 0,0465 a (1,55 %) | 0,282 a | −0,346 / +0,063 a |
| 6c | `HU50_lock_all_rake25cap2` | 2,5 %, cap 2a | 0,0277 a (0,92 %) | 0,0142 a (0,47 %) | **0,0419 a (1,40 %)** | 0,234 a | −0,321 / +0,087 a |
| 6d | `HU50_lock_all_rake5cap075` | 5 %, cap 0,75a | 0,0340 a (1,13 %) | 0,0140 a (0,47 %) | 0,0480 a (1,60 %) | 0,152 a | −0,284 / +0,132 a |

Dove sta il guadagno (`br_split.txt`, ante; "—" = sotto 0,00005 a):

| Test | Radice del CO: totale (verso all-in / open 5a / limp / fold) | CO contro l'isolation: totale (verso all-in / call / fold) | Call contro un all-in: CO contro lo shove del BTN sul limp / BTN contro lo shove del CO | BTN contro il limp: totale (verso check / all-in / isolation 5a) |
|---|---|---|---|---|
| 5 | 0,0386 (0,0062 / 0,0209 / 0,0115 / —) | 0,0064 (0,0026 / 0,0038 / —) | verso call 0,0001 / 0,0004 | 0,0153 (0,0123 / 0,0004 / 0,0026) |
| 6 | 0,0266 (0,0136 / 0,0082 / 0,0015 / 0,0033) | 0,0060 (0,0044 / 0,0004 / 0,0011) | verso fold 0,0015 / 0,0019 | 0,0163 (0,0079 / 0,0084 / —) |
| 6b | 0,0285 (0,0177 / 0,0070 / 0,0015 / 0,0023) | 0,0071 (0,0058 / 0,0005 / 0,0008) | verso fold 0,0008 / 0,0001 | 0,0158 (0,0074 / 0,0083 / 0,0001) |
| 6c | 0,0251 (0,0061 / 0,0136 / 0,0054 / 0,0001) | 0,0044 (0,0028 / 0,0013 / 0,0003) | verso fold 0,0008 / 0,0001 | 0,0136 (0,0101 / 0,0027 / 0,0008) |
| 6d | 0,0330 (0,0214 / 0,0103 / 0,0012 / 0,0001) | 0,0071 (0,0060 / 0,0011 / —) | — / — | 0,0134 (0,0067 / 0,0054 / 0,0012) |

- **Nessuna ipotesi di rake chiude lo scarto.** Il guadagno del CO scende da 1,44 % a 0,92-1,13 % del piatto: di circa
  un terzo con 6, 6b e 6c (dal 31 al 36 %), di un quinto con 6d (21 %). Quello del BTN sale con il 5 % e i cap di 3a e
  2a (0,63 % e 0,56 % contro 0,54 %) e scende con 6c e 6d (0,47 %). La somma è più bassa con 2,5 % / cap 2a (6c: 1,40 %
  contro 1,98 % senza rake). Con 6, 6b e 6c il guadagno del CO sta appena sotto 0,03 a (la soglia di accettazione della
  nostra strategia, qui solo come metro), con 6d sopra; in tutti i giochi resta 7-13 volte il nostro scarto dalla
  migliore risposta preflop nel nostro gioco (0,0027-0,0042 a al CO in G1, G1+, A a 32.000 e G1 con rake: circa lo
  0,1 % del piatto).
- **Cambia la natura della deviazione.** Con il 5 % la migliore risposta del CO vuole soprattutto più all-in: alla
  radice verso all-in 0,0136 / 0,0177 / 0,0214 a (6 / 6b / 6d, contro 0,0062 senza rake) e dopo l'isolation 0,0044 /
  0,0058 / 0,0060 (contro 0,0026); la spinta verso l'open a 5a scende a 0,0070-0,0103 (contro 0,0209). Un piatto all-in
  da circa 100 a paga il cap (3, 2 o 0,75 a, cioè il 3, il 2 o lo 0,75 % del piatto), un piatto più piccolo che vede il
  flop paga il 5 % intero: in proporzione il rake pesa meno sugli all-in, tanto meno quanto più il cap è basso, e infatti
  la spinta verso l'all-in è massima con il cap di 0,75a. Con il 2,5 % / cap 2a torna la spinta verso l'open a 5a
  (0,0136 a) e l'all-in resta al livello del gioco senza rake (0,0061).
- **Il limp di MonkerSolver** è coerente con il nostro postflop nei giochi al 5 %: verso il limp 0,0015 / 0,0015 /
  0,0012 a (6 / 6b / 6d) contro 0,0115 senza rake; con il 2,5 % / cap 2a ne resta circa metà (0,0054). Con il 5 % e i
  cap di 3a e 2a la migliore risposta vuole anche qualche fold alla radice (0,0033 / 0,0023 a).
- **BTN contro il limp**: sempre verso il check dietro (0,0079 / 0,0074 / 0,0101 / 0,0067 a in 6 / 6b / 6c / 6d,
  contro 0,0123 senza rake); con il 5 % altrettanto verso l'isolation all-in (0,0084 / 0,0083 / 0,0054 a in 6 / 6b /
  6d; 0,0027 in 6c).
- **Call contro un all-in** (i nodi dove la decisione dipende solo da equity e rake): senza rake la migliore risposta
  vuole chiamare un po' di più (0,0001 / 0,0004 a), con il cap di 3a foldare di più (0,0015 / 0,0019), con il cap di
  2a foldare un po' di più (0,0008 / 0,0001 in 6b e 6c), con il cap di 0,75a nulla (sotto 0,00005 a in entrambi i
  nodi). Il BTN contro lo shove del CO sull'isolation va nello stesso verso (verso fold 0,0004 a con 3a, 0,0001 con 2a, nulla con 0,75a e
  senza rake). Importi piccoli, ma nella stessa direzione della verifica sul 3-way a 50a (circa 0,75 a): fra le
  ipotesi provate solo il cap di 0,75a rende i call di MonkerSolver contro gli all-in coerenti con i nostri EV. Indizio,
  non prova (con quel cap è stata provata una sola percentuale).
- **Rake atteso** con il preflop di MonkerSolver e il nostro postflop: 0,373 / 0,282 / 0,234 / 0,152 a per mano (G1
  con rake all'arresto, con il nostro preflop: 0,338 a).
- L'utente non conosce le impostazioni di MonkerSolver usate per le chart (da non chiedere): i test dicono quali
  ipotesi sono meno incoerenti con le chart nel nostro gioco, non quale è stata usata.

**Test B con rake** (`HU50_g1_rake/exploit.txt`, 05:13-05:41, 19 minuti di passate esatte): il test B del 5.7 sul
gioco G1 con rake 5 % / cap 3a, policy a 37.850 iterazioni, nostre chart a 36.000. NashConv della nostra policy
0,4886 a (16,29 % del piatto) contro 0,897 a (29,9 %) in G1 senza rake (non confrontabili in senso stretto: altra policy,
37.850 iterazioni contro 16.000; A senza rake a 32.000 iterazioni: 0,777 a). Con il rake il gioco non è a somma zero:
la misura è la differenza dei guadagni ("extra" del programma; guadagno = migliore risposta − EV dello sfruttatore con
la nostra policy). La differenza delle migliori risposte è più piccola (+0,0936 a con il CO di MonkerSolver) perché con
il preflop del CO di MonkerSolver il rake atteso sale di 0,028 a per mano.

| Preflop giocato (con il nostro postflop) | Chi sfrutta | Guadagno contro le nostre | Guadagno contro le chart | **Differenza (extra)** | Differenza del guadagno con la sola migliore risposta preflop | Differenza del guadagno con la sola migliore risposta postflop |
|---|---|---:|---:|---:|---:|---:|
| CO con le chart di MonkerSolver | BTN | 0,1185 a | 0,2389 a | **+0,1204 a (+4,01 % del piatto)** | +0,0146 | +0,0963 |
| BTN con le chart di MonkerSolver | CO | 0,3701 a | 0,4127 a | **+0,0426 a (+1,42 %)** | +0,0100 | +0,0096 |
| CO con le nostre chart esportate (controllo) | BTN | 0,1185 a | 0,1181 a | −0,0004 a (−0,013 %) | +0,000015 | −0,0005 |
| BTN con le nostre chart esportate (controllo) | CO | 0,3701 a | 0,3701 a | +0,00006 a (+0,002 %) | −0,00002 | −0,00001 |

- Con il rake il preflop del CO di MonkerSolver è **più** sfruttabile del nostro (differenza dei guadagni +4,01 % del
  piatto; senza rake −0,78 %, cioè −0,0233 a nel 5.7, dove la differenza delle migliori risposte è −0,74 %),
  soprattutto con la sola migliore risposta postflop (+0,0963 a contro +0,0146 con la sola preflop). La variazione più
  grande è nel piatto limp-check: il guadagno postflop del BTN sulla linea limp del CO e check del BTN passa da 0,1016 a
  0,2260 a, ma la probabilità della linea quasi raddoppia (0,145 -> 0,282) mentre il guadagno per visita sale poco
  (0,70 -> 0,80 a, +14 %). La differenza viene soprattutto dal fatto che le chart di MonkerSolver (limp 29,0 % contro il
  14,7 % del nostro CO nel gioco con rake, chart a 36.000) portano il CO il doppio delle volte in un piatto dove il
  nostro postflop è già sfruttabile, in parte dal range di limp diverso da quello su cui il nostro postflop è
  addestrato. In entrambi i casi misura il nostro postflop, non il preflop di MonkerSolver: è il limite noto del test B
  (il postflop è il nostro, addestrato sui nostri range, 5.7), più pesante qui perché nel gioco con rake il nostro
  preflop è più lontano da quello di MonkerSolver (0,0837 / 0,481 a 36.000 contro 0,0641 / 0,350 in G1). La misura
  pulita è il test 6 (postflop addestrato sui range di MonkerSolver).
- Il BTN di MonkerSolver è sfruttato dal CO per +0,0426 a in più delle nostre chart (differenza dei guadagni +1,42 %;
  senza rake +0,82 %, cioè +0,0245 a nel 5.7, dove la differenza delle migliori risposte è +0,83 %); la differenza del
  guadagno con la sola migliore risposta preflop è +0,0100 e con la sola postflop +0,0096 (insieme meno della metà: non
  si sommano alla differenza completa e non ne sono parti, 5.7); la migliore risposta preflop del CO cambia 13 scelte
  di classe in 3 nodi, 10 da call a fold contro un all-in (elenco in `exploit.txt`). Il controllo con le nostre chart esportate resta entro 0,0004 a.

**Test 5M-5D: il preflop di MonkerSolver bloccato con un postflop più fine** (senza rake, come il test 5: gioco G1
`HU50_step2_donk.json`, turn in texture TX2, 24.000 iterazioni; 27-34 minuti di training, cioè 27-29 fino
all'iterazione 24.000 più la scrittura dello stato, circa 5 minuti per i 7,4 GB di F, e circa 4 di valutazione per
test). Domanda dell'utente: l'astrazione diversa di MonkerSolver può spiegare lo scarto del test 5? Se il preflop
di MonkerSolver fosse vicino a un equilibrio del gioco a carte vere e la causa fosse il nostro postflop grossolano, un
postflop più fine dovrebbe ridurre il guadagno. M (30 × 4, i conteggi di default di MonkerSolver dalle schermate,
sotto) è stato aggiunto e messo per primo alle 03:47 (coda `run_lock_abstr2.sh`).

| Test | Cartella | Bucket (id al flop / turn / river) | Stato del trainer | Guadagno del CO | Guadagno del BTN | Somma | Radice del CO: totale (verso open 5a / limp / all-in) | CO contro l'isolation | BTN contro il limp: totale (verso check / isolation 5a) |
|---|---|---|---:|---:|---:|---:|---|---:|---|
| 5 (G1, riferimento) | `HU50_lock_all` | 15 × 4 (60 / 60 / 15) | 1,08 GB | 0,0432 a (1,44 %) | 0,0161 a (0,54 %) | 1,98 % | 0,0386 (0,0209 / 0,0115 / 0,0062) | 0,0064 | 0,0153 (0,0123 / 0,0026) |
| 5M | `HU50_lock_all_m30x4` | 30 × 4 (120 / 120 / 30) | 2,17 GB | 0,0479 a (1,60 %) | 0,0181 a (0,60 %) | 2,20 % | 0,0433 (0,0222 / 0,0142 / 0,0069) | 0,0067 | 0,0173 (0,0149 / 0,0021) |
| 5E | `HU50_lock_all_e30x8` | 30 × 8 (240 / 240 / 30) | 3,70 GB | 0,0501 a (1,67 %) | 0,0193 a (0,64 %) | 2,31 % | 0,0455 (0,0231 / 0,0151 / 0,0073) | 0,0068 | 0,0185 (0,0163 / 0,0020) |
| 5F | `HU50_lock_all_f60x8` | 60 × 8 (480 / 480 / 60) | 7,40 GB | 0,0540 a (1,80 %) | 0,0213 a (0,71 %) | 2,51 % | 0,0496 (0,0239 / 0,0181 / 0,0076) | 0,0071 | 0,0204 (0,0182 / 0,0021) |
| 5D | `HU50_lock_all_dflop` | flop esatto (150-528 id), turn e river 15 × 4 | 1,41 GB | 0,0490 a (1,63 %) | 0,0183 a (0,61 %) | 2,24 % | 0,0448 (0,0248 / 0,0127 / 0,0074) | 0,0066 | 0,0175 (0,0146 / 0,0029) |

- **Ogni raffinatura del nostro postflop aumenta la sfruttabilità del preflop di MonkerSolver**: guadagno del CO dal
  +11 % (M) al +25 % (F), del BTN dal +13 % al +32 %; la somma passa da 1,98 % a 2,20-2,51 %. Nel test 5, fra 24.000
  e 64.000 iterazioni, il guadagno del CO variava di 0,02 punti (1,43-1,45 %) e la somma di circa 0,03 (da 1,98 a
  1,94 %, 5.9): gli aumenti (CO +0,16-0,36 punti, somma +0,22-0,53) restano ben sopra. Quella stabilità è misurata
  solo con i bucket di G1: con più righe (stato 1,3-6,9 volte quello di G1) e i board campionati, a 24.000 iterazioni
  i run più fini possono essere meno convergenti (non verificato). Cresce la spinta verso l'open a 5a (0,021 ->
  0,022-0,025 a, la più alta con il flop esatto, dove si decide la linea del piatto rilanciato, 5.7), verso il limp
  (0,0115 -> 0,0127-0,0181) e quella del BTN verso il check dietro il limp (0,0123 -> 0,0146-0,0182).
- **Conclusione**: la nostra astrazione postflop non è ciò che ci separa da MonkerSolver; con i conteggi di default di
  MonkerSolver (M) lo scarto è più grande che con G1. Spiegazioni che restano: il postflop di MonkerSolver stesso (altra
  astrazione, con classi di texture proprie al turn e al river; campionamento MCCFR; convergenza), le size postflop
  (l'albero postflop delle chart non è noto), il rake, e il limite del test (postflop appreso contro range fissi, 5.9).
  M usa i conteggi di MonkerSolver ma non le sue classi di texture (turn TX2, river per classe del turn).

**Le impostazioni di MonkerSolver (schermate mandate dall'utente verso le 03:45).** Sono le impostazioni generiche di
MonkerSolver per l'Hold'em, **non** quelle con cui sono state calcolate le chart dell'utente:

- **Astrazione**: al flop "Buckets/node" 210.600 con 30 livelli di forza ("Strength buckets") e texture del flop
  "Perfect" = 1.755 flop di Hold'em × 120 (30 livelli × 4); al turn 1.073.040 = 8.942 classi di texture ("Large") × 120;
  al river 110.310 = 3.677 classi × 30. Il default è quindi 30 × 4 al flop e al turn e 30 al river (è l'esempio
  ufficiale della sezione 2). Il nostro primo passo 2 del 28/09 usava 30 × 4; G1 usa 15 × 4 con le texture TX2, che
  fondono i turn più di "Large" (TX2 tiene 4.482 dei 13.761 turn canonici short deck, circa un terzo; "Large" 8.942
  classi sui 16.432 turn canonici di Hold'em, più della metà); il nostro river usa la classe del turn, MonkerSolver ha
  3.677 classi di river proprie.
- **Algoritmo**: CSCFR (campionamento del caso) o ESCFR (campionamento esterno), cioè MCCFR; strategia media ed EV
  tenuti solo sulla prima street (il preflop).
- **Rake**: "Rake (%)", "Rake cap" in mchip (un cap in ante dipende quindi dall'unità delle fiches del gioco), caselle
  "Rake preflop", "Rake uncalled bets" e "Rake raised bets" (il significato dell'ultima non è noto).
- **Conseguenza per il 3-way**: con i bucket 30 × 4 il passo 2 a tre giocatori (fase 3) raddoppierebbe la memoria,
  da 13,56 a 27,11 GB in double (circa 13,6 GB in float32; 9.1).

**Lettura dello scarto HU alle 08:50.**

1. Contro la nostra strategia le chart di MonkerSolver sono equivalenti in EV nel nostro gioco (perdita al massimo lo
   0,09 % del piatto senza rake, 5.4-5.8; 0,028 % / 0,027 % con il rake 5 % / cap 3a, 5.9).
2. Con tutto il preflop di MonkerSolver bloccato e il postflop appreso sui suoi range, la migliore risposta preflop
   guadagna al CO l'1,44 % del piatto senza rake, lo 0,92-1,13 % con le quattro ipotesi di rake e l'1,60-1,80 % con un
   postflop più fine (BTN 0,54 %, 0,47-0,63 %, 0,60-0,71 %). Il nostro preflop nel suo gioco: circa lo 0,1 %. Nessuna
   variante provata chiude lo scarto.
3. Il rake ne spiega una parte (da un quinto a un terzo del guadagno del CO) e cambia la deviazione (più all-in invece
   di più open); fra le ipotesi provate il cap di 0,75a è il più coerente con i call contro gli all-in in HU (indizio:
   negli altri giochi gli scarti in quei nodi sono di 0,0001-0,0019 a; con quel cap è stata provata una sola
   percentuale), in accordo con la stima di circa 0,75 a sul 3-way a 50a (5.9), ma lascia al CO l'1,13 %, il guadagno
   più alto fra le ipotesi di rake.
4. La nostra astrazione postflop non spiega lo scarto: raffinandola lo scarto cresce.
5. Restano il postflop di MonkerSolver (astrazione, campionamento, convergenza), le size postflop, le impostazioni di
   rake non note e il limite del test; il test che manca è sempre la migliore risposta preflop contro un postflop
   risolto esattamente per flop (5.9).

**Note del mattino.** Il test 6d, aggiunto alle 04:35 in coda ai test 5M-5D, è stato spostato alle 05:36 dietro i run
3-way del passo 1 (`run_3way_then_6d.sh`; la riga "test 6d cancelled" delle 05:35 del `chain.log` è lo spostamento).
L'utente ha chiesto di risentirsi verso le 12:00 del 30 e ha permesso di anticipare i run 3-way appena la macchina
fosse libera: sono partiti alle 07:55 invece che alle 11:30 (9.5).

## 6. Strumenti e riproduzione

```
gtosd_preflop_blueprint_checkdown --config benchmarks/monker/HU50.json --resources-dir out/preflop_blueprint_resources --output-dir out/monker/step1/HU50 --iterations 5000
gtosd_preflop_blueprint_monker_buckets --resources-dir out/preflop_blueprint_resources --output-dir out/monker/buckets_30x4 --threads 8
STEP=4000 MAX=160000 THRESHOLD=0.01 bash tools/monker_compare/run_step2.sh benchmarks/monker/HU50_step2.json out/monker/buckets_30x4 "<cartella delle chart MonkerSolver HU 50a>" out/monker/step2/HU50
python tools/monker_compare/compare_charts.py <nostre chart> <chart MonkerSolver> --json <report>
# modalità continua (dalla sera del 28): chart dal trainer vivo, file di stop, ripresa dal checkpoint
TRAIN_ARGS="--seed 2" bash tools/monker_compare/run_step2_continuous.sh benchmarks/monker/HU50_step2.json out/monker/buckets_30x4 "<chart MonkerSolver HU 50a>" out/monker/variants/HU50_seed2
gtosd_preflop_blueprint_monker_buckets --resources-dir out/preflop_blueprint_resources --output-dir out/monker/buckets_15x4 --levels 15 --threads 8
TRAIN_ARGS="--board-texture-map benchmarks/monker/textures/texture_map_TX2_recommended.txt" bash tools/monker_compare/run_step2_continuous.sh ... out/monker/variants/HU50_tx2
# chart MonkerSolver giocate nel nostro gioco (valutazione esatta, 4 minuti)
gtosd_preflop_blueprint_monker_values --config benchmarks/monker/HU50_step2.json --resources-dir out/preflop_blueprint_resources --buckets-dir out/monker/buckets_30x4 --board-class-rows --policy out/monker/step2/HU50/policy.bin --charts monker=<chart MonkerSolver> --charts ours=out/monker/step2/HU50/charts/it_24000 --all-flops --threads 8 --out <values.json>
python tools/monker_compare/monker_in_our_game.py <values.json> --monker <chart MonkerSolver> --ours out/monker/step2/HU50/charts/it_24000 --json <report> --check
# 29 settembre: bucket più fini (E, F) e flop esatto (D)
gtosd_preflop_blueprint_monker_buckets --resources-dir out/preflop_blueprint_resources --output-dir out/monker/buckets_30x8 --levels 30 --tiers 8 --threads 8
gtosd_preflop_blueprint_monker_buckets --resources-dir out/preflop_blueprint_resources --output-dir out/monker/buckets_flopexact_15x4 --levels 15 --tiers 4 --flop-exact --threads 4
# G1 con seed 2 e policy a ogni salvataggio (A), poi ripresa dal checkpoint fino a 32.000 senza arresto e curva esatta
POLICY_SNAPSHOTS=1 TRAIN_ARGS="--board-texture-map benchmarks/monker/textures/texture_map_TX2_recommended.txt --seed 2" bash out/frozen/run_step2_continuous_abd.sh benchmarks/monker/HU50_step2_donk.json out/monker/buckets_15x4 "<chart MonkerSolver HU 50a>" out/monker/variants/HU50_a_seed2
MAX=32000 THRESHOLD=0 POLICY_SNAPSHOTS=1 TRAIN_ARGS="... --seed 2" bash out/frozen/run_step2_continuous_abd.sh ... out/monker/variants/HU50_a_seed2
python tools/monker_compare/convergence_curve.py out/monker/variants/HU50_a_seed2 --monker "<chart MonkerSolver HU 50a>" --threads 8
# migliore risposta contro il preflop delle chart (B), sul run G1
gtosd_preflop_blueprint_monker_values --config benchmarks/monker/HU50_step2_donk.json ... --policy out/monker/variants/HU50_c_donk/policy.bin --charts monker=<chart MonkerSolver> --charts ours=out/monker/variants/HU50_c_donk/charts/it_16000 --all-flops --threads 8 --no-combo-values --exploit all --out <values.json> --exploit-out out/monker/variants/HU50_c_donk/exploit.json --exploit-summary out/monker/variants/HU50_c_donk/exploit.txt
# notte del 29-30: preflop bloccato alle chart di MonkerSolver (test 1: radice del CO; test 5: tutto il preflop, soglia 0 e 24.000 iterazioni)
BIN=out/monker/bin_lock THRESHOLD=0.005 TRAIN_ARGS="--board-texture-map benchmarks/monker/textures/texture_map_TX2_recommended.txt --lock-charts out/monker_lock/charts_50a --lock-nodes CO/CO_strategy.txt" bash out/frozen/run_step2_continuous_lock.sh benchmarks/monker/HU50_step2_donk.json out/monker/buckets_15x4 "<chart MonkerSolver HU 50a>" out/monker/variants/HU50_lock_root
MAX=24000 THRESHOLD=0 TRAIN_ARGS="... --lock-nodes all" bash out/frozen/run_step2_continuous_lock.sh ... out/monker/variants/HU50_lock_all
# rake: passo 1 e G1 con rake (soglia 0,005), valutazione esatta con il rake atteso
gtosd_preflop_blueprint_checkdown --config benchmarks/monker/HU50_rake.json --resources-dir out/preflop_blueprint_resources --output-dir out/monker/step1_rake/HU50 --iterations 5000
BIN=out/monker/bin_rake THRESHOLD=0.005 TRAIN_ARGS="--board-texture-map benchmarks/monker/textures/texture_map_TX2_recommended.txt" bash out/frozen/run_step2_continuous_rake.sh benchmarks/monker/HU50_step2_donk_rake.json out/monker/buckets_15x4 "<chart MonkerSolver HU 50a>" out/monker/variants/HU50_g1_rake
gtosd_preflop_blueprint_monker_values --config benchmarks/monker/HU50_step2_donk_rake.json ... --all-flops --threads 8 --expected-rake --out <values.json>
# 3-way: albero contro le chart (manifest o cartella), tabella esatta a tre giocatori
gtosd_preflop_blueprint_monker_tree --config benchmarks/monker/3WAY50_donk.json --manifest benchmarks/monker/3WAY50_tree_manifest.tsv --json <report>
gtosd_preflop_blueprint_game --config benchmarks/monker/3WAY50_donk.json --checkdown
gtosd_preflop_blueprint_three_way_table --resources-dir out/preflop_blueprint_resources --hero-classes all --threads 3 --output out/preflop_blueprint_resources/preflop_three_way_v1.bin --invariance KK,AKs,76s,AKo,T8o --brute-force 20 --dump-triples 300 --dump-class-triples 5 --dump-path out/three_way/webapp_dump.json > out/three_way/full_report.json
python tools/three_way_webapp_check.py out/three_way/webapp_dump.json
# fase 2b parte 1: checkdown per classi e controllo di equivalenza con il checkdown per combo (stesse iterazioni)
gtosd_preflop_blueprint_checkdown_classes --config benchmarks/monker/HU50.json --resources-dir out/preflop_blueprint_resources --output-dir <cartella> --iterations 2000 --reference <output di gtosd_preflop_blueprint_checkdown a 2.000 iterazioni>
# 30 settembre mattina: tutto il preflop bloccato nei giochi con rake (test 6-6d) e con altri bucket (5M-5D), scomposizione del guadagno
MAX=24000 THRESHOLD=0 BIN=out/monker/bin_rake TRAIN_ARGS="--board-texture-map benchmarks/monker/textures/texture_map_TX2_recommended.txt --lock-charts out/monker_lock/charts_50a --lock-nodes all" bash out/frozen/run_step2_continuous_rake.sh benchmarks/monker/HU50_step2_donk_rake5cap075.json out/monker/buckets_15x4 "<chart MonkerSolver HU 50a>" out/monker/variants/HU50_lock_all_rake5cap075
gtosd_preflop_blueprint_monker_values --config benchmarks/monker/HU50_step2_donk_rake5cap075.json ... --policy <run>/policy.bin --charts monker=<chart MonkerSolver HU 50a> --charts ours=<run>/charts/it_24000 --all-flops --threads 8 --expected-rake --out <run>/monker_values_exact.json
python <cartella temporanea della sessione>/decompose_br.py <run>/monker_values_exact.json "<etichetta>" > <run>/br_split.txt
MAX=24000 THRESHOLD=0 BIN=out/monker/bin_lock TRAIN_ARGS="... --lock-nodes all" bash out/frozen/run_step2_continuous_lock.sh benchmarks/monker/HU50_step2_donk.json out/monker/buckets_30x4 "<chart MonkerSolver HU 50a>" out/monker/variants/HU50_lock_all_m30x4
# B con rake, sul run G1 con rake (policy a 37.850 iterazioni, chart a 36.000)
gtosd_preflop_blueprint_monker_values --config benchmarks/monker/HU50_step2_donk_rake.json ... --policy out/monker/variants/HU50_g1_rake/policy.bin --charts monker=<chart MonkerSolver HU 50a> --charts ours=out/monker/variants/HU50_g1_rake/charts/it_36000 --all-flops --threads 8 --no-combo-values --expected-rake --exploit all --out <values.json> --exploit-out out/monker/variants/HU50_g1_rake/exploit.json --exploit-summary out/monker/variants/HU50_g1_rake/exploit.txt
# 3-way passo 1 (eseguibili out/monker/bin_3way_step1), controllo dell'albero, confronto, chart di MonkerSolver 3-way 50a valutate nello stesso gioco
gtosd_preflop_blueprint_checkdown_classes --config benchmarks/monker/3WAY50_donk_rake.json --resources-dir out/preflop_blueprint_resources --output-dir out/monker/step1_3way/3WAY50_rake_dead --iterations 10000 --report-every 1000 --threads 8 --folded-cards dead
gtosd_preflop_blueprint_monker_tree --config benchmarks/monker/3WAY50_donk_rake.json --charts out/monker/step1_3way/3WAY50_rake_dead/charts --manifest benchmarks/monker/3WAY50_tree_manifest.tsv
python tools/monker_compare/compare_charts.py out/monker/step1_3way/3WAY50_rake_dead/charts "<chart MonkerSolver 3-way 50a>" --json out/monker/step1_3way/3WAY50_rake_dead/vs_monker.json
gtosd_preflop_blueprint_checkdown_classes --config benchmarks/monker/3WAY50_donk_rake.json --resources-dir out/preflop_blueprint_resources --output-dir out/monker/step1_3way/3WAY50_rake_dead_monker_eval --iterations 0 --threads 8 --folded-cards dead --lock-charts "<chart MonkerSolver 3-way 50a>" --lock-nodes all
# controllo che le chart di un run lo riproducano (tutti i nodi bloccati, 0 iterazioni)
gtosd_preflop_blueprint_checkdown_classes --config <config> ... --iterations 0 --lock-charts <run>/charts --lock-nodes all --expect-summary <run>/summary.json --expect-tolerance 0.001
```

Opzioni aggiunte il 29 settembre (commit `5578ab8`):

- **Trainer**: `--policy-snapshots` salva accanto alle chart di ogni salvataggio la policy media di quella
  iterazione (`charts/it_<N>/policy.bin`, identica byte per byte a quella che `save_average_policy`
  scriverebbe in quell'iterazione, senza toccare lo stato del training); `--policy-snapshot-every N` (multiplo di `--chart-every`) la tiene solo ogni N
  iterazioni; `--policy-snapshot-reserve-gb` lascia libero spazio su disco (una policy che non ci sta viene
  saltata e registrata, il training continua). Nel runner: `POLICY_SNAPSHOTS=1`, `POLICY_SNAPSHOT_EVERY`,
  `POLICY_SNAPSHOT_RESERVE_GB`.
- **`tools/monker_compare/convergence_curve.py`**: valuta ogni salvataggio con una policy (migliore risposta
  esatta a carte vere, guadagni per giocatore, solo preflop e solo postflop, NashConv, perdita di
  MonkerSolver, distanza e differenza di range); gioco, bucket e mappa delle texture letti da `run.log`;
  risultati in `convergence.txt` e `convergence.json`, valutazioni già fatte riusate.
- **Costruttore dei bucket**: impostazioni per street (`--flop-levels`, `--flop-tiers`, `--turn-levels`,
  `--turn-tiers`, `--river-levels`, che prevalgono su `--levels` e `--tiers` per la singola street) e
  `--flop-exact` (un id per ogni
  orbita dei semi delle combo vive del flop); le tabelle di default restano identiche byte per byte.
- **`gtosd_preflop_blueprint_monker_values`**: `--exploit NOME[,NOME...]` o `all` (migliore risposta esatta
  contro il preflop di un insieme di chart, con il nostro postflop, accanto alla nostra policy),
  `--exploit-heroes CO,BTN`, `--exploit-streets`, `--exploit-out` (JSON), `--exploit-summary` (testo).

Opzioni e strumenti aggiunti nella notte del 29-30 settembre (5.9, 9):

- **Trainer** (`c7d6ba0`): `--lock-charts DIR --lock-nodes FILE[,FILE...]` o `all` (nodi preflop bloccati alle
  righe di un set di chart nel formato MonkerSolver; impronta del blocco nell'identità del trainer). Nel runner
  il cambiamento medio conta le chart bloccate come 0: la soglia va scalata (5.9).
- **Rake nel gioco** (`3ec4027`): chiavi `rake_mode` `"enabled"`, `rake_basis_points`, `rake_cap_units`,
  `rake_no_flop_no_drop`, `rake_minimum_pot_units`; `gtosd_preflop_blueprint_monker_values --expected-rake`
  (rake atteso e somma degli EV); il checkdown non presuppone più la somma zero.
- **Primo raise relativo al piatto** (`518bbfa`): `preflop_open_sizes_basis_points` (per esempio `[10000]` =
  100 % del piatto dopo la call), scritta solo se presente.
- **`gtosd_preflop_blueprint_monker_tree`** (`670f8fc`, `484208c`): controlla l'albero di una configurazione
  contro una cartella di chart (`--charts`) o un manifest (`--manifest`); `--write-manifest` scrive il manifest
  e rifiuta una cartella con chart illeggibili. Manifest in `benchmarks/monker/{3WAY50,3WAY100,HU50}_tree_manifest.tsv`.
  Oracolo Python indipendente: `tools/monker_compare/monker_tree_oracle.py`.
- **`gtosd_preflop_blueprint_game --checkdown`** (`9c8f845`): l'albero del passo 1 (flop chiuso da uno showdown).
- **Nomi delle chart multiway** (`6cb4a70`): `chart_nodes` omette il primo fold di un giocatore, come
  MonkerSolver, e rifiuta nomi duplicati; il checkdown scrive le chart attraverso `chart_nodes` (`06dd436`).
- **Tabella a tre giocatori** (`44a8a5a`-`fcf1ed9`, `ea1ef59`): libreria `card_abstraction/three_way_table`,
  programma di costruzione e validazione `gtosd_preflop_blueprint_three_way_table` (V1-V10), controllo con la
  DLL del calcolatore dell'utente `tools/three_way_webapp_check.py`.
- **`gtosd_preflop_blueprint_checkdown_classes`** (`76ed735`, `41cd7e0`): DCFR vettoriale del passo 1 sulle 81
  classi per 2 o 3 posti, EV e guadagno della migliore risposta per posto, `--reference` (controllo di
  equivalenza con il checkdown per combo); i terminali a tre sono ancora rifiutati (fase 2b parte 2, 9.4).
  **Superato alle 04:28** (sotto).
- Script di analisi fuori dal repository (cartella temporanea della sessione): `lock_check.py` (criterio del
  5.8), `decompose_br.py` (guadagno della migliore risposta preflop per nodo e azione), `root_mix.py` (radice
  del CO per salvataggio), `suited_pref.py`.

Opzioni e configurazioni aggiunte la mattina del 30 settembre (5.10, 9.4, 9.5):

- **`gtosd_preflop_blueprint_checkdown_classes`, fase 2b parte 2 e revisione** (`e4daf68`, `af9cf15`, `2184d66`,
  `67de1d7`): terminali a tre posti dalla tabella esatta (showdown a tre attivi per insieme di vincitori; a due attivi
  dopo un fold con le carte di chi ha foldato morte e la trasposta; fold e posti che hanno foldato dal tensore dei
  conteggi); `--threads N`; `--folded-cards dead|ignore` (solo 3 posti, default `dead`); `--lock-charts DIR
  [--lock-nodes all|a,b,...]` (righe bloccate come nel trainer; `--iterations 0` ammesso solo con tutti i nodi
  bloccati, cioè per valutare un set di chart da solo; `--lock-nodes` senza `--lock-charts` rifiutato); la riga `lock:`
  e il riepilogo contano le righe (righe delle chart + righe fuori range + righe nulle raggiunte = nodi × 81) e il
  programma si ferma se non tornano; `--expect-summary FILE [--expect-tolerance 0.001]` confronta EV e guadagni con un
  run di origine (rifiuta un riepilogo di un'altra configurazione, albero, numero di posti o convenzione delle carte
  foldate); nel riepilogo, per ogni chart, posto, reach propria e guadagno locale (deviare nel solo nodo: i guadagni
  locali non si sommano al guadagno completo). `monker_chart_lock.hpp`: `chart_lock_seats` per N posti (`chart_lock`
  HU invariato).
- **Configurazioni non committate** delle ipotesi di rake (no flop no drop; cap in unità, 1 ante = 10.000):
  `HU50_step2_donk_rake5cap2.json` e `HU50_step2_donk_rake25cap2.json` (dalla notte), `HU50_step2_donk_rake5cap075.json`
  (04:35, 500 punti base e cap 7.500), `3WAY50_donk_rake5cap075.json` (05:35) e `3WAY50_donk_rake25cap2.json` (08:40,
  250 punti base e cap 20.000); id `MONKER-{HU50-STEP2,3WAY50}-DONK-RAKE5-CAP075-001`, `...-RAKE25-CAP2-001`,
  `MONKER-HU50-STEP2-DONK-RAKE5-CAP2-001`.

Eseguibili congelati per i run lunghi: `out/monker/bin_allin` (da `410a380`: G2, G3, G4, E, F),
`out/monker/bin_abd` (da `5578ab8`: A, B, D), `out/monker/bin_lock` (costruito alle 20:27 dal codice poi committato
come `c7d6ba0` alle 20:43, a revisione conclusa: test 1, 2, 5 e 5M-5D; `out/monker_lock/bin` è una copia precedente
delle 20:15), `out/monker/bin_rake` (da `3ec4027`: passo 1 con rake, G1 con rake, test 6, 6b, 6c, 6d e B con rake),
`out/monker/bin_threeway` (programma della tabella a tre giocatori, costruzione completa del 30 alle 02:27) e
`out/monker/bin_3way_step1` (da `67de1d7`, copiati alle 05:35: `gtosd_preflop_blueprint_checkdown_classes` e
`gtosd_preflop_blueprint_monker_tree` per i run 3-way del passo 1, 9.5). Copie congelate del runner in
`out/frozen/` (`run_step2_continuous.sh`, `run_step2_continuous_abd.sh`, `run_step2_continuous_lock.sh`,
`run_step2_continuous_rake.sh`): devono stare due livelli sotto la radice del repository, perché il runner calcola
la radice dal proprio percorso; una copia nella cartella temporanea fallisce alla partenza. Un runner in uso non va
modificato (bash legge lo script mentre lo esegue: incidente di G4, 5.7).

Viewer web del confronto (artifact privato dell'utente): tre griglie 9x9 per nodo (MonkerSolver,
nostro, differenza), selettore della sorgente (passo 1, ogni salvataggio del passo 2 e delle varianti),
tabella dei run (distanza, differenza di range, cambiamento, preferenza suited), grafico di convergenza
per run e nodo, combo effettive per mano, per nodo e per azione, frequenze divise per coppie, suited e
offsuit, tabella "MonkerSolver nel nostro gioco".

Commit della giornata sul branch `feat/monker-step1-checkdown`: e576396 (passo 1), 7ff711b (passo 2),
b4f7956 (documenti del pomeriggio), e9b5c93 (campo di `ActionConfig` spostato in fondo: rompeva
l'inizializzazione per posizione di `core_tests`), 7a34a0a (chart dal trainer vivo, file di stop,
differenza di range), c9260ba (differenza di range sulle combo effettive, argomenti extra del runner),
364fd5e (MonkerSolver nel nostro gioco), 6de08ae (turn in texture).

Commit del 29 settembre: d92cea1 e ce82708 (documenti della notte e correzioni del verificatore), 410a380
(all-in postflop fino a 5 volte il piatto, opzionale), 27d65b0 (documenti di G1-G3, configurazioni di G4),
5578ab8 (policy a ogni salvataggio e `convergence_curve.py`, bucket per street e flop esatto, migliore
risposta contro il preflop delle chart; 44/44 test `preflop_blueprint` e test di base PASS, 4 rilievi della
revisione corretti prima del commit), dc7858b (documenti dei test del 29 e piano della radice bloccata),
c7d6ba0 (blocco dei nodi preflop; 47/47 e test di base PASS, revisione senza difetti del CFR).

Commit della notte del 30 settembre: 3ec4027 (rake nel gioco preflop; 54/54 e test di base PASS, senza rake
output identici byte per byte, revisione senza difetti); fase 1 del 3-way (ramo di lavoro 1): 518bbfa (primo
raise relativo al piatto), 6cb4a70 (nomi dei fold multiway in `chart_nodes`), 670f8fc (controllo dell'albero
MonkerSolver), 9c8f845 (`--checkdown` nel tool del gioco), 32ecef7 (fixture 3WAY50 con rake); fase 2a (ramo di
lavoro 2): 44a8a5a (tabella esatta a tre giocatori), 93d805d (test e programma di validazione), 80e7afc (kernel
con istogrammi per carta, byte di output invariati), b7d42a1 (controllo con la DLL del calcolatore, V9),
fcf1ed9 (commento del kernel); merge 6c17b5c (fase 1) e ad51088 (fase 2a), entrambi puliti; seguiti 484208c
(controlli del rake a tre giocatori, `--write-manifest` coerente con `--manifest`), ea1ef59 (V6 obbligatorio,
tabelle incomplete caricate solo su richiesta), 06dd436 (chart del checkdown attraverso `chart_nodes`). Dopo
l'integrazione `ctest -L preflop_blueprint` 65/65, test di base 2/2, output HU identici byte per byte;
revisione senza difetti (sezione 9). Parte 1 della fase 2b (03:49): 76ed735 (checkdown per classi a 2 o 3
posti, `gtosd_preflop_blueprint_checkdown_classes`; terminali a tre ancora rifiutati) e 41cd7e0 (controllo di
equivalenza HU e test; `ctest -R checkdown` 12/12, 9.4).

Commit della mattina del 30 settembre: e4daf68 e af9cf15 (04:28, fase 2b parte 2: terminali a tre, blocco delle chart
per N posti; test di forza bruta, identità del rake, smoke e blocco), a319d0c (04:34, documenti della notte), 2184d66 e
67de1d7 (05:20, correzioni dei tre rilievi della revisione: blocco controllato dal conteggio delle righe e da
`--expect-summary`, blocco parziale con 0 iterazioni rifiutato, tolleranze stampate con 12 cifre; test del blocco a tre
posti). `ctest -L "preflop_blueprint|card_abstraction"` 90/90 (743 s); output HU identici byte per byte (9.4). Commit
solo locali: il branch non è pushato.

## 7. Altri risultati del 28 settembre

- **Repository equity-calculator-web-app dell'utente:** classifica short deck identica alla nostra (il
  README dice il contrario, il codice è corretto); utile come controllo indipendente (enumerazione
  esatta fino a 4 mani), non per costruire le tabelle a tre giocatori o i kernel.
- **Alberi MonkerSolver 3-way a 50a:** tutte le size sono un raise del 100 % del piatto: open 6a,
  isolation del CO dietro il limp dell'UTG 7a, raise del BTN sul solo limp dell'UTG 6a, su due limp 7a; serve una modalità "raise al 100 % del piatto" nel costruttore.
  Il set HU 40a dell'utente ha un albero diverso (open 6a e 10a, 3-bet 10,5a e 14,5a). **Aggiornamento del 30
  settembre (9.1)**: la modalità esiste (`preflop_open_sizes_basis_points`, commit `518bbfa`) e ricostruisce
  esattamente i 54 file del 3-way a 50a; le cartelle 3-way "40a" e "60a" sono identiche e contengono il calcolo a
  60a.
- **Stime multiway:** motore 3-way completo circa 6-7 giorni per le prime chart; passo 1 in 3-way
  circa 3 giorni (serve la tabella di equity a tre giocatori). **Correzione del 30 settembre (sezione 9)**: la
  tabella esatta a tre giocatori è stata progettata, scritta, validata e costruita nella notte (progetto
  23:53-00:16 e critica 00:16-00:46, costruzione completa 02:27-02:40, 108 s di calcolo); il trainer del passo
  1 a tre giocatori (fase 2b) è partito alle 03:29: parte 1 (checkdown per classi, controllo di equivalenza HU)
  committata alle 03:49, parte 2 (terminali a tre) in corso dalle 03:51. La stima di 3 giorni per il passo 1
  era troppo alta almeno per la tabella. **Aggiornamento del 30 settembre mattina (9.4, 9.5)**: la fase 2b è finita
  alle 05:20 (revisione e correzioni comprese) e le prime chart 3-way del passo 1 sono uscite alle 07:58: dal progetto
  della tabella (23:53 del 29) al trainer del passo 1 a tre giocatori pronto sono passate circa 5 ore e mezza. La stima
  di circa 3 giorni era troppo alta anche per il passo 1.
- **Server a noleggio per i test** (prezzi del 28/09): phoenixNAP d1.c4.medium (2 x Gold 6258R, 56
  core, 256 GB, circa 0,68 euro l'ora, Windows a ore), Hetzner asta dell'usato (EPYC 7502P, 32 core,
  384 GB, circa 0,25 euro l'ora), AWS m5.metal spot a Milano (circa 0,46 euro l'ora); Cherry 2 x Gold
  6230R esaurito il 28/09.

## 8. Prossimi passi

Fatti il 28 settembre: risultati del passo 2 (5), varianti (5.2), export senza fermare il training, MonkerSolver
nel nostro gioco (5.4), turn in texture (5.5). Fatti il 29 settembre: giochi G0c-G3 sull'astrazione compatta (5.6);
G4 con due size, bucket più fini (E, F), flop esatto (D), rumore del seed e curva di convergenza esatta fino a
32.000 iterazioni (A), migliore risposta contro il preflop delle chart (B) (5.7). Fatti nella notte del 29-30
settembre: blocco dei nodi preflop e test 1, 2 e 5, rake nel gioco, passo 1 e G1 con rake, convenzioni di
MonkerSolver dagli EV del 3-way a 60a (5.9); fasi 1 e 2a del 3-way e loro integrazione, parte 1 della fase 2b
(sezione 9). Fatti la mattina del 30 settembre: test 6, 6b, 6c e 6d (preflop di MonkerSolver bloccato nei giochi con
rake), B con rake, test 5M, 5E, 5F e 5D (preflop bloccato con altri bucket) (5.10); fase 2b e prime chart 3-way del passo
1 (9.4, 9.5). Stato dei punti aperti il 28 (i punti 1 e 3 del 28 sono superati dalle misure e dalle decisioni del
29):

1. **Configurazione HU di riferimento: decisa il 29 settembre.** G1: astrazione compatta 15 livelli × 4 +
   TX2 con donk bet, una size (bet e raise al 100 % del piatto), all-in postflop senza limite (il limite a 5
   volte il piatto è respinto). Il 28 era aperto (donk bet sì o no, compatta + donk da provare): provato, G1
   è il più vicino a MonkerSolver fra i giochi G0c-G3 (0,0641 / 0,350, 5.6); D, E ed F arrivano a
   0,0621-0,0625 / 0,339-0,342 (0,0016-0,0020 / 0,008-0,011 sotto G1, meno delle soglie 0,005 / 0,02
   del 5.1), spostano le chart di 1,2-1,6 volte il rumore e costano 1,3-6,3 volte la memoria: G1 resta il
   riferimento. **Riaperto il 30 settembre**: resta da scegliere il rake (nessuno; 5 % / cap 3a; 5 % / cap 2a;
   2,5 % / cap 2a) e, con i test 5M-5D, l'astrazione. Si decide con i risultati dei test 6, 6b e 6c, del B con
   rake e dei test 5M, 5E, 5F e 5D (5.9). G1 con rake 5 % / cap 3a abbassa l'open a 5a (3,5 %) ma allontana
   le chart da MonkerSolver (0,0837 / 0,481 a 36.000). **Aggiornamento del 30 settembre mattina (5.10)**: i test
   sono fatti e non identificano un'impostazione. Nessuna ipotesi di rake chiude lo scarto del test 5 (CO 0,92-1,13 %
   del piatto contro 1,44 %); la somma dei guadagni è più bassa con 2,5 % / cap 2a (1,40 %), i call contro gli all-in
   sono senza deviazioni misurabili solo con il cap di 0,75a (aggiunto come test 6d; indizio: negli altri giochi gli
   scarti sono di 0,0001-0,0019 a e con quel cap è stata provata una sola percentuale). Un postflop più fine (30 × 4,
   30 × 8, 60 × 8, flop esatto) allarga lo scarto (CO 1,60-1,80 %): l'astrazione non è la leva; proposta: G1 resta il
   riferimento anche per l'astrazione. Da decidere con l'utente: questa proposta e il rake della configurazione di
   riferimento (nessuno, 5 % / cap 3a, 5 % / cap 2a, 2,5 % / cap 2a, 5 % / cap 0,75a).
2. **Criterio di somiglianza**: la perdita di MonkerSolver nel nostro gioco (al massimo lo 0,07 % del piatto
   nei giochi fermati a 0,01, lo 0,09 % in A a 32.000 iterazioni) dice che le chart sono equivalenti in EV.
   La migliore risposta contro il preflop delle chart (B) non la contraddice: con il preflop di MonkerSolver
   la sfruttabilità cambia di −0,74 % (CO) e +0,83 % (BTN) del piatto, poco rispetto alla NashConv del
   nostro gioco (29,9 %) ma circa 20 e 55 volte la perdita nel nostro gioco. La distanza (0,064 per G1) resta
   come misura descrittiva, con soglia di rumore fra chart 0,0088 / 0,0425 sul gioco G1 a 16.000 iterazioni.
   **Precisazione del 30 settembre (5.9)**: l'equivalenza in EV è locale (chart giocate contro la nostra
   strategia). Il test 5 (tutto il preflop bloccato, postflop addestrato contro i suoi range) misura se il
   preflop di MonkerSolver è stabile contro il postflop appreso sui suoi range: 1,44 % / 0,54 % del piatto
   senza rake, oltre la soglia di 0,03 a per il CO; non prova che nessun postflop lo renda un equilibrio (metà
   del guadagno del CO sta nell'open a 5a, linea rara nelle chart). La distanza dalle chart non è un test di
   correttezza; i confronti in EV sì. **Aggiornamento del 30 settembre mattina (5.10)**: nel gioco con rake 5 % / cap
   3a il test B dà l'opposto del gioco senza rake per il CO: in differenza dei guadagni le chart del CO di
   MonkerSolver sono più sfruttabili delle nostre di +4,01 % del piatto (senza rake −0,78 %, cioè −0,0233 a nel 5.7;
   la differenza delle migliori risposte citata sopra è −0,74 %), soprattutto nel piatto limpato, dove le chart di
   MonkerSolver portano il CO il doppio delle volte (limp 29,0 % contro 14,7 %) e il nostro postflop è già
   sfruttabile; quelle del BTN di +1,42 % (senza rake +0,82 %; migliori risposte +0,83 %). È il limite noto del
   test B; la misura pulita resta il test 5 e i suoi seguiti. Proposta: criterio di somiglianza in EV (perdita nel
   nostro gioco e test con il preflop bloccato), la distanza solo descrittiva; da confermare con l'utente.
3. **Seconda size postflop: fatta** (G4, 5.7) sul PC di sviluppo grazie all'astrazione compatta (picco
   15,67 GB, non gli 80 GB stimati il 28): alla pari con i donk bet è la leva più grande sulle chart fra quelle
   provate, ma allontana da MonkerSolver (0,0663 contro 0,0641); non adottata (criterio della notte: il gioco
   più vicino a MonkerSolver a parità di EV, 5.6).
4. **Radice del CO** (open a 5a al 7,0-7,7 % in G1 e nelle sue varianti di astrazione, in calo con le
   iterazioni (6,7 % in A a 32.000), contro lo 0,5 % di MonkerSolver; nessuna modifica provata lo avvicina):
   prossimo test proposto, varianti dell'albero delle azioni solo nei piatti rilanciati (size al flop più
   piccola, solo check o all-in a SPR basso), oppure le impostazioni dell'albero MonkerSolver dell'utente, se
   disponibili (chieste all'utente). Da decidere con l'utente. Prima, la notte del 30: test della radice
   bloccata (5.8), che dice se la radice è la causa comune dello scarto nel piatto limpato. **Aggiornamento
   del 30 settembre (5.9)**: i test della radice bloccata (1 e 2) sono falliti, ma con un criterio sbagliato
   in principio e non dicono nulla sulla causa; nel test 5 la migliore risposta al preflop di MonkerSolver vuole
   aprire di più anche con il postflop addestrato contro i suoi range, ma quella linea è rara nelle chart (0,5 %)
   e il postflop del BTN vi risponde a un range ristretto, quindi non basta per attribuire la spinta al nostro
   gioco; il rake 5 % / cap 3a porta l'open al 3,5 % (G1 con rake a 36.000) ma dimezza il limp.
   L'utente non conosce le impostazioni di MonkerSolver usate per le chart: non chiederle più. **Aggiornamento del
   30 settembre mattina (5.10)**: con il preflop di MonkerSolver bloccato la spinta verso l'open a 5a cresce con un
   postflop più fine (0,021 -> 0,022-0,025 a) e resta con il rake 2,5 % / cap 2a (0,014 a); con il rake 5 % scende a
   0,007-0,010 a e la migliore risposta vuole soprattutto più all-in.
5. **Regola di arresto per il 3-way**: proposta una soglia di 0,005 fra salvataggi consecutivi (o il doppio
   delle iterazioni dell'arresto a 0,01): lo 0,01 basta per il preflop ma non per la posizione finale delle
   chart (5.7). Da decidere con l'utente. Ancora aperto il 30 settembre (G1 con rake è stato fermato con la
   soglia 0,005, a 32.000 iterazioni), anche alle 08:50. Il passo 1 a tre giocatori non la usa (10.000 iterazioni,
   guadagno massimo sotto 2e-4 % del piatto, 9.5); serve per la fase 3.
6. **3-way 50a, fase 1 rinviata** (decisione dell'utente delle 20:00 del 29: si parte quando il problema della
   radice è capito meglio, probabilmente dopo le 3 del 30 settembre): albero identico a quello
   delle chart 3-way a 50a dell'utente (54 file di chart); raise al 100 % del piatto nel costruttore, regole
   sparse (cold call), tabella di equity a tre giocatori, motore multiway, astrazione compatta; confronto con
   le chart 3-way dell'utente con gli stessi strumenti (distanza, differenza di range, MonkerSolver nel
   nostro gioco). **Aggiornamento del 30 settembre (sezione 9)**: l'utente ha fatto partire subito le fasi 1 e
   2. Fatte: fase 1 (albero identico ai 54 file con il solo primo raise al 100 % del piatto: nessuna regola
   sparsa sulle cold call serve) e fase 2a (tabella esatta a tre giocatori), integrate nel ramo; parte 1 della
   fase 2b (checkdown per classi, controllo di equivalenza HU superato) committata alle 03:49. In corso dalle
   03:51: fase 2b parte 2 (terminali a tre dalla tabella, chart a tre posizioni). Prossimi: prime chart 3-way
   del passo 1 e loro confronto con le 54 chart di MonkerSolver; la fase 3 (postflop sparso a tre giocatori)
   dopo la fase 2b, con le impostazioni che aspettano la configurazione HU di riferimento e la scelta del rake.
   **Aggiornamento del 30 settembre mattina (9.4, 9.5)**: fase 2b fatta alle 05:20 (terminali a tre, blocco delle
   chart per N posti, revisione con tre rilievi corretti, 90/90 test); prime chart 3-way del passo 1 alle 07:58, in
   cinque giochi (senza rake; 5 % con cap 3a, carte foldate morte o ignorate; 5 % con cap 0,75a; 2,5 % con cap 2a):
   distanza da MonkerSolver 0,245-0,288, differenza di range 0,699-0,736, come il passo 1 HU. Prossimo: la fase 3
   (kernel di showdown a tre attivi nel trainer, migliore risposta e certificatore a tre), dopo le decisioni del punto 9.
7. **Risultati in attesa (30 settembre)**: test 6, 6b e 6c (preflop di MonkerSolver bloccato nei tre giochi con
   rake), test B con rake, test 5 con i bucket M, E, F e D (5.9). Da questi: configurazione HU di riferimento e
   scelta del rake (punto 1). **Arrivati la mattina del 30 (5.10)**, con il test 6d in più; esito nei punti 1, 2 e 4.
8. **Test che manca**: la migliore risposta preflop contro un postflop risolto esattamente per flop (5.9, "cosa
   vuol dire corretto"). Non costruito; costo da stimare.
9. **Decisioni in attesa dell'utente** (colloquio chiesto dall'utente verso le 12:00 del 30):
   - HU: impostazione del rake e configurazione di riferimento (punto 1);
   - criterio di somiglianza in EV (punto 2);
   - regola di arresto 0,005 (punto 5);
   - push del branch `feat/monker-step1-checkdown` (commit solo locali);
   - correzione del campionamento distorto del multiway con range nel calcolatore web dell'utente (9.2);
   - 3-way: rake e convenzione delle carte foldate per il passo 2 (fase 3); le carte morte sono quelle confermate
     dagli EV del set a 60a (5.9);
   - bucket della fase 3: 30 × 4 (default di MonkerSolver, 27,11 GB in double) o 15 × 4 (13,56 GB) (5.10, 9.1).

## 9. 3-way a 50a (dalla notte del 30 settembre)

Obiettivo: le chart MonkerSolver 3-way a 50a dell'utente (`GTO-Chart-Browser/ranges/Short Deck/Symmetrical
Chart/3-way/50a`, 54 chart di decisione: UTG 16, CO 18, BTN 20). Decisione dell'utente della notte: fasi 1 e 2
subito, in parallelo alle prove HU (revoca il rinvio delle 20:00 del 29); le impostazioni della fase 3
aspettano l'HU. Lavoro in due rami di lavoro separati (fase 1; fase 2a), poi integrato nel ramo
`feat/monker-step1-checkdown` dopo il commit del rake. Specifiche di progetto, così come scritte, con le loro
critiche indipendenti: [fase 1](threeway/PHASE1_SPEC_2026-09-29.md), [fase 2a](threeway/PHASE2A_SPEC_2026-09-30.md).

### 9.1 Fase 1: l'albero delle chart

**Regole lette dai 54 file** (un oracolo Python indipendente ricostruisce esattamente i 54 file: stessi
percorsi, stesse azioni in ogni nodo):

- Convenzione del denaro di HU50: ante morto di 1a per giocatore, blind vivo di 1a del BTN, stack di 50a
  compreso l'ante (49a vivi); piatto alla radice 4a.
- Un solo raise dimensionato per mano, al 100 % del piatto dopo la call: 6a per l'open dell'UTG, per l'open
  del CO dopo il fold dell'UTG e per il raise del BTN su un limp; 7a per l'isolation del CO sul limp dell'UTG e
  per il raise del BTN su due limp. Dopo il primo raise solo l'all-in; l'all-in è disponibile in ogni decisione
  che non affronta un all-in.
- Limp, over-limp, cold call e call multiway di un all-in sono tutti ammessi: nessuno dei filtri multiway di
  MonkerSolver (niente cold call quando restano altri da agire) è in uso in questo albero.
- Nomi dei file: MonkerSolver omette il fold che è la prima azione di un giocatore (`UTG_6.0ante_BTN` = UTG
  rilancia, CO folda, BTN agisce) e scrive i fold successivi (`..._UTG_Fold_CO`); 19 nomi su 54 contengono un
  fold omesso. I nomi HU non cambiano.
- Dalla critica: anche il set 3-way a 100a si ricostruisce con la stessa regola, quindi l'albero non ha una size
  di re-raise (non è un 3-bet convertito in all-in da una soglia); le cartelle 3-way "40a" e "60a" sono
  identiche e contengono il calcolo a 60a; il 60a scrive gli importi con uno scarto (gettone = importo vivo +
  1a per UTG e CO, + 2a per il BTN), stessa regola.

**Motore e strumenti** (commit `518bbfa`, `6cb4a70`, `670f8fc`, `9c8f845`, `32ecef7`):

- Chiave `preflop_open_sizes_basis_points` (`[10000]` = 100 % del piatto): il livello 0 usa le size in
  percentuale del core invece dei target fissi; scritta solo se presente (impronte HU invariate; HU50 con i donk
  ricostruito in modalità piatto dà gli stessi 571 nodi).
- Fixture `benchmarks/monker/3WAY50_donk.json`, `3WAY50_donk_allin5x.json`, `3WAY100_donk.json` e
  `3WAY50_donk_rake.json` (5 %, cap 3a, no flop no drop; il rake 3-way controllato in ogni terminale: 25 fold
  preflop senza rake, 36 all-in preflop al cap di 3a, 3.701 terminali postflop con rake).
- `gtosd_preflop_blueprint_monker_tree`: 3-way 50a 54 file su 54, 0 mancanti, 0 in più, 0 azioni diverse, 6
  raise dimensionati tutti secondo la regola del piatto; 3-way 100a 54 su 54; HU 50a 8 su 8.
- `gtosd_preflop_blueprint_game --checkdown` per l'albero del passo 1.

**Albero e dimensioni** (regole postflop di G1, astrazione compatta 15 × 4 + TX2): preflop 130 nodi, 54
decisioni, 25 fold, 36 all-in con runout, 15 ingressi al flop (11 a due, 4 a tre).

| Gioco | Nodi | Decisioni flop / turn / river | Celle | Double | Float32 |
|---|---:|---|---:|---:|---:|
| 3WAY50_donk | 7.225 | 330 / 936 / 1.802 | 847.242.189 | 13,56 GB | 6,78 GB |
| 3WAY50_donk_allin5x | 7.126 | 317 / 923 / 1.789 | 835.014.699 | 13,36 GB | 6,68 GB |
| 3WAY100_donk | 12.604 | | 1.460.565.009 | 23,37 GB | 11,68 GB |
| Passo 1 (checkdown) | 130 | | 10.449 | | |

Più 0,77 GB di timestamp dello sconto lazy per 3WAY50_donk. Il solo piatto limpato a tre vale il 47 % delle
celle, i quattro ingressi al flop a tre il 77 %; con i bucket 30 × 4 + TX2 le celle sarebbero 1.694.473.929
(27,11 GB in double). Il trainer, la migliore risposta, il certificatore e l'export completi accettano ancora
solo due giocatori: la fase 3 richiede un kernel di showdown a tre attivi (nessuna stima di tempo per
iterazione finché non esiste).

### 9.2 Fase 2a: tabella esatta a tre giocatori

**Contenuto** (commit `44a8a5a`, `93d805d`, `80e7afc`, `b7d42a1`, `fcf1ed9`): una voce per ogni terna ordinata
di classi (classe dell'eroe, primo avversario, secondo avversario), 81³ = 531.441 voci, calcolate per una combo
rappresentativa dell'eroe: il numero N di coppie di combo avversarie disgiunte fra loro e dall'eroe e 9
conteggi sui C(30,5) = 142.506 runout (eroe contro il primo avversario meglio / pari / peggio, per eroe contro il
secondo). 40 byte per voce, payload di 21.257.810 byte, file di 21.257.895 (21,3 MB). La tabella è esatta per
il passo 1, non un'astrazione: il gioco del checkdown è simmetrico nei semi, quindi un DCFR per classi coincide
con quello per combo. Serve ogni terminale del passo 1: checkdown e all-in a tre, showdown a due dopo un fold
(le carte di chi ha foldato sono **morte** per default, esatte, lette dalla voce eroe / avversario / chi ha
foldato; il commutatore `FoldedCards::Ignored` le ignora) e fold. Il rake resta nelle righe dei payoff del
motore, non nella tabella.

**Costruzione**: per ogni board il prodotto esterno dell'istogramma delle chiavi meno una correzione per le combo
che condividono una carta; un board per orbita dei semi dell'eroe (default, byte identici all'enumerazione
completa). **Validazione** V1-V10: identità intere (V1-V5), V6 contro la tabella HU (`preflop_all_in_v1.bin`:
sommati sulla mano foldata, i conteggi a due valgono 351 volte quelli HU), V7 stessa voce per ogni combo di una
classe, V8 forza bruta con il valutatore, V9 la DLL del calcolatore dell'utente, V10 salvataggio e ricarica. Dalla
revisione: V3 è strutturale in questo formato (non può fallire); V6 veniva saltato in silenzio senza la tabella HU
e le tabelle parziali si caricavano senza avviso (entrambi corretti in `ea1ef59`).

**Costruzione completa** (30/09 02:27-02:40, `out/monker/bin_threeway`, 3 thread accanto al training;
`out/three_way/full_report.json`):

| Voce | Risultato |
|---|---|
| Costruzione | 107,7 s di parete (145 s di CPU), 81 classi |
| Identità V1-V6 | 0 errori (V1 531.441 controlli, V2 1.594.323, V3 531.441, V4 1.062.882, V5 531.441, V6 26.244) |
| Invarianza (KK, AKs, 76s, AKo, T8o) | 0 voci diverse |
| Forza bruta | 20 terne, 0 differenze |
| Spostamento massimo di equity dovuto alle carte foldate | 0,208 (KJo contro 66 con 66 foldato) |
| Output | `out/preflop_blueprint_resources/preflop_three_way_v1.bin`, impronta `fnv1a64:31f1bb691ff8a4e8`, PASS |

V9 (DLL `equity-calculator-web-app/cpp/build/Release`, chiamata direttamente con le singole combo) è PASS sulla
tabella parziale del ramo di lavoro (AA, AKs, AKo): 300 terne di combo con errore massimo 1,66e-13 e 5 terne di
classi con 7,6e-14 per coppia; il test unitario verifica che quelle righe coincidono con la tabella completa. Le
300 terne di combo controllano la classifica short deck, non le voci; le 5 terne di classi toccano la tabella.
V9 è PASS anche sulla tabella completa: `three_way_webapp_check.py` (copia in `out/three_way/`, identica a quella
in `tools/`) su `out/three_way/webapp_dump.json` alle 02:40-02:42, 300 terne di combo e 5 terne di classi entro
la tolleranza di default di 1e-12 (358 chiamate alla DLL). L'esito è solo nell'output del task nella cartella
temporanea della sessione, non in `out/three_way/`. Il calcolatore dell'utente enumera esattamente fino a 4
mani specifiche (oltre, o sopra 1.000.000 di runout, passa al Monte Carlo); il suo multiway con range
(`multiwaySimulation.ts`, 3-6 giocatori) è Monte Carlo con un campionamento sequenziale dei range, distorto
quando i range si bloccano (mostrato su AK+KQ contro AA contro JJ confrontato con un campionamento per rifiuto).

Stime e realtà: il progetto stimava 10-20 minuti di costruzione su un i3 libero (40 come limite), la critica
20-80 minuti con la macchina condivisa; la costruzione completa ha preso 108 s. La stima del 28 settembre di
circa 3 giorni per il passo 1 in 3-way (sezione 7) è superata per la tabella.

### 9.3 Integrazione

Merge `6c17b5c` (fase 1) e `ad51088` (fase 2a), entrambi puliti (alberi identici a quelli calcolati da
`git merge-tree`); seguiti `484208c`, `ea1ef59`, `06dd436` (sezione 6). Build senza avvisi; `ctest -L
preflop_blueprint` 65/65, test di base 2/2, test del gioco 1.856.521 asserzioni, test a tre giocatori 20.536
asserzioni; chart HU50 e HU50 con rake identiche byte per byte prima e dopo lo spostamento del checkdown su
`chart_nodes`. Revisione indipendente senza difetti. La nota della revisione (i tre CTest della tabella non
rieseguiti dopo la modifica del messaggio di errore delle 03:14) è chiusa: `ctest -R three_way_table` alle
03:31-03:32 sull'ultimo binario (ninja non aveva nulla da ricostruire; binario delle 03:15, dopo `ea1ef59`),
3/3 PASS (`_smoke` 23,66 s, `_subset_name_refused` 0,05 s, `_v6_required` 0,09 s).

### 9.4 Fase 2b (dalle 03:29 alle 05:20: fatta)

Trainer del passo 1 a tre giocatori, lanciato come workflow in due parti (parte 1 dalle 03:29 alle 03:51,
parte 2 dalle 03:51), seguite da una revisione indipendente e dalle correzioni; ultimi commit alle 05:20. Il titolo
delle 03:55 ("parte 2 in corso") è superato.

**Parte 1** (commit `76ed735` e `41cd7e0` delle 03:49): programma `gtosd_preflop_blueprint_checkdown_classes`,
DCFR vettoriale sulle 81 classi per 2 o 3 posti, con gli sconti e l'alternanza del checkdown per combo estesi a
ogni posto, EV e guadagno della migliore risposta esatti per posto (con tre giocatori il CFR non ha garanzie di
Nash: la qualità si misura con il guadagno di ogni posto), chart attraverso `chart_nodes`, `--reference` per il
controllo di equivalenza. In HU i terminali vengono da `preflop_all_in_v1.bin` aggregato a 81 × 81 al
caricamento (nessuna risorsa nuova); con tre posti i terminali sono ancora rifiutati. Controlli nel run: i payoff
di ogni terminale sommano a meno il suo rake, la somma degli EV è meno il rake atteso entro 1e-9.

**Controllo di equivalenza HU superato** (classi contro combo, stesse iterazioni, un thread ciascuno):

| Run | Chart | Differenza di EV | Differenza di guadagno | Differenza di rake atteso |
|---|---|---:|---:|---:|
| HU50, 100 iterazioni | identiche byte per byte (0 celle diverse su 1.701) | 2,9e-13 a | 8,6e-16 a | — |
| HU50, 2.000 iterazioni | identiche byte per byte | 2,4e-13 a | 9,2e-16 a | — |
| HU50_rake, 100 iterazioni | identiche byte per byte | 4,6e-13 a | 3,5e-15 a | 1,3e-13 a |
| HU50_rake, 2.000 iterazioni | identiche byte per byte | 3,2e-14 a | 3,1e-15 a | 5,8e-14 a |

Le differenze di EV sono al livello delle 12 cifre stampate nel riepilogo del programma per combo; rake atteso
0,328497 a in entrambi. Tempo per iterazione 0,4-0,9 ms per classi contro circa 30 ms per combo (CPU condivisa
con il training). `ctest -R checkdown` 12/12 PASS (03:47), fra cui i due controlli di equivalenza, le chart per
classi contro il manifest dell'albero HU50, il rifiuto di un riferimento di un'altra configurazione e i test
unitari (aggregazione 81 × 81, tensori HU50_rake contro le matrici dei payoff per combo, contrazione a tre posti,
censimento dei terminali del checkdown 3WAY50 con rake: 25 fold, 13 showdown a tre attivi, 38 a due attivi).

**Parte 2** (in corso alle 03:55; fatta alle 04:28, sotto): terminali a tre dalla tabella, 115 tensori, uno per
showdown e posto attivo (3 × 13 + 2 × 38; alle 03:55 era scritto "115 tensori per posto", impreciso): 13 showdown a
tre attivi (9 all-in con runout e 4 foglie del checkdown) e 38 a due attivi, 0,49 GB in double; i 25 fold e i posti che
hanno foldato usano il tensore dei conteggi delle distribuzioni. Poi chart a tre posizioni attraverso `chart_nodes` e
valutazione delle chart 3-way di MonkerSolver bloccate. Convenzione delle carte foldate: morte (5.9). Rake: la fixture
3WAY50 con rake (5 %, cap 3a) non è confermata dalle chart; gli EV del 60a indicano un rake piatto di circa 2 a e la
stima indiretta sul 50a circa 0,7 a (5.9; verifica finale 0,745 a, 5.10).

**Parte 2 fatta** (commit `e4daf68` e `af9cf15` delle 04:28; `benchmarks/checkdown_classes.hpp`,
`benchmarks/preflop_blueprint_checkdown_classes.cpp`, `benchmarks/monker_chart_lock.hpp`):

- **Terminali** (`three_way_terminals`): un tensore per showdown e posto attivo, indici [eroe][A][B] con A l'altro posto
  più basso. Tre attivi: payoff letti per insieme di vincitori e per posto; al caricamento il programma controlla che il
  payoff di chi perde sia lo stesso nei tre insiemi senza quel posto. Due attivi dopo un fold: voce (eroe, avversario,
  chi ha foldato) con le carte foldate morte, trasposta quando chi ha foldato è il posto più basso; con
  `--folded-cards ignore` la tabella HU pesata dalle combo di chi ha foldato disgiunte da entrambe. Fold e posti che
  hanno foldato: tensore N dei conteggi scalato dal payoff, contratto per inclusione-esclusione sulle carte comuni
  (circa 5.000 operazioni invece di 531.441). Controllo del numero di distribuzioni: 630 × 561 × 496. In tutto 116
  tensori (N più i 115 degli showdown), 493.177.248 byte.
- **Calcolo**: ogni attraversamento raccoglie prima il reach dei terminali, poi li contrae su `--threads` thread;
  risultati identici con 1 e 2 thread; in HU la contrazione è quella della parte 1 (output identici byte per byte).
- **Blocco delle chart** per N posti (`chart_lock_seats`; `chart_lock` HU invariato), `--lock-charts` e `--lock-nodes`,
  `--iterations 0` per valutare un set di chart. Le chart 3-way di MonkerSolver a 50a si bloccano tutte: 54 nodi, 4.374
  righe (54 × 81) = 2.681 righe delle chart + 1.688 fuori range + 5 righe nulle raggiunte sotto le chart (0,056 combo,
  classi KQo, A9s e KJs del CO), lasciate libere (uniformi).
- **Validazione** (non esiste un programma per combo a tre posti): forza bruta a livello di combo
  (`count_combo_triple`, senza riduzione per classi) su 45 valori (showdown a tre, showdown a due con ciascun posto come
  chi ha foldato, fold; entrambe le convenzioni), differenza massima 1,7e-13 a, e una trasposta invertita apposta viene
  presa; identità del rake in ogni terminale (51 terminali con rake, differenza relativa massima 2,3e-15); smoke a 40
  iterazioni con le 54 chart contro il manifest dell'albero. Un revisore indipendente ha ricalcolato da zero, con un suo
  script sulle tabelle grezze, due scenari bloccati che coprono tutte e sei le coppie eroe / chi ha foldato (carte
  morte e ignorate, con e senza rake): EV, guadagni della migliore risposta e rake atteso uguali in tutte le 12 cifre
  stampate; ha riprodotto anche la valutazione delle chart di MonkerSolver e ha verificato che riempire le 5 righe nulle
  con altre azioni sposta EV e guadagni al massimo di circa 1e-5 a.

**Revisione e correzioni** (commit `2184d66` e `67de1d7` delle 05:20): tre rilievi, confermati da un verificatore e
corretti. (1, medio) Il CTest del blocco a tre posti controllava solo "lock: 54 charts" e PASS, un numero fissato prima
del ciclo sui posti: ora il programma si ferma se righe delle chart + fuori range + nulle non fanno nodi × 81 o se le
righe bloccate nel solver non coincidono, la nuova opzione `--expect-summary` confronta EV e guadagni con il run di
origine (tolleranza 0,001 a; differenze misurate 6,4e-5 / 1,4e-4 a) e un test unitario rifà il giro scrittura-blocco a
tre posti; due mutazioni (ciclo fermo a due posti, blocco rispettato solo alla radice) fanno fallire sia il CTest sia il
test unitario. (2, basso) `--iterations 0` con un blocco parziale dava PASS con nodi uniformi: ora è rifiutato, come
`--lock-nodes` senza `--lock-charts`. (3, basso) Il messaggio di errore del controllo di equivalenza stampava la
tolleranza 1e-9 come 0,000000: ora 12 cifre. `ctest -L "preflop_blueprint|card_abstraction"` 90/90 (743 s); chart HU50
e HU50_rake per classi identiche byte per byte a prima; la valutazione delle chart 3-way di MonkerSolver identica.

**Velocità**: circa 17 ms per iterazione con 8 thread sull'i3 libero (10.000 iterazioni in 172-176 s, 9.5), 42-49 ms
con 2 thread e la CPU condivisa durante il workflow; 0,49 GB di tensori, picco stimato circa 0,55 GB (dalle
allocazioni, non misurato). Il rapporto del workflow è nel suo giornale (cartella della sessione,
`subagents/workflows/wf_e198de0e-ef3/journal.jsonl`; copia leggibile in `scratchpad/p2b_results.txt`). Un viewer degli
output della fase 2b per il 3-way è in preparazione nella cartella temporanea della sessione, non nel repository.

### 9.5 Prime chart 3-way del passo 1 (mattina del 30 settembre)

Coda `run_3way_then_6d.sh` (05:36, dopo i test 5M-5D e prima del test 6d): l'utente aveva permesso di anticipare i run
3-way appena la macchina fosse libera, e sono partiti alle 07:55 invece che alle 11:30. Eseguibili
`out/monker/bin_3way_step1` (da `67de1d7`). Ogni run: 10.000 iterazioni, 8 thread, circa 2,9 minuti; cartelle in
`out/monker/step1_3way/`: `summary.json` (EV e guadagno della migliore risposta per posto), `tree_check.txt` (54 file su
54, 0 mancanti, 0 in più, 0 azioni diverse in tutti i run), `vs_monker.txt` (distanza e differenza di range dalle 54
chart di MonkerSolver) e `<nome>_monker_eval/summary.json` (le chart 3-way a 50a di MonkerSolver bloccate su tutti i
nodi nello stesso gioco del passo 1, 0 iterazioni: guadagno della migliore risposta di ogni posto). Quattro run dalle
07:55 alle 08:07; alle 08:40, con la macchina libera, un quinto con rake 2,5 % / cap 2a (l'ipotesi HU con la somma più
bassa, test 6c).

| Run | Configurazione | Rake | Carte foldate | EV UTG / CO / BTN | Rake atteso | Guadagno massimo (% del piatto di 4a) | Distanza / differenza di range | Chart di MonkerSolver nello stesso gioco: guadagno UTG / CO / BTN (% del piatto) | Somma |
|---|---|---|---|---|---:|---:|---:|---|---:|
| `3WAY50_norake_dead` | `3WAY50_donk.json` | nessuno | morte | −0,2070 / +0,0343 / +0,1727 a | 0 | 1,6e-4 | 0,288 / 0,736 | 5,46 / 4,71 / 1,40 | 11,57 |
| `3WAY50_rake_dead` | `3WAY50_donk_rake.json` | 5 %, cap 3a | morte | −0,3674 / −0,1015 / −0,0589 a | 0,528 a | 2,2e-5 | 0,255 / 0,715 | 4,47 / 4,17 / 1,58 | 10,23 |
| `3WAY50_rake075_dead` | `3WAY50_donk_rake5cap075.json` | 5 %, cap 0,75a | morte | −0,2731 / −0,0142 / +0,0706 a | 0,217 a | 1,7e-5 | 0,257 / 0,709 | **3,95 / 3,65** / 1,42 | **9,02** |
| `3WAY50_rake_ignore` | `3WAY50_donk_rake.json` | 5 %, cap 3a | ignorate | −0,3602 / −0,1132 / −0,0730 a | 0,546 a | 1,5e-4 | 0,245 / 0,699 | 4,51 / 4,08 / 1,64 | 10,24 |
| `3WAY50_rake25_dead` (08:40) | `3WAY50_donk_rake25cap2.json` | 2,5 %, cap 2a | morte | −0,3154 / −0,0548 / +0,0231 a | 0,347 a | 1,8e-5 | 0,264 / 0,721 | 5,06 / 4,52 / 1,41 | 10,99 |
| Passo 1 HU (4, 5.9) | `HU50.json` / `HU50_rake.json` | nessuno / 5 %, cap 3a | | | | | 0,262 / 0,729; 0,252 / 0,701 | | |

Primo a parlare (frequenze pesate sulle combinazioni, `compare_charts.read_chart`; il CO dopo il fold dell'UTG è la
chart `CO/CO_strategy.txt`):

| Chart | UTG alla radice: all-in / open 6a / limp / fold | CO dopo il fold dell'UTG: all-in / open 6a / limp / fold |
|---|---|---|
| MonkerSolver | 17,6 / 0,1 / 35,3 / 47,0 % | 32,8 / 3,8 / 31,4 / 32,0 % |
| Passo 1, senza rake | 11,4 / 0,0 / 54,6 / 34,0 % | 29,0 / 0,0 / 65,6 / 5,4 % |
| Passo 1, 5 % / cap 3a | 17,2 / 0,0 / 41,8 / 41,0 % | 32,3 / 0,0 / 57,9 / 9,8 % |
| Passo 1, 5 % / cap 0,75a | 15,3 / 0,0 / 44,3 / 40,5 % | 29,6 / 0,0 / 62,8 / 7,6 % |
| Passo 1, 5 % / cap 3a, carte ignorate | 17,2 / 0,0 / 42,7 / 40,1 % | 34,2 / 0,0 / 56,8 / 8,9 % |
| Passo 1, 2,5 % / cap 2a | 16,1 / 0,0 / 46,4 / 37,5 % | 30,2 / 0,0 / 63,8 / 6,0 % |

- **Convergenza**: a 10.000 iterazioni il guadagno massimo di un posto va da 1,7e-5 a 1,6e-4 % del piatto (i più lenti
  senza rake e con le carte ignorate); nelle prove del workflow il run senza rake aveva un inizio non monotono (0,009 ->
  0,020 % fra 250 e 500 iterazioni), poi è sceso: con tre giocatori il CFR non ha garanzie di Nash e la misura è il
  guadagno di ogni posto. EV: somma uguale a meno il rake atteso in ogni run (controllo nel programma).
- **Come in HU, il passo 1 non apre mai** (open 6a allo 0,0 % per UTG e CO; MonkerSolver 0,1 % e 3,8 %) e limpa troppo
  (UTG 42-55 % contro 35,3 %, CO 57-66 % contro 31,4 %): senza postflop limpare vale troppo e l'open non ha valore.
- **Il rake porta lo shove dell'UTG al livello di MonkerSolver** (17,2 % con 5 % / cap 3a contro 17,6 %; senza rake
  11,4 %) e alza il fold (41 % contro 34 %; MonkerSolver 47 %). La distanza scende da 0,288 a 0,245-0,264 con il rake,
  la più bassa con le carte ignorate (0,245), che però gli EV del set a 60a respingono (5.9): senza postflop è una misura
  indicativa. Le distanze sono dello stesso ordine del passo 1 HU (0,252-0,262).
- **Le chart di MonkerSolver bloccate nel gioco del passo 1** lasciano alla migliore risposta di UTG e CO il 3,65-5,46 %
  del piatto e a quella del BTN l'1,40-1,64 %; il cap di 0,75a le rende le meno sfruttabili per UTG e CO e nella somma
  (9,02 % contro 10,23-11,57 %), il BTN è più basso senza rake (1,40 % contro 1,42 %). Guadagni locali più grandi:
  radice dell'UTG, radice del CO, UTG contro l'isolation a 7a del CO dopo il suo limp, CO contro il raise a 6a del BTN dopo il suo limp, BTN contro il
  limp del CO (0,021-0,045 a ciascuno). Rake atteso con le chart di MonkerSolver 0,510 / 0,208 / 0,322 a (5 % con cap
  3a, 0,75a, 2,5 % con cap 2a). Solo indicativo: il passo 1 non ha postflop, quindi chart calcolate con un postflop sono
  sfruttabili qui per costruzione; il confronto fra i rake dice quale impostazione rende il preflop di MonkerSolver meno
  incoerente con un gioco senza postflop, non quale è giusta. Lo stesso ordine (0,75a il più coerente) viene dai call
  contro gli all-in in HU (5.10) e dalla verifica sul 3-way a 50a (5.9); il guadagno complessivo nel test HU con
  postflop (6-6d) va invece nell'altro verso (CO 1,13 % con 0,75a contro 0,92-0,99 % con gli altri rake, somma più
  bassa con 2,5 % / cap 2a).
- **Decisioni per la fase 3** (sezione 8, punto 9): rake 3-way (la fixture 5 % / cap 3a non è confermata), convenzione
  delle carte foldate per il passo 2 (morte secondo gli EV del 60a), bucket 30 × 4 (27,11 GB in double) o 15 × 4 (13,56
  GB). Nuove configurazioni non committate: `3WAY50_donk_rake5cap075.json` (05:35) e `3WAY50_donk_rake25cap2.json` (08:40).

