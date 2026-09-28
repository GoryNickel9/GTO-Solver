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
| Albero | Identico a quello delle chart MonkerSolver; postflop non noto, quindi bet e raise al 100 % del piatto, all-in sempre disponibile, niente donk bet (consiglio ufficiale di MonkerSolver) |
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
con MonkerSolver, arresto sotto 0,01. Costo misurato nel run: 0,070-0,079 s per iterazione (0,067 nella
prova breve, di cui 0,061 di attraversamento), salvataggio 24-30 s e ripresa circa 26 s per blocco:
blocchi da 5,4-6,5 minuti. Senza lo sconto lazy il costo era 0,75 s per iterazione (0,66 di sconto
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
fold 37,4 %; passo 2 all-in 26,8 %, open 9,3 %, limp 28,9 %, fold 35,0 %. Il nostro CO apre a 5a mani
forti (AA 83,5 %, KK, KQs, KJs) che MonkerSolver limpa (AA 93 %) o shova (KQs 99 %, KK 75 %).

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
  di 0,383): corretta su richiesta dell'utente ("non la media, ma le combo effettive").
- **Stessa azione principale: abbandonata** (decisione dell'utente): nasconde le strategie miste (51/49
  contro 49/51 conta come diverso, 100/0 contro 51/49 come uguale) e con range diversi dà 100 % anche dove
  i range non si somigliano (CO contro lo shove dopo l'open: 3,1 contro 58,9 combo effettive).
- **Preferenza suited alla radice** (diagnostica): media sulle 36 coppie di ranghi diversi di (limp + open
  della suited) − (limp + open della offsuit). MonkerSolver 0,224, passo 2 0,399.
- **Soglia di rumore**: due run identici con seed diverso (5.2) danno chart distanti 0,007 / 0,034 e
  valori contro MonkerSolver che differiscono di 0,0005 / 0,006. Una variante conta se sposta la distanza
  da MonkerSolver di almeno circa 0,005 o la differenza di range di almeno circa 0,02.

### 5.2 Varianti del passo 2 (una modifica alla volta, 28 settembre sera)

Tutte con lo stesso runner (`tools/monker_compare/run_step2_continuous.sh`, chart dal trainer vivo ogni
4.000 iterazioni, arresto sotto 0,01), tutte stabili a 24.000 iterazioni. Valori a 24.000 iterazioni.

| Run | Cosa cambia | Distanza da MonkerSolver | Differenza di range | Preferenza suited | Distanza / range dalle chart del passo 2 | Picco di memoria | Durata |
|---|---|---:|---:|---:|---:|---:|---:|
| Passo 2 | — | 0,0725 | 0,383 | 0,399 | — | 5,61 GB | 36 min |
| Seed 2 | solo il seed del campionamento dei board | 0,0720 | 0,388 | 0,399 | 0,007 / 0,034 (rumore) | 5,61 GB | 31 min |
| 15 livelli di forza | bucket 15 × 4 (livelli a 30 uniti a coppie) | 0,0723 | 0,378 | 0,364 | 0,009 / 0,055 | 2,86 GB | 28 min |
| Potenziale a 1 livello | bucket 30 × 1 | 0,0755 | 0,391 | 0,349 | 0,018 / 0,105 | 2,75 GB | 27 min |
| **Donk bet ammessi** | `HU50_step2_donk.json`, 571 nodi | **0,0667** | 0,376 | 0,372 | 0,018 / 0,136 | 6,62 GB | 33 min |
| Size 75 % | tolta dall'utente prima del run (1.036 nodi, 11,7 GB) | — | — | — | — | — | — |

- **15 livelli**: chart entro il rumore, metà memoria, blocchi del 15 % più rapidi: risparmio gratuito.
- **Potenziale a 1 livello**: sposta le chart più del rumore (0,018) ma non verso MonkerSolver; riduce la
  preferenza suited, non abbastanza. Non è un risparmio gratuito.
- **Donk bet**: l'unica variante che avvicina le chart a MonkerSolver oltre il rumore (−0,0058), nei nodi
  del piatto limp-isolation-call dove senza donk il CO può solo fare check: CO contro l'isolation 0,174 ->
  0,141, BTN contro il limp 0,124 -> 0,111. L'analisi preventiva (5.3) prevedeva il contrario. Indizio che
  le chart dell'utente siano state calcolate con i donk bet, contro la ricetta ufficiale.

### 5.3 Dove stanno le differenze (analisi con tre analisti indipendenti e un arbitro)

- Le mani **offsuit coincidono** con MonkerSolver entro 0,03 nei nodi principali; le differenze sono nelle
  **suited e nelle coppie** del CO (radice: suited limp/open/all-in 42/0/36 % in MonkerSolver contro
  55/8/20 % nostro; coppie open 2 % contro 27 %, all-in 22 % contro 9 %). Il nostro postflop dà più valore
  ai progetti di chi è fuori posizione nei piatti da 12a.
- **Esclusi dai dati**: "tris batte scala" (MonkerSolver chiamerebbe 99, che folda), button blind da 2a
  (chiamerebbe KJo, JTo, 99), rake (sposterebbe anche le offsuit, che coincidono).
- **Più iterazioni non chiudono lo scarto**: estrapolando la convergenza le chart si muovono ancora di
  circa 0,027 e la distanza sale leggermente (0,079): la radice migliora, gli altri nodi peggiorano.
- **Leve residue**: le varianti di astrazione (livelli, potenziale, texture) cambiano poco le chart;
  quelle dell'albero (donk bet, seconda size più piccola) molto di più. Una seconda size (50 % + 100 %)
  porta l'albero a 2.429 decisioni postflop (circa 16 volte memoria e tempo): solo su un server affittato.

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

**Conclusione.** Nel nostro gioco le chart di MonkerSolver valgono quanto le nostre: perdono da 30 a 140
volte meno della soglia dell'1 % del piatto (0,03 a), e meno del nostro stesso scarto dalla migliore
risposta preflop. Le differenze fra le chart (distanza 0,07) stanno fra azioni quasi equivalenti: in EV
il passo 2 riproduce MonkerSolver. La policy del passo 2, cancellata dal vecchio runner, è stata
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

- **TX1 e TX2 danno le stesse chart** (distanti fra loro 0,007 / 0,047, come due seed): lo spostamento
  dal passo 2 viene dalla fusione dei turn in sé, non dall'aggressività della regola. A parità di effetto
  TX2 risparmia di più.
- **Sulle chart non è neutra**: rispetto al passo 2 TX2 supera la soglia del piano (0,018 / 0,105), TX1 è
  nella zona grigia; gli asintoti stimati (a + b/t su due salvataggi) confermano uno spostamento reale
  (0,025 / 0,14 contro 0,0125 / 0,060 fra i seed). Lo spostamento va leggermente **verso MonkerSolver**
  (asintoti 0,0714 contro 0,0788), che fonde anch'esso turn e river.
- **In EV è neutra**: le chart TX2 giocate nel gioco del passo 2 perdono −0,00065 a (CO) e −0,00017 a (BTN),
  cioè fanno appena meglio delle chart del passo 2 (più convergenti); le chart di MonkerSolver nel gioco
  TX2 perdono 0,0016 a (0,052 % del piatto) e 0,0006 a.
- **Tempo**: la convergenza delle chart è più rapida (distanza da MonkerSolver 0,073 già a 8.000
  iterazioni) ma il tempo per iterazione non scende (0,087 s contro 0,080): il guadagno è di memoria.

- **Astrazione compatta (15 livelli + TX2)**: aggiungere i 15 livelli al TX2 è neutro (dal TX2 0,008 /
  0,048, come due seed); memoria 5,4 volte più piccola del passo 2 (1,03 GB); distanza da MonkerSolver
  0,0706; le sue chart nel gioco del passo 2 perdono −0,00082 / −0,00028 a (appena meglio); le chart di
  MonkerSolver nel gioco compatto perdono 0,0017 a (0,055 % del piatto, CO) e 0,0007 a (BTN).

Chart di tutte le varianti giocate nel gioco del passo 2 (esatto, 573 flop; CO / BTN, negativo = meglio
delle chart del passo 2): seed 2 −0,00002 / −0,00000 a, 15 livelli −0,00037 / −0,00015, potenziale a 1
livello +0,00014 / −0,00001, TX1 −0,00046 / −0,00017, TX2 −0,00065 / −0,00017, 15 livelli + TX2 −0,00082 /
−0,00028, donk bet (chart di un gioco diverso) +0,00036 / +0,00044: tutte equivalenti in EV entro lo
0,03 % del piatto. Con questa misura il passo 2 e tutte le sue varianti danno chart intercambiabili; la
distanza fra chart resta utile per vedere dove si spostano, non per giudicarle.

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
```

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
MonkerSolver nel nostro gioco (5.4), turn in texture (5.5). Da decidere con l'utente:

1. **Configurazione HU di riferimento**: donk bet sì o no (unica variante che avvicina a MonkerSolver);
   astrazione compatta 15 livelli + TX2 (1 GB invece di 5,6, equivalente in EV, sposta le chart di
   0,02 fra azioni equivalenti e un po' verso MonkerSolver). Da provare: donk bet + astrazione compatta
   insieme.
2. **Criterio di somiglianza**: la perdita di MonkerSolver nel nostro gioco (0,03 % del piatto) dice
   che le chart sono equivalenti in EV; la distanza 0,07 resta come misura descrittiva.
3. **Seconda size postflop** (50 % + 100 %): solo su un server affittato (circa 80 GB), con l'ok
   dell'utente; è la leva più grande rimasta sulle chart.
4. 3-way 50a: raise al 100 % del piatto nel costruttore, regole sparse (cold call), tabella di equity a
   tre giocatori, motore multiway, astrazione compatta; confronto con le chart 3-way dell'utente con gli
   stessi strumenti (distanza, differenza di range, MonkerSolver nel nostro gioco).
