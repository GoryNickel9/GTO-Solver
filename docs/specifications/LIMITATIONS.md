# Limitazioni

## Stato prodotto corrente

GTOSD supporta un workflow locale HU postflop exact con range pesati, tree
configurabile, CFR+, best response, checkpoint, storage autenticato, CLI e GUI
Qt. “Exact” significa enumerazione del gioco discretizzato configurato senza
sampling o bucketing; non significa equilibrio matematico esatto a iterazioni
finite.

## Limiti di parità

- dEV, root EV, layout, exact outcomes, `solver_state_bytes` e cap desktop
  passano sui tre benchmark correnti; i gate velocità TH e TST falliscono.
- Cinque processi final-head producono mediane/p95 AHK
  0,758705/0,790918 s, TH 19,948228/24,192260 s e TST
  184,095930/197,865030 s contro limiti 1,900000 / 19,622222 / 128,988889 s.
  TH e TST falliscono sia mediana sia p95; la mediana TH supera il limite del
  `1,661%`, quella TST del `42,722%`. Il cap desktop comune `< 2 GiB` passa su
  tutte le fixture; i riferimenti peak-RSS di GTO+ restano diagnostici separati.
- Gli EV BTN condizionali differiscono, ma i posteriori root non sono uguali;
  non sono quindi una prova isolata di errore downstream.
- F10.4 controlled-posterior è implementata esclusivamente come diagnostica
  test-only; non è node locking globale di prodotto e non sblocca F11+.
- Il run GTO+ a target 0,10% non ha raggiunto il target dopo circa 245 s.

Le fasi F11+ restano congelate dal parity journey.

## Vincolo permanente CPU/RAM

Il solving usa e userà soltanto CPU e RAM. L'assenza della GPU non è una
funzione ancora da implementare, ma una decisione permanente di prodotto e di
architettura. Sono esclusi backend CUDA, ROCm, OpenCL, Vulkan Compute,
DirectCompute e tecnologie equivalenti. La GPU può essere usata esclusivamente
dal sistema grafico per renderizzare la GUI, senza partecipare ai calcoli.

## Funzioni non ancora supportate

- node locking globale di prodotto;
- albero HU preflop completo e workflow preflop-river;
- multiway, side pot completi e metriche general-sum;
- database di flop e trainer di prodotto;
- abstraction/bucketing con errore misurato;
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
