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
  working set e di commit privato dopo il training e alla fine.
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
run misurati girano solo fra le 01:00 e le 09:00 (protocollo, sezione 6).

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

Risultati di E: da riempire con il report della suite (run notturni, dopo le ripetizioni di D).

## 6. Procedura di riproduzione

Vedi la sezione 8 del protocollo. Le build delle versioni: baseline dall'HEAD pulito
(`out/build/windows-release-suite` configurato con `configure_suite.cmd`), candidate dallo
stesso albero con le modifiche di questo milestone.
