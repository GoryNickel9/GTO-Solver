# R6 — Protocollo schedule di update V7

## Obiettivo

Separare la varianza propria del sampler dall'effetto della schedule parallela. Il confronto cambia soltanto la modalità di applicazione degli update:

- baseline: otto worker, batch deterministico di 32 iterazioni;
- challenger: un worker, update online dopo ogni traversata.

Non cambia l'astrazione. Una V8 introdotta nello stesso esperimento renderebbe impossibile attribuire il risultato.

## Contratto congelato

- gioco: `hu_preflop_co40_game_v1.json`, contratto monetario revisione 2;
- algoritmo: Linear MCCFR;
- training seed: `5923736619020283393` e `5200000000000000102`;
- evaluation seed: `5923736619020279297` e `5300000000000000202`;
- partition seed: `5923736619020287489`;
- V7 strutturata, MC8, capacità `32/128/512`;
- budget di screening: 250.000 iterazioni;
- valutazione e response diagnostic: 1.000 deal/iterazioni ciascuna;
- nessun export completo delle chart.

La valutazione ridotta controlla soltanto l'esecuzione. WMAE, TV contro Monker e distanza fra le due policy root sono calcolate dalle strategie medie e non dipendono dal numero di deal di valutazione.

## Gate

L'update online può avanzare a 500.000 iterazioni soltanto se, rispetto alla media batch-32 a 250.000:

1. riduce la WMAE media di almeno `0,5 pp`, oppure riduce la TV seed-vs-seed di almeno il `20%`;
2. non peggiora la TV media contro Monker di oltre `0,5 pp`;
3. mantiene output validi, policy normalizzate e lo stesso fingerprint del tree.

Se il gate fallisce, la schedule online viene respinta. Il passo successivo deve misurare una tecnica di riduzione della varianza; non autorizza una nuova astrazione.
