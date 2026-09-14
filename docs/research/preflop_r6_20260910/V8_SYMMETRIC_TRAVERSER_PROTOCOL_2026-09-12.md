# R6 — Protocollo v8 con traverser simmetrici

## Obiettivo

Verificare se l'astrazione postflop street-adaptive v8 beneficia della riduzione di varianza introdotta dalla media K=4 su entrambi i traverser. Il confronto isola il mapping: trainer, albero, contratto monetario, sampling, seed, batch e risorse restano quelli del v7 simmetrico.

## Configurazione congelata

- albero `fnv1a64:a68337fa567aa2d9` e contratto monetario v2;
- Linear MCCFR, batch `32`, otto worker e deal fisici indipendenti;
- quattro rollout condizionati per classe, continuation mean CO e traverser mean BTN;
- MC8, capacità `32/128/512` e partition seed `5923736619020287489`;
- `250.000` iterazioni, `1.000` deal di valutazione, `1.000` iterazioni BR e `1.000` deal BR;
- seed 1: training `5923736619020283393`, evaluation `5923736619020279297`;
- seed 2: training `5200000000000000102`, evaluation `5300000000000000202`.

L'unica variabile è il mapping postflop: `category_equity_ordered_profile_v7` contro `street_adaptive_category_equity_profile_v8`. Monker non viene consultato dal trainer.

## Invarianti

1. Entrambi i run devono dichiarare v8, K=4, continuation mean e symmetric traverser mean.
2. La policy media di ogni infoset deve essere finita, non negativa e normalizzata entro la tolleranza numerica.
3. La ricostruzione dei regret root deve avere errore massimo zero.
4. Ogni run usa al massimo otto worker e registra stato numerico, payload e memoria del processo; non è imposto un cap RAM qualificante.
5. I confronti Monker restano diagnostici e non certificati finché la configurazione esterna è incompleta.

## Gate dello screen 250k

Il baseline accoppiato è il v7 simmetrico a 250k: WMAE media `18,7001 pp` e TV tra seed `25,8938 pp`.

Lo screen passa soltanto se:

1. la WMAE media migliora di almeno `0,5 pp` oppure la TV tra seed scende di almeno il `20%`;
2. la metrica primaria non scelta non peggiora di oltre `0,5 pp`;
3. la P95 TV media contro Monker non peggiora di oltre `2 pp`;
4. tempo, payload, normalizzazione e integrità rispettano gli invarianti.

Un aumento di bucket occupati non vale come gate di qualità.

## Regola per l'aumento delle iterazioni

La decisione storica basata sullo screen a 250k è superata dall'istruzione dell'utente del 12 settembre 2026. V8 deve essere rieseguita direttamente a `2M` su entrambi i seed. Le metriche a 250k restano diagnostiche ma non possono qualificare o respingere la versione.

Anche dopo un PASS a 2M, R6 richiede WMAE `<= 1 pp`, TV media `<= 2 pp`, P95 `<= 5 pp` ed errore root aggregato `<= 1 pp`. Un miglioramento relativo non autorizza R7.

## Stato iniziale

`PROTOCOL_AMENDED_BY_USER / HISTORICAL_250K_FAIL / 2M_RETEST_REQUIRED / R6_BLOCKED`.

Risultati e decisione: [V8_SYMMETRIC_TRAVERSER_GATE_2026-09-12.md](V8_SYMMETRIC_TRAVERSER_GATE_2026-09-12.md).
