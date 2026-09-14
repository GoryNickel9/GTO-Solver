# R6 — Protocollo v7 per la media degli update delle continuazioni root

## Obiettivo

Il challenger K=4 read-only riduce la TV fra seed a 2M da `27,9000` a `16,6353 pp`, ma tre dei quattro rollout non addestrano gli infoset postflop. Questa prova misura se la stessa media campionaria può ridurre anche la varianza delle continuazioni senza cambiare il valore atteso di un'iterazione.

## Stimatore

Per il pass in cui CO è traverser, il deal primario induce la classe preflop `C`. Il solver campiona altri `K-1` deal indipendenti da `p(D | C)`. Poiché la classe primaria segue la massa delle combo fisiche,

```text
p(C) = numero_combo(C) / 630
p(D_k) = somma_C p(C) p(D_k | C) = p(D)
```

Ogni traversata produce un vettore sparso di delta profondi `g(D_k)`. Il nuovo update è:

```text
g_mean = (1 / K) * somma_k g(D_k)
```

Per una chiave non visitata da un rollout il relativo delta è zero. La riduzione può quindi concatenare i vettori sparsi dopo aver moltiplicato ogni regret delta e strategy-sum delta per `1/K`; il reducer esistente somma le entry con la stessa chiave.

Il root conserva l'aggiornamento già validato dal challenger read-only: un solo regret update costruito dalla media dei `K` valori di ciascuna azione. Non vengono creati altri update root.

## Ambito

- Linear MCCFR con policy congelata nel batch.
- Sampling fisico indipendente.
- Quattro rollout root nella prima prova.
- Profilo postflop v7 `32/128/512`, MC8.
- Pass BTN invariato.
- Opponent-value baseline esclusa: il suo accumulatore corrente non accetta osservazioni frazionarie.
- Default invariato: un rollout e continuation updates disattivati.
- Modalità K=4 read-only invariata e riproducibile.

L'algoritmo registra un suffisso distinto, `continuation_mean_updates_v1`. Questa modalità modifica il training postflop e non è compatibile sul piano numerico con la soluzione K=4 read-only.

## Invarianti

1. Le masse `p(C) p(combo | C)` valgono esattamente `1/630` per tutte le combo CO.
2. I pesi dei `K` rollout sono non negativi e sommano a uno.
3. K=1 e K=4 read-only restano bit-identici alle rispettive baseline.
4. K=4 continuation-mean produce lo stesso risultato con uno e otto worker.
5. La policy postflop cambia rispetto al read-only già alla prima iterazione CO, provando che gli update aggiuntivi raggiungono il reducer.
6. Nessun vettore sparso supera il limite per job; scratch e picchi restano osservabili.
7. La ricostruzione del regret root resta esatta.

## Gate progressivo

La prima prova usa 250.000 iterazioni e due seed. Il confronto principale è contro K=4 read-only allo stesso budget; la V7 standard resta controllo secondario.

Il challenger passa a 2M soltanto se:

- la TV fra seed scende almeno del 10% rispetto a K=4 read-only 250k;
- l'SE mediano non peggiora di oltre il 10%;
- la WMAE media contro Monker non peggiora di oltre 1 pp;
- build, test, finitezza, normalizzazione, ricostruzione regret e memoria passano;
- il costo non supera 1,25 volte K=4 read-only, salvo misura concorrente dichiarata non controllata.

Il confronto Monker resta diagnostico perché la fixture esterna non descrive il contratto postflop. Il gate causale è la stabilità fra seed.

## Aumento delle iterazioni

Se lo screen passa, si eseguono direttamente due seed a 2M. Un budget superiore è ammesso soltanto se la traiettoria 250k→2M riduce ancora la TV e se una proiezione conservativa può avvicinarsi materialmente al gate R6. Non si aumenta il budget per compensare una regressione o una rappresentazione errata.
