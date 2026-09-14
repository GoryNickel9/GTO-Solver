# Linear MCCFR 4M: gate della configurazione selezionata

Data: 2026-09-11

## Obiettivo

Validare a 4 milioni di iterazioni la configurazione scelta dagli screening brevi:

- Linear MCCFR;
- batch `32`;
- partizione `32/128/512`;
- feature MC8;
- sampling fisico indipendente;
- opponent-value baseline disattivata;
- otto worker.

Il confronto usa due seed indipendenti. Il codice di produzione e le fixture non sono stati modificati.

## Esecuzione

| Metrica | Seed 1 | Seed 2 |
|---|---:|---:|
| Training seed | `5923736619020283393` | `5200000000000000102` |
| Evaluation seed | `5923736619020279297` | `5300000000000000202` |
| Wall monitorato | 765,272 s | 765,261 s |
| Solve interno | 734,178 s | 735,201 s |
| Picco private campionato | 435.499.008 B | 434.880.512 B |
| Infoset | 1.428.496 | 1.427.213 |
| Payload numerico | 227.973.888 B | 227.849.760 B |
| Peak scratch | 1.416.992 B | 1.420.792 B |

Entrambi i processi riportano `HU_PREFLOP_SOLVE=PASS`; gli stderr sono vuoti. Binario SHA-256: `8BB2AB018B2BB5429EA2874DB83CDC1C62C88BCD47421D4B5CABEBB208C62229`.

## Qualità rispetto a Monker

| Metrica | Nuovo 4M seed 1 | Nuovo 4M seed 2 | Vecchio 4M seed 1 | Vecchio 4M seed 2 |
|---|---:|---:|---:|---:|
| WMAE azioni | **16,499699 pp** | **15,032418 pp** | 16,835914 pp | 16,498891 pp |
| TV media | **41,249248 pp** | **37,581046 pp** | 42,089786 pp | 41,247227 pp |
| P95 TV | **89,952645 pp** | **92,898956 pp** | 95,166766 pp | 93,943230 pp |
| Errore max root | **23,047123 pp** | **23,049821 pp** | 24,717449 pp | 26,074105 pp |
| EV CO | -0,094939a | -0,113573a | -0,340394a | +0,009119a |
| SE EV | 0,103834a | **0,101409a** | 0,104283a | 0,103167a |
| Errore EV | 0,205061a | **0,186427a** | **0,040394a** | 0,309119a |

La WMAE media scende da `16,667402` a `15,766059 pp`, un miglioramento di `0,901344 pp`. La TV media migliora di `2,253359 pp`, il P95 di `3,129197 pp` e l'errore max root di `2,347306 pp`.

La configurazione nuova migliora entrambi i seed sulla WMAE. L'EV puntuale non supera il gate: nessuna replica soddisfa il target `-0,30 ± 0,05 ante`, tanto meno il criterio con IC95.

## Progresso da 500k a 4M

| Metrica | 500k, media seed | 4M, media seed | Variazione |
|---|---:|---:|---:|
| WMAE | 19,550505 pp | **15,766059 pp** | -3,784447 pp (-19,36%) |
| TV media | 48,876264 pp | **39,415147 pp** | -9,461117 pp |
| Distanza WMAE fra seed | 12,075768 pp | **8,799834 pp** | -27,13% |

L'aumento delle iterazioni produce progresso reale, ma il residuo resta oltre quindici volte il gate finale di `1 pp`.

## Stabilità fra seed

| Configurazione 4M | WMAE fra policy | TV fra policy | Max delta azione aggregata |
|---|---:|---:|---:|
| Vecchia: batch 64, `64/256/1024` | **8,615793 pp** | **21,539482 pp** | 5,933475 pp |
| Nuova: batch 32, `32/128/512` | 8,799834 pp | 21,999586 pp | **3,799550 pp** |

La nuova configurazione riduce la deriva delle frequenze aggregate, ma peggiora leggermente la distanza media per combo fra seed. Il valore `8,799834 pp` manca il gate `<=8,4 pp` di `0,399834 pp`.

## Media root diagnostica

La media 50/50 delle sole policy root ottiene WMAE `15,414792 pp`, TV `38,536980 pp`, P95 `89,921896 pp` ed errore max root `23,048483 pp`.

Questa media non è una soluzione completa: non combina le policy delle continuazioni e non ha un EV di profilo valido. Serve soltanto a stimare quanta parte del residuo root sia rumore fra seed.

## Costi rispetto al vecchio 4M

Sulla media dei due seed:

- solve: da `694,456 s` a `734,690 s`, `+5,79%`;
- payload numerico: da `280.123.560` a `227.911.824 B`, `-18,64%`;
- picco private: da `496.658.432` a `435.189.760 B`, `-12,38%`;
- peak scratch: circa `-42,7%`.

## Validazione e gate

| Controllo | Esito | Evidenza |
|---|---|---|
| Due seed e contratto congelato | PASS | unica configurazione selezionata, seed separati |
| Solve, stderr e integrità | PASS | due solve passati, stderr vuoti, strategie normalizzate |
| Miglioramento WMAE su entrambi i seed | PASS | `-0,336215` e `-1,466472 pp` contro il vecchio 4M |
| Media WMAE migliore di almeno 0,5 pp | PASS | `-0,901344 pp` |
| Distanza fra seed `<=8,4 pp` | **FAIL** | `8,799834 pp` |
| Azioni dominate prossime a zero | PASS | fold AA massimo `0,00000143%`; all-in 76o massimo `0,00001145%` |
| Tempo sotto due ore e RAM sotto 8 GiB | PASS | circa 12,75 minuti wall e meno di 0,44 GB private per replica |
| Gate strategia Monker `<=1 pp` | **FAIL** | WMAE media `15,766059 pp` |
| Gate EV con IC95 | **FAIL** | errore puntuale 0,186–0,205a, SE circa 0,10a |
| Equivalenza configurazione Monker | NOT EVALUATED | albero e astrazione postflop esterni ignoti |

Il comparatore conserva correttamente lo stato `REJECTED / REFERENCE_CONFIG_INCOMPLETE`; non è un errore operativo dei solve.

## Decisione

La configurazione nuova sostituisce quella vecchia come migliore candidata Linear osservata per qualità media, memoria e frequenze aggregate. Non supera però il gate R6 né i criteri finali.

Non si procede a 8M: il miglioramento da 500k a 4M è troppo piccolo rispetto al residuo e la stabilità manca ancora il gate. Il prossimo lavoro deve diagnosticare il residuo sistematico per azione e combo, distinguendolo dalla dispersione fra seed, prima di cambiare ancora l'algoritmo o l'astrazione.

## Artefatti

- `linear_mccfr_8t_4m_batch32_partition_32_128_512.json` e confronto;
- `linear_mccfr_8t_4m_batch32_partition_32_128_512_seed2.json` e confronto;
- `CO_MONKER_LINEAR_MCCFR_4M_BATCH32_PARTITION_32_128_512_COMBO_COMPARISON.md`;
- `CO_MONKER_LINEAR_MCCFR_4M_BATCH32_PARTITION_32_128_512_SEED2_COMBO_COMPARISON.md`.
