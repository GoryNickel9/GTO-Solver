# P7 — Certificatore board-major

Data: 2026-09-16. Branch di fase: `feature/preflop-blueprint-p7-certifier`. Esito del gate:
**PASS** (§6): test PASS, passata esatta su HU10 completata (0,0042 a, 37 min sul completo),
proiezione CO40 misurata (22,4 h a 8 thread).

## 1. Cosa è stato prodotto

| Componente | File | Contenuto |
|---|---|---|
| Valutatore a due stadi | `include/gtosd/preflop_blueprint/best_response.hpp`, `libs/preflop_blueprint/src/best_response.cpp` | `BestResponseEvaluator`: stadio 1 `evaluate_flop` (valori di ogni combo alle entrate postflop per un gruppo di flop con i suoi runout, thread-safe), stadio 2 `aggregate` (aggregazione preflop su un insieme di `FlopValues`, con `exact`); `FlopValues` porta le **immagini** del flop (permutazioni dei semi della sua orbita) e l'aggregazione legge il valore della mano `h` sull'immagine `σ(flop)` come valore di `σ⁻¹(h)` sul flop canonico; `flop_images`; lo stimatore campionato P6 è il wrapper `evaluate_best_response` |
| Certificatore | `include/gtosd/preflop_blueprint/certifier.hpp`, `libs/preflop_blueprint/src/certifier.cpp` | passata esatta sui 573 flop canonici del catalogo con tutti i 33 × 32 runout (605.088 board per 7.140 flop fisici), parallela per flop a chunk, stato ripristinabile (`GTOSDCRT`: header con fingerprint di albero, policy e catalogo; record per flop con checksum, record troncati ignorati), `flop_limit` per passate parziali di misura, modalità campionata (`sample_flops`, lo stimatore P6 come comando separato), certificato `gtosd.preflop_blueprint_certificate.v1` con `exact/partial/sampled`, fingerprint di regole, albero, catalogo, tre tabelle bucket e policy, metriche, tempi, memoria |
| File di policy | `include/gtosd/preflop_blueprint/policy_file.hpp`, `libs/preflop_blueprint/src/policy_file.cpp` | `GTOSDPOL` v1: fingerprint dell'albero, capacità, sorgente, tabella densa `(nodo, riga, azione)`, checksum; scrittura atomica; caricamento solo per l'albero con lo stesso fingerprint; `policy_fingerprint` |
| Helper binari | `libs/preflop_blueprint/src/binary_io.hpp` | serializzazione little-endian condivisa da checkpoint, policy e stato del certificatore |
| Eseguibili | `benchmarks/preflop_blueprint_certify.cpp`; `benchmarks/preflop_blueprint_train.cpp` (`--policy-out`) | `--policy` o `--uniform`, `--threads`, `--chunk`, `--state`, `--flop-limit`, `--sample-flops/--sample-seed`, `--output`; progresso JSON per chunk con ETA; esito `EXACT`, `PARTIAL`, `SAMPLED` |
| Test | `tests/preflop_blueprint_certifier_tests.cpp` (373.068 asserzioni), `tests/preflop_blueprint_test_support.hpp` (helper condivisi con i test del trainer) | file di policy, orbite, ripresa, comando campionato |

## 2. Metodo

**Esattezza per orbite.** La strategia media è simmetrica nei semi per costruzione (righe = classi
di mano al preflop, bucket canonici dopo), quindi i valori di una mano `h` sul flop fisico
`σ(F)` sono i valori di `σ⁻¹(h)` sul rappresentante canonico `F`. Il certificatore valuta ogni
flop canonico una volta (stadio 1, tutti i runout fisici) e nell'aggregazione preflop somma su
tutte le immagini `σ` dell'orbita (una permutazione per immagine distinta, `flop_images`; il
numero di immagini è verificato uguale alla molteplicità del catalogo). Con tutte le orbite ogni
combo è compatibile con esattamente 5.984 flop fisici: `aggregate(exact = true)` lo verifica e
azzera gli errori standard. Il costo è quello di 573 flop invece di 7.140.

**Invarianti.** Con il catalogo completo `EV_CO + EV_BTN = 0` e la stima naive coincide con la
best response esatta (nessuna selezione: la scelta preflop è sulla distribuzione completa dei
flop). Su un sottoinsieme di flop (parziale o campionato) i due giocatori condizionano su
conteggi di flop compatibili diversi e l'EV non è a somma zero: il test lo verifica solo sulla
passata esatta.

**Ripresa.** Dopo ogni chunk i `FlopValues` dei flop completati sono accodati allo stato con
checksum per record; alla ripresa l'header deve coincidere (albero, policy, catalogo, numero di
entrate) e i record validi sono riletti, quelli troncati ricalcolati. L'aggregazione è
deterministica nell'ordine dei flop: passata ripresa e continua coincidono bit per bit.

## 3. Verifiche eseguite

| Verifica | Esito |
|---|---|
| File di policy: round trip bit-identico della tabella (308.217 valori su HU10 ridotto), fingerprint uguale in memoria e su file, header leggibile senza gioco, policy di un altro albero respinta (`game_mismatch`), file corrotto respinto (`integrity_failure`) | PASS |
| Orbite: due flop canonici (molteplicità 4 e 12) contro l'enumerazione esplicita delle 16 immagini fisiche: valori per combo uguali a quelli della mano preimmagine entro `1e-12` (compatibilità inclusa), EV / best response / limite inferiore dell'aggregazione per orbite uguali all'enumerazione entro `1e-12`, ogni immagine ricondotta dal catalogo al flop canonico | PASS |
| Passata parziale a chunk (7 flop, chunk 3, 3 callback di progresso, 7 × 1.056 board) e passata interrotta (4 flop) più ripresa (7 flop): report identici bit per bit, 4 flop riletti; stato di un'altra policy respinto; guadagni ≥ 0; stima naive ≥ limite inferiore; certificato JSON con schema, fingerprint di albero, policy e catalogo, flag | PASS |
| Comando campionato = stimatore del trainer sugli stessi flop (EV, best response, limite inferiore entro `1e-12`, stesso numero di board) dopo il round trip della policy | PASS |
| `ctest -L p7`: test (96 s) e smoke dell'eseguibile (4 flop di HU10 ridotto con policy uniforme, 10 s) | PASS |
| Suite `preflop_blueprint` P0–P7 | PASS: 18/18 in 506 s (`ctest -L preflop_blueprint`, Release) |

## 4. Passata esatta su HU10

Policy: strategia media dei checkpoint P6 (DCFR alternato, 200/500/1.000, 2.000 iterazioni, `B = 32`),
esportata con `--policy-out`. Passata esatta con 8 thread, chunk di 16 flop, stato su file.

| Gioco | Flop canonici / fisici / board | EV CO | Gain CO / BTN | Max gain | nashconv | Limite inferiore CO / BTN | Tempo | Memoria |
|---|---|---|---|---|---|---|---|---|
| HU10 ridotto (66 %) | 573 / 7.140 / 605.088 | 0,136084 a | 0,003031 / 0,004200 a | **0,004200 a** (0,14 % del piatto, 0,042 % dello stack) | 0,007231 a | 0,001379 / 0,000905 a | 453 s (0,79 s per flop canonico) | 251 MB |
| HU10 completo (33/66/120 %) | 573 / 7.140 / 605.088 | 0,136090 a | 0,003118 / 0,004228 a | **0,004228 a** (0,14 % del piatto) | 0,007346 a | 0,001417 / 0,000909 a | 2.220 s (60 s prima di un'interruzione più 2.160 s ripresi da 16 flop; 3,9 s per flop canonico) | 324 MB |

Invarianti verificati sulle passate esatte: `EV_CO + EV_BTN = 0` entro `1·10⁻¹⁶`; guadagni
positivi; ogni combo compatibile con 5.984 flop fisici (controllo interno di `aggregate`);
`aggregate` in 0,6 s; la passata completa ripresa dallo stato ha riletto i 16 flop del chunk
precedente e completato i restanti 557.

**Confronto con lo stimatore campionato di P6** (stessi checkpoint): HU10 completo, stima naive a
60 flop 0,0454 ± 0,0186 a, a 1.000 flop 0,0187 ± 0,0079 a, limite inferiore 0,0014 a; valore
esatto 0,0042 a, dentro l'intervallo `[limite inferiore, naive + semiampiezza]` e sopra il
limite inferiore di 0,0028 a (la parte preflop della best response, invisibile allo stimatore
campionato). Il bias della naive a 1.000 flop è 0,0145 a, cioè `0,46/√1000`, come previsto dalla
legge misurata in P6. Stessa situazione sul ridotto (esatto 0,00420 a; naive a 160 flop
0,0367 ± 0,023 a, a 1.000 flop 0,0187 ± 0,0078 a). Il blueprint HU10 di P6 è quindi sfruttabile
per lo 0,14 % del piatto iniziale: sotto D3 (1 %) di un fattore 7 e sotto D2 (0,1 a) di un
fattore 24. La regola D3 con lo stimatore campionato è conservativa (richiede 1.000 flop e 80
min); il certificatore esatto costa 37 min sul completo e dà il numero vero.

I tre bet size aggiuntivi dell'albero completo cambiano il valore esatto di 0,00003 a: su HU10 il
gioco è deciso quasi interamente al preflop (stack 10 a, SPR postflop inferiore a 1).

Comando campionato del certificatore sulla stessa policy (`--sample-flops 60`, seme proprio,
63.360 board, 243 s): certificato `sampled = true` con stima naive 0,080 ± 0,052 a e limite
inferiore 0,0014 a: lo stesso ordine delle stime P6 a 60 flop, conferma che a `M` piccolo la
naive misura il rumore di selezione (valore esatto 0,0042 a).

## 5. Proiezione su CO40

> **Erratum (2026-09-16).** La passata parziale di questa sezione usava `--chunk 2`: il
> certificatore parallelizza sui flop di uno stesso chunk, quindi lavoravano 2 thread e non 8.
> Misura su HU10 completo con chunk 8: 13,3 s per flop a 1 thread, 7,3 a 2, 4,2 a 4, 3,2 a 8
> (diario 2026-09-16, decisione 47). Proiezione corretta a 8 thread con chunk 16:
> 64 s per flop canonico misurati su 8 flop con chunk 8 e 8 thread, passata esatta ≈ 10,2 h (era 22,4 h). Le passate esatte di §4 (chunk 16) non sono affette.

Passata parziale su CO40 (policy uniforme, tabelle 200/500/1.000, 8 thread, chunk 2): i primi 4
flop canonici del catalogo (40 flop fisici, 4.224 board) in 563 s, cioè 141 s per flop canonico
e 1,07 s per board per thread, memoria 281 MB. Proiezione della passata esatta su CO40:
573 × 141 s ≈ 80.800 s ≈ **22,4 h** con 8 thread (605.088 board), circa 36 volte HU10 completo.
Il costo per board è 12 volte la traversata P5 (90 ms) perché ogni board richiede 4 passate
river (2 giocatori × 2 modi) e, per ogni terminale all-in del flop e del turn raggiunto, un
kernel showdown sul board completo: su CO40 questi terminali sono decine di migliaia. Riduzioni
possibili, rinviate dopo P9 (§3.4): kernel all-in per (flop, turn) calcolato una volta per tutti i
river invece che per board, simmetrie di turn e river sotto lo stabilizzatore del flop (2–6 volte
in meno), valutazione in `float`. Il valore parziale (max gain 9,6 a con policy uniforme) non ha
significato: serve solo per il tempo.

## 6. Esito del gate

| Criterio (roadmap P7) | Esito |
|---|---|
| Test PASS (`ctest -L p7`, 373.068 asserzioni) | PASS |
| Passata esatta su HU10 completata con tempo riportato | PASS: HU10 ridotto 453 s, HU10 completo 2.220 s (37 min), certificati `exact = true`, exploitability 0,0042 a |
| Proiezione misurata del tempo su CO40 | PASS: 141 s per flop canonico misurati su 4 flop, 22,4 h per la passata esatta |
| Verifiche della roadmap: coincidenza con `calculate_nash_conv` (P6, `1e-9` sul gioco ridotto dell'oracolo), `gain_p ≥ −1e-12`, `EV_CO + EV_BTN = 0`, ripresa uguale al run continuo, stima campionata compatibile con il valore esatto | PASS (§3, §4) |

Il certificatore è lo strumento di certificazione del programma: la regola D3 di P6 con lo
stimatore campionato resta la regola di arresto del training, ma il numero dichiarato nei
certificati (P8, P9) è quello esatto. Su CO40 la passata esatta costa circa un giorno di
macchina a 8 thread: da programmare come lavoro notturno in P9 con lo stato su file (ripresa a
chunk), oppure da ridurre con le ottimizzazioni di §5 prima della qualificazione.

## 7. Fallimenti registrati

1. `Result<T, E>` (variant con costruttore di default) richiede `T` costruibile per default:
   `load_policy` restituisce `std::unique_ptr<BucketPolicy>`.
2. Test "EV a somma zero" applicato a un sottoinsieme di flop: l'invariante vale solo sul catalogo
   completo (§2); il test è stato limitato alla passata esatta.
3. Spostamento degli helper binari da `trainer.cpp` all'header condiviso lasciato a metà da uno
   script (una parentesi residua): corretto al primo build.

## 8. Comandi

```text
cmake --build out/build/windows-release --target gtosd_preflop_blueprint gtosd_preflop_blueprint_certifier_tests gtosd_preflop_blueprint_certify gtosd_preflop_blueprint_train
ctest --test-dir out/build/windows-release -L p7 --output-on-failure -V
gtosd_preflop_blueprint_train --config benchmarks/fixtures/preflop_blueprint_hu10_full_v1.json --resources-dir out/preflop_blueprint_resources --buckets-dir out/preflop_blueprint_buckets_200_500_1000 --eval-only --eval-flops 1 --resume --checkpoint out/ckpt_full_dcfr_200.bin --policy-out out/policy_full_dcfr_200.bin
gtosd_preflop_blueprint_certify --config benchmarks/fixtures/preflop_blueprint_hu10_full_v1.json --resources-dir out/preflop_blueprint_resources --buckets-dir out/preflop_blueprint_buckets_200_500_1000 --policy out/policy_full_dcfr_200.bin --threads 8 --chunk 16 --state out/p7_state_full.bin --output out/p7_cert_full.json
gtosd_preflop_blueprint_certify --config benchmarks/fixtures/preflop_blueprint_co40_v1.json --resources-dir out/preflop_blueprint_resources --buckets-dir out/preflop_blueprint_buckets_200_500_1000 --uniform --threads 8 --chunk 2 --flop-limit 4 --output out/p7_cert_co40_partial.json
gtosd_preflop_blueprint_certify ... --policy out/policy_full_dcfr_200.bin --threads 8 --sample-flops 60 --output out/p7_cert_full_sampled.json
```
