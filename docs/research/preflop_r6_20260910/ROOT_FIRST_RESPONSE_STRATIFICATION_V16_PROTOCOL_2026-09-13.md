# R6 — Protocollo V16: stratificazione della prima risposta

Data: 2026-09-13  
Stato: preregistrato prima dell'implementazione e delle run

## Obiettivo

V16 verifica se la TV residua fra i seed dipende dalla copertura casuale della prima risposta
avversaria dopo un'azione del traverser. V15 usa quattro rollout e Common Random Numbers, ma ogni
rollout estrae ancora quella risposta da tutta la CDF della strategia avversaria. Più rollout
possono quindi visitare la stessa risposta e ignorarne altre.

V16 divide la variabile uniforme usata per la prima risposta in quattro intervalli equiprobabili.
Il rollout `r`, con `r` compreso fra zero e tre, usa:

```text
U_r = (r + V_r) / 4, con V_r uniforme in [0, 1)
A_r = CDF_strategia^-1(U_r)
```

La media dei quattro rollout assegna peso `1/4` a ogni intervallo. Il valore atteso coincide con
l'integrale sulla variabile uniforme originale; cambia la varianza, non il target. Deal e numeri
casuali interni all'intervallo usano stream separati. Lo stesso `U_r` viene riusato fra le azioni
enumerate nello stesso rollout, conservando il coupling CRN di V15.

## Confine della modifica

La stratificazione si applica soltanto al primo nodo avversario raggiunto dopo la prima decisione
enumerata del traverser:

- nel pass CO, dopo ciascuna azione root del CO;
- nel pass BTN, dopo ciascuna azione della prima decisione BTN raggiunta in seguito all'azione CO
  campionata alla root;
- se il ramo termina prima di una risposta avversaria, il quantile resta inutilizzato;
- ogni decisione avversaria successiva conserva l'External Sampling ordinario.

L'opzione sperimentale è disattivata per default. È valida soltanto con batch congelato, almeno due
rollout, update medi delle continuation e media simmetrica dei due traverser. Questi vincoli servono
perché gli update dei quattro strati devono essere ridotti con lo stesso peso.

V16 non modifica albero, range, size, payoff, rake, bucket, evaluator, numero di rollout o numero di
iterazioni. Monker non viene letto durante il training.

## Contratto della coppia

- Linear MCCFR, batch 32, otto worker;
- rappresentazione V8 street-adaptive MC8, capacità `32/128/512`;
- quattro rollout, update medi simmetrici e Common Random Numbers;
- stratificazione della prima risposta attiva;
- all-in preflop esatti;
- all-in postflop Flop e Turn esatti con cache lazy da 1.000.000 entry;
- evaluator `seven_card_table_v1:2236291214962974841`;
- seed, partition seed ed evaluation seed identici alla coppia V15;
- due run sequenziali da 2.000.000 iterazioni;
- export dei 20 nodi preflop e della policy postflop.

Non verranno usate run ridotte per decidere la qualità. Smoke test e test unitari controllano
soltanto formula, validazione, determinismo e costo evidente.

## Gate

V16 sostituisce V13 nel viewer soltanto se:

1. build Release e suite HU passano;
2. entrambi i seed completano 2M in non più di 60 minuti per solve e dichiarano il fingerprint
   della tabella a sette carte;
3. TV della strategia media e TV della policy corrente fra seed non superano `5 pp`;
4. WMAE contro Monker non supera `5 pp` in ciascun seed;
5. l'audit corrente-contro-corrente non trova azioni materialmente inferiori nei rami con reach
   pubblica almeno `1%`.

La massa TV pesata per il gap EV resta una diagnostica e non sostituisce i limiti in punti
percentuali. Se V16 fallisce, la stratificazione resta disponibile soltanto come modalità di
ricerca e il viewer continua a usare V13.

## Validazione richiesta

- test della partizione dei quantili e dell'inversione della CDF;
- identità del valore atteso su un caso enumerabile;
- rifiuto delle configurazioni che non mediano gli strati;
- riproducibilità bit per bit fra uno e otto worker;
- algoritmo e opzione serializzati nell'output;
- confronto V15/V16 su tempo, WMAE, TV media, TV corrente e audit Call/Fold.
