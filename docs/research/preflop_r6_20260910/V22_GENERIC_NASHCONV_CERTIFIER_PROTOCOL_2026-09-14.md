# V22 — Protocollo del certificatore NashConv generico

Data: 2026-09-14  
Stato: `PREREGISTERED_BEFORE_IMPLEMENTATION / BOARD-BATCH GATE RECORDED`

Il census, il gate di fattibilità e le ottimizzazioni root-locali sono descritti nel
[report V22](V22_GENERIC_NASHCONV_CERTIFIER_PHASE1_REPORT_2026-09-14.md). I gate matematici
definiti in questo documento restano invariati. Il successivo
[benchmark board-batched](V22_CROSS_ROOT_BOARD_BATCHED_REDUCER_REPORT_2026-09-14.md) chiude il
gate exact fisico sul kernel corrente.

## Obiettivo

Costruire un executor riutilizzabile che valuti checkpoint HU preflop prodotti da Linear MCCFR
senza introdurre codice specifico per stack, range o sizing. Cambiare da 40a a 50a deve richiedere
una nuova esecuzione, non una nuova implementazione.

## Scope matematico

Il certificato primario usa una best response fisica lifted:

- la strategia media V8 resta congelata;
- il responder osserva le proprie combo fisiche e tutte le carte pubbliche consentite dal gioco;
- la strategia avversaria usa i bucket e la fallback dichiarati dal checkpoint;
- chance, card removal, reach e utility sono quelli della configurazione serializzata;
- la massimizzazione avviene dopo avere aggregato tutte le history appartenenti allo stesso
  information set fisico.

Una futura NashConv interna V8, con responder vincolato agli stessi bucket, dovrà usare un campo e
un certificato separati. Non può sostituire la metrica lifted.

## Fasi

1. censire chiavi di policy, shape e stato per misurare il riuso disponibile;
2. scegliere automaticamente fra esecuzione esatta, esecuzione esatta con cache persistente e
   sola stima campionata non certificante;
3. eseguire profile EV e le due best response con checkpoint atomici;
4. ricomporre i contributi postflop, i terminali preflop e la root preflop;
5. emettere NashConv soltanto dopo copertura e fingerprint completi.

## Artefatti

Ogni esecuzione deve produrre un manifest JSON con:

- fingerprint di configurazione, tree, blueprint e policy;
- algoritmo, seed, iterazioni e vista della strategia;
- scope della best response;
- cardinalità raw e cardinalità dopo il riuso;
- task completati, checkpoint e contribution-chain fingerprint;
- profile EV, BR EV, deviation gain e NashConv;
- tempo, memoria e proiezione aggiornata del lavoro residuo;
- stato terminale esplicito; la fase 1 usa `STRICT_COVERAGE_INCOMPLETE`,
  `ESTIMATED_LOWER_BOUND_ONLY`, `INFEASIBLE_EXACT_ROOT_BY_ROOT` e
  `INFEASIBLE_EXACT_BOARD_BATCHED`, mentre `CERTIFIED` resta riservato al superamento di tutti i
  gate.

## Gate

Il certificato può riportare `CERTIFIED` soltanto se:

- la strategia media è completa tramite righe addestrate o fallback esplicitamente dichiarata;
- ogni boundary raggiungibile è inclusa una sola volta;
- entrambi i responder usano lo stesso checkpoint avversario;
- il reducer massimizza prima di fondere osservazioni pubbliche strategicamente distinte;
- profile EV e BR sono finiti e ogni BR non è inferiore al profilo oltre la tolleranza;
- NashConv è la somma dei due deviation gain non clampati, salvo solo rumore numerico entro
  `1e-12`;
- il percorso batch coincide con l'oracolo monolitico e con quello scalare sui giochi ridotti;
- interruzione e resume producono lo stesso fingerprint del run continuo.

Una best response MCCFR o una valutazione Monte Carlo può produrre una stima con intervallo di
confidenza, ma non può impostare `CERTIFIED` perché non fornisce un limite superiore rigoroso
sull'errore di ottimizzazione.

## Stop rule di fattibilità

Prima del run completo il census misura la cardinalità effettiva del lavoro. Se la proiezione
esatta supera sette giorni sul profilo desktop congelato, l'executor emette
`INFEASIBLE_EXACT_ROOT_BY_ROOT` oppure `INFEASIBLE_EXACT_BOARD_BATCHED` e non avvia il run. In quel
caso il report deve separare la stima campionata dalla certificazione mancante.
