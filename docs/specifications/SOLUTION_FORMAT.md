# Formato delle soluzioni

## Due livelli di persistenza

GTOSD distingue:

- checkpoint postflop 1.0: stato di calcolo necessario a pausa e resume;
- soluzione `.gtsd` 1.0: archivio autenticato, compresso, indicizzato e adatto
  al caricamento di prodotto.

Un checkpoint non sostituisce una soluzione verificata. Una soluzione conserva
config, range, checkpoint e certificazione coerenti.

## Manifest card abstraction e subgame

`CardAbstraction` usa il formato testuale `GTOSD_CARD_ABSTRACTION 1 0`. Conserva
kind, granularità, schema delle feature, information set, partizione, combo,
board mask, reach weight e feature con bit IEEE esatti. Il reader ricostruisce
l'astrazione e rifiuta un fingerprint divergente. L'identità del gioco CFR
include il fingerprint dell'astrazione.

Le feature preparatorie possono essere persistite separatamente nel manifest
`GTOSD_CARD_ABSTRACTION_FEATURE_CACHE 1 0`. L'header dichiara schema,
fingerprint della sorgente, fingerprint della cache, numero di partizioni e
osservazioni; ogni osservazione conserva player, combo, board mask, reach
weight e feature tramite bit IEEE esatti. Le righe sono in ordine canonico e
il reader impone limiti prima dell'allocazione, ricalcola il fingerprint e
rifiuta trailing data o contenuto corrotto. Il salvataggio usa temporary
sibling e replace atomico.

Il manifest è intenzionalmente indipendente da `buckets_per_partition`: più
configurazioni k-means possono riusare gli stessi dati esatti. Il fingerprint
della cache documenta la provenienza nel report, ma non cambia il fingerprint
del gioco quando le osservazioni sono semanticamente identiche a quelle
generate direttamente.

Il checkpoint prodotto da un subgame conserva il fingerprint del gioco
astratto e del frontier reach-weighted. La strategia distribuita e la metrica
full-game appartengono al risultato di resolving: non è valido riaprire quel
checkpoint contro un frontier, un blueprint o un'astrazione differenti.

Nel postflop nativo il fingerprint del checkpoint include configurazione,
feature schema e assegnazioni combo→bucket. Il report JSON/Markdown della CLI
conserva inoltre kind, bucket richiesti, fingerprint, compression ratio,
weighted MSE, cache riusata e tempi separati di feature/clustering. Il resume
richiede di nuovo lo stesso config, gli stessi range e
la stessa configurazione di astrazione; un checkpoint exact o un bucket count
diverso viene rifiutato prima del traversal.

`resolve-bucketed` scrive sempre un checkpoint di destinazione distinto con lo
stesso fingerprint del blueprint e con i soli segmenti accettati aggiornati.
Il report sidecar `gtosd.postflop.bucketed-subgame-resolution.v1` conserva path,
root canonica, reach, iterazioni locali, byte del rollback, certificazioni e
decisione candidate/fallback. Il clock globale del checkpoint non incorpora le
iterazioni locali. Il sidecar non è ancora autenticato insieme al checkpoint:
per audit e packaging `.gtsd` va conservato come artefatto associato esplicito.
Un successivo `resume-bucketed` è quindi un warm start CFR+ dal profilo
distribuito, non la continuazione bit-identica della traiettoria full-game
precedente; questa distinzione va mantenuta nel report di sessione.

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

Un solve con target di working set esplicito può conservare temporaneamente lo
stesso payload scaled-uint16 in una mapping OS-page-backed. L'owner runtime non
fa parte dell'identità serializzata e le API di query lo leggono senza cambiare
codec. Prima di scrivere checkpoint o `.gtsd`, il chiamante deve invocare la
materializzazione esplicita: i quattro array risultanti devono essere
byte-identici al backend residente e l'operazione può aumentare il working set
dell'intera dimensione logica dello stato.

## Container `.gtsd`

Il layout fisico è:

```text
HEADER | CHUNK_INDEX | ENCRYPTED_CHUNKS | FOOTER
```

I chunk tipizzati sono `CONFIG`, `TREE`, `ISOMORPHISM`, `STRATEGY`, `EV`,
`RANGES`, `NODELOCKS`, `METRICS` e `DICTIONARY`. Finché non esiste un chunk
`ABSTRACTION` versionato nel container `.gtsd`, un checkpoint postflop nativo
bucketed resta riprendibile e verificabile tramite config/range/abstraction
esterni e report, ma non è ancora impacchettabile come soluzione `.gtsd`
autosufficiente. L'indice registra offset,
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
