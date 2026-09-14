# R6 — Protocollo v7 per continuation rollout multipli al root

## Ipotesi

La diagnostica 2M mostra che molte differenze fra azioni root sono piccole rispetto alla dispersione dei campioni. Il challenger `root-multi-rollout` riduce la varianza dell'aggiornamento root mediando più continuation value indipendenti, senza cambiare astrazione o moltiplicare gli aggiornamenti degli infoset postflop.

## Contratto

- Baseline: v7, Linear MCCFR, batch 32, otto worker, MC8, `32/128/512`.
- Challenger iniziale: quattro rollout per osservazione root.
- Il primo rollout è la traversata di training esistente e conserva tutti i suoi aggiornamenti.
- Gli altri tre usano deal fisici indipendenti condizionati alla stessa classe del CO e policy congelata al batch.
- I rollout aggiuntivi sono read-only: i delta preflop e postflop prodotti vengono scartati.
- Ogni azione root usa lo stesso deal condizionato ma un RNG di continuazione separato.
- Il regret root usa la media dei quattro `Q_t(a)`; `V_t` viene ricalcolato dalla stessa strategia congelata.
- Strategy sum, averaging, evaluator, utility, albero e mapping v7 non cambiano.
- Monker non viene consultato dal trainer.

Il percorso è sperimentale e accetta soltanto batch External Sampling o Linear MCCFR con deal fisici indipendenti. Il valore predefinito resta un rollout e deve riprodurre la baseline bit per bit.

## Invarianti e test

1. Il sampler condizionato restituisce sempre la classe richiesta, senza carte duplicate, ed è deterministico a seed fisso.
2. Con un rollout il risultato resta bit-identico alla baseline.
3. In un solve da una iterazione, gli extra rollout non cambiano la policy postflop prodotta dalla traversata primaria.
4. Il reducer resta bit-identico con 1, 2, 4 e 8 worker.
5. Scratch aggiuntivo e numero di rollout sono limitati e serializzati nelle metriche.
6. I regret continuano a essere ricostruiti esattamente dalla telemetria root.

## Screen a due seed

Lo screen usa 250.000 iterazioni per seed. Il challenger passa alla conferma 2M soltanto se soddisfa tutti i criteri:

- errore standard mediano della differenza fra prima e seconda azione ridotto almeno del 35% rispetto alla baseline v7 250k;
- TV tra seed ridotta almeno del 15%;
- WMAE media contro Monker non peggiore di oltre 1 punto percentuale;
- nessuna regressione di correttezza, finitezza o memoria;
- costo wall non superiore a 4 volte la baseline 250k.

La riduzione dell'errore standard è il criterio causale. WMAE e TV sono gate esterni di sicurezza e non vengono usati per adattare il trainer.

## Conferma

Se lo screen passa, si eseguono due seed a 2M. Un ulteriore aumento è autorizzato soltanto con una riduzione ripetibile della TV e della quota di decisioni sotto 2 SE. Se lo screen fallisce, il challenger viene respinto e non si aumenta il budget.

## Eccezione richiesta dopo lo screen

Lo screen 250k ha fallito i gate di TV e WMAE. L'utente ha poi autorizzato esplicitamente due run a 2M per verificare se il verdetto dipendesse dal budget ridotto. Questa eccezione non modifica retroattivamente i criteri dello screen: produce una conferma diagnostica separata, con gli stessi due seed e un'unica variabile sperimentale, `root_action_value_rollouts=4`.

Il challenger può essere rivalutato soltanto sui dati completi delle due run. Anche in caso di miglioramento, la promozione a soluzione qualificata richiede il gate R6 originale; la sola riduzione della varianza o della distanza fra seed non certifica convergenza.
