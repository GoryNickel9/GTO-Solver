# R6 — Protocollo v7 per la media simmetrica delle traversate

## Obiettivo

La media K=4 applicata soltanto al pass CO riduce la TV fra seed del 17,41%, ma peggiora la WMAE media contro Monker di 4,0859 pp. Il pass BTN conserva un singolo campione e riceve quindi uno stimatore con varianza diversa. Questo challenger applica la media K=4 a entrambi i traverser.

## Stimatore

Il pass CO conserva la modalità già verificata: quattro deal fisici condizionati alla classe CO, quattro valori per ogni azione root e delta profondi pesati `1/4`.

Per il pass BTN, il deal primario induce la classe fisica BTN. Il solver esegue altre tre traversate complete condizionate alla stessa classe BTN. Ogni vettore di delta, incluso quello primario, viene moltiplicato per `1/4` prima della riduzione.

Per entrambi i giocatori:

```text
E[(1 / K) * somma_k g(D_k)] = E[g(D)]
```

La classe è estratta dalla distribuzione fisica del deal primario e il campionamento condizionato è uniforme. La distribuzione marginale di ogni rollout resta quindi quella originale.

## Contratto

- Linear MCCFR, batch 32, otto worker.
- V7, MC8, bucket `32/128/512`.
- Sampling fisico indipendente.
- K=4 per CO e BTN.
- Opponent-value baseline esclusa.
- Root CO aggiornato una volta con i valori medi.
- Clock delle iterazioni e pesi Linear invariati.
- Default, K=4 read-only e CO-only invariati.
- Algoritmo identificato da `symmetric_traverser_mean_updates_v1`.

## Invarianti

1. Il sampler condizionato è valido per tutte le 81 classi e per entrambi i traverser.
2. I quattro pesi di ogni pass sommano a uno.
3. La modalità cambia la policy postflop rispetto alla variante CO-only.
4. Uno e otto worker producono risultati numericamente identici.
5. Le configurazioni senza K>1, continuation mean CO o Linear MCCFR vengono rifiutate.
6. I limiti per job e scratch includono i vettori BTN aggiuntivi.
7. La ricostruzione del regret root resta esatta.

## Screen 250k

Il confronto principale è K=4 read-only a 250k. Il challenger passa direttamente a due run da 2M soltanto se:

- la TV fra seed scende almeno del 10%;
- l'SE mediano non peggiora di oltre il 10%;
- la WMAE media contro Monker non peggiora di oltre 1 pp;
- la WMAE migliora di almeno 2 pp rispetto alla variante CO-only;
- costo, memoria, finitezza, normalizzazione e test restano nei limiti.

Il confronto Monker resta diagnostico e non certifica convergenza. Se lo screen fallisce, non si aumenta il budget. Se passa, la conferma 2M decide se conservare il challenger; iterazioni ulteriori richiedono una traiettoria compatibile con un avvicinamento materiale al gate R6.

## Eccezione operativa autorizzata

Lo screen supera i tre gate di qualità ma richiede `1,80×` il wall del K=4 read-only, oltre il limite relativo di `1,25×`. Un A/B da 100k ha escluso l'aumento della cache come rimedio: con lo stesso seed, 4M entry producono la stessa strategia della cache 1M, riducono le eviction da 2.273.599 a 80.800 e il tempo aggregato di mapping da 239,926 a 212,427 s, ma aumentano il wall da 112,255 a 126,113 s.

L'utente ha autorizzato prima dello screen l'aumento delle iterazioni quando necessario. Poiché la proiezione della conferma 2M è circa 39 minuti ed entra nel limite assoluto di due ore, vengono eseguiti due seed a 2M come eccezione diagnostica. Il gate di costo resta `FAIL`; l'eccezione non promuove il challenger e non cambia i criteri già congelati.

## Recupero del costo

Se tutti i gate di qualità passano ma fallisce soltanto il costo, i contatori del run determinano l'intervento successivo. Un A/B a 100.000 iterazioni può aumentare esclusivamente il budget della cache bucket. La cache contiene feature ricalcolabili e non stato di strategia: il risultato numerico deve restare bit-identico.

La cache più grande viene mantenuta soltanto se riduce il wall di almeno il 15%, riduce le eviction, non cambia policy, regret o infoset e resta entro il budget RAM del trainer. In caso contrario il challenger conserva la cache standard e il gate di costo resta fallito.
