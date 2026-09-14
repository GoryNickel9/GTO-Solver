# R6 — Protocollo del profilo strutturato v7

## Obiettivo

Verificare se il peggioramento del v6 dipende dall'hash delle feature e dalla perdita della categoria corrente. Il v7 è un challenger R6: non modifica il mapping legacy, non usa la strategia Monker come segnale di training e non autorizza R7.

## Mapping congelato prima del test

Il bucket conserva tre livelli ordinati:

1. categoria della mano visibile, con 16 strati riservati e nove usati;
2. equity Monte Carlo entro la categoria;
3. coordinata distribuzionale ordinata, senza hash, ricavata da equity contro tre gruppi di categoria avversaria, varianza, categoria futura dominante e texture del board.

Con capacità `32/128/512`, i bit interni a ogni categoria sono ripartiti così:

| Street | Capacità | Categoria | Equity entro categoria | Profilo distribuzionale |
|---|---:|---:|---:|---:|
| Flop | 32 | 4 bit | 1 bit | 0 bit |
| Turn | 128 | 4 bit | 2 bit | 1 bit |
| River | 512 | 4 bit | 3 bit | 2 bit |

La coordinata distribuzionale è la media pesata e normalizzata delle feature già calcolate dal mapping. I pesi sono fissati nel codice e non dipendono dal riferimento esterno. La quantizzazione è monotona: a categoria e profilo uguali, un'equity maggiore non può produrre un bucket inferiore.

Capacità inferiori a 32 vengono rifiutate dal v7 perché non possono separare le nove categorie. Il mapping resta deterministico, suit-invariant, limitato alla capacità dichiarata e basato soltanto sulle carte visibili più runout Monte Carlo generati dal seed di partizione.

## Gate progressivo

Il confronto accoppiato usa Linear MCCFR, batch `32`, otto worker, MC8, partizione `32/128/512` e gli stessi due seed del v6.

1. Test unitari e persistenza `1.6`: tutti PASS.
2. Screen a `100.000` iterazioni, due seed: proseguire solo se la WMAE media e la TV media migliorano entrambe rispetto al legacy accoppiato, senza peggiorare entrambi i seed.
3. Conferma a `250.000` iterazioni, due seed: proseguire solo se il vantaggio resta presente e lo stato non supera `1,5x` il payload legacy equivalente.
4. Conferma a `500.000` iterazioni e valutazione EV forzata su almeno `200.000` deal: richiesta prima di qualsiasi run più lungo.

Il gate finale R6 resta invariato: WMAE `<= 1 pp`, TV media `<= 2 pp`, P95 TV `<= 5 pp` ed errore root aggregato `<= 1 pp`, oltre alla compatibilità EV prevista dal protocollo. Un miglioramento che resta lontano da queste soglie è evidenza di ricerca, non qualificazione.

## Invarianti e rischi

- La categoria corrente non collide con un'altra categoria alle capacità ammesse.
- Il mapping non consulta frequenze o EV Monker.
- La strategia media, l'albero, i payoff e il sampling restano invariati.
- Il profilo è ancora una proiezione scalare: può collassare forme distribuzionali diverse.
- MC8 introduce rumore nelle feature; aumentarne i campioni richiede un nuovo screen perché MC32 ha già peggiorato costo e qualità nel mapping legacy.
- La comparabilità scientifica con Monker resta incompleta finché non è noto l'albero postflop esterno.

## Stato iniziale

`PROTOCOL_FROZEN / EXPERIMENT_COMPLETED / GATE_FAIL`.

Risultati e decisione: [DISTRIBUTIONAL_STRUCTURED_V7_GATE_2026-09-11.md](DISTRIBUTIONAL_STRUCTURED_V7_GATE_2026-09-11.md).
