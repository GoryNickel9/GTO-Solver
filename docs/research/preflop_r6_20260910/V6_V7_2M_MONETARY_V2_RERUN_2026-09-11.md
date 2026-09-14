# R6 — rerun V6/V7 2M sul contratto monetario v2

## Esito

`ENGINEERING_PASS / SCIENTIFIC_GATE_FAIL`.

I quattro solve richiesti sono stati rigenerati: V6 e V7, 2.000.000 di iterazioni, due seed per profilo. Tutti usano l'albero corrente `fnv1a64:a68337fa567aa2d9`, esportano i 20 nodi preflop e coprono 630/630 combo fisiche. Nessun run supera però il gate strategico da 1 pp; R7 resta bloccato.

## Contratto e protocollo

- fixture gioco: `hu_preflop_co40_game_v1.json`, SHA-256 `5BA2FC78567CFD109B34BFDC9A208D13BCD08B99CDBB986FE155E407879E0619`;
- fixture riferimento: `hu_preflop_co40_reference_v1.json`, SHA-256 `D835898479493B24FAC3CE2686B43119CBBED40B5CEF40A01E25BC62B2DBE811`;
- fingerprint riferimento: `fnv1a64:f78898249087ee6d`;
- ante: `1a` morta per giocatore; button blind BTN: `1a` live; call root CO: `1a`;
- target live preflop: `6a`, `10a`, `10,5a`, `14,5a`, all-in CO `39a`.

Parametri comuni: Linear MCCFR, batch 32, otto worker, MC8, bucket capacity `32/128/512`, 20.000 deal di valutazione, 10.000 iterazioni BR, 10.000 deal BR e export completo delle chart. I due seed condividono soltanto la partizione; training ed evaluation seed sono indipendenti.

## Risultati

| Profilo | Seed | Solve | Infoset | Payload numerico | EV root CO | WMAE | TV media | P95 TV | Errore root max |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| V6 | 1 | 366,935 s | 3.224.546 | 497.284.704 B | −0,2843 ± 0,1086a | 18,8595 pp | 47,1488 pp | 99,2977 pp | 31,5764 pp |
| V6 | 2 | 389,367 s | 3.227.184 | 497.362.608 B | +0,0175 ± 0,1079a | 18,5052 pp | 46,2630 pp | 97,4461 pp | 31,1921 pp |
| V7 | 1 | 341,635 s | 878.669 | 145.645.488 B | −0,1921 ± 0,1084a | 18,3298 pp | 45,8246 pp | 99,2133 pp | 26,4713 pp |
| V7 | 2 | 341,172 s | 878.331 | 145.574.208 B | −0,3098 ± 0,1078a | **18,1872 pp** | **45,4680 pp** | **94,8627 pp** | 27,8464 pp |

V7 usa in media il 29,3% del payload numerico V6 ed è circa il 9,7% più veloce. La WMAE media scende da 18,6824 a 18,2585 pp: un miglioramento di 0,4238 pp, insufficiente rispetto al gate da 1 pp.

### Frequenze root aggregate

| Sorgente | All-in | Raise 6a | Raise 10a | Call | Fold |
|---|---:|---:|---:|---:|---:|
| Monker | 40,7789% | 2,4079% | 4,7196% | 7,5668% | 44,5269% |
| V6 seed 1 | 15,4942% | 10,0336% | 5,8648% | 39,1432% | 29,4642% |
| V6 seed 2 | 15,9895% | 8,5713% | 7,3999% | 38,7589% | 29,2805% |
| V7 seed 1 | 14,7245% | 7,4288% | 12,0689% | 34,0381% | 31,7397% |
| V7 seed 2 | 12,9325% | 9,1314% | 12,8925% | 30,9904% | 34,0531% |

La differenza non è un artefatto della sola visualizzazione. Per esempio, V7 seed 2 usa all-in 27,8464 pp meno di Monker e call 23,4237 pp in più.

### Stabilità fra seed

| Profilo | Δ EV root | TV media per combo root | Massimo Δ marginale root |
|---|---:|---:|---:|
| V6 | 0,3018a | 20,1723 pp | 1,5351 pp |
| V7 | 0,1178a | 27,9000 pp | 3,0476 pp |

Le frequenze aggregate possono sembrare vicine fra seed mentre le singole combo cambiano azione. V7 ha una dispersione EV minore, ma una TV per combo peggiore. A 2M non è quindi stabile al livello richiesto.

### Audit della risposta appresa

| Profilo | Seed | EV risposta CO | EV risposta BTN | Somma grezza | Campo esportato |
|---|---:|---:|---:|---:|---:|
| V6 | 1 | −0,5494a | −0,7353a | −1,2847a | 0 |
| V6 | 2 | −0,4097a | −0,8619a | −1,2717a | 0 |
| V7 | 1 | −0,6616a | −0,7434a | −1,4050a | 0 |
| V7 | 2 | −0,6800a | −0,6413a | −1,3213a | 0 |

Il solver addestra due policy di risposta campionate, somma i loro EV e applica `max(0, somma)`. Il risultato zero deriva quindi dal clamp di una somma negativa. Non è una best response esatta, non è un upper bound dell'exploitability e non dimostra convergenza.

## EV e dimensione del campione

`evaluation-deals=20000` produce 20.000 deal complessivi, non 20.000 o 100.000 campioni per mano. Alla root ogni azione di una classe riceve fra 100 e 438 deal grezzi, secondo la massa fisica della classe. Questo spiega SE per azione spesso nell'ordine di `1–2a` e valori locali controintuitivi.

Aumentare i campioni di evaluation renderebbe gli EV visualizzati più precisi, ma non cambierebbe le frequenze già apprese durante i 2M di training. Non può quindi correggere da solo la WMAE o la TV strategica.

## Viewer

Il generatore usa ora soltanto i quattro file `*_monetary_v2_full_tree.json`. Il payload contiene cinque sorgenti: Monker più V6/V7 su due seed. La verifica nel browser conferma:

- badge `RESEARCH / NOT QUALIFIED`, non `STALE`;
- V7 seed 2 selezionata all'apertura;
- 20 action path navigabili;
- EV della strategia e EV/SE per ogni azione e combo;
- confronto diagnostico con il nodo Monker corrispondente.

## Validazione

| Controllo | Esito |
|---|---|
| Solve 2M | `4/4 PASS` |
| Fingerprint albero | `4/4 PASS`, `fnv1a64:a68337fa567aa2d9` |
| Export chart | `4/4 PASS`, 20 nodi e 1.620 righe per file |
| Copertura EV | `4/4 PASS`, 630/630 combo fisiche |
| History uguali all'export Monker | `4/4 PASS` |
| Validatore R0 | `PASS` |
| Reference preflight | `PASS` |
| Gate contro un vecchio export | `STALE_TREE`, rifiuto atteso |
| Generazione viewer | `PASS`, 5 sorgenti e 20 nodi Monker |
| Verifica browser | `PASS`, badge e dati correnti |
| Gate strategico | `FAIL`, minimo 18,1872 pp contro soglia 1 pp |
| NashConv | `NOT CERTIFIED`; il valore numerico zero non è una certificazione |
| Comparabilità completa | `REFERENCE_CONFIG_INCOMPLETE`, postflop Monker sconosciuto |

Il runtime locale non dispone di un validatore JSON Schema Draft 2020-12. I vincoli monetari sono coperti dal validatore R0, dalla preflight C++ e dai test del ledger; non viene dichiarata una validazione Draft 2020-12 completa.

## SHA-256

- solver: `FE74CDA8D60C8E4FBB16ACA5D7DA1604D7D71415F6AB86F7FA745F05E4972B44`;
- comparator: `ED7526D16A877EDFCCCD0B0F7D9BA9311B6B836E84A0684F00F2B57B177AEED4`;
- V6 seed 1: `EB4F770FDD7336BA73F2917F96CB758235BBAA363EF7B8D580C3A25B3CE8D73D`;
- V6 seed 2: `F0827E2CBBF3AD3E6BA6CEB16F903907BA8DC18EC30CBA6E61B6730FB5EAA38E`;
- V7 seed 1: `D189C388183B1EF420200F1B2F3E3AD12737BE3A52AFD22934F6A49726F6FEB2`;
- V7 seed 2: `6F8304E9EF34187395CD7EB3AA789422C941AF386A3F21C834ABC19C1A4BF8BC`;
- payload viewer: `8070E81B7E6AB6D3908BEA92C24366F302E29E118EFE1B5FA6E4E03F35300B1B`.

## Decisione

V7 resta il profilo più efficiente e il migliore dei quattro sul confronto medio, ma non è qualificato. Non ha senso promuoverlo o passare a R7. Il prossimo controllo dentro R6 deve analizzare sampling e budget rispetto alla forte variabilità per combo. La risposta appresa non è una best response certificata: la sua somma grezza è negativa in tutti i run e il successivo clamp a zero non misura NashConv. Aumentare soltanto l'evaluation per combo migliora le barre d'errore, non la strategia.
