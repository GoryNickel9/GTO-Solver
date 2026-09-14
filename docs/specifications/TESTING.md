# Testing

Il backend sottoposto a unit, integration, differential e benchmark test è
esclusivamente CPU/RAM. I test grafici possono usare una GPU per il rendering,
ma non costituiscono né attivano un percorso di solving GPU.

## Strati

La suite copre:

- unit test di carte, money, range, azioni, rake e settlement;
- evaluator/showdown e casi Short Deck limite;
- tree builder, chance, hash e serialization;
- isomorfismo globale lossless;
- solver laboratory, best response e checkpoint;
- memoria e postflop exact;
- storage autenticato e migrazioni;
- GUI prototype/product E2E;
- riferimento e benchmark GTO+.

I bug matematici producono test permanenti. La parità interna non basta se i due
percorsi condividono la stessa formula sbagliata: servono oracle o invarianti
indipendenti.

## Preset canonici Windows

La build completa desktop usa l'ambiente Visual Studio Developer Command:

```powershell
cmake --preset windows-gui-release
cmake --build --preset windows-gui-release
ctest --test-dir out/build/windows-gui-release --output-on-failure
cmake --build --preset windows-gui-release --target format-check
```

Altri preset coprono Debug, Release senza GUI, ASan e nightly. L'assenza degli
header standard in MSVC è un problema di ambiente, non una regressione del
codice: i gate Windows devono essere eseguiti nel developer environment.

## Suite corrente

Il preset desktop `windows-gui-release` registra 38 test CTest: infrastruttura,
core, fasi 1-10, contratto `production_dcfr`, timer product, riferimento GTO+,
layout canonico, oracoli, benchmark smoke e sette E2E GUI. Sanitizer e nightly
restano prove separate e il report deve dire con precisione cosa è stato escluso.

Ultima verifica core completa (2026-09-06): Release `35/35 PASS` in `260,92 s`.
La verifica GUI Release separata resta `38/38 PASS` in `203,93 s` al
2026-09-05. Sono inoltre passati il resume production byte-equivalent attraverso
il reset finale, il contratto delle tre fixture e i 15 solve target-driven della
baseline B0 controllata. Questa baseline chiude R2 come misura riproducibile,
non come parità: TH e TST falliscono il gate tempo.

## Invarianti obbligatori

- nessuna carta duplicata;
- azioni solo legali e stack mai negativo;
- conservazione chip prima del rake;
- distribuzioni non negative e normalizzate;
- determinismo con input/seed uguali;
- chance mass e blocker corretti;
- physical tree e canonical path equivalenti;
- continuous solve e resume equivalenti;
- storage corrotto rifiutato senza crash;
- nessun NaN o infinito.
- una solve core senza iteration limit richiede un target e termina solo alla
  prima certificazione che soddisfa il confronto configurato;
- il rounding aggressivo opera sul target totale impegnato, è serializzabile e
  non dipende dall'identificatore del benchmark.

## F10.4 — root lock diagnostico

Il test `EXTERNAL_ROOT_LOCK_TEST` copre esattamente le 36 combo GTO+ root,
azioni e somme, input duplicati/mancanti/bloccati, posteriori bet/check,
fingerprint invariato, convergenza del gioco vincolato e assenza di regressioni
nel percorso standard. F10.4 è completata come diagnostica test-only; non certifica
il node locking globale di prodotto.

## Reporting

Un comando interrotto per timeout non viene dichiarato come run completo. È
accettabile completare una suite in segmenti deterministici, riportando
esplicitamente i segmenti e l'esito totale. Test locali, package consumer, E2E
installato e CI remota sono prove diverse e non vanno fuse in un unico “PASS”.
## Stato production node-scaled

I test Release coprono allocazione, update e certificazione dello stato core
`ScaledUint16RegretStrategy`, validazione dei valori finiti, scale per decision
node, resume continuo/segmentato byte-equivalent e persistenza autenticata con
round-trip byte-for-byte. Il preset Release corrente passa `35/35` in
`260,92 s`; il preset GUI verificato separatamente resta `38/38` in `203,93 s`.
I tre benchmark RAM restano integration gate separati dalla suite e dai time
gate.

## Profilo ProductionDcfr di prodotto

I test phase10 coprono il resolver condiviso, il rifiuto di checkpoint CFR+ e
backend non supportati, il solve di un gioco ridotto, il modello byte dello
stato e il round-trip del checkpoint ProductionDcfr v2. I test phase8 coprono
la persistenza in `.gtsd` di algoritmo, precisione ed esponenti nello schema
metrics v4. Il test di residency verifica che il dispatch ai due lati del budget
produca checkpoint materializzati byte-identici. Il smoke CLI verifica il
profilo e i tre intervalli product tramite il vero entry point. Il caller GUI è
verificato dal preset `windows-gui-release`: l'E2E
create-solve-save-reopen-navigate-resume passa in 12,96 s. Il test usa una linea
bet-call raggiunta a iterazione 2; una linea a reach zero non viene trasformata
in analytics uniforme.

Phase10 verifica inoltre la policy postflop `exact_identity` 1.0: mapping
implicito a zero byte, conteggi infoset/action invariati, perfect recall,
rifiuto di versioni o modalità sconosciute ed equivalenza esatta di fingerprint,
codici, scale e round-trip del checkpoint binario ProductionDcfr.

Lo stesso target verifica `made_hand_value` su river ridotto e fixture D/V:
copertura di ogni infoset fisico, pesi iniziali anche non uniformi, riduzione di
infoset/action-entry, byte model e fingerprint. Esegue il traversal
ProductionDcfr bucketizzato, confronta 1.024 update con l'oracolo scalare,
certifica nel gioco originale e verifica checkpoint, checksum e resume
byte-identico. Il percorso copre anche chance node e street precedenti al river.

Phase10 espone inoltre due runner diagnostici isolati. `--bucket-profile-only`
pubblica timer additivi, capacità transiente, numero di passate e contatori del
lavoro fisico per R3; `--r6-rbp-only` osserva checkpoint ProductionDcfr alle
iterazioni 20 e 32 senza saltare nodi o modificare lo stato. La verifica mirata
2026-09-06 passa rispettivamente `763.823` e `8` asserzioni; phase7 passa `216`
asserzioni e conserva il differenziale RBP legacy OFF/ON. Gli stessi due runner
phase10 passano nel preset ASan senza diagnostiche; i tempi strumentati non sono
usati come benchmark Release.

## R2-S — contratti ridotti

`gtosd_card_abstraction_tests` verifica mapping identità, strategy tying
pesato, firme player/action, copertura completa, formato v1, fingerprint,
aggregazione/lift e NashConv nel gioco originale. Il caso coarse deve restare
peggiore di `0.1` NashConv originale: impedisce di promuovere la sola
convergenza astratta.

`gtosd_subgame_contract_tests` verifica public-root chiusi sugli infoset,
boundary privati completi, range asimmetrici, reach zero, fingerprint di gioco e
blueprint, algoritmo ProductionDcfr, byte model e round trip. Verifica inoltre
l'estrazione esatta di reach controfattuali e CFV condizionali, la distribuzione
per root fisico, il gadget opt-out e lo splice del solo resolving player. Dopo
50.000 iterazioni DCFR richiede che la BR globale avversaria non aumenti oltre
`0.001` sia su Kuhn sia sullo Short Deck river toy zero-rake. Il test copre anche
envelope con checksum, sostituzione atomica e rifiuto di un file corrotto.

`gtosd_postflop_subgame_tests` verifica il bridge exact e bounded da un river
postflop a `FiniteGame`: pesi di range, tutti i deal compatibili, equivalenza di
profile value/NashConv, boundary, persistenza, opt-out, splice e BR globale.
Verifica inoltre S3 con una collisione reale `12 -> 4` infoset e certifica
exact, bucket-only e combinazione nel gioco originale. La build ASAN passa sia
questo target sia `phase10 --bucket-only`.

I test phase5 distinguono `ProductionDcfr` dalla variante DCFR parametrica:
resume `16 -> 32` byte-identico attraverso il reset d'epoca, round-trip
dell'enum e rifiuto di thread count, gamma o averaging delay fuori contratto.

## River bucket-native

`gtosd_postflop_subgame_tests` copre anche il kernel river-native:

- collisione fisica 9 deal → 1 coppia di bucket, equivalenza EV uniforme e
  confronto del traversal con il gioco astratto materializzato;
- lift e BR/NashConv nel gioco originale, query identica per le combo dello
  stesso bucket e fingerprint separato dall'exact;
- checkpoint atomico con checksum, rifiuto di corruzione e diversa astrazione,
  resume 64 → 128 byte-identico al solve continuo;
- full range 188.790 → 611 e default senza `FiniteGame` di validazione;
- due board pesati D/V con 8.640 deal, confronto exact/native a 128 iterazioni
  e limiti fail-closed.

La verifica 2026-09-06 passa 338 asserzioni in Release. I tre target
`gtosd_card_abstraction_tests`, `gtosd_subgame_contract_tests` e
`gtosd_postflop_subgame_tests` passano 3/3 sia Release sia AddressSanitizer; il
target River viene riconfermato sotto ASan in 209,04 s senza diagnostiche.
Il CTest Release completo sullo stesso stato passa 35/35 in 260,92 s.

`gtosd_river_bucket_qualification_preflight` valida schema, selezione
indipendente, contratto ProductionDcfr, soglie, limiti, range deterministici e
fattibilità dei sette oracle prima di qualunque solve. Passa in Release e in
ASan; in ASan richiede il runtime MSVC tramite `ENVIRONMENT_MODIFICATION`, come
gli altri benchmark. Il runner completo produce un report JSON atomico e usa
exit code 0 per `QUALIFIED`, 2 per `REJECTED` e 3 per input/errore.

La qualifica completa Release e ASan conclude `REJECTED`, non test failure: il
programma termina intenzionalmente con code 2 perché almeno un gate è falso.
Gli stessi NashConv sui due build verificano il percorso end-to-end; i tempi
ASan non sono dati prestazionali.

### River exact-blocker v2

Lo stesso target River verifica `exact_blocker_signature_v2` con tre livelli:

- una classe con più membri e compatibilità identica coincide con profile
  value, BR e NashConv del gioco fisico entro `1e-12`;
- combo con lo stesso valore finale ma blocker diversi vengono separate, e
  ogni classe viene controllata contro tutte le combo avversarie attive;
- il full range produce 465 classi per 465 combo per player, 188.790 coppie per
  188.790 deal e uguale lavoro per passata.

`gtosd_river_exact_blocker_qualification_preflight` verifica schema v2,
fingerprint del corpus v1 importato, cinque holdout nuovi, contratto solver,
soglie e limiti. Passa in Release e in ASan; l'ultimo run ASan impiega 2,03 s.

La qualifica Release completa usa 12 fixture, 1.024 iterazioni e cinque
ripetizioni. Conclude `REJECTED`, 0/12: riduzione nodi e speedup falliscono
12/12, memoria passa 12/12 e l'insieme dei gate di qualità passa 11/12. Il
report è
`benchmarks/results/river_exact_blocker_qualification_2026-09-06.json`.
La qualifica completa non viene usata come test CTest perché `REJECTED` è il
risultato atteso del candidato e il runner restituisce code 2. ASan copre il
kernel, il full range e il preflight; non replica le 60 misure prestazionali.

## River weighted-equitable feasibility

`gtosd_postflop_subgame_tests` verifica una collisione lossless controllata,
il conteggio full-range di 188.790 deal e la partizione stabile 45/45 con 2.005
coppie. Ogni classe viene ricontrollata contro la massa pesata di tutte le
classi avversarie.

`gtosd_river_joint_equitable_feasibility_preflight` esegue lo stesso
analizzatore sulle 12 fixture congelate v2. Il test passa quando schema,
determinismo e analisi sono validi; il report applica separatamente il gate di
prodotto e conclude `BLOCKED` perché la riduzione è `1,00x` in 12/12. Release e
ASan coprono il preflight; sotto ASan il test postflop più il preflight passa
2/2 in 198,94 s.

La regressione Release completa con il nuovo preflight passa `36/36` in
`267,95 s`.

## HU preflop reference

`gtosd_hu_preflop_reference_preflight` valida l'unica fixture esterna HU
preflop. Copre grammatica delle frequenze, 81 classi, masse 630, righe
arrotondate a 100/101%, root state CO 40a e quantità fold/call/raise 6a/raise
10a/all-in 40a. Rake e continuazioni mancanti devono rimanere dichiarate come
unknown.

Il preflight esegue inoltre un self-compare attraverso lo schema candidato e
richiede delta strategia ed EV esattamente zero. Il comparatore rifiuta righe
mancanti, probabilità non finite o fuori intervallo, somme diverse da uno e
metadati di rake/tree assenti.

La verifica 2026-09-06 passa in Release e ASan; la suite Release completa con
il nuovo benchmark passa `37/37` in `275,08 s`.

## HU preflop tree e sampled solve

`gtosd_hu_preflop_tests` verifica la configurazione JSON, il catalogo root,
le continuazioni 10,5a/14,5a, la struttura dopo limp, l'override isolato per il
non-full raise, il fingerprint deterministico e la terminazione postflop per
stack. Il resource test richiede che il limite interno 63 non venga raggiunto.

Lo smoke solve attraversa deal fisici, bucket postflop, fold, all-in, showdown
exact, DCFR e risposta campionata. Controlla finitezza e normalizzazione delle
81 righe root. Non è un test di convergenza.

`gtosd_hu_preflop_reference` richiede `nashconv_certified=true` per superare il
gate. I candidati v1 dichiarano `false`: la risposta appresa è un lower bound
campionato e non sostituisce una NashConv o un upper bound certificato.

Validazione 2026-09-06: suite Release `39/39` PASS in `251,60 s`; target
preflop mirati AddressSanitizer `3/3` PASS in `3,57 s`.

## Oracle River exact-vs-bucket CFV/RSS

`gtosd_river_bucket_diagnostic_smoke` esegue la prima fixture del corpus v1 e
richiede CFV finite, strategie normalizzate, infoset root presenti e
ricostruzione del valore del profilo. Il runner completo aggiunge a ogni fixture
CFV per infoset e azione, TV della strategia root, campioni RSS delle fasi e
Peak RSS del processo. La qualifica completa resta fuori da CTest perché
`REJECTED` è un esito di prodotto atteso, non un errore del test runner.

Validazione 2026-09-07: smoke diagnostico Release PASS in `1,41 s`; corpus v1
completato 7/7 con decisione `REJECTED` e report
`benchmarks/results/river_bucket_cfv_ram_diagnostic_2026-09-07.json`.
La suite Release completa passa `40/40` in `245,99 s`.
Lo smoke AddressSanitizer passa in `22,27 s` senza diagnostiche.

## River showdown distribution v3

`gtosd_postflop_subgame_tests` verifica quantum valido, identità distinta,
fingerprint deterministico e separazione dei blocker rispetto a
`made_hand_value_v1`, senza superare il numero di classi exact-blocker.

`gtosd_river_showdown_distribution_qualification_preflight` valida schema,
fingerprint della regressione v1, cinque holdout, limiti e quantum 5%.
`gtosd_river_showdown_distribution_diagnostic_smoke` esegue solve, lift fisico,
CFV, NashConv e RSS sulla prima regressione. La qualifica completa resta fuori
da CTest perché `REJECTED` è la decisione misurata.

Validazione 2026-09-07: suite Release `42/42` PASS in `240,17 s`; target
postflop e smoke v3 AddressSanitizer `2/2` PASS in `302,91 s`. Il corpus da 12
fixture completa la qualifica con esito `REJECTED` e Peak RSS
`155.385.856 B`.

## Decomposizione HU preflop exact

`gtosd_hu_preflop_tests` copre blueprint denso, partizione delle probabilità,
573 flop canonici con molteplicità totale 7.140, 5.157 task entry/flop,
condizionamento fisico a 528 combo vive, telemetria della quantizzazione,
boundary completi, reach zero e fingerprint incompatibili. Verifica inoltre che
le history Turn/River abbiano identità stabile e che lo sampled solver esporti
la propria policy preflop completa.

`gtosd_hu_preflop_decomposition` misura i nove ingressi postflop, separa action
entry e infoset per street, enumera 201 root Turn e 993 River e svolge una reale
iterazione ProductionDcfr sul massimo subgame River. Il target richiede anche
CFV root fisiche e ricomposizione del profile value.

`gtosd_phase10_tests` aggiunge una regressione con range asimmetrici e
frazionari. Controlla la mappa player-local del DAG canonico, CFV finite,
counterfactual reach esplicita, errore di ricomposizione `<= 1e-9` e rifiuto di
un checkpoint associato a range differenti. Lo stesso test estrae la best
response exact per combo di entrambi i player, la ricompone contro il valore BR
della certificazione entro `1e-9` e verifica che anche questo percorso rifiuti
range/checkpoint incompatibili.

Il test HU copre inoltre round-trip e corruzione di blueprint, piano, boundary,
accumulatore River e aggregato River persistiti. Per accumulatore e aggregato
verifica anche ordine dei resolver, somme compensate, 528 combo vive al Flop e
coerenza fra reach pesata, utility pesata e valore condizionale. Verifica inoltre
che la reach delle azioni proprie dimezzi la massa con probabilità `0,5` e che
valori fuori `[0,1]` vengano rifiutati. Il certificatore globale viene esercitato su un catalogo
intenzionalmente incompleto: deve riportare 10.314 boundary attesi, uno validato
e rifiutare una best response campionata come prova di NashConv.

Il builder delle boundary River solleva sia il report `AverageStrategy` sia il
report `ExactBestResponse`. I due percorsi hanno modalità e fingerprint
distinti: un accumulatore rifiuta boundary dell'altra modalità e l'assemblatore
Flop del profilo rifiuta un aggregato BR. Il round-trip v4 di accumulatore e
aggregato conserva il tag; payload v3 privi del tag falliscono chiuso.

Validazione del canale BR River v4: test mirati Release `2/2` PASS in
`99,29 s`, suite Release `42/42` PASS in `318,07 s` e target HU preflop
AddressSanitizer PASS in `1.115,26 s` con 8.971 asserzioni e nessuna diagnostica.

Il test HU verifica anche la vista Turn-major: i gruppi sono contigui nel
catalogo board, la loro massa ricostruisce `molteplicità Flop × 33 × 32` e gli
span per history coprono ogni root del task una sola volta. Catalogo, task o
fingerprint del gruppo manomessi vengono rifiutati. Il test Release passa con
8.975 asserzioni; la suite Release completa passa `42/42` in `333,03 s`.

Validazione 2026-09-07: suite Release `42/42` PASS in `153,10 s`. ASan mirato
su `gtosd_hu_preflop_tests`, `gtosd_phase10_tests` e
`gtosd_hu_preflop_decomposition` passa `3/3` in `487,21 s`; dopo l'aggiunta del
certificatore il test HU è stato rieseguito sotto ASan e passa in `9,13 s`.

Il test scheduler verifica 366.488.496 stati pubblici, 732.976.992 boundary per
lato, l'allineamento delle coppie dei resolver e il rifiuto del payload River da
8,18 TB, i batch da 64 MiB, 5.157 span task-local e la somma delle molteplicità
fisiche a 7.539.840. Gli ordinali devono conservare entry, Flop, runout,
history, resolver e limiti dello span; batch saltati o riordinati e checkpoint
corrotti sono rifiutati. Il byte model distingue inoltre 130.699.008 B di
boundary Flop canonici da 1.628.605.440 B di frontiere fisiche.

Lo stesso target valuta terminali exact Flop fold/all-in e Turn fold/all-in su
combo fisiche selezionate. L'assemblatore viene esercitato con una policy
deterministica che termina al Flop: consuma tutte le 3.792 entry del manifesto,
rifiuta contributi incompleti o fuori ordine e ricostruisce esattamente 812
runout ordinati per deal privato compatibile prima di finalizzare le due
boundary Flop.

Validazione corrente dell'assemblatore: `gtosd_hu_preflop_tests` Release PASS
con 8.965 asserzioni; suite Release `42/42` PASS in `317,59 s`; lo stesso target
PASS sotto AddressSanitizer senza diagnostiche.

L'estrazione BR per combo passa inoltre sotto AddressSanitizer nel target
`gtosd_phase10_tests`: 776.083 asserzioni, nessuna diagnostica.

I probe file-backed `--file-backed-probe turn|flop` sono benchmark manuali
pesanti, esclusi da CTest. Richiedono un percorso backing nuovo, verificano la
presenza durante il solve e la cancellazione alla chiusura. Il probe Flop del
2026-09-07 ha completato una traversata in 1.285,694435 s e l'intero solve con
certificazione in 9.719,15 s; non va ripetuto nella regressione ordinaria.
