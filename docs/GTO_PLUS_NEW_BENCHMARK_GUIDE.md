# Guida: come introdurre un nuovo benchmark di convergenza GTO+

Il benchmark v3 è guidato dalla specifica JSON, ma la creazione di nuovi
benchmark ufficiali è **sospesa fino alla migrazione v4 della memoria**. Il
campo v3 `peak_rss_bytes` è semanticamente errato: contiene “Memory needed for
solving”, non Peak RSS. Una copia del template può essere usata per prove di
correttezza e tempo, ma non per pubblicare un PASS/FAIL memoria. La struttura v4
prevista è descritta nel
[`piano di correzione`](GTO_PLUS_SOLVER_MEMORY_SEMANTICS_AND_GATE_CORRECTION_PLAN_2026-09-03.md).

> I valori del template **non vanno lasciati così**: `benchmark_id`, flop, range, pot/stack,
> size, EV/frequenze dei nodi e `expected_layout` vanno adattati allo scenario.
> Il template è congelato come base — i fixture veri si chiamano `gto_plus_<board>_<nnn>.json`.
>
> Ogni run comparativo usa il backend solver CPU/RAM-only. CPU, thread e RAM
> devono essere riportati; una GPU non può partecipare al solve né essere usata
> per ottenere il tempo dichiarato.

---

## 1. Convenzione di nome

`benchmark_id`: `GTP-<BOARD>-<NNN>`

- `<BOARD>` = 2+ caratteri alfanumerici maiuscoli, es. `AHKHQH` (le carte del flop) o il nome dello scenario.
- `<NNN>` = 3 cifre. `003` = v1 congelata (non toccare). `101+` = v3.
- Nome file: `benchmarks/fixtures/gto_plus_<board>_<nnn>.json`.

## 2. Campi da compilare — `fixture`

| Campo | Significato | Note |
|---|---|---|
| `flop` | Le 3 carte del flop | Formato `["Ah","Kh","Qh"]`; carta valida `^[AKQJT9876][shdc]$` (mazzo corto 6-A). |
| `initial_pot_antes` | Pot iniziale in ante (già postato) | GTO+: 40. |
| `effective_stack_antes` | Stack effettivo in ante | GTO+: 100 (40+20+20... vedi sopra: 100 dopo il preflop). |
| `bet_size_percent_pot` | Bet size in % del pot | GTO+: 50 → bet da 20 ante. Singolo intero **oppure array** per più size, es. `[33, 75]`. |
| `raise_size_percent_pot` | Raise size in % del pot | GTO+: 50 → raise da 60 ante. Singolo intero **oppure array**, es. `[33, 75]`. |
| `raise_size_percent_pot_by_raise_count` | Calendario opzionale delle size per profondità | Array con una entry per ogni raise consentito. L'indice zero è il primo raise. Esempio `[[33,75],[75],[75],[75]]`. Se assente, ogni profondità riusa `raise_size_percent_pot`. |
| `maximum_raises_per_street` | Profondità raise consentita per strada | GTO+: 1. |
| `automatic_all_in` | Regola all-in | `"disabled"`; `"add_if_push_below_<N>_percent_pot_after_call"`; oppure `"go_if_push_below_<N>_percent_pot_after_call"`, con N 1–1000. Il push è `(stack-call)/(pot+call)`: `Add` conserva le size normali, `Go` le sostituisce quando la soglia scatta. |
| `automatic_all_in_strict_boundary` | Confronto `<` vs `<=` alla soglia | Per la dicitura GTO+ “less than” usare `true`. |
| `final_bet_smoothing` | Smoothing dell'ultima bet | `"disabled"`. |
| `rake_percent` | Rake | GTO+: 0. |
| `range_co` / `range_btn` | Range dei due giocatori (notazione hand class, es. `AA-QQ,AKs-AQs,KQs,AKo-AQo,KQo`) | Formato GTOSD **e** export GTO+ con pesi: `[73.0]77[/73.0]` (peso in %, per singola hand class, 0–100; `[0.0]X[/0.0]` esclude la classe). I pesi diventano `RangeWeight` per combo. |

## 3. Campi da compilare — `gtosd_run`

| Campo | Significato |
|---|---|
| `maximum_iterations` | **Non ammesso.** Il solver core usa la modalità target-driven senza limite di iterazioni e termina solo alla prima certificazione con dEV strettamente inferiore a `target_dev_percent`, oppure per pausa, cancellazione o errore reale. |
| `certification_interval` | Ogni quante iterazioni si certifica (20). |
| `averaging_delay` | Iterazioni prima di iniziare la media (20). |
| `parallel_action_depth` | Profondità dell'albero parallelo (5). |
| `maximum_solver_threads` | Thread GTOSD (6; GTO+ usa 8 — il tempo GTOSD è misurato onesto). |
| `independent_processes` | Quante run indipendenti per il benchmark (5, regola PERFORMANCE.md). |
| `timer_scope` | `"run_solver"` (solo il tempo del solver, confrontabile con GTO+). |

## 4. Campi da compilare — `gto_plus_reference` (i valori GTO+ esterni)

| Campo | Significato |
|---|---|
| `elapsed_seconds` | Tempo GTO+ per il target (1.71). Usato come base del gate speed. |
| `convergence_trace` | Se disponibile, sequenza temporale osservata `elapsed_seconds`, `dev_percent`, `dev_antes`; preserva i valori mostrati da GTO+ senza interpolazione. |
| `first_strictly_below_target` | Primo punto osservato con `dev_percent < target_dev_percent`; deve coincidere con `elapsed_seconds`. |
| `peak_rss_bytes` | **Legacy v3, nome errato.** Conserva il valore normalizzato del display GTO+ “Memory needed for solving”; non è Peak RSS e non deve decidere un gate. |
| `memory_unit` | **Legacy v3.** Registra la convenzione di normalizzazione del display, non prova la semantica della metrica. |
| `target_dev_percent` | dEV target GTO+ in %; per questa suite deve essere `1.0` e il confronto è strettamente `<`. |
| `target_definition` | Definizione del dEV (stringa, va lasciata/adeguata). |
| `target_provenance` | Da dove arriva il riferimento (versione GTO+, macchina, run). |
| `timing_scope` | Cosa misura `elapsed_seconds`. |
| `metadata_complete` | `false` finché non hai l'action tree completo (turn/river); **non blocca i gate**. |
| `gate_node` | **Nodo che decide la correttezza** (default: root, es. `flop_co_root`). Lascialo al root: è l'unico EV confrontabile in modo incondizionato. |
| `ev_absolute_tolerance_antes` | Tolleranza EV assoluta in ante (0.05). |
| `action_frequency_absolute_tolerance_fraction` | Tolleranza frequenze (0.01). |
| `display_precision_percent` | Precisione di visualizzazione (0.1). |
| `reference_nodes[]` | I nodi di confronto. Ogni nodo: `id` (**etichetta arbitraria**, regex `^[a-z0-9_]+$`, il template usa nomi generici tipo `flop_btn_after_co_bet` — è il `path` a identificare il nodo, non l'id), `path` (etichette azioni dal root, es. `[]`, `["check"]`, `["bet_20"]`, `["check","bet_20"]`), `player` (0=CO, 1=BTN), `ev_antes` (EV condizionale GTO+), `actions` (frequenze opzionali key=etichetta azione). |

Per una nuova acquisizione conservare separatamente, anche prima che la v4 sia
implementata: label esatta, valore visualizzato, unità, numero di decimali,
versione GTO+ e screenshot/provenienza. Non inferire byte esatti oltre la
precisione del display e non copiare il valore da un progetto con albero solo
apparentemente simile.

Etichette azioni (convenzione report): `check`, `bet_20`, `fold`, `call_20`, `raise_60` —
tipo di azione + importo in ante se presente. Devono combaciare con quelle dell'action tree
GTOSD: il modo più sicuro è copiarle dal report di una run di prova (vedi passo 5). **Gli id
dei nodi sono generici** (`flop_btn_after_co_bet`, non `flop_btn_after_co_bet_20`): se cambi
bet size nel fixture, cambia solo il `path`, non l'id.

## 5. Workflow in due passi

**Passo A — run di prova** (calcola il layout GTOSD e i delta EV):

```bash
./out/build/windows-release/apps/gto_cli/gto_cli.exe postflop benchmark-gto-plus \
    benchmarks/fixtures/gto_plus_<board>_<nnn>.json out/gtp_<nnn>.json
```

**Passo B — congela il layout**: copia nel campo `expected_layout` i valori stampati dal
report (`game_fingerprint`, `physical_public_nodes`, `canonical_public_nodes`,
`information_sets`, `actions`, `solver_state_bytes`). Da questo momento il fixture è
**autocertificante**: se l'action tree cambia, la fingerprint cambia e il gate fallisce.

### Come compilare `expected_layout` (in dettaglio)

L'`expected_layout` non lo calcoli a mano: lo **produce il run di prova** (Passo A). Il report
JSON scritto da `postflop benchmark-gto-plus` contiene i campi reali a livello di top-level
(stessi nomi del fixture):

| Campo (fixture + report) | Significato | Da dove viene |
|---|---|---|
| `game_fingerprint` | `fnv1a64:<hex>` — impronta dell'intero action tree (config + board + dimensioni) | Calcolata dal CLI; **qualsiasi cambio all'albero la cambia** |
| `physical_public_nodes` | Nodi pubblici dell'albero fisico (senza canonizzazione) | `tree-inspect` o report |
| `canonical_public_nodes` | Nodi pubblici del DAG canonico (0 se il flop non ha automorfismi, es. board rainbow) | Report |
| `information_sets` | Numero totale di infoset (union range × nodi decisionali) | Report |
| `actions` | Numero totale di slot azione (regret+strategy) | Report |
| `solver_state_bytes` | Byte di stato persistente (checkpoint) | Report |

Procedura per un benchmark nuovo:

1. Copia `TEMPLATE.json` in `benchmarks/fixtures/gto_plus_<board>_<nnn>.json` e compila
   `fixture`, `gtosd_run` e `gto_plus_reference` (§2-4). Per `expected_layout` metti
   **segnaposto** (es. i valori del template o `0`).
2. Lancia il Passo A. Il run **fallisce sul gate layout** (`layout_matches_fixture: false`)
   ma scrive comunque il report con i valori reali.
3. Copia i 6 valori dal report in `expected_layout`.
4. Rilancia il Passo A: ora `layout_matches_fixture: true` e `correctness_passed` riflette i
   gate veri. Se il valore di `information_sets`/`actions`/`solver_state_bytes` non combacia
   esattamente, confronta con `physical_public_nodes`/`canonical_public_nodes` per capire se è
   cambiato l'albero (fingerprint diversa) o solo una stima.
5. Verifica che i `reference_nodes` del fixture combacino con l'action tree (etichette azioni
   e `path` dal report di prova) e che `gto_plus_unconditional_ev_checks` del report passi.

Il gate layout è **binario** sulla fingerprint (l'albero deve essere bit-identico): non è una
tolleranza, è un vincolo esatto — il fixture certifica che il solver sta risolvendo esattamente
la partita dichiarata.

**Passo C — benchmark a 5 run** (gate attivi: correctness e speed;
EV/frequenze e memoria diagnostici finché la v4 non stabilisce la
comparabilità):

```powershell
powershell -ExecutionPolicy Bypass -File tools/run_gto_plus_convergence_benchmark.ps1 `
    -Specification benchmarks/fixtures/gto_plus_<board>_<nnn>.json `
    -OutputDir out/gtp_<nnn>_five_runs
```

(`benchmark_id` viene letto dal fixture; lo script accetta anche `-Runs` e `-EnforceGate`.)

Risultato: `out/gtp_<nnn>_five_runs/summary.json` con mediana/p95, cinque
processi indipendenti e nessuna cache condivisa, più `run-01.json`…`run-05.json`.
I campi v3 `memory_gate` e `desktop_memory_gate` devono essere ignorati; i
valori grezzi restano evidenza diagnostica.

## 6. Regole fisse (PERFORMANCE.md, non modificabili)

- Speed ≤ `elapsed_seconds / 0.90` (1.71/0.90 = 1.9 s).
- Memoria: `NOT_EVALUATED_COMPARABILITY_UNRESOLVED`; nessun confronto tra Peak
  RSS, `solver_state_bytes` e display GTO+ è ammesso.
- Non esiste un cap desktop `<2 GiB` implicito. Un eventuale budget di prodotto
  futuro dovrà essere esplicito e indipendente dal benchmark GTO+.
- Correttezza = convergenza + layout/fingerprint + EV di `gate_node` (root) entro 0.05 ante
  **+ EV incondizionata dell'avversario al root entro 0.05 ante** (criterio EV, §4.1).
- EV BTN condizionali e frequenze: **diagnostici**, non decidono il gate.
- ≥5 processi indipendenti, mediana/p95 nearest-rank, build Release, report grezzi conservati.

### 6.1 Criterio EV: media pesata (incondizionata) invece delle EV condizionali

Le EV condizionali dei sotto-nodi BTN (`flop_btn_after_co_check`, `flop_btn_after_co_bet`)
sono **fragili**: a dEV vicino al target, soluzioni equivalenti possono avere miscele
check/bet diverse al root (es. 10% vs 5% di bet) con EV condizionali che differiscono oltre la
tolleranza pur essendo entrambe corrette. Per questo il criterio EV di correttezza usa
l'**EV incondizionata pesata sulle frequenze**:

```
EV_incondizionata = Σ_nodi_figli  frequenza_azione(gate) × EV_condizionale(nodo)
```

Ogni soluzione è pesata con **le proprie posteriori** (frequenze misurate × EV misurate vs
frequenze di riferimento × EV di riferimento). Il check è pubblicato nel report come
`gto_plus_unconditional_ev_checks.<gate_node_id>` (`weighted_measured_antes`,
`weighted_reference_antes`, `delta_antes`, `base_absolute_tolerance_antes`,
`display_rounding_budget_antes`, `absolute_tolerance_antes`, `passed`) e determina
`ev_correctness_passed`.
È coerente col gate root a somma zero: se l'EV del root combacia, anche l'EV incondizionata
dell'avversario combacia per costruzione (il check resta un diagnostico di coerenza).

**Tolleranza dell'aggregato (derivata dal display)**: i componenti di riferimento sono
arrotondati alla precisione di visualizzazione GTO+ (EV a `display_precision_percent` del pot,
frequenze a 0.001), quindi la somma pesata di riferimento porta un **budget di
arrotondamento** = Σ_componenti (|freq| × step_EV + |EV| × step_freq), con
`step_EV = display_precision_percent/100 × initial_pot` e `step_freq = 0.001`. La tolleranza
aggregata = `ev_absolute_tolerance_antes` + budget (caso peggiore su tutti i prodotti
arrotondati). Esempio 101: budget ≈ 0.079 → tolleranza ≈ 0.129 (delta misurato 0.053 → passa).
La tolleranza base resta quella del singolo nodo; il budget copre solo l'errore di
visualizzazione dei valori di riferimento, non la qualità della soluzione.

**Come compilare i `reference_nodes` per questo criterio**: il `gate_node` (root, player 0)
deve avere le `actions` (frequenze di riferimento); i suoi figli diretti (path di 1 elemento,
player 1, con `ev_antes`) sono i componenti della media pesata. Se non ci sono figli, il
criterio ricade sulle EV condizionali per-nodo (comportamento v1).

## 7. Benchmark completamente nuovi (altra board/stack/size)

Basta cambiare i campi `fixture` e ripetere il workflow. Per ogni scenario serve il
**riferimento GTO+ esterno** (EV/frequenze dei nodi che vuoi confrontare) — i valori del
template sono specifici di Ah Kh Qh / 40-100 / 50% e **non valgono per altri board**.

Se il riferimento GTO+ lo fornisci tu (es. export del nuovo board), il resto è tutto qui.

## 8. Diagnostico root-lock (F10.4, implementato test-only)

Per verificare che gli EV condizionali BTN combacino quando la strategia root è identica,
esiste il diagnostico a root lock: fixture `benchmarks/fixtures/root_lock_gto_plus_003.json`
(36 probabilità combo-per-combo dal root GTO+) + comando:

```bash
./out/build/windows-release/apps/gto_cli/gto_cli.exe postflop root-lock-diagnostic \
    benchmarks/fixtures/postflop_config_ahkhqh_003.json \
    benchmarks/fixtures/root_lock_gto_plus_003.json 200 out/f104_root_lock.json
```

Risultato documentato: delta BTN `+0.0348`/`+0.0366 ante` con il root bloccato.
È una diagnostica validata, non un gate automatico e non costituisce node
locking di prodotto. L'evidenza completa è in `GTO_PLUS_PARITY_JOURNEY.md`.
