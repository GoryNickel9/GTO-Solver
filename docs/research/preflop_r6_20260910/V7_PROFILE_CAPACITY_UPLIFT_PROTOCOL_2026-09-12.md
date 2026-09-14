# R6 — Protocollo v7 con capacità di profilo aumentata

## Obiettivo

Separare l'errore dovuto alle collisioni del v7 dall'errore dovuto alle sue feature. Il candidato conserva categoria esatta, ordinamento e trainer simmetrico, ma aumenta le capacità da `32/128/512` a `128/512/2048`.

Non è una ricerca della capacità che meglio imita Monker. È un confronto accoppiato tra due rappresentazioni generiche, con il riferimento esterno usato soltanto come test diagnostico.

## Modifica isolata

Nel mapping v7 i quattro bit della categoria sono invariati. La capacità standard assegna ai campi forza/profilo:

| Street | Standard | Bit forza/profilo | Challenger | Bit forza/profilo |
|---|---:|---:|---:|---:|
| Flop | 32 | 1/0 | 128 | 2/1 |
| Turn | 128 | 2/1 | 512 | 3/2 |
| River | 512 | 3/2 | 2048 | 4/3 |

Il challenger aggiunge quindi un bit di equity e un bit di profilo su ogni street, senza raggruppare categorie come v8 e senza modificare il codice del mapping.

## Configurazione congelata

- albero `fnv1a64:a68337fa567aa2d9`, contratto monetario v2 e rake disattivato;
- Linear MCCFR, batch `32`, otto worker e deal fisici indipendenti;
- K=4, continuation mean CO e traverser mean BTN;
- MC8 e partition seed `5923736619020287489`;
- `250.000` iterazioni, `1.000` deal di valutazione, `1.000` iterazioni BR e `1.000` deal BR;
- stessi training/evaluation seed del v7 simmetrico standard.

L'unica variabile è `distributional_bucket_capacities`.

## Gate dello screen 250k

Il baseline è v7 simmetrico `32/128/512`: WMAE media `18,7001 pp`, TV fra seed `25,8938 pp`, P95 media `96,7350 pp`, tempo medio `293,819 s` e payload massimo `120.218.544 B`.

Lo screen passa soltanto se:

1. la WMAE media migliora di almeno `0,5 pp` oppure la TV fra seed scende di almeno il `20%`;
2. la metrica primaria non scelta non peggiora di oltre `0,5 pp`;
3. la P95 media non peggiora di oltre `2 pp`;
4. il payload numerico resta entro `512 MiB` e il tempo proiettato a 2M resta sotto `7.200 s` per run;
5. identità, normalizzazione, EV, export dei 20 nodi e ricostruzione dei regret risultano validi.

## Regola per le iterazioni

- `PASS`: conferma a 2M su entrambi i seed.
- `FAIL`: nessun run più lungo.
- `INCONCLUSIVE`: un solo passaggio a 500k è ammesso se il margine dal gate è inferiore a `0,25 pp` o a due errori standard aggregati.

Il gate finale R6 non cambia. Una capacità migliore ma lontana da WMAE `1 pp`, TV `2 pp` e P95 `5 pp` resta sperimentale.

## Stato iniziale

`PROTOCOL_FROZEN / SCREEN_COMPLETED / GATE_FAIL / NO_2M_RUN / R6_BLOCKED`.

Risultati e decisione: [V7_PROFILE_CAPACITY_UPLIFT_GATE_2026-09-12.md](V7_PROFILE_CAPACITY_UPLIFT_GATE_2026-09-12.md).
