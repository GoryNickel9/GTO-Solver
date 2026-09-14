# R6 — Protocollo dell'astrazione category-history v10

## Obiettivo

Conservare un segnale storico interpretabile senza l'esplosione di stato del v9. Il v10 usa il bucket v7 della street corrente e, da Turn in poi, la sola categoria visibile della street precedente.

## Semantica congelata

| Street | Chiave astratta |
|---|---|
| Flop | bucket v7 Flop |
| Turn | categoria visibile Flop + bucket v7 Turn |
| River | categoria visibile Turn + bucket v7 River |

La categoria è il valore esatto Short Deck `0–8` prodotto dall'evaluator. Il mapping corrente resta `category_equity_ordered_profile_v7`, MC8, capacità `32/128/512`. La classe preflop non viene conservata.

Il v10 non ricava feature dalla strategia, dagli EV Monker o dal futuro. Distingue soltanto se e da quale categoria visibile deriva la mano corrente.

## Invarianti

1. Il bucket corrente è identico al v7 sulla stessa osservazione.
2. Il codice storico coincide con i quattro bit di categoria del bucket v7 precedente ed è sempre fra 0 e 8.
3. Al River il bucket Flop è assente; al Turn il bucket River è assente.
4. Una chiave con categoria storica maggiore di 8 viene rifiutata.
5. Il formato policy è `1.9`; v10 marcato 1.8 viene rifiutato e i formati precedenti restano leggibili.
6. La modalità è disattivata per default, identificata nella CLI e bit-riproducibile fra uno e otto worker.

## Gate ingegneristico

Build Release `/W4 /WX`, test di estrazione categoria, pattern delle chiavi, rifiuti, save/load/query, parità 1/8 worker e CTest preflop `13/13` devono passare prima dello screen.

## Gate a 2M

La validazione usa direttamente due seed a 2M con lo stesso trainer v7 simmetrico K=4. Le run a 250k non possono decidere la qualità della candidata: nel v7 il passaggio da 250k a 2M ha ridotto la WMAE media da `18,7001 pp` a `14,8844 pp`. Il baseline qualificato resta quindi il v7 a 2M: WMAE `15,2418/14,5270 pp`, media `14,8844 pp`, e TV fra seed `18,6455 pp`.

Il v10 passa se:

1. migliora la WMAE media di almeno `0,5 pp` oppure riduce la TV fra seed di almeno il `20%`;
2. non peggiora l'altra metrica primaria di oltre `0,5 pp`;
3. non peggiora la P95 media di oltre `2 pp`;
4. completa senza esaurire le risorse disponibili e registra separatamente payload numerico e memoria del processo;
5. esporta 20 nodi con probabilità ed EV validi e regret root ricostruiti con errore zero.

Se i due seed restano materialmente distanti, l'aumento oltre 2M viene deciso dalla varianza osservata e dalla copertura effettiva per mano. Nessun artefatto sotto 2M può essere pubblicato come soluzione v10.

Non esiste un limite RAM qualificante fissato dall'utente per HU preflop. Il precedente valore sperimentale di `512 MiB` è rimosso come criterio PASS/FAIL: memoria e payload restano metriche comparative, mentre l'unico limite operativo è la possibilità di completare il solve senza compromettere la macchina.

## Stato iniziale

`PROTOCOL_AMENDED_BY_USER / ENGINEERING_GATE_PASS / PAIRED_2M_COMPLETE / COMPARATIVE_GATE_PASS / R6_BLOCKED`.

Risultati e decisione: [DISTRIBUTIONAL_CATEGORY_HISTORY_V10_GATE_2026-09-12.md](DISTRIBUTIONAL_CATEGORY_HISTORY_V10_GATE_2026-09-12.md).
