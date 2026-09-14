# R6 — Gate del profilo strutturato v7

## Esito

`ENGINEERING_PASS / SCIENTIFIC_GATE_FAIL`.

Il v7 conserva la categoria corrente e sostituisce l'hash v6 con una coordinata ordinata. Il mapping, la CLI e la persistenza `1.6` sono implementati e verificati. Il vantaggio osservato a 100.000 iterazioni non regge a 250.000; il protocollo vieta quindi il run da 500.000.

## Risultati

Tutti i run usano Linear MCCFR, batch `32`, otto worker, MC8 e capacità `32/128/512`.

| Iterazioni | Seed | WMAE | TV media | P95 TV | Errore root massimo | Infoset | Payload | Solve |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 100.000 | 1 | 18,9443 pp | 47,3609 pp | 90,6955 pp | 26,4951 pp | 540.590 | 95.438.448 B | 24,193 s |
| 100.000 | 2 | 17,7126 pp | 44,2815 pp | 98,0428 pp | 25,8775 pp | 541.516 | 95.661.072 B | 21,493 s |
| 100.000 | media | **18,3285 pp** | **45,8212 pp** | 94,3691 pp | **26,1863 pp** | 541.053 | 95.549.760 B | 22,843 s |
| 250.000 | 1 | 18,7921 pp | 46,9802 pp | 96,0882 pp | 28,7887 pp | 686.462 | 117.953.712 B | 45,961 s |
| 250.000 | 2 | 18,8510 pp | 47,1275 pp | 94,9115 pp | 25,5034 pp | 690.640 | 118.865.520 B | 48,834 s |
| 250.000 | media | **18,8215 pp** | **47,0538 pp** | 95,4998 pp | **27,1460 pp** | 688.551 | 118.409.616 B | 47,398 s |

Il legacy accoppiato a 100.000 ottiene WMAE media `18,6477 pp`, TV `46,6193 pp`, errore root massimo `26,2224 pp`, 678.405 infoset e 117.229.248 B di payload. Il v7 a 100.000 migliora WMAE di `0,3192 pp` e TV di `0,7982 pp`, usando il `81,5%` del payload legacy. Il seed 1 peggiora però di `0,1961 pp` in WMAE; il seed 2 migliora di `0,8346 pp`.

A 250.000 il vantaggio scompare: rispetto al legacy 100.000, la WMAE è peggiore di `0,1738 pp`, la TV di `0,4344 pp` e l'errore root massimo di `0,9237 pp`. Il risultato si allontana dal riferimento mentre riceve più update. Non esiste quindi una traiettoria misurata che giustifichi 500.000.

L'occupancy resta molto sotto la capacità nominale: `15/55/129` e `15/55/134` a 100.000; `15/56/136` e `15/57/138` a 250.000. La separazione per categoria riduce le collisioni arbitrarie del v6, ma la proiezione distribuzionale non risolve la selezione delle azioni root.

## Implementazione

- `DistributionalStrengthStructuredV7` è una modalità sperimentale separata.
- `--postflop-distributional-structured-v7` la espone nel runner.
- Il formato policy corrente è `1.6`; il reader continua ad accettare le policy `1.5` e rifiuta un v7 marcato come `1.5`.
- Capacità inferiori a 32 vengono rifiutate perché non possono isolare le nove categorie.
- Monker non viene letto dal mapping o dal trainer.

## Validazione

Build Release MSVC `/W4 /WX`: PASS.

```text
gtosd_hu_preflop_trainer_dependency_check PASS
gtosd_external_sampling_tests              PASS
gtosd_hu_preflop_sampling_tests            PASS
gtosd_hu_preflop_compiled_tests            PASS
gtosd_hu_preflop_abstraction_tests         PASS
gtosd_hu_preflop_parallel_tests            PASS
6/6 passed in 22,05 s
```

Il test di astrazione esegue 31.116 assertion. Verifica separazione delle categorie, determinismo, limiti di capacità, rifiuto delle capacità non valide, solve ridotto, ID dell'astrazione e round-trip atomico con checksum.

Hash SHA-256:

- binario: `541BACFB1E6CED7196A46B740C3C47F27A5CE952D0D640DCB67A26AA58993E69`;
- 100k seed 1: `078D9C3EBB7D3C42200810FADB3182164F5845F408C0C2ECB31F7944461A5CC4`;
- 100k seed 2: `2ED5A47843DEC7417BA17C9BF8550C500E4D0A6D680BA2C59253507EEA4940D6`;
- 250k seed 1: `9918E2192016390D6961F445E641B93A72C9501A27641681F980949AB7A94A24`;
- 250k seed 2: `0C303470C599AE799D1D0B37AECBB97441B99BF99053E932B9C1093A409BD944`.

## Decisione

Il challenger v7 è respinto. Non viene eseguito a 500.000 e non sostituisce il legacy. R6 resta `SCIENTIFIC_GATE_FAIL`; R7 rimane bloccato.
