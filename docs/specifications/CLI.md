# Interfaccia a riga di comando

## Contratto generale

`gto_cli` è l'interfaccia automatizzabile locale. Scrive risultati leggibili o
report versionati su stdout/file, diagnostica su stderr e usa codici di uscita
non zero per input, I/O, incompatibilità, failure numeriche o gate falliti.
Un comando sconosciuto restituisce `2`.

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
gto_cli postflop benchmark-config <pf-f1|pf-f2|pf-f3> <output.json>
```

`solve` e `resume` producono checkpoint e report JSON/Markdown. `certify`
ricalcola best response e NashConv. `query` restituisce la strategia media per
nodo/combo. `benchmark-gto-plus` usa lo schema e la fixture versionati; non va
sostituito con un timing ad hoc.

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
