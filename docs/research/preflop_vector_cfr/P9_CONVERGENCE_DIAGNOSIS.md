# P9 — Diagnosi della convergenza CO40

> **Riapertura, 2026-09-19:** l'[audit della normalizzazione](NASH_AUDIT_2026-09-19.md)
> corregge un fattore 630 nella perdita postflop del diario, con test indipendenti
> e metriche globali invariate. Le conclusioni di esaurimento delle possibilita
> riportate sotto restano una valutazione storica, non un limite dimostrato.
> L'utente ha autorizzato una nuova indagine causale; CO40 resta non qualificato.

Aggiornamento: 2026-09-19. Stato: **indagine conclusa senza qualificazione di CO40**; la
decisione che segue è dell'utente.

CO40 non è qualificato e le misure non indicano più un parametro da girare. Tre assi indipendenti
dell'astrazione — memoria della chiave, capacità dei bucket, budget di iterazioni — sono stati
spazzati in entrambe le direzioni e ciascuno rende qualche punto percentuale, mentre il divario dal
gate vale 2,0 volte su HU20 e 16,7 su CO40. Il divario è **strutturale alla famiglia di
astrazione**, non a un suo parametro.

Il riuso del campione negli aggiornamenti alternati resta un errore dimostrato e corretto.
L'astrazione dimentica informazioni già usate dal giocatore, quindi la garanzia CFR ordinaria non
si applica a questa rappresentazione. Il prototipo con memoria completa (`recall32`, 184.528 righe
di river) ha dato 0,491631 a contro lo 0,500181 di `class` a 41.973: quattro volte la
rappresentazione per l'1,7 %.

Sintesi in **§ Esito della fase**; il resto del documento è il registro di come ci si è arrivati.

## Obiettivo

CO40 usa una sola size postflop del 100% del piatto, più all-in. Confermato
dall'utente il 2026-09-16: il passaggio a tre size non fa parte di questa attività.

Il lavoro attuale si concentra su CO40. L'utente ha chiesto di non avviare
ulteriori test a 100 e 300 ante; i risultati già ottenuti restano riportati
come controlli precedenti. La correzione deve essere generale, senza rami
di codice o parametri scelti per far passare questa fixture.

## Esito della fase

### Il gate

L'utente ha stretto il gate di accettazione il 2026-09-18 (**D27**): da 0,1 ante per giocatore
— dichiarata «soglia fisica *iniziale*» e «da stringere quando l'astrazione migliora» — a
**0,03 ante, l'1 % del piatto iniziale**, che è già lo standard con cui è stato accettato il
prodotto postflop. La soglia resta ancorata al piatto e indipendente dallo stack.

D3 non è un gate: è la **regola di arresto del training**. Nel programma è stata a lungo citata
come soglia di accettazione, ed è un errore corretto nel diario il 2026-09-18.

### La frontiera

Certificazioni esatte, 605.088 board, 2.000 iterazioni, variante `class`.

| Gioco | Stack | baseline | `class` | % del piatto | **× il gate (0,03)** |
|---|---:|---:|---:|---:|---:|
| HU10 | 10 a | 0,003981 | — | 0,13 % | **passa** |
| HU20 | 20 a | 0,128849 | 0,060957 | 2,03 % | 2,03× |
| HU30 | 30 a | 0,571773 | 0,324082 | 10,80 % | 10,8× |
| CO40 | 40 a | 0,827177 | 0,500181 | 16,67 % | 16,7× |

Il guadagno di `class` decresce con la profondità: 52,7 → 43,3 → 39,5 %. **La frontiera del
prodotto sta fra 10 e 20 ante**, e HU20 è l'unico bersaglio a portata.

### I tre assi, tutti esauriti

| Asse | Escursione provata | Resa migliore |
|---|---|---:|
| Memoria della chiave | 1.000 → 765.243 righe di river | +1,7 % (`recall32`) |
| Capacità dei bucket | 200/500/1.000 → 500/1.000/2.000 | +1,8 % (su HU20) |
| Budget di iterazioni | 1.000 → 10.000 | banda del 7 %, **minimo interno** |

### La curva iterazioni/exploitability ha un minimo interno

Sulla stessa traiettoria di CO40, misurata esattamente in cinque punti:

| Iterazioni | 1.000 | 2.000 | 4.000 | 8.000 | 10.000 |
|---|---:|---:|---:|---:|---:|
| Exploitability | 0,529728 | **0,500181** | 0,502202 | 0,528748 | 0,535882 |
| EV di CO | −0,149135 | −0,146476 | −0,145398 | −0,144610 | −0,144386 |

L'**EV migliora monotonicamente** su tutti e cinque i punti mentre l'**exploitability fisica ha un
minimo a 2.000** e poi peggiora. CFR converge nel proprio gioco astratto — il valore lo dimostra —
ma la soluzione di quel gioco non è quella del gioco fisico, e avvicinarsi alla prima allontana
dalla seconda. È la patologia dell'astrazione, mostrata sull'asse delle iterazioni dentro un solo
run. Conseguenza operativa: **nessun budget di iterazioni qualifica CO40**, e la regola di arresto
D3, applicata come nel codice al valutatore fisico, non può scattare su questo gioco — tutti i run
finiscono con `ITERATION_LIMIT`.

### Ipotesi generate e cadute

1. L'astrazione postflop corrompe i valori preflop — refutata: `class` migliora del 39,5 % e la
   strategia preflop non si muove di un decimale.
2. Lo shove cresce con la profondità — refutata: decresce, 59,9 → 45,7 → 35,1 %.
3. La strategia media è incoerente con i propri EV — non supportata.
4. Il postflop è giocato male, perciò CO evita di entrarci — refutata: la perdita postflop è piatta
   fra gli ingressi, uguale per i due giocatori, e vale due ordini di grandezza meno dei divari di
   EV preflop.
5. La capacità del river è mal allocata, e il rango relativo al board la userebbe tutta —
   refutata: `rankriver` dà 0,986 contro 0,500. Difetto trovato nella variante (indice ordinale
   invece che percentile), ma soprattutto il clustering globale **allinea** lo stesso indice alla
   stessa forza su board diversi, e quell'allineamento è ciò che rende usabile una chiave cieca al
   board. La capacità non toccata per board ne è il prezzo, non uno spreco.
6. La capacità dei bucket è la leva — refutata: 500/1.000/2.000 rende l'1,8 % su HU20, con il
   sotto-allenamento escluso in anticipo (58.160 righe contro le 184.528 già allenate bene).

### Misure utili prodotte lungo la strada

- **Censimento dell'occupazione**, esatto su tutti i board canonici: al river un board occupa una
  mediana di 18 bucket su 1.000 (**1,8 % della capacità**) e una mano condivide il bucket con 53
  altre sullo stesso board, pesando per mano. Flop 24,5 %, turn 9,4 %.
- **Decomposizione in tre**: l'interazione preflop-postflop vale il 91 % dell'exploitability su
  HU20, il 76 % su HU30, il 65 % su CO40. Nessuno dei due livelli è sbagliato da solo.
- **Disaccoppiamento EV/exploitability**: su otto rappresentazioni l'exploitability si muove fra
  0,12 e 1,80 mentre l'EV di CO si muove fra 0,00009 e 0,08. L'astrazione è simmetrica: costa
  pochissimo in valore e moltissimo in exploitability, e CFR ottimizza il valore.

### Cosa resta, e non spetta all'agent

1. **Famiglia di feature diversa** — l'unico asse mai toccato. Stima bassa: l'EMD su istogrammi di
   equity è già la scelta potential-aware standard. Circa due ore per variante.
2. **Variante per percentile del river** — il difetto della versione fallita è identificato e
   specifico; contro un divario di 2,0× su HU20 è più difendibile che contro il 16,7× di CO40.
   Circa 75 minuti.
3. **Accettare la frontiera a 10 ante** e definire lo scopo del prodotto.
4. **Rivedere il gate** — a 3,3 % del piatto, dov'era prima di D27, HU20 passava.

## Errore riprodotto: campione riutilizzato dopo un aggiornamento alternato

`Trainer::iterate` estraeva un batch B, aggiornava il giocatore 0, ricalcolava la
strategia e aggiornava il giocatore 1 riutilizzando B. La nuova strategia di 0
dipende da B: il campione usato per valutare 1 non è indipendente dal profilo
contro cui viene calcolato il regret.

Il requisito dell'estimatore è, condizionatamente alla strategia aggiornata:

```text
E[delta_R_1(B_1; sigma_0_new, sigma_1) | B_0]
    = delta_R_1_exact(sigma_0_new(B_0), sigma_1).
```

Con `B_1 = B_0` questa uguaglianza non vale in generale. Il test permanente
`test_alternating_conditional_expectation` la verifica senza intervalli statistici:
due board equiprobabili, quattro deal disgiunti, primo board fissato ad A,
enumerazione dei due possibili board del secondo aggiornamento. Il riferimento
usa il solver scalare FiniteGame con il profilo del primo giocatore aggiornato
su A e una traversata esatta su A e B per il secondo.

Prima della correzione, a 40 ante: scarto massimo nei regret **2,58391**, scarto
nelle somme di strategia **0,015625**. Dopo: **4,44089e-16** e **0**.
Questa prova identifica un errore dell'estimatore. Non dimostra da sola che sia
l'unica causa della exploitability fisica di CO40.

## Correzione

Gli aggiornamenti alternati campionati estraggono un nuovo batch dopo il cambio
di snapshot. Gli aggiornamenti simultanei conservano il batch condiviso, poiché
il profilo è fissato prima del campionamento. Le traversate esatte conservano
l'intera lista pesata per entrambi i giocatori.

Nessun parametro nuovo. Il contatore dei board comprende entrambi i batch;
il costo di preparazione va misurato, perché si preparano due batch invece di
uno. Le traversate dei due giocatori restano due.

Il checkpoint passa alla versione 2 e l'identità del trainer a `v2`. I checkpoint
precedenti sono rifiutati: riprendere i loro regret manterrebbe la traiettoria
calcolata con il vecchio estimatore. I file di policy precedenti restano
leggibili per confronto e valutazione; non costituiscono nuove soluzioni.

## Controlli indipendenti sulle strutture profonde

Sono stati aggiunti otto runout con prefissi condivisi: due flop, due turn per
flop e due river per turn. I pesi dei runout non sono uniformi. Le due mani per
giocatore sono disgiunte dalle carte pubbliche e fra i giocatori.

| Stack | Nodi pubblici | Nodi FiniteGame | Scarto massimo regret, 25 iterazioni Linear simultaneo |
|---|---:|---:|---:|
| 40 a | 637 | 20.393 | 9,09e-13 |
| 100 a | 1.543 | 49.385 | 4,28e-11 |
| 300 a | 3.067 | 98.153 | 4,37e-11 |

Strategia media e somme coincidono entro la tolleranza dei test. EV e best
response fisica coincidono con il FiniteGame lossless entro `1e-9` sui tre
stack. Questi sono controlli dei valori, non certificati di convergenza del
gioco completo a 100 o 300 ante.

## Esperimenti su CO40 test con range interi

I confronti usano il CO40 test attuale: apertura 5 a, risposta 13 a,
una size postflop del piatto più all-in, 637 nodi pubblici. Ogni run termina a
2.000 iterazioni, batch 32, DCFR, 8 thread. La valutazione finale campiona
8 flop ed enumera tutti i loro runout. I valori seguenti sono stime in ante,
non certificazioni esatte; i flop delle valutazioni periodiche cambiano.

| Variante | Bucket F/T/R | Max gain campionato | Semilarghezza riportata | Max gain lower |
|---|---|---:|---:|---:|
| Vecchio aggiornamento alternato, bucket più fini | 500/1.000/2.000 | 0,546176 | 0,087971 | 0,238532 |
| Aggiornamento simultaneo | 200/500/1.000 | 0,945012 | 0,141510 | 0,307237 |
| Alternato con campioni indipendenti | 200/500/1.000 | 0,918223 | 0,134932 | 0,303665 |

Il difetto di campionamento è reale, ma la sua correzione non risolve da sola
CO40. La baseline storica del medesimo albero ha max gain **esatto** 0,651556 a
su 573 flop canonici e 605.088 board; non va confrontata come se avesse lo
stesso errore statistico delle tre righe sopra.

La strategia con campioni indipendenti è stata poi certificata esattamente:
max gain **0,83020566987928368 a**, NashConv **1,1585784190956665 a**,
EV **[-0,14710799240764666; 0,14710799240764674]**. La passata comprende
573 flop canonici e 605.088 board; `exact: true`, `partial: false`,
`sampled: false`. Policy `fnv1a64:d22ea072ae06a65f`. Artefatto:
`out/co40_corrected_exact.json`. Il gap residuo non è dovuto soltanto
all'errore della valutazione campionata. La correzione non migliora questa
singola traiettoria a 2.000 iterazioni rispetto alla baseline storica;
la sua necessità deriva dall'oracolo dell'estimatore, non dal benchmark.

Gli esperimenti hanno condiviso CPU con compilazioni e controlli. I loro tempi
non costituiscono benchmark di prestazioni. Log locali: `out/co40_diagnosis_fine.log`,
`out/co40_diagnosis_simultaneous.log`, `out/co40_corrected.log`.

La suite del trainer passa con **4.050.617 asserzioni**, comprese le regressioni
sui tre stack, determinismo fra thread e ripresa da checkpoint. Passano anche
le suite del certificatore (**131.148**) e dell'export (**14.205**).

## Difetto strutturale: informazioni precedenti fuse nello stesso bucket

La riga postflop è `(nodo delle puntate, bucket della street corrente)`.
Il bucket flop non identifica la classe preflop; quelli turn e river non
identificano i bucket precedenti. Due storie nella medesima riga possono quindi
avere reach del giocatore differenti. Il confronto degli aggiornamenti con
un FiniteGame che usa la stessa fusione verifica le formule implementate,
ma non ripristina l'ipotesi di perfect recall richiesta dalla garanzia CFR.

Un controesempio esatto di 11 nodi isola il problema. Chance sceglie L/R con
probabilità 1/2. Il giocatore 0 conosce L/R e sceglie `quit` (payoff zero) oppure
`enter`. Dopo `enter`, sceglie a/b: payoff L = (2, -1), payoff R = (-3, 0).
Il giocatore 1 è passivo e ha payoff opposto. La strategia `enter` in L, `quit`
in R, poi a è rappresentabile anche fondendo le due decisioni finali e vale 1.

Dopo 10.000 iterazioni Linear CFR:

| Informazioni alla decisione finale | EV del giocatore 0 | Migliore deviazione verificabile | Gap |
|---|---:|---:|---:|
| L/R dimenticato | 0,25 | 1 | 0,75 |
| L/R conservato | 1 | 1 | 2,49975e-8 |

La deviazione della prima riga è un piano esplicito con le azioni sopra,
non una best response che osserva illegalmente L/R dopo `enter`.
Regressione permanente: `test_forgotten_information_witness` in
`tests/preflop_blueprint_trainer_tests.cpp`. La prova valuta il piano di
deviazione direttamente, senza usare una best response su un gioco con
memoria imperfetta.

Una seconda prova ha pesato i regret con il reach del giocatore, usando una
copia del trainer esclusa dal prodotto. Max gain campionato a 2.000 iterazioni:
0,772083 a, lower 0,208382 a. Non risolve la convergenza e non costituisce una
correzione CFR con garanzie; non è stata applicata al motore.

### Un caso concreto nella policy CO40

Il diagnostico `out/co40_memory_witness.cpp`, applicato alla policy corretta
a 2.000 iterazioni, trova 20.397 celle flop/bucket che riuniscono combo con
reach preflop diversi. Dopo CO call e BTN check, sul flop `7c Tc Ac`,
il nodo 4 e il bucket 127 assegnano la stessa strategia a:

| Combo | Probabilità di call alla radice |
|---|---:|
| `6c 7d` | 0,0005515467368 |
| `8c 8d` | 0,9898615682088 |

Il bucket dimentica una distinzione che ha già determinato una decisione
del giocatore. Le due combo preferiscono entrambe check nella continuazione
valutata: questo esempio dimostra la perdita di memoria, non una preferenza
opposta fra queste due mani.

Aggregando tutte le combo sullo stesso flop, il bucket 178 dà invece bet
come azione preferita nella somma controfattuale e check quando le combo
sono pesate con il loro effettivo reach preflop. La differenza condizionata
fra le due azioni è 0,1002959309 a. È un contributo locale con continuazione
fissa, non l'exploitability dell'intero gioco. Nel CFR con perfect recall
l'esclusione del reach proprio è corretta perché quel reach è comune
all'information set; qui questa ipotesi viene meno. Il diagnostico non
giustifica sostituire CFR con il precedente tentativo di pesare i regret
con il reach proprio. Output riproducibile: `out/co40_memory_witness.log`.

## Rappresentazione con memoria: prova conclusa

> **Esito (2026-09-19).** Conclusa e refutata come leva. `recall32` (184.528 righe di river)
> certifica 0,491631 a contro lo 0,500181 di `class` (41.973): quattro volte la rappresentazione
> per l'1,7 %. `classprev1` (765.243) peggiora a 2,430784 per sotto-allenamento. Gli artefatti
> binari dei due bracci (45,9 GB, albero `fnv1a64:9066044f8c0f0f59`, non più riprendibili dopo il
> cambio di albero) sono stati cancellati su autorizzazione dell'utente il 2026-09-18; sorgenti,
> log e certificati restano in `out/recall_full` e `out/recall32`. Quanto segue è il registro
> dell'indagine.

La combinazione completa `(classe, flop, turn, river)` produce già
7.509 / 137.349 / 465.514 prefissi distinti su 5.000 board fisici campionati.
Una tabella densa di quel tipo non è una soluzione di memoria accettabile
senza ulteriori misure.

Il prototipo in `out/recall_probe.cpp` costruisce partizioni gerarchiche:
ogni figlio appartiene a un solo genitore, a partire dalla classe preflop.
Con quattro figli per genitore e un campione deterministico di 8.192 board
per costruire le partizioni, produce 324 / 1.272 / 4.638 righe. Ogni street
usa soltanto i bucket della street corrente e delle precedenti.

Le prove sui giochi ridotti a 40, 100 e 300 ante coincidono con il riferimento
scalare e con la best response lossless entro `1e-9`. A 2.000 iterazioni su
CO40 test con range interi il max gain campionato è 3,59519 a, con
semilarghezza 0,359565 a e max gain lower 1,14575 a: la partizione provata è
troppo grossolana per sostituire quella esistente. Conservare la memoria
rende applicabile l'argomento CFR al gioco
astratto; rimane da misurare l'errore dell'astrazione nel gioco fisico.
Il prototipo è escluso dal prodotto. I suoi file di policy diagnostici non
sono intercambiabili con quelli ordinari, poiché non incorporano la mappa
gerarchica; un'integrazione dovrà versionare e serializzare tale mappa.

Una prova distinta mantiene tutti i bucket di base e separa le classi
preflop, senza ricordare gli altri bucket precedenti. L'enumerazione esatta
delle tabelle produce 7.585/21.638/41.973 righe e 405.658.776 byte di stato.
A 2.000 iterazioni: max gain campionato 0,594497 a, semilarghezza 0,092207 a,
max gain lower 0,176209 a. Il risultato resta sopra soglia; non è una soluzione
generale, né una rappresentazione con perfect recall. Log:
`out/co40_class_probe.log` e `out/co40_class_probe_2000.log`.

La prova successiva (`out/recall32`) mantiene tutte le 7.585 coppie
classe/bucket flop e partiziona turn e river all'interno del rispettivo
genitore. Una riga figlia identifica sempre il genitore; non usa carte
future. La costruzione deterministica usa 16.384 board e al massimo otto
figli per genitore: 7.585/58.221/184.528 righe, con indici a 32 bit.
Sono 1.572.896.088 byte per regret, somme di strategia e policy su CO40.
I confronti ridotti con il riferimento scalare e la best response lossless
passano. Questa prova resta esclusa dal prodotto; i risultati del run con
range interi e del certificato esatto sono riportati sotto.

A 250 iterazioni, max gain campionato 0,802754 a, semilarghezza 0,207732 a,
lower 0,225114 a. L'interruzione della sessione ha lasciato il checkpoint
a 250 iterazioni e quello del certificatore ordinario a 464 flop. Il
2026-09-17 entrambi sono stati ripresi dopo aver verificato che i processi
precedenti erano terminati.

Dopo la ripresa, a 500 iterazioni: max gain campionato 0,839891 a,
semilarghezza 0,155998 a, lower 0,221515 a. Il run continua fino a 2.000
iterazioni per confrontarlo allo stesso punto delle altre prove. Log:
`out/recall32/co40_batch_resume500.log`, `out/recall32/co40_2000.log`.

Il profiling del prototipo sulle prime cinque iterazioni attribuisce
7,05 secondi su 18,71 al ricalcolo completo della policy. Una copia
diagnostica aggiorna solo le righe che il batch può leggere, prima di
ogni passata, mantenendo il campionamento indipendente negli aggiornamenti
alternati. Dopo cinque iterazioni reali CO40, lo stato coincide bit per bit
con la versione precedente: `fnv1a64:1992fb5755449446`. Passano anche i
confronti CO40 con il solver scalare (scarto massimo regret 9,09e-13) e la
best response lossless. I tempi sono misure di profiling con altri lavori
attivi, non benchmark isolati. Log: `out/recall32/profile.log`,
`out/recall32/batch_equivalence.log`, `out/recall32/batch_tests_co40.log`.

Il run `recall32` si è concluso a 2.000 iterazioni: max gain campionato
0,709805 a, semilarghezza 0,188537 a, lower 0,183623 a. Rimane sopra soglia.
Le valutazioni intermedie aggiuntive cambiano la sequenza dei flop di
valutazione rispetto al baseline; i valori puntuali non sono un confronto
a campioni appaiati. La certificazione esatta della policy finale è completa:
573 flop canonici e 605.088 board, EV (-0,14282722397; +0,14282722397),
gain (0,49163135911; 0,24524335470) a, NashConv 0,73687471380 a.
Il max gain esatto scende da 0,83020566988 a del baseline corretto a
0,49163135911 a; resta oltre sedici volte la soglia D3 di 0,03 a.
File: `out/recall32/co40_2000_exact_DIAGNOSTIC_ONLY.json`.
Questa singola traiettoria non separa l'effetto della memoria da quello del
nuovo raggruppamento turn/river e dalla variabilità del training campionato.

Il certificatore dedicato è stato controllato sulla policy a 500 iterazioni:
il fingerprint della tabella salvata e riletta coincide
(`fnv1a64:50d9abacc533c76e`); con otto flop e seed 123 la misura coincide
con l'output arrotondato del trainer entro `1e-6`. Non si deduce una
tolleranza più stretta dai soli log con sei cifre significative. File:
`out/recall32/policy_roundtrip_train.log` e
`out/recall32/policy_roundtrip_certify.json`.

L'enumerazione completa dei prefissi dei bucket esistenti, senza nuovi
raggruppamenti né un corpus campionato di costruzione, produce
7.585 / 222.865 / 4.248.476 righe. Le tre tabelle dense in float64
richiederebbero 31.138.638.936 byte, esclusi mappe e temporanei.
Il conteggio attraversa tutte le 369.072 storie canoniche e le relative
combo valide: `out/full_prefix_count.cpp` e `out/full_prefix_count.log`.

Il prototipo `out/recall_full` conserva questi prefissi. Per renderlo
eseguibile sul desktop prepara snapshot delle sole righe lette dal batch,
regret e somme in float32 con calcoli dei valori in float64, discount DCFR
applicato al primo aggiornamento di ogni riga e I/O progressivo. La precisione
ridotta richiede confronti numerici dedicati; questa implementazione è
diagnostica e non è stata integrata nel prodotto. La conservazione della
memoria resta distinta dall'errore dell'astrazione rispetto al gioco fisico.

I controlli iniziali del prototipo passano sul solo CO40, con supporto
chance esplicito e prefissi senza ulteriore clustering: 335.327 asserzioni.
Il confronto scalare a 25 iterazioni usa la formula di discount `t-1`
del trainer. Scarti massimi assoluti DCFR: regret `2,68461e-6`, somme
`3,09764e-7`; Linear CFR: regret `5,61522e-4`, somme `1,52588e-5`, entro
la tolleranza relativa `1e-5`. La best response contro la stessa policy
coincide con il riferimento lossless entro `1e-9`. Ripresa, thread count
1/2/4/8 e partizione dei sottoalberi producono stati bit-identici; la policy
media salvata progressivamente coincide bit per bit con quella in memoria.
Questi controlli verificano il prototipo sul corpus ridotto, non dimostrano
la convergenza sul mazzo intero. Log: `out/recall_full/tests_co40_v2.log`.

La prima esecuzione fisica CO40 raggiunge 250 iterazioni in 170,023 secondi
di training. Tabelle e timestamp occupano 12.691.588.728 byte; il processo
usa circa 13,84 GB. Il controllo preventivo richiede almeno 1 GiB libero
oltre allo stato previsto. Tempi con altri controlli attivi, non benchmark
isolato. Checkpoint: `out/recall_full/co40.bin`; log:
`out/recall_full/co40_250.log`. La valutazione fisica su 32 flop, seed 123,
restituisce max gain 3,3969522128 a, semilarghezza 0,2566565328 a e lower
1,0244867272 a. È una stima diagnostica, non una qualificazione. File:
`out/recall_full/co40_250_sample32_DIAGNOSTIC_ONLY.json`.

Un controllo successivo a 1.000 iterazioni campionate confronta lo storage
nuovo con il trainer float64 a discount esplicito, sugli stessi board,
seed e information set di un corpus CO40 ridotto. La prima versione ha
errore relativo massimo nelle somme `2,80616e-5`, oltre il limite di prova
`1e-5`; nelle frequenze l'errore massimo è `4,07835e-6`. Accumulare gli
incrementi del batch in float64 prima di scrivere float32 riduce i due
errori a `1,30942e-5` e `1,02216e-6`, ma il primo resta sopra il limite.
Non si è allargata la tolleranza per dichiarare il controllo passato.

La revisione successiva usa l'identità
`S_t = t^(-gamma) * sum(k^gamma * deltaS_k, k=1..t)` e conserva la somma
pesata; il fattore comune si cancella nella normalizzazione della policy.
Questo evita di arrotondare a ogni iterazione il prodotto del discount
per tutte le somme. Il nuovo formato e l'identità del checkpoint distinguono
questa rappresentazione dalle precedenti. I file a 250 iterazioni sopra
restano quelli della prima versione diagnostica.

La revisione con somma pesata passa gli stessi 335.327 controlli CO40 e il
confronto campionato a 1.000 iterazioni. Su 74.545 celle: scarto regret
massimo `6,61243e-7`, somme `5,89400e-5` assoluto e `1,30343e-6` relativo,
frequenze `2,22214e-7`. Il max gain del gioco ridotto è `1,48363346497e-7`
contro `1,48363257901e-7` del riferimento float64. File:
`out/recall_full/tests_co40_weighted_v3.log`,
`out/recall_full/precision_lazy32_weighted_v3.log`,
`out/recall_full/precision_eager64.log`,
`out/recall_full/precision_comparison.json`.

Il nuovo run fisico CO40 riparte da zero perché il significato dello stato
salvato è cambiato. Obiettivo diagnostico: 2.000 iterazioni con la stessa
sequenza di board del baseline e successiva valutazione fisica. Checkpoint
`out/recall_full/co40_v3.bin`; log `out/recall_full/co40_2000_v3.log`.
Questa prova è ancora in corso e non risolve, da sola, il requisito Nash.

## Correzioni dei contratti diagnostici

Un corpus limitato di board con range interi cambia la distribuzione delle
carte private del trainer: le carte bloccate dal corpus vengono escluse.
Il valutatore, invece, usa la distribuzione preflop fisica completa per i
terminali preflop. Non sono lo stesso gioco. Una prova a 96 board e range
interi ha mostrato EV di somma diversa da zero pur riportando `exact: true`;
le sue misure non sono state usate come evidenza di convergenza.

`estimate_exploitability` restituisce ora `unsupported_board_prior` per i
corpora diagnostici che non rispettano il contratto verificato: tutte le
combo dei sottoinsiemi devono restare vive su ogni board e ogni combo deve
avere lo stesso numero di avversarie compatibili all'interno del proprio
giocatore. Anche i sottoinsiemi privati campionati dal catalogo senza un
corpus esplicito non sono coperti da questo valutatore. Il training fisico
ordinario, senza hook, mantiene il proprio comportamento. La CLI riporta
il motivo del rifiuto.

Questo controllo limita le modalità diagnostiche dichiarate supportate;
non implementa la valutazione di distribuzioni private condizionate
arbitrarie. Il nuovo confronto fra bucket e rappresentazione lossless usa
120 combo per giocatore sui ranghi T/J/Q/K, compatibili con tutti i board
della prova, e 96 immagini di quattro runout sotto tutte le permutazioni
dei semi. Resta un gioco ridotto di carte, non CO40 sull'intero mazzo.

Su questo gioco coerente, dopo 100 iterazioni il max gain esatto è
0,00767342 a con la rappresentazione lossless e 0,0276253 a con i bucket;
entrambe raggiungono la soglia dello 0,03 a. La prova non dimostra che i
bucket impediscano sempre la convergenza. Log:
`out/co40_lossless_private_corpus.log` e `out/co40_bucket_private_corpus.log`.

L'identità dei checkpoint diagnostici prima conteneva solo il numero dei
board e la presenza di sottoinsiemi. Ora comprende carte, pesi normalizzati
e maschere delle mani: corpus diversi della stessa dimensione non possono
condividere i regret tramite una ripresa. Le regressioni coprono modifiche
di board, probabilità e supporto privato.

## Riferimenti per le garanzie

Resta da isolare l'interazione fra discount e campionamento su righe rare.
Il trainer applica il discount DCFR a ogni iterazione globale, anche quando
un information set non compare nel batch. Con beta zero, un regret negativo
si dimezza a ogni iterazione; un'assenza di venti iterazioni lo riduce di
un fattore 1.048.576. Questo fatto non dimostra un bias dell'estimatore o
una mancata convergenza asintotica, ma può cambiare l'efficacia del training
quando la rappresentazione diventa molto più fine.

La sezione «Discounted Monte Carlo CFR» di
[Brown e Sandholm, 2019](https://arxiv.org/pdf/1809.04040) verifica una
variante ispirata a LCFR, con discount per periodi di nodi attraversati.
Non è una validazione diretta della combinazione DCFR(1,5; 0; 2) e
campionamento pubblico usata qui. Un confronto successivo dovrà mantenere
fissi rappresentazione, board e lavoro effettuato per distinguere questo
effetto dalla perdita di memoria e dalla copertura insufficiente. È
un'ipotesi da verificare, non un nuovo bug dichiarato risolto.

La memoria imperfetta richiede condizioni aggiuntive per trasferire le
garanzie CFR: [Lanctot et al., 2012](https://arxiv.org/abs/1205.0622).
La conservazione della memoria in un gioco astratto non certifica da sola
la qualità nel gioco originale. Esistono algoritmi di raffinamento con
garanzie, ma richiedono contratti e aggiornamenti ulteriori rispetto al
trainer attuale: [Čermák et al., 2020](https://arxiv.org/abs/1803.05392).
Nessuno di questi algoritmi è dichiarato implementato da questa correzione.
