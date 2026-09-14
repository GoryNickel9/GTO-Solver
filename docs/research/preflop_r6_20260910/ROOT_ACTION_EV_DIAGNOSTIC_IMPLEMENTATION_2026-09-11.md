# Diagnostica EV per azione alla root CO

## Esito

`IMPLEMENTATION_PASS / MEASUREMENT_PASS / R6_GATE_FAIL`.

Il runner ora esporta, per tutte le 81 classi e per ognuna delle cinque azioni root, EV in ante, errore standard e numero di campioni. Due repliche da 500.000 iterazioni con 200.000 deal di valutazione completano senza errori e conservano la strategia prodotta prima della diagnostica.

La misura cambia la diagnosi, non il gate: gli EV per azione sono molto più vicini a Monker delle frequenze, ma la candidata 4M resta a 15,7661 pp di errore medio per azione. R6 non passa e R7 non viene avviata.

## Contratto implementato

Per ogni deal fisico indipendente dal training, il valutatore:

1. identifica la classe esatta della mano CO;
2. forza una volta ciascuna azione root: all-in, raise-to 6a, raise-to 10a, call e fold;
3. prosegue con la strategia media dello stesso solve per entrambi i giocatori;
4. accumula media, errore standard e campioni per classe e azione;
5. usa stream pseudo-casuali distinti e deterministici per le cinque continuazioni.

Le cinque azioni condividono lo stesso insieme di deal fisici. La valutazione non legge il riferimento Monker e non modifica regret, somme della strategia o training. Non è una best response e non certifica NashConv.

Il JSON aggiunge:

- `root_action_ev.<combo>.<action>.ev_ante`;
- `root_action_ev.<combo>.<action>.standard_error_ante`;
- `root_action_ev.<combo>.<action>.samples`;
- `root_action_ev_scope=forced_root_action_then_sampled_average_policy_continuation_common_physical_deals`.

## File modificati

- `include/gtosd/preflop/hu_preflop.hpp`: payload tipizzato del risultato;
- `libs/preflop/src/hu_preflop_solver.cpp`: valutazione forzata delle azioni, mapping canonico unico e stima dell'incertezza;
- `benchmarks/hu_preflop_solve.cpp`: export JSON della diagnostica;
- `tests/hu_preflop_sampling_tests.cpp`: finitezza, errore standard non negativo, copertura dei deal e stato zero-campioni;
- `tests/hu_preflop_parallel_tests.cpp`: riproducibilità bit-a-bit fra worker;
- `tests/hu_preflop_compiled_tests.cpp`: equivalenza fra betting compilato e reference.

## Run fisici

Contratto comune: Linear MCCFR, 500.000 iterazioni, batch 32, 8 thread, partizione `32/128/512`, MC8, evaluator oracle, 200.000 deal di valutazione e 10.000 iterazioni di risposta appresa.

| Run | EV CO ± SE | Solve | Peak private | Campioni per classe e azione |
|---|---:|---:|---:|---:|
| Seed 1 | 0,0289902 ± 0,0369830 ante | 115,352 s | 371.867.648 B | 1.207–3.919 |
| Seed 2 | −0,0219802 ± 0,0362167 ante | 116,915 s | 374.714.368 B | 1.208–3.966 |

Ogni azione riceve esattamente 200.000 osservazioni. Il fold vale −1,000 ante con errore standard zero in tutte le 81 classi. Il confronto con i vecchi JSON 500k conferma che le strategie root non cambiano.

## Confronto degli EV con Monker

Le medie sono pesate sulle 630 combo fisiche. `WMAE` e distanza fra seed sono espresse in ante, non in punti percentuali di frequenza.

| Azione | EV Monker | EV seed 1 | EV seed 2 | Bias medio | WMAE medio | WMAE fra seed |
|---|---:|---:|---:|---:|---:|---:|
| All-in | −0,69817 | −0,72711 | −0,65415 | +0,00754 | 0,47357 | 0,50478 |
| Raise-to 6a | −0,47868 | −0,56582 | −0,46583 | −0,03715 | 0,69296 | 0,55859 |
| Raise-to 10a | −0,43016 | −0,73030 | −0,59433 | −0,23216 | 0,83341 | 0,45802 |
| Call | −0,40157 | −0,30944 | −0,30170 | +0,09601 | 0,51898 | 0,48792 |
| Fold | −1,00000 | −1,00000 | −1,00000 | 0,00000 | 0,00000 | 0,00000 |

La WMAE per classe resta dello stesso ordine dell'errore standard medio pesato, pari a 0,34–0,44 ante, e della distanza fra seed. La misura distingue quindi un bias aggregato limitato da valori individuali ancora rumorosi; non autorizza a chiamare equivalenti i due alberi postflop.

La tabella completa è in `CO_MONKER_LINEAR_MCCFR_500K_ROOT_ACTION_EV_COMPARISON.md`.

## Perché frequenze molto diverse possono avere EV vicini

Le azioni che Monker miscela sono quasi indifferenti. Pesando sulle combo, lo spread degli EV fra le sole azioni presenti nel supporto Monker è 0,00365 ante. Piccoli errori di stima o differenze nelle continuazioni possono quindi spostare molta frequenza fra call, raise e all-in con perdita di valore ridotta.

Usando gli EV Monker come diagnostica esterna, non come label di training:

| Strategia 4M | EV sotto la tabella azioni Monker | Perdita rispetto alla migliore azione Monker | Massa entro 0,25 ante dalla migliore |
|---|---:|---:|---:|
| Seed 1 | −0,29943 | 0,03749 ante | 96,49% |
| Seed 2 | −0,29939 | 0,03745 ante | 95,31% |
| Strategia Monker arrotondata | −0,26248 | 0,00054 ante | n/a |

Questa è una valutazione locale alla root e usa i valori dichiarati da Monker; non sostituisce la valutazione fisica del profilo locale. Mostra però che 15,7661 pp di errore nelle frequenze non implicano una perdita della stessa scala in EV.

Per AA, Monker riporta EV 5,828 / 6,237 / 6,242 / 6,238 / −1,000 ante. Nei due solve 4M il fold di AA è rispettivamente `1,26e-8` e `1,43e-8` in frequenza: il difetto osservato nei run più brevi non persiste. Il solver sceglie soprattutto call e raise-to 6a fra azioni quasi equivalenti; non considera il fold competitivo.

## Validazione

Build Release dei target modificati: PASS.

```text
gtosd_hu_preflop_trainer_dependency_check PASS
gtosd_external_sampling_tests              PASS
gtosd_hu_preflop_sampling_tests            PASS
gtosd_hu_preflop_compiled_tests            PASS
gtosd_hu_preflop_abstraction_tests         PASS
gtosd_hu_preflop_parallel_tests            PASS
6/6 passed
```

Hash SHA-256 dell'eseguibile: `D9A1DBA03EEB99E569FBCC9489FF7541C24A8F8851B3E65636D104037FFA93E3`.

Artefatti principali:

- seed 1 JSON: SHA-256 `AA423AE0081CF31A34370504AEAB11BB4AF8B1F52761216E9F71D6FCFE144120`;
- seed 2 JSON: SHA-256 `E43B16EE71FAA39E81D01778C17A9667CD43039F38A8C2A6E9F57DBD52E232D5`;
- stdout e stderr omonimi nella directory R6;
- tabella completa degli EV per 81 classi.

## Decisione

La diagnostica non giustifica un run 8M. L'errore residuo di frequenza è molto più grande della soglia e la strategia di equilibrio non è identificata univocamente dalle azioni quasi indifferenti. Più iterazioni possono ridurre il rumore, ma non garantiscono la stessa selezione di equilibrio di una build Monker con algoritmo, astrazione e albero postflop ignoti.

Raggiungere il gate attuale richiede una delle seguenti nuove evidenze:

1. il contratto postflop Monker completo, per dimostrare che il gioco confrontato è lo stesso;
2. una regola di selezione dell'equilibrio indipendente dal target e condivisa dai due solver;
3. una revisione esplicita del gate che distingua errore di frequenza da perdita di EV nelle azioni quasi equivalenti.

Senza una di queste condizioni, adattare il trainer alle frequenze note sarebbe target tuning vietato dalla roadmap.
