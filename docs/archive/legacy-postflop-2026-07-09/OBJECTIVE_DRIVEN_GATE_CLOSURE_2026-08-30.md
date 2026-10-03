# Objective-Driven Gate Closure — 2026-08-30

> **CORREZIONE SEMANTICA 2026-09-04 — REPORT STORICO.** I termini Peak RSS
> della funzione obiettivo, il cap desktop e i PASS/FAIL contro il display GTO+
> non sono comparabili e sono ritirati. Correttezza, dEV, root, timing e misure
> OS grezze restano evidenza. Vedere il
> [`piano di correzione`](../../GTO_PLUS_SOLVER_MEMORY_SEMANTICS_AND_GATE_CORRECTION_PLAN_2026-09-03.md).

## 1. Provenienza e stato iniziale

- branch: `main`;
- HEAD locale iniziale: `fa77ba436208e4987983eccd11f548b9747ffc6d`;
- `origin/main` iniziale: `fa77ba436208e4987983eccd11f548b9747ffc6d`;
- tracked/staged changes iniziali: nessuno;
- untracked preservati: `.reasonix/`, `.tmp/`.

Il contratto production è rimasto invariato: exact alternating DCFR, alpha 1,5,
beta 0, gamma 2, average immediato reach-weighted con peso `t^2`, signed regret,
`ScaledUint16RegretStrategy`, exact BR, CPU/RAM e massimo otto thread.

### Revalidation omogenea sull'HEAD pubblicato

Dopo il rilievo che la prima matrice finale mescolava run di diversa freschezza,
le tre fixture sono state rieseguite target-driven sullo stesso binario Release
costruito da `6508bddd039d44ecb941acded4b5b16d39f4f7e8`. Gli artifact locali sono:

- `out/objective-gate-closure-20260830/final-head-ahkhqh.json`;
- `out/objective-gate-closure-20260830/final-head-th7d6s.json`;
- `out/objective-gate-closure-20260830/final-head-tstc9d.json`.

Questi tre singoli processi sostituiscono i numeri finali eterogenei riportati
nella prima revisione del documento. Non sono la certificazione a cinque
processi, che resta vietata perché TST non supera il gate temporale.

## 2. Funzione obiettivo e loop

Il loop ha minimizzato lessicograficamente, senza somme compensabili:

```text
F = (
  correctness_fail_count,
  worst_correctness_violation,
  hard_gate_fail_count,
  worst_time_ratio,
  aggregate_time_ratio,
  worst_peak_rss_ratio,
  aggregate_peak_rss_ratio
)
```

Correctness precede ogni altro termine; i gate matematici precedono performance;
il worst fixture precede l'aggregato. Ogni candidato è passato attraverso prova
statica, differential breve, A/B ripetuto e cross-fixture solo se l'upside lo
giustificava. I candidati già chiusi nel report di revalidation non sono stati
riaperti.

## 3. Baseline gate matrix

Questa è la baseline autorevole consegnata con la task. Un rerun iniziale sullo
stesso binario ha prodotto AHK 0,703 s, TH 23,832 s e TST 288,707 s; il carico
era chiaramente non comparabile e quei tempi sono marcati `contaminated`, non
sostituiscono la baseline.

| Fixture | dEV | Root EV / delta | payoff-sum | Correctness | Solver | Peak RSS | State |
|---|---:|---:|---:|---|---:|---:|---:|
| AHKHQH | 0,655702% PASS | 19,108987 / -0,041013 PASS | -1,568246e-6 FAIL | FAIL | 0,714445 s PASS | 166,7 MB | 5.300.664 B PASS |
| TH7D6S | 0,806385% PASS | 8,221632 / -0,000348 PASS | entro tolleranza | PASS | 19,446404 s PASS | PASS | 334.452.416 B PASS |
| TSTC9D | 0,991865% PASS | 8,494698 / -0,006952 PASS | 2,862e-7 FAIL | FAIL | 206,257104 s FAIL | 1,969 GB PASS | 1.472.605.376 B PASS |

Il runner espone anche `memory_gate` contro la memoria GTO+ (8 MB AHK e un
riferimento analogo TH): sul final HEAD questo check è rosso per AHK e TH,
verde per TST. Nel presente report `peak RSS`, cap desktop production da 2 GB e
`solver_state_bytes` restano tre condizioni separate. Di conseguenza non viene
più usata la dicitura non qualificata `RAM PASS`: il cap desktop e lo stato
solver passano su tutte le fixture, mentre il confronto peak-RSS-vs-GTO+ no.

## 4. Loop ledger

| Loop | Termine dominante | Candidato | Predizione | Misura | Decisione | Nuovo F |
|---:|---|---|---|---|---|---|
| 0 | 2 correctness FAIL | ladder zero-sum seriale | localizzare il primo rung >1e-11 | terminali/river <=2,7e-15; salto turn chance 1,56e-7 | ACCEPT tooling temporaneo, poi rimosso | invariato, informazione acquisita |
| 1 | worst correctness 156.825x | scomposizione chance | distinguere identity/transform/multiplicity | identity <=4,4e-16; transformed 1,56e-7; multiplicity zero | root cause confermata | invariato |
| 2 | reuse con reach non invariante | rivalutare outcome con reach opponent distinto | payoff <=1e-11, BR/profile exact | TST@5 6,143e-9 -> -8,88e-16; TST@20 -> -3,39e-15; target -> 3,22e-15 | PROMOTE `40104f0` | correctness `(2,156825x) -> (0,0)` |
| 3 | certification aumentata | BR actor fast path | -5..15% certification, bit-identico | tre A/B: certification mediana 9,6249 -> 9,0981 s; solver 27,9039 -> 26,1106 s | PROMOTE `c9acfe3` | correctness invariata; time migliorato ma FAIL TST |
| 4 | traversal TST dominante | signed state pass fusion | eliminare almeno una passata >=5% traversal | misura iniziale attribuiva erroneamente il costo river a `value+update`; candidato riaperto per una misura ai boundary reali | INCONCLUSIVE -> loop 13 | invariato |
| 5 | certification duplicata | pair profile+BR dello stesso player | -20..35% certification, bit-identico | tre A/B: cert mediana 9,96 -> 7,75 s; exact differential e zero-rake mirror PASS | PROMOTE `4a7e851` | performance migliora, TST time ancora FAIL |
| 6 | terminal/showdown dominante | frontier batching 4-lane | iniziale 2,2..2,9x kernel sintetico | benchmark non faithful: baseline scalarizza prefix/output già AVX2 production; coverage root-local 53,695% | CLOSE autonomo; tooling conservato `0b71489` | informazione acquisita, F invariato |
| 7 | distinct-outcome tail seriale | flat worklist exact + nested pool | -15..30% certification residua | tre A/B con binari distinti: cert mediana 7,148 -> 5,904 s; payoff/BR/profile bit-identici | PROMOTE `0d51fd6` | performance migliora, TST time ancora FAIL |
| 8 | traversal target > limite anche con certification gratuita | whole-river lanes / cross-root broker / continuation | richiede >=30% traversal | hero-SIMD già 8-wide, state disgiunto richiede gather, RAM margin ~27 MB; upper bound credibile 0..13% | REJECT/CLOSE fino a nuovo cost model faithful | blocker architetturale distinto |
| 9 | validare il cost model terminal-lane | benchmark production-faithful | >=2,0x medium/large, bit-identico | accumulation-only 0,895..0,933x; 2-card x 4-root mediana 1,846x medium e 1,584x large | REJECT production; tooling PROMOTE `91caf7e` | informazione acquisita, F invariato |
| 10 | verificare whole-river lane occupancy | topology/kernel-shape telemetry width 4/2 | coverage sufficiente a >=30% traversal | 167.992 root: zero quartetti; width-2 copre 34,1305% del lavoro, massimo ideale 17,065% | REJECT entrambe; telemetry PROMOTE `18751d8` | blocker corrente dimostrato |
| 11 | nuovo correctness/stability FAIL | paired certification stack frames | rimuovere overflow senza cambiare valori o stack process | phase7 pre-fix `0xC00000FD`; post-fix 3/3 PASS; oracle legacy/paired PASS | PROMOTE `19fe8ea` | correctness/stability ripristinata; F torna allo stato precedente |
| 12 | terminale dominante | same-reach aggregation sharing | kernel exact >=2x e reuse sufficiente nel regime target | large Level 1 circa 2,0x; ultimi 40 player-pass target: 160.392 repeat / 23.170.982 showdown = 0,6922% | REJECT production; tooling PROMOTE `782acf0` | F invariato; upper bound <1% terminale |
| 13 | signed-state cost ignoto | full state-pass fusion | eliminare una passata completa exact con upside >=5% traversal | @20: signed 19.015,0 / 43.823,4 ms = 43,39%; encode 5.605,3 ms = 12,79%; la scala globale impone la seconda fase | CLOSE; telemetry PROMOTE `5ae7629` | F invariato; rappresentazione exact invariata |
| 14 | terminal+reach dataflow | `TerminalReachView` q=1/q=4 | q=1 >=2,2x per sostenere >=30% traversal | q=1 wall `1,010x` P0 e `0,978x` P1; q=4 circa `1,71x`; oracle bitwise PASS | REJECT production; benchmark PROMOTE `1e1d264` | F invariato |
| 15 | signed family 43,39% | tile dominance / hierarchical encode / recompute | updater-only >=3,2405x | dominance/recompute upper bound <30%; hierarchical recupera al massimo il 15,08% wall dall'occupancy | CLOSE a Level 0 | F invariato |
| 16 | pipeline terminal-to-state | opponent reach pack + direct value sink | coverage e speedup composti >=30% traversal | leaf coverage 38,40%; signed 40,66%; opponent 36,17%; upper bound generoso circa 26,6% | REJECT; telemetry PROMOTE `4a0ce8c` | F invariato |
| 17 | traversal TST ratio 1,958 | recursive sink / flat arena / hero-tiled treelet | identificare una famiglia exact con ceiling >=30% | sink opponent ceiling bandwidth 20,25%; arena flat circa 28,7 MB prima dei metadata; solo hero tiling supera appena Level 0 con ceiling 34,76% | sink e arena CLOSE; hero tiling a Level 1 | F invariato, informazione acquisita |
| 18 | verificare il ceiling hero-tiled | treelet depth-3 tile 32/64 | exact, RAM <=16 MB/8 worker e >=1,50x P0/P1 | oracle bitwise PASS; RAM max 1.623.488 B; speedup P0 0,958/0,971x, P1 0,861/0,867x | REJECT al Level 1; tooling conservato | F invariato |
| 19 | freschezza non omogenea della matrice | final-head target-driven AHK/TH/TST | sostituire inferenze e run fixed con tre misure comparabili | AHK 0,670928 s PASS; TH 17,645055 s PASS; TST 208,111772 s FAIL; correctness/state PASS; `memory_gate` AHK/TH FAIL | ACCEPT revalidation; nessuna modifica production | `F_runner=(0,0,2,1,613409,0,955256,20,813824,7,933239)`; `F_desktop=(0,0,0,1,613409,0,955256,0,984961,0,489134)` |

Candidate pool dell'ultima iterazione, ordinato per expected information gain:

| Ipotesi | Gain obiettivo atteso | Confidenza | Costo impl./valid. | Rischio numerico/RAM | Generalità | Evidenza precedente | Esito |
|---|---:|---:|---|---|---|---|---|
| share aggregation fra showdown con reach identico | 0-8% traversal prima della coverage | media | medio/medio | basso/basso | alta | repeat elevati nei primi pass | REJECT dopo coverage target 0,6922% |
| full signed pass fusion / scratch-free recompute | 5-15% traversal | media | alto/alto | alto/basso | alta | attribuzione timer incompleta | CLOSE: scala globale e encode 12,79% |
| joint reach/rank-card + signed tile layout | 20-40% traversal teorico | bassa | molto alto/molto alto | alto/alto | alta | terminale 57,19% e signed 43,39% | DEFER: manca prova end-to-end >=30% |

La priorità sperimentale ha scelto prima i due candidati con costo Level 1
contenuto e informazione capace di chiudere un'intera famiglia. Il terzo non è
stato mutato in production: senza una prova di layout, ordine IEEE e RAM
rischierebbe di combinare due cambiamenti non differenziabili.

La successiva iterazione ha risolto il `DEFER`: il Level 1 della reach view e
la coverage river dimostrano che anche la pipeline congiunta resta sotto la
soglia economica. Non rimane una mutazione production autorizzata nell'attuale
rappresentazione.

L'iterazione architetturale successiva ha generato tre ipotesi indipendenti:

| Ipotesi | Gain atteso | Confidenza | Costo impl./valid. | Rischio numerico/RAM | Generalità | Prior evidence | Esito |
|---|---:|---:|---|---|---|---|---|
| recursive hierarchical opponent sink | 0-20,25% traversal | media | medio/alto | medio/basso | alta | 50,42% delle entry river è opponent, ma il boundary risparmia solo `8N` byte/nodo | CLOSE: ceiling sotto il 30% |
| flat bottom-up river arena | 10-30% traversal | bassa | molto alto/molto alto | alto/alto | alta | reach top-down e value bottom-up; circa 28,7 MB/8 board prima dei metadata | CLOSE: margine RAM TST circa 27 MB e nessuna nuova località |
| bounded hero-tiled treelet | 30-34,76% ceiling teorico | bassa | medio/medio | alto/basso | alta | round-trip action values fino a 101,86 GB/@20, ma array cache-hot e 10-12 tile walk | REJECT: Level 1 0,861-0,971x |

La selezione ha privilegiato l'hero tiling perché era l'unico candidato con
ceiling teorico sopra la soglia. Il benchmark ha mantenuto parentesi IEEE,
massimi globali prima dell'encode, scratch signed completi e reach distinti;
il risultato negativo chiude la famiglia senza una mutazione production.

## 5. AHK correctness audit

| Check | Observed finale fix | Tolerance/reference | Gate? | Esito |
|---|---:|---:|---|---|
| dEV | 0,655665% | <1% | sì | PASS |
| Root EV | 19,108984; delta -0,041016 | 19,15 +/-0,05 | sì | PASS |
| layout/fingerprint | exact | fixture | sì | PASS |
| normalization | 0 | 1e-11 | sì | PASS |
| NashConv | 0,0119318, finito | finitezza | sì | PASS |
| convergence | true @80 | target dEV | sì | PASS |
| payoff-sum | 0 | 1e-11 | sì | PASS |
| child EV after check | 21,944595; delta +0,294595 | +/-0,05 | no | diagnostic FAIL |
| child EV after bet | 16,004087; delta -1,505913 | +/-0,05 | no | diagnostic FAIL |
| root bet/check frequency | 0,254208 / 0,745792 | 0,197 / 0,803 +/-0,01 | no | diagnostic FAIL |
| solver state | 5.300.664 B | 8.000.000 B | sì | PASS |
| solver time | 0,670928 s | limite production 1,900 s | sì | PASS |

`ev_correctness_passed` e `action_frequency_correctness_passed` sono
diagnostici. `correctness_gate` nel JSON indica soltanto il Root EV, mentre
`correctness_passed` è il composito formale. La nomenclatura resta ambigua, ma
non ha causato l'aggregazione errata dei diagnostici.

## 6. TST zero-sum differential

La ladder runtime-gated è stata costruita nel build profile, usata e rimossa
dal codice finale. Evidenza @20:

| Rung | count | max abs residual | accumulated |
|---|---:|---:|---:|
| terminal fold flop/turn/river | 479.580 | <=3,33e-15 | circa 1e-15 |
| terminal showdown river | 642.088 | 1,11e-15 | -9,73e-14 |
| river decision | 624.160 | 3,05e-16 | 4,48e-15 |
| turn chance | 6.325 | 1,563014e-7 | -2,246537e-7 |
| turn decision | 6.400 | 2,285603e-7 | 3,572008e-7 |
| flop chance | 35 | 2,260975e-7 | 1,575740e-7 |
| root | 1 | 1,575740e-7 | 1,575740e-7 |

Scomposizione del chance:

- identity outcome turn: max `2,78e-16`;
- transformed outcome turn: max `1,563014e-7`;
- physical multiplicity: nessun contributo in questa fixture;
- differenza massima fra reach del representative e reach trasformato: `1,12e-4`.

La certification valutava una sola volta `edge.outcomes.front()` e riusava il
value vector per outcome trasformati. Quel vector dipende dal reach
dell'avversario; dopo aggiornamenti quantizzati il reach non è necessariamente
invariante nell'orbita. Il fix mantiene il reuse solo quando il reach opponent è
byte-identico e rivaluta altrimenti l'outcome con il suo reach reale. Non assegna
P1 da P0, non normalizza il root e non cambia tolleranze.

## 7. Certification profiling

Prima del fix, con `rake.enabled=true` e percentuale zero, la certification
eseguiva quattro traversal indipendenti: profile P0/P1 e BR P0/P1. `prepare_ranks`
era già condiviso. Sono duplicati policy decode, reach reconstruction, chance
transform, scratch e worker-pool setup.

Il fix correctness aumenta correttamente il lavoro quando un outcome ha reach
opponent distinto: il target **pre-ottimizzazioni certification** ha misurato
traversal `183,039575 s`, certification `69,069453 s`, solver `252,534707 s`.
Questi numeri descrivono il cost model storico, non il final HEAD. Il fast path BR promosso
elimina policy decode, strategy scratch e copie actor-reach ai nodi unlocked del
best responder. Nel TST@20 A/B:

| Variante | solver mediana | traversal mediana | certification mediana |
|---|---:|---:|---:|
| correctness baseline | 27,903908 s | 17,840062 s | 9,624898 s |
| BR fast path | 26,110616 s | 16,601912 s | 9,098058 s |
| delta | -6,43% | -6,94% (rumore incluso) | -5,47% |

Tutti i profile EV, BR, dEV e payoff-sum dei tre candidate run sono identici al
baseline. Il traversal non è modificato dal candidato; la sua variazione non è
attribuita causalmente al patch.

Il ciclo successivo ha accoppiato, per ciascun player, profile evaluation ed
exact BR. Le due lane condividono opponent reach, terminal kernel, chance
transform e policy decode; divergono soltanto nella riduzione ai decision node
del player (`sum sigma*child` contro `max child`). Root lock e zero-rake mirror
mantengono esattamente la semantica legacy. Un oracle nel reference test
confronta i bit di profile P0/P1, BR P0/P1, payoff-sum e NashConv.

TST@20, tre processi per variante:

| Variante | solver mediana | traversal mediana | certification mediana |
|---|---:|---:|---:|
| legacy quattro traversal | 28,82 s | 18,43 s | 9,96 s |
| same-player pair | 25,45 s | 17,53 s | 7,75 s |
| delta certification |  |  | **-22,2%** |

La residual tail era ancora seriale: dopo il join dei representative, gli
outcome con opponent reach trasformato distinto venivano rivalutati dal solo
caller. La worklist finale materializza representative e distinct reach come
work item read-only, li distribuisce sul pool anche ai chance annidati e salva i
risultati per indice. L'accumulo resta successivo al join e nello stesso ordine
canonico, quindi non cambia l'ordine IEEE.

Attribuzione TST@20:

- flat worklist soltanto: `7,744 -> 6,644 s` certification (`-14,21%`);
- flat + nested: `6,644 -> 5,329 s` nel run di isolamento (`-19,79%`);
- combined contro paired baseline: `-31,18%` nel run di selezione.

L'A/B definitivo ha usato un binario detached esatto a `4a7e851` e il binario
candidate, ordine B/C/B/C/B/C:

| Pair | baseline cert | candidate cert | baseline solver | candidate solver |
|---:|---:|---:|---:|---:|
| 1 | 7,343335 s | 5,994388 s | 25,372564 s | 23,578930 s |
| 2 | 6,770451 s | 5,904024 s | 24,200711 s | 21,905603 s |
| 3 | 7,148386 s | 5,610861 s | 24,930610 s | 22,856513 s |
| mediana | **7,148386 s** | **5,904024 s** | **24,930610 s** | **22,856513 s** |

Tutti i valori matematici sono identici. Il peak massimo passa da
`1.971.040.256 B` a `1.972.858.880 B`, entro il cap.

## 8. Signed state byte/pass profiling

L'ispezione del path production mostra che l'updater signed è già fuso. Per un
nodo con `E = actions * live_hands`:

1. regret matching/policy decode quando serve al child reach;
2. fused update/max/average: legge code regret `2E`, code strategy `2E`, scale
   `8 B/node`, action values/policy/reach; scrive scratch regret `4E` e average
   `4E`;
3. encode/writeback: legge scratch `8E`, scrive code `4E` e scale `8 B/node`.

Il cost model completo del path generico float è circa `40 B/action update`:
`28 B` letti e `12 B` scritti, più `16 B/node` per read/write delle due scale.
I kernel specializzati valgono circa `36 B/entry` a due azioni e `34,7 B/entry`
a tre. Max scan e average non sono passate complete separate: la prima passata
produce entrambi gli scratch e i massimi; la seconda deve attendere le scale
globali definitive prima di quantizzare e scrivere i due code.

La prima attribuzione (`396,8 ms / 26.103,8 ms = 1,52%`) era errata: il timer
river includeva l'updater in `value+update`. La telemetria compile-time ora
misura direttamente i boundary della funzione signed. Aggregato su tutti i 40
player-pass TST fixed@20:

| Boundary | Tempo CPU-equivalent | Quota lavoro nominato |
|---|---:|---:|
| calculate/update/max/average | 12.673,2 ms | 28,92% |
| scale compute/telemetry | 736,5 ms | 1,68% |
| encode/writeback | 5.605,3 ms | 12,79% |
| signed totale | 19.015,0 ms | 43,39% |

Il denominatore omogeneo è `43.823,4 ms` di subpart CPU-equivalent; il wall
dei 40 pass è `27.644,1 ms`. Sono state elaborate `6.393.749.957` entry in
`10.955.244` chiamate. Il traffico algoritmico contato ai confini signed è
circa `24,02 B/entry` (`8E + 16 B/node` state e `16E` scratch), oltre agli
input action/current/reach.

La correzione del cost model riapre ma non promuove la fusion. La scala finale
è definita dal massimo di tutte le entry del nodo; quantizzare durante la prima
passata cambierebbe scale, rounding o richiederebbe una riscrittura dei code.
La sola seconda passata vale al massimo il `12,79%` del lavoro nominato, già
insufficiente per il gap traversal >=30%, e non è eliminabile byte-exact con la
rappresentazione corrente. Ricalcolare gli update nella seconda fase evita lo
scratch ma duplica decode, gather e aritmetica. `full state-pass fusion` è
quindi CLOSE per questa rappresentazione, non perché l'updater sia irrilevante.

La distribuzione weighted delle `6.393.749.957` entry aggiorna il ranking dei
kernel specializzati:

| Arity | Entry | Quota |
|---:|---:|---:|
| 2 | 3.088.109.820 | 48,30% |
| 3 | 1.893.680.646 | 29,62% |
| 4 | 1.026.237.676 | 16,05% |
| 5 | 385.721.815 | 6,03% |

Un'ottimizzazione limitata ai nodi 2-action non ha quindi coverage sufficiente
per chiudere il gate neppure rendendo gratuita quella sola arity. Tile
dominance e scratch-free recompute conservano exactness ma hanno upper bound
rispettivamente inferiore al 28,90% assoluto e circa 19,28% realistico. Una
pipeline encode gerarchica non elimina lavoro: con CPU target già circa 84,92%
può recuperare al massimo il 15,08% wall dall'occupancy, contro `3,2405x`
richiesto sull'intera famiglia signed.

## 9. Ranking architetturale

| Candidate | Gain atteso solver | Confidenza | Costo/rischio | Esito |
|---|---:|---:|---|---|
| BR actor fast path | 1-3% atteso, 6,43% short osservato | alta | basso/basso | PROMOTE `c9acfe3` |
| same-player profile+BR pair | 20-35% certification | medio-alta | medio/medio-basso | PROMOTE `4a7e851` |
| flat distinct worklist + nested pool | 15-30% certification residua | alta dopo A/B | medio/medio-basso | PROMOTE `0d51fd6` |
| frontier batching sola accumulation | <=6-7% traversal root-local | alta sull'upper bound | alto/medio | CLOSE |
| cross-root terminal broker | kernel large 1,584x anche a occupancy ideale | alta dopo benchmark faithful | alto/alto | REJECT |
| four-lane whole river | zero gruppi compatibili da quattro | alta dopo telemetry | molto alto/alto RAM | REJECT |
| two-lane whole river | <=17,065% river work a 2x ideale | alta dopo telemetry | molto alto/alto RAM | REJECT |
| four-lane joint certification | 8-18% certification residua | media-bassa | alto/alto | CLOSE per economics |
| same-reach showdown aggregation | circa 2x kernel, ma <1% terminale target | alta | medio/basso | REJECT; tooling `782acf0` |
| full state-pass fusion | signed 43,39%; seconda fase 12,79%; nessuna eliminazione exact | alta | alto/alto | CLOSE; telemetry `5ae7629` |
| `TerminalReachView` | q=1 circa 1,0x; q=4 circa 1,71x | alta | medio/basso | REJECT; benchmark `1e1d264` |
| hierarchical signed pipeline | <=15,08% wall da occupancy | alta | alto/medio | CLOSE Level 0 |
| terminal-to-state direct sink | upper bound generoso circa 26,6% traversal | alta | molto alto/alto | REJECT; telemetry `4a0ce8c` |
| paired certification out-parameters | correctness/stability, nessun gain rivendicato | alta | basso/basso | PROMOTE `19fe8ea` |

Il benchmark four-lane conserva valore come proof-of-concept bit-identica con
quattro reach e payoff distinti, ma non come prova prestazionale production:
la baseline sintetica usa prefix/output scalari, mentre production usa AVX2 su
otto card/hero. La telemetria su 40 player-pass misura `22.132.502` showdown in
`6.231.136` river root; solo `53,695%` cade in quartetti root-local. Non è stato
promosso alcun path sulla proiezione non faithful.

Il Level 1 successivo replica invece il kernel production: accumulation nello
stesso ordine, prefix AVX2 su otto card, output AVX2 su otto hero, metadata SoA,
slot invalidi e payoff `double` lane-specific. L'oracle usa `memcmp` completo
small/medium/large. Tre processi baseline/candidate alternati danno:

| Workload | production sequential mediana | 2-card x 4-root mediana | speedup |
|---|---:|---:|---:|
| medium | 7.542 ns | 4.085 ns | 1,846x |
| large | 18.363 ns | 11.592 ns | 1,584x |

La sola accumulation batched e quattro finish production è regressiva
(`0,895..0,933x`). Il large non raggiunge neppure la soglia `1,74x` richiesta
con occupancy teorica 100%, quindi il broker terminale è economicamente morto.

Il benchmark same-reach separa invece l'aggregation reach-dependent dal
producer payoff-dependent. L'oracle confronta bit per bit quattro output con
payoff distinti. Nel run finale la mediana large passa da `15.569 ns` CPU a
`7.785 ns` (circa `2,00x`; run precedente `2,15x`). Questo dimostra il kernel,
non la coverage. La telemetria di reuse distance mostra che il vantaggio è
concentrato nelle prime iterazioni; nel regime target degli ultimi 40
player-pass soltanto `160.392 / 23.170.982 = 0,6922%` delle chiamate ripete un
reach. Anche rendendo gratuita ogni aggregation ripetuta, l'upper bound resta
inferiore all'1% del terminale: integrazione production REJECT.

Il Level 1 successivo include l'intera pipeline `parent reach * strategy ->
child reach -> rank/card -> prefix -> hero output`. Usa due orientamenti TST
asimmetrici, conversione production `double -> float`, quattro payoff e oracle
bitwise su child, summary e output. Nel rerun indipendente a sette repetition:

| Workload | q | Reference wall | Reach view wall | Speedup |
|---|---:|---:|---:|---:|
| TST P0 | 1 | 1.782 ns | 1.765 ns | 1,010x |
| TST P1 | 1 | 1.775 ns | 1.814 ns | 0,978x |
| TST P0 | 4 | 6.870 ns | 4.101 ns | 1,675x |
| TST P1 | 4 | 6.851 ns | 3.850 ns | 1,779x |

Il q=1 rappresenta il regime senza reuse e fallisce nettamente la soglia
`2,2x`; q=4 è diagnostico e non autorizza integrazione. La summary richiede
`10.804 B/view`.

La coverage compile-time dei river leaf su 40 player-pass TST@20 è:

| Classe | Entry totali | Tutti i child terminali | Coverage |
|---|---:|---:|---:|
| tutte le decisioni river | 12.732.704.167 | 4.888.861.536 | 38,40% |
| actor = updating player | 6.313.008.060 | 2.566.647.874 | 40,66% |
| actor = opponent | 6.419.696.107 | 2.322.213.662 | 36,17% |

Applicando generosamente la coverage massima `40,66%` sia al terminale
(`57,19%`) sia al decision/state (`36,42%`), concedendo il decision work
eleggibile e il reach completamente gratuiti e limitando il terminale al
`1,71x` misurato, l'upper bound è circa `26,6%` traversal. È inferiore al 30%
prima di overhead, RAM e validation cost: la pipeline combinata è REJECT.

La telemetry compile-time del river subtree usa descriptor strutturali
internati con full equality; reach, payoff, amount, offset e metadata fisici
restano input lane-local e non autorizzano riuso. Nel primo player-pass TST:

- `167.992` root in `135.861` gruppi;
- `104.236` singleton;
- `31.119` coppie e `506` terne, quindi **zero quartetti**;
- width-2: `31.625` pair, `63.250` root accoppiabili;
- coverage work-weighted width-2 `34,1305%`.

Senza cross-lane result reuse, vietato dal contratto, un kernel width-2 ha
speedup ideale massimo `2x`: l'upper bound diventa quindi `17,065%` del lavoro
river e ancora meno del traversal completo. Anche questa famiglia è CLOSE.

Anche rendendo gratuita la certification, il traversal target post-fix
misurato (`183,04 s`) supera il limite solver (`128,989 s`). Nessuna candidate
certification può chiudere il gate; serve una riduzione traversal architetturale
di almeno circa 30%, non dimostrata dalle famiglie rimaste.

## 10. Candidate provati, differential e rollback

- zero-sum ladder: tooling temporaneo, rimosso dopo la diagnosi;
- distinct transformed reach evaluation: PROMOTE, commit `40104f0`;
- BR actor fast path: PROMOTE, commit `c9acfe3`;
- decision-node rounding come root cause: REJECT, il primo salto è chance;
- terminal payoff correction: REJECT, terminali già entro circa 1e-15;
- post-hoc zero-sum/mirror/tolerance change: vietati e non implementati;
- same-reach aggregation: REJECT production; il kernel exact è circa `2x`
  large ma il reuse target è soltanto `0,6922%`; benchmark PROMOTE `782acf0`;
- full state-pass fusion: CLOSE dopo misura corretta; signed totale `43,39%`,
  encode/writeback `12,79%`, ma il massimo globale del nodo rende la seconda
  fase necessaria per conservare scale, rounding e byte finali; telemetry
  PROMOTE `5ae7629`;
- `TerminalReachView`: REJECT al Level 1, q=1 circa `1,0x` contro gate
  `2,2x`; benchmark exact PROMOTE `1e1d264`;
- tile dominance, hierarchical encode e scratch-free recompute: CLOSE; le
  prime e terze non hanno headroom >=30%, la seconda non può recuperare più
  del 15,08% wall dall'occupancy corrente;
- terminal-to-state direct sink: REJECT dopo coverage; anche con assunzioni
  volutamente favorevoli l'upper bound composto è circa `26,6%`; telemetry
  PROMOTE `4a0ce8c`;
- recursive hierarchical opponent sink: CLOSE staticamente; estende la
  coverage strutturale ai non-leaf ma il ceiling bandwidth è circa `20,25%`
  e non elimina terminal, signed update o le barriere IEEE dei nodi annidati;
- flat bottom-up arena: CLOSE staticamente; la DFS corrente è già board-local
  e il payload minimo circa `28,7 MB` prima dei metadata supera il margine RAM
  TST disponibile;
- bounded hero-tiled treelet: REJECT al Level 1; oracle bitwise e RAM PASS, ma
  tile 32/64 misurano `0,958/0,971x` P0 e `0,861/0,867x` P1 contro gate
  `1,50x`; benchmark tooling conservato;
- same-player profile+BR pair: PROMOTE, commit `4a7e851`;
- four-lane terminal proof-of-concept e frontier telemetry: tooling PROMOTE
  `0b71489`, integrazione production CLOSE;
- flat distinct-outcome worklist + nested read-only pool: PROMOTE `0d51fd6`;
- whole-river four-lane: REJECT come minima integrazione per perdita hero-SIMD,
  gather state e rischio RAM;
- cross-root broker: REJECT dopo benchmark production-faithful, kernel large
  sotto soglia anche assumendo occupancy 100%;
- benchmark production-faithful: tooling PROMOTE `91caf7e`, accumulation-only
  e terminal 2-card x 4-root REJECT;
- whole-river width-4: REJECT, nessun quartetto structural/kernel-compatible;
- whole-river width-2: REJECT, upper bound ideale `17,065%` del river work;
- river subtree compatibility telemetry: PROMOTE `18751d8`, interamente
  compilata fuori dal build Release normale.
- paired certification stack fix: PROMOTE `19fe8ea`; la ricorsione interna ora
  scrive in due buffer forniti dal chiamante invece di restituire due
  `ComboVector` per valore a ogni frame. Nessun aumento dello stack process,
  fallback legacy o rilassamento dei gate.

La regressione generica nel reference test usa un DAG realmente compresso,
DCFR signed e `ScaledUint16RegretStrategy`; richiede payoff-sum `<1e-11` senza
dipendere dall'ID TST.

## 11. Evoluzione objective vector

Usando i limiti production AHK `1,900 s`, TH `19,622222 s`, TST
`128,988889 s` e il cap RAM 2 GB:

```text
iniziale autorevole:
  F ~= (2, 156824.6, 1, 1.599, 0.989, 0.985, 0.489)

dopo fix correctness, target misurato:
  F = (0, 0, 0, 1.958, >=1.14, 0.984, circa 0.49)
  Il time FAIL non incrementa `hard_gate_fail_count`: è rappresentato dai
  due termini temporali successivi.

dopo BR fast path:
  correctness invariata; TST@20 solver -6.43%; nessun nuovo target-driven
  perché l'upside massimo del candidato non può chiudere il ratio 1.958.

dopo certification pair + worklist/nested:
  correctness invariata; certification TST@20 9.96 -> 7.75 -> 5.90 s nelle
  rispettive mediane; il target solver resta proiettato ampiamente >1.0 perché
  il solo traversal target noto vale circa 1.419x il limite.

dopo Level 1 faithful + topology telemetry:
  F invariato; terminal four-lane max 1.584x large, whole-river width-4 senza
  quartetti e width-2 limitato idealmente a 17.065% del river work. Nessuna
  candidate attraversa la soglia economica per Level 2.

durante full CTest a 18751d8:
  nuovo correctness/stability FAIL: phase7 termina con 0xC00000FD. Il test
  legacy PASS e il paired flat-only FAIL localizzano il problema nei grandi
  return value ricorsivi, non nel pool annidato.

dopo out-parameter recursion a 19fe8ea:
  phase7 3/3 PASS e differential legacy/paired PASS; F torna allo stato
  precedente senza variazioni matematiche osservate.

dopo same-reach e signed-pass telemetry:
  F invariato; il reuse target limita il primo candidato a <1% del terminale.
  Il secondo corregge il cost model (signed 43,39%, non 1,52%), ma nessuna
  passata >=30% è eliminabile exact con la rappresentazione corrente.

dopo TerminalReachView, arity e sink coverage:
  F invariato; q=1 è circa 1,0x, soltanto il 40,66% massimo delle value-entry
  river è leaf-eleggibile e l'upper bound combinato resta circa 26,6%.
  Nessun candidato corrente supera il gate economico per Level 2.

dopo audit sink/arena e hero-tiled Level 1:
  F invariato; sink opponent e arena flat sono chiusi staticamente, mentre
  il solo ceiling >30% è falsificato da speedup 0,861-0,971x. Nessuna mutazione
  production corrente è autorizzata.

revalidation omogenea final-head:
  F_runner = (0, 0, 2, 1.613409, 0.955256, 20.813824, 7.933239)
  usando il `memory_gate` canonico peak-RSS-vs-GTO+ delle fixture.
  F_desktop = (0, 0, 0, 1.613409, 0.955256, 0.984961, 0.489134)
  usando invece il cap operativo uniforme 2 GB richiesto dalla missione.
  Il primo è il vettore conservativo della suite versionata; il secondo misura
  la sola idoneità desktop. Nessuno dei due consente compensazioni e non devono
  essere fusi sotto una generica etichetta `RAM PASS`.
```

Il peggioramento temporale del fix è accettato soltanto perché correctness è il
termine lessicograficamente superiore. Non è presentato come chiusura performance.

## 12. Gate matrix finale misurata

| Gate | AHKHQH | TH7D6S | TSTC9D |
|---|---|---|---|
| dEV | 0,655665% PASS | 0,806385% PASS | 0,991863% PASS |
| Root EV | 19,108984 PASS | 8,221632 PASS | 8,494698 PASS |
| payoff/correctness | 0 PASS | -4,44e-16 PASS | 3,22e-15 PASS |
| layout/convergence | PASS | PASS | PASS |
| solver state | 5.300.664 B PASS | 334.452.416 B PASS | 1.472.605.376 B PASS |
| peak RSS / cap desktop 2 GB | 166.510.592 B PASS | 798.371.840 B PASS | 1.969.922.048 B PASS |
| solver time | 0,670928 s target PASS | 17,645055 s target PASS | 208,111772 s target FAIL |

La tabella è ora interamente derivata dai tre run final-head. Per TST @202:
traversal `174,926380 s`, certification `32,780800 s`, payoff-sum
`3,22e-15`, peak RSS `1.969.922.048 B` e state `1.472.605.376 B`. Il limite
temporale è `128,988889 s`: il solver eccede di `79,122883 s`, rapporto
`1,613409x`, cioè `+61,3409%`. La precedente misura `252,534707 s` era
pre-ottimizzazione della certification e non è più il numero finale corrente.

Semantica memoria final-head:

| Gate memoria | AHKHQH | TH7D6S | TSTC9D |
|---|---:|---:|---:|
| `solver_state_bytes` / limite fixture | 5.300.664 / 8.000.000 PASS | 334.452.416 / 399.000.000 PASS | 1.472.605.376 / 2.000.000.000 PASS |
| peak RSS / cap desktop 2 GB | 166.510.592 PASS | 798.371.840 PASS | 1.969.922.048 PASS |
| JSON `memory_gate` peak RSS / riferimento GTO+ | 166.510.592 / 8.000.000 FAIL | 798.371.840 / 399.000.000 FAIL | 1.969.922.048 / 2.000.000.000 PASS |

## 13. Validazione

- build Release `gto_cli` e `gtosd_gto_plus_reference_tests`: PASS;
- full CTest Release a `0d51fd6`: 20/20 PASS in 210,70 s;
- reference test completo: PASS, 24 assertion più regression zero-sum;
- TST fixed @5 e @20: correctness differential PASS;
- tre TST@20 baseline/candidate alternati: exact-value differential PASS;
- same-player pair: tre processi per variante, bit differential PASS;
- worklist+nested: tre B/C alternati con due binari distinti, tutte le coppie
  più veloci e exact-value differential PASS;
- showdown four-lane proof-of-concept: small/medium/large bit identity con
  reach e payoff lane-specific; timing non usato come production evidence;
- frontier occupancy TST@20: 40 player-pass, coverage quartet root-local
  `53,695%`;
- production-faithful showdown oracle: `memcmp` PASS small/medium/large;
- same-reach showdown oracle: bit identity PASS small/medium/large; tre
  repetition finali, large CPU mediana `15.569 -> 7.785 ns`;
- TST fixed@20 reuse-distance: PASS diagnostico; il target autorevole degli
  ultimi 40 player-pass misura `0,6922%` repeat;
- TST fixed@20 signed-boundary profile: 40 player-pass, `6.393.749.957` entry,
  calculate `12.673,2 ms`, scale `736,5 ms`, encode `5.605,3 ms`;
- TST signed arity profile: quote entry `48,30 / 29,62 / 16,05 / 6,03%`
  per arity `2 / 3 / 4 / 5`;
- `TerminalReachView`: oracle bitwise PASS; sette repetition indipendenti,
  q=1 circa `1,0x`, q=4 circa `1,71x`, REJECT;
- river terminal sink coverage: 40 player-pass; all `38,40%`, signed
  `40,66%`, opponent `36,17%`;
- hero-tiled treelet Level 1: oracle bitwise PASS su reach/summary, root,
  regret/average scratch, scale e codici; sette repetition, tile 32/64 sotto
  baseline in entrambi gli orientamenti; RAM arena massima `1.623.488 B` per
  otto worker; smoke benchmark più showdown smoke 2/2 PASS;
- tre processi alternati production sequential/two-card: medium `1,846x`,
  large `1,584x`, REJECT;
- topology telemetry TST fixed@1: zero quartetti; pair work coverage
  `34,1305%`; il correctness FAIL del run è atteso perché fixed@1 non è una
  prova di convergenza;
- build profile `/W4 /WX`: PASS; build Release completa con telemetry esclusa:
  PASS;
- reference suite Release post-telemetry: PASS in `97,70 s`;
- full CTest a `18751d8`: 19/20, `gtosd_phase7_tests` riproduce stack overflow
  Windows `0xC00000FD`; legacy certification PASS e flat-worklist-only paired
  FAIL, quindi il pool annidato è escluso;
- paired out-parameter fix: `gtosd_phase7_tests` PASS in tre processi
  consecutivi (`16,38 / 16,24 / 16,75 s`), 215 assertion per processo;
- reference suite Release post-stack-fix: PASS in `98,42 s`, incluso oracle
  bit-exact legacy/paired e mirror zero-rake;
- build Release completa post-stack-fix: PASS;
- full CTest Release finale a `19fe8ea`: **20/20 PASS in 202,66 s**;
- build Release completo post-telemetry: PASS; full CTest finale a `5ae7629`:
  **20/20 PASS in 192,11 s**, incluso benchmark smoke e reference differential;
- build Release completo dopo reach-view/sink telemetry: PASS; full CTest a
  `4a0ce8c`: **20/20 PASS in 193,85 s**;
- build Release finale post-Level-1: PASS; full CTest a `6508bdd`:
  **21/21 PASS in 193,72 s**, incluso il nuovo treelet smoke;
- final-head AHK target-driven: @80, dEV/root/payoff/state/time PASS, solver
  `0,670928 s`, artifact `final-head-ahkhqh.json`;
- final-head TH target-driven: @80, dEV/root/payoff/state/time PASS, solver
  `17,645055 s`, artifact `final-head-th7d6s.json`;
- final-head TST target-driven: @202, dEV/root/payoff/state PASS e time FAIL,
  solver `208,111772 s`, artifact `final-head-tstc9d.json`;
- `git diff --check`: PASS (soli warning EOL Windows).

## 14. Five-process e parity

`five-process certification`: **NON AUTORIZZATA**. TST time e i `memory_gate`
AHK/TH sono rossi. La parity production completa non è dichiarata.

## 15. Stato conclusivo e prossimo passo

Gate final-head chiusi: correctness formale, dEV, Root, layout, convergence,
`solver_state_bytes`, cap desktop 2 GB e tempo AHK/TH. Gate aperti nella suite
versionata: `memory_gate` peak-RSS-vs-GTO+ per AHK/TH e TST solver time. Il
termine dominante lessicografico torna quindi a essere il conteggio hard-gate
(`2`), non il tempo. La five-process resta congelata.

Il 2D-tiling production-faithful e la compatibilità degli interi river subtree
sono ora misurati e chiusi. Il traversal visita `630.596` decision node nel
player-pass pieno, pari al numero totale di decision node riportato dal layout:
non emerge una seconda valutazione dello stesso decision state da eliminare.

Il prossimo singolo passo ad alta priorità è chiudere l'ambiguità normativa
RAM con unità omogenee: stabilire se il gate autorevole è peak RSS contro un
vero peak RSS GTO+, oppure il cap desktop 2 GB separato dallo stato solver, e
versionare tale scelta nelle fixture e nel runner. Finché questo non è deciso,
ottimizzare AHK verso `8 MB` di RSS o dichiararlo PASS a `166,5 MB` sarebbero
entrambi arbitrari.

Dopo quel hard gate, il residuo temporale richiede una fase architetturale
distinta. Il layout reach/rank-card e signed-state tile-local è
stato ora falsificato nella forma bounded treelet: conserva hero-SIMD, scala
exact, stato serializzato, ordine IEEE e margine RAM, ma non riduce il tempo.
Il cost model ora
identifica due budget reali, terminale `57,19%` e signed `43,39%` del lavoro
nominato. Sono ora chiusi anche il layout reach q=1, le tre varianti
updater-only, la pipeline river leaf congiunta, il recursive sink e l'arena
flat: rispettivamente circa `1,0x`, upper bound sotto soglia, circa `26,6%`,
ceiling `20,25%` e rischio RAM oltre margine. Per riaprire la performance serve
una riduzione algoritmica exact non ancora identificata che agisca insieme sui
due budget dominanti senza moltiplicare i tree walk; deve avere una prova
statica e un benchmark end-to-end con upside traversal almeno `30%` prima di
qualsiasi mutazione production. Ogni outcome trasformato continua a richiedere
il reach opponent reale o una prova di invarianza bytewise.
