# Risultati piano di ottimizzazione TSTC9D — 2026-08-29

> **CORREZIONE SEMANTICA 2026-09-04.** Le misure e i reject di velocità restano
> evidenza; i PASS di stato/Peak RSS rispetto al valore TST da 2.000 MB non sono
> un confronto GTO+ valido. Vedere il
> [`piano di correzione`](GTO_PLUS_SOLVER_MEMORY_SEMANTICS_AND_GATE_CORRECTION_PLAN_2026-09-03.md).

## Esito

La fase chiude con l'**Esito B — nuovo collo dimostrato**. Nessun candidato
prestazionale supera lo smoke A/B, quindi nessun esperimento viene promosso e
non vengono eseguiti un full TST candidato o cinque processi indipendenti.
L'ultimo full ufficiale resta il baseline DCFR a 170 iterazioni.

- HEAD iniziale: `8b2d025084cfedc0fc600fde999ca9706100ebe5`;
- ultimo commit di implementazione prima di questo report: `205abd0`;
- benchmark: `GTP-TSTC9D-101`, exact CPU/RAM-only, otto thread;
- gioco, range, sizing, fingerprint e criterio dEV non sono stati modificati.

## Modifiche mantenute

1. Fix della current strategy signed: ogni raw `uint16_t` regret DCFR viene
   reinterpretato come `int16_t`, troncato con `max(0, regret)` e normalizzato.
   Sono coperti path 2/3/generic action, AVX2 e tail scalare.
2. Telemetria read-only compile-time per scale churn, density/liveness e
   workload/reuse showdown. Le scansioni diagnostiche sono assenti dal build
   Release di timing.
3. Microbenchmark standalone `gtosd_signed_codec_benchmark` per confrontare
   il backend scaled 4-byte e signed13/strategy11 3-byte.
4. Documentazione completa di misure e decisioni in
   `docs/speed_optimization_journey.md` §§8.53-8.59.

I prototipi persistent scale, cache showdown e action liveness sono stati
rimossi integralmente dopo il reject. Non rimangono flag dormienti o branch
nel solver production.

## Current vs average corretti

| Benchmark | Checkpoint | dEV average | dEV current | Decisione |
|---|---:|---:|---:|---|
| AHKHQH | 80 | 0,685946% | 3,980414% | average |
| TH7D6S | 80 | 0,805130% | 1,473611% | average |
| TSTC9D | 170 | 0,985760% | 2,389411% | average |

La current oscilla (TH: 1,473611% @80, 7,159870% @100, 1,853445% @120).
L'average è migliore a ogni checkpoint condiviso e resta l'output canonico.

## Telemetria P1

TSTC9D, secondo aggiornamento, somma dei due player pass:

| Famiglia | Misura |
|---|---:|
| scale check | 902.106 |
| entry che richiedono rescale | 409.655.611 / 507.046.140 (80,79%) |
| strategy entry exact zero | 124.900.139 / 536.333.425 (23,28%) |
| whole-zero action | 266.469 |
| subtree saltati | 194.027 |
| showdown | 891.296 |
| fingerprint reach ripetuti | 500.747 (56,18%) |

Il reuse è asimmetrico: 495.971/586.980 nel primo pass, 4.776/304.316 nel
secondo. Il fingerprint usa gli exact bit IEEE, ma non sostituisce l'exact
equality richiesta prima di un reuse effettivo.

## A/B e decisioni

| Step | Candidato | Risultato | Decisione |
|---|---|---|---|
| P0 | decoder signed current | 179 assertion Phase 7; 24 reference; differential zero | ACCEPT correctness |
| P0.5 | current vs average | average migliore e stabile su 3 benchmark | ACCEPT average |
| P1 | telemetria compile-time | scale/density/showdown misurati | ACCEPT tooling |
| P2 | persistent scale | traversal medio 17,170568 -> 17,424070 s (+1,48%) | REJECT |
| P3 | exact last-showdown cache | 17,796734 -> 19,647289 s (+10,40%) | REJECT |
| P4 | action liveness scan | 17,765595 -> 19,188408 s (+8,01%); trajectory diversa | REJECT |
| P5 | signed13/strategy11 | update 6,597 -> 11,418 ms; errore mean 0,000488 -> 0,277688 | REJECT integration |
| P6 | chance mapping/locality | chance circa 8,3%; ordine canonico vincolante | REJECT come priorità corrente |
| P7 | scheduling | imbalance già misurato 9-11%, upside globale stimato <5% | REJECT come priorità corrente |

P6 e P7 non ricevono un prototipo: dopo i reject precedenti non hanno impatto
teorico sufficiente per colmare 24,19 s senza rischio numerico/architetturale.

## Gate e ultimo full autorevole

| Metrica | GTOSD | Gate | Stato |
|---|---:|---:|---|
| iterazioni / dEV | 170 / 0,985759519% | <1% | PASS |
| root EV | 8,492540035 ante | 8,50165 ±0,05 | PASS |
| peak RSS | 1.968.537.600 B | <2.000.000.000 B | PASS |
| solver state | 1.472.605.376 B | <2.000.000.000 B | PASS |
| elapsed solver | 153,176351 s | <=128,988889 s | FAIL |

Gap residuo: `37,086351 s` rispetto al GTO+ raw `116,09 s` e `24,187462 s`
rispetto al limite 90%. Il full non viene ripetuto perché nessun candidato è
stato promosso; una nuova esecuzione identica non costituirebbe evidenza di
ottimizzazione. Per lo stesso motivo i cinque processi restano congelati.

## Validazione finale

- build Release completa: PASS;
- `ctest`: 18/18 PASS, 192,25 s;
- Phase 7: PASS, 179 assertion;
- reference GTO+: PASS, 24 assertion;
- serial/parallel regret e strategy delta: zero;
- asymmetric range node-owned isomorphism: PASS;
- microbenchmark codec: build e cinque ripetizioni PASS;
- `git diff --check`: PASS (soli warning EOL del worktree Windows).

## Risposta conclusiva

**Quale costo misurato impedisce ancora a GTOSD di superare il gate GTO+, e
quale singola modifica generale ha ora il miglior rapporto impatto/rischio?**

Il costo dominante resta lo showdown dinamico: accumulo rank/card, prefix e
produzione dei valori valgono circa il 55,8% del traversal. Il reuse per cache
non ha località sufficiente e il confronto exact peggiora il tempo. La singola
direzione con il miglior rapporto impatto/rischio è quindi una fusione locale
del path `PlayerIndexed<float>` che costruisca rank base e correzioni blocker
durante la prefix construction, eliminando almeno una lettura completa di
`card_prefix` senza cambiare ordine di somma, payoff, mapping o storage. Deve
partire da un microbenchmark del kernel showdown e da un differential
bit-identico prima di un nuovo TST smoke.
