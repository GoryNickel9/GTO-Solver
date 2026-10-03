# Analisi memoria GTO+ (399MB) vs GTOSD (667MB stato / 868MB picco)

> **CORREZIONE SEMANTICA 2026-09-04 — REPORT STORICO / NON NORMATIVO.** I
> valori GTO+ citati provengono da “Memory needed for solving” e non sono Peak
> RSS né, senza prova ulteriore, `solver_state_bytes`. I confronti numerici e i
> PASS/FAIL memoria di questo report non dimostrano parità; breakdown, profili e
> misure grezze restano evidenza tecnica. Vedere il
> [`piano di correzione`](../../GTO_PLUS_SOLVER_MEMORY_SEMANTICS_AND_GATE_CORRECTION_PLAN_2026-09-03.md).

> **AGGIORNAMENTO OPERATIVO 2026-08-14.** Il problema `solver_state_bytes` è
> chiuso sui tre benchmark mediante il formato core packed 13+11 da 3
> byte/action. TH7D6S usa 249.955.776 B contro 399.000.000 B; AHKHQH usa
> 2.503.908 B contro 8.000.000 B; TSTC9D usa 1.747.903.656 B contro
> 2.000.000.000 B. Questi confronti sono ora classificati non comparabili. Tutti
> conservano dEV <1% e root correctness. I tempi
> 4,970917 / 37,810434 / 690,307523 s falliscono i rispettivi limiti e non sono
> compensati dalla RAM. Peak RSS e transient workspace restano metriche
> separate. L'analisi float32 seguente resta storica.
> Tutte le soluzioni di memoria restano CPU/RAM-only: non è ammesso spostare
> stato, traversal o certificazione su GPU.

> **STATO: ANALISI STORICA.** I numeri appartengono al benchmark
> `GTP-TH7D6S-101` del 2026-08-06 e non sono la baseline corrente di
> `GTP-AHKHQH-003`.

**Data**: 2026-08-06 · **Benchmark**: `GTP-TH7D6S-101` (short deck HU postflop, flop Th7d6s)
**Riferimento GTO+** (dalla fixture): `elapsed_seconds: 17.66`, `solver_memory_bytes: 399000000`
**Stato attuale GTOSD**: 164.0s (miglior run pulito), stato 667MB, picco RSS 868MB.

---

## 1. Configurazione del benchmark (identica per entrambi i solver)

Dalla fixture `benchmarks/fixtures/gto_plus_th7d6s_101.json`:

| Parametro | Valore |
|---|---|
| Variante | short_deck_hu_postflop (deck 36 carte, 6→A) |
| Flop | Th 7d 6s |
| Pot iniziale | 19 antes |
| Stack effettivo | 92 antes |
| Bet size | 50% pot |
| Raise size | 50% pot |
| Max raises/street | 4 |
| All-in automatico | disabilitato |
| Rake | 0 |

## 2. L'albero di GTO+ (ricostruito dallo screenshot dell'utente)

Lo screenshot mostra la vista "solo flop" dell'albero di GTO+. Ricostruzione via OCR
(Windows.Media.Ocr) + bounding box:

```
p 100%  (root — pot)
├── Bet 9.5 ───────────►  (BTN risponde)
│       ├── Fold
│       ├── Call
│       └── Raise 28 ──►
│               ├── Fold
│               ├── Call
│               └── Raise 65 ──►
│                       ├── Fold
│                       ├── Call
│                       └── Raise 92  (= all-in, stack 92)
└── Check ─────────────►  (turn: stessa struttura: Bet 9.5 / Check / Raise 28 / ...)
```

Le azioni rilevate: **Bet 9.5, Check, Raise 28, Raise 65, Raise 92, call, fold**.
Con pot = 19 e stack = 92: bet 9.5 = 50% pot; raise 28 → 65 → 92 (all-in) con 50% pot
e max 4 raise/street.

## 3. Il nostro albero è IDENTICO

La nostra fixture ha la stessa configurazione e i reference_nodes confermano le stesse
azioni:

- `flop_co_root` (CO): `check 0.951 / bet_9 0.0491`
- `flop_btn_after_co_bet_9` (BTN): `fold / call_9 / raise_28`
- poi `raise_65` → `raise_92` (all-in a 92 = stack)

**Conclusione: GTO+ e GTOSD risolvono lo stesso albero → lo stesso spazio delle azioni**
(~83,318,592 azioni, 36,596,832 infoset — i valori della nostra fixture, verificati).

## 4. Il conto decisivo: byte per azione

| | GTO+ | GTOSD |
|---|---|---|
| "Memory needed for solving" | 399 MB | 667 MB (stato) |
| Byte/azione | **4.79** | **8.00** (float32 × 2: regret + strategy media) |

399MB ÷ 83.3M azioni = **4.79 byte/azione** — non un multiplo di 4, e meno di 8.
GTO+ **non** memorizza 8B/azione come noi.

## 5. Ipotesi sulla rappresentazione di GTO+ (coerente con 4.79 B/azione)

| Componente | GTO+ (ipotesi) | GTOSD |
|---|---|---|
| Regret | 4 B/azione (float32) | 4 B/azione (float32) |
| Strategy media | **non memorizzata per-azione durante il solve** | 4 B/azione, accumulata a ogni iterazione (333MB) |
| Metadata infoset | ~66MB (36.6M infoset × ~1.8B) | nel layout (separato) |
| **Totale** | **~399MB** | **667MB** |

La spiegazione più plausibile: GTO+ **deriva la strategy corrente per regret-matching**
(non la accumula) e calcola la media a fine run (o solo per i nodi riportati). Questo
spiega sia i 4.79 B/azione sia l'etichetta "Memory needed for **solving**".

Alternative (da escludere con i dati a disposizione):
- **n−1 normalizzato** (strategy: n−1 valori per infoset): NON risparmia — la
  `strategy_float32` accumula valori **pesati** (`weight × reach × σ`), non normalizzati;
  per ricostruire l'ultima azione servirebbe il totale pesato per infoset = stessa
  dimensione del valore eliminato. E i regret non hanno la proprietà somma-zero
  (Σₐ r(a) = Σ v(a) − n·v(σ) ≠ 0 in generale) → n−1 non applicabile.
- **float16**: precisione ~1e-3 relativa, fuori dal gate dEV (1e-6). Escluso.

## 6. Cosa significherebbe replicare i 399MB in GTOSD

Il nostro `solver_state_bytes` = 667MB è **il contratto della fixture**
(`expected_layout.solver_state_bytes: 666,548,736`) e la **certificazione di convergenza**
(ogni 20 iterazioni) legge la strategy media accumulata. Per arrivare a ~399MB:

1. **Non accumulare la strategy media per-azione** durante il solve (−333MB
   → stato ≈ 399MB con metadata);
2. **Ricalcolare la media a fine run** (passata finale) — ma la certificazione
   intermedia non potrebbe più usarla: bisognerebbe certificare sulla strategy
   corrente (regret-matching) o con un criterio diverso;
3. **Conseguenze**: il dEV finale non sarebbe più `0.9249962879022555` (bit-exact);
   la semantica di convergenza cambia; la fixture va aggiornata
   (`solver_state_bytes` e possibilmente il criterio di certificazione).

**Questa è una modifica di protocollo, non un'ottimizzazione locale.**

## 7. Opzioni

| Opzione | Stato | RAM | Bit-exact | Costo |
|---|---|---|---|---|
| **A. Mantenere il formato attuale** (regret + strategy float32) | fixture intatta | 667MB stato / 868MB picco | sì | 0 |
| **B. Ricalcolo media a fine run** (regret-only durante il solve) | richiede autorizzazione fixture | ~399-450MB | **no** (dEV da rinegoziare, certificazione da ridisegnare) | giorni, rischio alto |
| **C. Concentrarsi sul timing (<90s, Fase D)** | compatibile con A | 667/868MB | sì | Fase D (in corso) |

**Raccomandazione**: A + C. La RAM 667MB è il pavimento del formato verificato e la
differenza con GTO+ è una scelta di rappresentazione non replicabile senza cambiare
il contratto. Il collo reale del benchmark è il **timing** (164s vs 17.66s = 9.3×).

## 8. Riferimenti

- Fixture: `benchmarks/fixtures/gto_plus_th7d6s_101.json` (+ `_smoke.json`)
- Piano: `docs/archive/legacy-postflop-2026-07-09/ARCHITECTURAL_REWRITE_PLAN.md` (roadmap A–F)
- Journey: `speed_optimization_journey.md` (§8.3 principio 399MB, §8.4 RAM, §8.5 analisi)
- Screenshot: `.reasonix/attachments/clipboard-20260806-225859.895719-000001.png`
- Dati misurati: `out/th7d6s_current.json` (164.0s, peak 867,954,688)
