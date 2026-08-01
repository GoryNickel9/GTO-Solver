# Changelog delle specifiche

Questo changelog registra modifiche ai contratti in `docs/specifications`, non
sostituisce la cronologia Git né i report di fase.

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
