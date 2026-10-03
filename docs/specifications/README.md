# Specifiche canoniche GTOSD

> **Verifica documentale:** 2026-09-04. La semantica memoria GTO+ è stata
> corretta: “Memory needed for solving” non è Peak RSS e non è mai esistito un
> cap desktop indipendente `<2 GiB`. Finché lo scope interno GTO+ non sarà
> ricostruito, il confronto memoria resta `NOT_EVALUATED`; i contatori OS sono
> diagnostici. Fixture, report e runner correnti usano lo schema v4, separano
> riferimento GTO+, accounting solver-owned e memoria di processo e non
> ricavano alcun budget dal display esterno. Gli input v3 sono soltanto legacy
> misclassified convertiti esplicitamente.

Questa cartella contiene i contratti tecnici del motore postflop HU esatto:
`gto_cli` e le librerie `core`, `equity`, `tree`, `isomorphism`, `solver`,
`best_response`, `memory`, `postflop` e `storage`. Contiene anche i contratti
di ricerca R2-S (`postflop_subgame` e la card abstraction di `gtosd::solver`) e,
marcati come legacy, quelli del preflop external sampling (`libs/preflop`). I
documenti descrivono il comportamento richiesto e lo stato realmente
implementato. Cronologia, gate e misure puntuali stanno nei report di fase, in
`docs/archive/`.

## Ambito (aggiornato il 2026-10-03)

- **Il solver preflop blueprint non è descritto qui.** `libs/preflop_blueprint`
  e `libs/card_abstraction` (step 1 e step 2, HU e 3-way) hanno la loro
  documentazione in `docs/research/preflop_vector_cfr/`: i report di modulo
  P0-P8, il diario [`PROGRESS_LOG.md`](../research/preflop_vector_cfr/PROGRESS_LOG.md),
  la ricetta di MonkerSolver e le specifiche in `threeway/`. Le loro CLI sono
  descritte nell'Appendice A di
  [`WEB_UI_PROTOTYPE_PROMPT.md`](../solver-ui/WEB_UI_PROTOTYPE_PROMPT.md).
  Quando questi documenti dicono che preflop, multiway, astrazione o formati
  preflop "non sono supportati", parlano di `gto_cli`, non del prodotto.
- **La GUI desktop Qt è stata tolta il 2026-10-02.** `DESKTOP_UI.md` è ora in
  [`docs/archive/legacy-postflop-2026-07-09/`](../archive/legacy-postflop-2026-07-09/README.md).
  L'interfaccia del prodotto è la web UI (`apps/solver-ui`, branch
  `feat/solver-ui`), che lancia `gto_cli` come processo separato. Dove questi
  documenti nominano ancora "la GUI" come consumatore dei risultati, si intende
  l'interfaccia che usa `gto_cli`.
- **`gto_cli` resta nel prodotto** come motore del postflop HU esatto
  (decisione del 2026-10-02). Sono previsti tre comandi nuovi: solve con i
  range dell'utente, eventi JSONL e un worker `serve`. Entreranno in
  [CLI.md](CLI.md) quando esisteranno.

Il backend di solving è, per contratto permanente, esclusivamente CPU/RAM. Una
GPU può servire soltanto a disegnare l'interfaccia e non partecipa mai al
calcolo del solver.

## Mappa dei documenti

- [ARCHITECTURE.md](ARCHITECTURE.md): moduli, dipendenze e confini.
- [MATHEMATICAL_MODEL.md](MATHEMATICAL_MODEL.md): gioco estensivo, reach, utility ed EV.
- [SHORT_DECK_RULES.md](SHORT_DECK_RULES.md): regole esplicite della variante.
- [SOLVER_ALGORITHMS.md](SOLVER_ALGORITHMS.md): algoritmi, averaging e certificazione.
- [NUMERICAL_PRECISION.md](NUMERICAL_PRECISION.md): unità, tipi e tolleranze.
- [TREE_FORMAT.md](TREE_FORMAT.md): configurazione e identità dell'albero pubblico.
- [SOLUTION_FORMAT.md](SOLUTION_FORMAT.md): checkpoint e contenitore `.gtsd`.
- [VALIDATION.md](VALIDATION.md): gerarchia delle prove e gate esterni.
- [TESTING.md](TESTING.md): suite, preset e copertura.
- [PERFORMANCE.md](PERFORMANCE.md): protocollo di benchmark e memoria.
- [CLI.md](CLI.md): comandi supportati e contratti di uscita.
- [LIMITATIONS.md](LIMITATIONS.md): limiti correnti dichiarati.
- [CARD_ABSTRACTION_AND_SUBGAME_CONTRACT.md](CARD_ABSTRACTION_AND_SUBGAME_CONTRACT.md):
  contratti R2-S di ricerca (card abstraction e subgame DCFR del postflop).
- [gtoplus_specs.md](gtoplus_specs.md): export di riferimento GTO+, fixture storica v1.
- [CHANGELOG.md](CHANGELOG.md): evoluzione delle specifiche.

## Precedenza

In caso di conflitto:

1. codice e test eseguibili definiscono il comportamento implementato;
2. questi documenti definiscono il contratto intenzionale corrente;
3. [GTO_PLUS_PARITY_JOURNEY.md](../GTO_PLUS_PARITY_JOURNEY.md) governa il gate
   temporaneo di parità GTO+. Se la parità resti un obiettivo di `gto_cli` è
   una decisione dell'utente ancora aperta;
4. l'[handoff corrente](../handoff/NEXT_STEPS_2026-10-02.md) fissa l'ordine dei
   lavori. La roadmap F0-F11+ del prodotto legacy
   ([ROADMAP_HU_SHORT_DECK_GTO_SOLVER.md](../archive/legacy-postflop-2026-07-09/ROADMAP_HU_SHORT_DECK_GTO_SOLVER.md),
   in archivio) è evidenza storica;
5. i report di completamento sono evidenza storica e non sovrascrivono un
   contratto successivo.

Ogni affermazione di completamento deve indicare test o benchmark osservati. Una
funzione pianificata non è supportata finché non esistono implementazione,
validazione e documentazione coerenti.

## Stato dei documenti esterni

- `GTO_PLUS_PARITY_JOURNEY.md` è il registro corrente del gate GTO+ e del freeze
  F11+.
- `GTO_PLUS_SOLVER_MEMORY_SEMANTICS_AND_GATE_CORRECTION_PLAN_2026-09-03.md` è
  l'errata corrige e il piano di migrazione della metrica memoria GTO+.
- `IMPLEMENTATION_STATUS.md` era la dashboard del programma legacy. Si ferma al
  2026-09-15 ed è in `docs/archive/legacy-postflop-2026-07-09/`, insieme ai
  report storici `PHASE_*_COMPLETION_REPORT.md`, ai cicli di ottimizzazione e
  alle analisi di memoria. Lo stato corrente del progetto è nel diario e
  nell'handoff.
- `archive/legacy-memory-gate/` conserva i report fondati sul falso gate senza
  conferirgli autorità corrente. Le altre cartelle di `docs/archive/` contengono
  i documenti chiusi, divisi per periodo, ciascuna con un README. I file
  cancellati il 2026-10-03 sono al tag `docs-pre-cleanup-2026-10-02`.
- I due piani `POSTFLOP_RESEARCH_DECISION_2026-09-10.md` e
  `POSTFLOP_AGENT_EXECUTION_ROADMAP_2026-09-10.md` e i documenti archiviati
  `*_OPTIMIZATION*` sono registri di engineering e non sostituiscono le
  specifiche.
