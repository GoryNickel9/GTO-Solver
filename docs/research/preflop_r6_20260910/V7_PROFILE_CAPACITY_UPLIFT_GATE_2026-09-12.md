# R6 — Gate v7 con capacità di profilo aumentata

## Esito

`ENGINEERING_PASS / SCREEN_GATE_FAIL / NO_2M_RUN / R6_GATE_FAIL`.

L'aumento da `32/128/512` a `128/512/2048` aggiunge risoluzione ma peggiora WMAE e stabilità tra seed. Il candidato non passa a 2M.

## Risultati 250k

| Profilo | Seed | WMAE | TV contro Monker | P95 TV | TV fra seed | Infoset | Payload | Bucket F/T/R | Solve |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| v7 `32/128/512` | 1 | 19,3968 pp | 48,4920 pp | 96,8198 pp | — | 799.600 | 119.062.080 B | 15/59/138 | 293,294 s |
| v7 `32/128/512` | 2 | 18,0035 pp | 45,0087 pp | 96,6501 pp | 25,8938 pp | 808.071 | 120.218.544 B | 15/59/138 | 294,343 s |
| v7 `128/512/2048` | 1 | 19,2092 pp | 48,0231 pp | 94,0708 pp | — | 1.848.528 | 270.640.800 B | 46/151/365 | 209,267 s |
| v7 `128/512/2048` | 2 | 19,9528 pp | 49,8819 pp | 95,2506 pp | 30,7244 pp | 1.851.873 | 271.061.856 B | 47/150/372 | 212,610 s |

| Gate | Baseline | Challenger | Esito |
|---|---:|---:|---|
| WMAE media migliore di 0,5 pp oppure TV fra seed −20% | 18,7001 / 25,8938 pp | 19,5810 / 30,7244 pp | FAIL |
| metrica primaria non scelta non peggiore di 0,5 pp | — | WMAE +0,8809; TV +4,8306 pp | FAIL |
| P95 media non peggiore di 2 pp | 96,7350 pp | 94,6607 pp | PASS |
| payload massimo 512 MiB | 120.218.544 B | 271.061.856 B | PASS |
| proiezione 2M sotto 7.200 s | 2.350,5 s | 1.687,5 s | PASS |

Le azioni dominanti differiscono fra seed in `31/81` classi, pari a `204/630` combo fisiche. Le strategie root sono finite, non negative e normalizzate entro `2,22e-16`. Per AA entrambi i seed scelgono Call; `Call - Raise 6a` misura `+0,9606 ± 0,2048a` e `+0,3817 ± 0,2095a`.

## Artefatti

- seed 1: `v7_symmetric_mean4_capacity128_512_2048_250k_seed1_monetary_v2.json`, SHA-256 `F440DC8A8B75D6D0DF87CB86B576F0BDA9EAEF252753F2E84EACD2807EA1BA76`;
- seed 2: `v7_symmetric_mean4_capacity128_512_2048_250k_seed2_monetary_v2.json`, SHA-256 `F026C72BC57D8D7ED67AB4177FD722A6ABC4577618366CA472DF4BCA64A48DBF`;
- i confronti restano `REJECTED / REFERENCE_CONFIG_INCOMPLETE`.

## Decisione

La capacità non è il limite dominante. Più bucket riducono la condivisione degli update e peggiorano la stabilità nel budget corrente. Non eseguire 2M e non aumentare ancora la partizione.

Il prossimo esperimento conserva `32/128/512` e introduce memoria selettiva della transizione tra street, versionata e disattivata per default.
