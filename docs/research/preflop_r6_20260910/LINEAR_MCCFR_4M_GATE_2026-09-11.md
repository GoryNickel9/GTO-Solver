# Linear MCCFR 4M: gate su due seed

## Obiettivo

Misurare la convergenza da 2M a 4M e decidere se procedere direttamente a 8M oppure intervenire su batch, sampling e rappresentazione. I due run condividono gioco, partizione `64/256/1.024`, MC8, precisione, albero, batch da 64, otto worker ed eseguibile.

Eseguibile SHA-256: `8BB2AB018B2BB5429EA2874DB83CDC1C62C88BCD47421D4B5CABEBB208C62229`.

## Esecuzione

| Metrica | Seed primario | Seed indipendente |
|---|---:|---:|
| Training seed | `5923736619020283393` | `5200000000000000102` |
| Evaluation seed | `5923736619020279297` | `5300000000000000202` |
| Wall | 672,994 s | 716,822 s |
| Peak private | 496.648.192 B | 496.668.672 B |
| Infoset | 1.770.386 | 1.765.020 |
| Payload numerico | 280.897.632 B | 279.349.488 B |
| Peak scratch | 2.476.632 B | 2.471.920 B |
| Cache evictions | 31.607.836 | 31.528.489 |

Entrambi i processi terminano con `HU_PREFLOP_SOLVE=PASS`; i file stderr sono vuoti.

## Confronto 2M contro 4M

| Metrica | Primario 2M | Primario 4M | Indipendente 2M | Indipendente 4M |
|---|---:|---:|---:|---:|
| Errore medio azioni | 18,1498 pp | **16,8359 pp** | 17,6447 pp | **16,4989 pp** |
| Miglioramento | — | **1,3139 pp** | — | **1,1458 pp** |
| TV media per classe | 45,3745 pp | **42,0898 pp** | 44,1118 pp | **41,2472 pp** |
| P95 TV | 94,5846 pp | 95,1668 pp | 94,9901 pp | **93,9432 pp** |
| Errore massimo root | 27,1694 pp | **24,7174 pp** | 27,5366 pp | **26,0741 pp** |
| EV root CO | −0,079761a | −0,340394a | −0,000722a | +0,009119a |
| Errore EV da Monker | 0,220239a | **0,040394a** | **0,299278a** | 0,309119a |
| Fold AA | 0,00000576% | 0,00000146% | 0,00000064% | 0,00000016% |

Il seed primario chiude il gate EV puntuale `<=0,05a`; il seed indipendente no. Entrambi migliorano oltre un punto WMAE e mantengono le azioni dominate prossime allo zero.

## Stabilità fra seed

| Metrica fra policy | 2M | 4M | Variazione |
|---|---:|---:|---:|
| WMAE | 10,4733 pp | **8,6158 pp** | −17,74% |
| TV media | 26,1834 pp | **21,5395 pp** | −17,74% |
| Massima differenza root aggregata | 2,5935 pp | 5,9335 pp | peggiora |

Il gate richiedeva WMAE fra seed `<=8,4 pp`, pari a una riduzione minima del 20%. Il risultato `8,6158 pp` manca il limite di `0,2158 pp`. Le classi più instabili restano AJs `85,75 pp`, J7s `77,28 pp`, QTs `71,73 pp`, T7s `68,24 pp` e Q8o `66,60 pp`.

## Media diagnostica root

La media 50/50 delle due policy root 4M ottiene:

| Metrica | Media Linear 4M | Miglior seed Linear 4M | DCFR 2M |
|---|---:|---:|---:|
| Errore medio azioni | **16,3623 pp** | 16,4989 pp | 16,9396 pp |
| TV media per classe | **40,9058 pp** | 41,2472 pp | 42,3491 pp |
| P95 TV | 89,9274 pp | 93,9432 pp | **78,8698 pp** |

La media root produce il miglior WMAE e la migliore TV osservati, ma non è una soluzione completa. Non combina le policy delle continuazioni e il suo EV medio non rappresenta l'EV del profilo misto.

## Esito del gate

**FAIL per stabilità fra seed.** Tre requisiti su quattro passano:

1. miglioramento WMAE di almeno `1 pp`: PASS su entrambi i seed;
2. distanza WMAE fra seed `<=8,4 pp`: FAIL, risultato `8,6158 pp`;
3. azioni dominate prossime a zero: PASS;
4. memoria, scratch, normalizzazione e integrità: PASS.

Il comparatore classifica entrambi i candidati `REJECTED` con `REFERENCE_CONFIG_INCOMPLETE`. NashConv resta una risposta campionata lower-bound non certificata.

## Decisione

Non procedere direttamente a 8M. Il miglioramento da 2M a 4M è reale, ma la stabilità manca il gate e alcune classi rimangono quasi disgiunte fra seed. Il passo successivo è ridurre la varianza per unità di calcolo: confrontare batch 16/32/64 e poi la partizione `32/128/512`, mantenendo invariato il resto del contratto.

## Artefatti

- `linear_mccfr_8t_4m.json` e relativo `.comparison.json`;
- `linear_mccfr_8t_4m_seed2.json` e relativo `.comparison.json`;
- `CO_MONKER_LINEAR_MCCFR_4M_COMBO_COMPARISON.md`;
- `CO_MONKER_LINEAR_MCCFR_4M_SEED2_COMBO_COMPARISON.md`;
- `linear_mccfr_4m_two_seed_root_mixture_diagnostic.json` e relativo `.comparison.json`.

