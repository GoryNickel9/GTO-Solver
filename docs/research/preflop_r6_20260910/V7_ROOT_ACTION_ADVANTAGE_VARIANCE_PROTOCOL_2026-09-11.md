# R6 — Protocollo per la varianza del vantaggio d'azione v7

## Obiettivo

Questo esperimento deve distinguere due cause della divergenza tra i seed v7:

1. differenze medie persistenti tra i valori delle azioni;
2. rumore elevato dei campioni usati dagli aggiornamenti MCCFR.

La misura primaria è il vantaggio istantaneo al CO root:

`A_t(a) = Q_t(a) - V_t`

Per Linear MCCFR ogni osservazione riceve lo stesso peso `t` usato dal regret update. Il confronto fra due azioni usa campioni accoppiati della stessa traversata:

`D_t(a,b) = A_t(a) - A_t(b) = Q_t(a) - Q_t(b)`

## Modifica consentita

La modifica è soltanto osservativa. Per ogni classe preflop e per le cinque azioni root registra:

- conteggio delle osservazioni;
- somma dei pesi e numerosità effettiva;
- media pesata, deviazione standard pesata ed errore standard descrittivo di `A_t(a)`;
- media pesata ed errore standard descrittivo di ogni differenza accoppiata `D_t(a,b)`;
- errore con cui `somma_pesi × media_pesata` ricostruisce il regret cumulativo.

La covarianza viene aggiornata online con Welford pesato. La varianza campionaria usa il denominatore di affidabilità `sum(w) - sum(w²) / sum(w)`; la numerosità effettiva è `sum(w)² / sum(w²)`.

Sono incluse soltanto le traversate principali in cui il CO è il traverser al root. Sono esclusi refinement preflop, training delle risposte e valutazioni post-hoc. Le statistiche seguono una policy non stazionaria e campioni serialmente dipendenti: gli errori standard sono diagnostici, non intervalli di confidenza formali né prove di convergenza.

## Invarianti

- L'ordine pubblico delle azioni resta `all_in`, `raise_6`, `raise_10`, `call`, `fold`.
- Seed, RNG, ordine delle traversate, policy, regret e strategy sum non cambiano.
- Le differenze accoppiate sono antisimmetriche; i relativi errori standard sono simmetrici e nulli sulla diagonale.
- Tutti i valori esportati sono finiti; deviazioni ed errori standard non sono negativi.
- Per Linear MCCFR il regret ricostruito coincide con lo stato del trainer entro l'errore di arrotondamento.

## Gate prima delle run lunghe

Le run v7 da due milioni sono autorizzate soltanto se:

1. il build Release con warning come errori passa;
2. i test sampling e parallel verificano invarianti e riproducibilità bit per bit;
3. una replay v7 ridotta produce strategia root e numero di infoset bit-identici all'artefatto precedente;
4. il controllo di ricostruzione dei regret passa per ogni classe osservata.

## Run di conferma

Superato il gate, si ripetono i due seed v7 a `2.000.000` iterazioni con il contratto già congelato: Linear MCCFR, batch `32`, otto worker, partizione `32/128/512`, `MC8`, stesso seed di partizione e seed di training/evaluation già usati.

L'analisi finale deve riportare almeno:

- AA call contro raise 6a per entrambi i seed;
- classi con azione corrente dominante diversa;
- rapporto `|media differenza| / SE accoppiato` per le decisioni discordanti;
- quota delle decisioni discordanti che rimane sotto `2 SE` in almeno un seed;
- decisione esplicita sul fatto che più campioni, una baseline o una nuova astrazione siano il prossimo esperimento giustificato.

## Criterio di arresto

Questa diagnostica non qualifica una strategia. R6 resta fallito finché i gate di qualità definiti non sono soddisfatti. Se la replay cambia la strategia o il regret non viene ricostruito, le run lunghe non partono e il difetto viene corretto prima di procedere.
