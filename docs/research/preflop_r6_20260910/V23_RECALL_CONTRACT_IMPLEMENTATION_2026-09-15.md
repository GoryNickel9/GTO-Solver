# V23 — Implementazione del contratto perfect recall

Data: 2026-09-15
Stato: `PHASE_2 HU10 K8 TWO-CORPUS TARGET MET / K-SENSITIVITY PENDING`

## Risultato

Il solver dispone di una modalità postflop V23 che riusa il mapping V8 e conserva la classe
preflop più tutti i bucket osservati. V17 e la modalità V8 restano invariati.

Il runner esporta inoltre un audit strutturale del contratto di recall. Lo smoke V8 registra sei
omissioni:

| Decisione | Osservazione dimenticata |
| --- | --- |
| Flop | classe preflop |
| Turn | classe preflop |
| Turn | bucket flop |
| River | classe preflop |
| River | bucket flop |
| River | bucket turn |

Lo smoke V23 non registra omissioni e restituisce `PASS_PERFECT_RECALL`.

Il compilatore HU10 materializza ora un `FiniteGame` intero usando un corpus chance stratificato
e congelato. Linear MCCFR e il certificatore esatto leggono lo stesso fingerprint. Il primo smoke
K=1 ha prodotto una NashConv whole-game esatta per il gioco empirico; non ha raggiunto il target
di convergenza e non certifica la distribuzione fisica completa.

Il run qualificante K=8/MC8 ha poi raggiunto `normalized_dev=0,0081173483` a 5.000.000
iterazioni. Il certificato vale per il solo corpus finito identificato; la stabilità fra K e seed
resta un controllo separato.

## Identità V23

La nuova rappresentazione è:

```text
DistributionalStrengthStreetAdaptivePerfectRecallV23
```

Il relativo identificatore serializzato è:

```text
preflop_exact81_postflop_distributional_strength_mc4_capacity_32_128_512_
street_adaptive_category_equity_profile_v8_full_history_v23_perfect_recall
```

Il numero di campioni MC fa parte dell'identificatore; lo smoke usa MC4, mentre il protocollo dei
run qualificanti richiede MC8.

La policy usa il formato `1.12`. Una policy V23 etichettata come `1.11` viene respinta. Le policy
V8 precedenti restano leggibili.

## Modifiche

| File | Modifica |
| --- | --- |
| `include/gtosd/preflop/hu_preflop.hpp` | enum V23, strutture dell'audit e formato policy 1.12 |
| `libs/preflop/src/hu_preflop_solver.cpp` | chiavi full-history, mapping V8 condiviso, validazione e audit |
| `libs/preflop/src/hu_preflop_sampled_persistence.cpp` | lettura e scrittura della rappresentazione V23 |
| `benchmarks/hu_preflop_solve.cpp` | flag CLI, identità leggibile e audit nel JSON |
| `tests/hu_preflop_abstraction_tests.cpp` | regressioni V8/V23, persistenza e chiavi invalide |
| `include/gtosd/preflop/hu_preflop_abstract_game.hpp` | contratto del corpus e gioco finito |
| `libs/preflop/src/hu_preflop_abstract_game.cpp` | corpus stratificato, compiler e collision audit |
| `benchmarks/hu_preflop_abstract_nashconv.cpp` | Linear MCCFR, BR esatte e certificato JSON |
| `tests/hu_preflop_abstract_game_tests.cpp` | determinismo, resource gate e NashConv esatta |
| `schemas/hu_preflop_v23_abstract_nashconv.schema.json` | schema del certificato |
| `docs/specifications/CLI.md` | comando e semantica dell'audit |
| `libs/solver/src/solver.cpp` | strategia lazy e delta sparse per MCCFR sul `FiniteGame` |

## Comando smoke V23

```powershell
out/build/windows-release-current/benchmarks/gtosd_hu_preflop_solve.exe `
  --config benchmarks/fixtures/hu_preflop_co40_game_v1.json `
  --output .tmp/v23_perfect_recall_smoke.json `
  --postflop-policy-output .tmp/v23_perfect_recall_smoke_policy.bin `
  --postflop-distributional-street-adaptive-perfect-recall-v23 `
  --iterations 64 --evaluation-deals 256 `
  --br-iterations 16 --br-evaluation-deals 256 `
  --equity-samples 4 `
  --flop-buckets 32 --turn-buckets 128 --river-buckets 512 `
  --algorithm external_sampling `
  --seed 5923076415064866305 `
  --partition-seed 5923076415064862721 `
  --evaluation-seed 5923076415064865793
```

Risultato osservato:

| Campo | Valore |
| --- | ---: |
| Stato | PASS |
| Iterazioni | 64 |
| Information set | 6.513 |
| Tempo solve | 0,0967719 s |
| Audit | `PASS_PERFECT_RECALL` |
| Formato policy | 1.12 |

Il valore `normalized_abstract_nashconv=0,00554929` di questo smoke deriva ancora dalle response
campionate esistenti. Non è il certificato NashConv V23.

## Test

| Controllo | Esito |
| --- | --- |
| Build Release con warning come errori | PASS |
| Test astrazione mirato | PASS, 119.202 asserzioni |
| Persistenza V23 e checksum | PASS |
| Rifiuto policy V23 formato 1.11 | PASS |
| Rifiuto chiave River senza bucket Flop | PASS |
| Smoke V8 con sei witness | PASS |
| Smoke V23 senza witness | PASS |
| Regressione HU preflop completa | PASS, 13/13 in 520,08 s |

## Smoke whole-game HU10

Configurazione: `K=1`, MC4, bucket `32/128/512`, 64 iterazioni Linear MCCFR e due checkpoint.

| Campo | Valore |
| --- | ---: |
| Deal chance | 81 |
| Nodi finiti | 166.780 |
| Decision node | 65.772 |
| Information set raggiungibili | 65.212 |
| Payload materializzato minimo | 28.021.033 B |
| Tempo compilazione | 4,354769 s |
| Tempo Linear MCCFR | 5,6352631 s |
| Tempo NashConv esatta | 2,8153457 s |
| NashConv finale | 6,8213295505a |
| `normalized_dev` finale | 1,3439417570 |
| Target `< 0,01` | FAIL |

Il certificato usa lo stato `CERTIFIED_ABSTRACT_EMPIRICAL_CHANCE_TARGET_NOT_MET`:
la metrica è calcolata esattamente sul gioco finito, ma la strategia da 64 iterazioni è ancora
fortemente sfruttabile. Un run 32→64 ripreso produce checkpoint byte-identico al run continuo e
la stessa NashConv finale.

## Policy completion e coverage audit

Il modulo best response espone ora `complete_strategy_profile`. Il contratto
`uniform_unseen_v1` costruisce una strategia valida per il gioco target usando la policy sorgente
dove player e azioni coincidono e una distribuzione uniforme esplicita sulle chiavi mancanti.
`reject_missing` mantiene la modalità stretta. Le chiavi sorgente che non appartengono al target
non entrano nel profilo completato e vengono contate come inutilizzate.

L'audit registra copertura esatta delle chiavi, copertura pesata per reach on-policy, nodi e massa
non coperti, componenti per giocatore e fingerprint del gioco target. Il test su Matching Pennies
verifica una policy con copertura chiavi e reach pari a `0,5`. Il test V23 porta una policy K=1 dal
corpus seed 1 a un corpus differente: trova 55.450 information set mancanti,
`exact_key_coverage=0,149305` e `reach_weighted_coverage=0,765321`. La modalità stretta rifiuta lo
stesso trasferimento.

Il certificato V23 usa ora lo schema `gtosd.hu_preflop_v23_abstract_nashconv.v2` e persiste tutti
i campi dell'audit. Un run HU10 K=1/16 sul corpus di training riporta copertura chiavi e reach pari
a `1`; il calcolo dell'audit richiede `0,3526849 s`. Lo schema mantiene compatibilità con i
certificati v1.

## Limiti

L'audit della rappresentazione verifica lo schema della chiave. Il compilatore whole-game
controlla inoltre le collisioni della public history e impone lo stesso fingerprint a trainer e
certificatore. Questi controlli valgono per le history materializzate dal corpus, non per ogni deal
della distribuzione fisica completa.

V23 non dispone ancora di:

- persistenza autonoma del corpus chance e del `FiniteGame` compilato;
- generatore separato dei corpus response/evaluation e relativi intervalli di confidenza;
- benchmark di crescita `10a -> 20a -> 40a`.

La sensibilità K=2/4/8 è completata. Ogni gioco finito converge sotto il target, ma il profile EV
CO cambia di `0,2299615199` ante fra K=2 e K=4 e di `0,1548813747` ante fra K=4 e K=8. Il corpus
chance è quindi ancora instabile come approssimazione del valore fisico.

La fixture `benchmarks/fixtures/hu_preflop_hu10_calibration_v1.json` congela intanto il primo
gioco di calibrazione: stack 10a, open 3a/5a, response 6a/8a e sizing postflop invariati.

## Gate HU10 K=8

Configurazione: `K=8`, MC8, bucket `32/128/512`, 5.000.000 iterazioni Linear MCCFR, seed solver
23170, corpus 23171 e partizione 23172.

| Campo | Valore |
| --- | ---: |
| Nodi | 1.334.233 |
| Information set | 490.050 |
| Payload materializzato minimo | 224.170.652 B |
| NashConv | 0,0436142329a |
| Gain CO | 0,0243520449a |
| Gain BTN | 0,0192621880a |
| `normalized_dev` | 0,0081173483 |
| Target `< 0,01` | PASS |
| Tempo totale segmento 1M→5M | 749,2181383 s |

Il target viene superato per la prima volta a 4.500.000 iterazioni, con
`normalized_dev=0,0093218629`. Il checkpoint finale misura 59,57 MiB.

La regressione Release HU successiva all'ottimizzazione passa 13/13 test in 515,65 s. Il test
dedicato al gioco astratto esegue ora 14 asserzioni e passa in 19,22 s. La regressione mirata di
solver, best response, card abstraction e subgame passa 6/6 test in 33,37 s.

## Passo successivo

Implementare il runner HU10 a tre corpus `training/response/evaluation`, mantenendo policy e best
response congelate durante la misurazione finale.
