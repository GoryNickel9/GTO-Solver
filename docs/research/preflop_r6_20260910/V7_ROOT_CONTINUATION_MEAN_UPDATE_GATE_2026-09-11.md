# R6 — Gate v7 della media degli update delle continuazioni CO

## Esito

`ENGINEERING_PASS / SEED_STABILITY_PASS / MONKER_SAFETY_FAIL / REJECTED_AT_250K`.

Mediare con peso `1/4` i delta profondi delle quattro traversate CO riduce la TV fra seed, ma peggiora la WMAE media contro Monker di `4,0859 pp`. Il challenger non passa a 2M.

## Implementazione

L'opzione `root_continuation_mean_updates` estende il K=4 read-only. Per il pass CO:

1. la traversata primaria e le tre traversate condizionate producono delta profondi;
2. ogni regret delta e strategy-sum delta viene moltiplicato per `1/4`;
3. i vettori sparsi vengono concatenati e sommati dal reducer deterministico esistente;
4. il root riceve un solo update, costruito dalla media dei quattro valori d'azione;
5. il pass BTN resta invariato.

La modalità richiede Linear MCCFR, batch congelato, sampling fisico indipendente, più di un rollout e opponent-value baseline disattivata. L'algoritmo registra `continuation_mean_updates_v1`. Default e K=4 read-only non cambiano.

## Correttezza dello stimatore

L'enumerazione delle 630 combo verifica che ogni classe abbia massa `6/630`, `4/630` o `12/630`. Campionare uniformemente una combo condizionata alla classe produce per ogni combo:

```text
p(C) * p(combo | C) = numero_combo(C) / 630 * 1 / numero_combo(C) = 1 / 630
```

I quattro pesi valgono `1/4` e sommano a uno. Il test verifica inoltre che la modalità modifichi davvero la policy postflop e che uno e otto worker producano lo stesso risultato numerico.

## Screen 250k

| Modalità | Seed | SE mediano prima/seconda | Classi sotto 2 SE | WMAE | TV contro Monker | Infoset | Solve |
|---|---:|---:|---:|---:|---:|---:|---:|
| K=4 read-only | 1 | 0,1601a | 62/81 | 18,1999 pp | 45,4998 pp | 609.448 | 163,262 s |
| K=4 read-only | 2 | 0,1635a | 65/81 | 19,7836 pp | 49,4591 pp | 625.719 | 163,520 s |
| K=4 mean update CO | 1 | 0,1563a | 64/81 | 23,7675 pp | 59,4189 pp | 753.565 | 180,400 s |
| K=4 mean update CO | 2 | 0,1524a | 68/81 | 22,3877 pp | 55,9692 pp | 748.561 | 180,344 s |

| Gate | Richiesto | Osservato | Esito |
|---|---:|---:|---|
| Riduzione TV fra seed | almeno 10% | da 37,4164 a 30,9023 pp, `−17,41%` | PASS |
| SE mediano | peggioramento massimo 10% | da 0,1618 a 0,1543a, `−4,63%` | PASS |
| Peggioramento WMAE media | massimo 1 pp | da 18,9918 a 23,0776 pp, `+4,0859 pp` | FAIL |
| Costo wall | massimo 1,25× | da 163,391 a 180,372 s, `1,10×` | PASS, run concorrenti |
| Ricostruzione regret | errore zero | `0` entrambi i seed | PASS |
| Update per job | massimo 10.000 | 1.098 / 1.173 | PASS |
| Scratch | massimo 512 MiB | 4.627.808 / 4.331.208 B | PASS |

La strategia corrente migliora la stabilità: TV da `46,5286` a `29,0821 pp` e azione dominante discordante da 42 classi / 312 combo a 31 classi / 230 combo. La strategia si allontana però dal riferimento in entrambi i seed, non per un singolo outlier.

AA sceglie Call in entrambi i seed. Il margine appaiato `Call - Raise 6a` è `+0,9221 ± 0,2292a` e `+0,7922 ± 0,2208a`; la media assegna al Call `98,8802%` e `99,8067%`. Questo risultato locale non compensa la regressione sulle 81 classi.

## Validazione

Build MSVC Release `/W4 /WX`: PASS.

```text
R6_HU_PREFLOP_PARALLEL_TESTS=PASS
assertions=1338
```

La regressione completa sugli eseguibili rilinkati passa:

```text
11/11 passed
Total Test time (real) = 498,30 s
```

I due file contengono 250.000 iterazioni, K=4, flag continuation-mean, strategie finite e normalizzate, ricostruzione regret a errore zero e limiti di memoria rispettati. I confronti esterni restano `REJECTED / REFERENCE_CONFIG_INCOMPLETE`.

SHA-256:

- seed 1: `2051861F8E9A6F160A004100FA850B561B80468D318B01A52C7ED325F5D4A5D7`;
- seed 2: `5DF4D9485478CD59A9B706E408FAC460FCE1316262A07330DA49749BDB8AC8A2`.

## Decisione

Il challenger CO-only è respinto a 250k e resta disattivato. Il miglioramento fra seed prova che i delta profondi aggiuntivi raggiungono il limite misurato, ma la riduzione agisce soltanto sul pass CO. Il pass BTN conserva una singola traversata: i due giocatori ricevono stimatori con varianza diversa e la dinamica finita si sposta lontano dal riferimento.

Il prossimo challenger deve applicare la stessa media K=4 anche al pass BTN. Questa variante mantiene il valore atteso di entrambi i pass e verifica se la regressione WMAE dipende dall'asimmetria introdotta qui.
