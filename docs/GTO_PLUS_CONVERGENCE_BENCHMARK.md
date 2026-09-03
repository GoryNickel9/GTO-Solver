# Benchmark end-to-end di convergenza GTO+

> **CONTRATTO RISORSE V3 — 2026-09-02.** Ogni fixture dichiara
> `gto_plus_reference.peak_rss_bytes`. Il `memory_gate` ufficiale confronta il
> massimo `peak_rss_bytes` del processo con quel riferimento usando `<=`.
> `solver_state_gate` e `desktop_memory_gate` (`<2 GiB`) restano separati e il
> secondo non decide la parità. I report run/summary correnti sono v3.

> **STATO: SPECIFICA V1 CONGELATA / RIFERIMENTO STORICO.** Il percorso v3 è
> parametrico e va usato per nuovi scenari; questo documento conserva il
> protocollo della fixture `GTP-AHKHQH-003` senza sostituire il parity journey.

Benchmark ID: `GTP-AHKHQH-003`
Schema: `gtosd.gto_plus_convergence_benchmark.v1`

> **Checkpoint operativo 2026-09-02.** Il protocollo v3 è applicato alla suite
> AHKHQH/TH7D6S/TSTC9D senza iteration cap e con arresto stretto a
> `Target dEV < 1%`. Cinque processi production passano dEV, root, layout,
> exact outcomes e stato su `15/15` solve. Il fresh audit memoria classifica
> AHK `166.789.120/8.000.000 B` FAIL, TH `799.059.968/399.000.000 B` FAIL e
> TST `1.970.229.248/2.000.000.000 B` PASS. Tempo e memoria restano gate
> indipendenti.
> Valori e report autorevoli sono nel parity journey. Le sezioni v1 e le
> diagnosi successive restano evidenza storica del protocollo, non il dashboard
> corrente.
> Il protocollo misura esclusivamente il backend CPU/RAM: non sono ammessi
> offload o kernel GPU durante solving e certificazione.

## Scopo e limite

Il benchmark misura il tempo GTOSD dal comando `Run Solver` su un albero già
preparato fino alla prima certificazione exact che soddisfa la stessa semantica
di `Target dEV` usata da GTO+:

```text
gain[p] = BR_value[p] - profile_value[p]
GTO+ dEV = max(gain[CO], gain[BTN]) / initial_pot
```

`NashConv/Pot = (gain[CO] + gain[BTN]) / initial_pot` resta pubblicato come
metrica diagnostica, ma non decide l'arresto. Il run enumera tutti gli outcome,
non usa sampling o bucketing e usa soltanto isomorfismo lossless. La modalità
prestazioni conserva regret e strategy sum in `float32`, esegue payoff,
traversal e certificazione in `float64` e viene confrontata col percorso
`float64` nei test differenziali.

La fixture esterna registra `1,71 s`, `8.000.000 byte`, Target dEV `1%` e tre
EV flop condizionali GTO+: CO root `19,15`, BTN dopo check CO `21,65`, BTN dopo
bet 20 CO `17,51`. Gli EV usano la convenzione visuale GTO+: payoff netto
condizionale più la quota iniziale di 20 ante del giocatore. La fixture registra
inoltre le frequenze GTO+ visibili: root `check 80,3% / bet 20 19,7%` e BTN dopo
bet 20 `fold 37,4% / call 62,6% / raise 60 0,0%`.

La tolleranza storica di `0,05 ante` sui nodi BTN resta pubblicata per
tracciabilità, ma dal 2026-08-02 non è più interpretata come gate autonomo di
correttezza: due profili con strategie root differenti inducono posteriori
privati differenti dopo bet/check e quindi non definiscono lo stesso subgame
condizionale. Il confronto torna causale soltanto dopo avere reso uguali tali
posteriori, per esempio con il root lock diagnostico descritto sotto.
L'utente ha confermato che GTO+ e GTOSD girano sulla stessa macchina e che gli
`1,71 s` vanno dal click su `Run Solver`, con albero già preparato, fino alla
soluzione completa consultabile. La versione osservata è GTO+ v1.6.9 64-bit
(8 thread). L'unità della memoria dichiarata è confermata (MB decimali: 8 MB =
8.000.000 byte, confronto diretto con il peak RSS GTOSD in byte).
L'action tree GTO+ è registrato a livello flop — 4 nodi, action set, frequenze
ed EV combo-per-combo in `docs/specifications/gtoplus_specs.md` — mentre turn
e river restano da acquisire; la certificazione scientifica definitiva resta
quindi pending, anche se i gate prestazionali sono misurabili.

La semantica è coerente con la spiegazione pubblica del rappresentante GTO+:
dEV è il massimo EV ottenibile sfruttando le imprecisioni dell'avversario
([fonte](https://forumserver.twoplustwo.com/167/poker-software/gto-cardrunnersev-155966/index493.html#post57429107)).
La soglia `1%` non è inferita dalla fonte: è il valore confermato dall'utente
per il run sorgente da `1,71 s`.

## Componenti

- `benchmarks/fixtures/gto_plus_ahkhqh_003.json`: contratto versionato, target e
  conteggi golden;
- `benchmarks/fixtures/gto_plus_ahkhqh_103.json` e `gto_plus_ahkhqh_104.json`:
  benchmark v3 costruiti dai valori degli export GTO+ documentati nei markdown
  (vedi sotto);
- `schemas/gto_plus_convergence_benchmark.schema.json`: schema del contratto;
- `gto_cli postflop benchmark-gto-plus`: un singolo processo indipendente e un
  report atomico `gtosd.gto_plus_convergence_run.v3`;
- `tools/run_gto_plus_convergence_benchmark.ps1`: almeno cinque processi,
  mediana, p95 nearest-rank, score e gate separati.

## Specifica v3: benchmark di convergenza generici

Il comando `benchmark-gto-plus` accetta la specifica generica
`gtosd.gto_plus_convergence_benchmark.v3`. Il contratto v1
`GTP-AHKHQH-003` resta congelato nei valori e nella validazione; anche quel
percorso emette ora l'envelope report v3. La v3 rende ogni parametro leggibile
dalla specifica:

- **fixture**: board, `range_co`/`range_btn` (liste di classi tipo
  `AA-QQ,AKs-AQs,KQs,AKo-AQo,KQo`), pot e stack in ante, bet/raise size in
  percentuale del pot, calendario opzionale
  `raise_size_percent_pot_by_raise_count`, `maximum_raises_per_street`,
  semantica all-in (`disabled`, `add_if_push_below_N_percent_pot_after_call`
  oppure `go_if_push_below_N_percent_pot_after_call`),
  `final_bet_smoothing`, `rake_percent`;
- **gtosd_run**: iterazioni, certification interval, averaging delay,
  parallel action depth, thread, processi indipendenti;
- **gto_plus_reference**: target dEV, tempo e `peak_rss_bytes` GTO+ specifico
  della fixture, tolleranze EV e
  frequenze, `gate_node` (opzionale: id del nodo il cui EV decide
  `correctness_passed`; default il root) e `reference_nodes`: una lista di
  nodi indirizzati con `path` (etichette di azione dal root, es. `[]` per il
  root, `["bet_20"]` per il figlio dopo una bet da 20 ante) + `player`, con
  `ev_antes` e/o `actions` attesi. I `path` non devono attraversare un cambio
  strada (i nodi chance non sono risolvibili da un path di sole azioni);
- **expected_layout**: fingerprint e conteggi, come in v1.

Il report corrente usa `gtosd.gto_plus_convergence_run.v3`: conserva i campi
di correttezza e stato, rende `memory_gate` il confronto inclusivo del peak RSS
con il riferimento GTO+ della fixture e pubblica il cap comune soltanto come
`desktop_memory_gate` diagnostico.
Le chiavi applicative restano stabili;
le chiavi di `gto_plus_ev_checks`, `gto_plus_action_frequency_checks` e
`reference_node_action_frequencies` sono gli `id` dichiarati nella specifica
(per `GTP-AHKHQH-003` restano `flop_co_root`, `flop_btn_after_co_check`,
`flop_btn_after_co_bet_20`; cambia soltanto l'envelope del contratto memoria).

Il wrapper `run_gto_plus_convergence_benchmark.ps1` accetta v1, v2 legacy e v3 e
deriva `benchmark_id` dalla specifica; i gate (tempo/memoria, riproducibilità,
EV, frequenze) restano quelli del protocollo. `GTP-AHKHQH-101` è la fixture v3
di validazione: replica esattamente lo scenario di `003` e deve produrre gli
stessi valori, ed è il riferimento per aggiungere nuovi benchmark: si copia la
fixture, si cambia `benchmark_id`, board/range/stack/sizing e i valori GTO+
osservati (`elapsed_seconds`, `peak_rss_bytes`, `target_dev_percent`,
`reference_nodes`), poi si aggiorna `expected_layout` con fingerprint e
conteggi del primo run GTOSD (la prima esecuzione con layout errato fallisce
il gate di layout, non la parità).

Il protocollo non ammette `maximum_iterations`: il solver core riceve
`iterations=0` insieme al target e continua fino alla prima certificazione con
`Target dEV < 1%`. Pausa, cancellazione ed errori espliciti restano gli unici
arresti alternativi; il numero di iterazioni completate è un risultato, non un
input del benchmark.

> **Nuovi benchmark**: parte da `benchmarks/fixtures/TEMPLATE.json`
> (schema-valido, valori noti del 101) e segui la procedura passo-passo in
> `docs/GTO_PLUS_NEW_BENCHMARK_GUIDE.md` — non serve toccare il codice C++.

## Bug risolto: stack overflow del solver con all-in Go a soglia bassa

Durante la validazione della v2 è emerso un bug latente pre-esistente del
solver (non del percorso benchmark): con pot 20 / stack 60, size 33% e una
soglia all-in allora descritta come `≤ 110%` del pot corrente, il processo terminava
con fail-fast `0xC0000409` (stack overflow) durante l'analisi dei nodi di
riferimento. Causa: `DenseTraversal::policy_decision` e
`DenseTraversal::cfr_decision` dichiaravano `action_values` (~39 KB) e
`strategies` (~39 KB) sullo stack in funzioni ricorsive; con alberi profondi
(max_depth 12, raggiungibili con size piccole e all-in tardivo) la ricorsione
superava lo stack da 1 MB dei thread. Fix (libs/postflop/src/postflop_solver.cpp):
i due buffer ora usano la `DecisionScratchLease` già esistente (scratch su
heap, depth-pooled, usata dalle path canoniche). Verificato con repro ASan
prima/dopo: prima stack-overflow in `policy_decision`, dopo nessun errore e
stessi valori numerici; il v1 `GTP-AHKHQH-003` produce lo stesso report di
prima del fix.

## Esecuzione

Compilare `gto_cli` in Release, quindi:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
  .\tools\run_gto_plus_convergence_benchmark.ps1 `
  -BuildDir out/build/windows-release `
  -OutputDir out/gto-plus-convergence
```

Il wrapper produce sempre `summary.json` anche quando dEV o EV sono rossi, per
non perdere l'evidenza diagnostica dei cinque processi. Build non Release,
fingerprint o dimensioni divergenti restano errori strutturali. `-EnforceGate`
restituisce errore quando il gate completo non passa.

Il timer primario continuo include inizializzazione dei buffer, CFR+ alternating,
averaging, certificazione exact BR finale e finalizzazione della soluzione.
Esclude preparazione del tree/layout, avvio del processo e scrittura JSON. Il
wall time comprensivo della preparazione viene pubblicato separatamente. Ogni
run parte senza checkpoint o backing file condiviso e usa al massimo 6 thread.

## Nuovi benchmark dai valori dei markdown (2026-08-02)

La tabella di parità 2026-08-02 (più sotto) contiene i valori combo-per-combo
dei due run GTO+ v1.6.9 dello scenario `Ah Kh Qh`: oltre ai valori arrotondati
già registrati in `003` (`19,15 / 21,65 / 17,51`), riporta i valori precisi di
entrambi i profili. Da questi sono state create due specifiche v2 senza nuovi
export:

- `GTP-AHKHQH-103` — profilo operativo (1,71 s, dEV 0,98%, 8 thread GTO+):
  target 1%, export combo-per-combo completo di `docs/specifications/gtoplus_specs.md`
  (4 nodi flop): EV `19,1588 / 17,5107 / 21,6597 / 11,8959`, root bet
  `7,102/36 = 19,7278%`, frequenze BTN dopo bet (`37,4 / 62,6 / 0,0`), BTN dopo
  check (`bet 25,7% / check 74,3%`) e CO dopo check-bet (`raise 3,75% / call
  56,3% / fold 40,0%`). GTOSD: `correctness=pass` (root EV delta `-0,0372
  ante`), 80 iterazioni, 8 thread allineati a GTO+.
- `GTP-AHKHQH-104` — profilo stretto (4,20 s, dEV 0,19%): target 0,19%, EV
  `19,1581 / 21,6682 / 17,1176`, root bet `6,487/36 = 18,0194%`; frequenze BTN
  non esportate per questo run (nodi solo-EV). GTOSD: `correctness=pass` (root
  EV delta `-0,0077 ante`), converge a 180 iterazioni (dEV 0,1898%), 8 thread.

Entrambe hanno `metadata_complete: false` (restano da registrare turn e river
dell'action tree GTO+; l'unità della memoria è confermata come MB decimali, e
per `104` la memoria è assunta uguale al run operativo, stesso albero). Le
fixture usano lo stesso `expected_layout` di `003` (stesso albero).

### Metadati del tree builder (completati 2026-08-05)

L'utente ha dichiarato le due impostazioni mancanti del tree builder GTO+:
turn e river usano le **stesse size del flop** (50% pot) e l'all-in scatta
**solo quando la bet size supera lo stack rimanente** (regola naturale; la
soglia 150% era un'inferenza non confermata ed è stata rimossa). Questa
correzione ha cambiato l'albero (`165.774` nodi fisici) e i valori del
benchmark; vedi la voce `2026-08-05` nel journey. `101` resta
`metadata_complete: true` (fonte esterna completa per il solo flop); `003`,
`103` e `104` restano `false` perché l'action tree turn/river non è esportato
nodo per nodo — non blocca alcun gate.

## Gate di correttezza: root EV (dal 2026-08-02)

`correctness_passed` è deciso da convergenza, layout/fingerprint e dall'EV GTO+
del **nodo di riferimento del gate**: per `GTP-AHKHQH-003` e per le specifiche
v2 senza `gate_node` esplicito è il root dell'albero (path vuoto), l'unico EV
comparabile in modo causale (vedi "Parità del root EV" più sotto). Gli EV BTN
condizionali e le frequenze restano pubblicati nel report e nel summary come
**diagnostica** (`ev_correctness_passed`, `action_frequency_correctness_passed`)
ma non decidono `correctness_passed`: a posteriori root differenti, due profili
con la stessa strategia root non definiscono lo stesso subgame condizionale.
Con questo gate `GTP-AHKHQH-003` risulta `correctness=pass` (root EV
`-0,0284 ante`, tolleranza `0,05`). Il gate tempo (`≤ 1,9 s`) e la
registrazione completa dei metadati GTO+ (`metadata_complete`) restano
separati e pendenti per `003`.

## Parità del root EV e posteriori BTN — 2026-08-02

Sono stati acquisiti due export completi GTO+ v1.6.9, combo-per-combo, sui tre
nodi flop osservati. Il primo è il run operativo da `1,71 s`, terminato a dEV
`0,390 ante` (`0,98%` del pot). Il secondo usa Target dEV assoluto `0,1 ante` e
termina in `4,20 s` a dEV `0,078 ante` (`0,19%`). Un tentativo separato con il
vero Target `0,10%` è rimasto a dEV `0,045 ante` (`0,11%`) dopo circa `245 s`:
il target non è stato raggiunto, quindi il tempo a `≤0,10%` è censurato a
`>245 s`, non misurato come `245 s`.

| Profilo | CO root EV | CO bet 20 | BTN dopo check | BTN dopo bet 20 |
|---|---:|---:|---:|---:|
| GTO+ dEV 0,98% | 19,1588 | 7,102/36 = 19,7278% | 21,6597 | 17,5107 |
| GTO+ dEV 0,19% | 19,1581 | 6,487/36 = 18,0194% | 21,6682 | 17,1176 |
| GTOSD, 1.000 iterazioni, dEV 0,011334% | 19,163591 | 24,6348% | 22,418551 | 15,996175 |

Il risultato decisivo è il root EV:

```text
delta_root = 19,163591179 - 19,1581 = +0,005491179 ante
```

La differenza è soltanto lo `0,0137%` del pot iniziale. Nella convenzione netta,
GTO+ assegna al CO `-0,8419 ante` e GTOSD `-0,836408821 ante`. Il BTN riceve il
complemento a 40; GTOSD ricompone esattamente:

```text
0,246348407 × 15,996174547 + 0,753651593 × 22,418551039
= 20,836408821
19,163591179 + 20,836408821 = 40
```

Anche il run GTO+ da 1,71 s ricompone, entro le cifre esportate:

```text
(7,102 / 36) × 17,5107 + (1 - 7,102 / 36) × 21,6597
= 20,8411945
19,1588 + 20,8411945 = 39,9999945
```

Quindi non esiste evidenza di un errore di unità, segno, payoff o
normalizzazione al root. I grandi delta condizionali del run più accurato,
`+0,750351 ante` dopo check e `-1,121425 ante` dopo bet, non si contraddicono:
il range CO raggiunto nei due rami dipende dalla strategia root
combo-per-combo:

```text
P(CO hand | action, BTN hand)
  ∝ initial_weight(CO hand)
    × sigma_CO(action | CO hand)
    × compatible(CO hand, BTN hand)
```

GTO+ cambia il bet root dal `19,7278%` al `18,0194%` tra i due run mentre il
root EV cambia di appena `0,0007 ante`; nello stesso intervallo l'EV BTN dopo
bet cambia di `-0,3931 ante`. Molte combo esportate hanno inoltre EV Bet/Check
separati da pochi millesimi o centesimi di ante, valori comparabili con il dEV
residuo GTO+. Questo è coerente con selezione numerica tra mix quasi
indifferenti o equilibri multipli: la strategia locale può muoversi molto senza
muovere materialmente il valore del gioco.

Conclusione aggiornata: il root EV è il gate di valore confrontabile ed è in
parità entro `0,0055 ante`. Gli EV BTN restano utili come diagnostica di output,
ma non possono provare da soli un gioco diverso finché i posteriori non sono
identici. Non è stato usato bucketing, sampling o card abstraction; il fenomeno
non richiede nessuna di queste ipotesi.

### Prossimo esperimento: root lock esterno combo-per-combo

La prossima fase controllata imporrà, soltanto nel test, le 36 probabilità
`Bet 20 / Check` del run GTO+ a dEV `0,078 ante` sul nodo CO root. Il root non
riceverà aggiornamenti di regret; tutte le strategie downstream resteranno
libere e verranno risolte da GTOSD. Il lock deve:

- coprire esattamente le 36 combo compatibili con `Ah Kh Qh`;
- usare le azioni root esistenti senza alterare l'albero;
- validare probabilità finite, non negative e con somma uno per combo;
- preservare blocker, card removal, enumerazione esatta e isomorfismo lossless;
- essere marcato `diagnostic_external_root_lock` e non modificare il solve
  normale, la fixture produttiva o il significato di equilibrio del gioco
  originale;
- riportare separatamente la convergenza del gioco vincolato e impedire che un
  checkpoint vincolato venga scambiato per una soluzione standard.

Il test è un discriminatore causale, non una correzione anticipata:

- se entrambi i delta BTN scendono entro `±0,5 ante`, la causa dominante è la
  diversa selezione della strategia root/posteriore;
- se almeno un delta resta oltre `±0,5 ante`, si procede al differenziale
  downstream di action tree, payoff e chance a posteriori già allineati;
- in nessun caso il profilo vincolato viene chiamato equilibrio del gioco
  originale o usato per sostituire il benchmark prestazionale standard.

## Verifica storica della divergenza BTN — 2026-08-01

Questa sezione conserva la diagnosi disponibile il 2026-08-01. La deduzione
che i delta BTN dimostrassero da soli un gioco diverso è superata dalla
ricomposizione del root e dall'analisi dei posteriori del 2026-08-02 sopra; i
conteggi dell'albero e lo sweep all-in restano evidenza valida.

La fixture `002` è ritirata. Applicava la soglia all-in a
`(stack - call) / (pot + call)` e quindi sostituiva il raise BTN con
`All-in 100`. L'ispezione della soluzione GTO+ v1.6.9 mostra invece, dopo
`CO bet 20`, tre azioni: `fold`, `call 20`, `raise 60`; l'all-in non è presente.

`003` applica la regola all-in naturale (all-in solo quando la bet size supera lo
stack rimanente; correzione 2026-08-05, vedi il journey). I golden diventano:
`165.774` nodi fisici, `46.065` nodi canonici, `385.980` infoset, `834.636`
action entry, fingerprint `fnv1a64:9e42ca23963f718b` e `6.677.088 byte` di
stato solver `float32`.

Cinque processi Release indipendenti hanno prodotto:

| Run | Iterazioni | dEV finale | Tempo `Run Solver` |
|---:|---:|---:|---:|
| 1 | 80 | 0,674155% | 2,973 s |
| 2 | 80 | 0,674155% | 3,048 s |
| 3 | 80 | 0,674155% | 3,128 s |
| 4 | 80 | 0,674155% | 3,268 s |
| 5 | 80 | 0,674155% | 3,271 s |

Mediana `3,128 s`, p95 `3,271 s`, speed score `54,34%` **FAIL** (gate 1,9 s).
Lo stato solver è `6.677.088 byte`, memory score `83,46%` **PASS**.

| Check EV flop | GTO+ | GTOSD a dEV 0,674% | Delta | Stato |
|---|---:|---:|---:|---|
| CO root (gate) | 19,15 | 19,112908 | -0,037092 | PASS |
| BTN dopo check CO | 21,65 | 22,083289 | +0,433289 | diagnostico |
| BTN dopo bet 20 CO | 17,51 | 17,360028 | -0,149972 | diagnostico |

Le frequenze non coincidono ancora pienamente: root bet `24,36%` contro
`19,7%`; il root lock diagnostico F10.4 (2026-08-05) porta i delta BTN a
`+0,0348` / `+0,0366` — vedi il journey.

L'utente ha confermato che la selezione GTO+ “With only 2 bets left, get the
money in smoothly” era disabilitata. Lo smoothing geometrico è quindi escluso
come causa. Finché l'intero action tree GTO+ non viene acquisito e confrontato
nodo per nodo, `003` è il contratto GTOSD corretto per la prima soglia osservata,
ma la parità completa del gioco resta **INCONCLUSIVE/FAIL**, non una differenza
del calcolo EV.

Evidenza locale: `out/gto-plus-convergence/gtp003-five-runs/summary.json` e
`out/diagnostics/trips-over-straight/out/high-accuracy-current-pot-150-run.json`.
La build completa `windows-gui-release` e la suite Release `22/22` sono PASS;
questo certifica la regressione locale, non la parità con l'albero esterno.

Le prove diagnostiche `raise_depth=2` e `raise_depth=3` producono gli stessi
conteggi, action entry, dEV, frequenze ed EV di `raise_depth=1`. Con pot 40,
stack 100 e size 50%, gli ulteriori raise raggiungibili sono già assorbiti dalla
policy all-in; il cap dei raise non spiega la divergenza.

### Sweep delle quattro interpretazioni della soglia all-in (storico)

Lo sweep del 2026-08-02 è conservato come registrazione storica: nessuna delle
quattro formule A-D è la regola GTO+. La correzione 2026-08-05 (dichiarazione
utente) stabilisce la regola naturale — all-in solo quando la bet size supera
lo stack rimanente — che sostituisce l'interpretazione A. Vedi il journey.

| ID | Rapporto sottoposto a soglia | Rapporto al nodo BTN | Azioni BTN | Nodi fisici | Action entry | dEV | EV CO / BTN-check / BTN-bet | Delta EV massimo |
|---|---|---:|---|---:|---:|---:|---|---:|
| A | `stack / current_pot` | 166,667% | fold, call 20, raise 60 | 112.848 | 526.872 | 0,695544% | 19,121570 / 22,201691 / 16,770667 | 0,739333 |
| B | `(stack-call) / (pot+call)` | 100,000% | fold, call 20, all-in 100 | 63.474 | 288.900 | 0,904789% | 19,128823 / 22,219692 / 16,196783 | 1,313217 |
| C | `stack / (pot+call)` | 125,000% | fold, call 20, all-in 100 | 63.474 | 288.900 | 0,904789% | 19,128823 / 22,219692 / 16,196783 | 1,313217 |
| D | `(stack-call) / current_pot` | 133,333% | fold, call 20, all-in 100 | 63.474 | 288.900 | 0,904789% | 19,128823 / 22,219692 / 16,196783 | 1,313217 |

Solo A supera il gate strutturale osservato in GTO+: B, C e D sostituiscono
erroneamente `raise 60` con `all-in 100` e, sugli stati materializzati, sono
identiche anche in layout, frequenze ed EV. A resta l'unica interpretazione
compatibile con il primo nodo noto, ma fallisce ancora la tolleranza EV BTN.
Quindi nessuna delle quattro formule semplici dimostra la parità dell'intero
albero; scegliere B, C o D perché un singolo EV appare più vicino sarebbe
scorretto, dato che il relativo action set contraddice direttamente GTO+.

Evidenza completa: `out/diagnostics/all-in-semantics-sweep-summary.json` e i
quattro report `out/diagnostics/all-in-semantics-*.json`.
