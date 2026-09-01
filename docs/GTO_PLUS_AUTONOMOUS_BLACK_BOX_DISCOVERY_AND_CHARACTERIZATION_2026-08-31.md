# GTO+ autonomous black-box discovery and characterization — final 2026-09-01

## Analisi

### Esito

**Outcome unico: C. PARTIALLY AUTOMATABLE — CHARACTERIZATION VALID WITH MANUAL MARKER.**

Il precedente outcome `E. UNSAFE OR INCONCLUSIVE` è superato da nuove evidenze:

- progetto TST identificato, copiato in read-only e verificato tramite SHA-256;
- due dry-run coerenti senza solve;
- configurazione visibile TST verificata;
- nuova generazione del trace dEV osservata in ogni run valida;
- completamento e tempo nativo letti nel pannello GTO+;
- cinque processi freschi validi;
- original e copia rimasti byte-invariant;
- valore UI `19.x GB` identificato dall'utente come RAM disponibile dell'host,
  non memoria del solver.

GTO+ resta `UIA_NONE`: l'albero accessibility espone soltanto la finestra
top-level. L'azione manuale minima è aprire il pannello `Run solver`, se non è
già aperto, e premere il relativo pulsante dopo il marker di armamento. Trace,
telemetria, crossing, integrità e aggregazione sono automatizzati.

### Authority map

| Fonte | Autorità corrente |
|---|---|
| `benchmarks/fixtures/gto_plus_tstc9d_101.json` | contratto machine-readable TST |
| `docs/GTO_PLUS_CONVERGENCE_BENCHMARK.md` | reference GTO+ e timing scope |
| `docs/TST_STRICT_2GB_BOTTLENECK_ATTRIBUTION_AND_FEASIBILITY_LOOP_2026-08-31.md` | baseline GTOSD fresca strict-2GB |
| artifact `final-20260901/summary.json` | aggregazione black-box corrente |
| questo report | decisione e limiti finali del loop |

Riferimenti congelati usati:

- GTO+ `116.09 s`, dEV `0.91%`, solver memory `2,000,000,000 B`, 8 thread;
- limite task GTOSD `128.988889 s`;
- GTOSD fresca `234.437726 s`, peak RSS `1,971,036,160 B`.

### Identità installazione e progetto

| Campo | Valore |
|---|---|
| metodo discovery | processi, registro, associazione `.gto`, shortcut e directory collegate |
| executable path redatto | `[volume esterno]\\GTO6+\\GTO\\GTO.exe` |
| executable SHA-256 | `CE3EA87A5BF755AC359DE1D2AE8595DAF0E8331FCF5DA30DDEF047FDC9FD522C` |
| GTO+ version | `1.6.9.0`, x64 |
| firma osservata | Authenticode valida, StoxEV B.V. |
| project path redatto | `[repository]\\benchmark GTO+\\GTOSD3.gto` |
| project SHA-256 | `48EEDD8FAFFC80B805CFF40E8BED390E6EBB97A95262EC0865A182E79C79C464` |
| metodo apertura | argomento file esplicito verso una copia read-only verificata |

La mappatura fornita dall'utente è `GTOSD=AHK`, `GTOSD2=TH`, `GTOSD3=TST`.
Per la serie primaria è stato usato soltanto `GTOSD3.gto`.

### Configurazione TST e stato pre-solve

La UI e l'identità del progetto hanno verificato:

- board `Ts Tc 9d`;
- pot iniziale `16` ante e stack `80`;
- azioni root `Bet 12 (75%)`, `Bet 5.3 (33%)`, `Check`;
- cash game `NO RAKE`;
- target dEV `1%`, interpretato strettamente come `<1%`;
- 8 solver thread;
- Short Deck 6+, `Straight > 3 of a kind`.

La soluzione salvata mostrava già `0.146 / 0.91%`; non è stata usata come
prova di una nuova run. La validità richiedeva il reset implicito del file
progress, una nuova generazione e il trace completo corrente.

### dEV, completion e memoria

Con pot iniziale `16`:

- ultimo punto `>=1%`: `0.180646 / 16 * 100 = 1.1290375%`;
- primo punto `<1%`: `0.145654 / 16 * 100 = 0.9103375%`;
- 24 aggiornamenti dEV in ciascuna run valida;
- crossing uncertainty mediana `4.8119598 s`.

La fonte ufficiale del tempo consultabile è il campo nativo `Time` di GTO+.
Il timestamp esatto del click non è catturato dall'observer; perciò il tempo
generation-to-crossing è diagnostico, mentre il tempo nativo è ufficiale.

Le metriche memoria sono separate:

- `19.x GB` in basso nella UI = memoria fisica disponibile dell'host
  (semantica fornita dall'utente);
- peak working set/private bytes = contatori OS del processo GTO+;
- `2,000,000,000 B` = riferimento congelato di solver memory, non il valore
  `19.x GB`.

## Piano

Il loop eseguito ha seguito: discovery, doppio dry-run, singola replica
interpretabile, cinque processi indipendenti, adjudication, aggregation,
secondary scaling gate, test e documentazione.

Thread scaling e size scaling non sono stati eseguiti. Sono fasi secondarie e
il gate è stato chiuso perché:

- nessun controllo thread semantico è esposto via UIA; modificare 1/2/4/8
  richiederebbe ulteriori mutazioni manuali non verificate;
- solo TST ha ricevuto il preflight completo e il protocollo five-run;
  l'identità cross-size di range/sizing/rake non è verificata indipendentemente.

## Implementazione

| Campo | Valore |
|---|---|
| branch | `research/gto-plus-autonomous-black-box-20260831` |
| base | `77677b2f0be711b588bebdbd2256fe8a28f4b863` |
| output runtime | `.tmp/gto-plus-black-box/` |
| production changes | nessuna |

Tooling finale:

- probe installazione, progetti e UIA;
- runner black-box unattended per profili semanticamente verificabili;
- observer manuale per GTO+ `UIA_NONE`;
- evaluator v2 con adjudication, statistiche e confronto GTOSD;
- test sintetico del manual-marker path e della replacement;
- directory run immutabili e copie progetto read-only.

Nessun executable, progetto, soluzione, screenshot, license file o artifact
`.tmp` entra nei commit.

## Validazione

### Run ledger primario

| Run | Stato | Time nativo | First `<1%` dal primo progress | Peak WS |
|---|---|---:|---:|---:|
| `tst-controlled-manual-20260901-05` | VALID | `130.41 s` | `123.6997971 s` | `2,182,807,552 B` |
| `tst-controlled-manual-20260901-06` | VALID | `118.91 s` | `111.7297067 s` | `2,183,634,944 B` |
| `tst-controlled-manual-20260901-08` | VALID replacement | `120.77 s` | `115.6197018 s` | `2,182,221,824 B` |
| `tst-controlled-manual-20260901-09` | VALID | `110.86 s` | `105.3730215 s` | `2,183,729,152 B` |
| `tst-controlled-manual-20260901-10` | VALID | `109.23 s` | `104.2213748 s` | `2,182,410,240 B` |

Run invalide:

- `manual-user-marker-tst-01`: project identity e completion non verificati;
- `tst-controlled-manual-20260901-07`: observer armato `13.05 s` dopo start;
  sostituita una sola volta da `-08`.

Conteggi: `valid=5`, `invalid=2`, `replacement=1`.

### Statistiche

| Metrica | Min | Mediana | Max / p95 nearest-rank |
|---|---:|---:|---:|
| soluzione consultabile, tempo nativo | `109.23 s` | `118.91 s` | `130.41 s` |
| primo `<1%` dal primo progress | `104.2213748 s` | `111.7297067 s` | `123.6997971 s` |
| peak working set | `2,182,221,824 B` | `2,182,807,552 B` | `2,183,729,152 B` |
| peak private bytes | `2,156,421,120 B` | `2,158,571,520 B` | `2,159,865,856 B` |

Tempo consultabile medio diagnostico `118.036 s`; sample standard deviation
`8.5204918 s`. La media della CPU normalizzata per run nella finestra del
trace ha mediana `0.8468583`; è una metrica OS, non il numero di solver thread.
Il process thread-count include thread non solver. Process-tree memory e I/O
non sono stati acquisiti e non vengono stimati retroattivamente.

### Confronto

- mediana GTO+ `118.91 s` vs reference `116.09 s`: `+2.42915%`;
- mediana GTO+ vs limite GTOSD `128.988889 s`: `7.81377%` sotto il limite;
- GTOSD fresca / mediana GTO+: `1.971556x`;
- peak WS massimo GTO+ / peak RSS GTOSD: `1.107909x`.

Il riferimento temporale `116.09 s` è compatibile con il campione osservato
(`109.23–130.41 s`) ma non è stato riprodotto esattamente dalla mediana, che è
più alta del `2.43%`. GTO+ resta quasi `2x` più veloce della baseline GTOSD
fresca, ma usa circa `10.79%` più working set OS del peak RSS GTOSD. Il
riferimento product-level `2 GB` resta separato.

### Test

- parsing PowerShell e safety contract: PASS;
- parser dEV punto/virgola e strict `<1%`: PASS;
- aggregazione sintetica con cinque manual-marker validi, un invalido e una
  replacement: PASS;
- hash original/copia dopo ogni run: PASS;
- doppio dry-run senza solve: PASS;
- rebuild pulita Release/Ninja con `/W4 /WX`: PASS, `ninja: no work to do`;
- full CTest Release pulita: `26/26 PASS` in `197.38 s`.

## Decisioni

| Decisione | Esito |
|---|---|
| automation classification | `C. PARTIALLY AUTOMATABLE` |
| characterization validity | VALID |
| reference `116.09 s` | COMPATIBILE, non identica: mediana `118.91 s` (`+2.43%`), reference interna all'intervallo osservato |
| project/configuration identity | VERIFIED per TST |
| Run Solver invocabile | YES manuale; NO autonomo semantico |
| target crossing osservabile | YES, trace nativo corrente |
| completion consultabile | YES, pannello e strategia visibili |
| memory metric mapping | RESOLVED per `19.x GB`; process metrics separate |
| cinque repliche | PASS |
| thread/size scaling | NOT PERFORMED, secondary gates closed |
| vantaggio black-box | `5. INSUFFICIENT OBSERVABILITY` |

La caratterizzazione misura il vantaggio, ma non lo attribuisce. Senza
iterazioni interne, costo unitario osservabile, scaling verificato e cadenza BR,
i dati non distinguono algoritmo, dataflow o stopping/certification. Non viene
fatto alcun claim su algoritmo interno, precisione, codec o layout GTO+.

## Passo successivo

Profilare nel solo GTOSD la differenza tra tempo traversal e certification sul
contratto TST congelato, usando la mediana GTO+ `118.91 s` soltanto come target
black-box e senza modificare production finché un candidato non supera i gate
comuni di correttezza, RAM e costo locale.
