# Protocollo esperimenti R5

Il confronto usa la fixture CO40, external sampling v2 e il betting compilato. Ogni variante esegue 50.000 iterazioni, 10.000 deal di valutazione fisica, 2.500 iterazioni di risposta appresa e 5.000 deal per risposta. I seed di training, partizione e valutazione sono identici.

Le quattro rappresentazioni congelate prima dei run sono:

| ID | Rappresentazione | Capacità Flop/Turn/River |
|---|---|---:|
| `category` | categoria/equity memoryless | nativa: 144/216/288 valori massimi teorici |
| `coarse` | distribuzionale v2 | 64/256/1.024 |
| `standard` | distribuzionale v2 | 256/1.024/4.096 |
| `fine` | distribuzionale v2 | 1.024/4.096/16.384 |

Si misurano EV fisico e IC 95%, infoset, payload numerico, occupancy per street, query della strategia media prive di stato addestrato, tempo del mapping, `solve_seconds`, wall, peak working set e private bytes. Il confronto serve a diagnosticare capacità e copertura; non usa le frequenze Monker per costruire o scegliere i bucket.

Il gate richiede mapping rileggibile, nessun leakage, memoria entro 8 GiB e un confronto esplicito con baseline e capacità più fine. Non richiede che la capacità più alta migliori in modo monotono: un peggioramento va registrato e diagnosticato.
