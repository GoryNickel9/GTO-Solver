# Formato delle soluzioni

## Due livelli di persistenza

GTOSD distingue:

- checkpoint postflop 1.0: stato di calcolo necessario a pausa e resume;
- soluzione `.gtsd` 1.0: archivio autenticato, compresso, indicizzato e adatto
  al caricamento di prodotto.

Un checkpoint non sostituisce una soluzione verificata. Una soluzione conserva
config, range, checkpoint e certificazione coerenti.

Il laboratorio `FiniteGame` usa `GTOSD_CFR_CHECKPOINT 1.1`. La minor 1 aggiunge
l'identità `ProductionDcfr` senza cambiare il payload; i checkpoint 1.0 dei
cinque algoritmi precedenti restano leggibili. Minor future e configurazioni
ProductionDcfr diverse da `1.5/0/3`, single-thread e averaging immediato sono
rifiutate.

## Checkpoint postflop

`PostflopCheckpoint` contiene:

- versione major/minor;
- fingerprint del gioco;
- iterazioni completate e averaging delay;
- numero di action slot;
- numero di decision node e relative scale, per gli stati node-scaled;
- precisione dello stato;
- algoritmo e parametri DCFR versionati;
- eventuale backing file;
- cumulative regret e cumulative strategy nel payload selezionato (`double`,
  `float`, packed legacy oppure uint16 action-major con scale float32).

Il resume rifiuta versione, fingerprint, layout, precisione o algoritmo
incompatibili. `ProductionDcfr` conserva il valore enum/checkpoint `11`; la
schedule e' coperta da resume continuo/segmentato byte-equivalent. Lo stato non
contiene una strategia root esterna implicita: F10.4 resta un percorso
diagnostico esplicito e non può essere riaperto come equilibrio standard.

Il writer checkpoint nativo usa il payload binario v2. Oltre ai campi legacy,
serializza algoritmo, precisione, esponenti DCFR, conteggio decision node,
scale e codici uint16, tutti coperti dal checksum. Il reader continua a leggere
il payload v1 come dato legacy; la leggibilità non autorizza il resume nel
prodotto se l'identità risolta non è ProductionDcfr scaled. Il chunk `METRICS`
dell'archivio `.gtsd` usa lo schema interno v4 per conservare gli stessi campi;
i reader mantengono la compatibilità con v1-v3.

Il formato conserva il supporto di lettura per payload materializzati da
backend storici. Il profilo operativo 1.0 non usa mapping OS-page-backed: il
backend ammesso è `LazyInRam`, così il budget e il codec non cambiano durante
solve, checkpoint o resume.

## Checkpoint della certificazione HU preflop

> **Legacy** (nota del 2026-10-03). Questi sono i formati del preflop
> external sampling (`libs/preflop`), programma chiuso il 2026-09-15. I
> formati del solver preflop blueprint sono descritti altrove: checkpoint e
> policy binari del trainer, chart `gtosd.preflop_blueprint_chart.v1`
> (`schemas/preflop_blueprint_chart.schema.json`) e chart nel formato di
> MonkerSolver. Le fonti sono i report P6 e P8 in
> `docs/research/preflop_vector_cfr/` e l'Appendice E di
> `docs/solver-ui/WEB_UI_PROTOTYPE_PROMPT.md`. Nessuno di questi formati è un
> chunk `.gtsd`.

Il formato JSON `gtosd.hu_preflop_whole_game_coverage.v1` conserva lo stato
streaming della copertura Flop. Contiene fingerprint di tree, blueprint e piano
di decomposizione, target NashConv, un mask CO/BTN e una probabilità per ogni
task canonico, contatori, massa coperta, hash incrementale dello stato e catena
ordinata dei contributi. Dalla minor 1.3 conserva inoltre
`continuation_checkpoint_fingerprint`, identità globale del checkpoint
postflop condivisa da tutti i task e un hash locale per task, usato per
verificare che i due resolver provengano dallo stesso assemblaggio. Un hash
separato delle boundary produce l'identità del profilo completa, indipendente
dall'ordine di arrivo.

L'envelope su disco usa il marker
`GTOSD_HU_PREFLOP_WHOLE_GAME_COVERAGE_FILE`, lunghezza e checksum. La scrittura
usa un file sibling temporaneo e sostituzione atomica. Il caricamento controlla
schema, limiti, finitezza, fingerprint e coerenza interna; prima di riprendere o
finalizzare, il solver ricontrolla il checkpoint contro il tree e il catalogo
dei 5.157 task.

Questo checkpoint prova quali boundary sono state validate. Non contiene le
boundary CFV già consumate e non certifica la strategia senza una best response
globale exact entro il target dichiarato. L'evidenza BR 1.2 deve identificare lo
stesso profilo di continuazione; tree e blueprint da soli non bastano.

Le boundary Flop usano `gtosd.hu_preflop_flop_boundary.v2`. Separano il
fingerprint locale dell'assemblaggio del task dall'identità globale del
checkpoint postflop. Il ledger confronta la seconda; task diversi possono e
devono avere fingerprint locali diversi.

Gli accumulatori task-local River usano
`gtosd.hu_preflop_river_task_accumulator.v5`; gli aggregati usano
`gtosd.hu_preflop_river_task_aggregate.v5`. La versione v5 conserva sia il tag
obbligatorio `AverageStrategy` o `ExactBestResponse`, sia l'identità del
checkpoint ricevuta dalla prima boundary River. Ogni boundary successiva e
ogni terminale Flop/Turn devono dichiarare la stessa identità. I fingerprint
locali di boundary, accumulatore e aggregato restano distinti. Ogni
contributo ha già sollevato l'orbita del runout, permutato le combo private,
applicato la molteplicità del Flop e incluso la reach delle azioni postflop del
player di cui si sta accumulando la CFV. I payload v1 applicavano una
moltiplicazione scalare al solo board rappresentante; i v2 omettevano la reach
delle azioni proprie; i v3 non distinguevano valori di profilo e best response;
i v4 non conservavano l'identità globale del checkpoint.
Tutti vengono rifiutati come incompatibili. I formati v5 conservano
fingerprint, checksum e sostituzione atomica.

I terminali best-response Flop/Turn usano la minor 1.1 del payload runtime.
Oltre a tree, blueprint, catalogo e iterazione, registrano il fingerprint della
continuation che ha fornito le probabilità della strategia avversaria. Il
dispatcher richiede la stessa identità usata dalle boundary BR River e rifiuta
un terminale di un altro checkpoint anche quando il payload è internamente
valido e il fingerprint è stato ricalcolato.

La riduzione best-response può essere ripresa a quattro livelli. Il leaf
accumulator usa `gtosd.hu_preflop_river_best_response_leaf_accumulator.v1` e
conserva query completa, manifest dei root River ordinati, prossimo root da
consumare, 630 righe con somma compensata, identità della continuation e catena
dei contributi. I livelli superiori usano
`gtosd.hu_preflop_best_response_task_evaluation.v1`,
`gtosd.hu_preflop_best_response_entry_accumulator.v1` e
`gtosd.hu_preflop_best_response_entry_evaluation.v1`. Conservano lo stato
pubblico, la history completa, i valori per combo e per classe e le identità
immutabili necessarie a ricomporre e validare il risultato.

I quattro envelope usano rispettivamente i marker
`GTOSD_HU_PREFLOP_RIVER_BR_LEAF_ACCUMULATOR_FILE`,
`GTOSD_HU_PREFLOP_BR_TASK_EVALUATION_FILE`,
`GTOSD_HU_PREFLOP_BR_ENTRY_ACCUMULATOR_FILE` e
`GTOSD_HU_PREFLOP_BR_ENTRY_EVALUATION_FILE`, con checksum e sostituzione
atomica. Il leaf viene committato solo dopo che il checkpoint sink ha accettato
il candidato: un errore conserva l'ultimo root completo e il resume parte dal
successivo. Questi formati riducono il lavoro perso dopo un'interruzione; non
riducono il costo di calcolo, l'errore di astrazione o la NashConv.

## Container `.gtsd`

Il layout fisico è:

```text
HEADER | CHUNK_INDEX | ENCRYPTED_CHUNKS | FOOTER
```

I chunk tipizzati sono `CONFIG`, `TREE`, `ISOMORPHISM`, `STRATEGY`, `EV`,
`RANGES`, `NODELOCKS`, `METRICS` e `DICTIONARY`. L'indice registra offset,
dimensione raw/compressa/cifrata e uso del dizionario. I feature bit dichiarano
chunking, Zstandard, secretstream, random access e strategia exact o quantizzata.

## Integrità e riservatezza

Ogni chunk viene compresso con Zstandard e cifrato/autenticato con uno stream
XChaCha20-Poly1305 indipendente. L'indice è autenticato; offset, dimensioni,
magic, versione, feature e footer vengono validati prima di allocare payload
significativi. Bit flip, file troncati, chunk duplicati o mancanti e chiave
errata producono errori classificati.

La chiave a 32 byte non è inclusa nell'archivio. Il formato protegge i dati a
riposo, ma non promette protezione dopo che il processo li ha decifrati in RAM.

## Scrittura atomica

Il writer:

1. crea un temporary sibling;
2. scrive e chiude l'archivio;
3. lo riapre, autentica e decomprime;
4. sostituisce atomicamente la destinazione.

Un errore lascia intatta l'ultima destinazione valida. La migrazione scrive su
un path distinto e non modifica la sorgente.

## Random access e catalogo

`open_solution` carica header e indice; `read_solution_chunk` apre il solo chunk
richiesto. `verify_solution` forza invece autenticazione e decompressione di
tutti i chunk.

Il catalogo `.gtsddb` SQLite è esterno e contiene path, fingerprint, size, mtime
e normalized NashConv. Non è parte della catena di integrità del `.gtsd` e non
è richiesto per aprire una soluzione.

## Precisione e compatibilità

Il percorso benchmark production usa `ScaledUint16RegretStrategy`: codici
uint16 action-major per regret signed e average strategy, con una scala float32
per decision node e payload. Il codec e' numericamente lossy rispetto a uno
stato real-valued, ma e' un formato production versionato e qualificato tramite
dEV, root EV, resume byte-equivalent, finiteness e cinque processi. La dicitura
`exact outcomes` significa enumerazione completa degli outcome del gioco
discretizzato, non identita' numerica con `Float64`. Zstandard resta lossless
rispetto ai byte del payload scelto.

Una major sconosciuta viene rifiutata. Feature sconosciute vengono rifiutate
prima della lettura dei chunk. Una minor compatibile può essere accettata solo
se i campi aggiunti hanno semantica definita. Ogni migrazione deve verificare
che source e destination rappresentino lo stesso gioco e conservare la source.

## Dati futuri

Nel container `.gtsd`, node lock di prodotto, preflop e multiway richiederanno
payload versionati. Il
chunk `NODELOCKS` esistente riserva il tipo, ma non dimostra che il node locking
globale sia implementato. Nessun reader deve trasformare `none` in una strategia
vincolata o viceversa.

R2-S usa per ora due sidecar di laboratorio distinti dal container `.gtsd`:
`GTOSD_CARD_ABSTRACTION 1 0` e `GTOSD_SUBGAME_BOUNDARY 1 1`, accompagnato da
`GTOSD_PUBLIC_SUBGAME 1 0`. Il probe postflop coarse usa inoltre il checkpoint
binario `GTOSD_POSTFLOP_BUCKET_1`, versione 1.0, con fingerprint separati per
gioco e astrazione, algoritmo ProductionDcfr, stato `float64` e checksum. Hanno
versione e fingerprint propri, ma non sono
ancora chunk di soluzione production. Un checkpoint abstract identifica il
gioco trasformato e non può essere ripreso come exact; un boundary identifica
gioco e blueprint. I sidecar public-state/boundary sono avvolti da checksum e
sostituzione atomica nello stesso filesystem. L'integrazione futura richiede
comunque chunk `.gtsd` autenticati e migrazione esplicita: l'envelope di
laboratorio non è un formato production promosso.

La diagnostica HU preflop legacy (external sampling) può produrre il sidecar JSON
`gtosd.hu_preflop_root_decision_trace.v1`. Il file conserva gli identificatori
di algoritmo, albero, astrazione ed evaluator, oltre ai seed; per ogni classe registra stato
di training e valutazioni paired delle cinque azioni root. I rami e i bucket
sono aggregati per evitare l'export dei singoli deal. Il sidecar non contiene lo
stato necessario per riprendere il solve e non certifica convergenza.

Il layout postflop espone inoltre `exact_identity` 1.0 come policy implicita.
Poiché è l'identità verificata, non aggiunge payload né cambia il fingerprint o
i byte del checkpoint ProductionDcfr v2; i checkpoint esistenti rappresentano
già questa semantica exact. `made_hand_value` 1.0 ha fingerprint, mapping e
checkpoint di laboratorio distinti. Il checkpoint bucket non può essere
ripreso come exact e non è incorporato nel container `.gtsd`; una promozione
futura dovrà serializzare policy, mapping, stato e schema di lift come chunk
autenticati.
