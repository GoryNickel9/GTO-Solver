# Range-aware solver memory

Aggiornato: 2026-07-29

## 1. Obiettivo

Questa regressione riproduce il confronto fornito con GTO+:

- flop `Ah Kh Qh`;
- range CO e BTN `AA-QQ, AKs-AQs, KQs, AKo-AQo, KQo`;
- una bet size 75% per entrambi su flop, turn e river;
- nessun raise e all-in automatico disabilitato;
- pot iniziale 40;
- stack effettivo 100;
- rake 0%.

Sul flop ciascun range contiene 36 combo fisiche non bloccate. Il solver resta
exact: non introduce sampling, bucketing o rinormalizzazione dei range.

## 2. Correzione

Il layout F7 precedente allocava regret e average strategy per tutte le
528/496/465 combo compatibili con il solo board. Le combo assenti dal range
sorgente avevano reach zero, ma conservavano infoset, action entry e lavoro di
traversal.

Il layout range-aware:

1. costruisce per ogni board l'unione delle combo con peso sorgente non zero;
2. applica card removal su flop, turn e river;
3. assegna offset CFR soltanto a tali combo;
4. limita i loop di fold, chance e blocker allo stesso insieme;
5. mantiene i pesi originali e la fingerprint dei range;
6. usa lo stesso conteggio fisico nella stima preventiva della GUI.

Per range asimmetrici l'unione preserva la correttezza, anche se può conservare
qualche entry non necessaria per uno dei due giocatori.

## 3. Regressione exact e isomorfismo lossless

Test dedicato: `gtosd_gto_plus_reference_tests`.

| Metrica | Prima | Range-aware fisico | Canonico lossless |
|---|---:|---:|---:|
| Nodi pubblici | 52.644 | 52.644 | 52.644 |
| Infoset | 10.019.328 | 683.136 | 125.352 |
| Action entry | 20.038.656 | 1.366.272 | 250.704 |
| Buffer CFR float64 | 320.618.496 B | 21.860.352 B | 4.011.264 B |
| Stima GUI conservativa | 1.749.048.264 B | 47.730.580 B | 47.730.580 B |
| Peak RSS Release osservato | 354.709.504 B | circa 52,4 MB | 47,2–47,7 MB |

Rispetto al layout range-aware fisico, le action entry diminuiscono di un
ulteriore 81,65%. Il mapping usa esclusivamente le permutazioni globali che
lasciano invariati flop e range di entrambi i giocatori. Board, ordine delle
chance card, history pubblica e mano privata fanno parte della chiave.

Poiché più rappresentanti fisici condividono lo stesso regret, CFR+ accumula
tutti i delta dell'update del giocatore e applica il clipping una sola volta.
Il test differenziale a due iterazioni confronta layout fisico e canonico:
profile EV, best-response EV e NashConv coincidono entro `1e-11`.

La stima GUI resta deliberatamente conservativa e conta il layout fisico:
include public tree materializzato, indici, due buffer CFR `float64` e scratch.
Il conteggio canonico dipende dalle simmetrie effettive dei range e viene
riportato dal risultato del solver.

## 4. Tempo osservato

Cinque processi Release indipendenti dopo il secondo loop, una iterazione CFR+
con certificazione exact BR/NashConv:

| Run | Secondi |
|---:|---:|
| 1 | 2,310 |
| 2 | 2,205 |
| 3 | 2,221 |
| 4 | 2,296 |
| 5 | 2,198 |

Mediana: 2,221 s. Il valore non è direttamente confrontabile con gli 0,85 s
di GTO+ finché non sono allineati algoritmo, numero di iterazioni, stopping
criterion e inclusione della best response. La memoria persistente diminuisce,
ma la costruzione della mappa canonica aggiunge costo fisso e il traversal
continua a visitare i 52.644 nodi pubblici fisici.

## 5. Gap residuo rispetto a GTO+

GTO+ riporta 2,6 MB di “Memory needed for solving”. Il nostro breakdown
range-aware è:

| Componente | Byte |
|---|---:|
| Public tree materializzato | 21.899.848 |
| Regret + average strategy float64 canonici | 4.011.264 |
| Delta regret differito float64 | 2.005.632 |
| Layout/board index fisico | almeno 2.543.764 |
| Scratch conservativo | 1.426.616 |

La dicitura GTO+ è quindi più vicina al solo storage persistente del solving
che al peak RSS dell'intero processo. Il target 2,6 MB non è ancora raggiunto:
il prossimo salto richiede un public DAG canonico con molteplicità fisiche,
così da non materializzare e attraversare tutti i runout equivalenti. Solo
dopo un confronto numerico dedicato è lecito valutare storage `float32`;
checkpoint e modalità di precisione dovranno essere versionati.

## 6. Controllo delle fasi

F0-F10 risultano completate. F10.1 integra nel solver production lo
stabilizzatore globale lossless di F4, mapping inverso implicito nelle query e
checkpoint separati tramite fingerprint `iso-infosets-v1`. F11 nodelock e le
fasi successive non sono prerequisiti di questa ottimizzazione.
