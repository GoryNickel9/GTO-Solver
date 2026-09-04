# Algoritmi del solver

## Framework di riferimento

`gtosd_solver` supporta su giochi finiti validabili:

- Vanilla CFR;
- CFR+;
- Linear CFR;
- Discounted CFR (DCFR);
- External Sampling MCCFR.

Questo laboratorio valida formule, checkpoint, averaging e best response su
Kuhn, Leduc e giochi ridotti. Non implica che ogni algoritmo sia disponibile
nel workflow postflop di prodotto.

## Percorso HU postflop production

Il percorso production corrente usa `ProductionDcfr`, una schedule DCFR signed
exact-outcome con aggiornamenti alternati su tutte le combo e chance
compatibili. Il contratto e' `alpha=1.5`, `beta=0`, `gamma=3`, regret signed e
average immediato (`averaging_delay=0`). L'average viene azzerato alle
iterazioni one-based `1,2,5,17,65`; dopo 65 l'ultima epoca viene mantenuta. Non
usa sampling, bucketing o astrazione del gioco. L'isomorfismo globale e il DAG
canonico sono riduzioni lossless; lo stato cumulativo production usa il codec
node-scaled uint16 dichiarato nella specifica di precisione.

L'intero percorso, inclusi exact BR e certificazione, viene eseguito su CPU con
stato e workspace in RAM. Un backend GPU non fa parte delle varianti ammesse e
non è un'estensione pianificata: ottimizzazioni future devono restare CPU-only.

Per ogni giocatore una traversata calcola i valori counterfactuali e produce
delta separati. Nel production backend, all'iterazione `t`, il clock positivo
e' `r=t-1` fino a 65 e `r=t-2` dopo 65; i regret positivi precedenti sono
moltiplicati per `r^alpha/(r^alpha+1)` e quelli non positivi per `1/2`.
All'interno di ogni epoca l'average strategy accumula con peso cubico
`(k+1)^3`, dove `k` parte da zero al reset. Questa forma additiva e' equivalente
fino a scala comune al discount ricorsivo gamma 3. La strategia esposta deriva
dall'accumulatore reach-weighted.

## Varianti

| Algoritmo | Regret | Averaging | Uso corrente |
|---|---|---|---|
| Vanilla CFR | somma integrale | uniforme | laboratorio |
| CFR+ | cumulativo troncato a zero | con delay | oracle/fallback exact |
| Linear CFR | peso crescente con iterazione | lineare | laboratorio |
| DCFR | discount separato positivo/negativo/strategy | parametrico `1.5/0/2` di default | laboratorio/comparator |
| Production DCFR | DCFR signed, reset bounded e pesi cubici | contratto fisso `1.5/0/3` | postflop production |
| External Sampling MCCFR | stima campionata | dipende dal campione | laboratorio |

DCFR continua a esporre `alpha=1.5`, `beta=0`, `gamma=2` come variante
parametrica versionata. `ProductionDcfr` ha identita' checkpoint distinta
(`11`) e non accetta una gamma alternativa. MCCFR registra seed e non può
essere chiamato exact.

## Parallelismo

Il postflop può parallelizzare action subtree fino a una profondità configurata.
I worker producono buffer separati e una riduzione deterministica. Thread count,
profondità e impatto sulla convergenza fanno parte del benchmark. Nessun lock
globale deve entrare nell'hot path.

Il parallelismo è esclusivamente tra thread CPU. Non sono ammessi offload GPU,
kernel compute o riduzioni numeriche eseguite da acceleratori esterni.

## Certificazione

La best response è separata dall'update CFR. La certificazione registra profile
value, best-response value, NashConv, NashConv/pot e payoff sum. Il criterio di
arresto viene valutato solo a intervalli di certificazione dichiarati; una
certificazione finale è obbligatoria.

La frequenza della certificazione cambia il tempo osservato e deve essere
pubblicata. Non è lecito confrontare il solo traversal GTOSD con il tempo
end-to-end GTO+.

## Checkpoint e resume

Il checkpoint conserva iterazioni, delay, precisione e accumulatori. Il resume
continua la stessa traiettoria solo con fingerprint e configurazione compatibili.
Cambiare algoritmo, action layout o significato delle utility richiede un nuovo
solve o una migrazione esplicita.

## Node locking

Il node locking globale di prodotto non è ancora implementato. F10.4 prevede un
root lock esterno combo-per-combo limitato a test diagnostico: il root non viene
aggiornato, le continuation restano libere e la certificazione riguarda il gioco
vincolato. Questo percorso non deve essere presentato come equilibrio del gioco
originale.

## Algoritmi futuri

Outcome Sampling, public chance sampling, safe subgame solving, continual
resolving e depth-limited solving sono candidati, non capacità correnti.
Preflop richiederà una decisione separata tra gioco non astratto, decomposizione
e abstraction misurata. Multiway richiederà una nozione di soluzione e metriche
separate; NashConv HU zero-sum non viene trasferita per assunzione.

## Target memory-bounded approvato

L'ADR
[`ADR_0002_MEMORY_BOUNDED_EXACT_SOLVER.md`](../ADR_0002_MEMORY_BOUNDED_EXACT_SOLVER.md)
selezionava DCFR standard (`alpha=1.5`, `beta=0`, `gamma=2`) come regret
minimizer production iniziale del nuovo kernel canonico, con CFR+ come oracle e
fallback. La decisione successiva del 2026-09-01 promuove la schedule comune
`ProductionDcfr` descritta sopra; l'ADR resta la motivazione del backend
memory-bounded, non l'autorita' della schedule corrente. Le tre fixture
AHKHQH, TH7D6S e TSTC9D applicano lo stesso contratto e non esiste selezione per
benchmark. `DcfrPlus` conserva una proiezione non-negativa custom e un
identificatore distinto.
