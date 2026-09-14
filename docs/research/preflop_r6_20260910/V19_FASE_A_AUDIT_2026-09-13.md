# V19 — Audit causale delle continuation

Data: 2026-09-13  
Esito: `PASS_WITH_CAPPED_EVIDENCE / PROCEED_CRN_COMPLETED`

## Perimetro

È stata implementata soltanto la Fase A del piano V19. Il lavoro non modifica:

- l'albero, il payoff, il rake, il ruleset o i default di produzione;
- l'algoritmo di training, il generatore casuale o l'ordine delle riduzioni;
- il viewer, la policy pubblicata o il comparatore esterno.

La telemetria è opt-in e resta separata dallo stato numerico del solver. L'assenza del flag
`collect_action_conditioned_telemetry` conserva il percorso precedente.

## Implementazione

`HuPreflopActionConditionedTelemetry` registra, per nodo, attore, classe fisica, azione, bucket e
terminale:

- massa osservata, reach pubblica e reach propria;
- conteggio, media, varianza Welford, errore standard e vantaggio medio;
- minimo, massimo e spread dell'EV d'azione;
- occupazione del bucket e contatori separati per all-in exact e sampled.

I terminali sono distinti in fold/all-in preflop, fold/all-in postflop, showdown river e
continuazione postflop. Il percorso parallelo accumula mappe locali e le fonde nell'ordine stabile
dei worker; la telemetria non entra nelle strutture di regret o strategy sum.

La CLI espone `--action-conditioned-telemetry` e
`--action-conditioned-telemetry-output`. L'export usa lo schema
`gtosd.hu_preflop_action_conditioned_telemetry.v1` e include fingerprint di albero, algoritmo,
astrazione, seed e partition seed.

## Validazione deterministica

| Controllo | Esito |
| --- | --- |
| Build MSVC Release con warning come errori | PASS |
| `gtosd_hu_preflop_telemetry_tests` | PASS, 4.167 asserzioni |
| Solve OFF/ON con seed e input identici | PASS |
| Policy, regret, strategy sum e blueprint OFF/ON | bit-identici |
| EV, errori standard e contatori OFF/ON | bit-identici |
| Fingerprint albero, algoritmo, astrazione e policy | identici |
| Ripetizione ON single-worker | telemetria identica |
| Solve parallelo a 2 worker con telemetria | PASS |

Il test ridotto usa il fixture CO40, `ExternalSampling`, `ExactPhysical`, 4 iterazioni e un solo
worker per il confronto OFF/ON; il controllo parallelo usa 2 iterazioni, batch 1 e due worker. La
regressione aggiunta per la Fase B usa 4 iterazioni, batch 1 e confronta uno e otto worker con CRN
globale. Nessun test usa WMAE o TV come criterio di qualità.

## Smoke CLI

Comando eseguito sul fixture `benchmarks/fixtures/hu_preflop_co40_game_v1.json`:

```text
iterations=2, evaluation_deals=4, br_iterations=2, br_evaluation_deals=4
algorithm=external_sampling, postflop_representation=exact_physical, threads=1
```

Risultato: `HU_PREFLOP_SOLVE=PASS`, albero `fnv1a64:a68337fa567aa2d9`, 358 righe esportate e
nessun valore non finito. La distribuzione dei terminali osservati è:

| Terminale | Righe |
| --- | ---: |
| `postflop_continuation` | 231 |
| `fold_postflop` | 68 |
| `showdown_river` | 37 |
| `all_in_postflop_sampled` | 16 |
| `fold_preflop` | 5 |
| `all_in_preflop_sampled` | 1 |

Artefatti dello smoke conservati in `.tmp`:

| File | SHA-256 |
| --- | --- |
| `v19_telemetry_smoke.json` | `166A489AC8A1F26F801222D1773D008275043F038D476F5C9E8F9FDADF52AA33` |
| `v19_telemetry_smoke_rows.json` | `B88A409F90EC0D27CB85389C4BC76874A6E2C868CD0CBCD16236D213E8D04C7C` |

Questo smoke verifica il contratto e il formato, non la dispersione a regime: ogni riga ha una
sola osservazione o pochi campioni e quindi non può stimare lo spread fisico dei bucket V8.

## Evidenza V17 e V17-4M

La coppia V17 già prodotta usa lo stesso fingerprint dell'albero
`fnv1a64:a68337fa567aa2d9`, due milioni di iterazioni e la configurazione V8
`32/128/512`:

| Metrica | Seed 1 | Seed 2 | Coppia |
| --- | ---: | ---: | ---: |
| Solve | 2.234,87 s | 2.101,53 s | 36,14 min medi |
| Infoset | 1.567.910 | 1.564.841 | — |
| WMAE diagnostica | 14,1312 pp | 13,7631 pp | 13,9472 pp |
| TV fra seed, strategia media | — | — | 10,9453 pp |
| TV fra seed, policy corrente | — | — | 11,1472 pp |
| Spread EV postflop medio | 22,8437a | 22,8207a | 22,8322a |

La decomposizione V17 assegna `10,2475 pp` della TV media a gap d'azione non superiori a `0,1a`.
Il contributo per famiglia è `1,4581 pp` coppie, `4,2098 pp` suited e `5,2774 pp` offsuit. Il
residuo V17-4M continua a mostrare il delta aggregato `-23,0485 pp` sull'all-in e `+21,2187 pp`
sul call rispetto al riferimento esterno, ma questi dati restano diagnostici.

Gli artefatti V17-4M disponibili hanno fingerprint dell'albero
`fnv1a64:0f9919d7d6030cf0` e, in parte, capacità `64/256/1024`; non sono quindi confrontabili
direttamente con la baseline V17 secondo il contratto V19. Inoltre il comparatore esterno riporta
`REFERENCE_CONFIG_INCOMPLETE` e `REJECTED`; non è una prova di equivalenza dell'albero postflop.

## Telemetria V8 a budget pieno

Il controllo C1 seed 1 da 2M usa la V8 `32/128/512` congelata e termina in `2.573,662 s`. La
policy esportata è byte-identica alla policy V17 seed 1: SHA-256
`CF4973D1DA09317895945B61A20BF28E66860F6F3DE7E67F8311053AAD097215`.

La telemetria contiene 9.966 righe e 3.475 gruppi bucket. Il cap scarta 1.101.277.721 osservazioni,
quindi la classifica è utile per localizzare il segnale ma non rappresenta l'intera distribuzione:

| Contributo | Frazione dell'impatto pesato |
| --- | ---: |
| Offsuit | 92,0371% |
| Suited | 5,5681% |
| Coppie | 2,3949% |
| Continuazioni postflop non terminali | 97,2866% |
| All-in postflop exact | 2,5122% |
| Showdown river | 0,2012% |

L'evidenza individua bucket con spread misurabile e attribuisce quasi tutto l'impatto osservato ai
rami di continuazione non all-in. Non dimostra da sola abstraction error: i momenti marginali
dell'EV non contengono la covarianza fra azioni e il riferimento esterno resta incompleto.

## Gate A

| Criterio | Stato | Evidenza |
| --- | --- | --- |
| Marginali di payoff e policy non mutate | PASS | test OFF/ON bit-identico |
| Telemetria non mutante e deterministica | PASS | 4.167 asserzioni, repeat ON |
| Terminali e contatori separati | PASS | schema v1 e smoke CLI |
| Bucket con spread fisico misurabile a regime | PASS CON CAP | 3.475 gruppi a 2M; 9.966 righe |
| Distinzione varianza EV/errore di astrazione | PRESERVATA | nessun gate WMAE/TV sullo smoke |

La massa `physical_combo_mass` attuale è massa osservata unitaria per deal campionato; non è la
cardinalità fisica `6/4/12`. Anche `bucket_occupancy` è un conteggio di osservazioni, non il numero
di combo fisiche distinte. Queste semantiche sono dichiarate per evitare una lettura errata dei
prossimi artefatti.

## Decisione

`PROCEED_CRN` è stato autorizzato come test causale, non come promozione. Il successivo Gate C ha
respinto il CRN globale: WMAE media `13,6347 pp`, TV fra seed `12,1140 pp` e 10 errori Call/Fold
raggiunti, contro `13,9472 pp`, `10,9453 pp` e 7 del controllo V17.

## Prossima attività autorizzata

Aprire un corpus fisico holdout per separare errore delle continuation, aliasing e mismatch del
riferimento prima di scegliere uno split selettivo. Non eseguire altre iterazioni dello stesso
candidato global CRN.
