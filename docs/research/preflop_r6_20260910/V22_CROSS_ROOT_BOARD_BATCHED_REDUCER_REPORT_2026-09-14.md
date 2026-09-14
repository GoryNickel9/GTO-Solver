# V22 — Report del reducer cross-root board-batched

Data: 2026-09-14  
Stato: `IMPLEMENTED / EXACT PHYSICAL GATE FAIL`

## Risultato

Il percorso River può ora valutare tutte le betting history di uno stesso board preparando una
sola volta combo vive, showdown e chiavi private della policy. L'implementazione conserva una
coppia distinta di risultati profile/BR per ogni root: non somma best response appartenenti a
information set diversi e non dichiara una NashConv globale.

Sul checkpoint V17, un board del primo entry contiene 633 River shape. Il batch richiede
`198,725931 s`, pari a `0,313943019 s` per sottogioco pubblico. Estrapolato sui 322.199.856
sottogiochi River canonici, il solo River richiederebbe `3,2053 anni` seriali oppure `146,34
giorni` nell'ipotesi ideale di scaling lineare su otto worker.

Il gate di sette giorni fallisce con stato `INFEASIBLE_EXACT_BOARD_BATCHED`.

## Implementazione

- `PreparedSampledRiverBoard` conserva le 465 combo vive, le mask, i valori exact di showdown e
  le chiavi private per entrambi i player;
- il batch accetta root con stesso entry e board, ma history differenti;
- reach, policy pubblica e traversata restano specifiche di ogni history;
- l'enumeratore strided materializza tutte le shape dopo una sola validazione di catalogo e piano;
- il risultato mantiene l'ordine delle shape e può alimentare un futuro reducer upper-street.

Il test differenziale confronta due history dello stesso board contro valutazioni indipendenti di
profilo e BR con tolleranza `1e-10`.

## Profilo del costo

La prima esecuzione ha rivelato un overhead non matematico: materializzare 633 root con la vecchia
API ripeteva la validazione completa del catalogo e consumava circa `493 s`. L'enumeratore batch
riduce questa fase a `0,814670 s`, circa `605x` più veloce.

| Fase | Prima | Dopo |
| --- | ---: | ---: |
| Materializzazione di 633 root | circa 493 s | 0,814670 s |
| Kernel profile + BR board-batched | 190,204101 s | 198,725931 s |
| Tempo totale del probe | 703,482998 s | 222,245670 s |

La variazione del kernel fra le due esecuzioni è rumore operativo e differenza di stato delle
cache. Il collo di bottiglia residuo è la traversata per history: propagazione del reach, lookup
della policy e operazioni sulle matrici combo. La preparazione del board non era il costo dominante.

## Proiezione e limite

La proiezione usa tutte le 633 shape del primo entry sul primo board canonico, non il precedente
root shallow isolato. Rimane una misura di fattibilità, non un certificato. Non include terminali
Flop/Turn, riduzione upper-street o ricomposizione preflop; il fatto che il solo River superi già
di oltre venti volte il budget rende comunque conclusivo il gate negativo.

## Decisione

Non si implementa l'executor fisico completo sopra questo kernel: produrrebbe mesi di calcolo anche
con otto worker. L'esatto può tornare candidato soltanto dopo un riuso delle traversate comuni fra
history, per esempio tramite un trie dei prefissi pubblici con propagazione condivisa del reach.
Nel frattempo i solve completi continuano a riportare la stima separata
`ESTIMATED_LOWER_BOUND_ONLY` con `certified: false`.

## Artefatto

Il manifest [v22_v17_seed1_nashconv_execution_plan_v1.json](v22_v17_seed1_nashconv_execution_plan_v1.json)
usa lo schema `gtosd.hu_preflop_nashconv_probe.v3` e contiene tempi, proiezioni, fingerprint e gate.
