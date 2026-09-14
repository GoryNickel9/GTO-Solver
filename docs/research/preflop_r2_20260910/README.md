# R2 — Sampling e averaging verificati

## Stato

`PASS` per il gate R2 della roadmap del 10 settembre 2026.

Il trainer preflop e il solver generico applicano ora una convenzione external-sampling esplicita e condivisa. Il cambiamento non modifica `ProductionDcfr` né i default del solver postflop standalone.

## Contratto matematico

Ogni iterazione HU esegue un passaggio per traverser. Al nodo del traverser si enumerano le azioni e si aggiornano i regret; ai nodi dell'avversario si campiona un'azione. La strategia media viene accumulata soltanto nel passaggio in cui l'attore è l'avversario del traverser:

```text
S_i(I,a) += w_t * sigma_i(I,a)
```

La probabilità di visitare quel nodo contiene già la reach del giocatore `i`. Moltiplicare ancora per `reach[i]` produce lo stimatore distorto precedente. Nel trainer one-sided della best response non esiste il passaggio dell'avversario: soltanto in quel percorso la strategia del traverser viene pesata esplicitamente con la sua reach.

Le varianti implementate sono:

| Identificatore | Regret | Strategia media | Stato |
|---|---:|---:|---|
| `external_sampling_v2_opponent_pass_average` | peso 1 | peso 1 | baseline verificata |
| `linear_mccfr_v1_opponent_pass_average` | peso `t` | peso `t` | challenger esplicito |
| `external_sampling_dcfr_1.5_0_3_alternating_v2_opponent_pass_average` | DCFR 1,5/0/3 lazy | DCFR lazy | compatibilità sperimentale, non baseline |

Per DCFR, `dcfr_1503_lazy_discount` riproduce il prodotto degli aggiornamenti densi fra l'ultima visita e l'iterazione corrente. I test coprono gap di 0, 1, 10 e 100 iterazioni. Questo verifica l'equivalenza aritmetica dello schedule; non qualifica DCFR come scelta finale.

## Implementazione

- `include/gtosd/core/external_sampling.hpp` contiene il contratto comune dell'averaging e il discount lazy.
- `libs/solver/src/solver.cpp` corregge il solver MCCFR generico e aggiunge `LinearMccfr` senza cambiare i valori numerici degli algoritmi esistenti.
- `libs/preflop/src/hu_preflop_solver.cpp` applica lo stesso contratto alle traversate preflop, postflop e di refinement.
- `benchmarks/hu_preflop_solve.cpp` espone `--algorithm external_sampling|linear_mccfr|discounted_mccfr_1.5_0_3`.
- Il formato del checkpoint del solver generico passa a minor version 2 per identificare il nuovo algoritmo.

## Validazione

Il controesempio algebrico riproduce la media corretta `1/10` e la vecchia media distorta `1/22`. Con pesi lineari conserva il target razionale `8/251`. Sono coperti reach nulla, azioni a probabilità zero, normalizzazione, resume byte-equivalente e rifiuto del parallelismo non validato per Linear MCCFR.

Seed di test: `0x5232415645524147`.

| Gioco | Algoritmo | Curva iterazioni → NashConv esatta | Soglia finale | Esito |
|---|---|---|---:|---|
| Kuhn | External sampling | 10.000: 0,0131033; 50.000: 0,0092986; 100.000: 0,00556635 | `< 0,05` | `PASS` |
| Kuhn | Linear MCCFR | 10.000: 0,0165206; 50.000: 0,00874448; 100.000: 0,00559391 | `< 0,05` | `PASS` |
| Leduc | External sampling | 5.000: 0,535231; 25.000: 0,206098; 50.000: 0,133575 | `< 0,35` | `PASS` |
| Leduc | Linear MCCFR | 5.000: 0,458179; 25.000: 0,186295; 50.000: 0,124887 | `< 0,35` | `PASS` |
| Short Deck River toy | External sampling | 5.000: 0,000924379; 25.000: 0,000185202; 50.000: 0,0000924747 | `< 0,08` | `PASS` |

CTest Release:

```text
gtosd_external_sampling_tests      PASS
gtosd_hu_preflop_sampling_tests    PASS
gtosd_phase2_tests                 PASS
3/3 test passed
```

Il test autonomo preflop esegue inoltre tutte e tre le modalità su deal fisici, valida 81 righe root normalizzate, blueprint e policy esportata. Un enum algoritmo sconosciuto restituisce `InvalidConfiguration`.

## Decisioni e limiti

External sampling v2 è la baseline di correttezza per R2. Linear MCCFR resta il primo challenger. DCFR 1,5/0/3 rimane disponibile per riproducibilità, ma non riceve alcun vantaggio presunto dalle verifiche di questa fase.

Le NashConv riportate riguardano giochi ridotti enumerabili. Non sono una certificazione della fixture HU CO40 completa e non autorizzano claim GTO sul gioco fisico.

## Prossima fase

R2.1 deve confrontare per 15–30 minuti la baseline corretta con un prototipo di astrazione a budget rigido, usando seed di partizione, training e valutazione separati.
