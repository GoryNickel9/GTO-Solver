# R4 — Betting compilato e memoria limitata

## Stato

`PASS` per il gate R4. Il percorso compilato produce gli stessi risultati del percorso di riferimento sullo stesso ordine di sampling, limita stato numerico, cache ed export e fallisce esplicitamente quando il budget non basta. Non effettua spill su disco né sostituisce la strategia sotto pressione di memoria.

## Implementazione

Il trainer compila prima del training 11.308 history postflop. Ogni nodo conserva stato meccanico, azioni legali e stati successivi. L'identità usa l'intera history delle azioni; pot e stack uguali non fondono history strategiche diverse. A runtime viene sostituito soltanto il `board_mask` della transizione prevalidata.

Regret e somme strategiche restano `double` e sono collocati in blocchi numerici indicizzati da un ID denso. La mappa `InformationKey -> ID` conserva history, giocatore, classe preflop o bucket e street; i blocchi da 1.024 stati mantengono stabili i riferimenti durante la ricorsione. Il payload dichiarato include chiave e stato numerico, mentre RSS/private bytes misurano anche hash map, allocator e slack.

I limiti introdotti sono:

- `maximum_numeric_state_bytes`, applicato cumulativamente a blueprint e due risposte apprese;
- `maximum_bucket_cache_entries`, diviso in modo deterministico fra giocatore e street;
- `maximum_exported_postflop_policy_payload_bytes`, già applicato prima dell'allocazione dell'export;
- cache FIFO: l'eviction elimina soltanto feature ricalcolabili, mai regret o somme strategiche.

La CLI espone i primi due budget e `--reference-betting` per il controllo A/B. L'output registra payload, budget, picco ed eviction della cache, nodi e payload minimo del piano compilato.

## Validazione

`gtosd_hu_preflop_compiled_tests`, `gtosd_hu_preflop_sampling_tests` e `gtosd_hu_preflop_trainer_isolation_tests` passano sul layout finale: 3/3 test in 0,47 s. Sono verificati:

- uguaglianza esatta di strategia root, EV, errore standard, risposte apprese e cardinalità degli infoset;
- transizioni compilate confrontate indirettamente con il percorso `legal_actions`/`apply_action` a seed fissato;
- `memory_failure` con 1.024 byte di budget numerico, senza fallback;
- rifiuto di un budget cache inferiore alle sei partizioni;
- eviction deterministica con cache minima e nessuna crescita oltre il limite.

## Benchmark CO40

Il protocollo è congelato in [BENCHMARK_PROTOCOL.md](BENCHMARK_PROTOCOL.md). Entrambi i run usano 100.000 iterazioni, external sampling v2, astrazione distribuzionale, 20.000 deal di valutazione, 5.000 iterazioni di risposta, 10.000 deal per risposta, budget numerico di 8 GiB e cache di 600 entry.

| Misura | Betting compilato | Riferimento |
|---|---:|---:|
| `solve_seconds` | 40,3126 s | 46,6358 s |
| Wall | 40,753 s | 47,105 s |
| Peak working set | 617.680.896 B | 590.573.568 B |
| Peak private bytes | 624.287.744 B | 595.546.112 B |
| Infoset | 2.788.136 | 2.788.136 |
| Payload numerico | 420.734.304 B | 420.734.304 B |
| Picco cache | 600 | 600 |
| Eviction cache | 858.482 | 858.482 |
| Nodi betting compilati | 11.308 | 0 |
| Payload minimo betting | 26.144.096 B | 0 B |

Il percorso compilato riduce `solve_seconds` del 13,56% e il wall del 13,48%. Il costo osservato è +27.107.328 B di working set e +28.741.632 B di private bytes, coerente con il piano e l'overhead dei contenitori. Strategia, EV root `0,320134`, errore standard, risposte apprese, infoset e contatori della cache coincidono esattamente.

La cache raggiunge 600 entry e registra 858.482 eviction: il run dimostra che, una volta piena, non continua a crescere. Il payload numerico resta sotto il budget; l'overhead dell'allocator non è presentato come parte del payload ed è coperto dalle misure di processo.

## Decisioni e limiti

Il betting compilato diventa il default del trainer dedicato perché preserva la semantica e riduce il tempo misurato. Il percorso di riferimento resta selezionabile per differential testing. Il limite numerico controlla il payload logico; non è un limite del working set imposto dal sistema operativo. La memoria reale resta limitata dalla cardinalità massima degli stati e delle cache, ma va sempre riportata separatamente.

Non sono stati modificati il solver postflop standalone, i suoi default o le fixture congelate.

## Prossima fase

R5 deve consolidare e persistere il mapping distribuzionale, misurare copertura e stati non addestrati e confrontare capacità 256/1.024/4.096 contro baseline e rappresentazione più fine.
