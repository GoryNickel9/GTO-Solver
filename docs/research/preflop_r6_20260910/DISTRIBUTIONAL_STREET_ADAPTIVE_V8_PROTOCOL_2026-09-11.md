# R6 — Protocollo dell'astrazione street-adaptive v8

## Obiettivo

Verificare se le collisioni introdotte dal profilo strutturato v7 amplificano la varianza tra seed. Il v8 è un challenger R6: modifica soltanto il mapping delle osservazioni postflop, non consulta la strategia Monker e non autorizza il passaggio a R7.

## Motivazione

Con capacità `32/128/512`, il v7 riserva quattro bit alla categoria su ogni street. Restano soltanto uno, tre e cinque bit per equity e profilo. Le run diagnostiche hanno occupato `15/60/143` bucket, molto meno dei `32/128/512` disponibili. Due run v7 da due milioni di iterazioni mantengono inoltre una TV pesata tra seed di `27,9000 pp` al CO root.

Tre controlli isolati a `250.000` iterazioni non hanno risolto il problema:

| Controllo | TV tra seed | Variazione contro v7 batch | Esito |
|---|---:|---:|---|
| aggiornamento online, un worker | 35,8437 pp | +0,4713 pp | respinto |
| baseline per l'azione avversaria | 34,4993 pp | -0,8731 pp | respinto |
| averaging non lineare | 33,1052 pp | -2,2672 pp | respinto |

Nessun controllo ha raggiunto la riduzione minima del `20%`; tutti hanno peggiorato l'accordo medio con Monker. Una nuova astrazione è quindi giustificata come esperimento separato.

## Mapping congelato prima dell'implementazione

Il mapping usa le stesse feature Monte Carlo del v7, con MC8 e seed di partizione invariati. Cambia soltanto la ripartizione dei bit:

| Street | Capacità | Gruppo categoria | Equity | Profilo |
|---|---:|---:|---:|---:|
| Flop | 32 | 2 bit | 3 bit | 0 bit |
| Turn | 128 | 3 bit | 4 bit | 0 bit |
| River | 512 | 4 bit | 4 bit | 1 bit |

I gruppi categoria sono:

- flop: `high card/pair`, `two pair/trips`, `straight/flush`, `full house o superiore`;
- turn: categoria esatta per le prime sette categorie; `quads/straight flush` condividono l'ultimo gruppo;
- river: categoria esatta, con i nove valori Short Deck distinti.

L'equity conserva i bit più significativi del bin MC16. Gli eventuali bit residui codificano la proiezione distribuzionale ordinata già usata dal v7. Il bucket finale è `gruppo categoria | equity | profilo`, senza hash o permutazioni.

Per capacità diverse da `32/128/512`, ma ancora potenze di due, il numero di bit del gruppo categoria resta rispettivamente `2/3/4`; fino a quattro bit sono assegnati all'equity e il resto al profilo. Una capacità che non contiene i bit minimi della street viene rifiutata.

## Invarianti

1. Il mapping è deterministico, suit-invariant e limitato alla capacità dichiarata.
2. A street, gruppo categoria e profilo uguali, una equity maggiore non produce un bucket inferiore.
3. Il v8 dimentica la classe preflop e conserva soltanto il bucket della street corrente, come il v7.
4. Albero, payoff, algoritmo, averaging, sampling e regole monetarie restano invariati.
5. Il formato della policy passa a `1.7`; le policy v7 `1.6` restano leggibili, mentre una policy v8 con minor inferiore a `7` viene rifiutata.

## Gate progressivo

Il confronto accoppiato usa Linear MCCFR, batch `32`, otto worker, MC8, partizione `32/128/512`, gli stessi due seed e il contratto monetario v2.

1. Mapping, limiti, suit-invariance, determinismo, persistenza e parità tra worker: tutti PASS.
2. Screen a `250.000` iterazioni, due seed: il v8 deve migliorare di almeno `0,5 pp` la WMAE media contro Monker oppure ridurre di almeno il `20%` la TV tra seed. La metrica non scelta non può peggiorare di oltre `0,5 pp`.
3. Devono essere occupati più bucket del v7 su almeno due street e il payload non deve superare `512 MiB`.
4. Solo dopo il PASS dello screen è ammessa la conferma a `500.000` iterazioni; ogni run più lunga richiede un nuovo gate scritto.

Il gate finale R6 resta invariato: WMAE `<= 1 pp`, TV media `<= 2 pp`, P95 TV `<= 5 pp`, errore root aggregato `<= 1 pp` e compatibilità EV prevista dal protocollo. Un challenger migliore del v7 ma lontano da queste soglie resta ricerca non qualificata.

## Rischi dichiarati

- I gruppi categoria più larghi al flop possono unire made hand con distribuzioni future diverse.
- MC8 lascia stime di equity rumorose; il v8 usa più risoluzione su quel segnale.
- Una minore divergenza tra seed non prova un minore abstraction error.
- La comparabilità con Monker resta incompleta finché il suo albero e la sua astrazione postflop non sono noti.

## Stato iniziale

`PROTOCOL_FROZEN / EXPERIMENT_COMPLETED / GATE_FAIL`.

Risultati e decisione: [DISTRIBUTIONAL_STREET_ADAPTIVE_V8_GATE_2026-09-11.md](DISTRIBUTIONAL_STREET_ADAPTIVE_V8_GATE_2026-09-11.md).
