# Analisi prestazionale del motore generale — 2026-08-29

## Analisi

### Obiettivo e perimetro

Questa analisi misura il motore postflop generale sul commit
`f4dd1f7d11a89a20f7e99ed8f4c27e21e77a69fa`. AHKHQH, TH7D6S e TSTC9D sono
usati esclusivamente come campioni small/medium/large. Non sono stati aggiunti
branch, euristiche o fast path dipendenti da fixture o board e il solver non è
stato modificato prima della stesura del rapporto.

Ambiente:

- Windows, build CMake `Release`, MSVC 14.51 / Visual Studio 2026;
- Intel Core i3-10100F, 4 core fisici / 8 logici, 3,6 GHz nominali;
- cache rilevate dal benchmark runner: L1D 32 KiB ×4, L2 256 KiB ×4,
  L3 6 MiB condivisa;
- 31,94 GiB RAM; banda DDR4 dual-channel storicamente stimata in circa
  20 GB/s, non rimisurata in questa sessione;
- binario timing: `out/build/windows-release-current/apps/gto_cli/gto_cli.exe`;
- binario instrumentato: stessa configurazione Release con
  `GTOSD_ENABLE_HOTPATH_PROFILE=ON` e `GTOSD_PROFILE_HOTPATH=1`.

I timing assoluti provengono sempre dal binario Release non instrumentato. Il
profilo hot-path somma tempi dei worker e serve solo a confrontare famiglie di
lavoro. Le sue sottofasi sono annidate e parallele: non sono percentuali wall
additive.

### Conclusioni principali

1. Il production path è un **node-owned canonical chance tree player-local**,
   non un DAG con stato mutabile condiviso e non un physical tree. Il nome
   interno `canonical_public_dag` è legacy e fuorviante.
2. Il runtime non è spiegato dal solo numero di nodi. A parità di scala,
   action-entry update e costo showdown per mano sono predittori migliori.
3. Nei casi medium/large lo scheduling è ragionevolmente bilanciato: imbalance
   medio worker 9,1% e 10,6%. Non esiste evidenza che l'uso CPU osservato sia il
   collo dominante.
4. Lo showdown è una famiglia di costo primaria: il costo instrumentato passa
   da 1,25 µs/showdown sul range small a 8,71 e 7,53 µs sui range medium/large.
   Rank metadata e blocker metadata sono già statici per board; aggregazione
   dell'opponent reach, prefix e output restano dinamici.
5. Il backend DCFR signed i16/u16 ricostruisce massimi, scale e tutti i codici a
   ogni update di nodo. Il numero di cambi effettivi delle scale non è contato:
   la persistent-scale hypothesis è promettente ma non ancora dimostrata.
6. La current-strategy certification del backend signed è **non valida**:
   `current_strategy(..., average=false)` tratta codici regret `int16` come
   `uint16`. I valori current DCFR 119–251% dEV misurati sono artefatti del
   certificatore, non una proprietà dimostrata di DCFR.
7. La telemetria strategy-density del percorso canonico incrementa il totale
   delle entry ma non i contatori zero/whole-zero. Gli zeri riportati sono
   quindi “non instrumentati”, non una densità del 100% dimostrata.
8. Le fixture ufficiali AHKHQH e TH7D6S hanno golden di fingerprint/layout non
   allineati al motore corrente. La suite è verde perché il reference test usa
   le autorità correnti, ma `benchmark-gto-plus` marca quei layout come mismatch.

### Risposte ai controlli su range, blocker e isomorfismi

1. **Le combo bloccate dalle nuove street vengono escluse dal calcolo live.**
   Il layout conserva il range di input per identità, persistenza e fingerprint,
   ma ricostruisce per ogni board `player_combos[player]` includendo soltanto le
   combo del range con peso non-zero e mask disgiunta dall'intero board corrente.
   Al chance edge filtra inoltre gli slot incompatibili con la nuova carta. Nel
   caso proposto, `Kh7h` è live su `8c6c7c`, ma dopo il turn `7h` non appartiene
   più alle combo live del player e il suo reach è zero: **non contribuisce a
   strategy, EV, showdown o conteggi turn/river**. Può restare presente nel range
   serializzato originale, ma non nel dominio live di quella board; confondere i
   due livelli sarebbe un errore di osservazione, non un errore di card removal.
2. **Il solve usa i due range separati per player, non tutte le 630 combo.** Ogni
   player riceve uno spazio flop compatto proprio, inizializzato con i suoi pesi;
   ogni decision node alloca stato soltanto sulle combo live del player agente.
   Alcuni metadata di board usano l'unione dei due range per essere condivisi,
   ma non attribuiscono all'altro player combo assenti dal suo range. Showdown e
   reach applicano anche la compatibilità fisica tra le due hole-card combo. Solo
   l'overload esplicitamente uniforme costruisce intenzionalmente un range pieno.
3. **Gli isomorfismi sono globali e range-aware.** Due board legati da una
   permutazione dei semi, come `As Kh 9d 7s` e `Ah Kc 9s 7h`, sono accorpabili
   soltanto se la stessa permutazione preserva esattamente i pesi di entrambi i
   range. Con range non invarianti restano stati distinti. Il differential
   ISO-on/ISO-off passa su EV, best response e NashConv entro `1e-11`; il test
   asimmetrico conferma che non viene applicata una canonicalizzazione board-only.
4. **Problemi principali emersi:** certificazione current signed errata; golden
   fixture AHKHQH/TH7D6S obsoleti; telemetria incompleta per density, scale churn
   e dimensioni showdown; hardware counter non acquisibili; cost model ancora
   sotto-identificato; averaging policy molto sfavorevole sul medium misurato.

## Piano e protocollo

Sono state eseguite quattro matrici isolate:

1. profiling uniforme a 10 iterazioni sui tre benchmark;
2. curve current/average con certificazione ogni 20 iterazioni;
3. scaling 1/2/4/8 thread, 10 iterazioni, su medium e large;
4. CFR+, DCFR+, DCFR e HS-DCFR-30 sul medium, tutti con backend
   `ScaledUint16RegretStrategy`, averaging delay 0, 60 iterazioni.

Le fixture diagnostiche cambiano soltanto limite iterazioni, intervallo di
certificazione, algoritmo/backend quando dichiarato e thread. Range, board,
tree e sizing rimangono invariati. Comando base:

```powershell
gto_cli.exe postflop benchmark-gto-plus <fixture-diagnostica.json> <report.json>
```

La current strategy è stata richiesta tramite
`GTOSD_DIAGNOSTIC_CERTIFY_CURRENT=1`. Questo ha esposto il bug signed descritto
sopra.

## Implementazione osservata

### Architettura production reale

Percorso effettivo:

```text
PostflopTreeConfig + PostflopRanges
  -> range_automorphisms(board iniziale + range dei due player)
  -> estimate_canonical_chance_layout
  -> build_public_tree con canonical_chance_permutations
  -> tree canonico costruito direttamente, senza espansione fisica residente
  -> build_direct_canonical_public_graph (wrapper/indice legacy)
  -> CanonicalPublicNode node-owned
  -> chance edge con physical outcome -> representative + permutazione
  -> traversal player-local float32
  -> stato action-major packed o node-scaled
  -> certificazione/best response float64
```

Ogni betting node possiede un solo blocco mutabile. Gli outcome chance omessi
sono espansi nel CFV restituito tramite inverse hand permutation; non aggiornano
più volte lo stesso stato. `materializes_physical_tree=false` nei tre layout.

La tabella fotografa il layout corrente, non i golden storici:

| Benchmark | Automorfismi range-preserving | Physical public | Canonical public | Decision | Chance | Terminal | Action entry | Max live combo | Profondità |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| AHKHQH | 6 | 165.774 | 46.065 | 18.438 | 320 | 27.307 | 1.288.290 | 36 | 11 |
| TH7D6S | 1 | 378.834 | 378.834 | 147.256 | 1.362 | 230.216 | 83.318.592 | 358 | 12 |
| TSTC9D | 2 | 2.791.872 | 1.758.624 | 630.596 | 6.360 | 1.121.668 | 366.890.152 | 301 | 13 |

AHKHQH oggi ha fingerprint `fnv1a64:1b6f30a930cd9bd0`, 595.626 infoset e
1.288.290 action; la fixture contiene ancora 385.980/834.636 e fingerprint
`fnv1a64:2b35eb1ab45315c5`. TH7D6S produce fingerprint
`fnv1a64:fcba3c9eff1b7147`, non il golden `250fcee14c115402`.

### Suit isomorphism

I board path fisici sono 1 flop, 33 turn e 1.056 river per tutti i casi. Le
riduzioni osservate sono:

| Benchmark | Flop phys/canon | Turn phys/canon | Fattore turn | River phys/canon | Fattore river |
|---|---:|---:|---:|---:|---:|
| AHKHQH | 1 / 1 | 33 / 15 | 2,20× | 1.056 / 291 | 3,63× |
| TH7D6S | 1 / 1 | 33 / 33 | 1,00× | 1.056 / 1.056 | 1,00× |
| TSTC9D | 1 / 1 | 33 / 25 | 1,32× | 1.056 / 664 | 1,59× |

La dipendenza dai range è verificata empiricamente: TH7D6S non riduce nulla
nonostante un board rainbow, perché i range asimmetrici pesati lasciano soltanto
l'identità; AHKHQH conserva sei automorfismi; TSTC9D due. Non è quindi una
canonicalizzazione della sola texture del board.

## Profiling cross-benchmark

### Release, 10 iterazioni

| Metrica | AHKHQH | TH7D6S | TSTC9D |
|---|---:|---:|---:|
| Traversal | 0,074903 s | 1,950640 s | 7,129352 s |
| ms/iter | 7,490 | 195,064 | 712,935 |
| Nodi/s | 5.908.214 | 1.727.946 | 1.905.832 |
| Nodi visitati | 442.540 | 3.370.601 | 13.587.348 |
| Decision evaluation | 311.778 | 2.486.051 | 9.908.837 |
| Chance outcome | 187.999 | 775.913 | 3.529.738 |
| Fold terminal | 3.844 | 15.705 | 80.043 |
| Showdown terminal | 121.046 | 844.603 | 3.488.184 |
| Regret entry aggiornate | 10.775.872 | 688.578.727 | 2.878.905.308 |
| Average-strategy entry aggiornate | 0 | 0 | 2.878.905.308 |

Le zero strategy write small/medium sono corrette per questa prova: a 10
iterazioni i loro averaging delay production (10 e 40) non hanno ancora
attivato l'average update. Non sono comparabili con TSTC9D per il costo
dell'averaging; il confronto delle strutture resta valido.

### Breakdown instrumentato

Tempi sommati su 20 pass giocatore. Sono CPU-equivalent dei worker e possono
sovrapporsi; il profiler corrente riporta inoltre `terminal showdown=0` pur
incrementando le tre sottofasi, quindi non esiste un denominatore totale
affidabile per percentuali wall.

| Fase (ms) | AHKHQH | TH7D6S | TSTC9D |
|---|---:|---:|---:|
| Rank/card accumulation | 53,0 | 2.864,1 | 9.037,7 |
| Prefix construction | 28,8 | 1.128,3 | 4.274,9 |
| Showdown value production + blocker | 70,0 | 3.361,2 | 12.962,1 |
| Reach propagation | 37,7 | 927,5 | 150,0 |
| Value + update (contenitore) | 63,4 | 1.913,9 | 10.440,6 |
| Regret update timer interno | 0,0 | 0,0 | 183,4 |
| Regret matching | 65,5 | 1.481,8 | 6.714,1 |
| Chance/board | 48,2 | 663,8 | 3.201,2 |
| Synchronization esplicita | 0,0 | 0,0 | 0,0 |

Il timer `regret update` interno copre solo alcuni dispatch signed; non misura
l'intera fase update. Il contenitore `value + update` è l'autorità più ampia.
`average update` è fuso nel signed update e il timer dedicato rimane zero.

### Cost model empirico

Il costo per nodo non è stabile:

- AHKHQH: 169 ns/nodo;
- TH7D6S: 579 ns/nodo;
- TSTC9D: 525 ns/nodo.

Il costo per regret-entry è 6,95 ns, 2,83 ns e 2,48 ns rispettivamente; il
piccolo paga overhead fisso e parallelismo poco ammortizzato. Il costo
showdown instrumentato, che include accumulation+prefix+output, è:

| Benchmark | Rank/card µs/showdown | Prefix | Output/blocker | Totale |
|---|---:|---:|---:|---:|
| AHKHQH | 0,438 | 0,238 | 0,578 | 1,254 |
| TH7D6S | 3,391 | 1,336 | 3,980 | 8,707 |
| TSTC9D | 2,591 | 1,226 | 3,716 | 7,532 |

Con soli tre punti cross-structure, un modello con quattro coefficienti non è
identificabile. Il modello non-negativo minimo, fit in-sample, è:

```text
traversal_seconds ~= 1,715 ns * regret_update_entries
                  + 0,644 us * showdown_terminal_evaluations
```

Predice 0,0964 / 1,7247 / 7,1833 s contro 0,0749 / 1,9506 / 7,1294 s:
errore +28,8%, -11,6%, +0,8%. È un modello **provisionale**, non una legge del
motore. Aggiungere decision, chance e showdown-hands con gli stessi tre punti
produce coefficienti negativi o un fit esatto privo di valore predittivo.

La conclusione robusta è più limitata: il runtime cresce con le entry action
effettivamente processate, mentre il coefficiente showdown cambia di circa 7×
con dimensione/densità dei range. Servono contatori `hero_hands`,
`opponent_hands`, `rank_count` e `rank_card_cells` per il modello richiesto
completo.

## Current strategy vs average strategy

### DCFR standard: risultato bloccato da un bug diagnostico

Con identico stato DCFR signed e identico traversal, l'average strategy dà:

| Benchmark | Iter | Nodi visitati | Traversal | dEV average |
|---|---:|---:|---:|---:|
| AHKHQH | 20 / 40 / 60 | 0,894 / 1,852 / 2,803 M | 0,142 / 0,289 / 0,452 s | 8,173% / 2,344% / 1,033% |
| TH7D6S | 20 / 40 / 60 | 6,679 / 14,064 / 21,304 M | 3,647 / 7,781 / 11,826 s | 11,666% / 3,064% / 1,340% |
| TSTC9D | 20 / 80 / 170 | 29,490 / 123,049 / 260,181 M | 15,820 / 69,898 / 151,554 s | 16,817% / 3,266% / 0,986% |

Il certificatore current restituisce invece 119–123% (small), 183–204%
(medium) e 226–251% (large). Il codice di training decodifica correttamente
`scaled_regret` con cast `int16_t` e clamp positivo; il percorso
`current_strategy(const CanonicalPublicNode&, ..., false)` usa direttamente il
`uint16_t`. La misura current DCFR è quindi invalida su tutte le scale.

Non è possibile stabilire onestamente quanto delle iterazioni DCFR dipenda dai
regret e quanto dall'averaging finché questo certificatore non viene corretto e
ricertificato. Questa è una lacuna di osservabilità/correttezza, non una
motivazione per cambiare parametri.

### DCFR+ production: evidenza valida ma non sostitutiva di DCFR

Il confronto unsigned è valido e mostra comportamento opposto per struttura:

| Benchmark | Iter | dEV average | dEV current |
|---|---:|---:|---:|
| AHKHQH | 20 | 5,236% | 8,402% |
|  | 40 | 1,454% | 5,499% |
|  | 60 | 0,941% | 3,378% |
| TH7D6S | 60 | 13,727% | 2,575% |
|  | 80 | 12,194% | 1,926% |
|  | 100 | 11,720% | 1,475% |
|  | 120 | 11,484% | 1,664% |

AHKHQH è averaging-limited in senso favorevole: l'average converge molto prima
della current. TH7D6S è invece penalizzato dal suo averaging delay 40 e dalla
media successiva: la current è molto migliore dell'average nel tratto misurato.
Non esiste una risposta fixture-independent del tipo “serve solo più
iterazioni”: regret dynamics e averaging contribuiscono entrambi.

### Algoritmi normalizzati sul medium

Tutti usano 4 byte/action node-scaled e averaging delay 0:

| Algoritmo | dEV @20 / @40 / @60 | Nodi @60 | Traversal @60 | ms/iter @60 | Nodi/s |
|---|---:|---:|---:|---:|---:|
| CFR+ | 13,236 / 6,513 / 3,816% | 21,811 M | 17,793 s | 296,555 | 1,226 M |
| DCFR+ | 11,880 / 4,267 / 2,193% | 21,910 M | 17,572 s | 292,863 | 1,247 M |
| DCFR | 11,666 / 3,064 / 1,340% | 21,304 M | 11,755 s | 195,924 | 1,812 M |
| HS-DCFR-30 | 25,518 / 9,904 / 3,642% | 22,115 M | 12,353 s | 205,889 | 1,790 M |

DCFR standard è migliore sia per dEV/iterazione sia per dEV/tempo in questa
matrice. Il vantaggio kernel non deriva dai byte/action, che sono uguali, ma dal
percorso signed fuso. Questo singolo medium non autorizza a dichiararlo
universalmente superiore; serve ripetere la matrice su almeno una struttura
small e una large dopo la correzione current.

## Showdown

### Lavoro statico e dinamico

Statico per board, già precomputato una volta prima dei worker:

- hand rank e mapping globale -> player rank;
- `own_slot`, `opponent_slot`, rank e card indices;
- blocker cell indices e lista deduplicata `touched_by_rank_card`;
- payoff terminali win/tie/loss.

Dinamico per visita terminale:

1. clear di `totals[rank_count]`, base prefix/card-prefix e touched cells;
2. scatter dell'opponent reach in rank e rank×card;
3. prefix per rank e per rank×36 card;
4. blocker correction e value production per hero hand;
5. reset delle sole touched rank-card cells.

Il profiler non conta hero hands, opponent hands, rank_count o celle. I massimi
live sono 36/358/301, ma non possono essere sostituiti alla distribuzione per
showdown. Anche `µs/showdown` sopra è quindi una normalizzazione aggregata.

### Riuso lossless

Ogni river board è condivisa da molti terminali pubblici:

| Benchmark | River board canoniche | Terminali river | Terminali/board |
|---|---:|---:|---:|
| AHKHQH | 291 | 27.063 | 93 |
| TH7D6S | 1.056 | 229.152 | 217 |
| TSTC9D | 664 | 1.116.184 | 1.681 |

Questo è un forte riuso **statico**, già sfruttato dai metadata. Non prova
riuso dinamico: terminali sulla stessa board normalmente ricevono opponent
reach differenti. Il motore fonde già un caso lossless concreto,
showdown+fold paired, quando condividono reach.

Il numero di ulteriori aggregazioni riutilizzabili non è oggi misurabile senza
una chiave `(board, opponent, exact reach identity/hash)`. Implementare una
cache prima di contare tali collisioni sarebbe speculativo. L'esperimento
minimo è solo telemetria hash/counter, senza riuso.

## Stato, update e precisione

### Passate sulle action entry

Per un decision node production `A actions × L live hands`:

**DCFR signed scaled, common arity 2/3:**

1. regret matching: una lettura dei codici; può essere fusa nell'update quando
   l'updating player non agisce più nei discendenti;
2. reach materialization: una passata equivalente A×L;
3. child traversal;
4. value combination + decode old regret/average + discount + delta + ricerca
   max regret/max average: una passata A×L, fusa per arity 2/3;
5. quantization/encode e write dei due array: una seconda passata A×L.

**Scaled unsigned CFR+/DCFR+:** oltre a strategy/reach/value, average e regret
fanno ciascuno una passata decode/update/max e una passata encode: fino a
quattro passate di storage dopo i child, contro due nel signed fuso.

**Packed 13+11:** regret e average condividono 3 byte/action; nei dispatch
specializzati il read-modify-write può aggiornare entrambi nello stesso word.

Fusioni teoriche:

- estendere decode-at-update oltre i leaf-like nodes, solo se la strategia non
  serve per reach discendente;
- persistent scale: eliminare max-search+re-encode quando il valore aggiornato
  resta nel range della scala esistente;
- fondere value combination e update per altre arity/forme, preservando ordine
  numerico e update schedule.

### ScaledUint16RegretStrategy e scale churn

Storage DCFR:

- regret `int16` + strategy `uint16`: 4 byte/action;
- `float regret_node_scale` + `float strategy_node_scale`: 8 byte/decision;
- TSTC9D: 1.467.560.608 B action + 5.044.768 B scale = 1.472.605.376 B.

Ogni update signed decodifica entrambi gli array, applica discount/update,
cerca due massimi, scrive due nuove scale e ricodifica tutte le entry del nodo.
Non esiste una fast path “scala invariata” e non esiste un contatore di:

- scale bit-identiche al valore precedente;
- variazione relativa;
- overflow rispetto alla scala persistente;
- numero di rescale realmente necessario.

Il churn reale non è quindi misurato. Il limite superiore delle scale
riconsiderate è il numero di decision update dell'updating player; non va
confuso con il numero di scale che cambiano.

### Candidato signed 24-bit

Il codec signed regret13 + unsigned strategy11 da 3 byte/action è un design
documentato ma **non implementato**. Il codec production 13+11 è unsigned e
serve CFR+/DCFR+; `ActionMajorFloat13RegretFloat11Strategy` non può
rappresentare regret DCFR negativi. Il candidato non è stato né bocciato
numericamente né dichiarato lento: è semplicemente non validato.

Non è stato prodotto un falso microbenchmark: manca una specifica eseguibile
del codec signed, e implementarla nel solver prima del rapporto avrebbe violato
il perimetro. Il vantaggio certo è 3 contro 4 byte/action (-25%, -366,9 MB su
TSTC9D prima delle scale); throughput, errore e convergenza restano ignoti.

### Float e double

Training compatto/canonico:

- state: byte/u16, scale float32;
- reach, action values, strategies e decision scratch: float32;
- showdown hot scratch: float32;
- schedule weights, payoff, scale scalar/tail, normalizzazione e metriche:
  double;
- layout initial reach e payoff persistenti: double, convertiti all'ingresso;
- certification/best response: traversal double con decode dello stesso stato.

Le routine AVX2 principali convertono u16/i16 -> i32 -> float, non passano per
double per entry. Scalar tails e alcune helper generic `load_four_as_double`
fanno float<->double. Payoff double viene convertito/broadcast una volta per
kernel terminale, non una volta per cella SIMD.

Non esiste un contatore di conversioni per dispatch e non è stata eseguita una
A/B che cambi solo il compute scalar. Di conseguenza non c'è evidenza che le
conversioni siano una quota dominante; il rapporto non attribuisce loro uno
speedup.

## Density e pruning

Il contatore production registra le entry di strategia visitate ma non gli
zeri: i punti instrumentati riportano sempre zero solo perché gli incrementi
`prof_zero_strategy_entries_` esistono nel physical path, non nei dispatch
canonici usati dai benchmark. Anche le probabilità “molto piccole” non sono
contate.

L'aumento reale di lavoro nel tempo è invece visibile nei nodi visitati. Il
motore ha già partial zero-reach pruning quando l'attore non è l'updating
player. Confrontando iterazione 1 con il plateau late:

| Benchmark | Nodi iter 1 | Nodi/iter late | Lavoro evitato iter 1 vs late |
|---|---:|---:|---:|
| AHKHQH | 47.973 | ~85.647 | ~44% |
| TH7D6S | 423.288 | ~702.939 | ~40% |
| TSTC9D | 1.818.031 | ~3.255.008 | ~44% |

Quindi le iterazioni avanzate costano di più principalmente perché più reach
diventano non-zero e più subtree vengono realmente attraversati, non perché il
public tree cambi. A regime il costo si stabilizza.

Opportunità:

- zero-reach pruning: già presente; manca il counter skipped-nodes;
- unreachable action pruning exact: possibile solo con reach esattamente zero;
  quantificazione bloccata dalla telemetria density assente;
- regret-based pruning con riesame: non lossless finite-iteration; deve restare
  modalità separata con intervallo di riesame e differential gate;
- skip di discount/average su stato unreachable: il codice attuale dichiara
  esplicitamente che cambia lo stato finite-iteration irraggiungibile; non va
  chiamato bit-exact anche se la certificazione raggiungibile resta autorità.

## Parallelismo

### Scaling

Il tempo principale è solo traversal. CPU time e total wall sono del processo
intero e includono tree preparation/certification; il report corrente non
espone CPU time della sola traversal. Il campo `normalized_cpu_utilization` è
invalido per 1–4 thread perché divide anche CPU di preparazione multithread per
il limite solver; supera infatti 100%.

| Bench | Thread | ms/iter | Speedup | Efficienza | Traversal | Process CPU | Total wall |
|---|---:|---:|---:|---:|---:|---:|---:|
| TH7D6S | 1 | 740,971 | 1,00× | 100,0% | 7,410 s | 26,406 s | 24,185 s |
|  | 2 | 391,724 | 1,89× | 94,6% | 3,917 s | 26,438 s | 20,925 s |
|  | 4 | 254,310 | 2,91× | 72,8% | 2,543 s | 28,250 s | 19,468 s |
|  | 8 | 188,363 | 3,93× | 49,2% | 1,884 s | 30,078 s | 18,761 s |
| TSTC9D | 1 | 2.901,261 | 1,00× | 100,0% | 29,013 s | 78,875 s | 58,619 s |
|  | 2 | 1.580,640 | 1,84× | 91,8% | 15,806 s | 80,172 s | 45,335 s |
|  | 4 | 927,468 | 3,13× | 78,2% | 9,275 s | 86,172 s | 39,154 s |
|  | 8 | 729,892 | 3,97× | 49,7% | 7,299 s | 94,563 s | 37,195 s |

### Task distribution e critical path

| Benchmark | Worker | Task totali/20 pass | Task medio | Imbalance medio | Imbalance max | Busy medio / pass wall |
|---|---:|---:|---:|---:|---:|---:|
| AHKHQH | 4 | 1.013 | 0,379 ms | 31,0% | 83,6% | 73,2% |
| TH7D6S | 5 | 2.211 | 5,334 ms | 9,1% | 43,0% | 90,9% |
| TSTC9D | 7 | 14.477 | 3,514 ms | 10,6% | 54,4% | 87,1% |

Small soffre granularità/overhead, ma non è il driver del motore grande.
Medium/large hanno task sufficientemente lunghi e distribuzione buona. Il
9–13% non-busy è un **upper bound** che include dipendenze reali, riduzione al
parent e coda finale; non è speedup scheduler recuperabile. I timer di sync
esplicito sono 0,0 ms alla risoluzione corrente. Senza eventi queue-wait e
critical-path dependency separati, la quota scheduler-only non è misurabile;
l'evidenza non giustifica una priorità alta.

## Memory e cache

Lower bound di traffico state+reach, usando i contatori production:

- compact: 3 B per strategy decode; almeno read+write del word per regret
  update;
- scaled signed: 2 B strategy decode e almeno 8 B per entry fusa
  regret+average read/write;
- reach materialization: almeno read parent + read strategy + write child,
  circa 12 B per actor write float.

| Benchmark | State lower bound/10 iter | State+reach lower bound | Effective BW lower bound |
|---|---:|---:|---:|
| AHKHQH | ~0,130 GB | ~0,261 GB | ~3,5 GB/s |
| TH7D6S | ~8,36 GB | ~17,0 GB | ~8,7 GB/s |
| TSTC9D | ~31,0 GB | ~75,9 GB | ~10,6 GB/s |

Non sono inclusi action-value scratch, showdown scatter/prefix, topology,
chance mapping, cache-line overfetch e write-allocate; il totale reale è più
alto. Rispetto alla stima storica ~20 GB/s, il lower bound raggiunge ~53% sul
large. Questo esclude “solo compute” ma non distingue DRAM bandwidth, cache
latency, gather latency e arithmetic.

Lo scratch showdown massimo allocato per `DenseTraversal` conserva sia array
float sia double: circa 189 KB float + 378 KB double, oltre a decision/value
scratch per profondità. Il kernel production usa la metà float; la metà double
serve ai percorsi accuratezza/certificazione nello stesso tipo.

### Hardware counters

`xperf -pmcsources` elenca cycles, instructions, cache misses, LLC references,
LLC misses e branch mispredictions. Due tentativi, prima con sei contatori e poi
con soli `TotalCycles,InstructionRetired`, sono falliti prima della raccolta:

```text
Failed to configure counters
NT Kernel Logger: ... WMI ... (0x1069)
```

Non sono quindi disponibili IPC, miss rate, vector utilization o bandwidth
hardware per questa sessione. La classificazione corretta è **mixed
cache/latency/compute con forte traffico**, non “memory-bound” dimostrato.

## Confronto osservabile con GTO+

| Benchmark | GTO+ tempo / RAM | GTOSD corrente | Stato / peak RSS | Esito comparabile |
|---|---:|---:|---:|---|
| AHKHQH | 1,71 s / 8 MB | 1,561 s @80, 0,775% | 3,865 MB / 166,4 MB | timing/state favorevoli, ma golden layout fixture stale e correctness aggregata false |
| TH7D6S | 17,66 s / 399 MB | 45,036 s @120, average 11,484% | 250,0 MB / 710,5 MB | non raggiunge target; current unsigned ~1,5–1,7%, average è il collo |
| TSTC9D | 116,09 s / 2,0 GB | 153,176 s @170, 0,986% | 1,473 GB / 1,969 GB | dEV/state/RSS pass, tempo 1,32× GTO+ |

Per TSTC9D il dato 153,176 s usa una sola certificazione finale; il run a
certificazione ogni 20 impiega 204,622 s e serve solo alla curva. AHKHQH e
TH7D6S non devono essere confrontati con i vecchi report che hanno fingerprint
diversi.

Il gap non cresce monotonicamente con i nodi: AHKHQH è veloce, TH7D6S è
dominato dalla policy average/large range senza iso, TSTC9D beneficia del
canonical tree ma paga action count, showdown e backend signed. Ciò suggerisce
una differenza architetturale generale in **showdown aggregation + state
passes/locality**, non un problema specifico del board TSTC9D.

## Priorità finale

Prima delle ottimizzazioni esiste un gate P0 non prestazionale: correggere e
testare il decode signed nella current certification e completare i contatori
density/scale. Senza questo gate alcune ipotesi non sono misurabili.

| Candidato | Costo attuale interessato | Speedup locale realistico | Impatto globale stimato | Generalità | Rischio correttezza | Rischio RAM |
|---|---:|---:|---:|---|---|---|
| 1. Persistent node scale con rescale-on-demand | due max search + re-encode A×L per update signed | 20–35% della fase signed update | 8–18% large DCFR, da confermare | ogni nodo DCFR scaled | alto: quantizzazione/discount trajectory | basso o favorevole |
| 2. Riuso lossless dell'aggregazione showdown provato da exact reach identity | 7,5–8,7 µs/showdown medium/large | 20–50% sui soli hit | 5–20%, dipende dall'hit rate oggi ignoto | tree con molti terminali per board | medio: chiave reach/permutazioni | medio se cache non bounded |
| 3. Fusione strategy/reach/value/update per più arity e liveness | 2–6 passate A×L secondo backend/dispatch | 10–25% decision kernel | 5–15% | ogni decision node | medio-alto: ordine update | neutro/favorevole |
| 4. Signed 13+11 isolato, solo se numericamente promosso | 4 -> 3 B/action, ~367 MB TSTC9D | 0–25% state traffic; codec può annullarlo | 3–12% stimato, non misurato | DCFR con grandi stati | alto | -25% state action |
| 5. Batch/locality chance mapping e accumulation | 3,201 s CPU-equivalent/20 pass large | 15–30% chance phase | 2–6% | chance-heavy tree canonici | medio: inverse mapping/order | basso |
| 6. Scheduler/granularità | upper bound non-busy 9–13% medium/large | <30% della sola quota idle | verosimilmente <5% | tree con chance tasks | basso-medio | neutro |

Gli intervalli sono engineering bounds, non benchmark promessi. Il candidato 2
non può essere selezionato prima del conteggio hit; il candidato 1 non può
essere selezionato prima della misura scale churn.

## Validazione

Comando:

```powershell
ctest --test-dir out/build/windows-release-current -C Release --output-on-failure
```

Risultato: **18/18 PASS**, 184,61 s reali.

- reference GTO+: PASS, 24 assertion;
- ISO-on/ISO-off node-owned: PASS, profile EV/BR/NashConv entro `1e-11`;
- serial/parallel: PASS, max regret difference 0 e max strategy difference 0;
- checkpoint/resume: PASS; continuous/resumed byte-equivalent per CFR+ e
  HS-DCFR-30 scaled;
- packed state: PASS per solve/certify/query, persistence e checksum;
- canonical layout: PASS, 15 assertion, path TSTC9D 1/25/664;
- asymmetric-range isomorphism: PASS, 165.774 -> 46.065 public nodes e
  4.361.904 -> 1.234.628 actions nel test dedicato.

Limiti della validazione:

- nessun hardware counter acquisito;
- current certification signed non coperta da un test che eserciti regret
  negativi;
- fixture golden AHKHQH/TH7D6S non allineati, non intercettati come failure da
  CTest;
- nessun counter production per density zero, scale churn, showdown hands/cells
  o reuse hit;
- nessun benchmark signed-24 perché il backend non esiste.

## Decisioni

- Nessuna ottimizzazione è stata implementata.
- Non si classifica il motore come scheduler-bound o memory-bound senza
  contatori sufficienti.
- DCFR resta il candidato algoritmico migliore della matrice medium, ma la
  current-strategy diagnosis è sospesa fino al fix signed.
- Il prossimo intervento prestazionale non viene scelto dal solo risultato
  TSTC9D: deve ridurre una classe misurata di passate, celle showdown o traffico
  di stato su più strutture.
- I report storici con fingerprint/layout diversi restano evidenza storica e
  non sono usati come baseline corrente.

## Esperimenti minimi prima del prossimo intervento

1. Correggere solo il decode signed del certificatore, aggiungere regression
   test con regret negativi e ripetere current/average DCFR small/medium/large.
2. Aggiungere contatori read-only per scale: unchanged, relative delta,
   overflow-needed e rescale; nessuna persistent scale ancora.
3. Aggiungere per showdown `hero_hands`, `opponent_hands`, `rank_count`,
   touched cells e hash exact dell'opponent reach per board; contare reuse hit
   senza cache.
4. Completare density telemetry nel canonical path: exact-zero, `<1e-6`,
   `<1e-4`, skipped actions/subtrees e skipped action entries.
5. Eseguire due A/B generali e isolati: persistent-scale prototype e standalone
   signed13+11 codec microbenchmark, ciascuno contro i16/u16 per GB/s,
   updates/s, error, dEV e root EV.

## Passo successivo

**Correggere e testare la current-strategy certification signed, senza toccare
il traversal.** È il gate più piccolo che rende finalmente interpretabile la
separazione regret dynamics/averaging richiesta per DCFR.
