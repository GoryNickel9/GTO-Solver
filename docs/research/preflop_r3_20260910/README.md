# R3 — Payoff exact e tabella a sette carte

## Stato

`PASS` per il gate R3. La tabella exact è verificata sull'intero dominio dichiarato e resta un backend opzionale. Il default rimane l'oracle: il vantaggio nel solo `solve_seconds` è misurabile, ma nel wall time completo è inferiore al rumore osservato e costa 31,84 MiB aggiuntivi.

## Implementazione

`SevenCardLookupTable` usa un indice combinatorio denso su `C(36,7) = 8.347.680` combinazioni. Ogni valore occupa 32 bit: categoria e cinque kicker vengono impacchettati preservando l'ordinamento di `HandValue`.

La risorsa contiene:

- magic e versione `1.0`;
- numero esatto delle entry;
- fingerprint `short_deck_36_flush_over_full_house_a6789_exact_v1`;
- checksum FNV-1a a 64 bit del payload;
- 33.390.720 byte di rank codificati.

Scrittura e sostituzione sono atomiche sul filesystem supportato. Il loader rifiuta magic, versione, fingerprint, dimensione, trailing bytes o checksum incoerenti. File mancante e payload corrotto producono errori espliciti.

Il trainer accetta `--seven-card-table <path>`. Senza il flag continua a usare `ExactHandEvaluator`. La cache del vincitore è per deal: riusa soltanto il confronto delle mani; settlement, contributi e rake vengono calcolati per ogni terminale e non sono condivisi.

## Validazione esaustiva

Il test Release ha costruito la tabella, l'ha salvata e riletta, quindi ha confrontato ogni entry con l'oracle:

| Misura | Risultato |
|---|---:|
| Combinazioni confrontate | 8.347.680 |
| Mismatch | 0 |
| Payload | 33.390.720 B |
| File completo | 33.390.806 B |
| Checksum | `2236291214962974841` |
| Build cold | 13,1719 s |
| Salvataggio | 1,13997 s |
| Caricamento con checksum | 1,00418 s |
| Confronto esaustivo tabella/oracle | 13,9133 s |
| Pass lookup warm | 2,39952 s |
| Wall totale del test | 34,676 s |
| Peak working set | 105.955.328 B |

L'enumerazione include per costruzione tie, flush sopra full house e wheel A-6-7-8-9. Gli indici estremi, i duplicati e la corruzione dell'ultimo byte sono testati separatamente. Un solve ridotto con tabella produce policy e utility identiche all'oracle.

Risorsa verificata: `out/build/codex-release-20260907/r3_seven_card_table_v1.bin`. È un artefatto di build, non un file sorgente.

## Benchmark trainer

Tre confronti A/B usano identici seed e ordine del sampling. Il protocollo è in [BENCHMARK_PROTOCOL.md](BENCHMARK_PROTOCOL.md).

| Replica | Oracle solve | Tabella solve | Oracle wall | Tabella wall | Esito numerico |
|---:|---:|---:|---:|---:|---|
| 1 | 28,9116 s | 27,7735 s | 30,972 s | 30,762 s | policy ed EV identici |
| 2 | 28,7764 s | 27,4534 s | 30,804 s | 30,407 s | policy ed EV identici |
| 3 | 28,8140 s | 28,2037 s | 30,848 s | 31,174 s | policy ed EV identici |

La mediana di `solve_seconds` scende da 28,8140 a 27,7735 s, circa il 3,61%. La mediana wall scende da 30,848 a 30,762 s, circa lo 0,28%; una replica wall è più lenta con la tabella. Il peak aumenta di circa 32–34 MiB, coerente con il payload.

La cache registra circa 3,88–3,93 milioni di hit contro 187–192 mila miss per replica. Il suo beneficio si applica sia all'oracle sia alla tabella e spiega perché il backend tabellare incide poco sul wall complessivo.

## Decisioni e limiti

Il backend tabellare è corretto e utilizzabile, ma non diventa il default. Resta utile per workload con molti showdown distinti o come risorsa di verifica. Lo scenario cold completo aggiunge almeno build, salvataggio e caricamento; lo scenario warm continua a includere caricamento e checksum.

La risorsa supporta soltanto il ruleset identificato. Varianti di ranking devono usare l'oracle o una nuova tabella con fingerprint diverso.

## Prossima fase

R4 deve compilare azioni e transizioni pubbliche e imporre limiti verificabili a stato numerico, cache ed export.
