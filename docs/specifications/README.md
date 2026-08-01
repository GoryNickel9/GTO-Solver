# Specifiche canoniche GTOSD

Questa cartella contiene i contratti tecnici correnti del progetto. I documenti
qui presenti descrivono il comportamento richiesto e lo stato realmente
implementato; la roadmap e i report di fase conservano invece cronologia, gate e
misure puntuali.

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
- [DESKTOP_UI.md](DESKTOP_UI.md): workflow Qt e semantica dei risultati.
- [CLI.md](CLI.md): comandi supportati e contratti di uscita.
- [LIMITATIONS.md](LIMITATIONS.md): limiti correnti dichiarati.
- [CHANGELOG.md](CHANGELOG.md): evoluzione delle specifiche.

## Precedenza

In caso di conflitto:

1. codice e test eseguibili definiscono il comportamento implementato;
2. questi documenti definiscono il contratto intenzionale corrente;
3. [GTO_PLUS_PARITY_JOURNEY.md](../GTO_PLUS_PARITY_JOURNEY.md) governa il gate
   temporaneo di parità GTO+;
4. [ROADMAP_HU_SHORT_DECK_GTO_SOLVER.md](../ROADMAP_HU_SHORT_DECK_GTO_SOLVER.md)
   governa ordine e dipendenze delle fasi;
5. i report di completamento sono evidenza storica e non sovrascrivono un
   contratto successivo.

Ogni affermazione di completamento deve indicare test o benchmark osservati. Una
funzione pianificata non è supportata finché non esistono implementazione,
validazione e documentazione coerenti.
