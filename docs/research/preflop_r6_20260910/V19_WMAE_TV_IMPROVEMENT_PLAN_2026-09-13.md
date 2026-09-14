# Piano V19 per ridurre WMAE e TV

Data: 2026-09-13  
Stato: `ESECUZIONE CONCLUSA / GATE C FAIL / GLOBAL CRN REJECTED`  
Scope: solver HU Short Deck CO40, preflop con continuation postflop campionata  
Baseline: V17  
V18: deprecata per la decisione strategica

## Aggiornamento esecutivo — 2026-09-14

Le Fasi A e B sono implementate e verificate. Il Gate C è stato eseguito con una coppia C2 matched
da `2M + 2M` e con V17 come controllo C1. Il CRN globale migliora la WMAE media da `13,9472 pp` a
`13,6347 pp`, ma peggiora la TV fra seed da `10,9453 pp` a `12,1140 pp` e porta gli errori
Call/Fold materialmente raggiunti da 7 a 10. Il candidato è respinto; V17 resta la baseline e il
viewer non cambia.

Il precedente `memory_failure` proveniva da un run non matched in perfect recall, non dalla V8
street-adaptive congelata. I solve corretti terminano entro 44,1 minuti e 2,91 GB private. La Fase
D non viene eseguita perché il Gate C è fallito. Il decision tree instrada il lavoro successivo a
E/F, che richiede un nuovo esperimento preregistrato e non altre iterazioni dello stesso candidato.

## 1. Obiettivo

Ridurre simultaneamente:

1. la `WMAE` della strategia media rispetto al riferimento esterno;
2. la `TV` fra due seed indipendenti;
3. la dispersione degli action EV nelle continuation non all-in;
4. gli errori esatti Call/Fold sui rami pubblicamente raggiunti.

Il target operativo è:

| Metrica | Baseline V17 | Gate intermedio | Target di lavoro | Target finale |
|---|---:|---:|---:|---:|
| WMAE media | 13,9472 pp | `<=12,55 pp` oppure miglioramento `>=10%` | circa 8 pp | `<=8,5 pp` |
| TV media fra seed | 10,9453 pp | `<=9,85 pp` oppure miglioramento `>=10%` | 5–6 pp | `<=6,0 pp` |
| TV policy corrente | 11,1472 pp | non peggiore della baseline | diagnostica | non usata da sola per pubblicazione |
| Errori Call/Fold esatti, reach pubblica `>=1%` | 7 complessivi | `<=6` | nessuna regressione | nessuna regressione |

Il target finale non autorizza a usare il riferimento esterno come segnale di training. Il riferimento resta un controllo diagnostico finché il contratto Monker completo non è disponibile.

## 2. Evidenza di partenza

### 2.1 V17

V17 ha corretto la semantica del profilo corrente, ma non ha modificato il training V15.

| Metrica | Valore |
|---|---:|
| Training per seed | 2.000.000 iterazioni |
| Solve medio | 36,14 minuti |
| WMAE seed 1 | 14,1312 pp |
| WMAE seed 2 | 13,7631 pp |
| WMAE media | 13,9472 pp |
| TV media fra seed | 10,9453 pp |
| TV corrente fra seed | 11,1472 pp |
| Infoset | circa 1,56 milioni |
| Rappresentazione | V8 street-adaptive `32/128/512` |
| Sampling | Linear MCCFR, batch 32, 8 worker |
| Rollout | K=4, update simmetrico |
| All-in | preflop esatto, flop/turn esatto con cache lazy |
| Evaluator | tabella sette carte V13 |

La decomposizione V17 attribuisce `10,2475 pp` della TV media a spostamenti fra azioni con gap EV medio `<=0,1a`. La massa più grande appartiene a offsuit e suited:

| Famiglia | Contributo TV medio |
|---|---:|
| Coppie | 1,4581 pp |
| Suited | 4,2098 pp |
| Offsuit | 5,2774 pp |

### 2.2 Residuo per azione

La diagnosi a 4M mostra un residuo stabile, non spiegabile soltanto dal rumore:

| Azione | Delta aggregato solver rispetto al riferimento |
|---|---:|
| All-in | `-23,0485 pp` |
| Raise 6 | `+9,4565 pp` |
| Raise 10 | `+2,1994 pp` |
| Call | `+21,2187 pp` |
| Fold | `-9,8259 pp` |

Interpretazione operativa: le continuation `Call` e `Raise` sono probabilmente troppo favorevoli in una parte degli stati postflop, oppure l’astrazione fonde stati con valori relativi diversi. L’origine precisa deve essere misurata prima di introdurre una nuova rappresentazione.

### 2.3 V18 da non ripetere

V18 ha aggiunto due milioni di refinement preflop con policy postflop congelata.

| Metrica | V17 | V18 |
|---|---:|---:|
| WMAE media | 13,9472 pp | 13,3838 pp |
| TV fra seed | 10,9453 pp | 13,0159 pp |
| Miglioramento WMAE | — | 4,04% |
| Gate 10% | — | fallito |

La sola fase di refinement ha TV fra seed `16,6728 pp`. Non eseguire altri run lunghi della stessa variante.

## 3. Contratto congelato

Ogni esperimento deve mantenere invariati, salvo la variabile esplicitamente dichiarata:

- mazzo Short Deck a 36 carte;
- ante, blind, stack e rake del fixture CO40;
- range, masse fisiche `6/4/12` e 630 combo;
- albero preflop e sizing;
- cinque azioni root;
- evaluator e fingerprint della tabella sette carte V13;
- payoff e `settle_terminal`;
- precisione numerica;
- seed di partizione;
- numero massimo di worker: 8;
- isolamento del target `gtosd_preflop_trainer`;
- assenza di riferimenti Monker nel codice di training.

Non modificare i default di produzione, il viewer o il file policy pubblicato prima del superamento dei gate.

## 4. Baseline riproducibile

La baseline ufficiale deve essere eseguita o verificata con questi elementi:

```text
algorithm                 linear_mccfr
iterations                2000000
training_batch_iterations 32
threads                   8
flop_buckets              32
turn_buckets              128
river_buckets             512
equity_samples            8
root_action_rollouts      4
continuation_mean         enabled
symmetric_mean_updates    enabled
postflop_representation   street_adaptive_v8
preflop_all_in            exact
postflop_all_in           exact_flop_turn_cached
opponent_baseline         disabled
public_board_sampling     disabled
preflop_refinement        0
```

CLI di riferimento, da adattare soltanto ai percorsi locali:

```powershell
gtosd_hu_preflop_solve `
  --config <co40-config.json> `
  --output <v17-seed1.json> `
  --postflop-policy-output <v17-seed1-policy.bin> `
  --algorithm linear_mccfr `
  --iterations 2000000 `
  --seed <seed-1> `
  --partition-seed <partition-seed> `
  --evaluation-seed <evaluation-seed-1> `
  --threads 8 `
  --training-batch-iterations 32 `
  --flop-buckets 32 `
  --turn-buckets 128 `
  --river-buckets 512 `
  --equity-samples 8 `
  --root-action-value-rollouts 4 `
  --root-continuation-mean-updates `
  --symmetric-traverser-mean-updates `
  --exact-preflop-all-in-expectation `
  --exact-postflop-all-in-flop-turn `
  --seven-card-table <seven-card-table.bin> `
  --full-preflop-chart-export `
  --evaluate-current-profile
```

Ripetere per il seed 2 con seed di training e valutazione indipendenti. Conservare stdout, stderr, JSON, policy binaria, fingerprint, SHA-256 e log di sistema.

## 5. Regole sperimentali comuni

Ogni candidato deve avere un identificatore univoco e serializzato nell’output JSON. L’identificatore deve includere:

- algoritmo;
- rappresentazione;
- numero di rollout;
- metodo CRN;
- seed;
- partizione;
- modalità all-in;
- capacità bucket;
- eventuale split selettivo;
- versione del protocollo.

Non confrontare run con:

- evaluator diversi;
- tree fingerprint diversi;
- range diversi;
- payoff diversi;
- stopping rule diverse;
- policy postflop non dichiarata;
- numero di update diverso senza normalizzazione esplicita.

Le iterazioni non hanno tutte lo stesso scopo. Questo protocollo separa tre livelli:

1. **Test deterministici e replay**: verificano formule, mapping, RNG, checksum, legalità delle carte, normalizzazione, contatori e overhead. Non usano WMAE o TV per promuovere un candidato.
2. **Diagnostica offline**: analizza gli artefatti V17/V17-4M già prodotti (e, se necessario, un nuovo run completo con la stessa durata della baseline). Serve a localizzare la causa, non a stimare la qualità di una nuova policy.
3. **Test di qualità strategica**: confrontano controllo e candidato con una coppia matched di `2M + 2M` iterazioni per ciascuna variante, due seed e stessa configurazione (quattro solve complessivi). Solo questa coppia può decidere WMAE, TV o promozione.

### Perché non usare più 250k e 500k come gate

La precedente progressione `250k → 500k` è rimossa. A quelle durate la strategia può essere lontana dal plateau e la differenza osservata fra seed può cambiare segno. Usarle per decidere WMAE o TV produrrebbe un gate sensibile al rumore, anche se si eseguono due seed.

250k/500k non sono quindi né una misura di convergenza né un filtro affidabile per una run lunga. Un miglioramento breve non autorizza automaticamente il run successivo e un peggioramento breve non prova da solo un difetto strategico.

Se serve misurare il costo del codice nuovo, si usa un solo budget preregistrato di **smoke/performance**, senza leggere WMAE o TV come risultato strategico. Se serve misurare la qualità, si passa direttamente alla coppia matched da `2M + 2M`.

`2M` è il budget minimo comparabile con V17, non una garanzia di rumore nullo. Per questo una decisione di promozione deve restare coerente nella replica indipendente da `2M + 2M` della Fase D; se gli intervalli si sovrappongono al gate, il risultato resta `INCONCLUSIVE`.

### Controlli anti-rumore

Per ogni confronto di qualità:

1. usare gli stessi seed di training, partizione, evaluator e configurazione del controllo;
2. confrontare candidato e controllo a coppie, con due seed indipendenti;
3. riportare WMAE, TV, P95 TV, action-difference variance e intervalli bootstrap/errori standard;
4. preregistrare durata, soglie e criterio di promozione prima di guardare i risultati;
5. non usare il riferimento esterno come criterio di training o come sostituto della convergenza.

Un risultato che non supera l’incertezza preregistrata è `INCONCLUSIVE`: non si aggiungono repliche corte per cercare un segnale favorevole; si ripete la coppia matched da `2M + 2M` oppure si abbandona il candidato.

## 6. Fase A — Audit causale delle continuation

### Obiettivo

Determinare se la TV deriva principalmente da:

1. varianza dello stimatore;
2. aliasing dei bucket;
3. sotto-copertura dei rami non all-in;
4. differenza strutturale dell’albero postflop esterno.

### A.1 Telemetria da aggiungere

Per ogni infoset preflop raggiunto e per ogni azione, registrare in una struttura diagnostica separata:

```text
node_id
player
history
hand_class
physical_combo_mass
public_reach
own_reach
action_id
sample_count
mean_action_value
variance_action_value
standard_error_action_value
mean_action_advantage
bucket_key
bucket_occupancy
postflop_street
terminal_type
all_in_exact_count
all_in_sampled_count
```

La telemetria non deve modificare regret, strategy sum, RNG, ordine delle riduzioni o payoff.

### A.2 Separazione dei terminali

Separare almeno:

- fold preflop;
- all-in preflop esatto;
- all-in flop esatto;
- all-in turn esatto;
- showdown river;
- fold postflop;
- terminali non all-in che proseguono su street successive.

Il contatore degli all-in esatti deve essere distinto dal numero di update. Il tempo sommato dai worker non deve essere presentato come wall clock.

### A.3 Misura dello spread dentro i bucket

Per ogni chiave astratta `K`, calcolare:

```text
spread(K, action) = max(value_physical) - min(value_physical)
```

e anche:

```text
weighted_variance(K, action)
weighted_range(K, action)
weighted_mass(K)
```

Ordinare le chiavi per:

```text
impact(K) = weighted_mass(K) * max_action_pair_spread(K)
```

Non usare il riferimento esterno per creare questa classifica. La classifica deve derivare da valori fisici e dati del solver.

### A.4 Analisi e test diagnostici

Prima usare gli artefatti V17/V17-4M già prodotti. Se la telemetria richiede un nuovo solve, eseguire la stessa durata della baseline (`2M` per seed), non un solve da 250k/500k. Questo run è diagnostico: non promuove una policy e non cambia il gate WMAE/TV. Il test deve produrre:

- top 100 bucket per spread;
- top 100 bucket per contributo TV stimato;
- contributo per famiglia coppie/suited/offsuit;
- Call-vs-All-in e Raise-vs-All-in;
- istogramma degli action gap;
- percentuale di massa con gap `<=0,1a`, `0,1–0,5a`, `>=0,5a`.

### Gate A

La fase passa se:

- le marginali dei payoff restano bit-identiche o entro la tolleranza numerica documentata;
- la telemetria OFF/ON non cambia policy, checksum o ordine di update;
- sono identificati bucket con contributo misurabile alla dispersione;
- non viene confusa la varianza dell’EV con abstraction error.

Se nessun bucket mostra spread elevato ma il bias esterno resta, sospendere il lavoro sull’astrazione e aprire il ramo di comparabilità dell’albero postflop.

## 7. Fase B — Test matematico CRN globale

### Obiettivo

Ridurre la varianza delle differenze fra azioni nello stesso infoset senza modificare la distribuzione marginale di ogni azione.

V15 applica CRN alla root. Questo test deve generalizzare il concetto a tutti i nodi preflop coperti dal trainer.

### B.1 Semantica richiesta

Per un infoset `I` con azioni `a_1 ... a_n`:

1. creare un contesto casuale deterministico per `(seed, iteration, traverser, infoset_id)`;
2. salvare lo stato RNG all’ingresso del confronto azioni;
3. ripristinare lo stesso stato prima di valutare ogni azione;
4. mantenere identica la policy avversaria e la reach;
5. usare lo stesso deal pubblico, ove legalmente condivisibile;
6. non riutilizzare carte illegali o collisioni fisiche;
7. ripristinare il vecchio percorso indipendente quando il contesto non è compatibile;
8. serializzare il metodo nel fingerprint dell’algoritmo.

CRN deve correlare i campioni, non sostituire il valore atteso con un valore comune o con una frequenza esterna.

### B.2 Test algebrici

Creare test su:

- gioco giocattolo enumerabile;
- nodo con due azioni e payoff noti;
- nodo con cinque azioni CO;
- campioni con RNG ripristinato;
- deal con card removal;
- terminale all-in esatto;
- terminale postflop campionato;
- batch da 1, 2, 4 e 8 worker.

Verificare:

```text
E_CRN[value(a)] = E_independent[value(a)]
```

e:

```text
Var_CRN[value(a) - value(b)] <= Var_independent[value(a) - value(b)]
```

La seconda disuguaglianza è un obiettivo empirico, non una proprietà da assumere senza misurazione.

### B.3 Determinismo parallelo

A parità di seed, batch e input, i run con 1 e 8 worker devono produrre:

- stesso fingerprint della policy;
- stessi regret e strategy sum, se il formato lo consente;
- stesso root EV;
- stessi contatori di campionamento;
- stesso export JSON;
- stesso checksum.

### Gate B

Il gate B è esclusivamente matematico e deterministico:

- marginale CRN invariata rispetto al percorso indipendente;
- riduzione della varianza action-difference misurata su fixture enumerate o su replay paired;
- nessuna collisione di carte, azione illegale o differenza di contatori inattesa;
- determinismo 1/8 worker entro il contratto numerico;
- overhead e memoria entro il budget preregistrato.

WMAE e TV non vengono valutate in questo gate. Se B passa, si esegue direttamente la coppia di qualità C da `2M + 2M`; se fallisce, il candidato viene corretto o scartato senza run intermedi da 250k/500k.

## 8. Fase C — Coppia CRN completa sul trainer principale

### Configurazione

Usare V17 come controllo e aggiungere soltanto il CRN globale. Non combinare nella prima run:

- CRN globale;
- nuova capacità bucket;
- nuova abstraction history;
- DCFR;
- opponent baseline;
- refinement V18;
- public-board sampling.

La prima implementazione può mantenere il flag root esistente come sotto-caso compatibile, ma deve aggiungere un identificatore diverso per il percorso globale.

### Matrice

| Run | Scopo | Iterazioni | Seed | CRN | Abstraction |
|---|---|---:|---|---|---|
| C0 | replay/unit/property | n/a | fixture fissate | root-only e globale | V8 `32/128/512` |
| C1 | controllo qualità | 2M | 1/2 | root-only | V8 `32/128/512` |
| C2 | candidato qualità | 2M | 1/2 | globale | V8 `32/128/512` |

C1 e C2 sono eseguiti come coppia matched completa. Non esiste un gate preliminare a 250k/500k.

### Gate C

La coppia C1/C2 è il gate di qualità e deve soddisfare:

- WMAE del candidato non oltre `0,5 pp` sopra il controllo in nessun seed e non peggiorata in media;
- TV fra seed del candidato migliorata di almeno `10%` rispetto al controllo C1;
- errore Call/Fold non peggiorato;
- action-difference variance ridotta nei bucket ad alto impatto;
- costo proiettato sotto `60 minuti` per seed;
- memoria e scratch entro i budget della baseline.

## 9. Fase D — Run CRN completo da 2M

Eseguire una seconda coppia indipendente da `2M + 2M` usando seed preregistrati diversi (`3/4`) e la configurazione candidata che ha superato C2. Questa replica misura la stabilità del risultato C1/C2; non è una scorciatoia basata su un miglioramento breve.

Ogni run deve salvare:

- candidato JSON;
- policy postflop;
- policy preflop;
- log completo;
- output stderr;
- fingerprint gioco/albero/evaluator;
- checksum;
- decomposizione TV;
- audit corrente e media;
- audit Call/Fold esatto;
- contatori CRN;
- wall clock e private peak memory;
- payload numerico e scratch.

### Gate D

Gate intermedio:

- WMAE media `<=12,55 pp` oppure miglioramento `>=10%`;
- TV fra seed `<=9,85 pp` oppure miglioramento `>=10%`;
- direzione del miglioramento coerente con C1/C2, senza inversione fra le due coppie;
- nessuna regressione Call/Fold;
- nessuna policy corrente chiaramente EV-dominata sui rami con reach pubblica `>=1%`.

Se passa, il candidato diventa la baseline sperimentale successiva. Non diventa ancora il viewer di default.

## 10. Fase E — Split selettivo dell’astrazione

Questa fase parte soltanto dopo l’audit A e, preferibilmente, dopo il test CRN.

### Obiettivo

Separare gli stati che il bucket V8 fonde impropriamente, senza introdurre history completa ovunque.

### E.1 Regola di selezione

Uno split è autorizzato solo se la chiave candidata soddisfa tutte le condizioni:

```text
weighted_mass(K) >= soglia_mass
spread(K) >= soglia_spread
visits(K) >= soglia_visits
action_gap(K) è misurato con dati indipendenti
```

Le soglie devono essere preregistrate prima del run. Non scegliere gli split guardando direttamente le frequenze Monker.

### E.2 Possibili feature

Valutare una feature alla volta:

1. street precedente;
2. categoria della street precedente;
3. bucket precedente solo per una piccola lista di chiavi ad alto impatto;
4. action history preflop/postflop limitata;
5. pot/SPR discretizzato;
6. posizione e giocatore attivo già presenti nella chiave;
7. flag all-in imminente o terminale.

Non introdurre simultaneamente `bucket_history`, perfect recall e capacità aumentata.

### E.3 Invarianti di mapping

Ogni split deve conservare:

- card removal;
- masse fisiche;
- normalizzazione delle strategie;
- validità delle azioni;
- stessa terminal utility;
- assenza di collisioni fra history incompatibili;
- persistenza versionata;
- caricamento/rifiuto esplicito di policy con mapping diverso.

### E.4 Test della feature

Prima eseguire soltanto test di mapping, replay deterministico e fixture ridotte enumerate. Questi test verificano collisioni, card removal, normalizzazione, persistenza e costo del mapping; non usano WMAE/TV.

Per la qualità strategica, ogni feature candidata richiede direttamente una coppia matched completa da `2M + 2M` (due seed per controllo e candidato), con lo stesso CRN, evaluator, batch e numero di worker. Non usare una progressione `250k → 500k` per decidere se continuare.

Registrare infoset, payload, peak private, cache, tempo, numero di split e metriche complete della coppia.

### Gate E

Promuovere una feature soltanto se:

- WMAE migliora di almeno `0,5 pp` oppure TV fra seed di almeno `20%`;
- l’altra metrica non peggiora oltre `0,5 pp`;
- nessun seed peggiora oltre `0,5 pp`;
- il contributo all-in/call migliora senza introdurre nuovi errori Call/Fold;
- la crescita dello stato è proporzionata al beneficio;
- il mapping resta deterministico e persistibile.

V9 e V11 sono precedenti negativi: più infoset e più payload non sono di per sé un miglioramento.

## 11. Fase F — Holdout postflop fisico

### Obiettivo

Capire se la WMAE elevata deriva da continuation value postflop errati, indipendentemente dall’aggiornamento preflop.

### F.1 Corpus

Creare un corpus versionato di holdout con:

- flop rainbow;
- flop two-tone;
- board paired;
- board con draw di scala;
- board Short Deck con wheel A-6-7-8-9;
- almeno un caso per ogni street;
- almeno un caso per ogni action history `Call`, `Raise 6`, `Raise 10`, `All-in`;
- range asimmetrici e card removal;
- casi con all-in flop/turn e casi che proseguono fino al river.

### F.2 Valori da confrontare

Per ogni holdout:

1. valore terminale esatto dell’all-in;
2. valore del bucket corrente;
3. valore della policy postflop media;
4. valore della policy postflop corrente;
5. errore per azione;
6. errore aggregato per classe;
7. BR fisica, se disponibile;
8. reach-conditioned EV.

L’all-in esatto elimina il rumore del runout, ma non corregge l’errore dei rami non all-in. Separare i due effetti nei report.

### F.3 Separazione architetturale

Il trainer preflop deve restare autonomo. Non collegare il target preflop allo standalone postflop solver come dipendenza nascosta.

Il corpus può essere valutato da un target diagnostico separato, purché:

- il metodo sia dichiarato;
- il risultato non venga usato come label di training;
- la policy importata abbia fingerprint e checksum;
- la comparazione distingua gioco fisico, gioco astratto e continuation esterna.

### Gate F

La fase produce una decisione causale:

- `CONTINUATION_VARIANCE_CONFIRMED` se CRN/all-in exact riducono l’errore ma i valori restano instabili;
- `ABSTRACTION_ALIASING_CONFIRMED` se lo spread intra-bucket spiega il bias;
- `POSTFLOP_TREE_MISMATCH_UNRESOLVED` se i valori locali sono coerenti ma il riferimento esterno resta diverso;
- `NO_CAUSAL_SIGNAL` se nessuna differenza supera l’incertezza.

Non passare direttamente da un holdout a un run completo.

## 12. Fase G — Capacità mirata

Solo dopo F, testare capacità aggiuntiva nei soli bucket ad alto impatto.

Ordine consigliato:

1. split selettivo con capacità globale invariata;
2. capacità raddoppiata soltanto per flop ad alta massa;
3. capacità raddoppiata soltanto per turn ad alta dispersione;
4. capacità river solo se il holdout mostra errore river dominante.

Non usare MC16 come sostituto: ha migliorato WMAE solo di `0,069 pp`, peggiorato la stabilità e aumentato il costo di circa `50%`.

Ogni aumento di capacità deve riportare:

- infoset aggiunti;
- payload;
- cache hit/miss/eviction;
- wall clock;
- WMAE;
- TV fra seed;
- P95 TV;
- errore massimo root;
- audit EV.

La decisione di qualità per ogni aumento usa una coppia matched da `2M + 2M`. Eventuali fixture brevi servono soltanto a verificare mapping, memoria e correttezza del percorso; non autorizzano la promozione sulla base di WMAE o TV.

## 13. Fase H — Challenger algoritmici

Non è la prima priorità. Dopo aver corretto stimatore e rappresentazione, valutare separatamente:

- DCFR online;
- External Sampling;
- Linear MCCFR;
- chance-sampled CFR.

Non combinare subito algoritmo, abstraction e sampling. Ogni challenger deve usare lo stesso gioco e la stessa rappresentazione della baseline.

La DCFR precedente ha mostrato WMAE migliore in alcuni test, ma costi maggiori e EV non conclusivo; non è una promozione automatica. Ogni challenger richiede direttamente una coppia matched da `2M + 2M` dopo i test deterministici, senza gate a 250k/500k.

## 14. Metriche obbligatorie

### 14.1 Metriche di qualità

- WMAE per seed;
- WMAE media;
- TV per combo;
- TV aggregata root;
- TV fra seed;
- P95 TV;
- massimo delta di un’azione;
- errore Call/Fold esatto;
- perdita EV pesata sul deal root;
- EV root e intervallo di errore;
- action-difference variance;
- massa TV per gap EV.

### 14.2 Metriche del solver

- wall clock;
- solve seconds interni;
- iterazioni;
- traversate/s;
- infoset;
- payload numerico;
- peak private memory;
- scratch;
- cache mapping;
- cache all-in;
- hit/miss/eviction;
- numero di update per worker.

### 14.3 Metriche di integrità

- tree fingerprint;
- evaluator fingerprint;
- policy fingerprint;
- SHA-256 di tutti gli artefatti;
- normalizzazione massima;
- valori non finiti;
- azioni illegali;
- combo duplicate;
- collisioni di carte;
- determinismo 1/8 worker.

## 15. Test automatici richiesti

### Unit test

- RNG snapshot/restore;
- deal CRN con card removal;
- marginale CRN invariata;
- terminale all-in esatto;
- cache all-in trasparente;
- split di una chiave bucket;
- normalizzazione strategy sum;
- serializzazione mapping versionato.

### Property test

- nessuna carta duplicata;
- nessun payoff non finito;
- stessa azione produce stesso campione a RNG ripristinato;
- permutation invariance dei semi;
- somma delle probabilità pari a uno;
- utility coerente prima del rake;
- equivalenza marginale CRN/independent;
- nessuna differenza 1/8 worker.

### Integration test

- solve ridotto con CRN globale;
- solve ridotto senza CRN;
- confronto dei contatori;
- export completo 20 nodi e 1.620 righe;
- caricamento policy e fingerprint;
- rifiuto di policy con mapping incompatibile;
- audit post-training senza mutazione della policy.

### Regression test

Ogni bug trovato deve aggiungere:

- fixture minima;
- seed;
- input JSON;
- output atteso;
- categoria dell’invariante violato;
- collegamento al report della fase.

## 16. Artefatti per ogni candidato

La directory del candidato deve contenere:

```text
<candidate>.json
<candidate>.comparison.json
<candidate>.log
<candidate>.stderr.log
<candidate>_policy.bin
<candidate>_pair_analysis.json
<candidate>_seed_tv_decomposition.json
<candidate>_action_ev_audit.json
<candidate>_allin_training_diagnostic.json
<candidate>_checksums.txt
<candidate>_REPORT.md
```

Il report deve indicare:

- commit e working tree;
- hardware e sistema operativo;
- compiler e flags;
- configurazione completa;
- seed e partizione;
- durata;
- RAM;
- metriche complete;
- gate PASS/FAIL/NOT EVALUATED;
- limitazioni;
- decisione di promozione o rigetto.

## 17. Criteri di stop

Interrompere un esperimento senza completare il run lungo se si verifica una delle condizioni:

- marginali dei payoff alterate;
- checksum non deterministico;
- collisione di carte;
- policy non normalizzata;
- memoria oltre il budget dichiarato;
- errore di mapping o fingerprint;
- WMAE peggiorata oltre `0,5 pp` in entrambi i seed della coppia matched;
- TV fra seed peggiorata oltre `10%` senza compensazione documentata;
- nuova strategia corrente con azioni chiaramente EV-negative su reach pubblica `>=1%`;
- costo proiettato oltre `60 minuti` per seed senza segnale quantitativo.

Un arresto precoce deve essere registrato come `EARLY_STOP_WITH_EVIDENCE`, non come fallimento del solver.

## 18. Decision tree operativo

```text
V17 baseline
  |
  +-- A: audit continuation/alias
  |       |
  |       +-- nessun segnale -> F: comparabilità postflop esterna
  |       |
  |       +-- varianza alta -> B/C: CRN globale
  |       |
  |       +-- spread intra-bucket alto -> E: split selettivo
  |
  +-- B passa
  |       |
  |       +-- C: coppia matched da 2M + 2M
  |               |
  |               +-- gate passa -> D: replica indipendente da 2M + 2M
  |               +-- gate fallisce -> E/F, non più iterazioni identiche
  |
  +-- E passa
  |       |
  |       +-- coppia matched da 2M + 2M -> D
  |
  +-- F conferma continuation error
          |
          +-- G: capacità mirata / holdout postflop
```

## 19. Decisione finale attesa

La promozione nel viewer richiede una coppia completa che:

- mantenga correttezza, persistenza e determinismo;
- raggiunga WMAE circa `8 pp`;
- raggiunga TV fra seed `5–6 pp`;
- non introduca regressioni Call/Fold;
- superi gli audit fisici e monetari;
- documenti esplicitamente qualunque astrazione;
- distingua sempre metrica interna, confronto esterno e certificazione.

Se nessun candidato raggiunge il target, pubblicare la migliore variante come `RESEARCH_ONLY`, mantenendo V17 come baseline diagnostica e V13/V17 come sorgenti del viewer secondo il gate corrente.

## 20. Primo task dell’agente coder

Implementare esclusivamente la Fase A:

1. aggiungere telemetria action-conditioned non mutante;
2. aggiungere test OFF/ON bit-identici;
3. esportare spread e varianza per bucket;
4. eseguire i test deterministici e l’analisi degli artefatti V17/V17-4M; se serve un nuovo solve, usare `2M` per seed senza trattarlo come gate di qualità;
5. scrivere il report A con decisione `PROCEED_CRN`, `PROCEED_ALIAS_SPLIT`, `PROCEED_EXTERNAL_COMPARABILITY` oppure `BLOCKED`.

Non implementare ancora CRN globale, nuovi bucket o modifiche ai default di produzione.

## 21. Esito dell'esecuzione autorizzata

L'autorizzazione successiva dell'utente ha esteso l'esecuzione oltre il primo task della sezione
20. L'esito verificato è:

| Gate | Stato | Risultato |
| --- | --- | --- |
| A | `PASS_WITH_CAPPED_EVIDENCE` | telemetria non mutante; classifica bucket a 2M, cap 10.000 |
| B | `ENGINEERING_AND_FINITE_FIXTURE_PASS` | marginali conservate, varianza paired ridotta, determinismo 1/8 |
| C | `FAIL` | WMAE -2,24%, TV +10,68%, Call/Fold 7 → 10 |
| D | `NOT_RUN_BY_GATE` | C non ha qualificato il candidato |
| E/F | `ROUTED` | prossimo esperimento separato; nessun'altra run global CRN |

Il report con configurazioni, tempi, memoria, checksum e limiti è
[`V19_COMPLETION_REPORT_2026-09-14.md`](V19_COMPLETION_REPORT_2026-09-14.md).
