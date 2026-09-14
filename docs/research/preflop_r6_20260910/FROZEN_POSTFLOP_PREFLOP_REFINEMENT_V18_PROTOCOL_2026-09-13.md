# R6 — Protocollo V18: refinement preflop con postflop congelato

Data: 2026-09-13  
Stato: preregistrato prima dell'implementazione e delle run
Esito successivo: `ENGINEERING_PASS / SCIENTIFIC_REJECTED`

## Obiettivo

V17 mostra che la media CFR conserva scelte Call/Fold storicamente inferiori, mentre la corrente
le ha quasi tutte rimosse. V18 verifica se una seconda fase dedicata al solo albero preflop riduce
questa inerzia senza ripetere il costoso training postflop.

La prima fase resta V15:

- 2.000.000 di iterazioni Linear MCCFR;
- batch 32, otto worker;
- V8 street-adaptive MC8, capacità `32/128/512`;
- quattro rollout, update medi simmetrici e Common Random Numbers;
- all-in preflop esatti;
- all-in postflop Flop e Turn esatti con cache lazy da 1.000.000 entry;
- tabella a sette carte `seven_card_table_v1:2236291214962974841`.

La seconda fase esegue 2.000.000 di iterazioni aggiuntive sui soli 20 nodi decisionali preflop. La
policy postflop media ottenuta alla fine della prima fase è congelata: il refinement può leggere
le continuation, ma non aggiorna regret, somme o bucket postflop.

## Semantica del refinement

Il clock Linear MCCFR continua da `2.000.001` a `4.000.000`. Regret e somme preflop ricevono il
peso lineare globale dell'iterazione. La strategia media finale preflop combina entrambe le fasi;
poiché la somma dei pesi della seconda metà è tre volte quella della prima, il refinement
contribuisce al 75% del peso teorico complessivo.

Il refinement usa External Sampling sequenziale con un campione di continuation per traversata.
Non applica i quattro rollout alle continuation congelate e non aggiorna lo stato postflop. Questo
è parte esplicita del candidato V18: non verrà presentato come identico allo stimatore della prima
fase.

La modalità batch diventa valida soltanto per `LinearMccfr` seguito dal refinement sequenziale.
DCFR e Chance-Sampled CFR restano rifiutati perché i rispettivi clock non hanno qui una semantica
verificata. Il numero di worker non modifica RNG o ordine della seconda fase.

## Invarianti

- stato numerico postflop bit-identico prima e dopo il refinement;
- nessuna chiamata al solver postflop standalone;
- gioco, albero, range, size, payoff, rake, equity e bucket invariati;
- stessa policy congelata letta da entrambi i traverser;
- strategia e output deterministici fra uno e otto worker a parità di batch;
- `postflop_training_iterations=2.000.000`, `preflop_refinement_iterations=2.000.000` e
  `iterations=4.000.000` serializzati separatamente.

## Gate emendato prima dei risultati

Emendamento richiesto dall'utente il 2026-09-13 mentre il seed 1 era ancora in esecuzione e prima
che esistesse un output V18. Il gate assoluto congiuntivo `TV <= 5 pp` e `WMAE <= 5 pp` viene
sostituito, per la decisione V18, da un miglioramento relativo di almeno il `10%` in almeno una
metrica. Il limite assoluto di `5 pp` resta riportato come obiettivo di qualità, ma non decide la
promozione di V18.

La baseline accoppiata è V15/V17, la cui strategia appresa è bit-identica:

- TV della strategia media fra seed: `10,9453 pp`; passa a `<= 9,8508 pp`;
- TV media contro Monker: `34,8679 pp`; passa a `<= 31,3811 pp`;
- WMAE media contro Monker: `13,9472 pp`; passa a `<= 12,5525 pp`;
- audit EV Call/Fold medio sui percorsi con reach pubblica almeno `1%`: `7` casi complessivi
  (`6 + 1`); passa con al massimo `6` casi.

V18 entra nel viewer se build, suite HU, integrità, non-mutazione postflop e due run complete da
2M+2M passano, ed è raggiunta almeno una delle quattro soglie relative. Tutte le metriche vengono
comunque pubblicate: il criterio `and/or` non consente di nascondere un peggioramento nelle altre.

La policy corrente resta diagnostica e non guida da sola la pubblicazione. Il confronto Monker
conserva lo stato `REFERENCE_CONFIG_INCOMPLETE`; non viene usato dal training o dal refinement.
Non verrà avviata una V19 senza consenso esplicito dell'utente. Se V18 non raggiunge alcuna soglia,
l'esperimento non verrà scartato quando mostra un miglioramento positivo: resterà una candidata
promettente documentata. Il report includerà una diagnosi causale e una proposta concreta per
raggiungere il `10%`, senza implementare o avviare il candidato successivo.

## Validazione prima delle run

- caso ridotto continuo contro batch+refinement;
- determinismo bit per bit batch con uno e otto worker;
- confronto della policy postflop con e senza refinement;
- errori espliciti per combinazioni di algoritmo non supportate;
- smoke CLI con contatori e suffisso `frozen_postflop_rollout_refinement_v1`;
- due run complete a 2M+2M; nessun artefatto ridotto decide la qualità.

## Decisione successiva al gate

Il protocollo resta invariato come registrazione delle condizioni definite prima dei risultati.
V18 ha fallito tutte le soglie del `10%`. Una prima decisione l'aveva conservata come candidata
promettente per il miglioramento WMAE del `4,04%`; l'utente ha poi respinto questa promozione perché
la TV fra seed è aumentata del `18,92%` e il miglioramento esterno è insufficiente.

La decisione corrente, il nuovo gate con TV obbligatoria e il ritorno a V17 sono documentati in
[V18_REJECTION_AND_NEXT_GATE_DECISION_2026-09-13.md](V18_REJECTION_AND_NEXT_GATE_DECISION_2026-09-13.md).
