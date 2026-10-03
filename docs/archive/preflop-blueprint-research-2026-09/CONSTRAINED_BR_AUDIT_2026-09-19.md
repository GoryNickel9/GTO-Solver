# Best response congiunta e memoria imperfetta

Protocollo successivo a `NASH_AUDIT_2026-09-19.md`, autorizzato dall'utente
con «Procedi». Base del lavoro: `9b9d427`, branch `codex/nash-convergence-audit`.
Obiettivo: separare una deviazione rappresentabile ma non appresa da una
deviazione che richiede informazione assente nei bucket. Il gate CO40 resta
0,03 ante di massimo guadagno individuale nel gioco fisico ad azioni fissate.

## 1. Oracolo e dominio matematico

`enumerated_best_response` enumera tutte le politiche pure del rispondente,
scegliendo una sola azione per insieme informativo. L'avversario resta fisso;
chance ed EV sono valutati integralmente. Non effettua massimi locali separati.
Il prodotto delle cardinalita delle azioni determina il numero di politiche.
I limiti iniziali sono 1.000.000 politiche e 100.000.000 valutazioni di nodo.
Overflow e superamento dei limiti producono un errore prima della ricerca.

Se un insieme informativo con piu azioni non ricorre due volte sullo stesso
percorso, il payoff e affine nella strategia di ciascun insieme, tenendo fisse
le altre. Ogni strategia comportamentale puo quindi essere portata a un vertice
puro, un insieme alla volta, senza diminuirne il valore. L'enumerazione globale
trova anche l'ottimo comportamentale. Il controllo rifiuta ricorrenze che rendono
inapplicabile questo argomento; non basta che il grafo sia aciclico.
Le azioni forzate sono costanti e non introducono questo problema.
«Esatto» indica enumerazione completa nel gioco dato, con aritmetica float64.

La nuova API restituisce la politica globale e il lavoro completato. Non
inventa valori controfattuali locali per quella politica. Le prove indipendenti
usano formule analitiche e il valutatore ricorsivo di profili gia esistente,
separato dalla nuova valutazione in ordine topologico.

## 2. Errore riprodotto nella vecchia API generica

Chance sceglie L o R con probabilita 1/2. Il giocatore vede il tipo e puo
rinunciare per zero o entrare. Dopo l'ingresso dimentica il tipo: deve usare
la stessa scelta a/b nei due casi. Payoff di a/b: L = (2, -1), R = (-4, 0).

La vecchia `exact_best_response` preferisce b al secondo livello, aggregando
anche R indipendentemente dalla precedente scelta del giocatore. Poi rinuncia
ovunque: valore 0. La deviazione congiunta entra soltanto in L e sceglie a:
valore 1, massimo delle otto politiche. Con memoria conservata anche la vecchia
API restituisce 1. La riproduzione prima della correzione e salvata in
`out/nash_audit/constrained_br_legacy_reproduction.log` (test PASS).

La API generica ora verifica perfect recall del rispondente e rifiuta un
dominio non supportato. Anche `calculate_nash_conv` propaga questo errore.
La prima regressione trova un chiamante errato nel test del trainer: il gioco
ridotto con bucket a stack 40 non ha perfect recall. Il confronto di regret e
strategy sum passa; fallisce la successiva falsa certificazione astratta.
Il test viene corretto confrontando le BR fisiche con il FiniteGame lossless,
sulla stessa policy sollevata. Questo non modifica il certificatore fisico
CO40 e non smentisce il suo valore precedente di 0,5001806189608786 ante.

## 3. Esperimento preregistrato prima dell'esecuzione

Tre partizioni dello stesso problema analitico, con payoff R(a) = -3:

1. `lossless`: conserva il tipo prima e dopo l'ingresso.
2. `forget_later`: conosce il tipo prima, lo dimentica dopo.
3. `blind`: non distingue il tipo ne prima ne dopo.

L'ottimo congiunto vale 1 per le prime due partizioni e 0 per la terza.
Il gioco fisico conserva il tipo. La policy viene sollevata senza cambiare
frequenze: il suo EV deve coincidere nei due giochi.

Training: Linear CFR esatto e Linear MCCFR, un thread, seed 101/202/303 per
il campionato; una sola traiettoria deterministica per l'esatto. Checkpoint
1.000, 10.000, 100.000 iterazioni. Moltiplicatori di tutti i payoff: 1, 10, 40.
Il moltiplicatore non cambia stack, azioni o informazione: serve a isolare la
scala monetaria. Ogni metrica viene certificata per enumerazione, anche quando
il training e campionato. Le tre traiettorie non sono un intervallo di
confidenza e non dimostrano un limite asintotico.

Per ciascun profilo registrare EV, BR vincolata, BR fisica, guadagno interno
`BR_vincolata - EV`, divario informativo `BR_fisica - BR_vincolata` e guadagno
fisico. La somma dei primi due coincide algebricamente con il terzo, per questo
avversario fisso. Non e una scomposizione causale universale di CO40.

Se l'oracolo fallisce un controllo analitico, fermare gli esperimenti dipendenti.
Se il lossless esatto non migliora, indagare il training prima dei bucket.
Se fallisce soltanto il campionato, controllare stimatore e copertura.
Se fallisce anche l'esatto con memoria cancellata, il rumore di campionamento
non e necessario a produrre quel fallimento. Se solo il gioco blind converge
internamente ma resta sfruttabile fisicamente, attribuire il residuo di questo
esempio all'informazione esclusa. Nessuno di questi esiti identifica da solo
la causa di CO40.

Eseguire anche il conteggio preventivo delle politiche nei giochi Short Deck
di riferimento e nel gioco del trainer. Se non sono enumerabili, registrare
il limite e ridurre esplicitamente il caso diagnostico; non sostituire un
ottimizzatore locale spacciandolo per BR globale.

## 4. Estensione globale dopo il limite di enumerazione

Il conteggio supera 64 bit sia nel riferimento Short Deck a quattro street
(74/76 insiemi informativi per giocatore), sia nel corpus del trainer a stack
40 (242/142). Il river toy richiede invece 16 politiche per giocatore.

SciPy 1.16.2 e gia disponibile localmente. Il diagnostico Python usa il suo
risolutore MILP HiGHS, senza aggiungere una dipendenza al motore nativo.
Variabili binarie scelgono una sola azione per insieme informativo. Per ogni
sequenza di azioni proprie, una variabile z realizza il prodotto fra la
sequenza precedente x e l'azione y: `z <= x`, `z <= y`, `z >= x+y-1`.
L'obiettivo aggrega i payoff terminali pesati soltanto per chance e avversario.
Il metodo ottimizza congiuntamente tutte le decisioni, con costante M = 1.

Dominio: alberi finiti, nessuna ricorrenza di un insieme informativo proprio
sullo stesso percorso. Politica restituita rivalutata indipendentemente,
controllo dell'obiettivo e dell'integralita. Limiti iniziali: 30 secondi e
10.000 nodi branch-and-bound per risposta, gap relativo richiesto zero.
Se la ricerca si interrompe, riportare il valore della deviazione fattibile
e il limite superiore disponibile; non dichiarare l'ottimo. I bound sono
numerici, non certificati razionali. Tolleranza assoluta dichiarata:
`1e-8 * max(1, massimo payoff assoluto)`.
Semantica di status e bound: [documentazione SciPy milp](https://docs.scipy.org/doc/scipy/reference/generated/scipy.optimize.milp.html).

Prima applicazione: otto confronti contro l'enumerazione C++ (due giocatori
su controesempio con/senza memoria, Kuhn e Short Deck river). Tutti passano.
Il primo tentativo rivela soltanto un caso non gestito dal report: con zero
decisioni proprie HiGHS non restituisce un MIP bound. Il valore unico viene
ora calcolato direttamente dal profilo; nessuna ottimizzazione e necessaria.

Protocollo Short Deck fissato prima del training: stesso fixture CO40-TEST,
modificare soltanto lo stack a 20/40 ante; otto board ramificati del test
esistente, pesi 1..8, prime/ultime combo dei suoi subset (due per giocatore).
Tre partizioni: `class` ricostruita dalle tabelle reali, stessa partizione
con sequenza completa delle informazioni/azioni proprie (`history`), lossless.
La memoria history non e il vecchio hash recall32 e viene misurata soltanto
nel corpus ridotto. Verificare identita di albero/payoff e lifting senza
informazione futura. Linear CFR esatto e Linear MCCFR con seed 101/202/303,
checkpoint 25/250/2.500. Iterazioni uguali non significano lavoro uguale.
Certificare fisicamente tutte le policy nel corpus; applicare prima il MILP
ai checkpoint finali e agli esatti intermedi, entro i limiti fissati.
Gli all-in preflop usano la tabella completa, come nell'oracolo esistente;
gli altri runout seguono il corpus finito. Non e la distribuzione completa
dei board del gioco CO40.

## 5. Esito del primo corpus e unico ampliamento mirato

Con training esatto il corpus del trainer converge in class/history/lossless a entrambi gli stack:
massimo guadagno fisico esatto a 2.500 iterazioni 4,198649e-6 ante (20) e
5,545578e-6 (40). Class viola perfect recall per BTN; il solo fatto di violarlo
non basta a produrre un fallimento in questo caso. Le 72 BR MILP chiudono
numericamente in 71 casi; history/20/seed303/BTN mantiene un gap 4,250357e-7
ante e resta non qualificata alla tolleranza preregistrata. Nessuna soglia viene
rilassata. ASAN passa enumerazione, riferimenti del solver e toy temporale.
Questi conteggi MILP descrivono il primo tentativo, superato dalla correzione
numerica della sezione 6. I risultati finali sono nei file `*_strict_bounds.json`.

Il corpus ha board con gli stessi ranghi e mani private a coppie: i cambiamenti
di informazione pubblica incidono poco sui payoff. Per evitare una diagnosi
basata su questo caso debole, fissiamo un solo ampliamento prima del training:
due esempi della riga class 28, nodo 4, vista self-reach nell'audit CO40 completo.
`AcAd` su `6c7dQc` e `AdAh` su `9c9dQc` condividono davvero la riga; la build
deve verificarlo nelle tabelle reali. Range eroico: quelle due combo; avversario:
`KsKh` e `QsQh`, disgiunte da tutti i board. Turn `8c`/`Kd`, river `Jc`/`Qd`:
otto combinazioni con pesi 1..8, come prima. Introducono colore, set e poker,
rendendo utile la distribuzione dell'informazione nel tempo. Size, stack,
partizioni, iterazioni e seed restano quelli del primo protocollo.

E una selezione diagnostica da un conflitto osservato, non un campione
rappresentativo e non un confronto contro la policy CO40 originale: i range
sono ristretti e le policy vengono allenate nuovamente. Se non riproduce una
separazione, non promuovere history e non cercare altri corpus fino a trovare
un risultato favorevole. Usare la traccia della BR CO40 per decidere quale
evidenza manca.

La traccia aggiunta segue il preflop della BR fisica completa. Registra per
ogni ingresso flop la probabilita sotto blueprint e sotto BR, e il guadagno
del cambio postflop su quel medesimo percorso. La somma viene verificata
contro un profilo lossless indipendente con preflop BR e postflop blueprint.
Il valore intermedio puo essere peggiore del blueprint: la deviazione
congiunta potrebbe richiedere un cambiamento preflop da solo sfavorevole.

## 6. Fallimento numerico e controllo delle traiettorie campionate

Il controllo indipendente sul corpus mirato rileva una BR MILP lossless
inferiore all'oracolo C++ di 8,903134e-7 ante, nonostante status ottimo e gap
numerico zero. Il processo si arresta. La versione HiGHS locale usa
`mip_abs_gap=1e-6` e `mip_feasibility_tolerance=1e-6`. Azzerare soltanto il
primo non corregge l'errore; portare anche la fattibilita a `1e-9` riproduce
l'ottimo C++ senza scarto osservato. Tolleranza di accettazione del report
invariata. Da qui tutti i risultati usano entrambi i parametri espliciti;
i file precedenti restano come tentativi superati. Un test Python permanente
copre il caso. [Opzioni HiGHS](https://ergo-code.github.io/HiGHS/dev/options/definitions/).

Precisazione sull'esito: tutte le partizioni convergono con training esatto;
il campionato mostra invece differenze sostanziali. A stack40/seed303,
class ha guadagno fisico 0,213965 ante, di cui almeno 0,198554 ottenibili
nella stessa rappresentazione. History scende sotto 0,0054 su tutti i tre
seed, ma anche lossless ha un seed a 0,0634: non basta aumentare informazione
per garantire un risultato migliore dopo un budget finito.

La regola per il fallimento del campionato richiede ora un controllo della
durata. Prima dell'esecuzione fissiamo: medesimo corpus mirato a stack40,
class/history/lossless, solo Linear MCCFR, stessi seed 101/202/303, checkpoint
2.500/10.000/100.000. Nessuna nuova combinazione di parametri. Verificare che
il primo checkpoint riproduca esattamente i profili gia misurati; salvare lo
stato intermedio. Misurare separatamente nodi visitati e tempo dei segmenti.
Un residuo che cala proseguendo non va chiamato pavimento; un residuo
persistente su questa sola prova non dimostra un limite asintotico CO40.
Registrare anche la BR fisica della strategia corrente agli stessi checkpoint,
separatamente dalla media: serve a distinguere una media lenta da una dinamica
che continua a produrre strategie sfruttabili. Non sostituire la metrica della
strategia media con quella corrente per dichiarare il successo del training.

## 7. Risultato delle traiettorie prolungate

I nove profili a 2.500 iterazioni coincidono esattamente con quelli del primo
run mirato. La continuazione usa i checkpoint del solver, senza reinizializzare
regret, accumuli della media o generatore casuale. I file checkpoint finali
restano in `out/nash_audit/short_deck_conflict_long_corpus.json.*.checkpoint.json`.

Massimo guadagno individuale fisico della **strategia media**, in ante,
prendendo il peggiore dei due giocatori e dei tre seed a stack 40:

| Partizione | 2.500 iterazioni | 10.000 | 100.000 |
|---|---:|---:|---:|
| class | 0,213964932 | 0,004714814 | 0,000046978 |
| history | 0,005397043 | 0,000334829 | 0,000003344 |
| lossless | 0,063399777 | 0,003097608 | 0,000031747 |

La strategia **corrente** ha NashConv fisica zero, in aritmetica float64, in
tutti i 27 checkpoint osservati. Non abbiamo osservato ogni iterazione fra
quei checkpoint. Il residuo della media cala continuando la stessa traiettoria:
il confronto breve aveva rilevato un transitorio, non un pavimento. History
riduce quel transitorio in questa prova; non dimostra una soluzione necessaria
al problema CO40. Anche class raggiunge una deviazione fisica molto piccola.

La certificazione fisica completa del corpus offre anche un limite superiore
alla deviazione vincolata ai bucket. Questo resta valido quando il MILP non
chiude entro 30 secondi. I risultati MILP incompleti conservano i due bound;
non diventano ottimi perche il training appare buono.

| Certificazione con opzioni numeriche corrette | Risposte chiuse | Totale |
|---|---:|---:|
| Corpus iniziale | 72 | 72 |
| Corpus mirato | 71 | 72 |
| Policy CO40 trasferita nel corpus mirato | 6 | 6 |
| Traiettorie prolungate | 49 | 54 |

Il caso aperto del corpus mirato e history/40/seed202/2.500/P0. Nel
prolungamento restano aperti class/seed202/P0 a 2.500 e 10.000, e
history/seed202/P0 a tutti e tre i checkpoint. Le risposte ripetute possono
terminare diversamente entro il limite temporale; non sono repliche
statistiche indipendenti. Non estendiamo il budget per farle apparire chiuse.

Il training di questi corpus usa `solve_finite_game`, il solver di riferimento
con Linear CFR / Linear External Sampling MCCFR. **Non e una nuova esecuzione
del trainer vettoriale CO40**, che campiona board in batch e usa il proprio
schedule. Le verifiche del trainer confrontano separatamente il core con il
gioco di riferimento. Il dato sulla lentezza della media giustifica un controllo
sul trainer reale; non autorizza a trasferire la diagnosi automaticamente.

## 8. Cosa cambia nella lettura del CO40 completo

Il replay dei 573 flop dal certificato storico riproduce esattamente EV e
massimo guadagno individuale: **0,5001806189608786 ante**, contro gate 0,03.
La policy resta `fnv1a64:be84f6b45d37b5b8`, albero
`fnv1a64:18d08f453034ac0f`. La nuova traccia usa la preflop policy della stessa
BR fisica che ottiene quel valore, tenendo fisso l'avversario.

Per CO, modificare quel solo preflop e ripristinare il postflop del blueprint
peggiora l'EV di **0,1357604811083087 ante**. Modificare poi anche il postflop
su quelle stesse rotte guadagna **0,6359411000691864 ante**. La somma vale
0,5001806189608786, con scarto numerico inferiore a 1e-14.

| Ingresso flop | Probabilita blueprint | Probabilita sotto BR CO | Guadagno del cambio postflop sulla rotta BR |
|---|---:|---:|---:|
| CO limp, BTN check (nodo 3; prima decisione 4) | 0,179398473 | 0,621118271 | 0,461761958 a |
| CO limp, BTN bet 4, CO call (nodo 321) | 0,034529155 | 0,170696974 | 0,174179142 a |
| CO raise 5, BTN call (nodo 447) | 0,007405374 | 0 | 0 |
| CO raise 5, BTN raise 16, CO call (nodo 567) | 0,000081239 | 0 | 0 |

Per BTN: cambio preflop -0,007105982704, cambio postflop +0,201644951122 ante.
Questa e una contabilita additiva di politiche fissate, non un'attribuzione
causale e non una BR globale vincolata ai bucket. Localizza gli ingressi flop
rilevanti; non localizza ancora le decisioni responsabili al turn o river.

Una prova aggiuntiva solleva la policy CO40 salvata nel corpus mirato, senza
riallenarla. Le sei risposte globali MILP chiudono: guadagni P0/P1
0,815649908 / 40,122973902 ante, uguali per class/history/lossless. **Non sono
i guadagni del CO40 originale**: restringere range e futuri cambia radicalmente
l'avversario affrontato e il gioco. Questa prova verifica il trasferimento
della policy e la BR congiunta; non misura la quota interna dello 0,500181.

## 9. Decisione e prossimo protocollo

Esito sulla causa dominante del CO40 completo: **INCONCLUSIVE**. Abbiamo
corretto un certificatore generico usato fuori dal suo dominio e costruito
una BR globale per giochi ridotti. Non abbiamo migliorato la policy CO40.
Non promuoviamo history, nuovi bucket o un nuovo averaging sulla base dei soli
corpus diagnostici.

Il confronto 20/40 cambia soltanto lo stack della stessa specifica di sizing;
i due alberi compilati differiscono per gli effetti dello stack sulle azioni
legali. Entrambi convergono con training esatto nei corpus. La scala dei payoff
isolata nel toy cambia le metriche normalizzate di al piu 6,661338e-16.
Questi controlli non riproducono il peggioramento del gioco completo. Non
consentono di attribuirlo alla sola grandezza dei numeri, alla memoria imperfetta
o al numero di iterazioni.

Il seguito va affrontato in quest'ordine, senza avviare una ricerca aperta di
parametri o corpus:

1. **Corrente contro media sul checkpoint reale CO40.** Verificare l'identita
   del checkpoint e della sua policy media esportata; estrarre anche la corrente
   senza allenare. Valutare entrambe sugli stessi flop, con identici range,
   sizing e risorse. Se il vantaggio della corrente non si ripete su controlli
   separati, non modificare l'averaging. Se si ripete, misurare gli accumuli
   temporali prima di proporre una finestra o un reset; la qualificazione resta
   sulla policy effettivamente esportata.
2. **Deviazione rappresentabile nelle due rotte limp dominanti.** Tenere i
   range e i futuri del CO40 completo. Cercare una politica congiunta preflop /
   postflop che condivida davvero le azioni dentro ogni bucket. Una deviazione
   rivalutata fornisce un lower bound interno anche senza ottimo globale.
   Se la ricerca non trova guadagno, l'esito resta inconcludente: non prova che
   i bucket siano il limite. Non sostituire i massimi locali all'ottimo congiunto.
3. **Controllo degli stack sul trainer reale.** Solo dopo aver verificato la
   misura precedente, confrontare stack diversi con la stessa specifica,
   seed e lavoro misurato, distinguendo unità monetarie e modifica dell'albero.
   Un risultato peggiore dopo lo stesso numero di iterazioni non basta:
   registrare nodi visitati, copertura, tempi e strategie corrente/media.
4. **Correzione condizionata all'evidenza.** Se domina una deviazione gia
   rappresentabile, intervenire sul training identificato dal controllo. Se
   emerge una perdita di rappresentazione, congelare un candidato offline e
   confrontarlo a parita di budget. In entrambi i casi servono più seed e BR
   fisica finale completa; se il candidato non migliora, registrare il fallimento
   senza promuoverlo o allentare il gate.

Nessun divieto documentale ha impedito una soluzione gia dimostrata utile.
Restano i limiti di memoria/tempo e il divieto di reclustering durante il
training. Se una futura proposta fondata richiedesse una deroga, occorrera
presentare all'utente la misura del beneficio, il costo e la regola precisa.

## 10. Riproduzione e validazione

Ambiente: Windows, i3-10100F (8 processori logici), MSVC 19.51, C++20 Release,
`/W4 /WX`, float64; trainer diagnostico a un thread. Python 3.13, SciPy 1.16.2.
I tempi includono l'esecuzione concorrente di controlli e non sono benchmark
di throughput isolato. Run completi: corpus iniziale 98,25 s, corpus mirato
150,96 s, prolungamento 20,70 s. Il primo include anche training esatto costoso;
il prolungamento comprende soltanto MCCFR. Non confrontarli come speedup.

Build con il preset `windows-release`, target `gtosd_constrained_br_audit`,
`gtosd_short_deck_constrained_audit`, `gtosd_preflop_blueprint_certify` e relativi
test. Dalla radice del worktree, dopo aver caricato l'ambiente MSVC:

```powershell
$bench = 'out/build/windows-release/benchmarks'
& "$bench/gtosd_constrained_br_audit.exe" out/nash_audit/constrained_br_mechanisms_with_oracles.json
& "$bench/gtosd_short_deck_constrained_audit.exe" out/preflop_blueprint_resources out/preflop_blueprint_buckets_200_500_1000 out/nash_audit/short_deck_constrained_corpus.json
& "$bench/gtosd_short_deck_constrained_audit.exe" out/preflop_blueprint_resources out/preflop_blueprint_buckets_200_500_1000 out/nash_audit/short_deck_conflict_corpus.json --conflict-witness
& "$bench/gtosd_short_deck_constrained_audit.exe" out/preflop_blueprint_resources out/preflop_blueprint_buckets_200_500_1000 out/nash_audit/short_deck_conflict_long_corpus.json --conflict-witness --long-sampling
python scripts/research/run_short_deck_constrained_milp.py out/nash_audit/short_deck_conflict_long_corpus.json out/nash_audit/short_deck_conflict_long_strict_bounds.json --all-checkpoints
python scripts/research/test_constrained_br_milp.py --oracle-file out/nash_audit/constrained_br_mechanisms_with_oracles.json --short-deck-file out/nash_audit/short_deck_conflict_corpus.json -v
```

Per i corpus iniziale/mirato applicare lo stesso driver MILP senza
`--all-checkpoints`. Per il trasferimento della policy usare `--saved-policy`
e `--reference-certificate`, entrambi riferiti a `out/class_20260917` del
checkout principale, su un output distinto `short_deck_saved_conflict_corpus.json`.
Per la traccia CO40 usare il certificatore con `--class-rows`, stessi file
policy/certificato, `--state` su una **copia** locale di `cert_state.bin`,
`--flop-limit 573 --chunk 8 --threads 8`. Non riutilizzare quel cache per una
policy differente. I fingerprint devono coincidere prima del replay.

Validazione completata:

- Release: 7/7 gruppi PASS in 127,30 s: external sampling/averaging, BR globale,
  riferimenti phase5, card abstraction, trainer vettoriale, toy temporale,
  certificatore. Le prove della nuova traccia confrontano un profilo lossless
  costruito indipendentemente, con range sovrapposti e stack 40/100/300.
- ASAN: BR globale, phase5 e toy temporale PASS, 3/3 in 80,43 s. Queste prove
  precedono la nuova traccia fisica: non dichiarano copertura ASAN di essa.
- Python: 6 test PASS in 4,51 s, inclusi otto confronti con enumerazione C++ e
  la regressione numerica HiGHS; Black e Ruff PASS.
- Il replay CO40 conserva esattamente le metriche storiche; l'identita additiva
  della traccia e verificata. Nessun nuovo training della policy completa.

I riepiloghi JSON adiacenti conservano metriche, opzioni del risolutore,
risposte incomplete e SHA256 degli input. `CONSTRAINED_BR_SOURCE_MANIFEST`
identifica i sorgenti rispetto alla base `9b9d427`.
`scripts/research/summarize_constrained_br_audit.py` li rigenera dai file locali,
verificando anche la riproduzione dei nove profili iniziali. Gli output grezzi
con i profili completi restano in `out/nash_audit`; non sono aggiunti a git.
