# Benchmark end-to-end di convergenza GTO+

Benchmark ID: `GTP-AHKHQH-003`
Schema: `gtosd.gto_plus_convergence_benchmark.v1`

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
soluzione completa consultabile. La versione osservata è GTO+ v1.6.9 64-bit.
L'action tree completo e l'unità esatta della memoria dichiarata non sono ancora
registrati; per questo la certificazione scientifica definitiva resta pending,
anche se i gate prestazionali sono misurabili.

La semantica è coerente con la spiegazione pubblica del rappresentante GTO+:
dEV è il massimo EV ottenibile sfruttando le imprecisioni dell'avversario
([fonte](https://forumserver.twoplustwo.com/167/poker-software/gto-cardrunnersev-155966/index493.html#post57429107)).
La soglia `1%` non è inferita dalla fonte: è il valore confermato dall'utente
per il run sorgente da `1,71 s`.

## Componenti

- `benchmarks/fixtures/gto_plus_ahkhqh_003.json`: contratto versionato, target e
  conteggi golden;
- `schemas/gto_plus_convergence_benchmark.schema.json`: schema del contratto;
- `gto_cli postflop benchmark-gto-plus`: un singolo processo indipendente e un
  report atomico `gtosd.gto_plus_convergence_run.v1`;
- `tools/run_gto_plus_convergence_benchmark.ps1`: almeno cinque processi,
  mediana, p95 nearest-rank, score e gate separati.

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

`003` applica la condizione stretta allo stack completo rispetto al pot prima
dell'azione. I golden diventano: `112.848` nodi fisici, `31.461` nodi canonici,
`250.704` infoset, `526.872` action entry, fingerprint
`fnv1a64:001a19fa48cd8b1e` e `4.214.976 byte` di stato solver `float32`.

Cinque processi Release indipendenti hanno prodotto:

| Run | Iterazioni | dEV finale | Tempo `Run Solver` |
|---:|---:|---:|---:|
| 1 | 80 | 0,695544% | 2,8812251 s |
| 2 | 80 | 0,695544% | 2,7197086 s |
| 3 | 80 | 0,695544% | 2,8186997 s |
| 4 | 80 | 0,695544% | 2,6386802 s |
| 5 | 80 | 0,695544% | 2,6308951 s |

Mediana `2,7197086 s`, p95 `2,8812251 s`, speed score `62,874383%` **FAIL**.
Lo stato solver è `4.214.976 byte`, memory score `189,799420%` **PASS**.

| Check EV flop | GTO+ | GTOSD a dEV 0,696% | Delta | Stato ±0,5 ante |
|---|---:|---:|---:|---|
| CO root | 19,15 | 19,121570 | -0,028430 | PASS |
| BTN dopo check CO | 21,65 | 22,201691 | +0,551691 | **FAIL** |
| BTN dopo bet 20 CO | 17,51 | 16,770667 | -0,739333 | **FAIL** |

Le frequenze non coincidono: GTOSD usa root bet `24,36%` contro `19,7%` e
BTN raise 60 `11,49%` contro `0,0%`. Un probe GTOSD a 1.000 iterazioni
(`dEV 0,011334%`) porta i delta BTN a `+0,768551` e `-1,513825 ante`; la
divergenza quindi non è un arresto prematuro e non è una normalizzazione EV.

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

### Sweep delle quattro interpretazioni della soglia all-in

Lo sweep mantiene invariati fixture, size, `raise_depth=1`, confronto stretto
`<150%`, `smoothly=false`, CFR+ exact, averaging delay 20 e target dEV 1%.
Al nodo osservato dopo `CO bet 20`, il pot corrente è 60, lo stack BTN è 100,
il call è 20, il pot dopo il call è 80 e lo stack aggiuntivo disponibile dopo
il call è 80.

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
