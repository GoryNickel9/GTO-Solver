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

`GTP-AHKHQH-003` è il benchmark comparativo corrente (specifica v1 congelata).
Lo script `tools/run_gto_plus_convergence_benchmark.ps1` avvia processi
indipendenti e produce report versionati. Il confronto primario usa la mediana
di cinque run e pubblica anche p95 e ogni campione.

La specifica generica v2 (`gtosd.gto_plus_convergence_benchmark.v2`) permette
di registrare nuovi benchmark di convergenza senza modifiche al codice: board,
range, stack-to-pot, sizing, raise depth e nodi di riferimento EV/frequenze
sono letti dalla fixture (`benchmarks/fixtures/gto_plus_ahkhqh_101.json` è la
validazione v2 dello scenario 003). Ogni nuovo benchmark mantiene il proprio
riferimento GTO+ e i propri gate.

Baseline documentata (re-baseline 2026-08-05, regola all-in naturale):

- GTO+ operativo: 1,71 s a dEV 0,98%, memoria solver 8 MB;
- GTOSD: mediana 3,128 s, p95 3,271 s (cinque run, albero 165.774 nodi);
- GTOSD state `Float32`: 6.677.088 byte (più vicino agli 8 MB GTO+ del
  precedente 4.214.976 byte);
- gate tempo `<=1,900000 s`: FAIL;
- gate memoria `<=8.888.889 byte`: PASS;
- gate correttezza (`correctness_gate`, EV del nodo root): PASS dal 2026-08-02;
  gli EV BTN condizionali e le frequenze restano diagnostica (con il root lock
  F10.4 i delta BTN scendono a +0,0348 / +0,0366 ante,
  vedi GTO_PLUS_PARITY_JOURNEY.md).

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
