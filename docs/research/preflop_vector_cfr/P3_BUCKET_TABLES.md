# P3 — Clustering e tabelle bucket

Data: 2026-09-15. Branch di fase: `feature/preflop-blueprint-p3-clustering`. Esito del gate: **PASS**.

## 1. Cosa è stato prodotto

| Componente | File | Contenuto |
|---|---|---|
| Clustering | `libs/card_abstraction/src/bucket_tables.cpp`, `include/gtosd/card_abstraction/bucket_tables.hpp` | k-means intero con inizializzazione k-means++ sul campione sistematico, `restarts` riavvii valutati per `screening_iterations` iterazioni sul campione, il migliore rifinito per `maximum_iterations` sull'intero insieme; scelta per inerzia minima; rietichettatura dei bucket per forza crescente del centroide |
| Distanze | idem | flop e turn: EMD monodimensionale esatta come L1 delle cumulate degli istogrammi a 16 bin; centroide = mediana pesata coordinata per coordinata delle cumulate (baricentro L1 esatto). River: L2 al quadrato sui 9 vettori OCHS in virgola fissa; centroide = media pesata arrotondata |
| Tabelle | `BucketTable` | `uint16` per (board canonico, combo); combo sovrapposte al board = `no_bucket`; centroidi salvati; parametri e fingerprint di catalogo e feature incorporati nel file (`kind = bucket_table`, versione 1, checksum FNV-1a) |
| Lookup | `lookup_flop_bucket`, `lookup_turn_bucket`, `lookup_river_bucket` | board fisico + mano → canonicalizzazione del board, permutazione applicata alla mano, indice combo, lettura della riga: tempo costante |
| Gruppi sintetici | `OpponentGroups::from_ranking` | gruppi avversari costruiti da un ranking arbitrario, usati dal test e dallo smoke senza la tabella all-in |
| Eseguibile | `benchmarks/preflop_blueprint_buckets.cpp` | carica le risorse P2 (o le ricostruisce), esegue i tre clustering, salva, ricarica e verifica, stampa il report JSON |
| Test | `tests/card_abstraction_bucket_tests.cpp` | 16.431.981 asserzioni |

## 2. Verifiche eseguite

| Verifica | Esito |
|---|---|
| Determinismo a seed fisso (due costruzioni identiche bit a bit) | PASS |
| Indipendenza dal numero di thread (1, 3, 8 thread: tabelle uguali) | PASS |
| Invarianza ai semi: board e combo permutati insieme danno lo stesso bucket | PASS |
| Copertura: ogni combo viva di ogni board canonico ha un bucket `< capacity`; ogni combo sovrapposta ha `no_bucket` | PASS |
| Coerenza sotto lo stabilizzatore del board (mani equivalenti, stesso bucket) | PASS (conseguenza della costruzione nel frame canonico, verificata sui board con orbita ridotta) |
| Nessuna dipendenza dalle carte future: le API ricevono solo mano e board visibile | PASS (per costruzione delle firme) |
| Persistenza: salvataggio, ricaricamento, uguaglianza; rifiuto di file corrotti e di fingerprint diversi | PASS |
| Ordine dei bucket per forza crescente del centroide | PASS |
| `ctest -L p3`: test unitari (47,85 s) e smoke dell'eseguibile con gruppi sintetici (21,7 s) | PASS |

## 3. Tabelle alle capacità di default (200/500/1.000)

Comando: `gtosd_preflop_blueprint_buckets --resources-dir out/preflop_blueprint_resources
--output-dir out/preflop_blueprint_buckets_200_500_1000 --threads 8 --flop 200 --turn 500
--river 1000 --restarts 10 --screening-iterations 10 --max-iterations 25 --screening-sample 500000`.
Seed di partizione `0x5041525449544F49` (5782993918481418575). Catalogo `fnv1a64:51879f40626cb7dd`.
Le tabelle dipendono solo dal mazzo: valgono per HU10 e per CO40.

| Street | Capacità | Righe (board canonici) | Osservazioni | Peso totale | Riavvio scelto | Iterazioni | Inerzia finale | Distanza media dal centroide | Occupazione (righe) min / max | Reseed vuoti | Byte | Secondi |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Flop | 200 | 573 | 302.544 | 3.769.920 | 6 | 25 (limite) | 5,68·10⁸ | 150,7 | 449 / 3.035 | 0 | 728.380 | 55,5 |
| Turn | 500 | 13.761 | 6.825.456 | 116.867.520 | 1 | 8 (convergenza) | 9,49·10⁸ | 8,12 | 2.399 / 102.648 | 0 | 17.354.860 | 416,0 |
| River | 1.000 | 19.998 | 9.299.070 | 175.301.280 | 8 | 25 (limite) | 1,68·10¹⁶ | 9,61·10⁷ | 1.239 / 326.951 | 0 | 25.215.480 | 813,8 |

Fingerprint: flop `fnv1a64:33f06cf437f8f26d`, turn `fnv1a64:51814338fcf1236c`, river
`fnv1a64:2e59aa76f59c0fcd`. Salvataggio e ricaricamento verificati (`reload_verified: true`).
Tempo totale 1.294 s a 8 thread (preparazione 7,1 s). File prodotti: 43,3 MB in tre file,
gitignorati sotto `out/`.

Lettura delle dispersioni. La distanza flop è una L1 fra cumulate di conteggi su 465 runout e 16
bin: il massimo teorico è 465 × 15 = 6.975, quindi la media di 150,7 vale circa il 2,2 % del
massimo. Al turn il massimo è 30 × 15 = 450 e la media 8,12 vale l'1,8 %. Al river la distanza è
un quadrato L2 su 9 coordinate a scala 65.535: la radice della media, 9.804, corrisponde a uno
scarto quadratico medio per coordinata di circa 3.270 unità, cioè 0,050 di equity per gruppo
avversario. L'occupazione è squilibrata ma senza bucket vuoti; la rietichettatura per forza rende
gli id confrontabili fra costruzioni.

## 4. Osservazioni e dubbi

1. Flop e river si sono fermati al limite di 25 iterazioni; il turn ha raggiunto la convergenza
   in 8. La roadmap §3.4 fissa i riavvii (10, intervallo 3–50) ma non il numero massimo di
   iterazioni: per le tabelle finali di P9 conviene misurare l'inerzia con 50 e 100 iterazioni e
   registrare la differenza. Non è un requisito del gate P3.
2. Il costo è dominato dal river (814 s) e dal turn (416 s): l'assegnazione di 9,3 milioni e 6,8
   milioni di osservazioni pesate a 1.000 e 500 centroidi per iterazione. È una costruzione una
   tantum per identità dell'astrazione; il file salvato evita di ripeterla.
3. La scelta "solo distanze fisse" (EMD/L2) è quella della roadmap §3.4; nessuna feature o peso è
   stato scelto guardando i risultati Monker.

## 5. Fallimenti registrati

1. Il primo tentativo di accesso ai membri privati di `BucketTable` dal builder (`struct Access :
   BucketTable`) non compila: i membri privati non sono accessibili a una classe derivata.
   Corretto con il pattern attorney (`friend struct BucketTableBuilderAccess` dichiarato
   nell'header e definito nel `.cpp`).
2. Il lancio in background della costruzione completa tramite il wrapper dell'ambiente Visual
   Studio ha stampato un errore non fatale (`vswhere.exe` non trovato nel PATH del processo) e ha
   prodotto un log che sembrava vuoto; il processo era invece in esecuzione. Un secondo lancio
   diretto ha fallito per il lock del file di log, un terzo ha avviato un processo duplicato che
   è stato terminato dopo 30 s per non contendere la CPU e la directory di output. Il run
   originale è arrivato a `PASS`. Lezione: verificare il processo con `Get-Process` prima di
   rilanciare.

## 6. Comandi

```text
cmake --build out/build/windows-release --target gtosd_card_abstraction gtosd_card_abstraction_bucket_tests gtosd_preflop_blueprint_buckets
ctest --test-dir out/build/windows-release -L p3 --output-on-failure -V
gtosd_preflop_blueprint_buckets --resources-dir out/preflop_blueprint_resources --output-dir out/preflop_blueprint_buckets_200_500_1000 --threads 8 --flop 200 --turn 500 --river 1000 --restarts 10 --screening-iterations 10 --max-iterations 25 --screening-sample 500000
```
