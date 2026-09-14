# R6 — Gate V18: refinement preflop con postflop congelato

Data: 2026-09-13  
Esito: `ENGINEERING_PASS / SCIENTIFIC_REJECTED / TEN_PERCENT_GATE_FAIL`

## Verdetto

V18 produce un miglioramento esterno misurabile ma non raggiunge il gate preregistrato del `10%`.
La TV fra seed e l'audit EV peggiorano. V18 viene quindi respinta come candidata principale; file,
policy e audit restano archiviati come evidenza riproducibile.

Non è stata avviata V19.

## Gate di coppia

La baseline è V15/V17, bit-identica per la strategia appresa. Un valore positivo nella colonna
“miglioramento” indica una riduzione dell'errore.

| Metrica | Baseline | Soglia 10% | V18 | Miglioramento | Esito |
| --- | ---: | ---: | ---: | ---: | --- |
| TV fra seed, strategia media | 10,9453 pp | ≤9,8508 pp | 13,0159 pp | −18,92% | FAIL |
| TV media contro Monker | 34,8679 pp | ≤31,3811 pp | 33,4596 pp | +4,04% | FAIL |
| WMAE media contro Monker | 13,9472 pp | ≤12,5525 pp | 13,3838 pp | +4,04% | FAIL |
| Audit esatto Call/Fold, reach pubblica ≥1% | 7 casi | ≤6 casi | 8 casi | −14,29% | FAIL |

Il `4,27%` osservato sul seed 1 è corretto. Il seed 2 migliora del `3,80%`; la media accoppiata è
`4,04%`.

| Seed | WMAE baseline | WMAE V18 | TV esterna V18 | Miglioramento |
| --- | ---: | ---: | ---: | ---: |
| 1 | 14,1312 pp | 13,5277 pp | 33,8192 pp | 4,27% |
| 2 | 13,7631 pp | 13,2400 pp | 33,1000 pp | 3,80% |

## Audit EV

L'audit generale della policy corrente riduce i casi separati dopo correzione Bonferroni da
`11` a `7`, cioè del `36,36%`. Questa metrica non era il gate preregistrato e non viene usata per
dichiarare un PASS post-hoc.

La perdita EV pesata sul deal root passa invece da `0,81330a` a `0,83194a`, un peggioramento del
`2,29%`. I due seed si muovono in direzioni opposte: seed 1 migliora del `6,57%`, seed 2 peggiora.
L'audit esatto degli all-in trova `8` casi medi materialmente inferiori sui percorsi con reach
pubblica almeno `1%`, contro i `7` della baseline.

## Perché il 4,04% è utile ma non basta

V18 riduce soprattutto gli scarti aggregati di Call e Fold. Rispetto alla baseline media, il delta
Call verso Monker migliora di circa `3,68 pp` e Fold di circa `2,30 pp`. In compenso il deficit
All-in peggiora di circa `0,75 pp` e l'eccesso di Raise 6a cresce di circa `1,96 pp`. Il refinement
sta quindi correggendo una parte della distribuzione, non l'errore strutturale completo.

La decomposizione fra seed spiega perché le percentuali restano instabili:

- TV media V18: `13,0159 pp`; policy corrente: `22,7363 pp`;
- `12,2863 pp` dei `13,0159 pp` medi, cioè il `94,39%`, spostano massa fra azioni il cui gap EV
  medio è al massimo `0,1a`;
- nessuna massa TV media cade su gap EV medio almeno `0,5a`;
- Offsuit contribuisce `6,7740 pp`, suited `4,5636 pp`, coppie `1,6783 pp`.

I seed scelgono azioni diverse quando il continuation value campionato non separa chiaramente le
alternative. La strategia corrente amplifica il fenomeno; la media CFR lo riduce ma non lo elimina.

## Diagnostica della sola seconda fase

Il tool `tools/analyze_hu_preflop_refinement_phase.py` sottrae dai pesi cumulativi V18 quelli V17
e normalizza la massa accumulata soltanto nelle iterazioni `2.000.001–4.000.000`. Il controllo
ricostruisce 81 classi, 630 combo fisiche e `6.000.001.000.000` unità di peso root per seed, senza
pesi negativi.

Questa diagnostica usa il TSV Monker completo, leggermente più preciso della fixture arrotondata;
per questo i valori differiscono di circa `0,006 pp` dal comparatore ufficiale.

| Profilo | WMAE media | TV fra seed |
| --- | ---: | ---: |
| V15/V17 | 13,9529 pp | 10,9453 pp |
| V18 finale | 13,3895 pp | 13,0159 pp |
| Solo refinement V18 | 13,3997 pp | 16,6728 pp |

La sola seconda fase non è migliore della miscela finale e ha una TV fra seed ancora più alta.
Prolungare lo stesso refinement K=1 non offre quindi evidenza di poter raggiungere il `10%`;
probabilmente consolida il plateau vicino a `13,4 pp` aumentando il costo.

Il profilo ricostruito è soltanto diagnostico: non possiede un proprio file policy postflop, una
valutazione EV separata o un gate preregistrato.

## Correzione proposta per raggiungere il 10%

La prossima candidata, solo dopo consenso esplicito, deve ripartire dal trainer principale V17:

1. rimuovere il refinement postflop congelato dalla candidata;
2. accoppiare con Common Random Numbers le azioni di ogni nodo preflop durante il training
   principale, mantenendo il postflop adattivo;
3. verificare su giochi ridotti che ogni marginale resti corretto e che la varianza delle
   differenze d'azione diminuisca senza bias;
4. eseguire due run complete da 2M soltanto dopo i test matematici. Nessun risultato ridotto decide
   il gate.

Questa proposta attacca il `94,39%` della TV associato a gap piccoli. Non garantisce una WMAE più
vicina a Monker: la comparabilità esterna resta incompleta e nessuna frequenza verrà forzata verso
il benchmark. La TV media fra seed deve migliorare almeno del `10%`; WMAE, TV corrente e audit EV
non possono peggiorare.

## Decisione

V18 è respinta e non sostituisce il gate R6. Aumentare le sue iterazioni è escluso. V17 torna a
essere la baseline perché conserva la migliore TV fra seed della linea corrente e include la policy
corrente completa. Il nuovo gate e il piano non autorizzato sono in
[V18_REJECTION_AND_NEXT_GATE_DECISION_2026-09-13.md](V18_REJECTION_AND_NEXT_GATE_DECISION_2026-09-13.md).

Il viewer deve mostrare V18 come evidenza respinta e selezionare V17 dopo un aggiornamento separato.
Non è stata avviata V19.
