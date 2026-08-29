# Normalizzazione del contratto production DCFR — 2026-08-29/30

## Esito

La suite AHKHQH/TH7D6S/TSTC9D usa ora un solo contratto matematico production:

```text
algorithm          DCFR exact alternating
alpha              1.5
beta               0 (discount dei regret non positivi = 1/2)
gamma              2
averaging_delay    0
average            t^2 * reach proprio * strategia corrente
state              signed scaled_uint16_regret_strategy
best response      exact
threads massimi    8 (7 worker + thread chiamante)
```

Non esistono selettori per fixture, fingerprint, board o classe small/medium/
large. I riferimenti GTO+ e la semantica dei tre giochi sono rimasti invariati.

La normalizzazione strutturale, il build Release e i test mirati passano. Il
gate complessivo richiesto non e' pero' chiuso: AHKHQH raggiunge exact dEV
inferiore all'1%, ma il root EV signed/packed resta fuori dalla tolleranza GTO+
di 0,05 ante. Il delta scende da `+0,310280` a 100 iterazioni a `+0,056125` a
2.000, senza passare. TH7D6S e TSTC9D passano il root gate.

Conseguenza: l'audit/prototipo RBP resta **BLOCCATO**. Avviarlo ora violerebbe il
prerequisito esplicito di riprendere RBP soltanto dopo una baseline production
normalizzata e verificata, oltre a rendere ambiguo ogni differenziale.

## Provenienza e audit iniziale

- HEAD iniziale: `23d9b81d0c175e084047f09f47c9ed60a43a33b8`.
- Branch: `main`, allineato a `origin/main` all'inizio dell'attivita'.
- Working tree utente preservato: `.reasonix/` e `.tmp/` non sono stati letti,
  modificati o inclusi nei commit.
- I commit annunciati dal task erano presenti:
  `be99b41` (AHKHQH signed DCFR) e `23d9b81` (TH7D6S signed DCFR).

Differenze trovate rispetto al contratto comune:

| Fixture | Alpha | Gamma | Delay | Worker aggiuntivi | Thread max | Limite diagnostico |
|---|---:|---:|---:|---:|---:|---:|
| AHKHQH | 1,4 | 2 | 10 | 4 | 6 | nessuno |
| TH7D6S | 1,5 | 2 | 40 | 5 | 6 | nessuno |
| TSTC9D | 1,9 | 3 | 0 | 7 | 8 | 170 |

Il precedente profilo TSTC9D `1.9/0/3` era tuning storico della fixture, non
autorita' production. Il limite a 170 iterazioni avrebbe inoltre censurato il
time-to-target del profilo comune ed e' stato rimosso.

`parallel_action_depth` e' un nome legacy: nel percorso corrente rappresenta il
numero di worker aggiuntivi creati dal traversal, non una soglia matematica di
profondita'. Il valore comune `7`, insieme al thread chiamante, applica il tetto
di otto thread richiesto senza una riscrittura dello scheduler.

## Autorita' del contratto e averaging

`docs/ADR_0002_MEMORY_BOUNDED_EXACT_SOLVER.md`, i default di
`PostflopSolveOptions` e il backend concordano su `alpha=1.5`, `beta=0`,
`gamma=2`. All'iterazione `t`, il backend signed applica ai regret precedenti:

```text
positive:     (t-1)^alpha / ((t-1)^alpha + 1)
non-positive: 1/2
```

e accumula l'average dalla prima iterazione con peso `t^gamma`, moltiplicato
per il reach proprio dell'infoset. Durante l'audit e' stato corretto il ramo
scaled signed non-fused, che amplificava l'accumulato precedente e ometteva il
reach. Una regressione verifica esplicitamente il rapporto di peso `1:4` fra
prima e seconda iterazione con `gamma=2`. I tre benchmark principali usano il
kernel fused gia' corretto: le loro curve sono risultate identiche prima/dopo,
fornendo un differenziale numerico indipendente.

## Configurazione finale e golden strutturali

Tutte le fixture usano DCFR `1.5/0/2`, delay zero, stato signed scaled uint16,
certificazione base ogni 20 iterazioni, sette worker aggiuntivi e massimo otto
thread. I dati GTO+ (tempo, RAM, root EV, frequenze e target) non sono cambiati.

| Benchmark | Fingerprint | Fisici | Canonici | Scale decisione | Infoset | Action entry | Solver state |
|---|---|---:|---:|---:|---:|---:|---:|
| AHKHQH | `fnv1a64:1b6f30a930cd9bd0` | 165.774 | 46.065 | 18.438 | 595.626 | 1.288.290 | 5.300.664 B |
| TH7D6S | `fnv1a64:fcba3c9eff1b7147` | 378.834 | 378.834 | 147.256 | 36.596.832 | 83.318.592 | 334.452.416 B |
| TSTC9D | `fnv1a64:731bf9562e90e792` | 2.791.872 | 1.758.624 | 630.596 | 145.524.152 | 366.890.152 | 1.472.605.376 B |

I golden sono misure prodotte dal layout corrente, non valori scelti per far
passare il runner. Il runner valida ora anche `decision_node_scales` e pubblica
nel JSON i parametri DCFR, incluso `beta=0`.

## Baseline a iterazioni fisse

I tempi sono singoli run Release isolati, non mediane. `Root P0/P1` sono i
payoff netti del profilo certificato; BR P0/P1 sono best response exact. I
contatori sono cumulativi al checkpoint.

### AHKHQH

| Iter | dEV % | NashConv norm. | Root P0/P1 | BR P0/P1 | Traversal s | Solver s | ms/iter | Nodi | Decisioni | Chance outcome | Showdown | Regret/strategy entry |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 20 | 8,247774 | 0,13478628 | -1,254397 / 1,254397 | 2,044712 / 3,346739 | 0,131884 | 0,185028 | 6,5942 | 893.449 | 627.252 | 377.795 | 246.529 | 21.699.378 |
| 40 | 2,231236 | 0,03628222 | -0,947725 / 0,947724 | -0,055230 / 1,506519 | 0,272921 | 0,360583 | 6,8230 | 1.849.861 | 1.310.250 | 775.723 | 499.116 | 45.258.671 |
| 60 | 0,924175 | 0,01811576 | -0,901153 / 0,901156 | -0,531483 / 1,256116 | 0,416826 | 0,540864 | 6,9471 | 2.798.884 | 1.989.421 | 1.176.979 | 748.191 | 68.705.205 |
| 80 | 0,655702 | 0,01193216 | -0,891013 / 0,891012 | -0,676008 / 1,153292 | 0,548762 | 0,707646 | 6,8595 | 3.704.868 | 2.640.010 | 1.557.685 | 983.713 | 91.185.853 |
| 100 | 0,507845 | 0,00828349 | -0,881583 / 0,881581 | -0,753382 / 1,084719 | 0,675014 | 0,866544 | 6,7501 | 4.586.968 | 3.278.074 | 1.923.403 | 1.208.554 | 113.223.444 |

Stato solver `5.300.664 B`; peak RSS `167.276.544 B`. Root EV GTO+ gate a
100: `19,460280` contro `19,15`, delta `+0,310280`, FAIL.

### TH7D6S

| Iter | dEV % | NashConv norm. | Root P0/P1 | BR P0/P1 | Traversal s | Solver s | ms/iter | Nodi | Decisioni | Chance outcome | Showdown | Regret/strategy entry |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 20 | 11,666855 | 0,21234250 | -1,656058 / 1,656058 | 0,560645 / 3,473863 | 3,703204 | 5,425539 | 185,1602 | 6.679.383 | 4.918.761 | 1.543.445 | 1.680.747 | 1.361.476.645 |
| 40 | 3,063918 | 0,05732045 | -1,358061 / 1,358061 | -0,775917 / 1,865005 | 7,827897 | 10,943352 | 195,6974 | 14.063.044 | 10.475.565 | 3.221.309 | 3.418.331 | 2.899.760.537 |
| 60 | 1,332874 | 0,02646677 | -1,296362 / 1,296362 | -1,046740 / 1,549608 | 12,018001 | 16,421758 | 200,3000 | 21.298.612 | 15.941.627 | 4.880.645 | 5.098.906 | 4.414.737.260 |
| 80 | 0,806385 | 0,01534232 | -1,278368 / 1,278368 | -1,140077 / 1,431581 | 16,008549 | 21,763136 | 200,1069 | 28.481.560 | 21.374.046 | 6.533.677 | 6.760.737 | 5.916.859.741 |
| 100 | 0,594914 | 0,01120892 | -1,272476 / 1,272476 | -1,172540 / 1,385509 | 20,504767 | 27,787675 | 205,0477 | 35.652.764 | 26.799.675 | 8.181.173 | 8.417.836 | 7.416.871.654 |
| 120 | 0,512104 | 0,00931346 | -1,270537 / 1,270537 | -1,190881 / 1,367837 | 24,654845 | 33,282473 | 205,4570 | 42.854.613 | 32.240.961 | 9.839.293 | 10.089.913 | 8.922.538.829 |

Stato solver `334.452.416 B`; peak RSS `797.753.344 B`. Root EV gate a 120:
`8,204980` contro `8,22198`, delta `-0,017000`, PASS.

### TSTC9D

| Iter | dEV % | NashConv norm. | Root P0/P1 | BR P0/P1 | Traversal s | Solver s | ms/iter | Nodi | Decisioni | Chance outcome | Showdown | Regret/strategy entry |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 20 | 15,447066 | 0,30052270 | 0,486195 / -0,486195 | 2,823028 / 1,985336 | 17,396815 | 23,478397 | 869,8407 | 29.646.967 | 22.017.088 | 7.550.565 | 7.216.063 | 6.393.749.957 |
| 40 | 7,438142 | 0,12929681 | 0,370503 / -0,370502 | 1,560605 / 0,508144 | 35,650856 | 47,559158 | 891,2714 | 61.213.711 | 46.153.513 | 15.362.181 | 14.210.676 | 13.389.777.954 |
| 80 | 3,190583 | 0,04973978 | 0,443274 / -0,443275 | 0,953767 / -0,157931 | 73,191589 | 97,479544 | 914,8949 | 123.334.955 | 93.886.098 | 30.887.661 | 27.735.155 | 27.169.845.127 |
| 120 | 1,640371 | 0,02728278 | 0,477827 / -0,477826 | 0,740287 / -0,303761 | 110,290671 | 146,960602 | 919,0889 | 184.445.181 | 140.864.907 | 46.291.285 | 41.004.349 | 40.695.440.603 |

Stato solver `1.472.605.376 B`; peak RSS `1.969.483.776 B`. Root EV gate a
120: `8,477827` contro `8,50165`, delta `-0,023823`, PASS.

## Time-to exact dEV inferiore all'1%

| Benchmark | Iter | dEV % | NashConv norm. | Traversal s | Solver s | Root EV / riferimento | Root | Tempo GTO+ | Peak RSS / riferimento | Stato solver |
|---|---:|---:|---:|---:|---:|---:|---|---:|---:|---:|
| AHKHQH | 80 | 0,655702 | 0,01193216 | 0,577730 | 0,702062 | 19,565535 / 19,15 | FAIL | 1,71 s, PASS | 167.161.856 / 8.000.000, FAIL | 5.300.664 B, PASS |
| TH7D6S | 80 | 0,806385 | 0,01534232 | 16,885724 | 21,652627 | 8,173389 / 8,22198 | PASS | 17,66 s, FAIL | 798.064.640 / 399.000.000, FAIL | 334.452.416 B, PASS |
| TSTC9D | 202 | 0,991865 | 0,01675701 | 201,941454 | 247,928298 | 8,494698 / 8,50165 | PASS | 116,09 s, FAIL | 1.969.573.888 / 2.000.000.000, PASS | 1.472.605.376 B, PASS |

La replica TST post-fix ha prodotto gli stessi valori numerici a 202 iterazioni
ma `260,058 s`, con spike di sistema osservabili fra 80 e 120; e' diagnostica di
stabilita' matematica, non sostituisce il campione temporale isolato in tabella.

AHK e TH arrivano sotto l'1% a 60 e 80 nei fixed checkpoint; il runner
target-driven usa certificazione adattiva e certifica AHK al successivo punto
80. Nessun parametro DCFR e' stato cambiato fra i tre run.

## Confronto con le baseline storiche

Le baseline DCFR+ packed di AHK/TH e la baseline TST-specifica DCFR
`1.9/0/3` restano evidenza storica, ma sono **superseded per confronti del
motore production comune**. In particolare il TST storico arrivava a circa
`0,985760%` a 170 iterazioni e `153,176351 s`; il contratto comune richiede 202
iterazioni e `247,928298 s` nel campione isolato. Il peggioramento e' il
risultato richiesto della normalizzazione, non viene compensato con tuning.

## Validazione

- configurazione/build MSVC Release: PASS;
- test del contratto fixture/GTO+ immutabile: PASS;
- Phase 7 production, signed regret, exact BR, resume e average 1:4: PASS;
- CTest Release completo: 20/20 PASS in 202,66 s;
- riferimento GTO+, asymmetric range, serial/parallel, isomorfismo ed exact BR:
  PASS nel CTest finale;
- layout e `decision_node_scales`: PASS nei sei run fixed/target;
- dEV exact inferiore all'1%: PASS su tutte le fixture;
- root EV: TH PASS, TST PASS, AHK FAIL;
- gate tempo: AHK PASS, TH FAIL, TST FAIL;
- peak RSS: AHK FAIL, TH FAIL, TST PASS;
- `solver_state_bytes`: PASS su tutte le fixture.

Il report non aggrega questi gate: un PASS di dEV o stato solver non compensa un
FAIL root, tempo o peak RSS.

## HEAD finale e stato RBP

Il codice normalizzato parte da `23d9b81d0c175e084047f09f47c9ed60a43a33b8`;
gli hash dei commit atomici finali sono riportati nel log e nell'handoff della
task, evitando una auto-referenza impossibile nel commit che contiene questo
file.

Stato RBP: **BLOCCATO**. Il prossimo passo prioritario e' diagnosticare il floor
AHKHQH signed/packed contro il root GTO+ senza cambiare contratto, fixture,
riferimento o tolleranza. Solo dopo root PASS su tutti e tre si puo' avviare la
telemetria RBP read-only `1.5/0/2`; `beta=0.5` resta vietato.
