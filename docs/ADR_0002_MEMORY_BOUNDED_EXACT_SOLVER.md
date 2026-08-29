# ADR 0002 — Motore exact memory-bounded

Stato: **Accettato; compilatore layout-only implementato, traversal non ancora migrato**
Data: 2026-08-27

## Contesto

`GTP-TSTC9D-101` contiene 2.791.872 nodi pubblici fisici, 231.129.064
information set e 582.634.552 action entry. Lo stato packed corrente usa
1.747.903.656 byte, esattamente 3 byte/action. Il riferimento GTO+ usa
2.000.000.000 byte per l'intero solve, non per il solo stato persistente: al
processo GTOSD restano quindi appena 252.096.344 byte per topologia, indici,
reach, valori, best response, allocator e parallelismo.

Il percorso fisico corrente non rispetta questo contratto. Due report completi
precedenti hanno misurato peak RSS di 3.166.359.552 e 3.173.212.160 byte. Il
layout espone inoltre 27.966.458.496 byte logici di delta regret transienti:
un vettore `float64` globale e una copia completa per ciascuno dei cinque
worker. Un probe del build corrente è stato interrotto dopo avere osservato
12.031.537.152 byte di working set e 13.155.037.184 byte privati. Quest'ultima
è evidenza diagnostica di allocazione/touch, non un benchmark completato.

La causa architetturale è duplice:

1. il solver costruisce e attraversa il public tree fisico prima di applicare
   le riduzioni;
2. quando più occorrenze fisiche condividono uno stato canonico, il
   parallelismo conserva la semantica del clipping allocando delta densi
   `O(worker * actions)`.

L'esperimento che ha aggregato le orbite pubbliche con range player-asimmetrici
non è promuovibile: dopo due iterazioni ha modificato profile EV, best response,
regret e strategy. Il problema non autorizza a disabilitare la simmetria o ad
allargare le tolleranze; richiede coordinate, reach e molteplicità player-local.

## Decisione

Il motore HU postflop exact adotterà un **canonical chance tree costruito
direttamente**, con **trasformazioni private player-local** e
un traversal **owner-computes a memoria limitata**. Il regret minimizer
production iniziale sarà **DCFR standard** con update alternati e parametri
versionati `alpha=1.5`, `beta=0`, `gamma=2`; CFR+ resta l'oracle differenziale
e il fallback exact. La rappresentazione lossless non cambia il gioco, le
chance o le utility.

L'attuale `DcfrPlus`, che combina proiezione non-negativa CFR+ e discount dei
soli regret positivi, non deve essere rinominato silenziosamente in DCFR. Può
restare come algoritmo legacy con identificatore proprio, ma il nuovo kernel
implementa le formule DCFR positive e negative pubblicate oppure usa CFR+.

La decomposizione CFR-D è accettata come secondo livello per giochi che non
entrano nel budget dopo la canonicalizzazione lossless, in particolare il
preflop. Non sostituisce il kernel TSTC9D finché quest'ultimo può essere
rappresentato esattamente nel budget. MCCFR resta una modalità campionata
separata e non può certificare il percorso `exact`.

## Architettura scelta

```text
config/rules/ranges
        |
        v
canonical chance compiler ----> symmetry audit / physical expander (test only)
        |
        +---- immutable orbit-representative chance tree
        +---- subtree-local infoset/action interval
        +---- chance-edge suit transforms + physical multiplicities
                       |
                       v
bounded exact traversal -----> packed state store
        |                            |
        +---- exact BR streamer <----+
        +---- checkpoint/solution chunks
        |
        +---- optional CFR-D boundary store (large-game tier)
```

### Compilatore canonico diretto

Il compilatore non materializza l'intero tree fisico come passaggio
intermedio. A ogni chance node genera un solo rappresentante per orbita e
conserva:

- stato pubblico completo, inclusi board, history, pot, stack e player attivo;
- action catalog ordinato e stabile;
- figli chance canonici e molteplicità fisica degli outcome omessi;
- permutazione dei semi dall'outcome fisico al rappresentante;
- mapping inverso per i counterfactual value restituiti al padre;
- intervallo contiguo delle action entry possedute dal subtree.

Una simmetria è lossless solo se preserva separatamente il range iniziale di
ciascun player, oltre a board iniziale, regole, azioni, blocker e payoff. Non è
richiesto che i due range siano uguali tra loro. Lo stabilizzatore viene
ristretto dopo ogni carta pubblica. I counterfactual value degli outcome omessi
sono trasformati con la permutazione inversa prima dell'accumulo nel padre.

Non vengono unificate history pubbliche arbitrarie in un DAG globale. Questa
condivisione renderebbe lo stesso stato modificabile da traversal concorrenti e
reintrodurrebbe reducer densi o sincronizzazione. Una futura condivisione DAG è
ammessa soltanto quando ownership unica e equivalenza dei reach player-local
sono dimostrate.

### Information set player-local

La chiave logica è:

```text
(player, canonical_public_history, canonical_private_combo_for_player)
```

Ogni chance edge trasforma indipendentemente i due reach vector. Lo stato CFR
appartiene a un solo subtree canonico e viene aggiornato direttamente in-place.
I valori vengono trasformati indietro prima dell'accumulo nel padre. Proiezione
CFR+ oppure discount DCFR vengono applicati una sola volta per update logico
completo dell'information set.

Questi vincoli sono parte della correttezza, non dettagli di ottimizzazione.
Se l'equivalenza dei reach non è dimostrabile, gli outcome restano distinti.

### Stato e traversal

Lo stato hot rimane indicizzato per action entry canonica. DCFR richiede regret
con segno: il candidato packed usa 13 bit signed-regret e 11 bit unsigned
strategy per action, sempre 3 byte/action, con compute `float64`. Codec,
discount positivo/negativo e accumulo della strategia devono superare il
differenziale contro uno stato `float64` prima della promozione. Se il codec
signed non supera il gate numerico, il profilo production passa a CFR+ con il
formato non-negativo corrente; non è ammesso aumentare la RAM o alterare le
formule senza una nuova decisione.

Il traversal non può allocare strutture `O(actions)` per worker. Reach, valori
e riduzioni sono stack/arena bounded di dimensione
`O(thread * depth * active_combos * max_actions)`. Gli update sono diretti
quando il task possiede un intervallo di stato disgiunto. Un task parallelo può
essere creato soltanto se tale disgiunzione è verificata dal compilatore; in
caso contrario il ramo è seriale o usa un reducer a tile con limite esplicito.

Le riduzioni dei valori al padre seguono l'ordine canonico degli edge. Il numero
di thread non deve cambiare fingerprint, layout o semantica; ogni deriva
floating-point deve restare entro il gate dichiarato e avere un test A/B.

La best response è read-only sullo stato medio e attraversa lo stesso chance tree in
streaming. Non materializza una seconda strategia completa e non mantiene un
workspace proporzionale a tutte le action entry.

## Budget di memoria

Il preflight usa peak RSS stimato e il benchmark usa peak RSS misurato. Il
target ingegneristico è 1.800.000.000 byte, lasciando 200 MB di margine prima
del limite GTO+ di 2.000.000.000 byte.

| Componente al picco | Budget TSTC9D |
|---|---:|
| Stato packed canonico DCFR/CFR+ | 1.101.000.000 B |
| Chance tree, edge e indici player-local | 200.000.000 B |
| Scratch di tutti i worker | 160.000.000 B |
| Certificazione/analytics temporanei | 120.000.000 B |
| Runtime, allocator e librerie | 120.000.000 B |
| Riserva interna | 100.000.000 B |
| **Target totale** | **1.800.000.000 B** |
| **Gate assoluto** | **2.000.000.000 B** |

Il compilatore layout-only misura per TSTC9D **366.890.152 action entry**, pari
a **1.100.670.456 byte** con il codec congiunto da 3 byte/action. La differenza
di 670.456 byte dal budget tondo iniziale di 1,1 GB è assorbita senza cambiare
il target complessivo: il modello completo resta tra 1.516.279.888 e
1.545.640.016 byte per 1-8 worker. Il profilo `i16 regret + u16 strategy`
richiede invece 1.888.214.808-1.917.574.936 byte: passa il gate assoluto di
2 GB, ma non il target ingegneristico di 1,8 GB.

Questi sono conteggi esatti del layout e una previsione ingegneristica del
picco, non una misura RSS del solver migrato. Il gate definitivo resta il peak
RSS del processo completo nello stesso run che certifica correttezza e tempo.

## Alternative valutate

| Alternativa | Decisione | Motivo |
|---|---|---|
| Tree fisico + delta densi per-worker | Rifiutata | RAM `O(worker * actions)`; il probe supera di molte volte il riferimento |
| Solo rimozione dei delta | Insufficiente | Lo stato lascia 252 MB a tutto il resto e non risolve il traffico del tree fisico |
| Canonical chance tree player-local | **Scelta** | Sottotree disgiunti, ownership semplice e riduzione lossless degli outcome chance |
| DAG pubblico generale | Rifiutato come primo livello | Stato multi-owner e reducer/sincronizzazione proporzionali al layout |
| Solo street decomposition | Rifiutata come primary | Il prototipo locale mostra che il river resta dominante e il boundary aumenta il picco |
| CFR+ | Oracle/fallback | Formula semplice e stato non-negativo già validabile; può convergere più lentamente del profilo DCFR |
| DCFR standard | **Regret minimizer scelto** | Discount e averaging versionati; mantiene 3 byte/action solo con codec signed validato |
| `DcfrPlus` custom corrente | Legacy, non target | Non coincide né con CFR+ né con il DCFR pubblicato |
| CFR-D | Secondo livello | Ha garanzie di decomposizione e trade-off spazio/tempo, ma cambia il protocollo operativo e richiede boundary CFV certificati |
| Out-of-core/mmap | Fallback exact | Controlla la residenza ma il solve completo e il costo dei page fault non sono ancora misurati |
| MCCFR/PCS | Modalità separata | Converge in aspettativa/probabilità, ma introduce sampling, seed e varianza nel gate |
| Bucketing/imperfect recall | Esclusa dal percorso exact | Riduce memoria modificando il gioco o introducendo errore di astrazione |

## CFR-D come livello di scala

Quando il layout canonico non rispetta il preflight, il gioco viene diviso su
public states che siano chiusi rispetto agli augmented information set. Il
trunk conserva strategia/regret e counterfactual value di frontiera; un solo
blocco/subgame è residente alla volta. La strategia scartata deve poter essere
rigenerata o persistita a chunk.

Non è sufficiente risolvere una street assumendo fisso il trunk: il boundary
deve conservare i counterfactual value avversari richiesti dalla costruzione
CFR-D. `dcfr_decomposed` avrà un identificatore algoritmo, checkpoint e
benchmark distinti da `dcfr` e dal legacy `dcfr_plus`; nessun risultato sarà dichiarato
bit-identico al traversal monolitico senza prova.

## Piano di migrazione e gate

1. **Compiler layout-only — COMPLETATO.** Costruire il chance tree per conteggio
   senza materializzare il tree fisico e pubblicare nodi, edge, infoset, action
   entry e previsione RSS, senza eseguire CFR. Su TSTC9D: 2.791.872 nodi fisici
   stimati, 1.758.624 canonici, 145.524.152 infoset e 366.890.152 action entry;
   preflight 0,0041 s e peak RSS del solo tool 6.471.680 byte.
2. **Interprete differenziale.** Espandere fixture piccole in forma fisica e
   confrontare, dopo ogni update player, reach trasformati, action value,
   regret, strategy, profile EV, exact BR e NashConv entro `1e-11`.
3. **Regret minimizer.** Confrontare DCFR `float64` con il codec signed packed
   a `alpha=1.5`, `beta=0`, `gamma=2`. In caso di FAIL, selezionare CFR+ packed
   senza modificare il resto dell'architettura.
4. **Range asimmetrici.** Coprire range player-diversi e pesati con almeno una
   simmetria non banale; il test oggi divergente deve diventare PASS senza
   fallback e senza cambiare le golden fisiche.
5. **Runtime single-thread bounded.** Eliminare tutti i buffer
   `O(actions)` diversi dallo stato persistente. Misurare peak RSS prima di
   reintrodurre il pool.
6. **Parallelismo owner-computes.** Abilitare soltanto partizioni con action
   interval disgiunti e verificare equivalenza 1/N thread.
7. **TSTC9D singolo.** Richiedere dEV ≤ 1%, root EV entro 0,05 ante, peak RSS
   ≤ 2.000.000.000 byte e tempo ≤ 120,6 s nello stesso run.
8. **Certificazione finale.** Cinque processi indipendenti, con mediana, p95,
   deviazione standard e min/max; i gate restano separati.

Il passaggio alla fase successiva è vietato se il gate corrente fallisce. In
particolare, non si implementa CFR-D prima che il kernel canonico superi il
differenziale matematico, perché entrambi i livelli dipendono dalle stesse
trasformazioni di reach e counterfactual value.

## Conseguenze

- Il vecchio `DenseTraversal` diventa un oracle fisico test-only, non il centro
  dell'architettura production.
- Il fingerprint deve includere versione del compilatore canonico, regole di
  equivalenza, layout player-local e formato packed.
- Checkpoint vecchi non sono reinterpretati; serve migrazione esplicita o nuovo
  solve.
- Analytics e GUI interrogano mapping canonico→fisico e non duplicano lo stato.
- Il limite di 2 GB viene verificato sul processo completo tramite peak RSS.
- Preflop e configurazioni più grandi usano il livello CFR-D o vengono
  rifiutati dal preflight; non causano allocazioni ottimistiche.

## Evidenza locale

- fixture: `benchmarks/fixtures/gto_plus_tstc9d_101.json`;
- report layout-only: `out/tstc9d_canonical_layout.json`;
- report completi: `out/compact_no_average_simd3_tstc9d.json` e
  `out/ram_final_tstc9d.json`;
- limite attuale della canonicalizzazione:
  [`CANONICAL_PUBLIC_DAG.md`](CANONICAL_PUBLIC_DAG.md);
- prototipo street:
  [`PHASE_6_STREET_DECOMPOSITION_REPORT.md`](PHASE_6_STREET_DECOMPOSITION_REPORT.md);
- fallback mmap:
  [`PHASE_6_OUT_OF_CORE_REPORT.md`](PHASE_6_OUT_OF_CORE_REPORT.md).

## Fonti primarie

- [CFR — Zinkevich et al., NeurIPS 2007](https://proceedings.neurips.cc/paper/2007/file/08d98638c6fcd194a4b1e6992063e944-Paper.pdf)
- [CFR+ — Tammelin, 2014](https://arxiv.org/abs/1407.5042)
- [DCFR — Brown e Sandholm, AAAI 2019](https://ojs.aaai.org/index.php/AAAI/article/view/4007)
- [MCCFR — Lanctot et al., NeurIPS 2009](https://proceedings.neurips.cc/paper/2009/file/00411460f7c92d2124a67ea0f4cb5f85-Paper.pdf)
- [CFR-D — Burch, Johanson e Bowling, AAAI 2014](https://ojs.aaai.org/index.php/AAAI/article/view/8810)
- [Lossless Abstraction of Imperfect Information Games — Gilpin e Sandholm, JACM 2007](https://www.cs.cmu.edu/~sandholm/extensive.jacm07.pdf)
- [b-inary/postflop-solver — chance isomorphism e mapping player-local](https://github.com/b-inary/postflop-solver)
- [exinori/DCFR-SOLVER — inverse permutation e test di isomorfismo](https://github.com/exinori/DCFR-SOLVER)
