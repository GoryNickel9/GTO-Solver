# Strict 2 GB exact-algorithm recheck

> **ARCHIVIATO 2026-09-04 — CONTRATTO MEMORIA RITIRATO.** Il cap Peak RSS
> `<2.000.000.000 B` non era un requisito desktop: era il display GTO+ TSTC9D
> “Memory needed for solving”. Le prove algoritmiche e temporali restano
> evidenza; gli scarti memory-based richiedono rivalutazione. Vedere il
> [`piano di correzione`](../../GTO_PLUS_SOLVER_MEMORY_SEMANTICS_AND_GATE_CORRECTION_PLAN_2026-09-03.md).

Data: 2026-09-01

Fixture: `GTP-AHKHQH-003`, `GTP-TH7D6S-101`, `GTP-TSTC9D-101`

Outcome: **B. SYNC-PCFR REQUIRES AN ISOLATED POSTFLOP TRAJECTORY GATE**

## Analisi

### Obiettivo

Rivalutare indipendentemente sia le famiglie algoritmiche nuove sia quelle già
escluse, senza ottimizzare una fixture e senza modificare production. Il gate è
superato soltanto da una soluzione common che preserva gioco, albero, range,
outcome, exact best response e certificazione.

### Contratto congelato

- stesso desktop, CPU-only, massimo 8 thread;
- peak RSS strettamente `< 2.000.000.000 B`;
- niente sampling, bucketing, GPU, fast-math o logica fixture-specific;
- comparator production: alternating signed DCFR `1.5/0/2`, delay zero,
  `ScaledUint16RegretStrategy`;
- target TST: dEV `< 1%` entro `128,988889 s`;
- produzione e fixture non sono state modificate.

La fresh authority TST misura:

| quantità | valore |
|---|---:|
| dEV @202 | `0,991863%` |
| traversal | `197,392990 s` |
| certificazione | `36,592053 s` |
| solver | `234,437726 s` |
| traversal budget residuo | `91,9441528 s` |
| speedup traversal richiesto | `2,146879208x` |
| peak RSS | `1.971.036.160 B` |
| headroom | `28.963.840 B` |
| persistent state | `1.472.605.376 B` |

Il collo di bottiglia resta il ciclo node-global
`decode -> update -> max/scale -> encode/store`; il replay attribuisce il
`60,007%` del path state a encode/store. Il dettaglio autoritativo è in
[`TST_STRICT_2GB_BOTTLENECK_ATTRIBUTION_AND_FEASIBILITY_LOOP_2026-08-31.md`](TST_STRICT_2GB_BOTTLENECK_ATTRIBUTION_AND_FEASIBILITY_LOOP_2026-08-31.md).

### Assunzioni e rischi

- "exact" indica full-tree/full-chance e certificazione exact; non richiede
  identità bit-per-bit con la traiettoria DCFR;
- il test quantizzato Pure/Sync-PCFR usa scale per infoset nei giochi toy,
  mentre production usa scale per nodo pubblico: è un pre-gate favorevole, non
  prova postflop;
- il corpus real-node segue traiettorie DCFR, non PCFR: occupancy e pursuit
  time sono proxy di screening, non risultati di convergenza PCFR;
- Sync-PCFR è al momento una technical report/preprint, quindi richiede una
  validazione locale più forte di un algoritmo production consolidato.

## Piano

1. riapplicare i gate RAM, correttezza, costo locale e limite Amdahl alle
   esclusioni precedenti;
2. verificare le famiglie 2024-2026 su fonti primarie;
3. implementare Pure/Sync-PCFR soltanto in un oracolo indipendente su giochi
   enumerabili;
4. misurare convergenza per iterazioni logiche e traversate esterne, state
   persistente e comportamento con quantizzazione `uint16`;
5. misurare sul corpus reale occupancy della pure policy e un proxy del tempo
   di pursuit globale;
6. fermarsi prima del target TST se manca una traiettoria postflop PCFR
   rappresentativa.

## Implementazione

Sono stati aggiunti soltanto artefatti di ricerca:

- `tests/pure_cfr_recheck_oracle_tests.cpp`: full-tree Pure CFR e Sync-PCFR
  indipendenti, matching pennies, Kuhn, albero alternato profondo, confronto
  DCFR e ricodifica signed/unsigned `uint16`;
- `tools/analyze_pure_policy_occupancy.ps1`: occupancy delle azioni pubbliche
  quando ogni mano seleziona l'argmax della policy corrente;
- `tools/analyze_sync_pursuit_proxy.ps1`: pursuit-time proxy ricavato da gap
  signed-regret e slope counterfactuale dei replay reali.

Non sono stati aggiunti enum, dispatch, formato checkpoint o path postflop
production.

### Modello Pure/Sync-PCFR controllato

L'oracolo usa due payload persistenti per action-entry:

1. cumulative `Q` signed;
2. cumulative average reach unsigned.

La policy corrente è pura, `argmax_a Q(I,a)`. Durante una fase Sync, finché
l'argmax resta invariato, il delta è moltiplicato per il primo pursuit time
globale. Il modello statico TST è quindi:

```text
366.890.152 actions * 2 payload * 2 B
+ 630.596 nodes * 2 scale * 4 B
= 1.472.605.376 B
```

Lo state passa il gate RAM senza terzo payload.

## Validazione

### Risultati dell'oracolo

Release, processo isolato:

| controllo | risultato |
|---|---:|
| asserzioni | `31/31 PASS` |
| matching pennies NashConv @100k Pure | `0,00448` |
| Kuhn NashConv @250k Pure | `0,000742764` |
| Kuhn EV P0 @250k Pure | `-0,055553` |
| Kuhn NashConv @20k Pure | `0,00326878` |
| Kuhn NashConv @20k Sync | `0,00385844` |
| Kuhn traversate esterne @20k Sync | `405` |
| Kuhn nodi attraversati @20k Sync | `26.694` |
| Kuhn NashConv @20k Sync + uint16 | `0,00273173` |
| Kuhn traversate @20k Sync + uint16 | `428` |
| Kuhn NashConv @20k DCFR comparator | `0,0017313` |
| albero alternato depth 12, Pure | `443 / 8.191` nodi |
| projected TST state | `1.472.605.376 B` |

La build Release completa termina con successo. La suite completa passa
`27/27` test in `203,59 s`, inclusi contratto production DCFR, canonical layout
TST, test postflop/reference e i tre oracle algoritmici. Gli analyzer leggono
tre corpus ciascuno senza modificarli.

Sync-PCFR elimina il `97,975%` delle traversate esterne su Kuhn a parità di
20.000 iterazioni logiche. La ricodifica favorevole `uint16` non distrugge la
convergenza toy. Questo riapre la famiglia: il precedente mismatch IEEE tra
una fase pesata e aggiornamenti ripetuti non è, da solo, un kill gate valido.
Nel controesempio Kuhn @2.000 il massimo delta di strategia esportata è
`0,0125`; deve essere trattato come rischio numerico da misurare, non come
violazione della full-tree exactness.

### Occupancy delle pure policy sul corpus reale

La proxy argmax sulle policy DCFR reali misura:

| fixture | occupancy complessiva | occupancy non iniziale |
|---|---:|---:|
| AHKHQH | `0,8082` | `105/116 = 0,9052` |
| TH7D6S | `0,7647` | `133/148 = 0,8986` |
| TSTC9D | `313/444 = 0,7050` | `278/334 = 0,832335` |

Il traversal vettoriale deve seguire un ramo pubblico se almeno una mano lo
seleziona. Anche concedendo irrealisticamente che tutto il traversal TST
scali con `0,832335`, il risultato sarebbe `164,297159 s`, speedup soltanto
`1,201439x`, contro il budget `91,944153 s`. Pure CFR senza Sync è quindi
eliminato dal gate temporale.

### Pursuit-time proxy Sync sul corpus reale

Il proxy reinterpreta i gap signed-regret come gap `Q` e usa
`opponent_reach * action_value` come slope. Il minimo sui campioni è:

| fixture | iterazioni catturate | minimo globale |
|---|---|---:|
| AHKHQH | `1 / 10 / 20` | `1 / 1 / 2` |
| TH7D6S | `1 / 10 / 20` | `1 / 1 / 1` |
| TSTC9D | `1 / 40 / 80` | `1 / 1 / 1` |

Su TST la frazione di pursuit concorrenti con fase unitaria scende da `100%`
a `0,61%` e `0,19%`, ma basta un solo infoset per imporre fase globale 1. Il
risultato espone il rischio estremo-dimensionale che non esiste su Kuhn. Non è
però un kill gate definitivo perché i gap provengono da una traiettoria DCFR.

### Rivalutazione delle esclusioni precedenti

| famiglia | evidenza rivalutata | decisione corrente |
|---|---|---|
| schedule S6 `1.5/0/5` | Pair 1 TST `8,018810%` tempo e `0,990099%` iterazioni, sotto entrambi i gate `>=10%` | resta esclusa |
| Predictive CFR+/PDCFR+ | terza policy persistente minima `+442.732.000 B`, peak `2.412.654.048 B` | RAM fail |
| Lazy-CFR | anche il lower bound `float32/infoset` aggiunge `582.096.608 B`, peak `2.552.018.656 B` | RAM fail |
| FD-FTRL/FD-OMD | due payload possibili, ma costo locale `3,513x/3,303x`; crossing richiesto `<82,4768/<84,2703` iterazioni | local-cost fail |
| state codec alternativi | float32/16 `154,54 s` e RAM fail; float32/32 `119,98 s` e RAM fail; ideal-state `93,30 s` | tempo/RAM fail |
| producer streaming | circa `1,041x`; idealizzato `168,04 s` | tempo fail |
| wavefront/treelet/compiled traversal | `1,059x-1,191x` locale/river; compiled-control `2,94%` | end-to-end fail |
| RBP | nessuna finestra guaranteed-skip comune; manca una prova per signed DCFR | correctness/evidence fail |
| range-aware physical orbit | il controesempio asymmetric-range conserva un value rappresentativo ma altera EV/BR/regret/strategy | correctness fail |

Nessuna esclusione precedente viene riaperta. La sola riapertura è la nuova
composizione **Pure CFR + synchronization**, che non era stata sottoposta al
gate corretto di traversate esterne.

### Famiglie nuove verificate

| famiglia | fonte primaria | gate |
|---|---|---|
| PTB+ | [NeurIPS 2024 paper](https://proceedings.neurips.cc/paper_files/paper/2024/file/3e2aeb66481dd63a32421bf032b70384-Paper-Conference.pdf) | vettori treeplex/prediction e due proiezioni; RAM/costo locale non compatibili |
| RTCFR+ | [NeurIPS 2025 paper](https://personal.ntu.edu.sg/boan/papers/NeurIPS25_RTCFR.pdf) | anche concedendo metà state-path, floor `145,346495 s`; servirebbe almeno `1,580813x` meno iterazioni, non dimostrato |
| SOGRM+ | [NeurIPS 2025 paper](https://proceedings.neurips.cc/paper_files/paper/2025/file/1b8612e11c75456c90963fd408d75c4d-Paper-Conference.pdf) | il paper dichiara aperta la convergenza delle varianti RM+ dentro CFR/EFG |
| policy-gradient exploitability | [NeurIPS 2025 paper](https://openreview.net/attachment?id=VYY5sG4EMm&name=pdf) | trajectory sampling, fuori contratto |
| NashPG | [TMLR submission](https://openreview.net/forum?id=yIA2Fjs1FK) | model-free/sampling e non ancora authority consolidata |
| Pure/Sync-PCFR | [arXiv 2311.07155](https://arxiv.org/abs/2311.07155), [Pure CFR arXiv 2309.03084](https://arxiv.org/abs/2309.03084) | RAM/toy pass; Pure-only time fail; Sync resta provisional survivor |

### Casi non verificati

- nessuna traiettoria PCFR sul vero layout postflop;
- nessuna distribuzione reale delle lunghezze di fase PCFR su AHK/TH/TST;
- nessun costo del global minimum pursuit scan su `366.890.152` action-entry;
- nessun target-driven TST Sync-PCFR;
- nessuna compatibilità checkpoint/CLI, intenzionalmente fuori scope finché il
  trajectory gate non passa.

## Decisioni

1. **Pure CFR senza Sync è respinto**: due payload/RAM passano, ma occupancy
   vettoriale e convergenza per iterazione non offrono lo speedup `2,146879x`.
2. **Sync-PCFR non è respinto né promosso**: il toy oracle mostra un vantaggio
   strutturale reale e compatibilità preliminare con due payload; il proxy TST
   mostra però che la fase globale può collassare a 1.
3. **Nessuna precedente esclusione cambia stato** dopo la rivalutazione.
4. **Nessuna modifica production** e nessun target TST sono autorizzati da
   questa evidenza.
5. L'outcome non è più `C. frontier exhausted`: è `B. survivor requires
   postflop trajectory gate`.

Compromesso: il prossimo prototipo deve duplicare temporaneamente una parte
del traversal/update path. È debito di ricerca accettabile perché evita di
contaminare l'enum, il checkpoint e il comparator production prima della
prova determinante.

## Passo successivo

Implementare un **isolated Sync-PCFR postflop trajectory probe** su un piccolo
river enumerabile e poi su AHKHQH bounded: deve misurare lunghezza delle fasi,
traversate/nodi, NashConv exact, drift `uint16`, RSS e costo del pursuit scan.
Solo se il probe proietta TST sotto `91,944153 s` traversal si autorizza una
Pair 1 target-driven TST; altrimenti la famiglia viene chiusa senza spendere
il benchmark completo.
