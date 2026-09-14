# Evidenze dell'analisi preflop del 10 settembre 2026

Questa cartella conserva risultati e sonde dell'analisi HU CO40. Non contiene una soluzione certificata né un'ottimizzazione di produzione.

Il [rapporto](C:/Users/GoryNickel/Documents/GitHub/GTO-Solver/docs/research/HU_PREFLOP_12H_FEASIBILITY_2026-09-10.md) distingue risultati misurati, dimostrazione dell'averaging, proiezioni e proposte.

## Contenuto

| File | Significato |
|---|---|
| `runs.json` | 16 processi sequenziali, comandi originali, wall time, solve time, memoria osservata e metriche |
| `baseline_*.json`, `cached_*.json`, `profile_*.json` | Output completi dei candidati brevi, con strategie root e `nashconv_certified=false` |
| `*.stdout.log`, `*.stderr.log` | Output originali; nei quattro log `profile` ci sono timer e istogrammi delle visite |
| `analysis_summary.json` | Mediane A/B, percentuali di singleton, conteggi combinatori e throughput richiesti dal budget ipotetico |
| `artifact_verification.json` | Verifica finale dei 12 hash, output deterministici, link locali e sintassi degli script; non è un test del solver |
| `source_manifest.json` | Hash dei sorgenti, fixture e librerie effettivamente usati; compiler, flags e hardware |
| `machine_memory.json` | Snapshot `GlobalMemoryStatusEx` all'inizio delle sonde |
| `averaging_counterexample.py`, `.json` | Calcolo razionale indipendente: formula corretta contro accumulatore locale atteso |
| `river_certificate_probe.cpp`, relativi log | Valutazione del profilo uniforme e BR exact su una sola root River |
| `preflight.stdout.log` | Preflight della fixture esterna: PASS, mapping e fingerprint |
| `prepare_probe.py`, `run_probes.py`, `build_river.cmd` | Riproduzione delle sonde; non modificano il motore originale |

I comandi nei risultati conservano il percorso originale `.tmp/preflop-research-20260910`. Le copie degli script qui presenti risolvono invece il repository dalla posizione di questa cartella. Il generatore controlla l'hash del sorgente prima di produrre le copie; una revisione diversa richiede un nuovo audit, non una sostituzione silenziosa della baseline.

La variante `baseline` copia il sorgente originale; `profile` aggiunge timer, contatori e 8 byte per stato; `cached` memorizza soltanto il vincitore del deal completo già estratto. Le tre varianti sono compilate in eseguibili separati e collegate alle stesse librerie. Non confrontare i tempi strumentati con quelli non strumentati come se misurassero uno speedup del solver.

## Riproduzione sul PC originale

Prima verificare gli hash del manifest e la disponibilità delle librerie Release indicate. Gli script presuppongono l'installazione Visual Studio usata nella misura. Non eseguono download.

1. Eseguire `averaging_counterexample.py` con Python: termina dopo le asserzioni razionali e scrive il suo JSON.
2. Eseguire `prepare_probe.py`: genera le tre copie diagnostiche e `build.cmd` in questa cartella.
3. Eseguire `build.cmd`, quindi `run_probes.py`: ricompila ed esegue 16 processi in sequenza, sovrascrivendo i risultati con lo stesso nome. Conservare prima una copia se si desidera mantenere le misure originali.
4. Eseguire `build_river.cmd`: compila e lancia la sonda River, salvando stdout/stderr.

Esempio per il primo controllo, senza avviare benchmark:

```powershell
& 'C:\Users\GoryNickel\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' `
  'C:\Users\GoryNickel\Documents\GitHub\GTO-Solver\docs\research\preflop_12h_evidence_20260910\averaging_counterexample.py'
```

Il sampler usa un solo thread; il numero di processori logici non modifica questa condizione. Le repliche A/B sono misure descrittive a seed fisso, non una qualifica multi-seed di convergenza. Il confronto automatico fra varianti verifica strategia root, EV, standard error, numero di infoset e metrica di risposta; non confronta una policy completa esportata.

## Semantica dei dati

`observed_peak_working_set_bytes` legge il picco riportato dal processo ai polling; `observed_max_private_bytes` è il massimo delle letture campionate ogni 50 ms. Non sono il payload dei regret e non sono la memoria dell'applicazione GTO+.

`solve_seconds` include anche la valutazione e le risposte apprese, ma esclude parte della preparazione e la scrittura finale. `wall_seconds` comprende l'intero processo e la granularità del polling. Nessuna metrica di questa cartella dimostra un solve globale entro 12 ore.

I timer `bucket_nested` e `canonical_nested` sono già compresi in `key_inclusive`. Gli istogrammi `VISITS` sono acquisiti subito dopo il training, prima delle valutazioni; `PROBE all` include anche le tabelle delle risposte apprese. Una query mancante riceve la fallback policy uniforme prevista dal sorgente.

Il controesempio normalizza gli accumulatori attesi: non equipara l'aspettativa di un rapporto al rapporto delle aspettative. Il test con pesi cubici usa la proporzione temporale effettiva del discount locale. È una verifica dell'estimatore, non una simulazione di poker.

La sonda River seleziona una shape poco profonda nel primo task canonico, non un campione rappresentativo dell'intero catalogo. L'errore di ricomposizione nullo è una proprietà interna del risultato, non una certificazione indipendente della correttezza della BR.
