# Interfaccia a riga di comando

## Contratto generale

`gto_cli` è l'interfaccia automatizzabile locale. Scrive risultati leggibili o
report versionati su stdout/file, diagnostica su stderr e usa codici di uscita
non zero per input, I/O, incompatibilità, failure numeriche o gate falliti.
Un comando sconosciuto restituisce `2`.

Tutti i comandi di build-tree, solve, resume, best response e benchmark usano
esclusivamente CPU e RAM. La CLI non espone e non esporrà opzioni GPU.

## Diagnostica e tree

```text
gto_cli self-check
gto_cli tree-inspect <config.json> [maximum_nodes]
gto_cli isomorphism-audit <config.json>
```

`self-check` verifica versione, mazzo e stato HU minimo. `tree-inspect` costruisce
e riepiloga il public tree. `isomorphism-audit` confronta le 24 permutazioni
globali dei semi.

## Laboratorio solver e memoria

```text
gto_cli solver-lab <game> <algorithm> <iterations> [seed] [threads]
gto_cli dcfr-sweep <game> <iterations>
gto_cli memory-lab <pf-f1|pf-f2|pf-f3> <lazy|street|out-of-core> [resident_pages]
gto_cli memory-probe <pf-f1|pf-f2|pf-f3> <backing_file>
```

Giochi: `matching`, `kuhn`, `leduc`, `short-deck-toy`,
`short-deck-rake-toy`. Algoritmi: `cfr`, `cfr+`, `linear`, `dcfr`, `mccfr`.

## Postflop

```text
gto_cli postflop validate <config.json>
gto_cli postflop build-feature-cache <config.json> <feature_cache>
gto_cli postflop estimate <config.json> <ram_gib> <disk_gib>
gto_cli postflop solve <config.json> <iterations> <checkpoint> <report_prefix> <ram_gib> <disk_gib> [cert_interval]
gto_cli postflop resume <config.json> <iterations> <checkpoint> <report_prefix> <ram_gib> <disk_gib> [cert_interval]
gto_cli postflop solve-bucketed <config.json> <iterations> <checkpoint> <report_prefix> <ram_gib> <disk_gib> <buckets> [cert_interval] [feature_cache]
gto_cli postflop resume-bucketed <config.json> <iterations> <checkpoint> <report_prefix> <ram_gib> <disk_gib> <buckets> [cert_interval] [feature_cache]
gto_cli postflop edges-bucketed <config.json> <buckets> <node> [feature_cache]
gto_cli postflop resolve-bucketed <config.json> <blueprint_checkpoint> <output_checkpoint> <buckets> <edge:outcome,...> <iterations> <ram_bytes> <disk_bytes> <snapshot_bytes> <report.json> [feature_cache]
gto_cli postflop pause|cancel <checkpoint>
gto_cli postflop query <config.json> <checkpoint> <node> <combo_id>
gto_cli postflop query-bucketed <config.json> <checkpoint> <node> <combo_id> <buckets> [feature_cache]
gto_cli postflop certify <config.json> <checkpoint>
gto_cli postflop certify-bucketed <config.json> <checkpoint> <buckets> [feature_cache]
gto_cli postflop compare-gto-plus <config.json> <checkpoint> <reference.json>
gto_cli postflop benchmark-gto-plus <specification.json> <report.json>
gto_cli postflop layout-gto-plus <specification.json> <report.json>
gto_cli postflop preflight-bucketing-gto-plus <specification.json> <report.json> <K_csv> <ram_bytes> <disk_bytes> [fixed_turn]
gto_cli postflop build-feature-cache-gto-plus <specification.json> <feature_cache> [fixed_turn]
gto_cli postflop qualify-bucketing-gto-plus <specification.json> <feature_cache> <report.json> <K> <iterations> <ram_bytes> <disk_bytes> [fixed_turn]
gto_cli postflop benchmark-config <pf-f1|pf-f2|pf-f3> <output.json>
gto_cli postflop root-lock-diagnostic <config.json> <lock.json> <iterations> <report.json>
```

La selezione production di K usa il runner comune, non il comando
single-fixture direttamente:

```powershell
& tools/run_global_card_abstraction_qualification.ps1 `
  -GtoCli out/build/codex-release/apps/gto_cli/gto_cli.exe `
  -Specifications @(
    'benchmarks/fixtures/gto_plus_ahkhqh_101.json',
    'benchmarks/fixtures/gto_plus_th7d6s_101.json',
    'benchmarks/fixtures/gto_plus_tstc9d_101.json') `
  -OutputDirectory out/qualification/global-k32 `
  -Buckets 32
```

Il runner richiede un worktree tracked pulito, registra commit e SHA-256 del
binario, costruisce una sola cache exact per fixture e applica lo stesso K,
CFR+ Float64, otto thread e orizzonte a tutta la suite. Un singolo FAIL
produce `REJECTED_GLOBAL`; non esiste una mappa K-per-fixture.

`solve` e `resume` producono checkpoint e report JSON/Markdown exact.
`build-feature-cache` enumera una volta le feature postflop esatte per config e
range uniformi CLI e salva atomicamente un manifest 1.0 indipendente dal numero
di bucket. Stampa schema, fingerprint sorgente/cache, partizioni, osservazioni,
byte e tempo di costruzione.

I corrispondenti comandi `*-bucketed` selezionano esplicitamente k-means su
feature W/T/L/equity exact e CFR+ Float64 a otto thread; non modificano il default
dei comandi storici. Il parametro `buckets` è per partizione board/player.
`certify-bucketed` rialza la policy e ricalcola best response/NashConv sul gioco
combo-level completo. `query-bucketed` restituisce strategia media, bucket e
numero di combo membro. Senza `feature_cache`, i comandi ricostruiscono le
feature; con il manifest opzionale riusano l'enumerazione e verificano schema,
sorgente e fingerprint prima del clustering. Il report pubblica flag di riuso,
fingerprint e tempi separati. Percorso diretto e cache producono checkpoint
bit-identici; granularità o config differenti sono rifiutate. La CLI corrente
usa range uniformi, come il percorso
postflop exact storico; range personalizzati sono disponibili nell'API C++.

`edges-bucketed` restituisce JSON v1 con gli edge di un nodo canonico e, per
ogni outcome, child, chance card, molteplicità fisica e automorfismo. Si parte
dal nodo `0`; le coppie `edge_index:outcome_index` percorse formano il path per
`resolve-bucketed`. Quest'ultimo non sovrascrive il blueprint: alloca entro il
budget esplicito soltanto il rollback degli action slot del frontier, esegue
CFR+ con sette worker più coordinatore e salva un checkpoint distinto. Il
report `gtosd.postflop.bucketed-subgame-resolution.v1` conserva path, reach
pubblico, byte dello snapshot, metriche exact full-game prima/dopo e decisione
`candidate_accepted` o `blueprint_fallback`. Un frontier terminale, condiviso
attraverso il confine o con più ingressi viene rifiutato.

Il preflight CLI bucketed usa la forma canonica senza allocare lo stato e
calcola upper bound specifici per ogni K: stato Float64 astratto, mapping,
scratch a otto thread, cache, transienti e spazio atomico/page-backed. Non
attribuisce un vantaggio RSS non misurato all'out-of-core. Il report solve
dichiara sempre `solver_threads=8`, `uses_bucketing`, fingerprint, compression
ratio e weighted MSE.

I tre comandi `*-bucketing-gto-plus` applicano lo stesso contratto alle fixture
versionate e ai turn opzionali. Il runner
`tools/run_card_abstraction_qualification.ps1` costruisce una cache una volta,
esegue cinque processi a otto thread e un oracle seriale separato, richiedendo
fingerprint e metriche deterministiche, NashConv `<1%`, RAM sotto il budget e
differenza parallelo/seriale `<=1e-4`. La variabile
`GTOSD_CARD_ABSTRACTION_DIAGNOSTIC_SOLVER_THREADS=1` è riservata all'oracolo;
il percorso promuovibile resta a otto thread. `RamBytes` e `DiskBytes` sono
obbligatori: il runner non reintroduce un cap implicito da 2 GB né ricava un
budget dai valori “Memory needed for solving” di GTO+.

`benchmark-gto-plus` usa lo schema e la fixture versionati; non va
sostituito con un timing ad hoc. La specifica corrente è
`gtosd.gto_plus_convergence_benchmark.v4`: parametrizza board, range, stack,
sizing, profondità di raise, regola all-in, parametri di run, nodi di riferimento
e il riferimento `solver_memory`. Il report v4 separa
`gto_plus_reference_memory`, `solver_memory_accounting` e `process_memory`; il
confronto memoria è `not_evaluated`/`passed: null`. Il CLI diretto legge anche
v1/v2/v3 per conversione esplicita, marcando v3 come
`legacy_metric_misclassified`; il wrapper multiprocesso richiede v4.
`layout-gto-plus` produce `gtosd.canonical_chance_layout.v2` senza budget
impliciti. `root-lock-diagnostic` è il percorso
diagnostico F10.4 `diagnostic_external_root_lock`: blocca la strategia del nodo
root CO sulle probabilità esterne combo-per-combo (36 righe Bet 20/Check di
GTO+ v1.6.9), risolve il gioco vincolato e riporta convergenza del gioco
vincolato, exploitability del gioco originale ed EV dei nodi di riferimento
con delta rispetto a GTO+.

Nel campo `gtosd_run.algorithm` della specifica v4, `production_dcfr` seleziona
il contratto comune qualificato `1.5/0/3` con reset `1,2,5,17,65`. `dcfr`
rimane la variante parametrica/comparator e non e' un alias della production.

## Storage

```text
gto_cli storage keygen
gto_cli storage pack <config.json> <checkpoint> <solution.gtsd> <key_hex>
gto_cli storage verify <solution.gtsd> <key_hex>
gto_cli storage query <solution.gtsd> <key_hex> <node> <combo_id>
gto_cli storage migrate <source.gtsd> <destination.gtsd> <key_hex>
gto_cli storage catalog-add <catalog.gtsddb> <solution.gtsd> <key_hex>
gto_cli storage catalog-list <catalog.gtsddb>
```

La chiave è esadecimale a 32 byte e deve essere protetta dal chiamante. `verify`
legge e autentica tutti i chunk; `query` usa random access; `migrate` non
sovrascrive la source.

## Riproducibilità

I comandi di benchmark devono essere eseguiti su build Release, da un ambiente
Visual Studio configurato, registrando commit e hardware. Path di output e
checkpoint devono essere distinti tra processi indipendenti. Un report parziale
o un processo terminato non viene aggregato come successo.

## Evoluzione

Nuovi sottocomandi mantengono compatibilità oppure incrementano la versione dei
report/schema. Node lock, preflop e multiway non sono comandi supportati oggi;
non devono essere emulati modificando checkpoint a mano.
