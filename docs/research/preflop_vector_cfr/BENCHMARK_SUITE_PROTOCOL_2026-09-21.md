# Protocollo comune della suite di benchmark e criteri di accettazione

Data: 2026-09-21. Documento del milestone "riduzione RAM e tempi su tutta la suite di
benchmark", parti 2-5 e 7: configurazione comune, controllo automatico dell'uniformita',
misure di memoria e di tempo, tolleranze e criteri di accettazione fissati **prima** dei run
di confronto. Il censimento e' in
[BENCHMARK_SUITE_INVENTORY_2026-09-21.md](BENCHMARK_SUITE_INVENTORY_2026-09-21.md).

## 1. File del protocollo

| File | Contenuto |
|---|---|
| `benchmarks/suite/preflop_blueprint_suite.json` | profilo comune (regole, catalogo delle size, astrazione con SHA-256 degli artefatti, protocollo del trainer e del certificatore, build, hardware, politica di misura), elenco degli scenari con i soli tre parametri, elenco delle versioni con i loro argomenti |
| `benchmarks/suite/fixtures/<scenario>.json` | fixture generate dal resolver; le uniche differenze fra due file sono `id`, `effective_stack_units`, `open_target_units`, `response_target_units`, `limp_response_target_units`, `postflop_sizes_basis_points`, tutte funzioni dei tre parametri |
| `benchmarks/suite/resolved.json` | per scenario: fingerprint dell'albero, conteggi dei nodi, layout `history7` (celle e righe per street, byte delle tabelle) e flag di fattibilita' rispetto alla RAM e al budget |
| `tools/preflop_suite/suite.py` | `resolve`, `check`, `run`, `report` |
| `out/suite/<versione>/<scenario>/rep<N>/` | log JSONL del trainer e del certificatore, certificato, campioni di memoria, manifest con hash di eseguibili, fixture e artefatti |

## 2. Configurazione comune

Tutto cio' che non e' uno dei tre parametri e' fissato nel profilo comune e verificato dal
controllo di uniformita':

- regole: Short Deck HU CO/BTN, ante 1a, button blind 1a, rake disabilitato, contratto
  monetario revisione 2, `allow_configured_incomplete_raise`, `include_all_in`,
  `raise_termination natural_stack`, `postflop_minimum_bet` 1a;
- astrazione: risorse `out/preflop_blueprint_resources` (rank table, all-in table, catalogo),
  tabelle bucket 200/500/1.000 (`fnv1a64:33f06cf437f8f26d` / `51814338fcf1236c` /
  `2e59aa76f59c0fcd`), mappa `history7` (`fnv1a64:3c9ee76ca6aad23b`); SHA-256 di ogni file nel
  profilo e nel manifest di ogni run;
- trainer: 16.000 iterazioni, batch 32 (64 board per iterazione con update alternati), 8
  thread, partizione 64, DCFR 1,5/0/2, update alternato, discount lazy v2 ibrido, refresh delle
  sole righe del batch, seed di training e di valutazione di default, nessuna valutazione
  campionata (`--eval-every 0`), progresso ogni 500 iterazioni, checkpoint e policy media
  scritti alla fine;
- certificatore: BR fisica esatta su 573 flop canonici e 605.088 board, 8 thread, chunk 16,
  soglia 1 % del piatto (0,03 a), file di stato scritto;
- build: MSVC 19.51 x64, Release `/O2 /Ob2 /DNDEBUG`, `/W4 /permissive- /WX`, Ninja, nessun
  `/arch`, stesso `vcpkg_installed`;
- hardware: i3-10100F (4 core / 8 thread), 32 GiB, Windows 11; nessun altro run del solver
  concorrente (il driver rifiuta di partire se un trainer o un certificatore e' in esecuzione);
  il worker del viewer, se presente, e' registrato nel manifest.

## 3. Catalogo delle size

Catalogo preflop (target di impegno vivo, index-matched): `[5a open, 17a response]`.
Catalogo postflop (basis points del piatto dopo il call): `{1: [100 %], 3: [33 %, 66 %, 120 %]}`.

| Parametro | Regola deterministica |
|---|---|
| `preflop_sizes = 1` | open 5a; sopra l'open fold / call / all-in |
| `preflop_sizes = 2` | open 5a; risposta 17a; sopra la risposta fold / call / all-in; nel piatto limpato re-raise solo all-in (`limp_response_target_units: []`) |
| cap allo stack (preflop) | un target `>= effective stack` viene rimosso dal resolver (nel motore coinciderebbe con l'all-in e il loader rifiuta la fixture): a 10 ante il target 17a sparisce e `preflop_sizes = 2` produce la stessa fixture di `1` |
| cap allo stack (postflop) | una size la cui puntata totale raggiunge lo stack diventa l'azione all-in (`legal_actions`: `rounded_total >= stack -> AllIn`) e viene deduplicata |
| minimum raise | un target o una size sotto l'ultimo incremento pieno e' scartato; i target preflop configurati sono ammessi anche se incompleti (`allow_configured_incomplete_raise = true`); una bet sotto `postflop_minimum_bet` e' scartata |
| all-in | mai contato come size: aggiunto sempre (`AllInMode::Add`, soglia 1000 % del piatto dopo il call) finche' lo stack residuo sopra il call e' `<= 10` volte il piatto dopo il call; alla radice il piatto dopo il call e' 4a, quindi lo shove preflop esiste fino a 41 ante; tutti gli stack della suite rientrano nella finestra |
| duplicati | `add_unique_aggressive` produce un solo arco per importo |
| `postflop_sizes = 2` | non definito: il resolver rifiuta lo scenario |

Azioni effettivamente legali per nodo: l'elenco completo dei nodi preflop con etichette e
importi e l'istogramma delle firme delle azioni postflop per street sono prodotti da
`gtosd_preflop_blueprint_game --config <fixture> --actions` e riportati nel rapporto finale
per ogni scenario (sezione "Azioni legali").

Le due righe del catalogo postflop non sono annidate (100 % contro 33/66/120 %): entrambe sono
decisioni dell'utente (D24 e decisione 44) e non vengono modificate da questo milestone. Lo
storico "HU10 ridotto" con il 66 % e' fuori catalogo e resta solo come storia.

### 3.1 Azioni legali risultanti per scenario

Liste complete (nodo per nodo, importi e flag all-in) in `benchmarks/suite/actions/<scenario>.json`,
generate con `gtosd_preflop_blueprint_game --actions`; riepilogo:

| Scenario | Albero (fnv1a64) | Nodi | Decisioni | Decisioni preflop | Archi all-in preflop | Etichette preflop | Decisioni flop/turn/river | Firme postflop |
|---|---|---:|---:|---:|---:|---|---|---:|
| `HU10` | `d7b31d6f2cb759fc` | 193 | 80 | 8 | 4 | fold, call, check, all_in, bet_4, raise_5 | 16/24/32 | 12 |
| `HU10-FULL` | `bc9e7b35ad8c021d` | 1501 | 584 | 8 | 4 | fold, call, check, all_in, bet_4, raise_5 | 48/160/368 | 29 |
| `HU20` | `f5b432de223744cc` | 571 | 228 | 8 | 4 | fold, call, check, all_in, bet_4, raise_5 | 28/68/124 | 17 |
| `HU20-2` | `7b59c6cacc9f5da5` | 604 | 242 | 10 | 5 | fold, call, check, all_in, bet_4, raise_16, raise_5 | 32/72/128 | 18 |
| `HU30` | `17dc5c7d07ea30c2` | 604 | 242 | 10 | 5 | fold, call, check, all_in, bet_4, raise_16, raise_5 | 32/72/128 | 18 |
| `HU40` | `18d08f453034ac0f` | 604 | 242 | 10 | 5 | fold, call, check, all_in, bet_4, raise_16, raise_5 | 32/72/128 | 18 |
| `HU40-FULL` | `d6c10723d35b9503` | 26878 | 9958 | 10 | 5 | fold, call, check, all_in, bet_4, raise_16, raise_5 | 300/1988/7660 | 130 |

Lettura: con due size preflop (`raise_5` = 5a e `raise_16` = 17a, etichetta dal rilancio in ante
sopra la posta) lo stack di 30a e 40a produce lo stesso albero a 604 nodi con importi diversi;
`HU20-2` (20a con la 3-bet a 17a) ha la stessa struttura. La size 17a e' esclusa a 10a dal cap
dello stack (regola di catalogo), a 20a per decisione dell'utente (fixture storica). Le size
postflop 33/66/120 % generano etichette `bet_1_32`/`bet_2_64`/`bet_4_8` (frazioni di piatto in
ante) e l'all-in viene aggiunto dove il push vale al massimo 10 volte il piatto dopo la chiamata;
i duplicati vengono deduplicati per importo e le size che superano lo stack diventano all-in.

## 4. Controllo automatico dell'uniformita'

`python tools/preflop_suite/suite.py check [--runs-root out/suite/<versione>]` fallisce se:

1. una fixture generata non coincide con la derivazione dal catalogo;
2. due fixture differiscono in un campo diverso dai sei campi derivati dai tre parametri;
3. un artefatto condiviso (risorse, tabelle bucket, mappa) ha un SHA-256 diverso dal profilo;
4. nella cartella dei run di una versione compaiono eseguibili con SHA-256 diversi;
5. gli eventi `start` del trainer differiscono fra scenari in un campo che non dipende
   dall'albero (batch, thread, thread di valutazione, schema, discount lazy, update, capacita',
   storage delle tabelle, modalita' automatica, soglia);
6. gli eventi `start` del certificatore differiscono in thread, chunk, `flop_limit`,
   `sample_flops`;
7. gli argomenti di protocollo o di versione differiscono fra scenari.

Il fingerprint dell'albero, il numero di nodi, l'identita' del trainer, i byte di stato e la
partizione sono esclusi perche' derivano legittimamente da stack e size; sono esclusi anche i
campi di misura scritti negli eventi `start` dalla build strumentata (`preparation_seconds`,
`memory_breakdown`, `history_map_resident_bytes`, `process_after_load`), che descrivono il run e
non la configurazione. Ogni manifest registra
inoltre HEAD e stato del working tree di git, hostname, RAM, memoria disponibile prima e dopo,
altri processi `gtosd_*` presenti. I manifest nascono con `status: RUNNING`; un run non
`COMPLETE` (in corso, `TRAIN_FAILED`, `CERTIFY_FAILED`) viene elencato come "not compared
(incomplete run)" e non entra nel confronto, cosi' un run incompleto non puo' risultare
uniforme per assenza di dati.

## 5. Misura della memoria

Per ogni processo (trainer, certificatore) il driver campiona ogni 0,5 s `WorkingSetSize`,
`PeakWorkingSetSize`, `PagefileUsage` (commit privato), `PeakPagefileUsage`, page fault,
tempi CPU, memoria disponibile del sistema e uso del file di paging; al termine legge i contatori
esatti dall'handle del processo uscito (`GetProcessMemoryInfo`), che restituisce i picchi
definitivi. Il picco riportato e' il massimo fra campione e contatore finale. Private commit e
working set non vengono mai sommati. Il picco end-to-end di uno scenario e' il massimo fra i
picchi del trainer e del certificatore (processi sequenziali).

Conteggio interno (evento `memory_breakdown` del trainer, dopo l'inizializzazione e dopo la
prima iterazione): byte riservati (capacita' dei contenitori) di regret, somme della strategia
media, policy (tabella densa nella baseline, capacita' della policy compatta nelle candidate),
timestamp del discount, fattori e offset del discount, matrici all-in dense, buffer del batch di
board, workspace per thread, unita' della partizione, offset del layout, lista dei board,
maschere delle mani, albero (nodi, archi, stati pubblici), mappa history residente, tabelle
bucket, rank table, catalogo, tabella all-in; celle e righe per street. Il certificatore riporta
i byte della policy caricata, la mappa residente e i picchi di processo. La differenza fra il
picco di commit e il totale conteggiato e' riportata come "non attribuito" (allocatore, stack,
DLL, buffer del sistema).

Pressione di memoria: il minimo di memoria disponibile e il massimo di file di paging usato
durante il run sono nel manifest (`system_min_available_bytes`, `system_max_swap_used_bytes`),
insieme ai page fault del processo; i tempi di un run con memoria disponibile sotto 1 GiB o con
crescita del file di paging sono marcati come non interpretabili.

## 6. Misura dei tempi

Tempo reale del driver: durata di ogni processo dal lancio all'uscita
(`train_wall_seconds`, `certify_wall_seconds`, somma = `end_to_end_wall_seconds`); secondi CPU
cumulati dei thread da `GetProcessTimes` (`process_times.cpu_seconds`), riportati a parte.
Indicatori di contesa (aggiunti il 2026-09-22 dopo i run diurni di B e C): `cores_used_mean` =
secondi CPU / tempo reale del processo (circa 6,5 per il trainer da solo su 4c/8t; valori
inferiori a parita' di versione indicano CPU sottratta da altri processi) e
`system_cpu_percent_mean`, media del carico CPU di sistema campionato ogni 0,5 s durante il run
(colonna `system_cpu_percent` in `samples_*.csv`; assente nei run precedenti alla modifica). Un
run con `cores_used_mean` inferiore al 90 % del miglior valore fra le ripetizioni della stessa
versione e dello stesso scenario e' contaminato: il report lo marca con `*` nella tabella delle
ripetizioni, lo esclude dalle mediane dei tempi (colonna "Rip. (pulite)" = ripetizioni complete
e, fra parentesi, quelle usate per i tempi) e lo tiene per i picchi di memoria e la qualita', che
non dipendono dalla contesa. Il criterio non distingue la CPU sottratta da altri processi dalla
paginazione: la colonna della CPU di sistema media e i page fault permettono di attribuire la
causa run per run.

**Finestra di misura (dal 2026-09-24, aggiornata lo stesso giorno).** I run misurati partono
solo fra le 00:00 e le 18:00 locali di ogni giorno, la fascia in cui la macchina non e' usata
(indicazione dell'utente): la coda (`run_queue.sh`, variabile `SUITE_WINDOW`, default
`00:00-18:00`) avvia un run solo se l'ora corrente e' nella finestra e la durata attesa dello
scenario (40 min per HU10, 100 min per gli altri) rientra nella finestra; altrimenti attende.
La prima notte (01:00-09:00) ha usato la finestra precedente. Dal 2026-09-24 sera la fascia
00:00-18:00 e' l'unico periodo in cui la macchina puo' essere caricata da qualsiasi attivita' del
milestone, misurata o no: i run misurati usano 00:00-17:00 (`SUITE_WINDOW=00:00-17:00`), l'ultima
ora 17:00-18:00 e' riservata a build, test unitari e sonde diagnostiche (che attendono comunque la
fine di ogni processo del solver); dopo le 18:00 la macchina e' dell'utente e nessun processo del
milestone deve girare. Build, test unitari e sonde diagnostiche non sono misure di tempo e possono girare di
giorno, mai in concorrenza con un run misurato (il driver rifiuta di partire se trova processi
del solver). `SUITE_WINDOW=off` disattiva la finestra per macchine dedicate.
Dal 2026-09-26 (decisione dell'utente del 25 settembre) la fascia disponibile e' 00:00-21:00: i
run misurati usano 00:00-20:00 (`SUITE_WINDOW=00:00-20:00`), l'ultima ora 20:00-21:00 e'
riservata a build, test unitari e sonde diagnostiche (che attendono comunque la fine di ogni
processo del solver) e dopo le 21:00 nessun processo del milestone gira. Il runner accetta anche
una pausa giornaliera facoltativa (`SUITE_PAUSE=HH:MM-HH:MM`: un run parte solo se la sua durata
attesa termina prima della pausa o se la pausa e' finita) e `SUITE_NOT_BEFORE="YYYY-MM-DD HH:MM"`,
che rinvia il primo avvio a una data e ora (regole che entrano in vigore un giorno successivo).

**Criterio di accettazione (decisione dell'utente del 2026-09-28, sostituisce la decisione (3) del
2026-09-24).** Un benchmark e' accettato quando la best response esatta dentro l'astrazione (tutti i
573 flop canonici, il giocatore che devia vede solo le righe dell'astrazione) e' al massimo l'1 % del
piatto iniziale, cioe' 0,03 a, per entrambi i giocatori. Il certificato fisico esatto (best response
sul gioco vero, strumento `certify`) e i controlli puntuali con il solver postflop esatto restano
metriche di qualita' riportate a ogni versione: la loro differenza con la best response astratta e'
l'errore di rappresentazione dell'astrazione. Precisazione dell'utente dello stesso giorno: il
certificato fisico non e' piu' un obiettivo, ma resta un limite superiore di accettazione al 5 % del
piatto iniziale (0,15 a). Condizioni di accettazione: best response astratta <= 0,03 a e certificato
fisico <= 0,15 a. Stato a 32.000 iterazioni: HU30 0,0349 / 0,1676, HU40 0,0640 / 0,2526. Motivo: i solver preflop di riferimento
dichiarano la convergenza dentro la propria astrazione; le diagnosi del 2026-09-27/28 mostrano che
su HU30/HU40 il 75-80 % dell'exploitability fisica e' errore di rappresentazione. Valori di partenza a
32.000 iterazioni: HU30 0,0349 (policy del 2026-09-21), HU40 0,0640.

**Build e test (decisione dell'utente del 2026-09-27).** Build, test unitari e sonde diagnostiche
sono ammessi in qualsiasi momento fra le 00:00 e le 21:00, purche' non sia in corso un run di cui si
misura il tempo (ripetizioni di confronto dei tempi, run sui 35 minuti). Durante i run di sola
qualita' (max gain certificato, deterministico: per esempio la convergenza di HU30/HU40) una build
in parallelo rallenta il run ma non cambia il risultato, e resta ammessa. Dopo le 21:00 nessun
processo del milestone gira.

**Obiettivo di tempo (decisione dell'utente del 2026-09-25).** Ogni benchmark della suite
(HU10, HU20, HU30, HU40) deve chiudere in meno di 35 minuti end-to-end (training, scrittura e
certificazione fisica esatta con max gain <= 1 % del piatto) sotto il tetto di memoria di A. Lo
stato di partenza (F, 2026-09-25): HU10 20 minuti certificato; HU20 47,5 minuti misurati con
macchina contaminata (43-44 stimati puliti) certificato; HU30 e HU40 53-54 minuti a 16.000
iterazioni senza certificato. La metrica resta il tempo end-to-end per certificare l'1 %.

Scomposizione interna (eventi JSONL, senza doppi conteggi):

| Voce | Origine |
|---|---|
| 1. inizializzazione e caricamento | `preparation_seconds` dell'evento `start` del trainer |
| 2. allocazione delle tabelle | inclusa in 1 (le tabelle sono allocate alla creazione del trainer); riportata separatamente solo dalla scomposizione della memoria |
| 3. discount | `discount_seconds` (eager: scansione densa; lazy: solo preparazione dei fattori, il catch-up per riga e' dentro il refresh) |
| 4. refresh / materializzazione della policy | `policy_refresh_seconds` (materializzazione lazy delle righe del batch + regret matching + nelle candidate mappatura degli slot) |
| 5. preparazione board e matrici all-in | `board_prepare_seconds` |
| 6. traversata CFR | `traversal_seconds` |
| 7. riduzione dei risultati | inclusa in 6 (la riduzione delle unita' e' la fase `top_reduce` della traversata, separabile solo con `--profile-traversal`) |
| 8. training complessivo | `training_seconds` = somma di 3 + 4 + 5 + 6 (piu' l'overhead di telemetria); non va sommato di nuovo alle sottofasi |
| 9. materializzazione, checksum e scrittura | `write_seconds` (candidate) oppure `total_seconds - preparation_seconds - training_seconds - evaluation_seconds - certification_seconds` (baseline) |
| 10. preparazione certificatore | `preparation_seconds` dell'evento `start` del certificatore |
| 11. BR fisica esatta | `seconds` del certificato (valutazione + aggregazione) |
| 12. totale end-to-end | interno: `total_seconds` del trainer + 10 + 11 (confrontabile con il riferimento 4.494,095 s); driver: somma delle durate dei processi |

Tempo per iterazione = `training_seconds / iterazioni`; tempo per unita' di lavoro =
`training_seconds / board elaborati` (64 per iterazione con update alternati).

Confronti: processi nuovi per ogni fase, nessun warm-up, cache del file system del sistema
operativo non svuotata (le risorse da 400 MB sono lette da tutte le versioni allo stesso modo).
Per i confronti conclusivi almeno tre ripetizioni per scenario e versione; si riportano la
mediana dei tempi, il minimo e il massimo, e il massimo picco di memoria osservato.

## 7. Tolleranze e criteri di accettazione (preregistrati)

### 7.1 Candidata A (policy compatta, all-in senza copia, export in streaming, storage double)

Criterio: **identita' bit per bit** con la baseline. Misure:

1. fingerprint dello stato del trainer uguale dopo 40 iterazioni su HU10 (riferimento
   `fnv1a64:cad91343de7ab2d8`, policy `fnv1a64:c85e99914c747ed6`, run di prova
   `out/suite/smoke/HU10/rep1`) e dopo 100 iterazioni su HU20 con la baseline;
2. fingerprint della policy media uguale a quello della baseline sui run completi a 16.000
   iterazioni di ogni scenario, quindi certificato identico (stesso `max_gain`, stessa
   NashConv, stessi EV);
3. file di policy uguale byte per byte al file scritto dall'export denso (test
   `preflop_blueprint_trainer_tests`, `test_batch_policy_refresh`);
4. determinismo rispetto ai thread (1/2/4/8) e alla partizione, ripresa da checkpoint
   bit-identica, oracolo `FiniteGame` entro `1e-9`, eager/lazy invariati (suite di test).

Qualsiasi differenza rende A non accettabile come "stesso algoritmo".

### 7.2 Candidate B (somme float32) e C (regret e somme float32)

Le candidate narrow non sono bit-identiche: si separano errore di storage ed errore delle
riduzioni. Gli incrementi sono calcolati in double e arrotondati una sola volta al momento
della scrittura nella cella; le riduzioni (somme sui figli, regret matching, media) restano in
double. Quindi l'errore di riduzione e' nullo per costruzione e l'errore misurato e' di storage.

Misure e tolleranze:

1. validita' delle strategie: ogni riga della policy media e corrente somma a 1 entro `1e-12`
   (il calcolo della riga e' in double), nessun NaN o infinito, nessun valore negativo nelle
   somme (verificato al caricamento del file di policy e dal checkpoint);
2. errore di storage per cella dopo 12 iterazioni sul gioco HU10 ridotto dei test: massimo
   errore relativo per cella `< 1e-2` (test `test_table_storage`; il valore osservato viene
   riportato); sui run completi si riporta la distanza `L_inf` e la variazione totale media
   della policy media rispetto alla baseline per street;
3. EV delle azioni preflop (certificato, `ev` di radice): differenza assoluta `<= 1e-4` a;
4. certificato: `max_gain` della candidata `<= max_gain` della baseline `+ 0,0003 a` (1 % della
   soglia di 0,03 a) e stesso esito `passes_target` su ogni scenario; NashConv entro `0,0003 a`;
5. tempo e memoria: la candidata non deve essere piu' lenta della baseline oltre la
   variabilita' delle ripetizioni (3 %) su nessuno scenario e non deve superare il picco di
   commit della baseline su nessuno scenario.

Una candidata che migliora HU20 e peggiora un altro scenario (tempo, memoria o certificato)
non viene promossa; il compromesso viene riportato scenario per scenario.

**Emendamento del 2026-09-22 (dopo il primo test, dichiarato come tale).** Il criterio "errore
relativo per cella < 1e-2 dopo 12 iterazioni" e' stato applicato cosi' com'era: B (somme
float32) lo supera (regret identici, errore massimo sulle somme 5,0e-6 relativo, 2,8e-7
assoluto); C (regret e somme float32) **non lo supera**: dopo 1 iterazione l'errore massimo e'
4,6e-4 (con soglia assoluta 1e-6 volte la scala della tabella; e' l'arrotondamento float32 con
cancellazione, cioe' solo storage), ma dalla seconda iterazione la discontinuita' del regret
matching in zero amplifica le differenze (389 relativo, 1.838 celle oltre l'1 % alla seconda
iterazione; 48.483 celle su 102.901 oltre l'1 % alla dodicesima, differenza assoluta massima
0,0453 su scala 0,121): la traiettoria di C e' un altro run CFR dello stesso gioco, non lo
stesso run con celle arrotondate. Il criterio per cella sulla traiettoria non e' soddisfacibile
da nessuna implementazione con regret float32 e non misura lo storage; il test lo applica
percio' dopo la prima iterazione (dove misura solo lo storage) e riporta, senza asserirli, i
valori a 12 iterazioni. La candidata C resta marcata "criterio per cella preregistrato non
superato" e viene valutata solo sui criteri di esito (EV, max gain, certificato, NashConv) con
le stesse soglie di B; la candidata B, che non cambia la policy corrente (i regret restano
`double`), e' quella con la garanzia numerica piu' forte fra le narrow.

**Decisioni dell'utente del 2026-09-24 (sera).** (1) Qualunque sia l'architettura, il picco di
memoria di ogni benchmark non deve superare il livello gia' raggiunto dalla candidata A: HU10 2,11
GiB, HU20 7,94 GiB, HU30 e HU40 8,18 GiB (private commit, trainer e certificatore); un'astrazione
piu' fine deve stare in questo tetto (storage narrow, fase 2). (2) L'algoritmo puo' cambiare e le
16.000 iterazioni non sono un vincolo: gli unici vincoli sono tempo e memoria. Conseguenza per la
valutazione: il protocollo a lavoro fisso resta come misura di confronto con i risultati storici,
ma il criterio principale diventa il **tempo end-to-end per raggiungere la qualita' di
riferimento** sotto il tetto di memoria. (3) La qualita' di riferimento e' una sola per tutti i
benchmark: il certificato esatto della BR fisica sotto l'1 % del piatto, anche per HU30 e HU40 che
oggi non lo raggiungono con nessuna versione. Finche' un benchmark non e' certificato, per esso si
riportano il miglior max gain fisico raggiunto e il tempo speso, mai un "superato"; una candidata
(di codice, di astrazione o di algoritmo) e' promossa solo se certifica tutti e quattro i benchmark
sotto l'1 % in meno tempo della versione corrente senza superare il tetto di memoria; le
ripetizioni e gli indicatori di contesa restano quelli della sezione 6. Il completamento del
programma coincide quindi con la qualificazione di HU30 e HU40 (gate P9).

### 7.3 Due valutazioni distinte

- **Prestazioni a lavoro fissato**: stesso protocollo, 16.000 iterazioni, 1.024.000 board;
  tabella per scenario e versione con picchi di commit e working set, training, BR esatta,
  totale interno e totale dei processi.
- **Qualita' ottenuta**: `max_gain`, NashConv, EV di radice e fingerprint della policy per
  scenario e versione, con la differenza rispetto alla baseline.

Uno scenario che non completa (memoria insufficiente, timeout, errore) viene riportato con
l'esito `RESOURCE_LIMIT` / `TIMEOUT` / `FAILED` e non entra nelle medie.

## 8. Procedura di riproduzione

```bash
python tools/preflop_suite/suite.py resolve --game-exe <build>/benchmarks/gtosd_preflop_blueprint_game.exe
python tools/preflop_suite/suite.py check
python tools/preflop_suite/suite.py run --version <versione> --exe-dir <build>/benchmarks --scenario HU20 --rep 1
python tools/preflop_suite/suite.py check --runs-root out/suite/<versione>
python tools/preflop_suite/suite.py report --baseline baseline-ba93c75
```

Le versioni e i loro argomenti sono in `versions` del profilo; una build di versione va
archiviata in `out/suite/bin/<versione>/` e il suo SHA-256 compare nel manifest di ogni run.

Sequenza completa usata per il milestone (Git Bash, MSVC 19.51 x64 Release tramite il wrapper
`VsDevCmd -arch=x64`, generatore Ninja, vcpkg del checkout `windows-release-main-integration`):

```bash
# 1. build della versione da misurare (baseline: HEAD pulito; candidate: working tree) e archivio
cmake --build out/build/windows-release-suite --target gtosd_preflop_blueprint_train   gtosd_preflop_blueprint_certify gtosd_preflop_blueprint_game -- -j 8
mkdir -p out/suite/bin/<versione> && cp out/build/windows-release-suite/benchmarks/gtosd_preflop_blueprint_{train,certify,abstract_br,game,history_rows,export}.exe out/suite/bin/<versione>/
# 2. test (sei eseguibili) e liste delle azioni legali
out/build/windows-release-suite/tests/gtosd_preflop_blueprint_trainer_tests.exe --resources-dir out/preflop_blueprint_resources --buckets-dir out/preflop_blueprint_buckets_200_500_1000
out/build/windows-release-suite/benchmarks/gtosd_preflop_blueprint_game.exe --config benchmarks/suite/fixtures/HU20.json --actions
# 3. sonde di identita' bit per bit (40 iterazioni HU10, 100 iterazioni HU20) contro la baseline
python tools/preflop_suite/suite.py run --version probe-<v> --exe-dir out/suite/bin/<versione> --scenario HU10 --rep 1 --iterations 40 --overwrite
# 4. coda sequenziale (una riga per run: versione scenario ripetizione cartella-eseguibili)
bash tools/preflop_suite/run_queue.sh out/suite/queue.txt
# 5. controllo di uniformita' per versione e report
python tools/preflop_suite/suite.py check --runs-root out/suite/<versione>
python tools/preflop_suite/suite.py report --baseline baseline-ba93c75 --markdown-out out/suite/report.md --json-out out/suite/report.json
```

Regole operative: mai compilare o eseguire altro lavoro CPU mentre un run misurato e' attivo
(il driver rifiuta di partire se trova processi `gtosd_preflop_blueprint_train/certify`);
le sonde non sono misure di tempo; i file di policy e checkpoint vengono cancellati dopo
l'hash salvo `--keep-policy` / `--keep-checkpoint`.
