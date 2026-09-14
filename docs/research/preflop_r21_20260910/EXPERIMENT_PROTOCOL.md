# Protocollo R2.1 — fattibilità iniziale dell'astrazione

## Contratto congelato prima dei run

- Gioco: fixture `GTP-HU-PREFLOP-CO40-001`, invariata.
- Algoritmo: `external_sampling_v2_opponent_pass_average`, un thread.
- Baseline: rappresentazione fisica exact con suit isomorphism lossless.
- Candidata: feature distribuzionali da sole carte visibili; capacità Flop/Turn/River 256/1.024/4.096; sola osservazione corrente, history delle puntate conservata, preflop class dimenticata nel postflop.
- Seed distinti: partizione `5900000000000000001`, training `5200000000000000101/102`, valutazione `5300000000000000201/202`.

Il primo esperimento usa 100.000 iterazioni per configurazione, 20.000 deal di valutazione, 5.000 iterazioni di risposta e 10.000 deal per risposta. Il limite è 15 minuti wall per processo e 12 GiB di peak working set. Se entrambe le varianti terminano con margine, una seconda replica indipendente viene eseguita con lo stesso budget. Non si prolunga un processo oltre il limite per ottenere un esito favorevole.

## Criteri fissati

Il prototipo supera R2.1 se:

1. i test ridotti dimostrano mapping deterministico, suit invariance, assenza di lettura del runout futuro, capacità rispettate e policy normalizzate;
2. ogni processo termina entro 900 s e sotto 12 GiB;
3. il payload numerico minimo e il numero di infoset diminuiscono rispetto alla baseline fisica allo stesso numero di iterazioni;
4. gli intervalli al 95% dell'EV root della candidata e della baseline si sovrappongono, oppure la differenza assoluta fra le medie è al massimo 0,5 ante;
5. il risultato è riproducibile su due seed di training e valutazione indipendenti con partizione congelata.

La metrica di risposta appresa resta diagnostica e non certificata. Un eventuale valore zero di `abstract_nashconv_ante` non è interpretato come exploitability zero. Il riferimento Monker non entra nel mapping né nei criteri di costruzione.
