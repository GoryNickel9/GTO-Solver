# Performance

## Principio di misura

Le prestazioni vengono ottimizzate solo dopo profiling. Ogni misura registra
hardware, OS, compiler, flags, commit, thread, precisione, fixture, seed e
criterio di convergenza. Un miglioramento che altera gioco o accuratezza viene
rifiutato.

## Metriche

Il solver pubblica almeno:

- tempo layout, inizializzazione, traversal, apply regret, certificazione,
  finalizzazione e totale;
- iterazioni e nodi attraversati;
- physical/canonical nodes, infoset e actions;
- normalized NashConv o maximum deviation;
- byte di regret e strategy sum;
- transient workspace e peak RSS come metriche separate.

Peak RSS non sostituisce la memoria solver dichiarata da GTO+. Cache e layout
preparato non possono essere inclusi da un lato e esclusi dall'altro.

## Fixture di parità

`GTP-AHKHQH-003` è il benchmark comparativo corrente. Lo script
`tools/run_gto_plus_convergence_benchmark.ps1` avvia processi indipendenti e
produce report versionati. Il confronto primario usa la mediana di cinque run e
pubblica anche p95 e ogni campione.

Baseline documentata:

- GTO+ operativo: 1,71 s a dEV 0,98%, memoria solver 8 MB;
- GTOSD: mediana 2,7197086 s, p95 2,8812251 s;
- GTOSD state `Float32`: 4.214.976 byte;
- gate tempo `<=1,900000 s`: FAIL;
- gate memoria `<=8.888.889 byte`: PASS.

Il tentativo GTO+ a target 0,10% è censurato a `>245 s` e non sostituisce il
riferimento operativo senza ridefinire l'intero protocollo.

## Profiling

Le aree misurate separatamente sono traversal, certificazioni exact BR,
inizializzazione/riduzione dei buffer, allocazioni, cache, contention e I/O.
Ogni ottimizzazione significativa registra prima/dopo sullo stesso gate.

Le ottimizzazioni lossless già disponibili includono combo attive, public DAG,
isomorfismo globale, stato `Float32` con calcolo `Float64`, parallelismo per
action subtree e riuso del prepared tree. La validazione differenziale resta
obbligatoria.

## Benchmark non comparativi

La suite interna misura evaluator, tree build, CFR traversal, best response,
checkpoint, storage, scaling multicore e GUI. Questi benchmark individuano
regressioni, ma non dimostrano parità GTO+ se fixture o timer differiscono.

## Regole di decisione

- confrontare distribuzioni, non un solo best run;
- non mediare tempo e memoria in un punteggio compensatorio;
- non riutilizzare involontariamente checkpoint o cache tra processi;
- non cambiare certification interval per nascondere il costo senza dichiararlo;
- non chiamare “convergenza” una singola iterazione più certificazione;
- conservare report grezzi insieme al riepilogo.

Il prossimo intervento prestazionale standard resta bloccato dalla diagnosi
F10.4, così il profiling non ottimizza un percorso ancora ambiguo.
