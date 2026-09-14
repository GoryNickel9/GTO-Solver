# R6 — Gate v7 della media simmetrica delle traversate

## Esito

`ENGINEERING_PASS / 250K_QUALITY_PASS / RELATIVE_COST_FAIL / 2M_DIAGNOSTIC_PASS / R6_GATE_FAIL`.

La media K=4 applicata a entrambi i traverser produce il miglior accordo medio con Monker sul contratto monetario corrente: `14,8844 pp` WMAE a 2M, contro `16,9571 pp` del K=4 read-only. La stabilità della strategia media fra seed peggiora però da `16,6353` a `18,6455 pp`. Il candidato resta sperimentale e R6 non passa.

## Implementazione

Il pass CO usa quattro deal condizionati alla classe CO. Ogni azione root viene valutata sui quattro deal; i delta delle continuazioni pesano `1/4` e il root riceve un solo update medio.

Il pass BTN usa a sua volta quattro traversate complete condizionate alla classe BTN. I quattro vettori sparsi pesano `1/4`. Il reducer mantiene ordine deterministico, clock Linear MCCFR e policy congelata nel batch.

La modalità è disattivata per default e viene identificata da:

```text
root_action_rollouts4_v1
continuation_mean_updates_v1
symmetric_traverser_mean_updates_v1
```

## Correttezza

Il test enumera le 630 combo e dimostra `p(C) p(combo | C) = 1/630`. Il sampler è deterministico e privo di collisioni per tutte le 81 classi, sia con CO sia con BTN traverser. Le configurazioni senza Linear MCCFR, batch, K>1 o continuation mean CO vengono rifiutate.

Uno e otto worker producono lo stesso risultato numerico. Una prova separata verifica che gli update BTN aggiuntivi cambino la policy postflop rispetto alla variante CO-only.

## Screen 250k

| Modalità | Seed | SE mediano | WMAE | TV contro Monker | Infoset | Solve |
|---|---:|---:|---:|---:|---:|---:|
| K=4 read-only | 1 | 0,1601a | 18,1999 pp | 45,4998 pp | 609.448 | 163,262 s |
| K=4 read-only | 2 | 0,1635a | 19,7836 pp | 49,4591 pp | 625.719 | 163,520 s |
| K=4 simmetrico | 1 | 0,1282a | 19,3968 pp | 48,4920 pp | 799.600 | 293,294 s |
| K=4 simmetrico | 2 | 0,1443a | 18,0035 pp | 45,0087 pp | 808.071 | 294,343 s |

| Gate | Osservato | Esito |
|---|---:|---|
| TV fra seed almeno −10% | `37,4164 → 25,8938 pp`, −30,80% | PASS |
| SE mediano massimo +10% | `0,1618 → 0,1362a`, −15,82% | PASS |
| WMAE media massimo +1 pp | `18,9918 → 18,7001 pp`, −0,2916 pp | PASS |
| Recupero di almeno 2 pp sul CO-only | `23,0776 → 18,7001 pp`, −4,3775 pp | PASS |
| Costo massimo 1,25× | `163,391 → 293,819 s`, 1,80× | FAIL |

L'aumento della cache bucket da 1M a 4M entry è stato escluso con un A/B 100k sullo stesso seed. Policy e diagnostica restano identiche; le eviction scendono da 2.273.599 a 80.800, ma il wall sale da 112,255 a 126,113 s. La cache standard resta selezionata.

## Conferma 2M autorizzata

L'utente ha autorizzato prima dello screen l'aumento delle iterazioni quando necessario. Tutti i gate di qualità passano e la proiezione resta sotto le due ore; la conferma 2M viene quindi eseguita come eccezione diagnostica, senza cancellare il fallimento del costo relativo.

| Modalità | Seed | SE mediano | Classi sotto 2 SE | WMAE | TV contro Monker | Infoset | Solve |
|---|---:|---:|---:|---:|---:|---:|---:|
| K=4 read-only | 1 | 0,0404a | 44/81 | 17,6745 pp | 44,1863 pp | 881.212 | 1.347,806 s |
| K=4 read-only | 2 | 0,0440a | 49/81 | 16,2397 pp | 40,5993 pp | 877.419 | 1.348,347 s |
| K=4 simmetrico | 1 | 0,0416a | 48/81 | 15,2418 pp | 38,1045 pp | 998.535 | 2.077,126 s |
| K=4 simmetrico | 2 | 0,0390a | 49/81 | 14,5270 pp | 36,3174 pp | 996.278 | 2.077,458 s |

| Metrica a due seed | K=4 read-only | K=4 simmetrico | Variazione |
|---|---:|---:|---:|
| WMAE media | 16,9571 pp | **14,8844 pp** | −2,0727 pp |
| TV media contro Monker | 42,3928 pp | **37,2110 pp** | −5,1818 pp |
| TV strategia media fra seed | **16,6353 pp** | 18,6455 pp | +12,08% |
| TV strategia corrente fra seed | 21,6081 pp | **20,9959 pp** | −2,83% |
| Azione corrente discordante | **17 classi / 114 combo** | 23 classi / 142 combo | +6 classi / +28 combo |
| SE mediano medio | 0,0422a | **0,0403a** | −4,64% |

La traiettoria simmetrica `250k→2M` riduce la TV fra seed del 27,99% e la WMAE di 3,8157 pp. Il risultato migliora con le iterazioni, ma non abbastanza da chiudere R6.

Per AA entrambi i seed scelgono Call. Il margine `Call - Raise 6a` è `+0,7000 ± 0,1943a` e `−0,0173 ± 0,1992a` a 250k; a 2M diventa rispettivamente `+0,1954 ± 0,0657a` e `+0,0050 ± 0,0661a`. La strategia media chiama AA al `99,9248%` nel seed 1 e al `90,6866%` nel seed 2.

## Risorse e integrità

I due run 2M registrano:

- seed 1: `147.547.440` byte di stato numerico, `1.511` update simultanei massimi, `711` shadow rollout e `6.873.008` byte di scratch;
- seed 2: `147.461.328` byte di stato numerico, `1.496` update simultanei massimi, `711` shadow rollout e `6.908.408` byte di scratch.

La ricostruzione del regret root ha errore massimo zero. I confronti Monker restano `REJECTED / REFERENCE_CONFIG_INCOMPLETE` e non costituiscono certificazione.

Build MSVC Release `/W4 /WX`: PASS.

```text
R6_HU_PREFLOP_PARALLEL_TESTS=PASS
assertions=2073

11/11 passed
Total Test time (real) = 488,12 s
```

SHA-256:

- screen 250k seed 1: `524D4FC166E7D5438F126F5E5E1E3F99166C6FC669F263628D774F91D3B2E040`;
- screen 250k seed 2: `03AABB7B41609FCD6034CFB0613A7ABA55E3BBEFC1BCCC044F4EB9FE918DB971`;
- conferma 2M seed 1: `F9ECFEFE50EBD28534B20CA10690CB7BD9BDE8C01CACB7C7C5D56D9B590DEB4A`;
- conferma 2M seed 2: `9AD8088B608F5525BBB7C5C13B2779E5EC56323F5CF8B0F34B5EE64AB0D92C85`.

## Decisione

Il simmetrico è il miglior candidato corrente per WMAE, ma non sostituisce K=4 read-only come candidato più stabile. Nessuno dei due passa R6. Il confronto 250k→2M prevede, con una semplice legge di potenza descrittiva, circa `13,79 pp` WMAE e `16,71 pp` TV a 4M. Un altro raddoppio non può avvicinare materialmente il gate di 1 pp e non viene eseguito.

Il prossimo controllo mantiene trainer simmetrico, seed e risorse e cambia soltanto l'astrazione da v7 a v8. Serve a verificare se la rappresentazione street-adaptive, respinta con il trainer precedente, beneficia della minore varianza delle continuazioni.
