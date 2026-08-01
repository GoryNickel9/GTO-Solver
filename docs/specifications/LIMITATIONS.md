# Limitazioni

## Stato prodotto corrente

GTOSD supporta un workflow locale HU postflop exact con range pesati, tree
configurabile, CFR+, best response, checkpoint, storage autenticato, CLI e GUI
Qt. “Exact” significa enumerazione del gioco discretizzato configurato senza
sampling o bucketing; non significa equilibrio matematico esatto a iterazioni
finite.

## Limiti di parità

- Il gate memoria GTO+ passa; il gate velocità fallisce.
- Il root EV AhKhQh è allineato entro 0,0055 ante.
- Gli EV BTN condizionali differiscono, ma i posteriori root non sono uguali;
  non sono quindi una prova isolata di errore downstream.
- F10.4 controlled-posterior è pianificata, non implementata.
- Il run GTO+ a target 0,10% non ha raggiunto il target dopo circa 245 s.

Le fasi F11+ restano congelate dal parity journey.

## Funzioni non ancora supportate

- node locking globale di prodotto;
- albero HU preflop completo e workflow preflop-river;
- multiway, side pot completi e metriche general-sum;
- database di flop e trainer di prodotto;
- abstraction/bucketing con errore misurato;
- GPU o calcolo distribuito;
- rake avanzato per stake/player count, jackpot drop e valute;
- formati di soluzione preflop/multiway.

La presenza di tipi, placeholder o chunk riservati non costituisce supporto.

## Vincoli tecnici

- GUI desktop e packaging certificati sono attualmente Windows/Qt 6.
- Il public tree ha un build limit configurabile e può superare RAM per alberi
  grandi.
- `Float32` riduce lo stato ma può cambiare la traiettoria numerica; richiede
  confronto con `Float64`.
- La quantizzazione `uint16` è sperimentale e lossy.
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
