# Piano di correzione delle regressioni di iterazioni e tempo — 2026-09-05

## Analisi

### Obiettivo

Portare bucketing e subgame solving production a una configurazione globale,
con CFR+ e otto thread, che non richieda più iterazioni e non richieda più
tempo del percorso production precedente. Non sono accettabili compromessi in
cui uno dei due assi migliora mentre l'altro peggiora.

I benchmark servono a validare il prodotto condiviso. Non è ammesso cambiare
K, feature, algoritmo o parametri in base al nome della fixture, al board o al
fingerprint.

### Baseline congelata

I valori completi sono in
`docs/BUCKETING_PRODUCT_BENCHMARK_BASELINES_2026-09-05.md`.

| Benchmark | Exact production: iterazioni | Exact production: solver mediano | Exact production: wall mediano | Migliore point qualification bucketed |
|---|---:|---:|---:|---|
| AHKHQH | 80 | 0,758705 s | 6,231883 s | K16, primo `<1%` a 200; 500 iterazioni, 9,936001 s mediani |
| TH7D6S | 80 | 19,948228 s | 35,208170 s | K128, primo `<1%` a 400; 435,349473 s mediani |
| TSTC9D | 160 | 184,095930 s | 208,403423 s | nessuna solve bucketed |

La baseline exact usa target dEV, mentre i run bucketed usano exact full-game
NashConv. Prima del confronto finale occorre certificare entrambe le famiglie
con la stessa metrica. Questa correzione metodologica non autorizza ad alzare i
limiti di iterazioni o tempo.

### Evidenza del problema

Il percorso W/T/L/equity v1 K32 su TH termina a 800 iterazioni con
`2,8595946893%` NashConv/pot e `752,519128 s`. Il candidato potential-aware v2
K32 migliora a `2,8185024455%` e `720,911874 s`, ma resta respinto. La curva v2
ha il minimo osservato a 400 iterazioni (`2,7619094034%`) e poi risale:

```text
iterazione:       100       200       300       400       500       600       700       800
NashConv/pot: 4,598051  3,000406  2,788622  2,761909  2,771263  2,787935  2,804004  2,818502 %
```

Continuare a iterare non risolve il floor. Anche il miglioramento di circa
`4,20%` del wall v2 contro v1 è insufficiente rispetto al prodotto exact.

## Stato effettivo del subgame solving — audit 2026-09-05

**Implementato come resolver locale; decomposizione production completa non
ancora implementata né qualificata.** Non è corretto dichiararlo già equivalente
alle architetture commerciali con solving a profondità limitata.

| Capacità | Stato verificato | Evidenza |
|---|---|---|
| Estrazione di frontier senza tagliare information set, reach dal blueprint | Presente | `libs/subgame/src/subgame_solver.cpp` |
| Solve locale CFR+, merge e fallback | Presente | `solve_subgame`, `solve_abstract_subgame` |
| Resolver postflop nativo su frontier canonica, range condizionati, snapshot/rollback | Presente | `libs/postflop/src/postflop_solver.cpp`, CLI `postflop resolve-bucketed` |
| Controllo della strategia composta con NashConv exact prima/dopo | Presente | modalità `ExactNashConvGuard`; certificazione full-game |
| Trunk con boundary CFV, gadget di safe resolving e decomposizione CFR-D | Da implementare | non sostituito dal controllo ex post |
| Scheduler di sottogiochi con stato caricato/scaricato entro budget | Da implementare | il resolver nativo opera sul layout/checkpoint completo esistente |
| Depth-limited value model e gestione del suo errore | Da implementare | nessuna qualifica production disponibile |
| Accelerazione della terna mediante decomposizione | Non dimostrata | i run K32 v1/v2 chiamano `solve_postflop_abstracted`, senza resolver |

Il guard corrente accetta solo un candidato la cui NashConv full-game non
supera quella del blueprint oltre la tolleranza numerica. Offre un controllo
misurato sul risultato composto, ma richiede due certificazioni globali e non
costituisce una garanzia locale tramite boundary CFV. Non assicura un
miglioramento: può restituire integralmente il blueprint. Di conseguenza non
abbiamo ancora misurato il risparmio di tempo/RAM di una decomposizione completa.

### Confronto con le architetture commerciali

Non esiste un'unica architettura commerciale. La documentazione tecnica di
[PioSOLVER](https://piosolver.com/docs/technical_details/) descrive solving senza
card abstraction; la pagina sul
[salvataggio](https://piosolver.com/docs/viewer/saving_trees/) precisa che il
full tree rimane in memoria durante il solve. GTO Wizard descrive invece
[solving per street e dynamic depth-limited resolving](https://blog.gtowizard.com/gto-wizard-ai-custom-multiway-solving/)
con reti neurali, dichiarando quel motore privo di card abstraction e blueprint.
Sono descrizioni dei produttori: non provano equivalenza con il nostro solver
Short Deck, CPU locale, né autorizzano confronti diretti dei tempi.

Va quindi ritirata la precedente generalizzazione secondo cui tutti i solver
commerciali usano bucketing e lo stesso tipo di subgame solving.

## Possibilità di migliorare insieme le quattro metriche rispetto a DCFR

**Possibile come obiettivo ingegneristico; non dimostrato dal candidato corrente
e non garantito da CFR+, bucketing o subgame solving.** Algoritmo di aggiornamento,
rappresentazione e decomposizione sono scelte distinte. Le ottimizzazioni di
layout e memoria possono beneficiare anche DCFR: il confronto causale deve
includere, quando tecnicamente supportato, entrambi gli algoritmi sulla stessa
rappresentazione.

| Metrica | Meccanismo possibile | Limite della conclusione attuale |
|---|---|---|
| Iterazioni alla stessa accuratezza | Averaging verificato, warm start con costo incluso, astrazione più fedele, boundary CFV accurati | CFR+ non domina DCFR; più bucket non garantiscono meno exploitability |
| Solver mediano | Meno lavoro attivo, kernel contigui, parallelismo, certificazione riusabile | Il risparmio deve includere coordinamento, aggregazioni e guard |
| Wall mediano | Miglioramento solver più preparazione/cache/layout efficienti | Su TH la sola cache v2 richiede 158,057 s, oltre il wall storico di 35,208170 s: cold-start attuale già incompatibile |
| RAM | Stato astratto, sottogiochi residenti a richiesta, precisione qualificata | Contare anche cache, transizioni, blueprint, scratch e certificazione; lo stato compresso da solo non basta |

La [ricerca su DCFR](https://arxiv.org/abs/1809.04040) riporta vantaggi rispetto
a CFR+ nei giochi testati. Non prova quale vincerà nel nostro motore, ma esclude
l'assunto che scegliere CFR+ basti a ridurre le iterazioni. La variante CFR+
rimane il candidato richiesto; un eventuale mancato superamento va dichiarato.

### Contratto a quattro assi

Congelare una baseline verificata con stesso hardware, otto thread, albero,
range, soglia e frequenza di certificazione. Registrare separatamente i dati
storici e il replay contemporaneo per distinguere regressioni da variazioni
della macchina. Il cap di 12 GB è un limite di fattibilità, non prova di un
miglioramento RAM rispetto a DCFR.

La promozione richiede non regressione in tutte e quattro le metriche; dichiarare
un miglioramento simultaneo richiede riduzione misurata di ciascuna. Per RAM
pubblicare Peak RSS per processo con mediana/p95/massimo e stato solver a parte.
I massimi RSS storici disponibili sono 166.645.760 / 799.043.584 /
1.969.860.608 B per AHK/TH/TST; non sono mediane né memoria visualizzata da GTO+.

Con decomposizione, 80 iterazioni locali non equivalgono a 80 passate full-game.
Riportare iterazioni trunk, iterazioni di ciascun subgame, cicli esterni,
traversate e aggiornamenti totali, oltre al tempo fino alla certificazione
globale. Qualsiasi numero equivalente deve avere una formula fissata prima del
run. Non usare il reset dei contatori locali per rivendicare meno iterazioni.

Prima della promozione serve un confronto controllato che separi algoritmo,
precisione, astrazione e decomposizione. Le cause seguenti distinguono proprietà
visibili nel codice da ipotesi ancora da quantificare con quel confronto.

## Cause radice

### C1 — Contratto algoritmico diverso

La baseline usa DCFR alternato `1.5/0/2`; il percorso bucketed usa CFR+.
L'iterazione è una traversata completa in entrambi i casi, ma discount e
averaging possono produrre velocità di convergenza diverse. Il contributo del
passaggio a CFR+ all'aumento da `80` a `200/400+` non è ancora isolato:
sono cambiati anche astrazione, precisione e metrica di arresto.

### C2 — Metrica di arresto diversa

La baseline exact è target-driven su dEV; il bucketed è certificato con exact
full-game NashConv ogni 100 iterazioni e viene spesso eseguito fino
all'orizzonte fisso. dEV e NashConv non possono essere trattati come la stessa
misura. L'orizzonte fisso fa inoltre pagare iterazioni successive al primo PASS.

### C3 — L'implementazione condivide lo stato, ma attraversa ancora il gioco exact

L'attuale bucketing riduce regret e strategy sum, ma continua a enumerare:

- combo private esatte;
- chance outcome esatti;
- payoff terminali esatti;
- action value per combo;
- exact combo best response durante la certificazione.

In più aggrega i delta combo→bucket a ogni decision node. Ridurre lo stato non
riduce quindi in proporzione il lavoro di traversata; introduce anzi gather,
scatter e somme aggiuntive. Questo è il motivo principale per cui il costo per
iterazione non scende come atteso.

### C4 — Floor dell'astrazione

Con K32 più combo strategicamente diverse condividono una sola strategia. Lo
schema v1 aggregava tutti i runout in W/T/L/equity e perdeva il potenziale della
prossima street. Il v2 conserva quantili next-street e migliora ogni checkpoint
TH, ma K32 resta troppo poco espressivo: l'errore rialzato non scompare con
altre iterazioni.

### C5 — Precisione e hot path differenti

Il bucketed production è vincolato a stato Float64 e percorre un ramo generico
non coperto da tutte le ottimizzazioni SIMD/codec del percorso exact. Allocazioni
e azzeramenti dei buffer di aggregazione per nodo, accessi indiretti
local→bucket e layout non bucket-major aumentano il costo.

### C6 — Certificazione costosa

Su TH v2 K32, 800 iterazioni spendono `688,629563 s` in traversata e
`30,309202 s` in certificazione. Ridurre le certificazioni aiuta il wall, ma da
solo non chiude il divario: la traversata resta il costo dominante.

### C7 — Preparazione cold-start

La cache v2 richiede `1,541 s` AHK, `158,057 s` TH e `126,667 s` TST. Questi
tempi non sono inclusi nel wall della singola qualification, che parte da cache
già costruita. Per il prodotto vanno pubblicati separatamente cold-start,
warm-cache e solve; nessuna colonna può essere rinominata per ottenere un PASS.

## Invarianti

1. CPU locale, nessuna GPU e massimo/target production di otto thread.
2. CFR+ resta l'algoritmo production richiesto; DCFR rimane controllo causale.
3. Chance e runout non vengono campionati nel gate autorevole.
4. Best response e NashConv finali restano combo-level esatti.
5. Card removal, range pesati, ruleset e action tree restano invariati.
6. Un solo contratto globale governa tutte le fixture.
7. Warm start, precomputation e cache vengono conteggiati esplicitamente; non si
   nasconde lavoro fuori dal timer.
8. Ogni candidato deve essere Pareto non-regressivo: tempo **e** iterazioni non
   superiori, qualità non inferiore, RAM entro il budget dichiarato.
9. Il percorso exact resta oracle e deve conservare test e benchmark propri.

## Spazio completo delle soluzioni

### A — Correggere il contratto di misura

Queste azioni non rendono il solver più veloce da sole, ma sono prerequisite.

1. **Cross-certification della baseline exact.** Calcolare exact NashConv sui
   checkpoint DCFR a `80/80/160`, senza retraining, e registrare insieme dEV e
   NashConv.
2. **Runner target-driven.** Fermare CFR+ al primo checkpoint strettamente sotto
   `1%`, registrando sia `requested_max_iterations` sia
   `completed_iterations`.
3. **Timer non ambigui.** Pubblicare feature preparation, layout/clustering,
   traversal, certification, solver total e process wall.
4. **Cold e warm separati.** Un gate per primo solve senza cache e uno per solve
   con cache verificata; il prodotto deve mostrare entrambi.
5. **Work counters normalizzati.** Nodi, chance outcome, terminali, regret entry
   e strategy entry per iterazione, per attribuire il costo senza affidarsi al
   solo wall rumoroso.
6. **Cinque processi solo dopo i pre-gate.** Mediana/p95, stesso exe hash, CPU
   idle e piano energetico registrati.

### B — Ridurre le iterazioni CFR+

1. **Verifica della semantica CFR+ standard.** Audit di alternating update,
   regret truncation, linear averaging, reach e ordine update. Un errore qui ha
   priorità assoluta su qualsiasi tuning.
2. **Averaging CFR+ globale.** Valutare delay e peso lineare con una sola policy
   versionata. Nessun valore per-fixture; il lavoro scartato prima del delay
   conta comunque nel totale.
3. **Regret-based pruning CFR+.** Dopo un burn-in globale, saltare azioni con
   un criterio di pruning dimostrato e riattivarle periodicamente. I regret
   memorizzati da CFR+ sono troncati a zero: non si può usare una soglia negativa
   direttamente su quel buffer. Occorrono bound o stato ausiliario, con costo
   e garanzie espliciti. Exact BR finale non sostituisce la prova del criterio.
4. **Warm start multi-risoluzione.** Risolvere una granularità più piccola,
   prolungare strategia/regret su quella più fine e continuare. Si contano tutte
   le traversate di entrambi i livelli.
5. **Blueprint warm start.** Riutilizzare una soluzione compatibile per solve
   successive reali. Il cold-start resta comunque un gate separato.
6. **Feature potential-aware.** Quantili next-street, histogrammi/CDF ed EMD o
   Wasserstein per non fondere mani con equity media uguale e potenziale diverso.
7. **Clustering reach-aware robusto.** Seeding deterministico weighted
   k-means++, medoid o quantili pesati; più restart deterministici soltanto se il
   costo cold-start resta nel budget.
8. **Astrazione street-aware.** River exact o quasi exact, turn più fine del
   flop, con formula globale basata su street e cardinalità, non sul board.
9. **Transition-consistent buckets.** Penalizzare split incoerenti tra street
   consecutive per stabilizzare i counterfactual value.
10. **Action-aware/CVF-aware features.** Usare vettori di counterfactual value o
    risposta alle azioni ottenuti da un pilot solve generico, conteggiandone il
    costo. È più fedele dell'equity, ma rischia circolarità e overfitting.
11. **Refinement adattivo globale.** Split dei bucket con alta varianza di
    regret/CFV secondo una regola comune; merge dei bucket indistinguibili.
12. **K globale più alto.** K64/K128 può abbassare il floor, ma è accettabile
    solo se chiude anche tempo, RAM e iterazioni. Non è una soluzione automatica.
13. **Regola globale K(n).** Una funzione deterministica della cardinalità della
    partizione può evitare bucket inutilizzati e allocazioni eccessive. Non può
    contenere ID fixture, board specifici o fingerprint.
14. **DCFR come controllo/alternativa esplicita.** Serve a quantificare quanto
    del gap dipende da CFR+. Non sostituisce CFR+ production senza una nuova
    decisione dell'utente.

### C — Ridurre il costo di ogni iterazione

Prerequisito matematico: matrici bucket→bucket statiche non preservano
automaticamente i valori combo-level. La distribuzione interna ai bucket può
cambiare con history, reach e blocker. Prima del kernel aggregato occorre
dimostrare sufficienza della rappresentazione (inclusa memoria delle bucket
history/perfect recall) oppure dichiarare e misurare l'ulteriore approssimazione.
Enumerare esattamente le carte nella precomputazione non rende lossless il
gioco astratto risultante. Questo è un gate preliminare, non un dettaglio SIMD.

1. **Vera traversata del gioco astratto — priorità massima.** Precomputare e
   attraversare stati bucket-level invece di attraversare tutte le combo e
   condividere soltanto regret/strategy.
2. **Transizioni chance bucket→bucket.** Per ogni carta pubblica canonica,
   precomputare masse di transizione range- e blocker-correct; nessun sampling.
3. **Payoff terminali bucket×bucket.** Preaggregare fold e showdown con pesi
   combo esatti. Validare contro enumerazione combo-level su giochi ridotti.
4. **Layout bucket-major SoA.** Rendere contigui regret, strategy, reach e value
   per bucket/action; eliminare gather casuali.
5. **Local→bucket preordinato.** Riordinare le combo per bucket una volta e
   memorizzare offset/count, evitando mappe e branch nell'hot path.
6. **Buffer thread-local persistenti.** Eliminare `assign`, resize e zeroing
   ridondanti per decision node; usare generazioni o touched ranges.
7. **SIMD sulle action lane e sui bucket.** Vectorizzare regret matching,
   accumulo value e strategy sum dopo avere ottenuto layout contigui.
8. **Kernel specializzati per 2/3/4 azioni.** Generazione compile-time dei casi
   dominanti, mantenendo un fallback generico testato.
9. **Parallelismo chance/board bilanciato.** Profilare scaling 1/2/4/8, work
   stealing e granularità task. Otto thread devono essere realmente occupati,
   non soltanto configurati.
10. **Riduzione delle synchronization barrier.** Thread-local delta e reduction
    deterministica per blocchi, senza lock globali nell'hot path.
11. **Certificazione a due livelli.** Proxy conservativo economico ai checkpoint
    e exact BR solo quando il proxy può attraversare la soglia, più exact BR
    finale obbligatorio. Nessun proxy viene pubblicato come NashConv.
12. **Exact BR incrementale.** Riutilizzare topologia, payoff, ordine e buffer;
    invalidare soltanto le parti dipendenti dalla strategia.
13. **Evitare rebuild duplicati.** Preparare una volta layout, abstraction e
    analysis view; riusarli in solve, certify e query.
14. **Cache binaria versionata.** Eliminare parsing decimale e copie delle
    feature; mmap/read-only opzionale con checksum e atomic replace.
15. **Precisione ibrida.** Storage Float32 con accumulo/reduction Float64, o
    codec CFR+ dedicato. Promuovere solo se la curva e la strategia rialzata
    restano entro tolleranze più strette del guadagno.
16. **Precomputation condivisa.** Riutilizzare evaluator, matchup e runout table
    tra feature, traversal e BR senza duplicare memoria oltre budget.

### D — Ridurre il gioco attivo con subgame solving

1. **Trunk + subgame decomposition.** CFR+ sul trunk astratto e solve separati
   dei subgame alle frontier pubbliche.
2. **CFR-D/safe resolving.** Boundary counterfactual values e gadget game per
   impedire l'aumento di exploitability quando si sostituisce un subgame.
3. **Depth-limited solving.** Arrestare la traversata a frontier versionate e
   usare CFV verificati, non equity grezza.
4. **Canonical subgame cache.** Riutilizzare soluzioni fra board isomorfi con
   prova di compatibilità di range, blocker, action tree e ruleset.
5. **Solve-on-demand con budget.** Dare priorità ai subgame con maggiore reach;
   per il gate completo tutte le masse omesse devono avere un bound esplicito.
6. **Parallelismo per subgame.** Scheduler globale a otto thread, evitando otto
   pool annidati e oversubscription.
7. **Refinement locale governato.** Aumentare granularità dove il bound CFV è
   alto usando una regola matematica comune, non un elenco di fixture.

### E — Soluzioni combinate realistiche

1. **E1: CFR+ + vera traversata astratta + K64 globale.** Riduce il lavoro per
   iterazione e il floor; è il candidato diretto più semplice.
2. **E2: CFR+ + K(n) globale + river exact + transizioni preaggregate.** Migliore
   allocazione della capacità e più correttezza endgame.
3. **E3: CFR+ multi-risoluzione + refinement CFV-aware.** Mira soprattutto alla
   riduzione delle iterazioni, ma deve conteggiare il pilot solve.
4. **E4: trunk CFR+ + safe subgame solving + cache canonica.** È la strada più
   adatta al preflop e ai grandi alberi, ma richiede boundary CFV affidabili.
5. **E5: E2/E4 + precisione ibrida qualificata.** Solo dopo avere chiuso
   correttezza e profilo; non è il primo intervento.

## Alternative respinte in anticipo

- aumentare semplicemente a 800/1.600/3.200 iterazioni;
- promuovere il v2 K32 perché è soltanto meno lento del v1;
- usare K16 su AHK e K128 su TH;
- introdurre eccezioni per board, benchmark ID o fingerprint;
- ridurre i thread sotto otto per ottenere un oracle più semplice;
- rilassare la soglia `<1%`;
- chiamare exact una best response campionata;
- confrontare solver time exact con wall bucketed o viceversa;
- escludere cache, warm start o pilot solve dal tempo senza dichiararlo;
- sostituire CFV con equity grezza alle frontier;
- ottimizzare soltanto serializzazione/cache e dichiarare risolto il solve;
- promuovere Float32/codec senza differential test e exact BR;
- scegliere K128 globale senza prima dimostrare il limite di tempo.

## Piano operativo

### Fase 0 — Correzione metrologica

File coinvolti:

- runner globale e comando CLI di qualification;
- schema JSON dei report;
- documenti PERFORMANCE, VALIDATION e TESTING.

Implementazione minima:

1. cross-certificare i checkpoint exact con NashConv;
2. aggiungere stop target-driven e `completed_iterations` al report bucketed;
3. separare cold/warm/solver/wall;
4. aggiungere confronto automatico contro il baseline JSON versionato.

Gate: nessuna promozione se un campo temporale o metrico è ambiguo.

### Fase 1 — Profilo causale del costo per iterazione

Misurare AHK/TH a 100 iterazioni con v1/v2 e K16/32/64/128, senza usare i
risultati per scegliere parametri per-fixture. Attribuire tempo a traversal,
bucket aggregation, terminal, chance, regret matching, reduction e BR.

Kill gate: se una proposta non riduce almeno un costo dominante senza
peggiorare gli altri benchmark, non procede a TST.

### Fase 2 — Hot path bucket-major

Implementare nell'ordine:

1. preordine per bucket e offset contigui;
2. buffer persistenti/touched ranges;
3. kernel action-count specializzati e SIMD;
4. scaling 1/2/4/8 e determinismo.

Gate intermedio: stessa strategia/NashConv bitwise o entro tolleranza dichiarata,
stesse iterazioni, wall inferiore su AHK e TH.

### Fase 3 — Vera traversata astratta

Costruire transizioni chance e payoff terminali preaggregati. Validare prima su
Kuhn/Leduc e su un river Short Deck enumerabile, poi sul postflop nativo.

Gate matematico:

- conservazione della massa;
- utility zero-sum;
- card removal esatto;
- differenziale contro traversal combo-level;
- exact lifted NashConv finale.

### Fase 4 — Riduzione delle iterazioni

Dopo avere ridotto il costo per iterazione, valutare con una policy globale:

1. K64;
2. K(n) deterministico + river exact;
3. warm start multi-risoluzione;
4. regret pruning CFR+;
5. refinement CFV-aware.

Ogni candidato viene confrontato contro il genitore su tutti i checkpoint. Si
mantiene soltanto una modifica causalmente utile alla volta.

### Fase 5 — Decomposizione production

Collegare trunk, boundary CFV, safe subgame solver e cache canonica. Il guard
full-game exact decide merge o rollback. Nessun supporto preflop viene dichiarato
finché il tree/layout preflop non esiste e il gate postflop non è chiuso.

### Fase 6 — Gate globale finale

Ordine fisso AHKHQH → TH7D6S → TSTC9D:

1. un processo target-driven per fixture;
2. early reject immediato su qualità, iterazioni, solver time, wall, RAM o disco;
3. cinque processi indipendenti solo dopo tre PASS;
4. oracle seriale soltanto dopo le ripetizioni;
5. suite Release completa;
6. documentazione e commit finale.

## Gate finali non negoziabili

La configurazione production comune deve:

- usare CFR+ e otto thread;
- ottenere exact full-game `NashConv/pot <1%`;
- non superare `80/80/160` iterazioni su AHK/TH/TST;
- non superare, dopo calibrazione metrica, i solver time mediani exact
  `0,758705/19,948228/184,095930 s`;
- non superare i wall mediani exact
  `6,231883/35,208170/208,403423 s` nel contratto cold/warm corrispondente;
- non peggiorare root EV, invarianti o correctness;
- rispettare 12.000.000.000 B RAM e 10 GiB disco;
- non peggiorare la RAM rispetto al replay DCFR comparabile, confrontando
  mediana/p95/massimo del Peak RSS; il solo rispetto del cap non basta;
- usare una sola policy globale e artefatti versionati;
- superare cinque processi e la suite Release completa.

Se CFR+ non può soddisfare contemporaneamente questi limiti dopo vera
traversata astratta e decomposizione, l'esito corretto è un blocker documentato,
non più iterazioni e non una soglia più debole.

## Decisioni

1. Il candidato quantile v2 resta evidenza sperimentale e non viene promosso:
   migliora K32 TH ma non chiude il gate.
2. Il prossimo intervento non sarà un altro K isolato. Prima si corregge la
   misura e si profila il costo per iterazione; poi si costruisce la vera
   traversata bucket-level.
3. L'aumento di K viene rivalutato soltanto insieme a una riduzione dimostrata
   del lavoro per iterazione.
4. Il subgame solver esistente resta un componente verificato, ma non viene
   confuso con una decomposizione full-game production già completata.

## Passo successivo

Implementare esclusivamente la **Fase 0 — correzione metrologica**, iniziando
dalla cross-certification NashConv dei checkpoint exact e dal runner
target-driven con confronto automatico contro la baseline versionata.
