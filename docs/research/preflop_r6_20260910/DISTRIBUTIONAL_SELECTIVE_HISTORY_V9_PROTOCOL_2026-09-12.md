# R6 — Protocollo dell'astrazione selective-history v9

## Obiettivo

Verificare se la sola osservazione corrente del v7 unisce traiettorie strategicamente diverse. Il v9 conserva il mapping v7 e aggiunge un solo bucket precedente alla chiave dell'infoset postflop.

Il candidato non usa frequenze o EV Monker nel training. Il riferimento esterno resta un test diagnostico con configurazione incompleta.

## Semantica congelata

Il mapping di ogni osservazione è bit-identico al `category_equity_ordered_profile_v7`, con MC8, partition seed invariato e capacità `32/128/512`.

La chiave conserva:

| Street corrente | Bucket presenti | Bucket assenti |
|---|---|---|
| Flop | Flop | Turn, River |
| Turn | Flop, Turn | River |
| River | Turn, River | Flop |

Il v9 è quindi una memoria di ordine 1 tra street. Non conserva l'intera sequenza Flop-Turn-River e non conserva la classe preflop. Public action history, player e street restano parte della chiave come nelle altre rappresentazioni.

## Invarianti

1. Il bucket corrente e quello precedente coincidono con il mapping v7 applicato alle stesse osservazioni visibili.
2. Nessuna carta futura o privata avversaria entra nella chiave.
3. La trasformazione è deterministica e suit-invariant perché riusa il mapping v7 canonico.
4. Le chiavi Flop, Turn e River hanno esattamente il pattern dichiarato; ogni altro pattern viene rifiutato.
5. Il formato policy è `1.8`; una policy v9 marcata `1.7` viene rifiutata. Le policy precedenti restano leggibili.
6. Uno e otto worker producono lo stesso risultato numerico nel batch deterministico.
7. La modalità è disattivata per default e ha identità distinta nel runner e negli artefatti.

## Validazione prima dello screen

- build Release MSVC con `/W4 /WX`;
- test di pattern della chiave sulle tre street;
- confronto dei bucket con il v7;
- save, checksum, load e query della policy;
- rifiuto della versione e delle chiavi non valide;
- parità numerica tra uno e otto worker;
- suite CTest preflop completa senza regressioni.

Lo screen CO40 è vietato finché questi controlli non passano.

## Gate progressivo

Dopo il gate ingegneristico, due run accoppiati a 250k usano il trainer v7 simmetrico K=4, batch `32`, otto worker, stessi seed e stesso contratto monetario.

Rispetto al v7 current-observation, il v9 deve:

1. migliorare la WMAE media di almeno `0,5 pp` oppure ridurre la TV fra seed di almeno il `20%`;
2. non peggiorare l'altra metrica primaria di oltre `0,5 pp`;
3. non peggiorare la P95 media di oltre `2 pp`;
4. completare senza esaurire le risorse disponibili e registrare separatamente payload numerico e memoria del processo.

La decisione storica basata sul primo seed a 250k è superata dall'istruzione dell'utente del 12 settembre 2026. V9 deve essere rieseguita direttamente a 2M su entrambi i seed. Il superamento di `512 MiB` resta un dato di costo, non un criterio di esclusione; una run può essere interrotta soltanto se non è operativamente completabile sulla macchina disponibile.

Il gate finale R6 resta WMAE `<= 1 pp`, TV `<= 2 pp`, P95 `<= 5 pp` ed errore root aggregato `<= 1 pp`.

## Stato iniziale

`PROTOCOL_AMENDED_BY_USER / ENGINEERING_PASS / PAIRED_2M_COMPLETE / COMPARATIVE_GATE_FAIL / R6_BLOCKED`.

Implementazione e gate ingegneristico: [DISTRIBUTIONAL_SELECTIVE_HISTORY_V9_IMPLEMENTATION_2026-09-12.md](DISTRIBUTIONAL_SELECTIVE_HISTORY_V9_IMPLEMENTATION_2026-09-12.md).

Screen ed early stop: [DISTRIBUTIONAL_SELECTIVE_HISTORY_V9_GATE_2026-09-12.md](DISTRIBUTIONAL_SELECTIVE_HISTORY_V9_GATE_2026-09-12.md).
