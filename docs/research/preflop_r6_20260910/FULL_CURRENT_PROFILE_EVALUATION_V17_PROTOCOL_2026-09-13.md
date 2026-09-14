# R6 — Protocollo V17: valutazione del profilo corrente completo

Data: 2026-09-13  
Stato: preregistrato prima dell'implementazione e delle run

## Obiettivo

V17 corregge un limite diagnostico, non il training. Fino a V16, la chart denominata `corrente
finale` mostrava le frequenze ottenute dal regret matching finale ai nodi preflop, ma gli EV delle
azioni erano calcolati continuando con la strategia media CFR. La policy postflop esportata
conteneva inoltre soltanto la media. Quel confronto non rappresentava quindi un profilo corrente
completo.

V17 deve produrre e distinguere due profili coerenti:

1. `media CFR`: frequenze medie preflop e postflop, valutate contro continuazioni medie;
2. `corrente finale`: frequenze correnti preflop e postflop, valutate contro continuazioni
   correnti.

Il calcolo della strategia, dei payoff terminali, delle equity e dei bucket non cambia.

## Base sperimentale

V17 usa il training V15, perché è la coppia con la migliore stabilità osservata fra seed. La
stratificazione V16 resta disattivata dopo il fallimento del suo gate.

- Linear MCCFR, batch 32, otto worker;
- rappresentazione V8 street-adaptive MC8, capacità `32/128/512`;
- quattro rollout con update medi simmetrici e Common Random Numbers;
- all-in preflop esatti;
- all-in postflop Flop e Turn esatti con cache lazy da 1.000.000 entry;
- evaluator `seven_card_table_v1:2236291214962974841`;
- due run sequenziali da 2.000.000 iterazioni;
- stessi seed di training, partizione e valutazione della coppia V15.

Non verranno usate run inferiori a 2M per decidere qualità o promozione. Gli smoke test ridotti
servono soltanto a verificare formule, formato, determinismo e gestione degli errori.

## Valutazioni interne

Per ciascun profilo e seed verranno prodotti:

- EV root e suo errore standard;
- EV di ogni azione root e di ogni decisione preflop;
- due risposte campionate indipendenti, una per giocatore;
- miglioramento inferiore osservato della risposta:

```text
L = max(0, BR_CO - EV_CO) + max(0, BR_BTN - EV_BTN)
```

`L` è un limite inferiore campionato: non è exploitability esatta e non certifica NashConv. Il
report deve includere seed, iterazioni della risposta, deal di valutazione ed errori standard. La
metrica precedente, ottenuta sommando direttamente le due risposte e poi applicando un solo clamp,
resta leggibile per compatibilità ma non guida il gate.

## Persistenza

La policy postflop viene estesa in modo versionato per salvare, per ogni information set:

- probabilità medie;
- probabilità correnti;
- action count e chiave già esistenti.

I file storici restano caricabili. Se un file precedente non contiene la policy corrente, una
query corrente deve fallire esplicitamente; non può sostituirla in silenzio con la media.

## Gate diagnostico

V17 chiude questo esperimento soltanto se:

1. build Release e suite HU passano;
2. persistenza, checksum, compatibilità storica e query media/corrente passano;
3. entrambi i seed completano 2M in non più di 60 minuti per solve;
4. ogni EV etichettato `corrente` usa il profilo corrente su tutto l'albero;
5. l'audit corrente-contro-corrente non seleziona azioni materialmente inferiori nei rami con
   reach pubblica almeno `1%`;
6. la TV fra seed viene riportata separatamente per media e corrente;
7. il limite inferiore di risposta è finito e corredato dai relativi errori standard.

Il gate diagnostico non promuove automaticamente V17 nel viewer. La promozione richiede ancora
TV non superiore a `5 pp` e WMAE non superiore a `5 pp` per ciascun seed. Se questi limiti non
passano, V13 resta il candidato visualizzato per default.

## Validazione richiesta

- gioco enumerabile in cui media e corrente differiscono e producono EV attesi distinti;
- round trip della policy postflop con entrambe le strategie;
- caricamento di un file storico e rifiuto esplicito della query corrente;
- normalizzazione e valori finiti per entrambi i profili;
- determinismo bit per bit fra uno e otto worker;
- serializzazione esplicita di scope, seed, campioni e limiti della best response;
- confronto della coppia V17 con V15 su tempo, WMAE, TV e audit locale.
