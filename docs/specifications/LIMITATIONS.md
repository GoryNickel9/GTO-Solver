# Limitazioni

## Stato prodotto corrente

GTOSD supporta un workflow locale HU postflop exact con range pesati, tree
configurabile, `ProductionDcfr` (CFR+ come oracolo), best response, checkpoint,
storage autenticato e CLI (`gto_cli`). “Exact” significa enumerazione del gioco
discretizzato configurato senza sampling o bucketing; non significa equilibrio
matematico esatto a iterazioni finite.

Questo file descrive i limiti di `gto_cli` e delle librerie postflop. Il
solver preflop blueprint ha limiti propri, riassunti nella sezione "Solver
preflop blueprint". La GUI desktop Qt è stata tolta il 2026-10-02.

## Limiti di parità

- dEV, root EV, layout ed exact outcomes passano sui tre benchmark correnti; i
  gate velocità TH e TST falliscono.
- La final-head del 2026-09-01 conta cinque processi production. Ognuno risolve
  AHKHQH, TH7D6S e TSTC9D fino a `Target dEV < 1%` stretto, in `80/80/160`
  iterazioni deterministiche. Mediane/p95 del solver: AHK
  0,758705/0,790918 s, TH 19,948228/24,192260 s e TST 184,095930/197,865030 s,
  contro limiti di 1,900000 / 19,622222 / 128,988889 s.
- TH e TST falliscono sia la mediana sia il p95. La mediana TH supera il limite
  del `1,661%`; quella TST lo supera di `55,107041 s` (`42,722%`). Il
  riferimento grezzo GTO+ di TSTC9D resta `116,09 s`, il primo punto
  strettamente sotto l'1%. Documento di riferimento:
  [`DCFR_EPOCH_RESET_GAMMA3_FEASIBILITY_2026-09-01.md`](../archive/legacy-postflop-2026-07-09/DCFR_EPOCH_RESET_GAMMA3_FEASIBILITY_2026-09-01.md).
- La parità memoria non è attualmente valutabile. I valori GTO+ `8/399/2.000
  MB` provengono da “Memory needed for solving”, la cui composizione interna è
  ignota; i Peak RSS GTOSD `7.790.592/363.569.152/1.534.152.704 B` sono soltanto
  telemetria OS. Non esiste un cap desktop `<2 GiB` indipendente.
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
DirectCompute e tecnologie equivalenti. La GPU può servire soltanto a disegnare
l'interfaccia, senza partecipare ai calcoli.
Il target di working set opzionale può affidare la residenza delle pagine al
sistema operativo e quindi causare paging locale. Il wiring benchmark corrente
lo deriva impropriamente dal riferimento GTO+ e deve essere rimosso. Se il
backend resterà disponibile come funzione di prodotto, richiederà un budget
utente esplicito; in ogni caso il backing logico completo deve essere
contabilizzato e non può ridurre la metrica comparabile.

## Funzioni non ancora supportate da `gto_cli`

- node locking globale di prodotto;
- albero HU preflop completo e workflow preflop-river;
- multiway, side pot completi e metriche general-sum;
- database di flop e trainer di prodotto;
- abstraction/bucketing con errore misurato;
- calcolo distribuito;
- rake avanzato per stake/player count, jackpot drop e valute;
- formati di soluzione preflop/multiway.

La presenza di tipi, placeholder o chunk riservati non costituisce supporto.
Preflop HU e 3-way, astrazione delle carte e chart preflop esistono nel solver
preflop blueprint, con i limiti descritti più sotto.

## Vincoli tecnici

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

## Solver preflop blueprint

`libs/preflop_blueprint` è un solver distinto e non segue il contratto "exact"
di questo file. La sua documentazione è in `docs/research/preflop_vector_cfr/`.
Limiti al 2026-10-03:

- Usa l'astrazione delle carte (bucket e righe per classe di board) e campiona
  i board, quindi le sue strategie non sono exact. Su HU50 la NashConv fisica
  della configurazione di riferimento G1 è circa 0,78 a, il 26 % del piatto.
  È il pavimento dell'astrazione, non un difetto di convergenza.
- Il gate "best response astratta ≤ 0,03 a" è stato ritirato insieme a
  history7. I criteri di accettazione del prodotto (correttezza, best response
  sulle carte reali, confronto con MonkerSolver) sono una proposta ancora da
  approvare.
- La best response esatta sulle carte reali e il valore delle chart di
  MonkerSolver (`gtosd_preflop_blueprint_monker_values`) funzionano solo in HU.
- Il 3-way non deve essere esatto per scelta. Con tre giocatori CFR non
  garantisce un equilibrio di Nash, e la qualità si misura con i guadagni di
  deviazione per seggio. La parte A della fase 3b dà i valori a policy fissa
  per seggio. La best response completa 3-way (NashConv, parte B) non è
  implementata.
- Le partite da 4 a 6 giocatori non sono implementate.

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
