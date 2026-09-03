# Contratto risorse 2 GiB e rivalutazione della frontier — 2026-09-01

> **SUPERSEDED 2026-09-02.** Questo documento conserva la decisione e le prove
> del 2026-09-01. Il contratto corrente usa il riferimento peak RSS specifico
> della fixture (`8.000.000 / 399.000.000 / 2.000.000.000 B`, confronto `<=`)
> come gate GTO+. Il cap comune `<2 GiB` è soltanto diagnostico.

## Outcome

**B. CONTRATTO 2 GiB ADOTTATO; NESSUNA FAMIGLIA ESCLUSA DIVENTA
PROMOVIBILE.**

Il limite operativo comune del processo solver è ora:

```text
peak_rss_bytes < 2 GiB = 2.147.483.648 B
```

Il confronto è stretto. `2 GiB` non significa `2.000.000.000 B`. I valori
storici `solver_memory_bytes` di GTO+ (`8.000.000`, `399.000.000` e
`2.000.000.000 B`) restano immutati nelle fixture come riferimenti esterni e
come limiti dello **stato solver** GTOSD; non sono più usati come limite del
working set del processo.

Production resta invariata: CPU/RAM-only, massimo otto thread, exact outcome
enumeration ed exact best response, alternating signed DCFR `1.5/0/2`,
`ScaledUint16RegretStrategy`, nessun sampling, bucketing, GPU, fast-math o
logica specifica per fixture.

## Evidenza black-box GTO+

Il run manuale TSTC9D del 2026-09-01 usa GTO+ v1.6.9, board `TsTc9d`, otto
thread e target visuale `1%`. Il pannello finale mostra:

| Campo GTO+ | Valore |
|---|---:|
| Solve time | `121,77 s` |
| dEV | `0,146 (0,91%)` |
| Memoria disponibile di sistema | `17,7 GB` |

Il valore `17,7 GB` è memoria disponibile del PC e non memoria del processo.
Il monitor per-processo ha misurato un massimo campionato di
`2.061.209.600 B`; Windows riporta peak working set `2.061.889.536 B`, pari al
`96,014%` del nuovo cap e con `85.594.112 B` di margine. Il processo supera il
vecchio cap decimale dopo circa `51,25 s`, ma passa il contratto binario.

Questo singolo run è una probe diagnostica di risorse. Non sostituisce il
riferimento temporale autorevole `116,09 s`, che resta il primo punto GTO+
strettamente sotto `1%` registrato nella fixture.

## Semantica dEV e pot confermato

L'utente ha confermato un pot iniziale TSTC9D pari a `16` ante. Il punto
visualizzato è quindi numericamente coerente con la definizione già congelata
nella suite:

```text
gain[p] = BR_value[p] - profile_value[p]
Target dEV percent = 100 * max(gain[CO], gain[BTN]) / initial_pot
                    = 100 * 0,146 / 16
                    = 0,9125%
                    -> 0,91% alla precisione visuale GTO+
```

Questo chiude l'ambiguità di unità e normalizzazione del target. Non dimostra,
da solo, che due strategie interne siano identiche: correttezza, root EV ed
exact BR restano gate separati.

## Separazione dei due contratti di memoria

| Contratto | Metrica | Soglia | Confronto |
|---|---|---:|---|
| Riferimento esterno/stato AHKHQH | `solver_state_bytes` | `8.000.000 B` | `<=` |
| Riferimento esterno/stato TH7D6S | `solver_state_bytes` | `399.000.000 B` | `<=` |
| Riferimento esterno/stato TSTC9D | `solver_state_bytes` | `2.000.000.000 B` | `<=` |
| Risorsa desktop comune | `peak_rss_bytes` del processo | `2.147.483.648 B` | `<` |

Il report `gtosd.gto_plus_convergence_run.v2` pubblica entrambi. Il wrapper
multi-processo produce `gtosd.gto_plus_convergence_summary.v2`, calcola uso e
margine del cap e non confonde più il peak RSS con il dato esterno di stato.

## Rivalutazione delle esclusioni

Le proiezioni usano la baseline final-head TST `1.969.922.048 B` peak e
`1.472.605.376 B` state. La sola variazione di state viene sommata al peak
baseline; è una stima favorevole che non include nuovi transienti nascosti.

| Famiglia/candidato | Peak proiettato | Gate 2 GiB | Gate indipendente | Decisione |
|---|---:|---:|---|---|
| Tile-local K8 | `2.112.559.200 B` | PASS, margine `34.924.448 B` | shadow `0,948–0,977x`, quindi più lento | chiuso |
| Tile-local K16 | `2.040.199.152 B` | PASS, margine `107.284.496 B` | shadow più lento | chiuso |
| Tile-local K32 | `2.004.228.224 B` | PASS, margine `143.255.424 B` | power-of-two `0,972x` | chiuso |
| Tile-local K64 | `1.986.567.296 B` | PASS, margine `160.916.352 B` | shadow più lento | chiuso |
| Per-hand scale | `3.129.070.496 B` | FAIL di `981.586.848 B` | `0,874x` | chiuso |
| Predictive CFR lower bound | `2.412.654.048 B` | FAIL di `265.170.400 B` | terzo payload necessario | chiuso |
| Lazy CFR lower bound | `2.552.018.656 B` | FAIL di `404.535.008 B` | residuo reach necessario | chiuso |
| Sync-PCFR delta float32 | `3.438.596.768 B` | FAIL di `1.291.113.120 B` | AHK `0/80` fasi comprimibili | chiuso |
| Raw/direct 8 B/action | `3.432.437.888 B` minimo | FAIL di `1.284.954.240 B` | richiede anche BR `<=8,604297 s` | chiuso |
| FD-FTRL / FD-OMD | baseline memory-neutral | PASS | costo locale `3,513x / 3,303x` RM | chiuso |
| S6 `1.5/0/5` | baseline memory-neutral | PASS | TST pair: tempo `+8,018810%`, iter `+0,990099%`, sotto i gate `>=10%` | chiuso |

Il nuovo cap riapre soltanto la colonna RAM di K8/K16/K32. Non riapre il
candidato complessivo perché i relativi gate di throughput sono già falliti
su replay/shadow reali. Le rappresentazioni compact che riducono RAM restano
chiuse dai gate numerici o di convergenza; il producer streaming isolato resta
limitato a circa `1,041x`. Nessun target-driven costoso è giustificato.

## Modifiche implementate

- aggiunto alle tre fixture il cap comune, unità e confronto espliciti;
- separato nel report CLI il riferimento esterno di memoria dal gate peak RSS;
- aggiornato il wrapper a schema summary v2 e confronto stretto `<`;
- congelati cap e semantica nel test del contratto production;
- congelati per TST pot `16`, `0,146 ante` e `0,91%` come golden di
  normalizzazione;
- mantenuti invariati algoritmo, layout, stato, checkpoint e riferimenti GTO+.

## Validazione

- parse PowerShell del wrapper: PASS;
- test del contratto fixture, inclusi cap/unità/confronto e golden TST:
  PASS;
- build Release del CLI con MSVC, `/W4 /WX`: PASS;
- AHKHQH target-driven diagnostico: `0,655665% @80`, `0,670567 s`, report
  `gtosd.gto_plus_convergence_run.v2`; state gate
  `5.300.664 <= 8.000.000 B` PASS e peak RSS gate
  `166.502.400 < 2.147.483.648 B` PASS;
- full CTest Release: `27/27` PASS in `195,28 s`;
- rerun focalizzato dopo l'ultimo hardening delle asserzioni: `2/2` PASS
  (`gtosd_gto_plus_action_catalog_smoke` e
  `gtosd_production_dcfr_contract`) in `6,94 s`.

Il run AHK verifica forma e comportamento del report; non è una nuova baseline
temporale promossa. Non è stato rieseguito TST target-driven perché il cambio
è nel contratto/report e il suo time FAIL è già quantitativamente indipendente.

## Decisione operativa

Il blocker corrente resta il tempo TSTC9D: `208,111772 s` contro il limite
`128,988889 s`. Il cambio GB→GiB corregge un contratto di risorse e rimuove i
falsi memory FAIL AHK/TH, ma non fornisce lo speedup `1,613409x` richiesto.
Non si promuove alcuna variante e non si modifica production.
