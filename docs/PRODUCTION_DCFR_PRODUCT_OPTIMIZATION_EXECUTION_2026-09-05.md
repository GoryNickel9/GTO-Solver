# Esecuzione del piano ProductionDcfr

## Stato verificato

R0 è completata. Provenienza, ambiente, binario, profilo e corpus D/V/H sono
congelati; H è sigillato e non può essere usato per scegliere architettura o
parametri. R1 è implementata nei caller disponibili. CLI, API e GUI sono
verificate.

Questi risultati appartengono al checkout
`ffb208a0dad018dc537288c42239322ff7dfde6d`, branch `main`, con 48 commit locali
rispetto a `origin/main`. I file utente non tracciati sono rimasti intatti. Il
binario Release corrente è `gto_cli.exe`, 4.388.864 byte, SHA-256
`19925276F6403D799435EC2FC376EC6BEFC9612744DF5116FEC940EF632E5767`.

## Manifest ambiente R0

| Campo | Valore verificato |
|---|---|
| Host | Windows 11 Home 64 bit, build 26200 |
| CPU | Intel Core i3-10100F, 4 core, 8 logical processor, 3.601 MHz dichiarati |
| RAM fisica | 34.294.738.944 byte, 31,939 GiB |
| Generatore | Ninja, CMake Release |
| Compilatore | MSVC 14.51.36231, `cl.exe` x64 |
| Flag Release | `/O2 /Ob2 /DNDEBUG`, C++20, warning come errori |
| Limite prodotto | massimo 8 thread CPU; nessun backend GPU |
| Research path | nightly, hotpath e pure probe disabilitati; test e oracle range abilitati |
| GUI | preset `windows-gui-release`, Qt 6.11.1, E2E product PASS |

Il corpus corrente è
`benchmarks/fixtures/production_dcfr_product_corpus_v2.json`, seed
`1305092026`; v1 resta lo snapshot storico precedente al holdout. Lo script
`tools/generate_production_dcfr_holdout.py` seleziona il minimo SHA-256 per tre
strati di texture, escludendo D/V e le regressioni. Assegna profili già fissati,
genera range espliciti da 630 combo per giocatore e verifica byte, hash, ID,
partizioni e seal. Nessun output del solver entra nella selezione.

H contiene:

| ID | Board/strato | Preflight lazy peak | Stato |
|---|---|---:|---|
| H-PAIRED-TWOTONE-001 | `7s 8c 8s`, paired two-tone | 6.398.975.928 B | sealed |
| H-CONNECTED-TWOTONE-001 | `Ts Qs Kd`, connected two-tone | 15.632.954.976 B | sealed |
| H-ACELOW-RAINBOW-001 | `7c Qh As`, ace-low rainbow | 3.311.132.808 B | sealed |

I preflight verificano solo validità, struttura e ceiling hard su 31 GiB RAM;
non sono stati eseguiti solve né letti strategia, EV, dEV, NashConv o tempi H.

## Contratto misure

Un'iterazione ProductionDcfr è un aggiornamento alternato completo di entrambi
i giocatori. I pass giocatore, le visite, gli update locali e le traversate
equivalenti sono contatori separati. `maximum_deviation` è il massimo gain di
deviazione unilaterale; NashConv è la somma dei gain. Entrambi vengono
normalizzati sul pot solo nei campi esplicitamente denominati `normalized`.

I timer hanno questi confini:

1. `build_to_ready`: parsing già avvenuto; include build fisica, layout,
   inizializzazione e preparazione specifica del gioco.
2. `solver`: solo kernel iterativo, con certificazioni registrate a parte.
3. `solve_to_consultable`: parte dal comando solve sul progetto pronto e termina
   quando checkpoint, analytics necessari e vista interrogabile sono pronti.
4. `build_to_consultable`: somma operativa dei primi e terzo intervallo; startup
   del processo escluso.

Peak RSS è il massimo working set del processo. `solver_state_bytes` è il
payload persistente calcolato dal layout. Nessuno dei due è confrontabile con
il display GTO+ “Memory needed for solving” finché il perimetro esterno non è
dimostrato equivalente; il gate resta
`NOT_EVALUATED_COMPARABILITY_UNRESOLVED`.

## Matrice caller R1

| Caller | Risoluzione profilo | Nuovo solve | Resume legacy | Verifica |
|---|---|---|---|---|
| API postflop | `resolve_postflop_production_options` | ProductionDcfr, scaled uint16, cert 20, depth 7 | rifiuto esplicito | phase10 PASS |
| CLI solve/resume | resolver condiviso | stesso profilo API | CFR+ e backend non supportati rifiutati | smoke solve/resume PASS |
| Benchmark GTO+ | resolver condiviso, poi validazione fixture | nessun override nascosto | non applicabile | contract/reference PASS |
| GUI worker | resolver condiviso | stesso profilo API | rifiuto esplicito | E2E Qt PASS |
| Solution archive | metrics schema v4 | algoritmo, precisione ed esponenti persistiti | versioni 1-3 leggibili | phase8 PASS |

Il checkpoint nativo ProductionDcfr usa payload binario v2 e conserva algoritmo,
precisione, esponenti, iterazione, delay, layout e checksum. Il reader conserva
il formato v1 legacy ma il prodotto non lo riprende se identifica CFR+ o se i
metadati non ricostruiscono senza ambiguità il profilo production.

## Registro decisioni

| ID | Stato | Decisione |
|---|---|---|
| R1-PROFILE-001 | implementato | Un solo resolver production per API, CLI, benchmark e GUI |
| R1-PERSIST-001 | implementato | Checkpoint v2 e metrics archive v4; lettura legacy conservata |
| R1-BACKEND-001 | implementato | Solo `LazyInRam`; rimosso il fallback silenzioso Float64 out-of-core |
| R0-HOLDOUT-001 | implementato | H v2 deterministico, disgiunto, con range espliciti e seal verificato |
| R1-GUI-001 | implementato | Qt6 feature installata; build Release ed E2E product PASS |

## Validazione eseguita

- phase8 e phase10: `2/2 PASS`, 16,49 s;
- smoke CLI nuovo solve: 20 iterazioni, profilo 1.0, 0,696522 s, peak RSS
  32.772.096 byte;
- smoke CLI resume: 40 iterazioni, 0,660994 s, peak RSS 36.823.040 byte;
- contratto ProductionDcfr, catalog smoke e riferimento GTO+: `3/3 PASS`,
  86,47 s;
- build GUI Release: PASS; E2E create-solve-save-reopen-navigate-resume PASS
  in 12,97 s, heartbeat massimo 30,97 ms;
- suite GUI Release completa sull'implementazione corrente: `38/38 PASS`,
  203,93 s;
- corpus H: generator/checker PASS; tre config valide, hash e partizioni PASS;
  ceiling hard 31 GiB PASS; nessun solve H eseguito.
- accounting product: action-catalog, contratto ProductionDcfr e nuovo smoke
  `postflop solve` `3/3 PASS`; phase10 con i due lati della soglia di residency
  `PASS`.

Nessuno smoke è una baseline R2 o una prova di speedup. I tempi servono soltanto
a dimostrare che il percorso operativo termina e produce il profilo risolto.

## R2 — Baseline contemporanea controllata

I dati grezzi qualificati come baseline sono in
`out/production-dcfr-product-b0-controlled-20260905`. Ogni fixture ha cinque
processi indipendenti, report v4 e summary v4. Il preflight è stato eseguito
prima di ogni processo: CPU media massima 11,40% AHK, 14,44% TH e 13,86% TST;
RAM libera minima rispettivamente 15.117.537.280, 15.166.881.792 e
14.317.359.104 byte. Nessuna run accettata ha superato il limite CPU del 15% o
il minimo RAM di 4 GiB.

| Caso | Iter. | dEV | Solver mediana / p95 | Build→ready mediana / p95 | Build→consultabile mediana / p95 | Gate |
|---|---:|---:|---:|---:|---:|---|
| AHK | 80 | 0,951423% | 0,788373 / 0,817360 s | 0,040839 / 0,041272 s | 0,829805 / 0,857142 s | root PASS; speed PASS; EV/action diag FAIL |
| TH | 80 | 0,807956% | 22,682420 / 22,975138 s | 0,367735 / 0,404968 s | 23,042815 / 23,336229 s | root PASS; speed FAIL; EV/action diag FAIL |
| TST | 160 | 0,904505% | 245,082130 / 304,203644 s | 2,794833 / 3,098190 s | 247,841130 / 306,967936 s | root/EV/action PASS; speed FAIL |

Correttezza root, layout, exact outcomes, target e iterazioni sono `15/15 PASS`.
I diagnostici downstream restano separati: AHK e TH hanno controlli EV/action
GTO+ fuori tolleranza, mentre TST li supera. Il gate tempo fallisce su TH e TST;
R2 è quindi chiusa come baseline riproducibile, non come parity o promozione.
Il worktree è dirty perché contiene l'implementazione in corso; il summary AHK
precede il fallback host e conserva `hardware_metadata_complete: false`, mentre
TH/TST hanno metadati completi. Nessuno di questi fatti viene riscritto a
posteriori nei report grezzi. La memoria GTO+ resta non valutabile per
comparabilità irrisolta.

H resta sigillato e non è stato usato. R2-S può procedere con D/V, ma nessun
risultato complessivo può essere qualificato finché tempo, accuratezza e RAM non
superano i gate finali.

Il comando prodotto prepara esplicitamente l'albero prima di avviare il solve.
Lo smoke ridotto corrente misura `build_to_ready=0,014158 s`,
`solve_to_consultable=0,782953 s` e `build_to_consultable=0,797110 s`, con
`elapsed_seconds=0,684000 s`; il secondo intervallo include il checkpoint finale.
Il benchmark v4 con i range reali espone lo stesso contratto
`gtosd.product_timing.v1`. Uno smoke AHK completo ha misurato rispettivamente
`0,043209 / 0,869172 / 0,912381 s`, oltre a `solver=0,869017 s`. Sono verifiche
dell'accounting, non campioni B0 qualificati.

Il runner multiprocesso esegue, prima di ogni processo, cinque campioni CPU via
`GetSystemTimes`, verifica RAM libera e rifiuta processi incompatibili. I primi
tentativi ad alto carico si sono fermati prima del solve e non sono entrati in
B0. Una verifica grezza successiva ha confermato il calcolo del gate: su 1,5 s,
121.250.000 tick totali, 115.156.250 idle e CPU 5,026%.

## R2-S — Architettura A0 implementata e non promossa

S0 è chiusa in
`specifications/CARD_ABSTRACTION_AND_SUBGAME_CONTRACT.md`. Il contratto separa
action abstraction, card abstraction e decomposizione; definisce identità,
versione, perfect recall, byte model, qualità nel gioco originale e boundary
CFV del subgame.

S1 parte dal prototipo isolato sul motore `FiniteGame`. Il formato
`GTOSD_CARD_ABSTRACTION 1 0` supporta mapping completo, pesi, fingerprint,
serializzazione, aggregazione e lift. Tutti i chance outcome fisici restano nel
gioco. Il test identità produce la stessa strategia DCFR; il mapping coarse Kuhn
riduce gli action-entry da 24 a 8 ma, una volta sollevato, mantiene NashConv
originale sopra 0,1. Il test prova quindi che la convergenza astratta non viene
confusa con la qualità nel gioco originale.

S2 usa lo stesso contratto anche su un river postflop reale tramite un bridge
esatto e bounded. Il bridge materializza ogni deal privato compatibile con i
pesi originali, clona l'action tree, importa la strategia media dal checkpoint
ProductionDcfr exact e rifiuta chance pubbliche interne o limiti superati senza
campionare. La fixture `R2S-RIVER-SPARSE-001` conserva 5 deal fisici in 46 nodi:
il gioco proiettato riproduce profile value e NashConv dell'oracolo postflop a
`1e-9`. Projection e boundary richiedono rispettivamente 13.860 B e 152 B nel
modello minimo, allocator esclusi. Dopo resolve, la BR globale avversaria resta
`-1,8` prima e dopo. Boundary incompleti, root off-tree e checkpoint mismatched
sono rifiutati.

S1 identity è ora portata nel layout postflop. La policy
`postflop-card-abstraction/1.0/exact-identity` è esplicita e validata nei confini
di prepare/solve. La mappa infoset è implicita `i -> i`: conserva infoset,
action-entry e perfect recall senza un vettore proporzionale all'albero. Policy
implicita ed esplicita producono gli stessi fingerprint, codici action-major e
scale; il checkpoint ProductionDcfr binario v2 compie round-trip senza drift.

La policy `made_hand_value` 1.0 aggiunge il primo mapping coarse postflop. Usa
categoria e kicker dell'intero `HandValue` corrente e raggruppa soltanto entro
lo stesso nodo decisionale pubblico. Range, blocker, runout e payoff restano
fisici; la policy non rivendica perfect recall né accuratezza nel gioco
originale. Il mapping memorizza ID bucket locali a 16 bit, offset per decisione
e indici dei membri; i pesi sono letti dai range canonici invece di essere
duplicati per infoset. D-RIVER-CHECK-001 passa da 1.015.872 infoset a 72.052 con
4.700.960 B di mapping. V-RIVER-BET-001 passa da 1.997.952 a 141.328, con
action-entry da 3.962.112 a 279.880 e 9.242.624 B. I fingerprint restano
`fnv1a64:5d66ebe092d31392` e `fnv1a64:c650ee934c5de4bb`.

Il kernel double-precision è collegato al traversal postflop, inclusi chance
node e street precedenti al river. Applica ProductionDcfr direttamente nello
spazio bucket, controlla 1.024 update contro l'oracolo scalare, certifica la
strategia sollevata nel gioco originale e salva `GTOSD_POSTFLOP_BUCKET_1` con
checksum e sostituzione atomica. Il resume segmentato è byte-identico al run
continuo; checkpoint exact e bucket restano incompatibili per costruzione.

La misura Release A0 a 32 iterazioni non giustifica la promozione. D è
exact-equivalent ma richiede `3,64483 s` operativi contro `0,662169 s` del solve
exact osservato. V richiede `7,89550 s` contro `1,01448 s`; NashConv è
`0,00330981` contro `0,00175170`, il massimo delta di profilo è `0,00590437`
ante e quello di BR `0,0109969` ante. Lo stato bucket è 1.152.832 B su D e
4.478.080 B su V, esclusi mapping e overhead dichiarati.

S3 combina le due primitive su una fixture con collisioni reali: 12 infoset
fisici diventano 4 bucket. La certificazione nel gioco originale riporta
NashConv `0,156251` per exact a 256 iterazioni e bucket-only, e `0` dopo il
resolve combinato. È una prova funzionale bounded, non una stima di speedup o
una qualification generale. R2-S è quindi chiusa come fattibilità A0; il
prodotto resta exact/no bucketing e R3 deve spiegare i costi prima di ogni
ulteriore ottimizzazione.

### Audit anti-specializzazione eseguito

L'audit riproducibile è in
`out/production-dcfr-anti-specialization-audit-20260905`. A 20 iterazioni fisse,
la baseline e la variante `GTP-META-777` riserializzata hanno mantenuto lo stesso
fingerprint, 1.288.290 azioni e dEV `0,10761127843895468`. La permutazione globale
dei semi ha mantenuto azioni, infoset e dEV. Le perturbazioni di range, stack e
sizing hanno invece cambiato il fingerprint e l'andamento matematico, lasciando
invariati algoritmo, codec, esponenti, intervallo, profondità e limite thread.

La ricerca statica negli entry point production trova due commenti descrittivi
nel solver e un parser benchmark v1 legacy vincolato a `GTP-AHKHQH-003`; non
trova dispatch del kernel v4 per ID. Il parser v1 non è un caller di prodotto e
resta separato per compatibilità. TH/TST esercitano strutture, range, stack e
sizing diversi con lo stesso profilo. Phase10 copre inoltre il dispatch attorno
al byte model: backend residente e page-backed materializzano checkpoint
byte-identici. L'audit R2-B è chiuso nel perimetro dichiarato. H è rimasto
sigillato e non è stato usato.

### Correzioni emerse dal gate GUI

Il primo E2E ha rilevato che il nuovo progetto impostava `Go all-in <150%`
mentre il contratto predefinito e il test richiedono fold/call/raise 60 senza
all-in automatico. Il default è ora `Disabled`. Il test di navigazione usava
poi una linea a reach zero subito dopo il reset ProductionDcfr di iterazione 2;
ora verifica bet-call, selezione chance e decision node turn senza inventare
analytics per massa nulla. Il successivo resume a iterazione 3 passa.

## R3 — Protocollo e ipotesi predefiniti

Il profiling usa soltanto D-RIVER-CHECK-001 e V-RIVER-BET-001. H resta
sigillato. Il runner esegue cinque processi Release indipendenti e sequenziali;
pubblica tutti i campioni, mediana e p95 nearest-rank, che con cinque campioni
coincide con il massimo. I timer strumentati non qualificano un candidato: un
eventuale gate finale richiede una build non strumentata sullo stesso HEAD.

Il wall A0 è additivo: layout, mapping, setup rank/stato/runner, training,
certificazione originale e finalizzazione del checkpoint. Il confronto exact
usa lo stesso perimetro `prepare + solve`. I contatori di nodi, chance, showdown
e action-entry provengono dal training. Il percorso policy/BR non espone ancora
contatori indipendenti: per R3 è disponibile il suo wall, mentre task wait, page
fault e traffico byte misurato sono `NOT AVAILABLE`, non zero. A0 è seriale per
costruzione; non esistono attese di worker da attribuire.

Le prime due ipotesi sono fissate prima del benchmark ripetuto:

| ID | Quota aggredibile e ceiling | Esperimento minimo | RAM e precisione | Criterio di scarto |
|---|---|---|---|---|
| R4-A1 | Il mapping vale circa il 46–58% del wall osservato. Anche azzerandolo, A0 resta fuori dal tempo exact nel campione diagnostico | Calcolare una sola volta la partizione `HandValue` per coppia board/player e riusarla nei nodi decisionali con lo stesso dominio privato | Solo cache transiente durante il build; mapping persistente e fingerprint invariati; nessun effetto su iterazioni o numerica | Scarto se la mediana mapping non cala almeno del 50% su entrambe D/V, se il picco transiente non è pubblicato o se cambia un solo byte logico del mapping |
| R4-B1 | Il training vale circa il 40–52% del wall e visita gli stessi nodi fisici dell'exact; il solo stato compatto non riduce il lavoro | Decodificare una strategia una volta per bucket e distribuirla ai membri, eliminando lookup e divisioni ripetuti per infoset esatto | Nessuno stato persistente aggiuntivo; stessa aritmetica DCFR e stesso checkpoint | Scarto se la mediana training non cala almeno del 15% su entrambe D/V, se preparation cresce oltre l'1% o se fingerprint, NashConv, profile/BR od oracle cambiano |

R4-A1 precede R4-B1 perché il costo di mapping è la singola fase maggiore e la
dipendenza `HandValue(board, combo)` è immutabile e verificabile. Nessuna delle
due ipotesi, da sola, ha ceiling sufficiente per promuovere A0: la composizione
si valuta soltanto se entrambe superano il proprio pre-gate.

### Risultati R3 e R4-A1/B1

I cinque campioni A0 hanno prodotto i seguenti secondi
`mapping / training / operational / exact operational`:

| Run | D | V |
|---:|---|---|
| 1 | 2,11892 / 1,64834 / 3,84195 / 0,822873 | 3,89111 / 4,28777 / 8,37360 / 1,12246 |
| 2 | 2,08174 / 1,52099 / 3,67412 / 0,683107 | 4,03828 / 4,36573 / 8,62473 / 1,13260 |
| 3 | 2,03107 / 1,53840 / 3,66617 / 0,699091 | 4,01376 / 4,26529 / 8,46074 / 1,09103 |
| 4 | 2,00079 / 1,56311 / 3,63522 / 0,713685 | 3,88354 / 4,61445 / 8,70773 / 1,16959 |
| 5 | 2,07693 / 1,76273 / 3,91163 / 0,766770 | 4,06977 / 4,59855 / 8,87517 / 1,20594 |

Le mediane A0 sono `2,07693 / 1,56311 / 3,67412 / 0,713685 s` su D e
`4,01376 / 4,36573 / 8,62473 / 1,13260 s` su V. I p95 nearest-rank sono
`2,11892 / 1,76273 / 3,91163 / 0,822873 s` e
`4,06977 / 4,61445 / 8,87517 / 1,20594 s`. Mapping e training rappresentano
rispettivamente il 56,53% e 42,54% del wall D, e il 46,54% e 50,62% del wall V.
La certificazione pesa 1,48% e 1,96%; setup e finalizzazione sono inferiori
all'1%.

R4-A1 e R4-B1 sono stati quindi misurati insieme, conservando tutti gli output
matematici. I campioni `mapping / training / operational / exact operational`
sono:

| Run | D | V |
|---:|---|---|
| 1 | 1,10401 / 1,46082 / 2,62380 / 0,768260 | 1,18798 / 3,61194 / 4,99257 / 1,23793 |
| 2 | 1,33972 / 1,44323 / 2,84909 / 0,747639 | 1,12057 / 3,42704 / 4,71446 / 1,10183 |
| 3 | 1,16472 / 1,36193 / 2,59148 / 0,722422 | 1,26607 / 3,40583 / 4,82040 / 1,18358 |
| 4 | 1,30443 / 1,19719 / 2,56830 / 0,667395 | 1,11206 / 3,19670 / 4,47245 / 1,07964 |
| 5 | 1,05326 / 1,42532 / 2,54243 / 0,751018 | 1,12587 / 3,40776 / 4,69348 / 1,15976 |

La mediana R4-A1 riduce il mapping del 43,92% su D e del 71,95% su V: fallisce
il pre-gate del 50% su entrambe. La cache richiede 6.316.136 B di payload
transiente stimato, oltre all'overhead allocator non misurato. R4-B1 riduce il
training dell'8,82% su D e del 21,94% su V: fallisce il pre-gate del 15% su
entrambe. La composizione resta 3,47 volte più lenta dell'exact su D e 4,07
volte su V. Entrambe le modifiche sono classificate `REJECTED_FEASIBILITY` e
rimosse; rimangono soltanto la telemetria R3 e il runner riproducibile.

Le metriche matematiche sono rimaste identiche in tutti i campioni: fingerprint
D `fnv1a64:5d66ebe092d31392`, fingerprint V
`fnv1a64:c650ee934c5de4bb`, oracle massimo `1,11022e-16`, NashConv V bucket
`0,00330981`, exact `0,00175170`, delta profilo `0,00590437` ante e delta BR
`0,0109969` ante. Il training A0 esegue 64 passate e aggiorna 32.507.904 entry
su D e 125.805.504 su V: la riduzione degli infoset non elimina il lavoro
fisico. R3 è chiusa; nessun ramo R4 offre il margine necessario. Si passa a R6
senza aprire H.

## R6 — Fattibilità di pruning e lazy update

### R6-A — Pruning rigoroso respinto

`RbpReadOnlyTelemetry` accetta ora anche i checkpoint `ProductionDcfr`, oltre ai
checkpoint DCFR legacy. Il cambiamento è diagnostico: non entra nel traversal e
non aggiunge campi al checkpoint. Phase7 conserva il differential OFF/ON
byte-identico; phase10 esegue il profilo product 1.5/0/3 su D/V ai checkpoint 20
e finale.

| Fixture | Iterazione | Regret negativi | Proxy CFR `m>=1` | Persistenti | Upper bound nodi |
|---|---:|---:|---:|---:|---:|
| D | 20 | 0 | 0 | 0 | 0 |
| D | 32 | 0 | 0 | 0 | 0 |
| V | 20 | 1.388.324 | 0 | 0 | 0 |
| V | 32 | 1.487.188 | 0 | 0 | 0 |

I metadati diagnostici sono 253.968 B su D e 990.528 B su V. Il checkpoint non
contiene reach controfattuale esatta, quindi `exact_zero_reach_available=false`.
La presenza di regret negativi non autorizza uno skip: nessuna entry supera
nemmeno il bound CFR originale per una singola iterazione. Inoltre quel bound
non è sound per gli sconti signed dipendenti dal segno, i reset della media e
l'averaging cubico di ProductionDcfr. R6-A è `REJECTED_CORRECTNESS` e
`REJECTED_FEASIBILITY`; non è stato implementato pruning.

### R6-B — Lazy update respinto al pre-gate

Il Lazy-CFR pubblicato accumula reward e reach dei round collassati per CFR/RM.
Non esiste nel materiale verificato una derivazione che ricostruisca la stessa
traiettoria alternating ProductionDcfr quando lo sconto cambia con il clock e
con il segno del regret, e quando la media viene azzerata agli epoch 1, 2, 5,
17 e 65. Applicare l'algoritmo pubblicato cambierebbe algoritmo; aggregare solo
gli incrementi perderebbe informazione necessaria.

Una implementazione lazy richiederebbe inoltre residui continui e accumulatori
per history/history-action. Il lower bound favorevole di un solo `float32` per
infoset aggiungerebbe 4.063.488 B su D e 7.991.808 B su V; non basta comunque a
rappresentare tutti i reward differiti. Il vecchio stop basato su un cap desktop
di 2 GB è ritirato e non viene riutilizzato. Il blocker corrente è matematico,
non quel cap. R6-B è `REJECTED_CORRECTNESS`; non è stato scritto un prototipo.

### Esito R6-E e chiusura della roadmap

R6-C/D restano fuori mandato perché sostituiscono ProductionDcfr o ne cambiano
la schedule. Non rimane una famiglia autorizzata con un ceiling sufficiente.
H resta sigillato: senza un candidato che superi D/V, consumarlo non produrrebbe
una conferma valida. R7 non è raggiunta e R8 resta fallita sui gate tempo TH/TST
e `NOT_EVALUATED_COMPARABILITY_UNRESOLVED` sulla RAM GTO+.

L'esito della roadmap è `BLOCKED_WITH_EVIDENCE`. Per riaprirla serve una sola
delle seguenti nuove condizioni: una prova e una rappresentazione sostenibile
per pruning/lazy ProductionDcfr signed, una nuova architettura che riduca il
lavoro fisico senza perdita di informazione, oppure l'autorizzazione esplicita a
valutare un algoritmo diverso. Il prodotto resta ProductionDcfr exact; A0,
bucketing e subgame restano oracle/prototipi isolati e non diventano default.

## Validazione finale 2026-09-06

Con MSVC 18.8 x64 e il build tree `windows-release-current`:

- build Release completa: PASS;
- phase7: PASS, 216 asserzioni;
- phase10 bucket: PASS, 763.823 asserzioni;
- phase10 R6-RBP: PASS, 8 asserzioni;
- CTest Release completo: PASS, 33/33 in 270,43 s.
- phase10 ASan mirato: bucket PASS, 763.823 asserzioni; R6-RBP PASS, 8
  asserzioni; nessuna diagnostica sanitizer.
- `git diff --check`: PASS. `format-check` globale: FAIL su violazioni diffuse
  già presenti nel worktree, a partire da
  `benchmarks/exact_state_representation_benchmark.cpp`; nessuna formattazione
  massiva è stata applicata perché avrebbe riscritto modifiche utente fuori dal
  delta R3/R6.

Il run bucket successivo alla rimozione di R4-A1/B1 conserva i fingerprint D/V,
l'oracolo massimo `1,11022e-16` e le metriche matematiche registrate sopra. H non
è stato letto o eseguito durante questa validazione.

## Follow-up 2026-09-06 — kernel river bucket-native

Il follow-up implementa una rappresentazione astratta che elimina i deal
fisici dal traversal river. La preparazione enumera i deal una volta, applica i
pesi dei range e card removal, quindi aggrega per coppia di `HandValue` finale.
Il runtime conserva un albero pubblico, righe di stato `(decisione, bucket del
player attivo)` e payoff terminali per vittoria P0, vittoria P1 e tie.

Il solver accetta soltanto ProductionDcfr `1.5/0/3`, un thread e il contratto
numerico corrente. Espone query combo-per-combo e un lift bounded sul gioco
fisico per la certificazione originale. Il checkpoint usa l'envelope
`GTOSD_RIVER_BUCKET_CHECKPOINT 1 0`, identità source/abstraction separate,
sostituzione atomica, checksum e resume byte-identico. Checkpoint exact, astrazioni
diverse e payload fuori limite vengono rifiutati.

### Risultati

| Fixture di test | Deal fisici | Coppie bucket | Nodi/passata fisici | Nodi/passata bucket | Byte physical oracle | Byte bucket con oracle |
|---|---:|---:|---:|---:|---:|---:|
| RIVER-MEDIUM-WEIGHTED-001 | 8.640 | 90 | 77.761 | 811 | 15.082.610 | 167.872 |
| RIVER-MEDIUM-WEIGHTED-002 | 8.640 | 70 | 77.761 | 631 | 15.082.610 | 132.636 |

Le mediane di cinque processi Release a 128 iterazioni sono 7,2651/3,1269 ms
exact/native per D e 8,0537/3,0401 ms per V. La NashConv calcolata dopo il lift
nel gioco originale è 0,000202927/0,00377002 su D e
0,0000145436/0,000511609 su V. Il guadagno non è equivalenza: combo con lo
stesso valore finale condividono la strategia anche quando hanno blocker
diversi.

La fixture full range conta 188.790 deal, 611 coppie, 25 bucket per player e 9
nodi pubblici. Il lavoro per passata scende da 1.699.110 a 5.499 nodi; il byte
model nativo predefinito è 55.972 B, escluso overhead allocator. Il gioco
`FiniteGame` di validazione non viene materializzato nel percorso normale.

Validazione: target Release `gtosd_postflop_subgame_tests` PASS con 313
asserzioni; suite correlata Release PASS 3/3; stessa suite AddressSanitizer PASS
3/3; la riconferma finale del target river passa in 211,36 s senza diagnostiche.
Stato finale:
`FEASIBILITY_ONLY`. Exact resta default; CLI, GUI, `.gtsd`, più thread e street
precedenti non sono implementati per questo kernel. Il CTest Release completo
sullo stato finale di quel checkpoint passa 33/33 in 279,25 s.

## Follow-up 2026-09-06 — qualifica River v1

Prima del primo solve sono stati fissati corpus, semi, 256 iterazioni, cinque
ripetizioni alternate e otto soglie per fixture. Il manifest congelato ha
SHA-256 `7E542407BCCF598BA5E3A0CB5A3F8ECCEBF0DE3D8DB70A79E99D1DDFE73F2619`
e fingerprint runner `fnv1a64:8be8d95b06ac9768`. Il preflight passa 7/7 entro
i limiti di deal, coppie bucket e nodi dell'oracolo.

Ambiente: Windows `10.0.26200.9168`, Intel Core i3-10100F 3,60 GHz, 4 core/8
thread logici, 31,94 GiB disponibili, MSVC 14.51.36231 da Visual Studio 18.8,
C++20 Release `/O2 /Ob2 /DNDEBUG /MD`. Exact usa il profilo product
`ScaledUint16RegretStrategy` e parallel action depth 7; il kernel bucket usa
`float64` e un thread. Il seed del manifest è
`5932453446437656385`.

### Risultato Release

| Fixture | Exact/native mediana | Speedup | Riduzione nodi | Exact/bucket NashConv | Esito |
|---|---:|---:|---:|---:|---|
| paired rainbow | 11,1662 / 45,8101 ms | 0,244x | 12,37x | 0,00003198 / 0,00698764 | FAIL |
| five-card monotone | 9,9197 / 4,2290 ms | 2,346x | 298,44x | 0,00003869 / 0,00777115 | FAIL |
| four-flush paired | 14,3474 / 73,3181 ms | 0,196x | 12,85x | 0,00026681 / 0,01138636 | FAIL |
| double-paired rake | 7,1408 / 9,9066 ms | 0,721x | 62,40x | 0,01319316 / 0,00686519 | FAIL |
| straight board | 15,8480 / 3,9005 ms | 4,063x | 892,33x | 0,00076581 / 0,05338713 | FAIL |
| ace-low straight | 6,2764 / 1,5502 ms | 4,049x | 889,33x | 0,00000069 / 0,00550591 | FAIL |
| high-card two-tone | 10,2014 / 104,5430 ms | 0,098x | 8,34x | 0,00018510 / 0,00836814 | FAIL |

Decisione: **`REJECTED`, 0/7**. Tutte le fixture falliscono bucket NashConv e
delta NashConv; quattro falliscono speedup, quattro delta BR e una riduzione
nodi. Profile-value delta e byte model passano 7/7. Il caso con rake fallisce
anche exact NashConv: è inadeguato come confronto conclusivo a 256 iterazioni,
ma fallisce comunque bucket NashConv, delta BR e speedup.

Il timer exact comprende build, solve e certificazioni del percorso product;
quello native comprende build e solve bucket. Lift e BR fisica sono fuori dal
timer native e restano lavoro di qualifica separato. Questa asimmetria favorisce
il candidato: includerle potrebbe soltanto ridurne lo speedup, quindi non rende
meno conservativo il `REJECTED`, ma vieta di usare questi tempi come previsione
del workflow completo.

ASan esegue il test River da 313 asserzioni in 199,06 s, il preflight in 0,30 s
e la qualifica end-to-end senza diagnostiche. Il report ASan conserva gli stessi
NashConv del Release; i tempi strumentati non partecipano alla decisione.
Report autoritativo:
`benchmarks/results/river_bucket_qualification_2026-09-06.json`.
SHA-256 report Release:
`0C79C5AD89374039E6B908B57826E753DDC4D6B05603CACB380FB85B91AD3508`;
report ASan:
`DF2268E7C010832044272BE01B2B046F2623002CF9F4B13FA1E29E3CFAC367D7`.

Build Release completa PASS; il CTest corrente, dopo il follow-up v2, passa
35/35 in 260,92 s.

Il risultato chiude il kill gate del candidato 1.0. Il corpus non viene
ritoccato. Exact resta production; nessuna estensione a Turn o preflop è
autorizzata da questa evidenza.

## Follow-up 2026-09-06 — River v2 blocker-aware lossless

`ExactBlockerSignatureV2` partiziona le combo usando la coppia composta da
`HandValue` finale e bit vector di compatibilità contro il range avversario
attivo. Il builder applica board removal e pesi prima della partizione. Chiavi
infoset e fingerprint includono la versione dell'astrazione; la modalità v1
mantiene il proprio tag e i fingerprint precedenti.

I test aggiunti coprono due proprietà. Una collisione davvero equivalente
condivide la strategia e coincide con profile value, BR e NashConv fisici entro
`1e-12`. Una collisione per solo valore della mano viene invece separata quando
i blocker differiscono. Per ogni classe il test confronta la compatibilità di
ogni membro contro tutte le combo avversarie. Sul full range il lower bound
osservato è 465 classi per player, 188.790 coppie e 6.620.596 B.

Il manifest
`benchmarks/fixtures/river_exact_blocker_qualification_corpus_v2.json` è stato
sigillato prima del primo solve. Contiene le sette fixture v1 come regressione e
cinque holdout nuovi: unpaired rainbow, trips board, full-house con rake,
four-straight two-tone e four-flush high. SHA-256 manifest:
`F3E262337D5481651D80101C6AD9A09AD8BBC146379FE10D794B169ACED95B14`;
fingerprint: `fnv1a64:812e7edf4d682820`.

### Risultato v2

Il preflight trova una classe per combo e una coppia per deal in tutte le 12
fixture. La qualifica Release a 1.024 iterazioni e cinque ripetizioni restituisce
`REJECTED`, 0/12:

- riduzione nodi: 0/12, sempre `1,00x`;
- speedup: 0/12, da `0,006709x` a `0,017259x` exact/native;
- byte model: 12/12, da 373.856 a 793.104 B;
- profile-value delta: 12/12;
- insieme dei gate di qualità: 11/12.

La sola eccezione di qualità è `RQ-DOUBLE-PAIRED-RAKE-001`. L'exact quantizzato
resta a NashConv `0,0131932` alla quota fissata; il candidato double converge a
zero e supera quindi anche il limite di delta BR/NashConv rispetto alla
traiettoria product. Non è abstraction error: la partizione contiene comunque
96 classi per 96 combo. La fixture resta un confronto non conclusivo sulla
traiettoria numerica, ma il candidato fallisce già i gate strutturale e tempo.

Report autoritativo:
`benchmarks/results/river_exact_blocker_qualification_2026-09-06.json`, SHA-256
`B872A274662FA485A121D75E6795505C10BCBF7EF0DEF9C9DE715D17F449F459`.

Validazione: test River Release PASS, 338 asserzioni; lo stesso target ASan
PASS in 209,04 s; preflight v2 ASan PASS in 2,03 s; CTest Release completo
35/35 PASS in 260,92 s. La qualifica completa prestazionale è stata eseguita
solo in Release; i controlli ASan coprono invarianti, full range e tutte le
fixture preflight senza usare tempi strumentati come benchmark.

Decisione: `REJECTED_FEASIBILITY`. Il kernel v2 resta isolato per ricerca e
regressione. Exact resta il prodotto; CLI, GUI, `.gtsd`, Turn e preflop non
cambiano.

## Follow-up 2026-09-06 — fattibilità River lossless generale

È stato aggiunto un analizzatore bounded della partizione equa pesata del grafo
bipartito delle combo compatibili. I colori iniziali conservano player e
`HandValue`; ogni round aggiunge, per ciascuna classe avversaria, la somma dei
pesi delle combo compatibili. Il raffinamento termina quando le classi sono
stabili e una verifica indipendente ricontrolla l'equità.

Il test full-range termina in due round con 45 classi per player, 2.005 coppie
di classi e 188.790 deal fisici: riduzione righe `10,3333x`, riduzione coppie
`94,1596x`. Il manifest v2 produce invece classi singleton su tutte le 12
fixture: 96/96 sui sette casi di regressione, 112/112 sui cinque holdout e
riduzione `1,00x` ovunque. Il risultato complessivo del runner è `BLOCKED`.

Artefatto:
`benchmarks/results/river_joint_equitable_feasibility_2026-09-06.json`, schema
`gtosd.river_joint_equitable_feasibility_report.v1`, fingerprint manifest
`fnv1a64:812e7edf4d682820`, SHA-256
`9E75FD396393FD4EB49C23881F8233237CDE22004ED660E0E4322BC287565C69`.

Il preflight passa in Release e ASan. Il test postflop e il nuovo preflight
passano insieme sotto ASan in 198,94 s. Poiché il gate strutturale fallisce
prima del solving, non è stato implementato un kernel v3 e nessuna superficie
product è cambiata.

La suite Release completa sullo stato finale passa `36/36` in `267,95 s`.

## Follow-up 2026-09-08 — ledger streaming della certificazione preflop

Il certificatore HU preflop ora consuma le boundary Flop una alla volta. Per
ciascuno dei 5.157 task canonici conserva il lato CO/BTN già ricevuto e la
probabilità fisica associata. Rifiuta duplicati prima di mutare lo stato e
conteggia la massa soltanto quando entrambi i lati sono presenti.

Il ledger ha fingerprint semantico, hash incrementale dei mask e catena
ordinata dei contributi. Serializzazione e file checkpoint 1.0 controllano
schema, dimensioni, valori finiti, checksum e identità. Salvataggio e
sostituzione sono atomici; dopo il caricamento il ledger viene ricontrollato
contro tree, blueprint, piano e catalogo dei task.

Un secondo hash combina slot canonico e fingerprint di ciascuna boundary. Il
fingerprint risultante identifica il profilo postflop indipendentemente
dall'ordine di consegna dei worker. La BR globale 1.1 deve dichiarare la stessa
identità; una BR calcolata su continuazioni diverse fallisce chiuso.

Il resource model del piano 1.2 misura `5.157 B` per i mask, `41.256 B` per le
probabilità, `46.413 B` per il ledger e `59.085 B` per `ledger + una boundary`.
La materializzazione delle boundary canoniche resta `130.699.008 B` e non è più
necessaria per la certificazione.

Il test end-to-end costruisce e valida `10.314/10.314` boundary e completa
`5.157/5.157` task. Senza best response globale restituisce
`GLOBAL_BEST_RESPONSE_MISSING`; con una risposta campionata restituisce
`GLOBAL_BEST_RESPONSE_NOT_EXACT`. Il test copre anche i gate exact sopra e sotto
la soglia NashConv. HU e preflight passano `2/2` in `30,13 s`.
Il benchmark Release passa da solo in `3,71 s` e dentro CTest in `3,42 s`; la
suite Release completa passa `42/42` in `255,69 s`. Fingerprint piano:
`fnv1a64:55a80db03369199d`. Il target HU preflop passa sotto
AddressSanitizer in `300,37 s` senza diagnostiche.

Restano da integrare i contributi terminali Flop/Turn con gli aggregati River
per produrre boundary Flop complete, quindi calcolare una best response globale
exact. I default ProductionDcfr non sono cambiati.

## Follow-up 2026-09-08 — lift orbitale e reach upper-street

Il test per-combo ha rilevato che la riduzione River applicava la molteplicità
del board canonico senza permutare l'indice della combo privata. La massa
totale poteva restare corretta mentre blocker di semi diversi ricevevano pesi
errati. Il bridge ora enumera l'orbita del runout che stabilizza il Flop,
trasforma insieme la combo privata e applica separatamente la molteplicità del
Flop rappresentato.

Una boundary River passa da 465 a 528 righe: include tutte le combo vive sul
Flop, anche quando una combo è bloccata sul singolo board River
rappresentante. L'accumulatore consuma valori già pesati e non applica più una
seconda moltiplicazione. L'oracolo con reach e utility unitarie verifica per
ogni combo la massa `molteplicità Flop × 31 × 30 × 406` su tutte le orbite del
primo task.

Il formato precedente non è compatibile con questa semantica. Root boundary e
accumulatore passano a 1.1; accumulator e aggregate JSON usano gli schema v2;
il batch plan passa a 1.4. I loader rifiutano esplicitamente payload v1. Il
record denso è ora 12.672 B, la materializzazione teorica 9.288.284.442.624 B e
lo scheduler da 64 MiB produce 141.687 batch con massimo 5.294 boundary e 836
nell'ultimo batch.

Le shape Turn/River conservano ora anche le azioni complete. Il replay verifica
legalità, cambi street, stato finale e fingerprint. Il nuovo propagatore usa un
provider esplicito della strategia media, applica la probabilità soltanto al
player che agisce e mantiene le reach sulle 630 combo fisiche con blocker
street-by-street. Probabilità non finite o fuori `[0,1]` falliscono chiuso.

Un catalogo separato enumera tutte le terminazioni prima della root River:
3.792 history, di cui 612 sul Flop, 3.180 sul Turn, 2.388 fold e 1.404 all-in
runout. Ogni history viene riprodotta e confrontata con lo stato terminale;
una sequenza troncata viene rifiutata. Questo è il manifesto di copertura per i
contributi terminali, non ancora il loro calcolo CFV.

Validazione intermedia: test HU 6.669 asserzioni PASS; HU, preflight e benchmark
di decomposizione `3/3` PASS in `38,93 s`; suite Release completa `42/42` PASS
in `258,86 s`; HU preflop AddressSanitizer PASS in `362,36 s` senza
diagnostiche. Restano da collegare il provider a una strategia upper-street
concreta, aggiungere i terminali Flop/Turn e calcolare la best response globale
exact. I default ProductionDcfr sono invariati.

## Follow-up 2026-09-08 — terminali upper-street e canale River BR v4

L'assemblatore task-local integra l'aggregato River della strategia media con
i 3.792 terminali Flop/Turn replayabili. Calcola fold e showdown exact,
verifica ordine e fingerprint dei contributi e conserva per ogni combo la
massa `molteplicità Flop × 812 × reach preflop compatibile` prima di emettere
le due boundary Flop.

Il solver postflop espone inoltre CFV root per-combo sotto best response exact.
Il bridge applica anche a questo report il lift orbitale blocker-aware e lo
accumula in un canale River task-local distinto. Boundary, accumulatore e
aggregato dichiarano `AverageStrategy` o `ExactBestResponse`; modalità diverse
non possono essere mescolate e l'assemblatore Flop del profilo rifiuta un
aggregato BR. Gli accumulatori e aggregati persistiti usano schema v4 e
rifiutano v3, che non dichiarava la modalità.

I test mirati Release passano `2/2` in `99,29 s`, la suite Release completa
passa `42/42` in `318,07 s` e il target HU preflop passa sotto AddressSanitizer
in `1.115,26 s` con 8.971 asserzioni e nessuna diagnostica. Restano la
ricorsione BR Flop/Turn e quella preflop; nessun default production è cambiato.

## Follow-up 2026-09-08 — scheduling Turn-major della BR

Il catalogo River può ora derivare, per un task Flop, gruppi di runout con lo
stesso Turn canonico. Ogni gruppo espone uno span di ordinali per ciascuna
history River. Gli span mantengono adiacenti le due boundary dei resolver e la
loro unione copre ogni root del task esattamente una volta.

La vista è task-local e non duplica boundary o cataloghi. Permette al futuro
riduttore BR di completare e liberare un Turn prima del successivo, preservando
il punto informativo corretto in cui il player sceglie l'azione. Il test HU
Release passa con 8.975 asserzioni; restano da implementare le foglie terminali
BR per Turn e la ricorsione `somma avversario / max rispondente`.
