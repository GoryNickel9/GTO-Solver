# Riduzione di RAM e tempi sulla suite di benchmark: ottimizzazioni, test e risultati

Data di inizio: 2026-09-21. Documento del milestone, parti 6-8. Il censimento e' in
[BENCHMARK_SUITE_INVENTORY_2026-09-21.md](BENCHMARK_SUITE_INVENTORY_2026-09-21.md), il
protocollo e i criteri preregistrati in
[BENCHMARK_SUITE_PROTOCOL_2026-09-21.md](BENCHMARK_SUITE_PROTOCOL_2026-09-21.md).

Stato: **in corso**. Le sezioni "Risultati" vengono riempite dal report generato con
`python tools/preflop_suite/suite.py report --baseline baseline-ba93c75` man mano che i run
completano; ogni tabella riporta la data del report.

## 1. Versioni confrontate

| Versione | Eseguibili | Contenuto |
|---|---|---|
| `baseline-ba93c75` | `out/suite/bin/baseline-ba93c75` (trainer SHA-256 `d9d7bba6ddd813072bbbd2930b25da5e91532c1c55d34c6df0b102db78d868ff`, certificatore `7816a5b0...`) | HEAD `ba93c75` senza modifiche: tre tabelle dense `double` (regret, somme della media, policy corrente), copia 465 x 465 delle matrici all-in per ogni board del batch, export della policy tramite tabella densa |
| `cand-a-compact-double` | build dell'HEAD piu' le modifiche di questo milestone, `--table-storage double` | policy compatta per batch al posto della terza tabella, matrici all-in raccolte dalle tabelle dense 630 x 630 senza copia per board, export della policy in streaming, strumentazione |
| `cand-b-mixed` | stessa build, `--table-storage mixed` | come A con le somme della strategia media in `float32` (regret `double`) |
| `cand-c-float32` | stessa build, `--table-storage float32` | come A con regret e somme in `float32` |

Le tre candidate usano lo stesso eseguibile; l'argomento di versione e' applicato identicamente
a tutti gli scenari (controllo di uniformita').

## 2. Ottimizzazioni

### 2.1 A. Policy temporanea per batch al posto della tabella globale

Nella baseline la policy corrente e' una terza tabella densa di `layout.entries` double
(HU20: 467.201.821 celle, 3,74 GB). Con il refresh selettivo, a ogni pass venivano ricalcolate
solo le righe toccate dai 32 board del batch, ma nella tabella densa: le celle allocate erano
tutte, quelle ricalcolate e lette erano solo quelle delle righe attive (al massimo 32 x 465 =
14.880 righe per street e per pass).

Nella candidata la tabella densa sparisce. Prima di ogni pass il trainer:

1. raccoglie le righe distinte di ogni street toccate dal batch (bitmap, come prima);
2. assegna a ogni (board, street, mano) lo slot della sua riga fra le righe attive (ricerca
   binaria nella lista ordinata, in parallelo sui board);
3. materializza il discount lazy e il regret matching di ogni riga attiva una sola volta, in
   una tabella compatta indicizzata per (nodo decisionale, slot); l'offset di ogni nodo e' la
   somma delle righe attive della sua street per il numero di azioni;
4. la traversata legge `compact[offset(nodo) + slot(board, street, mano) x azioni]` invece di
   `dense[offset(nodo) + riga(board, street, mano) x azioni]`.

I valori sono gli stessi numeri calcolati sugli stessi regret nello stesso istante: la
traiettoria e' bit-identica (criterio 7.1 del protocollo). La chiave della cache e' (nodo, riga,
versione della policy): la versione e' il pass, perche' la tabella viene ricostruita a ogni
pass e nessun valore sopravvive oltre. Non viene ricalcolata la stessa riga per ogni visita:
una riga usata da piu' mani o piu' board del batch e' materializzata una volta per pass
(contatori `rows_materialized`, `hand_lookups` e "riusi" nel report). Il valutatore a policy
fissa (BR astratta) usa lo stesso percorso con gli slot uguali alle righe dense.

Memoria: la tabella compatta occupa al massimo
`sum_nodi (righe attive della street x azioni) x 8 byte`, cioe' per HU20 meno di 70 MB contro
3,74 GB; il report misura la capacita' effettivamente riservata.

### 2.2 B. Traversata, workspace e riduzioni

I workspace per thread (livelli x (8 + 8 + 3) x 465 double, circa 1,3 MB per thread) e le
unita' della partizione (3 x 465 double per unita') sono gia' compatti; le riduzioni sono le
somme per mano dei valori dei figli e la fase `top_reduce` che ricompone le unita', senza
atomiche nel percorso critico e con un solo scrittore per cella. Non sono state modificate.
Con `--profile-traversal` il trainer misura separatamente le fasi della traversata.

### 2.3 C. Preparazione board e matrici all-in

La baseline copiava per ogni board due matrici 465 x 465 (3,46 MB) raccolte dalle tabelle
dense 630 x 630 (6,35 MB) delle probabilita' esatte di vittoria e pareggio: 64 board per
iterazione, 221 MB di scritture per iterazione. Le matrici di due board coincidono solo se i
due board hanno le stesse cinque carte (evento raro nel campionamento), quindi una cache per
board avrebbe hit rate trascurabile. La candidata elimina la copia: al terminale all-in
preflop la massa di vittoria di ogni mano e' il prodotto scalare della riga della tabella
densa del suo combo con il vettore di reach, raccolto attraverso gli id dei combo vivi. I
termini sommati sono gli stessi nello stesso ordine (i combo vivi sono enumerati per id
crescente su ogni board), quindi il risultato e' bit-identico. La chiave completa e' il combo
id (indipendente dal board): card removal resta nel vettore di reach (mani morte a zero).

Conteggio dei rebuild identici (misura, non stima): il trainer marca ogni board campionato in
un bitset (una posizione per board possibile: indice della lista quando e' caricata una lista
di board, altrimenti combinazione del flop x turn x river, 22.100 x 52 x 52 posizioni, 7,5 MB
conteggiati in `board_list_bytes`) e riporta nell'evento `end` `boards_distinct` e
`boards_repeated`: un board ripetuto e' una preparazione del contesto (righe di bucket e di
history per street, indicizzate per mano fisica) che una cache con chiave completa avrebbe
potuto evitare. Il campionamento estrae board fisici (52 x 51 x 50 / 6 flop x 49 turn x 48 river
= 51.979.200 storie fisiche possibili; il catalogo campiona il flop fra i rappresentanti
canonici ponderati e turn e river fisici). Misura sul run completo HU10 della candidata A
(2 x 16.000 x 32 = 1.024.000 estrazioni): 957.696 board distinti e 66.304 ripetizioni (6,5 %; 0 su
6.400 estrazioni nella sonda HU20 a 100 iterazioni, come atteso per un campione piccolo). Una cache
con chiave completa e senza limite di memoria avrebbe quindi evitato al massimo il 6,5 % della
preparazione dei board, cioe' circa 3 s dei 42,6 s di HU10 nella candidata A (la preparazione
valeva 363 s nella baseline: la parte eliminata dalla candidata e' la copia delle matrici all-in,
non la costruzione del contesto). A livello canonico (605.088 board, molteplicita' per seme) le
ripetizioni sarebbero molto piu' frequenti, ma il contesto e' indicizzato per mano fisica e
andrebbe permutato per seme, e una cache di tutti i board visti costerebbe decine di GB
(centinaia di migliaia di contesti da 40-50 KB); con un budget comune di 256 MB (circa 6.000
contesti) l'hit rate sarebbe dell'ordine dell'1 %. La cache non viene introdotta; il conteggio
nel report documenta la scelta.

### 2.4 D. Precisione numerica

Le candidate B e C conservano tutto di A e cambiano solo il tipo delle celle persistenti.
Ogni incremento e ogni fattore di discount e' calcolato in `double`; la cella viene
arrotondata una sola volta quando viene scritta. Il regret matching, la media e le somme
sui figli restano in `double`: l'errore delle riduzioni e' nullo per costruzione e l'errore
misurato e' di storage. Checkpoint e fingerprint dello stato usano i byte nel formato di
storage; l'identita' del trainer include il formato, quindi un checkpoint `double` non viene
ripreso da un trainer narrow. La policy esportata resta `double` (stesso formato di file).

### 2.5 E. Certificazione ed esportazione

L'export della policy media non costruisce piu' una tabella densa: le righe vengono calcolate
in blocchi da 65.536 valori e scritte con lo stesso formato `GTOSDPOL` e lo stesso checksum
(`PolicyStreamWriter`); il fingerprint della policy e' calcolato durante la scrittura. Il
checkpoint era gia' scritto in streaming dalle due tabelle. La BR fisica esatta e' invariata
nel significato e nel codice: il certificatore riporta ora i picchi di processo e i byte della
policy caricata.

Caricamento della policy nel certificatore (diagnosi con la build strumentata, sonda HU20 a 100
iterazioni): picco di commit 7,63 GB con 3,74 GB di tabella e 53 MB di mappa, stato stabile
3,89 GB dopo il caricamento. Causa: `gtosd_preflop_blueprint_certify` costruiva una policy
uniforme segnaposto con la tabella completa (3,74 GB azzerati e resi uniformi) e poi vi
spostava la tabella caricata da `load_policy`, tenendo due tabelle vive durante il
caricamento. Correzione (2026-09-22 03:33, solo `benchmarks/preflop_blueprint_certify.cpp`): la
policy e' tenuta tramite puntatore e la segnaposto viene costruita solo in modalita' uniforme;
`parse_policy` legge gia' la tabella in una sola copia. Il picco atteso scende al livello dello
stato stabile (circa 3,9 GB su HU20-HU40, circa 1,05 GB su HU10); la BR esatta non cambia (stesso
codice di valutazione, stessa policy). Le versioni candidate A, B e C usano tutte questo
certificatore; la baseline conserva il suo.

## 3. Strumentazione

- Evento `memory_breakdown` del trainer (dopo l'inizializzazione e dopo la prima
  iterazione): byte riservati per componente, celle e righe per street.
- Evento `end` del trainer: `write_seconds` (materializzazione, checksum e scrittura),
  `policy_cells` (righe e celle materializzate, lookup mano-riga, picco della tabella compatta),
  `boards_distinct` / `boards_repeated` (rebuild identici dei contesti di board), picchi di
  working set e di commit privato dopo il training e alla fine; dalla candidata H
  `rows_total` / `rows_touched` / `regret_pages_total` / `regret_pages_touched` per street
  (copertura delle righe materializzate almeno una volta e delle pagine da 4 KiB della
  tabella dei regret), `lazy_discount_epoch` nell'evento `start`.
- Opzioni della CLI: `--reuse-discount-invariant-policy` viene rifiutata con un messaggio
  (la policy compatta e' ricostruita a ogni batch); `--batch-policy-refresh` resta accettata
  per compatibilita' con il protocollo comune ma non cambia il comportamento (il refresh per
  batch e' l'unica modalita').
- Certificatore: `preparation_seconds`, `process_peaks`, `policy_table_bytes`,
  `history_map_resident_bytes` nel certificato.
- `gtosd_preflop_blueprint_game --actions`: nodi preflop con azioni legali e importi,
  istogramma delle firme delle azioni postflop per street.

## 4. Test

| Test | Contenuto | Esito |
|---|---|---|
| `preflop_blueprint_trainer_tests` | oracolo `FiniteGame`, determinismo 1/2/4/8 thread e partizione, ripresa, eager/lazy, policy compatta bit-identica fra 1 e 4 thread, export in streaming uguale byte per byte all'export denso, opzione di riuso rifiutata, storage narrow (errore per cella, checkpoint round-trip, rifiuto incrociato) | PASS 2026-09-22 03:30, 21.310.606 asserzioni (`out/suite/tests/trainer_tests.log`); B: errore massimo 5,0e-6 relativo dopo 12 iterazioni; C: 4,6e-4 dopo 1 iterazione, poi divergenza di traiettoria (criterio per cella preregistrato non superato: vedi emendamento 7.2 del protocollo) |
| `preflop_blueprint_kernel_tests`, `certifier_tests`, `export_tests`, `game_tests`, `scaffold_tests` | invariati | PASS 2026-09-22 (1.636.010 / 146.541 / 14.205 / 797.826 / 84 asserzioni, log in `out/suite/tests/`) |
| Identita' bit per bit A vs baseline | HU10 40 iterazioni (stato `fnv1a64:cad91343de7ab2d8`, policy `fnv1a64:c85e99914c747ed6`), HU20 100 iterazioni, run completi a 16.000 | **PASS** HU10 40 it e HU20 100 it (stato `fnv1a64:81695fcdc36df274`, policy `fnv1a64:437892420c1b3714`, max gain 1,381071955975485 a identico); run completi: vedi 5.2 |
| Certificatore corretto (E) vs baseline | sonda `probe-e` HU10 40 iterazioni con il certificatore del 2026-09-22 03:33 sullo stesso trainer | **PASS** stesso certificato (max gain 0,2844864884682953 a, NashConv 0,3843136507 a, EV 0,1473409491 a), picco di commit del certificatore 1.141.248.000 B contro 2.010.230.784 B della baseline (-43 %) |

## 5. Risultati

### 5.1 Baseline canonica (`baseline-ba93c75`, ripetizione 1, 2026-09-21/22)

Protocollo comune (16.000 iterazioni, batch 32, 8 thread, `history7`), controllo di
uniformita' `UNIFORMITY_CHECK=PASS` sui quattro run (stesso eseguibile, stessi argomenti,
stessi artefatti; solo i campi derivati dall'albero cambiano).

| Benchmark | Picco private commit (GiB) | Picco working set (GiB) | Training (s) | BR esatta (s) | E2E interno (s) | max gain (a) | Esito |
|---|---:|---:|---:|---:|---:|---:|---|
| HU10 | 3,067 | 3,062 | 1.482,0 | 289,0 | 1.797,1 | 0,002045732 | PASS |
| HU20 | 11,487 | 11,465 | 4.105,6 | 1.459,3 | 5.679,0 | 0,027999589 | PASS |
| HU30 | 11,830 | 11,808 | 4.270,5 | 1.064,6 | 5.442,0 | 0,191818521 | FAIL (lavoro fisso) |
| HU40 | 11,830 | 11,809 | 3.620,5 | 1.301,0 | 5.036,2 | 0,289556855 | FAIL (lavoro fisso) |

Scomposizione dei tempi (s): HU10 init 4,4, refresh policy 376,3, board + all-in 363,3,
traversata 742,4, scrittura 21,7, preparazione certificatore 7,7, BR 281,4; HU20 7,5 / 1.706,5
/ 444,0 / 1.955,0 / 106,5 / 27,2 / 1.432,1; HU30 7,8 / 1.669,9 / 423,5 / 2.177,2 / 99,1 / 21,2 /
1.043,4; HU40 7,0 / 1.420,4 / 376,3 / 1.823,7 / 107,7 / 21,1 / 1.279,8. Il refresh della policy
densa vale il 39-42 % del training su HU20-HU40 (25 % su HU10); il certificatore ha un picco di
commit di 7,63-7,86 GB su HU20-HU40 e 2,01 GB su HU10 (policy 3,7 GB + mappa + buffer: da
attribuire con la build strumentata). HU30 e HU40 riproducono esattamente i valori storici a
16.000 iterazioni (HU30 storico 0,191818521 a) e non raggiungono l'1 % del piatto a lavoro
fisso: restano nella suite come benchmark "non certificati" e vengono confrontati fra versioni
su memoria, tempo, EV e max gain, mai promossi a superati. Pressione di memoria: minimo di
memoria disponibile 3,19-4,59 GiB e paging massimo 3,3-4,8 GB durante HU20-HU40 (trainer 11,5-11,8
GiB piu' il processo viewer da 3,7 GiB): i tempi della baseline su questi scenari includono
page fault (3,15 M su HU20).

### 5.2 Risultati finali: quattro versioni, quattro benchmark, tre ripetizioni (2026-09-23)

Tabelle complete generate da `suite.py report` (48 run, `UNIFORMITY_CHECK=PASS` per ognuna
delle quattro versioni): [BENCHMARK_SUITE_REPORT_2026-09-23.md](BENCHMARK_SUITE_REPORT_2026-09-23.md)
(JSON grezzo: `BENCHMARK_SUITE_REPORT_2026-09-23.json`; manifest, log, campioni e certificati in
`out/suite/<versione>/<scenario>/rep<N>/`). Mediane dei tempi sulle ripetizioni pulite (criterio
del protocollo, sezione 6: core effettivi non inferiori al 90 % del miglior run della stessa
versione e scenario), picchi massimi su tutte le ripetizioni.

#### 5.2.1 Tabella per benchmark

| Benchmark | Versione | Rip. (pulite) | Picco private commit (GiB) | Picco working set (GiB) | Training (s) | BR esatta (s) | Totale interno (s) | max gain (a) | Esito |
|---|---|---:|---:|---:|---:|---:|---:|---:|---|
| HU10 | baseline-ba93c75 | 3 (2) | 3,067 | 3,062 | 1.446,0 | 267,2 | 1.739,0 | 0,002045732 | PASS |
| HU10 | A compatta double | 3 (2) | 2,111 | 2,107 | 1.202,1 | 269,9 | 1.496,9 | 0,002045732 | PASS (identica) |
| HU10 | B somme float32 | 3 (1) | 1,677 | 1,673 | 1.063,9 | 223,0 | 1.308,2 | 0,002041681 | PASS |
| HU10 | C float32 | 3 (2) | 1,242 | 1,238 | 1.112,2 | 252,4 | 1.384,3 | 0,002041738 | PASS |
| HU20 | baseline-ba93c75 | 3 (1) | 11,487 | 11,466 | 3.059,0 | 924,9 | 4.083,7 | 0,027999589 | PASS |
| HU20 | A compatta double | 3 (3) | 7,940 | 7,919 | 2.680,4 | 918,9 | 3.686,7 | 0,027999589 | PASS (identica) |
| HU20 | B somme float32 | 3 (2) | 6,196 | 6,178 | 2.574,4 | 826,6 | 3.480,2 | 0,027975831 | PASS |
| HU20 | C float32 | 3 (2) | 4,453 | 4,438 | 2.575,1 | 966,1 | 3.614,9 | 0,027996645 | PASS |
| HU30 | baseline-ba93c75 | 3 (2) | 11,830 | 11,809 | 3.566,1 | 1.113,7 | 4.785,5 | 0,191818521 | FAIL (lavoro fisso) |
| HU30 | A compatta double | 3 (1) | 8,180 | 8,157 | 2.739,8 | 867,0 | 3.697,0 | 0,191818521 | FAIL (identica) |
| HU30 | B somme float32 | 3 (3) | 6,383 | 6,364 | 2.916,0 | 957,2 | 3.953,3 | 0,191811144 | FAIL |
| HU30 | C float32 | 3 (2) | 4,586 | 4,571 | 2.811,9 | 979,4 | 3.869,5 | 0,191846648 | FAIL |
| HU40 | baseline-ba93c75 | 3 (1) | 11,830 | 11,809 | 3.620,5 | 1.301,0 | 5.036,2 | 0,289556855 | FAIL (lavoro fisso) |
| HU40 | A compatta double | 3 (2) | 8,180 | 8,158 | 2.873,4 | 1.003,8 | 3.967,5 | 0,289556855 | FAIL (identica) |
| HU40 | B somme float32 | 3 (2) | 6,383 | 6,364 | 3.005,1 | 1.009,6 | 4.105,6 | 0,289519745 | FAIL |
| HU40 | C float32 | 3 (2) | 4,586 | 4,571 | 3.010,1 | 1.023,8 | 4.108,9 | 0,289562140 | FAIL |
| HU10-FULL | tutte | 0 | | | | | | | RESOURCE_LIMIT (34,1 GiB di stato baseline con history7: non eseguibile su 32 GiB) |
| HU40-FULL | tutte | 0 | | | | | | | RESOURCE_LIMIT (746,5 GiB di stato baseline: non eseguibile) |
| HU20-2 | tutte | 0 | | | | | | | NON ESEGUITO (scenario derivato, escluso dalla fixture storica per decisione dell'utente) |

Picco del certificatore (tutte le ripetizioni uguali): baseline 2,01 GB su HU10, 7,63 GB su HU20,
7,86 GB su HU30 e HU40; candidate (certificatore corretto, E) 1,14 GB, 3,70 GiB, 3,82 GiB, 3,82
GiB. HU30 e HU40 non raggiungono l'1 % del piatto a 16.000 iterazioni con nessuna versione (la
baseline riproduce il valore storico): restano "non certificati" e sono confrontati a lavoro
fisso; nessuna versione li trasforma in superati.

#### 5.2.2 Confronto con la baseline (mediane pulite; delta di qualita' su tutte le ripetizioni, identici fra ripetizioni)

| Benchmark | Candidata | GiB risparmiati | Riduzione memoria | Secondi risparmiati (e2e) | Speedup training | Speedup e2e | delta max gain (a) | delta EV CO (a) | delta NashConv (a) | Criterio preregistrato |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| HU10 | A | 0,955 | 31,1 % | 242,2 | 1,203 | 1,162 | 0 | 0 | 0 | 7.1 identica: PASS |
| HU10 | B | 1,390 | 45,3 % | 430,9 | 1,359 | 1,329 | -4,05e-6 | -3,05e-6 | -5,49e-6 | 7.2 PASS |
| HU10 | C | 1,825 | 59,5 % | 354,7 | 1,300 | 1,256 | -3,99e-6 | -3,23e-6 | -8,12e-6 | 7.2 PASS |
| HU20 | A | 3,546 | 30,9 % | 397,0 | 1,141 | 1,108 | 0 | 0 | 0 | 7.1 identica: PASS |
| HU20 | B | 5,291 | 46,1 % | 603,5 | 1,188 | 1,173 | -2,38e-5 | +8,38e-6 | -3,35e-5 | 7.2 PASS |
| HU20 | C | 7,034 | 61,2 % | 468,8 | 1,188 | 1,130 | -2,94e-6 | +7,89e-6 | -1,17e-5 | 7.2 PASS |
| HU30 | A | 3,651 | 30,9 % | 1.088,4 | 1,302 | 1,294 | 0 | 0 | 0 | 7.1 identica: PASS |
| HU30 | B | 5,448 | 46,0 % | 832,2 | 1,223 | 1,211 | -7,38e-6 | +1,11e-5 | -2,69e-5 | 7.2 PASS |
| HU30 | C | 7,244 | 61,2 % | 916,0 | 1,268 | 1,237 | +2,81e-5 | +1,16e-5 | -5,9e-7 | 7.2 PASS |
| HU40 | A | 3,651 | 30,9 % | 1.068,7 | 1,260 | 1,269 | 0 | 0 | 0 | 7.1 identica: PASS |
| HU40 | B | 5,447 | 46,0 % | 930,6 | 1,205 | 1,227 | -3,71e-5 | +9,37e-6 | -4,89e-5 | 7.2 PASS |
| HU40 | C | 7,244 | 61,2 % | 927,3 | 1,203 | 1,226 | +5,28e-6 | +7,09e-6 | -1,58e-5 | 7.2 PASS |

Secondi CPU del trainer (mediana): baseline 7.501 / 19.544 / 21.410 / 21.507 su HU10 / HU20 / HU30 /
HU40; A 5.782 / 16.816 / 18.674 / 18.785 (-23 / -14 / -13 / -13 %); B 5.830 / 16.607 / 18.460 /
18.650; C 5.521 / 16.418 / 18.225 / 18.557. Il lavoro CPU e' l'indicatore piu' robusto alla
contesa: conferma che il guadagno di A e' un costo per iterazione inferiore, non solo meno
paginazione.

#### 5.2.3 Memoria per componente e per street (byte riservati, GiB)

| Benchmark | Versione | Regret | Somme | Policy (densa o compatta) | Timestamp | Mappa + albero + resto | Totale conteggiato | Picco commit | Non attribuito |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| HU10 | baseline | 0,868 | 0,868 | 0,868 | 0,204 | 0,168 | 2,976 | 3,067 | 0,090 |
| HU10 | A | 0,868 | 0,868 | 0,004 | 0,204 | 0,145 | 2,089 | 2,111 | 0,023 |
| HU10 | B | 0,868 | 0,434 | 0,004 | 0,204 | 0,145 | 1,655 | 1,677 | 0,022 |
| HU10 | C | 0,434 | 0,434 | 0,004 | 0,204 | 0,145 | 1,221 | 1,242 | 0,021 |
| HU20 | baseline | 3,481 | 3,481 | 3,481 | 0,768 | 0,168 | 11,379 | 11,487 | 0,108 |
| HU20 | A | 3,481 | 3,481 | 0,015 | 0,768 | 0,145 | 7,890 | 7,940 | 0,050 |
| HU20 | B | 3,481 | 1,740 | 0,015 | 0,768 | 0,145 | 6,150 | 6,196 | 0,047 |
| HU20 | C | 1,740 | 1,740 | 0,015 | 0,768 | 0,145 | 4,409 | 4,453 | 0,043 |
| HU30 / HU40 | baseline | 3,586 | 3,586 | 3,586 | 0,795 | 0,168 | 11,722 | 11,830 | 0,108 |
| HU30 / HU40 | A | 3,586 | 3,586 | 0,015 | 0,795 | 0,145 | 8,128 | 8,180 | 0,052 |
| HU30 / HU40 | B | 3,586 | 1,793 | 0,015 | 0,795 | 0,145 | 6,335 | 6,383 | 0,048 |
| HU30 / HU40 | C | 1,793 | 1,793 | 0,015 | 0,795 | 0,145 | 4,542 | 4,586 | 0,045 |

Celle per street (preflop / flop / turn / river): HU10 1.701 / 273.060 / 11.588.980 / 104.670.360
(righe 648 / 121.360 / 5.348.760 / 49.256.640), HU20 1.701 / 546.120 / 35.658.400 / 430.995.600
(righe 648 / 212.380 / 15.154.820 / 190.869.480), HU30 e HU40 481,4 M celle; byte per cella regret
+ somme 16 (baseline, A), 12 (B), 8 (C): il river vale il 90 % delle celle e dei byte su ogni
scenario (HU20: 6,42 GiB a 16 B/cella, 3,21 GiB a 8). La policy compatta per batch vale 5,2 MB
(HU10) e 17,7 MB (HU20-HU40) di picco contro 0,87 e 3,48-3,59 GiB della tabella densa:
646.757 e 2.207.461 celle allocate contro 116,5 M e 467,2-481,4 M; righe materializzate per
passata (media) 81 / 2.375 / 4.016 / 4.175 su 14.880 lookup mano-riga per street. I timestamp
del discount lazy (uint32 per riga, 0,77-0,80 GiB su HU20-HU40) sono ora la seconda voce dopo le
due tabelle. Board ripetuti: 66.304 su 1.024.000 estrazioni (6,5 %) in ogni run (stessa
sequenza di board a parita' di seme): una cache dei contesti avrebbe evitato al massimo il 6,5 %
di una fase da 36-46 s.

#### 5.2.4 Compromessi, variabilita' e limiti

- Tempi: le misure diurne sono contaminate dall'uso interattivo della macchina (core effettivi
  4,1-5,2 contro 6,0-6,5 nei run notturni a parita' di lavoro CPU; sezione 6 del protocollo);
  le mediane usano solo le ripetizioni pulite, che per alcune righe sono una sola (baseline
  HU20/HU40, A HU30, B HU10): gli speedup vanno letti con un'incertezza dell'ordine del 10 %.
  In ogni coppia di ripetizioni confrontabili A e' piu' veloce della baseline (8 coppie su 8) e
  usa meno CPU su tutti gli scenari; fra A, B e C le differenze di tempo (fino al 6 %) sono
  dentro la variabilita'.
- Traversata: su HU10 la traversata di A costa 853,9 s contro 742,4 s (+15 %) mentre la
  preparazione dei board scende da 363 a 43 s: il costo delle masse all-in si sposta dalla copia
  contigua per board alla raccolta dalle tabelle dense (sezione 2.3); il bilancio e' -209 s.
  Attribuzione con le sonde `--profile-traversal`: sezione 5.2.5.
- Memoria: i picchi sono deterministici (uguali in tutte le ripetizioni) e la riduzione e' uniforme
  su tutti i benchmark: A -30,9 / -31,1 %, B -45,3 / -46,1 %, C -59,5 / -61,2 %; il certificatore
  scende del 43-52 %. Con C il picco end-to-end su HU20-HU40 e' ora il trainer (4,45-4,59 GiB),
  non piu' il certificatore.
- Qualita': A e' bit-identica (stesse policy, stessi certificati). B e C restano entro le
  tolleranze preregistrate su tutti gli scenari (|delta EV| <= 3,7e-5 a contro 1e-4; |delta max gain|
  <= 3,7e-5 a contro 3e-4; stesso esito del certificato); C non supera il criterio per cella
  preregistrato (traiettoria diversa dalla seconda iterazione, emendamento 7.2), B lo supera
  (regret in double: policy corrente esatta, solo la media arrotondata, 5e-6 relativo).
- Nessuna candidata peggiora alcun benchmark in memoria, tempo pulito o qualita' oltre le
  tolleranze; nessun timeout, nessun OOM; HU10-FULL, HU40-FULL e HU20-2 non sono eseguiti e
  restano dichiarati come tali.

#### 5.2.5 Attribuzione del costo della traversata (sonde diagnostiche, fuori dalle misure)

Due sonde `--profile-traversal` (HU10, 2.000 iterazioni, eseguibili archiviati di baseline e A,
notte del 2026-09-23, log in `out/suite/tests/profile/`), stesso stato e stessa policy finale
(`fnv1a64:1b30f7b8f8128273` / `fnv1a64:0e9da3746f58004d` per entrambe), stessi contatori di
lavoro (25.983.401 nodi, 4.560.580.840 righe di policy lette, 4.159.096.710 celle di regret
scritte, 58.167 potature a reach zero): training 195,7 s -> 153,0 s; preparazione dei board 48,8
-> 5,4 s (CPU dei thread per le copie all-in 287,9 s -> 0; contesto dei board 62,4 -> 34,1 s);
refresh 44,9 -> 40,3 s; traversata 102,0 -> 107,4 s (+5,4 s, +5,3 %), interamente nella sezione
parallela (94,7 -> 99,9 s). Nelle fasi campionate (1 nodo su 1.024) l'unica che cresce e' il
terminale all-in preflop (0,097 -> 0,127 s campionati, +31 %): e' la raccolta delle masse di
vittoria dalle tabelle dense 630 x 630 attraverso gli id dei combo, meno favorevole alla cache
della copia contigua per board che la baseline pagava nella preparazione; le altre fasi (reach,
aggiornamento dei regret, fold, showdown) sono invariate. Nei run completi la differenza di
traversata su HU10 e' maggiore (+15 % nella ripetizione 1, sotto contesa) ma il bilancio resta
sempre a favore di A (-209 s su HU10, -320/-444 s di preparazione recuperati su HU20-HU40 contro
una traversata uguale o piu' veloce grazie all'assenza di paginazione).

### 5.3 Decisione

Implementazione unica raccomandata: candidata A (policy compatta per batch, all-in senza copia,
export in streaming, certificatore senza tabella segnaposto) con storage `double`: e'
bit-identica alla baseline su tutta la suite, riduce il picco di memoria del 31 % e il lavoro CPU
del 13-23 % su ogni benchmark. Lo storage `mixed` (B) e' la modalita' consigliata quando la
memoria e' il vincolo: -46 % con la policy corrente esatta e delta di qualita' dell'ordine di
1e-5 a; `float32` (C) resta disponibile dietro la stessa opzione `--table-storage` come modalita'
sperimentale (-61 %), con la divergenza di traiettoria documentata e la validazione affidata al
certificato. La baseline (HEAD `ba93c75`, eseguibili in `out/suite/bin/baseline-ba93c75`) e tutti i
run restano disponibili; le ottimizzazioni sono reversibili (nessun cambiamento di formato dei
file per `double`; identita' del trainer estesa solo per gli storage narrow).

## 5.4 Roadmap successiva e fase 1 (dal 2026-09-24)

Vincoli fissati dall'utente: il picco di memoria di ogni benchmark non deve superare quello
attuale di A (HU10 2,11 GiB, HU20 7,94, HU30/HU40 8,18) e il tempo va ridotto ulteriormente; i
run misurati girano solo fra le 00:00 e le 17:00 con build, test e sonde fra le 17:00 e le 18:00
fino al 25 settembre, e dal 26 settembre fra le 00:00 e le 20:00 con la manutenzione fra le 20:00
e le 21:00 (protocollo, sezione 6; la prima notte la finestra era 01:00-09:00, poi 00:00-18:00).

| Fase | Contenuto | Gate |
|---|---|---|
| 1. Tempo a memoria invariata | discount lazy in tempo costante (rapporto di prodotti prefissi), checkpoint finale scritto in parallelo all'export della policy, timer del refresh (raccolta righe attive / materializzazione), profilo del certificatore | HU20 e2e sotto 45 min allo stesso picco, poi 37m27s (requisito P9); criteri 7.2 |
| 2. Margine di memoria | timestamp del discount a 16 bit (-0,4 GiB), allocazione a blocchi delle righe mai visitate (dopo misura della copertura), `mixed` quando serve | almeno -1 GiB su HU20 senza cambiare policy |
| 3. Astrazione piu' fine nel budget | flop 200 -> 500 bucket (costo di memoria nullo, guadagno locale dimostrato dall'audit HU30), river cap 7 -> 23 con storage narrow e margine della fase 2, poi storia completa se la tendenza lo giustifica | max gain fisico su HU30/HU40 sotto 0,10 a, poi 0,03 a |
| 4. Feature universali | `[equity, hand strength, draw potential, nut potential, blockers, future distribution]` al posto dei soli istogrammi di equity | divario BR astratta / fisica per street; gate P9 |

### 5.4.1 Candidata D (fase 1): cosa cambia

- **Discount lazy in tempo costante.** `materialize_row` applicava a ogni regret positivo un
  prodotto di una moltiplicazione per iterazione saltata; con 10.647 righe materializzate per
  passata su 206 milioni (HU20) una riga viene rivisitata in media dopo migliaia di iterazioni,
  quindi il ciclo costava migliaia di moltiplicazioni per cella. Ora il fattore complessivo e' il
  rapporto `prefix[iteration] / prefix[last]` dei prodotti prefissi dei fattori
  t^1,5 / (t^1,5 + 1), che restano nell'intervallo [0,1, 1] (nessun underflow), come gia' per le
  somme di strategia. Il fattore applicato coincide con il prodotto sequenziale a meno
  dell'arrotondamento (test: errore relativo <= 1e-12 su 70 coppie di iterazioni fino a 20.000),
  ma il regret matching e' discontinuo in zero: una differenza a livello di arrotondamento puo'
  cambiare la traiettoria, come gia' osservato per lo storage float32. D non e' quindi
  bit-identica ad A e viene valutata con i criteri 7.2; il vecchio test di uguaglianza esatta
  eager/lazy (25 iterazioni sul gioco ridotto) mostra infatti differenze macroscopiche dopo il
  primo scambio di argmax e viene sostituito dal test del rapporto e dal determinismo fra thread,
  che resta esatto. Sonde: HU10 a 40 iterazioni delta max gain +5,3e-5 a, delta EV +9e-7 a;
  HU20 a 100 iterazioni +1,4e-4 a e -1,3e-5 a (su max gain di 0,28 e 1,38 a).
- **Scrittura finale sovrapposta.** I discount pendenti vengono materializzati una volta
  (`materialize_discounts`, con un marcatore che rende idempotenti i salvataggi), poi il
  checkpoint (copia grezza delle tabelle) viene scritto da un thread ausiliario mentre l'export
  della policy media procede in streaming sul thread principale; entrambi sono in sola lettura.
- **Timer del refresh.** `policy_refresh_collect_seconds` (raccolta righe attive, assegnazione
  slot, offset) e `policy_refresh_materialize_seconds` (materializzazione parallela) negli
  eventi del trainer, per guidare il passo successivo della fase 1.

### 5.4.2 Candidata D, ripetizione 1 (notte del 2026-09-24, finestra 01:37-04:52)

| Benchmark | Training D / A (s) | Refresh D / A (s) | di cui materializzazione | Scrittura D / A (s) | CPU D / A (s) | E2E D / A (s) | Speedup e2e vs baseline | Criterio |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| HU10 | 1.144 / 1.202 | 271 / 372 | 252 | 8 / 19 | 5.095 / 5.782 | 1.439 / 1.497 | x1,21 | 7.2 PASS (delta max gain -9e-8 a) |
| HU20 | 2.594 / 2.680 | 928 / 1.194 | 908 | 33 / 77 | 14.141 / 16.816 | 3.487 / 3.687 | x1,17 | 7.2 PASS (+7e-7 a) |
| HU30 | 2.426 / 2.740 | 825 / 1.322 | 808 | 35 / 90 | 15.804 / 18.674 | 3.324 / 3.697 | x1,44 | 7.2 PASS (-4,3e-5 a) |
| HU40 | 2.441 / 2.873 | 816 / 1.241 | 798 | 34 / 78 | 15.911 / 18.785 | 3.358 / 3.968 | x1,50 | 7.2 PASS (+1,5e-5 a) |

Picchi di memoria identici ad A (2,11 / 7,94 / 8,18 / 8,18 GiB). Il refresh scende del 27-38 % e la
scrittura finale del 55-60 %; la CPU del trainer del 12-16 % rispetto ad A. Il run HU20 (02:00-03:00)
ha avuto 5,35 core effettivi contro 6,4 degli altri (manutenzione notturna di Windows, probabile):
la sua traversata (1.622 s contro 1.454 di A) e' contaminata, le ripetizioni lo chiariranno.

Cosa resta nel refresh: la materializzazione (800-910 s su HU20-HU40) e' ormai un costo di accesso
casuale alla memoria. Ogni passata materializza le righe attive di ogni nodo di decisione della
street (HU20: 28 nodi flop x 2.375 righe + 68 turn x 4.016 + 124 river x 4.175 = 857.000
materializzazioni per passata, 27 miliardi in 16.000 iterazioni), circa 200 ns di CPU ciascuna:
il costo di un cache miss e di un page walk su tabelle da 7 GB, non di aritmetica. Lo stesso vale
per l'aggiornamento dei regret nella traversata (la fase dominante nel profilo campionato).

### 5.4.3 Candidata E (fase 1): prefetch software

Come D, con `_mm_prefetch` delle righe che verranno lette poche posizioni piu' avanti: nel
refresh la riga attiva 4 posizioni avanti (regret, somme, timestamp del discount), nella
traversata le celle di regret e somme della mano 8 posizioni avanti nel ciclo di aggiornamento.
Un prefetch non cambia alcun valore, quindi E e' bit-identica a D (stessi fingerprint di stato e
policy nelle sonde); si misura solo il tempo, con la stessa memoria. Alternativa successiva se il
prefetch non basta: pagine grandi (2 MB) per le tabelle, che riducono i page walk ma richiedono
il privilegio di lock della memoria su Windows.

Risultati di E: sezione 5.4.6 (tre ripetizioni). Sonde diurne (indicative, non misure): HU10 40
iterazioni 2,60 s -> 2,12 s di training, HU20 100 iterazioni 14,8 s -> 11,1 s; fingerprint
identici a D. Distanze di prefetch (sonde del 25 settembre, HU20 a 2.000 iterazioni, stato
bit-identico in tutte): nessun prefetch 284 s di training, 4/8 244,5 s, 8/16 242,9 s, 16/32
252,6 s: le distanze predefinite 4/8 restano e la variante E2 non entra nella suite.

### 5.4.4 Candidata F (fase 1): scheduling dinamico del certificatore

Il certificatore valutava i 573 flop a blocchi di 16 con un pool di thread ricreato a ogni blocco
e una barriera alla fine di ognuno. F usa un solo pool con un contatore atomico su tutti i flop
pendenti; il file di stato riceve comunque un gruppo di record ogni 16 flop completati (i record
portano l'indice del flop e il lettore accetta qualsiasi ordine) e l'aggregazione resta in ordine
di flop: il certificato e' identico bit per bit (sonda HU10: stessi max gain, NashConv, EV, guadagni
e limiti inferiori). Il guadagno atteso e' pero' piccolo: nei run notturni il certificatore usa
gia' 7,2-7,5 core su 8 (HU20 e HU40 di D: 826 e 850 s di valutazione, aggregazione 0,5-0,7 s,
caricamento 15 s) e i 5,8-6,7 core visti in precedenza erano run contaminati; la sonda HU10 passa
da 201,9 a 200,0 s. La BR esatta e' quindi limitata dal suo lavoro di valutazione (6.100-6.350 s
di CPU su HU20-HU40), non dall'inattivita': una riduzione sostanziale richiederebbe la
vettorizzazione di `evaluate_flop`, fuori dalla fase 1. F resta nella coda (tre ripetizioni) come
versione di riferimento "trainer di E + certificatore corretto".

### 5.4.5 Prossimo candidato in valutazione (G): righe intercalate

Dopo E, se la materializzazione e l'aggiornamento dei regret restano dominati dagli accessi
casuali, il passo successivo e' cambiare il layout delle due tabelle: oggi regret e somme di una
riga stanno in due tabelle separate (due cache miss per riga, piu' il timestamp in una terza
struttura); un layout per riga `[regret x azioni | somme x azioni | timestamp]` porta tutto in
una o due linee di cache contigue. Stessi byte totali (memoria invariata al byte), stessi valori
(bit-identico), formato del checkpoint diverso (identita' del trainer estesa). Da misurare con la
stessa suite; da decidere dopo i risultati di E.

### 5.4.6 Risultati della fase 1: D, E, F su tre ripetizioni (2026-09-24/25)

Report completo: `out/suite/report.md` (`suite.py report --baseline baseline-ba93c75`). Mediane
sulle ripetizioni pulite (D: due ripetizioni su HU20-HU40, tre su HU10; E e F: tre ovunque);
picchi di memoria identici ad A su ogni benchmark e ogni ripetizione (2,112 / 7,940 / 8,180 /
8,180 GiB); policy identiche fra ripetizioni della stessa versione e fra D, E, F (stessi
fingerprint: E e F sono bit-identiche a D per costruzione); criteri 7.2 PASS ovunque
(delta max gain rispetto alla baseline: HU10 -9e-8 a, HU20 +6,8e-7, HU30 -4,3e-5, HU40 +1,5e-5).

| Benchmark | Training D / E / F (s) | Refresh D / E / F (s) | Traversata D / E / F (s) | BR esatta D / E / F (s) | CPU trainer D / E / F (s) | Core trainer D / E / F | E2E D / E / F (s) | E2E A / baseline (s) | E2E F vs A / baseline |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| HU10 | 976 / 991 / 961 | 241 / 180 / 178 | 707 / 772 / 745 | 218 / 253 / 241 | 4.853 / 4.435 / 4.386 | 4,9 / 4,4 / 4,5 | 1.209 / 1.266 / 1.216 | 1.497 / 1.739 | -19 % / -30 % |
| HU20 | 2.142 / 2.090 / 2.009 | 857 / 598 / 582 | 1.471 / 1.451 / 1.387 | 818 / 962 / 892 | 14.006 / 11.532 / 11.569 | 6,3 / 5,2-5,6 / 5,5-5,7 | 3.007 / 3.102 / 2.953 | 3.687 / 4.084 | -20 % / -28 % |
| HU30 | 2.393 / 2.314 / 2.218 | 816 / 617 / 604 | 1.540 / 1.657 / 1.573 | 850 / 958 / 929 | 15.768 / 13.121 / 13.230 | 6,4-6,5 / 5,4-6,0 / 5,8-6,1 | 3.291 / 3.321 / 3.190 | 3.697 / 4.786 | -14 % / -33 % |
| HU40 | 2.424 / 2.443 / 2.239 | 811 / 636 / 600 | 1.576 / 1.764 / 1.600 | 860 / 956 / 930 | 15.919 / 13.232 / 13.358 | 6,4-6,5 / 5,3-5,8 / 5,8-5,9 | 3.332 / 3.448 / 3.221 | 3.968 / 5.036 | -19 % / -36 % |

Lettura.

- **Condizioni di misura.** Le ripetizioni di D su HU20-HU40 sono notturne e pulite (6,3-6,5 core
  su 8); tutte quelle di E e F hanno girato con 5,2-6,0 core: dal 24 settembre i processi
  dell'app Claude (0,3-0,5 core continui, circa 20.000 s di CPU in 16 ore) e, fino alla
  chiusura, Brave sottraggono capacita' anche nella finestra di misura. Il tempo di parete di E
  e F e' quindi sottostimato rispetto a D; il confronto robusto e' la CPU del trainer, che scende
  del 17 % da D a E/F su HU20-HU40 (del 9-10 % su HU10) a lavoro identico. Il verdetto sul tempo
  di parete e' affidato alla ripetizione 4 appaiata (D, E, F consecutive nelle stesse
  condizioni, sezione 5.4.8).
- **Refresh.** Il prefetch di E taglia la materializzazione del refresh del 25-30 % su HU20-HU40
  (857 -> 598, 816 -> 617, 811 -> 636 s) e del 25 % su HU10: la latenza di memoria era il costo
  dominante, come previsto in 5.4.2.
- **Traversata.** In tempo di parete non migliora (contaminazione: E su HU30/HU40 e' anche piu'
  lenta di D), ma la CPU totale del trainer scende: il prefetch nell'aggiornamento dei regret
  vale circa un terzo del guadagno di CPU.
- **Certificatore.** Con la stessa macchina contaminata E e F pagano 100-140 s in piu' di D
  (6,5-7,1 core contro 7,2-7,3); F rispetto a E recupera 30-70 s su HU20-HU40 (scheduling
  dinamico) con CPU uguale. Come stimato in 5.4.4 il guadagno di F e' piccolo: il certificatore
  e' limitato dalla valutazione (6.100-6.600 s di CPU).
- **Stabilita'.** F ha le ripetizioni piu' stabili della suite: HU10 961/961/964 s di training,
  HU20 2.009/1.995/2.068, HU30 2.218/2.221/2.120, HU40 2.243/2.239/2.188.
- **Obiettivo P9.** HU20 end-to-end e' a 49 minuti misurati (F) contro 61 di A e 68 della
  baseline; a 6,4 core (stima da CPU) sarebbero circa 45 minuti. Il requisito P9 (37 m 27 s)
  richiede ancora -20 %: non lo si ottiene con altre micro-ottimizzazioni del trainer, ma con
  la fase 2/3 e la linea sulla convergenza (meno iterazioni per lo stesso certificato).

Decisione: F (trainer di E + certificatore a scheduling dinamico) e' la versione di riferimento
del branch `feat/preflop-phase1-time` (HEAD = F piu' le distanze di prefetch configurabili con
gli stessi default 4/8). La candidata G (righe intercalate) resta rinviata: il prefetch ha gia'
tolto la parte del costo che G avrebbe attaccato, e il passo successivo con piu' valore e' la
fase 2 (margine di memoria), da cui dipende la fase 3.

### 5.4.7 Fase 2, candidata H: timestamp del discount lazy a 16 bit e copertura delle righe

Il discount lazy conservava per ogni riga delle tabelle l'iterazione dell'ultima
materializzazione in 32 bit: 0,77 GiB su HU20, 0,80 su HU30/HU40, 0,20 su HU10 (sezione 5.2.3,
colonna "Timestamp discount"). H la porta a 16 bit con un'epoca: lo slot di una riga vale 0 se
la riga non e' mai stata materializzata (tutte le celle ancora a zero), altrimenti
`ultima = base + slot - 1`; quando il target del discount raggiunge `base + epoca` (epoca
predefinita 65.535 iterazioni, opzione `--lazy-discount-epoch`), ogni riga toccata viene
materializzata al target con un solo rapporto di prodotti prefissi e la base si sposta li'. Un
run piu' corto dell'epoca non fa mai il rebase ed e' bit-identico a F (sonde HU10 40 iterazioni
e HU20 100 iterazioni: stato e policy identici a E, HU10 9a60bc552a23db61 / b2a5843bba44c910 e HU20 4a64454160bbf7b8 / 6f061ce7618487ff, sei suite di test PASS il 25 settembre alle 17:10); oltre l'epoca cambia solo l'associazione dei prodotti
(arrotondamento, come D rispetto ad A), e un run ripreso da checkpoint mantiene le iterazioni
di rebase del run continuo (base allineata all'epoca al caricamento; test
`test_lazy_discount_epoch`: rebase coincidente con la materializzazione finale bit-identico,
ripresa attraverso un rebase bit-identica, epoca 1). Le materializzazioni complete saltano le
righe mai toccate (nessun valore cambia), cosi' il conteggio resta valido anche con salvataggi
e valutazioni intermedie.

Il secondo contenuto di H e' la telemetria di copertura: nell'evento finale il trainer riporta,
per street, le righe che una passata ha toccato almeno una volta e le pagine da 4 KiB della
tabella dei regret che quelle righe attraversano (`rows_touched`, `regret_pages_touched`).
Sono i numeri da cui dipende la candidata I (allocazione sparsa: riserva dello spazio di
indirizzi e commit delle sole pagine toccate al primo accesso, su Windows `MEM_RESERVE` +
`MEM_COMMIT` con una mappa di bit delle pagine; su Linux `mmap` senza riserva): a 16.000
iterazioni con 32 board per iterazione il river di HU20 materializza 4.175 righe per passata
(66,8 milioni di materializzazioni in tutto, con ripetizioni) su 190,9 milioni di righe, quindi
la frazione toccata e' al massimo il 35 % e il risparmio potenziale sulle due tabelle del river
(6,4 GiB) e' di alcuni GiB, prima di ogni raffinamento dell'astrazione (fase 3), che aumenta le
righe totali ma non quelle toccate per iterazione. Prima misura (sonde del 25 settembre): a 40 iterazioni su HU10 sono gia' toccate il 14 % delle righe river e il 47 % delle righe turn, a 100 iterazioni su HU20 il 26 % delle righe river (49,6 su 190,9 milioni) e il 67 % delle righe turn, ma le pagine da 4 KiB toccate sono gia' il 100 % su ogni street: una riga occupa in media 2,3 celle (18 byte), una pagina ne contiene piu' di 200 e le righe toccate sono sparse su tutta la tabella. L'allocazione sparsa a granularita' di pagina (candidata I come progettata) non puo' quindi risparmiare nulla; una granularita' di riga richiederebbe un indice per riga (4 byte, 0,77 GiB su HU20) e solo la copertura a 16.000 iterazioni, misurata dai run di H, puo' dire se ha senso. Il margine di memoria per la fase 3 verra' percio' dallo storage narrow (B mixed -46 %, C float32 -61 %, gia' misurati con criteri 7.2 PASS) e dai timestamp a 16 bit di H.

Memoria attesa di H: HU10 2,01 GiB, HU20 7,56, HU30/HU40 7,78 (stessi tempi di F). Risultati
misurati: in coda per il 26 settembre (tre ripetizioni su HU10-HU40 dalle 06:30).

### 5.4.8 Ripetizione 4 appaiata: D, E, F consecutive nelle stesse condizioni

Per neutralizzare la contaminazione diurna, la ripetizione 4 esegue D, E ed F una dopo l'altra
sullo stesso benchmark (stesse condizioni entro un'ora): il confronto e' fra run adiacenti, non
fra notti diverse. HU20 (25 settembre, 13:10-15:45, app Claude aperta per tutti e tre):

| Versione | Training (s) | Refresh (s) | Traversata (s) | CPU trainer (s) | Core trainer | BR esatta (s) | Core BR | E2E (s) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| D | 2.418 | 868 | 1.509 | 14.034 | 5,69 | 938 | 6,68 | 3.398 |
| E | 1.928 | 560 | 1.330 | 11.434 | 5,79 | 908 | 6,89 | 2.878 |
| F | 1.932 | 565 | 1.329 | 11.535 | 5,83 | 877 | 7,22 | 2.853 |

A parita' di condizioni E riduce il training del 20 % rispetto a D (refresh -35 %, traversata
-12 %, CPU -18,5 %), quindi il guadagno di CPU misurato nelle ripetizioni 1-3 si trasferisce
quasi interamente al tempo di parete; F ha lo stesso trainer di E (differenze entro l'1 %) e un
certificatore piu' veloce del 3,4 % (877 contro 908 s, 7,2 contro 6,9 core). End-to-end F e' il
16 % sotto D e il 23 % sotto A (2.853 contro 3.687 s): 47,5 minuti con la macchina contaminata,
circa 43-44 stimati a 6,4 core. HU40, HU30 e HU10 appaiati nella notte del 26 settembre
(00:00-06:30, macchina libera): da aggiungere qui.

### 5.4.9 Fase 3: censimento delle astrazioni candidate (2026-09-26)

Tre mappe di righe di storia ricostruite in coda (37 s l'una, strumento `history_rows`, formato
history-v1) e memoria stimata con la formula calibrata su H (16, 12 o 8 byte per cella secondo lo
storage, 2 byte per riga di timestamp, 0,21 GiB fissi; certificatore 8 byte per cella piu' 0,2):

| Mappa | Righe flop / turn / river per nodo | Scenario | Celle | double | mixed | float32 | Certificatore | Tetto |
|---|---|---|---:|---:|---:|---:|---:|---:|
| history7 (flop 200, cap 7) | 7.585 / 222.865 / 1.539.270 | HU20 | 467 M | 7,56 | 5,82 | 4,08 | 3,68 | 7,94 |
| history7 | | HU30/HU40 | 481 M | 7,78 | 5,99 | 4,19 | 3,79 | 8,18 |
| flop 500, cap 7 | 11.801 / 267.785 / 1.845.609 | HU20 | 560 M | 9,02 | 6,93 | 4,85 | 4,38 | 7,94 |
| flop 500, cap 7 | | HU30/HU40 | 577 M | 9,29 | 7,14 | 4,99 | 4,50 | 8,18 |
| flop 500, cap 16 | 11.801 / 267.785 / 3.405.178 | HU20 | 997 M | 15,89 | 12,18 | 8,46 | 7,63 | 7,94 |
| flop 500, cap 16 | | HU30/HU40 | 1.027 M | 16,36 | 12,53 | 8,71 | 7,85 | 8,18 |
| flop 200, cap 16 | 7.585 / 222.865 / 2.884.576 | HU20 | 844 M | 13,48 | 10,34 | 7,19 | 6,49 | 7,94 |
| flop 200, cap 16 | | HU30/HU40 | 869 M | 13,87 | 10,64 | 7,40 | 6,67 | 8,18 |

Correzioni alla roadmap della sezione 5.4: il flop a 500 bucket non ha costo nullo (le chiavi di
turn e river includono il bucket flop: +20 % di righe river, +20 % di celle) e sta nel tetto solo
con storage mixed o float32; il river a cap 23 non sta nel tetto neppure in float32 (stima
8,63 GiB su HU30/HU40 a flop 200, sezione 5.4.7 e memoria del 26 settembre); l'allocazione sparsa
e' esclusa (99 % delle righe river toccate a 16.000 iterazioni). La prima candidata della fase
3 e' quindi K = flop 200, river cap 16, storage float32 (`cand-k-river16-float32`: HU20 7,19,
HU30/HU40 7,40 GiB stimati), con la mappa `out/phase3/history_f200_cap16.bin`
(fingerprint bbb89834017b028e) dichiarata come override di astrazione della versione nella
suite (il run e' marcato come deviazione dal protocollo comune e non entra nei confronti a
lavoro fisso). Il flop a 500 con cap 7 in mixed (6,93 / 7,14 GiB) e' la seconda opzione.

### 5.4.10 Linea A, candidata J: motore river congiunto del certificatore (2026-09-26)

Analisi (agent architetto, conteggio delle operazioni calibrato sui run): per ogni board il
certificatore attraversava ogni sottoalbero river quattro volte (due eroi per due modalita',
risposta e media), ripeteva tre volte su quattro le stesse passate di showdown, leggeva ogni riga
della policy tre volte con una chiamata virtuale per mano e ricostruiva per ogni board il
contesto di flop e turn che il river non usa. J valuta ogni sottoalbero river una sola volta
per board con i due eroi come corsie SSE2 e le due modalita' insieme, copia le righe della policy
del nodo in blocchi contigui con prefetch, costruisce solo il contesto river con un cursore di
storia per (turn, mano) e replica i kernel operazione per operazione: il certificato e'
bit-identico al motore di riferimento, che resta selezionabile (`--river-engine reference`).
Revisione statica (agent Opus) prima dell'unica build: nessun errore di compilazione o di
esattezza, quattro modifiche minori applicate (flop ordinato nel prefisso, caso di massa nulla
nei test, confronto del certificato completo, commenti).

Build del 26 settembre alle 20:06: sei suite di test PASS (kernel a due corsie identici a quelli
scalari su 100 board; cursore di storia uguale a `row()` su 127.256 ricerche; 1.386 sottoalberi
identici a `ValueTraversal`; 7 flop identici al motore di riferimento, 2,89 s contro 4,80 s).
Sonda HU10 (policy a 40 iterazioni, fingerprint identici a E/F/H): certificato identico
(max gain 0,2845395268589276 in entrambi), valutazione 84,4 s contro 223,2 s, **2,6 volte piu'
veloce**. Misure sui quattro benchmark nella notte del 27 settembre.

Primi riferimenti a 32.000 iterazioni (astrazione history7, eseguibili di H): HU30 max gain
0,1676 a (0,1918 a 16.000), HU40 0,2526 a (0,2896), end-to-end 91 e 92 minuti: raddoppiare le
iterazioni riduce il max gain del 13 % circa, lontano dall'1 % (0,03 a). Prima misura della
candidata L (flop 500, river cap 12, float32) su HU30 a 16.000: max gain 0,2062 a (peggiore
dell'astrazione attuale allo stesso numero di iterazioni), picco 7,48 GiB (stima 7,46), training
2.229 s come H; il confronto utile e' a 32.000 iterazioni (27 settembre).

### 5.4.11 Misure del 26-27 settembre: J sui quattro benchmark, astrazioni K e L, riferimenti a 32.000 iterazioni

**J sui quattro benchmark** (ripetizione 1 nella notte del 27, ripetizione 2 su HU30/HU40 la sera
del 27): certificati bit-identici a H (max gain 0,0020456 / 0,0280003 / 0,1917755 / 0,2895718 a
su HU10/HU20/HU30/HU40), certificazione 96 / 318 / 325 / 297 s contro 220 / 821 / 856 / 856 s di H,
cioe' 2,3 / 2,6 / 2,6 / 2,9 volte piu' veloce (ripetizione 2: 326 e 328 s). Il trainer e' quello di
H (stesse policy): le differenze di training fra le serie sono carico della macchina.

**Astrazioni della fase 3** (max gain fisico in a; K e L in float32, history7 in double):

| Versione | Astrazione | HU30 16k | HU30 32k | HU40 16k | HU40 32k | Picco trainer |
|---|---|---:|---:|---:|---:|---:|
| H / J | history7 (flop 200, river cap 7) | 0,1918 | 0,1676 | 0,2896 | 0,2526 | 7,78 GiB |
| K | flop 200, river cap 16 | 0,2124 | 0,1690 | 0,3179 | 0,2525 | 7,41 GiB |
| L | flop 500, river cap 12 | 0,2062 | 0,1586 | 0,3109 | 0,2399 | 7,48 GiB |

K non migliora a 32.000 iterazioni (+0,8 % su HU30, invariato su HU40) pur dimezzando l'errore
interno del river (RMS 0,067 -> 0,033); L migliora del 5,4 % e del 5,0 % a 32.000 iterazioni ed e'
peggiore a 16.000 (piu' righe da riempire). I run a 32.000 iterazioni sono deterministici: il
riferimento history7 del 26 (`ref-h-32k`) e quello del 27 con policy conservata (`diag-h-32k`)
danno lo stesso max gain al bit.

**HU20** (certificato fisico, prima del nuovo criterio): 8.000 iterazioni 0,0438 a in 23,8 minuti
end-to-end, 12.000 0,0335 in 29,0, 16.000 0,0280 in 39,7; batch 64 a 8.000 iterazioni 0,0303 in
38,3 minuti (a parita' di tempo di training circa il 5 % peggiore del batch 32, interpolato a 0,0287). Il tempo di HU20 e' sospeso per decisione
dell'utente del 27 settembre (priorita' a HU30 e HU40, HU20 ripreso dopo).

### 5.4.12 Candidata N: best response fisica ristretta per street (27-28 settembre)

Commit `943c7b4`: opzione `--deviation-from preflop|flop|turn|river|none` del certificatore; il
giocatore che devia segue la policy fino alla street indicata e risponde al meglio da li' in poi. I
certificati ristretti sono marcati `restricted`, non superano mai il target e l'export li rifiuta.
Test: oracolo su gioco finito nel trainer (`check_street_restrictions`) e
`test_street_restricted_response`. Guadagno del CO (a) con le policy a 32.000 iterazioni:

| Astrazione | Benchmark | Completa | Dal flop | Dal turn | Solo river | Quote postflop flop / turn / river | Limp-check (quota del guadagno dal flop) |
|---|---|---:|---:|---:|---:|---|---:|
| history7 | HU30 | 0,1676 | 0,0558 | 0,0374 | 0,0210 | 33 / 29 / 38 % | 84 % |
| history7 | HU40 | 0,2526 | 0,1163 | 0,0825 | 0,0486 | 29 / 29 / 42 % | 79 % |
| L | HU30 | 0,1586 | 0,0531 | 0,0358 | 0,0202 | come history7 | |
| L | HU40 | 0,2399 | 0,1106 | 0,0790 | 0,0462 | come history7 | |

Nessuna street supera il 50 % della quota postflop: non esiste una correzione su una sola street. Il
piatto limpato domina (limp-check 79-84 % del guadagno dal flop, circa 99 % con il limp/iso/call). Il guadagno completo e' amplificato dal preflop: tenendo la policy fino al
flop il CO sfrutta solo il 33 % (HU30) e il 46 % (HU40) del guadagno completo; la risposta
completa arriva al limp-check con probabilita' 0,51-0,57 contro 0,12-0,20 della policy, per portare nel
piatto limpato le mani rappresentate male. L riduce tutte le street del 4-5 % in modo uniforme.

### 5.4.13 Nuovo criterio di accettazione e best response astratta (28 settembre)

Decisioni dell'utente nella notte del 28 (protocollo, commit `0980dec` delle 02:03 e `ae13445` delle 02:38): un benchmark
e' accettato con best response esatta dentro l'astrazione <= 0,03 a (1 % del piatto iniziale) e
certificato fisico <= 0,15 a (5 %, limite superiore e non piu' obiettivo). La best response astratta
(`gtosd_preflop_blueprint_abstract_br`) restringe il giocatore che devia alle righe
dell'astrazione e somma i valori su tutte le 605.088 sequenze di board (573 flop canonici x 33
turn x 32 river); oggi costa 47-54 minuti a misura, perche' riusa il trainer un board alla volta.

Valutazione del postflop esatto per flop (workflow del 28, solver `libs/postflop`): fattibile
tecnicamente, memoria entro 8 GiB un flop alla volta, ma 19-29 ore per passata su HU30/HU40
(32-50 volte oltre i 35 minuti) e 56-88 ore su HU100, con il problema dell'accoppiamento
preflop-postflop. Scartato come strada principale, resta un oracolo per i controlli puntuali.

HU100 (definito dall'utente il 28: open CO 150 % del piatto, isolation del BTN sul limp 150 %,
3-bet del BTN 100 %, limp/raise non all-in del CO 100 %, stack 100 a, all-in sempre disponibile):
bozza `out/hu100/HU100_draft.json`, 1.543 nodi, decisioni postflop 54 / 166 / 374, 1.443 milioni di
celle (3 volte HU40), trainer stimato 22,9 GiB double / 17,5 mixed / 12,1 float32: non entra in
8 GiB con l'astrazione attuale. Il motore aggiunge l'all-in solo se non supera il 1000 % del piatto
(`game_model.cpp`): a 100 a mancano l'open-shove del CO, lo shove di isolamento del BTN e gli
all-in dei primi livelli del piatto limpato (correzione in attesa dell'ok dell'utente).

### 5.4.14 Fase 0: curve di convergenza e taratura della best response campionata (28 settembre)

Run con policy conservata (`diag-h-48k`, `diag-h-64k`, astrazione history7, eseguibili di J) e best
response astratta esatta sulle policy:

| Benchmark | Iterazioni | Training | BR astratta | Certificato fisico | Fisico - astratta | Tempo BR astratta |
|---|---:|---:|---:|---:|---:|---:|
| HU30 | 32.000 | 4.473 s | 0,034880 | 0,167639 | 0,1328 | 3.230 s |
| HU30 | 48.000 | 6.239 s | **0,026921** | 0,160893 | 0,1340 | 2.820 s |
| HU40 | 32.000 | 4.234 s | 0,064020 | 0,252620 | 0,1886 | 3.052 s |
| HU40 | 64.000 | 8.625 s | 0,041717 | 0,238264 | 0,1965 | 2.820 s |

- **HU30 a 48.000 iterazioni passa il criterio principale** (0,0269 <= 0,03) e supera del 7 % il
  limite fisico di 0,15 a (0,1609): non e' ancora accettato.
- **La best response astratta scende come T^-0,64 (HU30) e T^-0,62 (HU40)**: nessun plateau, ma
  lontano dal ritmo T^-1 di DCFR senza campionamento. Estrapolazione: HU30 sotto 0,03 a circa
  40.500 iterazioni (circa 88 minuti di training), HU40 a circa 109.000 (circa 4,1 ore).
- **Il certificato fisico scende come T^-0,10 e T^-0,08** e la differenza fisico - astratta resta
  ferma o cresce (HU30 0,133 -> 0,134, HU40 0,189 -> 0,197): su HU40 le iterazioni non bastano per
  lo 0,15 (serve la rappresentazione, fase 3); su HU30 servirebbe una best response astratta
  intorno a 0,016-0,017, cioe' circa 100.000 iterazioni (estrapolazione).
- **Bilancio dei 35 minuti:** con il nuovo criterio il tempo end-to-end comprende training,
  certificato fisico (circa 5 minuti) e best response astratta (47 minuti oggi, da sola oltre il
  limite). Con una best response astratta da 10 minuti restano circa 20 minuti di training, cioe'
  circa 9.000 iterazioni al costo attuale: servirebbe convergere 4,5 volte piu' in fretta su HU30
  e 12 volte su HU40.

Regola pre-registrata (28 settembre, 03:00): HU40 a 64.000 iterazioni <= 0,040 -> fase 1 (arresto
automatico e best response astratta veloce); >= 0,048 -> fase 2 (convergenza per iterazione);
fascia intermedia -> decisione dell'utente. Esito 0,0417: fascia intermedia, decisione chiesta
all'utente alle 06:24 con raccomandazione per la fase 2.

**Taratura della best response astratta campionata** (`--sample-flops`, policy a 32.000 iterazioni,
due seed per dimensione del campione):

| Benchmark (esatta) | 16 flop | 32 flop | 64 flop |
|---|---|---|---|
| HU30 (0,034880) | 0,2318 / 0,2175 (6,6x / 6,2x) | 0,1387 / 0,1376 (4,0x / 3,9x) | 0,0984 / 0,1038 (2,8x / 3,0x) |
| HU40 (0,064020) | 0,2732 / 0,2437 (4,3x / 3,8x) | 0,1883 / 0,1554 (2,9x / 2,4x) | 0,1512 / 0,1343 (2,4x / 2,1x) |
| Tempo | 94-110 s | 170 s | 322-327 s |

Il campione sovrastima sempre, di 2-7 volte: con pochi flop ogni riga compare su pochi board e il
giocatore che devia si adatta a quei board, avvicinandosi a una best response fisica sul campione.
Il fattore dipende dal benchmark e dal seed, quindi non esiste una correzione fissa: la best
response campionata non serve come stima ne' come filtro per la soglia di 0,03 a. L'arresto
automatico e ogni esperimento sulla convergenza richiedono la best response astratta esatta resa
veloce (tecniche di J piu' simmetria di seme su turn e river; obiettivo circa 10 minuti).

### 5.4.15 Pausa della linea HU (28 settembre, pomeriggio)

Dopo la fase 0 l'utente ha chiarito che il prodotto e' un preflop multiway fino al 6-way su un server
da 52 core e 256 GB, e che 8 GiB e 35 minuti valgono solo per la suite HU10-HU40. Le fasi 1 e 2 sui
35 minuti di HU40 sono in pausa (la best response astratta veloce resta progettata: 330-425 s stimati,
bit-identica, circa 30 ore di codice). Il lavoro continua sulla riproduzione della ricetta
MonkerSolver: [MONKER_RECIPE_REPRODUCTION_2026-09-28.md](MONKER_RECIPE_REPRODUCTION_2026-09-28.md).

## 6. Procedura di riproduzione

Vedi la sezione 8 del protocollo. Le build delle versioni: baseline dall'HEAD pulito
(`out/build/windows-release-suite` configurato con `configure_suite.cmd`), candidate dallo
stesso albero con le modifiche di questo milestone.
