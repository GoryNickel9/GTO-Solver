# Next traversal optimization - 2026-08-30

## Stato e decisione

**Decisione finale: `REJECT`.**

Questo report riguarda esclusivamente il traversal postflop production
condiviso. TSTC9D e' usato come microscopio; non esistono branch per fixture,
board, range, fingerprint o dimensione. RBP resta fuori scope e non viene
riaperto.

## 1. Checkout e contratto

- branch iniziale: `main`;
- HEAD iniziale: `c20050a62bc64e8b81512e45133e01c03b2071a3`
  (`docs(postflop): record RBP read-only audit`);
- `origin/main`: identico a HEAD;
- working tree iniziale: nessuna modifica tracked; `.reasonix/` e `.tmp/`
  erano untracked e sono stati preservati;
- build timing: `out/build/windows-release-current`, Release MSVC 19.51,
  `GTOSD_ENABLE_HOTPATH_PROFILE=OFF`;
- build profiling: `out/build/windows-profile-current`, Release MSVC 19.51,
  `GTOSD_ENABLE_HOTPATH_PROFILE=ON` e runtime
  `GTOSD_PROFILE_HOTPATH=1`;
- macchina: Intel Core i3-10100F, 4 core/8 thread logici; solver configurato a
  8 thread.

Il contratto e' rimasto exact alternating DCFR signed con `alpha=1.5`,
`beta=0`, `gamma=2`, averaging delay zero, average reach-weighted `t^2`,
`ScaledUint16RegretStrategy`, exact BR, CPU/RAM-only e public DAG/isomorfismi
lossless. Le fixture versionate non sono state modificate.

## 2. Baseline Release del commit corrente

Run target-driven singoli, stesso binario Release, nessuna telemetria RBP o
hot-path. I tempi precedenti restano riferimenti storici; questa tabella e'
la misura omogenea sul commit iniziale.

| Fixture | Iter | dEV | Root EV / delta | Root | Correctness aggregata | Traversal | Certificazione | Solver | CPU norm. | Peak RSS | Solver state |
|---|---:|---:|---:|---|---|---:|---:|---:|---:|---:|---:|
| AHKHQH | 80 | 0.655702% | 19.108987 / -0.041013 | PASS | FAIL (nodi condizionali/action frequency e payoff sum) | 0.590752 s | 0.097088 s | 0.714445 s | 69.71% | 166,727,680 B | 5,300,664 B |
| TH7D6S | 80 | 0.806385% | 8.221632 / -0.000348 | PASS | PASS | 15.225668 s | 3.828727 s | 19.446404 s | 84.11% | 797,368,320 B | 334,452,416 B |
| TSTC9D | 202 | 0.991865% | 8.494698 / -0.006952 | PASS | FAIL (payoff sum `2.862e-7` vs `1e-11`; EV/frequency PASS) | 167.716297 s | 38.092169 s | 206.257104 s | 84.92% | 1,968,726,016 B | 1,472,605,376 B |

AHK non viene dichiarato correctness PASS: il solo Root EV passa. TST passa
dEV, root e RAM ma il suo `206.257104 s` fallisce sia il riferimento GTO+
grezzo `116.09 s` sia il limite temporale `128.988889 s`.

### Contatori strutturali cumulativi

| Fixture | Visited | Decision | Chance | Outcomes | Fold | Showdown | Regret entry | Strategy entry |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| AHKHQH @80 | 3,704,868 | 2,640,010 | 48,653 | 1,557,685 | 32,492 | 983,713 | 91,185,853 | 91,185,853 |
| TH7D6S @80 | 28,481,560 | 21,374,046 | 204,133 | 6,533,677 | 142,644 | 6,760,737 | 5,916,859,741 | 5,916,859,741 |
| TSTC9D @202 | 310,650,321 | 237,715,700 | 2,442,166 | 78,163,225 | 1,914,196 | 68,578,259 | 68,580,297,489 | 68,580,297,489 |

## 3. Profiling aggiornato

I run profilati sono fixed-iteration e certificano soltanto alla fine. Il
profiling esegue scansioni read-only aggiuntive di densita', scale e
fingerprint: i suoi wall time non sono confrontabili con la Release. I tempi
sotto sono CPU-equivalent sommati sui worker per gli ultimi due player-pass;
non sono percentuali wall.

### TSTC9D early/mid/late - ultimo update P0/P1

| Checkpoint/pass | Wall pass | Decision | Showdown calls | Rank/card | Prefix | Output | Value/update | Regret matching | Chance | Reach |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 20 / P0 | 741.2 ms | 620,693 | 626,549 | 673.9 ms | 282.8 ms | 672.9 ms | 577.8 ms | 438.5 ms | 173.7 ms | 7.7 ms |
| 20 / P1 | 728.6 ms | 599,631 | 605,529 | 493.3 ms | 255.0 ms | 877.8 ms | 683.4 ms | 334.0 ms | 192.7 ms | 7.6 ms |
| 80 / P0 | 682.1 ms | 609,760 | 610,099 | 592.5 ms | 252.1 ms | 648.8 ms | 537.5 ms | 399.5 ms | 150.6 ms | 7.9 ms |
| 80 / P1 | 660.7 ms | 568,994 | 574,239 | 427.9 ms | 249.7 ms | 791.6 ms | 616.9 ms | 303.3 ms | 178.3 ms | 8.5 ms |
| 202 / P0 | 683.5 ms | 598,068 | 600,198 | 616.8 ms | 253.2 ms | 641.4 ms | 547.2 ms | 413.1 ms | 152.0 ms | 7.7 ms |
| 202 / P1 | 657.4 ms | 553,611 | 556,621 | 426.6 ms | 232.6 ms | 783.9 ms | 603.5 ms | 304.3 ms | 187.4 ms | 13.5 ms |

La crescita early->late non e' monotona nel numero di chiamate: il pruning
exact riduce alcuni rami, mentre densita' e asimmetria P0/P1 cambiano il costo
per chiamata. Il wall Release, non perturbato, cresce invece da 0.464 s alla
prima iterazione a circa 0.80-0.87 s nella parte tarda (con un outlier di
1.034 s a iterazione 160).

### Confronto cross-fixture @80 - ultimi due player-pass

| Fixture/pass | Wall | Decision | Showdown calls | Hero combo | Opponent combo | Value/update | Regret matching | Chance |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| AHK / P0 | 5.4 ms | 17,206 | 15,285 | 492,891 | 492,891 | 5.2 ms | 2.7 ms | 2.4 ms |
| AHK / P1 | 6.0 ms | 15,776 | 13,673 | 440,671 | 440,671 | 5.3 ms | 2.6 ms | 2.5 ms |
| TH / P0 | 245.7 ms | 141,375 | 130,622 | 23,698,282 | 41,184,140 | 196.5 ms | 190.3 ms | 37.9 ms |
| TH / P1 | 166.8 ms | 129,186 | 117,545 | 37,047,915 | 21,302,644 | 145.2 ms | 70.4 ms | 41.4 ms |
| TST / P0 | 682.1 ms | 609,760 | 610,099 | 119,763,234 | 161,695,238 | 537.5 ms | 399.5 ms | 150.6 ms |
| TST / P1 | 660.7 ms | 568,994 | 574,239 | 152,161,217 | 112,661,988 | 616.9 ms | 303.3 ms | 178.3 ms |

Il numero di action entry aggiornate @80 cresce da 91.2 M (AHK) a 5.92 G
(TH) e 27.17 G (TST). Il carico TST amplifica quindi sia il numero di
decisioni sia le passate su grandi vettori; il delta non dipende da un ID di
fixture.

### Scale, ricodifica e scheduling

Negli ultimi pass TST @202 si osservano rispettivamente 594,810/542,760 scale
check, 295,238,866/364,743,528 entry ricodificate e
174,485,019/227,281,804 entry in nodi che eccedono la scala precedente. La
persistent scale gia' respinta non viene riaperta: cambierebbe la traiettoria
quantizzata e non aveva uno speedup riproducibile.

La distribuzione dell'ultimo TST @202 e' bounded e senza coda persistente:
P0 assegna 7 task al main e 89-158 a ciascun worker; P1 assegna 9 task al main
e 79-133 a ciascun worker, con worker task-wall compresi rispettivamente circa
646-701 ms e 609-672 ms. Il CPU normalizzato Release sul target completo e'
84.92%: esiste tail/serial overhead, ma non e' il candidato a rischio minimo.
Queue depth puntuale e hardware cache counters non sono disponibili; non
vengono inventati.

## 4. Ranking dei costi prima del codice

1. **Showdown dataflow** - massimo volume CPU-equivalent: accumulo rank/card,
   prefix e produzione valori. E' molto chiamato e memory/scatter heavy, ma i
   tentativi recenti di cache/fusione hanno mostrato localita' insufficiente o
   regressioni. Non e' selezionato senza nuova identita' matematica.
2. **Value/update e state recode** - 537-617 ms negli ultimi pass TST @80;
   decine di miliardi di action entry nel run. Contiene passate obbligatorie
   per massimo/scala/encoding e almeno una passata write-only ridondante.
3. **Regret matching** - 303-400 ms/pass TST @80, elevato costo per action
   entry e crescita molto maggiore di AHK. E' gia' fuso nel path signed river
   quando la strategy non serve ai discendenti; ulteriori cambi rischiano
   ordine/rounding.
4. **Chance/board** - 151-178 ms/pass TST @80, proporzionale agli outcome e ai
   mapping; non domina per chiamata e le trasformazioni osservate sono
   strutturalmente necessarie.
5. **Scheduling/tail** - CPU Release ~85%, task-wall non perfettamente
   uniforme ma coda bounded; e' poco scalabile, non la maggiore ridondanza.
6. **Reach materialization** - 8 ms/pass circa nel TST corrente; molto meno
   importante dei vecchi profili e non selezionato.

Classificazione: showdown e regret matching sono soprattutto "molto
chiamati"; output showdown e state update hanno costo/chiamata e traffico
memoria alti; scheduling e' il componente poco scalabile; le passate sui
buffer state/value sono memory-bandwidth sensitive; lo zero-fill selezionato
e' ridondanza strutturale dimostrabile.

## 5. Unico candidato selezionato - ipotesi pre-implementazione

**Ipotesi:** nel fast path river signed-scaled, quando
`decision.player == updating_player`, rimuovere lo `std::fill_n` del buffer
`values` immediatamente precedente a `update_scaled_regrets(...,
compute_values=true)`. Il kernel scrive una volta ogni indice in
`board.player_combos[updating_player]`; con `BoardLocal=true` tali indici sono
il prefisso contiguo `0..updating_count-1`, esattamente il dominio azzerato.

**Lavoro eliminato:** un passaggio di store su `updating_count * sizeof(float)`
per ogni decisione river signed in cui agisce il player aggiornato. Due scale
check corrispondono a un update di decisione signed; il profilo TST late
osserva circa 271k-297k update/pass complessivi, in larga parte river. Il
numero esatto di byte risparmiati verra' esposto da un contatore diagnostico
se necessario; non si confonde capacita' del `ComboVector` con celle toccate.

**Perche' lossless:** il valore precedente di ogni cella nel dominio vivo non
viene letto; `update_scaled_regrets<true,...>` costruisce `current` nello
stesso ordine FP e chiama `store_values` su tutte le celle. Regret, average,
scale, encoding e traversal dei child non cambiano. Nessuna slot bloccata e'
nel dominio board-local restituito dal river.

**Rischio:** una futura uscita anticipata dal kernel o un action count non
coperto potrebbe lasciare una cella stale. Il patch deve quindi restare nel
branch signed/non-locked che chiama il kernel con `compute_values=true`; il
branch opponent continua ad azzerare prima della somma.

**Criterio di rollback:** qualunque differenza byte-level nei quattro buffer,
dEV/profile/BR/root/work counter, oppure nessun vantaggio riproducibile TST o
una regressione cross-fixture, comporta rimozione completa del candidato.

## 6. Differential, A/B, RAM e decisione finale

### Differential TSTC9D @20

Per il differential e' stato usato temporaneamente un hash FNV-1a sui byte raw
dei quattro vettori checkpoint. La scansione avveniva dopo il timer solver ed
e' stata rimossa prima degli A/B; non resta alcun hook nel codice finale.

| Buffer | Hash baseline | Hash candidato | Esito |
|---|---|---|---|
| `cumulative_regret_uint16` | `fnv1a64:46ed7068f1a5d5ba` | `fnv1a64:46ed7068f1a5d5ba` | byte-equal |
| `cumulative_strategy_uint16` | `fnv1a64:223553ecbe6831ca` | `fnv1a64:223553ecbe6831ca` | byte-equal |
| `regret_node_scale` | `fnv1a64:bbdfa70f389aefe7` | `fnv1a64:bbdfa70f389aefe7` | byte-equal |
| `strategy_node_scale` | `fnv1a64:e3af321eca993dcd` | `fnv1a64:e3af321eca993dcd` | byte-equal |

Entrambi i run completano 20 iterazioni con dEV `15.4470661808115%`, profile
EV `0.486195210946663 / -0.486195053372660`, exact BR
`2.82302779143125 / 1.98533553555717`, normalized NashConv
`0.300522698088401` e Root EV `8.48619521094666`, tutti identici. Sono
identici anche i work counter: 29,646,967 visited, 22,017,088 decision,
235,913 chance, 7,550,565 outcome, 177,903 fold, 7,216,063 showdown e
6,393,749,957 entry sia regret sia strategy.

La prova lossless passa. Il timing della stessa coppia non e' una promozione:
baseline traversal/solver `15.184468 / 20.983836 s`, candidato
`15.528871 / 21.964992 s`.

### A/B Release TSTC9D @20

Due eseguibili costruiti con le stesse opzioni e distinti soltanto dal
candidato sono stati eseguiti in ordine B/C, C/B, B/C. SHA-256 binari:

- baseline: `84CEF8FBFEAAFB2A04214C53BA8992BC7FAE21D5D5CB68358A2326FF4DA9A4F9`;
- candidate: `08986C18D3457FE1E3211B11B1ED9397881A686C1F35EC7B49D39CEE962B358A`.

| Run | Traversal baseline | Traversal candidate | Solver baseline | Solver candidate | Peak RSS baseline | Peak RSS candidate |
|---|---:|---:|---:|---:|---:|---:|
| 1 | 16.339862 s | 21.800534 s | 22.588381 s | 29.259740 s | 1,968,508,928 B | 1,968,496,640 B |
| 2 | 17.185959 s | 16.387618 s | 23.311125 s | 22.622994 s | 1,968,717,824 B | 1,968,381,952 B |
| 3 | 16.473481 s | 16.475385 s | 22.463485 s | 22.511017 s | 1,968,304,128 B | 1,968,119,808 B |
| **mediana** | **16.473481 s** | **16.475385 s** | **22.588381 s** | **22.622994 s** | - | - |
| min/max | 16.339862 / 17.185959 s | 16.387618 / 21.800534 s | 22.463485 / 23.311125 s | 22.511017 / 29.259740 s | - | - |

Il primo candidate e' contaminato anche nella preparazione e ha CPU
normalizzata `68.58%`, contro `85.39%` nel baseline adiacente. Anche
escludendolo, il confronto piu' pulito run 3 e la mediana danno traversal
rispettivamente `+0.001904 s` (`+0.0116%`) e nessun guadagno. La mediana
solver peggiora di `0.034613 s` (`+0.1532%`). Sono differenze nel rumore della
macchina, non uno speedup riproducibile. La RAM e' invariata nella pratica e
il solver state resta strutturalmente 1,472,605,376 B.

### Cross-benchmark e time-to-target

Non eseguiti per il candidato. Il criterio pre-implementazione richiedeva un
vantaggio TST riproducibile prima di 80 iterazioni, TH/AHK e target-driven;
questo gate fallisce gia' @20. I profili baseline cross-fixture della sezione 3
restano validi, ma non vengono falsamente presentati come non-regressione del
candidato. Cinque processi indipendenti non sono autorizzati da questo esito.

### Decisione

`REJECT`. Il candidato e l'hash diagnostico sono stati rimossi dal production
path. La prova matematica/byte-level era positiva, ma il lavoro eliminato e'
troppo piccolo o gia' assorbito dal traffico dominante per produrre un
vantaggio end-to-end misurabile. Non vengono modificati contratto, fixture,
tolleranze o riferimenti GTO+.

La prossima singola direzione ad alta priorita' e' misurare una riduzione di
una passata completa nel dataflow signed regret+average/encoding, non un altro
zero-fill locale; qualunque fusione deve mantenere l'ordine FP e i quattro
buffer byte-identici.

## 7. Validazione finale e repository hygiene

- candidato traversal: rimosso;
- hook hash diagnostico: rimosso;
- build Release completa sul production rollback: PASS;
- CTest Release completo: **20/20 PASS**, `219.27 s`;
- `gtosd_phase7_tests`: PASS, `19.63 s`;
- `gtosd_gto_plus_reference_tests`: PASS, `94.54 s`;
- `git diff --check`: PASS;
- diff finale tracked: solo questo report;
- `.reasonix/` e `.tmp/`: preservati;
- report JSON/log ed eseguibili A/B: soltanto sotto `out/`, non versionati;
- commit/push: non eseguiti, perche' non esiste una modifica production
  promossa.
