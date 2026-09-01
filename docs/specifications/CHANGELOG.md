# Changelog delle specifiche

Questo changelog registra modifiche ai contratti in `docs/specifications`, non
sostituisce la cronologia Git né i report di fase.

## 2026-09-01

### Aggiornato

- Promossa la schedule comune `production_dcfr` (`1.5/0/3`, reset
  `1,2,5,17,65`, regret clock post-65 ritardato di una iterazione).
- Allineate le tre fixture production e mantenuta l'identita' checkpoint `11`.
- Registrata la qualificazione final-head a cinque processi: `15/15` solve con
  dEV `<1%`, correctness/layout/exact outcomes e cap desktop PASS.
- Pubblicati mediana/p95 AHK `0,758705/0,790918 s`, TH
  `19,948228/24,192260 s`, TST `184,095930/197,865030 s`.
- Corretto lo stato del parity gate: AHK time PASS; TH e TST time FAIL
  rispettivamente del `1,661%` e `42,722%` sulla mediana.
- Reso `ScaledUint16RegretStrategy` il formato benchmark production dichiarato;
  “exact outcomes” non implica identita' numerica con Float64.

### Verificato

- CTest Release `26/26 PASS`.
- Contratto fixture, resume byte-equivalent, PowerShell parse e diff check PASS.

## 2026-08-14

### Aggiornato

- Allineato il checkpoint della suite GTO+ ai report correnti: dEV/root/RAM
  PASS su 3/3 e tempo FAIL su 3/3.
- Separati esplicitamente riferimenti temporali GTO+ grezzi, limiti al 90%,
  `solver_state_bytes`, transient workspace e peak RSS.
- Registrati l'assenza di iteration cap, il confronto stretto `Target dEV < 1%`
  e il freeze F11+ fino al superamento con modifiche del solo core generale.
- Documentato il fallback fisico necessario per range asimmetrici e il ritiro
  dell'esperimento isomorfico non lossless.
- Reso permanente il contratto di solving CPU/RAM-only: nessun backend GPU o
  acceleratore di calcolo presente o futuro; l'eventuale GPU è rendering GUI.
- Ordinato il piano tempo: fast path generale del fallback fisico, DAG
  player-local, isomorfismo street-local e soltanto dopo layout/scheduling/SIMD.
- Marcato il candidato DAG presente nel worktree come non verificato e non
  promosso; i benchmark correnti non sono stati aggiornati.

### Verificato

- Build Release di `gto_cli` e `gtosd_gto_plus_reference_tests`.
- Riferimento GTO+ PASS con 24 asserzioni, fallback asimmetrico a differenza
  zero e root lock esterno PASS. La suite CTest completa non è stata rieseguita
  in questa chiusura.

## 2026-08-02

### Aggiunto

- Corpus canonico separato per architettura, matematica, regole, algoritmi,
  precisione, tree, soluzione, validazione, testing, performance, GUI, CLI e
  limitazioni.
- Indice e regola di precedenza tra specifiche, parity journey, roadmap e report
  storici.
- Semantica esplicita dei posteriori privati e degli EV condizionali.
- Contratto del futuro esperimento F10.4 controlled-posterior, marcato come
  diagnostico e non implementato.

### Corretto

- La parità GTO+ non richiede più EV BTN condizionali uguali quando i posteriori
  combo-per-combo sono differenti.
- Il root EV AhKhQh è registrato come PASS: delta `+0,005491179 ante`.
- La baseline resta FAIL sul tempo e PASS sulla memoria.
- Il tentativo GTO+ a target 0,10% è registrato come censurato `>245 s`, con dEV
  osservato circa 0,11%.
- La soglia automatica all-in segue la frazione dello stack impegnata e distingue
  `Add` da `Go`.

### Chiarito

- Nessun bucketing, sampling o smoothing è usato nella fixture di parità.
- `Float32` riguarda lo stato cumulativo; traversal e certificazione restano in
  precisione più alta.
- La quantizzazione `uint16` è sperimentale e lossy.
- Node locking globale, preflop HU e multiway restano non supportati.

## Cronologia precedente

Le decisioni e misure precedenti sono conservate in:

- [ROADMAP_HU_SHORT_DECK_GTO_SOLVER.md](../ROADMAP_HU_SHORT_DECK_GTO_SOLVER.md);
- [IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md);
- [GTO_PLUS_PARITY_JOURNEY.md](../GTO_PLUS_PARITY_JOURNEY.md);
- report `PHASE_*_COMPLETION_REPORT.md` nella directory `docs`.
