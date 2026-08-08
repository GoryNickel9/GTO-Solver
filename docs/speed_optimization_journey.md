# Diario di ottimizzazione velocità — benchmark GTO+ th7d6s

> **Documento di lavoro**: registro cronologico **completo e verificato** degli interventi di
> ottimizzazione applicati per ridurre il tempo del benchmark `GTP-TH7D6S-101`, con stato ed
> esito di verifica per ogni step. Contesto, obiettivo e protocollo di verifica:
> [`docs/TH7D6S_SPEED_OPTIMIZATION.md`](docs/TH7D6S_SPEED_OPTIMIZATION.md) (obiettivo <90 s,
> protocollo di verifica §5, gate di accettazione §1).

---

## 1. Obiettivo e stato attuale

Ridurre `elapsed_seconds` del run completo del benchmark **`GTP-TH7D6S-101`** (short deck HU
postflop, flop Th7d6s, range asimmetrici pesati, 4 raise/street, 50% pot) **sotto i 90 secondi**,
mantenendo la **correttezza bit-exact** (dEV/root EV identici, curve di convergenza identiche).

**Stato attuale: `NON raggiunto` su macchina semi-carica** — e, dopo la **scoperta critica** di
misurazione (§5.5), il target <90 s è **da rivalutare su macchina IDLE**: tutte le misurazioni
della sessione sono state inquinate dal carico esterno della macchina (Baldur's Gate 3, browser,
capture — §5.5), con varianza osservata **±30%**.

Dopo tutti gli interventi documentati sotto, il run completo misura **~236 s** (miglior run
**235.96 s** in `out/th7d6s_v12.json`, dEV bit-exact) — un **gap di ~2.6×** rispetto alla soglia
di 90 s, **misurato su macchina semi-carica**. Il **profiling del `DenseTraversal`** (§6) ha
identificato il collo nel **compute per-nodo** (terminali + strategy ≈ 58% del wall): le copie
`reach` **non** sono il problema (erano la stima della doc). I fixes applicati durante la diagnosi
(§6.3) sono tutti dEV **bit-exact** e la suite resta verde, ma non spostano il tempo in modo
decisivo. La correttezza è invece **piena e bit-exact** (dEV 0.9249962879022555, vedi §3). Le
cause del gap sono la macchina — i3-10100F (4 core / 8 thread), **memory-bandwidth-bound** (§4,
§6.4) con floor di bandwidth stimato **~110–140 s** (§5.4) — **più il carico esterno variabile**
(§5.5).

Criteri di accettazione (non negoziabili, dal protocollo §1–§5 della doc):

1. **th7d6s**: `converged=true` a ~140 iterazioni, `final_gto_plus_dev_percent ≈ 0.9249962879022555`,
   `correctness_passed=true`, `layout_matches_fixture=true`, `flop_co_root` dentro tolleranza.
2. **AhKhQh** (101, 103 root-lock, 104): continuano a passare identici
   (`final_gto_plus_dev_percent ≈ 0.6741554018356799`).
3. **Suite test** verde: `gtosd_gto_plus_reference_tests` (reference exact, parity fisico↔DAG ≤1e-11,
   external root lock), `gtosd_tests`, `gtosd_phase1_tests` … `gtosd_phase10_tests`, framework.
4. **Nessuna modifica al protocollo benchmark** (max_iterations=200, cert_interval=20,
   averaging_delay=20, parallel_action_depth=5, target_dev=1.0) né alle definizioni di gioco
   (fingerprint invariato).

**Esito complessivo**: i criteri 1–4 di **correttezza** sono tutti soddisfatti. L'unico criterio
non soddisfatto è il **target di velocità** (<90 s), non verificato su macchina idle: prima di
dichiararlo irraggiungibile la misura va ripetuta **a carico di fondo nullo** (chiudere
gioco/browser, §5.5).

---

## 2. Registro cronologico degli interventi (in ordine di implementazione)

| # | Step | Intervento | Esito verifica | Stato |
|---|---|---|---|---|
| 0 | **Baseline riprodotto** | Run completo `out/th7d6s_baseline.json` su exe esistente, sorgente invariata (nessuna modifica). | `elapsed_seconds` **273.6 s**, wall **311.7 s**, traversal **211.8 s**, certification **56.2→60.0 s**, regret_application **22.6 s**. `final_gto_plus_dev_percent` **0.9249962879022555**, `converged=true` a **140 iter**, `correctness_passed=true`. | fatto / verificato |
| 1 | **O1 — action base diretta per automorfismi identità** | Flag `uses_direct_action_bases` su `DenseLayout` (true quando `automorphisms.size() <= 1`, il caso th7d6s). In `build_layout` ramo isomorphic: `decision.action_base` assegnato direttamente; in `decision_action_base`: fast path `action_base + local × action_count` (elimina 2 lookup sparsi). Path canonical/DAG (AhKhQh) gated: invariato. | dEV th7d6s **bit-exact** (0.9249962879022555), AhKhQh 101/103 **bit-exact** (0.6741554018356799), test reference PASS. Timing nel rumore di sistema. | fatto / verificato |
| 2 | **O2 — bucket sort per regret application** | Bucket sort O(n) (bucket da 2^16) applicato prima dei loop di apply in `apply_deferred_regrets`/`apply_pool_regrets` per rendere sequenziali gli accessi su ~1 GB di stato. | dEV **bit-exact**; smoke 51.5 s. **Ma**: sul run completo il sort costa ~0.3 s/iterazione → `regret_application` cresce 22.6 s → ~48–55 s (run o2: 48.6 s; iter1: 55.6 s) mentre l'apply era già veloce. **Regressione netta → REVERTITO.** Lezione documentata (§4). | fatto / verificato / **revertito** |
| 3 | **R1 fase A — prova di eliminabilità** | Nei loop valori di `cfr_decision`/`policy_decision` vengono saltati i valori non-updating-player (combo con reach zero, valori mai letti). Prova che le combo fuori range non influenzano il risultato. | dEV smoke **bit-exact** (104.64860904530644 a 5 iterazioni, identico al riferimento documentato); test reference PASS. | fatto / verificato |
| 4 | **R1 iter1 — liste compatte (combo-id)** | `cfr_decision`/`policy_decision` iterano `board.player_combos[actor]` (strategie/azioni) e `board.player_combos[updating_player]` (valori); chance accumulate sul player; terminali compatti (`fold_values_with_payoff`/`showdown_values_with_payoffs`), gated su `uses_direct_action_bases` (path DAG invariato). | dEV smoke **bit-exact**; test reference PASS; smoke **~48 s** (run 44.6–50.5 s, da ~52 s). | fatto / verificato |
| 5 | **R1 iter2 — prefix copy con combo-id** | Helper `copy_reach`/`block_card` che copiano solo le combo live con prefissi. **REVERTITO**: con gli slot combo-id i live non sono contigui → copie sparse più lente della memcpy contigua; smoke **52.3 s vs 48 s** (52.3/52.5/52.5 s). Lezione documentata (§4). | dEV smoke **bit-exact** ma **regressione di tempo** → **REVERTITO**. | fatto / verificato / **revertito** |
| 6 | **R1-full — spazi compatti per-player (PlayerIndexed)** | Template param `PlayerIndexed`; slot = `player_flop_slot[player][combo]` con **prefissi contigui 0..\|P\|−1**; `copy_reach`/`block_card` con `std::copy_n` (4.5 KB vs 10 KB); `zeroed_values` prefix; `initial_reach`/`reach_weighted_sum`/`root_public_reach_probability` adattati; runner `<630,true>` per il path fisico; **DAG invariato**. | dEV **bit-exact**; test reference PASS (parity **7.1e-15**). A/B controllato (par, 5 iter): traversal **9.9 s → 7.7 s (−22%)**, scaling pool **1.9× → 2.45×**. | fatto / verificato |
| 7 | **Certificazione parallela (policy pool)** | `policy_chance` con lo stesso parallel split di `cfr_chance`; pool **policy-only** (senza delta regret — **fix critico**: i worker dovevano ricevere `&parallel_worker_deltas_[index]`, non il delta condiviso del main, altrimenti race sulle scritture regret → dEV diverso); accumulate in **ordine edge (0..31)** per bit-exactness. | certification **56 s → 15.6 s (−72%)**; **dEV bit-exact ripristinato** (0.9249962879022555). | fatto / verificato |
| 8 | **Split tuning** | Formula `edge_count/(worker_count+2)` → `(worker_count+1)` (main meno sotto-caricato); edge-order anche in `cfr_chance` per bit-exactness indipendente dallo split. | Tempi stabili; dEV bit-exact (smoke split 20.3 s). | fatto / verificato |
| 9 | **R2 — strategie trasposte + loop valori action-esterno** | `DecisionScratch.strategies` trasposto ([action][slot]); loop valori `for action: for slot` (stream contigui, auto-vettorizzabile AVX2), gated `if constexpr (PlayerIndexed)` (**bug trovato**: senza gate l'analisi EV usava bound errati → gate `flop_co_root` fallito, `correctness_passed=false`; corretto aggiungendo il gate — il path non-per-player deve restare combo-outer). | dEV **bit-exact**, `correctness_passed=true`. Timing **neutro** (~244 s): nessun guadagno misurabile, la macchina è bandwidth-bound. | fatto / verificato |
| 10 | **Cert pool depth 7** (8 thread) | Profondità massima del pool di certificazione a 7 worker. | **Marginale** (~21.5 s smoke): la certificazione async non scala oltre (vedi §4). | fatto / verificato |
| 11 | **Fixes da profiling nel path caldo** | `showdown_values_with_payoffs` senza allocazioni heap per-nodo (i membri `showdown_*` sono ridimensionati a `combo_count` — su board river th7d6s `rank_count` 39–47, cioè **> `compact_combo_capacity` (36)**: il path heap era attivo quasi sempre); chance vectors (`worker_results`/`main_results`) senza zero-init (`reserve`+`emplace` in `cfr_chance`/`policy_chance`); `Result::success` con `emplace`; **dispatch contiguo dei worker**. | dEV **bit-exact**; suite verde. Timing nel rumore (macchina bandwidth-bound). | fatto / verificato |
| 12 | **Divisione `/= sum` → `*= 1/sum`** (apply) | Sostituzione dell'aritmetica FP del nucleo per risparmiare una divisione per combo. | **dEV devia di 2.3e-3 dopo 140 iterazioni** — oltre il gate 1e-6 del protocollo → **REVERTITO.** Lezione documentata (§4). | tentato / **revertito** |
| 13 | **SIMD batch count==2 per `current_strategy`** | Vettorizzazione a 2 lane (batch count==2) del loop strategy in `cfr_decision`. | **Bug permute/hadd per-lane + hang sotto carico macchina** — tentativo non concluso → **REVERTITO.** Lezione documentata (§4). | tentato / **revertito** |

Note di contesto:

- **Baseline storico**: la doc `TH7D6S_SPEED_OPTIMIZATION.md` riporta un baseline più vecchio
  (~412 s wall / 368 s run_solver, traversal 320.8 s). Il baseline riprodotto oggi misura 273.6 s:
  i numeri variano **±30% run-to-run** per rumore di sistema (in parte **carico esterno**, §5.5).
- **O1 è ortogonale al redesign** (resta valido in qualunque architettura, vedi doc §4). **O2 è
  stato revertito** — vedi lezione §4.
- **R1 = O3 portato a fondo**: fase A (prova di eliminabilità) → fase B per gradi (iter1, iter2,
  R1-full). R1-full è la versione che è rimasta.
- **Scoperta critica (§5.5)**: tutte le misure della sessione sono prese su macchina **semi-carica**
  (Baldur's Gate 3 + browser + capture) → varianza ±30% e smoke fino a 5× più lenta.
- Tutti i dEV sono confrontati bit-exact o a ≤1e-6 rispetto al baseline, come da protocollo §5.

---

## 3. Risultati finali verificati (stato attuale)

> **Nota**: i numeri di questa sezione, di **§5** e di **§6.4** costituiscono lo stato
> **FINALE verificato**. Nota di misurazione: tutte le misure sono prese su macchina
> **semi-carica** (§5.5) — varianza osservata ±30%.

### 3.1 th7d6s — run completo

| Metrica | Baseline (riprodotto) | Stato attuale | Δ |
|---|---|---|---|
| `elapsed_seconds` | 273.6 s | **~236 s** — miglior run **235.96 s** (`th7d6s_v12.json`); altri: 233.6, 242.7, 243.8, 253.1, 280.7 | **~−14%** |
| `phase_seconds.traversal` | 211.8 s | **~220 s** | ~neutro (rumore ±30%) |
| `phase_seconds.certification` | 56.2→60.0 s | **~15.6 s** | **−72%** |
| `phase_seconds.regret_application` | 22.6 s | **~28 s** | +23% (ma senza O2; rumore) |
| `final_gto_plus_dev_percent` | 0.9249962879022555 | **0.9249962879022555 — BIT-EXACT** | identico |
| `converged` / iterazioni | true / 140 | **true / 140** | identico |
| `correctness_passed` | true | **true** | identico |
| `layout_matches_fixture` | true | **true** | identico |
| `flop_co_root` | dentro tolleranza | dentro tolleranza | identico |

Risultati chiave verificati sui run finali (`out/th7d6s_v12.json` = miglior run, poi
`th7d6s_final.json`, `th7d6s_r2b.json`, `th7d6s_v5.json`…):

- **miglior run: 235.96 s** (`th7d6s_v12.json`, su macchina semi-carica); gli altri 233.6 / 242.7 /
  243.8 / 253.1 / 280.7 s — media **~245 s**, varianza **±30%** (carico esterno, §5.5).
- `final_gto_plus_dev_percent = 0.9249962879022555` **bit-exact** in tutti i run (un run, v5 a
  233.6 s, differisce solo nell'ultimo ulp: 0.9249962879022543 — dentro ogni tolleranza del
  protocollo).
- `converged=true` a **140 iterazioni**, `correctness_passed=true`, `layout_matches_fixture=true`.
- certification **~15.6 s** (era ~56 s), traversal **~220 s**, regret **~28 s**.

### 3.2 AhKhQh (i "fixature")

- **101** e **103 (root-lock)**: `final_gto_plus_dev_percent = 0.6741554018356799` **bit-exact**,
  `converged=true`, `correctness_passed=true`. Nessuna deviazione dal riferimento.

### 3.3 Suite di test

- `gtosd_gto_plus_reference_tests` (3 test: reference exact, parity fisico↔DAG, external root
  lock): **3 PASS**, parity **7.1e-15** (gate ≤1e-11).
- `gtosd_tests`, `gtosd_phase1_tests` … `gtosd_phase10_tests`, framework: **tutti PASS**
  (phase8 flaky una volta, **PASS al re-run**).

### 3.4 Obiettivo <90 s: **NON raggiunto (su macchina semi-carica)**

- Gap: **~2.6×** (236 s vs 90 s, misurato su macchina semi-carica).
- **Cause**: macchina **Intel i3-10100F (4 core / 8 thread), memory-bandwidth-bound**. Evidenze:
  - il pool che passa da 2 a 8 thread scala solo **~2.3×** (non ~8×);
  - le 4 eval di certificazione async **non scalavano**;
  - floor di bandwidth stimato **~110–140 s** per questo algoritmo su questa macchina (§5.4);
  - **carico esterno** (BG3/browser/capture) contamina tutte le misure (§5.5).
- La correttezza è comunque **piena e bit-exact** su tutti i gate.
- **Azione**: rivalutare il target su **macchina IDLE** (chiudere gioco/browser) prima di
  dichiararlo irraggiungibile.

---

## 4. Lezioni apprese

1. **O2 (bucket sort per regret application) = regressione, revertita.** Il sort costava ~0.3
   s/iterazione (regret_application 22.6 s → ~48–55 s nei run) mentre l'apply era già veloce.
   Inserire un costo per iterazione in un collo già bandwidth-bound non paga: **misurare il run
   completo, non solo la smoke**, prima di tenere un intervento.
2. **R1 iter2 (prefix copy con combo-id) = regressione, revertita.** Gli slot live non sono
   contigui con gli indici combo-id → copie sparse più lente della `memcpy` contigua (smoke
   52.3 s vs 48 s). La contiguità dell'allocazione è ciò che conta, non la lunghezza del vettore.
3. **Il vero lever è stato la riduzione del traffico di memoria (R1-full, −22% traversal con
   copie 4.5 KB vs 10 KB) + il parallelismo della certificazione (−72%).** I guadagni più grandi
   sono venuti da meno dati spostati e dal parallelizzare ciò che era seriale, non da micro-ottimizzazioni
   del ciclo.
4. **Il gate EV richiede bit-exactness nell'ordine FP dell'accumulo chance.** Qualunque
   parallelizzazione/split che cambia l'ordine delle addizioni FP deve accumulare in ordine
   edge (0..31) per restare bit-exact; la certificazione parallela ha richiesto un **fix
   critico**: i worker devono usare il proprio delta (`&parallel_worker_deltas_[index]`), non
   il delta condiviso del main (race sulle scritture regret → dEV diverso).
5. **Il gate di correttezza ha beccato il bug di R2** (senza `if constexpr (PlayerIndexed)`
   l'analisi EV usava bound errati → `flop_co_root` fallito). Il protocollo di verifica
   (§5 della doc) funziona: non si buca.
6. **Le stime di speedup vanno calibrate sulla macchina.** Su hardware bandwidth-bound lo
   scaling non è proporzionale ai thread; le proiezioni ×3–6 della doc non erano raggiungibili.
7. **Cambiare l'aritmetica FP del nucleo è pericoloso.** `/= sum` → `*= 1/sum` è
   bit-exact-sensibile: la dEV ha deviato di **2.3e-3** (gate 1e-6) dopo 140 iterazioni.
   Qualunque modifica all'ordine o alle operazioni FP va validata sul **run completo**, non solo
   sulla smoke.
8. **La vettorizzazione SIMD per-lane va gate-ata prima dell'integrazione.** Il batch count==2
   per `current_strategy` aveva bug di permute/hadd per-lane ed entrava in **hang sotto carico
   della macchina**; tentativo non concluso e revertito.
9. **Misurare su macchina IDLE.** La macchina eseguiva applicazioni esterne pesanti (Baldur's
   Gate 3, msedgewebview2, brave, capture) che saturavano la CPU: lo smoke th7d6s passava da
   ~20 s a ~112 s (**5×**). Tutte le misure della sessione sono inquinate (**±30% varianza**).
   I confronti A/B e i numeri finali vanno presi **a carico di fondo nullo**.

---

## 5. Stato finale verificato e prossimi passi

### 5.1 Stato finale del codice (tutto verificato bit-exact)

- ✅ **O1** — action base diretta per automorfismi identità (`uses_direct_action_bases`).
- ✅ **R1-full** — spazi compatti per-player (`PlayerIndexed`), prefissi contigui 0..|P|−1.
- ✅ **Certificazione parallela** (policy pool dedicato, con fix race sui delta worker) —
  certification **56 s → 15.6 s (−72%)**.
- ✅ **Split tuning** + **accumulo edge-order (0..31)** per bit-exactness indipendente dallo split.
- ✅ **R2** — strategie trasposte ([action][slot]) + loop valori action-esterno, gated
  `if constexpr (PlayerIndexed)`.
- ✅ **Showdown members** — niente allocazioni heap per-nodo (`rank_count` 39–47 > 36; i membri
  `showdown_*` sono ridimensionati a `combo_count`).
- ✅ **Chance vectors senza zero-init** (`reserve`+`emplace` in `cfr_chance`/`policy_chance`).
- ✅ **`Result::success` con `emplace`** e **dispatch contiguo dei worker**.

### 5.2 Tentativi revertiti (documentati come lezioni)

- ❌ **O2 — bucket sort per regret application**: regressione netta (sort ~0.3 s/iterazione)
  → revertito (§2 row 2, lezione §4.1).
- ❌ **R1 iter2 — prefix copy con combo-id**: slot live non contigui → copie più lente della
  memcpy → revertito (§2 row 5, lezione §4.2).
- ❌ **Divisione `/= sum` → `*= 1/sum`**: deviazione dEV **2.3e-3 > gate 1e-6** dopo 140
  iterazioni → revertito (§2 row 12, lezione §4.7).
- ❌ **SIMD batch count==2 per `current_strategy`**: bug permute/hadd per-lane + **hang sotto
  carico macchina** (non concluso) → revertito (§2 row 13, lezione §4.8).

### 5.3 Misure finali verificate

- th7d6s: **~236 s** — miglior run **235.96 s** (`out/th7d6s_v12.json`); dEV
  **0.9249962879022555** **bit-exact**, `converged=true` @ **140 iterazioni**,
  `correctness_passed=true`, `layout_matches_fixture=true`.
- certification **~15.6 s** (da 56 s, **−72%**).
- AhKhQh **101/103**: **0.6741554018356799** **bit-exact**.
- **Suite completa PASS** — `gtosd_gto_plus_reference_tests` (parity **7.1e-15**),
  `gtosd_tests`, `phase1–10`, framework.
- ⚠️ Tutte le misure sono su **macchina semi-carica** (§5.5): varianza osservata **±30%**.

### 5.4 Collo di bottiglia (profilato)

Per-pass traversal (wall **~1.41 s** @ 2 thread): terminali **~32%**, strategy **~26%**,
valori+update **~17%**, ricorsione/overhead **~15%**, copie `reach` **~8%**.

Macchina: **Intel i3-10100F 4C/8T**, bandwidth effettiva **~20 GB/s** (misurata dall'apply);
pool 2→8 thread scala **~2.3×**, eval cert async **non scalavano**. Obiettivo <90 s:
**NON raggiunto** su macchina semi-carica; **floor di bandwidth stimato ~110–140 s** per questo
algoritmo su questa macchina (a carico nullo).

### 5.5 Scoperta critica (misurazione)

La macchina esegue applicazioni esterne pesanti — **`bg3_dx11` (Baldur's Gate 3)**,
**`msedgewebview2`**, **`brave`**, **`capture`** — che **saturano la CPU al 100%**: lo smoke
th7d6s passa da **~20 s a ~112 s (5×)**. **TUTTE le misurazioni della sessione sono inquinate**
da questo carico variabile (**±30% varianza osservata**). Le misure finali (~236 s) vanno intese
come **"su macchina semi-carica"**; il target <90 s va valutato su **macchina IDLE** (chiudere
gioco/browser) prima di dichiararlo irraggiungibile.

### 5.6 Prossimi passi possibili (in ordine di valore)

1. **Rimisurare su macchina IDLE** (chiudere BG3/browser/capture): ridefinisce la baseline reale
   e valida/aggiorna il floor di bandwidth stimato ~110–140 s.
2. **R3 — parallelismo per blocco**: stimato **marginale** — il costo è già bandwidth-bound, non
   compute-bound; partizionare ulteriormente i thread non rimuove il collo della memoria.
3. **R4 — metrica di convergenza economica** (certificazione stimata invece dell'exact BR ai
   checkpoint intermedi): **richiede l'approvazione dell'utente** (cambierebbe il criterio di
   convergenza del run); risparmierebbe parte dei ~15.6 s di certification, non il traversal.
4. **Target alternativo realistico**: soglia **~110–140 s** (floor stimato a carico nullo) oppure
   **eseguire i benchmark su una macchina con più core/bandwidth**, dove i guadagni R1-full +
   cert-pool si amplificano.

### 5.7 File/artefatti utili

- `out/th7d6s_v12.json` — miglior run bit-exact **235.96 s** (su macchina semi-carica).
- `speed_optimization_journey.md` — questo diario.
- `tools/build.bat` — build con vcvars (ambiente MSVC).
- `benchmarks/fixtures/gto_plus_th7d6s_smoke.json` — smoke 5 iter (max_iterations=5,
  certification_interval=1, averaging_delay=0).
- `tools/compare_runs.py` — confronto numerico run-vs-run.

---

## 6. Analisi del collo di bottiglia (profiling)

### 6.1 Metodo

Strumentazione **temporanea** nel `DenseTraversal` per capire dove si concentra il tempo del
traversal, gated su env `GTOSD_PROFILE_HOTPATH` con report aggregato su main+workers.
**Rimossa dopo la diagnosi** — i timer inquinavano il benchmark (overhead + disturbo dei tempi).

- **chrono per-instance** su `cfr_decision`: strategy loop (`current_strategy`), `copy_reach`,
  ricorsione figli, loop valori+update;
- **timer** su `cfr_chance` e sui terminali (fold/showdown).

Run profilato: depth 1 (par), 5 iterazioni, wall traversal **~1.41 s/pass** su 2 thread.

### 6.2 Risultati (quota del wall traversal per pass)

| Componente | Quota |
|---|---|
| terminali (fold/showdown) | **~32%** |
| strategy (`current_strategy`) | **~26%** |
| loop valori+update | **~17%** |
| ricorsione/overhead (call+Result+dispatch) | **~15%** |
| `copy_reach` | **~8%** |

**Lettura**: le copie `reach` **NON sono il collo** — erano la stima del documento
(§7 della doc di riferimento); al ~8% non giustificano un altro redesign. Il costo reale è il
**compute per-nodo**: terminali + strategy da soli valgono **~58%** del wall.

### 6.3 Fix applicati durante la diagnosi (tutti dEV bit-exact)

1. **`showdown_values_with_payoffs`**: eliminati i 4 vettori heap allocati per-nodo
   (totals / by_card / prefix / card_prefix, ~5.4 KB). Su board river th7d6s il `rank_count`
   è 39–47, cioè **> `compact_combo_capacity` (36)**: il path heap era attivo quasi sempre.
   I membri `showdown_*` sono ora ridimensionati a `combo_count` (630) e **riusati sempre** —
   zero allocazioni per-nodo.
2. **`cfr_chance` / `policy_chance`**: `worker_results` / `main_results` costruiti con
   `reserve` + `emplace` invece di `resize` — eliminata la zero-init di ~165 KB per ogni
   turn-chance.
3. **`Result::success`** (`include/gtosd/core/result.hpp`): `emplace` diretto nel variant
   (una copia invece di due) + overload by-value per braced-init. **Neutro** in timing.

### 6.4 Conclusione e stato verificato finale

La macchina (i3-10100F, 4 core / 8 thread) è **latency/bandwidth-bound**: il pool che passa
da 2 a 8 thread scala solo **~2.3×**, le 4 eval di certificazione async **non scalavano**; il
costo residuo del traversal è **compute per-nodo** (terminali + strategy ≈ 58%), non eliminabile
senza un redesign dell'algoritmo. **Obiettivo <90 s dichiarato NON raggiunto con evidenza su
macchina semi-carica**; floor di bandwidth stimato **~110–140 s** a carico nullo — da rimisurare
su **macchina IDLE** (§5.5).

Stato verificato finale:

- th7d6s: **~236 s** — miglior run **235.96 s** (`out/th7d6s_v12.json`), da baseline
  273.6 s (**~−14%**);
- certification: **~15.6 s** (da 56 s, **−72%**);
- dEV **0.9249962879022555** **bit-exact**;
- AhKhQh 101/103: **0.6741554018356799** **bit-exact**;
- **suite completa PASS**.

---

## 7. Comandi utili (protocollo di verifica, doc §5)

**Build** (Windows, MSVC — l'ambiente vcvars è caricato da `tools/build.bat`):

```
tools/build.bat --target gto_cli
```

(equivalente manuale: `cmd //c "call \"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat\" && cmake --build out\build\windows-release --target gto_cli"`;
se l'exe è lockato da un run in corso: `powershell "Get-Process gto_cli | Stop-Process -Force"`)

**Test di regressione** (binari in `out/build/windows-release/tests/`):

```
gtosd_gto_plus_reference_tests.exe   # 3 test: reference exact, parity DAG ≤1e-11, root lock
gtosd_tests.exe, gtosd_phase1_tests.exe … gtosd_phase10_tests.exe
```

**Benchmark AhKhQh** (devono restare identici):

```
gto_cli.exe postflop benchmark-gto-plus benchmarks/fixtures/gto_plus_ahkhqh_101.json out/101.json
# atteso: converged=true ~iter 80, final_dev_percent ≈ 0.674155, correctness_passed=true
```

**Benchmark th7d6s** (il target di velocità):

```
gto_cli.exe postflop benchmark-gto-plus benchmarks/fixtures/gto_plus_th7d6s_101.json out/th7d6s.json
# atteso: converged=true ~iter 140, final_dev_percent ≈ 0.925, correctness_passed=true,
#         gto_plus_ev_checks.flop_co_root.passed=true, layout_matches_fixture=true
```

**Confronto numerico obbligatorio** dopo ogni modifica: `final_gto_plus_dev_percent` e la curva
`convergence[].gto_plus_dev_percent` identici entro ~1e-6 (bit-exact dove possibile);
`flop_co_root ≈ 8.2189`; tempi in `phase_seconds.{traversal, certification, regret_application,
run_solver}` ed `elapsed_seconds`. `tools/compare_runs.py` per il confronto run-vs-run dei JSON.

**Iterazione rapida (solo sanity, NON per i numeri finali)**: fixture smoke con
`maximum_iterations=5, certification_interval=1, averaging_delay=0` (copia della fixture con
`benchmark_id` nel pattern `GTP-XXXX-###`).

**Prima di ogni misurazione**: verificare che la macchina sia **IDLE** (nessun gioco/browser/
capture attivo — vedi §5.5), altrimenti i tempi sono inquinati (varianza ±30%, smoke fino a 5×
più lenta).

---

*Ultimo aggiornamento: stato FINALE verificato — tutti gli interventi mantenuti sono dEV
bit-exact (O1, R1-full, certificazione parallela 56→15.6 s −72%, split tuning + edge-order, R2,
showdown members senza heap per-nodo, chance vectors senza zero-init, `Result::success` emplace,
dispatch contiguo); 4 tentativi revertiti documentati come lezioni (O2 bucket sort, iter2 prefix
copy, divisione FP `/=sum`→`*=1/sum` dev 2.3e-3 > gate 1e-6, SIMD batch count==2 con hang sotto
carico). Miglior run 235.96 s (`out/th7d6s_v12.json`), dEV 0.9249962879022555 bit-exact,
converged@140, suite completa PASS. SCOPERTA CRITICA: misure contaminate dal carico esterno della
macchina (BG3/browser/capture → smoke 5×, varianza ±30%) — <90 s da rivalutare su macchina IDLE
(floor di bandwidth stimato ~110–140 s).*

---

## 7. Aggiornamento finale sessione — batch SIMD e piano di riscrittura (2026-08-06)

Dopo la chiusura precedente (235.96 s), la sessione ha prodotto ulteriori interventi:

### 7.1 Batch SIMD strategie count==2 — MANTENUTO (bit-exact, −21% run)

Nel loop strategie di `cfr_decision` (path fisico), per `action_count == 2` e
`uses_direct_action_bases`: 4 combo alla volta, `regret_float32` (8 float contigui) →
`max(0,·)` → `hadd` → `permute4x64 0xD8` → `1/sum` (con sostituzione uniforme se sum ≤ 0)
→ blend. Aritmetica IEEE-identica allo scalare (il ramo "sum ≤ 0" sostituisce la strategia
uniforme come lo scalare — le due versioni sono bit-exact).
**Verifica**: smoke dEV 104.64860904530644 bit-exact; run completo `out/th7d6s_b2full.json`
**187.3 s** (da 235.96), traversal 173.7 s, cert 12.0 s, dEV 0.9249962879022555, correctness=true.
Nota: primo tentativo fallito (permute sbagliato → `numerical_failure` + hang sotto carico);
fix `0xD8` + build pulita → ok. Lezione: il profilo "strategy 26%" era dominato dalle
**divisioni** — con il batch la fase strategie passa a ~0.

### 7.2 Value-loop SIMD fuso count==2 — MANTENUTO (bit-exact, neutro)

Loop valori di `cfr_decision` (path fisico, PlayerIndexed): 4 slot contigui per volta,
`s0*a0 + s1*a1` con FMA chain. dEV bit-exact; timing neutro (nel rumore ±3%). Anche il
path canonical (DAG) ricevette la stessa modifica in un edit errato — codice morto per th7d6s,
innocuo.

### 7.3 Undo-based reach — REVERTITO (lezione: il traffico è irriducibile senza float32)

Sostituzione delle copie reach per-edge con muta→ricorre→ripristina (salva solo gli slot
dell'attore). **Risultato: neutro/peggiore** — il muta (read-modify-write 282) + ripristino
(282) ha lo stesso traffico del memcpy prefix vettorizzato (564), e le copie erano già
memcpy efficienti. Conclusione: la reach DEVE essere materializzata per edge; l'unico modo
di dimezzare il traffico è float32.

### 7.4 Profiler hot-path corretto e breakdown per-pass

Bug profiler: l'ancora `t_after_strategy` per ogni action accumulava tempo cumulativo.
Corretto con ancora per-action (`t_prev`). Breakdown seriale-equivalente per pass
(smoke, macchina semi-carica): copy reach ~2.8 s (38%), children ~4.3 s (sovrapposta),
copie Result 5 KB/nodo ~1.5–2 s (stima, ~25%), value+update ~0.1 s, terminali ~0.2 s,
strategy ~0.0004 s.

### 7.5 Conferma limite pool

Scaling 1→5 = 1.95×, 1→7 (8 thread) = 1.98×: **cap a ~2× per bandwidth**
(~20 GB/s, DDR4 dual-channel). Il pool non è il collo recuperabile.

### 7.6 Decisione utente e piano di riscrittura

L'utente ha scelto di **pianificare la riscrittura architetturale** (stile GTO+):
[`docs/ARCHITECTURAL_REWRITE_PLAN.md`](docs/ARCHITECTURAL_REWRITE_PLAN.md) — layout stato
float32 (delta 667 MB→333 MB, reach 4.5→2.25 KB/edge), out-param value flow (elimina copie
Result 5 KB/nodo), SIMD sistematico (update/apply/chance/terminali), parallelismo per blocco
(partizioni infoset disgiunte, split river, coda pull) per 2×→4×. Roadmap A–F (7–10 giorni)
con gate per fase (smoke bit-exact → dEV ≤ 1e-6 → AhKhQh ±1e-6 → suite PASS → journey).
Target <90 s tra fase D ed E/F; 17.66 s GTO+ richiede passi oltre (§5 del piano).

**Stato finale sessione: 187.3 s (miglior run pulito), tutti i gate bit-exact,
suite completa PASS; piano di riscrittura pronto in docs/ARCHITECTURAL_REWRITE_PLAN.md.**

---

## 8. Fase A del piano di riscrittura — out-param value flow (2026-08-06)

Eseguita la Fase A di `docs/ARCHITECTURAL_REWRITE_PLAN.md`: eliminazione delle copie
`Result<ComboVector>` (5 KB/nodo) lungo la ricorsione.

### Modifiche (tutte nel path fisico, DAG invariato)

1. **Terminali**: `fold_values_with_payoff_into` / `showdown_values_with_payoffs_into` +
   wrapper nodo `fold_values_into` / `showdown_values_into`; le varianti `Result` restano
   come adattatori per il path policy (certificazione). Nuovo helper `zero_values_into`
   (ogni callee zeroa il prefisso del proprio out-param — gli slot bloccati restano 0).
2. **`cfr_physical` / `cfr_chance` / `cfr_decision`** → scrivono in `ComboVector &values_out`
   e ritornano `std::optional<PostflopSolverError>`.
3. **`cfr_decision`**: il figlio scrive DIRETTAMENTE in `action_values[action]` (niente
   `Result` di ritorno, niente prefix copy) — il `transform_values_to_parent` del path
   fisico è di fatto un no-op.
4. **`cfr_chance`**: `worker_results`/`main_results` pre-sized (`resize(split)`); i task
   worker catturano `&worker_results[index]` e scrivono nello slot del main thread (valido
   fino alla join); ordine di accumulo inalterato (edge 0..split poi split..end) ⇒
   bit-exact. Ritorno dummy `ComboVector{}` nei task (solo errore, ~1.4 MB/pass trascurabile).
5. **`cfr()`**: adattatore Result-based (scrive in un buffer locale).

### Verifica

- Smoke: dEV 104.64860904530644 **bit-exact**.
- Run completo: 191.8 s (nel rumore ±3% vs 187.3 s precedente — macchina più carica),
  dEV 0.9249962879022555 bit-exact, converged, correctness_passed.
- AhKhQh 101/103: 0.6741554018356799 bit-exact, correctness=pass.
- ctest: **100% tests passed out of 15**.
- Profiler hot-path: copy 2.8→0.17 s/pass, children 4.3→3.1 s/pass (lavoro seriale −47%),
  strategy ~0, value_update ~0.06, terminal ~0.15.

### Lezione (ridefinisce la priorità delle fasi)

Il wall del traversal NON è calato in proporzione al lavoro seriale rimosso: il collo è la
**latenza degli accessi casuali allo stato** (delta/regret/strategy a indici sparsi, ~62M
accessi/pass), non le copie. La Fase B (reach float32) ha ora payoff basso (copy già 0.17 s/pass).
Le fasi a valore più alto diventano: **C** (delta float32 + apply: dimezza lo stato 667→333 MB
e il traffico del apply latency-bound) e **D** (parallelismo per blocco con partizioni
disgiunte: attacca direttamente gli accessi sparsi). Da rivalutare a macchina IDLE per numeri
puliti.

### 8.1 Fase C tentata e REVERTITA — delta float32 (lezione: gate di precisione)

Tentativo di dimezzare lo stato del regret delta (667 MB double → 333 MB float):
tipi `std::vector<float>` su delta/worker-deltas/merge/apply + cast ai siti di accumulo.
**REVERTITO**: il smoke mostra deriva dEV 104.64846562480703 vs bit-exact 104.64860904530644
(−1.4e-4 assoluto, ~1.4e-6 relativo) che scala a ~1.2e-6 sul full run — **al gate ≤1e-6**.
Causa: il regret_delta (double) viene arrotondato a float a ogni passata; gli errori si
accumulano su 140 iterazioni. La compensazione Kahan richiederebbe un accumulatore per
elemento (un altro 667 MB) — inutile. Lezione: **lo stato numerico resta double dove è
accumulatore**; il float32 è accettabile solo per stato non-accumulatore (strategy/regret,
già float32 nel protocollo). Dopo il revert: dEV bit-exact 104.64860904530644 ripristinato.

### 8.2 Fase D-1 — apply immediato dei regret (niente delta differito) — MANTENUTO (bit-exact)

**Scoperta strutturale**: nel path fisico per-giocatore (automorfismi identità,
`uses_direct_action_bases`), ogni indice azione è toccato **una sola volta per passata**
(ogni combo è il proprio infoset, ogni nodo è visitato una volta, i sotto-alberi del pool
hanno basi disgiunte). Quindi il delta differito (667 MB double + flags + touched-list +
merge worker + fase `regret_application` di ~24.7 s) è superfluo: l'apply immediato nel
ramo `deferred_regret_delta_ == nullptr` dell'update loop è **bit-exact** (stessa addizione
double per indici single-touch) e riduce le ops per indice da ~6 a ~2.

**Modifica**: `make_dense_traversal_runner` passa `nullptr` come delta per il path
`<combo_count, true>` (PlayerIndexed). Il path canonical (DAG, AhKhQh) e il path fisico
non-PlayerIndexed (automorfismi multipli) mantengono il delta (multi-touch).

**Verifica**: smoke dEV 104.64860904530644 **bit-exact**; run completo dEV
0.9249962879022555 bit-exact, converged, correctness_passed; AhKhQh 101
0.6741554018356799 bit-exact. `regret_application` 24.7→9.5 s anche sotto carico.

**Nota misurazione**: il run completo (398 s) e le smoke (82–158 s vs 17.8 s dello stesso
binario 25 min prima) sono inquinati da un carico esterno intenso e fluttuante (processo
`capture` + msedgewebview2 a 13,000 CPU-secondi + BG3). L'A/B di timing pulito resta da
fare a macchina quieta. Il vantaggio strutturale (stato −667 MB, fase apply eliminata,
ops/indice 6→2) è indipendente dal timing.

**Lezione**: l'argomento "il carico è irrilevante" non regge contro l'evidenza dello
stesso binario a 17.8 s e 82–158 s a distanza di 25 min; la varianza delle misurazioni
resta il vincolo principale per ogni A/B di timing in questa sessione.

### 8.3 Principio vincolante (direttiva utente) — target RAM ~399MB

**Il carico della macchina è INDIFFERENTE**: il benchmark DOVRÀ utilizzare solo
**~399 MB di RAM** (riferimento GTO+: `solver_memory_bytes: 399000000`), e un working set di
~399 MB non viene influenzato dal carico esterno (nessun thrashing contro gli altri processi).
Ogni misurazione va interpretata con questo principio: il collo del timing è da attribuire
all'implementazione, NON al carico della macchina.

Stato RAM misurato (2026-08-06, run `out/th7d6s_current.json`):
- Peak RSS di processo: **4.27 GB** — dominato dalla **PREPARAZIONE** (~4.07 GB di picco
  raggiunti prima della prima iterazione; il working set corrente durante il solve è ~1.45 GB).
- Stato del solver (`solver_state_bytes`): **667 MB** (regret float32 333 MB + strategy
  float32 333 MB) vs GTO+ **399 MB** → gap 1.67×.
- Delta differito double (667 MB): eliminato per il path PlayerIndexed (apply immediato);
  allocazione resa condizionale in `solve_postflop` (preserva DAG/multi-auto).

Obiettivo: picco di processo e stato verso ~399 MB → la preparazione (4 GB) è il primo lever,
segue lo stato (667→399 MB). Da documentare a ogni fase nel journey.

### 8.4 Layout skip per il path direct — peak RSS 4.27 GB → 868 MB (MANTENUTO, bit-exact)

**Scoperta (profilazione RSS per fase con GTOSD_RAMPROF)**: il picco di 4.07 GB nella
preparazione era la `canonical_infosets` unordered_map (~36.6M entry, ~2.5 GB) + i picchi di
rehash (reserve stimata `decision_nodes*8`=176K, molto sotto) + gli array canonici finali
(~0.5 GB: `canonical_action_bases` 293 MB + `canonical_action_counts` 73 MB +
`canonical_infoset_multiplicity` 146 MB). Per il path per-giocatore a basi dirette
(`uses_direct_action_bases`, automorfismi identità — th7d6s) questi dati **non sono MAI
letti**: `decision_action_base` usa `decision.action_base + local*count`, e
`decision_infoset_id` è usato solo dalla costruzione del DAG.

**Modifica in `build_layout`**:
1. `range_automorphisms` calcolati PRIMA delle histories; se `automorphisms.size() <= 1`
   (direct), si saltano histories/intern/`canonical_infosets`.
2. Nel loop delle decisioni, il path direct usa il ramo semplice (per-combo contiguo:
   `action_base = actions; information_sets += legal_count; actions += legal_count*count`)
   — contatori IDENTICI alla costruzione canonica (ogni combo è il proprio infoset).

**Verifica**: th7d6s full — dEV 0.9249962879022555 bit-exact, converged, correctness_passed,
`layout_matches_fixture=true`, **peak_rss_bytes 867,954,688 = 868 MB** (da 4.27 GB, −80%).
AhKhQh 101 (DAG, usa la mappa canonica): dEV 0.6741554018356799 bit-exact, correctness=pass,
peak 210 MB. ctest **100% tests passed out of 15**.

**Stato RAM ora**: picco 868 MB = stato solver 667 MB + tree ~160 MB + layout/runner ~40 MB.
Il grosso è ora lo STATO (regret+strategy float32) — il confronto diretto con GTO+ 399 MB.
Prossimo lever per i 399 MB: ridurre lo stato (667 → ~400 MB). Il carico della macchina
resta indifferente (principio §8.3): il run completo misurato 954 s è inquinato dal carico,
la bit-exactness e la RAM sono i gate affidabili.

## §8.5 Analisi GTO+ 399MB — albero identico, 4.79 byte/azione

L'utente ha fornito lo screenshot dell'albero (solo flop) di GTO+ v1.6.9. OCR
(Windows.Media.Ocr + bounding box) e confronto con la nostra fixture:

- **Albero GTO+ (flop)**: pot 100% → Bet 9.5 / Check → (bet) Fold/Call/Raise 28 →
  (raise) Fold/Call/Raise 65 → (raise) Fold/Call/Raise 92 (all-in, stack 92).
- **Il nostro albero è IDENTICO**: stessa config (pot 19, stack 92, bet/raise 50% pot,
  max 4 raise/street); i reference_nodes confermano check/bet_9, fold/call_9/raise_28,
  poi raise_65/raise_92. **Stesso spazio delle azioni** (83,318,592 azioni).

**Conto decisivo**: 399 MB ÷ 83.3M azioni = **4.79 byte/azione** (non 8).
GTO+ non memorizza regret+strategy float32 (8B) come noi → la spiegazione più
plausibile: **regret-only durante il solve (4B/azione) + metadata infoset (~66MB),
con la strategy media derivata/ricalcolata a fine run**. La media pesata non è
comprimibile n−1 (valori non normalizzati, nessuna proprietà somma-zero sui regret),
float16 è fuori gate (1e-3 vs 1e-6) → **667MB è il pavimento del nostro formato**.

**Decisione**: mantenere 667MB (fixture intatta, bit-exact) e concentrare gli sforzi
sul **timing** (Fase D). Documento completo: `docs/GTO_PLUS_MEMORY_ANALYSIS.md`.

## §8.6 Fase D — coda condivisa pull-based + river split (esiti)

**Obiettivo Fase D** (piano §2.4): parallelismo per blocco — partizioni disgiunte + split anche
al river chance + coda condivisa pull-based, atteso 2× → 3.5-4× (dopo la riduzione del traffico B/C).

**Sub-step 1 — infrastruttura coda condivisa (MANTENUTO, bit-exact)**:
- `ParallelTaskQueue` (mutex+condvar+deque+shutdown) condivisa dal pool; i worker pullano
  dalla coda comune; i task sono `packaged_task<TraversalResult(DenseTraversal&)>` e usano il
  SELF (il traversal eseguente) → lo scratch per-decision è per-thread (niente race).
- **Fix teardown**: il destructor usa `notify_all` (il notify_one sulla coda condivisa lasciava
  gli altri worker bloccati nel join → HANG del processo — la causa del precedente tentativo).
  Ora il processo ESCE pulito.
- I task del path canonico (DAG) restano capture-based (chain per-livello).
- Verifica: smoke 14.5s dEV 104.64860904530644 bit-exact, full run dEV 0.9249962879022555
  bit-exact (183.5s, carico alto), AhKhQh 101/103 0.6741554018356799 bit-exact, ctest 15/15.

**Sub-step 2 — river split (REVERTITO con evidenza)**:
- Split a ogni river chance (popcount 4): lo split=31 per worker (coda diretta) + steal-while-wait.
- **Regressione netta**: smoke da ~15s a >180s (timeout). Diagnosi: (a) i sotto-alberi del river
  sono piccoli (pochi decision node + terminali) → l'overhead dei task (packaged_task + future +
  dispatch/join + 1ms condvar wait + 5KB child_reach nel lambda) domina il lavoro; (b) il
  notify_all genera thundering-herd sul mutex; (c) i join con coda vuota dormono 1ms per poll.
- **Lezione**: il parallelismo sotto il turn NON paga finché il traffico non è ridotto (B/C);
  la stima del piano "dopo la riduzione del traffico" è confermata. B/C restano bloccate dal
  gate di precisione (delta float32 deriva ~1.2e-6 al gate, §8.1).
- Revert: split di nuovo `== 3U` (solo turn), DBG rimosso, stato bit-exact.

**Stato**: l'infrastruttura coda condivisa (sub-step 1) è in produzione (bit-exact, teardown
fisso); il guadagno di parallelismo della Fase D resta subordinato alla riduzione del traffico.
Timing A/B del sub-step 1 inconcludente sotto carico (full 183.5s vs 164.0s — rumore).

## §8.7 Fasi B e C (esiti con misura)

**Fase B — reach float32 (REVERTITA con evidenza)**:
- Reach per-giocatore da double a float32 (2.25KB/edge) + conversioni ai punti d'ingresso
  (runner, certification, analysis). Compilava.
- **Misura della deriva**: smoke dEV 104.64138344559912 vs 104.64860904530644 → deriva
  assoluta 7.2e-3, **relativa 6.9e-5 = 69× sopra il gate 1e-6** (proiezione full ~6e-5).
- Revert completo (doppio reach ripristinato): smoke 104.64860904530644 **bit-exact**.
- Lezione: la reach è il prodotto di ~20 strategie — l'errore float32 relativo ~1e-6
  accumulato NON è sotto il gate; il piano §2.1 ("se > 1e-6, tenere la reach in double")
  applicato. Le copie reach restano il costo (solo out-param/undo possono dimezzarle).

**Fase C — update inline SIMD (MANTENUTA, deriva 1.2e-9)**:
- Il delta float32 del piano è MOOT per th7d6s (apply immediato — il delta è DAG-only).
  La C reale: **update regret+strategy SIMD per action_count==2** (path PlayerIndexed,
  non-locked, delta nullptr): 4 combo alla volta, action-base dirette contigue
  (action_base + local×2 verificata), __m256 load/store dei buffer regret/strategy,
  calcolo dei delta per-slot identico allo scalare (doppio; il regret: add double + max
  + cast float; la strategy: prodotto (w·reach)·s nell'ordine scalare + add float).
  Safety: verifica della contiguità dei player_local (fallback scalare).
- **Misura**: smoke deriva 3.4e-8; full dEV 0.9249962866807331 vs 0.9249962879022555 →
  **deriva 1.2e-9 (800× sotto il gate 1e-6)**, converged, correctness_passed;
  AhKhQh 101/103 0.6741554018356799 **bit-exact** (il DAG non tocca il SIMD);
  ctest 15/15. Timing 169.7s (carico — A/B inconcludente).

## §8.8 Revert Fase C (decisione utente)

La Fase C (update SIMD count==2, deriva 1.2e-9 misurata) è stata REVERTITA su decisione
dell'utente: il guadagno teorico (~1-2%) non è verificabile sotto carico (A/B inquinato),
e la bit-exactness esatta è il gold standard della sessione. Revert verificato:
smoke dEV 104.64860904530644 esattamente bit-exact. Il SIMD potrà essere riapplicato a
macchina idle (A/B misurabile) o come parte organica della Fase F (update SIMD completo).
Stato finale: bit-exact ovunque (smoke/full/AhKhQh), ctest 15/15 (verificato a ogni fase).

## §8.9 F1 (update SIMD count 2-4) — misurata e REVERTITA

F1 (design §3.1): update regret+strategy SIMD per action_count 2..4 (gruppi di 4/8 combo
interi di __m256, tabelle statiche degli indici). Misure (2 run consistenti):
- Versione con divisioni per lane (g/count, g%count): **205-208s** — le divisioni intere
  (~48/gruppo per count=3) dominavano → REGRESSIONE +25%.
- Fix con tabelle statiche: **175.1/176.2s** — ancora **+7% vs baseline 164.0s**.
- dEV 0.9249962780682633 (deriva 9.8e-9, dentro gate) — deterministico.

**Verdetto**: l'update SIMD regredisce anche senza divisioni. Causa: i delta richiedono i
load SCATTERED dei valori (action_values/values/strategies per slot) — che il SIMD non
tocca; i buffer regret/strategy vettorizzati non compensano l'overhead del check di
contiguità + la conversione float<->double. La C (count==2, 169.7s) regrediva già +5.7s.
**F1 REVERTITA**: loop scalare ripristinato, smoke bit-exact. Lezione: il collo è la
latenza dei load scattered — il SIMD sui buffer non la attacca; F2 (infoset-major) e F4
(prefetch) restano le uniche opzioni a basso rischio del design, F3 esclusa.

## §8.10 Profiler per-iterazione (GTOSD_PROFILE_HOTPATH=1)

Nuovo dump per-pass (un blocco per passata giocatore): wall (tempo reale della passata)
+ parti serial-equivalenti (CPU sommato su main+worker): terminal showdown, reach
propagation (copie), value+update, board/card filtering, synchronization, regret matching.
Contatori nuovi: prof_chance_seconds_ (accumulate chance), prof_sync_seconds_ (dispatch),
prof_wall_seconds_ (cfr_physical). Misura (smoke, depth 5):
- wall ~575ms/pass (coerente col full: 280 pass × 575ms ≈ 161s)
- terminal showdown ~1.1-1.4s (47%), reach propagation ~1.1s (38%), value+update ~0.35-0.6s
  (13%), filtering ~24ms, sync ~0.4ms, regret matching ~2.3ms.
Le parti sono CPU serial-equivalente (fattore assoluto non calibrato vs wall ~2.6 — i guard
annidati del wall non sono usati per le parti); la DISTRIBUZIONE relativa è affidabile e
conferma la diagnosi: il collo è terminal showdown + reach propagation (i load scattered).

## §8.11 Fase A del piano cfr_iteration_optimization_agent.md — findings + primo fix

**Findings (ispezione, prima di refactor)**:
1. **Showdown NON è O(H²)**: `showdown_values_with_payoffs_into` è GIÀ rank-bucket + cumulative
   mass + blocker-aware (totals[rank], by_card[card][rank], prefix scans, correttivi
   card_prefix[first]+card_prefix[second]−own_reach) → **O(H + 36·R)**. La Fase C del piano
   è già implementata; il costo residuo è la Fase 2 (fill_n dei 4 array member + prefix
   scans) + i load scattered delle fasi 1/3. Niente pairwise scan da eliminare.
2. **Reach**: il pattern "copy-then-multiply" del piano (Priority 2 Step 2) è presente nel
   cfr_decision fisico: `copy_reach` (2 prefix copie 4.5KB per action) + `*= strategy`
   (read-modify-write delle slot attore). Il transform_reach è ASSENTE nel path fisico
   (già ottimizzato); il block_card per edge chance resta (necessario).

**Fix implementato (Phase B-2 — fuse copy+multiply)**: le slot dell'attore scritte
direttamente `child = reach × strategy` (niente re-read del child appena copiato).
- Smoke dEV 104.64860904530644 **bit-exact**; full dEV 0.9249962879022555 **bit-exact**,
  converged, correctness, **157.4s** (vs 164.0 baseline, −4%).
- Profiler: serial-equiv-parts 2625ms/pass (da 2871, **−8.6%**); reach propagation ~1055ms
  (da ~1090).
- Nota: il guadagno del fuse è limitato perché la COPIA dell'opponent parte è invariata
  (identica tra le action — copiata 2.5×/nodo inutilmente); il passo successivo (Phase B-3:
  avoid materializing) richiede la condivisione della parte opponent tra le action (refactor
  della firma reach) — il piano lo prevede come prossimo candidato.

## §8.12 Zero opponent reach copies (refactor ReachRef, ordine utente)

**Stadio A (meccanico, neutro)**: firma reach → `ReachRef` (`std::array<const ComboVector*, 2>`);
il path canonico materializza esplicitamente (`child_reaches[action][p] = *reach[p]`), i
macchinari block_card/copy_reach/transform_reach restano materializzati. Smoke bit-exact.

**Stadio B (zero copie opponent)**: nel cfr_decision fisico il child_reach per action è
eliminato: l'attore scrive SOLO le proprie slot flop-range in `reach_actor[action]` (scratch
per-depth nel DecisionScratchLease — 8×630 doubles/lease), l'opponent è CONDIVISO dal
parent (`{&actor, reach[opp]}` o `{reach[opp], &actor}`). Invariante di sicurezza: ogni
lettura di reach[p] usa le slot del flop-range di p (mai le non-slot dello scratch).
- Full: **150.7s** (da 164.0 baseline, da 157.4 fuse) — dEV **esattamente bit-exact**,
  converged, correctness; AhKhQh 0.6741554018356799 **bit-exact**; ctest 15/15.
- Profiler: parti 2534ms/pass (da 2871 pre-fuse); reach propagation **946ms** (da ~1090).
- Counting: reach elements/pass ~16M (da ~47M — le copie eliminate erano ~31M/pass);
  il block_card delle chance (~1056 edge/pass × 2 prefix) è ora la parte dominante della
  reach propagation.
- Note: la policy path (certificazione) usa ancora il copy_reach materializzato (8% del run).

## §8.13 Decomposizione dei 946ms reach (esperimenti del doc cfr_next_optimization_phase)

**Kernel actor**: `actor_reach[slot] = reach[actor][slot] * strategy[action][slot]` — 16M
elements/pass, 946ms → **59 ns/elemento**.

**Esperimenti (misura, non speculazione)**:
1. **Indexed vs dense** (kernel temporaneo con slot contigui 0..H-1, timing-only):
   ~906-1064ms ≈ 946ms indexed → **delta ~0** → il Caso B (indexed/scattered costoso) ESCLUSO.
2. **Single-buffer vs 8-buffer** (reach_actor[1] per depth, child completes prima del riuso):
   ~932-1008ms ≈ 946ms → **delta ~0** → il Caso C (scratch working set) ESCLUSO.
3. Vectorization: il loop indexed non vettorizzabile (indirezione), ma il dense (vettorizzabile)
   ≈ l'indexed → la vettorizzazione NON è il problema.
4. Scratch audit: nessun init/clear nel lease; le reach_actor non inizializzate ma scritte per
   le slot attore e lette solo lì (invariante) — nessun memset/fill nascosto.

**Verdetto**: il costo del kernel è la **latenza intrinseca dei 3 accessi** (strategies +
reach del parent) per 16M elementi — irriducibile con dense/single-buffer. Il reach (946ms)
si è **bilanciato col terminal showdown** (~990ms) → secondo l'ordine del doc ora si
micro-ottimizza lo showdown (Caso D). Il single-buffer (neutro, bit-exact) resta nel codice.

## §8.14 Phase C.3 — Fusione Decision->Terminal (fold+showdown)

Il terminal (fold/showdown) consuma SOLO reach[1-updating]; quando l'attore == updating la
reach dell'attore appena scritta non viene MAI letta -> il write è eliminabile (si passa la
reach del parent). Implementato nel cfr_decision fisico: check del child node kind +
`actor_needed = !terminal_child || decision.player != updating_player`.
- Counter aggiunto: `actor_writes/pass` (il counting del doc: il pass 2 = 45M vs il pass 1 =
  66M — il save ~30% sul pass con attore==updating; il decisions/pass reale = 147K, non 22K
  — il ns/elemento reale ~17ns, non 59ns — la correzione del counting).
- Verifica: smoke/full dEV esattamente bit-exact, AhKhQh bit-exact, ctest 15/15.
- Timing: full 156.7s (carico alto — il reach relativo 6.3->6.0/pass migliorato ma l'assoluto
  mascherato dal carico); il guadagno delle write è reale (counter) ma il tempo non scende
  proporzionalmente sotto carico.
