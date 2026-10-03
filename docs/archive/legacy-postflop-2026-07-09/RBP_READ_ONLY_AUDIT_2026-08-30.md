# Audit read-only del Regret-Based Pruning — 2026-08-30

## Esito

**Categoria C — non applicabile in modo sound al contratto production corrente.**

Non e' stato implementato alcun pruning. E' stata aggiunta esclusivamente
telemetria diagnostica post-iterazione, attivabile con
`GTOSD_RBP_READ_ONLY_TELEMETRY`, che legge i checkpoint di certificazione e non
entra nel traversal. Nei tre fixture production non esiste, ai punti misurati,
una sola action entry con una finestra garantita di almeno un'iterazione secondo
la formula CFR originale. Questo risultato non e' uno speedup e non sblocca il
parity gate o F11+.

Il contratto congelato e' rimasto invariato: alternating exact DCFR,
`alpha=1.5`, `beta=0`, `gamma=2`, averaging delay zero, media reach-weighted
con peso `t^2`, stato firmato `ScaledUint16RegretStrategy`, BR esatta, public DAG
e isomorfismi lossless, massimo otto thread. Nessun fixture, riferimento GTO+ o
tree sizing e' stato modificato. `beta=0.5` non e' stato introdotto.

## 1. Contratto matematico originale

La fonte primaria e' Brown e Sandholm, [Regret-Based Pruning in
Extensive-Form Games](https://proceedings.neurips.cc/paper/2015/file/c54e7837e0cd0ced286cb5995327d1ab-Paper.pdf),
NeurIPS 2015.

Per un infoset `I` e un'azione `a`, `R^T(I,a)` e' il regret controfattuale
cumulativo CFR. Se `R^T0(I,a) <= 0`, regret matching assegna probabilita' zero
all'azione quando almeno un'altra azione ha regret positivo. Il Teorema 1 usa il
bound per-iterazione

```text
r^t(I,a) <= U(I,a) - L(I)
```

e garantisce la possibilita' di non attraversare `D(I,a)` per

```text
m = floor(|R^T0(I,a)| / (U(I,a) - L(I)))
```

iterazioni. La condizione adattiva piu' precisa del paper accumula il reach
controfattuale dell'avversario e mantiene un upper bound sulla value dell'azione;
il ramo deve essere rivisitato non oltre la prima iterazione in cui il regret
avrebbe potuto tornare positivo.

La visita differita non equivale a ignorare gli aggiornamenti. Al ritorno, il
paper calcola una best response contro la strategia media avversaria giocata
nell'intervallo potato e ricostruisce i regret del ramo. La strategia media
richiede quindi dati dell'intervallo e una procedura di recupero. Il paper
dimostra il bound asintotico CFR per questa costruzione e tratta separatamente
una variante CFR+ con regret negativi ausiliari; segnala inoltre effetti piu'
rumorosi con linear averaging.

### Divario rispetto al production DCFR

La produzione non conserva il `R^T` non scontato del teorema. Prima di ogni
nuovo incremento, il regret positivo usa il fattore DCFR
`(t-1)^1.5 / ((t-1)^1.5 + 1)` e il regret negativo viene moltiplicato per `0.5`.
La strategia media usa pesi reach-weighted `t^2`. Di conseguenza:

- la negativita' del codice firmato resta sufficiente per osservare policy zero,
  ma non rappresenta il budget cumulativo CFR usato dal Teorema 1;
- una durata calcolata dal valore DCFR corrente non ha la garanzia del paper;
- la best response differita CFR non ricostruisce automaticamente una media
  DCFR `t^2` equivalente;
- il paper originale non dimostra che sconto negativo, recupero differito e
  alternating DCFR conservino lo stesso stato o lo stesso bound.

Per questo l'audit pubblica `original_formula_candidates` come **proxy
diagnostico** e dichiara esplicitamente
`original_cfr_formula_is_sound_for_production_dcfr=false`.

## 2. Audit del percorso production

Il percorso verificato usa:

- regret matching firmato: i payload `uint16` sono interpretati come `int16` e
  solo i codici positivi contribuiscono alla policy corrente;
- scala comune per decision node: il codice firmato viene moltiplicato per
  `regret_node_scale` quando serve una grandezza in ante; la scala si cancella
  soltanto durante la normalizzazione della policy;
- update alternating P0 poi P1, con applicazione completa del regret prima del
  player successivo;
- discount positivo e negativo applicati all'accumulato prima dell'incremento
  immediato;
- strategy sum reach-weighted con peso `t^2`, delay zero;
- contatori di decision, chance, outcome, fold, showdown, regret entry e
  strategy entry separati;
- checkpoint che serializza esclusivamente i quattro buffer production e i
  metadati algoritmici, senza stato RBP.

Le scorciatoie gia' presenti per zero reach o subtree senza il player in update
restano meccanismi distinti. La telemetria non chiama “zero reach” una semplice
azione a policy zero: `exact_zero_counterfactual_reach_available=false` perche'
il checkpoint post-state non conserva i reach vector del traversal.
`structurally_unreachable_action_entries=0` per definizione del layout, che
materializza solo combo legali e non bloccate sul board locale.

## 3. Telemetria implementata

Ogni osservazione, eseguita soltanto nel callback del checkpoint esatto,
pubblica per player:

- decisioni e action entry;
- policy-zero e regret negativi;
- proxy che supera almeno una volta `U(I,a)-L(I)`;
- fraction, decisioni con candidati e con tutte le azioni tranne una candidate;
- breakdown per street e numero di azioni;
- regret in ante, rapporto dalla soglia e distanza del negativo piu' vicino;
- persistenza tra osservazioni, nuovi candidati e riattivazioni;
- upper bound strutturale separato per public/decision/chance/outcome/fold/
  showdown/regret/strategy entry.

Il bound di payoff e' conservativo: `L(I)` e' il minimo terminale raggiungibile
nel subtree pubblico dell'infoset e `U(I,a)` il massimo dopo l'azione. Il lavoro
strutturale somma i subtree per action entry e puo' sovrapporre discendenti
condivisi; e' quindi soltanto un upper bound, non tempo risparmiabile.

Persistenza e riattivazione usano due bitset, `previous` ed `ever`, senza
contatori nel solver e senza campi nel checkpoint. Le osservazioni del benchmark
sono campionate ogni 20 iterazioni e all'iterazione finale; non descrivono le
transizioni intermedie.

## 4. Risultati

Build: MSVC 19.51, Release AVX2, exact alternating DCFR, otto thread, fixture
v2 immutati. I run sono diagnostici a iterazioni fisse; soltanto TH7D6S passa
anche il criterio aggregato del report. AHKHQH e TSTC9D non vengono presentati
come certificazioni di parity, pur avendo dEV finale sotto 1%.

| Fixture | Iterazione | Regret negativi | Proxy RBP `m>=1` | Max rapporto P0 | Max rapporto P1 |
|---|---:|---:|---:|---:|---:|
| AHKHQH | 20 | 474.756 | 0 | 0,082035 | 0,057221 |
| AHKHQH | 40 | 476.148 | 0 | 0,099850 | 0,052775 |
| AHKHQH | 80 | 476.790 | 0 | 0,136454 | 0,050857 |
| TH7D6S | 20 | 29.520.052 | 0 | 0,039491 | 0,031278 |
| TH7D6S | 40 | 30.970.293 | 0 | 0,034053 | 0,039988 |
| TH7D6S | 80 | 33.315.573 | 0 | 0,031625 | 0,033025 |
| TSTC9D | 20 | 131.868.539 | 0 | 0,092764 | 0,025088 |
| TSTC9D | 40 | 138.116.511 | 0 | 0,089408 | 0,022610 |
| TSTC9D | 80 | 145.601.208 | 0 | 0,090390 | 0,025438 |
| TSTC9D | 202 | 149.857.466 | 0 | 0,101574 | 0,036117 |

Poiche' nessun rapporto raggiunge 1, candidati persistenti, riattivazioni,
decisioni candidate e tutti gli upper bound di lavoro candidato valgono zero.
L'elevato numero di regret negativi non e' evidenza di potenziale pruning: e'
proprio la distinzione che questo audit rende misurabile.

| Fixture | Iterazioni | dEV finale | Root EV (ante) | Tempo telemetria | Peak RSS |
|---|---:|---:|---:|---:|---:|
| AHKHQH | 80 | 0,655702% | 19,108987 | 0,825057 s | 167.067.648 B |
| TH7D6S | 80 | 0,806385% | 8,221632 | 25,629370 s | 858.406.912 B |
| TSTC9D | 202 | 0,991865% | 8,494698 | 303,434350 s | 2.265.341.952 B |

Questi tempi includono scansioni diagnostiche ai checkpoint e non sono baseline
prestazionali. Non viene calcolata una stima revisit-adjusted: senza una
traduzione dimostrata del regret DCFR e senza candidati originali sarebbe una
quantita' priva di fondamento.

## 5. Costo dei metadati

| Fixture | Metadati | B/action | B/decisione | % solver state | % peak RSS osservato |
|---|---:|---:|---:|---:|---:|
| AHKHQH | 322.080 B | 0,250006 | 0,540742 | 6,0762% | 0,1928% |
| TH7D6S | 20.829.648 B | 0,250000 | 0,569165 | 6,2280% | 2,4265% |
| TSTC9D | 91.722.544 B | 0,250000 | 0,630291 | 6,2286% | 4,0489% |

Il costo e' diagnostico e viene allocato solo quando richiesto. Il percorso OFF
non costruisce il tracker e non aggiunge branch al traversal.

## 6. A/B di non interferenza

Su AHKHQH @80, OFF e ON hanno prodotto valori identici per iterazioni, dEV,
NashConv, root EV e tutti i work counter. Il test production confronta
direttamente e byte-per-byte:

1. `cumulative_regret_uint16`;
2. `cumulative_strategy_uint16`;
3. `regret_node_scale`;
4. `strategy_node_scale`.

Confronta inoltre profilo, BR esatta e contatori a ogni certificazione. Il test
toy forza scale ampie conservando i codici firmati reali e verifica che i
negativi siano riconosciuti come candidati; un confronto prima/dopo prova che
l'osservazione non modifica il checkpoint. Il confronto seriale/parallelo
rimane byte-identico.

Il singolo tempo AHK OFF e' 0,758590 s e ON 0,825057 s. La differenza e'
overhead di osservazione, non una misura stabile di regressione del solver; il
traversal e i suoi contatori sono identici.

## 7. Validazione

- build Release completa: PASS;
- CTest Release: **20/20 PASS**, 199,58 s;
- test F7: **215 asserzioni PASS**;
- `git diff --check`: PASS;
- format-check globale: FAIL preesistente su file CRLF/non normalizzati; il
  comando segnala migliaia di righe anche fuori dal diff e non e' stato usato
  per riscrivere in massa file user-owned.

## Decisione

RBP effettivo resta non implementato. Non e' autorizzato aggiungere skip,
revisit schedule, regret differiti o metadati persistenti al motore production.
Un eventuale nuovo esperimento richiede prima un teorema o una validazione
indipendente per discounted signed regret e averaging `t^2`; dovra' poi essere
un ramo sperimentale separato, non una modifica del contratto congelato.

## Passo successivo

Riprendere il profiling del codice condiviso del traversal production su
TSTC9D, fuori dall'RBP, cercando una riduzione misurabile del lavoro per
iterazione senza cambiare algoritmo, fixture o ordine numerico.
