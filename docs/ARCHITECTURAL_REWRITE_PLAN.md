# Piano di riscrittura architetturale — th7d6s < 90 s

Data: 2026-08-06 · Stato: PIANO (da approvare prima dell'esecuzione)
Obiettivo: `run_solver` th7d6s < 90 s con tutti i gate verdi (dEV ≤ 1e-6, AhKhQh invariato, suite PASS).
Riferimento: GTO+ 17.66 s sullo stesso benchmark (misura dell'utente, anche sotto carico).

---

## 1. Stato attuale (misurato, verificato bit-exact)

| Metrica | Baseline | Attuale | Delta |
|---|---|---|---|
| `run_solver` | 273.6 s | **187.3 s** | −31.5% |
| traversal | 211.8 s | 173.7 s | −18% |
| certification | 60.0 s | 12.0 s | −80% |
| regret_application | 22.6 s | 23.3 s | ~0 |
| dEV finale | 0.9249962879022555 | 0.9249962879022555 | bit-exact |
| AhKhQh 101/103 | 0.6741554018356799 | 0.6741554018356799 | bit-exact |

Ottimizzazioni già integrate (tutte bit-exact): O1 action-base diretta, R1-full spazi
per-giocatore contigui (`PlayerIndexed`), certificazione parallela (policy pool),
split `worker_count+1`, dispatch contiguo, edge-order, R2 strategie trasposte,
**batch SIMD strategie count==2** (divisioni, −21% run), **value-loop SIMD fuso**.
O2 (bucket sort) provato e **revertito** (regressione netta).

### 1.1 Profilazione per-pass (seriale-equivalente, profiler hot-path corretto)

| Voce | s/pass | Note |
|---|---|---|
| copy reach (`copy_reach` + multiply) | ~2.8 | 38% — 2 copie prefix (564 double = 4.5 KB) per edge |
| children (ricorsione, inclusa) | ~4.3 | sovrapposta: include copy+value+terminal dei figli |
| copie Result `ComboVector` (5 KB) | ~1.5–2 (stima) | il return-by-value muove 5 KB per nodo |
| value loop + update | ~0.1 | SIMD fuso, ormai trascurabile |
| terminali (fold/showdown) | ~0.2 | member scratch, no alloc |
| strategy | ~0.0004 | batch SIMD |

### 1.2 Limiti fisici misurati

- **Bandwidth**: ~20 GB/s effettivi (DDR4 dual-channel, i3-10100F). Traffico stimato
  ~4.5 TB per run → ~225 s se saturo → il solver È bandwidth-bound.
- **Pool**: scaling 1→5 = 1.95×, 1→7 (8 thread) = 1.98×. **Cap a ~2×**: saturazione bus.
- **Stato**: 667 MB (regret float32 333 MB + **delta double 667 MB** + strategy float32),
  picco RSS 6.5 GB.
- GTO+ a 17.66 s ⇒ ~350 GB di traffico ⇒ rappresentazione ~13× più compatta
  (float32 ovunque + layout infoset compatto + AVX2 sistematico).

### 1.3 Gap residuo

187.3 → 90 s = **2.08×**. Con le sole leve incrementali (out-param, float32 reach)
si arriva realisticamente a ~120–140 s. Il <90 s richiede la riscrittura sotto.

---

## 2. Design architetturale proposto

Principio guida: **ridurre il traffico memoria (il collo) di ~2.5× e portare il
parallelismo effettivo da ~2× a ~4×**, senza cambiare il protocollo (140 iterazioni,
cert ogni 20, target 1%) e senza alterare la convergenza (stesse iterazioni ⇒
stesso dEV a parità di aritmetica; dove l'aritmetica cambia, gate ≤ 1e-6).

### 2.1 Layout stato compatto (tutto float32)

- `deferred_regret_delta_` da `double` (667 MB) a **`float`** (333 MB) con
  **accumulazione compensata a coppie** nel batch per-action (Kahan su blocchi di 4)
  oppure flush parziale: il delta accumula solo gli scarti della passata; l'errore
  float32 relativo ~1e-7 × √N è sotto il gate se la compensazione è a blocchi.
  → −333 MB di traffico nel apply + metà cache miss.
- Reach per-giocatore in `float` (2.25 KB/edge invece di 4.5 KB): le copie reach
  dimezzano. La precisione: prodotti di strategie (0..1) su ≤ 20 moltiplicazioni,
  errore relativo ~1e-6 — **misurare la deriva dEV per fase**; se > 1e-6, tenere
  la reach in double ma dimezzare le copie con l'out-param del §2.2.
- Strategy/regret già float32 ✓. Il `strategy_float32` resta (stato del protocollo).

### 2.2 Flusso valori out-param (eliminazione copie Result)

Il costo nascosto: `Result<ComboVector, Error>` return-by-value muove 5 KB per nodo
(trivial-copy std::array ⇒ copia reale) + la copia prefix in `action_values`.
Riscrittura: le funzioni interne `cfr_*`/`policy_*` scrivono in un **out-param**
(`std::optional<PostflopSolverError>` come ritorno) e l'API pubblica resta
`Result`-based (adattatore). Inoltre:

- **Buffer valore per-thread**: uno stack di `ComboVector` riusabili (uno per
  profondità di ricorsione, ~20 × 5 KB = 100 KB/thread) invece di allocare lo
  stack-frame 5 KB a ogni chiamata; il figlio scrive nel buffer del padre
  (`action_values[action]`) direttamente.
- Il `transform_values_to_parent` per l'identità (path direct-action-bases) diventa
  un no-op: il figlio scrive già nello spazio del padre (stesso updating player).
- → elimina ~1.5–2 s/pass di copie + la thrash dello stack (write-allocate DRAM
  sulle linee dei frame).

### 2.3 SIMD sistematico (già parzialmente fatto)

- **update regret+strategy** (per-combo, azioni contigue dopo O1): `__m256d` su
  4 azioni o 4 combo; add_strategy con gather della reach.
- **apply_deferred_regrets**: il loop sugli indici touched è scattered (casuale) —
  riordinare il touched per blocco contiguo alla raccolta (bucket per range di
  azioni) e vettorizzare il clip `max(0, r+d)`.
- **chance accumulate**: liste per-card di combo non-bloccate precomputate nel
  layout (elimina il mask-check per combo per edge); SIMD su 4 slot.
- **terminali**: prefix scan del showdown trasposto `[rank][card]` (36 card
  contigue per rank-step → 8× __m256d); fold con read di `by_card` vettorizzate.
- **certificazione**: già parallela (12 s); applicare le stesse ottimizzazioni.

### 2.4 Parallelismo per blocco (R3 del doc, con correzioni misurate)

Il pool a turn-chance satura a ~2× per bandwidth. Dopo la riduzione del traffico
(§2.1–2.2), il margine di bandwidth permette più parallelismo. Design:

- **Partizione per infoset**: ogni thread possiede un sottoinsieme disgiunto di
  action-base (blocchi contigui del flat array) per regret/strategy/delta ⇒
  **nessun atomico, nessun merge** alla fine della passata (il merge
  `merge_deferred_regrets_from` scompare).
- Il traverse dell'albero resta condiviso (read-only: tree/layout), i valori
  (per-thread buffer) e le reach (per-thread copie) sono privati.
- Split anche al **river chance** (non solo al turn): i 32×32 sotto-alberi turn-river
  danno granularità fine e bilanciamento dinamico (coda condivisa pull-based,
  niente FIFO che serializza — il doc avvertiva del rischio).
- Atteso: 2× → 3.5–4× (se il traffico ridotto non satura il bus).

### 2.5 Ordine di applicazione e gate

Ogni fase: build → smoke (5 iter) **bit-exact** → run completo (dEV ≤ 1e-6,
`correctness_passed`, `layout_matches_fixture`) → AhKhQh 101/103 (0.6741554018356799,
±1e-6) → suite (reference + phase1-10 + framework + gtosd_tests) → journey update.

---

## 3. Roadmap a fasi (stime ottimistiche su macchina scarica)

| Fase | Contenuto | run_solver stimato | Rischio | Durata |
|---|---|---|---|---|
| **A** | Out-param valori + buffer per-thread + transform no-op | ~140–155 s | basso | 2–4 h |
| **B** | Reach float32 (con misura deriva dEV; fallback: solo out-param) | ~120–135 s | medio | 1 giorno |
| **C** | Delta float32 compensato + apply SIMD (bucket contiguo) | ~110–125 s | medio | 1 giorno |
| **D** | Parallelismo per blocco (partizioni infoset disgiunte + split river + coda pull) | ~85–105 s | alto | 2–3 giorni |
| **E** | SIMD terminali/chance (prefix trasposto, liste non-bloccate) | ~75–95 s | medio | 1–2 giorni |
| **F** | Polish: update SIMD, certificazione, rimozione scratch residui | < 90 s | basso | 1 giorno |

Totali: ~7–10 giorni lavorativi. Il target <90 s è raggiunto tra D ed E/F;
GTO+ 17.66 s richiede ulteriori passi (v. §5).

---

## 4. Vincoli da rispettare (dal protocollo e dal doc)

1. **Niente modifiche ai parametri del protocollo**: fixture th7d6s/AhKhQh,
   `maximum_iterations` 140, `certification_interval` 20, `averaging_delay` 0,
   `parallel_action_depth` 5, `target_dev_percent` 1, range, bet-sizes.
2. **Precisione**: stato float32, compute float64 — ogni deroga (reach/delta
   float32) è ammessa SOLO con misura esplicita della deriva dEV e accettazione
   ≤ 1e-6; il percorso bit-exact resta preferito dove possibile.
3. **Determinismo**: stesso dEV a parità di build (già verificato); il nuovo
   parallelismo deve preservare l'ordine di accumulo o documentare la deriva.
4. **Expected layout** invariato (info_sets 36,596,832 / actions 83,318,592);
   se cambia, aggiornare la fixture con un passaggio di review.
5. Ogni fase si chiude solo con gate verdi (§2.5) e aggiornamento di
   `speed_optimization_journey.md`.

---

## 5. Oltre il target: verso 17.66 s (riferimento GTO+)

Se il benchmark deve avvicinare GTO+ (17.66 s), dopo le fasi A–F servono:

- **Layout infoset compatto**: azioni per infoset contigue e indicizzate
  direttamente (niente `decision_action_base` ricercato), iterazione
  infoset-major nei traverse (oggi è combo-major: 22K decisioni × 2.5 azioni).
- **Albero esplorato per street** con value single-pass (niente ricorsione
  profonda con frame da 5 KB).
- **Averaging nel dominio float32 con accumulatore doppio** (il GTO+ usa
  accumulazione periodica).
- **NUMA/affinità** e prefetch software sulle liste combo.
- Misurabile solo con un profiler a campionamento (VTune/perf) su build di fase.

---

## 6. Metriche di successo (accettazione finale)

- `elapsed_seconds` (run_solver) th7d6s **< 90 s** (best-of-3, macchina scarica).
- `final_gto_plus_dev_percent` ≈ 0.925 ± 1e-6 (o ≤ 1e-6 di deriva dal reference).
- `converged: true`, `correctness_passed: true`, `layout_matches_fixture: true`.
- AhKhQh 101/103: `correctness=pass`, dEV 0.6741554018356799 ± 1e-6.
- Suite completa PASS (reference + phase1-10 + framework + gtosd_tests).
- Journey aggiornato a ogni fase con numeri e decisioni.
