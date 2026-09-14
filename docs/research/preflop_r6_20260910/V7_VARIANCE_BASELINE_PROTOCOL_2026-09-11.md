# R6 — Protocollo baseline di varianza V7

## Obiettivo

Misurare se la baseline control-variate già verificata riduce la divergenza fra seed della V7 sul contratto monetario corrente. Il confronto modifica soltanto `opponent_value_baseline`.

## Contratto congelato

- gioco: `hu_preflop_co40_game_v1.json`, revisione monetaria 2;
- Linear MCCFR, otto worker, batch 32;
- V7 strutturata, MC8, capacità `32/128/512`;
- gli stessi due training seed, evaluation seed e partition seed del protocollo di convergenza;
- 250.000 iterazioni;
- valutazione e response diagnostic ridotte a 1.000;
- limite baseline: 512 MiB;
- nessun export completo delle chart.

## Gate

La baseline può avanzare a 500.000 iterazioni soltanto se:

1. riduce la distanza TV fra seed di almeno il `20%`, oppure riduce la WMAE media di almeno `0,5 pp`;
2. non peggiora l'altra metrica di oltre `0,5 pp`;
3. resta nel limite di memoria e conserva tree fingerprint, policy normalizzate e validità numerica.

Il confronto primario usa la strategia media. Le EV con 1.000 deal non qualificano il candidato.
