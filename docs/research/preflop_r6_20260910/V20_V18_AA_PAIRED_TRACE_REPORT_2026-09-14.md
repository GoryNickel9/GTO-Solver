# V20 — Trace paired V18 per AA

Data: 2026-09-14  
Stato: `ENGINEERING_PASS / LOCAL ACTION EV INCONCLUSIVE / NASHCONV NOT CERTIFIED`  
Input: `docs\research\preflop_r6_20260910\v20_v18_seed1_aa10k_root_decision_trace_v1.json`  
SHA-256: `86BF835A764057244C115431B32F5B57741CB2191572DF11144F28738AC9AB1C`  
Tree: `fnv1a64:a68337fa567aa2d9`  
Algoritmo: `linear_mccfr_v1_opponent_pass_average_deterministic_batch32_workers8_root_action_rollouts4_v1_continuation_mean_updates_v1_symmetric_traverser_mean_updates_v1_root_common_random_numbers_v1_full_current_profile_evaluation_v1_exact_preflop_all_in_expectation_v1_exact_postflop_all_in_flop_turn_v1_frozen_postflop_rollout_refinement_v1`  
Astrazione: `preflop_exact81_postflop_distributional_strength_mc8_capacity_32_128_512_street_adaptive_category_equity_profile_v8_current_observation_imperfect_recall`  
Evaluator: `seven_card_table_v1:2236291214962974841`  
Iterazioni: `4000000`  
Seed training/partizione/valutazione: `5923736619020283393` / `5923736619020287489` / `5923736619020279297`

## Ambito

La trace legge la policy dopo il training. Per ogni classe usa gli stessi deal fisici e lo stesso stato RNG iniziale per le cinque azioni root. Regret, strategy sum e policy non vengono aggiornati. Gli intervalli descrivono questa valutazione campionata; non sono una NashConv né una prova di equivalenza con Monker. Le tabelle mostrano intervalli puntuali; la lettura contro la migliore alternativa osservata applica Bonferroni ai quattro confronti con Call.

Il protocollo preregistrato è in
[`V20_V18_AA_PAIRED_TRACE_PROTOCOL_2026-09-14.md`](V20_V18_AA_PAIRED_TRACE_PROTOCOL_2026-09-14.md).
Il confronto locale viene eseguito prima della NashConv globale perché può identificare una
deviazione root redditizia senza costruire la best response dell'intero gioco.

## Esito sintetico

| Classe | Continuation | Call nella policy | Migliore alternativa osservata | Call − alternativa | IC simultaneo 95% | Esito |
| --- | --- | ---: | --- | ---: | --- | --- |
| `AA` | `average` | 100.00% | `raise_6` | +0.2267a | [-0.1426a, +0.5960a] | non separato |
| `AA` | `current` | 100.00% | `raise_6` | -0.1052a | [-0.4672a, +0.2567a] | non separato |

## Confronto con la stima precedente

| Continuation media | Valutazione V18 originale | Trace paired 10k |
| --- | ---: | ---: |
| EV Call | +7.2405a, 188 campioni | +7.0210a, 10.000 campioni |
| EV Raise 6 | +8.2008a, 188 campioni | +6.7942a, 10.000 campioni |
| Call − Raise 6 | -0.9603a | +0.2267a |

Il vantaggio puntuale di Raise 6 non si replica. Con 10.000 deal Call è la migliore azione
osservata contro la continuation media, ma il suo vantaggio su Raise 6 non supera l'intervallo
paired al 95%. Contro la continuation corrente Raise 6 precede Call di `0,1052a`, ancora senza
separazione statistica.

## AA

Deal condizionati per azione: `10000`. Ultimo update dell'infoset root: `3999974`.

| Azione | Strategia media | Strategia corrente | Regret cumulativo positivo | Strategy sum | Vantaggio medio nel training |
| --- | ---: | ---: | ---: | ---: | ---: |
| `all_in` | 0.00% | 0.00% | 0 | 29.0863 | -0.6040a |
| `raise_6` | 0.00% | 0.00% | 0 | 67383.8 | -0.2046a |
| `raise_10` | 0.00% | 0.00% | 0 | 355243 | -0.4235a |
| `call` | 100.00% | 100.00% | 583665 | 7.68475e+10 | +0.0000a |
| `fold` | 0.00% | 0.00% | 0 | 14.45 | -7.8487a |

### Lettura causale

- Call è l'unica azione con regret cumulativo positivo (583665). Il regret matching assegna quindi a Call il 100.00% della strategia corrente.
- La frequenza media di Call (100.00%) deriva dalla strategy sum accumulata: 7.68475e+10 su 7.6848e+10.
- Con la continuation media, Call − raise_6 vale +0.2267a (IC simultaneo 95% Bonferroni [-0.1426a, +0.5960a]): il campione paired non separa le due azioni.
- Con la continuation corrente, Call − raise_6 vale -0.1052a (IC simultaneo 95% Bonferroni [-0.4672a, +0.2567a]): il campione paired non separa le due azioni.

#### Continuation average

| Azione | EV | SE | Call − azione | IC 95% puntuale |
| --- | ---: | ---: | ---: | --- |
| `all_in` | +6.4352a | 0.0782a | +0.5858a | [+0.3030a, +0.8686a] |
| `raise_6` | +6.7942a | 0.1085a | +0.2267a | [-0.0630a, +0.5165a] |
| `raise_10` | +6.6139a | 0.0995a | +0.4071a | [+0.1154a, +0.6988a] |
| `call` | +7.0210a | 0.1529a | +0.0000a | [+0.0000a, +0.0000a] |
| `fold` | -1.0000a | 0.0000a | +8.0210a | [+7.7213a, +8.3207a] |

#### Scomposizione di Call, continuation average

| Terminale | Street | Probabilità | EV condizionato | Contributo EV |
| --- | --- | ---: | ---: | ---: |
| `all_in_preflop_exact` | `preflop` | 21.12% | +20.4161a | +4.3119a |
| `showdown_river` | `river` | 21.97% | +3.7419a | +0.8221a |
| `fold_preflop` | `preflop` | 8.93% | +7.3046a | +0.6523a |
| `fold_postflop` | `flop` | 18.73% | +2.6158a | +0.4899a |
| `fold_postflop` | `turn` | 13.16% | +3.5069a | +0.4615a |
| `fold_postflop` | `river` | 8.77% | +2.1465a | +0.1883a |
| `all_in_postflop_exact` | `flop` | 3.14% | +3.2757a | +0.1029a |
| `all_in_postflop_exact` | `turn` | 4.18% | -0.1880a | -0.0079a |

| Continuation preflop | Terminale | Probabilità | EV condizionato | Contributo EV |
| --- | --- | ---: | ---: | ---: |
| BTN:all_in → CO:call | `all_in_preflop_exact` | 9.08% | +21.5751a | +1.9590a |
| BTN:raise_6 → CO:all_in → BTN:call | `all_in_preflop_exact` | 7.02% | +19.2976a | +1.3547a |
| BTN:check | `showdown_river` | 21.89% | +3.7373a | +0.8181a |
| BTN:raise_10 → CO:all_in → BTN:call | `all_in_preflop_exact` | 2.50% | +19.9172a | +0.4979a |
| BTN:raise_6 → CO:all_in → BTN:fold | `fold_preflop` | 6.81% | +7.0000a | +0.4767a |

Le righe per street non sono additive: ogni riga riusa il payoff terminale dell'intera mano e lo condiziona al raggiungimento della street.

| Street raggiunta | Reach | EV finale condizionato | Reach × EV finale |
| --- | ---: | ---: | ---: |
| `flop` | 69.95% | +2.9404a | +2.0568a |
| `turn` | 48.08% | +3.0449a | +1.4640a |
| `river` | 30.74% | +3.2868a | +1.0103a |

| Street | Player | Bucket | Reach | EV finale condizionato | Reach × EV finale |
| --- | --- | ---: | ---: | ---: | ---: |
| `flop` | `CO` | `673085710337625475` | 14.45% | +5.4132a | +0.7822a |
| `flop` | `BTN` | `1588398071236634728` | 11.10% | +4.4172a | +0.4903a |
| `flop` | `BTN` | `348747726282816473` | 12.97% | +3.5328a | +0.4582a |
| `flop` | `BTN` | `5157229796808569454` | 11.53% | +3.3654a | +0.3880a |
| `turn` | `CO` | `13740024092369919811` | 3.02% | +12.0549a | +0.3641a |
| `flop` | `BTN` | `12852950528333317551` | 8.23% | +4.4005a | +0.3622a |
| `river` | `CO` | `5326434114389160536` | 2.09% | +16.8768a | +0.3527a |
| `flop` | `CO` | `7440382575549460919` | 7.14% | +4.3648a | +0.3116a |

#### Continuation current

| Azione | EV | SE | Call − azione | IC 95% puntuale |
| --- | ---: | ---: | ---: | --- |
| `all_in` | +6.0743a | 0.0773a | +0.7560a | [+0.4762a, +1.0358a] |
| `raise_6` | +6.9355a | 0.0992a | -0.1052a | [-0.3892a, +0.1788a] |
| `raise_10` | +6.9137a | 0.0949a | -0.0833a | [-0.3730a, +0.2064a] |
| `call` | +6.8303a | 0.1509a | +0.0000a | [+0.0000a, +0.0000a] |
| `fold` | -1.0000a | 0.0000a | +7.8303a | [+7.5345a, +8.1262a] |

#### Scomposizione di Call, continuation current

| Terminale | Street | Probabilità | EV condizionato | Contributo EV |
| --- | --- | ---: | ---: | ---: |
| `all_in_preflop_exact` | `preflop` | 18.83% | +20.7200a | +3.9016a |
| `fold_preflop` | `preflop` | 9.57% | +8.6594a | +0.8287a |
| `showdown_river` | `river` | 22.65% | +3.4061a | +0.7715a |
| `fold_postflop` | `flop` | 19.01% | +2.4953a | +0.4743a |
| `fold_postflop` | `turn` | 13.60% | +3.3524a | +0.4559a |
| `all_in_postflop_exact` | `flop` | 3.34% | +5.0382a | +0.1683a |
| `fold_postflop` | `river` | 9.14% | +1.4429a | +0.1319a |
| `all_in_postflop_exact` | `turn` | 3.86% | +2.5426a | +0.0981a |

| Continuation preflop | Terminale | Probabilità | EV condizionato | Contributo EV |
| --- | --- | ---: | ---: | ---: |
| BTN:all_in → CO:call | `all_in_preflop_exact` | 8.12% | +22.9952a | +1.8672a |
| BTN:raise_6 → CO:raise_10_5 → BTN:all_in → CO:call | `all_in_preflop_exact` | 7.38% | +19.0999a | +1.4096a |
| BTN:check | `showdown_river` | 22.53% | +3.3355a | +0.7515a |
| BTN:raise_10 → CO:all_in → BTN:call | `all_in_preflop_exact` | 2.33% | +19.2438a | +0.4484a |
| BTN:raise_10 → CO:all_in → BTN:fold | `fold_preflop` | 3.97% | +11.0000a | +0.4367a |

Le righe per street non sono additive: ogni riga riusa il payoff terminale dell'intera mano e lo condiziona al raggiungimento della street.

| Street raggiunta | Reach | EV finale condizionato | Reach × EV finale |
| --- | ---: | ---: | ---: |
| `flop` | 71.60% | +2.9330a | +2.1001a |
| `turn` | 49.25% | +2.9593a | +1.4574a |
| `river` | 31.79% | +2.8417a | +0.9034a |

| Street | Player | Bucket | Reach | EV finale condizionato | Reach × EV finale |
| --- | --- | ---: | ---: | ---: | ---: |
| `flop` | `CO` | `673085710337625475` | 14.71% | +5.3995a | +0.7943a |
| `flop` | `BTN` | `348747726282816473` | 13.10% | +3.7944a | +0.4971a |
| `flop` | `BTN` | `5157229796808569454` | 12.13% | +3.8707a | +0.4695a |
| `flop` | `BTN` | `1588398071236634728` | 11.18% | +4.1391a | +0.4628a |
| `river` | `CO` | `5326434114389160536` | 2.23% | +18.0188a | +0.4018a |
| `turn` | `CO` | `13740024092369919811` | 3.20% | +11.9003a | +0.3808a |
| `flop` | `BTN` | `12852950528333317551` | 8.63% | +3.9309a | +0.3392a |
| `flop` | `CO` | `7440382575549460919` | 7.15% | +4.1493a | +0.2967a |

## Decisione consentita

La trace non conferma che V18 scelga un'azione meno redditizia con `AA`. Non autorizza modifiche
al trainer: la continuation media favorisce Call nel valore puntuale e la continuation corrente
lascia Call e Raise 6 entro l'intervallo di indifferenza campionato.

## NashConv

Il run conserva la best response campionata V18 con 5.000 iterazioni e 10.000 deal di valutazione.
Il risultato serializza `nashconv_certified=false`; `normalized_nashconv=0` appartiene allo scope
legacy `legacy_sum_of_sampled_response_values_not_certified_use_sampled_response_lower_bound`.
Anche il sampled-response lower bound è zero perché le response apprese non migliorano il profilo
nel campione. Nessuno dei due zeri certifica convergenza.

La misura locale più vicina a un deviation gain è:

- continuation media: massimo vantaggio osservato contro la policy root `0a`, perché Call ha
  l'EV puntuale maggiore;
- continuation corrente: Raise 6 supera Call di `0,1052a`, con IC simultaneo
  `[-0,4672a; +0,2567a]`.

Questi valori riguardano un solo information set e non si sommano in una NashConv globale.

## Validazione

| Controllo | Esito |
| --- | --- |
| Replica strategia V18 seed 1 | PASS, identica bit per bit |
| Replica regret root | PASS, identici bit per bit |
| Replica vantaggi root | PASS, identici bit per bit |
| Replica algoritmo e root EV | PASS, identici bit per bit |
| Trace | PASS, 10.000 deal per azione, policy media e corrente |
| Analizzatore schema e valori | PASS |
| Oracolo exact | PASS, `69,19 s` |
| Solve | PASS, 4.000.000 iterazioni, 1.567.910 infoset, `2.954,52 s` |

SHA-256 della trace:
`86BF835A764057244C115431B32F5B57741CB2191572DF11144F28738AC9AB1C`.
Il candidato diagnostico usa
`7C2F43A4974DFCA75B0C256FAF2B792019F9E93C6E0AE5AC8639A9A5A114DF03`; il log usa
`42A894F2218574E48B80C0D30C2392CF918CDF2683A446DB74D6DC2E5766C896`.

## Uso di Monker

Monker resta una stima esterna per misurare WMAE, TV, forma dei range e priorità delle anomalie.
Il contratto incompleto ne riduce il peso: non può essere l'unico criterio di promozione o rifiuto,
ma un miglioramento coerente con EV interno, stabilità fra seed e NashConv costituisce evidenza
favorevole.
