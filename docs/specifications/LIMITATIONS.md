# Limitazioni

## Stato prodotto corrente

GTOSD supporta un workflow locale HU postflop exact con range pesati, tree
configurabile, CFR+/DCFR, best response, checkpoint, storage autenticato, CLI e
GUI Qt. Supporta inoltre API installabili di card abstraction e subgame solving
CFR+ con guard exact-NashConv e un percorso HU postflop bucketed opt-in via API
e CLI. “Exact” significa enumerazione del gioco
discretizzato configurato senza sampling o bucketing; non significa equilibrio
matematico esatto a iterazioni finite.

## Limiti di parità

- dEV, root EV, layout ed exact outcomes passano sui tre benchmark correnti; i
  gate velocità TH e TST falliscono.
- Cinque processi final-head producono mediane/p95 AHK
  0,758705/0,790918 s, TH 19,948228/24,192260 s e TST
  184,095930/197,865030 s contro limiti 1,900000 / 19,622222 / 128,988889 s.
  TH e TST falliscono sia mediana sia p95; la mediana TH supera il limite del
  `1,661%`, quella TST del `42,722%`.
- La parità memoria non è attualmente valutabile. I valori GTO+ `8/399/2.000
  MB` provengono da “Memory needed for solving”, la cui composizione interna è
  ignota; i Peak RSS GTOSD `7.790.592/363.569.152/1.534.152.704 B` sono soltanto
  telemetria OS. Non esiste un cap desktop `<2 GiB` indipendente.
- Gli EV BTN condizionali differiscono, ma i posteriori root non sono uguali;
  non sono quindi una prova isolata di errore downstream.
- F10.4 controlled-posterior è implementata esclusivamente come diagnostica
  test-only; non è node locking globale di prodotto.
- Il run GTO+ a target 0,10% non ha raggiunto il target dopo circa 245 s.

La parity è posposta mentre viene qualificata una granularità globale del nuovo
percorso bucketed/subgame. La precedente matrice AHK K=16 / TH K=128 non è un
default di prodotto. Il path exact resta l'oracolo e i risultati bucketed non
possono essere chiamati exact strategy.

## Vincolo permanente CPU/RAM

Il solving usa e userà soltanto CPU e RAM. L'assenza della GPU non è una
funzione ancora da implementare, ma una decisione permanente di prodotto e di
architettura. Sono esclusi backend CUDA, ROCm, OpenCL, Vulkan Compute,
DirectCompute e tecnologie equivalenti. La GPU può essere usata esclusivamente
dal sistema grafico per renderizzare la GUI, senza partecipare ai calcoli.
Il target di working set opzionale può affidare la residenza delle pagine al
sistema operativo e quindi causare paging locale. Il wiring storico che lo
derivava impropriamente dal riferimento GTO+ è stato rimosso; il backend è ora
solo opt-in tramite budget utente esplicito. Il backing logico completo deve
comunque essere contabilizzato e non può ridurre la metrica comparabile.

## Funzioni non ancora supportate

- node locking globale di prodotto;
- albero HU preflop completo e workflow preflop-river;
- multiway, side pot completi e metriche general-sum;
- database di flop e trainer di prodotto;
- chunk `ABSTRACTION` e packaging autosufficiente `.gtsd` del postflop bucketed;
- selettore di granularità nella GUI;
- cache feature compressa/memory-mapped; il manifest testuale exact 1.0 e il
  preflight RAM/disco specifico per cache/layout astratto sono disponibili;
- codec bucketed compresso e DCFR bucketed qualificato; gli update CFR+
  Float64 a otto thread sono supportati;
- multi-root, continual resolving e gadget teorico safe con boundary
  counterfactual values; il resolver `DenseLayout` corrente accetta un singolo
  frontier canonico a ingresso univoco e usa un guard exact full-game;
- provenance del resolving incorporata nel container: lo stato è nel checkpoint
  atomico distinto, mentre path e decisione candidate/fallback sono nel report
  JSON sidecar; un resume successivo è un warm start, non trajectory parity;
- generazione/qualificazione delle feature preflop;
- qualifica globale K16/K32 sulla terna AHK/TH/TST e, successivamente, su
  ulteriori fixture turn/preflop rappresentative; K16 e K32 sono entrambi
  respinti dal FAIL TH a 800 iterazioni, TST K32 è skipped dall'early reject;
- gadget safe scalabile con boundary counterfactual values quando la BR esatta
  full-game non è fattibile;
- calcolo distribuito;
- rake avanzato per stake/player count, jackpot drop e valute;
- formati di soluzione preflop/multiway.

La presenza di tipi, placeholder o chunk riservati non costituisce supporto.

## Vincoli tecnici

- GUI desktop e packaging certificati sono attualmente Windows/Qt 6.
- Il public tree ha un build limit configurabile e può superare RAM per alberi
  grandi.
- Il benchmark production usa regret e strategy uint16 action-major con scale
  float32 per decision node (`ScaledUint16RegretStrategy`) e hot-path float32
  dichiarato nel report; la best response enumera tutti gli outcome sullo stato
  decodificato. Ogni nuovo formato richiede BR, dEV e confronto di root EV, non
  soltanto finitezza.
- Lo stato runtime OS-page-backed non è direttamente serializzato: checkpoint
  o archivio richiedono materializzazione esplicita, che può aumentare il
  working set dell'intera dimensione logica dello stato.
- Con range asimmetrici e simmetrie di seme non banali il core usa ancora il
  layout fisico: la condivisione canonica richiede reach e molteplicità
  player-local non ancora implementate correttamente.
- Il catalogo SQLite non replica né recupera la chiave degli archivi.
- Le chiavi `.gtsd` devono essere gestite dal layer chiamante.

## Limiti teorici

NashConv e best response correnti sono definite per il dominio HU. Il rake
introduce una somma payoff negativa ma mantiene due decisori; il multiway non
può essere dichiarato risolto trasferendo automaticamente garanzie zero-sum.

Strategie differenti possono condividere lo stesso valore in un gioco piatto o
con equilibri multipli. Parità di EV root non implica parità combo-per-combo;
viceversa, un profilo visivamente simile non dimostra bassa exploitability.

## Uso

Il software è destinato ad analisi offline. Non legge client poker live, non
riconosce tavoli in tempo reale, non produce overlay o suggerimenti durante il
gioco e non automatizza azioni.
