# Linear MCCFR 2M: validazione su due seed

## Obiettivo

Verificare se il miglioramento osservato a 2.000.000 di iterazioni sia stabile rispetto al seed e decidere se ulteriori iterazioni siano giustificate. I due run condividono gioco, partizione `64/256/1.024`, MC8, precisione, albero, batch, otto worker ed eseguibile.

## Contratto

| Campo | Seed primario | Seed indipendente |
|---|---:|---:|
| Training seed | `5923736619020283393` | `5200000000000000102` |
| Partition seed | `5923736619020287489` | `5923736619020287489` |
| Evaluation seed | `5923736619020279297` | `5300000000000000202` |
| Iterazioni | 2.000.000 | 2.000.000 |
| Worker / batch | 8 / 64 | 8 / 64 |

Eseguibile SHA-256: `8BB2AB018B2BB5429EA2874DB83CDC1C62C88BCD47421D4B5CABEBB208C62229`.

## Risultati

| Metrica | Seed primario | Seed indipendente |
|---|---:|---:|
| Wall | 356,580 s | 327,159 s |
| Peak private | 475.312.128 B | 475.127.808 B |
| Infoset | 1.647.145 | 1.648.350 |
| Payload numerico | 263.092.176 B | 263.405.808 B |
| EV root CO | −0,079761a | −0,000722a |
| Errore standard EV | 0,107600a | 0,104762a |
| Errore assoluto EV da Monker | 0,220239a | 0,299278a |
| Errore medio azioni | 18,1498 pp | **17,6447 pp** |
| TV media per classe | 45,3745 pp | **44,1118 pp** |
| P95 TV per classe | **94,5846 pp** | 94,9901 pp |
| Errore massimo root aggregato | **27,1694 pp** | 27,5366 pp |
| Fold AA | 0,00000576% | **0,00000064%** |

Entrambi i run eliminano praticamente il fold di AA. Il seed indipendente migliora le metriche medie contro Monker, ma peggiora l'EV puntuale, il P95 e il massimo errore root.

## Stabilità fra seed

La distanza diretta fra le due strategie è:

- WMAE per classe e azione: `10,4733 pp`;
- TV media per classe: `26,1834 pp`;
- massima differenza sulle frequenze root aggregate: `2,5935 pp`.

Le frequenze aggregate nascondono quindi forti compensazioni fra classi. Le maggiori TV fra seed sono:

| Classe | TV fra seed |
|---|---:|
| AJs | 91,56 pp |
| QTs | 83,47 pp |
| A7s | 73,81 pp |
| J7s | 70,57 pp |
| K7s | 65,01 pp |

La policy non è stabile a livello di singola classe dopo 2M. Questo giustifica un ulteriore esperimento di convergenza, ma impedisce di trattare uno dei due seed come strategia affidabile.

## Media diagnostica delle policy root

La media 50/50 delle due strategie root ottiene:

| Metrica | Media due seed | Miglior seed singolo | DCFR 2M |
|---|---:|---:|---:|
| Errore medio azioni | 17,5093 pp | 17,6447 pp | **16,9396 pp** |
| TV media per classe | 43,7732 pp | 44,1118 pp | **42,3491 pp** |
| P95 TV | 87,6625 pp | 94,5846 pp | **78,8698 pp** |

La media riduce una parte della varianza, ma non è una soluzione completa: combina soltanto le policy root e non ricostruisce una policy coerente delle continuazioni. Il suo EV medio non è l'EV della strategia mista e non viene usato come gate.

## Ha senso aumentare ancora?

### 4M: sì, come test diagnostico

Due run a 4M richiederebbero circa 22–24 minuti complessivi. La CLI corrente non riprende lo stato MCCFR di questi run, quindi ogni 4M partirebbe da zero. Il budget RAM resta ampio: a 2M il picco è circa 475 MB contro 8 GiB di budget numerico, ma il valore 4M deve comunque essere misurato.

Il gate 4M è:

1. miglioramento WMAE di almeno `1,0 pp` per almeno uno dei due seed rispetto al suo 2M;
2. distanza WMAE fra seed non superiore a `8,4 pp`, almeno il 20% sotto `10,4733 pp`;
3. nessuna ricomparsa significativa di azioni dominate nelle mani forti;
4. nessun errore di memoria, scratch o normalizzazione.

### 8M: solo se 4M supera il gate

Due run a 8M richiederebbero circa 44–48 minuti. Se la componente casuale diminuisse idealmente come `1/sqrt(N)`, la distanza fra seed passerebbe da `10,47 pp` a circa `7,41 pp` a 4M e `5,24 pp` a 8M. È una proiezione ottimistica, non un benchmark.

Con la stessa ipotesi servirebbero circa 219 milioni di iterazioni per portare la sola distanza fra seed vicino a `1 pp`, oltre dieci ore per seed al throughput corrente. Questo calcolo non riduce l'errore strutturale dovuto alla rappresentazione o alla configurazione postflop sconosciuta.

## Decisione

Linear 4M su due seed è autorizzabile come ultimo test breve di convergenza. Passare direttamente a 8M non è giustificato. Nessun aumento di iterazioni può essere promosso come soluzione del confronto Monker finché la strategia per classe resta instabile e il contratto postflop esterno rimane incompleto.

## Artefatti

- `linear_mccfr_8t_2m_seed2.json`;
- `linear_mccfr_8t_2m_seed2.comparison.json`;
- `CO_MONKER_LINEAR_MCCFR_2M_SEED2_COMBO_COMPARISON.md`;
- `linear_mccfr_2m_two_seed_root_mixture_diagnostic.json`;
- `linear_mccfr_2m_two_seed_root_mixture_diagnostic.comparison.json`.

