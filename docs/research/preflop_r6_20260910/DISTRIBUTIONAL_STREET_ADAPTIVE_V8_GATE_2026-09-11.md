# R6 — Gate dell'astrazione street-adaptive v8

## Esito

`ENGINEERING_PASS / EXPERIMENT_GATE_FAIL / NO_500K_RUN`.

Il v8 usa meglio la capacità dichiarata, ma peggiora l'accordo con Monker e riduce appena la divergenza tra seed. Non viene promosso alla conferma da `500.000` iterazioni.

## Configurazione

Entrambe le run usano:

- albero corrente `fnv1a64:a68337fa567aa2d9` e contratto monetario v2;
- Linear MCCFR con batch `32` e otto worker;
- MC8, capacità `32/128/512` e seed di partizione `5923736619020287489`;
- `250.000` iterazioni, `1.000` deal di valutazione, `1.000` iterazioni BR e `1.000` deal BR;
- stessi seed training/evaluation delle run v7 accoppiate.

Il mapping è stato congelato prima dell'implementazione in [DISTRIBUTIONAL_STREET_ADAPTIVE_V8_PROTOCOL_2026-09-11.md](DISTRIBUTIONAL_STREET_ADAPTIVE_V8_PROTOCOL_2026-09-11.md).

## Risultati

| Profilo | Seed | WMAE | TV contro Monker | P95 TV | TV tra seed | Infoset | Payload | Bucket occupati F/T/R | Tempo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| v7 | 1 | 19,3262 pp | 48,3154 pp | 95,7646 pp | — | 615.167 | 92.179.584 B | 15/56/136 | 36,754 s |
| v7 | 2 | 16,5799 pp | 41,4497 pp | 92,4199 pp | 35,3724 pp | 614.781 | 91.801.728 B | 15/56/138 | 44,706 s |
| v8 | 1 | 19,9692 pp | 49,9230 pp | 88,8812 pp | — | 937.114 | 138.633.552 B | 29/121/198 | 33,732 s |
| v8 | 2 | 19,9945 pp | 49,9862 pp | 94,0043 pp | 34,9185 pp | 937.467 | 138.791.808 B | 29/123/201 | 33,571 s |

Confronto medio:

| Metrica | v7 | v8 | Variazione v8 |
|---|---:|---:|---:|
| WMAE contro Monker | 17,9530 pp | 19,9818 pp | **+2,0288 pp** |
| TV contro Monker | 44,8826 pp | 49,9546 pp | **+5,0720 pp** |
| TV tra seed | 35,3724 pp | 34,9185 pp | −0,4540 pp, pari a −1,28% |
| massimo delta marginale tra seed | 5,5602 pp | 2,6197 pp | −2,9405 pp |

## Valutazione del gate

| Requisito | Risultato | Esito |
|---|---:|---|
| WMAE migliore di almeno 0,5 pp oppure TV tra seed ridotta di almeno 20% | WMAE peggiora di 2,0288 pp; TV tra seed migliora dell'1,28% | FAIL |
| metrica secondaria non peggiore di oltre 0,5 pp | WMAE e TV contro Monker peggiorano oltre 0,5 pp | FAIL |
| più bucket occupati su almeno due street | più bucket su tutte e tre le street | PASS |
| payload inferiore a 512 MiB | massimo 138.791.808 B | PASS |
| test mapping, persistenza e determinismo | 7/7 test preflop PASS | PASS |

L'aumento di occupazione non basta: il raggruppamento più aggressivo delle categorie al flop rimuove separazioni strategiche utili. Il risultato distingue un problema di capacità da un problema di semantica del bucket: riempire più bucket non garantisce una migliore astrazione.

## Artefatti

- `v8_screen_250k_seed1_monetary_v2.json`, SHA-256 `967B8493E6E503B2D6EB2E4C4DA1D30D22F0FD743D6350CE738D8456F0EBEE47`;
- `v8_screen_250k_seed2_monetary_v2.json`, SHA-256 `DC455D4128E3C375550D9FC7FB635B914CAB5FC97781F4841950D211E08F2A77`;
- relativi report `.comparison.json`, con rifiuto atteso perché il gate finale resta lontano dalle soglie e la configurazione Monker è incompleta;
- runner Release, SHA-256 `4EF1ED2478B521AA333E725D58662D9FA5798ED4090B52EC66F79023F2451DAA`.

## Decisione

Il v8 resta disponibile soltanto come rappresentazione di ricerca versionata `1.7`. Non sostituisce v7, non giustifica una run più lunga e non sblocca R7. Il prossimo esperimento non deve riutilizzare il raggruppamento v8 al flop.
