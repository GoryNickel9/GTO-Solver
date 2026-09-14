# R6 — Chiusura V14 K=8

Data: 2026-09-13  
Esito: `PROTOCOL_CONFOUNDED / COST_GATE_FAIL / SEED_2_ABORTED`

## Risultato

V14 raddoppia da quattro a otto i continuation rollout root di V13. Il seed 1 ha completato
2.000.000 iterazioni e prodotto candidato e policy postflop integri, ma ha richiesto `8.365,78 s`,
pari a `139,43 minuti`. V13 seed 1 aveva richiesto `1.994,12 s`, pari a `33,24 minuti`.

Il costo osservato è aumentato di `4,195×`, ma il confronto non isola K=8: V13 caricava la
tabella precalcolata a sette carte, mentre V14 ha usato per errore
`exact_hand_evaluator_oracle_v1`. Il protocollo di parità del backend è quindi violato.
Il seed 2 è stato fermato dopo pochi minuti su richiesta dell'utente e non ha prodotto una
soluzione. V14 non entra nel viewer.

## Diagnosi del costo

| Metrica seed 1 | V13 K=4 | V14 K=8 | Rapporto |
| --- | ---: | ---: | ---: |
| Solve | 33,24 min | 139,43 min | 4,195× |
| Terminali all-in esatti | 105.830.238 | 203.738.219 | 1,925× |
| Runout enumerati | 14.233.622.844 | 26.910.717.722 | 1,891× |
| Cache hit-rate | 87,15% | 87,39% | stabile |
| Eviction cache | 12.480.578 | 24.574.158 | 1,969× |
| Throughput runout per secondo di solve | 7.137.799 | 3.216.762 | 0,451× |

La cache non mostra una tempesta di miss: hit-rate e lavoro totale crescono quasi come K. Il
throughput complessivo dell'enumeratore scende però del `54,9%`. Questa differenza non può essere
attribuita a saturazione o a K=8 perché il backend è cambiato. V13 riporta
`seven_card_table_v1:2236291214962974841`; V14 riporta `exact_hand_evaluator_oracle_v1`.
`exact_postflop_all_in_seconds` somma inoltre i tempi dei worker e non va letto come wall-clock
indipendente.

Il payload numerico è `0,24 GiB` e il processo ha raggiunto circa `0,55 GiB` di working set. La RAM
non è il limite osservato.

## Qualità parziale

| Metrica | V13 seed 1 | V14 seed 1 | Variazione |
| --- | ---: | ---: | ---: |
| WMAE root | 14,90 pp | 13,14 pp | -1,76 pp |
| TV pesata contro riferimento | 37,24 pp | 32,85 pp | -4,39 pp |
| Errore massimo azione root | 25,06 pp | 22,15 pp | -2,91 pp |
| Errore EV root | 0,18a | 0,09a | -0,09a |

Il miglioramento del singolo seed non raggiunge il gate WMAE `<=5 pp` e non giustifica 139 minuti.
La TV fra seed non è misurabile perché il secondo seed è stato annullato.

## Validazione e artefatti

- iterazioni seed 1: `2.000.000`;
- root rollout: `8`;
- candidato SHA-256: `1AB7F56A5016877E558178174BA380061A3B7E4A126105B91AEA3672E321DD65`;
- policy SHA-256: `FEDAF65AC138E81C75D35B3A5A9E434EDEFCEDA3437E6B5CA732493D427FCB17`;
- confronto esterno: `REJECTED / REFERENCE_CONFIG_INCOMPLETE`;
- viewer: invariato su V13.

## Decisione

La run V14 eseguita è scartata per costo e protocollo confuso. Non dimostra che K=8 richieda 140
minuti con il backend corretto; quel costo resta non misurato. Il secondo seed non viene ripetuto.
V15 torna a K=4, ripristina esplicitamente la stessa tabella a sette carte di V13 e riduce la
varianza delle differenze fra azioni accoppiando i numeri casuali, senza aggiungere terminali o
runout.
