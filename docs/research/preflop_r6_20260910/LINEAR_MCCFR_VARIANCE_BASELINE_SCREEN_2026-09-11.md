# Linear MCCFR: screening della opponent-value baseline

Data: 2026-09-11

## Obiettivo

Verificare se la `opponent-value baseline` già disponibile nel trainer riduce la varianza e la distanza strategica a parità di lavoro. Il controllo usa due seed indipendenti a 500.000 iterazioni, batch `32` e partizione `32/128/512`.

## Contratto

Restano invariati gioco, algoritmo Linear MCCFR, seed, evaluator oracle, astrazione distributional strength MC8, albero compilato, otto worker, cache e budget. L'unica variabile è `opponent_value_baseline=0/1`.

Non è stato modificato il codice di produzione. I nuovi artefatti sono:

- `linear_mccfr_8t_500k_batch32_partition_32_128_512_opponent_baseline.json`;
- `linear_mccfr_8t_500k_batch32_partition_32_128_512_opponent_baseline_seed2.json`;
- stdout, stderr e confronto `.comparison.json` associati.

## Qualità

| Configurazione | Seed | WMAE (pp) | TV media (pp) | P95 TV (pp) | Errore max root (pp) | EV CO (ante) | SE EV |
|---|---:|---:|---:|---:|---:|---:|---:|
| Controllo | 1 | **19,344159** | **48,360397** | **95,508322** | **26,387556** | -0,096547 | 0,117304 |
| Baseline | 1 | 19,365685 | 48,414213 | 96,795187 | 27,436858 | -0,111483 | **0,113398** |
| Controllo | 2 | **19,756852** | **49,392130** | **95,958384** | **28,989995** | +0,124768 | **0,112607** |
| Baseline | 2 | 20,484220 | 51,210550 | 96,676939 | 29,680664 | -0,027643 | 0,115026 |

La baseline peggiora la WMAE su entrambi i seed: `+0,021526 pp` e `+0,727368 pp`. La WMAE media passa da `19,550505` a `19,924953 pp`; la TV media da `48,876264` a `49,812381 pp`.

L'errore standard EV migliora soltanto sul seed 1 e peggiora sul seed 2. La media scende da `0,114956` a `0,114212 ante`, appena lo `0,65%`: il cambiamento non è coerente fra repliche.

## Stabilità fra seed

| Configurazione | WMAE fra policy (pp) | TV fra policy (pp) | Max delta azione aggregata (pp) |
|---|---:|---:|---:|
| Controllo | **12,075768** | **30,189421** | **2,913336** |
| Baseline | 13,620731 | 34,051827 | 3,612929 |

La baseline aumenta la WMAE fra seed di `1,544962 pp`, pari al `12,79%`. Non riduce quindi la variabilità che motivava l'esperimento.

## Costo

| Configurazione | Seed | Solve (s) | Payload numerico (B) | Payload baseline (B) | Picco private campionato (B) |
|---|---:|---:|---:|---:|---:|
| Controllo | 1 | **87,922163** | 176.579.856 | 0 | **340.541.440** |
| Baseline | 1 | 104,960386 | 179.931.600 | 115.308.416 | 528.388.096 |
| Controllo | 2 | **88,554543** | 178.164.720 | 0 | **342.720.512** |
| Baseline | 2 | 108,776923 | 178.537.968 | 114.592.920 | 523.972.608 |

In media la baseline aumenta il tempo del `21,11%` e il picco private campionato del `54,02%`. Alloca circa 115 MB aggiuntivi per 848 mila contesti di baseline.

## Validazione e gate

| Controllo | Esito | Evidenza |
|---|---|---|
| Due repliche confrontabili | PASS | stessi seed del controllo e unica opzione variata |
| Solve e log | PASS | entrambi `HU_PREFLOP_SOLVE=PASS`; stderr vuoti |
| Strategia valida | PASS | 81 classi, nessun valore negativo o non finito, errore somma massimo `2,220446049250313e-16` |
| WMAE non peggiore su entrambi i seed | FAIL | peggiora su entrambi |
| Dispersione fra seed ridotta | FAIL | WMAE fra policy `+12,79%` |
| Costo tempo massimo +15% | FAIL | `+21,11%` medio |
| Costo RAM giustificato dalla qualità | FAIL | `+54,02%` senza beneficio strategico |
| Equivalenza Monker | NOT EVALUATED | comparatore `REFERENCE_CONFIG_INCOMPLETE` |

## Decisione

La `opponent-value baseline` è scartata per questa candidata. Non entra nella configurazione promossa e non viene portata a 2M o 4M.

Il prossimo esperimento isolato confronta il campionamento fisico indipendente con `public_board_stratified`, mantenendo Linear MCCFR, batch `32`, partizione `32/128/512` e due seed.
