# Fase 6 — Report prototipo A: Lazy in-RAM

## 1. Contratto

Il prototipo usa public node compatti, edge compatti, indice infoset sparse,
buffer SoA `regret/strategy`, workspace separati per reach e best response e
staging lossless del checkpoint. Le chance vengono contate simbolicamente ma
con molteplicità fisica esatta; non viene introdotto bucketing.

## 2. Risultati

| Benchmark | Nodi pubblici | Infoset exact | Azioni exact | Peak previsto | Byte/nodo | Byte/infoset |
|---|---:|---:|---:|---:|---:|---:|
| PF-F1 | 165.774 | 30.873.216 | 66.756.096 | 5.622.269.688 B, 5,236 GiB | 56,000 | 181,808 |
| PF-F2 | 9.119.010 | 1.547.151.936 | 4.009.059.648 | 317.705.954.904 B, 295,887 GiB | 56,000 | 205,019 |
| PF-F3 | 75.265.098 | 12.409.482.624 | 32.978.723.712 | 2.609.622.971.928 B, 2,373 TiB | 56,000 | 209,953 |

Il conteggio degli infoset condiziona il range fisico completo alla board:
528 combo sul flop, 496 sul turn e 465 sul river.
Reachability e best response sono dimensionati più conservativamente su ogni
nodo pubblico, inclusi chance e terminali: 77.150.304 range-state slot in
PF-F1, non soltanto i 30.873.216 infoset decisionali.

## 3. Scaling del planner

Benchmark Release `BM_MemoryPlanPfF1`, una ripetizione con tempo minimo 1 s:

| Thread | Report/s |
|---:|---:|
| 1 | 8.275 |
| 2 | 16.718 |
| 4 | 29.329 |
| 8 | 40.330 |

Questa misura riguarda il kernel di pianificazione esatta, non le
iterazioni CFR del futuro solver F7.

## 4. Decisione

Il prototipo passa il budget PF-F1 di 12 GiB ed è selezionato come **primary
memory layout per F7/PF-F1**. PF-F2 e PF-F3 richiedono out-of-core o una
decomposizione più efficace; non devono essere avviati in modalità in-RAM su
hardware da 16/32 GB.
