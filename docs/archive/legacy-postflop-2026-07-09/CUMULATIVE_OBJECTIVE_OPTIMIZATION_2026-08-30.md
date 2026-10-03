# Cumulative Objective Optimization Loop — 2026-08-30

> **CORREZIONE SEMANTICA 2026-09-04 — REPORT STORICO.** Il confronto Peak RSS
> con “Memory needed for solving” e il cap desktop 2 GB non sono contratti
> validi. Misure, correttezza e timing restano evidenza; i giudizi memoria sono
> ritirati. Vedere il
> [`piano di correzione`](../../GTO_PLUS_SOLVER_MEMORY_SEMANTICS_AND_GATE_CORRECTION_PLAN_2026-09-03.md).

## 1. Stato iniziale e scope

- branch locale: `main`;
- HEAD locale iniziale: `d798d8967ee30658177a56acd2e4bbc1088d1e8a`;
- `origin/main` dopo `git fetch origin main`:
  `d798d8967ee30658177a56acd2e4bbc1088d1e8a`;
- tracked/staged changes iniziali: nessuno;
- untracked preservati: `.reasonix/`, `.tmp/`.

Il confronto `peak RSS` contro il campo GTO+ `Memory needed for solving` è
deliberatamente deferred. Il runner non è stato modificato. Restano autorità:
correctness, dEV, Root EV, layout/fingerprint, convergence, payoff-sum,
normalization, solver state, cap desktop 2 GB, AHK/TH time e contratto
production. Exact alternating DCFR, parametri, averaging, signed regret,
`ScaledUint16RegretStrategy`, exact BR, fixture e precisione sono rimasti
congelati.

## 2. Funzione obiettivo

```text
F = (
  correctness_fail_count,
  hard_constraint_fail_count,
  worst_time_ratio,
  aggregate_time_ratio,
  tst_solver_seconds
)
```

La valutazione è lessicografica. Il `memory_gate` GTO+ deferred non contribuisce
a `hard_constraint_fail_count`. Per la baseline:

```text
AHK ratio = 0.670928 / 1.900000 = 0.353120
TH  ratio = 17.645055 / 19.622222 = 0.899238
TST ratio = 208.111772 / 128.988889 = 1.613409

F = (0, 0, 1.613409, 0.955256, 208.111772)
```

Ogni iterazione ha seguito `MEASURE -> MODEL -> GENERATE -> RANK -> TEST ->
PROMOTE/REJECT -> REBASELINE -> RECOMPUTE OBJECTIVE -> REPEAT`. Nessun
candidato ha superato Level 1, quindi non è stata creata una falsa rebaseline.

## 3. Baseline autorevole omogenea

| Fixture | Iter | dEV | Root EV | Solver | Limite | Traversal | Certification | Correctness/state/desktop |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| AHKHQH | 80 | 0,655665% | 19,108984 | 0,670928 s | 1,900000 s | n/a | n/a | PASS |
| TH7D6S | 80 | 0,806385% | 8,221632 | 17,645055 s | 19,622222 s | n/a | n/a | PASS |
| TSTC9D | 202 | 0,991863% | 8,494698 | 208,111772 s | 128,988889 s | 174,926380 s | 32,780800 s | PASS |

Gli artifact omogenei sono `out/objective-gate-closure-20260830/final-head-*.json`.
Il cap desktop è PASS con peak RSS rispettivamente 166.510.592 B,
798.371.840 B e 1.969.922.048 B; gli state sono 5.300.664 B, 334.452.416 B e
1.472.605.376 B.

## 4. Riclassificazione storica

### A — empirical reject

Non riaperti: streaming-by-rank, rank-base fusion, hero-tiled treelet,
zero-fill, showdown cache, action-liveness, terminal reach q=1, cross-root
four-lane broker, accumulation-only batching e le implementazioni già provate
di state/update che erano regressive o sotto rumore. Questi hanno benchmark
faithful negativo oppure exactness non sufficiente.

### B — coverage / mathematical close

- same-reach showdown aggregation: coverage target `0,6922%`;
- whole-river width-4: zero quartetti compatibili;
- terminal-to-state direct sink: il q=1 faithful è circa `1,0x`;
- flat bottom-up arena: payload minimo ~28,7 MB prima dei metadata contro
  ~27 MB di margine TST;
- full state-pass fusion: la scala globale del nodo rende obbligatoria la fase
  di encode exact;
- same-reach, transformed-result reuse e physical-orbit reuse restano chiusi
  dai rispettivi vincoli di reach/exactness.

### C — chiusi soltanto dalla vecchia soglia del 30%

Sono tornati nel pool: recursive hierarchical opponent sink, width-2 river,
hierarchical signed encode/partial writeback e joint certification. I primi due
sono stati misurati in questa task. Gli ultimi due sono stati ricalcolati con i
cost model finali e chiusi solo dopo aver dimostrato un upper bound wall sotto
5%.

## 5. Candidate pool e composability

| Candidate | Family | Expected gain | Confidence | Cost | Numerical risk | RAM | Composability | Prior evidence |
|---|---|---:|---:|---:|---:|---:|---:|---|
| recursive opponent sink | value traffic | 5-20,25% traversal ceiling | media | medio | medio | basso | alta | chiuso solo da 30% |
| whole-river width-2 | terminal SIMD | 4-10% traversal | media | alto | medio | medio | alta | 34,1305% river coverage |
| hierarchical signed encode | state traffic | 0-5% traversal | medio-bassa | alto | medio | medio-alto | alta | encode 5.605,3 ms CPU-equivalent |
| joint certification | exact BR | 8-18% certification | medio-bassa | alto | alto | medio | alta | pair+worklist già promossi |

La priorità interna ha favorito prima il sink (richiesto, indipendente e costo
Level 1 moderato), poi width-2 (unico residuo con un upper bound vicino a 5% del
traversal). Le famiglie signed e certification sono state ricalcolate dopo i
due REJECT.

## 6. Loop ledger

| Loop | HEAD | Bottleneck | Candidate | Expected gain | Measured gain | Hard constraints | Decision | New objective |
|---:|---|---|---|---:|---:|---|---|---|
| 0 | `d798d89` | TST traversal 174,926 s | historical reclassification | informazione | pool A/B/C ricostruito | invariati | ACCEPT analysis | `(0,0,1.613409,0.955256,208.111772)` |
| 1 | `d798d89` | opponent value round-trip | recursive hierarchical sink | 5-20,25% traversal ceiling | 1,4-2,4% Level 1; un outlier 4,2% | bitwise output; RAM/scratch invariati | REJECT | invariato |
| 2 | `d798d89` | river terminal | width-2 production kernel | 4-10% traversal | 1,19-1,26x kernel; 3,1-3,9% traversal upper bound | bitwise output; fixture-independent | REJECT | invariato |
| 3 | `d798d89` | signed encode/writeback | hierarchical encode / partial writeback | vecchio ceiling 15,08% wall | <=2,5% traversal anche eliminando tutto l'encode | exact scale barrier; RAM cap | MATHEMATICAL CLOSE | invariato |
| 4 | `d798d89` | certification 32,781 s | joint four-lane certification | 8-18% certification | wall upper bound <5%: due pair già concorrenti 4+4 thread | exact BR/layout invariati | MATHEMATICAL CLOSE | invariato |

Non ci sono PROMOTE production. Tutti i benchmark candidate sono stati rimossi;
nessun flag dormiente o ramo benchmark-specific resta nel solver.

## 7. Recursive hierarchical opponent sink — Level 1

La vecchia ipotesi è stata ricostruita così: il primo child di ogni nodo
opponent scrive direttamente nel sink del nodo; i child successivi usano un
solo temporaneo depth-local e sono sommati immediatamente. Questo conserva
l'ordine per azione e le parentesi IEEE del child, evita il round trip del primo
action value e non modifica reach, regret timing o update order.

Il benchmark separato dal solver ha usato:

- `float`, come il value representation production;
- combo reali TH `206/358` e TST `358/301` nei due orientamenti;
- topologia mista depth 3, arità `3 x 2 x 4`, 24 producer;
- gli stessi action scratch preallocati per baseline e candidate;
- producer/consumer order identico e `memcmp` completo degli output.

Tre coppie di processi in ordine `B/C`, `C/B`, `B/C`, sette/ cinque repetition
interne secondo la misura, hanno dato mediane wall con gain tipico:

| Workload | Gain processi | Range |
|---|---:|---:|
| TH P0 | ~2,1% | 1,7-2,3% |
| TH P1 | ~1,7% | 1,4-2,0% |
| TST P0 | ~1,6% | 1,4-4,2% (ultimo baseline contaminato) |
| TST P1 | ~1,7% | 1,6-2,4% |

Il risultato è sotto 3% nella parte rilevante e il solo valore sopra 4% non è
riproducibile: REJECT Level 1.

## 8. Whole-river width-2 — Level 1

Il secondo benchmark ha mantenuto accumulation per reach nello stesso ordine,
prefix AVX2, metadata SoA, payoff distinti e output production; ha interlacciato
due root e quattro hero per vettore AVX2. L'oracle ha confrontato bit per bit
entrambi gli output contro due chiamate production sequential.

Tre processi alternati `B/C`, `C/B`, `B/C`, sette repetition ciascuno:

| Orientamento | Baseline median range | Candidate median range | Speedup range |
|---|---:|---:|---:|
| TST P0 | 3.399-3.444 ns | 2.820-2.905 ns | 1,185-1,221x |
| TST P1 | 3.387-3.437 ns | 2.695-2.788 ns | 1,231-1,257x |

Applicando il miglioramento alla coverage work-weighted `34,1305%` e al river
terminale ~`57,19%` del traversal, il massimo realistico è circa `3,1-3,9%` del
traversal prima di scheduling, packing e tail: REJECT, tooling rimosso.

## 9. Signed/state e certification residuali

La telemetry storica misura per 40 pass signed `5.605,3 ms` CPU-equivalent di
encode/writeback. Rapportato alla capacità di `27.644,1 ms x 8` del traversal,
anche eliminare gratuitamente l'intera fase vale ~`2,5%` wall. La maggioranza
delle scale cambia; un optimistic first-pass encode aggiungerebbe lavoro nei
nodi cambiati. Hierarchical encode e partial writeback sono quindi chiusi con
la nuova soglia, non con quella vecchia del 30%.

La certification corrente esegue già due `policy_profile_br_pair` concorrenti,
tre worker interni più caller per giocatore: `4 + 4 = 8` thread. Fondere i due
player dimezzerebbe idealmente le traversal ma anche i due gruppi concorrenti;
range `358/301`, reach e payoff distinti aggiungono padding/gather. Non esiste
un wall upper bound credibile >=5% senza una nuova architettura BR.

## 10. Differential, A/B e promotion

- opponent sink: exact output equality PASS; A/B Level 1 REJECT;
- width-2: exact output equality PASS; A/B Level 1 REJECT;
- nessun candidate ha raggiunto Level 2, quindi non è stata toccata production
  e non sono stati rivendicati state/profile/BR differential non eseguiti;
- nessun PROMOTE, nessuna nuova baseline e cumulative speedup `0,000%`;
- il profilo dominante resta traversal, con terminal/rank-card e signed/state
  già coperti dai REJECT/upper bound sopra.

## 11. Stato finale e blocker

```text
Initial TST:      208.111772 s
Final TST:        208.111772 s
Cumulative delta: 0.000000 s (0.000%)
Limit:            128.988889 s
Residual gap:      79.122883 s (1.613409x)
```

Con certification invariata, il traversal dovrebbe scendere da `174,926380 s`
a non più di `96,208089 s`: riduzione `44,999%`, speedup ~`1,818x`. Anche con
certification gratuita serve ridurre traversal di `26,264%` (~`1,356x`). I
candidati exact e componibili dell'attuale action/value/state layout con
expected gain >=5% sono empirical reject, mathematical close o RAM-prohibitive.

Stato: **EXHAUSTED con blocker architetturale**. Per proseguire serve una nuova
rappresentazione/dataflow con una prova end-to-end che elimini congiuntamente
producer terminale e state traffic, conservando scale/rounding exact e il cap
2 GB. Tale rewrite non è una mutazione sicura da introdurre come singolo
candidato in questa task.

La certificazione a cinque processi non è stata eseguita perché il singolo TST
non passa il time gate.

## 12. Validazione finale

- `git diff --check`: PASS;
- CTest Release completo: **21/21 PASS**, 197,38 s;
- `gtosd_gto_plus_reference_tests`: PASS, 88,69 s;
- `gtosd_tstc9d_canonical_layout`: PASS;
- `gtosd_production_dcfr_contract`: PASS;
- benchmark smoke showdown/treelet: PASS.

Poiché nessun candidato ha modificato production, AHK e TH mantengono la
baseline omogenea PASS (`0,670928 s` e `17,645055 s`); TST resta FAIL temporale
ma PASS per correctness, state e cap desktop. Il memory gate GTO+ deferred non
è stato reinterpretato né forzato.
