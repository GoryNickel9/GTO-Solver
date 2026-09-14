# Protocollo V11 — street-adaptive con memoria di categoria

Stato: `PROTOCOL_FROZEN / ENGINEERING_GATE_PASS / PAIRED_2M_PASS / COMPARATIVE_GATE_FAIL`

## Obiettivo

V11 verifica se la risoluzione corrente di V8 e la memoria compatta di V10 sono complementari. La variabile sperimentale è soltanto la rappresentazione postflop; gioco, albero preflop, trainer, semi e budget restano invariati.

## Rappresentazione congelata

`DistributionalStrengthAdaptiveCategoryHistoryV11` usa:

- Flop: bucket corrente street-adaptive V8;
- Turn: categoria Short Deck esatta visibile al Flop + bucket corrente V8 del Turn;
- River: categoria Short Deck esatta visibile al Turn + bucket corrente V8 del River.

La memoria contiene una sola street e un codice di categoria nell'intervallo `0..8`. Non conserva la classe preflop, il bucket completo precedente né carte private avversarie. La categoria storica viene valutata direttamente dalle carte visibili: non viene ricostruita dai gruppi di categoria V8, perché al Flop quei gruppi non sono invertibili.

## Configurazione delle run

- algoritmo: Linear MCCFR;
- traverser: media simmetrica K=4;
- iterazioni: `2.000.000` per seed, senza screen ridotto decisionale;
- seed: gli stessi due seed accoppiati usati per V7–V10;
- campioni bucket: MC8;
- capacità Flop/Turn/River: `32/128/512`;
- batch deterministico: `32`;
- worker: `8`;
- export: tutti i 20 nodi preflop, EV per azione e combo, policy postflop se il payload operativo lo consente;
- RAM: nessun gate artificiale; si registrano infoset, payload numerico e dimensione della policy.

Il formato della policy passa a `1.10`. Le policy V11 con minor precedente devono essere rifiutate.

## Gate di ingegneria

Prima delle run 2M devono passare:

1. build Release con warning trattati come errori;
2. test del mapping V8 corrente e della categoria storica esatta;
3. validazione, persistenza e query della policy `1.10`;
4. determinismo numerico 1 worker contro 8 worker;
5. suite HU preflop completa.

## Gate comparativo

Il baseline primario è V10/2M, la candidata più stabile. V11 passa il confronto se:

1. migliora la WMAE media di almeno `0,5 pp` oppure riduce la TV fra seed di almeno il `20%`;
2. la metrica primaria non scelta non peggiora di oltre `0,5 pp`;
3. entrambe le run esportano 20 nodi preflop validi, EV finiti e strategie normalizzate;
4. il risultato è riproducibile con hash e configurazione registrati.

Il confronto secondario con V8 stabilisce quale versione pubblicare come più vicina a Monker. Il sito seleziona la minore WMAE media misurata a 2M; la stabilità fra seed resta visibile nel report e non viene nascosta.

## Gate finale R6

R6 resta invariato: WMAE `<= 1 pp`, TV media contro Monker `<= 2 pp`, P95 TV `<= 5 pp`, errore root aggregato `<= 1 pp` e compatibilità EV prevista dal protocollo generale. Un miglioramento relativo non sblocca R7.

## Albero postflop nel viewer

La policy postflop è una tabella di infoset astratti, non una chart per board già pronta. Il viewer può mostrarla soltanto dopo un export versionato che associ in modo verificabile ogni entry a street, history pubblica, giocatore, bucket e catalogo delle azioni. Non verranno creati nodi sintetici né confronti postflop con Monker, perché il relativo contratto non è disponibile.
