# R6 — Viewer completo delle chart HU CO40

## Esito

`ENGINEERING_PASS / SCIENTIFIC_GATE_FAIL`.

Il sito statico in `tools/hu_preflop_chart_viewer` naviga tutti i 20 nodi decisionali preflop. Il selettore contiene il riferimento Monker e, per il nostro solver, soltanto V6 e V7 a 2.000.000 di iterazioni su due seed. I quattro dataset sono stati rigenerati sul contratto monetario v2. Ogni nodo mostra le 81 classi, le frequenze, l'EV della strategia, l'EV di ogni azione e l'errore standard.

## Implementazione

L'opzione CLI `--full-preflop-chart-export` abilita una diagnostica separata dal training. La `HuPreflopBlueprint` fornisce la strategia media di ogni nodo. Per l'EV non-root, il valutatore:

1. condiziona sulla classe privata dell'attore e sulla history pubblica;
2. pesa ogni deal con la probabilità delle azioni precedenti dell'avversario;
3. forza una singola azione nel nodo;
4. segue la policy media nelle continuazioni;
5. riporta media pesata, campioni grezzi, effective sample size e SE.

Le azioni precedenti dell'attore non entrano nel peso perché si cancellano quando si condiziona sulla sua classe e sul percorso pubblico. La formula e i gate sono congelati in `PREFLOP_FULL_TREE_CHART_EXPORT_PROTOCOL_2026-09-11.md`.

Il generatore del viewer rifiuta export con meno di 20 nodi, meno di 81 classi per nodo, history diverse dall'albero Monker o copertura EV inferiore a 630 combo fisiche. Nessun dato mancante viene inventato.

## Run V6/V7 a 2M

Tutti i run usano Linear MCCFR, batch 32, otto worker, MC8, capacità `32/128/512`, 20.000 deal di valutazione, 10.000 iterazioni BR e 10.000 deal BR.

| Profilo | Seed | Solve | Infoset | Payload numerico | EV root CO | WMAE root | TV media | P95 TV | Errore root max |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| V6 | 1 | 366,935 s | 3.224.546 | 497.284.704 B | −0,2843 ± 0,1086a | 18,8595 pp | 47,1488 pp | 99,2977 pp | 31,5764 pp |
| V6 | 2 | 389,367 s | 3.227.184 | 497.362.608 B | +0,0175 ± 0,1079a | 18,5052 pp | 46,2630 pp | 97,4461 pp | 31,1921 pp |
| V7 | 1 | 341,635 s | 878.669 | 145.645.488 B | −0,1921 ± 0,1084a | 18,3298 pp | 45,8246 pp | 99,2133 pp | **26,4713 pp** |
| V7 | 2 | 341,172 s | 878.331 | 145.574.208 B | −0,3098 ± 0,1078a | **18,1872 pp** | **45,4680 pp** | **94,8627 pp** | 27,8464 pp |

V7 usa circa il 29% del payload numerico V6 e ottiene WMAE migliore su entrambi i seed. La TV root media fra i due seed V7 è però 27,9000 pp. Il profilo rimane molto lontano dal gate finale di 1 pp e non sblocca R7. Il report numerico completo è [V6_V7_2M_MONETARY_V2_RERUN_2026-09-11.md](V6_V7_2M_MONETARY_V2_RERUN_2026-09-11.md).

## Viewer

Funzioni disponibili:

- selezione Monker, V6/2M e V7/2M;
- action path completo, breadcrumb e salto diretto a ogni nodo;
- matrice Short Deck 9×9 con celle divise per frequenza;
- EV per combo e dettaglio di frequenza, EV e SE per azione;
- confronto con il nodo Monker corrispondente tramite ΔEV per combo e WMAE locale;
- ricerca della classe e layout responsive.

L'EV della combo è `Σ_a σ(a|combo) · EV(a|combo) / Σ_a σ(a|combo)`. Le barre possono essere scalate per riempire la cella, ma i valori numerici restano quelli esportati.

## Validazione

- build Release MSVC `/W4 /WX`: PASS;
- regressione preflop: 13/13 PASS in 509,45 s;
- smoke solve con export completo: PASS;
- quattro run 2M: PASS;
- per ogni run: 20/20 nodi, 1.620/1.620 righe, EV 630/630 combo fisiche per nodo;
- insieme delle 20 history identico all'export Monker: PASS;
- generazione payload: 5 sorgenti, 20 nodi per sorgente, PASS.

SHA-256:

- eseguibile: `FE74CDA8D60C8E4FBB16ACA5D7DA1604D7D71415F6AB86F7FA745F05E4972B44`;
- V6 seed 1: `EB4F770FDD7336BA73F2917F96CB758235BBAA363EF7B8D580C3A25B3CE8D73D`;
- V6 seed 2: `F0827E2CBBF3AD3E6BA6CEB16F903907BA8DC18EC30CBA6E61B6730FB5EAA38E`;
- V7 seed 1: `D189C388183B1EF420200F1B2F3E3AD12737BE3A52AFD22934F6A49726F6FEB2`;
- V7 seed 2: `6F8304E9EF34187395CD7EB3AA789422C941AF386A3F21C834ABC19C1A4BF8BC`;
- payload viewer: `8070E81B7E6AB6D3908BEA92C24366F302E29E118EFE1B5FA6E4E03F35300B1B`.

## Limiti

La copertura dell'albero preflop coincide con Monker, ma la comparabilità del gioco completo resta incompleta. Non conosciamo albero postflop, bucket, abstraction dipendenti dalla history, build o stopping rule Monker. Gli EV del nostro solver sono stime Monte Carlo condizionate; quelli Monker provengono direttamente dai file forniti.

I quattro comparatori restano `REJECTED` con `REFERENCE_CONFIG_INCOMPLETE`. Il viewer è uno strumento diagnostico e non trasforma V6 o V7 in soluzioni qualificate.
