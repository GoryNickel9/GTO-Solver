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
gto_cli postflop pause|cancel <checkpoint>
gto_cli postflop query <config.json> <checkpoint> <node> <combo_id>
gto_cli postflop query-bucketed <config.json> <checkpoint> <node> <combo_id> <buckets> [feature_cache]
gto_cli postflop certify <config.json> <checkpoint>
gto_cli postflop certify-bucketed <config.json> <checkpoint> <buckets> [feature_cache]
gto_cli postflop compare-gto-plus <config.json> <checkpoint> <reference.json>
gto_cli postflop benchmark-gto-plus <specification.json> <report.json>
gto_cli postflop layout-gto-plus <specification.json> <report.json>
gto_cli postflop benchmark-config <pf-f1|pf-f2|pf-f3> <output.json>
gto_cli postflop root-lock-diagnostic <config.json> <lock.json> <iterations> <report.json>
```

`solve` e `resume` producono checkpoint e report JSON/Markdown exact.
`build-feature-cache` enumera una volta le feature postflop esatte per config e
range uniformi CLI e salva atomicamente un manifest 1.0 indipendente dal numero
di bucket. Stampa schema, fingerprint sorgente/cache, partizioni, osservazioni,
byte e tempo di costruzione.

I corrispondenti comandi `*-bucketed` selezionano esplicitamente k-means su
feature W/T/L/equity exact e CFR+ Float64 seriale; non modificano il default
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

Il preflight CLI bucketed è conservativo: usa ancora la stima exact esistente,
quindi non sottostima le risorse ma può rifiutare una configurazione che il solo
stato astratto farebbe entrare in RAM. Il report dichiara sempre
`uses_bucketing`, fingerprint, compression ratio e weighted MSE.

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
