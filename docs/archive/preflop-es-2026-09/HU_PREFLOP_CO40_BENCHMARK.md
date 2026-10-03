# Benchmark HU preflop CO 40a

Data di acquisizione: 2026-09-06. Stato all'11 settembre 2026: **gate monetario
PASS; V6/V7 sono stati rigenerati a 2M sul fingerprint corrente, ma il gate
scientifico resta FAIL**.

> **Aggiornamento del 2026-09-15.** Il programma external sampling descritto dalla sezione
> "Primo solve R9-C v1" in giù è chiuso. I gate di questo documento basati sulle frequenze Monker
> non sono più in vigore: valgono le decisioni D1–D3 del
> [registro](../../research/PREFLOP_ARCHITECTURE_DECISION_LOG.md). Restano validi fixture, contratto
> monetario, provenienza e conteggi dell'albero. Evidenze rimosse: vedi
> [PREFLOP_LEGACY_INDEX.md](PREFLOP_LEGACY_INDEX.md).

Fixture SHA-256:
`D835898479493B24FAC3CE2686B43119CBBED40B5CEF40A01E25BC62B2DBE811`.
Configurazione del gioco SHA-256:
`5BA2FC78567CFD109B34BFDC9A208D13BCD08B99CDBB986FE155E407879E0619`.
Fingerprint del riferimento: `fnv1a64:f78898249087ee6d`.
Fingerprint dell'albero locale: `fnv1a64:a68337fa567aa2d9`.

## Contratto

`GTP-HU-PREFLOP-CO40-001` è l'unico benchmark esterno autorizzato per il solver
HU preflop. I giochi piccoli, gli oracle matematici e i test di regressione non
sono benchmark alternativi: servono a verificare il motore.

Il riferimento fissa:

- Short Deck HU, stack effettivo 40 ante e CO primo attore;
- ante morte CO 1a e BTN 1a, più button blind live BTN 1a; pot root 3a e call
  incrementale richiesto al CO pari a 1a;
- azioni CO `fold`, `call`, raise live a 6a, raise live a 10a e all-in live a
  39a; i contributi totali, ante inclusa, sono rispettivamente 1a, 2a, 7a,
  11a e 40a;
- strategia completa sulle 81 classi exact, con masse fisiche `6/4/12`;
- EV CO dello spot `-0,3a`.

Le size preflop sono target monetari live assoluti. Non vengono approssimate
come percentuali del pot.

La configurazione dichiarativa
`benchmarks/fixtures/hu_preflop_co40_game_v1.json` implementa le continuazioni
preflop confermate e l'albero postflop locale:

- BTN contro raise-to 6a: fold, call, raise-to 10,5a, all-in;
- BTN contro raise-to 10a: fold, call, raise-to 14,5a, all-in;
- dopo il limp BTN può checkare o scegliere 6a, 10a e all-in; CO riceve le
  stesse risposte;
- dopo il re-raise configurato restano fold, call e all-in;
- flop, turn e river usano 33%, 66%, 120% e all-in per bet e raise, finché lo
  stack termina naturalmente la sequenza;
- rake disabilitata.

L'equivalenza dell'albero preflop esterno è confermata. L'equivalenza
dell'albero postflop Monker non è confermata ed è in attesa di nuove
informazioni; le size postflop sopra descrivono quindi il candidato locale,
non un fatto noto sul solve esterno.

I raise-to 10,5a dopo 6a e 14,5a dopo 10a aumentano la puntata di 4,5a, meno
dei precedenti incrementi completi di 5a e 9a. Il core li accetta solo quando
`allow_configured_incomplete_raise=true`; tutti gli altri giochi continuano a
rifiutare un non-full raise non all-in.

## Provenienza e dati non disponibili

Il 7 settembre 2026 l'utente ha confermato rake `0%`. Il 9 settembre ha
identificato la sorgente come una versione modificata di MonkerSolver e ha
confermato lo stesso ranking del postflop GTOSD: colore sopra full house e
scala bassa `A-6-7-8-9`. Il 10 settembre ha precisato la composizione delle
puntate obbligatorie, il costo incrementale del call e che l'albero postflop
esterno potrebbe essere diverso. Metodo e quantità dei bucket per street,
abstraction dipendenti dalla history, versione precisa, numero di iterazioni,
criterio e metrica di arresto, tempo esatto, CPU e RAM del run esterno restano
sconosciuti. L'ipotesi di arresto all'1% non è trattata come evidenza. Resta
confermato soltanto un wall time inferiore a 12 ore per questo spot.

Ogni candidato deve dichiarare la rake applicata; una rake diversa da zero
fallisce il gate.

Questa incompletezza ha una conseguenza precisa: possiamo misurare quanto il
nostro output assomiglia al target, ma senza la metrica di convergenza esterna
una differenza non prova da sola un bug del solver. Il limite `<12 h` è però un
vincolo prestazionale autorevole: un'architettura che richiede più tempo per
una sola sweep non è competitiva con il run di riferimento. Non si ritoccano
regole o payoff in modo nascosto per migliorare il confronto.

## Frequenze e arrotondamento

La fixture conserva il testo ricevuto. Quattordici classi sommano a `101%` per
arrotondamento intero: `JJ`, `TT`, `88`, `77`, `K9s`, `K8s`, `K7s`, `QJs`,
`Q9s`, `Q8s`, `J8s`, `T7s`, `QTo`, `98o`.

Il confronto divide ogni riga per il totale riportato, senza riscrivere il dato
originale. Le frequenze root risultanti, pesate sulle 630 combo fisiche, sono:

| Azione | Frequenza |
|---|---:|
| All-in | 40,7789% |
| Raise 6a | 2,40789% |
| Raise 10a | 4,71957% |
| Call | 7,56675% |
| Fold | 44,5269% |

## Gate

Gate in vigore dal 2026-09-15 (decisioni D1–D4 del registro):

| Metrica | Ruolo |
|---|---|
| Massimo guadagno di deviazione per giocatore nel gioco fisico CO40, best response esatta della strategia sollevata | gate di qualificazione: ≤ 0,1 ante per mano (D2); si riportano anche NashConv, la normalizzazione sul pot iniziale (3a) e sullo stack (40a) |
| Massimo guadagno nel gioco astratto, stimato sui board campionati | criterio di arresto del training: ≤ 1 % del pot iniziale (D3) |
| EV CO con intervallo al 95 % | descrittivo; il valore Monker −0,3a è un controllo di sanità (D4) |
| Distanza dalle frequenze Monker (WMAE, TV, P95, root), pesata per perdita EV | descrittiva; stato permanente `EXTERNAL_CONTRACT_INCOMPLETE` (D1) |
| Rake | disabilitata (`0%`), invariata |

La tabella seguente è il gate storico del programma external sampling, conservata per
riferimento e non più in vigore:

| Metrica | Limite |
|---|---:|
| Rake | disabilitata (`0%`) |
| Errore assoluto medio action/class, pesato per combo | <= 1,0 punti percentuali |
| Total variation media per classe, pesata | <= 2,0 punti percentuali |
| Errore massimo sulle frequenze root aggregate | <= 1,0 punti percentuali |
| P95 total variation per classe | <= 5,0 punti percentuali |
| Delta EV root CO | <= 0,05a |
| NashConv normalizzata sullo stack effettivo | <= 1% e certificata |

La vicinanza al riferimento non certifica nulla: due equilibri diversi, o due astrazioni
diverse, producono frequenze diverse sulle azioni quasi indifferenti con lo stesso EV. Il
comparatore verrà aggiornato alle nuove semantiche nella fase P8 della roadmap.

## Preflight

```powershell
out/build/windows-release-current/benchmarks/gtosd_hu_preflop_reference.exe `
  --fixture benchmarks/fixtures/hu_preflop_co40_reference_v1.json `
  --preflight-only
```

Il preflight valida identità, classi, tag percentuali, somme 100/101, masse
fisiche, catalogo azioni e quantità prodotte dal game state. Non esegue ancora
un solve.

Il runner accetta anche un candidato conforme a
`gtosd.hu_preflop_candidate.v1`:

```powershell
out/build/windows-release-current/benchmarks/gtosd_hu_preflop_reference.exe `
  --fixture benchmarks/fixtures/hu_preflop_co40_reference_v1.json `
  --candidate candidate.json `
  --output comparison.json
```

Il candidato contiene 81 righe normalizzate, EV root, NashConv normalizzata,
stato della certificazione, rake dichiarata e fingerprint dell'albero. Una
stima di risposta campionata con `nashconv_certified=false` fallisce sempre il
gate, anche se il lower bound osservato è zero. Il runner restituisce `0` per
`QUALIFIED`, `2` per `REJECTED` e `3` per input o schema non validi. Anche un
PASS conserva `configuration_comparability=REFERENCE_CONFIG_INCOMPLETE` finché
la configurazione esterna mancante non viene recuperata.

Il run esplorativo con rake 5%/cap 3a è soltanto un'analisi di sensibilità:
non può qualificarsi contro questo benchmark, anche quando il suo EV aggregato
è vicino a `-0,3a`.

## Lower bound della chance

Il conteggio exact indipendente dalle size successive è:

| Oggetto | Conteggio fisico | Lower bound dopo al massimo 24 permutazioni dei semi |
|---|---:|---:|
| Deal privati ordinati e compatibili | 353.430 | — |
| Deal privati + flop | 1.753.012.800 | 73.042.200 |
| Deal privati + board completo | 71.172.319.680 | 2.965.513.320 |

Il lower bound usa soltanto la cardinalità massima del gruppo dei semi; non
afferma che ogni orbita abbia dimensione 24. Dimostra però che materializzare
il full game prima della decomposizione non è una strategia desktop credibile.
Il resource estimator successivo dovrà contare boundary, stato DCFR, scratch e
recovery senza moltiplicare implicitamente ogni subgame postflop.

## Albero e resource gate

Il generatore produce 58 nodi preflop: 20 decisioni, 9 ingressi postflop, 19
terminali fold e 10 terminali all-in. Il fingerprint è
`fnv1a64:c87905ceb69551a1`.

Con 33/66/120/all-in il conteggio dinamico produce 30.324 nodi nello scheletro
pubblico postflop, 11.308 decisioni e 29.112 archi azione. La profondità
massima è 15 e il massimo osservato è quattro raise per street. Il limite
interno di sicurezza è 63: nessun ramo lo raggiunge e lo stack 40a dimostra la
terminazione naturale.

## Storico del programma external sampling (chiuso il 2026-09-15)

Le sezioni seguenti descrivono i candidati del programma chiuso. Sono conservate come storia
del benchmark; i loro gate, le loro baseline e le loro proiezioni non guidano il nuovo lavoro.
I documenti e gli artefatti citati sono al tag `preflop-legacy-es-2026-09-15`.

## Primo solve R9-C v1

Il primo candidato usa:

- 81 classi exact al preflop e deal fisici per card removal;
- bucket postflop `categoria × equity` con otto campioni deterministici per
  street e memoria dei bucket precedenti;
- external-sampling DCFR alternato `1.5/0/3`;
- showdown exact e rake zero.

Il run iniziale da 20.000 iterazioni termina in 13,810 s con 1.444.270
infoset. Il run diagnostico da 100.000 iterazioni termina in 75,141 s con
6.602.077 infoset. Entrambi usano un solo thread.

Ogni riga sparse occupa almeno 136 B fra chiave e payload DCFR, senza bucket
table, nodi dell'`unordered_map` o allocator. Il blueprint 20k richiede quindi
almeno 196.420.720 B; includendo le risposte campionate il payload minimo è
206,45 MiB. A 100k il solo blueprint sale a 897.882.472 B e il totale minimo a
949,06 MiB. Questi valori non sono Peak RSS.

| Metrica | 20k | 100k | Gate |
|---|---:|---:|---:|
| EV CO | +0,5657a ±0,1678a | +0,5958a ±0,1027a | −0,3a ±0,05a |
| MAE action/class | 22,499 pp | 21,763 pp | <=1 pp |
| TV media per classe | 56,248 pp | 54,407 pp | <=2 pp |
| Errore root massimo | 17,770 pp | 27,896 pp | <=1 pp |
| P95 TV | 88,581 pp | 93,704 pp | <=5 pp |
| NashConv | non certificata | non certificata | certificata <=1% |

Decisione: **`REJECTED`**. Cinque volte più iterazioni riducono poco gli errori
per classe e non spostano l'EV verso il riferimento. Il prossimo candidato deve
migliorare la rappresentazione postflop e la stima BR; non basta prolungare il
run v1.

## Oracle ridotto exact-vs-bucket 2026-09-07

Prima di modificare ancora il solver preflop, il bucket River
`made_hand_value_v1` è stato isolato in sette giochi ridotti. Ogni fixture
risolve sia il gioco fisico exact sia il gioco bucketizzato con ProductionDcfr
`1.5/0/3`; la strategia bucket viene poi sollevata sulle combo fisiche. Il
confronto avviene quindi nello stesso gioco originale e misura strategia root,
CFV per infoset e azione, BR e NashConv certificata.

| Intervallo sul corpus | Risultato |
|---|---:|
| Riduzione nodi per passata | 8,34x–892,33x |
| NashConv exact | 0,0001%–1,3193% |
| NashConv bucket nel gioco fisico | 0,5506%–5,3387% |
| TV media della strategia root | 0,00%–37,26% |
| Errore CFV medio, normalizzato sul pot | 0,39%–7,41% |
| Errore CFV massimo di una azione | 28,80%–88,76% |

Decisione: **`REJECTED` 7/7**. La fixture con rake non è un confronto pulito
di errore d'astrazione perché anche l'oracolo exact resta a `1,3193%` di
NashConv alle iterazioni fissate. Le altre fixture mostrano comunque lo stesso
problema: comprimere per sola categoria finale lega combo con blocker e risposte
ottime diverse. In `RQ-ACE-LOW-STRAIGHT-001` la strategia root aggregata
coincide, ma la CFV media differisce del `3,50%`: guardare soltanto le frequenze
root avrebbe nascosto l'errore downstream.

Il Peak RSS dell'intero processo di qualifica ridotto è `107.356.160 B`
(`102,38 MiB`). Non è una stima del solve HU preflop completo: il report tiene
separati Peak RSS di processo, delta RSS per fase e byte model dello stato.

Questo oracle non sostituisce `GTP-HU-PREFLOP-CO40-001`, che resta l'unico
benchmark esterno autorizzato. Stabilisce invece che R9-C v2 non deve riusare il
bucket postflop v1: serve una rappresentazione sensibile a distribuzioni,
transizioni e blocker, oppure decomposizione con boundary CFV verificabili.

## Candidato R9-C v2: showdown distribution v3

Il candidato v3 conserva l'intero `HandValue` River e aggiunge dodici feature:
massa pesata del range avversario compatibile nelle nove categorie finali e
masse hero loss/tie/win. Le masse sono normalizzate rispetto al range avversario
prima del card removal della mano hero e arrotondate al 5%. Versione, quantum e
partizione entrano nel fingerprint; checkpoint v1, v2 e v3 non sono
intercambiabili.

Cinque holdout sono stati congelati prima del primo solve v3. La qualifica usa
anche le sette regressioni v1, ProductionDcfr `1.5/0/3`, 1.024 iterazioni e
cinque ripetizioni alternate exact/native.

| Gate | Pass |
|---|---:|
| Exact NashConv | 11/12 |
| Bucket NashConv | 7/12 |
| Delta NashConv | 2/12 |
| Delta profile value | 12/12 |
| Delta best response | 6/12 |
| Speedup operativo | 0/12 |
| Riduzione nodi minima | 5/12 |
| Byte model | 12/12 |

Decisione: **`REJECTED`**, inclusi 0/5 holdout. Le coppie bucket sono
434–9.240; la riduzione varia da `1,18x` a `18,57x`, ma il tempo native è solo
`0,008x–0,131x` quello exact. La NashConv fisica arriva al `2,6527%`, la TV
root al `32,37%` e l'errore CFV medio al `4,07%` del pot. Il profile value passa
12/12, mostrando che l'EV aggregato non rileva da solo l'exploitability
introdotta.

Il Peak RSS dell'intero workflow è `155.385.856 B` (`148,19 MiB`). Il
candidato resta isolato nel benchmark River e non viene collegato al solver HU
preflop, alla CLI, alla GUI o ai formati di prodotto. Il quantum congelato non
è stato ritoccato dopo l'holdout.

Comando riproducibile:

```powershell
out/build/windows-release-current/benchmarks/gtosd_hu_preflop_solve.exe `
  --config benchmarks/fixtures/hu_preflop_co40_game_v1.json `
  --output benchmarks/results/hu_preflop_co40_first_candidate_v1.json `
  --iterations 20000 --evaluation-deals 20000 `
  --br-iterations 5000 --br-evaluation-deals 10000 `
  --equity-samples 8 --seed 5207644666046803969
```

## Validazione corrente

- build mirata Release con warning-as-error: PASS;
- test albero, resource gate e solve smoke: PASS, 115 asserzioni;
- primo solve 20k e run diagnostico 100k: PASS operativo;
- confronto 20k e 100k: `REJECTED` come richiesto dai gate;
- regressione Release completa: `39/39` PASS in `251,60 s`;
- AddressSanitizer mirato: `3/3` PASS in `3,57 s`, nessuna diagnostica.
- oracle River CFV/RSS: test diagnostico PASS; qualifica attesa `REJECTED` 7/7,
  report `river_bucket_cfv_ram_diagnostic_2026-09-07.json`.
- regressione Release dopo l'oracle: `40/40` PASS in `245,99 s`.
- smoke diagnostico AddressSanitizer: PASS in `22,27 s`, nessuna diagnostica.
- candidato v3: suite Release `42/42` PASS in `240,17 s`; due target ASan PASS
  in `302,91 s`; qualifica `REJECTED` su 12/12.

## R9-C v2: decomposizione exact Flop/River

Il blueprint preflop è ora una policy densa e versionata: 20 decisioni per 81
classi, con fingerprint del tree, algoritmo e iterazioni. La propagazione delle
reach conserva pesi `float64` sulle 630 combo fisiche. Nessun bucket postflop è
usato nel contratto autorevole; l'adattatore a basis point è esplicito e
riporta l'errore di quantizzazione.

Il catalogo lossless dei semi riduce i 7.140 flop fisici a 573 flop canonici.
Sui nove ingressi postflop si passa quindi da 64.260 root pubbliche a 5.157
task canonici. Le molteplicità ricostruiscono tutti i 7.140 flop e le
probabilità di ingresso, fold e all-in sommano a `1`.

La sola separazione al Flop non basta per il desktop. Il ramo limp/check,
pot `4a` e stack residuo `38a`, richiede 7.843.579.392 action entry e
31.374.317.568 B di stato ProductionDcfr. Il picco modellato è
32.528.500.840 B. Questo piano è **respinto per RAM**.

La decomposizione annidata al River cambia il limite attivo:

| Componente peggiore | Byte |
|---|---:|
| Stato persistente Flop + Turn | 262.408.000 |
| Massimo subgame River | 1.041.600 |
| Picco modellato del subgame River | 374.624.404 |
| Certificazione task-atomica | 365.706 |
| Lower bound attivo annidato | 637.398.110 |

Il ramo peggiore contiene 633 history River ma solo 25 forme distinte
pot/stack; sull'intero gioco sono 201 history root Turn e 993 River. Il primo
smoke reale ProductionDcfr `1.5/0/3` sul massimo subgame River ha allocato
260.400 action entry. Una sola iterazione produce NashConv normalizzata
`3,0992`: prova esecuzione e memoria, non convergenza.

L'estrattore postflop restituisce CFV di root per combo fisica e reach
controfattuale esplicita. Sullo smoke River produce 465 valori per giocatore e
ricompone l'EV con errore `6,49e-15`. Un regression test con range frazionari
ha inoltre rilevato e corretto l'indicizzazione player-local del DAG canonico.

Le 465 righe descrivono il solo board River rappresentante. Prima della
riduzione task-local, il bridge solleva quel risultato sull'orbita che
stabilizza il Flop, permuta le combo private con il board e applica la
molteplicità del Flop. La boundary intermedia contiene quindi tutte le 528
combo vive sul Flop. Un oracolo con reach unitaria verifica per ogni combo la
massa `molteplicità Flop × 31 × 30 × 406`; la precedente moltiplicazione
scalare della boundary rappresentante non preservava i blocker per seme.

Stato del gate: **struttura, condizionamento, resource model, boundary CFV,
scheduler River bounded-memory, accumulatore River task-local, assemblaggio
Flop/Turn, valutazione exact del profilo e BR/NashConv globale PASS sul percorso
di test; solve HU completo non ancora certificato**. L'accumulatore usa somme
compensate e persiste stato parziale e aggregato con fingerprint, checksum e
scrittura atomica. Il ledger 1.3 lega tutte le boundary allo stesso checkpoint;
lo streaming riparte dai resolver mancanti e committa ogni task dopo il
checkpoint. Il fingerprint locale dell'assemblaggio resta distinto
dall'identità globale del checkpoint. Accumulatori e aggregati River v5,
terminali Flop/Turn 1.1 e boundary Flop 1.1 propagano quest'ultima senza
mescolare continuation diverse. Anche i terminali BR Flop/Turn 1.1 dichiarano
la continuation della strategia avversaria; il dispatcher li confronta con le
boundary BR River prima della ricorsione. Il target HU corretto passa sotto
AddressSanitizer con 9.090 asserzioni in 4.793,91 s, senza diagnostiche. Manca
il provider che produca continuation postflop convergenti per
l'intero benchmark. Nessun default CLI, GUI o formato `.gtsd` è stato
modificato.

La suite Release corrente passa 42/42 in 787,22 s; il target HU impiega
506,16 s. Il run non produce output su stderr.

Validazione del lift e delle reach: test mirati Release `3/3` PASS in
`38,93 s`, suite completa `42/42` PASS in `258,86 s` e target HU preflop sotto
AddressSanitizer PASS in `362,36 s` senza diagnostiche.

Il manifesto upper-street individua 3.792 terminali prima della root River:
612 sul Flop e 3.180 sul Turn, ripartiti in 2.388 fold e 1.404 all-in runout.
Il replay verifica ogni sequenza d'azioni contro lo stato terminale. Il nuovo
valutatore calcola utility exact e runout residui; l'assemblatore rifiuta
contributi mancanti o fuori ordine e finalizza soltanto dopo la conservazione
della massa per tutte le combo vive. Un test end-to-end con policy deterministica
terminante al Flop ricostruisce esattamente 812 runout per deal compatibile.

Validazione corrente: suite Release `42/42` PASS in `317,59 s`; target HU
preflop AddressSanitizer PASS con 8.965 asserzioni e nessuna diagnostica.

Il primo segmento della BR globale è ora concreto: il solver postflop estrae
per ogni combo fisica sia la CFV di profilo sia la CFV della deviazione exact e
ricompone quest'ultima contro la best response autorevole entro `1e-9`. I due
report hanno modalità distinte. Il bridge applica ora a entrambi il lift
orbitale blocker-aware; accumulatori e aggregati task-local separati impediscono
di mescolare profilo e BR. L'assemblatore Flop del profilo rifiuta l'aggregato
BR perché la massimizzazione alle decisioni Flop/Turn non è ancora
implementata. I test Release mirati passano `2/2` in `99,29 s` e il test HU
conta 8.971 asserzioni; la suite Release completa passa `42/42` in `318,07 s`.
Il target HU passa sotto AddressSanitizer in `1.115,26 s` senza diagnostiche.
Mancano ancora la ricorsione BR Flop/Turn e quella preflop.

Il catalogo espone ora anche gruppi task-local per Turn canonico e span di root
per ogni history River. La vista Turn-major copre ogni root del task esattamente
una volta senza materializzare nuove boundary; il test Release passa con 8.975
asserzioni e la suite completa passa `42/42` in `333,03 s`. È il prerequisito
per scegliere le azioni BR separatamente dopo ciascuna osservazione Turn.

Blueprint, piano e boundary dispongono ora di JSON canonico, versione,
fingerprint semantico, envelope con checksum e sostituzione atomica. Il loader
rifiuta payload corrotti e versioni incompatibili. Il certificatore globale
richiede due boundary per ciascuno dei 5.157 task (`10.314` totali), ricostruisce
la probabilità postflop coperta e richiede una best response globale exact. Una
risposta campionata non può impostare `certified=true`.

È stato misurato anche un oracle sparse senza bucket. La chiave postflop
comprende combo fisica, flop non ordinato, ordine Turn/River e history delle
azioni; i semi sono canonicalizzati lossless sulle 24 permutazioni. Il run
20k crea 1.699.846 infoset, usa almeno 244.777.824 B di payload e termina in
7,19 s. Il run 100k crea 8.996.964 infoset, usa almeno 1.295.562.816 B e termina
in 41,17 s.

Decisione: **oracle sparse `REJECTED` come percorso di solve**. A 100k l'EV CO
è `+0,7437a`, l'errore medio action/class è `21,627 pp` e la TV media
`54,068 pp`; la risposta resta un lower bound non certificato. La
canonicalizzazione lossless non basta perché lo spazio pubblico è ancora
troppo grande per ottenere visite ripetute. Resta disponibile come controllo
di rappresentazione, non come candidato da prolungare.

Validazione aggiornata: CTest Release `42/42` PASS in `153,10 s`; ASan mirato
su HU preflop, Phase 10 e decomposizione `3/3` PASS in `487,21 s`, più
riconferma HU dopo il certificatore PASS in `9,13 s`.

### Scheduler River e probe file-backed

I `64.260` root Flop indicano `9 × 7.140` frontiere fisiche, non 64.260 flop
distinti. Il percorso operativo canonico usa `9 × 573 = 5.157` task e produce
due boundary per task: `10.314`, pari a `130.699.008 B` nel modello denso. Il
costo `1.628.605.440 B` resta esposto come confronto senza isomorfismo.

Il catalogo River comprende `369.072` board history canoniche e conserva la
molteplicità delle `7.539.840` history fisiche. Incrociando 993 history di
betting e i due resolver si ottengono `732.976.992` root. Salvare ogni boundary
River richiederebbe `9.288.284.442.624 B`; il piano fail-closed vieta questa
modalità. Con un target di 64 MiB usa al massimo 5.294 boundary per batch,
141.687 batch allineati ai task e 836 boundary nell'ultimo batch. Ogni batch
mantiene unita la coppia di CFV dei due player: i solve pubblici River sono
366.488.496, mentre 732.976.992 è il numero di boundary per lato.

L'ordine `entry/flop/river-history/runout/resolver` raggruppa le root in 5.157
span task-local. Il catalogo si costruisce in `1,23068 s`; enumerare il primo
batch richiede `0,680133 s`. Sono misure singole Release e non una qualifica di
throughput.

Il backend temporaneo file-backed ha completato due prove reali senza cambiare
il default del prodotto. Il Turn peggiore usa `238.611.936 B` logici e termina
in `2,87843 s`, con Peak RSS `18.939.904 B`. Il Flop peggiore usa
`31.426.437.952 B` logici: una traversata richiede `1.285,694435 s` e solve più
certificazione `9.719,15 s`, con Peak RSS `608.006.144 B`. La NashConv
normalizzata dopo una iterazione è `4,26804`; il test prova soltanto
rappresentabilità e lifecycle del backing. Il solve monolitico resta respinto
per tempo e mancata convergenza.

Anche lo sweep exact dei subgame River separati è respinto come percorso di
produzione. Una traversata sul subgame peggiore misura `0,0026794 s`; proiettata
sui `366.488.496` stati pubblici equivale a `981.969 s` (`11,37 giorni` seriali,
`1,42 giorni` con scaling ideale su otto worker) per una sola iterazione. È una
proiezione diagnostica, non un tempo certificato dell'intero sweep. Catalogo,
scheduler e aggregati restano utili come oracle per validare un percorso con
riuso fra root; non sono il motore del primo solve HU 40a.

### Resume della best response

La best response exact può ora salvare un leaf River dopo ogni root completato
e ripartire dal successivo. Il checkpoint conserva il manifest ordinato, le
630 somme compensate e le identità di query, blueprint e continuation. Sono
persistibili anche risultato del task, accumulatore delle entry e risultato
dell'entry; checksum, limiti di lettura e sostituzione atomica coprono tutti i
formati.

Il test HU Release passa con 9.099 asserzioni in 462,24 s e stderr vuoto. Il
test interrompe il provider dopo il primo root, ricarica il checkpoint e prova
che il secondo run non ripete il lavoro già committato. Questo rende recuperabili
i calcoli lunghi, ma non cambia la proiezione temporale dello sweep e non
fornisce ancora la NashConv del benchmark. La stessa revisione passa la suite
Release completa 42/42 in 752,22 s; nel run integrale il target HU impiega
484,88 s.
