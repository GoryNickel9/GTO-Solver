# V19 — Audit CRN globale

Data: 2026-09-14  
Esito: `ENGINEERING_AND_FINITE_FIXTURE_PASS / QUALITY_GATE_C_FAIL`

## Perimetro

La Fase B aggiunge il flag opt-in `--global-common-random-numbers` al percorso batched del
trainer HU preflop. Il default resta disattivato. Il percorso non cambia l'albero, il payoff, il
rake, l'evaluator, il mapping dei bucket o l'ordine di riduzione.

Per ogni decisione del traverser, il percorso globale copia lo stato RNG all'ingresso e lo
ripristina prima di valutare ogni azione. Dopo il confronto, lo stato RNG prosegue dal primo ramo,
così i campioni sono correlati senza riutilizzare un valore comune come stima dell'EV. Il metodo è
serializzato nell'identificatore algoritmo `global_common_random_numbers_v1`.

## Implementazione

- `HuPreflopSolveOptions::global_common_random_numbers` è disponibile solo con il training batch.
- Il runner espone `--global-common-random-numbers` e registra il flag nell'output JSON.
- Il percorso copre decisioni preflop e postflop del sampler batched, inclusi card removal e
  terminali exact già previsti dalla configurazione.
- Il percorso root-only precedente resta compatibile e mantiene il proprio fingerprint.
- Il flag non è accettato nel percorso sequenziale online, per evitare una semantica RNG non
  verificata.

## Validazione deterministica

| Controllo | Esito |
| --- | --- |
| Build Release MSVC dei target HU | PASS |
| `gtosd_hu_preflop_telemetry_tests` | PASS, 4.167 asserzioni |
| Ripetizione single-worker con CRN globale | PASS |
| CRN globale con 1 e 8 worker | PASS, policy/EV/telemetria identiche |
| Fixture paired: marginali d'azione | PASS, multiset identici |
| Fixture paired: varianza della differenza | PASS, inferiore al replay indipendente |
| Fingerprint gioco/albero | PASS, `fnv1a64:a68337fa567aa2d9` |
| Rifiuto CRN fuori dal batch | PASS |
| Default di produzione | PASS, `false` |

Il test 1/8 usa quattro iterazioni, batch 1 e il gioco exact ridotto CO40. Il confronto esclude
soltanto i campi descrittivi che devono differire (`worker_threads` e il suffisso worker del
fingerprint); confronta policy, regret, strategy sum, EV, contatori, mapping e righe telemetry.

## Smoke CLI

Gli smoke CRN globali sono stati eseguiti su `GTP-HU-PREFLOP-CO40-001` con due iterazioni, batch 1,
telemetria attiva e worker 1/8. Entrambi producono 153 righe telemetry, lo stesso root EV (`-6,25`)
e lo stesso fingerprint dell'albero. I JSON differiscono solo per i campi descrittivi del numero
di worker e per i percorsi dei file.

| Artefatto | SHA-256 |
| --- | --- |
| `.tmp/v19_global_crn_smoke.json` | `019136DBCF589D1BC789247C8E22BB7C6A3551BE01D9A9A88BF140F5F1D17D47` |
| `.tmp/v19_global_crn_smoke_8.json` | `6646832232A92FF5BFAC913FDCACBB5F20123E4B9BB93A1C9ABE6750FBDC2C83` |
| `.tmp/v19_global_crn_smoke_telemetry.json` | `E996F74CC8B01E945436FE4D348754C812DE0114601A19EBCB70D65CCE348A3A` |
| `.tmp/v19_global_crn_smoke_8_telemetry.json` | `1AEBC548CB9969CBEC20C2F17D11BDAA803047B0EECB7E26232B7ADC15AD1613` |

## Gate matematico e limite della misura

La fixture finita usa gli stessi shock marginali per le due azioni e cambia soltanto il pairing.
Verifica in modo deterministico:

```text
Var_CRN[value(a) - value(b)] <= Var_independent[value(a) - value(b)]
```

Questa prova chiude il requisito algebrico, non misura la covarianza nei bucket reali a 2M. La
telemetria di produzione conserva momenti marginali per azione, ma non la covarianza paired; il
cap da 10.000 righe rende inoltre incompleto il campione dei bucket.

La coppia C da `2M + 2M` è stata completata. Il CRN globale migliora la WMAE media del `2,24%`,
ma peggiora la TV fra seed del `10,68%` e gli errori Call/Fold raggiunti passano da 7 a 10. Il
beneficio algebrico della fixture non si traduce quindi nel gate strategico richiesto.

## Decisione

`GLOBAL_CRN_REJECTED_AT_GATE_C`: il percorso resta isolato, versionato, riproducibile e
disattivato di default. Non viene promosso nel viewer o nei default; V17 resta la baseline.
