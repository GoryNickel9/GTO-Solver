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

Il percorso production corrente usa DCFR standard exact con aggiornamenti
alternati su tutte le combo e chance compatibili. Il contratto e' congelato a
`alpha=1.5`, `beta=0`, `gamma=2`, regret signed e average immediato
(`averaging_delay=0`). Non usa sampling, bucketing o astrazione lossy.
L'isomorfismo globale e il DAG canonico sono riduzioni lossless.

L'intero percorso, inclusi exact BR e certificazione, viene eseguito su CPU con
stato e workspace in RAM. Un backend GPU non fa parte delle varianti ammesse e
non è un'estensione pianificata: ottimizzazioni future devono restare CPU-only.

Per ogni giocatore una traversata calcola i valori counterfactuali e produce
delta separati. Nel backend signed, all'iterazione `t`, i regret positivi
precedenti sono moltiplicati per `(t-1)^alpha/((t-1)^alpha+1)` e quelli non
positivi per `(t-1)^beta/((t-1)^beta+1)`; con
`beta=0` il fattore negativo effettivo e' `1/2`. L'average strategy accumula
`t^gamma * pi_i^sigma(I) * sigma_i^t(I,a)` dalla prima iterazione e la strategia
esposta deriva da tale accumulo reach-weighted.

## Varianti

| Algoritmo | Regret | Averaging | Uso corrente |
|---|---|---|---|
| Vanilla CFR | somma integrale | uniforme | laboratorio |
| CFR+ | cumulativo troncato a zero | con delay | oracle/fallback exact |
| Linear CFR | peso crescente con iterazione | lineare | laboratorio |
| DCFR | discount separato positivo/negativo/strategy | parametrico | postflop production |
| External Sampling MCCFR | stima campionata | dipende dal campione | laboratorio |

DCFR espone gli esponenti `alpha=1.5`, `beta=0`, `gamma=2` come default
versionati. MCCFR registra seed e non può essere chiamato exact.

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
seleziona DCFR standard (`alpha=1.5`, `beta=0`, `gamma=2`) come regret minimizer
production iniziale del nuovo kernel canonico, con CFR+ come oracle e fallback.
Le tre fixture production AHKHQH, TH7D6S e TSTC9D applicano questo stesso
contratto. `DcfrPlus` conserva una proiezione non-negativa custom e un
identificatore distinto; non viene usato per scegliere parametri diversi in
base al benchmark.
