# V20 — Root decision trace

Data: 2026-09-14  
Stato: `ENGINEERING_PASS / ROOT MECHANISM IDENTIFIED / ACTION EV INCONCLUSIVE`  
Input: `docs\research\preflop_r6_20260910\v20_v17_seed1_root_decision_trace_v1.json`  
SHA-256: `9579E64F75792E4463B4767DA09F459BD8C114868F5B40A59C6DB4634D430A17`  
Tree: `fnv1a64:a68337fa567aa2d9`  
Algoritmo: `linear_mccfr_v1_opponent_pass_average_deterministic_batch32_workers8_root_action_rollouts4_v1_continuation_mean_updates_v1_symmetric_traverser_mean_updates_v1_root_common_random_numbers_v1_full_current_profile_evaluation_v1_exact_preflop_all_in_expectation_v1_exact_postflop_all_in_flop_turn_v1`  
Astrazione: `preflop_exact81_postflop_distributional_strength_mc8_capacity_32_128_512_street_adaptive_category_equity_profile_v8_current_observation_imperfect_recall`  
Evaluator: `seven_card_table_v1:2236291214962974841`  
Iterazioni: `2000000`  
Seed training/partizione/valutazione: `5923736619020283393` / `5923736619020287489` / `5923736619020279297`

## Ambito

La trace legge la policy dopo il training. Per ogni classe usa gli stessi deal fisici e lo stesso stato RNG iniziale per le cinque azioni root. Regret, strategy sum e policy non vengono aggiornati. Gli intervalli descrivono questa valutazione campionata; non sono una NashConv né una prova di equivalenza con Monker. Le tabelle mostrano intervalli puntuali; la lettura contro la migliore alternativa osservata applica Bonferroni ai quattro confronti con Call.

## Esito sintetico

| Classe | Continuation | Call nella policy | Migliore alternativa osservata | Call − alternativa | IC simultaneo 95% | Esito |
| --- | --- | ---: | --- | ---: | --- | --- |
| `JTo` | `average` | 90.45% | `all_in` | -0.0382a | [-0.8027a, +0.7263a] | non separato |
| `JTo` | `current` | 100.00% | `raise_6` | -0.5922a | [-1.3594a, +0.1750a] | non separato |
| `QJo` | `average` | 85.70% | `raise_10` | +0.6271a | [-0.1470a, +1.4011a] | non separato |
| `QJo` | `current` | 100.00% | `all_in` | +0.1029a | [-0.5862a, +0.7920a] | non separato |
| `J9s` | `average` | 96.50% | `raise_6` | +0.4297a | [-0.2962a, +1.1556a] | non separato |
| `J9s` | `current` | 100.00% | `raise_10` | +0.4316a | [-0.2848a, +1.1481a] | non separato |

## JTo

Deal condizionati per azione: `2000`. Ultimo update dell'infoset root: `1999999`.

| Azione | Strategia media | Strategia corrente | Regret cumulativo positivo | Strategy sum | Vantaggio medio nel training |
| --- | ---: | ---: | ---: | ---: | ---: |
| `all_in` | 0.00% | 0.00% | 0 | 501.323 | -0.0445a |
| `raise_6` | 9.45% | 0.00% | 0 | 3.59172e+09 | -0.0348a |
| `raise_10` | 0.10% | 0.00% | 0 | 3.6682e+07 | -0.0887a |
| `call` | 90.45% | 100.00% | 3.97558e+08 | 3.43791e+10 | +0.0104a |
| `fold` | 0.00% | 0.00% | 0 | 29.0914 | -1.0670a |

### Lettura causale

- Call è l'unica azione con regret cumulativo positivo (3.97558e+08). Il regret matching assegna quindi a Call il 100.00% della strategia corrente.
- La frequenza media di Call (90.45%) deriva dalla strategy sum accumulata: 3.43791e+10 su 3.80075e+10.
- Con la continuation media, Call − all_in vale -0.0382a (IC simultaneo 95% Bonferroni [-0.8027a, +0.7263a]): il campione paired non separa le due azioni.
- Con la continuation corrente, Call − raise_6 vale -0.5922a (IC simultaneo 95% Bonferroni [-1.3594a, +0.1750a]): il campione paired non separa le due azioni.

#### Continuation average

| Azione | EV | SE | Call − azione | IC 95% puntuale |
| --- | ---: | ---: | ---: | --- |
| `all_in` | -0.0370a | 0.1049a | -0.0382a | [-0.6381a, +0.5617a] |
| `raise_6` | -0.1860a | 0.1856a | +0.1108a | [-0.5056a, +0.7272a] |
| `raise_10` | -0.1573a | 0.1800a | +0.0821a | [-0.5380a, +0.7022a] |
| `call` | -0.0752a | 0.3041a | +0.0000a | [+0.0000a, +0.0000a] |
| `fold` | -1.0000a | 0.0000a | +0.9248a | [+0.3288a, +1.5207a] |

#### Scomposizione di Call, continuation average

| Terminale | Street | Probabilità | EV condizionato | Contributo EV |
| --- | --- | ---: | ---: | ---: |
| `showdown_river` | `river` | 20.20% | +1.3958a | +0.2820a |
| `all_in_preflop_exact` | `preflop` | 6.85% | -2.8253a | -0.1935a |
| `all_in_postflop_exact` | `flop` | 6.35% | -2.9766a | -0.1890a |
| `fold_preflop` | `preflop` | 7.25% | -1.9103a | -0.1385a |
| `fold_postflop` | `flop` | 26.40% | +0.3752a | +0.0991a |
| `fold_postflop` | `turn` | 17.80% | +0.4392a | +0.0782a |
| `all_in_postflop_exact` | `turn` | 5.05% | -1.3437a | -0.0679a |
| `fold_postflop` | `river` | 10.10% | +0.5392a | +0.0545a |

| Continuation preflop | Terminale | Probabilità | EV condizionato | Contributo EV |
| --- | --- | ---: | ---: | ---: |
| BTN:check | `showdown_river` | 17.35% | +1.7058a | +0.2960a |
| BTN:all_in → CO:call | `all_in_preflop_exact` | 6.60% | -2.5363a | -0.1674a |
| BTN:all_in → CO:fold | `fold_preflop` | 6.75% | -2.0000a | -0.1350a |
| BTN:check | `fold_postflop` | 19.30% | +0.6884a | +0.1329a |
| BTN:check | `all_in_postflop_exact` | 2.20% | -5.4221a | -0.1193a |

Le righe per street non sono additive: ogni riga riusa il payoff terminale dell'intera mano e lo condiziona al raggiungimento della street.

| Street raggiunta | Reach | EV finale condizionato | Reach × EV finale |
| --- | ---: | ---: | ---: |
| `flop` | 85.90% | +0.2990a | +0.2568a |
| `turn` | 53.15% | +0.6524a | +0.3467a |
| `river` | 30.30% | +1.1103a | +0.3364a |

| Street | Player | Bucket | Reach | EV finale condizionato | Reach × EV finale |
| --- | --- | ---: | ---: | ---: | ---: |
| `turn` | `CO` | `15448197710411062686` | 3.20% | +13.6620a | +0.4372a |
| `flop` | `BTN` | `5157229796808569454` | 14.60% | +2.8404a | +0.4147a |
| `flop` | `BTN` | `12571504534654866189` | 4.55% | -8.8352a | -0.4020a |
| `river` | `CO` | `1673623324237569044` | 2.40% | +16.0390a | +0.3849a |
| `flop` | `BTN` | `348747726282816473` | 15.65% | +2.3614a | +0.3696a |
| `flop` | `BTN` | `1588398071236634728` | 10.95% | +3.2112a | +0.3516a |
| `turn` | `BTN` | `4146058403633432550` | 1.10% | -23.9521a | -0.2635a |
| `flop` | `CO` | `9733572628461312019` | 2.05% | +12.7988a | +0.2624a |

#### Continuation current

| Azione | EV | SE | Call − azione | IC 95% puntuale |
| --- | ---: | ---: | ---: | --- |
| `all_in` | -0.0595a | 0.1054a | -0.5259a | [-1.1168a, +0.0650a] |
| `raise_6` | +0.0068a | 0.1718a | -0.5922a | [-1.1942a, +0.0098a] |
| `raise_10` | -0.1185a | 0.1645a | -0.4668a | [-1.0803a, +0.1466a] |
| `call` | -0.5854a | 0.2930a | +0.0000a | [+0.0000a, +0.0000a] |
| `fold` | -1.0000a | 0.0000a | +0.4146a | [-0.1596a, +0.9889a] |

#### Scomposizione di Call, continuation current

| Terminale | Street | Probabilità | EV condizionato | Contributo EV |
| --- | --- | ---: | ---: | ---: |
| `all_in_postflop_exact` | `flop` | 5.15% | -6.2117a | -0.3199a |
| `fold_preflop` | `preflop` | 15.05% | -2.0000a | -0.3010a |
| `all_in_postflop_exact` | `turn` | 5.10% | -2.5210a | -0.1286a |
| `fold_postflop` | `turn` | 17.65% | +0.6430a | +0.1135a |
| `fold_postflop` | `flop` | 26.35% | +0.3993a | +0.1052a |
| `fold_postflop` | `river` | 10.70% | -0.8686a | -0.0929a |
| `showdown_river` | `river` | 20.00% | +0.1918a | +0.0384a |

| Continuation preflop | Terminale | Probabilità | EV condizionato | Contributo EV |
| --- | --- | ---: | ---: | ---: |
| BTN:all_in → CO:fold | `fold_preflop` | 15.05% | -2.0000a | -0.3010a |
| BTN:raise_6 → CO:call | `all_in_postflop_exact` | 3.20% | -8.8655a | -0.2837a |
| BTN:check | `fold_postflop` | 15.65% | +1.0949a | +0.1714a |
| BTN:check | `fold_postflop` | 20.95% | +0.7203a | +0.1509a |
| BTN:check | `fold_postflop` | 9.95% | -1.2816a | -0.1275a |

Le righe per street non sono additive: ogni riga riusa il payoff terminale dell'intera mano e lo condiziona al raggiungimento della street.

| Street raggiunta | Reach | EV finale condizionato | Reach × EV finale |
| --- | ---: | ---: | ---: |
| `flop` | 84.95% | -0.3347a | -0.2844a |
| `turn` | 53.45% | -0.1303a | -0.0697a |
| `river` | 30.70% | -0.1778a | -0.0546a |

| Street | Player | Bucket | Reach | EV finale condizionato | Reach × EV finale |
| --- | --- | ---: | ---: | ---: | ---: |
| `flop` | `BTN` | `12571504534654866189` | 4.60% | -11.3425a | -0.5218a |
| `flop` | `BTN` | `5157229796808569454` | 14.05% | +3.3076a | +0.4647a |
| `turn` | `CO` | `15448197710411062686` | 3.00% | +13.5212a | +0.4056a |
| `river` | `CO` | `1673623324237569044` | 2.25% | +17.1094a | +0.3850a |
| `flop` | `BTN` | `4234861386498803353` | 2.75% | -11.3658a | -0.3126a |
| `flop` | `CO` | `9733572628461312019` | 2.20% | +13.0381a | +0.2868a |
| `flop` | `CO` | `449058634042022111` | 15.05% | -1.7767a | -0.2674a |
| `turn` | `BTN` | `4146058403633432550` | 1.05% | -23.1866a | -0.2435a |

## QJo

Deal condizionati per azione: `2000`. Ultimo update dell'infoset root: `1999963`.

| Azione | Strategia media | Strategia corrente | Regret cumulativo positivo | Strategy sum | Vantaggio medio nel training |
| --- | ---: | ---: | ---: | ---: | ---: |
| `all_in` | 4.42% | 0.00% | 0 | 1.68126e+09 | -0.0586a |
| `raise_6` | 0.72% | 0.00% | 0 | 2.7274e+08 | -0.1008a |
| `raise_10` | 9.15% | 0.00% | 0 | 3.47881e+09 | -0.0755a |
| `call` | 85.70% | 100.00% | 6.43647e+08 | 3.25685e+10 | +0.0170a |
| `fold` | 0.00% | 0.00% | 0 | 3.55 | -1.1764a |

### Lettura causale

- Call è l'unica azione con regret cumulativo positivo (6.43647e+08). Il regret matching assegna quindi a Call il 100.00% della strategia corrente.
- La frequenza media di Call (85.70%) deriva dalla strategy sum accumulata: 3.25685e+10 su 3.80013e+10.
- Con la continuation media, Call − raise_10 vale +0.6271a (IC simultaneo 95% Bonferroni [-0.1470a, +1.4011a]): il campione paired non separa le due azioni.
- Con la continuation corrente, Call − all_in vale +0.1029a (IC simultaneo 95% Bonferroni [-0.5862a, +0.7920a]): il campione paired non separa le due azioni.
- La strategia corrente usa Call più della media storica. I regret recenti stanno ancora spostando la policy verso Call.

#### Continuation average

| Azione | EV | SE | Call − azione | IC 95% puntuale |
| --- | ---: | ---: | ---: | --- |
| `all_in` | +0.0297a | 0.1130a | +0.6561a | [+0.1066a, +1.2057a] |
| `raise_6` | -0.0598a | 0.1992a | +0.7456a | [+0.1629a, +1.3282a] |
| `raise_10` | +0.0587a | 0.2019a | +0.6271a | [+0.0197a, +1.2345a] |
| `call` | +0.6858a | 0.2796a | +0.0000a | [+0.0000a, +0.0000a] |
| `fold` | -1.0000a | 0.0000a | +1.6858a | [+1.1378a, +2.2338a] |

#### Scomposizione di Call, continuation average

| Terminale | Street | Probabilità | EV condizionato | Contributo EV |
| --- | --- | ---: | ---: | ---: |
| `showdown_river` | `river` | 20.15% | +3.3461a | +0.6742a |
| `fold_preflop` | `preflop` | 13.75% | -1.9200a | -0.2640a |
| `fold_postflop` | `flop` | 25.30% | +0.7499a | +0.1897a |
| `all_in_postflop_exact` | `turn` | 3.70% | +2.4324a | +0.0900a |
| `fold_postflop` | `river` | 10.50% | +0.5825a | +0.0612a |
| `all_in_preflop_exact` | `preflop` | 3.00% | -2.0278a | -0.0608a |
| `all_in_postflop_exact` | `flop` | 7.00% | -0.3828a | -0.0268a |
| `fold_postflop` | `turn` | 16.60% | +0.1342a | +0.0223a |

| Continuation preflop | Terminale | Probabilità | EV condizionato | Contributo EV |
| --- | --- | ---: | ---: | ---: |
| BTN:check | `showdown_river` | 18.20% | +3.6058a | +0.6563a |
| BTN:all_in → CO:fold | `fold_preflop` | 11.40% | -2.0000a | -0.2280a |
| BTN:check | `fold_postflop` | 19.35% | +1.0045a | +0.1944a |
| BTN:check | `fold_postflop` | 14.60% | +0.8225a | +0.1201a |
| BTN:raise_6 → CO:call | `fold_postflop` | 1.95% | -4.5474a | -0.0887a |

Le righe per street non sono additive: ogni riga riusa il payoff terminale dell'intera mano e lo condiziona al raggiungimento della street.

| Street raggiunta | Reach | EV finale condizionato | Reach × EV finale |
| --- | ---: | ---: | ---: |
| `flop` | 83.25% | +1.2140a | +1.0106a |
| `turn` | 50.95% | +1.6638a | +0.8477a |
| `river` | 30.65% | +2.3993a | +0.7354a |

| Street | Player | Bucket | Reach | EV finale condizionato | Reach × EV finale |
| --- | --- | ---: | ---: | ---: | ---: |
| `flop` | `BTN` | `348747726282816473` | 13.95% | +3.3620a | +0.4690a |
| `flop` | `BTN` | `1588398071236634728` | 14.05% | +2.6508a | +0.3724a |
| `turn` | `CO` | `15448197710411062686` | 2.70% | +12.5583a | +0.3391a |
| `flop` | `BTN` | `12571504534654866189` | 3.05% | -10.3690a | -0.3163a |
| `flop` | `BTN` | `5157229796808569454` | 13.40% | +2.0540a | +0.2752a |
| `flop` | `CO` | `11147432522041874523` | 9.15% | +2.9365a | +0.2687a |
| `flop` | `CO` | `673085710337625475` | 4.10% | +6.4790a | +0.2656a |
| `flop` | `CO` | `449058634042022111` | 14.45% | -1.6708a | -0.2414a |

#### Continuation current

| Azione | EV | SE | Call − azione | IC 95% puntuale |
| --- | ---: | ---: | ---: | --- |
| `all_in` | +0.0720a | 0.1143a | +0.1029a | [-0.4378a, +0.6437a] |
| `raise_6` | +0.0417a | 0.1878a | +0.1332a | [-0.4388a, +0.7052a] |
| `raise_10` | -0.1669a | 0.1841a | +0.3418a | [-0.2452a, +0.9287a] |
| `call` | +0.1749a | 0.2687a | +0.0000a | [+0.0000a, +0.0000a] |
| `fold` | -1.0000a | 0.0000a | +1.1749a | [+0.6484a, +1.7015a] |

#### Scomposizione di Call, continuation current

| Terminale | Street | Probabilità | EV condizionato | Contributo EV |
| --- | --- | ---: | ---: | ---: |
| `showdown_river` | `river` | 21.75% | +2.3360a | +0.5081a |
| `fold_preflop` | `preflop` | 18.35% | -2.0000a | -0.3670a |
| `all_in_postflop_exact` | `flop` | 4.45% | -4.8840a | -0.2173a |
| `fold_postflop` | `flop` | 25.60% | +0.6215a | +0.1591a |
| `fold_postflop` | `turn` | 15.95% | +0.2514a | +0.0401a |
| `fold_postflop` | `river` | 10.95% | +0.3637a | +0.0398a |
| `all_in_postflop_exact` | `turn` | 2.95% | +0.4116a | +0.0121a |

| Continuation preflop | Terminale | Probabilità | EV condizionato | Contributo EV |
| --- | --- | ---: | ---: | ---: |
| BTN:check | `showdown_river` | 19.20% | +2.9247a | +0.5616a |
| BTN:all_in → CO:fold | `fold_preflop` | 15.85% | -2.0000a | -0.3170a |
| BTN:check | `fold_postflop` | 21.00% | +0.9576a | +0.2011a |
| BTN:raise_6 → CO:call | `all_in_postflop_exact` | 3.30% | -5.2097a | -0.1719a |
| BTN:check | `fold_postflop` | 14.30% | +0.6797a | +0.0972a |

Le righe per street non sono additive: ogni riga riusa il payoff terminale dell'intera mano e lo condiziona al raggiungimento della street.

| Street raggiunta | Reach | EV finale condizionato | Reach × EV finale |
| --- | ---: | ---: | ---: |
| `flop` | 81.65% | +0.6637a | +0.5419a |
| `turn` | 51.60% | +1.1631a | +0.6002a |
| `river` | 32.70% | +1.6756a | +0.5479a |

| Street | Player | Bucket | Reach | EV finale condizionato | Reach × EV finale |
| --- | --- | ---: | ---: | ---: | ---: |
| `flop` | `BTN` | `348747726282816473` | 13.10% | +2.9746a | +0.3897a |
| `flop` | `BTN` | `1588398071236634728` | 13.75% | +2.7566a | +0.3790a |
| `flop` | `CO` | `449058634042022111` | 14.40% | -2.4461a | -0.3522a |
| `flop` | `BTN` | `12571504534654866189` | 2.75% | -10.7173a | -0.2947a |
| `flop` | `CO` | `673085710337625475` | 4.05% | +6.7376a | +0.2729a |
| `flop` | `BTN` | `5157229796808569454` | 13.60% | +1.7870a | +0.2430a |
| `turn` | `CO` | `15448197710411062686` | 2.40% | +9.8217a | +0.2357a |
| `turn` | `BTN` | `7646288177772974751` | 3.65% | +6.0478a | +0.2207a |

## J9s

Deal condizionati per azione: `2000`. Ultimo update dell'infoset root: `1999995`.

| Azione | Strategia media | Strategia corrente | Regret cumulativo positivo | Strategy sum | Vantaggio medio nel training |
| --- | ---: | ---: | ---: | ---: | ---: |
| `all_in` | 1.03% | 0.00% | 0 | 1.31242e+08 | -0.0687a |
| `raise_6` | 0.64% | 0.00% | 0 | 8.15998e+07 | -0.0574a |
| `raise_10` | 1.83% | 0.00% | 0 | 2.33427e+08 | -0.1679a |
| `call` | 96.50% | 100.00% | 9.45242e+07 | 1.2304e+10 | +0.0075a |
| `fold` | 0.00% | 0.00% | 0 | 2409.78 | -0.6287a |

### Lettura causale

- Call è l'unica azione con regret cumulativo positivo (9.45242e+07). Il regret matching assegna quindi a Call il 100.00% della strategia corrente.
- La frequenza media di Call (96.50%) deriva dalla strategy sum accumulata: 1.2304e+10 su 1.27502e+10.
- Con la continuation media, Call − raise_6 vale +0.4297a (IC simultaneo 95% Bonferroni [-0.2962a, +1.1556a]): il campione paired non separa le due azioni.
- Con la continuation corrente, Call − raise_10 vale +0.4316a (IC simultaneo 95% Bonferroni [-0.2848a, +1.1481a]): il campione paired non separa le due azioni.

#### Continuation average

| Azione | EV | SE | Call − azione | IC 95% puntuale |
| --- | ---: | ---: | ---: | --- |
| `all_in` | -0.3331a | 0.0983a | +0.6152a | [+0.0818a, +1.1487a] |
| `raise_6` | -0.1475a | 0.1934a | +0.4297a | [-0.1399a, +0.9993a] |
| `raise_10` | -0.1992a | 0.1745a | +0.4813a | [-0.0866a, +1.0493a] |
| `call` | +0.2821a | 0.2684a | +0.0000a | [+0.0000a, +0.0000a] |
| `fold` | -1.0000a | 0.0000a | +1.2821a | [+0.7561a, +1.8081a] |

#### Scomposizione di Call, continuation average

| Terminale | Street | Probabilità | EV condizionato | Contributo EV |
| --- | --- | ---: | ---: | ---: |
| `showdown_river` | `river` | 18.85% | +3.5960a | +0.6779a |
| `fold_preflop` | `preflop` | 17.85% | -1.6471a | -0.2940a |
| `all_in_postflop_exact` | `turn` | 3.25% | -5.7363a | -0.1864a |
| `fold_postflop` | `flop` | 26.95% | +0.4140a | +0.1116a |
| `fold_postflop` | `turn` | 16.95% | +0.5920a | +0.1003a |
| `all_in_postflop_exact` | `flop` | 4.50% | -1.3279a | -0.0598a |
| `fold_postflop` | `river` | 11.25% | -0.3598a | -0.0405a |
| `all_in_preflop_exact` | `preflop` | 0.40% | -6.7438a | -0.0270a |

| Continuation preflop | Terminale | Probabilità | EV condizionato | Contributo EV |
| --- | --- | ---: | ---: | ---: |
| BTN:check | `showdown_river` | 16.25% | +4.4801a | +0.7280a |
| BTN:all_in → CO:fold | `fold_preflop` | 13.40% | -2.0000a | -0.2680a |
| BTN:check | `all_in_postflop_exact` | 1.90% | -8.4962a | -0.1614a |
| BTN:check | `fold_postflop` | 20.85% | +0.6002a | +0.1251a |
| BTN:check | `fold_postflop` | 14.85% | +0.5761a | +0.0855a |

Le righe per street non sono additive: ogni riga riusa il payoff terminale dell'intera mano e lo condiziona al raggiungimento della street.

| Street raggiunta | Reach | EV finale condizionato | Reach × EV finale |
| --- | ---: | ---: | ---: |
| `flop` | 81.75% | +0.7378a | +0.6031a |
| `turn` | 50.30% | +1.0960a | +0.5513a |
| `river` | 30.10% | +2.1175a | +0.6374a |

| Street | Player | Bucket | Reach | EV finale condizionato | Reach × EV finale |
| --- | --- | ---: | ---: | ---: | ---: |
| `flop` | `BTN` | `348747726282816473` | 16.10% | +3.0900a | +0.4975a |
| `flop` | `BTN` | `1588398071236634728` | 13.55% | +2.5822a | +0.3499a |
| `flop` | `BTN` | `12571504534654866189` | 3.65% | -7.3856a | -0.2696a |
| `river` | `CO` | `1673623324237569044` | 1.70% | +14.0364a | +0.2386a |
| `river` | `CO` | `5326434114389160536` | 0.80% | +26.0499a | +0.2084a |
| `flop` | `BTN` | `15952815874057380290` | 7.60% | +2.6337a | +0.2002a |
| `turn` | `CO` | `2089537245576346375` | 0.90% | +22.2041a | +0.1998a |
| `turn` | `CO` | `15448197710411062686` | 1.80% | +10.8772a | +0.1958a |

#### Continuation current

| Azione | EV | SE | Call − azione | IC 95% puntuale |
| --- | ---: | ---: | ---: | --- |
| `all_in` | -0.3965a | 0.0992a | +0.6296a | [+0.1211a, +1.1381a] |
| `raise_6` | -0.3580a | 0.1698a | +0.5912a | [+0.0538a, +1.1285a] |
| `raise_10` | -0.1985a | 0.1668a | +0.4316a | [-0.1306a, +0.9939a] |
| `call` | +0.2331a | 0.2486a | +0.0000a | [+0.0000a, +0.0000a] |
| `fold` | -1.0000a | 0.0000a | +1.2331a | [+0.7459a, +1.7204a] |

#### Scomposizione di Call, continuation current

| Terminale | Street | Probabilità | EV condizionato | Contributo EV |
| --- | --- | ---: | ---: | ---: |
| `showdown_river` | `river` | 19.35% | +2.7960a | +0.5410a |
| `fold_preflop` | `preflop` | 18.80% | -2.0000a | -0.3760a |
| `fold_postflop` | `flop` | 26.85% | +0.4485a | +0.1204a |
| `all_in_postflop_exact` | `flop` | 3.45% | -1.2337a | -0.0426a |
| `all_in_postflop_exact` | `turn` | 2.90% | +1.4532a | +0.0421a |
| `fold_postflop` | `turn` | 16.45% | -0.2401a | -0.0395a |
| `fold_postflop` | `river` | 12.20% | -0.1015a | -0.0124a |

| Continuation preflop | Terminale | Probabilità | EV condizionato | Contributo EV |
| --- | --- | ---: | ---: | ---: |
| BTN:check | `showdown_river` | 17.70% | +3.2272a | +0.5712a |
| BTN:all_in → CO:fold | `fold_preflop` | 14.20% | -2.0000a | -0.2840a |
| BTN:raise_6 → CO:call | `all_in_postflop_exact` | 1.30% | +10.7143a | +0.1393a |
| BTN:check | `fold_postflop` | 22.15% | +0.5338a | +0.1182a |
| BTN:check | `all_in_postflop_exact` | 1.60% | -6.0714a | -0.0971a |

Le righe per street non sono additive: ogni riga riusa il payoff terminale dell'intera mano e lo condiziona al raggiungimento della street.

| Street raggiunta | Reach | EV finale condizionato | Reach × EV finale |
| --- | ---: | ---: | ---: |
| `flop` | 81.20% | +0.7502a | +0.6091a |
| `turn` | 50.90% | +1.0438a | +0.5313a |
| `river` | 31.55% | +1.6756a | +0.5286a |

| Street | Player | Bucket | Reach | EV finale condizionato | Reach × EV finale |
| --- | --- | ---: | ---: | ---: | ---: |
| `flop` | `BTN` | `1588398071236634728` | 13.30% | +2.4285a | +0.3230a |
| `river` | `CO` | `1673623324237569044` | 1.90% | +16.3271a | +0.3102a |
| `flop` | `BTN` | `348747726282816473` | 15.75% | +1.7502a | +0.2756a |
| `turn` | `CO` | `15448197710411062686` | 1.95% | +13.7666a | +0.2684a |
| `flop` | `BTN` | `15952815874057380290` | 7.70% | +2.6094a | +0.2009a |
| `river` | `CO` | `5656253081206282873` | 0.85% | +21.2250a | +0.1804a |
| `river` | `CO` | `5326434114389160536` | 0.80% | +22.4249a | +0.1794a |
| `turn` | `CO` | `2089537245576346375` | 0.75% | +21.8023a | +0.1635a |

## Decisione consentita

La trace identifica il meccanismo interno che sostiene Call e i rami che ne producono l'EV. Non
autorizza ancora un cambiamento al trainer: nessuno dei sei confronti con la migliore alternativa
osservata supera l'intervallo simultaneo al 95%. La frequenza Monker resta un confronto
descrittivo perché il suo contratto completo non sarà disponibile.

## Implementazione

- `HuPreflopSolveOptions` accetta una lista opt-in di classi e un budget di deal per classe.
- Il solver forza le cinque azioni root sugli stessi deal fisici condizionati e sugli stessi seed di
  continuation, senza aggiornare regret o strategy sum.
- L'export versionato conserva policy media e corrente, stato dei regret, vantaggi osservati nel
  training, rami preflop, terminali, street e bucket.
- La CLI può scrivere il sidecar separato; lo script di analisi produce tabelle paired e intervalli
  simultanei Bonferroni.
- I limiti sono 16 classi, 10.000 deal per classe e 20.000 class-deal complessivi.

## Validazione

| Controllo | Esito |
| --- | --- |
| Build MSVC Release | PASS |
| Test mirato telemetry/trace | PASS, 4.537 asserzioni |
| Suite HU Release | PASS, 8/8 in 6,97 s |
| Smoke CLI media+corrente | PASS |
| Run V17 seed 1 | PASS, 2.000.000 iterazioni, 1.567.910 infoset, 2.546,17 s |
| Non-mutazione rispetto a V17 | PASS bit per bit per strategia, regret e vantaggi root |
| Analizzatore del sidecar | PASS, 3 classi e 2.000 deal per azione |

Il massimo di memoria privata osservato durante i controlli periodici è circa 645 MB. Non è una
misura strumentata del picco assoluto. Il candidato completo ha SHA-256
`5E046AA7A20EA19F9F9EBF0C37B735774F3815E95D69FB23CE32CB377DE6BA14`; il log ha SHA-256
`53DE7363726C94362660A498583D3536F3C4E49BC5117688DBAF9E7774E8A69C`.

## Interpretazione

Call viene scelto perché, a fine training, è l'unica azione con regret cumulativo positivo in
tutte e tre le classi. Il regret matching produce quindi una policy corrente 100% Call. La
strategy sum spiega le frequenze medie: 90,45% per JTo, 85,70% per QJo e 96,50% per J9s.

La rivalutazione congelata non dimostra però che Call sia l'azione migliore. QJo e J9s la
favoriscono nei valori puntuali; JTo media è quasi indifferente e JTo corrente assegna a Call
`0,5922a` meno di raise 6. Tutti gli intervalli simultanei attraversano zero. La diagnosi più
precisa è quindi: il trainer ha accumulato evidenza storica a favore di Call, ma 2.000 deal per
azione non bastano a confermare che tale preferenza valga ancora contro le continuation finali.

## Limiti

- La trace completa usa il seed 1. Il pattern di regret 100% Call è stato verificato anche
  nell'artefatto V17 seed 2, ma la rivalutazione paired del seed 2 non è stata eseguita.
- Gli intervalli misurano il gioco astratto V8 e non certificano NashConv.
- L'inizializzazione RNG comune non rende identici i percorsi dopo che le azioni divergono.
- Il contratto postflop Monker è permanentemente incompleto; WMAE e TV verso Monker non possono
  determinare correttezza o promozione.

## Passo successivo

Rendere la reference esterna `EXTERNAL_CONTRACT_INCOMPLETE` a livello di fixture, schema e
comparatore, così nessun risultato futuro possa essere qualificato o respinto usando Monker come
gate di correttezza.
