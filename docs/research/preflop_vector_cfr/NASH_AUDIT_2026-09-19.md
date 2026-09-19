# Audit della convergenza — 2026-09-19

## Perimetro e stato

Base: `744113c69342a82f3b920add498106af2b763d52`. Branch:
`codex/nash-convergence-audit`, worktree separato. L'utente ha autorizzato il piano
come unico goal della giornata. Regole, size, utility e soglie restano invariate.

| Fase | Stato | Evidenza richiesta |
|---|---|---|
| Normalizzazione del diagnostico | PASS | enumerazione indipendente, regressioni, ricalcolo del certificato |
| Conflitti decisionali nei bucket | INCONCLUSIVE sulla causa dominante | 573 flop, tre nodi, errori di entrambi i tipi |
| Meccanismo su gioco enumerabile | PASS sul toy | valore analitico e certificazione esatta; trasferimento a CO40 da verificare |
| Candidato potential-aware offline | NOT_RUN, evidenza insufficiente | il controllo decisionale peggiora due nodi e ne migliora uno |
| Confronto matched HU20/CO40 | NOT_RUN, gate precedente non superato | nessun nuovo training o certificato di candidato |

Questo PASS riguarda il diagnostico, non la qualificazione di CO40.

## 1. Massa e probabilita dell'ingresso

Il campo storico `opponent_reach` somma i pesi delle combo avversarie dopo le
azioni preflop dell'avversario. Le azioni dell'eroe sul percorso sono forzate.
Ogni combo parte da peso 1: la somma non e una probabilita.

Siano A(h) il peso di ammissibilita dell'eroe (0 o 1 nell'API attuale), O(o) quello
dell'avversario e r(o) la massa avversaria propagata, che comprende O(o):

```text
p(entry | h) = sum[o disjoint h] r(o) / sum[o disjoint h] O(o)
p(entry)     = sum[h] A(h) p(entry | h) / sum[h] A(h)
conditional_gain = mean_gain / p(entry), se p(entry) > 0
```

Il prior e quello uniforme sulle combo ammesse dell'eroe del diagnostico storico.
Non comprende la reach preflop della sua strategia. Con i range completi,
ciascuna delle 630 combo ha 561 avversarie compatibili. Ogni combo avversaria
compare in 561 coppie disgiunte: `p(entry) = opponent_reach / 630`. Questa
semplificazione non vale per sottoinsiemi arbitrari; il codice usa i blocker.

La tabella "Perdita postflop a reach fissato su CO40" del diario del 2026-09-18
divideva direttamente per `opponent_reach`: i valori erano 630 volte inferiori
al rapporto corretto nel caso esatto a range completi.

| Ingresso | CO corretto (ante/ingresso) | BTN corretto (ante/ingresso) |
|---|---:|---:|
| limp-check | 1,172574924 | 1,132333007 |
| limp-bet-call | 1,372247263 | 1,145197437 |
| open-call | 1,421923837 | 1,134191379 |
| open-3bet-call | 0,730602369 | 0,842305039 |

Artefatto: [certificato ricalcolato](CO40_NORMALIZATION_AUDIT_2026-09-19.json).
Questi valori non usano i range condizionati effettivi di entrambi i giocatori,
non sono additivi fra ingressi e non decompongono causalmente l'exploitability.
La conclusione che il postflop fosse irrilevante perche perdeva circa 0,002 ante
va ritirata; la correzione non prova da sola la causa delle scelte preflop.

### Disponibilita e compatibilita

`mean_gain` e `opponent_reach` mantengono valori e significato. Il report aggiunge
`entry_probability` e `conditional_gain`. Quest'ultimo e opzionale, serializzato
come `null` per ingressi irraggiungibili, valutazioni non esatte o range ristretti.
Per questi ultimi, anche la distribuzione dei board richiede una verifica:
il rapporto non e ancora validato come EV condizionata fisica.
Nessun cambiamento ai binari di policy/checkpoint/stato del certificatore.
Lo schema JSON v1 mantiene i campi precedenti; i consumatori devono ignorare
i nuovi campi opzionali quando non li usano.

### Verifiche eseguite

Build `windows-release`, MSVC 19.51.36248.0, C++20, `/W4 /WX`, otto job.
Dipendenze e risorse gia disponibili; nessuna ricostruzione delle tabelle.

```powershell
ctest --preset windows-release -R '^gtosd_preflop_blueprint_certifier_tests$' --output-on-failure
ctest --preset windows-release -R '^gtosd_preflop_blueprint_(trainer|export)_tests$' --output-on-failure
```

| Suite | Esito | Tempo |
|---|---|---:|
| Certificatore | PASS, 138.974 assert | 42,46 s |
| Trainer | PASS | 76,66 s |
| Export | PASS | 48,80 s |

Il nuovo oracolo enumera le coppie disgiunte e moltiplica le probabilita sul
percorso pubblico senza la propagazione o le somme per carta della produzione.
Copre percorsi frazionari, strategie deterministiche, blocker, sottoinsiemi e
probabilita zero. Inietta valori analitici di foglia per isolare l'aggregazione:
una perdita di due ante per ingresso deve restituire due ante, non due / 630.
Il fixture sintetico non viene presentato come gioco fisico.

CO40 e stato ricalcolato da una copia dei 573 flop di
`out/baseline_20260917/cert_state.bin`, con policy originale in lettura.
Tempo: 1,4004197 s. EV, best response, guadagni globali e preflop/postflop,
NashConv e fingerprint di policy/albero sono identici come valori double al JSON
precedente. `max_gain` resta 0,82717651588212859 ante. Il ricalcolo riusa le
valutazioni salvate; non e un nuovo solve ne una nuova enumerazione dei runout.

## 2. Conclusioni precedenti da riesaminare

L'EV di self-play monotona non certifica un equilibrio. Cinque certificazioni
esatte sulla stessa traiettoria non dimostrano un limite asintotico: il training
resta campionato. Il confronto `class`/`recall32` citato come miglioramento
dell'1,7% riguarda alberi diversi. Il numero di righe da solo non prova che ogni
riga sia sufficientemente allenata.

Gli istogrammi flop attuali descrivono l'equity al river, perdendo l'associazione
alle distribuzioni condizionate ai turn. In
[Ganzfried e Sandholm (2014)](https://www.cs.cmu.edu/~sandholm/potential-aware_imperfect-recall.aaai14.pdf)
questa e la rappresentazione distribution-aware; quella potential-aware descrive
anche le transizioni fra street. Non e gia stata esclusa dall'implementazione
attuale. Non e garantito che aggiungerla basti per il gate.

## 3. Regole degli esperimenti successivi

Con avversario e continuazione congelati, confrontare la strategia corrente con
la migliore azione comune del bucket, poi con scelte separate per stati fisici.
Integrare l'informazione nascosta prima del massimo. Esporre pesi, copertura e
perdite locali senza sommarle come exploitability globale.

Se prevale l'errore della strategia comune, verificare il training prima delle
feature. Se prevale il conflitto di rappresentazione, verificarne il meccanismo
su un gioco ridotto. Un risultato ambiguo consente un solo ampliamento diagnostico
motivato, poi resta INCONCLUSIVE. Costruire tabelle nuove solo con evidenza
favorevole; minore distanza di clustering senza vantaggio decisionale non basta.
Il confronto completo usa tre seed appaiati, checkpoint prestabiliti e
certificati esatti. Nessuna scelta del seed migliore.

## 4. Diagnostico dei conflitti: protocollo e controlli

L'import `class` ricostruisce la numerazione densa delle coppie (classe preflop,
bucket della street), nello stesso ordine class-major del probe storico.
Capacita e fingerprint di policy, albero e tabelle sono verificati contro il
certificato di riferimento. Non si riusano gli eseguibili sperimentali di `out/`.

Il test di lifting copia una strategia sui corrispondenti indici class e verifica
che tutti i valori di foglia, per mano e per entrambi i giocatori, coincidano
con quelli della rappresentazione base sui runout completi di un flop.
La suite del certificatore con questo test e PASS (43,13 s).
Otto flop CO40 class sono stati poi ricalcolati e confrontati con gli stessi
flop dello stato storico: EV, best response e guadagni coincidono entro 1e-12.
Tempo del ricalcolo fisico: 15,0397813 s; replay storico: 0,6733675 s.
Questi otto flop sono un controllo di equivalenza, non una nuova certificazione completa.

Per ogni nodo e riga, fissati valori d'azione Q(s,a), pesi w(s) e strategia pi(a):

```text
S(a) = sum_s w(s) Q(s,a)
shared_strategy_gain = max_a S(a) - sum_a pi(a) S(a)
separation_gain = sum_s w(s) max_a Q(s,a) - max_a S(a)
mass = sum_s w(s) P(opponent reaches node | s)
```

Q comprende gia la reach avversaria e l'integrazione delle carte nascoste e dei
runout. I pesi comprendono la molteplicita del flop canonico e, nella vista
`self_reach`, la reach dell'eroe sul percorso. La vista `forced_hero_prefix`
forza invece tutte le sue azioni precedenti. La divisione per mass restituisce
una perdita locale condizionata a quell'ingresso e a quella copertura di board.
Nessuna delle due viste e una best response vincolata ai bucket.

`preflop_blueprint_decision_gap_tests` PASS (0,05 s): preferenze opposte con
solo costo di aggregazione; preferenze allineate con sola strategia comune
subottimale; reach frazionaria; invarianza a costanti nei payoff; massa nulla;
rifiuto di pesi negativi, strategie invalide e NaN.

Primo screening: CO40 class, 16 flop canonici selezionati senza rimpiazzo,
seed `20260919`, otto thread, tutte le continuazioni future enumerate. Nodi:
prima decisione flop di ciascun ingresso e risposte immediate del secondo
giocatore. La scelta dipende dall'albero, non dagli esiti osservati. Si espongono
entrambe le viste di reach e cinque righe con maggiore perdita grezza di
aggregazione per nodo. Gli esempi mostrano almeno uno stato per azione ottima.

La copertura parziale puo sovrastimare il miglioramento della strategia comune:
gli altri stati che condividono la riga non sono ancora inclusi. Un risultato
locale non dimostra sottoallenamento della policy completa. I conflitti
osservati vanno verificati su maggiore copertura prima di decidere la fase 3.

### Screening CO40 e ampliamento prestabilito

Lo screening e completato in 386,8908267 s: 16 flop, 15 nodi, 1.766 righe
osservate su 7.585. Il riepilogo tracciato e
`CO40_BUCKET_SCREEN_2026-09-19.json`; l'identita dell'eseguibile e dei sorgenti
usati e in `DIAGNOSTIC_BUILD_2026-09-19.json`.

La strategia comune subottimale prevale nel campione, ma questo non basta per
concludere sottoallenamento. L'unico ampliamento di copertura previsto usa tutti
i 573 flop e tre nodi: 4, maggiore perdita grezza di separazione di CO nella
vista self-reach; 448, maggiore perdita di CO con prefisso forzato; 226, maggiore
perdita di BTN. Questa selezione precede la misura completa. Gli stessi pesi,
policy e runout sono mantenuti. Il risultato riguarda questi nodi, non tutte le
decisioni del gioco.

Un conflitto osservato e la riga 5039 al nodo 448: K9o preferisce all-in su
8cQcKc con 9dKh, check su JcKdAd con 9cKh. La perdita di separazione della riga,
condizionata al campione e con prefisso forzato, e 0,78896128427 ante. Questo
valore non e la perdita media del nodo e non e un contributo globale additivo.

## 5. Verifica temporale senza reclustering

Il diagnostico ricostruisce, per ogni stato flop/mano, i 31 istogrammi delle
equity al river condizionati ai turn legali. Ciascuno contiene 30 river.
La loro somma deve uguagliare esattamente, bin per bin, due volte l'istogramma
flop esistente di 465 runout non ordinati. La distanza potential-aware risolve
l'assegnamento esatto 31 per 31 con costo EMD fra istogrammi condizionati.
I costi sono interi; il costo primale deve coincidere con quello duale.

Il test confronta 96 assegnamenti di dimensione 1..6 con tutte le permutazioni,
controlla simmetria, invarianza all'ordine, identita e contrazione verso la
marginale. PASS, 0,44 s. I 152 stati degli esempi CO40 ricostruiscono tutti le
marginali. In 154 di 168 coppie la distanza temporale supera quella marginale.
Questo conteggio non prova utilita predittiva: la disuguaglianza e una proprieta
della metrica. Riepilogo: `TEMPORAL_SCREEN_2026-09-19.json`.

Prima di usare questa metrica per un candidato, il controllo decisionale e
fissato come segue: sugli stessi 16 flop e tre nodi selezionati, esportare tutte
le osservazioni, senza scegliere soltanto conflitti. Per ogni osservazione
cercare lo stato piu vicino della stessa riga su un altro flop; trasferire la
sua azione migliore e misurare la perdita nei valori Q della query. Mediare
tutti i vicini a pari distanza e tutte le azioni ottime a pari valore. Confrontare
EMD marginale e distanza temporale, con gli stessi stati, ed esporre sia i pesi
self-reach sia quelli a prefisso forzato. Escludere dai due metodi gli stessi
stati senza un vicino su altro flop e dichiarare la copertura. Una distanza
numericamente maggiore non e un criterio di successo; conta la perdita EV
fuori dal flop usato come vicino. Esiti discordanti restano INCONCLUSIVE.
Il controllo e condizionato a una policy congelata e non sostituisce un solve
del candidato ne un confronto tra seed.

Il gioco enumerabile di controllo ha due tipi equiprobabili, stesso esito
finale Bernoulli(1/2), e una scelta iniziale fra rinuncia a valore zero e acquisto
di un'opzione a costo S/4. Il primo tipo rivela l'esito prima dell'investimento
successivo di S, il secondo no. I valori di acquisto sono +S/4 e -S/4; l'ottimo
originale vale S/8, quello con scelta iniziale condivisa vale zero. Confrontare
questi valori analitici con best response esatta e Linear CFR a 8.000 iterazioni
per S=1,10,40. S e esposizione economica del toy, non uno stack Short Deck.
Il primo fixture e stato rifiutato per foglie irraggiungibili; la correzione
crea solo i terminali usati. Il primo tentativo non aveva avviato CFR.

Il test corretto e PASS (1,14 s). NashConv interna flat: 3,90576e-9 /
3,90576e-8 / 1,5623e-7; perdita nel gioco originale: 0,125 / 1,25 / 5.
NashConv con i tipi distinti: 1,85524e-8 / 1,85524e-7 / 7,42095e-7.
La crescita lineare e imposta dalla scala dei payoff del toy e non quantifica
l'effetto dello stack nel solver Short Deck. Il test controlla il meccanismo
e la separazione fra convergenza astratta ed errore nel gioco originale.

Anche il trasferimento dell'azione ha un oracolo indipendente: Q sintetici
opposti, due vicini a distanza identica su altro flop, pesi frazionari e una
riga senza supporto. La perdita nota e 0,5, con masse coperte 1,85 e 2;
`preflop_blueprint_temporal_controls_oracle.py` e PASS. Gli EV sintetici sono
esclusi dai risultati poker.

## 6. Confronto limitato fra stack

`STACK_ENTRY_SCREEN_2026-09-19.json` confronta lo stesso ingresso limp-check,
la stessa mappa class e gli stessi 16 flop, con i runout futuri enumerati.
HU20: 126,4174919 s, quattro thread; i tempi includono altre verifiche concorrenti.
La perdita di separazione condizionata nella vista self-reach e 0,01198017 ante
per HU20 e 0,00982720 per CO40; con prefisso forzato, 0,00863237 e 0,00611311.
Il campione non mostra un aumento locale monotono. Le masse self-reach sono
molto diverse (443,94 contro circa 16.195 nel campione): cambia anche quanto
la policy frequenta il ramo. Policy, size di risposta preflop e albero futuro
differiscono, quindi il confronto non isola causalmente lo stack.

## 7. Riproduzione dei diagnostici

Tutti i comandi seguenti partono dalla root del worktree; sostituire i percorsi
delle policy storiche con quelli dei propri artefatti verificati.

```powershell
cmake --build --preset windows-release --target gtosd_preflop_blueprint_bucket_diagnostics gtosd_preflop_blueprint_temporal_witness

out/build/windows-release/benchmarks/gtosd_preflop_blueprint_bucket_diagnostics.exe `
  --config benchmarks/fixtures/preflop_blueprint_co40_test_v1.json `
  --resources-dir out/preflop_blueprint_resources `
  --buckets-dir out/preflop_blueprint_buckets_200_500_1000 `
  --policy C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/out/class_20260917/policy.bin `
  --reference-certificate C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/out/class_20260917/cert.json `
  --rows class --flops 573 --seed 20260919 --threads 8 --nodes 4,226,448 `
  --output out/nash_audit/co40_class_full3.json
```

Per il controllo delle feature usare `--flops 16 --export-observations true`
e un output distinto, poi:

```powershell
out/build/windows-release/benchmarks/gtosd_preflop_blueprint_temporal_witness.exe `
  --resources-dir out/preflop_blueprint_resources `
  --buckets-dir out/preflop_blueprint_buckets_200_500_1000 `
  --diagnostics out/nash_audit/co40_class_controls_input.json `
  --output out/nash_audit/co40_temporal_controls.json --mode controls
```

I diagnostici non modificano policy, checkpoint o tabelle. La modalita `class`
ricostruisce soltanto la mappa storica verificata; non e un nuovo trainer.
Un futuro candidato deve avere una nuova identita delle tabelle e un confronto
end-to-end: migliorare le feature flop non ripristina automaticamente la memoria
persa quando il gioco passa ai bucket turn e river.

## 8. Esito della misura completa e decisione sul candidato

La misura completa e PASS come esecuzione: 573 flop, tutti i runout futuri,
7.585 righe per ciascuno dei tre nodi, 3.307,9656516 s. Il risultato diagnostico
e INCONCLUSIVE sulla causa dominante. Dati e identita degli input sono in
`CO40_FULL_BUCKET_AUDIT_2026-09-19.json`.

| Nodo / vista self-reach | Strategia comune, condizionata | Separazione, condizionata | Strategia comune, radice | Separazione, radice |
|---|---:|---:|---:|---:|
| 4, CO limp-check | 0,032694 | 0,042554 | 0,005865 | 0,007634 |
| 226, BTN contro bet 4 | 0,125137 | 0,090742 | 0,007452 | 0,005404 |
| 448, CO open-call | 0,072985 | 0,144682 | 0,000540 | 0,001071 |

Tutti i valori sono in ante. Le colonne radice sono guadagni di una singola
decisione, con il resto del profilo congelato. La normalizzazione della somma
pesata usa `630 * C(34,3) = 3.769.920`: per ogni mano dell'eroe si enumerano i
flop compatibili, e Q integra gia la distribuzione avversaria. La separazione
usa quindi la stessa prior fisica: `C(34,3) * C(31,2) = C(34,2) * C(32,3)`
permette di distribuire il flop prima o dopo la mano avversaria mantenendo
il card removal. La perdita di separazione
e il guadagno aggiuntivo rispetto alla migliore azione condivisa, non rispetto
alla policy originale. Le righe non costituiscono una decomposizione additiva
dell'exploitability globale. Nella vista a prefisso forzato i risultati restano
misti: separazione e strategia comune sono quasi pari al nodo 4, prevale la
strategia comune al 226 e la separazione al 448.

Il replay dei 573 flop gia certificati della stessa policy class richiede
1,2479083 s; riusa una copia byte-identica dello stato e non aggiunge campioni.
EV, BR, guadagni globali e fingerprint coincidono esattamente con il certificato
storico. Per CO/BTN:

| Deviazione | CO | BTN |
|---|---:|---:|
| Completa | 0,500181 | 0,194539 |
| Postflop, preflop dell'eroe congelato | 0,180378 | 0,148785 |
| Preflop, continuazione media congelata | 0,007291 | 0,003115 |

Il divario fra deviazione completa e deviazioni separate non e una quota
causale attribuibile a uno specifico errore. Mostra che cambiare insieme
percorso preflop e gioco successivo produce opportunita assenti nei controlli
separati. I tre nodi flop misurati non spiegano da soli il guadagno globale.
Corretto anche un commento di `BestResponseReport`: enumerare tutti i flop
elimina l'errore di campionamento, ma non rende `best_response_lower` uguale
alla BR libera, perche il primo mantiene il preflop dell'eroe congelato.
Il calcolo era gia distinto e corretto; la correzione riguarda il commento.

Il controllo delle feature e completato sui 16 flop prestabiliti. La riesecuzione
per esportare tutte le osservazioni riproduce esattamente i precedenti valori
dei tre nodi. Ogni nodo ha 8.448 osservazioni, delle quali 2.711 hanno un vicino
della stessa riga su un altro flop. Si calcolano 9.958 distanze uniche. Il report
`TEMPORAL_CONTROLS_2026-09-19.json` espone anche gli aggregati per flop.

| Nodo / vista self-reach | Massa coperta | Perdita con distanza marginale | Perdita con distanza temporale |
|---|---:|---:|---:|
| 4 | 34,00% | 0,091052 | 0,095658 |
| 226 | 33,02% | 0,203133 | 0,208895 |
| 448 | 33,46% | 0,315688 | 0,308985 |

La direzione e la stessa con prefisso forzato: peggioramento nei primi due nodi,
piccolo miglioramento nel terzo. Queste perdite misurano il trasferimento locale
delle azioni nel campione supportato, non una strategia allenata o NashConv.
La copertura limitata impedisce di bocciare ogni possibile metodo potential-aware;
il risultato non giustifica pero la promozione di questo candidato.

Decisione: applicare il criterio di arresto concordato. Dopo l'unico ampliamento
completo non emerge una causa dominante unica; il controllo della metrica non
mostra un miglioramento coerente. Non costruire nuove tabelle e non lanciare i
tre seed HU20/CO40. Non sono emersi divieti documentali da sottoporre all'utente:
l'arresto dipende dall'evidenza sperimentale insufficiente.

Questo audit corregge il diagnostico e fornisce strumenti riproducibili, ma non
riduce la deviazione Nash della policy salvata. CO40 resta non qualificato a
0,500181 ante rispetto al gate di 0,03. Non sono dimostrati ne un limite
asintotico della rappresentazione ne l'impossibilita di raggiungere il gate.

Il valore 0,500181 e il guadagno della BR fisica contro la policy astratta
sollevata nel gioco originale. Non misura direttamente NashConv del gioco
vincolato ai bucket. La verifica successiva piu utile, in un protocollo distinto,
e una deviazione congiunta vincolata alla rappresentazione, validata prima su
un gioco ridotto enumerabile. I miglioramenti di singoli nodi qui misurati
sono soltanto limiti inferiori a quella possibilita di deviazione.

## 9. Validazione finale e consegna

Correzione della normalizzazione: commit `17984a9`. Diagnostici, import class,
test e risultati: commit `04ba03a`. Branch `codex/nash-convergence-audit`.
Il checkout principale e pulito; nessuna nuova policy o tabella e stata prodotta.

MSVC 19.51.36248.0, C++20, `/W4 /WX`. CPU Intel Core i3-10100F, otto thread
logici. Le durate riportate comprendono verifiche concorrenti e non sono un
confronto di throughput fra versioni del solver.

| Suite ASAN (`windows-asan`) | Esito | Durata |
|---|---|---:|
| Kernel | PASS | 97,03 s |
| Trainer | PASS | 1.969,11 s |
| Certificatore | PASS | 511,69 s |
| Export | PASS | 543,92 s |
| Distanza temporale | PASS | 0,30 s |
| Scomposizione decisionale | PASS | 0,26 s |

Totale CTest ASAN: sei suite, zero fallimenti, 3.123,45 s. Nessun errore di
memoria segnalato. Il preset Release completa anche i quattro controlli
decision-gap, distanza temporale, gioco temporale e oracolo del trasferimento
(6,19 s complessivi in quella passata). Dopo la revisione degli input, l'oracolo
del trasferimento passa nuovamente e verifica anche il rifiuto di EV vuoti,
indice combo 65.536 e reach negativa. La build corretta precede ciascuna
esecuzione valida; i tentativi falliti del fixture e delle prime build restano
nel diario. La formattazione finale e i commenti non cambiano il calcolo.

I sette riepiloghi JSON dell'audit sono validi. Il diff supera il controllo
whitespace; i nuovi C++ e le righe modificate sono formattati con clang-format.
Policy e certificati storici sono rimasti in lettura; i replay hanno usato copie
locali dello stato. Non sono stati eseguiti training del candidato, confronti a
tre seed, modifiche di regole o size, oppure reclustering durante il training.
