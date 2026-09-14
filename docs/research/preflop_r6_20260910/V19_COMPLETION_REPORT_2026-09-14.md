# V19 — Report di completamento

Data: 2026-09-14  
Piano: `V19_WMAE_TV_IMPROVEMENT_PLAN_2026-09-13.md`  
Decisione: `GLOBAL_CRN_REJECTED / GATE_C_FAIL / V17_BASELINE`

## Analisi

V19 ha verificato se il CRN globale potesse ridurre insieme WMAE e variabilità fra seed nel
solver HU Short Deck CO40. Il confronto ha mantenuto il contratto V17: Linear MCCFR, `2M`
iterazioni, V8 `32/128/512`, batch `32`, otto worker, quattro rollout, update simmetrici,
all-in preflop e flop/turn esatti, full chart e valutazione della policy corrente. C2 cambia
soltanto il CRN da root-only a globale.

Il risultato è misto ma non ambiguo. La WMAE media migliora di `0,3124 pp`, mentre la TV della
strategia media peggiora di `1,1687 pp` e gli errori Call/Fold materialmente raggiunti salgono da
7 a 10. Il Gate C richiedeva una riduzione TV di almeno il 10% e nessuna regressione Call/Fold:
V19 fallisce entrambi i criteri. Il CRN globale non viene promosso.

## Correzione del precedente blocker memoria

Il log che aveva portato a classificare V19 come `BLOCKED_MEMORY` non era un run matched. Pur
riportando capacità `32/128/512`, usava
`postflop_representation=category_equity_mc_perfect_recall`, non la rappresentazione V8
street-adaptive congelata. Inoltre non valutava la current policy. L'aumento degli infoset del
perfect recall spiega il `memory_failure`; quel run non può essere usato per giudicare C1 o C2.

I tre solve corretti a budget pieno terminano con `HU_PREFLOP_SOLVE=PASS`. Il massimo private
osservato è `2.905.116.672 B`, sotto il budget numerico di 8 GiB. La mappa telemetry viene inoltre
rilasciata dopo l'estrazione e prima dell'export della policy, evitando co-residenza non necessaria.

## Implementazione

- La telemetria action-conditioned è opt-in, versionata e limitata da un cap esplicito; esporta
  anche il numero di osservazioni scartate.
- Il merge delle mappe locali dei worker è deterministico e non entra in regret, strategy sum,
  RNG o ordine delle riduzioni.
- Il CRN globale è disponibile soltanto nel percorso batched, resta disattivato di default e usa
  il fingerprint `global_common_random_numbers_v1`.
- Il sampler ripristina lo stato RNG all'ingresso di ogni confronto fra azioni e prosegue dallo
  stato consumato dal primo ramo.
- Una fixture paired finita conserva esattamente le marginali d'azione e verifica una varianza
  della differenza inferiore rispetto al replay indipendente.

## Validazione

| Controllo automatico finale | Esito |
| --- | --- |
| Build MSVC Release dei target HU modificati | PASS |
| CTest HU telemetry/parallel/sampling/compiled/abstraction/trainer | PASS, `8/8` in `510,15 s` |
| `gtosd_hu_preflop_telemetry_tests` diretto | PASS, `4.167` asserzioni |

### Controllo C1 e non-mutazione

Il rerun C1 seed 1 con telemetria produce la stessa policy V17 byte per byte:

| Controllo | V17 seed 1 | C1 telemetry seed 1 | Esito |
| --- | --- | --- | --- |
| Policy fingerprint | `fnv1a64:3821fd86bf83ad5f` | `fnv1a64:3821fd86bf83ad5f` | PASS |
| Infoset | 1.567.910 | 1.567.910 | PASS |
| Root EV | `-0,1223930791a` | `-0,1223930791a` | PASS |
| SHA-256 policy | `CF4973D…097215` | `CF4973D…097215` | PASS |

La coppia V17 già congelata resta quindi il controllo C1. Il seed 2 non è stato ricalcolato:
ripetere lo stesso solve solo per duplicare una policy già valida non avrebbe cambiato il gate.

### Solve C2

| Campo | Seed 1 | Seed 2 |
| --- | ---: | ---: |
| Iterazioni | 2.000.000 | 2.000.000 |
| Solve | 2.644,370 s | 2.590,206 s |
| Infoset | 1.569.640 | 1.564.269 |
| Peak private | 2.739.122.176 B | 2.804.924.416 B |
| Peak working set | 2.626.953.216 B | 2.651.758.592 B |
| Peak scratch payload | 6.950.120 B | 6.854.120 B |
| Righe telemetry | 9.953 | 9.982 |
| Osservazioni scartate | 1.102.687.512 | 1.090.536.387 |
| Esito | PASS | PASS |

Entrambi i run usano l'albero `fnv1a64:a68337fa567aa2d9`, V8 `32/128/512`, CRN root e globale,
all-in esatti e valutazione della current policy. Gli stderr sono vuoti.

### Gate C

| Metrica | C1 / V17 | C2 global CRN | Delta | Gate |
| --- | ---: | ---: | ---: | --- |
| WMAE seed 1 | 14,1312 pp | 13,5811 pp | -0,5502 pp | PASS |
| WMAE seed 2 | 13,7631 pp | 13,6884 pp | -0,0747 pp | PASS |
| WMAE media | 13,9472 pp | 13,6347 pp | -0,3124 pp (-2,24%) | PASS |
| TV media fra seed | 10,9453 pp | 12,1140 pp | +1,1687 pp (+10,68%) | **FAIL** |
| TV current policy | 11,1472 pp | 14,5225 pp | +3,3753 pp (+30,28%) | diagnostica, peggiore |
| Call/Fold, reach pubblica `>=1%` | 7 | 10 | +3 | **FAIL** |
| Tempo per seed | 35,0–37,2 min baseline | 43,2–44,1 min | sotto 60 min | PASS |
| Memoria e scratch | entro budget | entro budget | nessun blocker | PASS |
| Varianza action-difference ad alto impatto | non disponibile a regime | non disponibile a regime | — | NOT EVALUATED |

La metrica di varianza ad alto impatto non è ricostruibile dai momenti marginali per azione: manca
la covarianza paired. Il cap da 10.000 righe rende inoltre la telemetria incompleta. La fixture
finita del Gate B passa, ma non autorizza a dichiarare una riduzione di varianza nel solve da 2M.
Il candidato è già respinto dai due gate osservabili; non è stato eseguito un altro solve lungo
solo per colmare questa metrica.

La reference fixture esterna resta `REFERENCE_CONFIG_INCOMPLETE`. WMAE e TV esterna sono quindi
diagnostiche, non certificano equivalenza del gioco né NashConv.

## Stato delle fasi

| Fase | Stato | Evidenza |
| --- | --- | --- |
| A — audit causale | `PASS_WITH_CAPPED_EVIDENCE` | OFF/ON byte-identico; bucket e terminali misurati a 2M |
| B — CRN globale | `ENGINEERING_AND_FINITE_FIXTURE_PASS` | marginali finite, varianza paired, determinismo 1/8 |
| C — coppia qualità | `FAIL` | TV +10,68%; Call/Fold 7 → 10 |
| D — replica indipendente | `NOT_RUN_BY_GATE` | il piano la vieta dopo C FAIL |
| E/F — ramo successivo | `ROUTED_NOT_EXECUTED` | richiede un nuovo split preregistrato o un corpus holdout fisico |
| G/H | `NOT_APPLICABLE` | dipendono dall'esito E/F |

## Decisioni

- V17 resta la baseline diagnostica e la sorgente corrente non cambia.
- Il CRN globale resta research-only e disattivato di default.
- Nessuna policy V19 entra nel viewer; nessun default di produzione viene modificato.
- Non si eseguono i seed 3/4 della Fase D, perché C ha fallito.
- Non si ripetono più iterazioni identiche del global CRN: il ramo successivo è E/F.
- Il checkout sporco preesistente è stato preservato; nessun reset, clean, commit o push.

## Artefatti principali

| Artefatto | SHA-256 |
| --- | --- |
| `.tmp/v19_c1_control_seed1.json` | `3C92EDBAC1A33AB3E812834D96A9935311C5E7388B6D903DD1A24F60E55741E7` |
| `.tmp/v19_c1_control_seed1_policy.bin` | `CF4973D1DA09317895945B61A20BF28E66860F6F3DE7E67F8311053AAD097215` |
| `.tmp/v19_c2_global_crn_seed1.json` | `DF0E4223160FC258D86DA9F8D73082211081BD51117862A5B888721F00655B17` |
| `.tmp/v19_c2_global_crn_seed1_policy.bin` | `F24321E8790499BAAA834EF992A6721F84373C790E7B567F2FD5CE46F6365285` |
| `.tmp/v19_c2_global_crn_seed2.json` | `874913075A32026A9682A086160ABA3442314C5AA6E975851A952CF495E010B0` |
| `.tmp/v19_c2_global_crn_seed2_policy.bin` | `0D017D653B1DB56BB8F9D3961EC7080BD16ECC41FD7FA3E117B5FDEAC1A304C8` |
| `.tmp/v19_c2_global_crn_seed_tv.json` | `6EADA9C1D1D177F598ABD0AE41E1FFE8525B9EE5C849951980DF11DF59E9B673` |
| `.tmp/v19_c2_global_crn_pair_policy_audit.json` | `71845DEA749D422B37EED69B63FF1DA6AB714B0425A4B190FD95C06E720DE279` |

## Passo successivo

Aprire la Fase F con un corpus fisico holdout preregistrato; solo dopo una diagnosi causale dei
continuation value ha senso scegliere uno split selettivo E.
