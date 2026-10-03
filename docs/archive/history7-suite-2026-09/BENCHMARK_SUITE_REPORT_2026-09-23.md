> **Nota del 2026-10-03.** Documento archiviato. history7 e la suite HU10-HU40 sono stati
> ritirati il 2026-10-01 e il loro codice è stato tolto il 2026-10-03. Gli strumenti citati
> qui esistono solo al tag `history7-final` (= `88118a6`):
> `tools/preflop_suite/` (`suite.py`, `run_queue.sh`), `benchmarks/suite/` tranne `fixtures/`,
> l'opzione `--history-rows` delle CLI, gli eseguibili `gtosd_preflop_blueprint_abstract_br`,
> `gtosd_preflop_blueprint_history_rows` e `gtosd_preflop_blueprint_history_census` e gli script
> `scripts/research/run_hu40_history7_solve.ps1` e `finalize_hu40_t37000.ps1`.
>
> Vedi il [README dell'archivio](README.md).
> Il JSON da cui questo report è stato generato è stato cancellato il 2026-10-03:
> `git show docs-pre-cleanup-2026-10-02:docs/research/preflop_vector_cfr/BENCHMARK_SUITE_REPORT_2026-09-23.json`.

## Tabella principale (mediana dei tempi sulle ripetizioni pulite, picchi massimi su tutte)

| Benchmark | Versione | Rip. (pulite) | Picco private commit (GiB) | Picco working set (GiB) | Training (s) | BR esatta (s) | Totale interno (s) | Totale processi (s) | max gain (a) | Esito |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| HU10 | baseline-ba93c75 | 3 (2) | 3.067 | 3.062 | 1446.0 | 267.2 | 1739.0 | 1739.7 | 0.002045732 | PASS |
| HU10 | cand-a-compact-double | 3 (2) | 2.111 | 2.107 | 1202.1 | 269.9 | 1496.9 | 1498.3 | 0.002045732 | PASS |
| HU10 | cand-b-mixed | 3 (1) | 1.677 | 1.673 | 1063.9 | 223.0 | 1308.2 | 1309.5 | 0.002041681 | PASS |
| HU10 | cand-c-float32 | 3 (2) | 1.242 | 1.238 | 1112.2 | 252.4 | 1384.3 | 1386.0 | 0.002041738 | PASS |
| HU20 | baseline-ba93c75 | 3 (1) | 11.487 | 11.466 | 3059.0 | 924.9 | 4083.7 | 4084.7 | 0.027999589 | PASS |
| HU20 | cand-a-compact-double | 3 (3) | 7.940 | 7.919 | 2680.4 | 918.9 | 3686.7 | 3691.9 | 0.027999589 | PASS |
| HU20 | cand-b-mixed | 3 (2) | 6.196 | 6.178 | 2574.4 | 826.6 | 3480.2 | 3485.0 | 0.027975831 | PASS |
| HU20 | cand-c-float32 | 3 (2) | 4.453 | 4.438 | 2575.1 | 966.1 | 3614.9 | 3619.4 | 0.027996645 | PASS |
| HU30 | baseline-ba93c75 | 3 (2) | 11.830 | 11.809 | 3566.1 | 1113.7 | 4785.5 | 4787.1 | 0.191818521 | FAIL |
| HU30 | cand-a-compact-double | 3 (1) | 8.180 | 8.157 | 2739.8 | 867.0 | 3697.0 | 3702.1 | 0.191818521 | FAIL |
| HU30 | cand-b-mixed | 3 (3) | 6.383 | 6.364 | 2916.0 | 957.2 | 3953.3 | 3958.1 | 0.191811144 | FAIL |
| HU30 | cand-c-float32 | 3 (2) | 4.586 | 4.571 | 2811.9 | 979.4 | 3869.5 | 3874.3 | 0.191846648 | FAIL |
| HU40 | baseline-ba93c75 | 3 (1) | 11.830 | 11.809 | 3620.5 | 1301.0 | 5036.2 | 5037.1 | 0.289556855 | FAIL |
| HU40 | cand-a-compact-double | 3 (2) | 8.180 | 8.158 | 2873.4 | 1003.8 | 3967.5 | 3972.5 | 0.289556855 | FAIL |
| HU40 | cand-b-mixed | 3 (2) | 6.383 | 6.364 | 3005.1 | 1009.6 | 4105.6 | 4110.5 | 0.289519745 | FAIL |
| HU40 | cand-c-float32 | 3 (2) | 4.586 | 4.571 | 3010.1 | 1023.8 | 4108.9 | 4113.9 | 0.289562140 | FAIL |
| HU10-FULL | baseline-ba93c75 | 0 | | | | | | | | RESOURCE_LIMIT (stato baseline 34.1 GiB > 32 GiB di RAM con history7; nessun run) |
| HU40-FULL | baseline-ba93c75 | 0 | | | | | | | | RESOURCE_LIMIT (stato baseline 746.5 GiB > 32 GiB di RAM con history7; nessun run) |
| HU20-2 | baseline-ba93c75 | 0 | | | | | | | | NON ESEGUITO (scenario derivato: 20a con la 3-bet a 17a, che l'utente ha escluso dalla fixture storica) |
| HU10-FULL | cand-a-compact-double | 0 | | | | | | | | RESOURCE_LIMIT (stato baseline 34.1 GiB > 32 GiB di RAM con history7; nessun run) |
| HU40-FULL | cand-a-compact-double | 0 | | | | | | | | RESOURCE_LIMIT (stato baseline 746.5 GiB > 32 GiB di RAM con history7; nessun run) |
| HU20-2 | cand-a-compact-double | 0 | | | | | | | | NON ESEGUITO (scenario derivato: 20a con la 3-bet a 17a, che l'utente ha escluso dalla fixture storica) |
| HU10-FULL | cand-b-mixed | 0 | | | | | | | | RESOURCE_LIMIT (stato baseline 34.1 GiB > 32 GiB di RAM con history7; nessun run) |
| HU40-FULL | cand-b-mixed | 0 | | | | | | | | RESOURCE_LIMIT (stato baseline 746.5 GiB > 32 GiB di RAM con history7; nessun run) |
| HU20-2 | cand-b-mixed | 0 | | | | | | | | NON ESEGUITO (scenario derivato: 20a con la 3-bet a 17a, che l'utente ha escluso dalla fixture storica) |
| HU10-FULL | cand-c-float32 | 0 | | | | | | | | RESOURCE_LIMIT (stato baseline 34.1 GiB > 32 GiB di RAM con history7; nessun run) |
| HU40-FULL | cand-c-float32 | 0 | | | | | | | | RESOURCE_LIMIT (stato baseline 746.5 GiB > 32 GiB di RAM con history7; nessun run) |
| HU20-2 | cand-c-float32 | 0 | | | | | | | | NON ESEGUITO (scenario derivato: 20a con la 3-bet a 17a, che l'utente ha escluso dalla fixture storica) |

## Confronto con la baseline

| Benchmark | Candidata | GiB risparmiati (picco commit) | Riduzione memoria | Secondi risparmiati (e2e interno) | Speedup training | Speedup e2e | delta max gain (a) | delta EV CO (a) | delta NashConv (a) | Stessa policy | Criteri preregistrati (7.1 identita' / 7.2 esito) |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---|---|
| HU10 | cand-a-compact-double | 0.955 | 31.1 % | 242.2 | 1.203 | 1.162 | +0.000000000 | +0.000000000 | +0.000000000 | si | 7.1 identica (PASS) |
| HU10 | cand-b-mixed | 1.390 | 45.3 % | 430.9 | 1.359 | 1.329 | -0.000004050 | -0.000003052 | -0.000005489 | no | 7.2 PASS |
| HU10 | cand-c-float32 | 1.825 | 59.5 % | 354.7 | 1.300 | 1.256 | -0.000003994 | -0.000003230 | -0.000008124 | no | 7.2 PASS |
| HU20 | cand-a-compact-double | 3.546 | 30.9 % | 397.0 | 1.141 | 1.108 | +0.000000000 | +0.000000000 | +0.000000000 | si | 7.1 identica (PASS) |
| HU20 | cand-b-mixed | 5.291 | 46.1 % | 603.5 | 1.188 | 1.173 | -0.000023758 | +0.000008376 | -0.000033454 | no | 7.2 PASS |
| HU20 | cand-c-float32 | 7.034 | 61.2 % | 468.8 | 1.188 | 1.130 | -0.000002943 | +0.000007890 | -0.000011671 | no | 7.2 PASS |
| HU30 | cand-a-compact-double | 3.651 | 30.9 % | 1088.4 | 1.302 | 1.294 | +0.000000000 | +0.000000000 | +0.000000000 | si | 7.1 identica (PASS) |
| HU30 | cand-b-mixed | 5.448 | 46.0 % | 832.2 | 1.223 | 1.211 | -0.000007377 | +0.000011073 | -0.000026897 | no | 7.2 PASS |
| HU30 | cand-c-float32 | 7.244 | 61.2 % | 916.0 | 1.268 | 1.237 | +0.000028127 | +0.000011604 | -0.000000589 | no | 7.2 PASS |
| HU40 | cand-a-compact-double | 3.651 | 30.9 % | 1068.7 | 1.260 | 1.269 | +0.000000000 | +0.000000000 | +0.000000000 | si | 7.1 identica (PASS) |
| HU40 | cand-b-mixed | 5.447 | 46.0 % | 930.6 | 1.205 | 1.227 | -0.000037111 | +0.000009366 | -0.000048856 | no | 7.2 PASS |
| HU40 | cand-c-float32 | 7.244 | 61.2 % | 927.3 | 1.203 | 1.226 | +0.000005284 | +0.000007090 | -0.000015768 | no | 7.2 PASS |

## Scomposizione dei tempi (mediana, secondi)

| Benchmark | Versione | Init | Discount | Refresh policy | Board + all-in | Traversata | Training | Scrittura | Prep. cert. | BR esatta | E2E interno | s/iter | ms/board | CPU s (train) | CPU s (cert) | Core medi train / cert | CPU di sistema media (%) train / cert |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| HU10 | baseline-ba93c75 | 4.2 | 0.016 | 376.3 | 363.3 | 742.4 | 1482.0 | 21.7 | 7.5 | 278.2 | 1739.0 | 0.0926 | 1.447 | 7500.6 | 1671.1 | 5.00 / 5.94 | 77.3 / 91.4 |
| HU10 | cand-a-compact-double | 3.9 | 0.016 | 371.6 | 42.6 | 853.9 | 1268.2 | 19.3 | 6.7 | 287.6 | 1496.9 | 0.0793 | 1.238 | 5781.6 | 1715.0 | 4.47 / 5.80 | 76.7 / 93.6 |
| HU10 | cand-b-mixed | 3.9 | 0.017 | 393.1 | 45.6 | 916.5 | 1355.2 | 18.0 | 6.8 | 248.9 | 1308.2 | 0.0847 | 1.323 | 5829.5 | 1656.7 | 4.24 / 6.44 | 81.3 / 93.6 |
| HU10 | cand-c-float32 | 3.8 | 0.015 | 344.6 | 41.1 | 799.5 | 1185.2 | 15.2 | 6.5 | 277.1 | 1384.3 | 0.0741 | 1.157 | 5520.5 | 1689.2 | 4.58 / 5.93 | 76.9 / 93.0 |
| HU20 | baseline-ba93c75 | 7.5 | 0.017 | 1424.4 | 374.3 | 1653.0 | 3451.7 | 97.7 | 21.2 | 1015.1 | 4083.7 | 0.2157 | 3.371 | 19543.6 | 6301.8 | 5.45 / 6.08 | 90.1 / 93.2 |
| HU20 | cand-a-compact-double | 4.8 | 0.016 | 1193.7 | 38.8 | 1454.2 | 2680.4 | 76.8 | 15.1 | 904.0 | 3686.7 | 0.1675 | 2.618 | 16816.2 | 6217.3 | 6.04 / 6.73 | 87.7 / 93.8 |
| HU20 | cand-b-mixed | 4.7 | 0.016 | 1174.2 | 39.1 | 1457.9 | 2671.2 | 69.0 | 14.8 | 811.8 | 3480.2 | 0.1670 | 2.609 | 16606.9 | 6067.2 | 6.04 / 7.25 | 89.3 / 94.0 |
| HU20 | cand-c-float32 | 4.1 | 0.017 | 1188.0 | 39.8 | 1475.6 | 2703.4 | 65.1 | 15.2 | 914.1 | 3614.9 | 0.1690 | 2.640 | 16417.8 | 6233.8 | 5.90 / 6.68 | 88.3 / 92.4 |
| HU30 | baseline-ba93c75 | 6.8 | 0.017 | 1432.2 | 375.7 | 1794.5 | 3602.5 | 99.1 | 21.2 | 1077.4 | 4785.5 | 0.2252 | 3.518 | 21409.6 | 6727.6 | 5.77 / 6.16 | 90.9 / 95.5 |
| HU30 | cand-a-compact-double | 5.3 | 0.016 | 1322.0 | 41.3 | 1777.9 | 3141.1 | 90.4 | 17.6 | 1055.4 | 3697.0 | 0.1963 | 3.067 | 18673.8 | 6545.4 | 5.75 / 6.06 | 90.7 / 94.3 |
| HU30 | cand-b-mixed | 4.4 | 0.016 | 1205.9 | 38.9 | 1671.4 | 2916.0 | 71.0 | 15.3 | 941.7 | 3953.3 | 0.1822 | 2.848 | 18459.5 | 6445.0 | 6.13 / 6.74 | 88.3 / 93.6 |
| HU30 | cand-c-float32 | 4.1 | 0.016 | 1181.9 | 39.1 | 1647.9 | 2868.9 | 73.8 | 15.8 | 989.2 | 3869.5 | 0.1793 | 2.802 | 18224.9 | 6563.6 | 6.23 / 6.50 | 87.8 / 93.3 |
| HU40 | baseline-ba93c75 | 7.0 | 0.018 | 1585.0 | 411.8 | 2062.2 | 4059.0 | 105.2 | 21.1 | 1223.3 | 5036.2 | 0.2537 | 3.964 | 21506.8 | 6766.7 | 5.17 / 5.43 | 92.4 / 94.2 |
| HU40 | cand-a-compact-double | 4.6 | 0.016 | 1240.5 | 39.0 | 1700.2 | 2979.7 | 78.2 | 15.9 | 1116.5 | 3967.5 | 0.1862 | 2.910 | 18784.5 | 6543.7 | 6.12 / 5.76 | 90.2 / 93.0 |
| HU40 | cand-b-mixed | 4.7 | 0.017 | 1260.5 | 41.2 | 1793.5 | 3095.3 | 81.5 | 16.2 | 1044.9 | 4105.6 | 0.1935 | 3.023 | 18650.3 | 6563.3 | 5.84 / 6.26 | 88.5 / 93.8 |
| HU40 | cand-c-float32 | 4.3 | 0.017 | 1273.5 | 42.1 | 1814.0 | 3129.6 | 68.2 | 16.0 | 1074.5 | 4108.9 | 0.1956 | 3.056 | 18557.4 | 6571.5 | 5.79 / 6.07 | 88.9 / 91.7 |

## Scomposizione della memoria del trainer (byte riservati per componente)

| Benchmark | Versione | Regret | Somme strategia | Policy densa / compatta | Timestamp discount | All-in (dense + per board) | Batch board | Workspace | Mappa history | Albero + layout | Totale conteggiato | Picco commit | Non attribuito | Fonte |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| HU10 | baseline-ba93c75 | 0.868 | 0.868 | 0.868 | 0.204 | 0.006 | 0.104 | 0.008 | 0.049 | 0.000 | 2.976 | 3.067 | 0.090 | analytic (HEAD has no instrumentation) |
| HU10 | cand-a-compact-double | 0.868 | 0.868 | 0.004 | 0.204 | 0.006 | 0.001 | 0.009 | 0.049 | 0.000 | 2.089 | 2.111 | 0.023 | misurata |
| HU10 | cand-b-mixed | 0.868 | 0.434 | 0.004 | 0.204 | 0.006 | 0.001 | 0.009 | 0.049 | 0.000 | 1.655 | 1.677 | 0.022 | misurata |
| HU10 | cand-c-float32 | 0.434 | 0.434 | 0.004 | 0.204 | 0.006 | 0.001 | 0.009 | 0.049 | 0.000 | 1.221 | 1.242 | 0.021 | misurata |
| HU20 | baseline-ba93c75 | 3.481 | 3.481 | 3.481 | 0.768 | 0.006 | 0.104 | 0.009 | 0.049 | 0.000 | 11.379 | 11.487 | 0.108 | analytic (HEAD has no instrumentation) |
| HU20 | cand-a-compact-double | 3.481 | 3.481 | 0.015 | 0.768 | 0.006 | 0.001 | 0.009 | 0.049 | 0.000 | 7.890 | 7.940 | 0.050 | misurata |
| HU20 | cand-b-mixed | 3.481 | 1.740 | 0.015 | 0.768 | 0.006 | 0.001 | 0.009 | 0.049 | 0.000 | 6.150 | 6.196 | 0.047 | misurata |
| HU20 | cand-c-float32 | 1.740 | 1.740 | 0.015 | 0.768 | 0.006 | 0.001 | 0.009 | 0.049 | 0.000 | 4.409 | 4.453 | 0.043 | misurata |
| HU30 | baseline-ba93c75 | 3.586 | 3.586 | 3.586 | 0.795 | 0.006 | 0.104 | 0.009 | 0.049 | 0.000 | 11.722 | 11.830 | 0.108 | analytic (HEAD has no instrumentation) |
| HU30 | cand-a-compact-double | 3.586 | 3.586 | 0.015 | 0.795 | 0.006 | 0.001 | 0.009 | 0.049 | 0.000 | 8.128 | 8.180 | 0.051 | misurata |
| HU30 | cand-b-mixed | 3.586 | 1.793 | 0.015 | 0.795 | 0.006 | 0.001 | 0.009 | 0.049 | 0.000 | 6.335 | 6.383 | 0.048 | misurata |
| HU30 | cand-c-float32 | 1.793 | 1.793 | 0.015 | 0.795 | 0.006 | 0.001 | 0.009 | 0.049 | 0.000 | 4.542 | 4.586 | 0.045 | misurata |
| HU40 | baseline-ba93c75 | 3.586 | 3.586 | 3.586 | 0.795 | 0.006 | 0.104 | 0.009 | 0.049 | 0.000 | 11.722 | 11.830 | 0.108 | analytic (HEAD has no instrumentation) |
| HU40 | cand-a-compact-double | 3.586 | 3.586 | 0.015 | 0.795 | 0.006 | 0.001 | 0.009 | 0.049 | 0.000 | 8.128 | 8.180 | 0.052 | misurata |
| HU40 | cand-b-mixed | 3.586 | 1.793 | 0.015 | 0.795 | 0.006 | 0.001 | 0.009 | 0.049 | 0.000 | 6.335 | 6.383 | 0.048 | misurata |
| HU40 | cand-c-float32 | 1.793 | 1.793 | 0.015 | 0.795 | 0.006 | 0.001 | 0.009 | 0.049 | 0.000 | 4.542 | 4.586 | 0.045 | misurata |

## Celle per street (tabelle CFR) e policy per batch

| Benchmark | Versione | Celle preflop/flop/turn/river | Righe preflop/flop/turn/river | Celle policy allocate | Righe materializzate per pass (media) | Celle materializzate per pass (media) | Lookup mano-riga per pass | Riusi per pass | Policy compatta (byte, picco) | Board distinte / rebuild identici | Byte regret+somme per street (GiB) |
|---|---|---|---|---:|---|---|---|---|---:|---|---|
| HU10 | baseline-ba93c75 | 1701/273060/11588980/104670360 | 648/121360/5348760/49256640 | 116534101 (densa) | non strumentato | non strumentato | non strumentato | non strumentato | | non strumentato | 0.000/0.004/0.173/1.560 (16 B/cella) |
| HU10 | cand-a-compact-double | 1701/273060/11588980/104670360 | 648/121360/5348760/49256640 | 646757 (compatta) | 81/2375/4016/4175 | 1701/85483/208828/283914 | 14880/14880/14880/14880 | 14799/12505/10864/10705 | 5174056 | 957696 / 66304 | 0.000/0.004/0.173/1.560 (16 B/cella) |
| HU10 | cand-b-mixed | 1701/273060/11588980/104670360 | 648/121360/5348760/49256640 | 646757 (compatta) | 81/2375/4016/4175 | 1701/85483/208828/283914 | 14880/14880/14880/14880 | 14799/12505/10864/10705 | 5174056 | 957696 / 66304 | 0.000/0.003/0.130/1.170 (12 B/cella) |
| HU10 | cand-c-float32 | 1701/273060/11588980/104670360 | 648/121360/5348760/49256640 | 646757 (compatta) | 81/2375/4016/4175 | 1701/85483/208828/283914 | 14880/14880/14880/14880 | 14799/12505/10864/10705 | 5174056 | 957696 / 66304 | 0.000/0.002/0.086/0.780 (8 B/cella) |
| HU20 | baseline-ba93c75 | 1701/546120/35658400/430995600 | 648/212380/15154820/190869480 | 467201821 (densa) | non strumentato | non strumentato | non strumentato | non strumentato | | non strumentato | 0.000/0.008/0.531/6.422 (16 B/cella) |
| HU20 | cand-a-compact-double | 1701/546120/35658400/430995600 | 648/212380/15154820/190869480 | 2207461 (compatta) | 81/2375/4016/4175 | 1701/170966/642546/1169057 | 14880/14880/14880/14880 | 14799/12505/10864/10705 | 17659688 | 957696 / 66304 | 0.000/0.008/0.531/6.422 (16 B/cella) |
| HU20 | cand-b-mixed | 1701/546120/35658400/430995600 | 648/212380/15154820/190869480 | 2207461 (compatta) | 81/2375/4016/4175 | 1701/170966/642546/1169057 | 14880/14880/14880/14880 | 14799/12505/10864/10705 | 17659688 | 957696 / 66304 | 0.000/0.006/0.399/4.817 (12 B/cella) |
| HU20 | cand-c-float32 | 1701/546120/35658400/430995600 | 648/212380/15154820/190869480 | 2207461 (compatta) | 81/2375/4016/4175 | 1701/170966/642546/1169057 | 14880/14880/14880/14880 | 14799/12505/10864/10705 | 17659688 | 957696 / 66304 | 0.000/0.004/0.266/3.211 (8 B/cella) |
| HU30 | baseline-ba93c75 | 2187/606800/37441320/443309760 | 810/242720/16046280/197026560 | 481360067 (densa) | non strumentato | non strumentato | non strumentato | non strumentato | | non strumentato | 0.000/0.009/0.558/6.606 (16 B/cella) |
| HU30 | cand-a-compact-double | 2187/606800/37441320/443309760 | 810/242720/16046280/197026560 | 2302539 (compatta) | 81/2375/4016/4175 | 2187/189962/674673/1202458 | 14880/14880/14880/14880 | 14799/12505/10864/10705 | 18420312 | 957696 / 66304 | 0.000/0.009/0.558/6.606 (16 B/cella) |
| HU30 | cand-b-mixed | 2187/606800/37441320/443309760 | 810/242720/16046280/197026560 | 2302539 (compatta) | 81/2375/4016/4175 | 2187/189962/674673/1202458 | 14880/14880/14880/14880 | 14799/12505/10864/10705 | 18420312 | 957696 / 66304 | 0.000/0.007/0.418/4.954 (12 B/cella) |
| HU30 | cand-c-float32 | 2187/606800/37441320/443309760 | 810/242720/16046280/197026560 | 2302539 (compatta) | 81/2375/4016/4175 | 2187/189962/674673/1202458 | 14880/14880/14880/14880 | 14799/12505/10864/10705 | 18420312 | 957696 / 66304 | 0.000/0.005/0.279/3.303 (8 B/cella) |
| HU40 | baseline-ba93c75 | 2187/606800/37441320/443309760 | 810/242720/16046280/197026560 | 481360067 (densa) | non strumentato | non strumentato | non strumentato | non strumentato | | non strumentato | 0.000/0.009/0.558/6.606 (16 B/cella) |
| HU40 | cand-a-compact-double | 2187/606800/37441320/443309760 | 810/242720/16046280/197026560 | 2302539 (compatta) | 81/2375/4016/4175 | 2187/189962/674673/1202458 | 14880/14880/14880/14880 | 14799/12505/10864/10705 | 18420312 | 957696 / 66304 | 0.000/0.009/0.558/6.606 (16 B/cella) |
| HU40 | cand-b-mixed | 2187/606800/37441320/443309760 | 810/242720/16046280/197026560 | 2302539 (compatta) | 81/2375/4016/4175 | 2187/189962/674673/1202458 | 14880/14880/14880/14880 | 14799/12505/10864/10705 | 18420312 | 957696 / 66304 | 0.000/0.007/0.418/4.954 (12 B/cella) |
| HU40 | cand-c-float32 | 2187/606800/37441320/443309760 | 810/242720/16046280/197026560 | 2302539 (compatta) | 81/2375/4016/4175 | 2187/189962/674673/1202458 | 14880/14880/14880/14880 | 14799/12505/10864/10705 | 18420312 | 957696 / 66304 | 0.000/0.005/0.279/3.303 (8 B/cella) |

## Qualita' ottenuta (certificato esatto)

| Benchmark | Versione | EV CO (a) | EV BTN (a) | Guadagno CO | Guadagno BTN | NashConv (a) | max gain (a) | Soglia (a) | Esito | Policy | Albero |
|---|---|---:|---:|---:|---:|---:|---:|---:|---|---|---|
| HU10 | baseline-ba93c75 | 0.136116318 | -0.136116318 | 0.001629080 | 0.002045732 | 0.003674811 | 0.002045732 | 0.030 | PASS | fnv1a64:38564a90b4f1577d | fnv1a64:d7b31d6f2cb759fc |
| HU10 | cand-a-compact-double | 0.136116318 | -0.136116318 | 0.001629080 | 0.002045732 | 0.003674811 | 0.002045732 | 0.030 | PASS | fnv1a64:38564a90b4f1577d | fnv1a64:d7b31d6f2cb759fc |
| HU10 | cand-b-mixed | 0.136113266 | -0.136113266 | 0.001627641 | 0.002041681 | 0.003669322 | 0.002041681 | 0.030 | PASS | fnv1a64:8f100d9daf41889b | fnv1a64:d7b31d6f2cb759fc |
| HU10 | cand-c-float32 | 0.136113088 | -0.136113088 | 0.001624949 | 0.002041738 | 0.003666687 | 0.002041738 | 0.030 | PASS | fnv1a64:7873bbbb4f44acc9 | fnv1a64:d7b31d6f2cb759fc |
| HU20 | baseline-ba93c75 | 0.048618699 | -0.048618699 | 0.027999589 | 0.001943583 | 0.029943172 | 0.027999589 | 0.030 | PASS | fnv1a64:362045ee45623b7a | fnv1a64:f5b432de223744cc |
| HU20 | cand-a-compact-double | 0.048618699 | -0.048618699 | 0.027999589 | 0.001943583 | 0.029943172 | 0.027999589 | 0.030 | PASS | fnv1a64:362045ee45623b7a | fnv1a64:f5b432de223744cc |
| HU20 | cand-b-mixed | 0.048627075 | -0.048627075 | 0.027975831 | 0.001933887 | 0.029909718 | 0.027975831 | 0.030 | PASS | fnv1a64:022f2e8ca3870c9e | fnv1a64:f5b432de223744cc |
| HU20 | cand-c-float32 | 0.048626589 | -0.048626589 | 0.027996645 | 0.001934855 | 0.029931501 | 0.027996645 | 0.030 | PASS | fnv1a64:64b05e0097caaa2c | fnv1a64:f5b432de223744cc |
| HU30 | baseline-ba93c75 | -0.079119389 | 0.079119389 | 0.191818521 | 0.072123119 | 0.263941640 | 0.191818521 | 0.030 | FAIL | fnv1a64:197c8c00cb8a47bc | fnv1a64:17dc5c7d07ea30c2 |
| HU30 | cand-a-compact-double | -0.079119389 | 0.079119389 | 0.191818521 | 0.072123119 | 0.263941640 | 0.191818521 | 0.030 | FAIL | fnv1a64:197c8c00cb8a47bc | fnv1a64:17dc5c7d07ea30c2 |
| HU30 | cand-b-mixed | -0.079108316 | 0.079108316 | 0.191811144 | 0.072103598 | 0.263914743 | 0.191811144 | 0.030 | FAIL | fnv1a64:3452cb066d0a1368 | fnv1a64:17dc5c7d07ea30c2 |
| HU30 | cand-c-float32 | -0.079107785 | 0.079107785 | 0.191846648 | 0.072094404 | 0.263941051 | 0.191846648 | 0.030 | FAIL | fnv1a64:90165e2c863675f9 | fnv1a64:17dc5c7d07ea30c2 |
| HU40 | baseline-ba93c75 | -0.140884346 | 0.140884346 | 0.289556855 | 0.144496655 | 0.434053511 | 0.289556855 | 0.030 | FAIL | fnv1a64:294ba7ce3ceecb13 | fnv1a64:18d08f453034ac0f |
| HU40 | cand-a-compact-double | -0.140884346 | 0.140884346 | 0.289556855 | 0.144496655 | 0.434053511 | 0.289556855 | 0.030 | FAIL | fnv1a64:294ba7ce3ceecb13 | fnv1a64:18d08f453034ac0f |
| HU40 | cand-b-mixed | -0.140874981 | 0.140874981 | 0.289519745 | 0.144484910 | 0.434004654 | 0.289519745 | 0.030 | FAIL | fnv1a64:09d53c347ee58505 | fnv1a64:18d08f453034ac0f |
| HU40 | cand-c-float32 | -0.140877256 | 0.140877256 | 0.289562140 | 0.144475603 | 0.434037742 | 0.289562140 | 0.030 | FAIL | fnv1a64:b95245707b984f80 | fnv1a64:18d08f453034ac0f |

## Ripetizioni e condizioni di memoria

| Benchmark | Versione | Rip. (pulite) | Training per rip. (s) | E2E interno per rip. (s) | Core medi train per rip. (* = contaminata) | CPU sistema media (%) per rip. | Picco commit per rip. (GiB) | Min memoria disponibile (GiB) | Max paging usato (GiB) | Page fault (train) | Altri processi gtosd |
|---|---|---:|---|---|---|---|---|---:|---:|---:|---|
| HU10 | baseline-ba93c75 | 3 (2) | 1482.0 / 1694.1 / 1410.0 | 1797.1 / 2005.9 / 1681.0 | 5.00 / 4.36* / 5.19 | n/d / n/d / 77 | 3.067 / 3.067 / 3.067 | 11.443 | 3.632 | 947090 | gtosd_preflop_blueprint_export.exe |
| HU10 | cand-a-compact-double | 3 (2) | 1268.2 / 1422.0 / 1136.1 | 1588.4 / 1761.1 / 1405.3 | 4.47 / 4.06* / 4.83 | n/d / 83 / 70 | 2.111 / 2.111 / 2.111 | 11.849 | 4.030 | 696884 | gtosd_preflop_blueprint_export.exe |
| HU10 | cand-b-mixed | 3 (1) | 1063.9 / 1355.2 / 1409.6 | 1308.2 / 1688.9 / 1689.0 | 4.98 / 4.24* / 4.07* | n/d / 81 / 82 | 1.676 / 1.676 / 1.677 | 13.875 | 4.739 | 583181 | gtosd_preflop_blueprint_export.exe |
| HU10 | cand-c-float32 | 3 (2) | 1039.2 / 1331.7 / 1185.2 | 1279.8 / 1663.6 / 1488.9 | 5.00 / 4.25* / 4.58 | n/d / 80 / 74 | 1.241 / 1.242 / 1.241 | 14.151 | 4.718 | 468709 | gtosd_preflop_blueprint_export.exe |
| HU20 | baseline-ba93c75 | 3 (1) | 4105.6 / 3451.7 / 3059.0 | 5679.0 / 4594.2 / 4083.7 | 4.63* / 5.45* / 6.20 | n/d / 92 / 88 | 11.487 / 11.487 / 11.487 | 0.541 | 4.841 | 3154850 | gtosd_preflop_blueprint_export.exe |
| HU20 | cand-a-compact-double | 3 (3) | 2680.4 / 2822.8 / 2676.6 | 3602.9 / 3960.8 / 3686.7 | 6.04 / 5.77 / 6.10 | n/d / 89 / 87 | 7.940 / 7.940 / 7.940 | 5.277 | 4.857 | 2223814 | gtosd_preflop_blueprint_export.exe |
| HU20 | cand-b-mixed | 3 (2) | 2477.7 / 3311.1 / 2671.2 | 3383.3 / 4597.9 / 3577.1 | 6.41 / 5.04* / 6.04 | n/d / 92 / 87 | 6.196 / 6.196 / 6.196 | 7.947 | 4.717 | 1766867 | gtosd_preflop_blueprint_export.exe |
| HU20 | cand-c-float32 | 3 (2) | 2446.7 / 2820.3 / 2703.4 | 3350.5 / 3823.2 / 3879.3 | 6.41 / 5.73* / 5.90 | n/d / 89 / 87 | 4.452 / 4.453 / 4.452 | 12.567 | 3.877 | 1309855 | gtosd_preflop_blueprint_export.exe |
| HU30 | baseline-ba93c75 | 3 (2) | 4270.5 / 3602.5 / 3529.7 | 5442.0 / 4810.8 / 4760.1 | 4.90* / 5.77 / 5.84 | n/d / 91 / 91 | 11.830 / 11.830 / 11.830 | 0.991 | 5.000 | 3245154 | gtosd_preflop_blueprint_export.exe |
| HU30 | cand-a-compact-double | 3 (1) | 2739.8 / 3560.0 / 3141.1 | 3697.0 / 4904.0 / 4321.2 | 6.54 / 5.18* / 5.75* | n/d / 92 / 90 | 8.180 / 8.179 / 8.179 | 5.324 | 5.000 | 2286774 | gtosd_preflop_blueprint_export.exe |
| HU30 | cand-b-mixed | 3 (3) | 2916.0 / 2899.2 / 3002.3 | 3984.5 / 3953.3 / 3943.1 | 6.13 / 6.18 / 5.99 | n/d / 88 / 89 | 6.383 / 6.383 / 6.383 | 8.445 | 3.812 | 1815795 | gtosd_preflop_blueprint_export.exe |
| HU30 | cand-c-float32 | 3 (2) | 3276.0 / 2868.9 / 2755.0 | 4525.3 / 3896.7 / 3842.3 | 5.42* / 6.23 / 6.41 | n/d / 88 / 88 | 4.586 / 4.586 / 4.586 | 11.227 | 3.770 | 1344677 | gtosd_preflop_blueprint_export.exe |
| HU40 | baseline-ba93c75 | 3 (1) | 3620.5 / 4138.8 / 4059.0 | 5036.2 / 5496.0 / 5018.3 | 5.75 / 5.09* / 5.17* | n/d / 93 / 92 | 11.830 / 11.830 / 11.830 | 3.525 | 4.848 | 3245057 | gtosd_preflop_blueprint_export.exe |
| HU40 | cand-a-compact-double | 3 (2) | 2767.2 / 3678.8 / 2979.7 | 3733.1 / 5011.5 / 4202.0 | 6.53 / 5.08* / 6.12 | n/d / 92 / 88 | 8.180 / 8.180 / 8.179 | 7.569 | 4.807 | 2286739 | gtosd_preflop_blueprint_export.exe |
| HU40 | cand-b-mixed | 3 (2) | 3466.4 / 2914.8 / 3095.3 | 4689.3 / 3955.1 / 4256.1 | 5.23* / 6.23 / 5.84 | n/d / 88 / 89 | 6.383 / 6.383 / 6.383 | 9.340 | 3.797 | 1815857 | gtosd_preflop_blueprint_export.exe |
| HU40 | cand-c-float32 | 3 (2) | 3840.6 / 2890.6 / 3129.6 | 5043.7 / 3921.1 / 4296.7 | 4.73* / 6.21 / 5.79 | n/d / 88 / 90 | 4.586 / 4.586 / 4.586 | 10.269 | 3.735 | 1344695 | gtosd_preflop_blueprint_export.exe |
