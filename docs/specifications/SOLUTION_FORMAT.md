# Formato delle soluzioni

## Due livelli di persistenza

GTOSD distingue:

- checkpoint postflop 1.0: stato di calcolo necessario a pausa e resume;
- soluzione `.gtsd` 1.0: archivio autenticato, compresso, indicizzato e adatto
  al caricamento di prodotto.

Un checkpoint non sostituisce una soluzione verificata. Una soluzione conserva
config, range, checkpoint e certificazione coerenti.

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

Node lock di prodotto, preflop e multiway richiederanno payload versionati. Il
chunk `NODELOCKS` esistente riserva il tipo, ma non dimostra che il node locking
globale sia implementato. Nessun reader deve trasformare `none` in una strategia
vincolata o viceversa.
