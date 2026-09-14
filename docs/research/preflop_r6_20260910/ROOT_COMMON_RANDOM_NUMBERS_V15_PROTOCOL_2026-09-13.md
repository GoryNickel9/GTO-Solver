# R6 — Protocollo V15 Common Random Numbers

Data: 2026-09-13  
Stato: implementazione, test e coppia 2M completati; gate chiuso

## Obiettivo

V15 prova a ridurre la TV fra seed mantenendo il costo vicino a V13. Ogni azione root della stessa
combo usa lo stesso deal fisico e lo stesso seed della continuazione avversaria. Il numero di
rollout resta quattro.

Per due azioni `a` e `b`, la varianza della differenza campionata è:

```text
Var[Q(a) - Q(b)] = Var[Q(a)] + Var[Q(b)] - 2 Cov[Q(a), Q(b)]
```

Il riuso dello stesso flusso casuale conserva la distribuzione marginale di ogni `Q(a)`. Se le
continuation reagiscono in modo simile agli stessi quantili avversari, la covarianza è positiva e
la differenza è meno rumorosa. Il segno della covarianza non è assunto: lo misurano le diagnostiche
pairwise e la replica su due seed.

## Modifica isolata

La nuova opzione `--root-common-random-numbers` è disattivata per default. Quando è attiva:

- la traversata root del player aggiornato inizializza ogni ramo d'azione con lo stesso seed;
- ogni rollout root aggiuntivo usa un nuovo seed, condiviso però fra le cinque azioni;
- deal, board, distribuzione marginale delle azioni avversarie e pesi Linear MCCFR restano invariati;
- i delta delle continuation K=4 continuano a essere mediati simmetricamente per CO e BTN;
- l'ID algoritmo riceve il suffisso `root_common_random_numbers_v1`.

Il runner rifiuta CRN fuori da un batch congelato. Il cambiamento non modifica l'albero, le size,
i range, i payoff, il rake, l'equity o i bucket.

## Contratto V15

- Linear MCCFR, batch 32, otto worker;
- V8 street-adaptive MC8, capacità `32/128/512`;
- K=4 e aggiornamenti medi simmetrici;
- all-in preflop esatti;
- all-in postflop Flop/Turn esatti con cache lazy da 1.000.000 entry;
- evaluator `seven_card_table_v1:2236291214962974841`, lo stesso di V13;
- seed, partition ed evaluation identici a V13;
- due run sequenziali da 2.000.000 iterazioni;
- export di 20 nodi preflop e 10.060 nodi decisionali postflop.

Monker viene consultato soltanto dopo la validazione interna della coppia.

Una prima esecuzione V15 è stata annullata a 55 minuti prima dell'export: era partita senza
`--seven-card-table` e avrebbe ripetuto il confondente di V14. Non è una soluzione né un dato di
qualità. La run valida deve serializzare il fingerprint dell'evaluator sopra indicato.

## Gate

V15 entra nel viewer solo se:

1. entrambi i seed completano 2M e ogni solve resta entro `60 minuti`;
2. gli 11 test HU Release, normalizzazione, fingerprint ed export passano;
3. la TV root media fra seed non supera `5 pp`;
4. la WMAE root non supera `5 pp` in ciascun seed;
5. l'audit corrente-contro-corrente non trova azioni materialmente dominate nei rami con reach
   pubblica almeno `1%`.

La soglia temporale di 60 minuti è prudenziale: V13 richiedeva 33–37 minuti per seed e CRN non
aggiunge traversate. Un miglioramento di qualità che supera il tempo viene documentato ma non
promosso.

## Validazione precedente alle run

```text
Build MSVC Release: PASS
R6_HU_PREFLOP_PARALLEL_TESTS=PASS assertions=2088
CTest HU Release: 11/11 PASS, 559,19 s
Smoke CLI: PASS, flag e ID algoritmo serializzati
```

I test verificano default invariato, effetto del nuovo stimatore, determinismo bit per bit con uno
e otto worker e rifiuto della modalità online non congelata. La correttezza marginale segue dal
riuso dello stesso seed: cambia la distribuzione congiunta fra azioni, non la distribuzione di ogni
singola azione.

## Esito

Entrambi i seed hanno completato 2M in `39,15` e `35,58 minuti`. La TV media fra seed scende da
`12,6370` a `10,9453 pp`, ma resta sopra `5 pp`; la WMAE media è `13,9472 pp`. V15 non entra nel
viewer. Metriche, audit e hash sono nel
[report di gate](ROOT_COMMON_RANDOM_NUMBERS_V15_GATE_2026-09-13.md).
