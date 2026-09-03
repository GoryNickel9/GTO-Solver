# Piano di correzione della metrica memoria GTO+

**Data:** 2026-09-03; revisione documentale 2026-09-04
**Stato:** errata corrige accettata e piano operativo; nessuna modifica di produzione autorizzata da questo documento
**Ambito:** benchmark GTO+ AHKHQH, TH7D6S e TSTC9D; schema fixture/report;
accounting della memoria del solver; runner e documentazione correlati

## 1. Correzione di autorità

Il requisito indipendente di Peak RSS desktop `< 2 GiB` **non è mai esistito**.

I valori:

- AHKHQH: `8 MB`;
- TH7D6S: `399 MB`;
- TSTC9D: `2.000 MB`;

provengono esclusivamente dal campo GTO+ **“Memory needed for solving”**. Sono
stati successivamente normalizzati nelle fixture come `8.000.000 B`,
`399.000.000 B` e `2.000.000.000 B`, ma sono stati classificati erroneamente
come `peak_rss_bytes`.

Conseguenze immediate:

1. non esiste un secondo cap RSS a `2 GiB` o `2.147.483.648 B`;
2. `2.000.000.000 B` non è un cap generale del desktop: è il riferimento
   specifico mostrato da GTO+ per TSTC9D;
3. i riferimenti 8/399/2.000 MB non possono essere confrontati con il Peak RSS
   del processo GTOSD;
4. i PASS/FAIL ottenuti da quel confronto sono semanticamente invalidi;
5. Peak RSS resta una telemetria utile, ma senza soglia normativa finché un
   requisito autonomo non sarà esplicitamente introdotto;
6. nessuna tecnica di trimming, paging o rilascio del working set può essere
   usata per dichiarare parità con “Memory needed for solving”.

Questa correzione sostituisce ogni documento corrente che descrive un cap
desktop indipendente `< 2 GiB`. I report storici non saranno riscritti nei dati:
saranno marcati come storici, collegati a questa errata corrige e, quando il
falso contratto costituisce il loro oggetto principale, spostati nell'archivio
`docs/archive/legacy-memory-gate/`.

## 2. Obiettivo

Ripristinare un confronto scientificamente corretto tra GTOSD e GTO+:

- preservare i valori originali mostrati da GTO+ e la loro provenienza;
- eliminare il falso confronto tra tali valori e il Peak RSS;
- ricostruire, per quanto possibile tramite esperimenti differenziali, cosa
  include “Memory needed for solving”;
- introdurre in GTOSD un accounting generico e verificabile della memoria
  posseduta dal solver;
- attivare nuovamente un gate memoria GTO+ solo dopo aver dimostrato che la
  metrica GTOSD e quella GTO+ hanno perimetro equivalente;
- mantenere Peak RSS, private bytes e working set come diagnostica OS separata;
- evitare qualsiasi ottimizzazione specifica per AHKHQH, TH7D6S o TSTC9D.

## 3. Non-obiettivi

Questo piano non autorizza:

- un nuovo limite RSS implicito;
- la sostituzione dei `2.000 MB` con `2 GiB`;
- l’uso del carico o della memoria disponibile dell’host come riferimento GTO+;
- l’uso di pagefile, memory mapping o working-set trimming per ridurre la
  metrica comparabile;
- una modifica dell’algoritmo, del DCFR di produzione, della precisione, del
  tree, dei range o del criterio di convergenza;
- la promozione di `solver_state_bytes` a metrica equivalente senza una prova
  del suo perimetro;
- la riscrittura retroattiva dei dati grezzi o dei report storici.

## 4. Evidenza sperimentale corrente

Gli esperimenti black-box separano già il numero mostrato da GTO+ dai contatori
del processo:

| Fixture | “Memory needed for solving” | WS idle | WS durante solve | Peak WS processo | Delta private durante solve |
|---|---:|---:|---:|---:|---:|
| AHKHQH | `8.000.000 B` | `84.606.976 B` | `110.596.096 B` | `112.287.744 B` | `25.763.840 B` |
| TH7D6S | `399.000.000 B` | `91.856.896 B` | `590.721.024 B` | `628.072.448 B` | `509.382.656 B` |
| TSTC9D, mediana 5 run | `2.000.000.000 B` | `94.785.536 B` | `2.029.105.152 B` | `2.182.807.552 B` | `1.975.304.192 B` |

Conclusioni già autorizzate dall’evidenza:

- il campo GTO+ non è Peak RSS;
- non è working set totale;
- non è private bytes totale;
- non è il semplice incremento di working set o private bytes rispetto
  all’idle;
- non esiste un overhead fisso da aggiungere o sottrarre;
- il valore cresce con la dimensione del gioco e appare quindi come una stima
  interna costruita dal layout del solver;
- la formula e il perimetro esatti restano non identificati.

Il progetto open source `b-inary/postflop-solver`, usato come modello
architetturale e non come prova dell’implementazione chiusa di GTO+, calcola una
stima pre-allocazione da storage principali, storage IP/chance e memoria
accessoria. Il relativo frontend mostra tale stima come RAM necessaria prima di
allocare le arene. Questo rende promettente l’ipotesi “solver-owned storage
estimate”, ma non dimostra quali componenti GTO+ includa.

## 5. Tassonomia obbligatoria delle metriche

Ogni report futuro dovrà distinguere almeno le seguenti classi.

| Metrica | Definizione | Uso ammesso |
|---|---|---|
| `gto_plus_solver_memory_reference` | Valore normalizzato dal campo UI “Memory needed for solving” | Riferimento esterno; nessun PASS finché la comparabilità è irrisolta |
| `solver_state_logical_bytes` | Payload logico persistente di regret, cumulative strategy e scale | Breakdown dello stato; non è memoria totale del solver |
| `solver_managed_payload_bytes` | Somma dei payload logici posseduti dal solver secondo categorie dichiarate | Analisi architetturale |
| `solver_managed_allocated_bytes` | Capacità realmente allocata/reservata per gli stessi blocchi | Accounting interno e overhead allocator |
| `solver_managed_peak_bytes` | Massimo simultaneo dei byte solver-owned durante lo scope dichiarato | Candidato al confronto, soggetto a equivalenza dimostrata |
| `mapped_backing_logical_bytes` | Stato logico spostato su file/mapping locale | Deve restare visibile e non ridurre artificialmente il payload comparabile |
| `process_peak_rss_bytes` | Picco resident set/working set del processo | Telemetria OS, senza riferimento GTO+ |
| `process_peak_private_bytes` | Picco della memoria privata committed del processo | Telemetria OS, senza riferimento GTO+ |

Regola fondamentale: la riduzione della residenza fisica non riduce
automaticamente la memoria logica o allocata del solver. Uno stato da 500 MB
mappato e residente per 8 MB continua a rappresentare 500 MB di storage logico.

## 6. Stato dei gate durante la correzione

Fino alla chiusura della ricerca semantica:

| Gate | Stato |
|---|---|
| correttezza matematica, layout e fingerprint | attivo e invariato |
| convergenza/dEV | attivo e invariato |
| tempo rispetto a GTO+ | attivo e invariato |
| memoria rispetto a “Memory needed for solving” | `NOT_EVALUATED_COMPARABILITY_UNRESOLVED` |
| Peak RSS | diagnostico, nessun PASS/FAIL |
| private bytes / commit | diagnostico, nessun PASS/FAIL |

I risultati memoria già pubblicati saranno riclassificati come segue:

- non diventano automaticamente FAIL del solver;
- non restano PASS di parità GTO+;
- diventano risultati ottenuti con una metrica non comparabile;
- dEV, EV, fingerprint e timing restano validi se i rispettivi protocolli erano
  validi;
- i dati grezzi Peak RSS restano corretti come osservazioni del processo.

## 7. Fase A — Hotfix semantico e blocco dei falsi PASS

### 7.1 Fixture

Creare lo schema `gtosd.gto_plus_convergence_benchmark.v4` e migrare tutte le
fixture GTO+ correnti.

Sostituire:

```json
"peak_rss_bytes": 399000000
```

con una struttura che preservi sia il testo osservato sia la normalizzazione:

```json
"solver_memory": {
  "display_label": "Memory needed for solving",
  "display_value": 399,
  "display_unit": "MB",
  "normalized_reference_bytes": 399000000,
  "normalization_rule": "decimal_mb_fixture_convention",
  "semantic_class": "gto_plus_internal_pre_solve_estimate",
  "comparability_status": "unresolved"
}
```

Per AHKHQH conservare la precisione visualizzata `8.0 MB`. Per TSTC9D
conservare esattamente la forma visualizzata/provenienza disponibile senza
inventare ulteriore precisione.

File primari:

- `benchmarks/fixtures/gto_plus_ahkhqh_101.json`;
- `benchmarks/fixtures/gto_plus_ahkhqh_103.json`;
- `benchmarks/fixtures/gto_plus_th7d6s_101.json`;
- fixture TH diagnostiche `par*` e `smoke`;
- `benchmarks/fixtures/gto_plus_tstc9d_101.json`;
- `benchmarks/fixtures/TEMPLATE.json`.

### 7.2 Parser e report CLI

In `apps/gto_cli/main.cpp`:

- rinominare `gto_plus_peak_rss_bytes` in un nome semanticamente corretto;
- rimuovere `desktop_peak_rss_cap_bytes`, unità e confronto associati;
- non assegnare più il riferimento GTO+ a
  `PostflopSolveOptions::maximum_peak_rss_bytes`;
- continuare a misurare `process_peak_rss_bytes()` come diagnostica;
- rimuovere il confronto `process_peak_rss <= GTO+ solver memory`;
- rimuovere `desktop_memory_gate`;
- sostituire `memory_gate.passed` con uno stato tri-state o esplicito:
  `not_evaluated`, `passed`, `failed`;
- nello stato iniziale produrre `not_evaluated` con motivo
  `gto_plus_metric_semantics_unresolved`;
- incrementare lo schema report da
  `gtosd.gto_plus_convergence_run.v3` a `v4`;
- separare nel JSON `process_memory`, `solver_memory_accounting` e
  `gto_plus_reference_memory`.

Struttura target indicativa:

```json
{
  "process_memory": {
    "peak_rss_bytes": 0,
    "peak_private_bytes": null,
    "normative_gate": null
  },
  "solver_memory_accounting": {
    "schema": "gtosd.solver_memory_accounting.v1",
    "state_logical_bytes": 0,
    "managed_payload_peak_bytes": null,
    "managed_allocated_peak_bytes": null
  },
  "gto_plus_reference_memory": {
    "metric": "internal_solver_memory_estimate",
    "normalized_reference_bytes": 399000000,
    "comparability_status": "unresolved"
  },
  "memory_comparison": {
    "status": "not_evaluated",
    "passed": null,
    "reason": "gto_plus_metric_semantics_unresolved"
  }
}
```

### 7.3 Runner PowerShell

In `tools/run_gto_plus_convergence_benchmark.ps1`:

- rimuovere la variabile `$desktopPeakRssCapBytes`;
- rimuovere utilizzo, headroom e PASS desktop;
- rinominare `$referencePeakRssBytes` in
  `$gtoPlusSolverMemoryReferenceBytes`;
- non calcolare `memoryScore` come riferimento GTO+ diviso Peak RSS;
- aggregare Peak RSS soltanto come telemetria;
- propagare `memory_comparison.status = not_evaluated`;
- impedire `-EnforceGate` sulla memoria finché la comparabilità non è
  `established`;
- mantenere indipendenti enforcement di correttezza e tempo.

In `tools/run_common_schedule_target.ps1`:

- eliminare `desktop_ram_passed` hardcoded a `2.000.000.000 B`;
- conservare `peak_rss_bytes` come valore diagnostico;
- non tradurre l’assenza del gate memoria in PASS.

In `tools/run_gto_plus_black_box.ps1`:

- rinominare `SolverMemoryLimitBytes`, che attualmente è solo metadato del
  protocollo, in `DisplayedSolverMemoryReferenceBytes`;
- rimuovere la parola `limit` se il valore non viene realmente imposto;
- registrare label, unità e precisione visualizzata.

### 7.4 Backend page-backed

L’attuale wiring passa il riferimento GTO+ a
`maximum_peak_rss_bytes`, selezionando automaticamente lo stato
`os_page_backed_scaled_uint16` nei casi stretti. Questo wiring deve essere
rimosso.

La funzionalità generica page-backed non sarà eliminata alla cieca. Verrà
classificata con il seguente criterio:

1. cercare consumatori non-benchmark e requisiti utente reali;
2. se esiste un budgeting esplicito configurato dall’utente, rinominare
   l’opzione in `resident_working_set_budget_bytes` e richiedere opt-in;
3. se non esiste alcun consumatore indipendente, rimuovere il percorso come
   ottimizzazione nata da un gate errato;
4. in entrambi i casi, contabilizzare l’intero stato logico page-backed nella
   memoria solver-owned;
5. vietare la selezione del backend sulla base del riferimento GTO+.

Il normale backend di benchmark dovrà essere quello previsto dal contratto di
produzione, senza comportamento condizionato dal valore 8/399/2.000 MB.

## 8. Fase B — Rimozione dei falsi target da estimator e API

`CanonicalLayoutOptions` contiene attualmente:

- `engineering_target_bytes = 1.800.000.000`;
- `absolute_gate_bytes = 2.000.000.000`;
- `meets_engineering_target`;
- `meets_absolute_gate`.

Questi default incorporano il falso requisito generale e devono essere
rimossi.

Il layout estimator dovrà:

- continuare a produrre il breakdown numerico;
- non incorporare soglie normative di default;
- accettare eventualmente un budget opzionale fornito esplicitamente dal
  chiamante;
- chiamare il risultato `meets_requested_budget`, non “absolute gate”;
- indicare la provenienza del budget (`user_configured`, `experiment`, ecc.);
- non assumere mai `2.000.000.000 B` come valore globale.

Componenti coinvolti:

- `include/gtosd/postflop/canonical_layout.hpp`;
- `libs/postflop/src/canonical_layout.cpp`;
- output `postflop layout-gto-plus` in `apps/gto_cli/main.cpp`;
- `tests/verify_tstc9d_canonical_layout.cmake`;
- test unitari del layout estimator.

Il comando potrà continuare a riportare `estimated_peak_bytes`, ma dovrà
chiarire se si tratta di un modello statico, quali componenti include e che non
è Peak RSS.

## 9. Fase C — Accounting generico della memoria solver-owned

### 9.1 Requisiti

L’accounting deve essere generale per ogni tree e non conoscere gli ID dei
benchmark.

Deve misurare almeno:

- stato persistente regret;
- stato persistente cumulative strategy;
- scale e metadati del codec;
- tree fisico e DAG canonico;
- action/chance edges;
- board objects, board heaps e mapping combo-locali;
- rank/showdown/terminal metadata;
- cache permanenti richieste durante il solve;
- scratch per worker;
- buffer di traversal;
- buffer di certificazione/best response;
- copie temporanee durante finalizzazione e persistenza;
- capacità degli allocator/arena;
- mapping e backing file logici.

### 9.2 Modello dati

Introdurre un ledger a categorie e fasi, aggiornato soltanto nei punti di
allocazione/deallocazione, non nell’hot path delle singole azioni.

Ogni categoria dovrà esporre:

- payload logico corrente e massimo;
- capacità allocata corrente e massima;
- tipo di backing: heap, arena, mapped file;
- fase di prima allocazione e rilascio;
- proprietà: persistente, scratch, certificazione, finalizzazione;
- inclusione o esclusione dalla metrica candidata e motivazione.

Fasi minime:

1. `process_start`;
2. `tree_preparation`;
3. `solver_state_ready`;
4. `traversal`;
5. `certification`;
6. `finalization`;
7. `checkpoint_materialization`;
8. `solution_ready`.

### 9.3 Invarianti

- la somma per categoria deve uguagliare il totale del ledger;
- tutti gli incrementi devono avere il corrispondente rilascio o una lifetime
  persistente dichiarata;
- overflow e underflow devono fallire esplicitamente;
- il contatore deve essere thread-safe;
- disabilitare la telemetria dettagliata non deve cambiare layout o risultati;
- lo stesso input/build deve produrre lo stesso payload logico;
- il valore logico non deve dipendere dalla pressione memoria dell’host;
- trimming e paging non devono ridurre il payload logico;
- Peak RSS e accounting interno non devono essere mescolati.

### 9.4 Scope temporale

Il riferimento GTO+ è mostrato prima del Run Solver. La metrica candidata GTOSD
dovrà quindi essere disponibile anch’essa dal layout prima del solve e poi
verificata contro il ledger runtime.

Il piano richiede entrambe:

- `predicted_solver_managed_bytes` prima dell’allocazione;
- `observed_solver_managed_peak_bytes` durante il run.

La differenza deve essere spiegata categoria per categoria. Una previsione non
verificata non può decidere il gate.

## 10. Fase D — Ricostruzione black-box della formula GTO+

### 10.1 Disegno sperimentale

Creare famiglie di progetti GTO+ in cui cambia una sola variabile per volta.
Nessun progetto deve essere costruito ad hoc per favorire GTOSD: l’obiettivo è
identificare la metrica del prodotto esterno.

Famiglie minime:

1. **Action branching**
   - una sola bet size;
   - due bet size;
   - massimo raise 0/1/2/3/4;
   - all-in automatico off/on quando applicabile.
2. **Range support**
   - range piccoli, medi e completi;
   - stesso supporto con pesi differenti;
   - supporti asimmetrici tra i giocatori.
3. **Chance topology**
   - solve dal flop;
   - solve dal turn;
   - solve dal river;
   - board rainbow, monotone, paired e con diversi automorfismi.
4. **Thread count**
   - 1, 2, 4 e 8 thread con tree identico;
   - serve a identificare eventuale scratch per-thread incluso nella stima.
5. **Precision/storage mode**
   - ogni modalità realmente esposta da GTO+;
   - distinguere “Memory needed for solving” da “Basic storage” e dimensione
     della soluzione salvata.

### 10.2 Osservazioni per variante

Registrare:

- screenshot del valore e sua precisione visualizzata;
- versione e hash di `GTO.exe`;
- hash del progetto originale e della copia read-only;
- configurazione completa del tree e dei range;
- valore prima del solve;
- numero thread selezionato;
- Peak WS, private bytes e plateau di allocazione come diagnostica;
- tempo fino al primo progress, target crossing, finalizzazione e rilascio;
- conteggi GTOSD sul gioco equivalente: nodi, infoset, action entries, chance
  entries e breakdown memoria.

La maggioranza delle varianti richiede soltanto la lettura pre-solve della
stima; i run completi saranno riservati a un sottoinsieme rappresentativo.

### 10.3 Trattamento delle unità e dell’arrotondamento

Non trattare il valore UI come un byte count esatto.

Per ogni osservazione costruire un intervallo compatibile con:

- precisione visualizzata;
- arrotondamento o troncamento;
- MB decimali;
- MiB binari, finché non esclusi sperimentalmente.

Il modello deve predire l’intervallo visualizzato, non soltanto il valore
centrale normalizzato dalla fixture.

### 10.4 Modelli candidati

Valutare almeno:

```text
M = C
  + b_state * action_entries
  + b_infoset * information_sets
  + b_node * decision_nodes
  + b_chance * chance_entries
  + b_board * canonical_boards
  + b_thread * worker_threads
```

e il modello ispirato a `postflop-solver`:

```text
M = element_width * (2 * main_storage + ip_storage + chance_storage)
  + miscellaneous_storage
```

Non imporre coefficienti interi o una rappresentazione a 16 bit prima che i
dati la sostengano. AHKHQH, TH7D6S e TSTC9D devono essere holdout o punti di
validazione, non gli unici punti usati per il fit.

### 10.5 Criterio di identificazione

La semantica può essere dichiarata `established` soltanto se:

- un unico modello generico spiega tutte le famiglie entro gli intervalli di
  arrotondamento;
- il modello predice correttamente fixture non usate nel fit;
- il risultato è stabile tra processi indipendenti;
- l’effetto dei thread è identificato o escluso;
- ogni componente incluso/escluso è documentato;
- non sono presenti costanti per board, benchmark ID o hash specifici;
- la metrica GTOSD corrispondente è calcolabile prima del solve e verificabile
  a runtime.

Se questi criteri non vengono raggiunti, il gate resta `NOT_EVALUATED`.

## 11. Fase E — Definizione del confronto e riattivazione del gate

Solo dopo la Fase D scegliere una delle seguenti conclusioni.

### Esito 1 — Equivalenza dimostrata

Se il perimetro GTO+ viene ricostruito in modo robusto:

- definire `gto_plus_comparable_solver_memory_bytes`;
- versionare la formula di accounting;
- confrontare la metrica GTOSD equivalente con il riferimento normalizzato;
- documentare inclusioni, esclusioni e unità;
- riattivare PASS/FAIL memoria per fixture;
- mantenere Peak RSS fuori dal gate.

### Esito 2 — Equivalenza parziale

Se è identificato soltanto un lower/upper bound:

- riportare intervalli;
- consentire `PASS_PROVEN`, `FAIL_PROVEN` o `INDETERMINATE` soltanto quando i
  bound lo permettono;
- non trasformare `INDETERMINATE` in PASS.

### Esito 3 — Metrica non identificabile

Se il black-box non consente una formula affidabile:

- mantenere il riferimento GTO+ come dato descrittivo;
- pubblicare separatamente il breakdown GTOSD;
- non avere un gate memoria comparativo;
- ottimizzare il sistema sulla metrica interna documentata, senza dichiarare
  parità numerica GTO+.

## 12. Fase F — Ottimizzazioni di sistema

Dopo aver definito la metrica corretta, le ottimizzazioni saranno selezionate
dal breakdown globale, non dai benchmark.

Ordine di analisi:

1. stato persistente per action entry;
2. scale e metadata per decision node;
3. tree/DAG e board mappings;
4. evaluator e terminal metadata;
5. scratch per worker;
6. memoria di certificazione;
7. sovrapposizione temporale tra preparazione, solve e finalizzazione;
8. allocator capacity, frammentazione e copie;
9. mapping/checkpoint solo come scelta di prodotto esplicita.

Ogni ottimizzazione dovrà superare:

- equivalenza matematica e trajectory test;
- dEV e root EV;
- fingerprint/layout quando applicabile;
- serializzazione e resume;
- single-thread e multi-thread;
- small/medium/large tree;
- range simmetrici e asimmetrici;
- misurazione pre/post per tutte le categorie di memoria;
- assenza di regressioni temporali non dichiarate.

Non sono ammesse ottimizzazioni attivate da:

- benchmark ID;
- board specifica;
- valore 8/399/2.000 MB;
- fingerprint delle fixture;
- soglie costruite per scegliere un backend soltanto su AHKHQH, TH7D6S o
  TSTC9D.

## 13. Piano test

### 13.1 Schema e parsing

- tutte le fixture v4 contengono `solver_memory`, non `peak_rss_bytes`;
- il parser rifiuta `peak_rss_bytes` sotto `gto_plus_reference` nelle fixture
  v4;
- un eventuale loader v3 tratta il vecchio campo come legacy misclassified
  solver memory, mai come Peak RSS;
- nessun fallback silenzioso trasforma il riferimento in un cap del processo;
- unità, label, normalizzazione e comparability status sono obbligatori.

### 13.2 Report

- Peak RSS compare soltanto in `process_memory`;
- `desktop_memory_gate` è assente;
- nessun cap `2 GiB` è generato;
- `memory_comparison.passed` è `null` quando lo stato è `not_evaluated`;
- correttezza e tempo possono essere valutati indipendentemente;
- i sommari multi-processo non classificano l’assenza di confronto come PASS.

### 13.3 Backend

- la fixture AHKHQH non seleziona automaticamente lo stato page-backed;
- cambiare il riferimento GTO+ non cambia il backend o il comportamento del
  solver;
- un budget esplicito utente, se la funzionalità viene conservata, è testato
  separatamente e non influenza il gate GTO+;
- il payload logico page-backed resta interamente contabilizzato.

### 13.4 Accounting

- test unitari per ogni categoria e lifetime;
- test di allocazione/rilascio e peak simultaneo;
- test overflow/underflow;
- confronto tra previsione e ledger su tree giocattolo enumerabili;
- stabilità del payload logico sotto working-set trimming;
- variazione attesa dello scratch con 1/2/4/8 thread;
- nessuna variazione di strategia, dEV o fingerprint con accounting attivo.

### 13.5 Regression suite

- build Release pulita;
- CTest completo;
- microbenchmark e replay;
- AHKHQH, TH7D6S e TSTC9D target-driven;
- cinque processi indipendenti dopo la stabilizzazione dello schema;
- controllo automatico che il codice attivo non contenga più hardcode
  `desktop_memory_gate`, `desktop_peak_rss_cap` o il falso cap globale.

I numeri `8.000.000`, `399.000.000` e `2.000.000.000` potranno restare soltanto
come riferimenti fixture/provenienza GTO+, non come costanti globali del motore.

## 14. Piano documentazione

### 14.1 Documenti normativi da correggere

- `README.md`;
- `docs/IMPLEMENTATION_STATUS.md`;
- `docs/specifications/PERFORMANCE.md`;
- `docs/specifications/VALIDATION.md`;
- `docs/GTO_PLUS_CONVERGENCE_BENCHMARK.md`;
- `docs/GTO_PLUS_NEW_BENCHMARK_GUIDE.md`;
- `docs/GTO_PLUS_PARITY_JOURNEY.md`;
- `docs/specifications/CHANGELOG.md`;
- template e commenti delle fixture.

### 14.2 Report storici

I documenti `STRICT_2GB*`, `TWO_GIB*`, `TST_STRICT_2GB*`,
`PEAK_RSS_AUDIT_2026-09-03.md` e gli altri report storici non saranno alterati
nei risultati grezzi. Riceveranno un banner:

```text
CORREZIONE SEMANTICA: il riferimento 2.000.000.000 B proveniva dal campo
GTO+ “Memory needed for solving” e non costituiva un cap Peak RSS desktop.
I confronti RSS contro quel valore sono storici e non comparabili.
```

Ogni banner dovrà collegare questo piano e il futuro report di chiusura.

### 14.3 Retention, archivio ed eliminazione

La revisione documentale applica questa classificazione:

1. i documenti normativi e le dashboard correnti vengono corretti nel testo;
2. i report misti, che contengono evidenza tecnica ancora utile ma anche una
   classificazione memoria errata, restano nel percorso originale con un banner
   di correzione semantica;
3. i report il cui oggetto principale era il falso gate Peak RSS/2 GB vengono
   spostati in `docs/archive/legacy-memory-gate/` e indicizzati da un README;
4. i dati grezzi, le misure Peak RSS e gli artifact sperimentali non vengono
   cancellati né riscritti;
5. un documento viene eliminato soltanto se non contiene evidenza univoca, è
   interamente duplicato da un'autorità successiva e tutti i riferimenti sono
   stati verificati.

L'audit del 2026-09-04 non ha trovato documenti eliminabili con sicurezza. I
quattro report centrati sul falso contratto vengono quindi archiviati, non
cancellati. Questa scelta preserva la catena di audit senza lasciarli nella
gerarchia normativa corrente.

### 14.3 Linguaggio vietato nei documenti correnti

Rimuovere dalle sezioni normative:

- “desktop 2 GB cap”;
- “strict 2 GiB contract”;
- “absolute 2 GB gate”;
- “GTO+ peak RSS reference”;
- “RSS parity” rispetto a 8/399/2.000 MB.

Sostituire con:

- “GTO+ displayed solver-memory reference”;
- “process Peak RSS diagnostic”;
- “memory comparability unresolved/established”.

## 15. Compatibilità e migrazione degli artefatti

- non riscrivere report JSON già prodotti;
- i nuovi consumer devono riconoscere schema e versione;
- i report v3 devono essere etichettati `legacy_metric_misclassified` quando
  letti da strumenti nuovi;
- nessun report v3 può essere aggregato con report v4 senza una conversione
  esplicita;
- la conversione deve preservare il valore numerico e cambiare soltanto la
  semantica dichiarata;
- checkpoint e solution format non devono cambiare per la sola correzione del
  benchmark;
- eventuali modifiche al backend page-backed richiedono test specifici di
  compatibilità checkpoint.

## 16. Rischi e mitigazioni

| Rischio | Impatto | Mitigazione |
|---|---|---|
| trattare il valore UI arrotondato come byte esatti | modello falso | interval fitting e conservazione del testo visualizzato |
| sostituire il falso Peak RSS con `solver_state_bytes` senza prova | nuovo falso PASS | gate disabilitato fino all’equivalenza |
| perdere telemetria OS utile | regressioni invisibili | mantenere RSS/private bytes diagnostici |
| rimuovere una funzionalità page-backed utile | regressione prodotto | audit consumatori e decisione separata |
| conservare una scorciatoia nata dal benchmark | paging e timing nascosti | vietare wiring automatico dal riferimento GTO+ |
| modificare documenti storici come se i run non fossero avvenuti | perdita di auditabilità | banner, non riscrittura dei dati |
| fit eccessivo sui tre benchmark | ottimizzazione per fixture | famiglie OAT e holdout |
| overhead dell’accounting nell’hot path | timing alterato | aggiornamenti solo su alloc/free e benchmark A/B |
| confondere MB e MiB | errore fino al 4,86% | unità e conversione esplicite |

## 17. Ordine di esecuzione

1. congelare e hashare gli artefatti sperimentali AHK/TH/TST;
2. introdurre l’errata corrige nelle autorità correnti;
3. migrare fixture e report a v4;
4. rimuovere il falso gate RSS e il falso cap desktop;
5. scollegare il riferimento GTO+ dalla selezione page-backed;
6. aggiornare test e runner senza introdurre ancora un nuovo PASS memoria;
7. implementare e validare il ledger solver-owned generico;
8. eseguire la matrice differenziale GTO+;
9. decidere formalmente se la comparabilità è completa, parziale o
   irraggiungibile;
10. solo se dimostrata, attivare il nuovo gate memoria;
11. profilare e ottimizzare i componenti dominanti dell’intero sistema;
12. rieseguire la certificazione completa e pubblicare il report di chiusura.

## 18. Kill gate

Il lavoro deve fermarsi prima della riattivazione del gate memoria se si
verifica uno dei seguenti casi:

- la formula GTO+ richiede assunzioni non osservabili;
- più modelli incompatibili spiegano ugualmente i dati;
- le unità/precisione producono intervalli troppo ampi;
- la metrica GTOSD candidata esclude storage che GTO+ plausibilmente include;
- page backing o trimming sono necessari per ottenere il PASS;
- una modifica cambia correttezza, trajectory o risultati senza una decisione
  separata;
- il modello contiene eccezioni per fixture o board.

In caso di kill gate, l’esito corretto è `COMPARABILITY_UNRESOLVED`, non un
PASS forzato.

## 19. Definition of Done

La correzione sarà completa soltanto quando:

- nessuna fixture corrente chiama 8/399/2.000 MB `peak_rss_bytes`;
- nessun codice attivo contiene un cap desktop indipendente inventato;
- `2.000.000.000 B` compare soltanto come riferimento TSTC9D/provenienza o in
  materiale storico marcato;
- il benchmark non sceglie backend o residency in base al riferimento GTO+;
- Peak RSS è riportato come diagnostica priva di gate;
- il report distingue memoria GTO+, memoria solver-owned e memoria processo;
- i precedenti PASS/FAIL memoria sono riclassificati senza alterare i dati;
- l’accounting interno è testato e non modifica la matematica;
- la comparabilità è dichiarata con evidenza oppure resta esplicitamente
  irrisolta;
- eventuale nuovo gate usa una formula versionata, generica e validata su
  holdout;
- CTest, benchmark matematici, replay, target-driven e processi indipendenti
  passano per le parti ancora normative;
- la documentazione corrente non contiene affermazioni contraddittorie.

## 20. Decisione successiva richiesta

La prima implementazione dovrà limitarsi alla **Fase A e Fase B**: correggere
semantica, schema, runner e falsi cap, lasciando il confronto memoria in stato
`NOT_EVALUATED`. L’accounting e la ricerca black-box seguiranno come cambi
separati e verificabili.

Nessun nuovo limite memoria sarà introdotto senza una decisione esplicita.

## 21. Stato di esecuzione documentale — 2026-09-04

La fase documentale preliminare è completata:

- autorità correnti, dashboard, roadmap, guida benchmark e specifiche sono
  allineate su `NOT_EVALUATED_COMPARABILITY_UNRESOLVED`;
- i blocker algoritmici fondati soltanto sul falso cap memoria sono ritirati,
  senza promuovere automaticamente alcun candidato;
- i report misti conservano dati e decisioni non-memory con un banner;
- i quattro report centrati sul falso gate sono archiviati in
  `docs/archive/legacy-memory-gate/`;
- nessun report è stato cancellato perché tutti contengono evidenza univoca;
- al momento del commit documentale preliminare `eddaa58`, codice, fixture,
  runner e test conservavano ancora il comportamento legacy v3 descritto nel
  piano.

Questa fotografia preliminare è stata successivamente chiusa dalla Fase A+B
descritta sotto.

## 22. Stato di esecuzione Fase A+B — 2026-09-04

Le Fasi A+B sono implementate senza introdurre un nuovo limite memoria:

- tutte le fixture correnti usano
  `gtosd.gto_plus_convergence_benchmark.v4` e l’oggetto tipizzato
  `gto_plus_reference.solver_memory`;
- il CLI emette `gtosd.gto_plus_convergence_run.v4`; il runner multiprocesso
  emette summary v4 e accetta soltanto fixture v4;
- `process_memory`, `solver_memory_accounting` e
  `gto_plus_reference_memory` sono sezioni distinte;
- `memory_comparison.status` è `not_evaluated`, `passed` è `null` e la reason è
  `gto_plus_metric_semantics_unresolved`;
- `memory_gate`, `solver_state_gate`, `desktop_memory_gate` e il cap desktop
  implicito non sono più emessi;
- i benchmark GTO+ usano vettori residenti con budget `null`; il supporto
  page-backed rimane disponibile soltanto come opzione generica esplicita
  `resident_working_set_budget_bytes`;
- l’estimatore del layout non contiene più target predefiniti 1,8/2,0 GB e
  valuta soltanto un eventuale budget tipizzato user-configured/experiment;
- gli input diretti v3 restano leggibili soltanto tramite conversione v4
  `legacy_metric_misclassified`, senza propagare un falso confronto.

Gli artefatti sorgente esterni sono stati trattati come read-only e congelati
per SHA-256 prima della validazione:

| Artefatto | Byte | SHA-256 |
|---|---:|---|
| `benchmark GTO+/GTOSD.gto` | 149.608 | `5A22BB2803D84432FC23F0D48FA4C750CF3D205361FC7D28B8FB69C9E6AE74F6` |
| `benchmark GTO+/GTOSD2.gto` | 373.588 | `F0AEA001678B50EB69E592BC0C986E6316BF92C7D4ABD34A5418635410479A9A` |
| `benchmark GTO+/GTOSD3.gto` | 1.735.450 | `48EEDD8FAFFC80B805CFF40E8BED390E6EBB97A95262EC0865A182E79C79C464` |

Il ledger completo dei picchi solver-owned e la ricostruzione black-box della
formula GTO+ restano Fasi C/D separate. Finché non forniscono una prova di
equivalenza, nessun dato memoria chiude il parity gate.

Validazione osservata sul cambiamento Fase A+B:

- build MSVC Release dei target `gto_cli`, `gtosd_phase10_tests` e
  `gtosd_canonical_layout_tests`: PASS;
- CTest Release completo: `28/28 PASS` in `105,46 s`;
- regression v4 fixture/report, rigetto dei campi memoria legacy e conversione
  v3 misclassified: PASS;
- test manuali dei runner black-box e user-configured process-memory budget:
  PASS;
- smoke AHKHQH v4 a cinque processi: convergenza deterministica a 80
  iterazioni e dEV `0,951423%`; non promotion-grade perché il worktree era
  dirty e i metadati hardware non erano completi.
