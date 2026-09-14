# Postflop: colli di bottiglia e scelta della prossima architettura

Data dell'analisi: 2026-09-10. Checkout iniziale:
`ffb208a0dad018dc537288c42239322ff7dfde6d`, con modifiche locali estese.
Questo documento distingue misure esistenti, verifiche di questa sessione e
proposte ancora da implementare. Non certifica un'accelerazione del prodotto.

L'esecuzione successiva segue la
[roadmap dell'agente](POSTFLOP_AGENT_EXECUTION_ROADMAP_2026-09-10.md): fasi,
invarianti, criteri di accettazione, comportamenti richiesti e pratiche da evitare.

## Decisione

La priorità CPU è ridurre gli aggiornamenti e le traversate effettivamente
eseguite. La priorità RAM è ridurre gli infoset residenti, includendo mapping,
baseline del sampler, checkpoint e scratch. Il solo raggruppamento delle mani
non soddisfa entrambi gli obiettivi.

Il candidato algoritmico da verificare per primo è un backend sperimentale
Public Outcome Sampling con correzione tramite baseline, inizialmente senza
card abstraction. Mantiene le mani private vettoriali e il card removal fisico.
Per la RAM si valuta separatamente il raffinamento adattivo dell'astrazione,
ispirato a CFR+IRA. La combinazione dei due resta una nuova ipotesi di ricerca:
le garanzie dei singoli paper non dimostrano quelle della loro composizione.

Prima di investire nel bucket solver, va corretto il suo modello di traversata:
un albero pubblico, decisioni su vettori di bucket e terminali valutati con i
kernel fisici per ranghi/blocker. Ripetere l'albero per ogni coppia di bucket
mantiene un costo quadratico che il solver exact evita già in parte.

## Vincoli riaperti dalla richiesta dell'utente

| Aspetto | Contratto per questa ricerca |
|---|---|
| Algoritmo | Sono ammessi algoritmi diversi da ProductionDcfr; non è richiesta la stessa traiettoria numerica |
| Astrazione e sampling | Ammessi esplicitamente, con identificazione del metodo e verifica nel gioco originale |
| Qualità iniziale | Conservare il target corrente: massimo guadagno unilaterale di BR inferiore all'1% del pot; NashConv separata |
| Configurazione | Stesse regole, range pesati, board, rake, sizings e albero delle azioni per il confronto |
| Risorse | CPU locale, massimo 8 thread di solving; misurare RAM, tempo e lavoro separatamente |
| RAM GTO+ | `NOT_EVALUATED_COMPARABILITY_UNRESOLVED`; i 2.000 MB del display non sono un limite desktop |
| Prodotto | Un candidato sperimentale non cambia automaticamente default, checkpoint o risultati già qualificati |

I vecchi rifiuti motivati soltanto da `same ProductionDcfr trajectory`,
`no sampling`, `no bucketing` o dal falso cap di 2 GB non chiudono questa
ricerca. Restano valide le misure di errori, costi e mancata convergenza. Non
serve ripetere un candidato invariato già respinto per questi ultimi motivi.

Per una strategia media completa, nel gioco originale definiamo
`gain_i = BR_i(sigma_-i) - u_i(sigma)` e
`NashConv = gain_0 + gain_1`. Il target corrente controlla
`max(gain_0, gain_1) / pot < 0.01`: non equivale a `NashConv / pot < 0.01`.
Con due giocatori implica invece `NashConv / pot < 0.02`.
Un certificato finito di BR vale anche come verifica di epsilon-Nash quando
il gioco non è zero-sum. Le garanzie asintotiche citate per CFR richiedono
invece le rispettive ipotesi: in particolare, un rake dipendente dall'esito
può rendere il gioco non constant-sum. Non trasferire automaticamente a quel
caso i teoremi per due giocatori zero-sum.

Per gioco originale si intende il gioco con le regole e il catalogo di azioni
configurati prima della card abstraction. Una BR su quel catalogo non
certifica tutti gli importi possibili del poker no-limit. Ridurre o rimuovere
sizings richiede una verifica distinta contro il catalogo originale.

## Cosa fallisce nelle evidenze disponibili

La baseline controllata B0 del 5 settembre contiene cinque processi per caso,
report v4, preflight CPU/RAM e lo stesso profilo ProductionDcfr.
Questi sono risultati storici riletti dai file locali, non nuove misure del
10 settembre. Fonte: [registro di esecuzione](PRODUCTION_DCFR_PRODUCT_OPTIMIZATION_EXECUTION_2026-09-05.md),
sezione R2, e `out/production-dcfr-product-b0-controlled-20260905`.

| Caso | Iterazioni | dEV | Solver mediana / p95, secondi | Esito |
|---|---:|---:|---:|---|
| AHK | 80 | 0,951423% | 0,788373 / 0,817360 | target, root e tempo PASS; diagnostici downstream EV/action FAIL |
| TH | 80 | 0,807956% | 22,682420 / 22,975138 | target e root PASS; tempo e diagnostici downstream FAIL |
| TST | 160 | 0,904505% | 245,082130 / 304,203644 | target, root e diagnostici PASS; tempo FAIL |

Correttezza root, layout, outcome esatti, target e iterazioni passano 15/15.
Un test di regressione verde non dimostra che il gate prestazionale passi;
un processo con exit code zero non sostituisce la lettura dei gate nel JSON.
Una frequenza diversa da GTO+ può derivare da strategie quasi ottimali diverse
e da range posteriori differenti. Va investigata con BR, reach e convenzioni
EV; non va corretta forzando la strategia a imitare la reference.

### Stato e traffico di memoria

La rilettura strutturata dei 15 JSON B0, eseguita in questa sessione, aggiunge
un secondo costo che il profilo del prototipo A0 non rappresenta:

| Caso | Traversata mediana, s | Certificazione mediana, s | Mediana della quota certificazione sul solve |
|---|---:|---:|---:|
| AHK | 0,47 | 0,30 | 38,88% |
| TH | 15,38 | 6,83 | 30,51% |
| TST | 161,96 | 82,72 | 33,75% |

I valori per processo e gli SHA-256 dei report originali sono salvati in
`out/postflop-research-20260910/historical_costs.json`. Le quote sono mediane
dei rapporti per processo, non rapporti tra due mediane indipendenti.
Nel TST B0 le osservazioni sono a 20/40/80/120/160 iterazioni: il codice
ha già una cadenza adattiva. Non si può contabilizzare come nuova
ottimizzazione il semplice passaggio da 20 a 40 iterazioni fra due controlli.

Nel percorso DAG corrente, `certify_typed` usa `evaluate_pair` per profilo e
BR, ma passa zero worker a entrambe le valutazioni e le esegue in sequenza.
La motivazione nel codice è contenere gli arena aggiuntivi mentre il pool del
training è residente. Il confronto diagnostico già disponibile tramite
`GTOSD_DIAGNOSTIC_LEGACY_CERTIFICATION` consente di misurare il compromesso
con le valutazioni parallele. Non è una modifica della traiettoria DCFR.
Prima di adottarlo vanno controllati scratch, thread effettivamente attivi,
identità dei valori e compatibilità del memory ledger con accessi concorrenti.

Anche una certificazione gratuita lascerebbe, nella mediana B0 TST,
161,96 secondi di traversata contro un limite totale di 128,99 secondi.
Il suo miglioramento è quindi utile ma non sufficiente. Un nuovo backend
campionato deve includere nel confronto il costo della certificazione finale.

Il layout TST documentato occupa `1.472.605.376 B` di stato persistente. Il
profilo del 31 agosto individua come costo dominante
`decode -> update -> max/scale -> encode/store`, seguito dallo showdown.
Nel replay di 288 nodi reali, encode/store pesa circa il 60% della pipeline
isolata. Il 60% non è una percentuale del tempo totale del solve.
Fonte: [attribuzione storica dei costi](archive/legacy-memory-gate/TST_STRICT_2GB_BOTTLENECK_ATTRIBUTION_AND_FEASIBILITY_LOOP_2026-08-31.md).
Il cap RAM contenuto nello stesso rapporto è stato ritirato; la tassonomia
dei costi resta utile, ma le percentuali vanno rimisurate sulla revisione corrente.

Il codice corrente conferma la dipendenza: in
`libs/postflop/src/postflop_solver.cpp` il percorso scaled aggiorna regret e
media, cerca i massimi e ricodifica gli array. SIMD è già presente. Una scala
globale al nodo richiede conoscere il massimo prima di codificare tutti i
valori: eliminare quel passaggio mantenendo gli stessi codici non è una
semplice ottimizzazione del compilatore. Cambiare rappresentazione è ammesso,
ma richiede misurare l'errore numerico e la convergenza.

Qui `exact` indica enumerazione delle chance e best response sul gioco
modellato, non aritmetica a precisione infinita. Il codec a 16 bit introduce
errore numerico; il teorema di un algoritmo in aritmetica esatta non dimostra
da solo convergenza asintotica della sua implementazione quantizzata.
Il certificato numerico finale e il confronto con precisioni maggiori restano
necessari. Una baseline POS float32 per ogni action-entry TST richiederebbe
altri `1.467.560.608 B` prima di allocator e metadati: il campionamento da solo
non è una soluzione RAM.

### Perché i bucket correnti non risolvono il problema

| Esperimento locale | Risultato verificato nella documentazione e nei report | Implicazione |
|---|---|---|
| Mapping coarse postflop A0 | mapping 46,54–56,53% del wall D/V; il training conserva le visite fisiche | Meno stato non implica meno lavoro |
| River made-hand v1 | qualifica 0/7; NashConv fisica 0,5506–5,3387% nei confronti successivi | La sola forza della mano non conserva i blocker strategici |
| River exact-blocker v2 | 12/12 casi con una classe per combo, riduzione 1,00x | La firma lossless può annullare ogni compressione sui range realistici |
| Partizione equa pesata | full range 465→45 classi; 12 range realistici con singleton | Il vantaggio su range simmetrici non dimostra applicabilità generale |
| Showdown distribution v3 | qualifica 0/12; 7,6–123,9 volte più lento; NashConv fisica fino a 2,6527% | La distribuzione coarse corrente non è un candidato da promuovere |

Le qualifiche River usano anche soglie più strette del solo target GTO+;
`0/12` non significa che tutti i casi falliscano necessariamente dEV <1%.
Significa che nessuno supera l'insieme dei gate dichiarati.
Fonti: [stato implementazione](IMPLEMENTATION_STATUS.md),
`benchmarks/results/river_showdown_distribution_qualification_2026-09-07.json`
e [registro di esecuzione](PRODUCTION_DCFR_PRODUCT_OPTIMIZATION_EXECUTION_2026-09-05.md).

Un collo strutturale distinto è visibile in
`libs/postflop_subgame/src/postflop_subgame.cpp`:
`solve_fixed_river_bucket_game` itera sulle coppie e richiama
`traverse_river_bucket_pair` dall'inizio dell'albero. Il costo è proporzionale
a `coppie compatibili × nodi pubblici`, per passata. Spostare soltanto il loop
sulle coppie ai terminali lascia ancora un costo quadratico ai terminali.
La proposta utile è riusare i kernel fisici di rank-prefix e card removal,
espandendo la policy bucket alle combo soltanto al confine del terminale.
Questo modello deve essere misurato prima di attribuirgli uno speedup.

### Oracle nuovo: fattorizzazione delle masse con blocker

È stato implementato ed eseguito
`tools/probe_bucket_blocker_factorization.py`, un riferimento Python separato
dagli hot path. Usa 36 carte, cinque carte pubbliche e 465 combo legali,
con ranghi sintetici condivisi fra i giocatori: isola il calcolo dei blocker
dalla correttezza dell'hand evaluator. La partizione conserva un rango per
bucket e può differire fra i giocatori.

Per due gruppi di combo, la massa compatibile soddisfa l'identità:

`massa = prodotto delle masse totali - somma sui 36 blocker dei prodotti delle masse per carta + correzione per combo identiche`.

Una coppia che condivide una carta viene sottratta una volta. Una coppia di
combo identiche viene sottratta due volte: la correzione aggiunge un prodotto
per far tornare a zero la sua massa, senza renderla legale. Applicando somme
prefisse per rango si separano loss/tie/win. È possibile applicare il calcolo
ai vettori senza materializzare tutte le coppie. Il prototipo controlla
separatamente questi tre risultati contro la doppia enumerazione fisica.

Esito corrente: **12/12 combinazioni scenario/giocatore PASS, 1.530 confronti scalari
interi esatti, errore massimo zero**. Copertura: range uniformi, pesi
asimmetrici, supporti sparsi, pesi zero, unica combo condivisa impossibile e
supporti disgiunti, con reach avversaria pesata per bucket. I casi senza massa
valida sono controlli algebrici di confine, non configurazioni risolvibili.
Artefatto: `out/postflop-research-20260910/blocker_factorization.json`.

Questa è una prova indipendente del calcolo delle masse, non della convergenza
del bucketing o delle prestazioni native. L'identità non impone un nuovo
hand ranking e non giustifica merge di infoset con storia incompatibile.
Il successivo kernel C++ deve confrontarsi anche con i ranghi reali, il rake,
le utility del motore e il costo end-to-end. Non è stato integrato nel solver.

## Letteratura: applicabilità e limiti

| Metodo e fonte primaria | Cosa offre | Limite per il nostro caso |
|---|---|---|
| [DCFR, Brown e Sandholm](https://arxiv.org/abs/1809.04040) | Sconti dei regret e della media; alcune varianti compatibili con pruning o sampling | Non ogni combinazione di sconti e pruning eredita la garanzia |
| [Hyperparameter Schedules](https://naifeng.github.io/assets/pdf/AAAI_2026_HS.pdf) | Schedule dinamiche senza rete addestrata | `HsDcfr30` esiste già; il test locale TST a 135 iterazioni era a dEV 2,265369%. Non è un nuovo rimedio |
| [CFR+IRA, Čermák et al.](https://arxiv.org/html/1803.05392) | Astrazione raffinata durante il solve, con analisi della convergenza verso il gioco originale | Risparmio RAM, ma più iterazioni e lavoro di raffinamento; risultati pubblicati su giochi ridotti non sono benchmark Short Deck |
| [VR-MCCFR, Schmid et al.](https://arxiv.org/abs/1809.03057) | Baseline per ridurre la varianza senza introdurre bias nell'estimatore costruito correttamente | Baseline e visite campionate hanno costi; nessuna riduzione RAM automatica del tabellare |
| [POS e baseline, Davis et al.](https://proceedings.mlr.press/v119/davis20a.html) | Sampling delle azioni pubbliche, considerando gli stati privati in forma vettoriale | Serve correggere probabilità di campionamento, media e baseline. La proprietà zero-varianza vale nelle condizioni dimostrate, non dall'inizializzazione |
| [Total RBP, Brown e Sandholm](https://cdn.aaai.org/ocs/ws/ws0393/15203-68359-1-PB.pdf) | Riduce lavoro e permette di rimuovere stato di rami con condizioni di pruning certificate | Reintrodurre i rami correttamente e contabilizzare l'errore della media; probabilità corrente zero da sola non basta |
| [CFR-D, Burch et al.](https://poker.cs.ualberta.ca/publications/aaai2014-cfrd.pdf) | Decomposizione con garanzie sul gioco complessivo e scambio tempo/memoria | Risolvere moltissimi River separatamente può costare più CPU; non basta combinare Nash locali |
| [Safe and Nested Subgame Solving](https://papers.nips.cc/paper_files/paper/2017/file/7fe1f8abaad094e0b5cb1b01d712f708-Paper.pdf) | Refinement di sottogiochi usando valori di confine e garanzie appropriate | Richiede boundary coerenti con la strategia di riferimento; non è un oracle CFV gratuito |

La fonte CFR+IRA riporta l'uso di una piccola frazione degli infoset su alcuni
domini. Quel numero non è una previsione di compressione per TST. Il suo
schema di raffinamento, la memoria dei monitor e le ipotesi teoriche vanno
replicati, non sostituiti da un generico clustering periodico.

## Progetti GitHub controllati

| Progetto | Componenti utili | Decisione |
|---|---|---|
| [b-inary/postflop-solver](https://github.com/b-inary/postflop-solver) | DCFR gamma 3, reset della media, isomorfismi, float32, compressione 16 bit, SIMD e allocator dedicato | Molti elementi sono già nel progetto; confrontare il dataflow. Il README dichiara sviluppo sospeso e nessuna card abstraction |
| [bupticybee/TexasSolver](https://github.com/bupticybee/TexasSolver) | Motore C++, supporto dichiarato Short Deck, sorgenti e benchmark pubblici | Confronto architetturale e possibile oracle secondario dopo allineamento delle regole; i suoi numeri non sono misure sulla nostra macchina |
| [OpenSpiel](https://github.com/google-deepmind/open_spiel) | Algoritmi e giochi piccoli per verifica indipendente | Usare come riferimento matematico; non sostituire con la sua struttura generica il nostro hot path |
| [Outcome Sampling C++ di OpenSpiel](https://github.com/google-deepmind/open_spiel/blob/master/open_spiel/algorithms/outcome_sampling_mccfr.h) | Interfaccia dell'algoritmo campionato e baseline | Riferimento per la semantica degli estimatori; non prova da sola una implementazione POS vettoriale pronta per il nostro motore |

Non è stato copiato codice da questi repository né sono stati eseguiti i loro
benchmark. Le loro licenze vanno esaminate prima di una futura importazione.

## Architettura proposta e prove necessarie

1. **POS separato dall'oracle exact.** Nuovo backend di ricerca con seed e
   checkpoint espliciti. Condividere carte, regole, azioni, blocker e terminali;
   mantenere la best response originale come autorità. Validare su Kuhn,
   Leduc e River/Turn ridotti l'attesa degli update, le probabilità di
   inclusione e l'averaging. Baseline e stato RNG fanno parte del checkpoint.
2. **Misura a pari qualità.** Confrontare tempo fino al target, CPU-secondi,
   visite, update, certificazione, RAM e varianza tra seed. Non confrontare lo
   stesso numero di iterazioni di full CFR e MCCFR. Se le certificazioni exact
   periodiche dominano, ridurne la frequenza durante la ricerca mantenendo
   una certificazione exact finale inclusa nel costo operativo.
3. **Bucket con lavoro proporzionato allo stato ridotto.** Un albero pubblico,
   righe decisionali bucket e terminali fisici. Evitare una matrice persistente
   completa delle coppie. Prima testare identità, pesi frazionari, range
   asimmetrici, fold/showdown e parità; poi confrontare contro il vecchio
   kernel pairwise come oracle. La diversa somma floating-point richiede
   tolleranze documentate, non un claim di byte-identità.
4. **Raffinamento della partizione.** Partizione versionata per stato pubblico
   e giocatore. Conservare storia informativa/perfect recall oppure applicare
   integralmente un metodo con garanzia per imperfect recall. Separare bucket
   quando il monitor previsto dal metodo rileva perdita strategica; valutare
   dEV/BR fisica anche su combo di scarsa massa. Stato trasferito dopo split,
   mapping e monitor entrano nel conto RAM. Nessuna composizione automatica
   con gli sconti/reset attuali o con POS.
5. **Qualificazione.** D/V per sviluppo, holdout H ancora sigillato fino alla
   scelta del candidato. Configurazioni fisse, almeno cinque processi per la
   verifica finale, seed dichiarati per sampling, mediana/p95 e singoli
   risultati. Nessun PASS RAM contro GTO+ finché manca la comparabilità.

I file coinvolti in un futuro prototipo sono un nuovo modulo di ricerca
accanto a `libs/postflop`, un runner dedicato in `benchmarks`, test separati in
`tests` e il contratto
`docs/specifications/CARD_ABSTRACTION_AND_SUBGAME_CONTRACT.md`.
Il file monolitico corrente non va riscritto prima di avere un oracle di
equivalenza dei terminali e un vantaggio misurato.

## Stato della validazione di questa sessione

La ricompilazione iniziale ha incontrato un errore di accesso alla cache
registry vcpkg esterna al workspace. La configurazione è stata ripetuta con
`VCPKG_MANIFEST_INSTALL=OFF`, usando le dipendenze già installate; CMake ha
configurato e generato correttamente la build Release corrente.
L'inventario CTest contiene 43 test. Gli esiti di esecuzione e le misure
contemporanee sono separati dai risultati storici sopra.

| Verifica eseguita il 10 settembre | Esito |
|---|---|
| Build Release completa, MSVC, `/O2 /Ob3 /GL`, AVX2 e LTCG del progetto | PASS |
| CTest completo disponibile | **43/43 PASS**, 929,00 s di wall riportato da CTest |
| Reference GTO+ compresa nel CTest | `GTO_PLUS_REFERENCE_TEST=PASS`, 24 asserzioni |
| Oracle della fattorizzazione blocker | 12 combinazioni scenario/giocatore, 1.530 confronti interi, PASS |
| Runner A/B | Sintassi, CLI e lettura dei report verificate; tre preflight respinti, nessun solve A/B avviato |
| `git diff --check` | PASS sul workspace corrente |

JUnit: `out/postflop-research-20260910/ctest.xml`. Binario usato:
`out/build/windows-release-current/apps/gto_cli/gto_cli.exe`, SHA-256
`612F1183A00C687260F310DA97DE67CC11129636A86107E684EEB8E5F0C21947`.
CPU rilevata: Intel Core i3-10100F, 8 processori logici. Questa suite è quella
configurata nel preset corrente; la GUI Qt di prodotto è disabilitata e non è
stata eseguita una nuova build AddressSanitizer. I PASS dei test di smoke dei
bucket non trasformano le loro qualifiche prestazionali negative in PASS.

Il confronto dei due percorsi di certificazione è riproducibile con
`tools/compare_postflop_certification.py`. Usa processi freschi, cinque
campioni CPU di un secondo prima di ciascun processo, almeno 4 GiB liberi,
hash del binario e degli input, ambiente child isolato e confronto dell'intera
curva numerica. Un solo campione per percorso è diagnostico; il runner non
promuove mai automaticamente una modalità. Nei run ripetuti alterna l'ordine
dei due percorsi. Il memory ledger corrente usa un mutex negli aggiornamenti;
la sicurezza del suo accesso concorrente è stata verificata nel sorgente.

### A/B prestazionale non eseguito: carico esterno

Tre tentativi, tutti dopo la fine della suite, hanno rilevato CPU media
`70,5392%`, `40,4473%` e `63,9063%` contro il massimo del 15%. La RAM libera
era rispettivamente `15.618.297.856`, `15.272.329.216` e `15.277.006.848 B`,
sopra il minimo richiesto. Il runner ha interrotto ogni tentativo prima del
primo solve. Non è un fallimento matematico del solver né un campione di
timing utilizzabile. I processi esterni non sono stati interrotti.

I preflight grezzi sono conservati in
`out/postflop-research-20260910/certification-ab/summary.json`,
`certification-ab-r2/summary.json` e `certification-ab-r3/summary.json`.
Per riprendere usare il runner con una nuova directory di output e lasciare
passare il preflight; non alzare la soglia per ottenere un risultato.
L'effetto prestazionale del percorso parallelo resta **NOT_EVALUATED**.

In questa sessione sono stati aggiunti il report, due strumenti di ricerca
Python e il collegamento nel documento performance. Non sono state applicate
modifiche al motore C++ né ai default. L'oracle elimina la necessità della
matrice delle coppie nel calcolo sperimentale delle masse; una riduzione di
RAM/CPU del prodotto non è ancora dimostrata.

Nessuna nuova implementazione di POS, CFR+IRA o bucketing adattivo è stata
promossa. Il collo principale è identificato nelle evidenze precedenti e
riscontrato nella struttura del codice; la sua eliminazione resta da
dimostrare con i prototipi e i gate sopra.
