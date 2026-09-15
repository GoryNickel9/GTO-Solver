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
gto_cli postflop estimate <config.json> <ram_gib> <disk_gib>
gto_cli postflop solve <config.json> <iterations> <checkpoint> <report_prefix> <ram_gib> <disk_gib> [cert_interval]
gto_cli postflop resume <config.json> <iterations> <checkpoint> <report_prefix> <ram_gib> <disk_gib> [cert_interval]
gto_cli postflop pause|cancel <checkpoint>
gto_cli postflop query <config.json> <checkpoint> <node> <combo_id>
gto_cli postflop certify <config.json> <checkpoint>
gto_cli postflop compare-gto-plus <config.json> <checkpoint> <reference.json>
gto_cli postflop benchmark-gto-plus <specification.json> <report.json>
gto_cli postflop layout-gto-plus <specification.json> <report.json>
gto_cli postflop benchmark-config <pf-f1|pf-f2|pf-f3> <output.json>
gto_cli postflop root-lock-diagnostic <config.json> <lock.json> <iterations> <report.json>
```

`solve` e `resume` producono checkpoint e report JSON/Markdown. `certify`
ricalcola best response e NashConv. `query` restituisce la strategia media per
nodo/combo. `benchmark-gto-plus` usa lo schema e la fixture versionati; non va
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

I comandi operativi `postflop solve` e `postflop resume` risolvono sempre il
profilo production versionato `1.0`: `ProductionDcfr`, stato
`ScaledUint16RegretStrategy`, averaging delay 0, certificazione ogni 20
iterazioni, target stretto e profondità parallela 7. L'intervallo CLI diverso
da 20 viene rifiutato. Il backend production è `LazyInRam`; un preflight RAM
insufficiente produce un errore esplicito e non attiva un fallback Float64
out-of-core. `resume` rifiuta checkpoint CFR+ o privi di identità algoritmica
ricostruibile. Il riepilogo iniziale stampa profilo, algoritmo, precisione,
delay, intervallo e profondità risolti.

Il report solve schema 2 conserva `elapsed_seconds` e aggiunge
`build_to_ready_seconds`, `solve_to_consultable_seconds` e
`build_to_consultable_seconds`. Il comando carica la configurazione, avvia il
timer product, esegue preflight e preparazione, poi risolve sull'albero preparato.
Il secondo intervallo termina dopo la scrittura del checkpoint finale; startup e
parsing sono esclusi. `phase_seconds` separa layout,
inizializzazione, traversal, certificazione e finalizzazione; `timer_scope`
dichiara che startup è escluso e il lavoro specifico del gioco è incluso.

`benchmark-gto-plus` espone inoltre `product_timing` con contratto
`gtosd.product_timing.v1`: build-to-ready, solve-to-consultable e totale
build-to-consultable sono misurati sulla fixture con i range reali. Il wrapper
multiprocesso aggrega mediana e p95 dei tre intervalli dopo un preflight CPU/RAM
prima di ogni processo.

## HU preflop research

Il runner locale `benchmarks/gtosd_hu_preflop_solve` accetta il fixture CO40 e
supporta la telemetria V19 senza cambiare il training:

```text
gtosd_hu_preflop_solve --config <config.json> --output <result.json>
gtosd_hu_preflop_solve --config <config.json> --output <result.json> \
  --action-conditioned-telemetry \
  --action-conditioned-telemetry-output <telemetry.json>
gtosd_hu_preflop_solve --config <config.json> --output <result.json> \
  --global-common-random-numbers \
  --maximum-action-conditioned-telemetry-entries <N>
gtosd_hu_preflop_solve --config <config.json> --output <result.json> \
  --root-decision-trace-classes JTo,QJo,J9s \
  --root-decision-trace-deals-per-class 2000 \
  --root-decision-trace-output <trace.json>
gtosd_hu_preflop_solve --config <config.json> --output <result.json> \
  --postflop-distributional-street-adaptive-perfect-recall-v23 \
  --flop-buckets 32 --turn-buckets 128 --river-buckets 512 \
  --abstract-game-output <manifest.json> \
  --postflop-policy-output <policy.bin>
gtosd_hu_preflop_abstract_nashconv \
  --config benchmarks/fixtures/hu_preflop_hu10_calibration_v1.json \
  --output <certificate.json> \
  --checkpoint-output <checkpoint.bin> \
  --deals-per-co-class 2 --equity-samples 8 \
  --iterations 10000 --interval 1000 \
  --maximum-nodes 500000
gtosd_hu_preflop_tre_validation \
  --config benchmarks/fixtures/hu_preflop_hu10_calibration_v1.json \
  --output <certificate.json> \
  --training-deals-per-co-class 8 \
  --response-deals-per-co-class 8 \
  --evaluation-initial-deals-per-co-class 8 \
  --evaluation-maximum-deals-per-co-class 64 \
  --evaluation-normalized-half-width-target 0.01 \
  --equity-samples 8 --iterations 5000000 \
  --maximum-nodes 12000000
```

Nel certificatore V23, `--iterations` è il limite massimo. Per impostazione predefinita il run si
ferma al primo checkpoint con `normalized_dev < 0.01`; il JSON distingue `iterations`,
`requested_iterations` e `stopped_early_on_target`. `--continue-after-target` mantiene invece il
numero di iterazioni richiesto ed è riservato alle curve di ricerca che devono proseguire oltre il
gate.

`--action-conditioned-telemetry` abilita le osservazioni in memoria;
`--action-conditioned-telemetry-output` abilita anche l'export
`gtosd.hu_preflop_action_conditioned_telemetry.v1`. Il canale è diagnostico,
opt-in e non può essere usato per promuovere WMAE/TV su smoke brevi. La
classificazione dei terminali, la semantica della massa osservata e il gate
V19 sono documentati in
[`V19_FASE_A_AUDIT_2026-09-13.md`](../research/preflop_r6_20260910/V19_FASE_A_AUDIT_2026-09-13.md).

`--global-common-random-numbers` è un percorso di ricerca per la modalità
batched: ripristina lo stato RNG all'ingresso di ogni confronto d'azione del
traverser e aggiunge il suffisso `global_common_random_numbers_v1` all'identità
dell'algoritmo. Non è un default di produzione e richiede
`--training-batch-iterations`. Il limite telemetry è esplicito; quando viene
raggiunto, le nuove chiavi vengono scartate e il report espone
`action_conditioned_telemetry_dropped`, quindi un export troncato non può
essere usato come gate di qualità.

La root decision trace è opt-in e viene eseguita dopo il training sulla policy
congelata. Per ogni classe richiesta forza le cinque azioni CO sugli stessi deal
fisici condizionati e usa lo stesso seed iniziale delle continuation. Esporta EV,
errori standard paired, rami preflop, terminali, reach per street e contributi dei
bucket. Non modifica regret o strategy sum. Il formato
`gtosd.hu_preflop_root_decision_trace.v1` accetta al massimo 16 classi e 10.000
deal per classe, con un limite aggregato di 20.000 class-deal per contenere RAM
e dimensione dell'export. `tools/analyze_hu_preflop_root_decision_trace.py`
trasforma il sidecar in un report leggibile; il risultato resta una diagnostica
campionata, non una NashConv.

`--postflop-distributional-street-adaptive-perfect-recall-v23` conserva il mapping V8 ma include
nella chiave la classe preflop e tutti i bucket già osservati. L'output contiene
`postflop_recall_contract`: V8 produce `FAILED_PERFECT_RECALL` con le osservazioni dimenticate,
mentre la modalità V23 produce `PASS_PERFECT_RECALL`. Questo campo verifica lo schema della chiave;
la NashConv whole-game richiede ancora il certificatore previsto dal protocollo V23.

`--abstract-game-output` salva il manifest strutturale: fingerprint di regole, tree e astrazione,
census pubblico per street e limite cartesiano degli information set. Il manifest non dichiara il
limite cartesiano come insieme raggiungibile e resta
`STRUCTURE_COMPILED_CHANCE_MODEL_PENDING`.

`gtosd_hu_preflop_abstract_nashconv` costruisce invece un gioco finito con `K` deal completi per
ciascuna delle 81 classi CO. Linear MCCFR e le due best response consumano lo stesso
`finite_game_fingerprint`. La NashConv risultante è esatta per quel corpus chance congelato; il
campo `physical_nashconv_certified` resta sempre `false`. `--checkpoint-input` riprende un run se
game fingerprint, seed e intervallo coincidono; `--checkpoint-output` salva atomicamente lo stato
finale.

Il certificato `gtosd.hu_preflop_v23_abstract_nashconv.v2` aggiunge `policy_completion`. Il
contratto `uniform_unseen_v1` assegna una distribuzione uniforme soltanto agli information set
richiesti dal gioco target e assenti nella policy sorgente. L'audit pubblica chiavi richieste,
corrispondenti, mancanti e inutilizzate, oltre a `exact_key_coverage`,
`reach_weighted_coverage` e alle componenti per giocatore. Sul corpus usato dal training entrambe
le coperture devono essere `1`. Su un corpus indipendente ogni fallback resta visibile; il comando
non lo può presentare come supporto appreso. Lo schema continua ad accettare i certificati v1 già
prodotti.

`gtosd_hu_preflop_tre_validation` separa tre corpus chance con seed distinti. Linear MCCFR
addestra la strategia media su T. Due best response esatte vengono cercate su R contro la policy
congelata e poi congelate. Su E, baseline e risposta usano gli stessi deal per ottenere differenze
paired. Il runner pesa le 81 classi CO con la massa fisica esatta e campiona condizionatamente
hole BTN e board completo.

E cresce per raddoppio da `--evaluation-initial-deals-per-co-class` fino a
`--evaluation-maximum-deals-per-co-class`. Lo stop richiede che la massima semiampiezza
normalizzata fra due profile EV, due gain e la loro somma non superi il target. Gli intervalli
usano un'approssimazione normale stratificata e una correzione Bonferroni per cinque metriche e
tutti i look pianificati. Il certificato riporta ogni look, la copertura della policy T su R/E e
quella delle risposte R su E. `physical_nashconv_certified` resta `false`: il gain su E misura una
risposta candidata fuori campione, non la best response fisica esaustiva.

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
