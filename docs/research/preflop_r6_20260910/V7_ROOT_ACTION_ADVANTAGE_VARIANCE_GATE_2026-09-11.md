# R6 — Gate della varianza del vantaggio d'azione v7

## Esito

`DIAGNOSTIC_PASS / ACTION_VALUES_UNRESOLVED / R6_GATE_FAIL`.

La nuova telemetria ricostruisce esattamente i regret Linear MCCFR senza cambiare la strategia. Le 27 classi in cui i due seed scelgono azioni correnti diverse hanno tutte un margine inferiore a due errori standard accoppiati in almeno un seed; 20 su 27 sono sotto due errori standard in entrambi. La divergenza non è quindi spiegata dall'averaging o dal viewer: il trainer sta ordinando azioni i cui vantaggi campionati sono ancora piccoli rispetto alla dispersione osservata.

Questo risultato non prova che l'unica causa sia rumore indipendente. I campioni seguono una policy non stazionaria e sono serialmente dipendenti; gli errori standard riportati sono diagnostici. La rappresentazione postflop può ancora introdurre bias o far divergere le policy di continuazione fra seed.

## Implementazione

Per ogni classe preflop, il trainer registra il vettore `Q_t(a) - V_t` della traversata CO al root con il peso Linear `t`. Un accumulatore Welford pesato conserva media, matrice di co-momento, deviazione standard e numerosità effettiva. Le differenze fra azioni sono calcolate sullo stesso campione root e usano la covarianza osservata.

Il percorso parallelo allega il vettore non pesato al delta locale. Il thread principale aggiorna i momenti durante la riduzione stabile dei job. Il percorso sequenziale usa la stessa routine. Refinement, response training e valutazioni post-hoc sono esclusi.

Il JSON espone `root_action_advantage_diagnostics` con:

- osservazioni, somma dei pesi e numerosità effettiva;
- media, deviazione standard ed errore standard per azione;
- matrice completa delle differenze accoppiate;
- errore di ricostruzione del regret cumulativo.

## Gate di non interferenza

La replay v7 seed 1 a 250.000 iterazioni produce:

| Controllo | Risultato |
|---|---:|
| Strategia root contro artefatto precedente | bit-identica |
| Infoset | `615.167`, identici |
| Root EV | `0,3078538a`, identico |
| Osservazioni root | `250.000` |
| Massimo errore di ricostruzione regret | `0` |

Anche le due replay da 2M sono bit-identiche agli artefatti con la precedente diagnostica dei regret, inclusi regret correnti, strategia root, root EV e infoset.

## Risultati a 2M

| Misura | Seed 1 | Seed 2 |
|---|---:|---:|
| Osservazioni root totali | 2.000.000 | 2.000.000 |
| Osservazioni per classe, minimo / mediana / massimo | 12.445 / 19.014 / 38.406 | 12.440 / 18.996 / 38.755 |
| Campioni effettivi, minimo / mediana / massimo | 9.372,75 / 14.241,78 / 28.773,50 | 9.275,88 / 14.198,06 / 29.043,20 |
| Migliore contro seconda azione sotto 1 SE | 40/81 | 42/81 |
| Migliore contro seconda azione sotto 2 SE | 57/81 | 62/81 |
| Migliore contro seconda azione sotto 3 SE | 67/81 | 68/81 |
| Mediana di `|margine| / SE` | 1,124 | 0,837 |

Il numero di iterazioni non coincide con i campioni di una singola classe. A 2M, la classe più rara riceve circa 12.400 osservazioni e 9.300 campioni effettivi. Mantenendo gli stessi rapporti servirebbero circa 16,1M iterazioni per almeno 100.000 osservazioni grezze in ogni classe, oppure 21,6M per almeno 100.000 campioni effettivi. È una proiezione aritmetica, non una garanzia di convergenza.

## AA: call contro raise 6a

| Run | Osservazioni AA | Campioni effettivi | `Call - Raise 6a` | `|margine| / SE` |
|---|---:|---:|---:|---:|
| Seed 1, 250k | 2.362 | 1.771,90 | `+1,0843 ± 0,4365a` | 2,48 |
| Seed 1, 2M | 19.014 | 14.219,68 | `+0,2280 ± 0,1376a` | 1,66 |
| Seed 2, 2M | 19.041 | 14.229,67 | `+0,1767 ± 0,1331a` | 1,33 |

Entrambi i seed assegnano al call un vantaggio medio maggiore del raise 6a. Il regret cumulativo è la somma pesata di questi vantaggi: per questo la strategia corrente preferisce call. Il margine a 2M è però inferiore a due errori standard descrittivi e la sua media cambia fra 250k e 2M. Il `95,53%` della chart resta la media storica delle strategie, non una trasformazione degli EV post-hoc.

## Classi con azione corrente discordante

`Δ` è il vantaggio dell'azione scelta dal seed 1 rispetto a quella scelta dal seed 2. Il segno si inverte fra i seed per definizione delle due decisioni osservate.

| Classe | Seed 1 | Seed 2 | Δ S1 ± SE | `|Δ|/SE` S1 | Δ S2 ± SE | `|Δ|/SE` S2 |
|---|---|---|---:|---:|---:|---:|
| 98o | Raise 10a | Call | +0,030 ± 0,126 | 0,24 | −0,255 ± 0,129 | 1,98 |
| 98s | Raise 10a | Call | +0,219 ± 0,177 | 1,23 | −0,238 ± 0,191 | 1,25 |
| A6o | Raise 6a | Call | +0,038 ± 0,071 | 0,54 | −0,016 ± 0,073 | 0,22 |
| A6s | Raise 10a | All-in | +0,197 ± 0,119 | 1,66 | −0,188 ± 0,134 | 1,40 |
| A7s | All-in | Raise 10a | +0,050 ± 0,114 | 0,44 | −0,091 ± 0,132 | 0,69 |
| A8s | Raise 10a | Raise 6a | +0,104 ± 0,123 | 0,84 | −0,298 ± 0,124 | 2,41 |
| AJo | All-in | Raise 10a | +0,108 ± 0,064 | 1,68 | −0,053 ± 0,071 | 0,76 |
| AKo | Raise 10a | Raise 6a | +0,086 ± 0,069 | 1,26 | −0,057 ± 0,075 | 0,76 |
| AKs | Raise 6a | Call | +0,118 ± 0,162 | 0,73 | −0,126 ± 0,154 | 0,82 |
| AQs | Raise 10a | All-in | +0,021 ± 0,113 | 0,19 | −0,210 ± 0,127 | 1,65 |
| J9s | Call | Raise 10a | +0,078 ± 0,201 | 0,39 | −0,130 ± 0,181 | 0,72 |
| JTo | Raise 10a | Call | +0,052 ± 0,104 | 0,50 | −0,043 ± 0,098 | 0,43 |
| JTs | Call | Raise 6a | +0,687 ± 0,175 | 3,93 | −0,071 ± 0,155 | 0,46 |
| K7s | Raise 6a | Call | +0,050 ± 0,124 | 0,41 | −0,575 ± 0,128 | 4,50 |
| K8o | Call | Fold | +0,065 ± 0,049 | 1,34 | −0,039 ± 0,047 | 0,84 |
| KJs | Call | Raise 10a | +0,276 ± 0,169 | 1,64 | −0,017 ± 0,156 | 0,11 |
| KK | All-in | Raise 6a | +0,034 ± 0,097 | 0,35 | −0,253 ± 0,114 | 2,22 |
| KQo | All-in | Raise 10a | +0,074 ± 0,063 | 1,18 | −0,018 ± 0,071 | 0,25 |
| KTo | Call | Raise 10a | +0,007 ± 0,106 | 0,07 | −0,053 ± 0,104 | 0,51 |
| KTs | Call | All-in | +0,149 ± 0,169 | 0,88 | −0,360 ± 0,157 | 2,29 |
| Q7s | Fold | Call | +0,005 ± 0,081 | 0,06 | −0,024 ± 0,081 | 0,30 |
| Q8o | Call | Fold | +0,085 ± 0,049 | 1,74 | −0,066 ± 0,048 | 1,38 |
| Q9s | Call | Raise 10a | +0,116 ± 0,199 | 0,59 | −0,068 ± 0,197 | 0,35 |
| QTs | Call | Raise 6a | +0,419 ± 0,169 | 2,48 | −0,145 ± 0,156 | 0,93 |
| T8o | Fold | Call | +0,026 ± 0,052 | 0,50 | −0,109 ± 0,049 | 2,24 |
| T8s | Raise 10a | Raise 6a | +0,239 ± 0,172 | 1,39 | −0,317 ± 0,175 | 1,81 |
| TT | All-in | Call | +0,025 ± 0,143 | 0,18 | −0,137 ± 0,132 | 1,04 |

Le 27 classi rappresentano 192 delle 630 combo fisiche. Per tutte, almeno uno dei due seed sceglie fra azioni che la diagnostica non separa a due errori standard. Il minimo dei due rapporti ha mediana `0,441` e non supera mai `1,402`.

## Validazione

Build MSVC Release `/W4 /WX`: PASS.

```text
gtosd_hu_preflop_trainer_isolation_tests   PASS
gtosd_hu_preflop_trainer_dependency_check PASS
gtosd_hu_preflop_sampling_tests            PASS
gtosd_hu_preflop_compiled_tests            PASS
gtosd_hu_preflop_abstraction_tests         PASS
gtosd_hu_preflop_parallel_tests            PASS
6/6 passed in 3,07 s
```

Il test sampling verifica finitezza, pesi, numerosità effettiva, simmetria delle differenze e ricostruzione del regret. Il test parallel richiede identità bit per bit della nuova telemetria con 1, 2, 4 e 8 worker.

SHA-256:

- eseguibile: `9089A4B4B6DCEB50F5C981A904CD165593593204B67DEA908FBB111D44D8AB3D`;
- replay 250k: `47085FE54A7B80611E5DABDF8C08E61A32E0A430A4BB807D800349E209B20F78`;
- seed 1 2M: `7F87DD45B19487BEE3CA0F899C14A1BA1A633426D07FFF9863DE504753974752`;
- seed 2 2M: `96B73E4C74EDC9246C00962DD98B8676196C8FF254DC1B223DFD471E7308347E`.

## Decisione

Non viene introdotta una nuova astrazione: v8 ha già aumentato l'occupazione dei bucket peggiorando l'accordo medio, mentre questa prova identifica margini root non risolti. Non è neppure giustificato lanciare direttamente 16–22M: il costo cresce di 8–11 volte senza una garanzia che la policy non stazionaria si stabilizzi.

Il prossimo esperimento deve ridurre direttamente la varianza delle differenze root a parità di astrazione. Il candidato è una media di più continuation rollout indipendenti per azione root, con RNG separato e senza usare Monker durante il training. Prima di una run lunga deve superare un test di aspettativa, non interferenza delle valutazioni aggiuntive con gli stati postflop e uno screen breve a due seed.
