# Objective-Driven Gate Closure — 2026-08-30

## 1. Provenienza e stato iniziale

- branch: `main`;
- HEAD locale iniziale: `fa77ba436208e4987983eccd11f548b9747ffc6d`;
- `origin/main` iniziale: `fa77ba436208e4987983eccd11f548b9747ffc6d`;
- tracked/staged changes iniziali: nessuno;
- untracked preservati: `.reasonix/`, `.tmp/`.

Il contratto production è rimasto invariato: exact alternating DCFR, alpha 1,5,
beta 0, gamma 2, average immediato reach-weighted con peso `t^2`, signed regret,
`ScaledUint16RegretStrategy`, exact BR, CPU/RAM e massimo otto thread.

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
riferimento analogo TH): questo check risulta rosso pur con il gate production
RAM dichiarato verde. Nel presente report `peak RSS` e `solver_state_bytes`
restano separati; il confronto RSS-vs-GTO+ non viene rinominato né usato per
annullare il gate RAM production.

## 4. Loop ledger

| Loop | Termine dominante | Candidato | Predizione | Misura | Decisione | Nuovo F |
|---:|---|---|---|---|---|---|
| 0 | 2 correctness FAIL | ladder zero-sum seriale | localizzare il primo rung >1e-11 | terminali/river <=2,7e-15; salto turn chance 1,56e-7 | ACCEPT tooling temporaneo, poi rimosso | invariato, informazione acquisita |
| 1 | worst correctness 156.825x | scomposizione chance | distinguere identity/transform/multiplicity | identity <=4,4e-16; transformed 1,56e-7; multiplicity zero | root cause confermata | invariato |
| 2 | reuse con reach non invariante | rivalutare outcome con reach opponent distinto | payoff <=1e-11, BR/profile exact | TST@5 6,143e-9 -> -8,88e-16; TST@20 -> -3,39e-15; target -> 3,22e-15 | PROMOTE `40104f0` | correctness `(2,156825x) -> (0,0)` |
| 3 | certification aumentata | BR actor fast path | -5..15% certification, bit-identico | tre A/B: certification mediana 9,6249 -> 9,0981 s; solver 27,9039 -> 26,1106 s | PROMOTE `c9acfe3` | correctness invariata; time migliorato ma FAIL TST |
| 4 | traversal TST dominante | signed state pass fusion | eliminare almeno una passata >=5% traversal | updater già fuso; 396,8 ms / 26.103,8 ms = 1,52% wall profilato | CLOSE | invariato |
| 5 | certification duplicata | pair profile+BR dello stesso player | -20..35% certification, bit-identico | tre A/B: cert mediana 9,96 -> 7,75 s; exact differential e zero-rake mirror PASS | PROMOTE `4a7e851` | performance migliora, TST time ancora FAIL |
| 6 | terminal/showdown dominante | frontier batching 4-lane | iniziale 2,2..2,9x kernel sintetico | benchmark non faithful: baseline scalarizza prefix/output già AVX2 production; coverage root-local 53,695% | CLOSE autonomo; tooling conservato `0b71489` | informazione acquisita, F invariato |
| 7 | distinct-outcome tail seriale | flat worklist exact + nested pool | -15..30% certification residua | tre A/B con binari distinti: cert mediana 7,148 -> 5,904 s; payoff/BR/profile bit-identici | PROMOTE `0d51fd6` | performance migliora, TST time ancora FAIL |
| 8 | traversal target > limite anche con certification gratuita | whole-river lanes / cross-root broker / continuation | richiede >=30% traversal | hero-SIMD già 8-wide, state disgiunto richiede gather, RAM margin ~27 MB; upper bound credibile 0..13% | REJECT/CLOSE fino a nuovo cost model faithful | blocker architetturale distinto |

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
| solver time | 0,783254 s | limite production 1,900 s | sì | PASS |

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
opponent distinto: TST target ha misurato traversal `183,039575 s`,
certification `69,069453 s`, solver `252,534707 s`. Il fast path BR promosso
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

Il profilo TST@20 attribuisce all'intero signed update `396,8 ms` su
`26.103,8 ms` wall (`1,52%`). Anche eliminarlo interamente, cosa non exact,
resterebbe sotto la soglia economica. `full state-pass fusion` è quindi CLOSE.

## 9. Ranking architetturale

| Candidate | Gain atteso solver | Confidenza | Costo/rischio | Esito |
|---|---:|---:|---|---|
| BR actor fast path | 1-3% atteso, 6,43% short osservato | alta | basso/basso | PROMOTE `c9acfe3` |
| same-player profile+BR pair | 20-35% certification | medio-alta | medio/medio-basso | PROMOTE `4a7e851` |
| flat distinct worklist + nested pool | 15-30% certification residua | alta dopo A/B | medio/medio-basso | PROMOTE `0d51fd6` |
| frontier batching sola accumulation | <=6-7% traversal root-local | alta sull'upper bound | alto/medio | CLOSE |
| cross-root terminal broker | <=12-13% traversal ideale | bassa | alto/alto | MEASURE, insufficiente da solo |
| four-lane whole river | 0-10% pratico non dimostrato | bassa | molto alto/alto RAM | REJECT come integrazione minima |
| four-lane joint certification | 8-18% certification residua | media-bassa | alto/alto | CLOSE per economics |
| full state-pass fusion | <=1,52% traversal assoluto | alta | alto/alto | CLOSE |

Il benchmark four-lane conserva valore come proof-of-concept bit-identica con
quattro reach e payoff distinti, ma non come prova prestazionale production:
la baseline sintetica usa prefix/output scalari, mentre production usa AVX2 su
otto card/hero. La telemetria su 40 player-pass misura `22.132.502` showdown in
`6.231.136` river root; solo `53,695%` cade in quartetti root-local. Non è stato
promosso alcun path sulla proiezione non faithful.

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
- full state-pass fusion: CLOSE, nessuna passata completa eliminabile exact;
- same-player profile+BR pair: PROMOTE, commit `4a7e851`;
- four-lane terminal proof-of-concept e frontier telemetry: tooling PROMOTE
  `0b71489`, integrazione production CLOSE;
- flat distinct-outcome worklist + nested read-only pool: PROMOTE `0d51fd6`;
- whole-river four-lane: REJECT come minima integrazione per perdita hero-SIMD,
  gather state e rischio RAM;
- cross-root broker: MEASURE soltanto, upper bound insufficiente senza un nuovo
  benchmark production-faithful.

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
  F = (0, 0, >=1, 1.958, >=1.14, 0.984, circa 0.49)

dopo BR fast path:
  correctness invariata; TST@20 solver -6.43%; nessun nuovo target-driven
  perché l'upside massimo del candidato non può chiudere il ratio 1.958.

dopo certification pair + worklist/nested:
  correctness invariata; certification TST@20 9.96 -> 7.75 -> 5.90 s nelle
  rispettive mediane; il target solver resta proiettato ampiamente >1.0 perché
  il solo traversal target noto vale circa 1.419x il limite.
```

Il peggioramento temporale del fix è accettato soltanto perché correctness è il
termine lessicograficamente superiore. Non è presentato come chiusura performance.

## 12. Gate matrix finale misurata

| Gate | AHKHQH | TH7D6S | TSTC9D |
|---|---|---|---|
| dEV | 0,655665% PASS | 0,806385% PASS | 0,991863% PASS |
| Root EV | 19,108984 PASS | PASS/invariato | 8,494698 regime PASS |
| payoff/correctness | 0 PASS | -4,44e-16 PASS | 3,22e-15 PASS |
| layout/convergence | PASS | PASS | PASS |
| solver state | 5.300.664 B PASS | 334.452.416 B PASS | 1.472.605.376 B PASS |
| peak RSS | 166.567.936 B PASS cap | 797.335.552 B PASS cap | 1.968.865.280 B PASS cap |
| solver time | 0,688 s fixed@80 PASS | 18,638 s fixed@80 PASS nel sanity corrente | 252,534707 s target pre-perf FAIL; nessun nuovo target promosso |

Il TST@80 finale del nuovo scheduling misura solver `80,554211 s`, traversal
`73,862787 s`, certification `6,280057 s`, payoff-sum `-3,33e-16`, peak RSS
`1.972.805.632 B` e state `1.472.605.376 B`. Il dEV `3,19058%` è atteso per il
fixed-mid e non è un correctness fail. Non è stato speso un nuovo target@202:
anche l'upper bound dei candidati promossi non può portare il traversal noto
sotto il limite totale.

## 13. Validazione

- build Release `gto_cli` e `gtosd_gto_plus_reference_tests`: PASS;
- full CTest Release finale a `0d51fd6`: 20/20 PASS in 210,70 s;
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
- TST@80 mid: payoff/RAM/state PASS;
- AHK/TH target sanity: correctness PASS;
- TST target correctness: PASS;
- `git diff --check`: PASS (soli warning EOL Windows).

## 14. Five-process e parity

`five-process certification`: **NON AUTORIZZATA**. TST time è ancora rosso.
La parity production completa non è dichiarata.

## 15. Stato conclusivo e prossimo passo

Gate chiusi: correctness formale AHK e TST, dEV, Root, layout, convergence,
solver state e RAM cap; TH fixed@80 rientra nel tempo nel sanity finale. Gate
aperto: TST solver time. La five-process resta congelata.

Il prossimo singolo passo ad alta priorità è una fase distinta di ricerca per
un benchmark 2D-tiling production-faithful (hero SIMD x subtree lanes) e
telemetria di compatibilità degli interi river subtree. Deve dimostrare almeno
circa 30% traversal end-to-end prima di una mutazione production; non basta
accelerare la sola accumulation terminale. Ogni outcome trasformato continua a
richiedere il reach opponent reale o una prova di invarianza bytewise.
