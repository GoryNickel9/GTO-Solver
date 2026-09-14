# R6 — Gate v7 dei continuation rollout multipli al root

## Esito

`ENGINEERING_PASS / VARIANCE_REDUCTION_PASS / 2M_CHALLENGER_PASS / R6_GATE_FAIL`.

Lo screen 250k fallisce: quattro continuation rollout riducono del `50,52%` l'errore standard mediano, ma la TV tra seed sale da `35,3724` a `37,4164 pp` e la WMAE media contro Monker peggiora da `17,9530` a `18,9918 pp`.

Una successiva eccezione richiesta dall'utente cambia il verdetto sul challenger a 2M. La TV tra seed scende da `27,9000` a `16,6353 pp`, il SE mediano scende del `51,30%` e la WMAE media contro Monker migliora da `18,2585` a `16,9571 pp`. K=4 supera quindi la conferma comparativa a 2M, ma non il gate R6 di 1 pp e non costituisce una soluzione qualificata.

## Implementazione

Il runner accetta `--root-action-value-rollouts`, da uno a otto. Il default resta uno. Con un valore maggiore di uno:

1. il trainer esegue la traversata primaria esistente;
2. campiona deal fisici indipendenti condizionati alla stessa classe CO;
3. valuta ogni azione root con RNG di continuazione separato e policy congelata al batch;
4. scarta tutti i delta prodotti dalle traversate shadow;
5. aggiorna il regret root con la media dei valori d'azione.

Il sampler copre tutte le 81 classi, sceglie uniformemente una combo fisica della classe richiesta e completa mano avversaria e board senza collisioni. La modalità richiede batch External Sampling o Linear MCCFR e rifiuta il public-board sampling. Il cap è otto rollout.

Lo scratch shadow è limitato dallo stesso cap per-job delle traversate primarie. Il risultato registra `root_action_value_rollouts` e `peak_parallel_shadow_updates_per_worker`; l'ID dell'algoritmo riceve il suffisso `root_action_rollouts4_v1`.

## Non interferenza

Una replay v7 seed 1 a 250k con un rollout coincide bit per bit con il risultato precedente:

- strategia root;
- regret e strategy sum;
- telemetria del vantaggio d'azione;
- root EV;
- `615.167` infoset.

Il picco shadow resta zero. Un test separato a una iterazione confronta uno e quattro rollout: le entry e le probabilità della policy postflop sono identiche. Le traversate extra non aggiornano quindi lo stato di training profondo.

## Screen 250k

| Modalità | Seed | SE mediano prima/seconda | Classi sotto 2 SE | WMAE | TV contro Monker | Infoset | Solve |
|---|---:|---:|---:|---:|---:|---:|---:|
| 1 rollout | 1 | 0,331a | 74/81 | 19,3262 pp | 48,3154 pp | 615.167 | 44,249 s |
| 1 rollout | 2 | 0,323a | 74/81 | 16,5799 pp | 41,4497 pp | 614.781 | 38,937 s |
| 4 rollout | 1 | 0,162a | 62/81 | 18,1999 pp | 45,4998 pp | 609.448 | 163,262 s |
| 4 rollout | 2 | 0,162a | 65/81 | 19,7836 pp | 49,4591 pp | 625.719 | 163,520 s |

| Gate | Richiesto | Osservato | Esito |
|---|---:|---:|---|
| Riduzione SE mediano | almeno 35% | **50,52%** | PASS |
| Riduzione TV tra seed | almeno 15% | da 35,3724 a 37,4164 pp, **+5,78%** | FAIL |
| Peggioramento WMAE media | massimo 1 pp | **+1,0387 pp** | FAIL |
| Costo wall | massimo 4× | media 3,93×, run concorrenti | INCONCLUSIVE |
| Ricostruzione regret | entro arrotondamento | errore massimo `0` | PASS |
| Scratch | entro 512 MiB | 1.619.208 / 1.739.208 B | PASS |

Le run a quattro rollout sono state eseguite insieme, mentre le baseline sono state eseguite singolarmente. Il rapporto wall non è quindi una misura controllata.

La discordanza della strategia corrente scende da 46 a 42 classi, ma la strategia media si allontana tra seed. Per AA il margine `Call - Raise 6a` passa da `+1,084 ± 0,437a` a `+1,435 ± 0,224a` nel seed 1 e da `+0,558 ± 0,435a` a `+0,693 ± 0,222a` nel seed 2. Il controllo-varianza risolve meglio AA a 250k, ma non generalizza alle 81 classi.

## Conferma 2M richiesta dall'utente

La conferma usa la stessa fixture, i medesimi seed, Linear MCCFR batch 32, otto worker, MC8 e bucket `32/128/512`. L'unica differenza rispetto alla baseline v7 è K=4.

| Modalità | Seed | SE mediano prima/seconda | Classi sotto 2 SE | WMAE | TV contro Monker | Infoset | Solve |
|---|---:|---:|---:|---:|---:|---:|---:|
| 1 rollout | 1 | 0,0892a | 57/81 | 18,3298 pp | 45,8246 pp | 878.669 | 476,467 s |
| 1 rollout | 2 | 0,0843a | 62/81 | 18,1872 pp | 45,4680 pp | 878.331 | 477,106 s |
| 4 rollout | 1 | 0,0404a | 44/81 | 17,6745 pp | 44,1863 pp | 881.212 | 1.347,806 s |
| 4 rollout | 2 | 0,0440a | 49/81 | 16,2397 pp | 40,5993 pp | 877.419 | 1.348,347 s |

| Metrica a due seed | Baseline K=1 | Challenger K=4 | Variazione |
|---|---:|---:|---:|
| TV strategia media | 27,9000 pp | **16,6353 pp** | −40,38% |
| TV strategia corrente | 30,9242 pp | **21,6081 pp** | −30,13% |
| Azione corrente discordante | 27 classi / 192 combo | **17 classi / 114 combo** | −10 classi / −78 combo |
| SE mediano medio | 0,0867a | **0,0422a** | −51,30% |
| Classi sotto 2 SE | 57 / 62 | **44 / 49** | miglioramento entrambi i seed |
| WMAE media contro Monker | 18,2585 pp | **16,9571 pp** | −1,3014 pp |
| TV media contro Monker | 45,6463 pp | **42,3928 pp** | −3,2535 pp |
| Solve medio | 476,786 s | 1.348,076 s | 2,83×, misura non controllata |

Il passaggio da 250k a 2M riduce la TV tra seed del challenger del `55,54%`, da `37,4164` a `16,6353 pp`. Il risultato smentisce l'estrapolazione dallo screen corto: la maggiore precisione degli update root diventa visibile nella strategia media soltanto sul budget lungo.

Il confronto con Monker resta diagnostico e viene respinto come certificazione con `REFERENCE_CONFIG_INCOMPLETE`. Anche il P95 per classe resta quasi saturo, `98,7272` e `98,0979 pp`: il miglioramento medio non elimina gli outlier.

Per AA, K=4 sceglie Call in entrambi i seed e la strategia media assegna al Call `99,9645%` e `99,9998%`. Il margine appaiato `Call - Raise 6a` è però `+0,0542 ± 0,0697a` nel seed 1 e `+0,2184 ± 0,0678a` nel seed 2. Il primo seed non distingue le due azioni a 2 SE; il secondo sì. La preferenza comune per Call è più stabile, ma il valore preciso del margine non è ancora replicato.

Le due run registrano rispettivamente 881.212 e 877.419 infoset, 130.856.544 e 130.253.472 byte di stato numerico, e 2.382.608 e 2.054.208 byte di scratch. La ricostruzione del regret ha errore massimo zero in entrambe.

## Validazione

Build MSVC Release `/W4 /WX`: PASS.

```text
gtosd_hu_preflop_trainer_isolation_tests   PASS
gtosd_hu_preflop_trainer_dependency_check PASS
gtosd_hu_preflop_sampling_tests            PASS
gtosd_hu_preflop_compiled_tests            PASS
gtosd_hu_preflop_abstraction_tests         PASS
gtosd_hu_preflop_parallel_tests            PASS
gtosd_hu_preflop_preflight_tests           PASS
gtosd_hu_preflop_tree_tests                PASS
gtosd_hu_preflop_tests                     PASS
gtosd_hu_preflop_trainer_tests             PASS
gtosd_hu_preflop_decomposition_tests       PASS
11/11 passed in 492,35 s
```

Il test parallel verifica inoltre:

- determinismo e assenza di collisioni per quattro deal di ciascuna classe;
- rifiuto di classe, rollout e sampling incompatibili;
- assenza di update shadow nella policy postflop;
- identità numerica con uno e otto worker;
- limite e osservabilità dello scratch shadow.

I confronti esterni sono `REJECTED / REFERENCE_CONFIG_INCOMPLETE`, come previsto dal gate generale.

SHA-256:

- screen 250k seed 1: `193B267CB38483656EDB04BC294329EFB94C1DA5ADD9C1CD64915C2FACA1FA69`;
- screen 250k seed 2: `EA867B51AAD202B5185ABD800E9279DA95E54A187533212B9099238E2AFB993D`;
- conferma 2M seed 1: `E4454D3F541D9D39B4B976E5F41671D98F3198F7A6086CB3F6339C9701156709`;
- conferma 2M seed 2: `8007849A4F9EB71EA5E649E59D48CF8C48B4A211B96110BA14171243E24051A0`.

## Decisione

Il challenger K=4 resta disattivato per default, ma supera la conferma comparativa a 2M e rimane il candidato di ricerca successivo. La conclusione dello screen 250k non regge al budget lungo: dimezzare il rumore root stabilizza la policy media quando l'averaging dispone di abbastanza iterazioni.

Non viene ancora promosso. Una TV tra seed di `16,6353 pp` è migliore della baseline ma resta oltre sedici volte il gate finale. Le traversate shadow non addestrano gli infoset profondi, la comparabilità Monker è incompleta e non esiste una misura certificata di NashConv. Il prossimo aumento di budget deve essere deciso da una proiezione misurata sulla traiettoria K=4, non dalla sola riduzione dell'SE.
