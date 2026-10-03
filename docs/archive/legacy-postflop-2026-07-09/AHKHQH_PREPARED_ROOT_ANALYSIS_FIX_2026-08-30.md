# AHKHQH prepared-root analysis differential — 2026-08-30

## Esito

La divergenza AHKHQH non era un floor del backend signed/packed. Era un bug di
dispatch nell'overload:

```text
analyze_postflop_node(PostflopPreparedTree&, checkpoint, node)
```

Quando `prepare_analysis=true` materializzava il browser fisico, l'overload
usava sempre `analysis_layout`, incluso per il root. Il root prepared evitava
quindi `profile_root_values()` sul layout production canonico e veniva valutato
dal policy traversal fisico generico. Certification e API direct usavano invece
il percorso canonico capacity-dispatched autorevole.

Il contratto production, le fixture, i riferimenti, le tolleranze e i parametri
DCFR non sono stati modificati. RBP non e' stato implementato.

## Differential pre-fix

Checkpoint AHKHQH production: DCFR exact alternating, signed uint16,
`alpha=1.5`, `beta=0`, `gamma=2`, delay zero, 80 iterazioni, otto thread.
Il DAG era realmente compresso: 165.774 nodi fisici contro 46.065 canonici.

| Percorso | Profile P0 | GTO+ EV P0 |
|---|---:|---:|
| A. exact certification | -0,891013 | 19,108987 |
| B. direct canonical root analysis | -0,891013 | 19,108987 |
| C. prepared physical root analysis | -0,434465 | 19,565535 |

A e B coincidevano; C differiva di circa `+0,456548` ante. Il test aggiunto
falliva sul codice precedente con:

```text
prepared compressed root analysis matches direct canonical authority
```

Nello stesso differential, i nodi fisici non-root `check` e `bet_20` erano
entrambi analizzabili. Il problema era quindi confinato alla scelta del layout
per il root, non alla costruzione del browser.

## Correzione

L'overload prepared seleziona ora:

- layout production canonico per il root, quando il tree fisico del layout
  production e' stato rilasciato;
- `analysis_layout` fisico per i nodi non-root del browser.

La condizione dipende esclusivamente dalla struttura del layout e dall'identita'
del root canonico. Non contiene benchmark ID, fingerprint, board o soglie di
dimensione. Solver, regret, average, checkpoint e traversal di training non
sono cambiati.

## Test regressivo

`gtosd_gto_plus_reference_tests` copre permanentemente:

- physical != canonical;
- `prepare_analysis=true`;
- DCFR signed `1.5/0/2`, delay zero;
- `ScaledUint16RegretStrategy`;
- confronto certification/direct/prepared con tolleranza `1e-9`;
- analisi prepared dei child fisici `check` e `bet_20`.

Il test non si limita a verificare `has_value()` e fallisce sul codice pre-fix.

## Differential post-fix

| Percorso | Profile P0 | GTO+ EV P0 |
|---|---:|---:|
| A. exact certification | -0,891013 | 19,108987 |
| B. direct canonical root analysis | -0,891013 | 19,108987 |
| C. prepared root analysis | -0,891013 | 19,108987 |

Entrambi i child fisici restano analizzabili. Anche il test del root lock
esterno continua a passare con delta massimo di probabilita' `5,55e-17`.

## Benchmark finali root

| Benchmark | Iter | dEV % | Certification P0 | Root GTO+ misurato | Riferimento | Delta | Root |
|---|---:|---:|---:|---:|---:|---:|---|
| AHKHQH fixed | 100 | 0,507845 | -0,881583 | 19,118417 | 19,15 | -0,031583 | PASS |
| AHKHQH target | 80 | 0,655702 | -0,891013 | 19,108987 | 19,15 | -0,041013 | PASS |
| TH7D6S sanity | 80 | 0,806385 | -1,278368 | 8,221632 | 8,22198 | -0,000348 | PASS |
| TSTC9D target sanity | 202 | 0,991865 | 0,494698 | 8,494698 | 8,50165 | -0,006952 | PASS |

Il run TST sanity ha impiegato `313,325 s` ed era contaminato da rallentamenti
di sistema; conferma matematica e root, ma non sostituisce la baseline timing.

Il report aggregato AHK continua a pubblicare diagnostici separati rossi
(frequenze/EV child e residuo zero-sum del compute float rispetto alla soglia
`1e-11`). Questo non cambia il risultato richiesto: `correctness_gate` del root
e' esplicitamente PASS. Nessun gate viene compensato da un altro.

## Validazione

- build MSVC Release: PASS;
- test regressivo pre-fix: FAIL con differential A=B!=C;
- test regressivo post-fix: PASS con A=B=C;
- child physical browser: PASS;
- CTest Release completo: 20/20 PASS in 213,31 s;
- AHK fixed e target root: PASS;
- TH root sanity: PASS;
- TST root sanity: PASS;
- fixture, riferimento `19,15`, tolleranza `0,05` e contratto DCFR: invariati.

## Stato RBP

**PREREQUISITO ROOT EV SUPERATO.** AHKHQH, TH7D6S e TSTC9D hanno root PASS,
il contratto production e' invariato e CTest e' verde. RBP e' ora sbloccabile,
ma non e' stato avviato in questa attivita'.
