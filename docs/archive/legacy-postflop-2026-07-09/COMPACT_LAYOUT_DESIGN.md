# Design: layout compatto (§5) — verso il <90 s

> **STATO: DESIGN STORICO / NON APPROVATO.** Alcune varianti SIMD e di layout
> descritte sono state provate e revertite; non rappresentano codice corrente.
> Il layout solver resta in RAM e viene elaborato esclusivamente dalla CPU;
> questo design non prevede né autorizza storage o kernel GPU.

**Data**: 2026-08-06 · **Stato**: DESIGN (da approvare prima dell'esecuzione)
**Riferimento**: `docs/ARCHITECTURAL_REWRITE_PLAN.md` §5 (oggi 5 righe) + misure della sessione.
**Obiettivo**: dimezzare il tempo di `run_solver` th7d6s (~164 s → <90 s) attaccando il collo
strutturale: la latenza DRAM degli accessi sparsi allo stato.

---

## 1. Il collo, con i numeri misurati

| Metrica | Valore | Note |
|---|---|---|
| `run_solver` (miglior pulito) | **164.0 s** | traversal 150.3 + cert 12.7 + regret 7.8 |
| traversal / pass | ~0.54 s wall a pool ~2× | 280 pass (140 iter × 2 giocatori) |
| accessi sparsi allo stato | **~62 M/pass** | regret+strategy per-combo, slot sparsi |
| bandwidth DRAM | ~20 GB/s | DDR4 dual-channel, i3-10100F |
| pool | cap ~2× | saturazione bus/latenza |
| GTO+ | 17.66 s | 9.3× più veloce, 4.79 B/azione |

**Diagnosi**: il collo NON è il calcolo (le divisioni sono SIMD da Fase O1, il value-loop è
fuso) — è la **latenza degli accessi casuali** ai buffer regret/strategy (float32, 333 MB
ciascuno): ogni decisione tocca i blocchi azione delle sue combo live, che sono sparsi
(~1 ogni 4 slot). Il parallelismo non riduce la latenza; la riduzione del traffico (B/C)
è fallita al gate di precisione. L'unico lever strutturale è il **riordino dei dati + la
vettorizzazione sistematica** del layout.

## 2. Stato attuale del layout (cosa è GIÀ compatto)

- **Azioni per (decision, combo) contigue**: `decision_action_base = action_base + local ×
  count` (path direct) — i blocchi `[offset, offset+count)` sono contigui nel flat array.
- **Spazi per-giocatore (R1-full)**: `PlayerIndexed` — le combo del giocatore occupano
  prefissi contigui `0..player_flop_count[p]-1`; `value_slot` è una lookup diretta.
- **Batch SIMD strategie count==2** (divisioni): −21% run, bit-exact (permute 0xD8).
- **Value-loop SIMD fuso**: contiguo, bit-exact.
- **Coda condivisa pull-based** (Fase D sub-step 1): teardown corretto, bit-exact.
- **NON compatto**: l'iterazione è **combo-major** (le combo live sparsi per nodo); i
  buffer regret/strategy sono due array separati (niente struttura infoset); le reach sono
  copie double per edge (4.5 KB).

## 3. Le modifiche progettate (in ordine di rapporto valore/rischio)

### 3.1 Fase F1 — Update SIMD sistematico (erede della Fase C)

La Fase C (misurata, poi revertita su decisione utente) ha provato il concetto:
**4 combo × 2 azioni per gruppo**, action-base contigue, `__m256` load/store dei buffer
regret/strategy, aritmetica per-slot identica allo scalare.

- **Misura C**: deriva dEV full **1.2e-9** (800× sotto il gate 1e-6); AhKhQh bit-exact;
  guadagno atteso ~1-2% (non verificabile sotto carico).
- **Estensione F1**: count 2 **e 3-4** (le basi contigue `local × count`); grouped loop
  con fallback scalare (safety check già implementato); nessun cambiamento di formato.
- **Gate**: A/B a macchina **idle** (best-of-3): solo se traversal < 150.3 s viene tenuto.
  La deriva 1e-9 è documentata e accettata (entro gate); AhKhQh resta bit-exact.

### 3.2 Fase F2 — Iterazione infoset-major per decisione

Oggi il loop di una decisione è combo-esterno (per ogni combo live, le sue 2.5 azioni).
Con F1, il gruppo SIMD percorre le combo a blocchi di 4. L'**infoset-major** spinge oltre:
per ogni decisione, iterare le **azioni come stream contigui** `regret[base..base+n·count)`
con lo skip dei blocchi bloccati gestito dal gruppo (mai per-slot).

- **Cosa cambia**: solo la struttura del loop dell'update (la matematica per-slot identica).
- **Impatto atteso**: accessi ai buffer più sequenziali (DRAM row-buffer friendly) +
  il SIMD di F1 applicato su tutta la riga della decisione.
- **Rischio**: basso (stessa aritmetica, riordino dei soli accessi).

### 3.3 Fase F3 — Reach e valori: opzioni con gate (eredi di B)

- **Reach float32**: **FALLITA** (deriva 6.9e-5 = 69× il gate) — **esclusa** definitivamente
  (journey §8.7). Le copie reach restano double.
- **Valori (ComboVector) float32**: stessa famiglia di precisione — il value-loop è già
  SIMD fuso e marginale (0.06 s/pass): **non conviene** rischiare il gate.
- **Averaging con accumulatore doppio** (piano §5): `add_strategy` accumula già in float32;
  un accumulatore double + flush periodico cambierebbe la bit-exactness (deriva da misurare)
  — **solo se il 3.1+3.2 non bastano**, con gate ≤ 1e-6.

### 3.4 Fase F4 — Prefetch software sulle liste combo

Le liste `board.player_combos[player]` (206-358 combo live per decisione) sono note a
priori: `_mm_prefetch` dei blocchi azione dei prossimi gruppi mentre si elabora il gruppo
corrente. **Impatto**: riduce la latenza percepita degli accessi sparsi (~la metà del
guadagno di F1). **Rischio**: basso; il prefetch può essere rimosso se neutro.

## 4. Impatti attesi (stima onesta)

| Fase | Estensione | Traversal atteso | Cumulativo |
|---|---|---|---|
| attuale | — | 150.3 s | 164 s |
| F1 | update SIMD count 2-4 | −3-8 s | ~158 s |
| F2 | infoset-major | −5-15 s | ~150 s |
| F4 | prefetch | −5-10 s | ~140 s |
| (limite realistico delle sole fasi F) | | | **~135-145 s** |

**Il <90 s richiede di più**: le fasi F portano a ~135-145 s (1.15-1.2×) — ancora 1.5× dal
target. Il salto restante richiede una delle seguenti (tutte con rischio alto e impatto
incerto sul collo latenza):
- **Stato a metà precisione** dove il gate lo permette (solo parti dello stato — misura
  per-fase, come B ma circoscritta);
- **Ristrutturazione del valore** (evitare il materializzare le reach per edge — undo-log
  per-worker già provato e revertito: stesso traffico senza float32);
- **Parallelismo per partizioni infoset** (Fase D completa: ogni thread possiede blocchi
  disgiunti — il merge eliminato, ma il collo latenza resta).

**Raccomandazione**: eseguire F1+F2+F4 (basso rischio, ~135-145 s atteso), poi ricalibrare
con i dati reali prima di impegnare i giorni della riscrittura profonda.

## 5. Vincoli e gate (invariati dal piano)

1. Protocollo intatto: 140 iterazioni, cert ogni 20, `parallel_action_depth` 5, target 1%.
2. Precisione: stato float32/compute float64; ogni deroga con misura esplicita ≤ 1e-6;
   bit-exact preferito.
3. Determinismo: stesso dEV a parità di build; il parallelismo preserva l'ordine.
4. Expected layout invariato (info_sets 36,596,832 / actions 83,318,592); se cambia,
   fixture con review.
5. Ogni fase si chiude con: build → smoke bit-exact (o deriva ≤ gate) → full (dEV ≤ 1e-6,
   converged, correctness, layout_matches) → AhKhQh 101/103 (±1e-6) → suite → journey.

## 6. Criteri di accettazione finali

- `elapsed_seconds` th7d6s **< 90 s** (best-of-3, macchina idle).
- `final_gto_plus_dev_percent` ≈ 0.9249962879022555 ± 1e-6.
- `converged`, `correctness_passed`, `layout_matches_fixture` tutti true.
- AhKhQh 101/103: 0.6741554018356799 ± 1e-6.
- Suite completa PASS. Journey aggiornato.

## 7. Ordine di esecuzione consigliato

1. **A/B idle della baseline attuale** (164 s, 3 run) — la linea di riferimento pulita.
2. **F1** (SIMD count 2-4) → A/B idle → tenere solo se < 150.3 s.
3. **F2** (infoset-major) → A/B idle.
4. **F4** (prefetch) → A/B idle.
5. Ricalibrazione: se < 90 s non raggiunto con F1-F4, valutare la riscrittura profonda
   (stato misto precisione / valore single-pass) con una stima aggiornata.

---

## Appendice: dati di riferimento della sessione

- Baseline: 273.6 s → attuale 164.0 s (bit-exact, journey §1-§8).
- Fase C (SIMD): deriva 1.2e-9, revertita (§8.8 — riapplicabile come F1).
- Fase B (reach float32): deriva 6.9e-5, revertita (§8.7 — esclusa).
- Pool: 1→5 = 1.95×, 1→7 = 1.98× (cap ~2×, bandwidth/latenza).
- RAM: stato 667 MB (regret+strategy float32), picco 868 MB.
