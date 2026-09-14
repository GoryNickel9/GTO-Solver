# R2.1 — Fattibilità iniziale dell'astrazione

## Stato

`PASS` per il gate R2.1. Il prototipo offre un compromesso misurato: riduce di circa il 75% gli infoset e il payload numerico minimo a 100.000 iterazioni, ma aumenta il tempo del 20–22% perché calcola le feature online. Questo risultato giustifica il passaggio a R3 e R4, dove payoff e mapping devono uscire dall'hot path.

I criteri sono stati congelati prima dei run nel [protocollo](EXPERIMENT_PROTOCOL.md). Nessun risultato Monker è stato usato per costruire la partizione.

## Implementazione

La rappresentazione `DistributionalStrengthPrototype`:

- campiona deal fisici compatibili e applica il mapping dopo il card removal;
- usa soltanto hole card e board visibili alla street corrente;
- riassume categoria corrente, equity, varianza dell'esito, categoria futura dominante, texture paired/suited e forza contro tre gruppi di categoria avversaria;
- applica una mappa congelata e totale con capacità 256/1.024/4.096 al Flop/Turn/River;
- conserva la betting history, ma dimentica classe preflop e bucket delle street precedenti nel postflop; questa memoria imperfetta è dichiarata nell'identificatore;
- separa `partition_seed`, `seed` di training ed `evaluation_seed`.

Il formato della policy campionata passa da minor 0 a minor 1 e include il seed della partizione nel payload e nel fingerprint. La query autonoma ricostruisce lo stesso bucket senza leggere il solver postflop legacy.

## Test ridotti

`gtosd_hu_preflop_sampling_tests` passa con 359 asserzioni. Le nuove asserzioni verificano:

- determinismo a seed fissato;
- invarianza rispetto a una permutazione globale dei semi;
- identico bucket Flop quando cambiano Turn e River effettivi del deal;
- limiti di capacità per ogni street;
- solve, normalizzazione, fingerprint e registrazione dei tre seed.

## Esperimenti CO40

Tutti i processi usano external sampling v2, 100.000 iterazioni, 20.000 deal di valutazione, 5.000 iterazioni di risposta e 10.000 deal per risposta. Il limite prefissato era 900 s e 12 GiB per processo.

| Variante | Replica | Solve / wall | Peak working set | Infoset | Payload minimo | EV root ± IC 95% |
|---|---:|---:|---:|---:|---:|---:|
| Fisica exact | 1 | 44,57 / 46,65 s | 2.222.964.736 B | 10.824.368 | 1.558.708.992 B | 0,49566 [0,18539; 0,80593] |
| Distribuzionale | 1 | 53,50 / 54,00 s | 625.573.888 B | 2.687.053 | 386.935.632 B | 0,94834 [0,61635; 1,28033] |
| Fisica exact | 2 | 44,23 / 46,44 s | 2.214.551.552 B | 10.778.457 | 1.552.097.808 B | 0,55966 [0,24128; 0,87803] |
| Distribuzionale | 2 | 53,94 / 54,43 s | 633.462.784 B | 2.731.523 | 393.339.312 B | 0,86867 [0,53825; 1,19908] |

La candidata usa il 24,82% e il 25,34% degli infoset della baseline nelle due repliche. Le differenze assolute di EV root sono 0,45268 e 0,30901 ante; gli intervalli si sovrappongono. Entrambe le repliche rispettano i criteri prefissati.

## Decisioni e limiti

La partizione distribuzionale passa come direzione di lavoro, non come astrazione qualificata. Il test non dimostra che i bucket preservino una best response fisica né che la policy corrisponda ai range di riferimento. La metrica `normalized_abstract_nashconv=0` è una risposta appresa campionata e non viene interpretata come certificazione.

Il costo online delle feature annulla il vantaggio di throughput. R3 deve ridurre il costo dell'evaluator; R4 deve precompilare betting e limitare cache e stato. R5 deciderà la capacità definitiva confrontando anche una rappresentazione più fine.

## Artefatti

- `exact_seed1.json`, `exact_seed2.json`;
- `distributional_seed1.json`, `distributional_seed2.json`;
- `EXPERIMENT_PROTOCOL.md`.

## Prossima fase

R3 deve introdurre la cache del vincitore e il backend tabellare exact opzionale, con equivalenza esaustiva sulle 8.347.680 combinazioni a sette carte.
