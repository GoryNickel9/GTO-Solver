# Protocollo esperimenti R6

Fixture, regole, action tree, mapping distribuzionale v2, seed, precisione `double` e valutazione fisica restano fissi. Il primo confronto usa capacità `64/256/1.024`, 50.000 iterazioni, 10.000 deal di valutazione, 2.500 iterazioni della risposta appresa e 5.000 deal per risposta. Il batch congelato contiene 64 iterazioni e viene ridotto in ordine stabile. Si misurano 1, 2, 4 e 8 worker per External Sampling e Linear MCCFR.

Il confronto qualità successivo usa 500.000 iterazioni, 20.000 deal di valutazione, 10.000 iterazioni della risposta appresa e 10.000 deal per risposta. I challenger sono External Sampling, Linear MCCFR e DCFR lazy 1,5/0/3. DCFR resta online e single-thread perché il suo clock non è stato validato sotto aggiornamenti ritardati.

La sottocopertura osservata riapre soltanto la capacità della stessa mappa versionata. Le capacità diagnostiche `2/8/32`, `8/32/128` e `32/128/512` usano External Sampling, otto worker e lo stesso batch. Nessun run usa frequenze o EV esterni durante il training.

Il challenger di riduzione della varianza usa una media incrementale separata per infoset e azione avversaria. La baseline viene letta al confine del batch prima di osservare il campione; lo stimatore applica `sum(sigma * b) + v_sample - b_sample`. Il controllo 50k confronta plain e baseline. Il solo follow-up informativo usa 500k sulla partizione `32/128/512`.

Dopo il fallimento iniziale, il protocollo ammette controlli isolati. Public-board stratified usa lo stesso batch e mapping; DCFR 2M cambia soltanto la durata; bucket-history conserva i bucket precedenti con capacità `8/32/128`; chance-sampled CFR enumera tutte le azioni ma continua a campionare deal fisici. Un mapping balanced-strength viene misurato e poi respinto; il codice torna al mapping v2. Un controllo DCFR aumenta soltanto le capacità a `256/1.024/4.096`, lasciando invariati algoritmo, seed, campioni e valutazione del run standard da 500k. Il controllo finale confronta MC8 e MC32 a 100.000 update sulla stessa partizione `64/256/1.024`; nessun altro parametro cambia. Nessun controllo usa il target per costruire policy o bucket.

Il controllo di refinement esegue 500.000 update DCFR standard e poi 500.000 update del solo albero preflop con la strategia media postflop congelata. Il controllo di stabilità ripete DCFR a 500.000 e 2.000.000 update con seed di training e valutazione indipendenti, mantenendo fisso il seed della partizione. La distanza fra candidate e la media root a due seed sono diagnostiche: non sostituiscono il confronto esterno e non costituiscono una policy completa qualificabile.

Hardware e software: Windows, build Release MSVC 19.51, massimo otto thread CPU, nessuna GPU. Il wall include compilazione della betting plan, training, valutazione fisica e risposte apprese; esclude l'avvio del processo. Working set e private bytes sono campionati ogni 100 ms dal processo host.
