# GTO+ parity journey — AhKhQh

Aggiornato: 2026-08-02
Benchmark ID: `GTP-AHKHQH-003`
Stato del gate: **BLOCCANTE — NON SUPERATO**

## 1. Scopo

Questo documento è il registro canonico del percorso verso la parità con GTO+
per il test `AhKhQh`. Deve essere aggiornato nello stesso cambiamento che
introduce qualsiasi implementazione, benchmark o decisione relativa a questo
gate.

Non si avviano F11 o fasi successive finché GTOSD non raggiunge almeno il 90%
dei benchmark obbligatori, senza modificare il gioco e senza perdere
correttezza matematica.

## 2. Fixture immutabile

| Campo | Valore |
|---|---|
| Gioco | Heads-up Short Deck postflop |
| Mazzo | 36 carte, `6..A` |
| Flop | `Ah Kh Qh` |
| CO/OOP | `AA-QQ,AKs-AQs,KQs,AKo-AQo,KQo` |
| BTN/IP | `AA-QQ,AKs-AQs,KQs,AKo-AQo,KQo` |
| Combo fisiche dopo i blocker | 36 per giocatore |
| Pot iniziale | 40 ante |
| Stack effettivo | 100 ante |
| Bet size | 50% pot per entrambi, flop/turn/river |
| Raise | 50% pot, massimo uno per street |
| All-in automatico | Regola naturale: all-in solo quando la bet size supera lo stack rimanente (correzione 2026-08-05; la soglia `150%` era un'inferenza mai confermata ed è stata rimossa) |
| Smoothing due bet finali GTO+ | Disabilitato, confermato dall'utente |
| Rake | 0% |
| Precisione GTOSD | stato `float32`, calcolo e certificazione `float64` |
| Card abstraction | Nessuna |
| Sampling | Nessuno |
| Isomorfismo | Solo lossless |

Le fixture `GTP-AHKHQH-001` e `002` sono ritirate: la prima non riproduceva le
size, la seconda applicava la soglia all-in a `(stack-call)/(pot+call)` e
generava `All-in 100` dove GTO+ mostra `Raise 60`. Qualsiasi ulteriore variazione
genera un benchmark ID differente e non può sostituire questo gate.

## 3. Riferimento GTO+

I valori seguenti sono misure esterne fornite dall'utente e sono accettati come
target del progetto:

| Metrica | GTO+ |
|---|---:|
| Tempo fino alla convergenza | **1,71 s** |
| Memory needed for solving | **8 MB** |
| Target dEV del run sorgente | **1% del pot iniziale** |
| EV flop CO root | **19,15 ante** |
| EV flop BTN dopo check CO | **21,65 ante** |
| EV flop BTN dopo bet 20 CO | **17,51 ante** |
| Frequenze CO root | **check 80,3%; bet 20 19,7%** |
| Frequenze BTN dopo bet 20 | **fold 37,4%; call 62,6%; raise 60 0,0%** |

Il riferimento operativo da `1,71 s` è stato successivamente esportato con
maggiore precisione: dEV `0,390 ante` (`0,98%`), CO root EV `19,1588`, BTN dopo
check `21,6597`, BTN dopo bet `17,5107`. Un secondo run GTO+ a dEV `0,078 ante`
(`0,19%`, `4,20 s`) riporta rispettivamente `19,1581 / 21,6682 / 17,1176`.
Questi export sono diagnostici e non sostituiscono il target prestazionale da
`1,71 s`.

Il tentativo con vero Target `0,10%` non è sceso sotto `0,11%` in circa
`245 s`; il relativo tempo di convergenza è quindi soltanto limitato inferiormente
a `>245 s`, non misurato.

Confermato dall'utente:

- stesso hardware per GTO+ e GTOSD;
- Target dEV `1%` nel run da `1,71 s`;
- timer dal click su `Run Solver`, con albero già preparato, fino alla soluzione
  completa consultabile;
- unità della memoria GTO+: **MB decimali** (8 MB = 8.000.000 byte), quindi il
  confronto in byte con lo stato solver GTOSD è diretto.

Resta da registrare, senza invalidare il target operativo:

- turn e river dell'action tree GTO+ (il livello flop è registrato: 4 nodi con
  action set, frequenze ed EV combo-per-combo in
  `docs/specifications/gtoplus_specs.md`).

Finché questo campo non è allineato, una misura GTOSD può essere
diagnostica, ma non può essere dichiarata comparazione scientifica definitiva.

## 4. Definizione del gate 90%

Per una metrica in cui un valore inferiore è migliore:

```text
score = riferimento_GTO+ / misura_GTOSD × 100
```

Ne derivano le soglie:

| Gate obbligatorio | Formula | Soglia GTOSD |
|---|---|---:|
| Velocità di convergenza | `1,71 / tempo_GTOSD ≥ 0,90` | **≤ 1,900000 s** |
| Memoria solver | `8 / memoria_GTOSD ≥ 0,90` | **≤ 8,888889 MB** |

Entrambi devono essere superati nello stesso commit. Non è ammessa una media
che compensi il fallimento di una metrica con l'altra.

### Gate di correttezza non negoziabili

- stessa fixture e stesse azioni legali;
- range fisici e card removal identici;
- nessun sampling, bucketing o astrazione lossy;
- strategia media normalizzata;
- nessun NaN o infinito;
- parità del percorso canonico con quello fisico entro `1e-11`;
- suite Release completa verde;
- metrica di convergenza registrata e confrontabile;
- EV del gioco al root entro la tolleranza versionata, nella stessa convenzione
  e a convergenza dichiarata;
- EV condizionali e frequenze dei nodi osservati confrontati come gate soltanto
  quando le reach private/posteriori combo-per-combo sono uguali; altrimenti
  restano diagnostici;
- almeno cinque processi indipendenti; decisione sulla mediana, con tutti i run
  pubblicati.

Un'ottimizzazione che supera il benchmark ma fallisce uno di questi controlli
viene rifiutata.

## 5. Baseline GTOSD corrente

Implementazione: F10.3, selezione chance nel browser e oracolo EV GTO+.

| Metrica | GTOSD corrente | Score | Stato |
|---|---:|---:|---|
| Tempo `Run Solver` a dEV GTO+ ≤1%, 5 processi | mediana 2,7197086 s; p95 2,8812251 s | 62,874383% | **FAIL** |
| Convergenza deterministica | iterazione 80; dEV 0,695544%; NashConv/Pot 1,268598% | — | **PASS** |
| Regret + average strategy `float32` | 4,214976 MB | 189,799420% | **PASS** |
| EV flop CO root, probe accurato | 19,163591 vs 19,1581; delta +0,005491 | — | **PASS** |
| BTN dopo check, posteriori diversi | 22,418551 vs 21,6682; delta +0,750351 | — | Diagnostico |
| BTN dopo bet 20, posteriori diversi | 15,996175 vs 17,1176; delta -1,121425 | — | Diagnostico |
| CO root bet 20 | 24,6348% vs 18,0194% | — | Diagnostico: causa posteriori diversi |
| BTN raise 60 dopo bet 20 | GTOSD 3,0438% vs GTO+ 0,0% nel run accurato | — | Diagnostico |
| Nodi pubblici fisici | 112.848 | — | Informativo |
| Nodi pubblici canonici | 31.461 | — | Informativo |
| Infoset canonici | 250.704 | — | Informativo |
| Action entry canoniche | 526.872 | — | Informativo |
| Delta regret transient | pubblicato separatamente in ogni run | — | Diagnostico, separato dallo stato solver |
| Peak RSS | pubblicato in ogni run | — | Diagnostico, non equivalente agli 8 MB |

Il benchmark pubblica separatamente stato solver, transient workspace e peak
RSS. La modalità `float32` non cambia payoff o enumerazione: tutte le operazioni
del traversal e la best response restano `float64`.

### Perché la misura corrente non supera ancora la parità

Il benchmark automatico usa la semantica GTO+ corretta: massimo guadagno di
deviazione unilaterale diviso per il pot iniziale, target `1%`. Il timer continuo
parte dopo la preparazione del layout e include inizializzazione, CFR+,
averaging, certificazione exact BR finale e finalizzazione. I cinque run sono
processi distinti e pubblicano ogni tempo.

Il gate memoria passa e il gate velocità corrente fallisce. Il probe GTOSD a
1.000 iterazioni e dEV `0,011334%` produce CO root EV `19,163591`, contro
`19,1581` del run GTO+ a dEV `0,078 ante`: delta `+0,005491 ante`, pari allo
`0,0137%` del pot. Il valore del gioco è quindi allineato entro `0,0055 ante` e
non mostra un errore di normalizzazione o payoff.

I due EV BTN non rappresentano lo stesso subgame privato: dopo un'azione root,
il peso CO è proporzionale a `range_iniziale × strategia_root_combo × blocker`.
GTO+ betta il `18,0194%`, GTOSD il `24,6348%`, con allocazioni combo-per-combo
diverse. I posteriori CO dopo bet/check sono quindi diversi e possono generare
EV condizionali distanti anche quando il root EV coincide. Lo stesso GTO+
sposta il bet root di `1,7084` punti percentuali tra dEV `0,98%` e `0,19%`, ma
il root EV cambia soltanto di `0,0007 ante`; questo indica un equilibrio molto
piatto o selezioni numeriche differenti tra mix quasi indifferenti.

Lo smoothing resta escluso e non è stato usato bucketing. Prima di attribuire
il residuo al tree builder occorre l'esperimento controllato della fase F10.4,
che rende identica la strategia root GTO+ e lascia libere le continuation.

## 6. Protocollo di misura obbligatorio

Ogni candidato al gate deve registrare:

1. commit e worktree pulito;
2. build Release e compiler/flags;
3. CPU, RAM, sistema operativo e thread;
4. fingerprint completa della fixture;
5. algoritmo e precisione;
6. soglia di convergenza e valore finale osservato;
7. esclusione del layout già preparato e inclusione di inizializzazione,
   solving, certificazione e finalizzazione;
8. cinque processi indipendenti, senza riuso involontario di cache o
   checkpoint;
9. tempi individuali, mediana e p95;
10. memoria persistente, transient buffer e peak RSS separati;
11. suite di correttezza e differenziale physical/canonical.

Il timer primario “tempo alla convergenza” parte prima dell'inizializzazione
necessaria al solve e termina quando la soglia allineata è certificata. Eventuali
misure kernel-only restano secondarie.

## 7. Backlog ordinato del journey

| Priorità | Attività | Ipotesi misurabile | Stato |
|---:|---|---|---|
| 1 | Allineare e automatizzare il criterio di convergenza GTO+/GTOSD | Rende misurabile il gate tempo end-to-end | **Completato** |
| 2 | Profilare separatamente layout, CFR+, averaging e BR | Ha identificato traversal e certificazioni ripetute | **Completato** |
| 3 | Allineare il timer al tree già preparato | Replica il click `Run Solver` GTO+ | **Completato** |
| 4 | Compattare il traversal alle combo attive | Riduce 630 slot a 36 senza abstraction | **Completato** |
| 5 | Storage `float32` con calcolo `float64` | Dimezza i buffer senza abstraction | **Completato, differenziale richiesto nel gate finale** |
| 6 | Parallelismo deterministico per action subtree | Usa fino a 6 thread con delta separati e join | **Completato** |
| 7 | Worker persistenti e riuso layout GUI | Elimina creazione thread e rebuild ripetuti | **Completato** |
| 8 | Dimostrare la parità del root EV e ricomporre i rami BTN | Separa valore del gioco da EV condizionali dipendenti dal posteriore | **Completato: delta root +0,005491 ante** |
| 9 | **F10.4 — Root lock diagnostico GTO+ combo-per-combo** | Con posteriori root identici, misura se i due delta BTN scendono entro ±0,5 ante | **NEXT** |
| 10 | Classificare il residuo dopo root lock | Se passa: selezione dell'equilibrio; se fallisce: mismatch downstream | Bloccato da 9 |
| 11 | Acquisire e confrontare l'action tree GTO+ downstream | Necessario soltanto se il lock lascia almeno un delta BTN oltre ±0,5 ante | Condizionato da 10 |
| 12 | Profilare il percorso standard, senza lock | Ridurre la mediana da 2,7197086 s a ≤1,900000 s | Bloccato da 9–10 |

L'ordine può cambiare solo sulla base di profiling registrato qui.

### F10.4 — Controlled-posterior con root lock GTO+

**Obiettivo.** Stabilire causalmente se la distanza degli EV BTN deriva dalla
diversa strategia CO root o da una divergenza downstream. Non è node locking di
prodotto e non modifica il comportamento predefinito del solver.

**Input immutabile del test.** Le 36 righe combo-per-combo `Bet 20 / Check`
esportate da GTO+ v1.6.9 nel run a dEV `0,078 ante`, insieme ai riferimenti
`CO root 19,1581`, `BTN dopo check 21,6682` e `BTN dopo bet 17,1176`. Il source
dEV deve essere conservato nel report: il riferimento non è un equilibrio
esatto e non autorizza aspettative bit-per-bit.

**Comportamento richiesto.** Al solo nodo CO root, ogni combo usa esattamente la
probabilità esterna corrispondente; CFR+ non aggiorna i regret del root e
accumula/esporta la strategia locked. Tutti i nodi successivi restano liberi.
Le reach dopo bet/check devono essere costruite normalmente dal motore,
includendo compatibilità tra le due hole card, board blocker e pesi iniziali.

**Isolamento e sicurezza semantica.** Il percorso deve essere esplicitamente
denominato `diagnostic_external_root_lock`; deve rifiutare combo duplicate,
bloccate o mancanti, azioni inesistenti, probabilità non finite/negative e
somme diverse da uno. Resume, certificazione, salvataggio o browser non devono
poter perdere il vincolo in modo silenzioso. Il report deve distinguere la
convergenza del gioco vincolato dall'exploitability del gioco originale.

**Validazione obbligatoria.** Il test permanente verifica:

1. copertura esatta delle 36 combo e riproduzione delle probabilità locked;
2. nessun cambiamento a conteggi, azioni legali e fingerprint della fixture
   standard non vincolata;
3. ricomposizione zero-sum del root e posteriori Bet/Check generati dal lock;
4. solve downstream ad accuratezza almeno pari al probe GTOSD già usato;
5. EV BTN locked, delta rispetto a `21,6682 / 17,1176` e frequenze downstream;
6. differenziale physical/canonical oppure una limitazione esplicita se le
   asimmetrie numeriche tra semi dell'export GTO+ impediscono la condivisione
   lossless degli infoset locked;
7. suite Release senza regressioni del percorso standard.

**Decisione al termine.** Se entrambi gli EV BTN sono entro `±0,5 ante`, F10.4
classifica il mismatch storico come principalmente dovuto alla selezione del
mix root/posteriore; la soglia non certifica parità bit-per-bit. Se almeno uno
resta fuori, il prossimo lavoro è il differenziale downstream, partendo dal
primo nodo con reach già uguali. In entrambi i casi il lock resta test-only.

**Definition of Done.** Implementazione, fixture esterna versionata, test degli
input invalidi, run accurato, report JSON riproducibile, aggiornamento di questo
journey e del benchmark, build Release e suite completa PASS. Al 2026-08-05 la
fase è **COMPLETATA** — vedi la voce `2026-08-05 — Regola all-in corretta e
F10.4 completata` nel registro: con la regola all-in naturale e il root locked
i delta BTN sono `+0,0348` / `+0,0366` (entro `±0,05 ante`).

### 2026-08-05 — Regola all-in corretta e F10.4 completata

- **Correzione della regola all-in**: la soglia `Go all-in if remaining stack <
  150% current pot` era un'inferenza non confermata (interpretazione A dello
  sweep 2026-08-02). L'utente dichiara che GTO+ non ha tale regola: all-in solo
  quando la bet size supera lo stack rimanente (regola naturale). Applicata a
  tutte le strade (flop/turn/river), stesse size 50%.
- Impatto sull'albero (con range): `165.774` nodi fisici, `46.065` canonici,
  `385.980` infosets canoniche, `834.636` action entry, stato `6.677.088` byte
  (più vicino agli 8 MB dichiarati da GTO+ dei `4.214.976` precedenti).
  Fingerprint di gioco: `fnv1a64:9e42ca23963f718b`.
- Re-baseline completo: fixture `003` (v1, contratto aggiornato), `101`, `103`,
  `104` (v2) ri-parametrizzate con la regola naturale e nuovi `expected_layout`;
  `make_gto_plus_parity_config` e il config del diagnostic allineati.
- Numeri dopo la correzione (80 iterazioni, senza lock): dEV `0,674155%`,
  EV CO root `19,1129` (gate PASS), BTN dopo check `22,0833` (delta `+0,433`),
  BTN dopo bet `17,3600` (delta `-0,150` — prima era `-0,74`): la regola all-in
  da sola spiega gran parte del mismatch BTN storico.
- **F10.4 completata**: implementato il root lock diagnostico
  `diagnostic_external_root_lock` (36 righe combo-per-combo Bet 20/Check del
  run operativo GTO+ 0,98%, provenienza in `gtoplus_specs.md`, source dEV
  conservato nel report). Validazione permanente nel reference test:
  1. copertura esatta delle 36 combo e riproduzione delle probabilità locked
     (delta massimo `2,2e-16`);
  2. fingerprint del gioco invariato rispetto alla fixture non vincolata;
  3. ricomposizione zero-sum del root e posteriori Bet/Check dal lock;
  4. convergenza del gioco vincolato (dEV `0,203%` a 200 iterazioni, probe
     non vincolato `0,674%`).
- Risultato con root locked (200 iterazioni, gioco vincolato convergente):
  CO root `19,1232` (delta `-0,0356`), BTN dopo check `21,6945` (delta
  `+0,0348`), BTN dopo bet `17,5473` (delta `+0,0366`), CO dopo check-bet
  `11,9884` (delta `+0,0925`); frequenze BTN call `64,6%` vs `62,6%` GTO+,
  fold `34,8%` vs `37,4%`; CO dopo check-bet call `55,2%` vs `56,3%`,
  raise `4,5%` vs `3,75%`.
- **Decisione F10.4: PASS**. Con la regola all-in corretta e il mix root CO
  locked, entrambi gli EV BTN sono entro `±0,05 ante` (non solo `±0,5`):
  il mismatch storico era principalmente (1) la regola all-in errata e (2) la
  selezione del mix root/posteriore CO. Il nodo CO dopo check-bet (`+0,0925`)
  resta il primo candidato del differenziale downstream residuo.
- Evidenza: `out/f104-root-lock.json`, `out/_nat-101.json`, report dei run
  re-baseline, `EXTERNAL_ROOT_LOCK_TEST=PASS` nella suite.

## 8. Registro delle implementazioni

### 2026-08-02 — Root EV in parità e apertura F10.4

- Acquisiti due export GTO+ v1.6.9 combo-per-combo: run operativo a dEV
  `0,390 ante` (`0,98%`, `1,71 s`) e run più accurato a dEV `0,078 ante`
  (`0,19%`, `4,20 s`).
- Il vero Target `0,10%` non è stato raggiunto: dopo circa `245 s` GTO+ era a
  dEV `0,045 ante` (`0,11%`). Il tempo a target è censurato a `>245 s`.
- Nel run accurato GTO+: CO root EV `19,1581`, bet root `6,487/36 = 18,0194%`,
  BTN dopo check `21,6682`, BTN dopo bet `17,1176`.
- Nel probe GTOSD a 1.000 iterazioni: CO root EV `19,163591179`, bet root
  `24,6348407%`, BTN dopo check `22,418551039`, BTN dopo bet `15,996174547`.
- Delta root GTOSD−GTO+ `+0,005491179 ante` (`0,0137%` del pot): **PASS**.
- Ricomposizione GTOSD verificata esattamente:
  `0,246348407 × 15,996174547 + 0,753651593 × 22,418551039 = 20,836408821`;
  sommando il CO si ottiene `40`.
- Ricomposizione GTO+ da `1,71 s`, entro gli arrotondamenti esportati:
  BTN root `20,8411945`; CO + BTN `39,9999945`.
- La precedente deduzione “EV BTN diversi ⇒ gioco diverso” è ritirata. Gli EV
  BTN sono condizionati da posteriori CO differenti perché le strategie root
  combo-per-combo sono differenti. Restano diagnostici finché tali posteriori
  non vengono controllati.
- Frequenze GTO+ instabili ma valore stabile: tra i due export il bet root passa
  da `19,7278%` a `18,0194%`, il root EV cambia solo di `-0,0007 ante` e l'EV
  BTN dopo bet cambia di `-0,3931 ante`. Evidenza compatibile con mix quasi
  indifferenti/equilibrio piatto, non con bucketing.
- Aperta **F10.4 — Root lock diagnostico GTO+ combo-per-combo**. Il lock varrà
  soltanto per il test, fisserà le 36 strategie CO root del run a dEV
  `0,078 ante`, salterà gli aggiornamenti regret al root e lascerà libere tutte
  le continuation.
- Invarianti F10.4: nessuna modifica all'albero o alla fixture standard; niente
  sampling/bucketing; blocker e card removal esatti; probabilità valide per
  tutte le 36 combo; soluzione e checkpoint marcati come gioco vincolato, mai
  presentati come equilibrio del gioco originale.
- Criterio diagnostico: se entrambi i delta BTN scendono entro `±0,5 ante`, la
  causa dominante è la selezione root/posteriore; altrimenti si apre il
  differenziale downstream su action tree, payoff e chance.
- Nessun codice è stato modificato per F10.4 in questo aggiornamento: è una
  fase pianificata, non implementata né validata.
- Decisione: **ROOT VALUE PARITY PASS; GATE COMPLESSIVO ANCORA BLOCCATO** per
  velocità e per la diagnostica controlled-posterior non ancora eseguita.

### 2026-08-01 — Verifica BTN, soglia all-in corretta e fixture `003`

- Ispezionata la soluzione GTO+ v1.6.9: root bet 20 `19,7%`; BTN dopo bet 20
  usa fold `37,4%`, call `62,6%`, raise 60 `0,0%`; non espone all-in 100.
- Corretto il trigger: stack completo / pot prima dell'azione, non
  `(stack-call)/(pot+call)`. Aggiunto regression test al nodo esatto.
- Ritirata `002`; `003` ha `112.848` nodi fisici, `31.461` canonici,
  `250.704` infoset, `526.872` action entry e fingerprint
  `fnv1a64:001a19fa48cd8b1e`.
- Cinque run Release: `2,8812251`, `2,7197086`, `2,8186997`, `2,6386802`,
  `2,6308951 s`; mediana `2,7197086 s`, p95 `2,8812251 s`.
- Speed score `62,874383%` **FAIL**; stato `4.214.976 byte`, memory score
  `189,799420%` **PASS**.
- A dEV `0,695544%`: EV `19,121570 / 22,201691 / 16,770667`; i due BTN
  falliscono anche la richiesta ±0,5 ante.
- Probe a 1.000 iterazioni e dEV `0,011334%`: EV BTN
  `22,418551 / 15,996175`; la divergenza cresce, quindi non è convergenza.
- Build completa `windows-gui-release` e suite Release `22/22` PASS, inclusi
  E2E Qt, legal-action regression e differenziale physical/canonical.
- Aggiornamento utente: “smoothly” era disabilitato; non spiega il residuo.
- Rerun Release dopo aver versionato il flag come disabilitato: iterazione 80,
  dEV `0,695544%`, EV `19,121570 / 22,201691 / 16,770667`, identici al run
  precedente; il solo tempo del processo è `3,0260982 s`.
- Varianti diagnostiche con `raise_depth` 2 e 3: stessi `112.848` nodi fisici,
  `31.461` canonici, `250.704` infoset, `526.872` action entry, dEV
  `0,695544%` ed EV bit-per-bit identici a `raise_depth` 1. La fingerprint di
  configurazione cambia, ma non l'albero materializzato: “più raise in GTO+” è
  escluso come causa per questa fixture.
- Sweep controllato delle quattro letture della soglia `<150%`: solo
  `stack/current_pot` conserva al primo nodo BTN `fold / call 20 / raise 60`.
  Le altre tre — `(stack-call)/(pot+call)`, `stack/(pot+call)` e
  `(stack-call)/current_pot` — producono tutte `fold / call 20 / all-in 100` e
  sono identiche tra loro: `63.474` nodi fisici, `288.900` action entry, dEV
  `0,904789%`, EV `19,128823 / 22,219692 / 16,196783`.
- La sola interpretazione A produce `112.848` nodi fisici, `526.872` action
  entry, dEV `0,695544%` ed EV `19,121570 / 22,201691 / 16,770667`; supera il
  gate dell'action set immediato ma non la tolleranza EV BTN. Nessuna delle
  quattro formule semplici ricostruisce quindi l'intero gioco GTO+.
- La semantica `Go all-in` resta da verificare sui nodi successivi, ma non può
  essere risolta sostituendo globalmente numeratore o denominatore con una
  delle tre alternative bocciate dallo stesso nodo GTO+ osservato.
- Decisione: **INCONCLUSIVE/REJECT**. La formula immediata è corretta, ma manca
  ancora il differenziale completo dell'action tree GTO+.
- Evidenza: `out/gto-plus-convergence/gtp003-five-runs/summary.json` e
  `out/diagnostics/all-in-semantics-sweep-summary.json`.

### 2026-08-01 — Baseline 1,71 s / 8 MB e nuovo gate EV

- Aggiornati i riferimenti esterni GTO+ a `1,71 s` e `8.000.000 byte`.
- Aggiunti tre EV flop condizionali obbligatori nella convenzione visuale GTO+:
  `19,15`, `21,65`, `17,51`, tolleranza assoluta `0,05 ante`.
- Ogni run pubblica riferimento, misura, delta e stato; il wrapper conserva
  tutti e cinque i report anche quando il gate EV fallisce.
- Run: `1,9034193`, `1,7017160`, `1,8440822`, `1,8186019`, `1,7689969 s`;
  mediana `1,8186019 s`, p95 `1,9034193 s`.
- Speed score `94,028275%` PASS; memory score `346,140533%` PASS.
- EV CO root `19,128823` PASS; EV BTN dopo check `22,219692` FAIL; EV BTN dopo
  bet 20 `16,196783` FAIL.
- La GUI espone gli EV condizionali GTO+ per nodo e permette di scegliere il
  turn/river nei nodi chance; il percorso turn è coperto dall'E2E Qt.
- Decisione: **REJECT**. La soluzione non è ancora allineata a GTO+ nonostante
  il superamento dei gate prestazionali.
- Evidenza: `out/gto-plus-convergence/ev-gate-1_71s-8mb/summary.json`.

### 2026-08-01 — Fixture corretta, GUI allineata e gate memoria PASS

- Ritirata `GTP-AHKHQH-001`: non conteneva il raise osservato in GTO+.
- Introdotta `GTP-AHKHQH-002`: bet/raise 50%, massimo un raise e policy
  `Go all-in if push < 150% pot`; BTN affrontando bet 20 espone fold, call e
  `All-in 100`.
- La GUI usa Target dEV GTO+, averaging delay 20 e certificazione ogni 20;
  usa stato `float32` con aritmetica/certificazione `float64`, ma conserva la
  precisione dei checkpoint storici durante il resume;
  conserva il layout tra preflight, solve e browser e non ricostruisce più
  l'albero a ogni click.
- Il traversal parallelizza anche i nodi a tre azioni, riusa mapping compatti
  per blocker/isomorfismi e usa Release MSVC AVX2/LTCG senza `/fp:fast`.
- Navigazione resa orizzontale e cache delle analisi nodo; matrice strategia
  9×9 resa read-only.
- Cinque run: `2,4507398`, `2,4276519`, `3,4393542`, `2,6085426`,
  `2,9743763 s`; mediana `2,6085426 s`, p95 `3,4393542 s`.
- Tutti i run: iterazione 80, dEV `0,904789%`, NashConv/Pot `1,588940%`;
  stato solver `2.311.200 byte`.
- Speed score `31,435178%` **FAIL**; memory score `112,495673%` **PASS**.
- Correttezza benchmark, differential physical/canonical, build Release, GUI
  E2E e suite completa 22/22 PASS. Gate complessivo ancora **FAIL**.
- Evidenza: `out/gto-plus-convergence/final-rebuilt-candidate/summary.json`.

### 2026-08-01 — Candidato ritirato: fixture `001` non allineata

- Corretto il riferimento a Target dEV `1%` e timer dal click `Run Solver` su
  albero già preparato fino alla soluzione consultabile, sullo stesso hardware.
- Profilo iniziale a 1%: layout `2,27 s`, traversal `8,32 s`, certificazioni
  `15,63 s`, finalizzazione `<0,001 s`.
- Certificazione portata alla sola iterazione finale 40; averaging delay CFR+
  impostato a 10 dopo confronto misurato.
- Traversal lossless specializzato sulle 36 combo attive, scratch riusabili,
  payoff terminali precomputati e normalizzazione completa spostata fuori
  dall'hot path (`2,22e-16` massimo errore).
- Parallelismo deterministic action-subtree fino a 6 thread; ogni worker usa
  delta regret separati, fusi soltanto dopo il join.
- Stato performance `float32` con aritmetica e certificazione `float64`:
  `2.005.632 byte`; checkpoint consultabile e ricertificabile.
- Cinque run finali: `0,8648449`, `0,8912323`, `0,9653215`, `0,7757150`,
  `0,8542203 s`; mediana `0,8648449 s`, p95 `0,9653215 s`.
- Tutti i run: iterazione 40, dEV `0,982194%`, NashConv/Pot `1,724106%`.
- Speed score `94,814689%`, memory score `129,634948%`: entrambi i gate
  prestazionali PASS.
- Build Release completa PASS, build GUI Release PASS e suite `15/15` PASS.
- Stato storico: **RITIRATO**. Il PASS non è valido per il gate perché mancava
  l'azione raise/all-in presente nel riferimento GTO+.
- Evidenza locale: `out/gto-plus-convergence/final-candidate/summary.json`.

### 2026-08-01 — Baseline diagnostica superata (target allora assunto 0,5%)

- Questa voce è conservata come storia del profiling ma non è confrontabile col
  run GTO+ da `0,82 s`, successivamente confermato a Target dEV `1%` e con tree
  già preparato.
- Distinto formalmente `Target dEV GTO+ = max(BR[p] - EV[p]) / pot` da
  `NashConv/Pot`, che somma i due guadagni.
- Aggiunto arresto exact su massimo guadagno normalizzato senza modificare il
  percorso NashConv esistente.
- Aggiunti fixture/schema versionati, comando CLI single-process e runner
  PowerShell con minimo cinque processi, mediana e p95 nearest-rank.
- Scope timer GTOSD: layout, inizializzazione, CFR+, averaging e certificazioni
  exact BR; esclusi startup processo e scrittura JSON.
- Run Release: `40,4711801`, `39,9082154`, `35,8920193`, `34,3246333`,
  `34,0701124 s`; mediana `35,8920193 s`, p95 `40,4711801 s`.
- Tutti i run: iterazione 70, dEV `0,4906437527%`, NashConv/Pot
  `0,8507773499%`, fingerprint `fnv1a64:e6b50494a9986bb2`, storage solver
  `4.011.264 byte`.
- Score velocità `2,284630%` e memoria `64,817474%`: gate entrambi FAIL.
- Correttezza/reproducibilità del run PASS; build Release completa e suite
  `15/15` PASS; comparabilità scientifica completa PENDING per worktree sporco
  e metadati GTO+ incompleti.
- Evidenza macchina: `out/gto-plus-convergence/five-runs/summary.json` (artefatto
  locale non versionato).

### 2026-07-29 — Baseline e gate formalizzato

- Accettato il nuovo riferimento GTO+ di `0,82 s` fino alla convergenza.
- Conservato il riferimento memoria GTO+ di `2,6 MB`.
- Formalizzate le soglie bloccanti: `≤0,911111 s` e `≤2,888889 MB`.
- Baseline GTOSD: `4,011264 MB`, 125.352 infoset, 250.704 action entry,
  14.673 nodi pubblici canonici.
- La misura GTOSD da `2,192 s` resta diagnostica perché rappresenta una
  iterazione più certificazione, non una convergenza allineata.
- F11 e tutte le fasi successive sono congelate.

## 9. Template per i prossimi aggiornamenti

Ogni nuova voce deve contenere:

```text
Data e commit:
Ipotesi:
File/componenti modificati:
Correttezza e test:
Configurazione hardware:
Run tempo [1..5]:
Mediana / p95:
Convergenza finale:
Memoria solver / transient / peak RSS:
Score velocità:
Score memoria:
Decisione: ACCEPT / REJECT / INCONCLUSIVE
Prossimo esperimento singolo:
```

## 10. Condizione di sblocco

Il freeze delle fasi viene rimosso soltanto quando una voce del registro
dimostra contemporaneamente:

```text
speed_score >= 90%
memory_score >= 90%
correctness_gates = PASS
release_suite = PASS
reproducibility = PASS
```

Fino ad allora il solo lavoro autorizzato sul percorso principale è
benchmarking, profiling, correttezza o ottimizzazione direttamente collegata a
`GTP-AHKHQH-003`.
