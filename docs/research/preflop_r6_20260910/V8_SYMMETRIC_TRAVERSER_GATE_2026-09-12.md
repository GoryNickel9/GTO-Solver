# R6 — Gate v8 con traverser simmetrici

## Esito

`ENGINEERING_PASS / PAIRED_2M_COMPLETE / COMPARATIVE_GATE_FAIL / R6_GATE_FAIL`.

Il verdetto storico a 250k è stato riesaminato per istruzione dell'utente con due run complete a 2M. V8 migliora leggermente WMAE, TV fra seed e P95 rispetto a V7/2M, ma non raggiunge né il miglioramento minimo di `0,5 pp` WMAE né la riduzione del `20%` della TV fra seed.

## Configurazione

I due run rispettano il protocollo congelato in [V8_SYMMETRIC_TRAVERSER_PROTOCOL_2026-09-12.md](V8_SYMMETRIC_TRAVERSER_PROTOCOL_2026-09-12.md): Linear MCCFR, batch `32`, otto worker, K=4 su entrambi i traverser, MC8, capacità `32/128/512`, stessi seed del baseline v7 e contratto monetario v2.

L'unica differenza dal baseline è il mapping v8 `street_adaptive_category_equity_profile`.

## Risultati 250k

| Profilo | Seed | WMAE | TV contro Monker | P95 TV | TV fra seed | Infoset | Payload | Bucket F/T/R | Solve |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| v7 simmetrico | 1 | 19,3968 pp | 48,4920 pp | 96,8198 pp | — | 799.600 | 119.062.080 B | 15/59/138 | 293,294 s |
| v7 simmetrico | 2 | 18,0035 pp | 45,0087 pp | 96,6501 pp | 25,8938 pp | 808.071 | 120.218.544 B | 15/59/138 | 294,343 s |
| v8 simmetrico | 1 | 19,8029 pp | 49,5074 pp | 95,9491 pp | — | 1.237.927 | 182.417.328 B | 29/125/202 | 197,474 s |
| v8 simmetrico | 2 | 18,8884 pp | 47,2211 pp | 97,3305 pp | 26,4529 pp | 1.254.334 | 185.149.440 B | 29/124/207 | 203,145 s |

| Gate | Baseline v7 | v8 | Esito |
|---|---:|---:|---|
| WMAE media migliore di almeno 0,5 pp oppure TV fra seed −20% | 18,7001 / 25,8938 pp | 19,3457 / 26,4529 pp | FAIL |
| metrica primaria non scelta non peggiore di oltre 0,5 pp | — | WMAE +0,6456; TV +0,5591 pp | FAIL |
| P95 media non peggiore di oltre 2 pp | 96,7350 pp | 96,6398 pp | PASS |
| payload sotto 512 MiB | 120.218.544 B max | 185.149.440 B max | PASS |
| identità, normalizzazione, EV e ricostruzione | — | tutti validi | PASS |

Il v8 usa più bucket e più infoset, ma l'aumento di risoluzione non migliora la strategia root. Il risultato conferma il precedente gate v8: il raggruppamento più aggressivo delle categorie al flop elimina separazioni utili che la media simmetrica non può ricostruire.

## Risultati 2M

| Profilo | Seed | WMAE | TV contro Monker | P95 TV | TV fra seed | Infoset | Payload | Bucket F/T/R | Solve |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| v7 simmetrico | 1 | 15,2418 pp | 38,1045 pp | 97,9350 pp | — | 998.535 | 147.547.440 B | 15/61/145 | 2.077,13 s |
| v7 simmetrico | 2 | 14,5270 pp | 36,3174 pp | 95,7112 pp | **18,6455 pp** | 996.278 | 147.461.328 B | 15/62/146 | 2.077,46 s |
| v8 simmetrico | 1 | 15,3412 pp | 38,3529 pp | 96,5289 pp | — | 1.551.886 | 227.518.560 B | 29/125/212 | 1.267,22 s |
| v8 simmetrico | 2 | 14,0305 pp | 35,0764 pp | 92,5742 pp | **16,9686 pp** | 1.549.295 | 227.234.016 B | 29/125/213 | 1.248,41 s |

| Gate comparativo | V7/2M | V8/2M | Esito |
|---|---:|---:|---|
| WMAE media migliore di almeno 0,5 pp oppure TV fra seed −20% | 14,8844 / 18,6455 pp | 14,6858 / 16,9686 pp | FAIL: −0,1985 pp / −8,99% |
| metrica primaria non scelta non peggiore di oltre 0,5 pp | — | entrambe migliorano | PASS |
| P95 media non peggiore di oltre 2 pp | 96,8231 pp | 94,5515 pp | PASS |
| identità, normalizzazione, EV e ricostruzione | — | tutti validi | PASS |

La traiettoria 250k→2M dimostra che lo screen breve non era sufficiente per stimare la qualità: V8 passa da peggiore a leggermente migliore di V7 su tutte e tre le metriche, ma il margine resta inferiore al gate comparativo.

## Controlli dell'albero esportato

Ogni artefatto contiene tutti i `20` nodi decisionali preflop e `1.620` righe classe-nodo. Le probabilità sono finite e non negative; l'errore massimo di normalizzazione è `2,22e-16`. Tutti gli EV per azione sono finiti e l'errore massimo di ricostruzione dei regret root è zero.

Per i due artefatti 2M, tutti i `4.617` valori d'azione per seed sono finiti e l'errore massimo di ricostruzione dei regret root è zero. Per AA la strategia media è `99,9710%` call nel seed 1 e `99,6689%` call nel seed 2; gli EV post-hoc hanno soltanto 12 e 3 campioni per azione e non determinano retroattivamente le frequenze.

## Artefatti

- seed 1: `v8_symmetric_traverser_mean4_250k_seed1_monetary_v2.json`, SHA-256 `6130DB27BB3A8C24EF500613CC322160C733FED2DB49AD3E52B91CD91D8DA244`;
- seed 2: `v8_symmetric_traverser_mean4_250k_seed2_monetary_v2.json`, SHA-256 `470CC9C5DC141B038CFF271645D13F91266640A699A1DB2AE442382E8A9958C7`;
- 2M seed 1: `v8_street_adaptive_symmetric_mean4_2m_seed1_monetary_v2.json`, SHA-256 `852063B8581B3F2BE03F7E0E8B3AFD2AAAB7B0238DADD8B07F9D92E5A23C5366`;
- 2M seed 2: `v8_street_adaptive_symmetric_mean4_2m_seed2_monetary_v2.json`, SHA-256 `54B5E19A4BE3C489CBE8EA4ADFE988D52D696DB90BC58B04D1E7F9AF5368BDFF`;
- i report `.comparison.json` risultano, come previsto, `REJECTED / REFERENCE_CONFIG_INCOMPLETE`.

## Decisione

V8/2M non sostituisce V10: la sua WMAE è migliore di `0,5277 pp`, ma la TV fra seed è peggiore di `2,1245 pp`; V10 è l'unica candidata che supera il gate causale di stabilità del 20%. Nessuna delle due passa il gate finale R6.

Il prossimo controllo obbligatorio è V9/2M su due seed. Il precedente limite RAM da 512 MiB è stato rimosso perché non era un requisito fissato dall'utente.
