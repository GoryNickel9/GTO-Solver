# P1 — Canonicalizzazione e cataloghi

> **Stato al 2026-10-03.** Questo è il report del gate di P1, di settembre 2026, ed è
> congelato. Il modulo che descrive è ancora in produzione nel solver preflop blueprint.
> I riferimenti a HU10, CO40, P9 e alla best response astratta come lavoro o gate correnti
> sono però storici:
>
> - P9 è in [`archive/preflop-blueprint-research-2026-09/`](../../archive/preflop-blueprint-research-2026-09/README.md);
> - history7 e la suite HU10-HU40 sono stati tolti il 2026-10-03, e l'ultimo albero che li
>   contiene è al tag `history7-final`.
>
> Lo stato corrente è nel [diario](PROGRESS_LOG.md) e nella
> [ricetta di MonkerSolver](MONKER_RECIPE_REPRODUCTION_2026-09-28.md).

Data: 2026-09-15
Esito del gate: **PASS**
Roadmap: [PREFLOP_VECTOR_CFR_CODER_ROADMAP_2026-09-15.md](../../archive/preflop-blueprint-research-2026-09/PREFLOP_VECTOR_CFR_CODER_ROADMAP_2026-09-15.md), fase P1

## Identità

| Campo | Valore |
|---|---|
| Branch di fase | `feature/preflop-blueprint-p1-canonical` (da `feature/preflop-blueprint` a `11f94fb`) |
| Commit del codice P1 | `ca80dab` |
| Fingerprint del catalogo | `fnv1a64:51879f40626cb7dd` |
| Build | `out/build/windows-release` nel worktree, Release, `/W4 /permissive- /WX`, MSVC 19.51 |

## Consegne

1. **Indice combinatorio** (`combinatorics.hpp`): tabella dei binomiali `C(n,k)` per `n ≤ 36`,
   `k ≤ 7` calcolata a compile time; rango colex di un sottoinsieme strettamente crescente di
   1–7 carte e sua inversa; indice di coppia `combo_index` identico all'ordine di
   `gtosd::all_combos()`, quindi interscambiabile con `ComboId`.
2. **Canonicalizzazione dei semi** (`canonical_boards.hpp`): le 24 permutazioni in ordine
   lessicografico; per flop (3-insieme), flop+turn, board a cinque carte (5-insieme) e board
   history flop→turn→river il codice canonico è il minimo, sulle 24 permutazioni, del codice
   impacchettato a 6 bit per carta con il flop ordinato. Il risultato espone la permutazione
   (la più piccola che raggiunge il minimo) e la dimensione dell'orbita.
3. **Cataloghi con molteplicità** (`BoardCatalog`): costruiti per enumerazione esaustiva degli
   oggetti fisici, con riferimenti incrociati da ogni history alla classe del flop, del
   flop+turn e del board a cinque carte. Lookup in tempo logaritmico su vettori di codici
   ordinati; ogni lookup restituisce indice e permutazione fisico→canonico.
4. **Sampler deterministici**: `DeterministicRandom` (xoshiro256** seminato da splitmix64, draw
   limitati non distorti con il metodo di Lemire, indipendente da compilatore e libreria
   standard); campionamento fisico uniforme delle 7.539.840 history e campionamento canonico
   proporzionale alla molteplicità tramite cumulata; enumerazione ordinata delle history per la
   passata esatta.
5. **Persistenza** binaria versionata con checksum FNV-1a, scrittura atomica, rifiuto di file
   corrotti, troncati o con versione diversa, ricostruzione degli indici e verifica dell'ordine
   dei codici al caricamento.
6. **Eseguibile di report** `gtosd_preflop_blueprint_catalog`: conteggi, somme, tempi per
   catalogo, byte in memoria, fingerprint; opzioni `--output` e `--verify`.

## Conteggi

| Oggetto | Fisici | Canonici | Nota |
|---|---:|---:|---|
| Flop (3-insieme) | 7.140 | 573 | atteso 573, confermato |
| Flop + turn | 235.620 | **13.761** | prima noto solo il limite inferiore 9.818; ora fissato come costante attesa |
| Board a cinque carte (5-insieme) | 376.992 | 19.998 | atteso 19.998, confermato |
| Board history flop→turn→river | 7.539.840 | 369.072 | atteso 369.072, confermato |

Per ogni classe la molteplicità enumerata coincide con la dimensione dell'orbita calcolata sulle
24 permutazioni; le somme delle molteplicità riproducono i quattro conteggi fisici.

## Verifiche

| Controllo | Esito | Evidenza |
|---|---|---|
| Build Release dei target P1, warning come errori | PASS | nessun warning |
| `gtosd_card_abstraction_canonical_tests` | PASS | 4.062.607 asserzioni, 3,6 s |
| `gtosd_preflop_blueprint_catalog` | PASS | conteggi e somme attesi, 2,5 s |
| Regressione P0 (scaffold, dipendenze, schema) | PASS | 3/3 |

Contenuto del test:

- binomiali del mazzo (630, 7.140, 376.992, 8.347.680; 465, 406, 5.984, 201.376) e casi limite;
- round trip completo indice ↔ sottoinsieme per tutti i 2-, 3- e 5-insiemi e per un campione
  denso dei 7-insiemi; rifiuto di sottoinsiemi non ordinati, ripetuti o fuori mazzo;
- `combo_index` uguale all'indice di `all_combos()` in entrambi gli ordini degli argomenti, e
  `combo_from_index` inversa esatta sulle 630 combo;
- 24 permutazioni distinte, biiettive, con inversa corretta e rango preservato;
- per tutti i 7.140 flop e tutte le 24 permutazioni: codice canonico invariante, permutazione
  restituita che porta il flop sulle carte canoniche, dimensione dell'orbita uguale alla
  molteplicità, conteggio per classe uguale alla molteplicità, flop duplicato rifiutato;
- conteggi e somme dei quattro cataloghi; riferimenti incrociati di tutte le 369.072 history e
  di tutti i 13.761 flop+turn verificati con lookup indipendenti; orbita uguale a molteplicità
  per ogni board a cinque carte e ogni history;
- invarianza dell'indice di history sotto le 24 permutazioni su 5.000 history campionate, con
  la permutazione restituita che porta la history sulla rappresentante canonica;
- determinismo del generatore a seed uguale; 100.000 history fisiche campionate valide e con
  flop ordinato; mappa quantile→indice corretta agli estremi delle classi; 2.000.000 di
  estrazioni canoniche con frequenza per classe di flop entro sei sigma dalla molteplicità;
  draw limitati sempre sotto il limite;
- salvataggio, ricaricamento con uguaglianza completa e stesso fingerprint, lookup dal catalogo
  caricato, rifiuto di file corrotto (checksum) e di file mancante.

## Tempi e memoria

| Catalogo | Costruzione |
|---|---:|
| Flop | 0,0024 s |
| Flop + turn | 0,064 s |
| Board a cinque carte | 0,139 s |
| Board history | 2,03 s |
| Riferimenti incrociati | 0,32 s |
| Totale | 2,56 s |

Il catalogo in memoria occupa 13.970.940 B (record più vettori di codici e cumulata). La
costruzione è abbastanza rapida da non richiedere la distribuzione del file: la persistenza
serve a identificare la risorsa con checksum e fingerprint, non a risparmiare tempo.

## Contratti fissati per le fasi successive

- La permutazione restituita da `lookup_*` porta le carte fisiche nel frame canonico; le mani
  private vanno permutate con la stessa permutazione prima di consultare una tabella indicizzata
  per board canonico. Le tre street usano tre frame distinti (flop, flop+turn, board a cinque
  carte), coerenti con la memoria imperfetta della chiave postflop.
- Le history canoniche sono ordinate per codice: l'ordine del catalogo è l'ordine della passata
  esatta, con ripresa da un offset.
- Il campionamento fisico uniforme e quello canonico proporzionale alla molteplicità hanno la
  stessa distribuzione sulle classi.

## Decisioni prese dall'agent

| Decisione | Motivazione |
|---|---|
| PRNG proprio (xoshiro256** + Lemire) invece di `std::mt19937_64` con `std::uniform_int_distribution` | le distribuzioni standard sono implementation-defined: un seed non identificherebbe gli stessi board su compilatori diversi e i test di bit-identità perderebbero significato |
| Canonicalizzazione per minimo su 24 permutazioni di un codice a 6 bit per carta | semplice, verificabile con la dimensione dell'orbita; la costruzione dei cataloghi richiede 2,6 s |
| Conteggio dei flop+turn fissato a 13.761 dopo la misura | rende il caricamento del catalogo fail-closed su tutti e quattro i conteggi |
| Persistenza con checksum ma nessuna distribuzione del file | ricostruzione in 2,6 s; il file serve come identità verificabile |

## Limiti

- La canonicalizzazione è a forza bruta (24 immagini per oggetto); sufficiente per i cataloghi e
  per i lookup a runtime, misurato nel report.
- Il formato di persistenza è la versione 1 e conserva solo codici, molteplicità e riferimenti;
  le carte vengono ricostruite dai codici.
- `format-check` non eseguibile (clang-format assente).

## Comandi riproducibili

```text
cmake --build out\build\windows-release --target gtosd_card_abstraction gtosd_card_abstraction_canonical_tests gtosd_preflop_blueprint_catalog
ctest --test-dir out\build\windows-release -L p1 --output-on-failure -V
out\build\windows-release\benchmarks\gtosd_preflop_blueprint_catalog.exe --output out\board_catalog_v1.bin --verify out\board_catalog_v1.bin
```
