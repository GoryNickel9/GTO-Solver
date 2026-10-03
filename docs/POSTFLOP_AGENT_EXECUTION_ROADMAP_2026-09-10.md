# Roadmap dell'agente: riduzione di CPU e RAM del solver postflop

Data: 2026-09-10. Stato: **piano operativo da eseguire; nessun nuovo backend qualificato**.

Mandato: individuare e rimuovere i colli di bottiglia del postflop, cambiando
algoritmo e introducendo astrazione quando le evidenze lo giustificano, senza
indebolire la verifica della soluzione nel gioco originale. Questo documento
specifica il comportamento dell'agente, l'ordine delle prove e le condizioni
per fermare un esperimento o promuoverne il risultato.

La sua redazione non avvia nuovi solve. Le misure citate provengono dal
[report di ricerca del 10 settembre](POSTFLOP_RESEARCH_DECISION_2026-09-10.md).
Quando riceve l'incarico di eseguire questa roadmap, l'agente riparte dalla
prima fase incompleta, verificando che codice ed evidenze siano ancora validi.

## 1. Autorità, mandato e vincoli

### 1.1 Come risolvere i conflitti fra documenti

La richiesta corrente autorizza esplicitamente algoritmi alternativi,
sampling, bucketing e astrazione. Per questa ricerca postflop supera i vincoli
storici che ammettevano soltanto ProductionDcfr o la stessa traiettoria
numerica. Non autorizza a cambiare in silenzio il gioco, la qualità richiesta
o il significato delle metriche.

| Documento | Come deve usarlo l'agente |
|---|---|
| Richiesta corrente e istruzioni applicabili | Definiscono il mandato; prevalgono sulle restrizioni di una campagna precedente |
| Questa roadmap | Definisce esecuzione, verifiche e decisioni della nuova ricerca postflop |
| [Report di ricerca](POSTFLOP_RESEARCH_DECISION_2026-09-10.md) | Fornisce diagnosi, dati, fonti e limiti delle evidenze disponibili |
| [Roadmap del 5 settembre](archive/legacy-postflop-2026-07-09/PRODUCTION_DCFR_PRODUCT_OPTIMIZATION_ROADMAP_2026-09-05.md) | Conserva la storia della campagna precedente; non vieta i nuovi algoritmi qui autorizzati |
| [Contratti card abstraction e subgame](specifications/CARD_ABSTRACTION_AND_SUBGAME_CONTRACT.md) | Descrivono le interfacce esistenti; ogni nuova semantica richiede un contratto e una versione espliciti |

Le fasi preflop presenti nella roadmap precedente non vengono modificate da
questo documento. Neppure una nota storica nella memoria dell'agente costituisce
prova dello stato corrente del codice.

Se un vincolo sembra implementato male, l'agente deve ricostruirne la catena:
richiesta → specifica → codice → test → report. Deve correggere una discordanza
dimostrabile nel lavoro autorizzato. Se esistono due interpretazioni che
cambiano il gioco o il criterio di qualità, deve presentare il caso concreto,
il comportamento attuale e la scelta raccomandata, continuando le attività che
non dipendono dalla risposta. Non deve chiedere nuovamente se siano consentiti
sampling o algoritmi alternativi: lo sono già.

### 1.2 Contratto matematico

Il gioco originale è quello definito da regole, board, range pesati, rake e
catalogo di azioni configurati, prima della card abstraction. Non coincide
automaticamente con il poker no-limit con qualsiasi importo di puntata.

Per la strategia media completa `sigma`:

```text
gain_i   = BR_i(sigma_-i) - u_i(sigma)
NashConv = gain_0 + gain_1
target   = max(gain_0, gain_1) / pot < 0.01
```

Il target corrente è quindi un massimo guadagno unilaterale inferiore all'1%
del pot. Con due giocatori implica `NashConv / pot < 0.02`; non significa
`NashConv / pot < 0.01`. Le unità, il pot usato nella normalizzazione e le
convenzioni dei payoff devono essere identiche fra candidato e riferimento.

Per i casi qualificabili con BR completa, il certificatore deve valutare la
strategia candidata riportata sulle combo fisiche, con azioni e card removal
del gioco originale. Una BR limitata ai bucket può sottostimare il guadagno
della deviazione e non soddisfa questo requisito. Non tagliare valori negativi
anomali o NaN per far passare un gate: diagnosticarne la causa numerica o logica.

Il certificato finito e la garanzia asintotica sono due verifiche distinte.
CFR richiede le ipotesi del relativo teorema; in particolare, rake dipendente
dall'esito può produrre un gioco non constant-sum. Un certificato di deviazione
unilaterale resta significativo, ma non dimostra una garanzia generale di
convergenza di CFR per quel gioco. Analogamente, quantizzazione e modifiche
all'averaging devono essere analizzate: un teorema in aritmetica ideale non
certifica automaticamente lo storage a 16 bit.

### 1.3 Contratto di risorse e misura

| Aspetto | Regola operativa |
|---|---|
| Esecuzione | CPU locale; massimo 8 thread di calcolo solver attivi complessivamente, inclusa BR |
| Qualità | Stesso target e stesso gioco; una modifica del target richiede una decisione esplicita prima del confronto |
| Tempo | Escludere startup dell'applicazione; includere il lavoro specifico del gioco, la certificazione finale e la materializzazione necessaria alla consultazione |
| Confronto algoritmi | Tempo al target, CPU-secondi, visite/update e RAM; le iterazioni di full CFR e MCCFR non sono unità equivalenti |
| RAM esterna | Parità con GTO+ richiesta sullo stesso perimetro; finché manca, dichiarare `NOT_EVALUATED_COMPARABILITY_UNRESOLVED` |

Registrare separatamente `build-to-ready`, `solve-to-consultable` e
`build-to-consultable`, mantenendo anche le componenti di training,
certificazione e finalizzazione. Non confrontare un cronometro avviato con
albero pronto con uno che include la costruzione senza dichiararlo.

La contabilità RAM deve includere stato logico e capacità allocate: albero,
regret, strategy sum, scale, mapping, ranghi, baseline, monitor, RNG, scratch,
pool, BR e checkpoint. Affiancare Peak RSS, private bytes, memoria impegnata e
backing su disco quando disponibili. Distinguere picco e stato finale; evitare
di sommare picchi di fasi che non coesistono. Per mmap/paging misurare anche
fault, I/O e tempo di ricaricamento. Spostare lo stato su disco può essere una
scelta progettuale, ma non è un risparmio gratuito.

I valori GTO+ `8/399/2.000 MB` sono un campo UI, non Peak RSS né un limite
desktop di 2 GiB. L'incertezza sulla comparabilità non impedisce di migliorare
il solver, ma impedisce di dichiarare raggiunta la parità RAM fra prodotti.

## 2. Comportamento dell'agente

### 2.1 Durante analisi e implementazione

| Deve fare | Non deve fare |
|---|---|
| Leggere codice e risultati dell'implementazione esistente prima di proporre una tecnica | Presentare come nuove la SIMD già presente, la cadenza adattiva BR o `HsDcfr30` |
| Formulare un'ipotesi misurabile e una modifica isolabile | Riscrivere il monolite del solver senza un oracle e senza un costo dominante misurato |
| Conservare un riferimento esaustivo e una modalità numerica adeguata ai test | Rimuovere l'oracle appena il candidato sembra più veloce |
| Usare proprietà strutturali generali e corpus indipendenti | Fare dispatch per ID fixture, percorso, board riconosciuto o dimensione scelta per un benchmark |
| Registrare approssimazioni, limiti teorici e assunzioni | Chiamare esatto un metodo campionato o promettere Nash nel gioco originale da un test sui bucket |

| Deve fare | Non deve fare |
|---|---|
| Preservare dirty files, fixture, `.tmp/`, `.reasonix/` e corpora dell'utente | Eseguire reset, clean o sostituzioni estese per ottenere un checkout comodo |
| Catturare uno snapshot riproducibile dei sorgenti realmente usati | Citare soltanto HEAD quando il binario contiene modifiche locali |
| Isolare gli esperimenti senza perdere modifiche necessarie | Creare un worktree da HEAD e presumere che contenga il lavoro non committato |
| Completare autonomamente modifiche reversibili e prove già autorizzate | Chiedere approvazione per ogni passaggio o interrompersi dopo un semplice piano |
| Rispettare le autorizzazioni degli strumenti e il perimetro locale | Fare push, distribuzioni, messaggi esterni o avviare altri agenti senza l'autorizzazione richiesta |

### 2.2 Durante verifiche e comunicazione

| Deve fare | Non deve fare |
|---|---|
| Conservare tutti i campioni, inclusi fallimenti, timeout e run contaminati | Selezionare il seed migliore, cancellare gli outlier scomodi o modificare soglie dopo i risultati |
| Separare PASS matematico, prestazionale, ambientale e di integrazione | Considerare exit code zero, CTest verde o dEV root come parità completa |
| Fermare il ramo che viola un invariante, conservandone il caso minimo | Continuare grandi benchmark dopo un errore di blocker, reach, payoff o averaging |
| Segnalare ciò che è misurato, dedotto o ancora da verificare | Convertire un guadagno di microbenchmark in uno speedup end-to-end senza misurarlo |
| Aggiornare l'utente con risultato concreto, incertezza e verifica successiva | Nascondere un blocco dietro aggiornamenti ripetitivi o dichiarare eliminato un collo solo identificato |

Il confronto fra strategie deve distinguere errore da non unicità
dell'equilibrio. Una differenza di frequenze rispetto a GTO+ non prova da sola
un bug; neppure una buona EV root prova la correttezza downstream. Controllare
raggiungibilità, posteriori, normalizzazione, convenzioni e BR prima di
classificare la discordanza. Non modificare frequenze per imitare un output.

## 3. Punto di partenza verificato e limiti

Questa tabella descrive le evidenze del 10 settembre, non una nuova esecuzione
associata alla scrittura della roadmap. Prima di riusarle confrontare gli hash
del manifest con il checkout e il binario correnti.

| Evidenza | Stato e conseguenza |
|---|---|
| Build Release e CTest disponibili | 43/43 PASS, 929,00 s; [JUnit](../out/postflop-research-20260910/ctest.xml). Non è la qualifica temporale completa |
| Baseline B0 del 5 settembre | 15 report riletti; target/root PASS, tempo TH/TST FAIL; diagnostici downstream AHK/TH da analizzare |
| A/B certificazione | Tre preflight CPU respinti; nessun processo solver A/B avviato; prestazioni `NOT_EVALUATED` |
| Oracle blocker | 1.530 confronti interi PASS su 12 combinazioni scenario/giocatore; ranghi sintetici, nessuna prova di Nash o timing nativo |
| Nuovi backend e integrazioni | POS e raffinamento adattivo non implementati da questa ricerca; GUI Qt e nuova build ASan non verificate nella sessione |

Binario della suite: `out/build/windows-release-current/apps/gto_cli/gto_cli.exe`.
SHA-256: `612F1183A00C687260F310DA97DE67CC11129636A86107E684EEB8E5F0C21947`.
Il manifest e i dati derivati sono in
[`out/postflop-research-20260910`](../out/postflop-research-20260910/).

Tre risultati orientano il lavoro:

1. TST conserva circa 1,473 miliardi di byte di stato. La ricodifica
   `decode → update → max/scale → encode/store` è un costo dominante nelle
   evidenze di profiling. Il 60% attribuito a encode/store riguarda il replay
   isolato della pipeline, non l'intero solve.
2. Nella B0 la certificazione pesa circa il 31–39% del tempo solver. TST
   richiede comunque 161,96 s mediani di traversata: anche una certificazione
   gratuita non basterebbe per il limite temporale 128,988889 s di quella
   campagna. Un intervento sulla BR è utile, ma non chiude da solo TST.
3. Il prototipo River ripercorre l'albero per ogni coppia di bucket. Le
   qualifiche dei bucket esistenti non dimostrano un vantaggio congiunto di
   qualità e prestazioni. Ridurre le righe di stato senza ridurre il lavoro
   pubblico o terminale non risolve questa struttura di costo.

I risultati storici negativi restano validi nel loro perimetro. Non ripetere
`made_hand_value_v1`, exact-blocker v2, distribution v3 o HS invariati sperando
in un esito diverso. Una nuova prova deve identificare quale causa del
fallimento viene rimossa e con quale verifica.

## 4. Sequenza e dipendenze

Gli identificatori `P0–P8` appartengono a questa roadmap; non rinominano le
fasi R/F dei documenti precedenti.

```text
P0 contratto e provenienza → P1 baseline e diagnosi
                               ├─ P2 certificazione exact
                               ├─ P3 POS con baseline, senza astrazione
                               └─ P4 terminali/traversata bucket → P5 raffinamento

P3 e P5 validati separatamente → eventuale composizione in P6
P6 architettura scelta → P7 qualifica indipendente → P8 prodotto
```

Le diramazioni rappresentano dipendenze logiche, non un'autorizzazione a
eseguire benchmark concorrenti o avviare subagenti. Dopo la diagnosi P2,
il primo nuovo backend algoritmico è P3. P4/P5 affrontano il problema RAM;
possono avanzare con prove piccole mentre misure lunghe sono impedite
dall'ambiente. Non serve terminare ogni ramo per scartarne uno.

### P0 — Congelare il confronto e recuperare lo stato reale

**Ingresso:** mandato corrente e accesso al checkout.

1. Leggere questa roadmap, il report, le istruzioni applicabili e lo stato Git.
   Inventariare le modifiche concorrenti; delimitare i file che si intende
   modificare prima di intervenire.
2. Associare eseguibile, sorgenti, configurazione CMake, compiler/flags,
   dipendenze, hardware e input a un manifest. Includere i sorgenti non
   tracciati realmente compilati. Conservare checksum e differenze locali.
3. Registrare gioco, target, metriche di tempo/RAM, corpus D/V/H, algoritmo,
   precisione, seed, limiti di lavoro e criteri di esclusione dei run.
4. Inventariare test e benchmark effettivamente disponibili. Verificare che
   il build tree non punti a un altro checkout; ricostruire se le dipendenze
   del binario sono cambiate, senza riusare etichette di test passati.
5. Aprire un registro di esecuzione e una scheda per il primo esperimento.
   Se l'incarico è solo documentale, fermarsi alla verifica documentale.

**Output:** manifest del confronto e inventario PASS/FAIL/NOT AVAILABLE.
**Gate:** ogni risultato riusato è attribuibile al codice e alla configurazione
che lo hanno prodotto. Un hash diverso non significa automaticamente bug;
significa che il vecchio risultato non certifica il nuovo binario.

**Non fare:** un build completo per abitudine quando nulla di compilato è
cambiato; forzare aggiornamenti di dipendenze non richiesti; trattare la build
di un altro task come eseguibile della propria campagna senza verificarla.

### P1 — Riprodurre i fallimenti e misurare il costo

**Ingresso:** manifest P0 completo e binario Release coerente.

1. Separare errore matematico, target non raggiunto, diagnostica esterna
   discordante, timeout, memoria e ambiente. Ridurre ogni errore matematico a
   un caso riproducibile; correggerlo prima di usare quel caso per lo speedup.
2. Eseguire in sequenza i benchmark, senza build o suite pesanti concorrenti.
   Usare il preflight corrente: cinque campioni CPU da un secondo, media
   non superiore al 15%, RAM libera almeno 4 GiB. Il minimo libero non è un
   tetto RAM del solver.
3. Misurare anche durante il solve carico esterno, CPU-secondi, working set
   e memoria privata quando disponibili. Definire prima del run come
   identificare contaminazione; non usare l'elevata CPU del solver stesso
   come motivo per scartare il suo campione.
4. Raccogliere per fase visite, update, tempo, bytes e overhead di
   sincronizzazione. Per la diagnosi usare profiling separato dal timing di
   qualifica. Verificare l'overhead della strumentazione e lo scaling 1–8
   thread solo nelle configurazioni necessarie alla decisione.
5. Riprodurre la baseline sul corpus di sviluppo e validazione, registrando
   cadenza BR e costo fino alla soluzione consultabile. Conservare singoli
   risultati, mediana e dispersione; non consumare l'holdout per scegliere
   algoritmi o parametri.

**Output:** tabella dei colli con costo assoluto, quota, limite massimo di
miglioramento e causa nel codice. Una percentuale senza denominatore non basta.
**Gate:** baseline utilizzabile e cause dei FAIL classificate; se l'ambiente
è indisponibile, avanzano soltanto attività che non richiedono timing pulito.

**Gestione del blocco:** dopo tre preflight respinti senza un cambiamento
osservabile dell'ambiente, conservare i report e passare a prove indipendenti.
Non fare polling infinito, alzare la soglia o interrompere applicazioni altrui.
Non richiedere un'automazione futura se l'utente non l'ha chiesta. Al rientro,
ripetere il preflight, non assumere che il tempo trascorso equivalga a via libera.

### P2 — Confrontare i percorsi di certificazione già presenti

**Ingresso:** test matematici dei percorsi exact validi; ambiente P1 utilizzabile
per qualsiasi conclusione prestazionale.

1. Rileggere `certify_typed` nel
   [solver](../libs/postflop/src/postflop_solver.cpp), il ledger e i test
   differenziali. Verificare compatibilità di layout, scratch, cache, rake e
   pool; il mutex del ledger non rende automaticamente thread-safe le cache.
2. Usare [il runner A/B](../tools/compare_postflop_certification.py) per
   confrontare il percorso corrente con l'esistente
   `GTOSD_DIAGNOSTIC_LEGACY_CERTIFICATION=1`, in processi distinti e con
   ambiente child controllato. Non impostare flag persistenti nel sistema.
3. Verificare tutta la curva numerica, iterazioni, stato e fingerprint. Il
   confronto JSON numerico del runner non è una prova di identità binaria di
   tutti i buffer: aggiungere una verifica mirata se si dichiara byte-identità.
4. Misurare vantaggio, RAM extra e totale dei thread attivi. Conservare seriali
   i percorsi che dipendono da cache mutabili non concorrenti, inclusa la
   paginazione interessata, finché non vengono riprogettati e verificati.
5. Se il vantaggio è reale, proporre una selezione basata su capacità e costo
   del layout, quindi verificarla sul corpus. Una scelta per nome benchmark
   non è un'implementazione accettabile.

**Output:** A/B riproducibile e decisione mantenere/scartare/riprogettare.
**Gate:** equivalenza richiesta dalla trasformazione, limite di thread
rispettato e beneficio senza costo RAM nascosto. Un campione per percorso è
diagnostico; la qualifica finale richiede P7.

Il runner corrente controlla il preflight, non basta da solo a certificare
l'assenza di interferenze per tutta la durata del run. Estendere la raccolta
ambientale prima della qualifica. La cadenza adattiva della certificazione
esiste già: cambiarla è un esperimento distinto. Saltare una misurazione non
dimostra che il target non fosse già stato raggiunto; la convergenza osservata
non è necessariamente monotona.

**Non fare:** presentare P2 come soluzione completa di TST; omettere dal
cronometro la BR finale; dedurre la sicurezza concorrente dal solo numero di
thread; promuovere automaticamente un flag diagnostico.

### P3 — Prototipo Public Outcome Sampling con baseline

**Ingresso:** diagnosi P1, oracle esaustivo disponibile e specifica del nuovo
algoritmo. P2 non deve diventare una lunga serie di micro-ottimizzazioni che
rinvia indefinitamente questo ramo.

1. Specificare formula degli update, azioni pubbliche campionate, probabilità
   di inclusione, supporto, pesi correttivi, averaging, baseline e ordine del
   suo aggiornamento. Distinguere reach corrente, propria e counterfactual.
   Fissare le ipotesi teoriche e dichiarare quelle non soddisfatte.
2. Implementare un riferimento sui giochi piccoli: Kuhn, Leduc e postflop
   enumerabile. Quando possibile enumerare i campioni per verificare l'attesa
   dell'update; altrimenti usare test statistici con seed e precisione
   dichiarati. Non richiedere che una singola traversata campionata coincida
   con una traversata completa.
3. Aggiungere un backend sperimentale separato, condividendo carte, regole,
   blocker, azioni e kernel terminali. Mantenere inizialmente tutte le combo
   private; verificare molteplicità delle orbite e condizionamento del chance
   rispetto alle carte bloccate.
4. Prima dell'albero TST, calcolare i byte di baseline e scratch. Una baseline
   float32 per le 366.890.152 action entry storiche aggiungerebbe
   1.467.560.608 byte: il sampling, da solo, non riduce quello stato. Valutare
   rappresentazioni meno costose conservando la costruzione non distorta
   dell'estimatore e misurando la varianza.
5. Confrontare più seed a pari qualità originale, includendo certificazione
   finale, timeout e fallimenti. Misurare tempo, CPU-secondi e distribuzione
   del lavoro al target; scegliere se proseguire prima di aggiungere bucketing.

**Output:** backend versionato, specifica, test dell'estimatore e confronto
end-to-end senza card abstraction. Seed, RNG e baseline devono avere uno
stato serializzabile sufficiente alla ripresa riproducibile.
**Gate:** nessun errore matematico noto, qualità finale certificata nel gioco
originale e vantaggio misurabile rispetto al costo/varianza introdotti.

**Non fare:** campionare con probabilità zero azioni che devono essere coperte;
tagliare importance weights senza quantificare il bias; aggiornare una baseline
con il campione corrente in un ordine diverso da quello derivato; importare
sconti e reset DCFR presumendo che il teorema POS rimanga invariato.

### P4 — Correggere il costo strutturale del bucket solver River

**Ingresso:** oracle pairwise disponibile, masse e reach formalizzati.

1. Partire da `solve_fixed_river_bucket_game` e `traverse_river_bucket_pair`
   in [postflop_subgame.cpp](../libs/postflop_subgame/src/postflop_subgame.cpp).
   Misurare separatamente costruzione mapping, numero di traversate e terminali.
2. Portare in C++ l'identità verificata dal
   [probe dei blocker](../tools/probe_bucket_blocker_factorization.py), usando
   ranghi del ruleset reale, pesi e range del gioco. I test interi con ranghi
   sintetici sono il punto di partenza, non il certificato del nuovo kernel.
3. Implementare una traversata dell'albero pubblico per aggiornamento previsto
   dall'algoritmo, con vettori decisionali bucket e kernel di massa per
   rango/carta. Evitare sia la ripetizione dell'albero per coppia sia una
   matrice persistente densa delle coppie. Esplicitare dove resta lavoro sulle
   combo fisiche e includerlo nel costo.
4. Verificare compatibilità, correzione della combo identica, tie, fold,
   showdown, rake, pesi zero/frazionari, range asimmetrici e masse minime. Una
   formula che assume un rango unico per bucket deve imporre tale condizione
   o essere generalizzata e testata prima di accettare bucket multirango.
5. Confrontare con il pairwise su alberi piccoli e con partizione identità:
   valori terminali, reach, regret e averaging. Poi misurare scaling rispetto
   a nodi, combo e bucket, includendo mapping e certificazione fisica.

**Output:** kernel nativo differenziale e modello del costo verificato.
**Gate:** equivalenza nel caso identità entro tolleranze predefinite e
giustificate, vantaggio strutturale osservato, nessun costo quadratico nascosto
che annulli il guadagno end-to-end sui casi di interesse.

**Non fare:** spostare soltanto il ciclo sulle coppie dentro le foglie e
dichiarare eliminata la complessità; dedurre una strategia bucket corretta da
sole masse terminali aggregate; confondere un nuovo ordine di somma floating
point con una trasformazione garantita byte-identica.

### P5 — Raffinare l'astrazione mantenendo una verifica originale

**Ingresso:** P4 verificato e protocollo indipendente da POS. La convenienza di
una partizione va misurata sul solver corretto, non sul prototipo pairwise lento.

1. Specificare mapping per giocatore e stato pubblico, feature Short Deck,
   metrica, clustering, pesi e storia informativa. Distinguere compressione
   senza perdita da astrazione: stesso rango non implica stessa strategia.
2. Riprodurre un metodo di raffinamento con ipotesi documentate, usando
   CFR+IRA come riferimento, su giochi ridotti. Implementare il monitor e le
   regole di split effettive del metodo, compreso il loro costo; una soglia
   inventata di similarità non eredita la sua garanzia.
3. Definire trasferimento di regret, media e monitor dopo ogni split. Verificare
   conservazione delle masse e assenza di duplicazione della storia. Una
   copia dello stato del padre nei figli non è corretta per definizione:
   derivarne la semantica o usare un riavvio esplicito e contabilizzato.
4. Misurare BR fisica, qualità delle combo poco pesate, errore dell'astrazione,
   memoria residente, picco di raffinamento e tempo totale. Usare partizione
   identità e soluzioni più fini come riferimenti; conservare card removal.
5. Procedere River → Turn → Flop soltanto dopo i gate del livello precedente.
   Congelare feature e parametri su D/V; l'holdout non serve a scegliere split
   o numero di bucket.

**Output:** partizione versionata e raffinamento riproducibile, con test di
resume e qualità originale. Mantenere perfect recall oppure dimostrare
l'applicabilità del metodo scelto quando viene perso.
**Gate:** riduzione RAM reale, comprensiva di mapping/monitor, e qualità
originale verificata. Se il raffinamento tende alla partizione identità, è un
risultato di fattibilità, non un motivo per bloccare gli split necessari.

**Non fare:** generalizzare la riduzione 465→45 di un full range simmetrico
ai range realistici che hanno prodotto singleton; applicare bucket Hold'em
52 carte senza verifica Short Deck; reclamare i risparmi percentuali di un
paper come prestazioni misurate di questo motore.

### P6 — Scegliere l'architettura e ottimizzarla

**Ingresso:** almeno un candidato isolato con evidenze utilizzabili; i rami
falliti restano registrati. Non è necessario combinare tutte le tecniche.

1. Costruire una tabella a pari qualità con tempo operativo, CPU-secondi,
   picco RAM, stato residente e costi di certificazione. Scartare i candidati
   dominati e spiegare gli scambi dei rimanenti. Un progresso soltanto CPU
   o soltanto RAM resta esplicitamente parziale.
2. Combinare POS e raffinamento soltanto dopo la verifica separata: derivare
   update, averaging, monitor e probabilità nella composizione. In assenza di
   una garanzia applicabile, mantenere l'etichetta sperimentale e non affermare
   una convergenza teorica ottenuta sommando citazioni.
3. Considerare pruning o decomposizione come rami opzionali con una nuova
   ipotesi: finestre di regret certificate e reintroduzione per RBP; boundary
   CFV, strategia coerente e BR globale per CFR-D/safe subgame. Misurare
   rigenerazione e I/O. Convergenza di subgame indipendenti non basta.
4. Profilare di nuovo l'architettura scelta. Ottimizzare allora layout, codec,
   SIMD, scratch, allocazioni e parallelismo dei costi rimasti. Un nuovo codec
   può cambiare la traiettoria, ma deve dichiarare errore numerico e superare
   i confronti di precisione e qualità richiesti.
5. Fissare candidato, parametri, politica di certificazione e schema di
   persistenza. Verificare le combinazioni effettivamente supportate; non
   moltiplicare flag senza un caso d'uso e una copertura verificabile.

**Output:** decisione architetturale con alternativa scelta, scartate e costi.
**Gate:** beneficio riproducibile sui colli identificati; nessun errore teorico
o numerico occultato. Le cause di errore da quantizzazione, astrazione,
sampling e subgame sono riportate separatamente. I loro budget non si sommano
come un bound senza una derivazione; la BR originale misura la qualità
congiunta della strategia prodotta.

**Non fare:** inseguire ogni tecnica elencata nei paper; invocare un teorema
RBP incompatibile con sconti/reset; qualificare una rete o un valore di foglia
stimato come certificatore exact; trattare paging o cache warm come lavoro
fuori dal cronometro senza registrare costruzione e riuso.

### P7 — Qualifica indipendente del candidato congelato

**Ingresso:** candidato e criteri congelati su D/V, nessun tuning ancora aperto.

1. Congelare manifest, corpus, seed, budget, tolleranze numeriche e statistiche.
   Eseguire almeno cinque processi indipendenti per confronto deterministico,
   alternando l'ordine baseline/candidato. Riportare i singoli campioni;
   cinque osservazioni non stimano con precisione una coda p95.
2. Per sampling definire prima un insieme di seed e una numerosità coerente
   con varianza e precisione richieste. Cinque seed non sono automaticamente
   sufficienti. Includere tentativi che non raggiungono il target e il lavoro
   già speso; non confrontare solo i solve riusciti.
3. Aprire H solo adesso, dopo aver verificato che non sia già stato utilizzato.
   Se una sua osservazione guida una modifica, H è consumato: il successivo
   test indipendente richiede un nuovo insieme non usato per scegliere il
   candidato. Rinominare un corpus non lo rende nuovo.
4. Richiedere certificazione nel gioco originale sui casi qualificati,
   regression test, invarianti, single/multithread e ripresa. Per casi troppo
   grandi una stima campionata resta una classe separata con seed, campioni,
   varianza e intervallo; il valore di una risposta trovata può essere solo
   un limite inferiore alla BR e non un certificato superiore di exploitability.
5. Verificare ogni benchmark obbligatorio e l'applicabilità generale. Pubblicare
   tempo operativo, CPU-secondi, mediana/dispersione, RAM per fase, qualità e
   fallimenti. Non compensare un caso peggiorato con la media degli altri.

**Output:** dossier riproducibile con PASS/FAIL per ogni gate, non un solo
booleano complessivo. La suite va inventariata nuovamente: 43 è il conteggio
del 10 settembre, non una costante da imporre ai futuri CTest.

**Gate di miglioramento interno:** qualità rispettata, beneficio che supera
il rumore misurato e nessuna regressione non accettata sul corpus obbligatorio.
Le soglie pratiche vanno fissate prima degli esperimenti in base all'obiettivo
e al rumore, non ricavate dai risultati per promuoverli.

**Gate dell'obiettivo completo:** CPU/tempo e RAM soddisfano congiuntamente il
contratto concordato; la parità GTO+ usa perimetri realmente confrontabili.
Se la comparabilità RAM manca, riportare miglioramento interno e obiettivo
esterno non chiuso. I limiti storici di iterazioni 80/80/160 descrivono DCFR:
non sono tetti per un algoritmo campionato diverso.

### P8 — Integrare e rendere riproducibile il risultato

**Ingresso:** candidato qualificato per un perimetro esplicito; nessun default
cambia soltanto perché un prototipo ha superato uno smoke test.

1. Integrare il backend condiviso in CLI/API e, quando disponibile, GUI.
   Identificare algoritmo, astrazione, precisione e certificazione nel progetto
   e nella soluzione; rendere accessibile la stessa modalità usata nei test.
2. Versionare checkpoint e soluzione: mapping, regret, averaging, baseline,
   RNG, monitor, split, boundary e iterazione devono permettere la ripresa
   secondo il contratto. Non reinterpretare un vecchio checkpoint come
   appartenente a un algoritmo diverso.
3. Verificare salvataggio atomico, checksum, interruzione/ripresa e input
   corrotti, compresi limiti di allocazione. Eseguire test di memoria e
   sanitizer appropriati alle modifiche native; dichiarare strumenti o GUI
   non disponibili senza sostituirli con un PASS di un altro test.
4. Aggiornare specifiche di algoritmo, precisione, memoria, CLI, formato e
   limitazioni. Registrare una decisione esplicita sui default sostenuta dai
   gate; conservare accessibile il riferimento esaustivo per regressioni.
5. Verificare che i risultati finali corrispondano al binario consegnato.
   Consegnare diff, manifest, test, benchmark e limiti. Commit, push o
   distribuzione seguono l'autorizzazione applicabile, non sono impliciti nel
   superamento di una qualifica locale.

**Output:** implementazione utilizzabile e documentata, con compatibilità
esplicita e risultati attribuibili. **Gate:** nessun lavoro obbligatorio del
perimetro dichiarato resta nascosto dietro l'etichetta completato.

## 5. Protocolli trasversali

### 5.1 Scheda obbligatoria prima di ogni esperimento significativo

Compilare una scheda nel registro di esecuzione, con questi cinque blocchi:

1. **Ipotesi e causa:** costo osservato, punto nel codice, cambiamento previsto,
   motivo per cui un tentativo già respinto non risponde alla stessa domanda.
2. **Input congelati:** hash, configurazione, corpus D/V, algoritmo, precisione,
   seed, thread, binario e macchina. Specificare cosa cambia e cosa rimane
   uguale tra controllo e candidato.
3. **Invarianti e misura:** oracle, formule, tolleranze motivate, qualità,
   tempo, CPU-secondi, RAM completa e overhead della strumentazione.
4. **Budget e arresto:** limiti di tempo/lavoro/RAM motivati, errore che ferma
   subito il ramo, criteri ambientali e quantità di prove. Una stima iniziale
   in minuti/ore serve a gestire il lavoro, non promette la convergenza.
5. **Risultati e decisione:** percorsi dei dati grezzi, risultati singoli,
   sintesi, regressioni, esito, limite rimasto e unica attività successiva.

Non aprire un ramo costoso senza una previsione almeno dimensionale dei byte
e del lavoro. Non inventare una durata complessiva della ricerca: aggiornare
le stime dopo i primi campioni reali e distinguerle dalle misure.

### 5.2 Classificazione degli esiti

| Stato | Significato |
|---|---|
| `NOT_STARTED` / `IN_PROGRESS` | Attività non eseguita o con prove ancora incomplete |
| `PASS` / `FAIL_CORRECTNESS` | Gate specificato superato, oppure violazione matematica/di integrità riproducibile |
| `REJECTED_PERFORMANCE` / `INCONCLUSIVE` | Candidato fuori soglia con dati utilizzabili, oppure dati insufficienti/rumorosi |
| `NOT_EVALUATED_ENVIRONMENT` / `NOT_AVAILABLE` | Misura impedita dall'ambiente, oppure componente/strumento assente; nessun PASS inferito |
| `QUALIFIED_INTERNAL` / `COMPLETE` | Miglioramento interno qualificato, oppure tutti i requisiti dell'obiettivo dichiarato soddisfatti |

Specificare sempre a quale gate e caso si riferisce lo stato. Aggiungere il
campo RAM GTO+ `NOT_EVALUATED_COMPARABILITY_UNRESOLVED` quando applicabile,
anche se il candidato è `QUALIFIED_INTERNAL`. Non riclassificare un timeout
come dato mancante: è un fallimento rispetto al budget dichiarato, salvo una
causa ambientale documentata con i criteri già fissati.

### 5.3 Quando fermarsi e quando continuare

| Condizione osservata | Azione dell'agente |
|---|---|
| Invariante matematico violato | Fermare la qualifica del ramo, minimizzare il caso, correggere e aggiungere regressione |
| Ambiente inadatto al timing | Conservare il tentativo e avanzare con analisi/test piccoli indipendenti; nessun timing usato per la promozione |
| Beneficio assente dopo la prova predefinita | Scartare il candidato o formulare una nuova ipotesi misurabile; niente ripetizione identica per ottenere un campione favorevole |
| Requisito materialmente ambiguo | Esporre interpretazioni e conseguenze concrete; chiedere solo la decisione mancante e continuare il lavoro indipendente |
| Memoria o tempo della sessione insufficienti | Salvare un handoff completo; non dichiarare raggiunto l'obiettivo per esaurimento del budget |

Un blocco di autorizzazione degli strumenti va riportato con azione rifiutata
e motivo, senza aggirarlo. Prima di chiedere un intervento, completare le
verifiche e le modifiche sicure che rendono la decisione concreta e revisionabile.

## 6. Mappa dei file e riuso delle fonti

| Area | Punto di partenza e modifica prevista |
|---|---|
| Training/certificazione exact | [postflop_solver.cpp](../libs/postflop/src/postflop_solver.cpp), [test reference](../tests/gto_plus_reference_tests.cpp), [runner A/B](../tools/compare_postflop_certification.py); modifiche mirate dopo P1/P2 |
| Bucket River | [postflop_subgame.cpp](../libs/postflop_subgame/src/postflop_subgame.cpp), [test subgame](../tests/postflop_subgame_tests.cpp), [probe blocker](../tools/probe_bucket_blocker_factorization.py); kernel e regressioni P4 |
| Partizioni | [card_abstraction.hpp](../include/gtosd/solver/card_abstraction.hpp), [implementazione](../libs/solver/src/card_abstraction.cpp), [test](../tests/card_abstraction_tests.cpp); mapping/raffinamento versionati P5 |
| Nuovo backend | Modulo sperimentale accanto a `libs/postflop`, API circoscritta, test e benchmark dedicati; nomi definitivi scelti dopo l'inventario, nessun file fittizio presentato come esistente |
| Contratti e integrazione | [Algoritmi](specifications/SOLVER_ALGORITHMS.md), [precisione](specifications/NUMERICAL_PRECISION.md), [performance](specifications/PERFORMANCE.md), [formato](specifications/SOLUTION_FORMAT.md), [CLI](specifications/CLI.md) |

Le fonti e i limiti delle implementazioni consultate sono raccolti nel
[report](POSTFLOP_RESEARCH_DECISION_2026-09-10.md). Per P3 leggere il metodo di
[Public Outcome Sampling con baseline](https://proceedings.mlr.press/v119/davis20a.html)
e la costruzione di
[VR-MCCFR](https://arxiv.org/abs/1809.03057); per P5 la formulazione di
[CFR+IRA](https://arxiv.org/html/1803.05392). Le citazioni orientano la
derivazione: non sostituiscono la verifica delle ipotesi nel nuovo backend.

Usare [OpenSpiel](https://github.com/google-deepmind/open_spiel) come riferimento
per giochi e algoritmi ridotti. Confrontare l'architettura di
[postflop-solver](https://github.com/b-inary/postflop-solver) e
[TexasSolver](https://github.com/bupticybee/TexasSolver) senza assumere che i
loro benchmark si trasferiscano a questo hardware, ruleset e corpus. Prima di
riusare codice verificare licenza, versione e compatibilità; citare lo SHA
consultato. Questa ricerca non ha eseguito quei solver esterni né ne ha copiato
il codice. Per nuovi claim o codice remoto, verificare nuovamente la fonte.

## 7. Handoff e prima attività eseguibile

Il registro di handoff deve consentire a un altro agente di riprendere senza
ricostruire la conversazione. Deve contenere:

1. Fase corrente, gate completati e stato delle evidenze: storiche, correnti,
   invalidate da modifiche o ancora non misurate.
2. Snapshot dei sorgenti, hash del binario, build/test usati e file modificati
   dall'agente; distinguere le modifiche preesistenti o concorrenti.
3. Comandi riproducibili, directory nuove per i risultati e percorsi dei log,
   compresi fallimenti e tentativi interrotti.
4. Decisioni adottate, alternative respinte e requisito ancora irrisolto;
   indicare se H è intatto o è stato consumato.
5. Una sola prossima attività con prerequisiti e criterio di completamento.

**Prima attività al momento della redazione:** completare P0 sui file correnti
e verificare i prerequisiti matematici e ambientali di P1. Se il binario è
coerente e il preflight passa, eseguire la diagnosi A/B di P2. I tre tentativi
precedenti non hanno prodotto alcun campione solver.
In caso di CPU ancora occupata, il lavoro indipendente prioritario è la
specifica matematica e il riferimento ridotto di P3.

Comando della diagnosi, dalla radice del repository, dopo aver individuato un
interprete Python disponibile. Il token `python` indica quell'interprete;
se non è nel PATH, usare il suo percorso assoluto verificato. Scegliere una
directory mai usata: il runner rifiuta di sovrascriverne una esistente.

```powershell
python tools/compare_postflop_certification.py --build-dir out/build/windows-release-current --output out/postflop-certification-next-unique --repetitions 1
```

Il comando è diagnostico e non chiude P7. Non avviarlo durante una build o
una suite pesante. Non cambiare le fixture, il target o il preflight per farlo
terminare con successo.

La roadmap è completata quando il miglioramento promesso è implementato,
qualificato e utilizzabile nel perimetro dichiarato. Se un ramo è respinto,
il suo risultato è completo come esperimento, mentre l'obiettivo complessivo
resta aperto finché non viene raggiunto. Un limite documentato che richiede
una decisione dell'utente può bloccare l'esecuzione, ma non soddisfa l'obiettivo.
