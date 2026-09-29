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
| Classifica short deck | La scala batte il tris anche nel MonkerSolver delle chart (stessa classifica nostra) |
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
  ammettono da 0 a circa 1-2 a di rake su 100 a di piatto).
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
  dell'utente, se disponibili (chieste all'utente).

**Decisioni dell'utente del 29 settembre.**

| Tema | Decisione |
|---|---|
| Configurazione HU di riferimento | G1: astrazione compatta (15 livelli × 4 + TX2) con donk bet (decisione dell'utente); una size: G4 non adottato perché più lontano da MonkerSolver (criterio della notte, 5.6) |
| All-in postflop fino a 5 volte il piatto | No: revoca la decisione della notte (5.6); G2 e G3 un po' più lontani da MonkerSolver, e il limite toglie solo 6 all-in |
| Test B | Solo su G1 |
| Test C (postflop ricalcolato con il preflop di MonkerSolver fermo) | Non si fa |
| A | Continuato fino a 32.000 iterazioni |
| 3-way | Rinviato (decisione delle 20:00): si parte quando il problema della radice e del piatto limpato è capito meglio, probabilmente dopo le 3 del 30 settembre |
| Test della radice bloccata | Codice la sera del 29 (anche dopo le 21), test in partenza automatica alle 00:00 del 30; il secondo test (tutto il CO preflop bloccato) solo se il primo non converge verso le chart del BTN di MonkerSolver |

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

Eseguibili congelati per i run lunghi: `out/monker/bin_allin` (da `410a380`: G2, G3, G4, E, F) e
`out/monker/bin_abd` (da `5578ab8`: A, B, D). Copie congelate del runner in `out/frozen/`
(`run_step2_continuous.sh`, `run_step2_continuous_abd.sh`): devono stare due livelli sotto la radice del
repository, perché il runner calcola la radice dal proprio percorso; una copia nella cartella temporanea
fallisce alla partenza. Un runner in uso non va modificato (bash legge lo script mentre lo esegue: incidente
di G4, 5.7).

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
revisione corretti prima del commit).

## 7. Altri risultati del 28 settembre

- **Repository equity-calculator-web-app dell'utente:** classifica short deck identica alla nostra (il
  README dice il contrario, il codice è corretto); utile come controllo indipendente (enumerazione
  esatta fino a 4 mani), non per costruire le tabelle a tre giocatori o i kernel.
- **Alberi MonkerSolver 3-way a 50a:** tutte le size sono un raise del 100 % del piatto: open 6a,
  isolation del CO dietro il limp dell'UTG 7a, raise del BTN sul solo limp dell'UTG 6a, su due limp 7a; serve una modalità "raise al 100 % del piatto" nel costruttore.
  Il set HU 40a dell'utente ha un albero diverso (open 6a e 10a, 3-bet 10,5a e 14,5a).
- **Stime multiway:** motore 3-way completo circa 6-7 giorni per le prime chart; passo 1 in 3-way
  circa 3 giorni (serve la tabella di equity a tre giocatori).
- **Server a noleggio per i test** (prezzi del 28/09): phoenixNAP d1.c4.medium (2 x Gold 6258R, 56
  core, 256 GB, circa 0,68 euro l'ora, Windows a ore), Hetzner asta dell'usato (EPYC 7502P, 32 core,
  384 GB, circa 0,25 euro l'ora), AWS m5.metal spot a Milano (circa 0,46 euro l'ora); Cherry 2 x Gold
  6230R esaurito il 28/09.

## 8. Prossimi passi

Fatti il 28 settembre: risultati del passo 2 (5), varianti (5.2), export senza fermare il training,
MonkerSolver nel nostro gioco (5.4), turn in texture (5.5). Fatti il 29 settembre: giochi G0c-G3
sull'astrazione compatta (5.6); G4 con due size, bucket più fini (E, F), flop esatto (D), rumore del seed e
curva di convergenza esatta fino a 32.000 iterazioni (A), migliore risposta contro il preflop delle chart
(B) (5.7). Stato dei punti aperti il 28 (i punti 1 e 3 del 28 sono superati dalle misure e dalle decisioni
del 29):

1. **Configurazione HU di riferimento: decisa il 29 settembre.** G1: astrazione compatta 15 livelli × 4 +
   TX2 con donk bet, una size (bet e raise al 100 % del piatto), all-in postflop senza limite (il limite a 5
   volte il piatto è respinto). Il 28 era aperto (donk bet sì o no, compatta + donk da provare): provato, G1
   è il più vicino a MonkerSolver fra i giochi G0c-G3 (0,0641 / 0,350, 5.6); D, E ed F arrivano a
   0,0621-0,0625 / 0,339-0,342 (0,0016-0,0020 / 0,008-0,011 sotto G1, meno delle soglie 0,005 / 0,02
   del 5.1), spostano le chart di 1,2-1,6 volte il rumore e costano 1,3-6,3 volte la memoria: G1 resta il
   riferimento.
2. **Criterio di somiglianza**: la perdita di MonkerSolver nel nostro gioco (al massimo lo 0,07 % del piatto
   nei giochi fermati a 0,01, lo 0,09 % in A a 32.000 iterazioni) dice che le chart sono equivalenti in EV.
   La migliore risposta contro il preflop delle chart (B) non la contraddice: con il preflop di MonkerSolver
   la sfruttabilità cambia di −0,74 % (CO) e +0,83 % (BTN) del piatto, poco rispetto alla NashConv del
   nostro gioco (29,9 %) ma circa 20 e 55 volte la perdita nel nostro gioco. La distanza (0,064 per G1) resta
   come misura descrittiva, con soglia di rumore fra chart 0,0088 / 0,0425 sul gioco G1 a 16.000 iterazioni.
3. **Seconda size postflop: fatta** (G4, 5.7) sul PC di sviluppo grazie all'astrazione compatta (picco
   15,67 GB, non gli 80 GB stimati il 28): alla pari con i donk bet è la leva più grande sulle chart fra quelle
   provate, ma allontana da MonkerSolver (0,0663 contro 0,0641); non adottata (criterio della notte: il gioco
   più vicino a MonkerSolver a parità di EV, 5.6).
4. **Radice del CO** (open a 5a al 7,0-7,7 % in G1 e nelle sue varianti di astrazione, in calo con le
   iterazioni (6,7 % in A a 32.000), contro lo 0,5 % di MonkerSolver; nessuna modifica provata lo avvicina):
   prossimo test proposto, varianti dell'albero delle azioni solo nei piatti rilanciati (size al flop più
   piccola, solo check o all-in a SPR basso), oppure le impostazioni dell'albero MonkerSolver dell'utente, se
   disponibili (chieste all'utente). Da decidere con l'utente. Prima, la notte del 30: test della radice
   bloccata (5.8), che dice se la radice è la causa comune dello scarto nel piatto limpato.
5. **Regola di arresto per il 3-way**: proposta una soglia di 0,005 fra salvataggi consecutivi (o il doppio
   delle iterazioni dell'arresto a 0,01): lo 0,01 basta per il preflop ma non per la posizione finale delle
   chart (5.7). Da decidere con l'utente.
6. **3-way 50a, fase 1 rinviata** (decisione dell'utente delle 20:00 del 29: si parte quando il problema della
   radice è capito meglio, probabilmente dopo le 3 del 30 settembre): albero identico a quello
   delle chart 3-way a 50a dell'utente (54 file di chart); raise al 100 % del piatto nel costruttore, regole
   sparse (cold call), tabella di equity a tre giocatori, motore multiway, astrazione compatta; confronto con
   le chart 3-way dell'utente con gli stessi strumenti (distanza, differenza di range, MonkerSolver nel
   nostro gioco).
