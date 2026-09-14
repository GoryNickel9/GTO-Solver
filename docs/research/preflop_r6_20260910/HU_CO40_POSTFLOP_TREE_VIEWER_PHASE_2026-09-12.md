# Fase viewer — albero pubblico postflop HU CO40

## Esito

`IMPLEMENTATION_PASS / STRUCTURAL_EXPORT_PASS / BROWSER_QA_PASS`.

Il viewer ora espone l'intero albero pubblico postflop usato dal solver per ciascuna delle nove entry preflop. L'export deriva dal tree builder nativo e viene confrontato con i conteggi della skeleton già validata; non contiene nodi inventati.

## Implementazione

- `gtosd_hu_preflop_tree --output <file>` esporta entry, nodi decisionali, stato monetario, azioni, transizioni chance e terminali;
- il sito permette di scegliere l'entry, avanzare lungo le azioni e tornare al nodo precedente o al root Flop;
- ogni nodo mostra street, attore, pot, to-call, stack, commitment per street e totali, raise count e profondità;
- `validate_postflop_tree_export.py` controlla fingerprint, cardinalità, archi, tipi, confini delle entry e raggiungibilità;
- `generate_chart_data.py` incorpora l'artefatto validato in `dist/postflop-data.js`.

## Validazione

| Controllo | Risultato |
|---|---:|
| Entry preflop | 9 |
| Nodi rappresentati | 27.012 |
| Nodi decisionali | 10.060 |
| Archi di azione | 25.944 |
| Frontiere chance | 1.059 |
| Fold terminali | 7.942 |
| Showdown terminali | 6.715 |
| Runout all-in | 1.236 |
| Profondità massima | 15 |
| Raise massimi osservati | 4 |
| Terminazione naturale | PROVEN |

Build Release MSVC con warning trattati come errori: PASS. CTest `gtosd_hu_preflop_tree`: `1/1 PASS`. Validatore dell'export: PASS. Il controllo browser ha verificato V8 seed 2 come sorgente preflop iniziale, il passaggio alla vista postflop e il ramo `CO Check → BTN to act`; console senza errori o warning.

## Limite dichiarato

L'export statico mostra la struttura pubblica, non frequenze o EV postflop. Questi valori dipendono da board, combo privata e bucket V8/V11; la policy binaria non è una chart per board già materializzata. Per aggiungerli serve una query nativa o WASM che riceva board e combo e applichi lo stesso mapping versionato del solver. Il viewer segnala questo limite e non sostituisce valori sintetici.

