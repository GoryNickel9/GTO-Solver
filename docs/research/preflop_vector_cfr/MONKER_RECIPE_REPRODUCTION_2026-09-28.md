# Riproduzione della ricetta MonkerSolver (2026-09-28)

Documento del programma avviato il 28 settembre: riprodurre il modo in cui MonkerSolver costruisce
le chart preflop, verificarlo contro le chart MonkerSolver che l'utente possiede, e usarlo come base
del solver multiway. Branch `feat/monker-step1-checkdown` (da `feat/preflop-phase1-time`), commit
`e576396` (passo 1) e `7ff711b` (passo 2). Il passo 2 a tre giocatori (fase 3a del 3-way) è entrato nel branch con il
merge `238a41e` del 1° ottobre (9.7).

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

### 5.11 30 settembre dalle 09:30 alle 14:40: 30 × 4 con rake, test 7, studio I/N, texture di MonkerSolver, algoritmo, file .tree

Cronologia in `out/monker/variants/chain.log` (nessuna riga fra le 08:43 e le 11:47: la macchina è rimasta ferma fino
al colloquio con l'utente); spostamento dei run finiti sul disco USB F: e regola di archivio nella sezione 6. Tutti i run
del pomeriggio usano gli eseguibili `out/monker/bin_rake`, il runner congelato `out/frozen/run_step2_continuous_rake.sh`,
il gioco `HU50_step2_donk_rake25cap2.json` (G1 con rake 2,5 % / cap 2a, no flop no drop, non committato), i bucket
`out/monker/buckets_30x4` e i turn TX2 (il run TXM2 in coda usa la mappa TXM2). Ordine: 30 × 4 con rake (11:47-12:50,
valutazione compresa), test 7 (12:51-13:38), estensione del test 7 (13:38-14:18), estensione del 30 × 4 fino a 64.000 (dalle
14:18, in corso alle 14:40), poi il run con le texture TXM2 (in coda).

**`HU50_m30x4_rake25`: il gioco più vicino ai default di MonkerSolver** (richiesta dell'utente verso le 11:45). Bucket 30 × 4
(i conteggi di default delle schermate, 5.10), TX2, donk bet, rake 2,5 % / cap 2a (l'ipotesi con la somma dei guadagni più
bassa nei test 6-6d), soglia di arresto 0,005, massimo 60.000. Arresto per regola a 32.000 (12:37, cambiamento 0,0045; il
trainer si è fermato a 32.117 iterazioni), valutazione esatta su 573 flop fino alle 12:50 (`monker_in_our_game.txt`, copiata
come `*_32k` prima dell'estensione). 325-440 s ogni 4.000 iterazioni (0,08-0,11 s per iterazione), con lo spostamento su F:
in corso dalle 12:01 (dall'iterazione 8.000 circa).

| Iterazioni | Distanza da MonkerSolver | Differenza di range | Cambiamento |
|---:|---:|---:|---:|
| 4.000 | 0,1082 | 0,655 | — |
| 8.000 | 0,0924 | 0,594 | 0,0344 |
| 12.000 | 0,0830 | 0,514 | 0,0202 |
| 16.000 | 0,0745 | 0,441 | 0,0161 |
| 20.000 | 0,0668 | 0,383 | 0,0121 |
| 24.000 | 0,0614 | 0,351 | 0,0100 |
| 28.000 | 0,0585 | 0,332 | 0,0060 |
| **32.000 (arresto)** | **0,0568** | **0,320** | 0,0045 |
| 36.000 (estensione) | 0,0556 | 0,312 | 0,0037 |
| 40.000 | 0,0545 | 0,303 | 0,0034 |
| 44.000 | 0,0539 | 0,296 | 0,0028 |
| 48.000 (14:38) | 0,0532 | 0,289 | 0,0025 |
| G1 (16.000) | 0,0641 | 0,350 | |
| G1+ (28.000) | 0,0622 | 0,338 | |
| A continuato (32.000) | 0,0631 | 0,337 | |
| G1 con rake 5 % / cap 3a (arresto a 32.000) | 0,0849 | 0,490 | |

Radice del CO (combo, `root_mix.py`) e preferenza suited (`suited_pref.py`):

| Chart | All-in | Open 5a | Limp | Fold | AA (open / limp) | Preferenza suited |
|---|---:|---:|---:|---:|---|---:|
| MonkerSolver | 33,1 % | 0,5 % | 29,0 % | 37,4 % | 0,07 / 0,93 | 0,224 |
| 30 × 4 con rake 2,5 % / cap 2a, 32.000 | 35,6 % | 3,5 % | 24,0 % | 37,0 % | 0,42 / 0,57 | **0,214** |
| stesso, 44.000 (estensione) | 35,9 % | 2,7 % | 25,1 % | 36,4 % | 0,35 / 0,65 | 0,220 |
| G1 (16.000) | 27,8 % | 7,5 % | 30,8 % | 33,8 % | 0,72 / 0,28 | 0,365 |
| G1 con rake 5 % / cap 3a (36.000) | 39,1 % | 3,5 % | 14,7 % | 42,6 % | 0,49 / 0,51 | 0,147 |

- **La preferenza suited arriva a quella di MonkerSolver** (0,214 contro 0,224; G1 0,365, G1+ 0,380): lo spostamento da
  G1 (0,151) è più di 10 volte la differenza fra i due seed di G1 (0,365 contro 0,352). Nessuna variante precedente ci
  arrivava: i bucket più fini la alzavano (E 0,394), il rake 5 % / cap 3a la portava troppo giù (0,148 all'arresto a
  32.000, 0,147 a 36.000). Il run cambia due cose insieme (bucket e rake): il loro effetto separato non è misurato (manca
  un run a 15 × 4 con il preflop libero nel gioco con rake 2,5 % / cap 2a; il gioco esiste, test 6c).
- **Distanza**: 0,0568 / 0,320 all'arresto, la più bassa fra i giochi con il postflop. Il miglioramento su G1 (0,0073 /
  0,029) supera le soglie del 5.1 per la distanza da MonkerSolver (0,005 / 0,02; su questa misura i due seed di G1
  differiscono di 0,0009 / 0,0026); contro G1+ e A continuato, fermati a iterazioni simili, è di 0,0054-0,0063 /
  0,017-0,018, al limite (differenza di range sotto 0,02). Lo 0,0088 / 0,0425 fra le chart di due seed di G1 è invece la
  soglia per lo spostamento fra chart: le chart del 30 × 4 all'arresto distano da quelle di G1 0,0514 / 0,283 (circa 6 e 7
  volte; A continuato a 32.000, cioè seed e iterazioni in più, ne dista 0,0163 / 0,0840; `compare_charts.py`). Un
  miglioramento modesto della distanza (sopra le soglie contro G1, al limite contro G1+), un cambiamento netto delle chart e
  della preferenza suited. Nell'estensione la distanza continua a scendere (0,0532 / 0,289 a 48.000).
- **Radice**: open a 5a al 3,5 % (G1 7,5 %, MonkerSolver 0,5 %), all-in e fold vicini a MonkerSolver (35,6 / 37,0 % contro
  33,1 / 37,4 %), limp ancora sotto (24,0 % contro 29,0 %; con il rake 5 % / cap 3a era sceso al 14,7 %).
- **Valutazione esatta a 32.000** (`monker_in_our_game_32k.txt`, `monker_values_exact_32k.log`): le chart di MonkerSolver
  giocate in questo gioco contro la nostra strategia perdono −0,00071 a al CO (−0,024 % del piatto: fanno un po' meglio delle
  nostre chart) e 0,00038 a al BTN (0,013 %); il nostro scarto dalla migliore risposta preflop è 0,00275 a (0,092 %) /
  0,00096 a (0,032 %); rake atteso 0,239 a per mano; EV CO −0,317 a, BTN +0,078 a. In EV equivalenti come in tutti i giochi
  precedenti (5.4-5.10).
- L'estensione fino a 64.000 (soglia 0; cartella con il file `NO_ARCHIVE` dalle 13:07, sezione 6) è stata chiesta
  dall'utente verso le 13:10: il run si era fermato per regola mentre migliorava ancora, e lo studio I/N (sotto) gli dà
  circa 8,7 sulla scala per mano. Valutazione esatta a 64.000, poi archivio su F:.

**Test 7 (`HU50_lock_all_m30x4_rake25`): tutto il preflop di MonkerSolver bloccato nel gioco del run sopra.** Piano
concordato con l'utente alle 12:26: se il 30 × 4 con rake 2,5 % si avvicina a MonkerSolver, prima il test con il preflop
bloccato in quel gioco. Come il test 5 (5.9): postflop appreso per 24.000 iterazioni (12:51-13:28, soglia 0), valutazione
esatta e scomposizione (`br_split.txt`) alle 13:38. Poi esteso a 48.000 (13:38-14:09, valutazione alle 14:18) per la parità
di aggiornamenti per insieme di informazione con i test a 15 × 4 valutati a 24.000 (studio I/N: il 30 × 4 chiede circa il
doppio delle iterazioni; a 48.000 il test 7 ha 12,9 sulla scala per mano, come il test 5 a 24.000). Cartella archiviata su
F: alle 14:19 (sezione 6). Guadagni della migliore risposta preflop (ante; "—" = sotto 0,00005 a):

| Test | Bucket, rake | Iterazioni | Guadagno del CO | Guadagno del BTN | Somma | Rake atteso | Radice del CO: totale (verso all-in / open 5a / limp / fold) | CO contro l'isolation: totale (verso all-in / call / fold) | BTN contro il limp: totale (verso check / all-in / isolation 5a) |
|---|---|---:|---:|---:|---:|---:|---|---|---|
| 5 | 15 × 4, nessuno | 24.000 | 0,0432 a (1,44 %) | 0,0161 a (0,54 %) | 1,98 % | 0 | 0,0386 (0,0062 / 0,0209 / 0,0115 / —) | 0,0064 (0,0026 / 0,0038 / —) | 0,0153 (0,0123 / 0,0004 / 0,0026) |
| 5M | 30 × 4, nessuno | 24.000 | 0,0479 a (1,60 %) | 0,0181 a (0,60 %) | 2,20 % | 0 | 0,0433 (0,0069 / 0,0222 / 0,0142 / —) | 0,0067 (0,0030 / 0,0037 / —) | 0,0173 (0,0149 / 0,0003 / 0,0021) |
| 6c | 15 × 4, 2,5 % / cap 2a | 24.000 | 0,0277 a (0,92 %) | 0,0142 a (0,47 %) | 1,40 % | 0,234 a | 0,0251 (0,0061 / 0,0136 / 0,0054 / 0,0001) | 0,0044 (0,0028 / 0,0013 / 0,0003) | 0,0136 (0,0101 / 0,0027 / 0,0008) |
| **7** | 30 × 4, 2,5 % / cap 2a | 24.000 | 0,0311 a (1,04 %) | 0,0163 a (0,54 %) | 1,58 % | 0,235 a | 0,0283 (0,0067 / 0,0146 / 0,0068 / 0,0001) | 0,0050 (0,0032 / 0,0013 / 0,0004) | 0,0156 (0,0122 / 0,0026 / 0,0008) |
| **7 esteso** | 30 × 4, 2,5 % / cap 2a | 48.000 | 0,0310 a (1,03 %) | 0,0148 a (0,49 %) | 1,52 % | 0,233 a | 0,0281 (0,0056 / 0,0169 / 0,0056 / —) | 0,0046 (0,0028 / 0,0016 / 0,0002) | 0,0141 (0,0102 / 0,0029 / 0,0010) |

EV CO / BTN nel test 7: −0,323 / +0,088 a a 24.000, −0,320 / +0,087 a a 48.000. Call contro un all-in come nel 6c (verso
fold 0,0008 a al CO contro lo shove del BTN dopo il limp, 0,0001 al BTN contro lo shove del CO).

- **Il 30 × 4 non rende il preflop di MonkerSolver più coerente.** Nel gioco con rake 2,5 % / cap 2a, da 15 × 4 (6c) a 30 × 4
  (7) il guadagno del CO sale da 0,92 a 1,04 % (+12 %) e quello del BTN da 0,47 a 0,54 % (+14 %), come senza rake (da 5 a 5M:
  +11 % / +13 %). Con il doppio delle iterazioni il CO resta all'1,03 % e il BTN scende allo 0,49 % (−0,05 punti, la stessa
  deriva del test 5 fra 24.000 e 64.000: 0,54 -> 0,49 %). L'aumento dei test 5M-5F (5.10) non viene quindi dalla sola
  convergenza incompleta, almeno per il 30 × 4.
- **Fra le leve provate il rake è quella che lo abbassa di più**: con il 30 × 4, da 5M a 7, il CO scende da 1,60 a 1,04 %
  (−35 %), come da 5 a 6c (−36 %). Un bucket più grossolano lo abbassa meno (da 30 × 4 a 15 × 4 il CO scende del 10 % senza
  rake, dell'11 % con il rake 2,5 % / cap 2a); raffinare l'astrazione lo alza (15 × 4 < 30 × 4 < 30 × 8 < 60 × 8, 5.10) e non
  lo toglie; più iterazioni lo lasciano dov'è al CO (test 5, test 7). Texture più grossolane come quelle di MonkerSolver
  (TXM, TXM2) non sono ancora state provate con il preflop bloccato.
- **Il resto (circa l'1 % al CO, lo 0,5 % al BTN) sta nelle stesse linee**: al CO soprattutto la spinta verso l'open a 5a
  alla radice (0,0169 a su 0,0310 a 48.000), al BTN il check dietro il limp del CO (0,0102 a su 0,0148). Lettura:
  probabilmente una differenza strutturale fra il preflop di MonkerSolver e il nostro gioco (albero e size postflop,
  astrazione e algoritmo del postflop di MonkerSolver): non viene dalla convergenza (al CO il test 7 a 48.000 è uguale a
  24.000), e raffinare la nostra astrazione lo alza invece di toglierlo; resta il limite del test (postflop appreso contro
  range fissi, 5.9). Il nostro preflop nello stesso gioco lascia 0,092 % / 0,032 % (circa 11 e 15 volte meno a 48.000).

**Studio I/N** (workflow con due corsie e due verificatori indipendenti, circa 11:55-13:07; rapporto in
`scratchpad/monker_research/research_results.md` nella cartella temporanea della sessione): quanto sono allenati i nostri run
nella misura di MonkerSolver.

- **Cosa dice MonkerSolver.** La guida ufficiale indica come segno che una soluzione comincia a essere solida un numero di
  iterazioni pari a 10 volte il numero di nodi; il numero di nodi non è definito. Per inferenza (una cifra di HoldemTools,
  "~2.366 nodi" per un albero jam/fold a 4 giocatori con 14 decisioni = 14 × 169, ricavata dalle loro statistiche e non da
  una schermata di MonkerSolver; fattorizzazioni esatte delle "Buckets/node" delle schermate) è il numero di insiemi di
  informazione: nodi di decisione × bucket per nodo, contando la capacità (anche i bucket vuoti). Sul forum Two Plus Two: 10
  come minimo, di solito 20-40 (30-40 al preflop). Una iterazione di MonkerSolver è, secondo HoldemTools, una distribuzione
  campionata (una mano per posto e un board; non documentato da MonkerSolver); non è documentato se aggiorni uno o due posti.
- **Cosa fa una nostra iterazione** (codice letto e replicato esattamente: generatore casuale, board campionati, righe toccate
  e materializzate coincidono con i contatori dei run, anche in un ricalcolo indipendente): per ciascun giocatore 32 board
  nuovi, e su ogni board un attraversamento vettoriale completo (tutte le azioni, tutte le 465 mani dell'eroe e le 406
  avversarie); le righe del preflop sono aggiornate a ogni iterazione, una riga postflop solo sui board della sua classe.
- **Due scale**: per board (conservativa: un aggiornamento di una riga su un board vale un aggiornamento di MonkerSolver) e
  per mano (ogni mano dell'eroe su ogni board ne vale uno: 32 × T × 465 / N). Differiscono del fattore h = mani per riga su un
  board: con 15 × 4 5,7 al preflop, 12,2 al flop, 14,6 al turn, 33,8 al river. In più un fattore f ignoto (uno o due posti
  per iterazione di MonkerSolver): con f = 1/2 i valori raddoppiano.

| Run | Bucket | Iterazioni | I/N per mano (f = 1) |
|---|---|---:|---:|
| G1 (`HU50_c_donk`) | 15 × 4 | 16.196 | 8,7 |
| G1 con rake | 15 × 4 | 37.850 | 20,4 |
| Test 5 | 15 × 4 | 24.000 / 40.000 / 64.000 | 12,9 / 21,6 / 34,5 |
| 5M | 30 × 4 | 24.000 | 6,5 |
| E, G1+, 5E | 30 × 8 | 20.236 / 28.195 / 24.000 | 3,2 / 4,5 / 3,8 |
| F, 5F | 60 × 8 | 20.114 / 24.000 | 1,6 / 1,9 |
| `HU50_m30x4_rake25` | 30 × 4 | 32.117 | 8,66 (17,3 con f = 1/2) |

- Sulla scala per board nessun run arriva a 10 nei nodi più raggiunti (mediane per street: G1 con rake 4,6 / 2,0 / 1,7 / 0,6
  dal preflop al river; test 5 a 64.000 flop 3,3, turn 2,9, river 1,1; `HU50_m30x4_rake25` 1,95 / 1,25 / 1,03 / 0,49);
  corretti per il reach dell'avversario superano 10 solo nodi meno raggiunti (BTN dopo il limp del CO: circa 16 in G1 con
  rake). Fra i run della tabella superano 10 sulla scala per mano solo quelli a 15 × 4 con almeno 18.539 iterazioni (G1 con
  rake, test 5); a 48.000 anche il test 7 a 30 × 4 arriva a 12,9.
- **Conseguenza**: bucket più fini chiedono più iterazioni in proporzione (circa 2 volte per il 30 × 4, 3,4 per il 30 × 8,
  6,8 per il 60 × 8 a parità di I/N), quindi i test 5M-5F a 24.000 possono essere in parte sotto-convergenti (dubbio già nel
  5.10); per il 30 × 4 il test 7 a 48.000 lo ha controllato: nessun cambiamento al CO. La scala resta indicativa (algoritmi
  diversi, varianza per aggiornamento di MonkerSolver ignota): per il nostro trainer i criteri restano il cambiamento delle
  chart, la distanza e la migliore risposta.

**Le texture di MonkerSolver** (stesso workflow, corsia A e il suo verificatore):

- HoldemTools (PR #86 e #87 del repository rebrag/HoldemTools) decodifica solo le tabelle dei bucket di MonkerSolver
  (`holdem{flop,turn,river}*.ser`, bucket = 4 × forza + livello di potenziale), non la regola delle texture, che nessuna
  delle fonti lette (HoldemTools, monkerware.com, forum Two Plus Two) riporta. I conteggi "Large" delle schermate (8.942
  classi di turn e 3.677 di river in Hold'em) non sono riprodotti (circa 300 regole provate; la più vicina, 8.996, è la
  regola del TXM2 sotto).
- Riprodotti esattamente gli spazi delle chiavi delle tabelle di MonkerSolver: 1.755 flop, 16.432 turn (insieme non ordinato
  di 4 carte), 42.783 river (multinsieme dei ranghi più i ranghi del seme con 3 o più carte); in short deck 573 / 3.663 /
  6.318.
- Per inferenza le classi di MonkerSolver sono globali per street e dimenticano l'ordine delle carte e il flop. Per il river
  l'argomento è solido (3.677 classi di river sono meno delle 8.942 di turn: impossibile se fossero annidate nelle classi del
  turn); per il turn poggia solo su un esempio dello sviluppatore del 2017, e non è determinato se il turn "Perfect" sia
  l'insieme non ordinato (16.432) o quello ordinato (63.193).
- Stime short deck, scalando i rapporti di Hold'em: turn "Large" circa 1.990 classi (1.950-2.250 secondo il verificatore),
  river "Large" circa 540-760. Il nostro TX2 ha 4.482 classi di turn (sui 13.761 turn ordinati) e al river usa la classe del
  turn: per numero di classi è più fine di MonkerSolver di circa 2 volte al turn e di 6-8 volte al river.

**Mappe TXM e TXM2** (`benchmarks/monker/textures`, non committate; richiesta dell'utente verso le 13:12 di texture simili a
quelle di MonkerSolver). Il generatore (`generate_texture_maps.py`, modificato) scrive ora sette mappe, il `README.md` le
descrive:

- **TXM** (`texture_map_TXM_monker_like.txt`): chiave = multinsieme dei ranghi del turn (non ordinato) × schema dei semi
  (4 / 3+1 / 2+2 / 2+1+1 / 1+1+1+1), senza quali ranghi condividono un seme: **1.899 classi**, più grossolana dell'insieme
  non ordinato (3.663) e un po' sotto la stima di circa 1.990 per "Large" (1.950-2.250 secondo il verificatore; le regole
  naturali della corsia A danno 1.899-2.277, e 1.899 è proprio questa). La stessa regola sui turn di Hold'em dà 7.566 classi
  (15 % sotto gli 8.942).
- **TXM2** (`texture_map_TXM2_monker_like.txt`): TXM più, solo sui board 2+2, quali ranghi condividono ciascun seme: **2.151
  classi**; in Hold'em 8.996, lo 0,6 % dagli 8.942 di "Large": per quell'unico dato è l'imitazione più vicina.
- Il river prende la classe del suo turn come in TX2 (il motore accetta solo `river-key turn`; una chiave di river a 5 carte
  come in MonkerSolver richiede codice). TXM non è un raffinamento né un accorpamento di TX2 (8.217 classi congiunte; una
  classe TXM interseca in media 4,33 classi TX2, fino a 8) e unisce turn di flop diversi (1.890 classi su 1.899).
- Stato del trainer su HU50 passo 2 donk con 30 × 4: 966.809.248 byte con TXM e 1.084.079.968 con TXM2, contro 2.168.834.128
  con TX2 (0,45 e 0,50 volte).
- Verifiche: ricalcolo indipendente della partizione; le altre cinque mappe identiche byte per byte a prima; smoke del
  caricatore e del trainer (2 iterazioni con TXM, valutazione con la stessa mappa, rifiuto della policy con la mappa TX2).
- **Run TXM2 in coda** (`run_txm2.sh`, cartella `HU50_m30x4_txm2_rake25`): lo stesso gioco del 30 × 4 con rake e la mappa
  TXM2, 64.000 iterazioni, soglia 0, valutazione esatta e archivio su F:; parte dopo la valutazione dell'estensione del
  30 × 4.

**L'algoritmo di MonkerSolver e l'opzione A.** Nelle schermate (impostazioni generiche di MonkerSolver per l'Hold'em, non
quelle delle chart, 5.10) l'algoritmo è CSCFR (campionamento del caso) o ESCFR (campionamento esterno), cioè MCCFR, e
strategia media ed EV sono tenuti solo sulla prima street; le impostazioni usate per le chart non sono note. Il nostro
trainer: CFR vettoriale con campionamento pubblico del caso (32 board per giocatore per iterazione, su ogni board tutte le
mani e tutte le azioni), DCFR 1,5 / 0 / 2, aggiornamenti alternati, sconto lazy. L'opzione A misura se l'algoritmo sposta le chart verso MonkerSolver, sul
gioco G1 senza rake (riferimento: rumore del seed 0,0088 / 0,0425): A1 Linear CFR invece di DCFR (32.000 iterazioni); A2 DCFR
con un board per batch (1.024.000 iterazioni, gli stessi board di 32.000 × 32, tetto di 80 minuti); A3 Linear CFR con
aggiornamenti simultanei e un board per batch (il più vicino a un CFR campionato semplice; stessa lunghezza). Preparata con una
copia congelata del runner, `out/frozen/run_step2_continuous_algo.sh` con la variabile `LAZY_ARG` (lo sconto lazy richiede
DCFR, quindi A1 e A3 girano senza), smoke di 40 iterazioni delle tre varianti passati (12:18-12:23); messa in coda alle 12:23
(`run_algo_A.sh`) e tolta alle 12:26 dopo la domanda dell'utente: si decide dopo il risultato del 30 × 4, e se il 30 × 4 con
rake 2,5 % si avvicina chiaramente a MonkerSolver, prima il test con il preflop bloccato in quel gioco (il test 7) e l'opzione
A su quel gioco. Non partita alle 14:40.

**File .tree di MonkerSolver** (due alberi dell'utente, `100bb6maxsmall.tree` e `100bb6maxmedium.tree`, 6-max a 100 bb;
decodifica e verifica indipendente fra le 12:10 e le 12:40 circa):

- **Formato**: un testo UTF-8 in cui ogni carattere è un intero (0-65535); la codifica rifatta riproduce il file byte per
  byte. Intestazione di 3N + 5 valori: primo valore 33.484 (costante, significato ignoto), N = 6 giocatori, tre campi a zero,
  per ogni posto un campo a zero e il blind in mchip (1.000 / 2.000, cioè 0,5 / 1 bb con 1 fiche = 1.000 mchip), poi gli
  stack (200 fiche = 100 bb). Segue l'albero completo, preflop e postflop, in pre-ordine (numero di figli, poi per ogni figlio
  il codice dell'azione e il suo sottoalbero; nessun nodo del caso). Codici: 0 fold, 1 check o call, 3 all-in, 40100 puntata
  o rilancio al piatto (100 %, inferito dalla logica del gioco). La grammatica consuma ogni valore esattamente; un secondo
  agente ha verificato tutto con codice proprio.
- **Alberi in stile pot limit**: una sola size (il piatto) per open, 3-bet, 4-bet e puntate postflop; l'all-in solo quando il
  rilancio al piatto non ci sta, al suo posto; niente donk bet; call solo quando chiude l'azione o contro un all-in (niente
  limp, niente cold call: regole del small; il medium aggiunge il flat del BTN e l'overcall dello SB, le uniche eccezioni);
  al flop e al turn il call che lascerebbe 19 bb (0,117 volte il piatto) è tolto (una sola geometria osservata: la soglia
  sta fra 0,117 e 0,321 volte il piatto). Il gioco (NLHE o PLO) non è scritto nel file: le puntate sono esattamente quelle
  del pot limit.
- **Small** 3.705 nodi (1.739 di decisione), **medium** 11.034 (5.174): medium è small più il flat del BTN sull'open, con le
  sue conseguenze (overcall, squeeze, 6 flop a tre giocatori e 3 a quattro); i sottoalberi postflop comuni sono identici
  valore per valore.
- **Utilità**: il .tree di un calcolo short deck mostrerebbe direttamente le size postflop e i filtri usati. Ignoti: il
  significato dei campi a zero dell'intestazione (ante, blind del bottone) e i codici delle size diverse dal piatto. Lettore
  copiato in `tools/monker_compare/monker_tree_file.py` (sezione 6).

**Decisioni dell'utente del pomeriggio.**

| Tema | Decisione |
|---|---|
| Run 30 × 4 con rake | Verso le 11:45: il gioco più vicino ai default di MonkerSolver (30 × 4) con il rake 2,5 % / cap 2a |
| Spazio su disco | Verso le 12:00 (entro le 12:01; spostamento iniziato alle 12:01:28): i run finiti vanno dall'SSD al disco USB F:; da allora i run si allenano sull'SSD e si archiviano dopo la valutazione (sezione 6) |
| Opzione A (algoritmo) | Solo dopo il risultato del 30 × 4 (12:26); prima il test con il preflop bloccato in quel gioco |
| Test 7 | Dopo il 30 × 4 (piano delle 12:26), poi esteso a 48.000 per la parità di I/N (in coda alle 12:56) |
| Estensione del 30 × 4 | Verso le 13:10: fino a 64.000 iterazioni |
| Texture | Verso le 13:12: texture simili a quelle di MonkerSolver (TXM, TXM2); run TXM2 in coda dopo l'estensione |
| 3-way, fase 3 | Specifica scritta e criticata (9.6); decisioni D1-D7 aperte |
| Impostazioni di MonkerSolver | L'utente non le conosce: non chiederle |

**Lettura alle 14:40.**

1. Con i bucket di default di MonkerSolver e il rake 2,5 % / cap 2a le nostre chart sono le più vicine finora (0,0568 / 0,320
   all'arresto, 0,0532 / 0,289 a 48.000 nell'estensione) e la preferenza suited coincide con quella di MonkerSolver (0,214
   contro 0,224); in EV restano equivalenti (perdita di MonkerSolver entro lo 0,024 % del piatto).
2. Il preflop di MonkerSolver bloccato in quel gioco lascia al CO l'1,03-1,04 % del piatto: il 30 × 4 non lo abbassa, nemmeno
   con il doppio delle iterazioni; fra le leve provate il rake è quella che lo abbassa di più (−35/−36 %), un bucket più
   grossolano lo abbassa del 10-11 %, raffinare l'astrazione lo alza; texture più grossolane (TXM, TXM2) non sono ancora
   state provate con il preflop bloccato. Resta la spinta verso l'open a 5a.
3. I run a bucket fini vanno allenati di più in proporzione; il confronto con la regola dei 10 I/N di MonkerSolver è solo
   indicativo.
4. Secondo stime scalate dall'Hold'em (regola di MonkerSolver ignota), MonkerSolver ha circa 2 volte meno classi di texture
   al turn e 6-8 volte meno al river; il nostro river però non distingue la carta del river (riga = classe del turn, solo i
   bucket la vedono). TXM2 ne imita il numero di classi al turn (un solo dato, Hold'em) e il suo run è in coda.

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
# 30 settembre pomeriggio: 30 × 4 + TX2 + donk con rake 2,5 % / cap 2a (arresto 0,005), test 7; un nuovo lancio sulla stessa cartella riprende dal checkpoint (estensioni)
MAX=60000 THRESHOLD=0.005 BIN=out/monker/bin_rake TRAIN_ARGS="--board-texture-map benchmarks/monker/textures/texture_map_TX2_recommended.txt" bash out/frozen/run_step2_continuous_rake.sh benchmarks/monker/HU50_step2_donk_rake25cap2.json out/monker/buckets_30x4 "<chart MonkerSolver HU 50a>" out/monker/variants/HU50_m30x4_rake25
MAX=64000 THRESHOLD=0 BIN=out/monker/bin_rake TRAIN_ARGS="..." bash out/frozen/run_step2_continuous_rake.sh ... out/monker/variants/HU50_m30x4_rake25
MAX=24000 THRESHOLD=0 BIN=out/monker/bin_rake TRAIN_ARGS="--board-texture-map benchmarks/monker/textures/texture_map_TX2_recommended.txt --lock-charts out/monker_lock/charts_50a --lock-nodes all" bash out/frozen/run_step2_continuous_rake.sh benchmarks/monker/HU50_step2_donk_rake25cap2.json out/monker/buckets_30x4 "<chart MonkerSolver HU 50a>" out/monker/variants/HU50_lock_all_m30x4_rake25
MAX=48000 THRESHOLD=0 BIN=out/monker/bin_rake TRAIN_ARGS="... --lock-nodes all" bash out/frozen/run_step2_continuous_rake.sh ... out/monker/variants/HU50_lock_all_m30x4_rake25
gtosd_preflop_blueprint_monker_values --config benchmarks/monker/HU50_step2_donk_rake25cap2.json --resources-dir out/preflop_blueprint_resources --buckets-dir out/monker/buckets_30x4 --board-class-rows --board-texture-map benchmarks/monker/textures/texture_map_TX2_recommended.txt --policy <run>/policy.bin --charts monker=<chart MonkerSolver HU 50a> --charts ours=<run>/charts/it_<N> --all-flops --threads 8 --expected-rake --out <run>/monker_values_exact.json
# mappe delle texture (sette, TXM e TXM2 comprese), rigenerate byte per byte o confrontate
python benchmarks/monker/textures/generate_texture_maps.py [--check]
# opzione A (in attesa): runner congelato con LAZY_ARG (vuoto per Linear CFR, che non ammette lo sconto lazy)
LAZY_ARG="" MAX=32000 THRESHOLD=0 BIN=out/monker/bin_rake TRAIN_ARGS="--board-texture-map benchmarks/monker/textures/texture_map_TX2_recommended.txt --scheme linear" bash out/frozen/run_step2_continuous_algo.sh benchmarks/monker/HU50_step2_donk.json out/monker/buckets_15x4 "<chart MonkerSolver HU 50a>" out/monker/variants/HU50_algo_linear
# file .tree di MonkerSolver (in sola lettura): rapporto completo, confronto di due alberi
python tools/monker_compare/monker_tree_file.py <file.tree>
python tools/monker_compare/monker_tree_file.py --diff <A.tree> <B.tree>
# archivio su F: di un run finito e valutato (script nella cartella temporanea della sessione)
powershell -NoProfile -ExecutionPolicy Bypass -File <cartella temporanea>/archive_run.ps1 out\monker\variants\<run>
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

Strumenti, file e regole del pomeriggio del 30 settembre (5.11, 9.6):

- **Run finiti sul disco USB F:** (decisione dell'utente verso le 12:00, prima dell'inizio dello spostamento alle 12:01:28). L'SSD C: era al 98 % (circa 20 GB liberi). I run
  finiti sono stati spostati sul disco USB F: (Seagate Basic da 2 TB, 1,8 TiB) in `F:\GTO-Solver-out`, con gli stessi
  percorsi relativi e una directory junction al posto di ogni vecchio percorso: script, viewer e documenti leggono attraverso
  la junction senza cambiare nulla. 37 cartelle, 4.515 file, 299,7 GiB (circa 322 GB; i "GB" di `move_log.txt` sono GiB):
  tutte le cartelle di `out/monker/variants` tranne il run in corso (28), `out/monker/step2`, `out/monker/smoke_continuous`,
  `out/monker/smoke_policy_snapshots`, `out/monker/smoke_policy_snapshots_g1`, `out/matrix`, `out/hu40_history7_solve`,
  `out/suite`, `out/history7_optimized`, `out/hierarchy32`. Per ogni cartella: conteggio di file e byte, `robocopy /MOVE`,
  verifica di file e byte a destinazione, rimozione della sorgente vuota, junction, nuova verifica attraverso la junction; lo
  script si ferma al primo errore (nessuno). Dalle 12:01 alle 14:18, circa 37 MiB/s in media; dopo, C: ha 308,5 GiB liberi.
  Script `move_to_f.ps1` nella cartella temporanea della sessione; giornali `F:\GTO-Solver-out\move_log.txt` e
  `robocopy_log.txt`.
- **Regola da allora**: i run si allenano sull'SSD e vanno su F: dopo la loro valutazione, con `archive_run.ps1` (cartella
  temporanea della sessione; stessa procedura per cartella). Una cartella con il file `NO_ARCHIVE` (messo dagli script in coda
  sulle cartelle ancora in uso) viene saltata; dalle 14:31 lo script prende anche un lock esclusivo per cartella
  (`F:\GTO-Solver-out\locks`). Eseguibili, bucket, risorse, runner congelati e build restano su C:. Una junction non si
  cancella mai in modo ricorsivo (cancellerebbe i file su F:).
- **Incidente delle 14:18-14:31**: due script in coda (`run_test7_ext.sh` alle 14:18:15 e `run_lock_m30x4_rake25.sh` alle
  14:19:07) hanno archiviato insieme la cartella del test 7. Il primo ha finito e verificato (156 file, 3.091.519.957 byte su
  F:, ricontati attraverso la junction anche dopo l'incidente); il robocopy del secondo copiava la destinazione su sé stessa
  attraverso la nuova junction ed è stato bloccato dalle violazioni di condivisione (errore 32 in `robocopy_log.txt`); ucciso
  alle 14:31 (PowerShell 17140, robocopy 33484), nessun file perso (`chain.log`). Da qui il lock per cartella.
- **Mappe delle texture TXM e TXM2** (5.11; non committate): `generate_texture_maps.py` scrive sette mappe (le cinque di prima
  identiche byte per byte), `README.md` aggiornato, file `texture_map_TXM_monker_like.txt` (1.899 classi di turn) e
  `texture_map_TXM2_monker_like.txt` (2.151).
- **`tools/monker_compare/monker_tree_file.py`** (non committato): lettore dei file .tree di MonkerSolver (5.11), copia di quello
  scritto nella cartella temporanea della sessione con tre righe di intestazione; rapporto completo (intestazione, verifiche,
  conteggi per street, regole R1-R5) o `--diff` di due alberi; apre i file solo in lettura. Il testo di aiuto lo chiama ancora
  `monker_tree.py`.
- **Runner congelato `out/frozen/run_step2_continuous_algo.sh`** (12:19, per l'opzione A): copia di
  `tools/monker_compare/run_step2_continuous.sh` con la variabile `LAZY_ARG` (default `--lazy-discount`, vuota per Linear CFR).
- **Script in coda** (cartella temporanea della sessione, non nel repository): `run_m30x4_rake25.sh`, `run_algo_A.sh` (tolto),
  `run_lock_m30x4_rake25.sh` (test 7), `run_test7_ext.sh`, `run_m30x4_ext.sh`, `run_txm2.sh`, `archive_after_m30x4.sh`; file di
  annullamento in `out/monker/variants` (`CANCEL_M30X4`, `CANCEL_ALGO_A`, `CANCEL_TEST7`, `CANCEL_M30X4_EXT`, `CANCEL_TXM2`).

Eseguibili congelati per i run lunghi: `out/monker/bin_allin` (da `410a380`: G2, G3, G4, E, F),
`out/monker/bin_abd` (da `5578ab8`: A, B, D), `out/monker/bin_lock` (costruito alle 20:27 dal codice poi committato
come `c7d6ba0` alle 20:43, a revisione conclusa: test 1, 2, 5 e 5M-5D; `out/monker_lock/bin` è una copia precedente
delle 20:15), `out/monker/bin_rake` (da `3ec4027`: passo 1 con rake, G1 con rake, test 6, 6b, 6c, 6d e B con rake; il 30 settembre
pomeriggio anche il 30 × 4 con rake, il test 7 e le estensioni),
`out/monker/bin_threeway` (programma della tabella a tre giocatori, costruzione completa del 30 alle 02:27) e
`out/monker/bin_3way_step1` (da `67de1d7`, copiati alle 05:35: `gtosd_preflop_blueprint_checkdown_classes` e
`gtosd_preflop_blueprint_monker_tree` per i run 3-way del passo 1, 9.5). Copie congelate del runner in
`out/frozen/` (`run_step2_continuous.sh`, `run_step2_continuous_abd.sh`, `run_step2_continuous_lock.sh`,
`run_step2_continuous_rake.sh`, `run_step2_continuous_algo.sh`): devono stare due livelli sotto la radice del repository, perché il runner calcola
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
solo locali: il branch non è pushato. Poi i documenti: `5c8343f` (mattina del 30) e `061f167` (viewer 3-way).

Pomeriggio del 30 settembre (fino alle 14:40): nessun commit di codice. Non committati: le mappe TXM e TXM2 con il generatore
e il README modificati, le configurazioni dei rake (sopra), `tools/monker_compare/monker_tree_file.py`, la specifica della fase
3 (`threeway/PHASE3_SPEC_2026-09-30.md`) e questi documenti.

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
1 (9.4, 9.5). Fatti il 30 settembre dalle 09:30 alle 14:40: run 30 × 4 con rake 2,5 % / cap 2a, test 7 e sua estensione,
studio I/N, ricerca sulle texture di MonkerSolver e mappe TXM e TXM2, opzione A preparata, lettore dei file .tree (5.11);
run finiti spostati su F: (sezione 6); specifica della fase 3 del 3-way (9.6). In corso alle 14:40: estensione del 30 × 4
fino a 64.000 iterazioni, poi il run TXM2 (in coda). Fatti il 1° ottobre: batteria di correttezza HU chiusa (10.13); fase 3a del
3-way, merge e primo run 3WAY50 in coda per le 00:00 del 2 ottobre (9.7). Stato dei punti aperti il 28 (i punti 1 e 3 del 28 sono superati dalle
misure e dalle decisioni del 29):

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
   riferimento (nessuno, 5 % / cap 3a, 5 % / cap 2a, 2,5 % / cap 2a, 5 % / cap 0,75a). **Aggiornamento del 30 settembre
   pomeriggio (5.11)**: su richiesta dell'utente è stato provato il gioco con i bucket di default di MonkerSolver (30 × 4) e il
   rake 2,5 % / cap 2a: chart più vicine di G1 (0,0568 / 0,320 all'arresto a 32.000 contro 0,0641 / 0,350; sopra le soglie
   del 5.1 contro G1, al limite contro G1+ e A continuato) e preferenza suited uguale a quella di MonkerSolver (0,214 contro
   0,224); in EV equivalente. Il test 7 dice che il 30 × 4 non rende il preflop di MonkerSolver più coerente nel nostro gioco:
   la scelta dei bucket resta una questione di somiglianza delle chart, non di correttezza. In attesa: l'estensione fino a
   64.000 e il run con le texture TXM2 nello stesso gioco. Candidato: 30 × 4 + TX2 (o TXM2) + donk con rake 2,5 % / cap 2a,
   la stessa scelta che la specifica della fase 3 propone per il 3-way (D1 sul server, D2); da decidere con l'utente dopo
   quei due run.
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
   guadagno massimo sotto 2e-4 % del piatto, 9.5); serve per la fase 3. **Aggiornamento del 30 settembre pomeriggio
   (9.6)**: la specifica della fase 3 propone 0,008 sulla media delle 18 chart 3-way che non affrontano un all-in, fra
   salvataggi distanti 4.000 iterazioni, minimo 16.000 e tetto 48.000 (D3). È la soglia equivalente agli arresti HU: al loro
   arresto a 0,005 sulla media di tutte le chart, le chart HU non all-in cambiavano ancora di 0,0073-0,0085; nel 3-way 36
   delle 54 chart affrontano un all-in e una media su 54 a 0,005 sarebbe più larga della regola HU. Da decidere con l'utente.
   **Aggiornamento del 1° ottobre (9.7)**: l'utente ha accettato la D3 (0,008 sulle 18 chart non all-in, minimo 16.000, tetto
   48.000); è la regola del primo run 3WAY50, in coda per le 00:00 del 2 ottobre.
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
   **Aggiornamento del 30 settembre pomeriggio (9.6)**: la specifica della fase 3 è scritta e rivista dopo due critiche
   indipendenti (19 rilievi accolti): percorso a tre posti separato nel trainer (HU identico byte per byte), 26-36 ore di
   agente, 2-4,5 s per iterazione sull'i3 (stima), primo run a 15 × 4 in double sull'i3 (picco circa 14,8 GB); il codice parte
   dopo le decisioni D1-D7 (punto 9).
   **Aggiornamento del 1° ottobre sera (9.7)**: fase 3a fatta (codice in tre corsie, review, gate 3a tutto PASS tranne la clausola
   della traiettoria di V7, accettata come limite del disegno del test), merge `238a41e`; primo run 3WAY50 a 15 × 4 (1,5 s per
   iterazione misurati, picco 14,86 GB) in coda per le 00:00 del 2 ottobre.
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

   **Aggiornamento del 30 settembre alle 14:40.** Nel colloquio (dalle 11:45 circa) l'utente ha deciso lo spazio su disco
   (run finiti su F:, sezione 6), il run 30 × 4 con rake 2,5 % / cap 2a, poi il test 7 e la sua estensione, l'estensione del
   30 × 4 fino a 64.000, le texture simili a quelle di MonkerSolver (run TXM2 in coda), e ha tenuto l'opzione A fino al
   risultato del 30 × 4 (5.11); ha confermato che non conosce le impostazioni di MonkerSolver. Restano aperti gli altri punti
   dell'elenco: configurazione HU di riferimento (punto 1, dopo l'estensione e il TXM2), criterio di somiglianza in EV, push,
   calcolatore web. Le domande del 3-way sono ora le decisioni D1-D7 della specifica della fase 3 (9.6): D1 astrazione
   (proposta: 15 × 4 in double sull'i3, 30 × 4 in double su un server), D2 rake del passo 2 (proposta: prima 2,5 % / cap 2a,
   poi 5 % / cap 0,75a sul server), D3 regola di arresto (proposta: 0,008 sulle 18 chart non all-in; sostituisce lo 0,005 del
   punto 5), D4 ambito della valutazione (proposta: parte A), D5 server a noleggio per 2-3 giorni dall'1/10 (senza server,
   ambito ridotto sull'i3), D6 build fino alle 24:00 del 30, D7 conferme (carte foldate morte, aggiornamenti alternati, cache
   per classi, seed di default, igiene dei run). La specifica prevede il codice da circa le 15:00 del 30.

   **Aggiornamento del 1° ottobre (9.7).** D1-D4 e D7 prese la mattina come proposte, D5 senza server (ambito ridotto sull'i3), D6
   superata; verso le 18:00 la D7 è cambiata con un checkpoint ogni 16.000 iterazioni. Push del branch il 1° ottobre alle 09:10 e
   alle 10:29 (10.13); al merge delle 18:11, 12 commit locali.
10. **Opzione A e run in corso** (5.11): l'opzione A (Linear CFR, DCFR con un board per batch, Linear simultaneo con un board
    per batch, sul gioco G1) è pronta (runner congelato, smoke passati) e aspetta la decisione dell'utente; se parte, sul
    gioco del 30 × 4 con rake se questo diventa il riferimento. Alle 14:40 gira l'estensione del 30 × 4 fino a 64.000
    (valutazione esatta a 64.000, poi archivio su F:), in coda il run TXM2 (64.000 iterazioni, valutazione, archivio).

## 9. 3-way a 50a (dalla notte del 30 settembre)

Obiettivo: le chart MonkerSolver 3-way a 50a dell'utente (`GTO-Chart-Browser/ranges/Short Deck/Symmetrical
Chart/3-way/50a`, 54 chart di decisione: UTG 16, CO 18, BTN 20). Decisione dell'utente della notte: fasi 1 e 2
subito, in parallelo alle prove HU (revoca il rinvio delle 20:00 del 29); le impostazioni della fase 3
aspettano l'HU. Lavoro in due rami di lavoro separati (fase 1; fase 2a), poi integrato nel ramo
`feat/monker-step1-checkdown` dopo il commit del rake. Specifiche di progetto, così come scritte, con le loro
critiche indipendenti: [fase 1](threeway/PHASE1_SPEC_2026-09-29.md), [fase 2a](threeway/PHASE2A_SPEC_2026-09-30.md),
[fase 3](threeway/PHASE3_SPEC_2026-09-30.md) (9.6). Codice, gate, merge e primo run della fase 3a: 9.7.

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
`subagents/workflows/wf_e198de0e-ef3/journal.jsonl`; copia leggibile in `scratchpad/p2b_results.txt`). Il viewer 3-way
(artifact privato dell'utente "Short Deck 3-way 50a", pubblicato il 30/09 alle 09:30; generatore
`scratchpad/threeway/viewer/build_viewer_3way.py` nella cartella temporanea della sessione, non nel repository) mostra i
cinque run di 9.5 con la tabella dei 54 nodi e le griglie 9x9 MonkerSolver / nostre / distanza per mano.

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

### 9.6 Fase 3: la specifica (30 settembre, dalle 13:20 alle 14:30)

Specifica del passo 2 a tre giocatori (postflop sparso con bucket per board), scritta dalle 13:20 alle 13:40 e rivista dalle
14:00 alle 14:30 dopo due critiche indipendenti, una sulla correttezza (9 rilievi) e una su costi, memoria, tempi e logistica
(10 rilievi): 19 rilievi, tutti accolti, alcuni con una correzione di dettaglio, elencati nella sua sezione 12. Copia così
come scritta, in inglese (1.531 righe), in [threeway/PHASE3_SPEC_2026-09-30.md](threeway/PHASE3_SPEC_2026-09-30.md). È solo
un progetto: il repository è stato letto, non modificato; le stime sono segnate come tali.

- **Ambito**: alberi `3WAY50_donk*` (7.225 nodi, 54 chart su 54), regole postflop di G1, righe per classe di board (TX2, river
  per classe del turn), bucket 15 × 4 o 30 × 4, carte foldate morte; confronto con le 54 chart di MonkerSolver 3-way 50a.
  Fuori: 4-6 giocatori, 3WAY100, la migliore risposta completa (parte B, più tardi, su un server).
- **Algoritmo**: un percorso a tre posti separato nel trainer (funzioni proprie), così che l'output HU resti identico byte per
  byte; CFR vettoriale con campionamento pubblico del caso, tre passate alternate per iterazione (UTG, CO, BTN), ognuna con 32
  board nuovi. Ai nodi degli altri si divide solo il vettore di chi agisce e quello del terzo passa invariato: chi ha foldato
  resta con il reach congelato, cioè con le carte morte. Scorciatoia per l'eroe che ha foldato (valore costante, sottoalbero
  saltato).
- **Terminali**: showdown postflop con una sola scansione in ordine di forza e inclusione-esclusione sulle carte condivise dalle
  due mani avversarie (costo O(n·k) con k ≤ 36 invece di O(n³)); in short deck circa il 99 % delle mani vive ha lo stesso
  rango di un'altra, quindi i termini dei pareggi girano quasi sempre: circa 375-520 Mflop per board e passata (stima), contro
  circa 1 in HU. Terminali preflop (36 all-in, 25 fold) da una cache per classi costruita su `preflop_three_way_v1.bin`, esatta
  su tutti i runout, scalata di C(30,5)/C(34,5) = 0,512140 per restare non distorta; una modalità di sola validazione
  (`board_kernels`) li calcola con i kernel.
- **Memoria** (3WAY50): 15 × 4 13,56 GB di tabelle in double, picco circa **14,8 GB** (ci sta sull'i3); 30 × 4 27,11 GB, picco
  circa **29,2 GB**, in double solo su un server (sull'i3 in misto, picco 22,4 GB, o in float32, 15,6 GB); con le classi TXM il
  30 × 4 scenderebbe a 11,99 GB (picco 13,2).
- **Iterazioni e tempi** (stime): gli aggiornamenti per riga non dipendono dalla dimensione dell'albero, quindi il 3-way chiede
  circa le iterazioni dell'HU allo stesso livello di convergenza (24.000-40.000 a 15 × 4 con la regola proposta); **2-4,5 s per
  iterazione sull'i3** (0,4-1,0 s su un server), cioè 13-50 ore sull'i3, da una a tre finestre giornaliere 00:00-20:00 (più
  probabilmente due), e 2,7-11 ore sul server.
- **Regola di arresto proposta**: 0,008 sulla media delle 18 chart che non affrontano un all-in (punto 5 della sezione 8).
- **Valutazione**: parte A attraverso il trainer con la policy fissa (EV per posto e identità del rake, guadagno della migliore
  risposta solo preflop per posto, chart di MonkerSolver giocate nel nostro gioco, test con le 54 chart bloccate), esatta su
  605.088 board (1-2,5 ore sul server, 8-18 sull'i3) o campionata su flop fisici; parte B (migliore risposta completa,
  NashConv) più tardi, sul server.
- **Validazione**: V1-V13 più V9b, fra cui l'identità byte per byte dell'HU, la forza bruta di ogni kernel, "HU più una terza
  mano morta uniforme = HU", l'uguaglianza con il checkdown per classi del passo 1 con i terminali preflop calcolati dai kernel
  (V9) e cache = kernel per ogni riga (V9b), la forza bruta del percorso a tre posti del trainer (V13), il determinismo dei
  thread e le identità del rake.
- **Piano**: tre corsie in worktree separati (kernel; trainer; strumenti e cache), **26-36 ore di agente** secondo le stime
  della specifica (cammino critico 12-16 ore più 1-2 di build e test; le fasi 2a e 2b erano andate 2-3 volte più veloci delle
  loro specifiche), parte A 6,5-9 ore in più. Con il codice dalle 15:00 circa del 30, la fase 3a sarebbe pronta fra le 21:00 e
  le 02:00 (calibrata) o fra le 04:00 e le 09:00 dell'1/10 (stima della specifica); il primo run 3-way sull'i3 arriverebbe
  all'arresto molto probabilmente il 02/10, forse il 03/10.

Decisioni per l'utente (sezione 9 della specifica), aperte alle 14:40:

| # | Tema | Proposta della specifica |
|---|---|---|
| D1 | Astrazione | 15 × 4 in double sull'i3 per il primo run (dall'1/10); 30 × 4 in double su un server come run di riproduzione; senza server, 30 × 4 in misto sull'i3 dopo il primo run oppure niente 30 × 4 |
| D2 | Rake del passo 2 3-way | Prima 2,5 % / cap 2a (miglior distanza HU con 30 × 4, somma più bassa nei test 6-6d); poi 5 % / cap 0,75a sul server (circa 0,745 a dalle call miste del 3-way a 50a, preflop di MonkerSolver meno sfruttabile nel passo 1 3-way); 5 % / cap 3a sconsigliato |
| D3 | Regola di arresto | 0,008 sulle 18 chart non all-in ogni 4.000 iterazioni, medie su 54, 36 e 18 chart registrate, minimo 16.000, tetto 48.000; run con il preflop bloccato a soglia 0 e 24.000 iterazioni a 15 × 4, 48.000 a 30 × 4 |
| D4 | Ambito della valutazione | Parte A (esatta sul server, campionata sull'i3); parte B come fase 3c sul server |
| D5 | Server | Noleggio dalla mattina dell'1/10 per 2-3 giorni (circa 21-65 ore di server, disco di almeno 400 GB); senza server l'ambito completo finisce verso il 06-14/10, quello ridotto proposto (parte A campionata, un solo rake, test bloccato a 15 × 4, niente 30 × 4) verso il 03-05/10 |
| D6 | Build la sera del 30 | Fino alle 24:00 (senza, il run principale parte alle 01:30-03:00 invece che alle 00:00-02:30) |
| D7 | Conferme | Carte foldate morte, aggiornamenti alternati UTG → CO → BTN, cache per classi con la modalità `board_kernels`, seed di default, igiene dei run (STOP giornaliero alle 19:40 con il solo checkpoint, niente checkpoint periodici sull'i3, policy ogni 16.000 iterazioni, archivio su F: solo fra le 20:00 e le 24:00 e mai durante un salvataggio) |

Due punti della specifica aspettavano il test 7, arrivato alle 14:18 (5.11): il D2 va rivisto se il test 7 contraddice il
rake 2,5 % / cap 2a, e non lo contraddice (il rake abbassa ancora il guadagno del CO: 1,04 % contro 1,60 % del 5M); il tetto
dei run bloccati a 30 × 4 (48.000) va rivisto confrontando 24.000 e 48.000, e il guadagno del CO non cambia (1,04 -> 1,03 %).
Le cifre HU della specifica che venivano dai run del pomeriggio (0,057 di distanza, preferenza suited uguale a MonkerSolver)
sono confermate dai file (5.11).

### 9.7 Fase 3a: codice, gate 3a, merge e primo run in coda (1° ottobre, dalle 09:24 alle 18:12)

Rapporti nella cartella temporanea della sessione, `threeway/phase3a/`: `gate3a.md` (integrazione, 12:22-14:25), `review.md`
(review indipendente, 14:20-14:56), `deferred_tests.md`, `gate3a_final.md` (16:30-17:25), `final_chain.log` (catena
finale: gamba 3-way di V9 alle 16:45-16:50, controlli a 15 × 4 alle 18:00-18:11) e `final_chain_run2.out` (controlli a 15 × 4); in `threeway/run1/`: `NOT_READY.md`, `final_freeze.sh`,
`queue_3way50_15x4.sh` e le prove della coda (`qtest2/driver.log`). Codice nel worktree
`C:/Users/GoryNickel/Documents/GitHub/GTO-Solver-phase3`, branch `feat/threeway-step2` (da `d0796f8`). Cronologia nel diario
(PROGRESS_LOG, voci del 1° ottobre dalle 08:00 alle 16:30 e della sera). [V] = verificato (misurato, o letto nei file, nei log
o nel codice); [I] = inferito (calcolo, stima o ragionamento).

**Decisioni dell'utente sulla specifica (9.6)**, prese la mattina del 1° ottobre come proposte, salvo dove è scritto
altrimenti [V, diario]:

| # | Decisione |
|---|---|
| D1 | 15 × 4 in double sull'i3 |
| D2 | Rake 2,5 % / cap 2a, poi 5 % / cap 0,75a; con l'ambito ridotto (D5) un solo rake: il 2,5 % / cap 2a del primo run |
| D3 | Arresto a 0,008 sulla media delle 18 chart non all-in fra salvataggi distanti 4.000 iterazioni, minimo 16.000, tetto 48.000 |
| D4 | Parte A, campionata sull'i3 |
| D5 | Niente server, quindi l'ambito ridotto sull'i3: parte A campionata su 64 flop fisici, un solo rake, test del blocco solo a 15 × 4, nessun run 30 × 4 |
| D6 | Superata (riguardava le build della sera del 30) |
| D7 | Confermata (carte foldate morte, aggiornamenti alternati UTG → CO → BTN, cache per classi, seed di default, igiene dei run). **Cambiata verso le 18:00** (domanda della sessione principale): un checkpoint ogni 16.000 iterazioni, per non perdere il lavoro in un crash o in un riavvio di Windows Update, al posto di "niente checkpoint periodici sull'i3" |

Altre decisioni del giorno [V, diario]: il rake serve solo a riprodurre le chart di MonkerSolver (nelle soluzioni sue l'utente
sceglierà il proprio); prima di un run della fase 3, proporre una batteria di correttezza 3-way; il 02/10 senza la finestra solita
(dal 03/10 di nuovo run 00:00-20:00 con pausa alle 19:40); primo run alle 00:00 del 02/10 se i controlli rinviati sono verdi.

**Cosa è stato costruito** [V, log del worktree e `review.md`]. Dieci commit: sette nelle tre corsie (10:37-11:38), due di
integrazione (13:16 e 13:59), uno della review (14:53).

| Corsia | Commit | Contenuto |
|---|---|---|
| K (kernel) | `86dd104`, `2ebc4a5` | Kernel multiway a tre seggi: showdown a tre attivi con inclusione-esclusione sulle carte condivise, terminali a due attivi con il foldato come insieme statico (carte morte); forma statica G della massa multiway dei deal (K4) |
| T (trainer) | `306464a`, `161758d`, `fef4305` | Cache delle classi preflop a tre giocatori (scala 0,512140); percorso a 3 seggi del trainer con funzioni proprie, così che l'HU resti identico byte per byte; scorciatoia per l'eroe che ha foldato presa prima della ricerca dell'unità |
| C (strumenti) | `12492c3`, `4e4bce3` | Metrica di arresto 3-way (`non_all_in_mean_distance` in `compare_charts.py`), impostazioni del runner e pausa giornaliera; CLI di training, chart e blocco delle chart a 3 seggi |
| Integrazione | `726c112`, `6ec3016` | Test K5 (V8 attraverso `terminal3`, V13, V9b a livello dei terminali) e T3 (V4-V7, V9, V9b, V12 smoke); nessuna correzione fra le corsie |
| Review | `12fe441` | Tre difetti minori corretti (sotto) |

**Review indipendente** (14:20-14:56; 9 commit, 27 file, +8.737 / −191, tutto letto) [V, `review.md`]:

- nessun difetto di correttezza: mappa dei seggi, carte foldate morte, ordine della scorciatoia, D3, peso delle coppie, scala
  0,512140, rake per insieme di vincitori, identità e determinismo controllati alla lettura;
- quattro mutazioni di prova, mai committate (buffer di unità stantio, foldato letto dal bit sbagliato, vettore congelato
  sbagliato a due attivi, trasposizione sbagliata nella cache), prese ciascuna dal test atteso (una con un crash);
- corretti in `12fe441`: `--canonical-river-boards` rifiutato senza `--checkdown` (su un albero con il postflop distorcerebbe la
  legge del caso), errore esplicito della CLI per la scorciatoia spenta a 3 giocatori senza `board_kernels`, byte delle liste di
  unità nella telemetria della memoria; più un commento;
- rilievi informativi, fra cui il file di pausa letto prima di quello di stop: una finestra di secondi al giorno in cui il run va
  in pausa senza `policy.bin` e l'arresto slitta allo snapshot seguente, fino a 4.000 iterazioni dopo.

**Gate 3a, controllo per controllo** [V, `gate3a.md`, `gate3a_final.md`, `final_chain.log`, `final_chain_run2.out`].
All'integrazione (12:22-14:25) i controlli sono girati sugli alberi e sul codice di produzione ma con le tabelle piccole
200/500/1000 per board (circa 5 milioni di celle invece di 847 milioni), perché la coda HU lasciava 5,8-8 GB liberi; la sera
sono girati la build finale a `12fe441`, la ctest, V1, la gamba 3-way di V9 e i controlli a 15 × 4.

| Controllo | Cosa verifica | Esito |
|---|---|---|
| Build e ctest | Build della suite a `12fe441` (16:30:42-16:33:49, nessun warning con /W4 /WX); ctest `preflop_blueprint` e `card_abstraction` (16:36:47-16:44:47, 480,3 s) | **PASS**, 101/101 |
| V1 | Output HU identico byte per byte | **PASS**: IDENTICAL contro `v1_baseline` e `v1_c123` sulla build finale, 13 campi × 3 fixture (g1, rake, hu10): identità, fingerprint di policy e stato, sha256 di `state.ckpt` e `policy.bin`, 18 chart |
| V2 | Kernel contro forza bruta | **PASS**: 4,4e-15; con i seggi scambiati falliscono tutti i 66 + 156 casi distinguibili |
| V3 | HU più una terza mano foldata uniforme = HU | **PASS**: 351 × masse HU, 2,8e-15 |
| V4 | Cache delle classi | **PASS**: 115 tensori identici byte per byte al passo 1, scala esatta 0,512140 su tutti i 278.256 board |
| V5 | Payoff dei perdenti e dei foldati | **PASS**: quattro giochi alterati rifiutati, la copia esatta accettata |
| V6 | Scorciatoia accesa e spenta | **PASS**: 4,6e-15 della scala (soglia 1e-12), stati identici a 2 e 8 thread |
| V7 | Harness a tre seggi = HU (G1 / HU50 con rake) | **Parziale, accettato**: un'iterazione da zero a 2,0e-13 / 4,1e-13 della scala, un'iterazione alternata da una policy densa a 0,076 / 0,045 della tolleranza; la clausola della traiettoria a 100 iterazioni (0,001 sulle chart, 1e-6 a sull'EV) non è rispettata: 0,46 / 0,33 sulle righe preflop, 0,028 / 0,019 a sull'EV |
| V8 | Identità del rake | **PASS**: 1,65e-15 attraverso `terminal3` (3 fixture × 3.701 terminali postflop × 20 board, ogni seggio come eroe) |
| V9 | Uguale al passo 1 per classi sull'albero checkdown (19.998 board canonici, 6 iterazioni) | **PASS**: gamba HU 6,9e-14 / 8,4e-14 all'iterazione 1 (soglia 1e-9). Gamba 3-way (16:45-16:50, `board_kernels`, 8 thread, 27,5-27,7 s per iterazione), `3WAY50_donk_rake` / `_rake25cap2`: 1,45e-12 / 5,64e-12 all'iterazione 1, righe medie 2,8e-11 / 3,5e-12 (soglia 0,001), EV 1,2e-13 / 4,6e-13 a e guadagno 2,9e-13 / 5,3e-13 a (soglia 1e-6 a). Secondo la review è l'unico oracolo indipendente della traversata preflop a 3 seggi e del calendario alternato dei tre eroi |
| V9b | Cache = kernel | **PASS**: 5,2e-14 ai terminali (200 termini, 19.998 board), 2,7e-11 / 4,5e-12 sulla passata |
| V10 | Determinismo dei thread | **PASS** a tabelle piccole (stato `fnv1a64:25f269dcb61ae069` a 1, 2 e 8 thread) e a 15 × 4 (18:05-18:09, 20 iterazioni, `--validation`): stato `fnv1a64:6a94a517133aa13a` a 8, 2 e 1 thread, 54 chart con lo stesso digest `9da6182ae65102c6`, 0 file non finiti |
| V11 | Parte A | Fuori dal gate 3a: è il gate 3b |
| V12 | Albero, chart e blocco | **PASS** a tabelle piccole e a 15 × 4 (18:09-18:11): albero `nodes=54 files=54`; blocco alle chart 3-way 50a di MonkerSolver 2.681 righe + 1.688 fuori range + 5 di ripiego; ritorno 54 file, differenza massima 0,0010, 0 righe oltre k × 0,0005 + 0,0005 e, a 15 × 4, 0 righe con un altro stato di zero (a tabelle piccole erano 2, UTG AKs, per le chart stampate a tre decimali) |
| V13 | Forza bruta a livello del trainer | **PASS**: 1,2e-14 sui valori delle azioni (soglia 1e-12), 2.301 decisioni dell'eroe tracciate, 45 unità saltate attraversate |

**La clausola della traiettoria di V7.** Dopo l'iterazione 1 del run campionato 122 celle di regret sono esattamente 0 su un solo
percorso e 122 hanno il segno opposto (per esempio −3,1e-23 contro +4,0e-23): il percorso a tre seggi calcola le masse per
inclusione-esclusione, e un pareggio esatto lascia un residuo di arrotondamento dove i kernel HU danno 0. Il regret matching ne fa
righe diverse dall'iterazione 2 e le traiettorie campionate si separano come quelle di due seed. Il gate e la review lo giudicano
un limite del disegno del test, non un difetto: la clausola presumeva che l'arrotondamento restasse piccolo attraverso il regret
matching. In produzione tocca righe senza segnale su quel board, che i board seguenti sovrascrivono [I, review]. Il V7 statistico
suggerito dalla review (distanza harness-HU contro la distanza fra due seed HU dopo qualche migliaio di iterazioni) non è stato
fatto: non serve al gate.

**Il blocco della memoria (16:50-18:03)** [V, `gate3a_final.md`, `final_chain_run2.out`]. I controlli a 15 × 4 e il run vogliono
almeno 18.874.368 KB liberi (14,8 GB del trainer più 3 di margine) e nessun altro training. Dalle 16:50:41 lo smoke ha aspettato 30
minuti ed è uscito per timeout alle 17:21:26: memoria libera fra 14,7 e 16,0 GB (31 letture). I valori sotto 15 GB cadono fra
le 16:51 e le 16:56, mentre girava la prova della coda con il trainer vero su HU G1 (il gate non conta i training del worktree
della fase 3); dalle 16:57 alle 17:21, senza training nostri, 15,6-16,0 GB [V, `gate.log` di `out/final_smoke_t8` e `queue.log`
di `out/qtest_queue` nel worktree]. La memoria mancante la tenevano le
applicazioni dell'utente (memoria privata alle 16:58: Brave 4,2 GB in 33 processi, Claude 2,8, ChatGPT 1,8, Steam 1,0, Telegram
0,9, Discord 0,8). Verso le 18:00 l'utente ha chiuso Brave; alle 18:00:35 i KB liberi erano 18.700.448, appena sotto, e alle
18:03:39 il gate si è aperto con 19.051.048.

**Misure a 15 × 4** [V, `train.jsonl` di `out/final_smoke_t8` e `out/final_smoke_t4` nel worktree, `final_chain_run2.out`].
3WAY50, 15 × 4 + TX2, double, rake 2,5 % / cap 2a, 32 board per eroe e 96 per iterazione:

| Misura | Valore |
|---|---|
| Smoke a 8 thread, 20 iterazioni | **1,499 s per iterazione**; preparazione 7,3 s; uscita 0 in 54 s |
| Smoke a 4 thread, 10 iterazioni | 1,766 s per iterazione |
| V10 (`--validation`), 20 iterazioni | 1,418 s per iterazione a 8 thread, 2,825 a 2, 5,022 a 1 |
| Stato | 14.324.289.748 byte (14,32 GB) |
| Picco del working set | 14.864.637.952 byte (14,86 GB) a 8 thread; 14.856.216.576 a 4 |
| Picco del commit privato | 14.910.541.824 byte a 8 thread |

- Rispetto alla specifica (9.6): 1,5 s per iterazione, sotto la forbice stimata di 2-4,5 s e sotto i 2,6 s misurati a 8 thread
  con le tabelle piccole sulla macchina carica; il picco è quello stimato (circa 14,8 GB).
- **Scala con i thread** [I, rapporti dei tempi della tabella; il punto a 4 thread viene dallo smoke, gli altri da V10]: da 1 a 2
  thread 1,78 volte, da 1 a 4 2,84, da 1 a 8 3,54 (l'i3-10100F ha 4 core fisici: da 4 a 8 thread il guadagno viene
  dall'hyperthreading). Un fit di Amdahl dà circa il 12 % di lavoro non parallelo (12-14 % dai punti a 2 e 4 thread); su 48 core
  fisici sarebbero circa 0,5-0,8 s per iterazione, a parità di velocità per core e senza limiti di banda della memoria (la
  specifica stimava 0,4-1,0 s su un server).

**Guardie del 30** (10.12). Verso le 17:58 la sessione principale ha fermato `ext/stop_ext_1958.ps1` e
`night2/stop_night2_1958.ps1`: alle 19:40 avrebbero scritto file CANCEL nelle cartelle dei loro run, già archiviate su F:
attraverso le junction [V, sessione principale].

**Merge e congelamento** [V, `git log`, file `SOURCE.txt`, `queue.log`]:

- 18:11:32, merge `238a41e` di `feat/threeway-step2` (`12fe441`) in `feat/monker-step1-checkdown`. `git diff 12fe441 238a41e`
  su `libs`, `include`, `benchmarks`, `tests` e `tools` è vuoto: il codice del branch è quello del gate. Al merge il branch è 12 commit avanti a
  origin, non pushato.
- 18:11:39-18:11:40, `run1/final_freeze.sh` (FREEZE_OK e DRY_RUN_OK [V, sessione principale]), che aggiunge solo file nuovi nel
  checkout principale e rifiuta di sovrascrivere:
  - `out/monker/bin_3way_step2/`: train (sha256 `12c0dd6b8ccd…`) e monker_tree (`274505865911…`) della build del gate, con
    `SOURCE.txt`;
  - `out/frozen/threeway_step2_12fe441/`: runner, `compare_charts.py`, configurazione, manifest dell'albero e mappa TX2 estratti
    con `git show 12fe441`, sha256 in `SOURCE.txt`. La copia è autosufficiente: il runner lavora dalla propria radice, e il
    `compare_charts.py` del checkout principale prima del merge non aveva `non_all_in_mean_distance` (con quello il cambiamento
    sarebbe sempre `na` e il run andrebbe al tetto);
  - `out/frozen/queue_3way50_15x4.sh` (sha256 `7581f0f8…`).

**Il primo run e la sua coda** [V, `queue.log` e script della coda, salvo dove è segnato I]:

- **Configurazione**: `3WAY50_donk_rake25cap2.json` (3 giocatori, 50a, rake 2,5 % / cap 2a, no flop no drop),
  `out/monker/buckets_15x4`, mappa TX2, le 54 chart 3-way 50a di MonkerSolver; 8 thread, double, DCFR con aggiornamenti alternati,
  batch 32; chart ogni 4.000 iterazioni, arresto a 0,008 sulle 18 chart non all-in, minimo 16.000, tetto 48.000; **checkpoint ogni
  16.000** (decisione dell'utente delle 18:00), policy ogni 16.000 con una riserva di 40 GB, pausa attiva. Uscita
  `out/monker/step2_3way/3WAY50_15x4_rake25cap2`.
- **Lancio**: coda partita alle 18:11:54 (pid 1644 in `queue.lock`, nessun override), tutti i controlli ok (sha256 fissate di
  trainer, runner, `compare_charts.py`, configurazione e mappa; contenuto della configurazione; 54 chart; cartella nuova su C:;
  267 GB liberi), in attesa delle 00:00 del 02/10.
- **Memoria**: prima di ogni (ri)partenza il gate (almeno 18.874.368 KB liberi, nessun trainer); se è chiuso la coda aspetta e
  scrive nel log ogni 10 minuti.
- **Finestra**: il 02/10 è un giorno libero e il run gira senza pausa. Dal 03/10 il file PAUSE resta presente dalle 19:40: il
  trainer scrive il solo checkpoint ed esce PAUSED, la coda riparte alle 00:00 dallo `state.ckpt`, e dopo le 19:40 non parte nessun
  giro.
- **Fine e arresti**: la coda finisce con STOPPED (regola di arresto) o ITERATION_LIMIT (tetto); un errore la chiude senza nuovi
  tentativi. Arresto a mano con `QUEUE_CANCEL` (pausa pulita con checkpoint). Nessun archivio su F: prima della parte A.
- **Prove della coda** con un trainer finto (`qtest2/driver.log`, 17:41-17:54): T1 (giorno libero, arresto al minimo di 16.000),
  T2 (nessuna partenza dopo le 19:40), T3 (PAUSE tenuto durante l'avvio, PAUSED, ripresa fissata alle 00:00), T4 (annullamento →
  PAUSED a 8.600, annullamento rimasto → rifiuto, ripresa → STOPPED), T5 (errore → fine senza nuovi tentativi), T6 (cartella
  estranea rifiutata), T7 (junction nel percorso di uscita rifiutata, dopo una correzione del driver) tutti PASS; cartella delle
  chart di MonkerSolver invariata. Un primo giro dalle 17:39 non era valido: il driver leggeva i codici di uscita in una subshell.
  Le stesse prove hanno riprodotto due volte un difetto della versione della coda del pomeriggio (PAUSE scritto una volta sola e
  cancellato dall'avvio del runner o del trainer: il run avrebbe girato oltre le 19:40), corretto nella versione congelata. Alle
  16:51-16:57 la pausa era stata provata con il trainer vero su HU G1 (pausa a 1.668, ripresa da 1.668, annullamento durante il run
  → pausa a 2.840).
- **Limite**: le prove sono state fatte con `CHECKPOINT_EVERY=0`, prima della decisione delle 18:00: la copia provata
  (`dry_stage`, sha256 `608df367…`) differisce da quella congelata (`7581f0f8…`) solo per `R_CHECKPOINT_EVERY` e per il testo
  di una riga del dry run [V, diff]. Il checkpoint periodico (`--checkpoint-every`) chiama nel trainer lo stesso salvataggio
  delle pause e della fine, e nel runner il default HU è 20.000 [V, codice]; con questa coda e a 15 × 4 non è stato
  provato. Un crash o un riavvio fermano
  anche la coda: si rilancia lo stesso comando, dopo aver controllato che il PID di `queue.lock` non esista più e averlo tolto, e il
  run riparte dall'ultimo checkpoint [I].

**Fine prevista** [I]. A 1,5 s per iterazione (± 30 %): 16.000 iterazioni in circa 7 ore; l'arresto atteso dalla specifica, fra
24.000 e 40.000 (una stima per analogia con l'HU), in circa 10-17 ore, cioè il 02/10 fra le 10:30 e le 17:30; il tetto di 48.000
in circa 20 ore. Secondo `gate3a_final.md`, sotto 3,4 s per iterazione l'intero run, tetto compreso, finisce prima della prima
pausa (03/10 alle 19:40). Il costo dei checkpoint ogni 16.000 non è misurato: la specifica (4.3) stima 13,56 GB e 1,3-4,5 minuti
per un checkpoint a 15 × 4 (regret e somme, cioè lo stato di 14,32 GB senza i timestamp dello sconto lazy, 0,77 GB [V,
`memory_breakdown` dello smoke]).

**Cosa resta.**

1. Il risultato del run 1: iterazione dell'arresto, curve delle tre medie (54, 36 e 18 chart), distanza e differenza di range
   dalle 54 chart di MonkerSolver (`run.log`).
2. La fase 3b: parte A (valori con la policy fissa, migliore risposta preflop per posto, chart di MonkerSolver nel nostro gioco),
   V11 e gate 3b; 6,5-9 ore di agente secondo la specifica [I]. Poi la parte A campionata su 64 flop fisici sul run 1 (0,9-2 ore
   sull'i3 secondo la specifica [I]), prima dell'archivio su F:.
3. Il test del blocco: preflop delle chart 3-way di MonkerSolver bloccato a 15 × 4, soglia 0 e 24.000 iterazioni (D3); circa 10
   ore a 1,5 s per iterazione [I].
4. La proposta della batteria di correttezza 3-way chiesta dall'utente la mattina (oracolo esatto contro un CFR indipendente,
   regole e payoff indipendenti, guadagni di deviazione per giocatore): non è nei file letti per questa sezione.
5. Il push del branch (12 commit locali al merge, più gli aggiornamenti dei documenti), quando l'utente lo chiede. La parte B (migliore risposta completa, NashConv) resta la
   fase 3c su un server, fuori dall'ambito ridotto.

## 10. Batteria di correttezza del passo 2 HU50 (dal 30 settembre pomeriggio al 1° ottobre pomeriggio)

Dalle prime prove di calibrazione (15:46 del 30) alla fine della coda notturna (07:52 del 1° ottobre); la chiusura, con la coda
del giorno finita alle 16:28 del 1° ottobre, è in 10.13 (`day1/results.md`, `day1/checks.md`, `day1/queue.log`). Rapporti e script
nella cartella temporanea della sessione, `scratchpad/correctness/`: `design.md` (progetto), `results.md` (run del 30),
`coverage/plan.md` (piano di copertura, con i cinque studi `river_key_turn.md`, `bets_raises.md`, `shared_components.md`,
`seeds_variants.md`, `rake.md` e la critica `critic.md`), `night2/results.md` e `night2/checks.md` (coda notturna),
`night2/t1t3/RESULTS.md` (T1, T3, U1-U3), `night2/t1a2/`, `night2/t1a2b/`, `night2/t1a1/` (rafforzamenti di T1, con rapporto
e review). Run in `out/monker/correctness/` (il rapporto del 30 copiato come `results_2026-09-30.md`); giochi, tabelle, driver
e memoria in `benchmarks/monker/correctness/README.md`; riferimenti indipendenti in `tools/independent/`. Segni: **[V]** =
verificato (misurato, o letto nei file dei run, nei log, nei rapporti o nel codice); **[I]** = inferito (ragionamento, stima o
estrapolazione).

**In breve.**

- **Chiusura del 1° ottobre, 16:28 (10.13): la batteria HU è chiusa con 0 FAIL** [V]. Dopo l'estensione fino a 256.000 B2L e
  B1L passano (0,760 % e 1,519 % del piatto), e passa anche il gioco nuovo HU19_B1L (8,495 % a 64.000, pendenza −0,76), con il
  criterio 3 rivisto con l'utente verso le 10:30, prima dei risultati: discesa tardiva al posto del fit del pavimento. Il terzo
  parere con la DLL dell'utente (S2-DLL) passa in tutte le parti. I punti qui sotto sono lo stato delle 08:00.
- **Nessun errore del motore**: 0 FAIL in tutta la batteria [V].
- Sui giochi senza perdita il percorso di codice di HU50 porta la NashConv fisica esatta verso 0 senza pavimento: V2 (preflop
  libero, river esatto) **0,0014 % del piatto a 32.000**; V2L (preflop bloccato, river esatto) **0,059 % a 448.000**, sotto la
  soglia di livello dello 0,10 %; V1L (flop e turn esatti) **0,553 % a 448.000** con pendenza costante circa −0,5 [V] (10.3).
- **PASS**: T1 (le chiavi del river nel codice contro un oracolo esatto, anche dopo tre rafforzamenti), T3 (riferimenti
  indipendenti S1-S6, S5a e l'arbitro S3), T4 (rake: V2Z, V2R, V2R5, U1-U3), T5 (seed e determinismo), T6, D6, e in T2 B0L e
  B0M [V].
- **INCONCLUSIVE**: B2L e B1L (T2), solo sul criterio 3, il pavimento stimato con un fit a + b T^-p sugli ultimi cinque
  snapshot; trend, componenti, controllo e strumento passano. Estensione in corso oggi [V] (10.5). Poi **PASS** (10.13).
- **Provenienza (D6)**: gli eseguibili che hanno prodotto i risultati HU50 (`bin_rake`) e quelli dei certificati (`c123`)
  danno su HU50 risultati identici bit per bit, quindi i certificati coprono i risultati HU50 [V sui casi provati, I per il
  resto] (10.9).
- **Non certificabile formalmente**: la convergenza dell'astrazione stessa di HU50, un river senza perdita dopo puntate su una
  street precedente, l'equilibrio di Nash con il rake e alcuni altri punti (10.10).

### 10.1 La domanda e il metodo

**La domanda: il solver calcola l'equilibrio del gioco che gli diamo?** È distinta da altre due domande che la batteria non
tocca: se l'astrazione di HU50 (30 × 4, TX2, river per classe del turn) è buona, e se le nostre regole sono quelle di
MonkerSolver (5.1-5.11). Alla domanda dell'utente "Sul river non servono bucket esatti?" la risposta del progetto è: per un
certificato formale sì; per i run di produzione HU50 no, e lì non sono nemmeno realizzabili (con la chiave del river "turn"
servirebbero almeno 14.880 id per classe di turn, sopra il limite di 4.096 del formato dei bucket, e circa 26 GB di stato) [V,
`design.md` sezione 0].

**Il metodo** (progetto `design.md`, finito alle 16:23 del 30):

- **Giochi piccoli senza perdita**: ogni insieme di informazione del gioco astratto è uno del gioco reale (riga = board
  canonico × orbita della mano sotto le permutazioni dei semi che fissano il board: l'astrazione per isomorfismo classica). In
  un gioco a due giocatori a somma zero così, la NashConv fisica della media DCFR deve tendere a 0: un pavimento sopra 0
  vorrebbe dire che il trainer ottimizza un gioco diverso da quello che il valutatore misura, cioè un bug [I, teoria di CFR e
  MCCFR].
- **Lo stesso percorso di codice di HU50**: `gtosd_preflop_blueprint_train --board-class-rows` con una mappa di texture,
  DCFR 1,5 / 0 / 2 alternato, 32 board per giocatore per iterazione, sconto lazy, `--batch-policy-refresh`, tabelle in double.
  Unica differenza `--partition-target 4` invece di 64 (sui 37 nodi dei giochi piccoli 64 darebbe una sola unità di lavoro
  senza nodi top); la disposizione di HU50 è coperta da D5 e D6 (10.8, 10.9) [V].
- **Uno strumento indipendente dai regret del trainer**: la migliore risposta fisica esatta di
  `gtosd_preflop_blueprint_monker_values --all-flops` (573 flop canonici, 605.088 board per snapshot, modalità exact), già
  validata contro l'oracolo FiniteGame (P6) e dal certificatore (P7).
- **Controlli grossolani**: stesso gioco, eseguibili e calendario, un solo id per board sulla street che decide. Devono
  fermarsi su un pavimento: così si vede che la misura riconosce un non-equilibrio. I controlli a 3 livelli (V2LG, V1LG) non
  discriminavano (2,1 e 1,3 volte sopra a 32.000, ancora in calo) e sono stati sostituiti da quelli a un livello (V2LG1,
  V1LG1) [V].
- **Preflop bloccato** (rilievo D1 della review del progetto): nei giochi a 6a quasi ogni mano folda o shova preflop (in
  `HU6_all` a 32.000 il flop arriva nello 0,3 % circa delle mani), quindi un errore postflop sposta pochissimo la NashConv.
  Le chart `lock_limp_check/` bloccano il CO al limp e il BTN al check per tutte le 81 classi (`--lock-charts/--lock-nodes`,
  lo stesso percorso dei test con il preflop bloccato di HU50): ogni mano arriva al flop con 4a nel piatto e i range pieni.
  Questi run si giudicano sulla **NashConv postflop = gain_lower CO + gain_lower BTN** (chi risponde segue il preflop bloccato
  e risponde al meglio dal flop in poi); la NashConv piena resta a 2,000 a (lo shove contro i nodi preflop mai raggiunti, che
  restano a 50/50) e non è un criterio [V].
- **Criteri** (sezione 10 del progetto): G1 livello (NashConv all'ultimo snapshot <= 0,10 % del piatto per V1 e V2), G2
  trend (pendenza log-log fra Tmax/8 e Tmax <= −0,35, nessuna risalita oltre il 10 % dai 2.000), G3 componenti in calo, G4
  controllo almeno 5 volte sopra e piatto, G5 valutatore PASS in modalità exact. PASS = G1-G5; INCONCLUSIVE = G2-G5 senza G1;
  FAIL = plateau, risalita su due raddoppi, curva entro 1,5 volte dal controllo, o G5. Per i giochi con puntate e rilanci (T2)
  cinque criteri: trend, componenti, pavimento (fit a + b T^-p sugli ultimi cinque snapshot con a <= 0,1 della NashConv
  finale, o negativo), controllo, strumento; INCONCLUSIVE quando 1, 4 e 5 tengono e 2 o 3 è al limite, e allora si estende
  (`coverage/bets_raises.md` sezione 6) [V].
- **Codice nuovo**, additivo (uscite e impronte esistenti invariate): C1 la chiave di configurazione
  `postflop_betting_streets` (`591724c`), C2 `--turn-exact` e `--river-exact` nel costruttore dei bucket (`dc34131`), C3 la
  chiave del river "river-board" (`68cf367`); driver `run_correctness.sh` (`b505ad5`) con il blocco del preflop e la guardia
  dell'impronta dell'albero (`6bdb86f`): con gli eseguibili precedenti, che ignorano `postflop_betting_streets`, si è fermato
  al primo segmento invece di allenare l'albero sbagliato [V]. Eseguibili congelati `out/monker/bin_correct/c123` (C++
  `2184d66` + `591724c` + `dc34131` + `68cf367`), driver congelati `out/frozen/run_correctness_lock.sh` e (dalla notte)
  `out/frozen/run_correctness_v2.sh` [V].

### 10.2 Giochi e astrazioni

Tutti HU (CO poi BTN), ante 1a, blind del BTN 1a, piatto iniziale 3a, donk bet ammessi; tabelle in
`out/monker/correctness/buckets` [V, README].

| Gioco | Stack | Puntate postflop | Nodi | Albero | Run senza perdita | Controllo |
|---|---:|---|---:|---|---|---|
| `HU6_V0_flop` | 6a | flop, solo all-in | 25 | `cd66796bdbdac5c1` | V0 (solo smoke): flop esatto (`buckets_flopexact_15x4`), mappa identità | — |
| `HU6_V1_flopturn` | 6a | flop e turn, solo all-in | 31 | `da5c6942354ad5ad` | V1L: flop 528 e turn 496 id esatti, mappa identità | V1LG (turn 3 × 1), V1LG1 (un id per board di turn) |
| `HU6_V2_river` | 6a | solo river, solo all-in | 25 | `2f109f6f1891d9f2` | V2 (preflop libero), V2L: 465 id per board a 5 carte, chiave "river-board" | V2LG (3 livelli), V2LG1 (un livello: river cieco alla mano) |
| `HU19_B0_flop` | 19a | flop: `bet_4`, `raise_16`, all-in | 73 | `b58f4ac0e4cf68b1` | B0L: flop esatto, turn e river solo check | B0LG1 (`g1x1`) |
| `HU19_B0M_flop` | 19a | come B0 più un open al piatto: 3 ingressi postflop | 163 | `194fd41496666162` | B0M: flop esatto, blocco a forma di classi (`lock_b0m/`, 243 righe) | B0MG1 |
| `HU19_B2_river` | 19a | river, stesse forme | 49 | `23b83f3f3b18d1c0` | B2L: river esatto, chiave "river-board" | B2LG1 (`v2g_river1`) |
| `HU8_B1_flopturn` | 8a | flop e turn (turn dopo bet-call al flop, donk all-in) | 85 | `5269db409b4bcf45` | B1L: flop e turn esatti | B1LG1 (turn un id per board) |
| `HU6_V2_river_rake25cap2` | 6a | come V2, rake 2,5 % / cap 2a, no flop no drop | 25 | `cd2e1217488aaea9` | V2R | — |
| `HU6_V2_river_rakeinert` | 6a | come V2R con piatto minimo 13a (nessuna mano pagata) | 25 | `eb528dbdd5dbe94c` | V2Z: deve uguagliare V2 bit per bit | — |
| `HU6_V2_river_rake5cap05` | 6a | rake 5 %, cap 0,5a (morde sui piatti di 12a) | 25 | `0133288de510b8f0` | V2R5 | — |

- **Perché nei giochi a 6a il river è senza perdita solo in V2.** Con la chiave del river di HU50 ("turn") un river senza
  perdita dopo puntate al flop o al turn non si può esprimere: 6.336 collisioni di orbite di river, almeno 14.880 id per classe
  di turn contro il limite di 4.096, 205-294 milioni di righe per nodo di river [V, `coverage/river_key_turn.md`]. Senza
  decisioni fra flop e river l'ordine delle cinque carte non conta, quindi la riga del river è (linea preflop, mano, board a 5
  carte non ordinato) a meno di simmetria: 19.998 board canonici × al massimo 465 id, 1,2 GB [V per i conteggi, I per
  l'argomento].
- **I giochi B** riproducono le forme postflop di HU50 nei piatti limpati (`check;bet_4;all_in`,
  `fold;call;raise_16;all_in`, `check;bet_12;all_in` dopo un bet-call), con `maximum_raise_count` 1 come HU50 (0 in
  `HU8_B1`); il preflop è lo stesso albero di 4 decisioni dei giochi a 6a, quindi il blocco limp/check vale invariato. `bet_4`
  = 100 % di 4a e `raise_16` = 4 + (4 + 4 + 4) controllati a mano contro la regola del piatto di MonkerSolver [V, README e
  `bets_raises.md`].
- **B0M**: il CO apre 26 classi (4 metà open e metà limp), limpa circa il 40 % delle classi e folda il resto, ordinate per il
  valore dello shove in V2; il BTN checka dietro ogni limp e chiama ogni open. Due ingressi postflop con range del CO non
  uniformi e il range pieno del BTN [V, README]: le due caratteristiche di HU50 che nessun gioco senza perdita copriva.

### 10.3 V0, V1, V2, V1L, V2L e l'estensione fino a 448.000

**Calibrazione** (copia degli eseguibili `bin_probe`, albero di prova `573abdc7da5c939c` a tutte le street, lo stesso gioco di
`HU6_all` con un altro id, 15:46-16:21): probe A (flop esatto
più turn 15 × 4 e river 15, mappa identità) 0,027 % del piatto a 32.000; probe B (15 × 4) 0,046 % a 16.000; controllo C (3 × 1)
fermo allo 0,53 % a 16.000 [V]. Correzione in 10.11: A e B usano la mappa identità, non TX2, e su questo gioco il postflop pesa
poco.

**V0 e V1 senza blocco** hanno girato solo come smoke (100 iterazioni, 16:56-17:02: V0 0,0411 a = 1,371 % del piatto, V1 0,0878
a = 2,927 %, valutatore PASS exact) e poi sono stati tolti dal piano: senza blocco il flop si raggiunge nello 0,003 % circa
delle mani di V2 [V, critica], quindi nessuna potenza. Li sostituisce B0M (10.5).

**I run del 30** (17:45-19:47, 2 thread di training e 2 di valutazione ciascuno, macchina condivisa con le due prove HU20_deep)
e **l'estensione della notte** (00:00:38-06:42:31 del 1° ottobre, dalla decisione dell'utente verso le 20:00 "Mettili in coda";
stessi eseguibili, driver, tabelle, mappe, blocco e thread). NashConv postflop per i run bloccati, NashConv piena per V2; in
percentuale del piatto iniziale di 3a [V, `convergence.txt` dei run]:

| Run | 4.000 | 16.000 | 32.000 | 64.000 | 448.000 | Pendenze log-log | Controllo |
|---|---:|---:|---:|---:|---:|---|---|
| V2 (preflop libero, river esatto) | 0,001716 a (0,0572 %) | 0,000102 a (0,0034 %) | **0,0000424 a (0,0014 %)** | — | — | −1,78 fra 4.000 e 32.000 | affidato ai run bloccati (senza blocco un controllo non discrimina) |
| V2L (preflop bloccato, river esatto) | 0,594 a (19,8 %) | 0,186 a (6,21 %) | 0,0874 a (2,91 %) | 0,0349 a (1,16 %) | **0,00176 a (0,059 %)** | da −0,73 a −1,41 fino a 64.000; poi −1,55, −1,62, −1,63, −1,57, −1,47, −1,39, −1,29 | V2LG1 fermo a 1,465 a: 7,9 volte sopra a 16.000 [V], circa 42 volte a 64.000 [I] |
| V1L (preflop bloccato, flop e turn esatti) | 0,198 a (6,59 %) | 0,0921 a (3,07 %) | 0,0633 a (2,11 %) | 0,0438 a (1,46 %) | **0,01658 a (0,553 %)** | da −0,53 a −0,56 fra 4.000 e 64.000; poi da −0,48 a −0,52 | V1LG1 fermo a 0,609 a: 6,6 volte sopra a 16.000 [V], circa 14 volte a 64.000 [I] |

- **Verdetti del 30 sera** [V]: V2 PASS (G1, G2, G3, G5; G4 affidato ai controlli bloccati). V2L e V1L passano G2-G5 e sono
  INCONCLUSIVE sulla lettera (G1 non raggiunto: 1,16 % e 1,46 % a 64.000), senza condizioni di FAIL; la review del progetto
  aveva raccomandato di giudicare i run bloccati su pendenza e controllo, e su quel criterio passano. Fit a + b T^-p sugli
  ultimi cinque snapshot fino a 64.000: a = −0,0208 a (V2L) e −0,0014 a (V1L), compatibili con 0.
- **Estensione fino a 448.000** (tutti gli snapshot PASS exact) [V]:
  - **V2L** scende a 0,00176 a = **0,059 % del piatto, sotto lo 0,10 % di G1**; la soglia (0,003 a) è passata fra 256.000
    (0,128 %) e 320.000 (0,092 %). La stima del 30 era circa 420.000 iterazioni a pendenza −1,3 [I allora]: la pendenza è
    arrivata a −1,63 e poi si è addolcita fino a −1,29.
  - **V1L** scende a 0,01658 a = 0,553 %, con pendenza costante circa −0,5 (−0,48 / −0,49 negli ultimi tre intervalli): il
    ritmo Monte Carlo, perché i valori delle righe di flop e turn sono campionati sulle carte che restano [I]. Allo stesso
    ritmo lo 0,10 % chiederebbe circa 14 milioni di iterazioni [I: (0,01658 / 0,003)^2 × 448.000]. Nessun segno di pavimento.
  - **Fit del pavimento sull'estensione** (ultimi cinque snapshot, 192.000-448.000; calcolato per questa nota con la stessa
    procedura di `summarize_night2.py` sui valori non arrotondati di `convergence.json`) [V]: V1L a = −0,0012 a, compatibile
    con 0; **V2L a = +0,00053 a** (il 30 % della NashConv a 448.000, p = 1,77). È lo stesso tipo di esito che ha reso B2L e
    B1L INCONCLUSIVE (10.5): un fit con una sola potenza legge come pavimento una pendenza che si addolcisce. Lettura [I]:
    quando la parte deterministica dell'errore scende, pesa di più il rumore del campionamento dei board, che cala più
    lentamente (come V1L, a −0,5), e la pendenza passa da −1,6 verso valori più piatti senza che esista un pavimento; il
    controllo V2LG1 resta circa 830 volte sopra (1,465 a, misurato fino a 16.000).
- **Il ribasamento delle epoche dello sconto lazy** (quando l'obiettivo supera la base di 65.535 iterazioni:
  `trainer.cpp:1332-1335`; nell'estensione a 65.535, 131.070, 196.605, 262.140, 327.675 e 393.210) gira qui per la prima volta
  in un run; prima solo nel test unitario con epoca 4. Il run HU50 di riferimento (5.11) non l'ha mai attraversato
  (0 -> 32.117 -> 64.000). Nessun gradino: V1L −0,53 fra 48.000 e 64.000 contro −0,51 fra 64.000 e 96.000, V2L −1,41 contro
  −1,55; le pendenze restano regolari su tutti i ribasamenti [V, `night2/results.md`].

### 10.4 T1: le chiavi del river nel codice, contro un oracolo esatto, e tre rafforzamenti

La chiave "turn" di HU50 non può essere senza perdita in un run (10.2), quindi T1 la copre a livello di codice: trainer e
valutatore con righe per classe di board (chiavi "turn" e "river-board"; mappe identità, turn-as-flop, TX2 e
identity_river_board) confrontati con un FiniteGame sul gioco fisico senza perdita, risolto con LinearCfr, su HU10 ridotto e su
CO40-test. Test `gtosd_preflop_blueprint_board_texture_tests` (`tests/preflop_blueprint_board_texture_tests.cpp`), commit
`e459c17`, `1c5932d`, `45d1485`, `b738e71` e `9d0072e` (30/09, 21:06-21:54). Cinque asserzioni:

- **A1**: aritmetica del trainer (regret e somme della strategia) = FiniteGame LinearCfr entro 1e-9;
- **A2**: `estimate_exploitability` (motore del river congiunto sulle righe per classe) = `calculate_nash_conv` sul gioco
  senza perdita entro 1e-9;
- **A3**: struttura del raggruppamento (la chiave "turn" mette insieme i river di un turn e mai turn o flop diversi;
  turn-as-flop mette insieme dei turn, TX2 dei flop; "river-board" mette insieme i due ordini di un board a 5 carte e mai due
  board);
- **A4**: l'aggiornamento alternato campionato sulle righe raggruppate è non distorto;
- **A5**: `gain_lower`, il criterio di tutti i run bloccati, = migliore risposta per forza bruta dal flop in poi sotto un
  preflop bloccato (prima era controllato solo per segno, limite e accordo con una passata che riusa gli stessi valori).

**Prima esecuzione** (notte, 00:02-00:32) [V, `t1t3/RESULTS.md`]: tutto PASS; errori massimi 2,3e-12 (A1) e 4,4e-16 (A4); A5
[0,617306; 0,441533] = oracolo, e la chiave sposta il valore di circa 1e-5, molto sopra la tolleranza; `assertions=50079891`,
36,2 s. Limite O1: con le mani della prova (Ts Th Js Jh contro Qs Qh Ks Kh) ogni mano ha lo stesso esito di showdown su tutti
i 9 board, quindi l'A2 probabilmente non avrebbe visto un indice di classe sbagliato [I].

**Rafforzamento 1** (`bf0f63e`, 02:55-03:20; review 03:20-03:50) [V, `t1a2/`]:

- 7 board "decisivi" (flop 9s8s6d; turn Ad e 6c) su cui il river cambia il vincitore: 36 coppie di mani su 36 cambiano esito
  sui 4 river di Ad, 26 su 36 sui 2 di 6c (scala sopra tris, A-6-7-8-9 come scala più bassa, colore sopra scala). Il reviewer ha riprodotto
  tutte le 252 celle con un valutatore suo.
- **Scoperta: l'A2 senza blocco era cieco sulle righe del river.** 0 insiemi di informazione del river su 14.336 avevano una
  media non uniforme (sui nove board vecchi 0 su 1.728), perché il preflop allenato non porta al river (la migliore risposta del
  giocatore 0 è il fold preflop): un valutatore con la chiave sbagliata che leggesse righe uniformi sarebbe passato.
  Correzione: le asserzioni di potenza girano con un preflop bloccato misto (`mixed_preflop_lock`, ogni azione preflop con
  frequenza positiva): 481-558 insiemi del river su 1.344 non uniformi su HU10, 966-1.327 su 1.792 su CO40-test.
- Potenza: le due chiavi differiscono in NashConv di 0,0320 / 0,2188 (6 / 1 gruppi di river) e in EV di almeno 1,50e-4; tre
  letture sbagliate (M1 una tabella "river-board" letta con le righe della chiave "turn", come farebbe il ramo "turn" del
  motore del river; M2 i river di un turn messi insieme; M2' i due ordini di un board a 5 carte messi insieme) spostano la
  NashConv della forza bruta di almeno 0,0181, mille volte la tolleranza.
- Mutazioni del reviewer sul lato oracolo dell'A2 (in una copia del test, mai committata): con il blocco l'A2 fallisce in
  ognuna delle 5 modalità (4 varianti su 4), senza blocco passa sempre (cieco). Limite trovato: con questi range la migliore
  risposta del giocatore 1 non dipende dalle righe postflop del giocatore 0 (BR[1] = 2,225498844 in tutte le varianti),
  quindi un errore confinato alle righe del giocatore 0 si vedeva solo nell'EV.

**Rafforzamento 2** (`c50ff3c`, 03:45-03:53; review 04:00-04:45) [V, `t1a2b/`]:

- Potenza per giocatore: 24 letture sbagliate confinate alle righe di un giocatore (2 orientamenti × 2 numeri di gruppi × 3
  mutazioni × 2 giocatori); ognuna sposta l'EV del giocatore o la migliore risposta dell'avversario di almeno 1,149e-4, e le
  righe del giocatore che conta per la NashConv (il giocatore 1 nell'orientamento originale, il giocatore 0 in quello
  specchiato) la spostano di almeno 0,0181 e 0,0150.
- Varianti specchiate (mani scambiate: BR[0] = 1,966232108 costante, quindi ora sono le righe del giocatore 0 a muovere la
  NashConv) e non vacuità dell'A1 sul river (celle del river con regret sopra 1e-6: 6.600 su 6.604 sui nove board, 6.022 su
  6.022 sui board decisivi).
- Review: le 24 letture confinate fanno fallire l'A2 tutte e 24. Un errore che legge le righe del giocatore 0 con la chiave
  sbagliata solo nel passaggio della migliore risposta del giocatore 1 prima passava l'A2 in tutte le varianti; ora fallisce in
  tutte le 4 varianti HU10 specchiate per M1, M2 e M2'. 90 righe confrontate con un'implementazione indipendente, 0
  differenze.
- **Correzione del reviewer**: nelle 21 varianti senza blocco nessuna cella del river cambia dopo la prima iterazione (dalla
  seconda il river non riceve reach), quindi l'A1 senza blocco controllava solo l'iterazione 1, a giocata uniforme. La frase
  [I] del rapporto ("i regret possono ancora cambiare attraverso il reach dell'avversario") è smentita dalla misura.

**Rafforzamento 3** (`5bc2a9c`, 04:45-05:20; review 05:00-05:30) [V, `t1a1/`]: l'A1 sotto il blocco del preflop.

- Derivazione dal codice (`trainer.cpp`, `solver.cpp`, supporto dei test), confermata dal reviewer: il trainer tiene una
  frequenza bloccata nel reach dell'attore (somme della strategia) e in quello dell'avversario (regret), il FiniteGame nel
  reach del caso di entrambi. Con una sola classe preflop per giocatore (TsJs, ThJh contro QdKd, QcKc) vale esattamente:
  regret del trainer = regret dell'oracolo / f_attore, somma del trainer = somma dell'oracolo / f_avversario, medie uguali.
- 8 varianti, 6.140 celle, errori massimi 9,5e-11 / 2,8e-12 / 1,3e-12 su valori fino a 846,6; senza i fattori la relazione
  sbaglia di almeno 59,2, con i fattori scambiati di almeno 290,5.
- Sulle righe del river raggruppate cambiano dopo l'iterazione 1 il regret di 670 celle e la somma di 603 su 1.510, e 363 medie
  su 688 non sono uniformi; il reviewer ha contato 4.366 coppie (riga, iterazione) raggiunte dopo la prima, tutte con strategia
  corrente non uniforme, 333 righe raggiunte da due o più board canonici nella stessa iterazione.
- Mutazioni del reviewer: una cella di regret spostata di 1e-8 relativo; le celle raggruppate ferme ai valori
  dell'iterazione 1; l'oracolo con VanillaCfr (peso 1 invece di t); l'oracolo con il blocco spostato di 1,25e-4: l'A1 fallisce
  in 8 varianti su 8 per ciascuna. Gli scambi di righe falliscono dove cambiano il gioco (sono vacui con un solo gruppo di
  river). La stessa mutazione "celle ferme" senza blocco passa: la cecità dell'A1 vecchio, ora chiusa.

Il test passa da `assertions=50079891` a 100.785.546, 100.988.843 e 134.975.163, da 36 a circa 100-109 s; nessun cambiamento
di `libs/`, `include/` o del supporto dei test (l'ultimo commit che tocca `libs/` o `include/` è `68cf367`), e dopo ogni giro
l'output precedente è identico riga per riga [V]. Non asserita: la frase secondo cui con più classi preflop in una riga non
vale nessuna relazione (solo un commento) [I].

### 10.5 T2: puntate e rilanci sotto l'all-in (coda notturna)

[V, `night2/results.md`; NashConv postflop a 64.000, in percentuale del piatto iniziale di 3a]

| Run | Orario | NashConv a 64.000 | Pendenza fra 8.000 e 64.000 | Fit del pavimento: a contro 0,1 × NC(64.000) | Controllo a 16.000 (rapporto; a 64.000 [I]) | Verdetto |
|---|---|---:|---:|---|---|---|
| B0L | 02:38-04:06 | 0,07093 a (2,364 %) | −0,520 | 0,002319 contro 0,007093 | B0LG1 3,596 a (24,9 volte; 50,7) | **PASS** |
| B0M | 06:09-07:52 | 0,05900 a (1,967 %) | −0,506 | −0,009184 | B0MG1 1,906 a (16,1 volte; 32,3) | **PASS** |
| B2L | 00:16-02:52 | 0,13120 a (4,373 %) | −1,247 | **0,014306 contro 0,013120** | B2LG1 8,114 a (11,6 volte; 61,8) | INCONCLUSIVE |
| B1L | 02:52-04:49 | 0,09389 a (3,130 %) | −0,657 | **0,025975 contro 0,009389** | B1LG1 1,333 a (5,9 volte; 14,2) | INCONCLUSIVE |

- In tutti e quattro: ogni snapshot PASS exact; smoke del primo segmento PASS (capacità, byte di stato, texture, righe
  bloccate 162 o 243, picco di memoria); etichette dell'albero presenti (`check;bet_4;all_in` e `fold;call;raise_16;all_in`;
  in B1L, che non ha rilanci, solo la prima);
  in B2L a 4.000 il motore del river di riferimento dà valori identici bit per bit a quello congiunto (NashConv
  3,848271659043531 a). Passano il criterio 1 (trend: nessuna pendenza locale sopra −0,2 dai 4.000, nessuna risalita),
  il 2 (gain_lower di CO e BTN in calo in ogni raddoppio da 8.000 a 64.000), il 4 (controlli piatti e almeno 5 volte sopra) e
  il 5 (strumento). Nessuna condizione di FAIL.
- **B2L e B1L mancano solo il criterio 3.** Il fit con una sola potenza sugli ultimi cinque snapshot (24.000-64.000) dà un
  pavimento positivo perché la pendenza si addolcisce: B2L da −1,32 (8.000-16.000) a −1,15 / −1,18, B1L da −0,70 a −0,58.
  Secondo la regola dello studio l'esito è INCONCLUSIVE e si estende, come per V1L e V2L; l'estensione gira oggi. Lo stesso
  fit sull'estensione di V2L dà un a positivo (10.3).
- Livelli (riportati, non sono criteri): fra l'1,5 e il 3,3 % del piatto del flop (4a) a 64.000.

### 10.6 T3: riferimenti indipendenti (notte, 00:02-01:17)

Pacchetto `tools/independent/sdref` e script `sd_*.py`, scritti a partire dalle regole del gioco (S3 solo in parte: vedi i
limiti sotto), commit dalle 21:10 alle 21:55 del 30 (README `c1db800`); i C++ di supporto sono il dump dell'albero
`--dump-nodes` (`fdf8114`) e le CTest dell'arbitro (`65598b3`, `c2bddc5`) [V, `t1t3/RESULTS.md`]:

| Controllo | Esito | Cosa prova |
|---|---|---|
| S1 classifica | 10/10 PASS, 188,9 s | La tabella dei ranghi short deck del motore = un valutatore indipendente su tutti gli 8.347.680 insiemi di 7 carte e su tutti quelli di 5 (ordinali e ordine debole), 0 differenze |
| S2 tabella degli all-in HU | 8/8 PASS, 522,8 s | Vittorie, pareggi e sconfitte esatti per tutte le 176.715 coppie di combo disgiunte, 0 differenze su 376.992 board |
| S3 arbitro | PASS, uscita 0 | Un'implementazione indipendente delle regole riproduce albero, azioni legali, piatti, rimborsi e ogni riga di payoff su 24 configurazioni CTest (la famiglia HU6 e i 3 gemelli con rake, compreso V2R5 dove il cap morde; B0, B0M, B2, B1; la famiglia HU50_step2 e 2size; CO40-test; HU50 checkdown; preflop 3-way) e su HU20_deep: `rule_failures=0 abstraction_failures=0 convention_mismatches=0`. Autotest PASS (payoff+1 e call+1 presi). Con `--expect-tree` il build della notte produce l'albero di produzione HU50 `dde527d0de7e7ae9` (571 nodi) |
| S4 valore del passo 1 (programmazione lineare) | HU50 PASS; HU6_all VALUE_ONLY | HU50: v* = −0,121044566662 dentro la finestra certificata del motore [−0,1210452592; −0,1210436490], e la finestra esclude tre giochi mutati vicini. HU6_all: c'è solo il valore di riferimento, nessun riepilogo del motore da confrontare (INCONCLUSIVE per costruzione) |
| S5a identità EV del passo 2 | 16/16 PASS, 1.354 s | Per 8 politiche di progetto su HU6_all e sul suo gemello con rake il valutatore c123 dà esattamente i valori in forma chiusa (EV; valori e pesi per radice, combo e classe; reach dell'avversario; migliore risposta preflop; guadagno; somma zero; rake atteso), differenza massima 3,4e-14 |
| S6 catalogo e tabelle | 142/142 e 48/48 PASS | Conteggi di Burnside = enumerazione su 7.539.840 storie; ogni tabella rispetta la simmetria dei semi e quelle esatte sono davvero senza perdita (orbite = id), compresa la tabella esatta del flop di V0 |

Con loro, nella stessa notte: U1-U3 (10.7) e l'intera etichetta CTest `preflop_blueprint`, 88 test su 88 (806 s). Limiti
[V]: S3 è indipendente solo in parte (le convenzioni C1-C16 rispecchiano il motore per progetto, e nessuna configurazione
esercita in modo che possa fallire l'arrotondamento C2, il confine del cap dell'all-in C8 o la fiche dispari C11); per
default S3 asserisce le impronte registrate solo per i 5 giochi HU6 (HU50 una volta, a mano); S5a non è girato su HU50 (il
riferimento sta su F:, fuori dai limiti della notte); il build della notte e gli eseguibili c123 non sono confrontati in
binario [I che coincidano: stesse impronte degli alberi].

### 10.7 T4: il rake

- **U1** (`0bd2a5e`): i payoff dei due giochi HU6 con rake 2,5 % / cap 2a = una tabella derivata a mano (18 terminali di
  `HU6_all`, 10 di `HU6_V2_river`): i fold preflop non pagano, showdown e fold postflop pagano 0,1a / 0,3a, pareggi divisi,
  eccesso non chiamato restituito, albero invariato. Qui il cap non morde [V].
- **U2**: un piatto minimo sopra ogni piatto dà i payoff senza rake (quindi il minimo non è ignorato) [V].
- **U3** (`b8e1df0`): l'aggiornamento alternato con il rake = la sua attesa condizionata, errore massimo 4,4e-16 e 8,9e-16
  (161 e 401 showdown pagati, di cui 121 e 361 al cap): il giocatore 1 usa il proprio payoff con il rake, senza la scorciatoia
  della somma zero [V].
- **V2Z** (00:19-00:31): policy identiche a quelle di V2 bit per bit a 250, 500 e 1.000 iterazioni (`8f8d9a7ad1b9b35b`,
  `78d88ff2691e3d28`, `a02df4573c30e57a`), stime identiche, rake atteso circa 1e-16: **PASS** [V].
- **V2R** (00:31-02:26, senza blocco, NashConv piena): 0,0000567 a a 32.000 (sotto lo 0,0003 a = 0,01 %) e **0,000028 a =
  0,00092 % del piatto a 64.000**; pendenza fra 4.000 e 32.000 −2,015; mai sopra 3 volte V2 (a 8.000, 16.000 e 32.000).
  Controllo di potenza R3: la policy di V2 a 32.000 giocata nel gioco con il rake lascia 0,002850 a (prevista circa 0,0028 a
  [I]), 100 volte il livello di V2R; al contrario la policy di V2R nel gioco senza rake lascia 0,002232 a: il run converge
  all'equilibrio del gioco con il rake, non a quello senza. Contabilità: il rake atteso per eroe = −(EV0 + EV1) =
  0,24763207 a entro 1e-9. Valori: le 4 mani che la previsione al primo ordine toglie dallo shove (J6s, T6s, 96s, Q6o) shovano
  allo 0,0000, gli altri shover di V2 almeno allo 0,9955; EV entro 2e-5 dalla previsione. **PASS**, condizionato a U1-U3, che
  passano [V].
- **V2R5** (06:52-07:17; rake 5 %, cap 0,5a, che morde sui piatti di 12a): 0,0000566 a a 32.000, pendenza −2,116, rake atteso
  0,39514 a fra 0 e il cap: **PASS** [V].
- Un gioco con il rake è a somma non zero: CFR garantisce solo equilibri correlati grossolani, quindi un piccolo pavimento
  sarebbe ammesso anche con il codice giusto [I]; V2R e V2R5 non ne mostrano fino a 64.000 e 32.000 iterazioni.

### 10.8 T5 e T6: seed, determinismo, mappa TX2

- **Secondo seed** (opzione `SEED` del driver, `6f4a296`), V2L_s2 (04:49-05:56) e V1L_s2 (04:06-05:19) fino a 64.000 [V]: il
  rapporto fra le NashConv dei due seed sta fra 0,99 e 1,01 a ogni snapshot da 4.000 a 64.000; a 64.000 0,03488 contro 0,03499
  a (V2L) e 0,04375 contro 0,04376 a (V1L), cioè entro 1,1e-4 a; pendenze fra 8.000 e 64.000 −1,112 contro −1,114 e −0,538
  contro −0,540; a 16.000 il seed 2 è almeno 5 volte sotto il controllo del seed 1; l'identità del trainer e lo stato a 250
  differiscono dal seed 1 (il seed cambia davvero il campionamento). **PASS**.
- **Determinismo** (00:00-00:16) [V, `night2/checks.md`]: D1 V1 bloccato con 4 thread e partizione 1, D2 V2 bloccato con 1
  thread e partizione 64, D3 V2 libero con 2 thread e partizione 64: stato a ogni salvataggio e valori identici bit per bit ai
  run registrati; D4 valutatore con 2 thread contro 8: identico; D5 il gioco HU20 con la disposizione di HU50 (32 unità, 18
  nodi top), partizione 64 con 4 thread contro partizione 4 con 2 thread (192 unità): impronte di identità, stato e policy e 8
  chart identiche; lo stesso su HU50 (10.9). Il determinismo prova la riproducibilità, non la correttezza.
- **T6 P2-V0**: V0 con la mappa TX2 contro la mappa identità a 50 e 100 iterazioni: NashConv identica bit per bit e chart
  identiche byte per byte (4 file) [V]. È solo uno smoke: in V0 la sezione del flop di TX2 è l'identità e le classi di turn
  fuse raggiungono solo nodi con un'azione (rilievo D-c della critica); il controllo di TX2 con potenza è D6.

### 10.9 D6: la provenienza dei risultati HU50

La critica (20:35-21:00 del 30) ha trovato una lacuna nuova: i risultati HU50 (per esempio `HU50_m30x4_rake25`, 5.11) vengono
da `out/monker/bin_rake` (costruito il 30 alle 01:36, C++ circa `3ec4027` [I, dalle date e dalle stringhe mancanti]), i
certificati da `c123`, e fra i due è cambiato proprio il codice delle righe che dipende dalla chiave (uguale per ispezione per
la chiave "turn", nessun run lo confrontava). D6 (00:08-00:14 del 1° ottobre) [V, `night2/checks.md`]:

- `HU50_step2_donk_rake25cap2` (30 × 4, TX2, partizione 64, 4 thread), 200 iterazioni con `bin_rake` e con `c123`: identità del
  trainer `1f515aa183549007` (uguale a quella registrata del run HU50), stato `fa83d13ba4c7bf5d` e policy `556b52343b075485`
  uguali, 8 chart identiche byte per byte; anche `c123` con partizione 4 e 2 thread (192 unità) dà le stesse impronte.
- I due valutatori sulla policy di `bin_rake`, 20 flop, `--expected-rake`: NashConv 13,250337907950401 a, identica bit per bit.
- Quindi **`bin_rake` = `c123` bit per bit sul percorso HU50, e i certificati valgono per i risultati HU50** [V sulle 200
  iterazioni e sui 20 flop provati; I per i run interi: codice deterministico, stesse sorgenti salvo la selezione della chiave, che
  per "turn" è uguale per ispezione].

### 10.10 Cosa non si può certificare formalmente, e perché

Dalla sezione 6 del piano di copertura (`coverage/plan.md`), aggiornata con i risultati:

1. **La convergenza dell'astrazione stessa di HU50** (bucket 30 × 4, classi di turn fuse di TX2, chiave del river "turn" che
   mette insieme i river e dimentica il bucket del turn): è con perdita per costruzione e a memoria imperfetta, nessun teorema
   di convergenza di CFR si applica e il suo pavimento mescola l'errore di astrazione con qualunque altra cosa. Restano solo
   misure empiriche (prove A, B e C, prove profonde, valutazioni esatte di HU50). T1 prova soltanto che il codice calcola
   esattamente quel gioco astratto.
2. **Un river senza perdita dopo puntate su una street precedente**, cioè proprio la situazione "punta al flop o al turn, poi
   river" di HU50: con la chiave "turn" servono almeno 14.880 id per classe di turn (limite 4.096), cioè 205-294 milioni di
   righe per nodo di river, 14,7-40 GB di stato e circa 18-21 ore per run: codice nuovo e fuori dalla macchina. È coperto a
   pezzi: decisioni di river senza perdita (V2L, B2L), turn dopo puntate al flop senza perdita (B1L), raggruppamento della
   chiave "turn" a livello di codice (T1); la combinazione solo con perdita (HU20_deep, prova debole: 10.11).
3. **L'equilibrio di Nash con il rake**: il gioco è a somma non zero e CFR garantisce solo equilibri correlati grossolani; un
   piccolo pavimento positivo è possibile anche con il codice giusto (del secondo ordine nel rake [I]). V2R lo misura e non ne
   vede; niente può provare che sia zero.
4. **Rake postflop e cap che morde in un run convergente**: nessun gioco senza perdita ha potenza (senza blocco il flop arriva
   nello 0,003 % delle mani; nei giochi bloccati l'effetto del rake è 0,001-0,003 a contro gli 0,035 a di V2L a 64.000 [I]); se
   ne certifica solo l'aritmetica (S3, che include HU50 con il suo cap, e U1). In V2R5 il cap morde solo preflop.
5. **Quello che sta sotto la risoluzione dei run**: un errore il cui effetto ai nodi postflop bloccati è più piccolo della
   NashConv finale non si vede. L'estensione ha abbassato la soglia allo 0,059 % (V2L) e allo 0,553 % (V1L) del piatto; i
   giochi B sono fra il 2,0 e il 4,4 % a 64.000. Un secondo seed non cambia questo limite. Aggiornamento del 1° ottobre
   (10.13): a 256.000 B2L è allo 0,760 % e B1L all'1,519 %; HU19_B1L è all'8,495 % a 64.000.
6. **Errori comuni ai riferimenti indipendenti**: il Python, scritto dalle regole, potrebbe condividere un malinteso delle
   regole con il C++. Mitigazione: la DLL dell'utente come terzo parere (S2-DLL, decisa per oggi). È un indizio, non una prova.
   S2-DLL è girato il 1° ottobre alle 09:19-09:23: tutto PASS (10.13).
7. **Il determinismo non è la correttezza**: D1-D6 provano riproducibilità e uguaglianza dei build, nient'altro.
8. **Se le nostre regole e convenzioni sono quelle di MonkerSolver** (dettagli del rake, unità dispari, size postflop,
   astrazione di MonkerSolver): non è una domanda di correttezza del solver e la NashConv non la vede. La scala sopra il tris
   era già confermata il 30 (5.9).
9. **La profondità e la parametrizzazione preflop di HU50 in un run senza perdita** (open fisso a 5a,
   `allow_configured_incomplete_raise`, 48a dietro): coperte solo a livello di albero e di payoff (albero uguale a quello di
   MonkerSolver, S3). È accettabile perché le righe preflop per classe sono senza perdita per simmetria dei semi e V2 certifica
   il percorso del preflop [I].
10. **Fuori ambito**: il multiway, dove l'esattezza non è richiesta, e gli strumenti di confronto delle chart
    (`compare_charts.py`, `monker_in_our_game.py`), che la NashConv non vede.

### 10.11 Correzioni fatte lungo la strada

1. **La prova A usava la mappa identità e aveva poca potenza sul postflop** [V, critica]. Il rapporto del 30 sera diceva che
   la chiave "turn" di HU50 era misurata empiricamente dalle prove A e B (0,027 % del piatto a 32.000). Ma le tre prove di
   calibrazione usano la mappa identità, non TX2 (evento di partenza: `"name":"identity"`); A è flop esatto più turn 15 × 4 e
   river 15, non l'astrazione di HU50; e in `HU6_all` il flop arriva nello 0,3 % circa delle mani, quindi un errore postflop
   o di river sposta pochissimo la NashConv. La chiave "turn" è ora coperta a livello di codice da T1.
2. **La prova profonda dice poco sulle puntate postflop** [V, `deep/*/curve.txt`]. HU20_deep (20a, 571 nodi, 228 decisioni,
   15 × 4 + TX2, partizione 64, `bin_probe`; dalle 16:30 circa alle 20:34 del 30) arriva all'1,692 % del piatto a 32.000
   contro il 3,441 % del controllo 3 × 1, ma la differenza sta nel guadagno congiunto preflop e postflop del CO: il
   gain_lower, solo postflop, è appena 1,35 volte circa sotto il controllo (0,00219 / 0,00227 contro 0,00288 / 0,00302 a a 16.000; 0,00124 / 0,00121 contro
   0,00172 / 0,00169 a a 32.000). Le puntate e i rilanci postflop erano quindi in pratica scoperti: B0, B1 e B2 sono stati il
   primo vero test.
3. **S7 (scala contro tris in MonkerSolver) era già risolto.** Lo studio sui componenti condivisi lo proponeva come domanda
   aperta e "senza risposta possibile dalla NashConv"; la critica ha ricordato che il 30 settembre gli EV del set 3-way a 60a
   lo avevano già confermato (tris sopra scala dà un RMS di almeno 1,84 a, 5.9). S7 è stato tolto.
4. **"Le regole sono già coperte" era vero solo in parte** [V, `coverage/shared_components.md`]. Prima della batteria c'erano
   controlli esterni per la classifica (V9: la DLL dell'utente su 300 terne di combo 3-way entro 1e-12, 9.2) e per l'albero
   preflop (insiemi di nodi di MonkerSolver per HU50 e 3WAY50, 0 differenze). Ma la tabella dei ranghi era controllata solo
   contro lo stesso valutatore (P2), mai in modo esaustivo e indipendente; la tabella degli all-in solo per via transitiva;
   importi postflop, righe di payoff e cap del rake per niente in modo indipendente. Ora li coprono S1, S2, S3 e U1-U3.
5. **Due cecità di T1** (10.4): l'A2 senza blocco leggeva solo righe del river uniformi, e l'A1 senza blocco vedeva solo
   l'iterazione 1. Entrambe trovate dagli implementatori o dai reviewer e chiuse con il blocco del preflop; il meccanismo [I]
   del secondo rapporto è stato smentito dalla misura del reviewer.
6. **Altri rilievi della critica**: P2-V0 è quasi senza potenza (resta uno smoke); "il gemello P1 non è mai girato" era
   falso (è la prova B); V2LT (id esatti sotto la chiave "turn") metterebbe insieme board senza relazione ed è stato tolto;
   il guadagno `gain_lower` non era controllato contro la forza bruta (ora A5); la lacuna di provenienza (ora D6).

### 10.12 Stato alle 08:00 del 1° ottobre e decisioni dell'utente

| Quando | Decisione dell'utente |
|---|---|
| 30/09, verso le 20:00 | Estendere V1L e V2L oltre 64.000 ("Mettili in coda"): coda dalle 00:00, fino a 448.000 |
| 30/09, verso le 20:50 | Mettere tutto in coda per la notte dalle 00:00: T2, la parte di T4 con i run, T5 e T6 |
| 30/09 sera | Scrivere T1 e T3 (test in C++ e riferimenti in Python), da eseguire nella notte |
| 1/10 notte | Rafforzare T1: tre giri fra le 02:55 e le 05:30 (10.4) |
| 1/10, verso le 08:00 | Estendere B2L e B1L (oggi) |
| 1/10, verso le 08:00 | Eseguire HU19_B1 (bozza del 30: 175 nodi, albero `d71ba6ee3c439917`, le forme complete di flop e turn dei piatti limpati compreso il donk `bet_12` sotto l'all-in; circa 10,5 GB in double e 4,1 ore, mai insieme a B2L o B1L secondo il piano [I]) |
| 1/10, verso le 08:00 | Terzo parere con la DLL dell'utente (S2-DLL, classifica ed equity) in sola lettura: il suo repository non si compila e non si modifica |
| 1/10, 08:00 | Push del branch `feat/monker-step1-checkdown` su origin: fatto |

Non eseguiti, dal piano: V2L15 e V2LT15 (costo del raggruppamento della chiave "turn" a parità di livelli), V2-s2, S5b, S5a
su HU50. Guardie ancora armate oggi: `ext/stop_ext_1958.ps1` e `night2/stop_night2_1958.ps1` (CANCEL alle 19:40, kill alle
19:58 dei processi che corrispondono ai loro schemi).

### 10.13 Chiusura del 1° ottobre (pomeriggio): verdetti finali di T2, S2-DLL, archivio; la batteria HU è chiusa

Coda `day1/queue_day1.sh` (in coda alle 09:51:29, finita alle 16:28:02): eseguibili `c123`, driver congelato
`out/frozen/run_correctness_v2.sh`, 3 thread per run e al massimo 6 in tutto, controllo della memoria libera prima di ogni run
lungo. Riepilogo automatico in `day1/results.md`, scritto da `summarize_day1.py`, che importa le regole di
`summarize_night2.py` senza modificarle; controlli automatici in `day1/checks.md` [V].

**Il criterio 3 di T2, rivisto con l'utente verso le 10:30.**

- **Prima** (10.1): il pavimento stimato con il fit a + b T^-p sugli ultimi cinque snapshot, con a <= 0,1 × NC(Tmax) o
  negativo.
- **Perché è cambiato**: applicato all'estensione di V2L, lo stesso fit legge un pavimento (a = +0,00053 a, il 30 % della
  NashConv a 448.000) su un run che è sceso sotto lo 0,10 % del piatto senza fermarsi (10.3). Un fit con una sola potenza
  scambia per pavimento la pendenza che si addolcisce quando il rumore del campionamento dei board comincia a pesare [I sulla
  causa].
- **Ora**: criterio 3 = **discesa tardiva**, pendenza log-log fra Tmax/4 e Tmax <= −0,35 (un pavimento la porterebbe verso 0).
  Il fit del pavimento si riporta e non decide. Gli altri criteri restano; il riepilogo del giorno aggiunge due letture più
  severe:
  - il criterio 2 chiede che gain_lower del CO e del BTN scendano anche a ogni raddoppio oltre 64.000 (64.000 → 128.000 →
    256.000);
  - ogni run è giudicato anche sulle sole righe fino a 64.000, con le stesse regole.
- **Quando**: la regola è cambiata prima dei risultati. Le estensioni sono finite alle 13:42 e alle 14:02, HU19_B1L alle
  16:28 [V, `queue.log` e il commento in `summarize_day1.py`].
- **Con la regola vecchia** [V, fit riportati in `results.md`]:
  - B2L passerebbe comunque: a = −0,0071 a sugli snapshot 64.000-256.000;
  - B1L no: a = 0,0110 contro 0,1 × NC = 0,0046 a;
  - HU19_B1L no: a = 0,1130 contro 0,0255 a.

  Con la regola nuova B2L e B1L passano anche sulle sole righe fino a 64.000.

**Verdetti** [V, `day1/results.md`]. NashConv postflop = gain_lower CO + BTN, in percentuale del piatto iniziale di 3a. I
rapporti con il controllo a Tmax sono estrapolati [I]:

| Run | Orario | NashConv postflop | Pendenza (criterio 1) | Discesa tardiva (criterio 3) | Controllo: fermo a; rapporto a 16.000; a Tmax [I] | Verdetto |
|---|---|---|---|---|---|---|
| B2L (river esatto), estensione | 09:51-13:42 | 0,13120 a (4,373 %) a 64.000 → **0,02280 a (0,760 %)** a 256.000 | −1,253 fra 8.000 e 256.000 | **−1,262** fra 64.000 e 256.000 | B2LG1: 8,114 a; 11,6 volte; circa 356 volte | **PASS** |
| B1L (flop e turn esatti), estensione | 09:51-14:02 | 0,09389 a (3,130 %) a 64.000 → **0,04556 a (1,519 %)** a 256.000 | −0,603 fra 8.000 e 256.000 | **−0,522** fra 64.000 e 256.000 | B1LG1: 1,333 a; 5,9 volte; circa 29 volte | **PASS** |
| HU19_B1L (forme complete dei piatti limpati), nuovo | 14:03-16:28 | 5,08699 a (169,6 %) a 250 → **0,25486 a (8,495 %)** a 64.000 | −0,760 fra 8.000 e 64.000 | **−0,722** fra 16.000 e 64.000 | HU19_B1LG1: 5,268 a; 7,6 volte; circa 20,7 volte | **PASS** |

- **In tutti e tre** [V]:
  - ogni snapshot PASS exact;
  - le etichette dell'albero sono presenti: `check;bet_4;all_in`, `fold;call;raise_16;all_in` e, in HU19_B1L, anche
    `check;bet_12;all_in` (B1L non ha rilanci, quindi solo la prima);
  - nessuna pendenza locale sopra −0,2 dai 4.000, nessuna risalita;
  - gain_lower del CO e del BTN in calo in ogni raddoppio;
  - nessuna condizione di FAIL.
- **B2L**: oltre 64.000 le pendenze locali diventano più ripide invece di addolcirsi (−1,18, −1,24, −1,30, −1,35 fino a
  256.000). A 256.000 la NashConv è lo 0,570 % del piatto del flop (4a). A 4.000 il motore del river di riferimento dà valori
  identici bit per bit a quello congiunto.
- **B1L**: le pendenze locali vanno da −0,70 (4.000-16.000) a −0,49 (192.000-256.000), cioè verso il ritmo Monte Carlo −0,5 di
  V1L (10.3) [I]. A 256.000 è l'1,139 % del piatto del flop.
- **HU19_B1L**: gioco `HU19_B1_flopturn` (19a, 175 nodi, albero `d71ba6ee3c439917`, commit `3f4d0da`), flop e turn esatti
  (`v1_flopturn_exact`).
  - Aggiunge ai giochi B il `bet_12` non all-in dopo un bet-call al flop (donk compreso) e il `raise_16` al turn.
  - Controllo HU19_B1LG1: turn a un id per board (`v1g_flopexact_turn1x1`), dalle 14:05 alle 14:44.
  - Primo segmento PASS: capacità [302.544, 6.825.456, 206.415], 10.360.453.012 byte di stato come atteso, 162 righe
    bloccate, picco 10,514 GB (limite 13,612), 0,095 s per iterazione.
  - Ha aspettato la memoria dalle 13:46 alle 14:02 (servivano 10,5 + 3 GB liberi) ed è partito dopo la fine di B2L e B1L,
    come da piano.
  - Pendenze locali da −0,83 / −0,85 (8.000-24.000) a −0,60 (48.000-64.000).
  - Livello (non è un criterio): 6,371 % del piatto del flop a 64.000, il più alto dei giochi B.
- **Ripresa delle estensioni**: controllo automatico PASS alle 10:27:46 (B2L) e alle 10:31:03 (B1L). Ripresa da 64.000 con
  l'identità del trainer della notte (`a60b8a0424f397ea`, `25e79c66292f0013`) [V].
- **Ribasamento delle epoche dello sconto lazy** (a 65.535, 131.070 e 196.605): nessun gradino nelle pendenze locali. B2L
  −1,18 sia fra 48.000 e 64.000 sia fra 64.000 e 96.000, B1L −0,58 contro −0,55 [V].

**S2-DLL: terzo parere con la DLL dell'utente** [V]. Dalle 09:19 alle 09:23, `tools/independent/sd_dll_check.py` (commit
`d0796f8`, sezione nel README di `tools/independent/`), uscite in `out/monker/correctness/independent/S2_dll/`.

- **La DLL**: `equity_calculator.dll` del calcolatore dell'utente, compilata il 14/05/2026, SHA-256 `ab09c244…2fa985`.
  - È caricata in sola lettura con ctypes: niente build e nessuna scrittura nel suo repository.
  - Le chiamate usano sempre due combo singole, quindi l'enumerazione esatta. Le guardie a runtime escludono i rami Monte Carlo
    e la risposta di ripiego 1/n.
  - La distorsione nota del calcolatore sta nel Monte Carlo multiway in TypeScript (9.2), non nella DLL.
- **Tutto PASS** (187 s, 2 processi):
  - **R7**: i punteggi della DLL su tutti gli 8.347.680 insiemi di 7 carte hanno lo stesso ordine debole della tabella dei
    ranghi del motore (752 livelli, 0 violazioni);
  - **RV**: 1.000.000 di river casuali, 0 differenze di vincitore o di pareggio (fra questi 13.345 scala contro tris e 1.748
    colore contro full);
  - **PA**: 2.012 coppie preflop (2.000 casuali e 12 di bordo); le equity della DLL sono identiche bit per bit a
    (2W + T) / 402.752 della tabella degli all-in;
  - **PA2**: W, T e L uguali su tutte le 2.012 coppie.
- **Mutazioni** (`--self-test`): lo scambio dei livelli adiacenti scala/tris dà 1 violazione in R7 e 22 differenze in RV. La
  mutazione che lascia uguale 2W + T passa PA, come previsto, e fallisce PA2.
- **Limiti**:
  - la catena fino a tutte le 176.715 coppie è derivata (R7 + S1 + S2), non provata coppia per coppia con la DLL;
  - che le due funzioni esportate corrispondano ancora ai sorgenti di oggi è inferito [I];
  - resta un indizio indipendente, non una prova (10.10, punto 6).

**Archivio su F:** con `archive_run.ps1`, una cartella alla volta, ognuna verificata attraverso la junction [V]:

- **Mattina, 09:13-09:40** (`archive_1001/archive_1001.log`): 20 cartelle, circa 30,9 GB.
  - I 18 run finiti e valutati: V1L, V2L, V2, V1LG, V1LG1, V2LG, V2LG1, V1L_s2, V2L_s2, V2Z, V2R, V2R5, B0L, B0LG1, B0M,
    B0MG1, B2LG1, B1LG1.
  - Più `smoke2` e `checks` (18,51 GB). C: libero da 251,1 a 280,4 GB.
  - Il primo giro (09:13) si è fermato dopo V1L, già archiviata e verificata, perché un lettore teneva aperto il log; il secondo
    (09:24) l'ha saltata in quanto junction.
  - `smoke`, saltata alle 09:40 perché aperta da un `tail`, è una junction dalle 09:55, `R3` dalle 09:57 [V, elenco della
    cartella; il loro log non è fra quelli letti].
- **Pomeriggio, 16:31-16:42** (`archive_1001b/archive_hu.log`): i quattro run del giorno, circa 19,9 GB, tutti con rc 0.
  - B2L 6,69 GB, B1L 3,79 GB, HU19_B1L 9,18 GB, HU19_B1LG1 0,24 GB.
  - Prima di ogni cartella lo script controlla che `run.log` finisca con "done" e che nessun processo ne tenga aperto un file.
  - C: libero da 250,0 a 268,9 GB, F: da 175,9 a 156,0 GB.
- Restano su C: `buckets` e `independent`, che lo script non archivia per scelta.

**Correzione di 10.12** [V, reflog di `origin/feat/monker-step1-checkdown`]: il ref remoto è stato aggiornato da due push, alle
09:10:32 (fino a `5bc2a9c`) e alle 10:29:02 (fino a `0d7ef75`, che contiene questa sezione 10 fino a 10.12). Le "08:00" di 10.12
sono l'ora della decisione, non del push [I].

**Quadro finale: la batteria HU è chiusa, 0 FAIL** [V per gli esiti; sezioni 10.3-10.9 e questa]:

| Parte | Esito |
|---|---|
| V2, V2L | PASS; V2L passa anche il livello G1 dopo l'estensione (0,059 % del piatto a 448.000) |
| V1L | G2-G5 PASS, G1 non raggiunto: 0,553 % a 448.000, al ritmo Monte Carlo (circa 14 milioni di iterazioni per lo 0,10 % [I]); nessuna condizione di FAIL |
| T1 (A1-A5, tre rafforzamenti) | PASS |
| T2 (B0L, B0M, B2L, B1L, HU19_B1L) | PASS |
| T3 (S1, S2, S3, S4 su HU50, S5a, S6) e S2-DLL | PASS; S4 su HU6_all INCONCLUSIVE per costruzione (nessun riepilogo del motore da confrontare) |
| T4 (U1-U3, V2Z, V2R, V2R5) | PASS |
| T5 (seed 2, D1-D5), T6 (P2-V0), D6 | PASS |

- Resta valido l'elenco di quello che non si può certificare (10.10). Non eseguiti, e non necessari per chiudere: V2L15,
  V2LT15, V2-s2, S5b, S5a su HU50 (10.12).
- **Risposta alla domanda di 10.1**, nei limiti di 10.10: sui giochi senza perdita lo stesso percorso di codice di HU50 va verso
  l'equilibrio del gioco che gli diamo. Vale con e senza rake, con puntate e rilanci sotto l'all-in su flop, turn e river, e
  nessuna misura mostra un pavimento [V per le misure; I per l'estensione a HU50, la cui astrazione è con perdita].

