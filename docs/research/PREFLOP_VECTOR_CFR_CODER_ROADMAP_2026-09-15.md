# Roadmap operativa per l'agent coder: solver preflop vettoriale (P0–P9)

Data: 2026-09-15
Stato: `ROADMAP CONGELATA / IMPLEMENTAZIONE NON AVVIATA`
Documenti vincolanti: [analisi](HU_PREFLOP_ALGORITHM_AND_ABSTRACTION_ANALYSIS_2026-09-15.md) e
[registro decisioni](PREFLOP_ARCHITECTURE_DECISION_LOG.md) (decisioni D1–D26). In caso di
conflitto fra questo documento e il registro, vale il registro, che è più recente.

## 1. Mandato e risultato richiesto

Realizzare il nuovo solver preflop Short Deck descritto nell'analisi: astrazione delle carte
precalcolata con feature esatte e clustering, trainer CFR vettoriale con campionamento del board,
certificazione tramite best response esatta nel gioco fisico. Prima l'HU (validazione su HU10,
qualificazione su CO40), con strutture dati pronte per N giocatori (3-way subito dopo l'HU).

Il risultato finale della roadmap è:

1. una libreria di astrazione delle carte e i suoi file precalcolati distribuibili;
2. una libreria trainer separata dal solver postflop e dal trainer legacy;
3. un certificatore board-major con stima campionata e passata esatta;
4. eseguibili per costruire risorse, addestrare, certificare, interrogare ed esportare;
5. il chart viewer aggiornato al nuovo formato;
6. un solve CO40 con exploitability fisica misurata, tre seed, report riproducibile;
7. il diario dell'agent (§4) completo di gate, fallimenti e dubbi.

Non fa parte del mandato: modificare il solver postflop, riprodurre le frequenze Monker,
addestrare il 3-way. La preparazione del 3-way (P10) si limita al conteggio degli alberi.

## 2. Confini

### 2.1 Regole ereditate dal registro

| Regola | Fonte |
|---|---|
| Gate: exploitability fisica misurata + EV con intervallo; Monker descrittivo | D1, D4 |
| Soglia fisica: massimo guadagno per giocatore ≤ **0,03 ante, l'1 % del piatto iniziale**; riportare anche NashConv, /pot, /stack. *Stretta da D27 il 2026-09-18; era 0,1 a, dichiarata «iniziale» e «da stringere quando l'astrazione migliora»* | D2, D27 |
| Arresto del training: massimo guadagno ≤ 1 % del pot iniziale nel gioco astratto, stimato sui board campionati | D3 |
| Nessun link né copia dal solver postflop; ProductionDcfr solo come oracolo nei test | D5 |
| Validazione su HU10, poi CO40; prima albero ridotto, poi size complete | D6, D17 |
| Risorse precalcolate distribuite con l'applicazione | D11 |
| Archiviazione graduale del legacy, mai cancellazione dalla storia | D12 |
| N vettori di reach e kernel di showdown sostituibile dal primo giorno | D13 |
| Tutti i thread; risultato bit-identico al variare dei thread | D14 |
| Libreria separata per il nuovo trainer | D15 |
| Nessun budget di tempo come gate; il tempo è un output | D16 |
| Viewer aggiornato insieme al trainer | D18 |

### 2.2 Perimetro dei file

| Area | Intervento ammesso | Limite |
|---|---|---|
| Nuovo `libs/card_abstraction/`, `include/gtosd/card_abstraction/` | canonicalizzazione, cataloghi, feature, clustering, tabelle bucket, loader | nessuna dipendenza da preflop legacy o postflop |
| Nuovo `libs/preflop_blueprint/`, `include/gtosd/preflop_blueprint/` | modello di gioco, albero compilato, kernel, trainer, certificatore, export, query | dipende solo da core, equity, tree, card_abstraction |
| Nuovi `benchmarks/preflop_blueprint_*.cpp` | eseguibili CLI | nessun link a `gtosd_postflop*`, `gtosd_preflop`, `gtosd_preflop_trainer` |
| Nuovi `tests/card_abstraction_*_tests.cpp`, `tests/preflop_blueprint_*_tests.cpp` | test; gli oracoli possono linkare `gtosd_solver` (FiniteGame) e, nei soli test, il solver postflop | l'oracolo non entra nella libreria |
| Nuovi `schemas/preflop_blueprint_*.schema.json`, `benchmarks/fixtures/preflop_blueprint_*.json` | formati versionati | compatibilità dichiarata nello schema |
| `libs/core/` | solo aggiunte: costruttore di stato preflop a N giocatori (P10), nessun cambio di regole | test per ogni aggiunta |
| `libs/equity/` | solo lettura e uso di `SevenCardLookupTable` | nessuna modifica |
| `libs/solver/` | solo lettura; uso di `solve_finite_game`, `exact_best_response`, `calculate_nash_conv` nei test | nessuna modifica |
| `libs/preflop/` (trainer legacy) | nessuna modifica fino a P9; in P9 solo l'opzione CMake di archiviazione (§P9) | nessuna cancellazione |
| `libs/postflop*/`, `include/gtosd/postflop/` | lettura | nessuna modifica |
| `tools/hu_preflop_chart_viewer/` (repository git separato, ignorato dal principale) | generatore e sito aggiornati al nuovo formato (P8) | commit nel suo repository |
| `docs/research/preflop_vector_cfr/` | diario, report di fase, artefatti JSON piccoli | nessuna riscrittura delle evidenze precedenti |
| Fixture e risultati legacy, `.tmp/`, `.reasonix/` | lettura | nessuna modifica |

Ogni file nuovo va aggiunto ai target CMake con `gtosd_set_warnings` e compilato con warning come
errori, come il resto del repository.

## 3. Regole operative

### 3.1 L'agent deve

1. Lavorare su un branch dedicato (registro, D21) in un worktree separato; non toccare il working
   tree dell'utente; registrare commit hash, preset e compilatore in ogni report.
2. Aggiornare il diario (§4) a fine di ogni sessione di lavoro, a ogni gate e immediatamente a
   ogni fallimento o dubbio bloccante.
3. Riportare ogni gate come `PASS`, `FAIL`, `INCONCLUSIVE` o `NOT_RUN` con comando, seed, tempi,
   memoria e link all'artefatto; mai dichiarare raggiunto un gate non eseguito.
4. Distinguere sempre errore di codice, errore numerico, rumore statistico ed errore di
   astrazione.
5. Usare seed separati per partizione, training, board di valutazione; registrarli tutti.
6. Eseguire `ctest` del preset `windows-release` sui test toccati prima di dichiarare un gate; i
   kernel (P5) passano anche sotto `windows-asan`.
7. Tenere le unità esplicite: ante per mano; nessuna riscalatura nascosta.

### 3.2 L'agent non deve

1. Usare frequenze o EV Monker nel training, nel clustering, nell'inizializzazione o come gate.
2. Copiare o linkare kernel del solver postflop; l'algoritmo standard si reimplementa e il postflop
   resta oracolo nei test.
3. Modificare il trainer legacy, `ProductionDcfr`, regole, rounding, utility o ledger.
4. Abbassare una soglia per far passare un test, troncare stime negative a zero, o riportare
   una stima campionata come certificato.
5. Introdurre GPU, servizi remoti, quantizzazione o pruning senza misurarne l'effetto sulla
   qualità.
6. Cancellare file, fare reset o clean, riscrivere la storia git.
7. Passare alla fase successiva con un gate `FAIL` o `INCONCLUSIVE` senza registrare la diagnosi
   e la decisione nel diario.

### 3.3 Autonomia (D22)

L'agent procede da P0 a P10 senza chiedere conferma ai gate. Si ferma soltanto in tre casi:

1. un gate `FAIL`: registra diagnosi e proposta nel diario, poi si ferma;
2. un gate `INCONCLUSIVE`: registra cosa manca per decidere e il costo dell'esperimento che lo
   deciderebbe, poi si ferma;
3. una decisione fuori perimetro (§2.2 e D24): registra la domanda nel diario §4 e si ferma.

In tutti gli altri casi continua. Le aggiunte additive al core previste da P10 e l'opzione CMake
di archiviazione di P9 sono dentro il perimetro e non richiedono conferma.

### 3.4 Parametri di default e paletti (D24)

I default sono la baseline: si eseguono sempre per primi e ogni variante si confronta con essi a
pari tempo di parete, stessi seed, stessi board di valutazione, metrica D3. Un cambiamento va
registrato nel diario §5 con parametro, valore precedente e nuovo, tabella dell'evidenza e
fingerprint prodotti. Nessun parametro si regola guardando le frequenze Monker.

| Parametro | Default | Intervallo ammesso | Condizione per cambiarlo |
|---|---|---|---|
| Capacità bucket flop/turn/river | 200/500/1.000; secondo candidato 500/1.000/2.000 | 50–4.000 per street; stato numerico ≤ 4 GiB sulla macchina di riferimento | confronto matched su 3 seed |
| Bin dell'istogramma flop/turn | 16 | 8–32 | dispersione intra-bucket ed exploitability; nuovo fingerprint dell'astrazione |
| Gruppi avversari OCHS al river | 8 | 4–16 | come sopra |
| Distanze del clustering | EMD per istogrammi, L2 per OCHS | fisse | solo con evidenza e nuova identità dell'astrazione |
| k-means | k-means++, 10 riavvii | 3–50 riavvii | costo di costruzione e inerzia registrati |
| Batch di board `B` | 32 | 8–256 | scelta in P6 per exploitability a pari tempo |
| Board della stima campionata `M` | 2.000 | 500–20.000; valutazione ≤ 10 % del tempo di training | intervallo dichiarato nel report |
| Schema dei pesi | Linear baseline; DCFR `1,5/0/2` sfidante | altri schemi solo come sfidanti aggiuntivi dopo i due | le formule di §5 non cambiano |
| Precisione dello stato | double | float32 solo dopo P9 come ottimizzazione misurata | errore numerico documentato |
| Seed | 3 seed di training fissati in P0; seed di partizione fisso | — | mai scegliere il seed migliore |
| Run esplorativi | ≤ 1 h su HU10, ≤ 3 h su CO40 ciascuno | — | il run finale di P9 è escluso dal limite |

Non modificabili senza decisione dell'utente: regole del gioco, size, ledger, soglie D2 e D3,
definizione delle metriche, feature esatte (nessun Monte Carlo), deal fisici e card removal,
separazione dal solver postflop.

## 4. Diario dell'agent (obbligatorio)

File: `docs/research/preflop_vector_cfr/PROGRESS_LOG.md`. Il template è già presente. Struttura:

1. **Stato corrente:** fase in corso, ultimo gate, prossimo passo.
2. **Registro dei gate:** una riga per fase P0–P10 con esito, data, commit, link al report.
3. **Diario:** voci in ordine cronologico inverso. Ogni voce riporta data, fase, cosa è stato
   fatto, comandi eseguiti, risultati numerici, fallimenti con causa identificata o ipotesi,
   dubbi e domande, prossimo passo.
4. **Domande per l'utente:** aperte e chiuse, con la risposta ricevuta.
5. **Decisioni prese dall'agent:** scelte interne con motivazione, per esempio una struttura
   dati o un parametro non fissato dalla roadmap.

Le voci non si cancellano: una correzione è una nuova voce che rimanda alla precedente. Un
fallimento va registrato prima di tentare la correzione.

Per ogni fase l'agent produce inoltre un report `docs/research/preflop_vector_cfr/Px_NOME.md`
con esito, identità (commit, fingerprint), comandi, tabelle e limiti, nello stile dei report R0–R9.
I JSON piccoli (< 1 MB) stanno nella stessa cartella; gli artefatti grandi in
`benchmarks/results/preflop_blueprint/` (ignorato da git) con SHA-256 nel report.

## 5. Contratti matematici comuni

Questi contratti valgono per P5–P7 e vanno riportati nel report della fase che li implementa.

**Chance.** Un board completo `B` (cinque carte) ha probabilità uniforme sui board fisici. Dato
`B`, le mani vive per giocatore sono le `C(31,2) = 465` combo disgiunte da `B`; la coppia
`(h, o)` disgiunta ha probabilità `1 / (465 · 406)`. I board canonici hanno molteplicità pari al
numero di board fisici della loro orbita; la somma delle molteplicità per street deve coincidere
con il conteggio fisico (7.140 flop; 235.620 flop+turn ordinati; 7.539.840 board history).

**Reach.** Per ogni giocatore `p` e mano `h`, `reach_p[h]` è il prodotto delle probabilità delle
azioni di `p` lungo la history. La reach controfattuale di `h` per `p` è
`Σ_{o disgiunta da h} reach_{-p}[o] · (1/406)`, calcolata con i kernel di §P5.

**Valore di un nodo terminale per la mano `h` del giocatore `p`.** Con `r = reach_{-p}`:

- fold: `u_fold · D[h]`, dove `D[h] = S − C[h1] − C[h2] + r[h]`, `S = Σ_o r[o]`,
  `C[c] = Σ_{o ∋ c} r[o]`;
- showdown: `u_win · W[h] + u_tie · T[h] + u_lose · L[h]`, con `W`, `L` masse delle mani avversarie
  disgiunte da `h` rispettivamente peggiori e migliori, `T = D − W − L`;
- all-in preflop: come showdown ma con `W/T/L` ottenuti dalla tabella esatta per coppia di combo
  (§P2), sommando su `o` disgiunte da `h`;
- all-in flop/turn: come showdown sul board campionato (stima non distorta).

`u_win`, `u_tie`, `u_lose`, `u_fold` derivano da `settle_terminal` sullo stato del nodo, in ante
per mano, per ciascun giocatore.

**Regret e media.** Nel passaggio che aggiorna `p`, ai nodi di `p` con bucket `b(h)`:

```text
R[nodo][b][a] += Σ_{h ∈ b} cf_reach_p[h] · (v_a[h] − v[h])
S[nodo][b][a] += Σ_{h ∈ b} reach_p[h] · σ(a | nodo, b)
```

con `v[h] = Σ_a σ(a | nodo, b(h)) · v_a[h]`. Al preflop il bucket è la classe della combo. Lo
schema dei pesi (Linear: `× t` su `R` e `S`; DCFR `1,5/0/2`: sconto una volta per iterazione) si
applica dopo aver ridotto tutti i board del batch. La strategia corrente è il regret matching sui
regret positivi, uniforme se nessuno è positivo; la strategia media è `S` normalizzata.

**Best response.** Per il giocatore `p` contro la strategia media dell'avversario, su un board
fisso: ai nodi di `p`, `v[h] = max_a v_a[h]`; agli altri nodi, media pesata con la strategia media
avversaria. Il valore di best response del gioco è la media sui board (esatta con le molteplicità,
campionata con intervallo altrimenti); `gain_p = BR_p − EV_p(σ̄)`. La scomposizione per board è
esatta perché il board è pubblico e nessun information set attraversa due board.

> **Erratum (2026-09-15, P6).** L'ultima frase vale per l'EV della strategia media, non per la
> best response: gli information set del preflop (mano), del flop (mano + flop) e del turn (mano +
> flop + turn) attraversano tutti i board che condividono quel prefisso. Prendere il massimo per
> board ai nodi sopra il river dà un responder chiaroveggente sulle carte future e sovrastima
> l'exploitability (circa 1 a/mano su HU10, P6_TRAINER.md §4). La best response fisica corretta
> aggrega i valori delle azioni sulle carte ancora da distribuire prima del massimo: river massimo
> per board; turn somma sui river poi massimo; flop somma sui turn poi massimo; preflop somma sui
> flop poi massimo (`best_response.hpp`, decisione 30 del diario). Lo stimatore campionato (P6.3)
> campiona flop ed enumera tutti i loro runout; P7 deve usare la stessa aggregazione.

**Metriche.** `nashconv = gain_CO + gain_BTN`; `normalized_dev = max(gain) / pot_iniziale`;
`normalized_stack = max(gain) / stack_effettivo`. Tutte in ante per mano, con intervallo al 95 %
quando campionate.

## 6. Fasi

### P0 — Contratto, snapshot e scaffolding

**Scopo.** Rendere attribuibile ogni risultato e predisporre target, diario e branch.

**Da fare.**

1. Registrare commit, stato del working tree, CPU, RAM, compilatore, preset; verificare che
   esista il tag `preflop-legacy-es-2026-09-15` (creato il 2026-09-15 sul commit `04aa687`,
   registro D20) e non ricrearlo.
2. Creare i target CMake vuoti `gtosd_card_abstraction`, `gtosd_preflop_blueprint`, i primi
   test e l'opzione `GTOSD_BUILD_PREFLOP_BLUEPRINT` (default `ON`), con etichetta CTest
   `preflop_blueprint`.
3. Aggiungere un controllo di dipendenza (come `gtosd_hu_preflop_trainer_dependency_check`) che
   fallisce se i nuovi target linkano `gtosd_postflop*`, `gtosd_preflop` o
   `gtosd_preflop_trainer`.
4. Compilare il diario (§4) con la prima voce e il registro dei gate `NOT_RUN`.
5. Congelare le fixture di gioco della nuova libreria, schema `gtosd.preflop_blueprint_game.v1`
   con liste di size a lunghezza variabile: `preflop_blueprint_hu10_full_v1.json` (equivalente a
   `hu_preflop_hu10_calibration_v1.json`), `preflop_blueprint_hu10_reduced_v1.json` (una sola
   size postflop 66 % più all-in), `preflop_blueprint_co40_v1.json` (equivalente a
   `hu_preflop_co40_game_v1.json`).

> **Erratum (2026-09-16, P8).** Dal 2026-09-16 le fixture HU10 non sono più equivalenti alla
> calibrazione legacy: per decisione dell'utente hanno una sola size preflop (open 5 a) e, dalla
> sera del 2026-09-16, nessuna size di risposta (contro l'open solo fold, call e all-in) invece
> di 3 a / 5 a e 6 a / 8 a. Esiste inoltre `preflop_blueprint_co40_test_v1.json`
> (una size postflop 100 % più all-in) solo per misure di tempo e prove: il gate P9 resta sul
> CO40 completo.

> **Erratum (2026-09-17, P9). Autorizzazione esplicita dell'utente del 2026-09-17.** L'utente ha
> autorizzato la modifica dell'albero preflop di **entrambe** le fixture CO40, compresa
> `preflop_blueprint_co40_v1.json`, che fino a oggi era protetta come riferimento del gate P9. Due
> cambiamenti, decisi dall'utente e non dall'agent:
>
> 1. **Formula delle size di rilancio.** Vale la definizione esatta di rilancio di un piatto
>    intero: chi rilancia chiama la puntata in corso e poi rilancia del piatto risultante, cioè
>    `P + 2B - c` con `P` il piatto prima dell'azione, `B` la puntata da eguagliare e `c` le fiche
>    già versate dal rilanciante in quel giro. La scorciatoia `3 x last bet + pot`, valida solo
>    per `c = 0`, non viene adottata. Conseguenza su `preflop_blueprint_co40_test_v1.json`: la
>    risposta passa da 13 a a **17 a** (BTN deve 4 a, il piatto dopo il call è 12 a, quindi
>    1 + 4 + 12). Il 13 a proveniva da un calcolo errato registrato nel diario del 2026-09-16
>    ("BTN paga 3 a per chiamare, piatto 10 a"), smentito dallo stato pubblico del motore.
>    L'open di CO resta 5 a: con `c = 0` le due convenzioni coincidono. Anche il rilancio di BTN
>    sul limp resta 5 a, che è già il valore esatto (`4 + 2 - 1`).
> 2. **Ramo limpato senza size di re-raise configurata.** Dopo "CO limpa, BTN rilancia" il limper
>    conserva il re-raise, ma l'unico disponibile è l'all-in: le azioni sono fold, call e
>    all-in. Serve un campo nuovo perché i due rami raggiungono stati pubblici identici a meno di
>    quale posto tiene quale impegno: `limp_response_target_units`, opzionale, indicizzato sugli
>    open come `response_target_units`, assente = comportamento storico, vuoto = solo all-in.
>
> Questo erratum sostituisce, per le sole fixture CO40 e per la sola parte preflop, il divieto di
> "cambiare albero, size o soglie" del paragrafo **Da non fare** di P9: quel divieto resta in
> vigore contro le modifiche scelte dall'agent per migliorare un risultato, che restano proibite.
>
> 3. **Conversione delle size di `preflop_blueprint_co40_v1.json`.** Su indicazione dell'utente
>    del 2026-09-17 anche la fixture principale passa alla formula esatta. La formula produce una
>    sola size per spot, quindi i due open Monker (6 a e 10 a) collassano: sono entrambi decisi
>    alla radice, dove `P + 2B - c` vale 5 a. Le size diventano **open 5 a** e **risposta 17 a**,
>    le stesse della variante di test; il rilancio di BTN sul limp resta 5 a, che è già il valore
>    esatto anche in quel nodo. Le due fixture differiscono ora solo nelle size postflop
>    (3 size 33/66/120 % contro una sola del 100 %). **Conseguenza dichiarata:** le size non
>    corrispondono più a quelle del riferimento Monker e il comparatore le confronta a parità di
>    albero, quindi il confronto con Monker previsto da D1/D4 non è più disponibile su questa
>    fixture finché non si produce un riferimento esterno sul nuovo albero. L'utente è stato
>    informato di questa conseguenza prima di decidere.

**Da non fare.** Nessun run lungo; nessuna modifica al legacy.

**Gate P0.** Build Release dei target vuoti con warning come errori; controllo di dipendenza PASS;
diario e tag presenti; fixture validate dallo schema.

### P1 — Canonicalizzazione e cataloghi

**Scopo.** Fornire indici e cataloghi esatti per board e osservazioni.

**Da fare.**

1. Indice combinatorio per insiemi di carte ordinati (2, 3, 5, 7 carte) e sua inversa.
2. Canonicalizzazione dei semi di un board per street: permutazione che minimizza il codice; il
   risultato espone la permutazione applicata, così che le mani possano essere portate nello
   stesso frame.
3. Cataloghi con molteplicità: flop canonici (attesi 573), flop+turn canonici (limite inferiore
   9.818), board a cinque carte canonici (attesi 19.998), board history canoniche
   flop→turn→river (attese 369.072). Salvati con checksum.
4. Campionamento di board: fisico uniforme (default) e canonico proporzionale alla molteplicità
   (equivalente); enumerazione ordinata per la passata esatta con ripresa da un offset.

**Verifiche.** Somma delle molteplicità uguale ai conteggi fisici per ogni catalogo; round trip
indice ↔ carte su tutti gli insiemi; canonicalizzazione idempotente e invariante alle 24
permutazioni; conteggi attesi riprodotti; determinismo del sampler a seed fisso.

**Gate P1.** Tutti i test PASS; report con i conteggi e i tempi di costruzione dei cataloghi.

### P2 — Risorse esatte

**Scopo.** Calcolare una volta feature e tabelle esatte, indipendenti da stack, size e giocatori.

**Da fare.**

1. Caricare o costruire la `SevenCardLookupTable` esistente; verificarne checksum e fingerprint
   del ruleset; nessuna nuova implementazione dell'evaluator.
2. Tabella all-in preflop: per ogni coppia non ordinata di combo disgiunte (156.240) conteggi
   esatti `win/tie/lose` sui `C(32,5) = 201.376` board; salvata con checksum; sfruttare la
   simmetria dei semi solo se validata contro il calcolo diretto su un campione.
3. Feature flop: per ogni flop canonico e ciascuna delle 630 combo (528 vive), istogramma a 16
   bin dell'equity al river sui 465 runout, ciascuna equity calcolata esattamente contro le 406
   mani avversarie.
4. Feature turn: per ogni flop+turn canonico e combo viva, istogramma a 16 bin dell'equity al
   river sui 30 river, contro 378 mani.
5. Feature river: per ogni board a cinque carte canonico e combo viva, equity esatta contro le 406
   mani e contro ciascuno di 8 gruppi di forza preflop dell'avversario (OCHS); i gruppi sono
   definiti una volta sulle 81 classi per equity preflop contro mano casuale, con masse simili,
   e versionati.
6. Costruzione multi-thread a chunk per street; file versionati con fingerprint di mazzo,
   ranking, versione delle feature, checksum; tempi per street nel report.

**Verifiche.** Su un campione di 1.000 osservazioni per street le feature coincidono entro `1e-12`
con un calcolo scalare indipendente; le equity sono in `[0,1]` e gli istogrammi sommano a 1; la
tabella all-in riproduce `win + tie + lose = 201.376` per coppia e coincide con l'enumerazione
diretta su 200 coppie; invarianza ai semi: due osservazioni equivalenti hanno feature uguali.

**Da non fare.** Campionamento Monte Carlo delle feature; lettura di carte non visibili al
giocatore nella street della feature.

**Gate P2.** Test PASS; file prodotti con checksum; tempi di costruzione e dimensioni nel report.
Se la costruzione supera 60 minuti sull'i3, registrare il profilo e ottimizzare prima di P3.

### P3 — Clustering e tabelle bucket

**Scopo.** Produrre il mapping `(board canonico, combo) → bucket` per street.

**Da fare.**

1. k-means con inizializzazione k-means++ e `partition_seed`, 10 riavvii, scelta della minima
   inerzia; distanza EMD monodimensionale (L1 delle CDF) per gli istogrammi flop/turn, L2 per i
   vettori OCHS al river. Capacità iniziali `200/500/1.000`; il formato accetta capacità
   arbitrarie.
2. Tabelle di assegnazione `uint16` indicizzate da (id board canonico, combo) per street; centroidi
   salvati; file con fingerprint (feature, capacità, seed, versione).
3. API di lookup: dato un board fisico e una combo, restituire il bucket della street applicando la
   permutazione canonica del board alla combo. Costo target: tempo costante.
4. Diagnostica: occupazione, massa per bucket, dispersione intra-bucket delle feature, esempi di
   mani per bucket nel report.

**Verifiche.** Determinismo a seed fisso; invarianza ai semi (board e combo permutati insieme
danno lo stesso bucket); copertura totale delle combo vive; coerenza sotto lo stabilizzatore del
board (mani equivalenti ricevono lo stesso bucket); nessuna dipendenza dalle carte future
(costruzione per API che riceve solo mano e board visibile); rifiuto di file corrotti o con
fingerprint diverso.

**Da non fare.** Feature o pesi scelti guardando i risultati Monker; ricalcolo dei cluster durante
il training.

**Gate P3.** Test PASS; tabelle prodotte per HU10 e CO40 (sono le stesse: dipendono solo dal
mazzo); report con occupazione e dispersione.

### P4 — Modello di gioco e albero compilato

**Scopo.** Rappresentare il gioco in forma compilata, con strutture pronte per N giocatori.

**Da fare.**

1. Configurazione: `player_count`, posizioni, stack, ante, button blind, size preflop per numero
   di raise, size postflop per street, all-in, rake (disabilitata), con schema versionato (P0).
2. Albero preflop: per l'HU riprodurre esattamente l'albero corrente (CO40: 58 nodi, 20 decisioni,
   9 ingressi postflop, fingerprint riproducibile); il builder usa regole guidate dallo stato (numero
   di raise, limp, check del BTN, all-in) e non la macchina a stadi HU, così da estendersi a N
   giocatori in P10.
3. Albero pubblico postflop compilato per ogni ingresso: nodi con id interi, street, giocatore,
   lista azioni, figli, tipo di terminale, costanti di payoff per giocatore (`u_win`, `u_tie`,
   `u_lose`, `u_fold`) calcolate una volta con `settle_terminal`; ordine dei nodi per street e per
   sottoalbero per il parallelismo di P6. Per CO40 attesi 30.324 nodi e 11.308 decisioni.
4. Layout dello stato: `R` e `S` in double, contigui, indicizzati da (nodo, bucket, azione); al
   preflop (nodo, classe, azione); dimensione riportata in byte.

**Verifiche.** Ogni transizione compilata coincide con `legal_actions` e `apply_action`; conteggi
CO40 uguali ai valori attesi; fingerprint dell'albero preflop uguale a `fnv1a64:a68337fa567aa2d9`
per la fixture CO40 equivalente, oppure differenza spiegata e documentata; payoff terminali uguali
a `settle_terminal` su tutti i nodi.

**Gate P4.** Test PASS; report con conteggi, byte dello stato per le due capacità candidate e
tempi di compilazione.

### P5 — Kernel vettoriale HU

**Scopo.** Implementare i kernel di §5 e dimostrarne l'esattezza.

**Da fare.**

1. Kernel fold: `D[h]` per tutte le mani in tempo lineare tramite `S` e `C[c]`.
2. Kernel showdown: ordinamento delle 465 mani per rank (dalla tabella a 7 carte), passata
   crescente e decrescente con somme per carta correnti, gestione dei gruppi di pari rank
   (valori del gruppo calcolati prima di aggiungerne le masse), `W`, `L`, `T` per ogni mano.
3. Kernel all-in preflop dalla tabella P2.
4. Interfaccia `ShowdownKernel` con implementazione HU; la firma prevede N vettori di reach e
   una maschera dei giocatori vivi (D13), anche se solo N = 2 è implementato.
5. Propagazione delle reach e dei valori lungo l'albero compilato per un board, senza allocazioni
   nel percorso frequente.

**Verifiche.** Su 200 board casuali e vettori di reach casuali (incluse reach nulle e gruppi di
pari rank), uguaglianza entro `1e-12` con un'enumerazione brute force `O(n²)`. Oracolo postflop:
su almeno tre sottogiochi river con range fissi, i CFV per combo del nuovo kernel coincidono entro
`1e-9` con quelli estratti dal solver postflop `ProductionDcfr` (uso nei soli test). Nessuna
diagnostica AddressSanitizer.

**Da non fare.** Approssimazioni sui blocker; copie dal kernel postflop.

**Gate P5.** Test PASS in Release e ASan; report con tempo per board della sola traversata dei
valori (senza update) su CO40.

### P6 — Trainer con campionamento del board

**Scopo.** Addestrare la strategia media con CFR vettoriale, board campionati, update alternati.

**Da fare.**

1. Ciclo: batch di `B` board; per ogni board un passaggio che aggiorna il giocatore 0 e uno che
   aggiorna il giocatore 1, con strategia letta dallo stato all'inizio del passaggio e delta
   applicati alla fine del passaggio; riduzione del batch; schema dei pesi (Linear default, DCFR
   `1,5/0/2` opzionale) applicato una volta per iterazione.
2. Parallelismo per sottoalbero: i thread si dividono sottoalberi disgiunti dell'albero pubblico
   (partizione compilata in P4); ogni cella è aggiornata da un solo thread; nessuna copia dello
   stato.
3. Stimatore campionato di best response e EV su `M` board indipendenti (kernel di P5 in modalità
   `max`), con errore standard e intervallo; regola di arresto D3:
   `stima + semiampiezza ≤ 0,03a` sul massimo guadagno, oppure limite di iterazioni.
4. Checkpoint atomico con checksum: stato, contatori, RNG, configurazione, fingerprint di regole,
   albero, astrazione; ripresa byte-identica.
5. Telemetria minima: iterazioni, board processati, tempo per board, memoria di processo, curva
   della stima di exploitability.

**Verifiche.**

- Oracolo esatto: costruire con `FiniteGame` un gioco ridotto con lo stesso albero HU10 ridotto
  (D17), un insieme fisso di 2–3 board con molteplicità e un sottoinsieme di 6 combo per
  giocatore (gancio di test nel trainer), con information set uguali ai bucket P3. Il trainer in
  modalità esatta sugli stessi board e `solve_finite_game` con lo stesso schema Linear devono
  produrre strategia media e regret uguali entro `1e-9` dopo lo stesso numero di iterazioni.
- Bit-identità del checkpoint fra 1, 2, 4 e 8 thread.
- Ripresa da checkpoint uguale al run continuo.
- HU10 ridotto e HU10 completo: la stima campionata di exploitability decresce ai checkpoint
  dichiarati; stampa della curva.
- Stimatore campionato: su un gioco ridotto la media dello stimatore sui board coincide con la
  best response esatta della passata completa entro l'intervallo.

**Da non fare.** Regret e media con pesi diversi da quelli del contratto; letture della strategia
aggiornata a metà passaggio; cache non limitate.

**Gate P6.** Oracolo PASS; determinismo PASS; ripresa PASS; HU10 completo raggiunge D3 con tempo e
memoria riportati. Report con la curva exploitability/tempo.

### P7 — Certificatore board-major

**Scopo.** Best response esatta nel gioco fisico della strategia sollevata.

**Da fare.**

1. Passata esatta su tutte le board history canoniche con molteplicità (P1), parallela per board,
   con accumulatori per giocatore e ripresa da offset; output: `EV_p`, `BR_p`, `gain_p`,
   `nashconv`, `normalized_dev`, `normalized_stack`, tempi, memoria.
2. Stimatore campionato con intervallo (lo stesso di P6) esposto come comando separato.
3. Certificato JSON `gtosd.preflop_blueprint_certificate.v1` con fingerprint di regole, albero,
   astrazione, policy e catalogo; campo `exact=true|false`.

**Verifiche.** Sul gioco ridotto dell'oracolo P6, la passata esatta coincide con
`calculate_nash_conv` del `FiniteGame` entro `1e-10`; `gain_p ≥ −1e-12`; `EV_CO + EV_BTN = 0`;
ripresa da offset uguale al run continuo; su HU10 il valore campionato è compatibile con quello
esatto entro l'intervallo dichiarato.

**Gate P7.** Test PASS; passata esatta su HU10 completata con tempo riportato; proiezione misurata
del tempo su CO40.

### P8 — Export, query, comparatore, viewer

**Scopo.** Rendere la soluzione consultabile e aggiornare gli strumenti.

**Da fare.**

1. Formato policy `gtosd.preflop_blueprint_policy.v1`: strategia media per (nodo, bucket) e
   (nodo, classe), quattro fingerprint (regole, albero, astrazione, policy), checksum, scrittura
   atomica; la strategia corrente è opzionale e marcata diagnostica.
2. API di query: data una history legale, una combo e un board, restituire la distribuzione
   d'azione senza ricalcolare feature.
3. Export chart: 81 classi per ogni nodo preflop con frequenze, EV per azione e intervallo
   (stimati sui board campionati), più le decisioni postflop per board e path richieste dal viewer.
4. Comparatore: il verdetto dipende da D1, D2, D4 (exploitability fisica, EV con intervallo); le
   distanze Monker restano nel report con la metrica pesata per perdita EV e lo stato
   `EXTERNAL_CONTRACT_INCOMPLETE`.
5. Viewer (`tools/hu_preflop_chart_viewer`, repository separato): il generatore legge il nuovo
   export; badge di stato (`ESTIMATED`, `CERTIFIED_EXACT`, `EXTERNAL_REFERENCE`); nessuna policy
   di ricerca legacy presentata come baseline.

**Verifiche.** Round trip della policy; query uguale prima e dopo l'export; validatore statico del
viewer PASS; schema JSON accettato; comparatore su un candidato fittizio con exploitability sopra
soglia restituisce `FAIL`.

**Gate P8.** Test PASS; viewer navigabile con un export HU10; report.

### P9 — Qualificazione CO40 e archiviazione, primo stadio

**Scopo.** Produrre il primo solve CO40 misurato e avviare l'archiviazione (D12).

**Da fare.**

1. Confronto a pari tempo di parete: Linear contro DCFR `1,5/0/2`, batch `B` fissato, tre seed
   ciascuno, board di valutazione comuni fra i candidati; capacità `200/500/1.000` e
   `500/1.000/2.000`. Metrica: massimo guadagno stimato con intervallo; secondaria: EV CO con
   intervallo.
2. Run finale con la configurazione scelta fino a D3; passata esatta P7; certificato.
3. Report: curva exploitability/tempo, tabella per seed, memoria, tempi per componente, confronto
   Monker descrittivo, limiti.
4. Archiviazione primo stadio: opzione `GTOSD_BUILD_LEGACY_PREFLOP_RESEARCH` (default `OFF`) che
   esclude dalla build trainer legacy, benchmark di decomposizione, certificatore per sottogiochi
   river e relativi test; restano attivi FiniteGame, contratto di averaging, tabella a 7 carte,
   fixture e comparatore. La suite Release completa deve passare con l'opzione `OFF` e `ON`.

**Da non fare.** Cambiare albero, size o soglie per migliorare il risultato; scegliere il seed
migliore; promuovere un candidato senza passata esatta. Non vi rientrano le modifiche dell'albero
preflop CO40 autorizzate dall'utente il 2026-09-17 (erratum alla sezione delle fixture): sono
correzioni della formula delle size e della struttura del ramo limpato, decise dall'utente prima
di vedere il risultato che producono.

**Gate P9.** Certificato esatto CO40 con `max(gain) ≤ 0,1a` per la configurazione scelta su tutti i
seed dichiarati, oppure `FAIL` documentato con diagnosi (astrazione, algoritmo, budget) e prossimo
intervento proposto. Suite Release PASS in entrambe le modalità dell'opzione.

### P10 — Preparazione 3-way: conteggio degli alberi (A3)

**Scopo.** Misurare la dimensione del problema multiway prima di progettarne memoria e kernel.

**Prerequisito.** Gate P4. Indipendente da P5–P9; eseguibile in parallelo quando c'è capacità.

**Dato da produrre.** Una tabella per 3, 4, 5 e 6 giocatori con: nodi decisionali preflop,
ingressi postflop (con l'insieme dei giocatori vivi in ciascuno), nodi pubblici postflop per
ingresso e totali, byte dello stato per le capacità candidate, tempo stimato per board. Oggi
questi numeri esistono solo per l'HU (58 nodi preflop, 9 ingressi, 30.324 nodi postflop).

**Da fare.**

1. Aggiunta additiva in `libs/core`: costruttore di stato preflop a N giocatori con ante 1a per
   giocatore e button blind 1a sul BTN, ordine UTG → BTN (D9, D10), con test.
2. Builder preflop a N giocatori con le regole guidate dallo stato di P4 e i vincoli HU (open
   6a/10a, risposta 10,5a/14,5a, poi fold/call/all-in, all-in sempre disponibile).
3. Conteggio per 3, 4, 5, 6 giocatori: nodi preflop, decisioni, ingressi postflop, nodi pubblici
   postflop per ingresso e totali, insiemi di giocatori vivi per ingresso; byte dello stato per le
   capacità candidate; stima del tempo per board.

**Da non fare.** Training multiway; kernel di showdown a tre (fase successiva, con protocollo
proprio).

**Gate P10.** Report con i conteggi e la raccomandazione sull'action abstraction dei giocatori
successivi al primo raise, se i conteggi superano il budget di memoria.

## 7. Ordine e dipendenze

```text
P0 → P1 → P2 → P3 → P4 → P5 → P6 → P7 → P8 → P9 → P10
```

P4 può iniziare in parallelo a P2–P3 perché non dipende dalle feature. P8 può iniziare dopo P6
per il formato policy. Nessuna fase di qualificazione (P9) prima di P7. P10 dipende soltanto dal
gate P4 e può essere eseguita in qualsiasi momento dopo di esso, in parallelo a P5–P9: la sua
posizione in coda indica la priorità (prima l'HU), non una dipendenza tecnica.

## 8. Consegna finale

1. Codice: diff per modulo, prova della catena di dipendenze, elenco delle aggiunte al core.
2. Contratti: §5 con eventuali precisazioni emerse, identità degli algoritmi, unità.
3. Verifiche: registro dei gate P0–P10 con comandi ed esiti, test di correttezza distinti dai
   benchmark.
4. Evidenze: fingerprint, seed, tempi per componente, memoria, curve, certificati, tre seed.
5. Artefatti: file precalcolati, policy CO40, viewer aggiornato, comandi riproducibili, diario
   completo.

Il lavoro non è concluso quando il programma termina: è concluso quando ogni gate ha un esito
registrato e ogni fallimento ha una diagnosi.

## 9. Istruzioni di avvio per l'agent coder (D23)

1. Leggere nell'ordine: il registro decisioni (sezione 1, decisioni D1–D26), questa roadmap,
   l'analisi tecnica per le motivazioni, il template del diario.
2. La politica dei branch è confermata (D21): creare da `main` il branch di integrazione
   `feature/preflop-blueprint` e il branch di fase `feature/preflop-blueprint-p0-scaffolding` in
   un worktree separato dal working tree dell'utente.
3. Eseguire P0 e registrare la prima voce del diario con commit, ambiente e tag.
4. Procedere fase per fase secondo §3.3. Ogni gate produce una riga nel registro dei gate e un
   report `Px_NOME.md`.
5. Le domande per l'utente si scrivono nel diario §4, con il contesto necessario a rispondere
   senza rileggere il codice; poi l'agent si ferma e riporta la domanda nel proprio messaggio
   finale.
6. Alla consegna, verificare la lista di §8 e aggiornare lo stato corrente del diario.
