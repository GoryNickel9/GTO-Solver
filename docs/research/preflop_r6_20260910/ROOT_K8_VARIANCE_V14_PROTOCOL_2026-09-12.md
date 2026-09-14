# R6 — Protocollo V14 per la riduzione della varianza root K=8

Data: 2026-09-12  
Stato: pre-registrato, run 2M da eseguire

## Obiettivo

V14 verifica una sola modifica rispetto a V13: porta da quattro a otto i rollout indipendenti
usati per stimare il continuation value di ogni azione root, per entrambi i traverser. L'obiettivo è
ridurre la TV fra seed senza cambiare il gioco, l'astrazione o il valore esatto degli all-in.

La decomposizione V13 mostra che `12,1314 pp` dei `12,6370 pp` di TV media, pari al `96,0%`,
spostano massa tra azioni con gap medio non superiore a `0,1a`. Inoltre `7,8579 pp` sono entro due
SE marginali. Questo identifica la varianza dei continuation value come causa verificabile prima di
introdurre una nuova astrazione.

## Contratto congelato

Restano invariati:

- fixture monetaria v2 e albero `fnv1a64:a68337fa567aa2d9`;
- Linear MCCFR, batch 32, otto worker e aggiornamenti medi simmetrici;
- V8 street-adaptive MC8 con capacità `32/128/512`;
- oracle esatto degli all-in preflop;
- enumerazione esatta degli all-in postflop su Flop e Turn;
- cache lazy degli all-in postflop limitata a 1.000.000 entry;
- seed di training, partition ed evaluation;
- due run complete e indipendenti da 2.000.000 iterazioni;
- export dei 20 nodi preflop e della policy dei 10.060 nodi decisionali postflop.

La sola variabile è:

```text
root_action_value_rollouts: 4 -> 8
```

Monker non partecipa al training né alla scelta interna. Il confronto esterno viene eseguito solo
dopo che entrambi i seed hanno superato i controlli di integrità.

## Gate

V14 viene promossa nel viewer soltanto se:

1. entrambi i seed completano almeno 2.000.000 iterazioni e producono export integri;
2. normalizzazione, telemetria cache, test HU e audit corrente-contro-corrente non regrediscono;
3. la TV root media fra seed non supera `5 pp`;
4. la WMAE root contro il riferimento non supera `5 pp` in ciascun seed;
5. tempo, RAM, SE root e costo per riduzione di varianza sono riportati senza omettere regressioni.

Il punto 4 misura comparabilità con il benchmark, non autorizza fitting di soglie, range, size o
payoff. Un fallimento mantiene R6 aperto anche se V14 migliora V13.

## Criterio causale

Il confronto primario è V14 contro V13 a parità di seed. La modifica è efficace se riduce la TV
media e la massa TV entro due SE marginali. La policy corrente resta una diagnostica: V13 mostra
che pubblicarla al posto della media aumenterebbe la TV di `2,1880 pp`.

Se K=8 non raggiunge il gate, non si aumenta automaticamente K. Il passaggio successivo richiede
una stima stratificata dei rami avversari con pesi di importance sampling verificati su un gioco
enumerabile, oppure un audit contro un'astrazione più fine. Entrambe le opzioni richiedono un nuovo
protocollo.
