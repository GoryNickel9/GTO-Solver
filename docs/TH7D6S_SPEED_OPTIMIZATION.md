# Piano di ottimizzazione velocità — benchmark GTO+ th7d6s

> Documento operativo per un agent coder. **Unico obiettivo: ridurre il tempo di esecuzione** del
> benchmark `GTP-TH7D6S-101` (fixture `benchmarks/fixtures/gto_plus_th7d6s_101.json`), senza
> rompere la correttezza dei benchmark che oggi passano. Ogni modifica va verificata con il
> protocollo §5 e, se il risultato devia, va ripristinata.

---

## 1. Obiettivo e criteri di accettazione

### Target
- **Benchmark primario**: `GTP-TH7D6S-101` (short deck HU postflop, flop Th7d6s, range asimmetrici
  pesati, 4 raise/street, 50% pot).
- **Obiettivo**: ridurre `elapsed_seconds` del run completo (200 iterazioni massime, converge a
  ~140, target dEV 1%) il più possibile verso il riferimento GTO+ (**17.66 s**, campo
  `gto_plus_reference.elapsed_seconds` della fixture). Stato attuale: **~412 s (6.9 min)**.
- La soglia di "pass" del test di velocità è decisa dall'utente; il documento fornisce metriche
  di progresso per ogni intervento (§7). L'agent deve riportare `elapsed_seconds` e
  `phase_seconds` a ogni step.

### Criteri di accettazione (NON negoziabili)
1. **th7d6s**: `converged=true`, stop a ~iterazione 140, `final_gto_plus_dev_percent ≈ 0.925%`
   (target 1%), `correctness_passed=true`, root EV `flop_co_root ≈ 8.2189` (riferimento GTO+
   8.22198, tolleranza 0.05), `layout_matches_fixture=true`.
2. **Benchmark AhKhQh** (i "fixature"): `gto_plus_ahkhqh_101.json` (e 103 root-lock, 104)
   devono continuare a passare esattamente come oggi: 101 converge a ~iter 80 con
   `final_gto_plus_dev_percent ≈ 0.674155%` (valore documentato), `correctness_passed=true`.
3. **Suite test** verde: `gtosd_gto_plus_reference_tests` (3 test: exact reference, parity
   fisico↔DAG entro 1e-11, external root lock), `gtosd_tests`, `gtosd_phase4_tests` …
   `gtosd_phase10_tests`.
4. **Nessuna modifica al protocollo benchmark**: non toccare nel fixture `maximum_iterations`
   (200), `certification_interval` (20), `averaging_delay` (20), `parallel_action_depth` (5),
   `target_dev_percent` (1.0). Non cambiare le definizioni di gioco (il fingerprint
   `fnv1a64:db01987c7570ec46` non deve cambiare).

---

## 2. Baseline misurata (stato attuale)

Run completo th7d6s (per-player layout + pool, build `out/build/windows-release`):

| Metrica | Valore |
|---|---|
| `elapsed_seconds` (run_solver) | 412.2 s |
| traversal | 320.8 s / 140 iter = **2.29 s/iter** (~78%) |
| certification | 73.7 s (7 cert × ~10.5 s) (~18%) |
| regret_application | 33.4 s (~8%) |
| info_sets / actions | 36,596,832 / 83,318,592 |
| solver_state_bytes | 666,548,736 |
| peak RSS | ~6.5 GB |

Per-iterazione: 2 traversal (una per giocatore) dell'albero fisico (378,834 nodi, 147,256
decision). Il costo dominante è il **calcolo dei valori sull'unione delle combo** (~423 live al
flop, 206/358 per giocatore) con accessi sparsi agli slot azione.

---

## 3. Architettura dell'hot path (file: `libs/postflop/src/postflop_solver.cpp`)

Righe riferite alla revisione corrente — verificarle prima di modificare (il file è stato
modificato di recente).

- `build_layout` (~768): costruisce `BoardData` per board. `board.legal_combos` = **unione** dei
  due range live; `board.player_combos[player]` / `board.player_local[player][combo]` = combo
  live **per giocatore** (usate per gli slot azione). `initial_reach[player][combo]` > 0 iff la
  combo è nel range del giocatore al flop.
- `decision_action_base` (747): per il path isomorphic fa **due lookup sparsi** per
  (nodo, combo, azione): `canonical_action_bases[physical_infoset_ids[physical_infoset_base +
  local]]`. Per automorfismi identità (caso th7d6s: flop rainbow + range frazionari →
  `automorphisms.size() == 1`) questa indirezione è pura perdita.
- `cfr_physical` (1574) → `cfr_chance` (2260) / `cfr_decision` (2334): ricorsione sull'albero
  fisico. `cfr_chance` fa il **parallel split al turn chance** (board a 3 carte, § riga ~2291):
  i figli vengono distribuiti al pool round-robin, il thread corrente tiene la sua quota.
- `policy_physical` (1594) → `policy_decision` (2455): valutazione profile/BR (certificazione).
- `apply_deferred_regrets` (1636) + `apply_pool_regrets` (1679): applicano i delta differiti
  (clip `max(0, regret+delta)`) iterando gli indici toccati **in ordine di albero** → accessi
  casuali su ~1 GB di stato.
- `current_strategy` (2070): regret matching sullo storage dell'azione.
- `fold_values`/`showdown_values` (2101/2145): valori terminali per combo (unione).
- Pool parallelo: ctor `DenseTraversal` (1297-1320, `parallel_workers_` +
  `parallel_worker_deltas_`), `dispatch_parallel_task` (~1393), `run_worker_loop` (1270),
  apply parallelo (1647-1651).

Costi noti per iterazione:
1. **Loop valori sull'unione** (~423 combo/nodo) con offset sparsi (doppia indirezione) — il
   grosso del tempo.
2. **Copie dei vettori reach** (2×630 double = ~10 KB) per ogni edge azione (~370 K edge) e
   chance (~43.6 K) per passata giocatore.
3. **Regret application** con accesso casuale su ~83 M indici per passata.

---

## 4. Ottimizzazioni, in ordine di implementazione

Ogni intervento è indipendente e va verificato (§5) prima di passare al successivo. Ordine
consigliato per impatto/rischio. Nota: **O1 e O2 sono ortogonali al redesign** (§8): valgono in
qualunque architettura. **O3 è il primo passo del redesign (R1)** — non è lavoro da buttare, è la
fondazione su cui costruiscono R2/R3. **O5 è assorbito da R2** (update vettorizzati su blocchi
contigui) e **O4 da R4** (metrica di convergenza economica).

### O1 — Rimuovere la doppia indirezione per automorfismi identità
**Perché**: con `automorphisms.size() == 1` (solo identità — il caso th7d6s) ogni infoset fisico
è unico e `canonical_action_bases` è sequenziale: l'action base del combo locale `l` è
`base_azioni_del_nodo + l × action_count`, calcolabile direttamente. Elimina 2 lookup sparsi
(146 MB + 667 MB di array) dal loop più interno. Atteso: −20–40% sul traversal.

**Come**:
- In `build_layout`, nel ramo isomorphic (loop `for (combo : board.player_combos[decision.player])`
  ~riga 890): se `automorphisms.size() <= 1`, assegnare a `decision.action_base` l'offset azioni
  del local 0 (il valore di `layout.actions` prima del primo combo del nodo) e procedere in
  ordine; **non** inserire in `canonical_infosets` (o inserire ma senza riuso).
- In `decision_action_base` (747): se il layout è "identity-only" (es. nuovo flag su
  `DenseLayout`, es. `layout.uses_direct_action_bases` impostato quando
  `automorphisms.size() <= 1`), ritornare `decision.action_base + local × action_count`.
- **Guardia**: il path canonical/DAG (AhKhQh) ha `automorphisms.size() > 1` → invariato.
- Non toccare `decision_infoset_id` (serve ancora per il build).

**Verifica**: dEV/root EV identici; misurare `phase_seconds.traversal`.

### O2 — Regret application sequenziale (ordinare gli indici toccati)
**Perché**: `apply_deferred_regrets` (1636) e `apply_pool_regrets` (1679) iterano gli indici
toccati in ordine di visita dell'albero → accessi casuali. Gli indici sono applicati una sola
volta ciascuno, quindi **l'ordine non cambia il risultato**: ordinare (o bucketizzare per
pagina) rende l'accesso sequenziale su regret+delta (1 GB) → locality + possibilità di
vectorizzazione. Atteso: regret_application 33.4 s → ~10-15 s.

**Come**: prima dell'apply, `std::sort(deferred_regret_touched_.begin(), end)` (o radix/bucket
per pagina di memoria); stesso per i touched di ogni worker in `apply_pool_regrets`. Attenzione
che `apply_pool_regrets` itera due volte il touched del worker (merge + apply): ordinare una
volta sola prima dei due loop, e zeroare `target_delta[index]` dopo l'apply (già così).

**Verifica**: risultati bit-exact (ogni indice applicato una volta, somma su indici disgiunti) —
il dEV deve restare identico; misurare `phase_seconds.regret_application`.

### O3 — Spazi di valore per-giocatore (il grosso, da fare con cautela)
**Premessa già dimostrata empiricamente in sessione**: le combo fuori dal range dell'attore
hanno reach zero → le loro strategie/valori **non influenzano il risultato finale**. Prova:
sostituendo la strategia non-attore con fallback uniforme il dEV finale resta identico a 6
decimali (104.6486% a 5 iterazioni; 0.924996% a 140). Da qui deriva che i loop "valori" possono
restare corretti calcolando solo le combo dell'attore **se** la propagazione verso il parent
viene gestita (il parent legge i valori del child per le *sue* combo, che sono non-attore nel
child).

**Fase A — prova di eliminabilità (obbligatoria prima di progettare)**:
- In `cfr_decision`/`policy_decision`, nei branch dei valori non-attore (dove ora si usa il
  fallback uniforme), mettere 0.0 e verificare che dEV/root EV restino identici (§5, confronto
  a ~1e-6). Se si spostano, la premessa è più sottile: fermarsi e ripristinare (documentare
  perché).

**Fase B — spazi compatti per giocatore** (solo se A passa):
- Sostituire i vettori densi `ComboVector` (array<double,630>) nei punti caldi con array
  compatti indicizzati dalle combo live dell'attore; il parent rimappa i valori del child sulle
  proprie combo con una tabella di traduzione per board (le combo del parent ⊂ union del child;
  i valori per le combo non-attore nel child si possono **saltare** se la fase A conferma che
  sono irrilevanti — la verifica empirica decide).
- Riduce sia i loop valori (~423 → ~206/358) sia le **copie reach** (~10 KB → ~3-6 KB per edge).
- Atteso: traversal −40-60%.
- **Rischio alto**: procedere per gradi, verificando il dEV a ogni passo; mantenere invariato il
  path canonical/DAG (AhKhQh) che non deve cambiare comportamento.

**Verifica**: dEV finale identico (≤1e-6 di deviazione accettabile, meglio bit-exact), root EV
dentro tolleranza, tempo traversal.

### O4 — Certificazione più leggera (solo se O1–O3 non bastano)
La certificazione è 4 traversal (profile+BR × 2 giocatori) ≈ 10.5 s ciascuna (18% del totale).
Beneficia automaticamente di O1/O3 (usa `policy_physical`). Se serve di più:
- Non ridurre la frequenza (protocollo).
- Valutare se il passaggio BR può riusare parte dei valori del passaggio profile (attento: è
  una best response, i valori cambiano) — **solo se** la verifica dEV resta identica.

### O5 — SIMD (ultimo, solo se il collo residuo è computazionale)
Dopo O3 i loop per-combo saranno più compatti e gli offset (con O1) contigui: vectorizzare
(la build è `/arch:AVX2`) i loop valori e l'apply regret. Misurare prima/dopo; non è
prioritario rispetto a O1-O3.

### (Posticipato, non ora) — Parallelismo oltre il turn chance
Lo split al river è rischioso (serializzazione FIFO già osservata) e va valutato solo dopo O3,
quando il costo dei valori scende e il peso del parallelismo cresce.

---

## 5. Protocollo di verifica (comandi esatti)

Build (Windows, MSVC — serve l'ambiente vcvars):
```
cmd //c "call \"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat\" && cmake --build out\\build\\windows-release --target gto_cli"
```
(se l'exe è lockato da un run in corso: `powershell "Get-Process gto_cli | Stop-Process -Force"`)

Test di regressione (binari in `out/build/windows-release/tests/`):
```
gtosd_gto_plus_reference_tests.exe   # 3 test: reference exact, parity DAG ≤1e-11, root lock
gtosd_tests.exe, gtosd_phase4_tests.exe … gtosd_phase10_tests.exe
```

Benchmark AhKhQh (devono restare identici — convergenza e correttezza):
```
gto_cli.exe postflop benchmark-gto-plus benchmarks/fixtures/gto_plus_ahkhqh_101.json out/101.json
# atteso: converged=true ~iter 80, final_dev_percent ≈ 0.674155, correctness_passed=true
```
(ripetere per 103 root-lock se presente come gate)

Benchmark th7d6s (il target di velocità):
```
gto_cli.exe postflop benchmark-gto-plus benchmarks/fixtures/gto_plus_th7d6s_101.json out/th7d6s.json
# atteso: converged=true ~iter 140, final_dev_percent ≈ 0.925, correctness_passed=true,
#         gto_plus_ev_checks.flop_co_root.passed=true, layout_matches_fixture=true
```

Confronto numerico tra run (obbligatorio dopo ogni modifica):
- `final_gto_plus_dev_percent` e la curva di convergenza (`convergence[].gto_plus_dev_percent`)
  devono restare **identici entro ~1e-6** rispetto al baseline (bit-exact dove possibile).
- `gto_plus_ev_checks.flop_co_root.measured_antes` ≈ 8.2189.
- Tempi: `phase_seconds.{traversal, certification, regret_application, run_solver}` e
  `elapsed_seconds`.

Per iterare velocemente (solo sanity, NON per i numeri finali): fixture smoke con
`maximum_iterations=5, certification_interval=1, averaging_delay=0` (copia della fixture con
`benchmark_id` che rispetti il pattern `GTP-XXXX-###` — ultimi 3 caratteri numerici).

---

## 6. Vincoli e insidie

1. **Protocollo benchmark**: non modificare iteration/cert_interval/avg_delay/parallel_depth/
   target nella fixture; non modificare le definizioni di gioco (fingerprint invariato).
2. **Path canonical/DAG (AhKhQh)**: le ottimizzazioni si applicano al path **fisico** (no DAG).
   Qualsiasi modifica condivisa deve restare a comportamento invariato per range simmetrici
   (dove `player_combos[player] == legal_combos`). I test reference (parity 1e-11) sono il gate.
3. **Concorrenza**: il meccanismo deferred è single-writer-per-vettore; la pool usa delta
   per-worker **disgiunti** (le sottoalbero dei worker non condividono action base con
   automorfismi banali). Non introdurre scritture concorrenti sugli stessi indici
   (regret/strategy/apply).
4. **Precisione**: stato float32, compute float64: non cambiare. Il deferred (clip una volta a
   fine pass) è semantico: non applicare il clip incrementalmente.
5. **`decision_action_base` con local −1 è fuori bounds**: mai chiamarla per combo non-attore
   (guardare `in_actor_range`/`player_local`).
6. **Fixture `expected_layout`**: se cambiano info_sets/actions/solver_state_bytes, aggiornare
   `expected_layout` nella fixture (gate `layout_matches_fixture`). Il fingerprint NON cambia.
7. **Misurare su run completi** per i numeri finali; la smoke serve solo per iterare.

---

## 7. Metriche di successo per intervento

| Intervento | Atteso su th7d6s | Verifica |
|---|---|---|
| O1 (action base diretta) | traversal −20–40% | dEV identico, `phase_seconds.traversal` |
| O2 (apply ordinato) | regret_application −50–70% | dEV bit-exact, `phase_seconds.regret_application` |
| R1 = O3 (valori per-giocatore) | traversal −40–60% | dEV ≤1e-6 (idealmente identico), root EV |
| R2 (blocchi contigui + SIMD) | update ×2–5 | dEV bit-exact, traversal |
| R3 (parallelismo per blocco) | traversal ×3–6 | dEV identico, scaling su core |
| R4 (metrica economica, opzionale) | −~15% sul totale | dEV identico (solo se approvata) |

Traiettoria attesa (cumulativa, ordine di grandezza): 412 s → ~250-300 s (O1+O2) → ~120-180 s
(R1) → ~60-100 s (R2) → ~20-40 s (R3) → ~15-30 s (R4). Con il **redesign completo (§8) i
17.66 s di GTO+ diventano un obiettivo plausibile** (stime ottimistiche; il gate di accettazione
resta: correttezza identica + benchmark AhKhQh verdi).

---

## 8. Redesign architetturale — R1-R4

Le ottimizzazioni §4 migliorano *dentro* l'architettura attuale, il cui costo per iterazione è
~15-40× quello di GTO+. Il redesign cambia le scelte di fondo, a fasi, ognuna con i gate verdi
(§5). **Ogni fase è indipendente e verificabile**: niente big-bang.

### R1 — Spazi di valore compatti per giocatore (= O3 portato a fondo)
**Perché**: oggi ogni nodo calcola i valori sull'unione (~423 combo) con vettori fissi di 630
double, e ogni edge copia 2×630 double di reach (~10 KB × ~400 K edge per passata).
**Come**: rappresentare valori e reach con array compatti sulle combo live **dell'attore**
(206/358), con una tabella di traduzione per board per la propagazione parent↔child. La fase A
di O3 (prova di eliminabilità dei valori non-attore) è il gate d'ingresso.
**Atteso**: traversal −40-60%. **Rischio**: medio-alto (propagazione dei valori); procedere per
gradi, dEV ≤1e-6 a ogni passo. Questa fase si fa **dentro la struttura esistente** e de-rischia
R2/R3 (non è lavoro da buttare: è la fondazione).

### R2 — Action block contigui per infoset + update SIMD
**Perché**: gli slot di regret/strategy vivono in array piatti da 667 MB con accesso sparso
(offset = base + local × action_count, comunque a salti per combo) → ogni aggiornamento è una
cache-miss (~24M update/s per thread). GTO+ dispone gli action **contigui per infoset** e li
processa in raffiche AVX2.
**Come**: riordinare l'allocazione degli action per infoset (l'ordine oggi è per nodo); i loop di
regret/strategy/apply diventano loop vettorizzati su blocchi contigui
(`for (i = 0; i < block; i += 8) { _mm256_… }`, build `/arch:AVX2`). Aggiornare
`decision_action_base` e i checkpoint di conseguenza.
**Atteso**: ×2-5 sugli update (→ ~100-200M update/s per thread). **Rischio**: medio (ordine di
allocazione tocca checkpoint/indici); verifica bit-exact del dEV.

### R3 — Parallelismo per blocco di azioni
**Perché**: il pool attuale divide per sottoalbero al turn chance (~2× effettivo su 6 thread);
GTO+ parallelizza per blocchi di azioni con scaling quasi lineare.
**Come**: dopo R2, i blocchi per infoset sono unità di lavoro atomiche: ogni thread processa un
sottoinsieme di infoset/azioni con **delta per-thread** (il meccanismo deferred per-worker già
esiste e gli indici sono disgiunti per sottoalbero); il parallel split al chance può essere
sostituito/aumentato da una partizione per infoset.
**Atteso**: ×3-6 sul traversal (scaling che migliora man mano che il costo per unità scende con
R1/R2). **Rischio**: medio (sincronizzazione/merge, load balance).

### R4 — Metrica di convergenza economica (opzionale, richiede approvazione)
**Perché**: la certificazione exact BR costa ~18% (74 s). GTO+ usa una stima interna durante il
solve.
**Come**: a ogni checkpoint usare una stima interna (regret-based o exploitability stimata);
l'exact BR solo al checkpoint finale. **Vincolo**: NON modificare il protocollo benchmark attuale
(cert ogni 20, target 1%) senza l'approvazione dell'utente — questa è una proposta da validare
prima. Se non approvata, la certificazione beneficia comunque di R1-R3 (usa `policy_physical`).

---

## 9. Ordine di lavoro consigliato per l'agent

**Risposta alla domanda "redesign o ottimizzazioni prima?": le ottimizzazioni O1-O2 prima, poi il
redesign a fasi (R1 → R2 → R3 → R4).** Motivi:

1. **O1 e O2 sono ortogonali al redesign**: action base diretta e apply ordinato restano validi
   in qualunque architettura. Costo quasi zero, guadagno immediato, e allenano il protocollo di
   verifica (§5) prima di toccare strutture delicate.
2. **Il redesign va per fasi, mai big-bang**: ogni fase (R1-R4) deve tenere i gate verdi (dEV
   identico, AhKhQh, suite). Un rewrite totale in un colpo solo rende impossibile isolare i bug.
3. **R1 = O3 è il ponte**: si fa dentro la struttura esistente e produce la prova di
   eliminabilità + gli spazi per-giocatore su cui costruiscono R2/R3. Non è lavoro da buttare.
4. **R2/R3 costruiscono sopra R1**: i blocchi contigui per infoset (R2) e il parallelismo per
   blocco (R3) richiedono gli spazi compatti per-giocatore di R1.
5. R4 (se approvata) chiude il residuo della certificazione.

Sequenza concreta:

1. Riprodurre il baseline (build + run th7d6s completo + 003) e salvare i numeri di riferimento.
2. **O1** → verifica → commit.
3. **O2** → verifica → commit.
4. **R1 (= O3)**: fase A (prova di eliminabilità dei valori non-attore) → se passa, fase B per
   gradi → verifica → commit.
5. **R2**: allocazione action contigua per infoset + update SIMD → verifica bit-exact → commit.
6. **R3**: partizione per infoset/blocco sui thread (pool) → verifica scaling → commit.
7. **R4**: solo con approvazione dell'utente (cambia il criterio di convergenza del run).
8. Aggiornare `expected_layout` della fixture se il layout cambia; aggiornare questo documento
   con i risultati misurati a ogni fase.
