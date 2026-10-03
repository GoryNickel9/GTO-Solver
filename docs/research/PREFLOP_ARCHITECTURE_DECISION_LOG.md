# Registro decisioni: nuova architettura del solver preflop

Documento vivo. Raccoglie decisioni, chiarimenti e punti aperti emersi nelle discussioni sul
solver preflop Short Deck (HU prima, multiway poi). Ogni aggiornamento è datato nel changelog in
coda. L'analisi tecnica di partenza è in
[HU_PREFLOP_ALGORITHM_AND_ABSTRACTION_ANALYSIS_2026-09-15.md](../archive/preflop-blueprint-research-2026-09/HU_PREFLOP_ALGORITHM_AND_ABSTRACTION_ANALYSIS_2026-09-15.md).

Ultimo aggiornamento: 2026-09-21.

> **Registro congelato il 2026-10-03.** Dal 2026-09-21 in poi le decisioni non vengono più
> registrate qui. Si trovano in tre posti:
>
> - nelle voci datate del diario [`PROGRESS_LOG.md`](preflop_vector_cfr/PROGRESS_LOG.md);
> - per la ricetta di MonkerSolver, in
>   [`MONKER_RECIPE_REPRODUCTION_2026-09-28.md`](preflop_vector_cfr/MONKER_RECIPE_REPRODUCTION_2026-09-28.md)
>   (§1 per le decisioni del 28/09, §8 per i passi successivi);
> - per il 3-way, nelle specifiche di `preflop_vector_cfr/threeway/`.
>
> Le decisioni ancora da prendere sono nell'handoff
> [`NEXT_STEPS_2026-10-02.md`](../handoff/NEXT_STEPS_2026-10-02.md), §7.
>
> L'architettura descritta in §3-§6 resta la motivazione del solver in uso. Dopo il 21/09 alcune
> decisioni di questo registro sono cambiate:
>
> - **D1, D2, D27, gate di accettazione: superate.** Dal 2026-09-28 il gate era solo la best
>   response esatta dentro l'astrazione ≤ 0,03 a, e il fisico restava una misura di qualità.
>   Quel gate è stato ritirato il 2026-10-01 insieme a history7. I nuovi criteri sono una
>   proposta da approvare (handoff, T16).
> - **D28-D31: ritirate.** Budget di memoria e mandato del goal HU20/HU30/HU40 valevano per la
>   suite HU10-HU40, ritirata il 2026-10-01; il codice è stato tolto il 2026-10-03 e l'ultimo
>   albero che lo contiene è al tag `history7-final`.
>
> Le decisioni principali prese dopo il 21/09:
>
> - la ricetta di MonkerSolver come via verso il multiway (28/09);
> - HU50 con le righe per classe di board come standard HU, e la rimozione di history7 (01/10);
> - le fasi 1-3 del 3-way;
> - un solo solver con preflop e postflop, in cui `gto_cli` resta il motore del postflop HU
>   esatto (01-02/10);
> - l'archivio dei documenti (02/10, eseguito il 03/10).

## 1. Decisioni prese

| # | Decisione | Dettaglio | Data |
|---|---|---|---|
| D1 | Gate di qualificazione | Exploitability fisica misurata (best response esatta della strategia sollevata nel gioco CO40) più EV CO con intervallo. Le frequenze Monker restano descrittive, con metrica pesata per perdita EV. | 2026-09-15 |
| D2 | Soglia fisica iniziale | Massimo guadagno di deviazione per giocatore ≤ 0,1 ante per mano. Si riportano anche NashConv (somma), la normalizzazione sul pot iniziale (3a) e sullo stack (40a). Da stringere quando l'astrazione migliora. **Stretta a 0,03a (1 % del piatto) da D27 il 2026-09-18.** | 2026-09-15 |
| D3 | Criterio di arresto del training | Nel gioco astratto: massimo guadagno ≤ 1 % del pot iniziale (0,03a), stimato sui board campionati con intervallo. Coerente con il gate del prodotto postflop. | 2026-09-15 |
| D4 | EV CO vs Monker (−0,30a) | Descrittivo con intervallo; non è un gate. Il valore del gioco dipende poco dall'astrazione, quindi resta un controllo di sanità. | 2026-09-15 |
| D5 | Separazione dei solver | Preflop e postflop restano separati: nessun link e nessuna copia di kernel dal postflop. Il kernel di showdown si scrive ex novo nel trainer preflop. ProductionDcfr (solver postflop) è oracolo nei test. | 2026-09-15 |
| D6 | Fixture di validazione | HU10 (`benchmarks/fixtures/hu_preflop_hu10_calibration_v1.json`) per validare formule, resume, determinismo e uguaglianza con l'oracolo FiniteGame; poi CO40. | 2026-09-15 |
| D7 | Ordine dei formati | Chiudere l'HU, poi 3-way, poi 4/5/6-way. | 2026-09-15 |
| D8 | Multiway postflop | Il postflop modella 3 o più giocatori ancora in gioco. Quando un giocatore folda il sottogioco torna HU e usa il kernel HU. | 2026-09-15 |
| D9 | Struttura del gioco multiway | Ante 1a per ogni giocatore più button blind 1a sul BTN (pot iniziale N+1 ante). Size e vincoli sui raise uguali all'HU. Stack uguali (nessun side pot). | 2026-09-15 |
| D10 | Posizioni per table size | 3-way UTG, CO, BTN. 4-way UTG, MP, CO, BTN. 5-way UTG, MP, HJ, CO, BTN. 6-way UTG, UTG+1, MP, HJ, CO, BTN. Ordine di azione UTG → BTN, preflop e postflop (BTN sempre ultimo, come in HU). | 2026-09-15 |
| D11 | Risorse precalcolate | Distribuite con l'applicazione (risorse "warm"). Il tempo di costruzione si riporta a parte e non entra nel tempo di solve. | 2026-09-15 |
| D12 | Archiviazione | Graduale: tag git sullo snapshot, poi esclusione dei percorsi sperimentali dalla build tramite opzione CMake dopo il PASS HU10 del nuovo trainer. Nessuna cancellazione dalla storia. | 2026-09-15 |
| D13 | Struttura del trainer | Scritto da subito con N vettori di reach e kernel di showdown sostituibile (esatto HU, inclusione-esclusione 3-way, campionato oltre). | 2026-09-15 |
| D14 | Thread | Sempre tutti i thread disponibili. Riproducibilità bit-identica mantenuta con il parallelismo per sottoalbero (ogni cella aggiornata da un solo thread, board in ordine fisso). | 2026-09-15 |
| D15 | Collocazione del nuovo trainer | Libreria separata (`libs/preflop_blueprint` o nome equivalente), dipendente solo da core, equity, tree e da un piccolo modulo con configurazione, albero preflop e ledger; libreria `card_abstraction` distinta e riusabile nel multiway. Il vecchio trainer resta intatto come confronto fino all'archiviazione (D12). | 2026-09-15 |
| D16 | Budget di tempo | Il budget di 2 ore su HU40 è eliminato. Il solve è guidato dal target (D3) e il tempo è un risultato riportato, non una condizione di PASS/FAIL. La passata esatta di certificazione resta uno strumento di sviluppo separato dal solve dell'utente. | 2026-09-15 |
| D17 | Verifica a stadi con azioni ridotte | Durante l'implementazione si verifica algoritmo e NashConv su HU10 con un albero ridotto (una size postflop più all-in), poi con le size complete, poi su CO40. Ogni insieme di azioni definisce un gioco diverso: la strategia del gioco ridotto non è un equilibrio del gioco completo e va riaddestrata (con eventuale warm start) sull'albero completo. La correttezza del codice verificata sul piccolo vale sul grande. | 2026-09-15 |
| D18 | Chart viewer | Aggiornato al nuovo formato di output insieme al trainer (fase P8). Il viewer è un repository git separato in `tools/hu_preflop_chart_viewer`, ignorato dal repository principale: i suoi commit vanno fatti lì. | 2026-09-15 |
| D19 | Roadmap e diario dell'agent | Roadmap operativa P0–P10 in [PREFLOP_VECTOR_CFR_CODER_ROADMAP_2026-09-15.md](../archive/preflop-blueprint-research-2026-09/PREFLOP_VECTOR_CFR_CODER_ROADMAP_2026-09-15.md). L'agent coder mantiene il diario `preflop_vector_cfr/PROGRESS_LOG.md` con progressi, fallimenti, dubbi, registro dei gate e decisioni interne. | 2026-09-15 |
| D20 | Calendario dell'archiviazione (rivisto il 2026-09-15) | Documenti: rimossi subito dal working tree, con tag `preflop-legacy-es-2026-09-15` sul commit `04aa687` e indice `PREFLOP_LEGACY_INDEX.md`; nessuna cartella `docs/archive/` per il preflop, perché un agent che legge `docs/` potrebbe seguirli (eseguito, D25). Codice legacy in due stadi: stadio 1, dopo il gate P6, opzione CMake `GTOSD_BUILD_LEGACY_PREFLOP_RESEARCH` default `OFF` per trainer legacy, decomposizione CFR-D, certificatore per sottogiochi river e relativi test, restando attivi FiniteGame, contratto di averaging, tabella a 7 carte, fixture e comparatore; stadio 2, dopo il gate P9, sorgenti legacy fuori dalla build di default. Mai cancellazioni dalla storia git; mai rimozione di fixture, analisi e decisioni. | 2026-09-15 |
| D21 | Politica dei branch (confermata il 2026-09-15) | `main` resta sempre verde (build e CTest). Un branch di integrazione `feature/preflop-blueprint` da `main` per tutto il programma P0–P10; un branch per fase `feature/preflop-blueprint-pN-nome`, unito nell'integrazione via PR con l'evidenza del gate nel diario; l'integrazione si unisce a `main` ai gate P3, P6, P8, P9 con tag (`preflop-blueprint-p6-hu10`, `preflop-blueprint-p9-co40`). L'agent lavora in un worktree separato, senza toccare il working tree dell'utente; niente force push, niente riscrittura della storia; commit `feat(preflop-blueprint): ...` con la riga di attribuzione richiesta. I branch `research/*` esistenti restano come evidenza e non vengono uniti. Il viewer segue lo stesso schema nel proprio repository. | 2026-09-15 |
| D22 | Autonomia dell'agent | L'agent è autonomo da P0 a P10 e si ferma soltanto su `FAIL`, `INCONCLUSIVE` o decisioni fuori perimetro. Nessuna conferma intermedia ai gate. | 2026-09-15 |
| D23 | Avvio dell'implementazione | P0 viene avviato da un agent coder, con roadmap, registro e diario come input (roadmap §9). | 2026-09-15 |
| D24 | Parametri di default e paletti | L'agent può validare e cambiare i parametri di default (capacità dei bucket, bin, gruppi OCHS, riavvii k-means, batch, board di valutazione, schema dei pesi) partendo sempre dai default come baseline, entro gli intervalli e le condizioni della roadmap §3.4, con confronto matched registrato nel diario. Non modificabili senza decisione dell'utente: regole, size, ledger, soglie D2 e D3, definizione delle metriche, feature esatte (niente Monte Carlo), deal fisici, separazione dal postflop. | 2026-09-15 |
| D25 | Rimozione dei documenti superati (eseguita) | Rimossi dal working tree 706 file tracciati: vecchia roadmap R0–R9, studi 12 ore e CPU/RAM, evidenze `preflop_12h_evidence`, fasi R1–R5, cartella R6 (V6–V23). `docs/` passa da 7,0 GB a 1,8 MB. I 16 `.bin` non tracciati (6,9 GB) sono spostati in `benchmarks/results/legacy_preflop_es/`: le 14 policy V8–V16 e V18 sono cancellabili, le due V17 restano fino al gate P9. Restano `preflop_r0` (contratto monetario) e il benchmark CO40, con banner e gate riallineati a D1–D4. Indice: `PREFLOP_LEGACY_INDEX.md`. | 2026-09-15 |
| D26 | Pulizia dei branch (eseguita, con residuo) | Sei tag `archive/research/<nome>` creati e pubblicati su origin insieme a `preflop-legacy-es-2026-09-15`. Cancellati su origin `research/s6-production-qualification-20260831`, `research/tst-strict-2gb-bottleneck-loop-20260831` e `copilot/fix-windows-asan-clang-job`; su origin restano solo `main` e i tag. In locale rimossi tre worktree puliti e cancellati i branch `dcfr-epoch-reset`, `gto-plus-autonomous-black-box`, `range-aware-orbit-oracle`. Residuo: tre worktree con scratch e i loro tre branch (A8). | 2026-09-15 |
| D27 | Soglia fisica stretta all'1 % del piatto | Il gate di accettazione passa da 0,1 ante per giocatore (3,3 % del piatto iniziale di 3a) a **0,03 ante, l'1 % del piatto**. Decisione dell'utente del 2026-09-18. Motivazione: D2 era dichiarata "soglia fisica *iniziale*" e "da stringere quando l'astrazione migliora", e l'1 % del piatto è già lo standard con cui è stato accettato il prodotto postflop, come registra D3. La soglia resta ancorata al piatto e indipendente dallo stack. Conseguenza immediata: l'unico gioco qualificato resta **HU10** (0,003981a, 0,13 % del piatto); HU20 con `class` vale 0,060957a, il 2,03 % del piatto, e manca il gate di 2,03 volte; CO40 con `class` vale 0,500181a, 16,7 volte il gate. | 2026-09-18; **superata il 2026-09-28, gate ritirato il 2026-10-01** (vedi la nota in testa) |
| D28 | Memoria per il goal HU20/HU30/HU40 | L'utente elimina esplicitamente il limite di 4 GiB e lo sostituisce con **12 GiB**. Si aggiorna il limite dello stato numerico in roadmap §3.4; restano float64, fixture congelate e gate fisico 0,03 ante. Vanno riportati anche memoria di processo e picchi di salvataggio/esportazione. | 2026-09-19; **ritirata il 2026-10-01** (vedi la nota in testa) |
| D29 | Persistenza sul goal HU20/HU30/HU40 | Per mandato esplicito dell'utente, continuare fino alla qualificazione dei tre giochi. Per questo goal è superata la regola D22 di fermarsi al primo FAIL/INCONCLUSIVE: registrare l'esito e proseguire con un controllo fondato. Non cambia i criteri di correttezza né autorizza ad allentare il gate. | 2026-09-19; **ritirata il 2026-10-01** (vedi la nota in testa) |
| D30 | Memoria disponibile durante l'audit HU30 | L'utente sostituisce il limite D28 di 12 GiB con **25 GiB** per gli esperimenti diagnostici del 2026-09-21. I risultati precedenti restano validi come misure storiche. La stima include stato numerico, mappa, temporanei e picchi di persistenza; il layout completo HU30 da 31.138.638.936 byte per R+S+policy non entra nel limite. | 2026-09-21; **ritirata il 2026-10-01** (vedi la nota in testa) |
| D31 | Budget di prodotto del solver HU | HU10, HU20, HU30 e HU40 devono essere risolti con un picco di processo **non superiore a 8 GiB**. I 25 GiB di D30 restano il tetto storico dell'audit, non un obiettivo di prodotto. `history7` HU20 a 11,44 GiB e cap 23 HU30 a 24,67 GiB non soddisfano D31 anche quando producono dati diagnostici utili. | 2026-09-21; **ritirata il 2026-10-01** (vedi la nota in testa) |
| D32 | Un solo solver per la matrice HU | I benchmark misurano il solver e non ne selezionano i parametri. Possono variare soltanto stack, numero di size preflop e numero di size postflop. Astrazione, feature, clustering, regola di capacità, algoritmo, batch, arresto e certificazione seguono una sola politica automatica. Nessuna correzione può dipendere da nome, stack o fingerprint della fixture; deve passare HU10/HU20/HU30/HU40. | 2026-09-21 |

## 2. Punti aperti

Solo i punti effettivamente aperti. I punti chiusi (A1, A2, A4, A5, A6, A7) sono registrati nelle
decisioni corrispondenti e nel changelog; gli identificatori non vengono riassegnati.

| # | Tipo | Punto | Stato |
|---|---|---|---|
| A3 | Dato mancante | Dimensione degli alberi a 3–6 giocatori | Non è un problema né un bug: è un numero che non abbiamo. Per l'HU l'albero ha 58 nodi preflop e 30.324 nodi postflop, e da lì derivano memoria (nodi × bucket × azioni × 16 B) e tempo per board. Per il 3-way con le stesse regole il numero di nodi non è noto: con tre giocatori le sequenze di limp, raise e risposte si moltiplicano, e ogni ingresso postflop con tre giocatori vivi ha un albero molto più grande di quello HU. Non si calcola a mano in modo affidabile: serve il builder a N giocatori, che oggi non esiste. P10 lo costruisce e produce il conteggio. Dal numero dipendono tre scelte: quanti bucket per street stanno in memoria (esempio: 300.000 nodi postflop × 1.000 bucket × 4 azioni × 16 B = 19 GB, troppi; con 200 bucket 3,8 GB), quanto costa una passata per board, e se i giocatori dopo il primo raise devono avere meno size. Il supporto a 3 o più giocatori non esiste oggi e non è il risultato di P10: kernel di showdown a tre, trainer e certificazione multiway avranno una roadmap propria dopo il conteggio. P10 dipende solo dal gate P4 e può essere anticipata in parallelo a P5–P9. |
| A8 | Decisione in attesa | Pulizia dei branch esistenti | Precisazione: nessuno dei branch esistenti riguarda il preflop. Tutto il lavoro preflop (R0–R6, V1–V23) è stato committato direttamente su `main`; i sei branch `research/*` sono cicli di ricerca del solver postflop (ProductionDcfr, GTO+ black-box, cap 2 GiB, sync PCFR, orbit oracle, qualificazione S6). Stato verificato il 2026-09-15: sei branch locali `research/*` del 31 agosto e 1 settembre, ciascuno con 2–8 commit non in `main` per hash, ma il cui contenuto è in `main` o per patch identica o reintegrato con lo stesso subject e modifiche successive; `main` è avanti di 51 commit su tutti. Ognuno ha un worktree collegato in `C:/tmp/...` (1,8 GB complessivi; tre contengono cartelle `.tmp` di scratch sotto 1 MB). Su origin: due degli stessi `research/*` e `copilot/fix-windows-asan-clang-job`, un solo commit vuoto "Initial plan" del 28 luglio senza file. Proposta: creare tag `archive/research/<nome>` su ciascun branch (i commit restano raggiungibili), rimuovere i worktree, cancellare i branch locali e remoti, cancellare il branch copilot. Eseguito il 2026-09-15 (D26) salvo tre worktree: `C:/tmp/gto-solver-s6-production-qualification-20260831`, `C:/tmp/gtosd-exact-algorithm-recheck-20260901`, `C:/tmp/gtosd-tst-strict-2gb-20260831`, che contengono solo cartelle `.tmp` di scratch non tracciate (sotto 1 MB) e la cui rimozione forzata è stata bloccata dal classificatore di sicurezza dell'agent. I relativi branch locali `research/exact-algorithm-recheck-20260901`, `research/s6-production-qualification-20260831`, `research/tst-strict-2gb-bottleneck-loop-20260831` non possono essere cancellati finché i worktree esistono. Comandi da eseguire a mano: `git worktree remove --force <percorso>` per i tre percorsi, poi `git branch -D` per i tre branch. I loro commit sono già al sicuro nei tag `archive/research/*`, anche su origin. |

## 3. Architettura in sintesi

### 3.1 Astrazione delle carte

Raggruppare situazioni private diverse che condividono la stessa strategia. Le 81 classi preflop
sono già un'astrazione senza perdita (simmetria dei semi). Nel postflop ogni coppia (mano, board)
riceve per ogni street un numero di bucket letto da una tabella precalcolata; le mani nello
stesso bucket condividono regret e strategia, mentre card removal, chance e payoff restano
fisici. Tutte e tre le street postflop sono astratte; il numero di bucket per street è un
parametro (candidati iniziali `200/500/1.000` e `500/1.000/2.000`).

Feature esatte, calcolate una sola volta con la tabella a 7 carte:

| Street | Feature | Clustering |
|---|---|---|
| Flop | istogramma dell'equity al river sui 465 runout, contro 406 mani avversarie | k-means con distanza EMD |
| Turn | istogramma dell'equity al river sui 30 river, contro 378 mani avversarie | k-means con distanza EMD |
| River | equity esatta contro 406 mani, suddivisa per 8 gruppi di forza avversaria (OCHS) | k-means L2 |

Memoria imperfetta: la chiave postflop è (history pubblica completa, bucket della street
corrente). Il bucket al river non ricorda quello del flop.

Esempio del perché servono le distribuzioni e non la sola equity media: sul flop A♠K♠9♦ una
coppia media come 9♥8♥ e un progetto di colore come T♠8♠ possono avere equity media vicina ma
distribuzione futura opposta (la coppia resta media quasi sempre, il progetto diventa forte in
circa un caso su tre e debole altrimenti). L'istogramma le separa, l'equity media a 8 campioni
usata oggi no. La scelta finale la fanno le feature esatte, non l'intuizione sulle singole mani.

Perché non raggruppare per categoria della mano al flop e al turn: la categoria descrive solo la
mano fatta e ignora i progetti, che al flop e al turn sono la parte più importante della forza.
T♠8♠ e J♠8♠ su A♠K♠9♦ sono entrambe "carta alta" per la categoria e sono entrambe progetti di
colore da cinque carte con potenziale quasi identico; Q♦J♣ è "carta alta" ma è un progetto di
scala massima da quattro carte. La categoria non li distingue; gli istogrammi sì, e mettono T♠8♠ e
J♠8♠ nello stesso bucket senza che nessuno abbia codificato a mano il concetto di progetto di
colore. Un'altra regola: la feature non può dipendere dalla mano effettiva dell'avversario, che è
informazione privata; si misura contro la distribuzione delle mani avversarie compatibili. La
categoria può entrare come feature aggiuntiva, ma è il clustering a decidere se serve. Al river,
dove non c'è futuro, la forza della mano fatta è quasi tutto: l'equity per gruppi avversari (OCHS)
la cattura insieme agli effetti dei blocker.

### 3.2 Algoritmo di training

CFR vettoriale con campionamento del board (public chance sampling, che è una variante di MCCFR):

1. per ogni iterazione si campiona un batch di board completi;
2. per ogni board una sola traversata dell'albero pubblico con vettori di reach sulle combo
   compatibili (465 con cinque carte pubbliche note; 630 sono le combo senza board);
3. strategie lette dalla classe preflop (81) e dal bucket postflop; regret e media accumulati per
   cella (nodo pubblico, bucket) sommando sulle mani della cella;
4. showdown con ordinamento per rank e somme prefisse con correzione dei blocker; fold e all-in
   dal ledger; all-in preflop da tabella esatta per coppia di combo;
5. update alternati dei giocatori; schema dei pesi: Linear CFR come baseline, DCFR `1,5/0/2`
   applicato una volta per iterazione come sfidante, confronto a pari tempo.

Perché è meglio dell'external sampling attuale: il valore di ogni azione per ogni mano è
l'attesa esatta sulle mani avversarie invece di un singolo esito campionato, e una traversata
aggiorna tutte le mani su tutti i nodi. A parità di tempo i contributi per information set
crescono di tre o quattro ordini di grandezza. La convergenza nel gioco astratto è quindi molto
più rapida; l'exploitability fisica resta limitata dall'errore di astrazione, che dipende dai
bucket e non dall'algoritmo.

### 3.3 Certificazione

Best response esatta nel gioco fisico della strategia sollevata: una traversata vettoriale per
ciascuna delle 369.072 board history canoniche con molteplicità, `max` per mano e nodo. Output:
guadagni per giocatore in ante per mano, NashConv, normalizzazioni su pot e stack, EV del
profilo. Stima campionata con intervallo durante il training. Nessun certificato sul gioco
astratto a corpus (V23): resta un oracolo su HU10.

### 3.4 Parallelismo

Board in sequenza; per ogni board i thread si dividono sottoalberi disgiunti dell'albero pubblico.
Nessuna copia dello stato, nessuna race, risultato indipendente dal numero di thread.
Aspettativa sull'i3-10100F (4 core, 8 thread): 2,5–3× rispetto a un thread.

### 3.5 Multiway

Stessa architettura con N vettori di reach. Showdown a tre: ordinamento per rank e
inclusione-esclusione sulle carte condivise, validato contro l'enumerazione brute force. Da
quattro giocatori: mani avversarie campionate. Per N > 2 CFR non garantisce Nash: si riportano i
guadagni di deviazione per giocatore. L'albero pubblico cresce in modo esponenziale con N: il
conteggio (A3) precede ogni scelta di memoria.

## 4. Risorse precalcolate (distribuite con l'applicazione)

Dipendono solo da mazzo e ranking, non da stack, size o numero di giocatori.

| Risorsa | Contenuto | Dimensione | Uso |
|---|---|---|---|
| Tabella di rank ordinali | rank ordinale a 16 bit di ogni insieme di 7 carte (8.347.680) e di 5 carte (376.992), derivato dall'evaluator esatto; 1.404 rank distinti | 17,4 MB | rank al river per board (465 lookup); costruzione offline delle feature |
| Tabelle bucket per street | bucket di ogni combo (630) per ogni board canonico: 573 flop, 13.761 flop+turn, 19.998 board da cinque carte | ≈ 40 MB | lookup a runtime |
| Tabella all-in preflop | conteggi win/tie/lose esatti sui 201.376 runout per ciascuna delle 176.715 coppie di combo disgiunte (tabella triangolare da 198.135 voci) | 2,4 MB | terminali all-in preflop |
| Feature esatte (offline, non distribuite) | istogrammi a 16 bin dell'equity al river per flop (465 runout) e turn (30 river); equity esatta al river contro tutti e contro 8 gruppi avversari | 11,6 + 138,7 + 226,8 MB | input del clustering P3 |
| Cataloghi canonici | board canonici con molteplicità per street (573 / 13.761 / 19.998 / 369.072) | 7,7 MB su file, ricostruibili in 2,6 s | passata esatta e campionamento stratificato |
| Tabelle a 5 e 6 carte (opzionali) | valori di mano fatta a flop e turn | 1,5 + 7,8 MB | accelerano solo la costruzione offline delle feature |
| Rank ordinati per board al river (opzionale) | 465 rank ordinati per ciascuno dei 19.998 board | 37 MB | elimina ordinamento e lookup per board; a runtime costano comunque poco |

Costo di costruzione una tantum: 10–30 minuti per bucket e all-in sull'i3. Gli all-in su flop e
turn non richiedono tabelle: con il board campionato lo showdown al river è già una stima non
distorta.

"Offline" significa: sulla macchina di sviluppo, una sola volta, prima di distribuire i file. A
runtime il trainer non valuta mai una mano al flop o al turn, perché legge il bucket dalla
tabella; al river usa la tabella a 7 carte 465 volte per board. Le tabelle a 5 e 6 carte
servirebbero solo a calcolare più in fretta, in fase di costruzione, la mano fatta al flop e al
turn e la tabella a 7 carte stessa. Non cambiano nulla per l'utente.

## 5. Memoria attesa

| Componente | Oggi (V17/V19, 2 M iterazioni) | Proposta 200/500/1.000 | Proposta 500/1.000/2.000 |
|---|---:|---:|---:|
| Stato numerico | 1,57 M righe sparse × 136 B più hash map | 630 MB densi | 1,25 GB densi |
| Tabelle (bucket, all-in, 7 carte) | cache fino a 1 M voci ciascuna | ≈ 75 MB | ≈ 75 MB |
| Scratch per thread | delta sparsi, cap 512 MiB | ≈ 1 MB | ≈ 1 MB |
| Picco processo | 2,7–2,9 GB misurati | < 1 GB atteso | ≈ 1,5 GB atteso |

Lo stato denso conta 11.308 nodi decisionali postflop, 4 azioni medie e 16 B per cella (regret e
somma strategica in double); float32 dimezza. La passata di certificazione non aggiunge stato.

## 6. Catalogo delle astrazioni

| Famiglia | Varianti | Effetto | Stato nel progetto |
|---|---|---|---|
| Carte, senza perdita | simmetria dei semi (81 classi, board canonici) | riduce chance e stato senza errore | in uso |
| Carte, con perdita | equity media (EHS), E[HS²], istogrammi con EMD, potential-aware, OCHS, k-means; memoria imperfetta; astrazione asimmetrica per posizione | riduce lo stato strategico; introduce errore misurabile | proposta: istogrammi + OCHS, memoria imperfetta |
| Board (chance), senza perdita | 573 flop canonici, 19.998 board da cinque carte, 369.072 history canoniche | riduce le passate esatte | in uso |
| Board, con perdita | sottoinsiemi pesati di flop; classi di turn e river (brick, overcard, completa colore, accoppia il board); cluster di texture | riduce le passate esatte e i board distinti; errore dichiarato | opzione per il multiway e per passate esatte più rapide |
| Azioni | insieme di size per street e posizione; limite ai raise; fusione di size sulle street finali; traduzione delle azioni fuori albero (solo in gioco, non nel solve) | riduce l'albero pubblico | fissata uguale all'HU; leva principale nel multiway |
| Struttura dell'albero | depth-limited con valori di foglia multipli; decomposizione trunk/sottogiochi (CFR-D); chiusura anticipata al turn con equity | riduce profondità e costo | non ora; CFR-D già respinta per costo |
| Spazio delle strategie | pruning delle azioni con regret molto negativo; purificazione a posteriori | riduce il lavoro per iterazione | pruning valutabile dopo la baseline |
| Approssimazione funzionale | Deep CFR, reti di valore | sostituisce le tabelle | fuori perimetro |

Non si fa: unire nodi pubblici con pari pot e stack (strategie e range differiscono per history).

## 7. Piano di lavoro

Il piano operativo dettagliato, con fasi P0–P10, gate e test, è nella
[roadmap per l'agent coder](../archive/preflop-blueprint-research-2026-09/PREFLOP_VECTOR_CFR_CODER_ROADMAP_2026-09-15.md). La sintesi qui sotto
resta come indice.

1. Modulo di astrazione: enumerazione canonica, feature esatte, clustering, tabelle, test di
   determinismo, invarianza ai semi, copertura, assenza di leakage.
2. Trainer vettoriale su HU10 con parallelismo per sottoalbero, prima con albero ridotto (una
   size postflop più all-in) poi con le size complete (D17); confronto con l'oracolo FiniteGame;
   bit-identità 1/8 thread.
3. Certificatore board-major; uguaglianza con `calculate_nash_conv` sulla fixture V21.
4. Confronto Linear vs DCFR a pari tempo su CO40 con la metrica D3; passata esatta finale (D1,
   D2).
5. Report e comparatore aggiornati; archiviazione graduale (D12).
6. Tree builder a N giocatori e conteggi (A3); kernel di showdown 3-way; primo solve 3-way.

## 8. Glossario

- **Strategia sollevata:** la strategia per bucket applicata a ogni combo fisica del bucket.
- **Exploitability / NashConv:** guadagno che un avversario ottimale otterrebbe deviando dal
  profilo; NashConv è la somma dei guadagni dei giocatori. Si misura in ante per mano.
- **Public chance sampling:** si campionano solo le carte pubbliche; mani e azioni sono enumerate.
- **Memoria imperfetta:** la chiave di una decisione non ricorda i bucket delle street precedenti.
- **Passata esatta:** visita di tutte le board history canoniche con molteplicità; è
  un'iterazione esatta di CFR o una best response esatta.

## 9. Changelog

- 2026-09-15: creazione del registro. Decisioni D1–D14, punti aperti A1–A3, architettura,
  risorse, memoria, catalogo delle astrazioni, piano. Corretto l'esempio del flop A♠K♠9♦: Q♦J♣ ha
  un gutshot e non è un buon esempio di mano "vuota"; sostituito con coppia media contro progetto
  di colore.
- 2026-09-15, secondo aggiornamento: D15 libreria separata, D16 budget di 2 ore eliminato, D17
  verifica a stadi con azioni ridotte; chiusi A1 e A2; aggiunto A4. Aggiunte le note su categoria
  contro distribuzione (§3.1) e sul significato di "offline" (§4).
- 2026-09-15, terzo aggiornamento: D18 viewer, D19 roadmap e diario dell'agent, D20 calendario
  dell'archiviazione, D21 politica dei branch (proposta); chiuso A4; A3 dettagliato e trasformato
  nella fase P10; aggiunto A5. Creati la roadmap P0–P10 e il template del diario in
  `preflop_vector_cfr/PROGRESS_LOG.md`.
- 2026-09-15, quarto aggiornamento: rimossi dai punti aperti quelli chiusi (A1, A2, A4); A3
  riformulato come lavoro pianificato con stato esplicito "non implementato"; aggiunti A6
  (autonomia dell'agent fra i gate) e A7 (avvio dell'implementazione).
- 2026-09-15, quinto aggiornamento: D22 autonomia (chiude A6), D23 avvio con agent coder (chiude
  A7), D24 default e paletti; A3 riscritto come "dato mancante" con spiegazione ed esempio
  numerico; aggiunto A8 (pulizia dei branch esistenti, proposta). Roadmap aggiornata con §3.3
  autonomia, §3.4 paletti e §9 istruzioni di avvio.
- 2026-09-15, sesto aggiornamento: D21 confermata (chiude A5); P10 dichiarata dipendente solo da
  P4 ed eseguibile in parallelo; precisato in A8 che nessun branch esistente riguarda il preflop.
  Resta aperto solo A8.
- 2026-09-15, settimo aggiornamento: eseguita la rimozione dei documenti superati (D25, D20
  rivisto, A9 chiuso), creato `PREFLOP_LEGACY_INDEX.md`, riallineati benchmark CO40 e stato di
  implementazione; pulizia dei branch eseguita con residuo di tre worktree (D26, A8 aggiornato).
- 2026-09-18/19: soglia fisica stretta a 0,03 ante (D27), memoria portata prima a 12 GiB (D28)
  e mandato di persistenza sul goal HU20/HU30/HU40 registrato in D29.
- 2026-09-21: memoria disponibile per l'audit portata a 25 GiB (D30); il layout completo HU30
  resta oltre il limite. L'audit causale dei bucket è registrato nel diario P9. Il successivo
  requisito di prodotto fissa il picco a 8 GiB (D31) e vieta il tuning per singolo benchmark
  (D32).
