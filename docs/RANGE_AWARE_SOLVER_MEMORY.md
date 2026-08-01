# Range-aware solver memory

Contratto canonico corrente: [`specifications/PERFORMANCE.md`](specifications/PERFORMANCE.md).

I conteggi AhKhQh sotto riportati documentano la fixture storica
`GTP-AHKHQH-001` senza raise. Non rappresentano il gate GTO+ corrente
`GTP-AHKHQH-003`.

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
| Nodi pubblici attraversati | 52.644 | 52.644 | 14.673 |
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

Mediana F10.1: 2,221 s. Questo è il tempo di **una sola iterazione CFR+ più una
certificazione exact BR/NashConv**, non il tempo necessario a raggiungere il
target dEV né a “solvare l'intero albero”. Con F10.2, cinque ulteriori processi Release hanno
prodotto una mediana di 2,192 s e 29.346 visite CFR per iterazione a due
giocatori. Il valore non era direttamente confrontabile con il precedente
riferimento GTO+ da 0,82 s (ora sostituito dal riferimento confermato da 1,71 s)
finché non sono allineati algoritmo, numero di iterazioni, stopping criterion e
inclusione della best response. La costruzione della mappa canonica e del DAG
aggiunge ancora un costo fisso, ma il traversal non visita più tutti i 52.644
nodi fisici.

## 5. Gap residuo rispetto a GTO+

Il riferimento aggiornato GTO+ riporta 8 MB di “Memory needed for solving”. Il nostro breakdown
range-aware è:

| Componente | Byte |
|---|---:|
| Public tree materializzato | 21.899.848 |
| Regret + average strategy float64 canonici | 4.011.264 |
| Regret + average strategy float32 performance | 2.005.632 |
| Delta regret differito float64 | 2.005.632 |
| Layout/board index fisico | almeno 2.543.764 |
| Scratch conservativo | 1.426.616 |

La dicitura GTO+ è quindi più vicina al solo stato persistente del solving che
al peak RSS dell'intero processo. Il percorso accuratezza resta `float64`
(`4,01 MB`); il benchmark GTO+ seleziona esplicitamente stato `float32` con
calcolo `float64` (`2,005632 MB`) e supera il gate memoria. Delta regret,
indici, tree e peak RSS restano pubblicati separatamente e non vengono nascosti
nel confronto. Il checkpoint `float32` è consultabile e ricertificabile; la sua
persistenza versionata resta lavoro successivo e non sostituisce il formato
`float64` predefinito.

## 6. Controllo delle fasi

F0-F10 risultano completate. F10.1 integra nel solver production lo
stabilizzatore globale lossless di F4, mapping inverso implicito nelle query e
checkpoint separati tramite fingerprint `iso-infosets-v1`. F10.2 aggiunge il
public DAG canonico e conserva carta, molteplicità e mapping inverso per ogni
outcome chance. F11 e le fasi successive sono ora congelate dal gate descritto
in [`GTO_PLUS_PARITY_JOURNEY.md`](GTO_PLUS_PARITY_JOURNEY.md).
