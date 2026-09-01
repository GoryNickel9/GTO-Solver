# Sync-PCFR postflop trajectory gate

Data: 2026-09-01

Fixture autoritativa: `GTP-AHKHQH-101`

Outcome: **C. SYNC-PCFR POSTFLOP FAMILY CLOSED UNDER THE STRICT CONTRACT**

## Analisi

### Obiettivo

Risolvere l'unico survivor del recheck algoritmico precedente: verificare se
la compressione temporale Sync-PCFR osservata su Kuhn sopravvive sulla
traiettoria full-tree di un vero layout postflop Short Deck.

Il probe non applica una fase Sync. Esegue invece Pure CFR non compresso e,
prima di ogni update, misura esattamente il primo pursuit time compatibile con
lo state `Q` corrente e con gli action counterfactual values appena calcolati.
Questo evita di attribuire convergenza a una fase compressa prima di averne
provato la validità economica.

### Contratto e invarianti

- full tree e full chance, nessun sampling o bucketing;
- exact outcomes, exact best response e certificazione production;
- CPU-only, massimo 8 thread;
- stesso `ScaledUint16RegretStrategy`, reinterpretato come cumulative signed
  `Q` più cumulative average unsigned;
- tie break pure deterministico verso l'action index più piccolo;
- policy media, non policy corrente, usata per EV/BR/NashConv;
- nessun phase weight applicato;
- nessun resume, root lock o real-node replay combinato con il probe;
- implementation compile-time gated da
  `GTOSD_ENABLE_PURE_CFR_TRAJECTORY_PROBE=ON`, default `OFF`.

Lo state resta byte-identico nel modello:

```text
2 bytes signed Q/action
+ 2 bytes unsigned average/action
+ 2 float scales/decision node
```

### Rischi controllati

Il paper Sync-PCFR usa una dinamica con aggiornamento congiunto. Per non
chiudere la famiglia su una sola variante, il gate AHK è stato eseguito sia
con alternating traversal sia con il simultaneous traversal diagnostico.

## Piano

1. implementare il path Pure-CFR soltanto per lo state signed-scaled;
2. validare policy argmax, update `Q`, average, signed encoding e exact
   certification su un river enumerabile;
3. misurare una traiettoria AHK bounded @20;
4. estendere una sola volta ad AHK @80 se @20 mostra fase globale 1;
5. ripetere @80 con simultaneous traversal;
6. fermarsi prima di TH/TST se nessuna iterazione AHK è comprimibile.

## Implementazione

Il probe aggiunge:

- `PostflopSolveOptions::diagnostic_pure_cfr_trajectory`;
- telemetry per iterazione: minimum phase, pursuit finiti, pursuit unitari e
  costo scan;
- policy corrente `argmax Q` sul backend action-major signed `uint16`;
- update cumulative `Q += action_cf_value` e average reach lineare;
- output strutturato nel runner `benchmark-gto-plus`;
- `tools/run_sync_pcfr_trajectory_probe.ps1` per run bounded riproducibili.

Il flag CMake è default-off. Senza il flag, una richiesta runtime del probe
restituisce `InvalidConfiguration`; il traversal production non contiene il
ramo Pure/Sync attivo.

## Validazione

### River gate

Il test Release verifica:

- exact certification finita;
- prima average strategy coerente con il pure tie break;
- cumulative Q signed con valori negativi;
- telemetry valida per ogni iterazione;
- presenza di almeno una fase river comprimibile;
- NashConv in diminuzione;
- state e scale deterministici su due processi logici indipendenti.

Risultato: `gtosd_phase7_tests` PASS nella build con probe abilitato.

Confine compile-time verificato in Release:

- research build `GTOSD_ENABLE_PURE_CFR_TRAJECTORY_PROBE=ON`:
  `gtosd_phase7_tests` PASS;
- production-default build `OFF`: `gtosd_phase7_tests` PASS, inclusa la
  rejection `InvalidConfiguration` di una richiesta runtime del probe;
- production-default build `OFF`: CTest completo `27/27` PASS.

Lo smoke finale del runner PowerShell 5.1 AHK @20 riproduce dEV
`84,379840118%`, normalized NashConv `1,652681702`, `0/20` iterazioni
comprimibili e minimum/maximum phase `1/1`.

### AHK @20 alternating

| metrica | valore |
|---|---:|
| iterazioni Pure | `20` |
| iterazioni comprimibili | `0/20` |
| minimum phase | `1` |
| maximum dei minimum phase | `1` |
| dEV | `84,379840%` |
| normalized NashConv | `1,652681702` |
| traversal | `0,170479 s` |
| peak RSS | `166.375.424 B` |
| solver state | `5.300.664 B` |

### AHK @80 alternating

| metrica | Pure trajectory | DCFR `1.5/0/2` |
|---|---:|---:|
| dEV | `32,870858965%` | `0,655665276%` |
| normalized NashConv | `0,603126123` | `0,011931776` |
| traversal | `0,8171025 s` | `0,8042340 s` |
| peak RSS | `166.486.016 B` | `166.309.888 B` |
| solver state | `5.300.664 B` | `5.300.664 B` |

Pure CFR è `50,1336x` peggiore in dEV e `50,5479x` peggiore in normalized
NashConv a pari iterazioni; non offre neppure un vantaggio di traversal.

Pursuit telemetry @80:

```text
compressible iterations = 0 / 80
minimum phase per iteration = 1, per tutte le 80 iterazioni
finite pursuits = 6.900.512
unit pursuits = 1.045.791 (15,1553%)
```

La percentuale non deve essere grande: un singolo pursuit unitario in qualunque
infoset impone fase globale 1.

### AHK @80 simultaneous

| metrica | valore |
|---|---:|
| iterazioni comprimibili | `0/80` |
| minimum phase | `1` |
| maximum dei minimum phase | `1` |
| dEV | `29,988372515%` |
| normalized NashConv | `0,562970399` |
| traversal | `1,6534897 s` |
| finite pursuits | `6.324.199` |
| unit pursuits | `1.061.340` |

L'aggiornamento congiunto migliora lievemente la metrica Pure, ma non cambia
il fatto determinante: nessuna delle 80 iterazioni è comprimibile e il
traversal costa oltre il doppio del comparator DCFR.

### Gate RAM dell'alternativa delta-buffer

Con fase globale sconosciuta fino al termine del traversal, le implementazioni
generiche hanno due opzioni:

1. seconda traversata per applicare il peso trovato;
2. conservare il delta action-entry.

Sul TST persino il delta minimo `float32` richiederebbe:

```text
366.890.152 actions * 4 B = 1.467.560.608 B aggiuntivi
1.971.036.160 B fresh peak + 1.467.560.608 B = 3.438.596.768 B
```

Un delta `float64` richiederebbe:

```text
366.890.152 actions * 8 B = 2.935.121.216 B aggiuntivi
```

Entrambe le rappresentazioni fanno fallire il cap 2 GB; `float64` lo supera
anche senza sommare lo state esistente. La doppia traversata non produce alcun
beneficio quando la fase è sempre 1.

### Test non eseguiti

- TH7D6S non eseguito;
- TSTC9D non eseguito;
- nessun target-driven o processo di certificazione multipla.

Sono skip obbligatori del kill gate AHK, non casi mancanti.

## Decisioni

1. Il vantaggio Kuhn di Sync-PCFR non si trasferisce al layout AHK.
2. Alternating e simultaneous concordano: global minimum phase sempre 1.
3. Pure CFR converge circa 50x peggio del comparator a pari iterazioni AHK.
4. Non esiste un percorso RAM-safe per recuperare un phase benefit assente.
5. La famiglia Pure/Sync-PCFR è quindi **respinta** prima di TH/TST.
6. Production resta alternating signed DCFR `1.5/0/2`; nessun enum,
   checkpoint format o default è stato modificato.

Outcome unico: **C. SYNC-PCFR POSTFLOP FAMILY CLOSED UNDER THE STRICT
CONTRACT.** Con questa chiusura, il survivor del recheck algoritmico è
esaurito.

## Passo successivo

Tornare alla **constraint/black-box governance**: verificare con un esperimento
GTO+ discriminante se la differenza residua deriva da un contratto di risorsa
diverso (working set reale oltre 2 GB), da una semantica di convergenza diversa
o da un'architettura proprietaria non inferibile. Non aprire un'altra famiglia
software exact senza una nuova ipotesi falsificabile.
