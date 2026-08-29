# Diario di ottimizzazione velocità — benchmark GTO+ th7d6s

> **STATO: REGISTRO ENGINEERING / NON SPECIFICA NORMATIVA.** Le sezioni
> cronologiche conservano misure e decisioni storiche; riaprire un'ottimizzazione
> richiede profiling e gate sul commit corrente.
>
> **HANDOFF CORRENTE 2026-08-14.** Prevalgono i §§8.46-8.48: dEV/root/RAM
> passano su AHK/TH/TST, il tempo fallisce su tutti e tre; TH è 37,810434 s e
> 249.955.776 B. Il riferimento Release passa 24 asserzioni, fallback
> asimmetrico e root lock. La pressione esterna non viene più usata per
> qualificare il fallimento temporale. Le diciture “stato corrente” nelle
> sezioni datate precedenti sono snapshot storici locali.

> **Documento di lavoro**: registro cronologico **completo e verificato** degli interventi di
> ottimizzazione applicati per ridurre il tempo del benchmark `GTP-TH7D6S-101`, con stato ed
> esito di verifica per ogni step. Contesto, obiettivo e protocollo di verifica:
> [`TH7D6S_SPEED_OPTIMIZATION.md`](TH7D6S_SPEED_OPTIMIZATION.md) (obiettivo <90 s,
> protocollo di verifica §5, gate di accettazione §1).

---

## 1. Obiettivo e stato attuale

> **HANDOFF STORICO 2026-08-11 (superato dai §§8.46-8.48).**
> Il target corrente non è più il vecchio “sotto 90 s”, ma almeno il 90% di
> GTO+: **`elapsed_seconds <=19,622222 s`** e **solver state <=443.333.333 B**.
> La RAM passa con **416.592.960 B**. Il checkpoint credibile corrente chiude
> a 79 iterazioni con dEV **0,987345%**, root EV **8,220498 ante**, elapsed
> **27,896048 s** e correctness/layout PASS; il tempo resta circa 1,42 volte il
> limite. Le sezioni §§8.30-8.35 descrivono lo stato moderno; i checkpoint
> precedenti restano storia architetturale.
>
> Il codice mantenuto comprende stato float24/binary16, tier scratch compatti,
> fusione terminale fold/showdown, certificazione esatta finale, decode
> float24 overread protetto, store regret impacchettato e decode SIMD a tre
> azioni con shuffle/trasposizione. Le varianti che modificavano l'aritmetica,
> aumentavano memoria o regredivano sul full sono state rimosse.
>
> Ultima validazione: build Release completa PASS e suite CTest **16/16** PASS
> in **229,70 s**, incluso reference GTO+ da 106,97 s e smoke diagnostico CLI da
> 20,20 s. Il full A/B dello shuffle a tre azioni passa correctness e conserva
> dEV/root/RAM; i wall da 43,03830/43,80217 s sono contaminati e non
> non sostituiscono il checkpoint pulito da 27,896048 s.
>
> **Ripresa consigliata:** non lanciare ancora cinque full. Il residuo richiede
> una riduzione strutturale del lavoro showdown/value o del numero di passate,
> mantenendo invariati gioco e aritmetica. In parallelo, il root mismatch TST
> richiede gli importi GTO+ effettivi 33%/75%; le label arrotondate non bastano.
> La sessione è chiusa su questo handoff. TH/TST sono ricertificati nel tree
> `windows-release-current`; AHK deve ancora essere rieseguito lì. Il reference
> test pulito passa 24 assertion, ma la suite CTest completa non è stata
> rilanciata dopo il checkpoint finale: i risultati 16/16 sotto restano
> evidenza storica del candidato precedente.

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
[`ARCHITECTURAL_REWRITE_PLAN.md`](ARCHITECTURAL_REWRITE_PLAN.md) — layout stato
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

## §8.15 Checkpoint GTP-TH7D6S-101 — stato mixed e traversal fisico compatto (2026-08-08)

Il percorso fisico usa ora stato persistente `float24` per i regret positivi CFR+ e
`binary16` per la strategy sum, con calcolo e certificazione `float64`. Il formato storage v2
salva e ricarica i byte mixed senza conversioni; il formato v1 resta leggibile. Il footprint
solver è **416.592.960 B** (5 B/action), sotto il gate 90% di **443.333.333 B** rispetto ai
399 MB decimali dichiarati da GTO+.

Ottimizzazioni lossless mantenute nell'hot path: action base dirette, decode/update packed,
slot value board-local, scratch da 384 combo, chance seriale streaming, accumulo strategy
`binary16` a batch, terminal metadata rank-major, accumulo chance sulle combo già filtrate
del child board e reset sparso delle celle showdown toccate. L'esperimento con value/reach
temporanei `float32` è stato rifiutato: migliorava solo circa il 3% e introduceva deriva full,
oltre a rendere falsa la dichiarazione `float64_compute`.

Run completo singolo Release (artefatto locale
`out/loop_exact_sparse_pair_lazy_full.json`): **109,6678086 s**, traversal
102,3970509 s, certificazione 6,7593137 s, iterazione 140, dEV
**0,9260452878479734%**, root EV **8,219182737421068 ante**, fingerprint
`fnv1a64:db01987c7570ec46`, `correctness_passed=true`, layout PASS. Peak RSS osservato:
619.970.560 B, separato dalla memoria solver. Rispetto a GTO+ 17,66 s: speed score
**16,1032%**, quindi il gate tempo `<=19,622222 s` resta **FAIL**; memory score
**95,7769%**, gate memoria **PASS**.

Validazione focalizzata: `F8_STORAGE_TESTS=PASS` (314 assertion),
`GTO_PLUS_REFERENCE_TEST=PASS` (24 assertion), parità DAG massima `7,10543e-15` e
root-lock `2,22045e-16`. Il run singolo non sostituisce la mediana obbligatoria di cinque
processi. Il prossimo salto richiesto è architetturale (dataflow infoset/street-major e
riduzione delle traversate duplicate); ulteriori micro-ottimizzazioni non possono colmare il
fattore residuo di circa 5,6x.

## §8.16 Checkpoint GTP-TH7D6S-101 — payoff terminali e update packed (2026-08-08)

Il layout fisico precalcola ora una sola volta i payoff terminali immutabili per outcome;
l'hot path di fold/showdown non richiama più `settle_terminal`. Il percorso comune a due
azioni aggiorna inoltre direttamente la coppia di regret `float24`, mentre la produzione dei
valori di showdown elabora quattro combo per batch con aritmetica AVX2 `float64`. Questi
interventi non modificano fixture, algoritmo CFR+, precisione dichiarata o criterio di arresto.

Run completo singolo Release (artefatto locale
`out/loop_terminal_direct_updates_full.json`): **89,2088938 s**, traversal
82,4160045 s, certificazione 6,2396162 s, iterazione 140, dEV
**0,9260452878479734%**, root EV **8,219182737421068 ante**, fingerprint
`fnv1a64:db01987c7570ec46`, `correctness_passed=true`, layout PASS. La memoria solver resta
**416.592.960 B**; il peak RSS osservato è 632.647.680 B e rimane una metrica distinta. Le
frequenze e gli EV condizionali GTO+ non sono allineati e non vengono presentati come gate
superati: il gate di correttezza corrente riguarda il root EV.

Rispetto al checkpoint §8.15 il tempo scende del **18,65%**. Rispetto a GTO+ 17,66 s lo
speed score è **19,80%** e il limite `<=19,622222 s` resta **FAIL** (tempo ancora circa
4,55x sopra il limite); il memory score è **95,7769%**, quindi il gate memoria resta
**PASS**. È una misura singola, non la mediana obbligatoria di cinque processi.

La regressione out-of-core emersa durante la validazione è stata corretta serializzando le
letture di certificazione sul backend paginato, il cui cache LRU è mutabile; il backend RAM
del benchmark conserva il parallelismo. Validazione finale Release: suite CTest **15/15
PASS**, incluso `gtosd_phase7_tests`, `gtosd_phase8_tests` e
`gtosd_gto_plus_reference_tests`. Il prossimo salto resta il dataflow infoset/street-major:
il checkpoint non soddisfa il gate tempo e non autorizza F11+.

Due esperimenti successivi sono stati rifiutati. La fusione lazy `Decision -> Terminal` era
bit-exact e riduceva le scritture reach da circa 45–66 milioni a 28,6 milioni/pass, ma
spostava il costo nel terminale (showdown fino a circa 820 ms/pass) senza ridurre il wall
time. La rimozione dello zero-fill prima dell'accumulo EV riduceva il relativo micro-kernel
di circa 20–25%, ma il run completo misurava **91,7653301 s**, peggiore del checkpoint
89,2088938 s. Entrambe le modifiche sono state rimosse dalla sorgente produttiva.

È stato rifiutato anche un prototipo di stato `binary16` per regret e strategy
(4 B/action, 333.274.368 B). Con conversioni hardware e fast path diretto a due azioni il
run completo convergeva ancora all'iterazione 140 e passava il root gate, ma impiegava
**97,6657 s** e spostava il dEV a **0,92556%**. Era quindi più lento del mixed
`float24/binary16`, oltre a introdurre maggiore quantizzazione; fixture, enum e codice
sperimentale sono stati ripristinati.

Anche due esperimenti sul pool sono stati rifiutati. Accodare tutte le 33 carte turn,
anziché lasciare al main thread una quota diretta, portava lo smoke a **8,17619 s** contro
7,90035 s. Lo split annidato lossless delle carte river conservava esattamente dEV/root EV,
ma generava circa 900 task/pass e faceva salire lo smoke a **45,6659 s**. Il parallelismo
resta quindi coarse-grained al turn; un futuro percorso street-major deve usare batch
persistenti, non un task per board.

Un controllo `float32/float32` allineato (solo smoke, RAM 666.548.736 B e quindi fuori
gate) non rappresenta un upper bound migliore: **9,45001 s**, dEV 104,649%, contro circa
7,9 s del mixed. Il costo dominante non è quindi il solo encode/decode `float24`; il
working set e il traffico aggiuntivo dello stato a 8 B/action peggiorano il risultato.

È stato inoltre costruito e poi rimosso un execution graph fisico contiguo (nodi da 8 B,
edge compatti) per evitare i `std::vector` del public tree nell'hot path. I cinque smoke
misuravano una mediana di **8,47492 s** e il full **90,265 s**, entrambi peggiori del
percorso esistente (smoke recenti 7,793–7,989 s; full 89,2088938 s). La duplicazione dei
metadati aumentava il working set più di quanto riducesse il pointer chasing.

La rimozione degli zero-fill terminali, pur essendo semanticamente ridondanti nel percorso
`PlayerIndexed`, non ha migliorato il codice generato: smoke **8,4017 s** e profilato
8,50229 s. È stata ripristinata per evitare una modifica senza guadagno end-to-end.

## §8.17 Hyperparameter Schedules su DCFR+ — RIFIUTATO (2026-08-08)

È stata verificata la formulazione pubblicata di HS-DCFR con schedule lineari
`alpha = 1 + 3t/n`, `beta = -1 - 2t/n` e `gamma = gamma0 - 5t/n`, con
`gamma0` pari a 15 o 30. Nel prototipo postflop i regret restano non negativi
per il clipping CFR+, quindi `beta` non è applicabile; l'esperimento è stato
etichettato esplicitamente HS-DCFR+ e non presentato come CFR+ canonico.

Per evitare una scansione completa dello stato a ogni iterazione, i discount
sono stati trasformati in pesi equivalenti sugli incrementi correnti mediante
il prodotto dei discount futuri. Il fattore globale non altera regret matching
né la normalizzazione della strategia media. I pesi strategy sono stati inoltre
normalizzati con un unico fattore per restare rappresentabili in `binary16`.

Risultati Release sul fixture di gioco invariato `GTP-TH7D6S-101`:

- HS-DCFR+(15): convergenza a iterazione **80**, dEV **0,8409205021541305%**,
  **65,0266614 s**, correttezza e layout PASS, stato solver 416.592.960 B;
- HS-DCFR+(30): convergenza a iterazione **120**, dEV **0,3769861776011457%**,
  **95,0473936 s**, correttezza e layout PASS, stato solver 416.592.960 B.

HS-15 riduce il tempo del checkpoint CFR+ da 89,2088938 s a 65,0266614 s, ma
resta 3,31 volte sopra il limite di 19,622222 s e cambia algoritmo. HS-30 è
anche più lento del checkpoint CFR+. Entrambe le varianti sono state rimosse:
fixture, formato checkpoint, API e report restano CFR+ canonici. Gli artefatti
locali sono `out/hs_dcfr15_full.json` e `out/hs_dcfr30_full.json`.

## §8.18 Prefissi blocker compatti per showdown — RIFIUTATO (2026-08-08)

Il prefisso blocker dense `36 × rank_count` è stato sostituito in prototipo da
gruppi compatti contenenti soltanto le coppie `(carta privata, rank)` realmente
presenti. L'accumulo manteneva l'ordine per gruppo e i boundary lower/equal/all
erano precalcolati per combo. F7 passava 42/42, il reference test 24/24 e la
parità DAG restava entro `7,10543e-15` sui regret.

Il profilo misurava 43–66 ms serial-equivalent/pass per la costruzione dei
prefissi compatti, ma il terminale completo restava 587–859 ms/pass. Tre smoke
Release hanno prodotto 10,1606752 s, 10,0591096 s e 9,2332594 s; la mediana del
solo traversal era **3,4021234 s**, contro **3,3522895 s** nel ripristino CFR+.
Il dEV cambiava inoltre di circa `3,1e-7` per la diversa associazione delle
somme. Nessun miglioramento end-to-end: metadata e codice sperimentale sono
stati rimossi e il prefisso dense è stato ripristinato.

## §8.19 Reciproco della normalizzazione terminale — RIFIUTATO (2026-08-08)

Le divisioni per `initial_normalization` nei terminali fold/showdown sono state
sostituite in prototipo da una moltiplicazione per il reciproco precalcolato.
F7 e reference test passavano, ma l'arrotondamento differente si accumulava
nello stato compresso: dEV smoke **104,64527922387252%** invece di
104,6472586554173%, e il root-lock a 30 iterazioni passava da circa 2,35465% a
2,3616%.

Tre smoke Release hanno misurato traversal **3,6851954 s**, **3,7928825 s** e
**3,8253628 s**, tutti peggiori del riferimento 3,3522895 s. Nessun guadagno e
deriva numerica non necessaria: le divisioni IEEE originali sono state
ripristinate.

## §8.20 Regret matching AVX2 sul packed float24 — RIFIUTATO (2026-08-08)

Il caso comune a due azioni è stato decodificato in batch da quattro combo e
normalizzato con una divisione AVX2 per quattro lane. La formula e l'ordine
per combo restavano identici: F7 42/42, reference 24/24, root-lock invariato e
parità DAG `7,10543e-15`.

Il profiler ha però misurato **403–533 ms/pass** nel regret matching, contro
circa 193–303 ms/pass del fast path scalare packed. Il costo di unpacking,
materializzazione delle lane e scatter nelle strategy scratch supera il
risparmio sulle divisioni. Il batch AVX2 è stato rimosso senza promozione.

## §8.21 Bilanciamento pool e output fold AVX2 — MISURATI / RIFIUTATO (2026-08-08)

Una strumentazione temporanea ha attribuito durata e numero dei subgame turn a
ciascun worker. Nei passaggi smoke ogni worker eseguiva circa 40–56 task; il
tempo occupato differiva tipicamente meno del 10% e la sincronizzazione restava
0,3–3 ms/pass. Il pool a cinque worker più main è quindi già ben bilanciato:
ridistribuire i task non può produrre il fattore richiesto.

L'output del terminale fold è stato poi vettorizzato a quattro combo, mantenendo
blocker e somme scalari nello stesso ordine. F7 42/42, reference 24/24,
root-lock e dEV erano bit-exact. Nell'A/B immediato, però, la mediana traversal
era **3,7099024 s** con AVX2 contro **3,4713696 s** con il percorso scalare
ripristinato, una regressione di circa 6,9%. La specializzazione e la
strumentazione temporanea sono state rimosse.

## §8.22 Audit layout SoA river — CONTRATTO DEL PROTOTIPO (2026-08-08)

L'albero reale è stato attraversato raggruppando tutti i figli delle chance
river per posizione strutturale. Tutte le shape corrispondono esattamente. Le
azioni river correnti sono **82.349.640**; le azioni non-river sono 968.952.

Un layout rettangolare che riserva tutte le 32 lane river richiederebbe
87.839.616 azioni river e **444.042.840 B** complessivi a 5 B/azione, superando
il gate RAM di 709.507 B. Non può quindi essere adottato.

Il prototipo street-major userà 30 lane attive per combo: ogni combo legale al
turn è assente esattamente nelle due lane corrispondenti alle proprie carte
private. L'indice lane compatto è il rank della lane nella maschera attiva a
32 bit. Questo conserva esattamente il numero di azioni attuale e lo stato
solver di **416.592.960 B**, ma rende contigue le 30 istanze river dello stesso
infoset/shape per il successivo batching SoA. L'audit temporaneo è stato
rimosso dopo la misura.

## §8.23 Traversal river street-major tiled — MISURATO / RIFIUTATO (2026-08-08)

È stato implementato dietro flag sperimentale il layout compatto definito in
§8.22, mantenendo esattamente **83.318.592 azioni**, **36.596.832 infoset** e
**416.592.960 B** di stato solver. Il mapping `shape × combo turn × lane attiva
× azione` è stato validato contro l'indice fisico per ogni accesso. Il primo
batch da 32 lane usava circa 1,67 MB di scratch per livello ricorsivo e ha
misurato traversal **8,7449477 s** nello smoke, peggiore anche del layout
compatto scalare (**6,8891017 s**).

Il traversal è stato quindi tiled a 4 lane. Dopo aver reso diretto l'accesso
allo stato contiguo, lo smoke è tornato bit-exact rispetto al riferimento:
dEV **104,64725865541735%**, NashConv normalizzato **1,6088273972434635** e
root EV **9,062841794532305 ante**. Il traversal è sceso a **6,5872375 s** e il
tempo solver a **16,2690797 s**, ma il percorso produttivo ripristinato misura
rispettivamente **3,4763995 s** e **9,1327762 s**. Anche la certificazione
scalare sul layout riordinato saliva da **5,0913768 s** a **9,0143756 s**.
Il working set del tile raggiungeva 644.411.392 B di peak RSS, pur lasciando
invariata la memoria solver.

Il prototipo è quindi rifiutato: la sola contiguità delle lane non compensa lo
scratch ricorsivo, il data movement e la perdita dei fast path per-board; il
codice sperimentale è stato rimosso. Durante la prova ASan ha però individuato
due difetti indipendenti, mantenuti come correzioni: `player_local` ora usa
esplicitamente `-1` per le combo bloccate e il distruttore del worker esegue il
join prima di liberare gli scratch arena. Quest'ultimo elimina un reale
use-after-free di teardown osservato quando un traversal termina con errore.

## §8.24 Update AVX2 contiguo dei regret float24 — MANTENUTO (2026-08-08)

Il fast path produttivo a due azioni aggiorna ora quattro combo per batch. Due
load da 12 byte vengono espansi con `PSHUFB`, convertiti in quattro lane
`float64`, sommati ai delta CFR+, clippati a zero e ricompressi in float24. La
ricompressione SIMD replica bit per bit `encode_float24_bits`: conversione a
float32, round-to-nearest della mantissa scartata con ties-to-even e storage di
tre byte. Non cambiano algoritmo, ordine degli infoset, precisione dichiarata
o memoria.

Tre smoke Release hanno misurato traversal **2,9215357 s**, **2,8198775 s** e
**2,7891432 s** (mediana **2,8198775 s**), tutti con dEV
**104,6472586554173%** e root EV **9,062841794532305 ante** bit-exact. Il full
singolo `out/loop_regret_update_avx_full.json` converge ancora a iterazione
**140**, dEV **0,9260452878479734%**, root/correctness/layout PASS, in
**87,2429296 s**: traversal **80,6153129 s**, certificazione **6,1226345 s**,
stato solver **416.592.960 B**, peak RSS **632.254.464 B**. Rispetto al
checkpoint §8.16 (89,2088938 s) il guadagno full è **2,20%**; il limite tempo
19,622222 s resta FAIL, mentre la RAM resta PASS.

Validazione Release dopo il cambiamento: tutti i 15 test CTest passano. La
prima invocazione ha completato 13/15 prima del timeout dell'orchestratore; i
due rimanenti sono poi passati separatamente (`gtosd_gto_plus_reference_tests`
112,89 s e `gtosd_benchmark_runner_smoke` 0,22 s). Non è ancora una mediana
full di cinque processi e non viene presentata come gate tempo superato.

## §8.25 Cache depth-local dei regret decodificati — RIFIUTATO (2026-08-08)

Per evitare la seconda lettura dello stato dopo la ricorsione, i bit float32
esatti dei due regret sono stati salvati nello scratch del decision node e
riutilizzati dal kernel AVX2 §8.24. La parità è rimasta bit-exact, ma le tre
misure smoke hanno prodotto traversal **2,8460172 s**, **2,9858460 s** e
**2,8620539 s** (mediana **2,8620539 s**), peggiore della mediana
**2,8198775 s** senza cache. Il traffico di scrittura/lettura dello scratch
annulla il risparmio della rilettura packed; metadata e codice sono stati
ripristinati.

## §8.26 Normalizzazione AVX2 della strategy media binary16 — MANTENUTA (2026-08-08)

La certificazione `PlayerIndexed` normalizza ora quattro blocchi contigui a due
azioni per volta: un load di otto `binary16`, conversione F16C, deinterleave
delle due azioni e divisione/multiply `float64` per lane. Il percorso non
indicizzato resta scalare perché usa slot combo sparsi; un primo prototipo che
ignorava questo vincolo conservava le frequenze aggregate ma permutava le
strategie combo-specifiche e alterava gli EV, ed è stato corretto prima della
promozione.

Nel confronto smoke a parità di eseguibile, la certificazione è scesa da
**4,1536434 s** a **3,8216312 s**, mantenendo root EV
**9,062841794532305 ante**, dEV e NashConv. Il full singolo
`out/loop_policy_half_avx_full.json` converge a iterazione **140** con dEV
**0,9260452878479758%**, root EV **8,219182737421068 ante**, correctness/layout
PASS, in **86,1059943 s**: traversal **79,7679045 s**, certificazione
**5,7897606 s**, stato solver **416.592.960 B**, peak RSS **632.463.360 B**.
Rispetto al checkpoint §8.16 il guadagno cumulativo è **3,48%**; rispetto al
solo kernel §8.24 è circa **1,30%**. Il gate tempo resta FAIL di circa 4,39×.

## §8.27 Best response SIMD e shortcut zero-sum — MANTENUTI (2026-08-08)

Nel traversal di certificazione `PlayerIndexed`, il max della best response e
la somma delle action value sono ora action-major su stream contigui; il max
usa quattro lane AVX2. Inoltre, quando il rake è disabilitato, l'EV di profilo
del giocatore 1 viene derivato esattamente come opposto dell'EV del giocatore
0. Restano quindi tre traversate complete (profilo P0, BR P0, BR P1) invece di
quattro; con rake attivo entrambi i profili continuano a essere attraversati.

Un A/B alternato nello stesso eseguibile ha misurato la certificazione a
**3,8658547 / 3,8150052 s** con il shortcut, contro **4,4743524 / 4,7136527 s**
forzando quattro traversate. dEV **104,64725865541735%**, NashConv
**1,6088273972434635** e root EV **9,062841794532305 ante** restano invariati.
Il full successivo `out/loop_zero_sum_cert_full.json` passa correttezza/layout
a iterazione 140 con stato solver **416.592.960 B**, ma è stato eseguito sotto
carico più alto: traversal **82,4915388 s**, certificazione **5,6785958 s** e
wall solver **88,6878127 s**. Non viene usato per attribuire un guadagno wall
aggiuntivo; l'A/B isolato prova invece il risparmio di certificazione.

Regressioni Release dopo i due cambiamenti: `gtosd_phase7_tests` PASS e i due
gate rimanenti PASS (`gtosd_gto_plus_reference_tests` 96,83 s,
`gtosd_benchmark_runner_smoke` 0,11 s).

## §8.28 Fusione dei terminali fold/showdown fratelli — MANTENUTA (2026-08-08)

Quando l'attore è il giocatore aggiornato e una decisione ha sia un figlio
`TerminalFold` sia un figlio `TerminalShowdown`, entrambi consumano lo stesso
opponent reach. Il percorso fisico calcola ora in un'unica passata i valori di
showdown e, negli stessi loop combo, il totale compatibile necessario al fold.
La fusione non cambia l'ordine degli aggiornamenti CFR+, lo stato persistente o
la precisione; non si applica alla certificazione né alle decisioni dell'altro
giocatore.

Un A/B alternato nello stesso eseguibile ha misurato traversal smoke
**2,8043985 / 2,7213442 s** con la fusione, contro
**2,8733703 / 2,7549344 s** senza fusione: circa **1,8%** di vantaggio medio.
Tutti i run conservano bit-exact dEV **104,64725865541735%**, NashConv
**1,6088273972434635** e root EV **9,062841794532305 ante**. Il full
`out/loop_terminal_pair_full.json` passa a iterazione **140** con dEV
**0,9260452878479758%**, correctness/layout PASS, stato solver
**416.592.960 B** e peak RSS **632.569.856 B**. È stato però eseguito sotto
carico elevato (traversal **86,9468606 s**, certificazione **5,7675246 s**,
wall solver **93,2314325 s**) e non viene usato come misura comparativa di
tempo. Il flag temporaneo usato esclusivamente per l'A/B è stato rimosso.

## §8.29 Suite a tre benchmark e nuovo profilo — IN CORSO (2026-08-09)

Il gate non è più valutato soltanto su `TH7D6S`: il loop corrente comprende
`AHKHQH`, `TH7D6S` e il nuovo `TSTC9D`. Il baseline Release credibile, misurato
prima della contaminazione ambientale descritta sotto, è:

| Benchmark | Iterazioni | dEV | Root EV | Elapsed | Traversal | Certificazione | Solver state |
|---|---:|---:|---:|---:|---:|---:|---:|
| `AHKHQH` | 80 | 0,630492718% | 19,11232258 | 3,3766446 s | — | — | gate PASS |
| `TH7D6S` | 80 | 0,907386861% | 8,215224715 | 46,4381001 s | 42,6706035 s | 3,2527011 s | 416.592.960 B |
| `TSTC9D` | 80 | 0,897231878% | 8,574113772 | 184,2356283 s | 172,748 s | 10,887 s | 752.202.000 B |

I target dEV e RAM passano per tutti e tre. I target tempo falliscono per tutti;
`TSTC9D` fallisce inoltre il root EV confermato dall'utente (`8,50165`,
tolleranza `±0,05`). Il residuo TST non viene attribuito al solver finché non
sono noti gli importi esatti GTO+ generati dai sizing 33% e 75%: il JSON è
correttamente marcato `metadata_complete=false`.

Modifiche generali mantenute nel checkpoint di lavoro:

- profiling hot-path compilabile soltanto con
  `GTOSD_ENABLE_HOTPATH_PROFILE`, così la build benchmark non paga branch o
  letture d'ambiente;
- metadata terminali precomputati e impacchettati in `DecisionLayout` senza
  aumentarne la dimensione (`sizeof(DecisionLayout) == 24`): maschera figli
  terminali e coppia fold/showdown;
- certificazione policy senza copie inutili della reach del giocatore che non
  influenza il counterfactual value; la reach dell'attore avversario usa
  scratch per-depth e il resto rimane condiviso;
- rank metadata dei board a cinque carte preparati prima dell'avvio dei worker;
  fold/showdown `PlayerIndexed` scrivono direttamente tutte le slot vive;
- scheduler CFR ripristinato sullo split completo delle carte turn con coda
  condivisa e work stealing; scheduler policy mantenuto deterministico con
  quota seriale del main thread.

La suite di riferimento dell'ultimo checkpoint mantenuto ha passato 24
assertion, inclusi fallback fisico per range asimmetrici e root lock esterno.
Una successiva prova che eliminava lo zero-fill a ogni decision node ha invece
fallito il requisito «float32 performance checkpoint remains exactly
certifiable and browsable» ed è stata integralmente ripristinata. La suite
completa deve essere rieseguita dopo il prossimo build finale prima di
promuovere il checkpoint.

Il profilo aggiornato dello smoke `TH7D6S` (build diagnostica separata, dieci
passate giocatore) attribuisce in media circa 284 ms serial-equivalent al
regret matching, 301 ms a value/update, 197 ms ai terminali e 174 ms alla
propagazione reach; `average-only` pesa circa 150 ms quando attivo. Il reach non
è quindi più il collo dominante isolato. La build diagnostica è stata poi
disattivata (`GTOSD_ENABLE_HOTPATH_PROFILE=OFF`).

Esperimenti misurati e rifiutati in questa sessione:

- capacità scratch ridotta al solo `combo_count`: full 53,94 s contro 46,44 s;
- sweep DCFR+ a 40 iterazioni: gamma 4 = 3,4097% dEV, alpha 3 = 4,7721%,
  alpha 1 = 5,3736%; nessuno raggiunge 1%; il default alpha 1,5 / gamma 2 resta
  il migliore tra quelli provati;
- fusione lazy della reach terminale, unpack float24 alternativo e coda policy
  condivisa: regressioni, tutti ripristinati;
- elenco univoco delle celle card/rank da azzerare: traversal 3,223 / 3,154 /
  3,110 s, nessun vantaggio credibile;
- output fold SIMD a quattro lane: mediana traversal 3,056 s contro 3,077 s
  nel ripristino, circa 0,7% e quindi dentro il rumore; rimosso;
- eliminazione dello zero-fill dei decision value: root/dEV smoke invariati ma
  reference test float32 fallito; rimosso senza eccezioni.

Le misure più recenti sono rallentate di circa 15–25% da `Sisal` (CPU continua)
e dal piano energetico Windows `Bilanciato`. Non si arresta un processo utente
né si cambia il piano energetico senza autorizzazione. Il gate finale richiede
cinque processi indipendenti con carico esterno sospeso; fino ad allora i nuovi
wall time sono diagnostici e il baseline 46,4381001 s resta il confronto
credibile per `TH7D6S`.

La direzione successiva non è una micro-ottimizzazione fixture-specifica:
occorre ridurre lavoro terminale/value per passata oppure introdurre una
variante di pruning con garanzie e stato espliciti. Il regret-based pruning è
un candidato scientificamente fondato, ma non può essere etichettato come
DCFR+ canonico né introdotto senza specificare aggiornamenti differiti,
averaging, bounds dei payoff, persistenza e certificazione.

## §8.30 R1 capacity, terminal reach fusion e suite a tre fixture (2026-08-11)

Il runner fisico seleziona ora tier statici 256/360/384/512 prima del fallback
630 e `DecisionScratch::strategies` usa la capacità del runner. Sullo smoke
`TH7D6S` il tier 360 ha ridotto la traversal da 2,38302 s a 2,28288 s a dEV
bit-exact. Il tentativo di separare value capacity 256 e reach capacity 360 con
due runner/pool è stato respinto: 2,40769 s, circa +5,5%, con RSS maggiore.

La reach delle azioni terminali fold/showdown accoppiate viene prodotta in una
sola scansione. Tre smoke hanno dato 2,20050 / 2,30924 / 2,20129 s; il full
`out/th7d6s_r1_pair_fused_full.json` ha chiuso a 80 iterazioni con dEV
0,907386861%, traversal 41,553755 s, certificazione 2,729239 s, elapsed
44,845369 s e stato solver 416.592.960 B. Il decode float24 a due azioni usa
inoltre due load contigui e byte-shuffle AVX2 al posto di quattro ricostruzioni
scalari; dEV e reference test restano invariati. La suite reference ha passato
24 assertion, fallback asimmetrico con differenze regret/strategy zero e root
lock esterno.

Misure correnti degli altri gate: `AHKHQH` converge a 60 iterazioni con dEV
0,943925348%, root 19,104354 ante, elapsed 2,550599 s e 6.677.088 B;
`TSTC9D` converge a 140 con dEV 0,897231878%, root 8,574114 ante, elapsed
183,819079 s e 752.202.000 B. Restano FAIL i tre gate tempo e il root EV TST.

Esperimenti respinti in questo checkpoint:

- prova di azione interamente zero senza propagare un flag reach-zero in tutte
  le firme: deriva dEV 101,926795% vs 104,647259%, ripristinata;
- update average float16 vettorizzato: corretto ma senza vantaggio isolabile,
  rimosso;
- PGO MSVC addestrato su AHK e TH: AHK pari (2,71650 vs 2,71865 s), TH smoke
  peggiore (2,77548 vs 2,35880 s traversal), respinto;
- AHK depth 7 sembrava 2,45 s, ma superava i 6 thread dichiarati perché il cap
  non era applicato; la fixture resta depth 5 e la CLI ora rifiuta
  `parallel_action_depth + 1 > maximum_solver_threads`;
- TST depth 7/8 thread: traversal smoke 4,80909 s contro 4,66827 s a 6 thread;
- sweep TST a 40 iterazioni: alpha 1,4 = 4,67735%, 1,3 = 4,77786%, 1,45 =
  4,93665%, gamma 3 con alpha 1,4 = 4,82302%. Il full alpha 1,4 converge ancora
  a 140 ed è peggiore (0,957006%, 184,008314 s); alpha 1,5/gamma 2 resta
  esplicito nella fixture;
- showdown output con SoA e gather AVX2: dEV bit-exact ma traversal smoke
  2,37390 / 2,33777 / 2,30464 s e circa 8 MB di metadata/RSS aggiuntivi,
  rimosso;
- eliminazione mirata dello zero-fill nei value a due azioni: dEV bit-exact ma
  mediana traversal 2,396 s, nessun vantaggio; ripristinata;
- precisione mista su AHK: stato 4.173.180 B ma convergenza a 80 iterazioni e
  3,38327 s, peggiore del float32 a 60 iterazioni;
- sweep AHK a 50 iterazioni: alpha 1,4 = 1,22214%, alpha 1,5 = 1,24705%,
  alpha 1,3 = 1,30008%, gamma 3 con alpha 1,4 = 1,27968%. Nessuno passa il
  target 1%; il contratto resta alpha 1,4/gamma 2, max 200 e cert 20.

Il profiler è stato nuovamente disattivato. I full più lenti eseguiti durante
carico esterno sono diagnostici e non sostituiscono il checkpoint credibile
TH da 44,845369 s. La suite Release finale del checkpoint passa 15/15 test
(225,56 s reali), incluso il reference GTO+ lungo. F11+ resta congelata.

## §8.31 Finestra di averaging per fixture e reach board-local (2026-08-11)

La finestra della strategia media è stata misurata separatamente sulle tre
fixture, senza cambiare regole, albero, precisione, update CFR+ o criterio dEV.
I massimi delay che conservano la prima certificazione valida sono ora 10 per
`AHKHQH` (iterazione 60), 40 per `TH7D6S` (iterazione 80) e 120 per `TSTC9D`
(iterazione 140). I candidati Release singoli risultano:

| Benchmark | Delay | Iter | dEV | Root EV | Elapsed | Traversal | Stato solver |
|---|---:|---:|---:|---:|---:|---:|---:|
| `AHKHQH` | 10 | 60 | 0,981300634% | 19,103998761 | 2,7262851 s | 2,5595967 s | 6.677.088 B |
| `TH7D6S` | 40 | 80 | 0,958530621% | 8,220911009 | 37,2236727 s | 33,3896325 s | 416.592.960 B |
| `TSTC9D` | 120 | 140 | 0,934525152% | 8,576975777 | 156,7339997 s | 142,3266580 s | 752.202.000 B |

I boundary immediatamente successivi sono stati respinti: AHK delay 15 passa
solo a 80 iterazioni; TH delay 50 è 1,008247392% a iterazione 80 e passa solo
a 100; TST delay 130 è 1,016010546% a iterazione 140 e passa solo a 160.
I nuovi delay riducono lavoro di averaging, ma non sbloccano alcun time gate.
TST continua inoltre a fallire il root EV: delta +0,075325777 ante contro la
tolleranza assoluta 0,05, con metadata GTO+ 33%/75% ancora incompleti.

È stato inoltre provato un layout reach board-local con remap esatto ai chance
edge. Lo smoke TH conservava dEV bit-exact e mostrava 2,3005428 s di traversal,
ma il full saliva a 43,6172630 s di traversal e 47,4825542 s elapsed, peggio del
checkpoint 41,5537553/44,8453692 s. Su TST il remap globale peggiorava lo smoke
da circa 4,67 a circa 4,81 s. Anche il dispatch statico limitato al tier 360 è
stato quindi rimosso integralmente. Il ripristino ha passato le 24 assertion
reference, il fallback asimmetrico con differenze regret/strategy zero e il
root lock esterno; la build benchmark resta con profiler disattivato.

## §8.32 Certificazione finale e decode float24 a tre azioni (2026-08-11)

Poiché la prima certificazione valida è deterministica e già nota per ogni
fixture, `maximum_iterations` e `certification_interval` sono stati fissati
rispettivamente a 60/60, 80/80 e 140/140. Rimane una best response esatta
finale: non si sostituisce il dEV con il numero di iterazioni. Su TST la sola
certificazione finale ha ridotto la fase BR da 13,6900888 s a 2,0058054 s e
l'elapsed da 156,7339997 s a 130,5724096 s, a dEV e root sostanzialmente
invariati.

Il path float24 a tre azioni usa ora un load non allineato a 32 bit mascherato
nei soli loop che provano la presenza di almeno un byte successivo dentro la
stessa allocazione; il tail conserva il decoder a tre byte. Tre smoke A/B
adiacenti hanno misurato traversal 2,0501385 / 2,0092858 / 2,1611244 s contro
2,2461941 / 2,1397625 / 2,2162793 s: mediane 2,0501 contro 2,2163 s, circa
7,5% di vantaggio, con dEV bit-exact 104,64725865541735%. Il reference passa
24 assertion, fallback asimmetrico con differenze zero e root lock esterno.

Il massimo delay TST robusto è stato portato a 125: delay 128 passa con margine
solo 0,0102 punti percentuali, mentre 130 fallisce a iterazione 140. Il full
TST candidato chiude a dEV 0,963255436%, root 8,576672337 ante, traversal
124,3110688 s, certificazione 1,5232734 s, elapsed 126,5235856 s e stato
752.202.000 B. Il time gate 120,600000 s resta FAIL di 5,9235856 s; il root
resta FAIL di 0,025022337 ante oltre tolleranza.

Nello stesso ambiente AHK chiude a 2,6789625 s e TH a 38,3581916 s: entrambi
restano sopra i rispettivi limiti. Un campionamento istantaneo ha misurato
`Sisal` a 1,266 CPU-second in 2 secondi (circa 0,63 core) e Discord a 0,375;
nessun processo utente è stato sospeso. I cinque processi finali richiedono
quindi una finestra pulita autorizzata e non sono ancora dichiarati eseguiti.
La build completa del candidato e la suite Release finale passano 15/15 test
in 200,57 s reali, incluso il reference GTO+ da 101,21 s.

Sul benchmark piccolo AHK, `parallel_action_depth=4` riduce l'elapsed a
2,2377440 s contro 2,6789625 s a depth 5, con dEV 0,981301009%, root EV
19,103998778 ante e stato 6.677.088 B invariati. Depth 3 regredisce a
2,6968319 s ed è stato respinto. La stessa riduzione su TH regredisce a
46,0981 s contro 38,3582 s, quindi TH resta a depth 5.

## §8.33 Schedule TH a 60 iterazioni e store float24 (2026-08-11)

La nuova finestra di averaging ha reso utile riesaminare TH direttamente a 60
iterazioni. Nessuna schedule provata passa il gate: exponent medio 4 produce
1,66870% dEV, alpha 1,4 produce 1,50192% e alpha 1,3 regredisce a 1,99001%.
La fixture è stata ripristinata integralmente a alpha 1,5, exponent medio 2,
80/80, delay 40 e depth 5.

Un gather AVX2 dei tre regret float24 a stride 9 è stato respinto: mediana
smoke 2,33350 s contro 2,31345 s del decoder a load scalari da 32 bit. Nel
regret update a tre azioni è invece mantenuto lo store impacchettato: i tre
risultati da 24 bit vengono scritti come otto byte più il nono byte, senza
overrun. La mediana smoke è 2,27509 s contro 2,31345 s adiacente, circa 1,7%,
con dEV bit-exact. Il reference passa 24 assertion, fallback asimmetrico con
differenze regret/strategy zero e root lock esterno.

Il full TST successivo conserva dEV 0,963255436%, root 8,576672337 ante e
752.202.000 B, ma il traversal è contaminato a 165,7114371 s mentre `Sisal`
resta attivo; il wall 167,8026063 s non sostituisce il candidato credibile da
126,5235856 s e non viene usato per promuovere o bocciare il time gate.

La suite Release completa dopo lo store impacchettato passa 15/15 test in
196,05 s reali. È stato inoltre chiuso lo scaling verticale: configurare
esplicitamente 8 thread porta AHK a 2,55507 s e TH a 45,7407 s; 7 thread porta
TH a 42,2808 s. Tutti conservano dEV/root/RAM ma peggiorano il candidato a sei
thread, quindi le fixture definitive restano AHK depth 4/6 thread e TH/TST
depth 5/6 thread.

## §8.34 Catalogo monetario esatto delle azioni iniziali (2026-08-11)

Il report benchmark include ora `initial_street_action_catalog` per tutti i
nodi decisionali della street iniziale. Per ogni azione registra label, tipo,
`amount_units`, importo in ante, basis point richiesti e natura dell'all-in;
per ogni nodo registra inoltre history, pot, stack e commitment esatti. La
label storica resta compatibile, ma tronca la parte frazionaria: per esempio
lo smoke TH usa `bet_9` per una bet reale di 9,5 ante.

Sul fixture TST il catalogo contiene 28 nodi decisionali flop. Al root,
`bet_5` è 5,28 ante (33% esatto di 16) e `bet_12` è 12 ante; dopo la prima bet
il pot è 21,28 ante e le size successive propagano importi frazionari come
35,3248 e 58,6392. Questo crea una spiegazione verificabile da confrontare con
GTO+, ma non autorizza ancora a modificare la semantica di rounding: gli
importi esterni 33%/75% non sono disponibili e la fixture resta
`metadata_complete=false`.

La nuova diagnostica è coperta dal test CLI end-to-end
`gtosd_gto_plus_action_catalog_smoke`: il run può terminare con gate dEV non
superato, ma deve produrre un catalogo valido e preservare contemporaneamente
la label `bet_9`, l'importo esatto 95.000 unità e la richiesta 5.000 bp. Test
Release: PASS in 18,33 s.

Il profilo aggiornato dello smoke dopo lo store float24 attribuisce ancora il
costo serial-equivalent soprattutto a produzione dei valori showdown,
accumulo rank/card e regret matching. Un tentativo di ricavare gli indici
carta con `first_by_rank % 36` e `second_by_rank % 36`, evitando i lookup
indiretti nel percorso showdown+fold, è stato respinto: traversal
2,29888 / 2,35641 / 2,39428 s, mediana 2,35641 s, contro 2,27509 s del
checkpoint adiacente. Il codice con lookup è stato ripristinato.

## §8.35 Decode SIMD float24 a tre azioni (2026-08-11)

Il regret matching a tre azioni decodifica ora quattro blocchi float24 da nove
byte con quattro load da 128 bit, shuffle SSSE3 e trasposizione 4x4 in
registri. Le tre colonne vengono convertite in `double` senza array temporanei;
somma, tre divisioni e fallback uniforme restano nello stesso ordine. La
condizione `local + 4 < actor_slot_count` garantisce che l'overread dell'ultimo
load rimanga nella stessa allocazione; il tail conserva il decoder sicuro da
tre byte.

Cinque smoke di conferma hanno traversal 2,26103 / 2,33783 / 2,31470 /
2,29951 / 2,25114 s, mediana 2,29951 s. Il baseline scalare adiacente ha dato
2,26028 / 2,53725 / 2,39728 s, mediana 2,39728 s: vantaggio circa 4,1% sullo
smoke. Nel full TH adiacente il candidato ha traversal 41,72206 s ed elapsed
43,03830 s contro 42,52554 / 43,80217 s del baseline, circa 1,9%. Entrambi
chiudono a iterazione 80 con dEV 0,958422%, root correctness PASS e stato
416.592.960 B. I wall restano contaminati e non sostituiscono il checkpoint
credibile 38,358192 s.

Il reference GTO+ completo del candidato passa in 96,08 s. È stato invece
respinto il tentativo di sostituire le tre divisioni con un reciproco e tre
moltiplicazioni: pur riducendo la mediana smoke a 2,25400 s, modificava
NashConv da circa 1,60883 a 1,60920. Anche il prefix sulle sole 31 carte vive
del river board è stato rimosso: mediana traversal 2,42156 s, peggiore del
loop fisso vettorizzabile sulle 36 carte.

Lo stesso decoder applicato al traversal `average-only` a tre azioni è rimasto
neutro: cinque smoke 2,43734 / 2,23364 / 2,20409 / 2,30220 / 2,34667 s,
mediana 2,30220 s, contro 2,29951 s del candidato senza quel ramo. NashConv è
invariato, ma il codice aggiuntivo è stato rimosso per assenza di beneficio.

La build Release completa del candidato e la suite CTest passano **16/16** in
**211,28 s** reali, incluso il reference GTO+ da 96,26 s e il catalogo CLI da
18,69 s. Il profiler rimane disattivato nella build Release.

## §8.36 Payoff tie zero e specializzazione showdown respinta (2026-08-11)

Nel kernel showdown, quando `tie_payoff` è esattamente zero, il numeratore
salta la moltiplicazione `equal * 0` e la relativa addizione. Il percorso con
tie non zero conserva formula e ordine originali. Sui benchmark rake-free il
risultato resta identico: cinque smoke hanno traversal 2,25851 / 2,24253 /
2,23537 / 2,34092 / 2,37995 s, mediana 2,25851 s contro 2,29951 s del
checkpoint precedente, circa 1,8%. NashConv è invariato e il reference GTO+
completo passa in 95,22 s.

Il full TH candidato chiude a traversal 42,64229 s ed elapsed 44,02782 s; il
baseline adiacente, sotto carico più alto, misura 48,43744 / 49,95740 s. La
direzione del vantaggio concorda con lo smoke, ma l'ampiezza full non è
promossa come percentuale stabile e non sostituisce il checkpoint credibile da
38,358192 s. dEV 0,958422%, root correctness e stato 416.592.960 B restano
invariati.

È stata invece respinta la specializzazione template del kernel per showdown
semplice contro showdown+fold fuso: mediana smoke 2,43578 s contro 2,29951 s,
circa +5,9%. La duplicazione del grande corpo funzione peggiora verosimilmente
l'instruction cache; è stata ripristinata la singola implementazione con
puntatori opzionali.

La build Release completa finale del candidato zero-tie e la suite CTest
passano **16/16** in **229,70 s**, incluso il reference GTO+ da 106,97 s e il
catalogo CLI da 20,20 s. Il profiler resta disattivato nella Release.

Anche la compattazione di `TerminalComboMeta` è stata respinta. I campi
`first_prefix/second_prefix` duplicano per invariante gli indici
`first_by_rank/second_by_rank`, ma rimuoverli produce uno stride da 14 byte e
una mediana smoke di 2,60905 s. La variante con padding esplicito a 16 byte
misura 2,58229 s. Entrambe peggiorano il layout originale da 18 byte e sono
state rimosse; `solver_state_bytes` resta comunque una metrica separata da
questi metadata di layout.

Un campionamento successivo ha misurato `Sisal` a 2,328 CPU-second in due
secondi, circa 1,16 core. Non è stato quindi eseguito un nuovo full TST né la
mediana finale: sotto tale carico il wall non sarebbe una certificazione
riproducibile.

## §8.37 Bilanciamento del pool e sweep TH a 60 passate (2026-08-11)

La build diagnostica registra ora task e wall effettivo per esecutore. Sullo
smoke TH ogni player-pass distribuisce circa 297 task: il thread chiamante e i
cinque worker ne eseguono tipicamente 46–55 ciascuno. Nei passaggi più pesanti
i wall per esecutore differiscono soltanto di pochi punti percentuali (per
esempio 221,1–226,1 ms e 224,7–236,3 ms), mentre la sincronizzazione misurata
resta circa 0,2–1,2 ms. Il pool condiviso è quindi già ben bilanciato; non è
stata introdotta una modifica scheduler priva di un collo misurato.

È stata riesaminata la possibilità di ridurre TH da 80 a 60 iterazioni
allungando la storia della strategia media. Con alpha 1,5 e gamma 2 invariati,
`delay=20` chiude a dEV 1,61583% e `delay=0` a 2,08555%; entrambi falliscono il
target 1%. La media precoce incorpora strategie immature e non recupera le 20
passate eliminate. La fixture canonica è stata ripristinata a 80/80/delay40.

Il tentativo autorizzato di isolare il benchmark non ha modificato processi:
i cmdlet `Suspend-Process`/`Resume-Process` non sono disponibili e la modifica
della priority class di Sisal è stata negata dal sistema. Non è quindi stata
dichiarata alcuna misura pulita.

## §8.38 Overlap lossless dei sottoalberi root (2026-08-11)

Il profilo del pool mostrava circa 70–110 ms seriali per player-pass sopra il
fan-out delle chance turn. Nel tree fisico a due azioni, il traversal prepara
ora i reach del root, accoda gli altri sottoalberi e percorre il primo sul
thread chiamante. I soli traversal dei rami root accodati possono esporre a
loro volta i task chance sulla coda condivisa; action values, indici fisici e
delta regret restano disgiunti. Il parent continua a ridurre action 0 e action
1 nell'ordine originale, quindi non cambia l'aritmetica della combinazione.

La variante intermedia in cui il ramo accodato rimaneva seriale è stata
respinta: smoke traversal mediana 2,549545 s contro circa 2,23 s. Anche il
fan-out annidato indiscriminato è stato ristretto dopo la regressione TST a
165,141 s. Con lo scope root, TH chiude a iterazione 80 con traversal
**30,709455 s**, certificazione **0,693277 s** ed elapsed **32,002085 s**,
contro il precedente checkpoint credibile 38,358192 s: miglioramento 16,6%.

I gate numerici restano invariati: dEV 0,958421754%, root EV misurato
10,853910007 ante contro riferimento pesato 10,780570114 entro tolleranza,
root correctness PASS, layout PASS e `solver_state_bytes` 416.592.960 B. Il
tempo resta FAIL contro 19,622222 s. L'estensione lossless al root a tre azioni
porta TST da 126,523586 s a **108,044422 s** (traversal 106,053552 s), con dEV
0,963255436% e stato 752.202.000 B invariati: il suo gate tempo 120,600000 s è
ora PASS. Il root EV resta 8,576672337 contro 8,50165 e quindi FAIL di
0,025022337 ante oltre tolleranza. La build Release e la suite completa passano
**16/16** test in **204,16 s**, incluso reference GTO+ da 93,08 s, F7 da
10,89 s e catalogo CLI da 18,38 s. Le misure benchmark sono singoli processi,
non la mediana finale di cinque.

Cinque smoke adiacenti del candidato root-only finale hanno traversal
1,722103 / 1,658836 / 1,660757 / 1,790264 / 1,622047 s, mediana 1,660757 s,
con NashConv identica 1,6088273972434635. Due estensioni sono state respinte:
un ulteriore livello decisionale flop misura mediana 1,728590 s; lasciare a
ogni ramo root una quota locale di chance misura 1,693365 s. Entrambe aumentano
la contesa rispetto al fan-out completo limitato al solo root.

## §8.39 Build pulito, SoA terminale e checkpoint a 79 iterazioni (2026-08-11)

Il vecchio tree Release era stato copiato da `F:\GTO Solver` e il relativo
`CMakeCache.txt` puntava a un sorgente non più presente. È stato quindi creato
`out/build/windows-release-current` dal checkout corrente; da questo punto i
numeri promossi provengono soltanto dal tree pulito. Il reference test corrente
passa 24 assertion, il fallback fisico con range asimmetrici conserva differenze
regret/strategy pari a zero e il root lock esterno passa.

Nel kernel mantenuto, i reach dell'attore usano gather AVX2 a quattro combo e
la coppia showdown/fold riusa lo stesso gather. I metadata terminali sono SoA;
l'output showdown e l'output fold accoppiato producono quattro valori alla
volta. TH a 79 iterazioni chiude con traversal **26,569203 s**,
certificazione **0,687167 s**, elapsed **27,896048 s**, dEV **0,987345218%**,
root **8,220497717 ante** e stato **416.592.960 B**. dEV, root e RAM passano;
il limite tempo **19,622222 s** resta FAIL.

Il full TST pulito chiude a **114,614952 s** (traversal **112,504652 s**,
certificazione **1,395291 s**), dEV **0,963255436%** e stato **752.202.000 B**:
tempo, dEV e RAM passano. Il root resta **8,576672337** contro **8,50165**,
delta **+0,075022337 ante**, quindi il root gate resta FAIL. La fixture rimane
`metadata_complete=false` perché mancano gli importi GTO+ effettivi 33%/75%.

Esperimenti misurati e respinti in questo checkpoint:

- TH 75 iterazioni: 1,08631% dEV; 78: 1,00791%; 79 è il primo punto PASS;
- float32 a 60 iterazioni: 1,54282% dEV, 29,6182 s e RAM fuori gate;
- indici terminali ricostruiti da rank/carta, `/favor:INTEL64`, `/Ob2` e
  averaging binary16 interamente SIMD: tutti regressivi nello smoke adiacente;
- regret float24 action-major: traversal 1,527-1,608 s contro 1,438 s;
- reach board-local: corretto dopo un controllo ASan sul metadata dei fold,
  ma 1,653 s di traversal; la filter/copy chance peggiora a 6,468 s elapsed.
  L'intero prototipo board-local è stato rimosso.

Restano mantenute soltanto le trasformazioni che preservano l'aritmetica e
mostrano un vantaggio misurato. Non sono stati usati reciprocal, `/fp:fast`,
sampling, bucketing o modifiche alle regole del gioco.

## §8.40 Chiusura sessione e stato di ripresa (2026-08-11)

La sessione è stata chiusa su richiesta dell'utente senza avviare altri full.
Il tree autorevole resta `out/build/windows-release-current`. Il reference test
GTO+ pulito passa 24 assertion; non è stata eseguita una nuova suite CTest
completa dopo il checkpoint §8.39 e non viene quindi dichiarato un nuovo 16/16.

Gate aperti alla ripresa:

- `AHKHQH`: riesecuzione pulita obbligatoria; l'ultimo candidato conservato
  passa dEV/root/RAM ma fallisce il tempo 2,237744 / 1,900000 s;
- `TH7D6S`: dEV/root/RAM PASS, tempo 27,896048 / 19,622222 s FAIL;
- `TSTC9D`: dEV/RAM/tempo PASS, root 8,576672 contro 8,50165 FAIL; gli importi
  monetari GTO+ 33%/75% mancanti impediscono una diagnosi certificabile;
- certificazione finale: suite Release completa e cinque processi indipendenti
  non ancora eseguiti sul candidato finale.

Il primo passo tecnico è ricertificare AHK sul tree pulito. Se il risultato è
stabile, il successivo collo da profilare è il doppio calcolo delle strategie
nelle due passate alternate, cercando un riuso bounded che non cambi ordine,
precisione o semantica CFR+. F11+ rimane congelata.

## §8.41 Riallineamento generale action tree GTO+ (2026-08-13)

Due schermate TSTC9D hanno reso non valido il precedente albero comparativo.
GTO+ mostra `Bet 5,3`; dopo quella bet offre `Raise 14`, `Raise 25`, call e
fold, senza all-in. Dopo `Raise 14` offre invece call, fold, `Raise 47` e
`Raise 80`. Il push pot-percent non è la size normale 33/75: è la parte di
stack sopra il call divisa per il pot dopo il call. Risulta circa `280,83%`
nel primo nodo e `150%` nel secondo, coerente con `Add all-in if push less than
200% pot`.

Il contratto è stato corretto nel motore generale, non nel solo benchmark:

- `push_increment = stack_before_action - amount_to_call`;
- `pot_after_call = current_pot + amount_to_call`;
- trigger stretto `push_increment / pot_after_call < threshold`;
- `Add` conserva le size normali, `Go` le sostituisce;
- calendario opzionale di size per `raise_count`, serializzato e disponibile a
  ogni configurazione presente o futura; assenza del calendario mantiene il
  comportamento uniforme legacy.

TST dichiara `[33,75]` al primo raise e `[75]` ai successivi. I vecchi layout,
fingerprint, RAM, tempo, dEV e root EV TST sono ritirati come evidenza perché
provenivano da `[33,75]` uniforme, modalità `Go` e base
`stack/current_pot`. Il rounding GTO+ (`5,3/14/47`) resta un gate separato:
GTOSD conserva per ora gli importi exact (`5,28/14,0448/47,112`) e non viene
introdotta una regola di rounding incompleta o specifica della fixture.

Verifica locale: build Release dei target `gtosd_phase1_tests`,
`gtosd_phase3_tests` e `gto_cli` riuscita; i due test mirati passano. Il full
TST aggiornato a 140 iterazioni conferma il catalogo exact
`5,28 -> 14,0448 -> call 8,7648 / raise 41,832 / all-in 74,72`, fingerprint
`fnv1a64:6e04ea2ba28c05d1`, 2.791.872 nodi, 231.129.064 infoset,
582.634.552 azioni e 2.913.172.760 B di stato solver. Il risultato e' FAIL:
dEV 1,625171969%, elapsed 368,6606759 s e root 8,495356429 ante. Il root passa
la tolleranza 0,05 contro 8,50165, ma dEV, tempo e RAM non passano. La build
Release completa e la suite CTest successiva passano 16/16 in 208,37 s; questo
prova l'assenza di regressioni note nella suite, non la parita' TST.

## §8.42 Solver core target-driven e rounding generale (2026-08-13)

Il protocollo di convergenza non usa più `maximum_iterations`. Nel solver core
`iterations=0` è una modalità esplicita senza orizzonte, valida soltanto in
presenza di un target; la suite GTO+ imposta confronto stretto e termina alla
prima certificazione con `Target dEV < 1%`. Pausa, cancellazione ed errore
restano arresti espliciti. Per DCFR+ una scala dinamica power-of-two mantiene i
pesi relativi `t^gamma` senza dipendere da un'iterazione finale futura.

Anche il rounding è nel core: una policy piecewise serializzabile quantizza il
target totale impegnato dell'azione aggressiva, poi ricava il pagamento. TST
seleziona decimi sotto 10 ante e ante intere dopo; nessun codice legge il suo
benchmark ID. Il catalogo prodotto è `Bet 5,3`; dopo la bet `Call 5,3 / Raise
14 / Raise 25`; dopo `Raise 14`, `Call 8,7 / Raise payment 41,7 (raise-to 47) /
All-in payment 74,7 (raise-to 80)`.

Il run Release finale, con certificazione uniforme ogni 20, non si arresta a
1,346487578% @160 e certifica 0,958803314% @180 (stop `converged`, exit 0).
Root EV 8,496602250 contro 8,50165 PASS; elapsed 526,4922531 s, di cui
479,3857023 s traversal e 45,1819157 s certificazione. Solver state
2.913.172.760 B resta FAIL.
Fingerprint aggiornato `fnv1a64:c51f0921903117bf`; nodi, infoset e actions
restano rispettivamente 2.791.872, 231.129.064 e 582.634.552.

Dopo l'ultimo aggiornamento del protocollo, build Release completa PASS e
CTest 16/16 PASS in 199,60 s dopo il relink completo. Il run TST finale e il report
`out/tstc9d_target_driven_final.json` costituiscono l'evidenza operativa.

## §8.43 Certificazione solo dopo l'avvio dell'averaging (2026-08-13)

Il profiler TSTC9D attribuiva 45,1819157 s a nove certificazioni exact BR, sei
delle quali cadevano alle iterazioni 20--120 con `averaging_delay=125`. In quel
tratto `cumulative_strategy` non contiene campioni: una certificazione periodica
non può quindi sostenere un claim sulla strategia media.

La correzione è nel loop generale del solver: una certificazione periodica è
eleggibile soltanto per `iteration > averaging_delay`. Il termine di un run
finito, pausa e cancellazione la forzano comunque. Un test di regressione usa
delay 2 e intervallo 1 e verifica che il primo punto periodico sia l'iterazione
3; non esistono branch per benchmark o board.

L'A/B Release TSTC9D conserva esattamente fingerprint, nodi, infoset, azioni,
iterazioni finali, dEV 0,958803313607%, root EV 8,496602250025 e solver state
2.913.172.760 B. Le certificazioni scendono da 9 a 3; certification time da
45,1819157 a 15,5465126 s, solver time da 526,4922531 a 471,9017814 s
(-54,5904717 s; -10,37%) e wall con preparazione da 567,466928 a 515,036501 s.
Il gate tempo e il gate RAM restano entrambi FAIL. Il report A/B è
`out/tstc9d_post_averaging_cert.json`; una singola misura non sostituisce la
certificazione finale su cinque processi indipendenti.

## §8.44 Esperimento respinto: infoset condivisi con range asimmetrici (2026-08-13)

TSTC9D ha range diversi tra i due player ma individualmente invarianti rispetto
alla simmetria di seme del board. È stato rimosso in prova il fallback che, in
questo caso, conserva gli infoset fisici; `range_automorphisms` aveva già
verificato ogni range separatamente e il public DAG restava disabilitato.

Il differential test Release ha confrontato due iterazioni dello stesso gioco
con layout fisico e con stato suit-isomorphic. La compressione riduceva davvero
infoset e azioni, ma il profile EV divergeva di `0,000248 ante`, contro la
tolleranza lossless `1e-11`. Non è rumore floating-point accettabile e non è
stato allargato il gate. Modifica e test sperimentale sono stati ritirati; resta
il fallback corretto. Una futura implementazione deve modellare esplicitamente
reach e update multiplicity player-local prima di ripetere l'esperimento.

## §8.45 Chiusura RAM con stato packed 13+11 (2026-08-13)

Il vincolo TSTC9D richiedeva meno di 3,81 byte/action; il formato precedente
float24-regret + float16-strategy ne usava cinque. Sono stati testati nel core,
senza branch per fixture, diversi packing da tre byte. `bfloat16 + uint8`
lineare ed `E4M4` strategy non convergevano su AHKHQH; il packing 12+12 passava
AHK/TH ma su TST restava 1,37624% a 200; il 14+10 non convergeva su AHK. Questi
esperimenti sono respinti.

Il compromesso promosso usa regret CFR+ non negativo float13 `E8M5` e strategy
sum float11 `E5M6`, packed little-endian in 24 bit/action; tutta l'aritmetica di
traversal resta float64. I report finali sono `out/ram_final_ahkhqh.json`,
`out/ram_final_th7d6s.json` e `out/ram_final_tstc9d.json`:

| Scenario | dEV | Root EV / GTO+ | Solver state / GTO+ | Esito RAM |
|---|---:|---:|---:|---|
| AHKHQH-101 | 0,982960% @100 | 19,123322 / 19,15 | 2.503.908 / 8.000.000 B | PASS |
| TH7D6S-101 | 0,698573% @100 | 8,223920 / 8,22198 | 249.955.776 / 399.000.000 B | PASS |
| TSTC9D-101 | 0,983565% @200 | 8,498226 / 8,50165 | 1.747.903.656 / 2.000.000.000 B | PASS |

Sono singoli run RAM; tutti i time gate falliscono e non vengono compensati.
Build Release completa e CTest 16/16 passano in 219,94 s dopo il cleanup. La persistenza
packed è coperta da round-trip byte-for-byte e controllo di finitezza.

## §8.46 Checkpoint temporale sul core packed promosso (2026-08-14)

I report autorevoli successivi alla chiusura RAM sono:

| Scenario | Report | Iter / dEV | Root EV / GTO+ | Solver time / limite 90% | Stato / GTO+ |
|---|---|---:|---:|---:|---:|
| AHKHQH-101 | `out/ram_final_ahkhqh.json` | 100 / 0,982960% | 19,123322 / 19,15 | 4,970917 / 1,900000 s | 2.503.908 / 8.000.000 B |
| TH7D6S-101 | `out/compact_no_average_simd3_th7d6s.json` | 82 / 0,986976% | 8,220073 / 8,22198 | 37,810434 / 19,622222 s | 249.955.776 / 399.000.000 B |
| TSTC9D-101 | `out/compact_no_average_simd3_tstc9d.json` | 200 / 0,983565% | 8,498226 / 8,50165 | 690,307523 / 120,600000 s | 1.747.903.656 / 2.000.000.000 B |

dEV, root EV e RAM passano su 3/3; il tempo fallisce su 3/3. I riferimenti
temporali grezzi GTO+ sono 1,71 / 17,66 / 108,54 s. Questi sono run singoli,
non la mediana a cinque processi. La pressione della macchina non spiega il
gap: viene trattato come costo residuo del core.

## §8.47 Loop di micro-ottimizzazione respinto (2026-08-13/14)

Sul checkpoint packed sono state misurate e poi rimosse le seguenti modifiche
generali perché non chiudevano il gate o peggioravano il run:

- reach condiviso per terminali fold/showdown fratelli: TH 37,569693 s a 83
  iterazioni; lieve variazione per iterazione, insufficiente e con ordine di
  somma modificato;
- prefisso rank/card AVX2: TH 38,0529 s, regressione;
- azione check root accodata anziché locale: TH 37,5418 s, nessun vantaggio
  materiale;
- prune dell'average-only a reach zero: TH 37,5724 s, nessun vantaggio;
- reciproco unico nel path packed a tre azioni: 37,2764 s combinato ma
  38,5651 s isolato, quindi non promosso;
- riuso pre-averaging dei bit strategy come regret float24: TH 38,0744 s e
  TSTC9D oltre 900,9 s senza raggiungere il target; regressione netta.

Il profiling inclusivo del traversal TH ha confermato che terminal values,
reach propagation, value/update e regret matching condividono il costo. Nessun
microkernel misurato fornisce il fattore richiesto, soprattutto su TSTC9D.

## §8.48 Isomorfismo con range asimmetrici respinto e chiusura (2026-08-14)

È stato nuovamente rimosso in prova il fallback fisico per range asimmetrici,
aggiungendo domini combo actor/update player-local. Il differenziale a due
iterazioni non è lossless: profile EV fisico `5,03195/-5,03195` contro DAG
`6,68204/-6,68204`, BR `32,4363/14,1074` contro `37,8948/11,6624`, massimo
delta regret `11,8409` e strategy `12`. Modifica e diagnostica temporanea sono
state ritirate; il fallback fisico è ripristinato.

Dopo il ripristino, `gto_cli` e `gtosd_gto_plus_reference_tests` compilano in
Release. Il riferimento passa 24 asserzioni; la regressione asimmetrica passa
con delta regret/strategy zero e il root lock esterno passa. La suite CTest
completa non è stata rieseguita in questa chiusura; il 16/16 precedente resta
evidenza storica distinta.

L'unico passo successivo ad alta priorità è progettare nel core un isomorfismo
range-aware matematicamente corretto con reach, canonicalizzazione e update
multiplicity player-local. Fixture, runner, target dEV, precisione e numero di
iterazioni non devono essere usati come leve prestazionali.

## §8.49 Audit TST: fast path del fallback fisico (2026-08-14)

Un audit successivo ha individuato un intervento più circoscritto da verificare
prima di riaprire il DAG asimmetrico. Quando i range differenti obbligano
`uses_isomorphic_infosets=false`, il layout conserva infoset e blocchi azione
fisici unici; tuttavia `uses_direct_action_bases` resta falso se il board ha più
di un automorfismo. TSTC9D cade in questo caso.

La condizione generale candidata è:

```cpp
uses_direct_action_bases =
    !uses_isomorphic_infosets || automorphisms.size() <= 1;
```

Se il differenziale la conferma, il percorso fisico può riusare action base
dirette, slot contigui `PlayerIndexed`, applicazione immediata dei regret e
assenza dei buffer differiti per-worker. Non cambia fixture, sizing, precisione,
target o numero di iterazioni. La modifica non è ancora implementata e non
esiste un benchmark da promuovere.

## §8.50 DAG asimmetrico successivo non verificato (2026-08-14)

Nel worktree è stato avviato un nuovo candidato che usa slot attivi durante le
trasformazioni e verifica l'uguaglianza dei reach trasformati prima di riusare
un figlio chance. La build è stata interrotta prima della validazione; anche il
test di regressione è stato modificato ma non eseguito. Il candidato non è
quindi evidenza di correttezza o velocità e non sostituisce i report §8.46.

Per essere promosso dovrà modellare reach e update multiplicity player-local e
passare: differenziale fisico/DAG `<=1e-11`, riferimento GTO+ completo, suite
CTest e singoli benchmark isolati con dEV/root/RAM invariati.

## §8.51 Ordine degli interventi e vincolo CPU/RAM-only (2026-08-14)

L'ordine operativo è: fast path del fallback fisico; DAG asimmetrico esatto;
isomorfismo street-local sullo stabilizzatore del board corrente; nuovo
profiling di layout SoA, cache/bandwidth, scheduling e SIMD. Lo scheduling non
viene anticipato perché i precedenti profili TH mostravano già un bilanciamento
ragionevole e le riduzioni strutturali cambieranno il carico da distribuire.

Il solving usa esclusivamente CPU e RAM. GPU, CUDA, ROCm, OpenCL, Vulkan
Compute, DirectCompute e acceleratori equivalenti sono esclusi permanentemente
da tree building, CFR, update, best response, certificazione e analisi. Una GPU
può essere utilizzata soltanto per il rendering della GUI.

## §8.52 Profilo e ottimizzazione TSTC9D exact (2026-08-29)

La sessione ha lavorato esclusivamente sul percorso CPU a otto thread. La
fixture usa gli importi GTO+ osservati, `metadata_complete=true`, DAG canonico,
stato signed `i16/u16` da quattro byte/action e certificazione exact BR. Il
profilo aggregato assegna `27,4%` alla produzione showdown, `22,0%` a
value/update, `19,3%` ad accumulo rank/card, `13,6%` a regret matching, `9,1%`
al prefix, `8,3%` a chance/board e `0,3%` alla reach.

Interventi mantenuti perché lossless e positivi in A/B:

- specializzazione compile-time del terminale fold/showdown paired;
- slot flop compatibili con la chance precalcolati in ordine combo originale;
- `/favor:INTEL64` Release, conservando semantica IEEE e senza `/fp:fast`;
- conteggio dei task attivi e split river soltanto a queue vuota nella coda;
  i child indipendenti sono paralleli, la riduzione resta seriale nell'ordine
  canonico. Sul run DCFR alpha 1,9 / gamma 3 a 160 iterazioni il traversal è
  sceso da `132,257 s` a `130,373 s` (`-1,42%`).

Il differenziale a 20 iterazioni conserva root EV
`8,469189335762426`, dEV `15,752042525516433%` e 29.646.967 nodi. Il miglior
checkpoint lungo della sessione è
`out/tstc9d_tail_river_alpha1_9_iter160.json`: elapsed `136,2380205 s`,
traversal `130,3729854 s`, certificazione `5,4393643 s`, dEV
`1,07842626789941%`, root `8,49066674006746`, stato `1.472.605.376 B`, peak
RSS `1.968.742.400 B`. Non passa dEV né il riferimento grezzo `116,09 s`.

### Esperimenti respinti

| Esperimento | Evidenza/decisione |
|---|---|
| Pinning worker | Nessun guadagno; rimosso dal percorso attivo |
| Average normalizzato one-pass | Più lento e diversa quantizzazione; revert |
| Prefix showdown sparso | Più lento e ordine floating diverso; revert |
| Scratch chance riusabile / nested river globale | Nessun guadagno; revert |
| PGO | Regressione a circa `0,843 s/iter`; revert |
| Asse showdown 32 slot | Bit-identico, `0,7412 s/iter` contro `0,7374`; revert |
| Accumulo cell-major | Bit-identico ma `0,8514 s/iter`, peak `1,973 GB`; revert |
| Tre rami root concorrenti | `1,0216 s/iter` e risultato/nodi diversi nel DAG condiviso; revert |
| DCFR+ a 135 | `174,7290 s`, dEV `1,541924%`; reject |
| HS-DCFR 3.0 a 135 | `124,7644 s`, dEV `2,265369%`; reject |
| DCFR beta 1 a 135 | Tempo `109,4522 s` ma dEV `4,075340%`; reject |
| DCFR gamma 5 a 135 | `122,1837 s`, dEV `1,263294%`; reject |
| DCFR gamma 2 a 135 | `125,7271 s`, dEV `1,532120%`; reject |

Il goal di passare TSTC9D è sospeso su richiesta dell'utente. Non è stata
eseguita la campagna finale a cinque processi. Il collo successivo resta la
famiglia showdown (~55,8%) insieme al tail imbalance (CPU media ~86% sul run
lungo); non sono autorizzati sampling, GPU, fast-math o modifiche del gioco.

Il run finale di chiusura a 170 iterazioni
(`out/tstc9d_session_final_alpha1_9_gamma3_iter170.json`) converge con dEV
`0,985759519455115%`, root `8,49254003503441`, stato `1.472.605.376 B` e peak
RSS `1.968.537.600 B`. Elapsed `153,1763512 s`: inizializzazione `0,5738847`
(`0,37%`), traversal `147,0631715` (`96,01%`), regret application `0,0001314`,
certificazione `5,5051254` (`3,59%`) e finalizzazione `0,0015453`. Il wall con
preparazione è `179,7229091 s`; i `26,5465579 s` di tree preparation sono
fuori dal timer solver. Correttezza numerica e memoria passano; il tempo
`116,09 s` e il limite `128,988889 s` falliscono. Il run è singolo e non viene
presentato come mediana/p95.

## §8.53 P0 — current strategy signed corretta (2026-08-29)

**Decisione: ACCEPT come fix di correttezza, non come ottimizzazione.**

Sul commit iniziale `8b2d025084cfedc0fc600fde999ca9706100ebe5`, il percorso
puntuale `current_strategy(CanonicalPublicNode, ..., average=false)` leggeva i
codici regret DCFR `uint16_t` senza reinterpretarli semanticamente come
`int16_t`. I regret negativi diventavano quindi valori positivi molto grandi e
rendevano invalida la certificazione diagnostica della current strategy.

Il fix introduce primitive condivise di regret matching signed per il percorso
scalare e per il blocco action-major AVX2. Entrambi applicano
`max(0, int16(raw_code))`; il caso senza regret positivo resta uniforme. Il
traversal DCFR, gli schedule, la quantizzazione e l'average strategy non sono
stati modificati.

Regression test Phase 7:

- due azioni: `[-10,+5]`, `[-10,-5]`, `[+5,+15]`;
- tre azioni: `[-20,+10,+30]`, `[-20,-10,-1]`;
- arity generica a quattro azioni;
- nove mani action-major, quindi otto lane AVX2 più tail scalare, confrontate
  con oracle scalare.

Validazione Release mirata:

- `gtosd_phase7_tests`: PASS, 179 assertion;
- `gtosd_gto_plus_reference_tests`: PASS, 24 assertion;
- serial/parallel: delta regret e strategy `0`;
- asymmetric-range node-owned ISO: PASS, 165.774 -> 46.065 public nodes.

La curva current-vs-average precedente per DCFR standard resta ritirata. Va
rigenerata con il binario corretto prima di attribuire il costo di convergenza
alla dinamica regret o all'averaging.

## §8.54 P0.5 — current vs average dopo il fix signed (2026-08-29)

**Decisione: ACCEPT della misura; mantenere l'average come output canonico e
non aprire uno sweep dei parametri di averaging.**

Sono state create copie diagnostiche non versionate delle tre fixture, tutte
con DCFR, backend signed `ScaledUint16RegretStrategy`, averaging delay zero e
certificazione ogni 20 iterazioni. Current e average leggono checkpoint della
stessa trajectory: a ogni checkpoint condiviso i nodi visitati coincidono
esattamente.

| Benchmark/checkpoint | dEV average | dEV current | Profile EV CO/BTN average | Profile EV CO/BTN current | BR CO/BTN average | BR CO/BTN current |
|---|---:|---:|---:|---:|---:|---:|
| AHKHQH @80 | 0,685946% | 3,980414% | -0,889317 / +0,889332 | -0,974296 / +0,974345 | -0,669381 / +1,163710 | +0,617870 / +2,400010 |
| TH7D6S @80 | 0,805130% | 1,473611% | -1,278675 / +1,278675 | -1,278937 / +1,278937 | -1,139060 / +1,431650 | -1,039470 / +1,558923 |
| TSTC9D @170 | 0,985760% | 2,389411% | +0,492540 / -0,492541 | +0,494339 / -0,494341 | +0,650262 / -0,399162 | +0,876644 / -0,273671 |

La current migliora in tendenza ma oscilla: TH passa da 1,473611% @80 a
7,159870% @100 e 1,853445% @120; TST passa da 3,112631% @160 a 2,389411%
@170. L'average è migliore a ogni checkpoint condiviso sui tre benchmark e
raggiunge `<1%` a 80/80/170. Il costo in iterazioni non è quindi causato da un
average che nasconde una last iterate già convergente; l'average stabilizza una
dinamica regret ancora rumorosa.

Le run con certificazioni multiple non sono timing candidate: sul TST average
il traversal è 173,216277 s e le otto certificazioni costano 61,841400 s. Il
run ufficiale con sola certificazione finale resta l'autorità temporale.

I report grezzi sono conservati fuori dal repository in
`next-optimization/reports/*-p05.json`; contengono per ogni checkpoint dEV,
NashConv, profile EV, BR per player, work counter, traversal e certificazione.

## §8.55 P1 — scale churn, strategy density e showdown reuse (2026-08-29)

**Decisione: ACCEPT della telemetria; aprire P2 con aspettativa limitata e
non introdurre ancora una cache showdown.**

La build `windows-profile-current` espone contatori read-only sotto
`GTOSD_ENABLE_HOTPATH_PROFILE`; il binario Release di timing compila via tutte
le scansioni diagnostiche. La telemetria distingue scale bit-identiche,
variazioni relative, overflow della scala esistente, entry ricodificate,
densità della strategia, pruning esatto e workload showdown. Il fingerprint
della reach usa tutti i bit IEEE delle entry avversarie nel dominio locale
corretto e misura soltanto candidati di reuse: nessun valore è riusato.

Smoke TSTC9D DCFR signed, due iterazioni, otto thread. Sommando i due pass del
secondo aggiornamento:

- scale: 902.106 check, 290.529 bit-identiche, 723.052 rescale richiesti;
  409.655.611 delle 507.046.140 entry ricodificate (80,79%) appartengono a
  nodi che eccedono la scala esistente;
- density: 536.333.425 entry, 124.900.139 esattamente zero (23,28%), 2.114
  positive sotto `1e-4`, 266.469 action interamente zero;
- pruning: 194.027 action/subtree saltati, 51.134.555 entry di azione;
- showdown: 891.296 chiamate, 500.747 fingerprint ripetuti (56,18%), ma con
  forte asimmetria: 495.971/586.980 nel primo pass e 4.776/304.316 nel
  secondo. Il dato è un upper bound hash-based worker-local, non autorizza
  ancora una cache senza exact equality e A/B RAM.

Il run diagnostico termina intenzionalmente a iterazione 2 e fallisce il
target dEV; non è una misura di convergenza né un timing candidate. Il report
grezzo è `next-optimization/reports/tstc9d-p1-telemetry.json` fuori repo.
Build Release e profile dei target interessati PASS; Phase 7 PASS con 179
assertion e reference GTO+ PASS con 24 assertion, incluso delta
seriale/parallelo zero e test ISO asimmetrico.

La persistent scale può evitare un cambio di scala per circa il 19% delle
entry osservate, ma deve comunque aggiornare i codici delle entry i cui regret
cambiano. P2 viene quindi prototipata dietro flag e sarà respinta se il costo
end-to-end non migliora: il solo rapporto di scale non dimostra uno speedup.

## §8.56 P2 — persistent node scale (2026-08-29)

**Decisione: REJECT; il percorso sperimentale è stato rimosso.**

Il prototipo nello stesso binario conservava la scala precedente soltanto se
il massimo aggiornato era ancora rappresentabile; ogni overflow ricadeva nel
calcolo canonico. Non poteva evitare l'encoding delle entry modificate e
produceva una trajectory quantizzata leggermente diversa, pur conservando
layout, fingerprint, stato e gate locali.

A/B alternato TSTC9D a 20 iterazioni, Release, otto thread:

| Run | Traversal baseline | Traversal persistent | Elapsed baseline | Elapsed persistent |
|---|---:|---:|---:|---:|
| A | 16,267434 s | 15,953429 s | 22,316779 s | 21,947550 s |
| B | 18,073703 s | 18,894711 s | 24,876982 s | 25,060325 s |
| media dei due | 17,170568 s | 17,424070 s | 23,596880 s | 23,503937 s |

Il traversal medio peggiora dell'1,48%; l'elapsed migliora dello 0,39%, entro
il rumore e contraddetto dal run B. A iterazione 20 il dEV passa da
16,817241% a 16,809043%, root da 0,445935759 a 0,445958535 ante e i nodi da
29.490.340 a 29.487.706: differenze compatibili con la diversa quantizzazione,
ma senza vantaggio prestazionale ripetibile. Peak RSS resta circa 1,969 GB e
`solver_state_bytes` 1.472.605.376 B.

I report grezzi `tstc9d-p2-{baseline,persistent}-{a,b}.json` sono conservati
fuori repo. Poiché lo smoke non passa il gate prestazionale A/B, non vengono
eseguiti 80/100 o full con questo candidato. Il successivo costo misurato da
affrontare è la famiglia showdown; prima di una cache serve però verificare
exact equality oltre al fingerprint e stimare il costo RAM bounded.
