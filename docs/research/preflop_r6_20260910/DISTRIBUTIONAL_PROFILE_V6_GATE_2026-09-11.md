# R6 — Gate del profilo distribuzionale v6

## Obiettivo

Verificare se lo scarto residuo del candidato Linear MCCFR dipende dalla perdita delle feature distribuzionali nel mapping legacy. Il confronto usa lo stesso albero, gli stessi seed, `100.000` iterazioni, batch `32`, otto worker, MC8 e capacità `32/128/512`.

## Difetto individuato nel mapping legacy

Il mapping legacy costruisce un codice a 16 bit nell'ordine categoria, equity, profilo aggregato dell'avversario, varianza, categoria futura dominante e texture. La capacità seleziona però soltanto il prefisso più significativo.

Alle capacità selezionate questo produce:

| Street | Capacità | Informazione effettivamente conservata |
|---|---:|---|
| Flop | 32 | categoria corrente e 1 bit di equity |
| Turn | 128 | categoria corrente e 3 bit di equity |
| River | 512 | categoria corrente, 4 bit di equity e 1 bit del profilo avversario |

Varianza, categoria futura dominante e texture vengono quindi calcolate, ma non possono distinguere bucket a queste capacità. Il mapping resta deterministico e gerarchico; la sua descrizione precedente era incompleta rispetto all'informazione effettivamente usata.

## Implementazione del challenger

È stata aggiunta la rappresentazione esplicita `DistributionalStrengthProfileV6`, senza modificare il mapping legacy. Il nuovo mapping:

1. conserva fino a quattro bit di equity come coordinata primaria;
2. usa i bit residui per un hash versionato di categoria corrente, tre profili avversari distinti, varianza, categoria futura dominante e texture;
3. include seed di partizione, street e versione nel dominio dell'hash;
4. usa un ID di astrazione distinto e porta il formato della policy campionata a `1.5`.

La CLI espone `--postflop-distributional-profile-v6`. Il test di regressione enumera osservazioni legali e dimostra che, a Flop con capacità 32, il v6 separa almeno una coppia di osservazioni che il legacy collassa pur avendo feature distribuzionali differenti. Sono inoltre verificati solve ridotto, identità dell'astrazione, validazione della policy e save/load con il nuovo formato.

## Risultati accoppiati

| Profilo | Seed | WMAE | TV media | P95 TV | Errore root massimo | Infoset | Payload numerico | Solve |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Legacy | 1 | 18,748250 pp | 46,870626 pp | 95,217455 pp | 25,434912 pp | 687.763 | 118.959.552 B | 22,997 s |
| Legacy | 2 | 18,547220 pp | 46,368051 pp | 96,304257 pp | 27,009821 pp | 669.046 | 115.498.944 B | 22,067 s |
| v6 | 1 | 19,274966 pp | 48,187415 pp | 93,713635 pp | 26,739743 pp | 1.487.487 | 242.421.984 B | 23,650 s |
| v6 | 2 | 19,427988 pp | 48,569969 pp | 93,558371 pp | 29,697418 pp | 1.501.751 | 245.369.376 B | 25,643 s |
| Legacy, media | — | **18,647735 pp** | **46,619339 pp** | 95,760856 pp | **26,222366 pp** | 678.405 | 117.229.248 B | 22,532 s |
| v6, media | — | 19,351477 pp | 48,378692 pp | **93,636003 pp** | 28,218580 pp | 1.494.619 | 243.895.680 B | 24,647 s |

Rispetto al legacy, il v6 peggiora il criterio primario di `0,703741 pp`, la TV media di `1,759353 pp` e l'errore root massimo medio di `1,996214 pp`. Migliora il P95 di `2,124853 pp`, ma usa `2,20×` gli infoset e `2,08×` il payload numerico.

La distanza WMAE fra i due seed scende da `14,842176 pp` a `13,536171 pp`. Il miglioramento di stabilità è insufficiente: resta oltre tredici volte il gate finale e non si traduce in maggiore somiglianza con il riferimento.

L'occupancy passa da `15/62/178` e `15/62/180` nel legacy a `32/128/477` e `32/128/475` nel v6. Il nuovo profilo usa davvero la capacità aggiuntiva, ma a 100.000 iterazioni diluisce l'addestramento su oltre il doppio dello stato.

## Validazione

Build Release con MSVC `/W4 /WX`: PASS.

```text
gtosd_hu_preflop_trainer_dependency_check PASS
gtosd_external_sampling_tests              PASS
gtosd_hu_preflop_sampling_tests            PASS
gtosd_hu_preflop_compiled_tests            PASS
gtosd_hu_preflop_abstraction_tests         PASS
gtosd_hu_preflop_parallel_tests            PASS
6/6 passed
```

La regressione completa del sottosistema preflop passa `11/11` in `511,53 s`. Include il test esaustivo R0/R1 (`502,97 s`), isolamento e dipendenze del trainer, reference preflight, tree, persistenza, sampling, betting compilato, astrazione, parallelismo e decomposizione.

Hash SHA-256:

- binario usato per i due solve v6 e il legacy seed 2: `DA9912B7DDAFA6246443199EE1E132B4B6D5A517337B2BDBC9DAD41E2C69FF00`;
- binario ricostruito dopo il bump del formato policy a `1.5` e usato per il replay legacy seed 1: `83F37146F794FD2CDAEEBB72B130C71206DC5C895919E0C0702C3F4962E8F0B2`;
- JSON replay legacy seed 1: `C7AD87A253221512BEE79237D41DF111E9DAD40098A0A4CF6DCFDF1332C4C54A`;
- JSON v6 seed 1: `A380C4CCDFC7EB94A2BC5A8FCE151514B9DF44B86BD9C7B58A64570F308B340C`;
- JSON v6 seed 2: `035C129148BEB386F392976D5FCA96BBC72C3FD7C002F3DB81A3CFEF81253F4F`.

Il bump `1.4 -> 1.5` cambia soltanto il contratto di persistenza e non i calcoli dei quattro solve. Il reader rifiuta esplicitamente versioni diverse, quindi non interpreta in modo ambiguo una policy precedente.

Il replay legacy seed 1 prodotto dal binario corrente è bit-identico all'artefatto originario per l'intera strategia root. Coincidono anche infoset, payload, EV, errore standard e ID dell'astrazione. La sola differenza funzionale è la diagnostica EV per azione aggiunta al JSON.

## Decisione

`FAIL` per il challenger v6. La rappresentazione viene conservata come modalità sperimentale versionata, ma è esclusa dalla candidata R6. Il mapping legacy `32/128/512` resta il migliore misurato, con la descrizione corretta: alle capacità correnti è soprattutto una gerarchia categoria/equity, non una rappresentazione completa di tutte le feature calcolate.

R6 resta `SCIENTIFIC_GATE_FAIL`. Non è autorizzato un run v6 più lungo: il criterio primario peggiora su entrambi i seed, lo stato raddoppia e il risultato non mostra una traiettoria credibile verso 1 pp. R7 non può iniziare senza un candidato R6 vicino ai range esterni o una revisione esplicita del gate.
