# Architectural Traversal/Dataflow Feasibility Loop — 2026-08-30

> **CORREZIONE SEMANTICA 2026-09-04 — REPORT STORICO.** Il cap desktop 2 GB
> usato in questo studio non è mai stato un requisito indipendente. Le misure e
> i ceiling temporali restano evidenza; classificazioni e scarti che dipendono
> dal cap memoria devono essere rivalutati con la futura metrica solver-owned.
> Vedere il
> [`piano di correzione`](../../GTO_PLUS_SOLVER_MEMORY_SEMANTICS_AND_GATE_CORRECTION_PLAN_2026-09-03.md).

## 1. Stato iniziale

- branch: `main`;
- HEAD atteso e verificato: `abe77d6b39d9a7ef3503f0d942ca9aaa3d4fe6ea`;
- subject: `docs(perf): record cumulative objective optimization`;
- `git fetch origin main` eseguito prima dei commit: `origin/main` e
  `FETCH_HEAD` sono entrambi `abe77d6b39d9a7ef3503f0d942ca9aaa3d4fe6ea`;
- tracked changes iniziali: nessuno;
- `.reasonix/`, `.tmp/` e gli output locali non versionati sono stati
  preservati;
- nessun push è stato eseguito.

Il contratto è rimasto exact alternating DCFR signed `1.5/0/2`, delay zero,
average reach-weighted immediato `t^2`, `ScaledUint16RegretStrategy`, exact BR,
CPU/RAM only, massimo otto thread, public DAG canonico lossless, senza
sampling, bucketing, GPU o cambio di precisione. Il confronto peak RSS contro
il campo GTO+ `Memory needed for solving` è fuori scope; restano vincolanti il
cap desktop 2 GB e `solver_state_bytes`.

## 2. Baseline autorevole e blocker ereditato

| Fixture | Iter | dEV | Root EV | Solver | Limite | Traversal | Certification | Esito in scope |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| AHKHQH | 80 | 0,655665% | 19,108984 | 0,670928 s | 1,900000 s | n/a | n/a | PASS |
| TH7D6S | 80 | 0,806385% | 8,221632 | 17,645055 s | 19,622222 s | n/a | n/a | PASS |
| TSTC9D | 202 | 0,991863% | 8,494698 | 208,111772 s | 128,988889 s | 174,926380 s | 32,780800 s | time FAIL |

TST richiede traversal `<=96,208089 s` con certification invariata: riduzione
`44,999%`, speedup `1,818x`. Anche azzerando idealmente la certification serve
almeno `1,356x` sul traversal. Il cumulative loop precedente è **EXHAUSTED**:
le micro-ottimizzazioni della rappresentazione node/action/value/state
corrente non hanno un ceiling sufficiente. Le famiglie empiricamente respinte
e quelle chiuse matematicamente in
`CUMULATIVE_OBJECTIVE_OPTIMIZATION_2026-08-30.md` non sono state riaperte.

## 3. Objective e protocollo del loop

```text
F = (
  correctness_fail_count,
  hard_constraint_fail_count,
  architecture_feasibility_fail,
  required_tst_speedup_gap,
  tst_solver_seconds,
  th_time_ratio,
  ahk_time_ratio,
  desktop_ram_ratio
)
```

Il ciclo effettivamente usato è:

```text
OBSERVE -> MODEL -> GENERATE ARCHITECTURES -> COMPUTE CEILINGS -> RANK
-> PROVE FEASIBILITY -> BUILD SHADOW PROTOTYPE -> BENCHMARK
-> ACCEPT / REJECT -> UPDATE MODEL -> REPEAT
```

Un candidato costoso entra nello shadow soltanto con gain traversal previsto
`>=20%` (preferibile `>=30%`). Entra in production soltanto con shadow exact,
subtree `>=1,50x`, proiezione TST `>=15–20%`, RAM accettabile e generalità.

## 4. Loop ledger

| Loop | Hypothesis | Structural coverage | Theoretical ceiling | Exactness risk | RAM | Level | Measurement | Decision |
|---:|---|---:|---:|---:|---:|---|---|---|
| 1 | bounded wavefront per signature | TST frontier W4: 53,839% byte; W8: 22,108% | W4 con mediano peggiore: 1,059x sul river | medio | bounded <10 MB stimati; shadow 0,42 MB | SHADOW | exact; mediani 1,116x/1,327x, heavy 2,092x/1,854x | REJECT Level 2 |
| 2 | compiled/linearized river program | 100% dei 3.480.688 control op osservati | 2,94% del traversal @1, anche con costo lineare sostitutivo | basso | 56,69 MB piano ingenuo | MEASURED | checksum equal; 39,584 ms recursive vs 14,826 ms linear | REJECT |
| 3 | continuation producer→consumer | 100% value; sottoinsieme exact già dimostrabile 39,19% delle action value | 1,111x sul river per il sottoinsieme exact; 1,343x soltanto azzerando irrealisticamente ogni materializzazione | alto | tile bounded basso | MEASURED | modello 5,802 GB action-value traffic; global scale e ordine parent impediscono eliminazione totale | REJECT |
| 4 | runtime state AoSoA standalone | state traffic 16,022% dei byte modellati | 1,191x perfino azzerando tutto lo state traffic | alto | nessuna shadow copy da 1,47 GB consentita | MEASURED | modello byte-level | REJECT standalone |

Required speedup remaining:

- iniziale: `1,818x`;
- dopo la proiezione conservativa wavefront: `1,716x`;
- dopo il bound composito ideale dei soli pezzi exact dei loop 1–3:
  `1,499x` ancora necessario.

## 5. Topology dataset

Il comando generale, non fixture-specifico,
`gto_cli postflop architecture-gto-plus <fixture> <report>` prepara il vero
layout production canonico e produce un JSON aggregato più un CSV per work
unit. Una work unit è un river root canonico proveniente da un turn chance
site, per update player. L'inspector è read-only e non alloca lo state solver.

| Fixture | Work unit | Turn chance site | Public-node visit | Row metadata |
|---|---:|---:|---:|---:|
| TSTC9D | 335.984 | 6.325 | 3.480.688 | 56.445.312 B |
| TH7D6S | 86.592 | 1.353 | n/a | 14.547.456 B |
| AHKHQH | 12.222 | 315 | n/a | 2.053.296 B |

TST ha zero unit invalide/cicliche. Il dataset registra actor, update player,
action count, capacità hero/opponent, board mask/rank class, terminal pattern,
payoff hash, transform identity/non-identity, state interval/shape, profondità
strutturale tramite il subtree hash, chance descendants, public/decision node,
work e byte. I 56,4 MB sono tooling offline e non una proposta di metadata
persistent production.

## 6. Structural signature

La strict signature combina, senza benchmark ID o board string:

```text
(actor, update_player, action_count, exact hero/opponent capacity,
 terminal-child pattern, subtree control shape, payoff shape,
 identity/non-identity permutation shape, exact state shape)
```

Board, reach, payoff values, permutation, state offsets, scale e output restano
payload lane-local. La signature `variable_lane` rimuove capacità e state shape
dal key per misurare un upper bound con lane variabili; il suo 100% non è un
gain gratuito, perché richiederebbe padding, mask e gather.

## 7. Local, frontier e global batchability

Copertura strict TST work-weighted/byte-weighted:

| Scope | Width | Node | Showdown | Action/value/state | Byte | Lane occupancy |
|---|---:|---:|---:|---:|---:|---:|
| local | 2 | 46,687% | 46,165% | 46,165% | 46,174% | 65,226% |
| local | 4 | 0,602% | 0,589% | 0,589% | 0,589% | 33,809% |
| local | 8 | 0% | 0% | 0% | 0% | 16,904% |
| frontier-64 | 2 | 95,102% | 86,611% | 84,336% | 85,026% | 95,331% |
| frontier-64 | 4 | 81,582% | 58,376% | 51,904% | 53,839% | 82,217% |
| frontier-64 | 8 | 58,684% | 28,403% | 19,495% | 22,108% | 61,154% |
| global | 2 | 99,995% | 99,956% | 99,947% | 99,950% | 99,995% |
| global | 4 | 99,890% | 98,979% | 98,762% | 98,831% | 99,893% |
| global | 8 | 99,700% | 97,510% | 96,986% | 97,152% | 99,602% |

`global >> local` dimostra che il DFS nasconde lavoro omogeneo; la topologia
giustificava quindi lo shadow wavefront. Il fenomeno è generale: TH byte W4 è
`0 / 70,885 / 98,810%` local/frontier/global; AHK è già favorevole localmente
(`79,269 / 99,181 / 99,931%`). Node coverage non è stata usata al posto di
work/byte coverage.

## 8. Independence proof

- **fully independent:** sibling dello stesso turn chance source con state
  interval disgiunti; il parent può ridurre i risultati in ordine canonico;
- **conditionally independent:** source differenti soltanto dopo forward
  reach discovery e cattura della continuation; ogni lane conserva reach,
  board, transform, payoff, state, scale e output propri;
- **not reorderable:** interval sovrapposti, parent che richiede subito il
  risultato, shared mutable state o topologia invalida.

Sono state osservate 75.032 unit con state interval per ciascun update player e
zero overlap. Questo prova disgiunzione dello state, non rende disponibili in
anticipo i reach: per source differenti serve il descriptor bounded usato nello
shadow. Dentro ogni unit il kernel production conserva ordine di accumulo,
parenthesization, global max pre-encode, quantizzazione, discount, average e
update order.

## 9. Byte traffic current

Il modello conservativo non presume cache hit e conta entrambi gli update
player:

| Famiglia | Byte | Quota |
|---|---:|---:|
| terminal: opponent reach, rank/card, prefix, hero output | 8.052.441.408 | 35,481% |
| value: action write/read e parent accumulation | 8.104.758.784 | 35,713% |
| state: strategy/regret/scales/codes | 3.636.057.600 | 16,022% |
| reach: actor reach e strategy-driven propagation | 2.900.856.832 | 12,784% |
| totale | 22.694.114.624 | 100% |

Le sole action value materializzate valgono 725.214.208 entry e 5.801.713.664
B write+read, `25,565%` del totale. Azzerarle tutte darebbe al massimo
`1,343x` su questo river model; azzerare perfino ogni byte value darebbe
`1,556x`, ma è un bound non implementabile perché parent accumulation,
immediate regret e global node scale devono leggere i valori nell'ordine
production.

## 10. Architecture candidates e ranking

| Architecture | Work coverage | Byte reduction | SIMD | Control reduction | RAM | Exactness risk | Expected ceiling |
|---|---:|---:|---:|---:|---:|---:|---:|
| bounded wavefront strict W4 | 53,839% byte | nessuna nello shadow | medio-alto | scheduling amortizzato | basso | medio | 1,059x river misurato/proiettato |
| continuation fused exact subset | 39,19% action-value entry | <=10,019% byte totale | medio | medio | basso | alto | 1,111x river |
| compiled river program | 100% control op | metadata/pointer only | basso | alto localmente | 56,69 MB naive | basso | 1,030x traversal |
| runtime state AoSoA | 16,022% byte state | parziale, mai 100% | alto | basso | conversione in-place obbligatoria | alto | <1,191x river |
| static river supernode | shape compatibili | come continuation | alto | alto | bounded | alto | chiuso da global scale/order |

Ranking iniziale: wavefront, continuation, compiled plan, state AoSoA,
supernode. Il pool è stato aggiornato dopo ogni misura: lo shadow wavefront ha
spostato il compiled plan davanti a un rewrite AoSoA; il ceiling control ha
poi spostato continuation al primo posto; il byte proof ha chiuso anche quella
nella forma exact disponibile.

L'audit hot/cold separa nel `ControlOp` soltanto kind, board index, edge count e
action offset/count. Label, history, stringhe di serializzazione e metadata del
browser non entrano nel piano hot. Il piano resta respinto per ceiling e RAM,
non perché trascini cold metadata.

## 11. RAM model

TST production usa peak RSS 1.969.922.048 B, quindi il margine al cap decimale
2 GB è 30.077.952 B. Non è ammessa una shadow copy dello state da
1.472.605.376 B.

- shadow W4 massimo osservato: 422.048 B addizionali;
- descriptor: 11.552 B per quattro lane;
- state/scales copiati per sample: 166–410 KB / 0,5–1,2 KB;
- frontier bounded a 64 source: ordine di grandezza sotto 10 MB con descriptor
  e output bounded;
- topology rows offline: 56,45 MB, da non persistere;
- piano lineare naive: 56,69 MB, non entra nel margine production;
- global frontier: centinaia di MB di reach descriptor, quindi non feasible.

Un formato opcode compresso a 8 B sarebbe vicino a 28,35 MB e consumerebbe
quasi tutto il margine per un ceiling del 2,94%; viene respinto.

Il checkpoint/logical layout resta node-major/action-major e non è stato
modificato. Un eventuale runtime AoSoA dovrebbe sostituire in-place la vista
hot e convertire una sola volta a inizio/fine solve: una rappresentazione
persistent duplicata è esclusa. La conversione non è stata implementata perché
il candidato standalone fallisce già il ceiling teorico.

## 12. Shadow prototype

Il Level-1 shadow è compile/runtime gated da
`GTOSD_ARCHITECTURAL_SHADOW=1`; width 2/4/8 è selezionabile separatamente. Usa
river subtree reali TST, median/heavy, P0/P1 e signature strict con source
distinti. La schedule locale esegue le unit sequenzialmente; la frontier le
invia al pool production. Ogni run usa reach scoperti con forward traversal,
board/transform reali, state reali e il kernel completo legacy per terminal,
values, state, transforms e scratch. Lo state selezionato viene snapshot,
ripristinato e non resta modificato.

## 13. Exact differential

TST @20 sul binario Release finale con hotpath profiling OFF, tre ripetizioni
per sample:

- parent returned values: bit equal;
- regret code: byte equal;
- strategy code: byte equal;
- regret scale: bit equal;
- strategy scale: bit equal;
- tutti i sample W4 e W8: exact.

Il fixed-20 non è una certification di convergenza; è esclusivamente un
differenziale architetturale. Non sono stati usati i reach quasi-zero di @1
come risultato rappresentativo.

## 14. Subtree benchmark

| Width | Player | Workload | Local | Frontier | Speedup | Gate 1,50x |
|---:|---:|---|---:|---:|---:|---|
| 4 | P0 | median | 0,1714 ms | 0,1536 ms | 1,116x | FAIL |
| 4 | P0 | heavy | 0,3872 ms | 0,1851 ms | 2,092x | PASS |
| 4 | P1 | median | 0,1618 ms | 0,1219 ms | 1,327x | FAIL |
| 4 | P1 | heavy | 0,4288 ms | 0,2313 ms | 1,854x | PASS |
| 8 | P0 | median | 0,2462 ms | 0,2087 ms | 1,180x | FAIL |
| 8 | P0 | heavy | 0,6012 ms | 0,2418 ms | 2,486x | PASS |
| 8 | P1 | median | 0,2424 ms | 0,1298 ms | 1,867x | PASS |
| 8 | P1 | heavy | 0,5710 ms | 0,1972 ms | 2,896x | PASS |

Gli heavy non possono sostituire il workload rappresentativo. W8 supera il
gate locale ma copre soltanto il 22,108% byte nella frontier bounded; W4 copre
di più ma fallisce `1,50x` sui mediani.

## 15. Amdahl projection e control audit

Per W4, usando coverage byte `C=0,53839` e il peggiore mediano `S=1,11589`:

```text
projected river speedup = 1 / ((1-C) + C/S) = 1,0592x
```

Per W8, `C=0,22108`, `S=1,17968`: `1,0348x`. Entrambi sono sotto il gate
production `15–20%`, anche assumendo irrealisticamente che il river model sia
il 100% del traversal. L'utilizzo CPU lungo già osservato (`86,1%`) rende una
proiezione scheduling-only ancora meno favorevole.

Il compiled-plan audit ha eseguito gli stessi 3.480.688 op nello stesso ordine
e checksum:

```text
recursive pointer-rich: 39,5844 ms
linear hot-op plan:      14,8259 ms
local speedup:            2,6700x
eliminable / traversal:  24,7585 / 841,2636 ms = 2,94%
```

Il grande speedup control-only non è dominante end-to-end.

## 16. Decisione production

Nessuna integrazione production è autorizzata:

1. wavefront W4 fallisce il gate `1,50x` sui mediani e proietta 14,68% solo
   sul river model;
2. W8 ha copertura bounded insufficiente;
3. global batchability non è RAM-feasible;
4. compiled traversal ha ceiling 2,94% e piano troppo grande;
5. continuation exact disponibile non elimina abbastanza traffico;
6. state AoSoA standalone non raggiunge il threshold neppure con byte state
   idealmente gratuiti.

Non esistono dispatch per TST, board, fixture o fingerprint. Il production
path è invariato; non sono quindi necessari A/B B/C, cross-fixture target o
five-process.

## 17. Eventuale integration differential

Non eseguito, per decisione del gate e non per mancanza di verifica: nessun
candidato ha soddisfatto contemporaneamente subtree `>=1,50x`, proiezione
`>=15–20%` e RAM. Di conseguenza non esiste un nuovo production mode da
confrontare a TST @5/@20. I differenziali shadow bit-equal validano soltanto la
riordinabilità bounded dello stesso kernel legacy e non vengono presentati
come integration differential.

## 18. Proiezione finale e blocker quantitativo

Componendo in modo ottimistico e indipendente:

```text
wavefront W4 conservative       1,0592x
exact continuation subset       1,1113x
compiled control replacement    1,0303x
combined ideal                  1,2129x
projected TST traversal       144,227 s
projected solver              177,007 s  (certification invariata)
remaining traversal speedup     1,499x
```

Anche questa composizione ottimistica resta 48,018 s sopra il solver limit.
Il risultato del loop è quindi **FUNDAMENTAL BLOCKER per le architetture
valutate**: l'exact node-global scale e l'ordine di update obbligano una seconda
fase e impediscono di consumare tutte le action value direttamente; il cap RAM
impedisce la frontier globale e una seconda rappresentazione dello state; il
solo controllo è troppo piccolo. Non è una prova che nessuna architettura
futura possa riuscire, ma falsifica quantitativamente wavefront scheduling,
compiled traversal, continuation exact disponibile e state AoSoA standalone.

## 19. Next loop state

Candidate lifecycle finale:

```text
bounded wavefront: IDEA -> MEASURED -> FEASIBLE -> SHADOW -> REJECT
compiled plan:     IDEA -> MEASURED -> REJECT
continuation:      IDEA -> MEASURED -> mathematical CLOSE
state AoSoA:       IDEA -> MEASURED -> mathematical CLOSE
static supernode:  IDEA -> MEASURED -> mathematical CLOSE
```

La singola attività successiva ad alta priorità è dimostrare, prima di scrivere
codice production, se esiste una rappresentazione exact dello state che
mantenga i code/scale canonicali byte-identici ma renda tile-local la fase
producer→global-max→encode senza seconda copia persistente. Se il global max
node-wide resta inevitabile, il blocker è definitivo entro il contratto di
precisione congelato.

## 20. Validazione

- build Release MSVC `/W4 /WX`: `gto_cli` e `gtosd_phase10_tests` PASS;
- test topology read-only/disjoint: incluso in Phase 10;
- Phase 10: `10.593` assertions PASS;
- topology TST/TH/AHK: dataset generati, TST zero invalid/cyclic;
- shadow TST @20 W4/W8: tutti i differenziali code/scale/value bit-equal;
- fixed-20: diagnostico, non presentato come dEV/correctness certification;
- full Release CTest con hotpath profiling OFF: 21/21 PASS;
- nessun benchmark production promosso e nessuna five-process eseguita.

Gli artifact JSON/CSV restano output locali sotto `out/architectural-loop/` e
non fanno parte del commit.
