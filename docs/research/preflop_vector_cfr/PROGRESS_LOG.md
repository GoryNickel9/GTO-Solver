# Diario dell'agent coder: solver preflop vettoriale

Roadmap: [PREFLOP_VECTOR_CFR_CODER_ROADMAP_2026-09-15.md](../PREFLOP_VECTOR_CFR_CODER_ROADMAP_2026-09-15.md)
Registro decisioni: [PREFLOP_ARCHITECTURE_DECISION_LOG.md](../PREFLOP_ARCHITECTURE_DECISION_LOG.md)

Regole del diario: le voci non si cancellano; una correzione è una nuova voce che rimanda alla
precedente. Un fallimento si registra prima di tentare la correzione. Aggiornare a fine di ogni
sessione, a ogni gate e a ogni dubbio bloccante.

## 1. Stato corrente

| Campo | Valore |
|---|---|
| Fase in corso | P9: diagnosi e correzione generale della convergenza; CO40 non qualificato |
| Ultimo gate | P8 PASS (2026-09-16) |
| Branch di integrazione | `feature/preflop-blueprint` |
| Branch di fase | `codex/fix-preflop-deep-stack-convergence` |
| Worktree | `C:/Users/GoryNickel/Documents/GitHub/GTO-Solver`; risorse precedenti lette da `C:/tmp/gtosd-preflop-blueprint/out` |
| Commit di partenza | `main` a `55ed6ef`; il tag `preflop-legacy-es-2026-09-15` è su `04aa687` |
| Build | `out/build/windows-release-main-integration` (Release, MSVC) |
| Merge su `main` | eseguito dall'utente il 2026-09-16 (`97d8121`, tag P3/P6/P8); il completamento di P8 (viewer) è unito nell'integrazione e in `main` con lo stesso mandato; `main` non è pushato (non richiesto); correzione EV e size HU10 5a/8a unite in integrazione (`f047484`) e in `main` (`9c68a63`) il 2026-09-16, branch di fase e integrazione pushati |
| Gate di accettazione | **0,03 a, l'1 % del piatto iniziale** (D27, decisione dell'utente del 2026-09-18, stringe lo 0,1 a provvisorio di D2). Unico gioco qualificato: **HU10** a 0,003981 a |
| Prossimo passo | HU20 con `class` vale 0,060957 a, il 2,03 % del piatto: manca il gate di **2,03 volte**, ed e il bersaglio piu vicino. Il salto richiesto e della stessa taglia di quello gia ottenuto da base a `class` su HU20 (2,11 volte). CO40 con `class` vale 0,500181 a, 16,7 volte il gate, ed e il minimo di una curva che risale: nessun budget di iterazioni lo qualifica (voci del 2026-09-18 sera, correzione e frontiera) |

## 2. Registro dei gate

| Fase | Esito | Data | Commit | Report |
|---|---|---|---|---|
| P0 Contratto e scaffolding | PASS | 2026-09-15 | `ef6f691` | [P0_SCAFFOLDING.md](P0_SCAFFOLDING.md) |
| P1 Canonicalizzazione e cataloghi | PASS | 2026-09-15 | `ca80dab` | [P1_CANONICAL_BOARDS.md](P1_CANONICAL_BOARDS.md) |
| P2 Risorse esatte | PASS | 2026-09-15 | `9f8a6d3` | [P2_EXACT_RESOURCES.md](P2_EXACT_RESOURCES.md) |
| P3 Clustering e tabelle bucket | PASS | 2026-09-15 | `c7bb762` | [P3_BUCKET_TABLES.md](P3_BUCKET_TABLES.md) |
| P4 Modello di gioco e albero compilato | PASS | 2026-09-15 | `89f159e` | [P4_GAME_MODEL.md](P4_GAME_MODEL.md) |
| P5 Kernel vettoriale HU | PASS | 2026-09-15 | `738e361` | [P5_VECTOR_KERNELS.md](P5_VECTOR_KERNELS.md) |
| P6 Trainer con campionamento del board | PASS | 2026-09-16 | `c5ccef1` | [P6_TRAINER.md](P6_TRAINER.md) |
| P7 Certificatore board-major | PASS | 2026-09-16 | `01b7ca4` | [P7_CERTIFIER.md](P7_CERTIFIER.md) |
| P8 Export, query, comparatore, viewer | PASS | 2026-09-16 | `cdd3481`, `b41cee2` | [P8_EXPORT.md](P8_EXPORT.md) |
| P9 Qualificazione CO40 e archiviazione | NOT_RUN | | | |
| P10 Conteggio alberi 3-way | NOT_RUN | | | |

Esiti ammessi: `PASS`, `FAIL`, `INCONCLUSIVE`, `NOT_RUN`.

## 3. Diario

### 2026-09-19 — P9 — normalizzazione validata indipendentemente

Fatto: aggiunti probabilita dell'ingresso e guadagno condizionato opzionale;
campi storici e metriche globali invariati. Il fattore 630 della tabella storica
"Le quattro ipotesi cadute" e confermato per i range uniformi completi.
Le perdite corrette CO sono 1,172575 / 1,372247 / 1,421924 / 0,730602 ante
per ingresso sotto il prior del diagnostico. Non sono una decomposizione della root.

Comandi: CTest `windows-release` per certifier, trainer ed export; ricalcolo
CO40 dal checkpoint del certificatore su una copia locale dei 573 flop salvati.
Risultati: certifier PASS (138.974 assert, 42,46 s), trainer PASS (76,66 s),
export PASS (48,80 s). CO40 ricalcolato in 1,4004197 s, max_gain identico
0,82717651588212859. [Report e artefatto](NASH_AUDIT_2026-09-19.md).
Fallimenti: l'include del nuovo test e stato corretto prima della build verificata;
il precedente CTest su eseguibile obsoleto non e contato in questi risultati.
Dubbi: rapporti condizionati per range ristretti o cataloghi parziali non validati,
quindi `conditional_gain` e `null` in quei casi. Nessuna nuova qualificazione CO40.
Prossimo passo: separare perdita della strategia comune e costo dell'aggregazione.

### 2026-09-19 — P9 — audit della normalizzazione e piano causale autorizzato

Fatto: l'utente ha assegnato come unico goal della giornata il piano in cinque fasi:
normalizzazione indipendente, diagnostica dei conflitti nei bucket, verifica enumerabile
del meccanismo, candidato potential-aware offline solo se sostenuto dai risultati,
confronti matched HU20/CO40. Worktree isolato `nash-convergence-audit/GTO-Solver`,
branch `codex/nash-convergence-audit`, base `744113c69342a82f3b920add498106af2b763d52`.
Il checkout principale e le risorse precalcolate restano invariati.

Risultati iniziali: il campo `opponent_reach` somma pesi su combo e non e una probabilita.
Il ricalcolo dei certificati indica un fattore 630 nella tabella del 2026-09-18
"Le quattro ipotesi cadute". La verifica indipendente con codice di test e ancora in corso.
Il rapporto condizionato sara esposto solo con catalogo completo e range uniformi completi;
per range ristretti la distribuzione dei board richiede una verifica separata.

Fallimenti: prima compilazione del nuovo test, MSVC C2039/C2065 sul simbolo
`ca::preflop_hand_classes`: manca l'include che lo dichiara. Il successivo CTest,
avviato prima di controllare l'esito della compilazione, usa il vecchio eseguibile:
il suo risultato non valida i nuovi test. Correggere l'include e ricompilare prima del nuovo CTest.

Gate: normalizzazione indipendente NOT_RUN; nuova astrazione NOT_RUN; nessuna nuova
qualificazione o affermazione di convergenza.
Prossimo passo: completare la compilazione e confrontare il diagnostico con l'enumerazione
indipendente delle coppie disgiunte.

Formato di ogni voce:

```text
### AAAA-MM-GG — Px — titolo breve
Fatto: ...
Comandi: ...
Risultati: numeri, tempi, memoria, fingerprint
Fallimenti: cosa, causa identificata o ipotesi, cosa si è provato
Dubbi: ...
Prossimo passo: ...
```

### 2026-09-19 - la capacita dei bucket non e la leva: tre assi esauriti, il divario e della famiglia

Fatto: costruite per la prima volta le tabelle a **500/1.000/2.000**, il secondo candidato che la
roadmap nomina ("confronto matched su 3 seed") e che non era mai stato eseguito - su disco esisteva
una sola cartella di tabelle. Misurato `class` su HU20, il bersaglio piu vicino al gate.

## La costruzione

41 minuti, stessi parametri di clustering del report P3 (10 riavvii, 10 iterazioni di screening, 25
massime, campione 500.000), stesso seed di partizione, stesse feature e stesse distanze: l'unica
variabile e il numero di gruppi.

| Street | Capacita | Distanza media dal centroide | vs 200/500/1.000 |
|---|---:|---:|---|
| Flop | 500 | 119,0 | era 150,7, **-21 %** |
| Turn | 1.000 | 6,12 | era 8,12, **-25 %** |

Il clustering e genuinamente piu fine, non solo piu numeroso. Nessun bucket vuoto; il turn ha
converso in 11 iterazioni invece di fermarsi al limite di 25.

## Il confondente, escluso prima della misura

Il rischio dichiarato era il sotto-allenamento: `classprev1` era esploso a 2,43 con 765.243 righe
di river. Le righe di `class` con la capacita nuova sono **58.160** al river (71.196 -> 99.904 in
totale), cioe **1,4x**, non 2,5x come la capacita grezza, perche `class` conta solo le coppie
(classe preflop, bucket) realizzate. `recall32` si era allenato bene a 184.528 righe con lo stesso
budget, quindi 58.160 e comodamente dentro la zona allenabile e l'esito misura l'astrazione.

## L'esito

| | 200/500/1.000 | 500/1.000/2.000 |
|---|---:|---:|
| HU20 con `class`, esatta | 0,060957 | **0,059870** |
| Guadagno | - | **1,8 %** |
| x il gate (0,03) | 2,03 | 2,00 |

Criterio fissato prima della misura: sotto 0,045 la capacita e una leva e si prova 1.000/2.000/4.000;
sopra 0,058 non lo e. **0,059870 sta sopra 0,058.** Refutata.

## Tre assi indipendenti, tutti esauriti

| Asse | Escursione provata | Resa migliore |
|---|---|---:|
| Memoria della chiave | 1.000 -> 765.243 righe river | +1,7 % (`recall32`) |
| Capacita dei bucket | 200/500/1.000 -> 500/1.000/2.000 | **+1,8 %** |
| Iterazioni | 1.000 -> 10.000 | banda del 7 %, minimo interno a 2.000 |

Ogni parametro della famiglia, spazzato in entrambe le direzioni, rende qualche punto percentuale.
HU20 richiede **2,00x**, CO40 **16,7x**. La regolarita e troppo consistente per essere casuale: il
divario e **strutturale alla famiglia di astrazione** - k-means su istogrammi di equity al flop e
al turn, OCHS al river, chiave a memoria imperfetta - e non a un suo parametro.

Fallimenti: nessuno nuovo. L'ipotesi era dichiarata con un criterio quantitativo prima della misura
e il criterio ha deciso contro di essa.
Dubbi: (1) Resta non provata l'unica famiglia alternativa: feature diverse. La stima e bassa
(l'EMD su istogrammi di equity e gia la scelta potential-aware standard, non una svista) e il costo
e circa due ore per variante. (2) La variante per percentile del river resta non provata. Contro un
divario di 2,00x su HU20 e un candidato piu serio di quanto fosse contro il 16,7x di CO40, ma il
bilancio su questa linea e cinque ipotesi su cinque cadute.
Prossimo passo: decisione dell'utente. Le misure non indicano piu un parametro da girare. Le
opzioni sono: investire in una famiglia di astrazione diversa (progetto di ricerca, non un
pomeriggio), accettare la frontiera a 10 ante e definire lo scopo del prodotto, o rivedere il gate.

### 2026-09-18 (notte) - la frontiera: `class` su HU20 e HU30, e il gate stretto all'1 % del piatto

Fatto: misurato `class` su HU20 e HU30, che erano stati certificati solo sulla baseline. Serviva a
stabilire fin dove il prodotto qualifica, invece di continuare a tentare ipotesi su CO40.
Certificazioni esatte, 605.088 board, 2000 iterazioni, stesso trainer e stesso certificatore che
hanno prodotto lo 0,500181 di CO40. Controllo passato: `class` riporta 7585/21638/41973 righe,
identiche a prima della variante `rankriver`, quindi i quattro numeri sono confrontabili.

| Gioco | Stack | baseline | `class` | Guadagno | % del piatto | **x gate (0,03)** |
|---|---:|---:|---:|---:|---:|---:|
| HU10 | 10 a | 0,003981 | - | - | 0,13 % | **passa** |
| HU20 | 20 a | 0,128849 | **0,060957** | 52,7 % | 2,03 % | 2,03x |
| HU30 | 30 a | 0,571773 | **0,324082** | 43,3 % | 10,80 % | 10,8x |
| CO40 | 40 a | 0,827177 | 0,500181 | 39,5 % | 16,67 % | 16,7x |

Il guadagno di `class` **decresce con la profondita**: 52,7 -> 43,3 -> 39,5 %. Monotono sui tre
giochi.

## Il gate e cambiato: D27

Durante la lettura delle soglie e emerso che D2 (0,1 a) era dichiarata "soglia fisica **iniziale**"
e "**da stringere** quando l'astrazione migliora", mentre l'1 % del piatto e lo standard con cui e
stato accettato il prodotto postflop. L'utente ha deciso il 2026-09-18 di stringere il gate a
**0,03 a, l'1 % del piatto** (D27). La colonna di destra della tabella e contro quel gate.

## Verdetto del criterio fissato in anticipo

Prima di vedere HU30 era stato scritto: sotto ~0,15 un ulteriore 2x lo porta sotto 0,1 e il lavoro
sull'astrazione si ripaga; sopra ~0,3 nemmeno un raddoppio basta. **0,324082 sta sopra 0,3.** Un 2x
lascerebbe HU30 a 0,162, che falliva perfino il vecchio D2, e contro il gate nuovo servono 10,8x.

## La frontiera

La frontiera del prodotto sta **fra 10 e 20 ante**, e HU20 e l'unico bersaglio raggiungibile:
manca il gate di **2,03x**, che e la taglia di un salto gia ottenuto una volta sullo stesso gioco -
da baseline a `class` sono 2,11x. HU30 (10,8x) e CO40 (16,7x) chiedono piu di quanto qualunque
cambio di rappresentazione abbia mai reso in questo programma.

Fallimenti: (1) Previsto HU30 a 0,271 estrapolando il guadagno di HU20; il valore vero e 0,324
perche il guadagno di `class` decresce con la profondita, cosa che i tre punti mostrano
chiaramente e che una previsione a un punto solo ignorava.
Dubbi: (1) HU20 e certificato sulla **fixture di test**: una sola size postflop (100 % del piatto).
`hu20_full_v1` con le tre size non e mai stata costruita ne misurata. Il precedente di HU10
(ridotto 0,003981, completo 0,0039949, +0,4 %) e evidenza debole, perche a SPR 0,42 le size non
possono contare mentre a SPR 1,25 si. (2) Le 2000 iterazioni sono il minimo della curva **di
CO40**; la curva di HU20 non e mai stata misurata, e HU20 ha piu campioni per riga a parita di
iterazioni, quindi il suo minimo potrebbe cadere piu avanti. Parte del 2,03x potrebbe venire dal
solo punto sull'asse delle iterazioni.
Prossimo passo: misurare la curva iterazioni/exploitability di HU20 (un training con dump ai
raddoppi piu quattro certificazioni esatte, circa un'ora) e costruire `hu20_full_v1`. Sono le due
cose che non richiedono di indovinare un meccanismo.

### 2026-09-18 (notte, correzione) - D3 non e un gate: e la regola di arresto del training

Rilevato dall'utente. D2 e D3 sono stati usati come se fossero due soglie di accettazione. Non lo
sono, e la roadmap li distingue esplicitamente (sezione 2.1):

| | Definizione nella roadmap | Cosa e |
|---|---|---|
| **D2** | "Soglia fisica: massimo guadagno per giocatore <= 0,1 ante per mano" | **gate di accettazione** |
| **D3** | "Arresto del training: massimo guadagno <= 1 % del pot iniziale, stimato sui board campionati" | **regola di arresto**, implementata come `stima + semiampiezza <= 0,03a` |

D3 e ancorata al piatto e indipendente dallo stack, che e il modo standard di esprimere la
convergenza Nash. Non dice se un risultato sia accettabile: dice quando smettere di iterare.

## Cosa va corretto nelle voci precedenti

Le voci restano, come impone la regola del diario. Correzioni:

1. **"16,7 volte D3" come misura del fallimento** (voci del 2026-09-17 e del 2026-09-18 sera e
   notte, e la tabella della matrice con la colonna `x D3`). Il fallimento si misura contro **D2**:
   `class` a 0,500181 e **5,0 volte** il gate. Il rapporto con 0,03 resta un numero vero ma non e
   un verdetto.
2. **"Rivedere la soglia D3 per gli stack profondi"**, proposta come una delle due strade rimaste
   in tre voci. Proposta priva di senso: allentare una regola di arresto fa smettere di allenare
   prima, non fa qualificare niente. Ritirata. Le strade sono: lavorare sull'astrazione, rivedere
   **D2**, o restringere lo scopo del prodotto.
3. **"D3 si irrigidisce di quattro volte con la profondita"** (voce del 2026-09-18 sera, ripetuta
   all'utente). Il conto era 0,03 a rapportato allo **stack** - 0,3 % a 10 ante contro 0,075 % a 40.
   Ma il criterio e rapportato al **piatto**, che in questa struttura vale sempre 3 ante, quindi non
   si irrigidisce niente. Problema inventato misurando contro un metro che il progetto non usa.

## Posizione corretta, contro D2

| Gioco | Miglior misura esatta | D2 = 0,1 a | Fattore |
|---|---:|---|---:|
| HU10 | 0,003981 | **passa** | 25x sotto |
| HU20 | 0,128849 (baseline) | fallisce | 1,29x |
| HU30 | 0,571773 (baseline) | fallisce | 5,7x |
| CO40 | 0,500181 (`class`) | fallisce | **5,0x** |

HU20 manca il gate del 29 % sulla sola baseline, quindi `class` puo portarlo sotto: e la misura in
corso.

## Discrepanza fra regola scritta e codice

La roadmap dice che D3 va valutata "**nel gioco astratto**". `Trainer::meets_stop_rule` e applicata
al risultato di `estimate_exploitability`, che e il valutatore **fisico** su flop campionati.

Conseguenza concreta: la curva del 2026-09-18 sera mostra che l'exploitability fisica di CO40 non
si avvicina mai a 0,03 e risale dopo 2.000 iterazioni, quindi la regola di arresto implementata
**non puo scattare** su questo gioco. Tutti i log di training di oggi finiscono con
`PREFLOP_BLUEPRINT_TRAIN=ITERATION_LIMIT`, nessuno con `CONVERGED`. Se fosse applicata come
scritta, D3 sarebbe esattamente la misura che il 2026-09-18 si e tentato di costruire con il
regret residuo e poi abbandonata.

Fallimenti: (1) Due soglie con nomi simili usate come sinonimi per un'intera giornata, senza mai
aprire la definizione. (2) Su quella confusione e stata costruita e proposta all'utente una strada
d'azione inesistente ("rivedere D3"), e un problema inventato sulla rigidita della soglia con la
profondita. Nessuno dei due errori sarebbe sopravvissuto alla lettura di una riga di roadmap.
Dubbi: (1) La discrepanza fra D3 scritta ("nel gioco astratto") e implementata (fisica campionata)
non e risolta: non e chiaro se sia una scelta deliberata mai annotata o una deriva. Serve una
decisione dell'utente, perche D3 e fra le soglie non modificabili senza di essa (roadmap 2.1).
Prossimo passo: invariato, misurare `class` su HU20 e HU30 per stabilire la frontiera contro D2.

### 2026-09-18 (notte) - P9 - il river per rango relativo al board fallisce, e spiega cosa faceva il clustering

Fatto: censita l'occupazione dei bucket per board su tutti i board canonici, e provata una variante
`rankriver` che al river usa la posizione di rango della mano sul proprio board invece dell'indice
del cluster globale. Refutata: 0,986028 contro 0,500181 di `class`, peggio anche della baseline.

## Il censimento (misura valida, resta)

| Street | Capacita | Bucket occupati per board (mediana) | Quota usata | Mani per bucket, pesate |
|---|---:|---:|---:|---:|
| Flop | 200 | 50 | 24,5 % | 19,3 |
| Turn | 500 | 47 | 9,4 % | 20,7 |
| River | 1000 | 18 | 1,8 % | 53,0 |

Al river una mano e giocata identica ad altre 53 sullo stesso board, e il 98,2 % della capacita non
viene toccata. Entrambi i numeri sono esatti, su tutti i 19.998 river canonici.

## L'esperimento e il suo esito

Tenuto fisso tutto tranne la chiave del river: stesso albero, stesse fixture, stesse tabelle di
flop e turn, stesso schema, 2000 iterazioni, stesso batch e thread.

| | base | `class` | `rankriver` |
|---|---:|---:|---:|
| Exploitability esatta | 0,827177 | **0,500181** | **0,986028** |
| NashConv | - | 0,694720 | 1,471640 |
| EV di CO | - | -0,146476 | -0,137628 |
| Righe river | 1.000 | 41.973 | 37.665 |
| Stato | - | 392 MB | 362 MB |
| Secondi per iterazione | - | 0,432 | 1,206 (**2,79x**) |

## Il difetto, che e nel codice della variante

Assegnato l'indice **ordinale** del gruppo di rango (0 per il piu debole, poi 1, 2, ...) invece del
percentile. Il numero di gruppi distinti varia da 1 (board con scala servita, tutti pareggiano) a
circa 465. I nuts finiscono quindi sull'indice 17 su un board e sul 299 su un altro, e la stessa
riga di strategia serve i nuts su uno e una mano mediocre su un altro. La chiave e cieca al board,
quindi l'errore non e correggibile a valle.

## Cosa faceva il clustering, e che era stato letto al contrario

La rietichettatura per forza crescente e fatta **su tutti i board insieme**: per questo il bucket
999 e i nuts su board di texture opposta (verificato su tre). Quell'allineamento - stesso indice,
stessa forza assoluta, ovunque - e cio che rende usabile una chiave che non vede il board. La
capacita non toccata su un singolo board ne e il prezzo, non uno spreco.

Il 2,79x conferma l'altra faccia: con il river raggruppato le 465 mani vive di un board stanno in
una ventina di righe adiacenti e il kernel vettoriale legge poche linee di cache; per rango stanno
in fino a 465 righe sparse. La compressione comprava anche velocita.

Fallimenti: (1) Quinta ipotesi caduta, e la prima costruita su una misura invece che su
un'intuizione: il censimento era corretto, l'interpretazione no. (2) Strumento del regret residuo
scritto e poi abbandonato: il bound assoluto somma rumore una volta per riga e scala col numero di
righe (HU10, gioco risolto a 0,003981 fisico, riporta 0,0553 con tutte le 23.924 righe positive).
Prima della calibrazione stavo per puntarlo su CO40, dove avrebbe dato un numero grande e la
conclusione opposta a quella giusta. (3) La misura e stata proposta come decisiva senza verificare
che il suo esito cambiasse una decisione: la curva iterazioni/exploitability gia mostrava che
convergere di piu peggiora, quindi nessuno dei due esiti avrebbe cambiato il seguito. Fermata su
richiesta dell'utente. (4) Stima di 14 minuti per il training di `rankriver` presa da `class` senza
chiedersi se il costo per iterazione fosse lo stesso: sono stati 40. (5) Criterio del censimento
enunciato al contrario ("sotto la cinquantina di bucket occupati la compressione e trascurabile":
pochi bucket occupati significano piu compressione, non meno).
Dubbi: (1) La variante per **percentile** - indice = quota di mani battute, scalata alla capacita -
non e stata provata, ed e diversa da quella refutata: allinea i nuts sull'indice massimo di ogni
board e usa tutti gli indici ovunque. Il difetto identificato e specifico, ma il bilancio di
giornata su questa linea di ragionamento e cinque ipotesi su cinque. (2) Il costo di localita del
2,79x colpirebbe anche la variante per percentile, quindi anche riuscendo andrebbe pesato.
Prossimo passo: decisione dell'utente. La variante per percentile costa circa un'ora (10 minuti di
modifica, 40 di training, 25 di certificazione). In alternativa restano le due strade gia sul
tavolo: cambiare le feature del river, o rivedere la soglia D3 per gli stack profondi.

### 2026-09-18 (sera) - P9 - la curva exploitability/iterazioni ha un minimo interno: l'astrazione ha un pavimento

Fatto: misurata l'exploitability fisica **esatta** della stessa traiettoria a 1.000, 2.000, 4.000,
8.000 e 10.000 iterazioni. Nessun campionamento: 573 flop canonici per tutti i runout, 605.088
board, `max_gain_half_width` = 0 su tutti e cinque i punti. Le differenze non sono rumore.

| Iterazioni | Exploitability CO | BTN | NashConv | EV di CO |
|---:|---:|---:|---:|---:|
| 1.000 | 0,529728 | 0,204116 | 0,733843 | -0,149135 |
| 2.000 | **0,500181** | **0,194539** | **0,694720** | -0,146476 |
| 4.000 | 0,502202 | 0,201370 | 0,703572 | -0,145398 |
| 8.000 | 0,528748 | 0,211783 | 0,740532 | -0,144610 |
| 10.000 | 0,535882 | 0,217001 | 0,752882 | -0,144386 |

Due andamenti opposti sulla **stessa traiettoria**, ed e il risultato centrale:

- l'**EV migliora monotonicamente** su tutti e cinque i punti, senza mai invertire;
- l'**exploitability fisica ha un minimo interno** a 2.000-4.000 e poi peggiora monotonicamente.

CFR sta funzionando: converge nel proprio gioco astratto, e il valore lo dimostra. La soluzione di
quel gioco pero non e la soluzione del gioco fisico, e avvicinarsi alla prima allontana dalla
seconda. E la patologia dell'astrazione, mostrata qui **sull'asse delle iterazioni dentro un solo
run**, non piu confrontando rappresentazioni diverse.

Il pavimento e **0,500181**: cinque volte la soglia D2 (0,1) e 16,7 volte la D3 (0,03). Il bacino
del minimo e largo e piatto (2.000 e 4.000 distano lo 0,4 %), quindi non esiste un budget di
iterazioni da cercare meglio: nessun punto della curva si avvicina al gate. Entrambi i giocatori
peggiorano insieme dopo il minimo (CO 0,5002 -> 0,5359, BTN 0,1945 -> 0,2170), coerente con la
simmetria dell'astrazione gia registrata.

## Il test della deriva, e perche non decideva

Prima della curva, la domanda era se la media avesse converso. Salvate le policy ai quattro
raddoppi (determinismo verificato: la policy a 2.000 ha fingerprint `fnv1a64:be84f6b45d37b5b8`,
identico bit per bit a quella del run del 2026-09-17, due run indipendenti).

| Finestra | Tabella intera | Solo entries gia vive | **Root** |
|---|---:|---:|---:|
| 1.000 -> 2.000 | 0,053143 | 0,050801 | 0,012486 |
| 2.000 -> 4.000 | 0,046537 (x0,876) | 0,044507 (x0,876) | 0,007617 (**x0,610**) |
| 4.000 -> 8.000 | 0,038546 (x0,828) | 0,037603 (x0,845) | 0,005636 (**x0,740**) |

Il test dava risposte **opposte a seconda del peso**. Sulla tabella intera la deriva decade a
0,85 per raddoppio, piu lentamente dello 0,707 che avrebbe il puro rumore campionario attorno a un
punto fisso: sembra non convergere. Sul root decade a 0,61 e 0,74, cioe attorno o sotto quel
riferimento, e con ampiezza **quattro volte minore**: converge. La tabella pesa allo stesso modo
tutte le 16,3 M entries, quindi il suo numero e dominato da bucket di river quasi mai raggiunti.

Confondente esaminato e escluso: una entry mai visitata vale esattamente `1/actions`
(`trainer.cpp:844`), quindi la prima visita produce deriva che non parla di equilibrio. Vale il
10,8 %, 7,6 % e 3,8 % del totale nelle tre finestre, e toglierla non cambia il tasso di decadimento
(0,876 e 0,845 contro 0,876 e 0,828). Nota di validazione dello strumento: la quota di entries
ancora uniformi **si dimezza** a ogni raddoppio (6,65 -> 3,41 -> 1,41 %), quindi la misura rileva
un dimezzamento quando c'e; semplicemente non lo trova nel movimento della strategia.

## Difetto trovato nel driver di benchmark

`--checkpoint` e silenziosamente un no-op quando `--eval-every 0`: il salvataggio sta dentro il
ramo che scatta solo dopo una valutazione (`benchmarks/preflop_blueprint_train.cpp:291`). Il
`--resume` non ha quindi trovato nulla e i quattro passi si sono riallenati da zero, 15.000
iterazioni invece di 8.000. Non corretto: tocca un driver del prodotto e la decisione e dell'utente.

Fallimenti: (1) Il test della deriva e stato proposto come decisivo e non lo era: pesa a peso
uniforme entries di rilevanza diversissima, e la radice - l'unico blocco il cui offset si conosce
senza mappa dei nodi - si muoveva poco, cosa **gia misurata** prima di proporlo. La conclusione
"la media non si sta assestando", data all'utente a meta pomeriggio, e stata corretta poche ore
dopo dalla misura sul root. (2) Mezz'ora di calcolo persa per il no-op del checkpoint, non
verificato prima di lanciare la sequenza.
Dubbi: (1) Resta non misurata l'exploitability **dentro l'astrazione**: servirebbe una best
response ristretta ai bucket, che il certificatore non fa. Non e piu rilevante per la decisione -
il pavimento vale 0,50 al minimo e il limite a t->infinito e peggiore - ma il "CFR converge nel suo
gioco" resta un'inferenza da EV monotona e deriva del root, non una misura diretta. (2) Il root e
un nodo su 604 e i suoi rapporti sono due su un blocco di 324 slot: c'e spazio per il rumore. (3)
Il termine di interazione vale il 65 % dell'exploitability, quindi "il root converge" non implica
"converge dove conta".
Prossimo passo: **decisione dell'utente**. Il capitolo convergenza e chiuso: nessun budget di
iterazioni qualifica CO40 sotto questa astrazione. Le alternative sono cambiare famiglia di
feature (mai misurata) oppure rivedere la soglia D3 per gli stack profondi.

### 2026-09-18 — P9 — sweep sulla profondita, decomposizione della perdita, e quattro ipotesi cadute

Fatto: su richiesta dell'utente, verificato se il problema di CO40 si presenti anche a stack piu
bassi, e poi cercato il meccanismo. Il risultato utile e la sequenza di ipotesi falsificate: sono
state generate tutte prima di avere le misure giuste, e tutte e quattro sono cadute contro misure
che si potevano fare prima.

## Fixture nuove

`preflop_blueprint_hu20_test_v1.json` e `preflop_blueprint_hu30_test_v1.json`, identiche alla
variante di test CO40 tranne lo stack. Aggiunte al test di validazione dello schema. Su
indicazione dell'utente HU20 ha `response_target_units: []` come HU10: a 20 ante un 3bet a 17 a
lascia 3 a dietro in un piatto da 36 e non e distinguibile dallo shove a 19 a. Il suo albero ha
quindi 571 nodi e la stessa parte preflop di HU10 (22 nodi, 8 decisioni, 3 ingressi).

Vincolo del modello trovato per strada: l'all-in preflop e soggetto a `all_in_threshold`, il
1000 % del piatto dopo il call. Alla radice il piatto e 4 a, quindi lo shove sparisce dall'albero
sopra i **41 ante** (verificato: presente a 40 e 41, assente a 42 e 45). La finestra in cui questa
struttura e confrontabile e **18-41 ante**: sotto i 18 la risposta a 17 a supera lo stack e il
loader rifiuta la fixture, sopra i 41 l'albero cambia forma.

## Lo sweep

| Gioco | Stack | SPR dopo open+call | Nodi | **Max gain esatto** | Open di CO |
|---|---:|---:|---:|---:|---:|
| HU10 ridotto | 10 a | 0,42 | 193 | 0,003980549728196586 | 44,9 % |
| HU20 | 20 a | 1,25 | 571 | 0,12884900000000000 | 6,5 % |
| HU30 | 30 a | 2,08 | 604 | 0,57177300000000000 | 2,5 % |
| CO40 | 40 a | 2,92 | 604 | 0,82717651588212859 | 1,6 % |

L'exploitability e concentrata su CO: a HU20 il rapporto CO/BTN e **11,6 a 1**.

## Strumenti di misura aggiunti al prodotto

Tre aggiunte additive, nessuna tocca trainer, albero, policy o fingerprint. Dopo ognuna, suite
21/21 e regressione HU10 che riproduce `0.003980549728196586` alla cifra.

1. `best_response_preflop` / `gain_preflop`: la quarta casella del 2x2 che il codice gia
   calcolava per tre quarti. Deviazione **solo preflop**, con il postflop tenuto a quello del
   blueprint. Invariante nel test: `ev <= best_response_preflop <= best_response`.
2. `best_response_preflop_mix`: la strategia preflop **scelta dalla best response**, per ogni
   nodo decisionale dell'eroe, aggregata sulle 81 classi. Il vettore `choice` esisteva gia e
   veniva buttato via. `split_classes` verifica che le combo della stessa classe scelgano la
   stessa azione, come impone la simmetria dei semi: esce zero ovunque.
3. `postflop_entry_loss`: la perdita dentro ogni ingresso postflop **a reach fissato**, media a
   peso uniforme sulle combo vive invece che pesata col reach del blueprint. Serve perche
   `gain_lower` e cieco proprio dove il blueprint non va: un blueprint che evita un sottoalbero
   sembra giocarlo bene.

## Le misure

**Decomposizione in tre.** Nessuno dei due livelli e sbagliato da solo:

| Gioco | Totale | Solo preflop | Solo postflop | Interazione |
|---|---:|---:|---:|---:|
| HU20 | 0,128849 | 0,002747 | 0,008327 | 0,117776 (**91 %**) |
| HU30 | 0,571773 | 0,005802 | 0,130850 | 0,435121 (**76 %**) |
| CO40 | 0,827177 | 0,006229 | 0,282118 | 0,538829 (**65 %**) |

Correggere un livello solo recupera fra il 9 e il 35 %. Il blueprint e in un **ottimo locale**:
ogni pezzo e ottimale dati gli altri.

**La best response di CO su CO40, per nodo:**

| Nodo | Blueprint | Best response |
|---|---|---|
| radice | fold 27,4 · limp 35,8 · open 1,6 · shove 35,1 | **limp 100** |
| ha limpato, BTN punta a 5 | fold 34,8 · call 39,3 · shove 25,9 | fold 3,7 · **call 91,4** · shove 4,9 |
| ha limpato, BTN spinge | fold 70,3 · call 29,7 | fold 70,4 · call 29,6 |
| ha aperto, BTN 3betta | fold 60,7 · call 19,5 · shove 19,9 | fold 51,9 · call 46,9 · shove 1,2 |
| ha aperto, BTN spinge | fold 64,3 · call 35,7 | fold 64,2 · call 35,8 |

Dove la decisione e fold-o-call contro uno shove i due coincidono alla prima cifra: e una
decisione di sola equity, senza postflop dentro, e il blueprint la prende bene. Dove invece si
tratta di entrare in un piatto giocabile, divergono. La best response **non apre mai**: prende
flop economici con tutto. Attenzione, e uno **sfruttamento** di un BTN congelato che dopo il limp
checka il 63,7 % e non punisce mai, non una strategia di equilibrio.

**Perdita postflop a reach fissato su CO40**, per mano (diviso per il reach avversario, che e una
normalizzazione dell'analisi e non un'unita nativa del certificato):

| Ingresso | Piatto | CO | BTN |
|---|---:|---:|---:|
| limp-check | 4 | 0,001861 | 0,001797 |
| limp-bet-call | 12 | 0,002178 | 0,001818 |
| open-call | 12 | 0,002257 | 0,001800 |
| open-3bet-call | 36 | 0,001160 | 0,001337 |

HU10 per confronto: 0,000559 / 0,000087 / 0,000038.

## Le quattro ipotesi cadute

1. **L'astrazione postflop corrompe i valori preflop.** Refutata: `class` migliora
   l'exploitability del 39,5 % e la strategia preflop non si muove di un decimale
   (limp 35,8 -> 35,4 %, shove 35,1 -> 35,0 %).
2. **Lo shove cresce con la profondita.** Refutata: decresce, 59,9 -> 45,7 -> 35,1 %.
3. **La strategia media e incoerente con i propri EV.** Non supportata: solo 4 discordanze su 15
   superano un errore standard.
4. **Il postflop e giocato male, percio CO evita di entrarci.** Refutata quantitativamente: la
   perdita postflop e **piatta** fra gli ingressi (l'ingresso piu profondo e quello dove si perde
   meno), **uguale per i due giocatori** benche le loro exploitability differiscano di 2,5 volte,
   e vale circa 0,002 ante per mano contro divari di EV fra azioni preflop di 0,24. Due ordini di
   grandezza di distanza.

Perche 1 sembrava reggere e non reggeva: il valore del gioco per CO e **quasi invariante** rispetto
alla rappresentazione. Su otto rappresentazioni l'exploitability si muove fra 0,12 e 1,80 mentre
l'EV di CO si muove fra 0,00009 e 0,08; `class` cambia l'exploitability di 0,327 e l'EV di
0,00058, un rapporto di 1 a 564. L'astrazione e **simmetrica**: peggiora entrambi i giocatori, e
due giocatori handicappati uguale raggiungono all'incirca il valore giusto. Costa pochissimo in
valore e moltissimo in exploitability, e CFR ottimizza il valore.
Fallimenti: (1) Quattro ipotesi formulate prima di avere le misure che le avrebbero decise.
(2) Una frase scritta nel riassunto all'utente — "il blueprint non sa giocare a poker" — che
assumeva la qualita del postflop senza averla misurata, e che la misura successiva ha smentito.
(3) Un run HU20 scartato perche girava sulla fixture con il 3bet degenere. (4) Percentuali di BTN
citate da HU20 mentre si discuteva CO40.
Dubbi: (1) Non esiste un meccanismo che leghi i fatti sopravvissuti. Non ne viene proposto un
quinto. (2) Non esiste un riferimento esterno per CO40 sul nuovo albero, quindi "limpare il 36 %"
e giudicato assurdo senza uno standard. (3) La normalizzazione per reach avversario nella tabella
degli ingressi e una costruzione dell'analisi; i confronti robusti sono quelli interni allo stesso
certificato, cioe la piattezza fra ingressi e l'uguaglianza fra i due giocatori.
Prossimo passo: misurare se CFR abbia converso **nel proprio gioco astratto**, cosa mai fatta.
Tutte le misure di questo programma sono exploitability **fisiche**; se il regret medio residuo
fosse alto, il blueprint non sarebbe un equilibrio nemmeno della propria astrazione e tutto il
resto sarebbe a valle di quello.

### 2026-09-17 — P9 — il braccio Linear refuta l'ipotesi del discount: il muro e della rappresentazione

Fatto: ultimo esperimento della matrice. P9 registrava come aperta l'ipotesi che il discount DCFR
fosse responsabile del degrado sulle righe rare: con beta zero un regret negativo si dimezza a
ogni iterazione **globale**, anche quando la riga non compare nel batch, e venti iterazioni di
assenza lo riducono di un fattore un milione. Con 765.243 righe river e 64.000 board in 2.000
iterazioni quasi ogni riga e rara, quindi l'ipotesi prevedeva che Linear — che pesa gli incrementi
dell'iterazione t per t e non sconta i regret memorizzati — salvasse `classprev1`.

Cambiata **solo** la pesatura: update alternati, stesso albero, stesso protocollo, stessa passata
esatta. Max gain esatto su 573 flop canonici e 605.088 board:

| Rappresentazione | Righe river | DCFR | Linear | Rapporto |
|---|---:|---:|---:|---:|
| `class` | 41.973 | 0,50018061896087860 | 0,66222655337014890 | 1,324 |
| `classprev1` | 765.243 | 2,43078387921045060 | 2,62717999011574980 | 1,081 |

**L'ipotesi e refutata.** Linear non avvicina `classprev1` a `class`: resta a 2,63 contro 2,43,
cioe leggermente **peggiore**, e cinque volte peggio di `class` con entrambi gli schemi. Se il
discount fosse stato la causa, il rapporto Linear/DCFR sarebbe dovuto crollare sulla
rappresentazione fine; invece passa da 1,324 a 1,081.

Onesta sul residuo: quel calo del rapporto va nella direzione prevista dall'ipotesi — Linear e
relativamente meno penalizzato dove le righe sono rare. Ma e un effetto del 20 % su un divario di
cinque volte: esiste e non spiega il fenomeno. **Il muro e della rappresentazione**: con 64.000
board non si allenano 765.000 righe, e nessuno schema di pesatura lo compensa. Questo chiude
l'ipotesi aperta di P9 e spiega retroattivamente `recall_full`, che con 4.248.476 righe falliva
per lo stesso motivo e non per la precisione float32 o per il formato dello stato.

## Tabella finale della matrice

Nove rappresentazioni, stesso albero `fnv1a64:18d08f453034ac0f`, stesso protocollo
(2.000 iterazioni, batch 32, 8 thread, update alternati), stessa passata esatta.

| Rappresentazione | Righe F/T/R | Stato test | Stato `co40_v1` | **Max gain esatto** | % piatto | x D3 |
|---|---|---:|---:|---:|---:|---:|
| `base` (produzione) | 200/500/1.000 | 9 MB | 514 MB | 0,82717651588212859 | 27,6 % | 27,6 |
| `classf` | 7.585/500/1.000 | 22 MB | 663 MB | 0,70787939444806370 | 23,6 % | 23,6 |
| `class` @ 50/100/200 | 3.210/7.139/14.340 | 128 MB | 7,20 GB | 0,59405448398659820 | 19,8 % | 19,8 |
| `classft` | 7.585/21.638/1.000 | 104 MB | 3,19 GB | 0,58624021099899270 | 19,5 % | 19,5 |
| **`class`** | 7.585/21.638/41.973 | 374 MB | 21,13 GB | **0,50018061896087860** | 16,7 % | **16,7** |
| `class` @ 10.000 it. | idem | 374 MB | 21,13 GB | 0,53588168707754600 | 17,9 % | 17,9 |
| `class`, Linear | idem | 374 MB | 21,13 GB | 0,66222655337014890 | 22,1 % | 22,1 |
| `classprev1` | 7.585/222.865/765.243 | 5,78 GB | 361,99 GB | 2,43078387921045060 | 81,0 % | 81,0 |
| `classprev1`, Linear | idem | 5,78 GB | 361,99 GB | 2,62717999011574980 | 87,6 % | 87,6 |

Generata da `out/matrix/summary.py` leggendo i certificati.

## Conclusione

1. **Il trainer non e il collo di bottiglia.** Con perfect recall il CFR scende a 0,000011 a sul
   gioco ridotto; con la chiave di produzione si ferma a 0,027933 a. Il pavimento e l'astrazione.
2. **Dentro l'astrazione, la leva e la memoria, non la risoluzione.** Ricordare la classe preflop
   vale -39,5 % sul gioco vero; quadruplicare i bucket vale il 4,7 % e dimezzarli costa il 19 %.
3. **La memoria satura e poi collassa.** Ogni street che ricorda la classe compra una fetta simile
   (-0,119 flop, -0,122 turn, -0,086 river), ma oltre le circa 42.000 righe river la
   rappresentazione non e piu allenabile con questo budget di board e peggiora di cinque volte.
4. **Non e l'algoritmo.** Ne DCFR ne Linear cambiano il quadro; allenare cinque volte tanto
   peggiora del 7,1 %.
5. **Nessuna configurazione della famiglia si avvicina all'obiettivo.** Il campo va da 0,500 a
   2,627 contro una soglia D3 di 0,03 a. L'ottimo e `class` a **16,7 volte D3** e 5 volte D2.

Il vincolo di memoria posto dall'utente restringe ulteriormente: `class` costa 21,13 GB su
`co40_v1`, quindi la configurazione migliore della matrice non e nemmeno deployabile sul bersaglio
finale. Il miglior compromesso deployabile e `classft`, 0,586240 a a 3,19 GB.
Fallimenti: nessuno nuovo.
Dubbi: (1) La saturazione fra 42.000 e 765.000 righe river e stata osservata a 2.000 iterazioni e
64.000 board; non e noto dove si sposti aumentando i board per iterazione invece delle iterazioni,
che e l'unica variabile del campionamento non ancora toccata. (2) La matrice esplora una sola
famiglia: bucket di carte piu classe preflop. Feature diverse (equity contro range, potential-aware
al turn) restano non misurate ed erano l'opzione D del piano, mai avviata.
Prossimo passo: decisione dell'utente fra cambiare famiglia di astrazione e rivedere la soglia D3
per gli stack profondi; nessuna delle due e una decisione dell'agent.

### 2026-09-17 — P9 — matrice delle rappresentazioni: il muro dell'allenabilita fra 42.000 e 765.000 righe

Fatto: matrice di rappresentazioni postflop su `co40_test_v1`, tutte con lo stesso albero
`fnv1a64:18d08f453034ac0f`, lo stesso protocollo (DCFR alternato, 2.000 iterazioni, batch 32,
8 thread) e la stessa passata **esatta** su 573 flop canonici e 605.088 board. È la prima tabella
del programma in cui le rappresentazioni sono confrontabili fra loro. Vincolo posto dall'utente:
la rappresentazione deve stare nei 32 GB della macchina.

| Rappresentazione | Riga postflop | Righe F/T/R | Stato su `co40_test_v1` | **Max gain esatto** | Su `co40_v1` |
|---|---|---|---:|---:|---:|
| `base` | bucket corrente | 200/500/1.000 | 9,4 MB | **0,82717651588212859** | 539 MB |
| `classf` | classe al flop | 7.585/500/1.000 | 23,5 MB | **0,70787939444806370** | 696 MB |
| `class` @ 50/100/200 | classe ovunque, tabelle grossolane | 3.210/7.139/14.340 | 134 MB | **0,59405448398659820** | 7,73 GB |
| `classft` | classe a flop e turn | 7.585/21.638/1.000 | 109 MB | **0,58624021099899270** | 3,42 GB |
| **`class`** | classe ovunque | 7.585/21.638/41.973 | 392 MB | **0,50018061896087860** | 22,7 GB |
| `class` @ 10.000 it. | idem | idem | idem | 0,53588168707754600 | — |
| `classprev1` | classe + bucket precedente | 7.585/222.865/765.243 | 6,2 GB | vedi sotto | 389 GB |

**Contributo di ogni street.** Tenere la classe al flop vale −0,119, al turn −0,122, al river
−0,086. Nessun salto e nessuna saturazione: ogni street compra una fetta simile. Il river, che
costa 1.000 → 41.973 righe e quindi 3,42 → 22,7 GB sull'albero vero, è quello che rende meno.
`classft` è il miglior rapporto della matrice.

**L'asse risoluzione è chiuso.** Con le tabelle 50/100/200 la chiave `class` peggiora del 19 %
(0,594054 contro 0,500181): abbassare la risoluzione non recupera margine. Alzarla non è
praticabile, perché `class` con 500/1.000/2.000 costa 31,5 GB su `co40_v1`, cioè l'intera memoria
della macchina. Resta che 4x bucket senza memoria compravano il 4,7 % sul corpus ridotto: la
risoluzione non è la leva, in nessuna delle due direzioni.

**Il muro dell'allenabilita.** `classprev1` a 2.000 iterazioni dà max gain campionato **2,4710 a**
con limite inferiore non distorto **0,6757 a**: cinque volte peggio di `class` e **tre volte peggio
del baseline**. Il limite inferiore esclude che sia rumore dello stimatore. Anche il costo per
iterazione esplode, 3,2408 s contro 0,3941. Quindi fra **41.973 e 765.243 righe river** la
rappresentazione smette di essere allenabile con 2.000 iterazioni e 64.000 board, e aggiungere
memoria non smette semplicemente di pagare: **distrugge il risultato**. È coerente con
`recall_full` (4.248.476 righe, 1,3475 a campionato) e risponde alla domanda che P9 teneva aperta.

Comandi: `out/matrix/run_variant.py <nome> <variante> <buckets-dir> {train|certify}` e
`out/matrix/run_prev.py {train|certify}`. Probe nuovi: `out/class_probe.cpp` (famiglia
class/classft/classf, mappa densa per street) e `out/prev_probe.cpp` (mappa concatenata
(classe, bucket precedente, bucket corrente) enumerata sui cataloghi canonici).
Fallimenti: (1) Il runner `run_prev.py` scritto via heredoc ha perso i backslash doppi e non
compilava; riscritto con lo strumento di scrittura file. Un solo tentativo di riparazione, come
da regola concordata con l'utente.
Decisioni prese in autonomia, nel mandato dell'utente del 2026-09-17 per le 18 ore senza
supervisione: (1) **`coarse32` cancellato**, come da regola concordata, perché girava solo se
`classprev1` o `coarse8` avessero migliorato; con 2.793.223 righe river è ben oltre il muro.
(2) **`coarse8` declassato e braccio Linear promosso**: con 902.272 righe river `coarse8` sta
dallo stesso lato del muro e costerebbe 2-3 ore per un esito prevedibile, mentre il braccio
Linear è diventato l'esperimento a più alto valore informativo, perché testa se il crollo dipenda
dal discount DCFR. Lo scostamento dall'ordine concordato è motivato dal valore informativo, non
dal costo.
Dubbi: (1) Il migliore della matrice resta 0,500181 a, cioè **16,7 volte D3**. Nessuna variante
cambia l'ordine di grandezza: si muovono tutte fra 0,50 e 0,83. (2) Non è noto se il muro sia una
proprietà della rappresentazione o un artefatto dell'algoritmo: con beta zero DCFR dimezza i
regret negativi a ogni iterazione globale anche sulle righe non campionate, e con 765.243 righe
su 64.000 board la maggior parte delle righe è rara. È esattamente ciò che il braccio Linear
misura. (3) `classprev1` costerebbe 389 GB su `co40_v1` e non sarebbe comunque portabile.
Prossimo passo: Linear su `class` (riferimento) e su `classprev1` (test dell'ipotesi del discount).

### 2026-09-17 — P9 — conversione a 32 bit delle righe, e l'estensione a 10.000 iterazioni non aiuta

Fatto: due cose, su indicazione dell'utente che ha posto il vincolo di memoria dei 32 GB della
macchina e ha chiesto perché non passare direttamente a indici a 32 bit.

**Conversione a 32 bit.** Le capacità delle righe postflop erano `uint16_t` in `StateLayout`,
`PolicyInfo`, `TrainerConfig` e `Certificate`, più le firme di `layout_state` e `rows_for`.
Gli offset erano già a 64 bit, quindi si è mosso solo il tipo dell'indice. **Il formato su disco
non cambia**: `policy_file.cpp` scriveva già le capacità con `append_little32` e le troncava solo
in memoria. Tolta la troncatura, sparisce anche un difetto latente: una policy con 765.243 righe
river veniva riletta come 41.915 senza errori, producendo certificati plausibili e falsi — lo
stesso genere di problema del certificatore del probe, ma silenzioso, e sarebbe scattato esatto
al primo run di `classprev1`. La conversione è stata poi estesa ai probe (`lossless_probe_row`,
le righe della best response), che erano rimasti a 16 bit.

Regressione richiesta esplicitamente dall'utente, HU10 non deve rompersi. Ricertificate le policy
HU10 **esistenti**, non riallenate, con i binari nuovi:

| Fixture | Ricertificato | Registrato | Esito |
|---|---|---|---|
| HU10 ridotto | 0,003980549728196586 | 0,003980549728196586 | identico |
| HU10 completo | 0,0039948972150156414 | 0,0039948972150156414 | identico |

Identici anche NashConv e i fingerprint di policy e albero. Suite completa **76/76 PASS**, senza
adattare alcun valore atteso.

**Estensione a 10.000 iterazioni (punto A del piano).** Ripreso da checkpoint il run `class`:

| Iterazione | Max gain campionato | Limite inferiore non distorto |
|---:|---:|---:|
| 2.000 | 0,5393 | 0,1811 |
| 3.000 | 0,5827 | 0,1822 |
| 5.000 | 0,5592 | 0,1840 |
| 6.000 | 0,5167 | 0,1865 |
| 8.000 | 0,5486 | 0,1928 |
| 10.000 | 0,6007 | 0,1988 |

Nessuna tendenza al ribasso: la stima oscilla fra 0,52 e 0,65 e il **limite inferiore peggiora in
modo monotono**, da 0,1811 a 0,1988. Il limite inferiore è quello senza selezione (strategia media
al preflop, best response esatta dal flop in poi), quindi non è rumore dello stimatore naive: è la
strategia media che si allontana. È la stessa patologia del baseline, che fra 2.000 e 10.000
iterazioni era passato da 0,657 a 0,841 a sull'albero precedente.

**Conseguenza sul protocollo:** la matrice delle rappresentazioni si misura a **2.000 iterazioni**
e nessuna variante viene estesa. Training 5.359,6 s per le 8.000 iterazioni aggiuntive; policy
`fnv1a64:a83d66470e4793ac`. La certificazione **esatta** del punto a 10.000 conferma la
lettura senza passare per le stime: **0,535881687077546 a**, NashConv 0,7528823179600452 a,
17,86 % del piatto, limite inferiore 0,2002673400156187 a. Contro 0,50018061896087860 a del
punto a 2.000, allenare cinque volte tanto **peggiora del 7,1 %**. Il limite inferiore, che
non ha bias di selezione, sale da 0,18037784090724046 a 0,2002673400156187: la strategia
media si allontana davvero. File `out/class_20260917/cert10k.json`.
Fallimenti: (1) Il link del probe è fallito con `LNK1104` perché il run A teneva aperto
`co40_train_class_probe.exe`; risolto linkando la famiglia di varianti a un eseguibile distinto,
`co40_train_class_family.exe`. (2) Gli object dei probe erano stale rispetto alle firme nuove e
il link ha dato `LNK2019`: vanno ricompilati insieme, ed è stato aggiunto allo script di build.
Dubbi: (1) L'oscillazione fra 0,52 e 0,65 su stime a 20 flop ha semilarghezza circa 0,08, quindi i
singoli punti non sono distinguibili fra loro; la tendenza del limite inferiore sì. (2) Resta non
verificato se la patologia dipenda dal discount DCFR sulle righe rare: è il braccio Linear del
piano.
Prossimo passo: matrice delle rappresentazioni a 2.000 iterazioni, dalla più economica.

### 2026-09-17 — P9 — la chiave `class` su CO40 intero: 0,500181 a esatti, -39,5 % dal baseline

Fatto: portata la chiave `class` — riga postflop `(classe preflop, bucket della street corrente)`
invece del solo bucket — sul gioco intero e certificata in modo esatto contro il baseline dello
stesso albero. Il probe `out/class_probe.cpp` costruisce la mappa densa scandendo le tre tabelle
bucket, non dipende da un corpus dichiarato e quindi lascia il trainer campionare i board
normalmente. Protocollo identico al baseline: DCFR alternato, 2.000 iterazioni, batch 32,
8 thread, valutazione ogni 500 iterazioni su 20 flop.

| | baseline (`base`) | `class` |
|---|---:|---:|
| Righe F/T/R | 200 / 500 / 1.000 | 7.585 / 21.638 / 41.973 |
| Stato | 9.364.488 B | 391.977.480 B |
| Secondi per iterazione | 0,2288 | 0,3941 |
| Stima campionata a 2.000 it. | 0,8418 a | 0,5393 a |
| **Max gain esatto** | **0,82717651588212859 a** | **0,50018061896087860 a** |
| NashConv | 1,1558581520489282 a | 0,69471958737876530 a |
| Quota del piatto | 27,6 % | 16,7 % |
| Limite inferiore dal flop | 0,282118 a | 0,180378 a |
| EV | ∓0,147056 a | ∓0,146476 a |

Passata esatta su 573 flop canonici e 605.088 board in 787,9 s. Policy
`fnv1a64:be84f6b45d37b5b8`, capacità dichiarate nel certificato `[7585, 21638, 41973]`,
albero `fnv1a64:18d08f453034ac0f` uguale al baseline. File `out/class_20260917/cert.json`.

**Risultato: -39,5 %.** È il miglior valore mai ottenuto sul gioco a 40 ante con una
rappresentazione portabile in produzione. Il prototipo `recall32` aveva dato 0,491631 a, ma su
un albero diverso (`fnv1a64:9066044f8c0f0f59`, quindi non confrontabile alla cifra) e costando
1,57 GB più una mappa gerarchica da versionare e serializzare, contro 392 MB e una chiave che
in produzione è la concatenazione della classe al bucket.

**Ma il corpus ridotto aveva sovrastimato la leva.** Là la classe portava il pavimento da
0,027933 a a 0,000010 a, cioè lo azzerava; sul mazzo intero ne toglie il 39,5 %. Il dubbio
registrato nella voce precedente era esattamente questo e va considerato confermato: con un solo
flop canonico e 16 classi preflop il bucket flop era quasi costante, quindi la classe faceva un
lavoro che sul mazzo intero, con 573 flop canonici e 169 classi, il bucket flop svolge già in
parte. **La graduatoria delle varianti misurata sul corpus ridotto non è trasferibile.**

Il risultato resta **16,7 volte sopra D3** (0,03 a) e **5 volte sopra D2** (0,1 a): verdetto
REJECTED come tutte le passate esatte a 40 ante. Nessun Nash certificato.

Comandi: `out/co40_train_class_probe.exe --config benchmarks/fixtures/preflop_blueprint_co40_test_v1.json
--resources-dir out/preflop_blueprint_resources --buckets-dir out/preflop_blueprint_buckets_200_500_1000
--iterations 2000 --batch 32 --threads 8 --eval-flops 20 --eval-every 500
--checkpoint out/class_20260917/ckpt.bin --policy-out out/class_20260917/policy.bin`;
`out/co40_certify_class_probe.exe ... --threads 8 --chunk 16`.
Fallimenti: (1) Il certificatore del probe crashava con access violation senza stampare nulla:
non costruisce mai un `Trainer`, quindi nessuno chiamava `recall_initialize` e ogni risoluzione
di riga leggeva una mappa vuota. Serve anche `layout_state` con le capacità del probe invece di
quelle delle tabelle: sono le due modifiche che il prototipo `recall32` aveva già fatto al
proprio certificatore. (2) La prima correzione è stata cancellata da un `copy /y` nello script
di build che rigenerava il file appena patchato; il main del certificatore è ora mantenuto in
`out/` e la copia è stata rimossa dallo script. Senza accorgersene si sarebbe valutata la chiave
a bucket contro una policy allenata con la chiave `class`, ottenendo un numero plausibile e privo
di significato. Il cablaggio è verificato su due segnali: il certificatore stampa
7.585/21.638/41.973 righe e accetta la policy senza rifiutarla per capacità incompatibili.
Dubbi: (1) La curva di `class` stava ancora scendendo a 2.000 iterazioni (0,7053 / 0,5828 /
0,5669 / 0,5393 sulle stime campionate), mentre il baseline era piatto. Il certificato fotografa
quella traiettoria, non il suo asintoto. Attenzione però: sul baseline proseguire da 2.000 a
10.000 iterazioni **peggiorava** (0,657 a 0,841 a sull'albero precedente), quindi l'esito del
proseguimento è informativo in entrambi i sensi. (2) Non è noto se il residuo di 0,500 a sia
memoria ancora mancante (i bucket di flop e turn restano dimenticati) o risoluzione dei bucket
sul mazzo intero. Separarlo richiede di rifare lo sweep sul gioco vero con `classprev1` o
`coarse8`, che costano 6,2 e 6,6 GB e sono eseguibili su questa macchina.
Prossimo passo: proseguire il run `class` da checkpoint per distinguere plateau da traiettoria
(circa 20 minuti), e ripetere lo sweep delle varianti sul mazzo intero per attribuire il residuo.

### 2026-09-17 — P9 — quanta storia serve: basta la classe preflop, e costa 392 MB

Fatto: stabilito che il pavimento è memoria e non risoluzione, resta da capire **quanta**
storia serve, perché conservarla tutta su CO40 costa 30,3 GB e il run `recall_full` a 2.000
iterazioni aveva dato 1,3475 a campionato, peggio del baseline, per righe troppo rare.
Il probe è stato reso parametrico (`GTOSD_MEMORY_VARIANT` in `out/memory_probe.cpp`) e si
parte dalla chiave completa togliendo distinzioni, invece di partire dai bucket aggiungendone.
In parallelo `out/variant_prefix_count.cpp` conta le righe della stessa chiave sull'intero
mazzo, così ogni variante ha insieme la exploitability e il costo.

Varianti della riga postflop, tutte con le tabelle di produzione 200/500/1.000:

| Variante | Riga postflop | Plateau | Capacità su CO40 | Stato R+S+policy |
|---|---|---:|---|---:|
| `lossless` | per mano, perfect recall | 0,000011 a | — | — |
| `coarse8` | classe + bucket precedenti in 8 bande + corrente | **0,000007 a** | 7.585/87.952/902.272 | 6,6 GB |
| `classprev1` | classe + bucket della street precedente + corrente | **0,000009 a** | 7.585/222.865/765.243 | 6,2 GB |
| **`class`** | **classe preflop + corrente** | **0,000010 a** | **7.585/21.638/41.973** | **392 MB** |
| `full` | classe + tutti i bucket precedenti + corrente | 0,000011 a | 7.585/222.865/4.248.476 | 30,3 GB |
| `prev1` | bucket della street precedente + corrente, senza classe | 0,009939 a | 200/34.141/162.417 | 1,26 GB |
| `base` (produzione) | solo bucket della street corrente | 0,027933 a | 200/500/1.000 | 9,4 MB |
| `fine` | solo bucket corrente, capacità 500/1.000/2.000 | 0,026628 a | 500/1.000/2.000 | ~37 MB |

Il plateau è il minimo delle ultime cinque valutazioni su 2.000 iterazioni, valutazione esatta
sul corpus di 96 board. Log in `out/abstraction/mem_*_40.log`.

**Due risultati.** Primo: **ricordare la sola classe preflop basta**. `class` arriva a
0,000010 a, cioè il valore della rappresentazione lossless, e costa 392 MB contro i 30,3 GB
della storia completa: un settantasettesimo, per lo stesso risultato. Secondo: **è la classe a
portare l'informazione, non il bucket della street precedente**. `prev1`, che ricorda il bucket
precedente ma dimentica la classe, si ferma a 0,009939 a: tre volte meglio del baseline ma mille
volte peggio di `class`. Coerente con il testimone del 2026-09-17 sul flop `7c Tc Ac`, dove nella
stessa riga finivano `6c 7d` che chiama alla radice con probabilità 0,00055 e `8c 8d` con 0,98986:
la distinzione persa è quella che il giocatore aveva già usato nel preflop.

Comandi: `GTOSD_MEMORY_VARIANT=<variante> out/co40_train_memory_probe.exe --config
out/recall32/stack_40.json --iterations 2000 --eval-every 100 --eval-flops 24 --batch 32
--threads 2 --no-stop`; `GTOSD_MEMORY_VARIANT=<variante> out/variant_prefix_count.exe
benchmarks/fixtures/preflop_blueprint_co40_test_v1.json out/preflop_blueprint_buckets_200_500_1000`.
Fallimenti: nessuno nuovo.
Dubbi: (1) **Il corpus ridotto ha un solo flop canonico** (`6s 7d 8c` sotto le 24 permutazioni)
e 16 classi preflop invece di 169. Su quel corpus il bucket flop è quasi costante, quindi la
classe fa un lavoro che sull'intero mazzo potrebbe essere in parte già svolto dal bucket flop.
La graduatoria fra le varianti non è trasferibile così com'è: il conteggio delle righe è
sull'intero mazzo ed è reale, la exploitability no. (2) Il risultato non dice che `class` porti
CO40 sotto 0,03 a: dice che su un gioco dove il pavimento è 0,027933 a la classe lo rimuove.
L'errore di astrazione dei bucket sull'intero mazzo resta da misurare separatamente.
(3) `coarse8` e `classprev1` fanno marginalmente meglio di `class` ma costano sedici volte
tanto; la differenza fra 0,000007 e 0,000010 a è irrilevante rispetto alla soglia di 0,03 a.
Prossimo passo: portare la chiave `class` su CO40 intero e certificarla in modo esatto contro
il baseline 0,82717651588212859 a. Serve un probe che enumeri le righe sull'intero mazzo, come
fa `variant_prefix_count.cpp`, e le mappi durante il training con board campionati.

### 2026-09-17 — P9 — attribuzione del pavimento: è la memoria, non la risoluzione dei bucket

Fatto: esperimento che separa le due cause possibili del pavimento dell'astrazione misurato
stamattina. Quattro bracci sullo stesso albero (`fnv1a64:abe35f9a259e8571`), stesso corpus
dichiarato, stesso seed, stessa traiettoria, 2.000 iterazioni DCFR alternato, batch 32,
valutazione **esatta** sui 96 board del corpus. Cambia solo la riga informativa postflop.
Il braccio `memory` è nuovo (`out/memory_probe.cpp`): rimpiazza `lossless_probe.obj` al link,
così trainer e best response risolvono la riga allo stesso modo.

| Braccio | Riga postflop | Capacità | Righe usate F/T/R | Plateau |
|---|---|---|---|---:|
| `lossless` | per mano, perfect recall | — | 528 / 992 / 1.860 | **0,000011 a** |
| `memory` | classe preflop + tutti i bucket precedenti + corrente | 200/500/1.000 | 98 / 129 / 129 | **0,000011 a** |
| `bucket` | solo bucket della street corrente | 200/500/1.000 | 42 / 31 / 4 | **0,027933 a** |
| `fine` | solo bucket della street corrente | 500/1.000/2.000 | — | **0,026628 a** |

Il plateau è il minimo delle ultime cinque valutazioni; l'ultimo punto di `memory` è
0,000013 a, di `lossless` 0,000011 a. Fingerprint di stato: `lossless` `613c93cfcd0c78ad`,
`memory` `1814010557dfe66d`, `bucket` `e6e2a1e43755744c`, `fine` `9bf5757293e856bd`.

**Conclusione.** Con le **stesse** tabelle bucket di produzione, conservare la storia nella
riga porta la exploitability da 0,027933 a a 0,000011 a, cioè sul valore della rappresentazione
lossless: un fattore 2.500. Quadruplicare le capacità senza memoria la porta da 0,027933 a
0,026628 a, cioè il 4,7 %. La risoluzione dei bucket non è il collo di bottiglia; la memoria
imperfetta lo è, e da sola spiega praticamente tutto il pavimento.

Il conteggio delle righe lo mostra in modo diretto: sul corpus la chiave di produzione usa
**4 righe distinte al river**, quella con memoria 129. Non è che i bucket river siano pochi —
sono 1.000 — è che tutte le storie che arrivano allo stesso bucket river collassano insieme.

Comandi: `out/co40_train_memory_probe.exe` e `out/co40_train_private_corpus_baseline.exe` con
`--config out/recall32/stack_40.json --iterations 2000 --eval-every 50 --eval-flops 24
--batch 32 --threads 4 --no-stop`, il secondo anche con
`--buckets-dir .../preflop_blueprint_buckets_500_1000_2000`. Log in `out/abstraction/`.
Fallimenti: (1) La prima versione di `memory_probe.cpp` enumerava le righe con
`BoardContext::combo_ids()`, che elenca solo le mani vive al **river**: al flop restavano senza
riga tutte le mani uccise da turn o river, il trainer indicizzava con `no_bucket` e il processo
moriva con access violation `0xC0000005`. Corretta interrogando le tabelle bucket per board
parziale, con la maschera delle sole carte visibili a quella street.
Dubbi: (1) Il risultato vale sul corpus ridotto, dove la chiave con memoria costa 98/129/129
righe. Su CO40 intero la stessa chiave è l'enumerazione completa dei prefissi:
7.585 / 222.865 / 4.248.476 righe, 31 GB in float64, e il run `recall_full` a 2.000 iterazioni
ha dato 1,3475 a campionato, cioè **peggio** del baseline, perché le righe sono troppo rare per
essere allenate. Quindi la leva è identificata ma il problema si sposta: conservare la storia
**senza** far esplodere il numero di righe. È esattamente ciò che tentava `recall32`, che aveva
portato 0,830 a a 0,492 a. (2) Questo non dimostra che una rappresentazione con memoria
raggiunga 0,03 a su CO40 intero: dimostra che l'astrazione delle carte non è la causa e che i
bucket attuali sono abbastanza fini, non che il problema di allenabilità sia risolvibile.
Prossimo passo: cercare una chiave che conservi le distinzioni utili della storia restando
allenabile, misurando su CO40 intero contro il baseline esatto 0,82717651588212859 a.

### 2026-09-17 — P9 — baseline CO40 sull'albero nuovo: 0,827177 a, il cambio di size non sposta nulla

Fatto: su richiesta dell'utente, che ha scelto di concentrarsi sul solo gioco a 40 ante e di
sospendere le onde a 100 e 300 ante della curva dell'errore di astrazione, è stato rifatto il
numero di riferimento sull'albero preflop modificato oggi. Serviva perché tutti i certificati
CO40 precedenti valgono per l'albero `fnv1a64:9066044f8c0f0f59` e non per quello attuale.
Protocollo identico a quello storico, senza nessuna modifica: DCFR alternato, 2.000 iterazioni,
batch 32, 8 thread, valutazione ogni 500 iterazioni su 20 flop, poi passata esatta.

Comandi: `gtosd_preflop_blueprint_train --config benchmarks/fixtures/preflop_blueprint_co40_test_v1.json
--resources-dir out/preflop_blueprint_resources --buckets-dir out/preflop_blueprint_buckets_200_500_1000
--iterations 2000 --batch 32 --threads 8 --eval-flops 20 --eval-every 500
--checkpoint out/baseline_20260917/ckpt.bin --policy-out out/baseline_20260917/policy.bin`;
`gtosd_preflop_blueprint_certify --policy out/baseline_20260917/policy.bin --threads 8 --chunk 16
--state out/baseline_20260917/cert_state.bin --output out/baseline_20260917/cert.json`.

Risultati. Albero 604 nodi, 242 decisioni, stato 9.364.488 byte, fingerprint
`fnv1a64:18d08f453034ac0f`. Training 457,6 s (0,2288 s per iterazione), `converged: false`.
Stima campionata a 20 flop all'iterazione 2.000: 0,8418 a con semilarghezza 0,12 e limite
inferiore 0,2775 a. Certificato **esatto** su 573 flop canonici e 605.088 board:

| Grandezza | Valore |
|---|---:|
| Max gain esatto | **0,82717651588212859 a** |
| NashConv | 1,1558581520489282 a |
| Quota del piatto | 27,6 % |
| Quota dello stack | 2,07 % |
| Limite inferiore dal flop | 0,28211828155994961 a |
| EV | −0,14705579374932654 / +0,14705579374932351 a |

Policy `fnv1a64:c41b0fba6be10f18`, certificazione 554,8 s (9,2 min, contro i 23 min storici:
l'albero è sceso da 1.129 a 604 nodi). File: `out/baseline_20260917/cert.json`.

Confronto con il baseline corretto sull'albero precedente, stesso protocollo e stesse tabelle:
**0,83020566987928368 a** contro **0,82717651588212859 a**, cioè una differenza dello 0,4 %.
La previsione fatta prima del run era che il cambio delle size preflop non avrebbe spostato il
risultato, perché il pavimento misurato è l'astrazione postflop; il numero la conferma. Restano
valide entrambe le letture solo nel senso che il gioco è cambiato poco in exploitability, non che
i due certificati siano confrontabili come misure dello stesso gioco.

Verdetto D2 (soglia 0,1 a): **REJECTED**, come tutte le passate esatte a 40 ante. D3 (0,03 a) è
lontana di un fattore 27. Nessun Nash certificato esiste per questo gioco; l'unico gioco del
programma che raggiunge le soglie resta HU10, con max gain esatto 0,0039948972150156414 a
(0,13 % del piatto) su `r3_cert_full.json`.
Fallimenti: nessuno nuovo.
Dubbi: (1) Il baseline usa le tabelle 200/500/1.000; la misura di oggi dice che il loro pavimento
su un gioco ridotto è 0,027933 a contro 0,000011 a della rappresentazione lossless, ma non dice
quanto di questi 0,827 a sia risoluzione e quanto memoria imperfetta. Le due leve richiedono
interventi diversi. (2) Le onde 100 e 300 ante della curva sono state interrotte su richiesta
dell'utente: i punti a quegli stack non esistono e la domanda sul transfer resta aperta.
Prossimo passo: scelta dell'utente fra la leva della memoria e quella della risoluzione; ogni
intervento si misura contro 0,82717651588212859 a con lo stesso protocollo e la stessa passata
esatta.

### 2026-09-17 — P9 — albero preflop CO40: risposta 17 a e ramo limpato con re-raise solo all-in (richiesta dell'utente)

Fatto: l'utente ha chiesto due modifiche all'albero preflop CO40 e ha autorizzato
esplicitamente la modifica di **entrambe** le fixture, compresa quella principale
finora protetta come riferimento del gate P9. Ha inoltre chiesto di registrare
l'autorizzazione nella roadmap, fatto con un erratum alla sezione delle fixture e
una precisazione al «Da non fare» di P9.

Prima domanda dell'utente: «quando limpa CO, BTN raise perché è 4? Dovrebbe essere 6».
Verifica sullo stato pubblico del motore, non sulla prosa: l'etichetta dell'export conta
le fiche **aggiunte**, quindi `bet_4` significa che BTN aggiunge 4 a sopra il suo blind da
1 a e **arriva a 5 a**. Il livello di aggressione sale solo sulle azioni aggressive
(`compiled_game.cpp:164`), quindi dopo un limp BTN è ancora a livello 0 e riceve
`open_targets`. Con la formula esatta del rilancio di un piatto intero,
`P + 2B - c` (P piatto prima dell'azione, B puntata da eguagliare, c fiche già versate dal
rilanciante), il limp-raise vale `4 + 2 - 1 = 5`: il motore era già corretto. Il 6 viene
dalla scorciatoia `3 x last bet + pot`, che vale solo per `c = 0`. L'utente ha scelto la
formula esatta.

La verifica ha però trovato un errore vero: la risposta della fixture di test a **13 a**
non è un full pot sotto nessuna delle due convenzioni. L'esatta dà 17 a (BTN deve 4 a, il
piatto dopo il call è 12 a, quindi 1 + 4 + 12). Il 13 a proviene dal calcolo registrato
nella voce del 2026-09-16 («BTN paga 3 a per chiamare l'apertura, piatto 10 a»): quella
voce è sbagliata, lo stato pubblico dice 4 a da chiamare e piatto 12 a. Questa voce la
corregge.

Seconda richiesta: dopo «CO limpa, BTN rilancia» il limper deve avere solo fold, call e
all-in. I due rami raggiungono stati pubblici identici a meno di quale posto tiene quale
impegno, e `acted_players_mask` viene azzerato a ogni raise (`libs/core/src/game.cpp:558`),
quindi il ramo non è deducibile dallo stato. Il flag viene propagato dal compilatore:
`CompiledNode::limped_pot`, acceso da un call al livello 0 preflop. Non entra nel
fingerprint dell'albero, perché la differenza di comportamento è già nel fingerprint della
configurazione.

Comandi: `out\dump_preflop_tree.exe` (diagnostico nuovo, stampa la parte preflop con
piatto e impegni); `gtosd_preflop_blueprint_game --config ...`;
`gtosd_preflop_blueprint_game_tests`.
Risultati. Campo nuovo `limp_response_target_units`: opzionale, indicizzato sugli open,
assente = comportamento storico, vuoto = solo all-in. Serializzato solo quando presente,
così le configurazioni che lo precedono mantengono fingerprint e artefatti.

| Fixture | Albero | Preflop | Ingressi | Fingerprint |
|---|---:|---|---:|---|
| CO40 test (risposta 17 a, limp con re-raise solo all-in) | 604 nodi | 28 nodi, 10 decisioni | 4 | `fnv1a64:18d08f453034ac0f` |
| CO40 principale (limp con re-raise solo all-in, size convertite) | 26.878 nodi | 28 nodi, 10 decisioni | 4 | `fnv1a64:d6c10723d35b9503` |
| HU10 completo (non toccato) | 1.501 nodi | 22 nodi, 8 decisioni | 3 | `fnv1a64:bc9e7b35ad8c021d` |

Il fingerprint HU10 coincide con quello registrato negli artefatti esistenti
(`r3_chart_hu10_full.json`): le policy e i certificati HU10 restano validi. La parte
preflop CO40 non riproduce più l'albero legacy `fnv1a64:a68337fa567aa2d9`; il test congela
ora `fnv1a64:c2169c4295026609` come guardia di regressione, non come equivalenza al legacy.
Conteggi postflop CO40 principale dopo la conversione delle size: 26.854 nodi rappresentati,
9.948 decisioni, 25.852 archi (prima 27.012 / 10.060 / 25.944). Suite del modello di gioco
PASS con 797.826 asserzioni; suite completa del blueprint 19 test su 19 PASS, dopo aver
copiato nel checkout le tabelle bucket, che mancavano e facevano fallire cinque smoke per
un motivo indipendente da questa modifica.
Fallimenti: (1) La voce del 2026-09-16 che deriva la risposta a 13 a contiene un errore
aritmetico mai verificato contro lo stato pubblico; le size della fixture di test ne
dipendevano. (2) Tutte le policy, i checkpoint e i certificati CO40 esistenti sono
invalidati dal cambio di fingerprint, comprese le misure della diagnosi P9 sulla variante
di test.
Seguito, stessa giornata: l'utente ha deciso di convertire alla formula esatta anche le size
della fixture principale. I due open Monker erano entrambi decisi alla radice, dove la formula
dà 5 a, quindi collassano in una sola size; la risposta diventa 17 a. Le due fixture CO40 hanno
ora la stessa parte preflop e differiscono solo nelle size postflop (tre size 33/66/120 %
contro una sola del 100 %). Il rilancio di BTN sul limp resta 5 a, già esatto in quel nodo.
Dubbi: (1) Con le size convertite la fixture principale non corrisponde più all'albero del
riferimento Monker: il comparatore confronta a parità di albero, quindi il confronto con Monker
previsto da D1/D4 non è disponibile finché non esiste un riferimento esterno sul nuovo albero.
L'utente è stato informato di questa conseguenza prima di decidere. (2) A 300 ante l'all-in
sparisce dai nodi poco profondi perché la spinta supera la soglia `all_in_threshold` di 100.000
punti base sul piatto dopo il call: è comportamento preesistente del modello, non introdotto
qui, ma cambia la forma dell'albero fra i tre stack della curva.
Prossimo passo: rifare la curva dell'errore di astrazione a 40/100/300 ante sull'albero
definitivo.

### 2026-09-17 — P9 — errore di astrazione a 40 ante: il trainer converge, i bucket no

Fatto: misura diretta dell'errore di astrazione, mai fatta a 40 ante. Due bracci con lo
stesso albero, lo stesso corpus dichiarato, lo stesso seed e la stessa traiettoria; l'unica
differenza è la chiave postflop. Braccio lossless: righe per mano a perfect recall
(528 / 992 / 1.860 righe), con controllo esplicito che nessuna riga fonda genitori diversi.
Braccio bucket: le tabelle 200/500/1.000 di produzione. Corpus: flop `6s 7d 8c`, due turn,
due river, tutte le 24 permutazioni dei semi (96 board), 120 combo per giocatore sui ranghi
T/J/Q/K, valutazione **esatta** sul corpus. Rispetto alle due prove del 2026-09-16 è stato
aggiunto `--no-stop`: entrambe si erano fermate a 100 iterazioni sulla soglia D3, quindi il
plateau non era visibile.

Comandi: `out\co40_train_lossless_private_probe.exe` e
`out\co40_train_private_corpus_baseline.exe` con `--config out\recall32\stack_40.json
--iterations 2000 --eval-every 50 --eval-flops 24 --batch 32 --threads 4 --no-stop`.
Risultati (max gain in ante, valutazione esatta):

| Iterazione | lossless | bucket |
|---:|---:|---:|
| 50 | 0,047439 | 0,049161 |
| 100 | 0,007673 | 0,027625 |
| 650 | 0,000104 | 0,028285 (massimo) |
| 1.000 | 0,000042 | 0,027340 |
| 2.000 | **0,000007** | **0,026053** |

Il braccio lossless scende di quattro ordini di grandezza e continua a scendere; quello a
bucket sale fino all'iterazione 650 e poi scende lentamente verso 0,026 a. A 40 ante, su
questo gioco, **l'errore residuo è quasi interamente astrazione**: il rapporto fra i due
plateau è circa 3.700. Il primo punto di entrambi i bracci riproduce alla sesta cifra le
prove del 2026-09-16, quindi l'harness è deterministico.
Nella stessa sessione è stata completata la valutazione del run `recall_full` v3 (storia
completa dei bucket, 2.000 iterazioni, conclusa alle 01:42): max gain campionato su 32 flop
seed 123 **1,3475 ± 0,2536 a**, lower 0,3847 a, contro 3,3970 a di v1 a 250 iterazioni.
Migliora di 2,5 volte ma resta sopra il baseline corretto (0,8302 a esatto) e sopra
`recall32` (0,4916 a esatto). Il confronto non è omogeneo: 1,3475 è campionato e distorto
verso l'alto, gli altri due sono esatti su 573 flop. File:
`out/recall_full/co40_2000_v3_sample32_DIAGNOSTIC_ONLY.json`.
Fallimenti: (1) Il primo braccio bucket a 40 ante è morto con stack overflow
(`0xC00000FD`) all'iterazione 300 mentre il certificatore esatto occupava 10,6 GB e
paginava fuori gli altri processi; rilanciato senza `--checkpoint` e arrivato a 2.000.
Log conservato in `out/abstraction/bucket_40_crashed_at_300.log`. (2) Le onde 100 e 300
ante sono state fermate su richiesta dell'utente per cambiare prima l'albero preflop.
(3) La certificazione esatta di v3 è stata interrotta per liberare memoria; riprendibile
dal suo `--state`.
Dubbi: (1) Il corpus è ristretto (96 board, 120 combo per giocatore): il numero assoluto
0,026 a non è l'errore di astrazione di CO40 sull'intero mazzo, perché i bucket sono
costruiti sul mazzo completo e qui sono relativamente più grossolani. Ciò che si legge è il
confronto fra i due bracci, non la scala. (2) Il segnale sul transfer a 100 e 300 ante
richiede gli altri due punti della curva, non ancora misurati.
Prossimo passo: rifare i tre punti della curva sull'albero preflop definitivo.

### 2026-09-17 — P9 — precisione del prototipo con storia completa

Il primo run fisico con storia completa raggiunge 250 iterazioni, ma la
stima su 32 flop resta 3,396952 a ± 0,256657 a (lower 1,024487 a).
Non è una qualificazione. Il confronto su 1.000 iterazioni campionate di
un corpus CO40 ridotto trova inoltre un errore relativo nelle somme di
strategia di 2,80616e-5, oltre il limite di prova 1e-5. Accumulare i delta
per batch in float64 lo riduce a 1,30942e-5, ancora insufficiente.

La revisione v3 conserva `sum(k^gamma * deltaS_k)` invece di applicare
ogni volta il discount alle celle float32. È la stessa media DCFR dopo
normalizzazione. A 1.000 iterazioni, rispetto al trainer float64 con
discount esplicito: errore regret 6,61243e-7, somme 1,30343e-6 in scala
relativa, frequenze 2,22214e-7. Il controllo passa con la tolleranza
originaria; passano anche le 335.327 asserzioni CO40, la ripresa e i
confronti tra thread count. Risultati in `out/recall_full/precision_comparison.json`
e `out/recall_full/tests_co40_weighted_v3.log`.

Avviato da zero il run fisico v3 a 2.000 iterazioni. Primo checkpoint
completato a 500; log `out/recall_full/co40_2000_v3.log`, checkpoint
`out/recall_full/co40_v3.bin`. Stato con identità distinta dalle versioni
precedenti. La policy e il certificatore del prototipo restano diagnostici.

### 2026-09-17 — P9 — memoria delle street e prova senza ulteriore clustering

Il certificatore dedicato a `recall32` completa tutti i 573 flop canonici:
max gain esatto 0,49163135910804856 a e NashConv 0,73687471380326819 a,
contro max gain 0,83020566987928368 a del baseline corretto a 2.000
iterazioni. Il risultato resta sopra D3 (0,03 a); una singola traiettoria
non separa l'effetto della memoria da quello del nuovo clustering.
Certificato: `out/recall32/co40_2000_exact_DIAGNOSTIC_ONLY.json`.

Il diagnostico sulla policy baseline trova 20.397 celle flop/bucket con
reach preflop differenti fra le combo fuse. Sul flop `7c Tc Ac`, nel bucket
127, `6c 7d` chiama alla radice con probabilità 0,000552 e `8c 8d` con
0,989862. La distinzione già usata nel preflop viene dimenticata. Il report
P9 distingue questa prova strutturale dalle differenze locali di EV e
dall'exploitability del gioco intero.

L'enumerazione completa dei prefissi dei bucket 200/500/1000 produce
7.585/222.865/4.248.476 righe. Il prototipo `out/recall_full` conserva
tutte queste distinzioni, usando snapshot delle sole righe del batch,
R/S in float32, valori in float64, discount per riga e I/O progressivo.
Controlli solo CO40 su un corpus dichiarato: 335.327 asserzioni PASS,
scarto massimo regret DCFR 2,68461e-6, somme 3,09764e-7. Linear CFR ha
scarto assoluto regret 5,61522e-4 e somme 1,52588e-5, entro la tolleranza
relativa 1e-5. Best response fisica contro l'oracolo float64 entro 1e-9;
stato bit-identico a 1/2/4/8 thread, ripresa bit-identica, policy media
salvata progressivamente uguale alla versione in memoria. Il riferimento
scalare DCFR applica esplicitamente il discount t-1 del trainer prima
della traversata; la variante nominale del solver scalare usa una diversa
convenzione temporale e non era un confronto diretto valido.

Log: `out/recall_full/tests_co40_v2.log`. Avviata la prova fisica CO40 con
checkpoint; nessuna modifica del prodotto per questi prototipi, nessun
nuovo test a stack 100 o 300. La convergenza CO40 resta aperta.

### 2026-09-17 — P9 — gap residuo CO40 certificato esattamente

Completata la certificazione della policy a 2.000 iterazioni con campioni
indipendenti: 573 flop canonici, 605.088 board, max gain esatto
0,83020566987928368 a, NashConv 1,1585784190956665 a. File:
`out/co40_corrected_exact.json`. Il difetto di campionamento è corretto,
ma il problema CO40 resta aperto anche secondo la metrica esatta.

La nuova prova con memoria conserva tutte le coppie classe/bucket flop,
poi partiziona turn e river all'interno del genitore. Produce
7.585/58.221/184.528 righe e 1.572.896.088 byte di stato su CO40.
A 500 iterazioni: max gain campionato 0,839891 a, semilarghezza 0,155998 a,
lower 0,221515 a. Non è una qualificazione. Run ripreso fino a 2.000
iterazioni, con checkpoint `out/recall32/co40.bin` e log
`out/recall32/co40_2000.log`.

Per rendere praticabile la prova, il prototipo ricalcola la policy solo
sulle righe lette dal batch. Profiling eseguito prima della modifica;
equivalenza bit per bit dopo cinque iterazioni CO40 e confronto scalare
CO40 entro 9,09e-13. Codice sperimentale confinato in `out/recall32`.
Nessun nuovo test a 100 o 300 ante e nessuna modifica delle size.

### 2026-09-16 — P9 — ambito CO40 confermato: full pot più all-in

L'utente conferma una sola size postflop del 100% del piatto, con all-in
separato come nella fixture attuale. Il caso da risolvere è
`preflop_blueprint_co40_test_v1.json`; il passaggio al CO40 a tre size non
fa parte di questa attività. Questa indicazione aggiorna il piano delle
voci precedenti che prevedevano la qualificazione successiva a tre size.
La correzione deve restare generale rispetto a stack e albero delle azioni.
Con una precisazione successiva, l'utente sospende i test a 100 e 300 ante:
le prove attive e i prossimi controlli si concentrano esclusivamente su CO40.

### 2026-09-16 — P9 — campionamento alternato corretto, problema CO40 ancora aperto

Mandato aggiornato dell'utente: trovare e risolvere il problema con una regola
generale, valida anche per stack futuri di 100 e 300 ante, senza richiedere
parametri diversi all'utente finale. Nessun cambiamento delle size o delle soglie.

Correzione applicata: il secondo aggiornamento alternato campionato usa un
batch indipendente. Il precedente riuso del campione dava regret
condizionalmente distorti: scarto 2,58391 contro un riferimento esatto a due
board; dopo la correzione, 4,44e-16. Checkpoint versione 2. Oracoli su alberi
a 40, 100 e 300 ante verificano aggiornamenti ed EV/best response entro `1e-9`.

La correzione non chiude CO40 test: a 2.000 iterazioni, max gain campionato
0,918223 a (8 flop, semilarghezza 0,134932), max gain lower 0,303665 a.
Anche simultaneo e bucket più fini restano sopra soglia. Questi risultati non
escludono ogni effetto del budget o dello schema: le conclusioni categoriche
della precedente voce di diagnosi non sono dimostrate.

La chiave postflop dimentica le informazioni precedenti. Un controesempio
esatto ora riproducibile nel test `test_forgotten_information_witness` mostra
CFR fermo a gap 0,75 con tale fusione, contro 2,50e-8 conservando la memoria.
Questo dimostra una limitazione generale della rappresentazione; non prova
che spieghi da sola tutto il gap osservato in CO40.

Una gerarchia sperimentale con quattro figli per livello conserva la memoria
ma perde troppa risoluzione: max gain campionato 3,59519 a a 2.000 iterazioni.
Scartata come sostituzione del modello. Un'altra prova conserva i bucket e
separa le classi preflop: 7.585/21.638/41.973 righe, 405.658.776 byte di stato,
max gain campionato 0,729284 a a 500 iterazioni. Il miglioramento rispetto al
run base a 500 iterazioni non è conclusivo con otto flop di valutazione.
Entrambe le prove restano escluse dal prodotto.

Report, limiti e artefatti: [P9_CONVERGENCE_DIAGNOSIS.md](P9_CONVERGENCE_DIAGNOSIS.md).
Nessuna convergenza o qualificazione dichiarata.

### 2026-09-16 — P9 (diagnosi) — CO40 di test con una sola apertura full pot: la riduzione delle size non basta

Fatto: su richiesta dell'utente la fixture **di test** CO40 passa da due aperture (6 a / 10 a) e due
risposte (10,5 a / 14,5 a) a una sola apertura full pot e una sola risposta full pot. Calcolo delle
size con la stessa regola di HU10: piatto iniziale 3 a (due ante da 1 a più il blind del bottone da
1 a), CO paga 1 a per vedere (piatto 4 a) e rilancia di un piatto intero, quindi **apertura 5 a**;
BTN paga 3 a per chiamare l'apertura (piatto 10 a) e rilancia di un piatto intero, quindi
**risposta 13 a**. Lo stack (40 a) non entra nel calcolo: per questo l'apertura coincide con quella
di HU10. La fixture CO40 principale non è toccata (le size Monker restano il riferimento del gate
P9, che la roadmap vieta di cambiare, e i conteggi di P4 §4 e P5 §3 le citano). Training,
certificazione esatta ed export con lo stesso protocollo.
Comandi: `gtosd_preflop_blueprint_game --config …co40_test_v1.json`; `train …co40_test_v1.json
--iterations 2000 --batch 32 --threads 8 --eval-flops 20 --eval-every 500 --policy-out out/policy_co40t1.bin`;
`certify --policy out/policy_co40t1.bin --threads 8 --chunk 16 --output out/co40t1_cert.json`;
`export --certificate out/co40t1_cert.json --eval-flops 60 --threads 8 --postflop-tree …`.
Risultati: albero 637 nodi (256 decisioni), preflop 34 nodi / 12 decisioni /
5 entry postflop (prima 1.129 nodi, 456 decisioni, preflop 58 / 20 / 9); stato 9.7 MB;
training 7 min (0.205 s per iterazione), stima a 20 flop
all'iterazione 2.000 0,6668 ± 0,1075 a; certificato esatto
**0,6516 a** (21,7 % del piatto, 1,63 % dello stack), nashconv 1,0257 a, limite
inferiore dal flop 0,2638 a, EV di CO -0,1429 a, certificazione 12 min
(1.3 s per flop). Confronto con le due aperture (stesso protocollo, 2.000 iterazioni):
0,6569 a. Quindi **la riduzione non risolve**: da 0,6569 a 0,6516 a, cioè lo stesso ordine di grandezza, con un albero quasi dimezzato (637 nodi contro 1.129) e una parte preflop di 12 decisioni contro 20. Radice CO: all in 34.8 %, fold 33.0 %, call 31.1 %, raise 5 1.1 %; EV di radice -0,1397 a;
12 nodi preflop; albero postflop 5 entry, 244 nodi
decisionali, 560 archi; export 86 s.
Per confronto HU10 completo (1.501 nodi, stessa astrazione, stesso protocollo): 0,0040 a, 0,13 % del piatto.
Nota sulle etichette: nell'export e nel viewer la risposta compare come `raise_12` perché l'etichetta conta le fiche aggiunte dall'attore e BTN ha già 1 a di blind; la puntata raggiunta è 13 a, verificata sullo stato pubblico (dopo il call piatto 28 a e 26 a dietro a testa). Le aperture coincidono con il target perché CO non ha nulla nella puntata.
Fallimenti: nessuno.
Dubbi: (1) L'esperimento non separa le due ipotesi residue perché la risposta configurata a 13 a
tiene attivo il ramo `response_targets[index]`, che non ha oracolo esatto (scelta dell'utente fra
le due opzioni proposte). Per separarle servirebbe la variante senza risposta, con la parte
preflop identica a HU10 e l'unica differenza nello stack. (2) Con l'albero sceso a 637 nodi,
cioè meno della metà di HU10 completo (1.501), la dimensione dell'albero è definitivamente
esclusa come causa.
Prossimo passo: decisione dell'utente fra l'oracolo esatto a 40 a (punto 2 del piano) e la misura
dell'errore di astrazione a 40 a (punto 4).

### 2026-09-16 — P9 (diagnosi) — mappa della copertura: non esiste un oracolo esatto su CO40

Fatto: risposta alla domanda dell'utente "non c'è l'oracolo esatto in CO40?". Verificata riga per
riga la copertura dei test: la risposta è **no**, e la parte mancante è esattamente quella dove il
trainer produce i numeri sbagliati. Nessuna modifica al codice; note aggiunte a P4 §3, P5 §2,
P6 §3 e P7 §4.
Comandi: lettura di `tests/preflop_blueprint_{trainer,oracle,kernel,game,certifier}_tests.cpp`,
`tests/preflop_blueprint_test_support.hpp` (`oracle_boards`, `oracle_subsets`),
`libs/preflop_blueprint/src/game_model.cpp` (`action_config_at`); report del gioco sulle due
fixture (`gtosd_preflop_blueprint_game --config …`).
Risultati. Copertura attuale:

| Componente | Confronto con una sorgente indipendente | Gioco su cui gira | Tolleranza |
|---|---|---|---|
| Albero e payoff (P4) | ogni nodo decisionale contro `legal_actions` / `apply_action` del core, ogni figlio e ogni payoff | **CO40**, HU10 completa, HU10 ridotta, 3-way | uguaglianza esatta |
| Kernel per board (P5) | valori per combo contro il solver postflop (`ProductionDcfr`, solo come oracolo di test, D5) | **CO40**, tre sottogiochi **river** dalla radice pubblica (57 / 117 / 57 nodi, al massimo 3 rilanci) | `1,4·10⁻¹⁴` a |
| Policy a bucket contro la stessa strategia per mano (P5) | traversata completa dell'albero | **CO40**, un board casuale | `1e-12` |
| CFR vettoriale: regret cumulati, somme di strategia, strategia media (P6) | `solve_finite_game` sul gioco ridotto costruito come `FiniteGame` | **solo HU10 ridotta** (3 board, 6 combo per giocatore, 25 iterazioni Linear) | `1,7·10⁻¹³` / `1,4·10⁻¹⁴` / `1e-9` |
| Best response fisica non chiaroveggente (P6) | `calculate_nash_conv` del `FiniteGame` lossless | **solo HU10 ridotta** | `1e-9` |
| Passata esatta per immagini d'orbita (P7) | enumerazione fisica dei 5.984 flop per combo | **solo HU10** | `1e-12` |
| Errore di astrazione (policy a bucket contro policy per mano) | exploitability fisica delle due policy | **solo HU10** | misurato: 1–3 millesimi di ante |

Le prime tre righe girano su CO40, le altre quattro no. Ma le prime tre verificano il *gioco* e i
*kernel per board*, non il CFR: l'unica verifica dei valori del CFR vettoriale, della media e della
best response è l'oracolo `FiniteGame`, e gira solo su HU10 ridotta. Codice attraversato da CO40 e
mai confrontato con una sorgente esatta:

1. **Più di una size di apertura.** `action_config_at` al livello 0 passa l'intera lista di open a
   `target_config`; l'oracolo ne ha una sola (5 a), CO40 ne ha due (6 a e 10 a).
2. **Risposta indicizzata sull'open scelto.** Il ramo livello 1 che cerca `state.current_bet` fra gli
   open e usa `response_targets[index]` non è nel gioco dell'oracolo: dalla modifica delle fixture
   del 2026-09-16 (pomeriggio) HU10 ha `response_target_units: []` e il livello 1 passa da
   `all_in_config`. **Prima di quella modifica l'oracolo copriva questo ramo** (open 3 a / 5 a,
   risposte 6 a / 8 a): la copertura è stata persa come effetto collaterale, non dichiarato allora.
3. **Rilancio incompleto configurato.** `allow_configured_incomplete_raise` è `true` in entrambe le
   fixture, ma ha effetto solo insieme a una response target: nell'oracolo attuale non ha effetto.
4. **Livello ≥ 2 dopo un rilancio configurato.** Su CO40 è il nodo in cui l'apertore affronta la
   risposta a 10,5 a e può solo foldare, chiamare o spingere. Nell'oracolo il livello 2 esiste solo
   dopo un all-in, cioè per il ramo `facing_all_in` → `passive_config`, che è codice diverso.
5. **Profondità dei rilanci.** Il gioco dell'oracolo ha `maximum_raise_count = 1`, CO40 ne ha 4: la
   ricorsione del CFR vettoriale con più rilanci nella stessa street non è mai stata confrontata
   con un solver esatto.
6. **Postflop profondo.** Nell'oracolo, dopo open e call restano 5 a su un piatto di 11 a: una sola
   decisione effettiva per street. Su CO40 restano 33 a su un piatto di 15 a con tre street.
   L'aggregazione non chiaroveggente flop → turn → river (decisione 30) è verificata esattamente
   solo nella forma piatta di HU10.
7. **Nessuna seconda strada su CO40.** Il certificatore condivide con il trainer albero compilato,
   contesto di board e kernel: un difetto comune ai due non produce una discrepanza, produce solo
   una exploitability alta, che è quello che si osserva. Su HU10 la seconda strada esiste ed è il
   `FiniteGame` lossless.
8. **Errore di astrazione mai misurato a 40 a.** Le tabelle bucket sono costruite dalle sole carte e
   non dipendono dallo stack; la loro adeguatezza con 34 a dietro è un'assunzione, non una misura.
   Su HU10 la misura esiste (1–3 millesimi di ante) e la procedura per farla è quella di P6.

Da qui la struttura del problema: il certificato dice "questa strategia è sfruttabile per 0,657 a";
non dice se la strategia è sbagliata perché il CFR ha calcolato male i regret su quei rami (punti
1–6) o perché ha calcolato bene dentro un'astrazione troppo grossolana per 40 a (punto 8). Le due
cause richiedono interventi opposti e nessuna delle due è esclusa dai dati attuali.
Fallimenti: (1) La modifica delle fixture HU10 del 2026-09-16 (pomeriggio, richiesta dell'utente)
ha ridotto la copertura dell'oracolo esatto: i rami "seconda size di apertura" e "risposta
indicizzata", prima inclusi, ora non sono più in nessun test di valore. Va registrato come costo
non dichiarato di quella modifica: HU10 resta il gioco di validazione, ma ora valida meno.
Dubbi: (1) L'oracolo a 40 a va costruito come quello di P6 (gioco ridotto `FiniteGame` con pochi
board e poche combo) ma con la struttura preflop di CO40; la dimensione cresce con i rilanci
(`maximum_raise_count = 4`) e va misurata prima di scriverlo. (2) Se l'oracolo a 40 a passasse,
resterebbe il punto 8 e servirebbe la misura dell'errore di astrazione a 40 a, che è un secondo
esperimento indipendente.
Prossimo passo: nessuno finché l'utente non decide; il piano resta quello della voce precedente,
con i punti 2 (oracolo esatto a 40 a) e 1/4 (astrazione) come alternative da separare.

### 2026-09-16 — P9 (diagnosi) — perché CO40 test non converge e piano per la convergenza

Fatto: analisi delle tre certificazioni esatte di CO40 test (voce precedente) per rispondere
all'utente sul perché non si raggiunge l'1 % del piatto; lanciato il primo esperimento
discriminante (tabelle 500/1.000/2.000, stesso protocollo, certificazione esatta). Nessuna
modifica al codice.
Comandi: `gtosd_preflop_blueprint_train …co40_test_v1.json --buckets-dir out/preflop_blueprint_buckets_500_1000_2000
--iterations 2000 --batch 32 --threads 8 --eval-flops 20 --eval-every 500 --policy-out out/policy_co40t_b500.bin`;
`certify … --buckets-dir …500_1000_2000 --policy out/policy_co40t_b500.bin --threads 8 --chunk 16`.
Risultati (evidenze già disponibili): (1) budget escluso: da 2.000 a 10.000 iterazioni la
exploitability esatta sale (0,657 → 0,841 a); con un CFR corretto la strategia media si avvicina
all'equilibrio del gioco astratto come `1/√T`. (2) Schema di aggiornamento escluso: Linear
simultaneo, la variante verificata contro l'oracolo esatto su HU10, dà 1,086 a ed è piatto fra
500 e 2.000 iterazioni. (3) La perdita è distribuita: con il preflop fisso e best response solo
dal flop restano 0,26–0,36 a (9–12 % del piatto); il preflop è assurdo: CO limpa il 31 % (AA
97 %) e spinge 40 a con il 33 % delle mani; BTN chiama lo shove di 13 piatti con il 29 % del
range e rilancia all-in sull'open 6 a con il 26 % (sull'open 10 a 26 %): entrambi preferiscono
chiudere la mano preflop, come se il postflop valesse poco o fosse valutato male. (4) Su HU10
lo stesso codice e le stesse tabelle danno 0,13 % del piatto a 2.000 iterazioni.
Ipotesi residue: (A) **astrazione**: tabelle 200/500/1.000 identiche a HU10, dove perdono 1–3
millesimi di ante; a 40 a il postflop pesa molto di più (34 a dietro dopo l'open, tre street,
due rilanci) e la best response fisica sfrutta ogni mano nel bucket sbagliato; la exploitability
fisica può salire mentre quella astratta scende (patologia dell'astrazione, Waugh et al. 2009).
(B) **difetto del trainer sulle strutture proprie di CO40** (due open con risposte indicizzate,
rilancio incompleto, nodi di livello 2 con solo all-in, payoff fino a ±40 a): l'oracolo esatto
del trainer (P6 §3) copre solo HU10 ridotto; i test P4 su CO40 verificano albero e payoff, non i
valori del CFR vettoriale; il certificatore misura nello stesso gioco compilato, quindi un errore
nei regret di quei nodi produrrebbe esattamente questo quadro.
Piano per la convergenza (proposta, in ordine di costo):
1. Esperimento in corso: CO40 test con 500/1.000/2.000, 2.000 iterazioni, certificazione
   esatta (≈ 35 min). Discesa netta → (A); invariata → (B).
2. Oracolo esatto a 40 a: gioco ridotto `FiniteGame` con la struttura preflop di CO40 (due
   open, risposte, rilancio incompleto, livello 2) e postflop minimo; confronto di regret e
   strategia media con `solve_finite_game` come in P6 §3. È il test che manca; costa un test
   nuovo e qualche ora.
3. Fixture intermedia (stack 20 a, stesse size) per misurare come cresce la exploitability con
   la profondità e separare le ipotesi anche sul preflop.
4. Se (A): tabelle per stack profondi — più bucket (punto 1), feature diverse (distribuzione
   dell'equity contro range invece che contro mano casuale, "potential-aware" al turn), river
   senza astrazione dove la memoria lo consente; poi capacità e batch più grandi e regola di
   arresto con il certificatore esatto (22 min sull'albero di test).
5. Se (B): correzione del trainer, ripetizione di P6 con l'oracolo a 40 a, poi il protocollo
   HU10 su CO40 test.
6. Solo dopo: CO40 completo (tre size), certificazione 10,2 h a 8 thread (≈ 2 h su EPYC 7443).
Fallimenti: nessuno nuovo.
Dubbi: la variante di test (una size postflop) è più facile del CO40 completo: se non converge
questa, il completo non convergerà con lo stesso trainer e le stesse tabelle.
Prossimo passo: decisione dell'utente sull'ordine dei punti 1–5; nessun codice viene scritto
fino a quella decisione.
Nota (2026-09-16, sera): l'esperimento 1 è stato interrotto dall'utente durante il training
(nessun risultato); l'utente ha indicato che non è necessario scrivere codice. I punti 1–5
restano proposte.

### 2026-09-16 — P8 — soluzione CO40 a una size nel viewer, DCFR contro Linear su CO40, risposte su exploitability/tempo e sui nodi fuori percorso

Fatto: (1) L'utente si aspettava nel viewer anche CO40 a una size postflop: la variante di test
è stata risolta con il protocollo HU10 (DCFR alternato, 2.000 iterazioni, `B = 32`, 8 thread,
valutazione ogni 500 iterazioni su 20 flop), certificata esatta ed esportata (`out/co40t_*`).
Vista la exploitability alta e la stima campionata crescente, lo stesso run è stato proseguito a
10.000 iterazioni (`--resume`, `out/co40t10k_*`) e, come primo punto del confronto P9.1, è stato
addestrato anche Linear simultaneo per 2.000 iterazioni (`out/co40t_linear_*`), entrambi
certificati esatti ed esportati. Viewer rigenerato con tre sorgenti (HU10 completo, HU10 ridotto,
CO40 test: DCFR alternato, 2.000 iterazioni, la migliore delle tre); la navigazione postflop resta quella di
HU10 completo: generatore e server accettano un solo albero e una sola policy (decisione 48).
(2) Domanda sull'aumento della exploitability per ridurre il tempo: risposta nei dubbi qui sotto
e all'utente, senza modifiche al codice. (3) Domanda sul nodo `CO call → BTN all-in → CO` con una
strategia mentre alla radice `call` vale 0,0 %: la frequenza di limp alla radice è 3,5·10⁻⁶ di
range (massimo 5,6·10⁻⁵ per 87s); il nodo è praticamente irraggiungibile ma CFR aggiorna ogni
information set e la strategia media vi è definita (AA call 100 %, J6o fold 91 %); gli EV sono
condizionati al nodo (BTN spinge il 67 % delle mani dopo il limp). Nessuna modifica al codice.
(4) Note datate aggiunte a P4 §4 e P6 §5 sul cambio delle fixture HU10.
Comandi: `gtosd_preflop_blueprint_train …co40_test_v1.json --iterations 2000 --batch 32 --threads 8
--eval-flops 20 --eval-every 500 --policy-out out/policy_co40t_dcfr_200.bin`; `… --resume --checkpoint
out/ckpt_co40t_10k.bin --iterations 10000 --eval-every 1000 --policy-out out/policy_co40t_10k.bin`;
`… --scheme linear --update simultaneous --iterations 2000 --policy-out out/policy_co40t_linear.bin`;
`certify --threads 8 --chunk 16`; `export --certificate … --eval-flops 60 --postflop-tree …`;
`generate_chart_data.py --blueprint … (tre export) --postflop-tree out/r3_postflop_tree_hu10_full.json`.
Risultati: CO40 test (1129 nodi, 456 decisioni; 0.234 s per iterazione DCFR,
0.790 s Linear):

| Run | Training | Stima a 20 flop | Exploitability esatta | % piatto | % stack | nashconv | Limite inferiore dal flop | EV CO | Certificazione |
|---|---|---|---|---|---|---|---|---|---|
| DCFR alternato, 2.000 it. | 8 min | 0,6784 ± 0,1055 a | **0,6569 a** | 21,9 % | 1,64 % | 1,0278 a | 0,2622 a | -0,1448 a | 23 min |
| DCFR alternato, 10.000 it. (proseguimento) | 38 min | 0,9366 ± 0,1178 a | **0,8406 a** | 28,0 % | 2,10 % | 1,2467 a | 0,2978 a | -0,1459 a | 23 min |
| Linear simultaneo, 2.000 it. | 26 min (in parallelo a una certificazione) | 1,0783 ± 0,1072 a | **1,0862 a** | 36,2 % | 2,72 % | 1,5226 a | 0,3559 a | -0,1460 a | 23 min |

con DCFR alternato la exploitability esatta sale fra 2.000 e 10.000 iterazioni (la strategia media peggiora); Linear simultaneo a 2.000 iterazioni è peggiore: lo schema non è la causa principale. Confronto descrittivo con il riferimento Monker CO40 (comparatore su DCFR 2.000, verdetto `REJECTED`, contratto esterno incompleto): alla radice variazione totale media di classe 26.2 pp, errore massimo per azione 23.8 pp, differenza di EV di radice 0,159 a: CO limpa e spinge 40 a con frequenze che Monker non ha (AA limp 97 %, T9s all-in 82 %).. Per confronto HU10 completo a 2.000 iterazioni DCFR: 0,0040 a (0,13 % del piatto).
Radice CO (DCFR alternato, 2.000 iterazioni): fold 33,0 %, all in 32,6 %, call 31,4 %, raise 10 2,5 %, raise 6 0,6 %; EV di radice -0,1411 a; 20 nodi preflop; albero postflop
9 entry, 436 nodi decisionali, 992 archi. Verdetto D2 (soglia 0,1 a): REJECTED per la variante
di test con questi run; il gate P9 resta sul CO40 completo.
Fallimenti: (1) La stima del mattino "2.000–10.000 iterazioni" per CO40 era una supposizione:
con il protocollo HU10 la variante di test resta lontana dall'equilibrio (tabella sopra).
(2) La stima campionata a 20 flop di DCFR alternato cresce con le iterazioni (0,68 a a 2.000,
0,94 a a 10.000): su HU10 non era successo.
Dubbi: (1) Exploitability e tempo: il costo del training si riduce fermandosi prima (su HU10
l'1 % del piatto arriva a circa 50 iterazioni, 10 s), ma la certificazione esatta costa lo stesso
qualunque sia la soglia (35 min su HU10 completo, 22 min su CO40 test, 10,2 h su CO40 completo
a 8 thread); la regola D3 campionata con `M = 1.000` costa più della passata esatta e, per il
bias dello stimatore, di fatto richiede una exploitability vera intorno allo 0,1–0,2 % del piatto
per dichiarare l'1 %. Per fermarsi davvero all'1 % servirebbe usare il certificatore esatto come
regola di arresto (economico sugli alberi piccoli) o un limite inferiore senza bias. Su CO40 il
problema è opposto: con il protocollo HU10 non si scende sotto il 22 % del piatto.
(2) Nel viewer la classe è segnata "fuori percorso" sotto `1e-6` di reach proprio: al nodo dopo
il limp molte classi restano fra `1e-6` e `6e-5` e non sono segnate; una soglia sul reach del
nodo intero sarebbe più leggibile (non implementata: nessuna modifica richiesta). (3) La parte
postflop di CO40 test vale da sola 0,2622 a di exploitability nel run migliore: con 40 a di
stack i bucket 200/500/1.000 costruiti per HU10 possono essere un limite; P9.1 (Linear contro
DCFR, tre seed, capacità 500/1.000/2.000) e la diagnosi astrazione/algoritmo/budget restano da fare.
Prossimo passo: merge e push; P9 secondo l'indicazione dell'utente, partendo da questa diagnosi.

### 2026-09-16 — P8 — contro l'open 5a solo fold/call/all-in, scaling sui thread, curva exploitability/iterazioni, erratum sulle proiezioni del certificatore

Fatto: (1) Domande dell'utente: come essere certi della convergenza a Nash (la exploitability
esatta certificata da P7 è la distanza da un equilibrio nel gioco fisico con questo albero:
nessuna strategia guadagna più di 0,0041 a per mano contro il blueprint, 0,14 % del piatto),
differenza fra le due sorgenti del viewer (stesso algoritmo DCFR alternato, alberi diversi: tre
size postflop contro una), tempi su un AMD EPYC 7443, costo di una exploitability dell'1 % del
piatto, stato della documentazione. (2) Regola dell'utente: in HU10 contro l'open 5 a BTN ha solo
fold, call e all-in. Il loader accetta una lista di risposte vuota (nessuna size di rilancio
sopra un open: al livello 1 solo fold/call/all-in, decisione 46), schema con `minItems: 0`,
validatore Python delle fixture aggiornato, fixture HU10 con `response_target_units: []`, test
dello scaffolding (lista vuota accettata, casi di rifiuto spostati sulla fixture CO40). Alberi:
HU10 completo 1.501 nodi (584 decisioni; preflop 22 nodi, 8 decisioni, 3 entry postflop),
ridotto 193 nodi (80 decisioni). Nell'export precedente BTN usava il rilancio a 8 a contro
l'open 5 a per il 6 % del range (fold 20 %, call 19 %, all-in 55 %). (3) Scaling sui thread su
HU10 completo (i3-10100F, 4 core / 8 thread): training di 100 iterazioni a 1/2/4/8 thread;
certificatore su 8 flop con `--chunk 1` e con `--chunk 8`. (4) Curva della exploitability esatta
in funzione delle iterazioni (albero ridotto: 50, 100, 200, 400, 700, 1.000, 2.000; albero
completo: 2.000 più i punti attorno all'1 % del piatto), run di riferimento a 2.000 iterazioni
certificati ed esportati (`out/r3_*`, `out/policy3_*`), viewer rigenerato. Report:
[P8_EXPORT.md](P8_EXPORT.md) §10.
Comandi: `ctest -L "p0|p4|p6|p7|p8"`; `gtosd_preflop_blueprint_train …hu10_full_v1.json --iterations 100
--batch 32 --threads T --eval-every 0`; `gtosd_preflop_blueprint_certify …hu10_full_v1.json --uniform
--threads T --chunk 8 --flop-limit 8`; per N in 50…1000: `train …hu10_reduced_v1.json --iterations N
--eval-every 0 --policy-out out/curve_red_policy_N.bin` e `certify --policy … --threads 8 --chunk 16`;
run di riferimento come nella voce precedente (`policy3_*`, `r3_cert_*`, `r3_chart_*`).
Risultati: test 15/15 PASS (172 s). Scaling del training: 0,433 / 0,276 / 0,211 / 0,194 s per
iterazione a 1/2/4/8 thread (2,2× a 8 thread; frazione seriale ≈ 0,32 con 30 unità e 10 nodi in
alto seriali). Certificatore con `--chunk 8`: 13,3 / 7,3 / 4,2 / 3,2 s per flop (4,2× a 8 thread,
1,32× dai thread SMT); con `--chunk 1`: 13,4 s per flop a qualsiasi numero di thread, perché il
parallelismo è sui flop dello stesso chunk. Curva (P8 §10): ridotto: 0,0191 a (0,64 % del piatto) a 20 iterazioni, training 3 s; completo: 0,0111 a (0,37 % del piatto) a 200 iterazioni, training 37 s; a 2.000 iterazioni 0,0040 a (ridotto) e 0,0040 a (completo). Run di riferimento con la nuova regola: completo 1.501 nodi, training 7 min (+ 5 min di valutazioni), certificato esatto **0,0040 a** in 35 min, EV di radice 0,1325 a; ridotto 193 nodi, training 5 min, certificato esatto 0,0040 a in 3 min; albero postflop 3 entry, 576 nodi decisionali, 1396 archi. CO40 completo con chunk 8 e 8 thread: 64 s per flop canonico misurati su 8 flop con chunk 8 e 8 thread, passata esatta ≈ 10,2 h (era 22,4 h).
Fallimenti: (1) Il validatore Python delle fixture imponeva liste di open e di risposte della
stessa lunghezza anche con risposte vuote: test dello schema FAIL, corretto. (2) **Erratum sulle
proiezioni del certificatore**: le misure con `--chunk 1` (CO40-TEST, voce precedente: 8,5 s per
flop, passata esatta 1,35 h) e con `--chunk 2` (CO40 completo, P7 §5: 141 s per flop, 22,4 h)
avevano rispettivamente uno e due thread attivi, non otto; a 8 thread con chunk ≥ 8 il costo è
circa 4,2 (da chunk 1) e 2,3 (da chunk 2) volte minore. CO40-TEST: ≈ 2,0 s per flop, passata
esatta ≈ 20 min. CO40 completo: 64 s per flop canonico misurati su 8 flop con chunk 8 e 8 thread, passata esatta ≈ 10,2 h (era 22,4 h) (decisione 47). (3) Lo script di
interruzione della catena ha terminato anche il proprio lanciatore (il pattern sul nome dello
script compariva nella sua riga di comando): catena rilanciata separatamente.
Dubbi: (1) Il training scala poco (2,2× su 4 core): la parte alta seriale e le 30 unità di HU10
limitano il parallelismo; su CO40 completo (211 unità) la frazione parallela è maggiore ma non
misurata. (2) La stima per l'EPYC 7443 assume una velocità per core simile all'i3-10100F (Zen 3 a
3,6–4,0 GHz con IPC maggiore contro Comet Lake a 4,1–4,3 GHz) e scaling del certificatore
lineare sui flop con chunk ≥ thread: va verificata sulla macchina. (3) La curva exploitability /
iterazioni è misurata su HU10: su CO40 la forma può essere diversa (albero più profondo).
Prossimo passo: merge del branch di fase nell'integrazione e in `main`, push dei branch; P9 sul CO40 completo (passata esatta ≈ 10 h a 8 thread su questa macchina) o sulla variante di test, secondo l'utente.

### 2026-09-16 — P8 — EV condizionato corretto fuori dalla radice, size preflop HU10 solo 5a/8a, CO40 di test

Fatto: (1) L'utente ha chiesto perché `J6o` compare nel nodo `CO raise 5a → BTN all-in → CO`
del viewer se CO non rilancia mai J6o. L'export riporta la strategia media di tutte le 81 classi a
ogni nodo: alla radice CO rilancia a 5 a J6o con frequenza 3,1·10⁻⁹ (residuo delle prime
iterazioni nella media DCFR), che è il reach mostrato dal viewer (3,12·10⁻⁷ %); la riga è quindi
attesa. L'analisi ha però trovato un errore negli EV fuori dalla radice: l'EV di classe usava il
valore controfattuale `v_a[h]` (già moltiplicato per la reach avversaria `D[h]`) pesato di nuovo
con `D[h]`, invece di `v_a[h] / D[h]`; alla radice `D[h] = 1` e il test di ricostruzione
passava, ai nodi interni l'EV era scalato per `D[h]` (fold −3,39 a per J6o e −3,14 a per AA nello
stesso nodo invece della perdita costante). Correzione in `preflop_action_values` (valore e serie
per flop dell'errore standard, decisione 43) e nuovo test: a ogni nodo interno con arco di fold
l'EV di fold di ogni classe è uguale al payoff di fold entro `1e-9`. (2) Decisione dell'utente:
le size preflop di HU10 diventano solo full pot (open 5 a, risposta 8 a) in entrambe le fixture
(completa e ridotta); test dello scaffolding e del gioco, smoke di query (`raise_5,call`) e
fixture aggiornati. (3) Decisione dell'utente: variante CO40 solo per i test con una size postflop
(100 % del piatto) più all-in, fixture `preflop_blueprint_co40_test_v1.json`
(`PREFLOP-BLUEPRINT-CO40-TEST-001`, stesse size preflop Monker 6 / 10 a e 10,5 / 14,5 a,
decisione 44), misure di tempo su 20 iterazioni e 2 flop canonici. (4) Riaddestramento HU10
(completo e ridotto) con le nuove size, certificazione esatta, export delle chart e dell'albero
postflop, rigenerazione del viewer. (5) Viewer: le classi con reach proprio lungo la history sotto
`1e-6` restano nella griglia ma desaturate, con il reach nel tooltip (decisione 45, commit
`21617ef` del viewer). Report: [P8_EXPORT.md](P8_EXPORT.md) §9.
Comandi: `cmake --build … --target <16 eseguibili blueprint>` e `ctest -L "p0|p4|p6|p7|p8"`;
`gtosd_preflop_blueprint_game --config …co40_test_v1.json`; `gtosd_preflop_blueprint_train …co40_test_v1.json
--iterations 20 --batch 32 --threads 8 --eval-every 0`; `gtosd_preflop_blueprint_certify
…co40_test_v1.json --uniform --threads 8 --chunk 1 --flop-limit 2`; per HU10 completo e ridotto:
`train --iterations 2000 --batch 32 --threads 8 --eval-flops 20 --eval-every 500 --policy-out out/policy2_*.bin`,
`certify --threads 8 --chunk 16 --state out/r2_state_*.bin`, `export --certificate out/r2_cert_*.json
--eval-flops 60 --threads 8` (chart e albero postflop); `generate_chart_data.py --blueprint out/r2_chart_hu10_full.json
--blueprint out/r2_chart_hu10_reduced.json --postflop-tree out/r2_postflop_tree_hu10_full.json`.
Risultati: test 15/15 PASS (191 s). CO40-TEST: 1.129 nodi (456 decisioni, 20 preflop) contro
27.012 (10.060) di CO40; stato 16,9 MB con 200/500/1.000; training 0,223 s per iterazione
(`B = 32`, 8 thread; CO40 completo ≈ 3 s, HU10 completo 0,27 s); certificazione 8,5 s per flop
canonico (CO40 completo 141 s): passata esatta su 573 flop ≈ 81 min (1,35 h) contro 22,4 h;
valutazione campionata D3 con `M = 1.000` ≈ 2,4 h (più della passata esatta, come su HU10), con
`M = 200` ≈ 28 min. Stima della risoluzione di CO40-TEST: 2.000 iterazioni 7,5 min, 10.000
iterazioni 37 min, più valutazioni periodiche (`M = 20` ≈ 3 min ciascuna) e certificazione esatta
di 1,35 h: fra 1,5 e 2,5 h in totale contro circa un giorno per CO40 completo (non è noto quante
iterazioni servano a CO40 per D3: HU10 ne ha richieste 2.000). HU10 con le nuove size:
completo 1.567 nodi, training 7 min (+ 4 min di valutazioni), certificato esatto **0,0041 a** in 31 min; ridotto 259 nodi, training 5 min, certificato esatto 0,0040 a in 4 min; EV di radice 0,1322 a (completo) e 0,1321 a (ridotto); export con badge `CERTIFIED_EXACT`; Esempio (export completo, nodo `CO raise 5a → BTN all-in → CO`): fold −6,000 a per J6o e −6,000 a per AA (il payoff di fold, costante), call −2,642 a per J6o e 4,528 a per AA. Albero postflop esportato: 600 nodi decisionali, 1.444 archi, 5 entry.
Fallimenti: (1) `LNK1104` su `gtosd_preflop_blueprint_export.exe`: il worker `--serve` del viewer
teneva aperto l'eseguibile; server fermato prima della build. (2) Lo script di build della catena
non ricompilava `gtosd_preflop_blueprint_scaffold_tests` (elenco parziale di target) e il test
vecchio falliva sulle nuove fixture: lo script costruisce ora tutti i 16 eseguibili blueprint.
(3) L'errore dell'EV non era coperto dai test: il controllo di ricostruzione era solo alla radice,
dove `D[h] = 1`; gli export `p8_chart_hu10_*` e la verifica del viewer di P8 §4–§5 hanno EV
fuori dalla radice sbagliati (frequenze, EV di radice, badge e verdetti corretti): superati dagli
export `r2_*` (P8 §9).
Dubbi: (1) Il "da non fare" di P9 (non cambiare albero o size) riguarda la qualificazione: la
variante CO40-TEST serve alle misure di tempo e alle prove; il gate P9 resta sull'albero CO40
completo salvo diversa indicazione dell'utente. (2) Con una sola size postflop l'albero CO40 di
test ha 24 volte meno nodi del completo: le proiezioni non si trasferiscono linearmente alle tre
size. (3) Le size preflop HU10 dell'utente (solo 5 a / 8 a) rendono i risultati HU10 di P6–P8 non
confrontabili con i nuovi: i vecchi restano nei report come storia.
Prossimo passo: merge del branch di fase nell'integrazione e in `main` (autorizzazione del
2026-09-16), push dei branch; P9 sul CO40 completo o sulla variante di test secondo l'utente.

### 2026-09-16 — P8 — viewer aggiornato e domande risolte, gate PASS

Fatto: l'utente ha risposto alle domande aperte: Q1 branch pubblicati su origin (10 branch
`feature/preflop-blueprint*` e 3 tag), Q2/Q3 merge dell'integrazione in `main` eseguito nel suo
working tree (`97d8121`, tag `preflop-blueprint-p3-abstraction` su `981361e`,
`preflop-blueprint-p6-hu10` su `22e1015`, `preflop-blueprint-p8-export` su `9360836`; `main` non
pushato: non richiesto), Q4 viewer modificabile. Nel repository: export dell'albero pubblico
postflop nello schema del viewer, sonda per nodo nel valutatore, worker di query postflop
(`--serve`) con frequenze, reach ed EV per classe (esatti a turn e river, runout campionati al
flop), test e smoke. Nel repository del viewer (branch `feature/preflop-blueprint-p8-export`,
modifiche locali preesistenti conservate in `56cdf31`, aggiornamento `4edbb45`): generatore con
sorgenti blueprint e badge, frontend con azioni e gioco dinamici, server con backend blueprint,
validatori generici, README. Report: [P8_EXPORT.md](P8_EXPORT.md) §5.
Comandi: `ctest -L p8 -V`; export dell'albero HU10; `generate_chart_data.py --blueprint … --postflop-tree …`;
`serve_viewer.py --backend blueprint …`; verifica nel browser integrato.
Risultati: `ctest -L p8` 7/7; worker: EV di classe al flop uguale al valutatore entro `1e-9`,
flop 0,4–1,3 s (16–64 runout), turn 0,5 s esatto, river istantaneo; viewer: "HU 10a Chart Viewer",
badge `CERTIFIED EXACT · exploitability 0.0042a (0.14% pot)`, 20 nodi preflop e albero postflop
(9 entry, 792 nodi) navigabili, query sul board `Ac Kd Qh` con frequenze ed EV per classe.
Fallimenti: (1) `reference?.ev !== null` nel viewer con riferimento assente: eccezione nel render
della matrice, guardia aggiunta. (2) Il flop con tutti i 1.056 runout costa 17,6 s nel worker
(sonda sequenziale sui turn): il viewer usa runout campionati; parallelizzazione rinviata.
(3) Le commit `b41cee2` e `9d05224` sono nate direttamente sul branch di integrazione
invece che sul branch di fase (checkout non tornato sul branch dopo il merge): il branch di
fase è stato riallineato a `9d05224` con fast-forward e la deviazione da D21 è registrata qui.
Dubbi: (1) Le size preflop di HU10 (open 3 a / 5 a, risposte 6 a / 8 a) vengono dal fixture di
calibrazione legacy `hu_preflop_hu10_calibration_v1.json` (D6, commit `04aa687`) e non sono state
scelte in questo programma: HU10 è solo il gioco di validazione, CO40 usa il contratto Monker
(6 a / 10 a, 10,5 a / 14,5 a). (2) La stima di 22 h per la certificazione esatta di CO40 riguarda
solo il certificatore P7 (605.088 board a 1,07 s per board per thread, dominati dai kernel all-in
di flop e turn): il training CO40 costa circa 3 s per iterazione (10 volte HU10), quindi ore, non
giorni; le riduzioni possibili sono descritte in P7 §5.
Prossimo passo: P9 sul branch `feature/preflop-blueprint-p9-co40`.

### 2026-09-16 — P8 — export, query, comparatore, viewer, gate PASS con riserva (viewer INCONCLUSIVE)

Fatto: id stabili di azione e di nodo (`action_labels`), query della policy per history + combo +
board senza ricalcolo di feature (`policy_query`), valori per azione ai nodi preflop con EV
condizionato per classe ed errore standard sui flop campionati
(`BestResponseEvaluator::preflop_action_values`), export `gtosd.preflop_blueprint_chart.v1` con
schema JSON, quattro fingerprint, badge `ESTIMATED` / `CERTIFIED_EXACT` e checksum nel layout
letto dal generatore del viewer, comparatore con verdetto sulla exploitability fisica (D2) e
distanze Monker descrittive (`EXTERNAL_CONTRACT_INCOMPLETE`), validatore statico registrato in
CTest, eseguibili `gtosd_preflop_blueprint_export` (export e query) e
`gtosd_preflop_blueprint_compare`, smoke a catena tramite fixture CTest. Report:
[P8_EXPORT.md](P8_EXPORT.md).
Comandi: `ctest -L p8 -V`; export di HU10 ridotto e completo dalle policy P6 con i certificati
P7 (60 flop, 8 thread); export senza certificato e baseline Linear; query di esempio;
comparatore candidato/baseline/riferimento; validatore; suite `preflop_blueprint`.
Risultati: test 20.380 asserzioni PASS (812 nodi decisionali etichettati, 160 cammini di query
su tutte le strade uguali al `BoardContext`, EV di radice ricostruito dalle classi entro `1e-9`,
verdetti del comparatore, parser Monker a distanza zero su un riferimento sintetico); export HU10
completo certificato: 20 nodi, 1.620 righe, badge `CERTIFIED_EXACT` con 0,0042 a esatti, 239 s
per 60 flop; comparatore `QUALIFIED` per il candidato certificato, `INCONCLUSIVE_ESTIMATE` per lo
stesso senza certificato (stima 0,061 ± 0,05 a contro 0,1 a), `STALE_TREE` fra alberi diversi,
DCFR contro Linear sullo stesso albero confrontati su 20 nodi; validatore PASS sui tre export.
Suite `preflop_blueprint`: PASS: 23/23 in 505 s (`ctest -L preflop_blueprint`, Release, dopo la correzione del check di isolamento).
Fallimenti: (1) helper JSON `quoted` in conflitto con `std::quoted` per ADL; (2) target
`nlohmann_json` non visibile nella directory dei test; (3) cammini casuali del test di query
troppo brevi per il river; (4) smoke con `DEPENDS` non eseguiti fuori etichetta: fixture CTest.
Dubbi: (1) l'EV per azione è condizionato al nodo (diviso per la reach avversaria): nei nodi
profondi con reach piccola gli errori standard sono grandi a 60 flop; il viewer dovrebbe mostrare
la reach. (2) La strategia corrente non è nel file di policy: se il viewer la vuole, va aggiunta
al formato `GTOSDPOL` (opzionale, diagnostica). (3) Il riferimento Monker CO40 non è confrontabile
con HU10 (azioni diverse); il confronto ha senso solo in P9.
Prossimo passo: decisione dell'utente su Q4 (viewer) e Q2/Q3 (merge in `main`); poi P9 sul
branch `feature/preflop-blueprint-p9-co40` con la passata esatta CO40 pianificata come lavoro a
chunk ripristinabile (22 h stimate) o ridotta con le ottimizzazioni indicate in P7.

### 2026-09-16 — P7 — certificatore board-major, gate PASS

Fatto: valutatore di best response riorganizzato in due stadi (`BestResponseEvaluator`: valori
per flop, aggregazione preflop) con immagini di orbita: ogni flop canonico valutato una volta con
tutti i runout e sommato su tutte le sue immagini nei semi (la strategia è simmetrica: valore di
`h` su `σ(F)` = valore di `σ⁻¹(h)` su `F`), 573 flop canonici per 7.140 fisici e 605.088 board;
certificatore a chunk paralleli con stato ripristinabile (record per flop con checksum, header con
fingerprint di albero, policy e catalogo), passate parziali per la misura, comando campionato (lo
stimatore P6), certificato JSON `gtosd.preflop_blueprint_certificate.v1`; file di policy
`GTOSDPOL` scritto dal trainer (`--policy-out`) e letto dal certificatore; helper binari
condivisi; header di test condiviso. Report: [P7_CERTIFIER.md](P7_CERTIFIER.md).
Comandi: `ctest -L p7 -V`; esportazione delle policy dai checkpoint P6 (HU10 ridotto e completo,
DCFR 200/500/1.000, 2.000 iterazioni); `gtosd_preflop_blueprint_certify` esatto su HU10 ridotto e
completo (8 thread, chunk 16, stato su file); parziale su CO40 (policy uniforme, 4 flop) per la
proiezione; campionato a 60 flop su HU10 completo; suite `preflop_blueprint`.
Risultati: test 373.068 asserzioni PASS (orbite = enumerazione fisica entro `1e-12` per combo e
in aggregato; ripresa bit-identica; comando campionato = trainer entro `1e-12`; round trip della
policy). Passata esatta: HU10 ridotto EV CO 0,136084 a, guadagni 0,003031 /
0,004200 a, nashconv 0,007231 a, limite inferiore 0,001379 / 0,000905 a, 453 s (0,79 s per flop
canonico, 8 thread), 251 MB; HU10 completo EV CO 0,136090 a, guadagni 0,003118 / 0,004228 a,
nashconv 0,007346 a, 2.220 s (ripresa da 16 flop dopo l'interruzione della sessione), 324 MB;
`EV_CO + EV_BTN = 0` entro `1e-16`. Exploitability vera del blueprint HU10: 0,0042 a = 0,14 % del
piatto (D3 1 %, D2 0,1 a). Stimatore P6 sugli stessi checkpoint: naive a 1.000 flop 0,0187 ±
0,0079 a, limite inferiore 0,0014 a: il valore esatto sta nell'intervallo e il bias della naive
(0,0145 a) è `0,46/√1000` come misurato in P6. CO40 parziale (4 flop canonici, 40 fisici, 4.224
board, policy uniforme): 563 s, 141 s per flop canonico, 1,07 s per board per thread, proiezione
22,4 h per la passata esatta con 8 thread. Suite `preflop_blueprint`: 18/18 PASS in 506 s.
Fallimenti: (1) `Result<T,E>` richiede `T` costruibile per default: `load_policy` restituisce un
`unique_ptr`. (2) Invariante "EV a somma zero" applicato a un sottoinsieme di flop: vale solo sul
catalogo completo perché i due giocatori condizionano su conteggi di flop compatibili diversi;
test limitato alla passata esatta. (3) Script di refactoring degli helper binari lasciato a metà
(parentesi residua in `trainer.cpp`): corretto al primo build.
Dubbi: (1) Su CO40 il costo per board (1,07 s) è 12 volte la traversata P5 per i
terminali all-in di flop e turn (kernel showdown per terminale e per board): le riduzioni
(kernel all-in per (flop, turn), simmetrie sotto lo stabilizzatore, `float`) sono rinviate dopo
P9 per §3.4, ma la passata esatta CO40 va pianificata come lavoro notturno con ripresa. (2)
L'invariante "EV a somma zero" vale solo sul catalogo completo: sulle stime campionate i due
giocatori condizionano su insiemi di flop compatibili diversi; da tenere presente nel viewer P8
quando mostra EV campionati.
Prossimo passo: merge di P7 nell'integrazione; P8 (export, query, comparatore, viewer) sul
branch `feature/preflop-blueprint-p8-export`, con il certificato P7 come fonte del numero
dichiarato e il file di policy come formato di scambio.

### 2026-09-16 — P6 — trainer con campionamento del board, gate PASS

Fatto: trainer CFR vettoriale con campionamento pubblico del board (batch `B`, un passaggio per
giocatore, snapshot della strategia per passaggio, Linear/DCFR una volta per iterazione,
partizione dell'albero in unità indipendenti dal numero di thread con un solo scrittore per
cella, hook esatti per liste di board pesate e sottoinsiemi di mani, checkpoint atomico con
checksum e identità, telemetria); oracolo `FiniteGame` a bucket sul gioco ridotto; valutatore di
best response fisica **non chiaroveggente** (`best_response.hpp`): valori aggregati sulle carte
future prima del massimo, campionamento di `M` flop con tutti i 33 × 32 runout, errore standard
sui flop, stima naive più limite inferiore senza selezione (strategia media al preflop, best
response esatta dal flop in poi); test contro il `FiniteGame` lossless; eseguibile con
`--eval-flops`, `--eval-only`, `--eval-seed` dopo il caricamento, `--fixed-boards`,
`--permute-suits`, default DCFR alternato. Report: [P6_TRAINER.md](P6_TRAINER.md).
Comandi: `ctest -L p6 -V`; `gtosd_preflop_blueprint_train` su HU10 completo e ridotto (2.000
iterazioni, `B = 32`, 8 thread, 20 flop ogni 250 iterazioni) con DCFR alternato e Linear
simultaneo e con le tabelle 50/100/200, 200/500/1.000, 500/1.000/2.000; `--fixed-boards 1/4/8/64`
sul ridotto (valutazione esatta sulla lista); `--eval-only` sui checkpoint finali con 60 flop,
scansione `M = 5…160` sul ridotto e `M = 1.000` per il gate; suite `preflop_blueprint` completa.
Risultati: oracolo regret entro `1,7·10⁻¹³`, strategia media entro `1e-9`; bit-identità 1/2/4/8
thread e partizioni 1/8/58 unità; ripresa identica; best response fisica = `calculate_nash_conv`
del `FiniteGame` lossless entro `1e-9` (nashconv 0,0931106). HU10 completo, DCFR alternato,
200/500/1.000: 0,31 s per iterazione, valutazione a 20 flop 93–122 s (21.120 board, circa 42 ms
per board per thread), memoria 245 MB; massimo guadagno naive 0,099 / 0,108 / 0,084 / 0,091 /
0,108 / 0,137 / 0,122 / 0,085 a alle iterazioni 250…2.000 (semiampiezza 0,04–0,10 a), EV CO
0,136 a. Stesse iterazioni e stessi flop: HU10 ridotto 0,100…0,085 a; 50/100/200 0,098…0,087 a;
500/1.000/2.000 0,104…0,087 a; Linear simultaneo 0,18…0,24 a. Scansione di `M` sul checkpoint
ridotto: naive 0,239 / 0,171 / 0,093 / 0,066 / 0,045 / 0,037 a per `M = 5…160` (`naive · √M` ≈
0,4–0,5 a costante), limite inferiore 0,0005–0,0013 a. Checkpoint completi a `M = 60`: naive
0,045 / 0,048 / 0,046 a (± 0,019) per 200/500/1.000, 50/100/200, 500/1.000/2.000 con limite
inferiore 0,0013 / 0,0019 / 0,0010 a; Linear simultaneo naive 0,143 a, limite 0,0031 a. Board
fissi (valutazione esatta sulla lista, DCFR): 1 board 0,014 a a 100 iterazioni (regola D3
soddisfatta), 4 board 0,014 a, 8 board 0,025 a, 64 board 0,111 a, piatti fra 100 e 500
iterazioni (chiaroveggente: `1·10⁻⁴` / 0,105 / 0,163 / 0,473 a). Gate a `M = 1.000` flop
(1.056.000 board): massimo guadagno naive 0,0187 a + semiampiezza
0,0079 a = 0,0266 a ≤ 0,03 a (guadagni [0,0108, 0,0187] a, limite inferiore [0,0014, 0,0009] a,
4.796 s, 98 MB): `PREFLOP_BLUEPRINT_TRAIN=CONVERGED`, gate PASS. Suite `preflop_blueprint`: 16/16 PASS in 318 s.
Fallimenti: (1) lo stimatore P6.3 come prescritto (massimo per board) era chiaroveggente sopra
il river: plateau di circa 1 a su HU10 diagnosticato prima come pavimento dell'astrazione, poi
smentito dal confronto di capacità e dal test a semi ruotati; circa tre ore di esperimenti da
scartare, erratum alla roadmap §5, decisioni 30–31. (2) La stima naive sui flop campionati
sceglie e valuta le azioni preflop sugli stessi flop: bias `≈ 0,4/√M` a; la cross-fit provata
era negativa a ogni `M` e va scartata; aggiunto il limite inferiore senza selezione (decisione
32). (3) Semiampiezza non nulla sulle valutazioni esatte per lista: corretta a zero. (4)
`--eval-only` con `--eval-seed` diverso respinto per identità: il seme ora riavvia l'RNG dopo il
caricamento (decisione 33). (5) Errori del test di partizione (batch, identità, RNG) e link
mancante di `gtosd::best_response`: vedi report.
Dubbi: (1) tre capacità e due alberi danno curve naive identiche alla terza cifra perché la
stima a 20 flop è dominata dal rumore di selezione comune; la capacità si vede solo nel limite
inferiore (1,9 → 1,3 → 1,0 millesimi di ante), tutto sotto D3. (2) Il gioco ristretto a 64 board
fissi ha best response 0,111 a con un solo runout per flop: non misura l'astrazione del gioco
completo, dove il limite inferiore è di millesimi; la modalità a board fissi serve solo per la
convergenza. (3) La valutazione costa più del training (882 s contro 618 s su 2.000 iterazioni)
e la stima campionata per D3 richiede `M ≈ 1.000` flop (circa 95 min su HU10 completo): la
passata esatta di P7 su 573 flop canonici (605.088 board, circa 50 min) è più economica ed
esatta; conviene usare la stima campionata con `M` piccolo solo per la curva e certificare con P7.
(4) Nella stima campionata il giocatore più "sfruttabile" è il BTN, nel gioco a 64 board il CO:
entrambi effetti del rumore/della restrizione, non della strategia.
Prossimo passo: merge di P6 nell'integrazione; il gate P6 prevede anche il merge dell'integrazione
in `main` con tag `preflop-blueprint-p6-hu10` (Q3, stesso blocco di Q2); P7 sul branch
`feature/preflop-blueprint-p7-certifier` con la stessa aggregazione non chiaroveggente su tutti i
flop canonici.

### 2026-09-15 — P5 — kernel vettoriale HU, gate PASS

Fatto: contesto di board (465 mani vive, rank dalla tabella P2, ordine per rank, classi, bucket
P3 via permutazione canonica, incidenza per carta); kernel fold `D = S − C[h1] − C[h2] + r[h]`,
showdown a due passate con correzione dei blocker e gruppi di pari rank, cache all-in preflop
per board dalla tabella esatta; interfaccia `ShowdownKernel` a N reach (D13) con implementazione
HU; traversata dei valori senza allocazioni con policy a bucket e per mano, potatura a reach nulla
e modalità best response; compilazione di sottogiochi da uno stato arbitrario; test contro i
riferimenti pairwise, contro la ricorsione per coppia e contro il solver postflop `ProductionDcfr`
(solo test, D5); eseguibile di misura. Report: [P5_VECTOR_KERNELS.md](P5_VECTOR_KERNELS.md).
Comandi: build dei target P5; `ctest -L p5 -V`; `gtosd_preflop_blueprint_traversal` su CO40 e
HU10; configure `windows-asan` (RelWithDebInfo, `/fsanitize=address /bigobj`) e
`ctest -R "gtosd_preflop_blueprint_(kernel|game|oracle)_tests"`; suite CTest completa (65 test)
sull'integrazione dopo il merge di P3.
Risultati: kernel 1.636.010 asserzioni PASS in 7,7 s (200 board × 3 pattern di reach entro
`1e-12`; traversata contro ricorsione per coppia con errore massimo `6,5·10⁻¹³` ante; bucket contro
mano entro `1e-12`); oracolo postflop 179.342 asserzioni PASS: tre sottogiochi river (57, 117, 57
nodi) con errore massimo `1,4·10⁻¹⁴` ante sui valori condizionali per combo; ASan PASS senza
diagnostiche (gioco 7,0 s, kernel 43,3 s, oracolo 6,6 s). Tempo per board CO40 a macchina libera:
contesto 0,06 ms, cache all-in 2,05 ms, traversata 89,8 ms (policy a bucket) / 70,7 ms (per
mano), best response 72,5 ms; HU10 completa 9,4 ms. Suite completa sull'integrazione (build
completa 21 min, test 1.278 s): 61/65 PASS; i 4 test legacy `gtosd_river_*_preflight/smoke`
falliscono con "frozen v1 regression manifest fingerprint mismatch" perché il worktree ha
`benchmarks/fixtures/river_bucket_qualification_corpus_v1.json` in CRLF (`core.autocrlf=true`)
mentre il fingerprint congelato è sul testo LF del checkout dell'utente; con il file in LF i 4
test passano (65/65). Nessuna relazione con il codice del programma.
Fallimenti: (1) primo run dell'oracolo respinto dal solver postflop (`invalid_configuration`):
opzioni `ProductionDcfr` costruite a mano; sostituite da `resolve_postflop_production_options`.
(2) Build ASan dell'oracolo fallita per C1128 (troppe sezioni) in `postflop_solver.cpp`, libreria
fuori perimetro: risolto aggiungendo `/bigobj` ai flag del configure ASan, senza modifiche al
repository. (3) Le prime misure di tempo (300 ms per traversata) erano contaminate dalla suite in
esecuzione; rimisurate a macchina libera.
Dubbi: (1) il costo della traversata è dominato dai 15.922 terminali di showdown; ottimizzazioni
(float, vettorizzazione) rinviate dopo P9 per §3.4. (2) Il fingerprint dei manifest legacy è
sensibile ai fine riga: fragilità del legacy da segnalare, non da correggere in questo programma.
Prossimo passo: P6 sul branch `feature/preflop-blueprint-p6-trainer`.

### 2026-09-15 — P4 — modello di gioco e albero compilato, gate PASS

Fatto: layer di regole N-player (`game_model`): stato preflop a N giocatori (ante morte, button
blind vivo del BTN, primo posto ad agire), abstraction delle azioni come funzione del livello di
aggressione della street e del "facing all-in", transizioni HU delegate al core e fold
generalizzato per N > 2, avanzo di street con il primo giocatore attivo non all-in. Albero
compilato (`compiled_game`): un solo array in preordine con sottoalberi contigui, nodi Decision /
Chance / TerminalFold / TerminalShowdown, archi nell'ordine di `legal_actions`, payoff per ogni
sottoinsieme di vincitori settled una volta con `settle_terminal`, statistiche, fingerprint,
layout dello stato `(nodo, classe o bucket, azione)`. Test (`preflop_blueprint_game_tests`) e
eseguibile di report (`preflop_blueprint_game`). Report: [P4_GAME_MODEL.md](P4_GAME_MODEL.md).
Comandi: build dei target P4; `ctest -L p4 -V`; `gtosd_hu_preflop_tree` (legacy) per il confronto;
`ctest -L preflop_blueprint`.
Risultati: 836.981 asserzioni PASS in 0,6 s; controllo di isolamento PASS su 26 sorgenti;
regressione `preflop_blueprint` P0–P4 11/11 PASS (223 s). CO40 parte preflop 58 nodi, 20
decisioni, 9 ingressi, 19 fold, 10 all-in; fingerprint
legacy `fnv1a64:a68337fa567aa2d9` riprodotto dalla parte preflop dell'albero compilato; scheletro
postflop 27.012 nodi rappresentati, 10.060 decisioni (372 flop, 2.100 turn, 7.588 river), 25.944
archi azione, 1.059 frontiere chance, 7.942 fold, 6.715 showdown, 1.236 runout all-in, massimo 4
raise per street, compilazione 0,026 s; stato R+S in double: 356.617.872 B con 200/500/1.000 e
714.889.872 B con 500/1.000/2.000 (preflop 4.617 celle, flop 216.000, turn 2.796.000, river
19.272.000 con la baseline). HU10 completa 2.059 nodi (812 decisioni), HU10 ridotta 571 nodi
(236 decisioni). Albero 3-way (UTG, CO, BTN, 40a) solo preflop: 580 nodi, 234 decisioni, 75
ingressi, 115 fold, 156 runout all-in; il fold generalizzato restituisce l'eccesso non chiamato
(UTG raise 6a, due fold: UTG +3a, CO −1a, BTN −2a).
Fallimenti: (1) primo run del test fallito sul conteggio atteso 30.324 / 11.308 della roadmap;
il benchmark legacy `gtosd_hu_preflop_tree` sul codice attuale misura 27.012 / 10.060 / 25.944,
identici all'albero compilato classe per classe: il valore della roadmap era documentazione
stale. Costanti attese corrette (decisione 20). (2) Il controllo di isolamento ha rifiutato un
commento dell'header che citava il costruttore HU legacy per nome (pattern `hu_preflop`);
commento riformulato.
Dubbi: la roadmap cita anche una profondità massima 15 dallo scheletro legacy; l'albero compilato
misura 17 dalla radice preflop (2 livelli in più per il tratto preflop fino all'ingresso). Il
postflop multiway non è compilato in P4 (P10); la "call per meno" a N > 2 è rifiutata perché con
stack uguali non si presenta e i side pot non sono modellati.
Prossimo passo: P5 sul branch `feature/preflop-blueprint-p5-kernel`.

### 2026-09-15 — P3 — clustering e tabelle bucket, gate PASS

Fatto: k-means intero con k-means++ e riavvii su campione sistematico; EMD esatta (L1 delle
cumulate, centroidi mediane pesate) per flop e turn, L2 sui vettori OCHS al river; tabelle
`uint16` per (board canonico, combo) con centroidi, parametri e fingerprint incorporati; lookup a
tempo costante da board fisico e mano; diagnostica di occupazione, inerzia e distanza media;
gruppi avversari da ranking per test e smoke; eseguibile con report JSON, salvataggio e verifica
di ricaricamento. Report: [P3_BUCKET_TABLES.md](P3_BUCKET_TABLES.md).
Comandi: build dei target P3; `ctest -L p3 -V`; costruzione completa con
`--flop 200 --turn 500 --river 1000 --restarts 10 --screening-iterations 10 --max-iterations 25
--screening-sample 500000` dalle risorse P2; `ctest -L preflop_blueprint`.
Risultati: 16.431.981 asserzioni PASS in 47,85 s (flop K=32, turn e river K=64, indipendenza dai
thread 1/3/8, invarianza ai semi, persistenza, rifiuto dei file corrotti); smoke 21,7 s PASS.
Tabelle 200/500/1.000 a 8 thread: flop 25 iterazioni, inerzia 5,68·10⁸, distanza media 150,7
(2,2 % del massimo), occupazione 449–3.035 righe, 55,5 s; turn 8 iterazioni (convergenza),
inerzia 9,49·10⁸, distanza media 8,12 (1,8 %), occupazione 2.399–102.648, 416 s; river 25
iterazioni, inerzia 1,68·10¹⁶, RMS per coordinata 0,050 di equity, occupazione 1.239–326.951,
814 s; nessun bucket vuoto; 43,3 MB in tre file; ricaricamento verificato; totale 1.294 s.
Fingerprint flop `fnv1a64:33f06cf437f8f26d`, turn `fnv1a64:51814338fcf1236c`, river
`fnv1a64:2e59aa76f59c0fcd`. Regressione `preflop_blueprint` P0–P3: 9/9 PASS (223 s con i test
P4 in corso; il controllo di isolamento è fallito una volta su un commento del codice P4, non su
P3, ed è stato ripetuto dopo la correzione).
Fallimenti: (1) accesso ai membri privati dal builder tramite classe derivata: non compila,
sostituito dal pattern attorney. (2) Lancio in background tramite il wrapper Visual Studio:
messaggio non fatale su `vswhere.exe` e log apparentemente vuoto mentre il processo girava; un
rilancio diretto ha fallito per il lock del log; un terzo lancio ha creato un processo duplicato,
terminato dopo 30 s. Il run originale è arrivato a PASS. Regola adottata: controllare i processi
con `Get-Process` prima di rilanciare.
Dubbi: flop e river si fermano al limite di 25 iterazioni (il turn converge in 8); per le tabelle
finali di P9 misurare 50 e 100 iterazioni. Il merge in `main` previsto da D21 al gate P3 non può
essere eseguito dall'agent senza toccare il working tree dell'utente: domanda Q2.
Prossimo passo: P4 sul branch `feature/preflop-blueprint-p4-game-model`.

### 2026-09-15 — P2 — risorse esatte, gate PASS

Fatto: tabella di rank ordinali a 16 bit derivata dall'oracolo esatto a 5 carte (1.404 rank
distinti); kernel di conteggio degli esiti con blocker in n log n più riferimento pairwise;
tabella all-in preflop esatta per le 176.715 coppie disgiunte; 8 gruppi avversari per equity;
istogrammi esatti flop (465 runout) e turn (30 river) e equity river per gruppi, per board
canonico e combo nel frame canonico; contenitore di risorse con checksum; eseguibile con report,
verifica oracolo, scrittura e ricaricamento. Report: [P2_EXACT_RESOURCES.md](P2_EXACT_RESOURCES.md).
Comandi: build dei target P2; `ctest -L p2 -V`; eseguibile con `--output-dir` e
`--verify-oracle 200000`; `ctest -L preflop_blueprint`.
Risultati: 9.868.560 asserzioni PASS in 84 s; 0 discrepanze con l'oracolo su 200.000 campioni;
tempi con 8 thread: rank 1,4 s, all-in 53 s, flop 2,1–2,4 s, turn 3,3–4,1 s, river 4,7–4,8 s;
7 file per 404.579.533 B scritti e ricaricati; regressione P0–P2 7/7 in 171 s; equity AA contro
mano casuale 0,7308; masse dei gruppi `78, 74, 84, 78, 76, 78, 88, 74`.
Fallimenti: (1) costante attesa delle coppie disgiunte errata (156.240 invece di 176.715: C(32,2)
al posto di C(34,2)); il test l'ha rifiutata, corretta. (2) Soglia di plausibilità dell'equity di
AA calibrata sul mazzo intero (> 0,80) mentre nello Short Deck vale 0,7308; sostituita da un
controllo strutturale più un intervallo largo. Nessun errore nel codice di calcolo.
Dubbi: il costo della tabella all-in (53 s) è il più alto delle risorse; accettabile come una
tantum, da non ricalcolare a ogni build.
Prossimo passo: P3 sul branch `feature/preflop-blueprint-p3-clustering`.

### 2026-09-15 — P1 — canonicalizzazione e cataloghi, gate PASS

Fatto: indice combinatorio colex con inversa e indice di combo compatibile con `all_combos()`;
canonicalizzazione dei semi di flop, flop+turn, board a cinque carte e board history con
permutazione e orbita esposte; cataloghi con molteplicità e riferimenti incrociati; PRNG
deterministico indipendente dalla piattaforma; sampler fisico e canonico; persistenza con
checksum; test e eseguibile di report. Report: [P1_CANONICAL_BOARDS.md](P1_CANONICAL_BOARDS.md).
Comandi: build dei target P1; `ctest -L p1 -V`; `ctest -L preflop_blueprint`.
Risultati: 4.062.607 asserzioni PASS in 3,6 s; conteggi 573 / 13.761 / 19.998 / 369.072 con somme
fisiche 7.140 / 235.620 / 376.992 / 7.539.840; costruzione dei cataloghi 2,56 s (history 2,03 s);
catalogo 13.970.940 B; fingerprint `fnv1a64:51879f40626cb7dd`; regressione P0 3/3.
Fallimenti: (1) prima build fallita per `deck_cards` non dichiarata in `canonical_boards.cpp`
(include mancante di `combinatorics.hpp`), corretta al secondo tentativo. (2) Prevenuti prima
della build: `-bound` su unsigned (C4146 con `/WX`) sostituito da `0U - bound`; scrittura del
magic con tipo a 8 bit; `<cmath>` mancante nel test.
Dubbi: il conteggio dei flop+turn canonici (13.761) era noto solo come limite inferiore
(9.818); ora è fissato come costante attesa. Il test del sampler usa una soglia a sei sigma per
classe: è un controllo di sanità della cumulata, non un test statistico formale.
Prossimo passo: P2 sul branch `feature/preflop-blueprint-p2-resources`.

### 2026-09-15 — P0 — scaffolding completato, gate PASS

Fatto: verificato il tag di sicurezza; creati il branch di integrazione e il branch di fase in un
worktree separato; aggiunti i target `gtosd_card_abstraction` e `gtosd_preflop_blueprint` con
l'opzione `GTOSD_BUILD_PREFLOP_BLUEPRINT`; scritti schema `gtosd.preflop_blueprint_game.v1`, tre
fixture (HU10 completa, HU10 ridotta, CO40), loader C++ con validazione e fingerprint, identità
dell'astrazione, test di scaffolding, controllo di dipendenza CMake, validatore Python dello
schema. Report: [P0_SCAFFOLDING.md](P0_SCAFFOLDING.md).
Comandi: configure Release con Ninja e MSVC riusando i pacchetti vcpkg di
`out/build/windows-release-current`; build dei tre target; `ctest -L preflop_blueprint`.
Risultati: configure 15,5 s; build 19 passi senza warning con `/WX`; test 3/3 PASS
(73 asserzioni, 4 sorgenti guardati, 3 fixture valide); guardia negativa su albero sintetico:
link proibito rifiutato, include legacy rifiutato, albero pulito accettato.
Fallimenti: (1) il nome di branch `feature/preflop-blueprint/p0-scaffolding` previsto da D21 è
rifiutato da git perché esiste il ref `feature/preflop-blueprint`; risolto con il trattino,
D21 e roadmap §9 allineati. (2) Il primo wrapper per l'ambiente Visual Studio lanciato da Git
Bash convertiva `/c` in un percorso; risolto disattivando la conversione dei percorsi MSYS.
(3) La rimozione dei worktree legacy con scratch e la cancellazione dei file `.bin` erano già
state bloccate dal classificatore di sicurezza prima dell'avvio di P0 (registro, A8 e D25);
nessun impatto su P0.
Dubbi: nessuno bloccante. `maximum_postflop_sizes = 3` è un limite di scaffolding da rivedere in
P4 insieme al layout delle azioni compilate.
Prossimo passo: P1 sul branch `feature/preflop-blueprint-p1-canonical` dopo il merge di P0
nell'integrazione.

### 2026-09-15 — P0 — creazione del diario

Fatto: creato il template del diario insieme alla roadmap. Nessun codice scritto.
Comandi: nessuno.
Risultati: nessuno.
Fallimenti: nessuno.
Dubbi: nessuno.
Prossimo passo: P0.

## 4. Domande per l'utente

| # | Data | Domanda | Stato | Risposta |
|---|---|---|---|---|
| Q1 | 2026-09-15 | I branch di fase vengono uniti nell'integrazione con merge locali `--no-ff`; per aprire pull request su GitHub servirebbe il push dei branch su origin. Si pubblicano i branch su origin oppure restano merge locali fino ai gate di `main`? Nel frattempo si procede con merge locali. risolta 2026-09-16 | pubblicare i branch: eseguito, 10 branch e 3 tag su origin (`main` non pushato) |
| Q2 | 2026-09-15 | D21 prevede il merge dell'integrazione in `main` al gate P3 con tag. `main` è il branch checked-out nel working tree dell'utente (`C:/Users/GoryNickel/Documents/GitHub/GTO-Solver`): git non permette di farne il checkout in un secondo worktree e spostarne il ref da fuori lascerebbe il working tree dell'utente in uno stato incoerente. Comandi proposti, da eseguire nel working tree dell'utente con `main` pulito: `git merge --no-ff feature/preflop-blueprint -m "merge(preflop-blueprint): P0-P3 card abstraction, gate P3 PASS"` poi `git tag -a preflop-blueprint-p3-abstraction -m "P3 gate PASS"`. Suite CTest completa eseguita sull'integrazione dopo il merge di P3: 65/65 PASS (4 test legacy passano solo con il manifest v1 in LF, vedi voce P5 del diario; nel checkout dell'utente il file è in LF). In alternativa l'utente può autorizzare l'agent a eseguire i due comandi nel suo working tree. Nel frattempo P4 e P5 sono proceduti sull'integrazione. risolta 2026-09-16 | applicare il merge: eseguito nel working tree dell'utente (`97d8121`), tag `preflop-blueprint-p3-abstraction` su `981361e` |
| Q3 | 2026-09-16 | D21 prevede al gate P6 il merge dell'integrazione in `main` con tag `preflop-blueprint-p6-hu10`; stesso blocco di Q2 (`main` è il working tree dell'utente). Comandi proposti nel working tree dell'utente con `main` pulito: `git merge --no-ff feature/preflop-blueprint -m "merge(preflop-blueprint): P0-P6 trainer and physical best response, gate P6 PASS"` poi `git tag -a preflop-blueprint-p6-hu10 -m "P6 gate PASS: HU10 D3 with the sampled estimator at 1000 flops"`. Da eseguire dopo (o insieme a) Q2. Nel frattempo P7 procede sull'integrazione. risolta 2026-09-16 | come Q2: tag `preflop-blueprint-p6-hu10` su `22e1015` e `preflop-blueprint-p8-export` su `9360836` |
| Q4 | 2026-09-16 | P8 prevede l'aggiornamento del viewer `tools/hu_preflop_chart_viewer` (repository separato, D18, escluso da git in questo repository) perché legga il nuovo export e mostri i badge `ESTIMATED` / `CERTIFIED_EXACT` / `EXTERNAL_REFERENCE`; il gate P8 richiede "viewer navigabile con un export HU10". L'agent lavora nel worktree e non modifica né il repository del viewer né il working tree dell'utente. Proposta: l'agent produce in questo repository l'export nel formato che il generatore del viewer già legge (`preflop_nodes` con `history`, `strategy`, `action_ev` per classe), lo schema e un validatore statico registrato in CTest; la modifica del generatore (vincoli fissi su 20 nodi, fingerprint CO40 e path Monker da rendere generici; badge di stato dal certificato P7) va fatta nel repository del viewer: la esegue l'utente, oppure l'utente autorizza l'agent a modificare `tools/hu_preflop_chart_viewer` nel suo working tree. Fino alla risposta il criterio "viewer navigabile" del gate P8 resta INCONCLUSIVE e le altre parti di P8 procedono. risolta 2026-09-16 | il viewer può essere modificato dall'agent: fatto sul branch `feature/preflop-blueprint-p8-export` del viewer (`4edbb45`), modifiche locali preesistenti conservate in `56cdf31` |

## 5. Decisioni prese dall'agent

| # | Data | Fase | Decisione | Motivazione |
|---|---|---|---|---|
| 1 | 2026-09-15 | P0 | Branch di fase con trattino: `feature/preflop-blueprint-pN-nome` | git rifiuta `feature/preflop-blueprint/pN-nome` perché il ref `feature/preflop-blueprint` esiste |
| 2 | 2026-09-15 | P0 | `maximum_postflop_sizes = 3` nel loader | fold/check/call più size più all-in restano entro le sei azioni del motore esistente; da rivedere in P4 |
| 3 | 2026-09-15 | P0 | `button_blind_units` esplicito e strettamente positivo | D9; evita l'identità implicita con l'ante del formato legacy |
| 4 | 2026-09-15 | P0 | Riuso dei pacchetti vcpkg installati nella build principale (`VCPKG_MANIFEST_INSTALL=OFF`) | evita una nuova installazione delle dipendenze nel worktree; riproducibile |
| 5 | 2026-09-15 | P0 | Test dello schema con `SKIP_RETURN_CODE 77` se `jsonschema` manca | non fallire su macchine senza il pacchetto; il loader C++ applica comunque le regole |
| 6 | 2026-09-15 | P0 | Merge locali `--no-ff` nell'integrazione, nessun push su origin | il push pubblica contenuti; in attesa della risposta a Q1 il lavoro non si ferma |
| 7 | 2026-09-15 | P1 | PRNG proprio (xoshiro256** seminato da splitmix64, draw limitati con il metodo di Lemire) al posto di `std::mt19937_64` e `std::uniform_int_distribution` | le distribuzioni standard sono implementation-defined; un seed deve identificare gli stessi board su ogni piattaforma |
| 8 | 2026-09-15 | P1 | Canonicalizzazione per minimo su 24 permutazioni di un codice a 6 bit per carta, cataloghi per enumerazione esaustiva | verificabile con la dimensione dell'orbita; 2,6 s di costruzione |
| 9 | 2026-09-15 | P1 | Conteggio dei flop+turn canonici fissato a 13.761; il caricamento del catalogo è fail-closed sui quattro conteggi | misura ottenuta dall'enumerazione; sostituisce il limite inferiore della roadmap |
| 10 | 2026-09-15 | P1 | Il file del catalogo non viene distribuito | ricostruzione in 2,6 s; il file salvato serve come identità verificabile con checksum |
| 11 | 2026-09-15 | P2 | Rank ordinali a 16 bit derivati dall'oracolo a 5 carte invece di caricare la tabella R3 a 32 bit | stesso ordine dei `HandValue`, metà memoria, 1,4 s di costruzione, nessun file esterno; l'oracolo resta l'unico evaluator |
| 12 | 2026-09-15 | P2 | Kernel sweep per flop e turn, pairwise per il river | il river richiede conteggi per gruppo avversario; il pairwise è un controllo indipendente del kernel |
| 13 | 2026-09-15 | P2 | File delle feature (365 MB) come artefatti offline non distribuiti; equity river in virgola fissa a 16 bit | servono solo al clustering P3; dimensione dimezzata rispetto a float32 con errore 1/131070 |
| 14 | 2026-09-15 | P2 | Tabella all-in triangolare con voci vuote per le coppie sovrapposte | indirizzamento O(1) senza mappa |
| 15 | 2026-09-15 | P3 | k-means intero: istogrammi e cumulate come conteggi, centroidi come mediane pesate (EMD) o medie arrotondate (L2), partizione statica del lavoro | nessuna dipendenza dall'ordine di riduzione in virgola mobile: risultato identico a 1, 3 e 8 thread (D14) |
| 16 | 2026-09-15 | P3 | Riavvii valutati su un campione sistematico di 500.000 osservazioni per 10 iterazioni, solo il migliore rifinito sull'intero insieme | 10 riavvii completi costerebbero dieci volte il river (814 s); il campione sistematico è deterministico e copre tutte le righe |
| 17 | 2026-09-15 | P3 | Bucket rietichettati per forza crescente del centroide dopo la convergenza | id confrontabili fra costruzioni e leggibili nei report; l'assegnazione non cambia |
| 18 | 2026-09-15 | P4 | Regole di fase come funzione del livello di aggressione della street (0 apertura, 1 risposta, 2+ solo fold/call/all-in) e del "facing all-in" letto dallo stato, non della macchina a stadi HU | stesso albero HU (fingerprint legacy riprodotto) e regole valide per N giocatori |
| 19 | 2026-09-15 | P4 | Transizioni HU delegate a `gtosd::apply_action`/`advance_street`; per N > 2 fold generalizzato e avanzo di street nella libreria blueprint, nessuna modifica al core in P4 | il core gestisce il fold solo a due giocatori; il costruttore N-player nel core è previsto da P10 (§2.2) |
| 20 | 2026-09-15 | P4 | Conteggi attesi dello scheletro postflop CO40 corretti a 27.012 nodi / 10.060 decisioni / 25.944 archi (misurati con `gtosd_hu_preflop_tree` sul codice legacy attuale) | i 30.324 / 11.308 / 29.112 della roadmap provengono da documenti anteriori alle regole di puntata correnti e non sono riprodotti nemmeno dal codice legacy |
| 21 | 2026-09-15 | P5 | Cache per board delle probabilità esatte di vittoria e pareggio di ogni coppia viva (2 matrici 465×465 in double, 3,5 MB) invece di leggere la tabella P2 a ogni terminale | 10 terminali all-in preflop per traversata: costruzione una volta per board, poi prodotti matrice-vettore; la massa di sconfitta deriva da `D − W − T` con `D` esatto dal kernel fold |
| 22 | 2026-09-15 | P5 | Interfaccia `Policy` per (nodo, mano) con due implementazioni: `BucketPolicy` sul layout P4 e `HandPolicy` per mano | la traversata non conosce l'astrazione; gli oracoli e i test prescrivono strategie per combo, il trainer userà i bucket |
| 23 | 2026-09-15 | P5 | Oracolo postflop confrontato sul valore condizionale `v[h] / D[h]` invece che sul valore controfattuale grezzo | il solver postflop normalizza la reach avversaria con una costante interna; il rapporto elimina la costante e resta un confronto esatto per combo |
| 24 | 2026-09-15 | P5 | Tabella all-in e rank caricate dalla directory `out/preflop_blueprint_resources` quando presente, altrimenti ricostruite nel test | i test restano autosufficienti su altre macchine (circa un minuto di costruzione) e rapidi su quella di riferimento |
| 25 | 2026-09-15 | P6 | Nel regret `R += w_B · P(h) · P(o\|h) · (v_a − v)` il fattore `cf_reach` di §5 è già dentro `v` (valori dei kernel con la reach avversaria); non viene moltiplicato di nuovo | formula del CFR vettoriale standard; uguaglianza entro `1e-13` con `solve_finite_game` sul gioco ridotto |
| 26 | 2026-09-15 | P6 | Modalità di aggiornamento `Simultaneous` (uno snapshot per iterazione, entrambi i giocatori) come default e `Alternating` come opzione; Linear default, DCFR 1,5/0/2 opzione | la modalità simultanea riproduce esattamente `solve_finite_game` Linear (oracolo P6); DCFR alternato converge più in fretta nelle prove e resta lo sfidante di §3.4 |
| 27 | 2026-09-15 | P6 | Partizione dell'albero in unità di `max(256, nodi/128)` nodi, indipendente dal numero di thread; parte alta seriale; incrementi scritti direttamente nelle celle (un solo scrittore) senza buffer di delta | bit-identità per qualsiasi numero di thread e di unità verificata (0 celle diverse fra 1, 8 e 58 unità); nessuna copia dello stato (D14, roadmap P6.2) |
| 28 | 2026-09-15 | P6 | Lo stimatore D3 misura la best response fisica contro la strategia media a bucket, come prescrive P6.3. **Corretta dalla decisione 30**: il massimo per mano *su ogni board* è chiaroveggente sopra il river | la best response del gioco astratto con recall imperfetto non è calcolabile per board; quella fisica la domina ed è la misura che P7 certificherà |
| 29 | 2026-09-15 | P6 | Boards del batch pesati `1/B`; RNG di training e di valutazione separati e salvati nel checkpoint | costanti comuni alle iterazioni non cambiano il regret matching; la valutazione non perturba il training e la ripresa è bit-identica |
| 30 | 2026-09-15 | P6 | Best response fisica **non chiaroveggente**: ai nodi dell'eroe i valori delle azioni sono aggregati sulle carte future prima del massimo (turn: somma sui river; flop: somma sui turn; preflop: somma sui flop); il massimo per board resta solo al river. Erratum aggiunto alla roadmap §5 | la best response per board di P6.3/§5 dava un responder che conosce turn e river: plateau di circa 1 a su HU10 e pavimenti crescenti con il numero di board fissi (0,105/0,163/0,473 a per 4/8/64 board) non dovuti all'astrazione; l'aggregazione corretta coincide con `calculate_nash_conv` del `FiniteGame` lossless entro `1e-9` |
| 31 | 2026-09-15 | P6 | Valutazione campionata per flop: `M` flop campionati dal catalogo con tutti i 33 × 32 runout enumerati, errore standard sui gruppi di flop; le liste esplicite sono raggruppate per flop | l'aggregazione non chiaroveggente al flop e al turn richiede tutti i runout del prefisso; board completi indipendenti non bastano |
| 32 | 2026-09-16 | P6 | Lo stimatore campionato riporta la stima naive (scelta e valore preflop sugli stessi `M` flop, distorta verso l'alto come `1/√M`) e un limite inferiore senza selezione (strategia media al preflop, best response esatta dal flop in poi); il gate D3 usa la naive più semiampiezza con `M = 1.000` flop; la cross-fit provata è stata scartata | sul checkpoint HU10 ridotto `naive · √M` è costante (≈ 0,4–0,5 a) per `M = 5…160` mentre il limite inferiore è ≈ 0,001 a: la stima P6.3 a `M` piccolo misura solo rumore di selezione; la cross-fit era negativa a ogni `M` |
| 33 | 2026-09-16 | P6 | Default dell'eseguibile di training: DCFR 1,5/0/2 con update alternato; la libreria mantiene Linear simultaneo come default (l'oracolo `FiniteGame` lo richiede). `--eval-seed` riavvia l'RNG di valutazione dopo il caricamento del checkpoint invece di entrare nell'identità | su HU10 completo il Linear simultaneo resta 2–3 volte sopra il DCFR alternato a parità di flop di valutazione (0,143 contro 0,045 a a `M = 60`); una rivalutazione su flop freschi non deve cambiare l'identità del run ripreso |
| 34 | 2026-09-16 | P7 | La passata esatta valuta ogni flop canonico una volta con tutti i runout fisici e somma sulle immagini della sua orbita nei semi (`FlopValues.images`); `aggregate(exact)` verifica che ogni combo sia compatibile con 5.984 flop fisici | la strategia media è simmetrica nei semi per costruzione; verificato contro l'enumerazione fisica entro `1e-12` per combo; costo 573 flop invece di 7.140 |
| 35 | 2026-09-16 | P7 | Formato di policy `GTOSDPOL` (fingerprint dell'albero, capacità, sorgente, tabella densa, checksum) scritto dal trainer e letto dal certificatore; il certificato porta i fingerprint di regole, albero, catalogo, tabelle bucket e policy | il certificatore non deve ricostruire il trainer (identità, semi) per leggere una strategia; P8 esporta dallo stesso file |
| 36 | 2026-09-16 | P7 | Stato del certificatore accodato per chunk con checksum per record; ripresa dall'header (albero, policy, catalogo); il numero dichiarato nei certificati è quello esatto, la regola D3 campionata resta la regola di arresto del training | passata ripresa bit-identica; su CO40 la passata esatta costa 22 h e va spezzata; la stima campionata sovrastima di `≈ 0,46/√M` |
| 37 | 2026-09-16 | P8 | Export `gtosd.preflop_blueprint_chart.v1` nel layout `preflop_nodes` → `{history, strategy, action_ev}` già letto dal generatore del viewer, con id di azione compatibili con le chart legacy (`raise_6`, `call`, `fold`, `all_in`) e id di nodo `CO_raise_3_BTN` | il viewer richiede solo la rimozione dei vincoli fissi CO40 e i badge (Q4); nessun secondo formato da mantenere |
| 38 | 2026-09-16 | P8 | EV per azione condizionato al nodo: valore controfattuale diviso per la reach avversaria data la combo, media di classe pesata con la reach, errore standard sui flop campionati; alla radice coincide con l'EV del gioco. **Implementazione corretta dalla decisione 43**: fino al commit `46084f0` la divisione per `D[h]` mancava e i valori fuori dalla radice erano scalati per `D[h]` | è la semantica delle chart (EV dell'azione nello spot); verificata entro `1e-9` alla radice |
| 39 | 2026-09-16 | P8 | Verdetto del comparatore sulla sola exploitability fisica dichiarata (D2, soglia 0,1 a): `QUALIFIED` / `REJECTED` con certificato esatto, `PROMISING` / `INCONCLUSIVE_ESTIMATE` / `REJECTED` (limite inferiore sopra soglia) con stima campionata; distanze Monker descrittive con `EXTERNAL_CONTRACT_INCOMPLETE` | D1/D2/D4 della roadmap; la stima campionata ha bias di selezione e non può qualificare da sola |
| 40 | 2026-09-16 | P8 | Albero pubblico postflop esportato nello schema del viewer (`gtosd.hu_postflop_public_tree.v1`) leggendo lo stato pubblico conservato per nodo dal compilato; id compilati nel file per indirizzare il worker | il viewer già navigava quello schema; nessun secondo formato |
| 41 | 2026-09-16 | P8 | EV postflop del worker: valore controfattuale dell'azione con la strategia media diviso per la reach avversaria al nodo; esatto a turn (32 river) e river, medio su `samplesPerAction` runout campionati al flop (seme fisso), classi pesate con la reach avversaria, frequenze di range pesate anche con la reach dell'eroe | stessa semantica dell'export preflop; il flop completo (1.056 runout) costa 17,6 s e non è interattivo |
| 42 | 2026-09-16 | P8 | Viewer: generatore con sorgenti blueprint e badge, azioni e gioco dinamici, Monker solo a parità di albero, backend `--serve`; le modifiche locali preesistenti dell'utente sono conservate in un commit separato prima dell'aggiornamento | autorizzazione Q4; il repository del viewer non ha remote: i commit restano locali |
| 43 | 2026-09-16 | P8 | EV di classe fuori dalla radice: media di `v_a[h] / D[h]` pesata con `D[h]` (valore e serie per flop dell'errore standard); test del payoff di fold a ogni nodo interno | fino a `46084f0` la divisione per `D[h]` mancava e alla radice non era rilevabile (`D[h] = 1`); il payoff di fold è una costante nota a ogni nodo interno e verifica la semantica condizionata |
| 44 | 2026-09-16 | P8 | Variante CO40 di test come fixture separata con id `-TEST` (una size postflop 100 % più all-in), usata solo per misure di tempo e prove; la fixture CO40 completa resta il riferimento del gate P9 | richiesta dell'utente; il "da non fare" di P9 vieta di cambiare size per migliorare il risultato, non di misurare su un albero ridotto |
| 45 | 2026-09-16 | P8 | Nel viewer le classi con reach proprio lungo la history sotto `1e-6` restano visibili ma desaturate, con il reach nel tooltip | l'export riporta la strategia media di tutte le classi (residui ≈ 1e-9); togliere le righe cambierebbe la griglia 9×9; la soglia è sotto ogni frequenza di gioco significativa |
| 46 | 2026-09-16 | P8 | Lista di risposte vuota nella configurazione = nessuna size di rilancio sopra un open (livello 1: fold, call, all-in); con lista non vuota resta una risposta per open | regola dell'utente per HU10 (contro l'open 5 a BTN ha solo l'all-in); nessun secondo formato, la fixture CO40 non cambia |
| 47 | 2026-09-16 | P8 | Le misure di tempo del certificatore si fanno con `--chunk` ≥ numero di thread (il parallelismo è sui flop di uno stesso chunk); i tempi per flop nei report sono a 8 thread con chunk 16 salvo indicazione | le proiezioni di P7 §5 (chunk 2) e della voce CO40-TEST (chunk 1) avevano 2 e 1 thread attivi e sovrastimavano di 2,3 e 4,2 volte |
| 48 | 2026-09-16 | P8 | La soluzione della variante CO40 di test entra nel viewer come terza sorgente (chart preflop con badge e exploitability esatta dichiarata, il run migliore fra DCFR 2.000/10.000 e Linear 2.000); la navigazione postflop resta sull'albero e sulla policy HU10 completo | il generatore accetta un solo `--postflop-tree` e il server una sola policy; estenderli non era richiesto |
