# Fase 6 — Prototipi memoria exact

## 1. Esito

Il gate locale F6 è completato come gate di architettura memoria. Il nuovo
modulo `gtosd::memory` produce conteggi fisici exact per PF-F1/PF-F2/PF-F3,
separa tutte le categorie di memoria richieste, confronta tre layout, verifica
il round-trip lossless dei buffer CFR e misura una sonda RSS out-of-core.

La selezione è:

| Ruolo | Prototipo | Motivazione |
|---|---|---|
| Primary PF-F1 | Lazy in-RAM | 5,236 GiB, nessun I/O, sotto il budget di 12 GiB |
| Fallback exact | Out-of-core | Cache LRU limitata e peak RSS PF-F1 misurato a 16,918 MiB |
| Ricerca futura | Street decomposition | Corretta ma +1,56% su PF-F1 per i boundary lossless; il river domina |

F6 non dichiara ancora un solve Short Deck GTO. L'integrazione fra range
fisici, public tree, traversal CFR+, BR periodica e checkpoint production è F7.

## 2. Artefatti

| Artefatto | Responsabilità |
|---|---|
| `gtosd::memory` | Fixture PF, conteggi simbolici exact, breakdown, layout e round-trip |
| `gto_cli memory-lab` | Report riproducibile per benchmark/prototipo |
| `gto_cli memory-probe` | Capacità backing store, page traffic e peak RSS |
| `postflop_memory_benchmarks.json` | Fixture versionata, full range fisico non random |
| `gtosd_phase6_tests` | Nove report, parità checkpoint/EV/NashConv, errori e probe |

L'API passa da `0.5.0` a `0.6.0`.

## 3. Conteggi exact comuni

| Benchmark | Nodi | Edge | Decision node | Infoset | Azioni | Range-state slot |
|---|---:|---:|---:|---:|---:|---:|
| PF-F1 | 165.774 | 165.773 | 66.336 | 30.873.216 | 66.756.096 | 77.150.304 |
| PF-F2 | 9.119.010 | 9.119.009 | 3.326.100 | 1.547.151.936 | 4.009.059.648 | 4.241.796.768 |
| PF-F3 | 75.265.098 | 75.265.097 | 26.677.680 | 12.409.482.624 | 32.978.723.712 | 35.010.827.808 |

Il tree estimator usa una ricorrenza simbolica: le azioni dipendono dallo stato
monetario e dalla street, quindi un ramo chance rappresentativo può essere
moltiplicato per 33 turn e 32 river preservando esattamente il numero di
outcome fisici. Il builder eager F3 resta invariato.

## 4. Breakdown PF-F1

| Categoria | Byte |
|---|---:|
| Public tree compatto | 9.283.320 |
| Indice infoset | 740.957.184 |
| Action descriptor | 267.024.384 |
| Regret | 534.048.768 |
| Strategy | 534.048.768 |
| Reach | 1.234.404.864 |
| Best response | 1.234.404.864 |
| Checkpoint staging lazy/street | 1.068.097.536 |
| Boundary street decomposition | 166.298.880 |

## 5. Parità strategica

Fixture: Leduc, CFR+, 100 iterazioni, average strategy F5.

| Verifica | Esito |
|---|---|
| Lazy checkpoint round-trip | Byte-identico |
| Street checkpoint round-trip | Byte-identico |
| Out-of-core checkpoint round-trip | Byte-identico |
| EV CO/BTN | Delta `0` |
| NashConv | Delta `0` |
| NashConv di riferimento | `0,0798388` |

La parità usa il reference game certificato F5 perché il finite game Short Deck
postflop completo viene costruito in F7.

## 6. Stima PRE-FULL

La stima richiesta è pubblicata come **upper bound fisico conservativo**:
backing/peak di un flop fisso moltiplicato per i 7.140 flop fisici. Non applica
condivisione cross-flop, compressione o deduplica canonica fra file.

| Base | Proiezione PRE-FULL |
|---|---:|
| Lazy PF-F1 | 40.143.005.572.320 B, 36,510 TiB |
| Street PF-F1 | 32.516.789.165.280 B, 29,574 TiB |
| Out-of-core PF-F1 | 32.516.789.165.280 B, 29,574 TiB |

Il valore dimostra che PRE-FULL non può essere ottenuto replicando
ingenuamente 7.140 solve PF-F1. F14–F15 dovranno usare isomorfismo cross-flop,
subgame sharing e misure dedicate; nessun bucketing viene autorizzato.

## 7. Gate F6

| Criterio | Esito | Evidenza |
|---|---:|---|
| PF-F1 exact entro 12 GiB | PASS | Lazy 5,236 GiB; street 5,318 GiB; probe out-of-core RSS 16,918 MiB |
| Parità strategica | PASS | Checkpoint byte-identico, delta EV e NashConv zero |
| Stima preflop pubblicata | PASS | Upper bound fisico esplicito |
| Nessun bucketing | PASS | Full 630 fixture, combo condizionate 528/496/465 |

## 8. Verifiche

| Verifica | Risultato |
|---|---|
| MSVC Release `/W4 /WX` | PASS, suite completa 10/10 |
| MSVC Debug `/W4 /WX` | PASS, suite completa 10/10; F6 focalizzata 6,39 s |
| MSVC AddressSanitizer | PASS, F6 focalizzata 18,42 s |
| Suite F6 Release | PASS, 69 asserzioni |
| PF-F1/F2/F3 × tre prototipi | PASS, nove report |
| RSS probe PF-F1 memory-mapped/LRU | PASS, 17.739.776 B |
| Benchmark planner 1/2/4/8 thread | PASS |
| clang-format | PASS, dry-run `--Werror` |
| clang-tidy | PASS su `gtosd::memory` e CLI, zero warning finali |
| Install tree | PASS, libreria/header/fixture e CLI 0.6.0 |
| File temporaneo probe | Rimosso |

La verifica GCC UBSan resta affidata alla job Linux CI: WSL espone GCC 13.3 ma
non dispone di CMake nella distribuzione locale corrente.

## 9. Prossima fase

La prossima milestone è **Fase 7 — HU postflop CLI production**:

1. collegare public tree, range fisici e infoset al traversal CFR+;
2. usare lazy in-RAM come primary e out-of-core come fallback;
3. implementare validate/estimate/solve/pause/resume/cancel;
4. calcolare BR e NashConv periodiche;
5. rifiutare preventivamente configurazioni oltre RAM o disco;
6. dimostrare PF-F1 sotto `1%` del pot.
