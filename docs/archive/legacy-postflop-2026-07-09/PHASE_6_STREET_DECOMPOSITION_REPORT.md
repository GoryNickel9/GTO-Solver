# Fase 6 — Report prototipo B: Street decomposition

## 1. Contratto

Il prototipo separa flop, turn e river, conserva counterfactual value boundary
lossless e prevede la recovery completa della strategia. Ogni partizione
mantiene indice infoset, regret, strategy, reach, best response e staging del
checkpoint. Nessun outcome viene eliminato o bucketizzato.

## 2. Risultati

| Benchmark | Peak previsto | Boundary | Backing completo | Delta vs lazy |
|---|---:|---:|---:|---:|
| PF-F1 | 5.709.935.616 B, 5,318 GiB | 166.298.880 B | 4.554.172.152 B | +1,56% |
| PF-F2 | 319.652.146.176 B, 297,699 GiB | 3.723.785.472 B | 253.561.000.536 B | +0,61% |
| PF-F3 | 2.626.868.648.448 B, 2,389 TiB | 32.513.657.088 B | 2.081.963.392.536 B | +0,66% |

La partizione river resta dominante:

| Benchmark | Infoset flop | Infoset turn | Infoset river |
|---|---:|---:|---:|
| PF-F1 | 3.168 | 425.568 | 30.444.480 |
| PF-F2 | 15.840 | 8.216.736 | 1.538.919.360 |
| PF-F3 | 148.896 | 69.498.528 | 12.339.835.200 |

## 3. Scaling del planner

| Thread | Report PF-F1/s |
|---:|---:|
| 1 | 7.324 |
| 2 | 16.064 |
| 4 | 22.754 |
| 8 | 35.058 |

## 4. Decisione

Il prototipo passa PF-F1 entro 12 GiB, ma **non viene selezionato come
primary**: la sola separazione per street non riduce il picco perché il river
contiene quasi tutti gli infoset e i boundary lossless flop→turn e turn→river
aggiungono l'1,56% su PF-F1. Rimane una base utile per una futura decomposizione
river più fine e per boundary CFR-D, senza anticiparne le garanzie.
