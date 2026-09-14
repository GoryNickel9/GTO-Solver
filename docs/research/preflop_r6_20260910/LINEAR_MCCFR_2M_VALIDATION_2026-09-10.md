# Linear MCCFR 2M: esecuzione e validazione

## Obiettivo

Misurare l'effetto del passaggio da 500.000 a 2.000.000 di iterazioni sul candidato HU CO40 Linear MCCFR. Il confronto mantiene invariati gioco, seed, partizione, valutazione, precisione e parallelismo.

## Contratto eseguito

- algoritmo: `linear_mccfr_v1_opponent_pass_average_deterministic_batch64_workers8`;
- iterazioni: `2.000.000`;
- partizione distribuzionale: `64/256/1.024`, MC8;
- seed training/partition/evaluation: `5923736619020283393` / `5923736619020287489` / `5923736619020279297`;
- valutazione: 20.000 deal, risposta appresa 10.000 iterazioni e 10.000 deal;
- worker: 8, batch congelato da 64 iterazioni;
- eseguibile SHA-256: `8BB2AB018B2BB5429EA2874DB83CDC1C62C88BCD47421D4B5CABEBB208C62229`.

Il sorgente preflop era più recente dell'eseguibile R6 precedente. I target `gtosd_hu_preflop_solve` e `gtosd_hu_preflop_reference` sono quindi stati ricompilati con Visual Studio 2026 Developer Command Prompt prima dell'esecuzione.

## Validazione precedente al run

CTest Release:

```text
gtosd_external_sampling_tests      PASS
gtosd_hu_preflop_sampling_tests    PASS
gtosd_hu_preflop_parallel_tests    PASS
3/3 passed in 20.98 s
```

Una nuova baseline 500k eseguita con lo stesso binario 2M produce una strategia esattamente uguale al file R6 storico, oltre agli stessi EV, errore standard e numero di infoset. La differenza di build non altera quindi il confronto 500k contro 2M.

## Risultati

| Metrica | Linear 500k, stesso binario | Linear 2M | Variazione |
|---|---:|---:|---:|
| Wall | 89,814 s | 356,580 s | 3,97× |
| Peak private | 416.399.360 B | 475.312.128 B | +58.912.768 B |
| Infoset | 1.322.626 | 1.647.145 | +324.519 |
| Payload numerico | 216.658.512 B | 263.092.176 B | +46.433.664 B |
| EV root CO | −0,221459a | −0,079761a | +0,141698a |
| Errore standard EV | 0,119311a | 0,107600a | −0,011711a |
| Errore assoluto EV da Monker | 0,078541a | 0,220239a | +0,141698a |
| Errore medio azioni | 21,3195 pp | **18,1498 pp** | **−3,1697 pp** |
| TV media per classe | 53,2987 pp | **45,3745 pp** | **−7,9242 pp** |
| P95 TV per classe | 97,3526 pp | 94,5846 pp | −2,7681 pp |
| Errore massimo root aggregato | 30,0508 pp | 27,1694 pp | −2,8815 pp |
| Fold AA | 0,00009510% | **0,00000576%** | −0,00008934 pp |

Il costo cresce quasi linearmente con le iterazioni. La memoria cresce del 14,15%, molto meno del wall, perché il numero di infoset tende a saturare.

## Confronto con DCFR 2M

| Metrica | Linear 2M | DCFR 2M |
|---|---:|---:|
| Errore medio azioni | 18,1498 pp | **16,9396 pp** |
| TV media per classe | 45,3745 pp | **42,3491 pp** |
| P95 TV per classe | 94,5846 pp | **78,8698 pp** |
| Errore massimo root | 27,1694 pp | **26,2362 pp** |
| Errore assoluto EV | **0,220239a** | 0,233022a |

Linear 2M riduce il fold spurio delle mani dominate fino alla risoluzione numerica mostrata, ma resta più distante da Monker sulla strategia complessiva. DCFR 2M conserva il vantaggio sulle frequenze, mentre Linear ottiene un EV puntuale leggermente più vicino al riferimento.

## Esito del gate

`REJECTED`, con `configuration_comparability=REFERENCE_CONFIG_INCOMPLETE`.

Il candidato fallisce i limiti provvisori su strategia, EV puntuale e NashConv certificata. Il valore `normalized_nashconv=0` appartiene a una risposta campionata lower-bound e non certifica la convergenza. Restano sconosciuti l'albero postflop Monker, i bucket, le abstraction dipendenti dalla history e il criterio di arresto esterno.

L'aumento a 2M smentisce l'ipotesi di un plateau completo osservata tra 50k e 500k: la strategia migliora di 3,17 pp WMAE. Non basta però a qualificare Linear o a spiegare da solo lo scarto da Monker.

## Artefatti

- candidato: `linear_mccfr_8t_2m.json`;
- confronto automatico: `linear_mccfr_8t_2m.comparison.json`;
- strategia CO completa: `CO_MONKER_LINEAR_MCCFR_2M_COMBO_COMPARISON.md`;
- baseline corrente: `linear_mccfr_8t_500k_current.json`;
- log stdout/stderr: `linear_mccfr_8t_2m.stdout.log` e `linear_mccfr_8t_2m.stderr.log`.

