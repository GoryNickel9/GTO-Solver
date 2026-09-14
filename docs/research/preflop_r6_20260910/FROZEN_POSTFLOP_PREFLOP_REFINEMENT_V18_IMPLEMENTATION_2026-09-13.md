# R6 — Implementazione V18: refinement preflop con postflop congelato

Data: 2026-09-13  
Stato: `ENGINEERING_PASS`
Esito scientifico: `REJECTED`

## Obiettivo

V18 aggiunge una seconda fase di training ai due milioni di update V15/V17. La fase aggiuntiva
aggiorna soltanto regret e somme della strategia nei 20 nodi preflop. Le continuation postflop
usano la policy media prodotta dalla prima fase e non modificano lo stato postflop.

Il benchmark esterno non entra nel training. Regole, albero, rake, size, evaluator, tabella esatta
degli all-in e astrazione V8 restano invariati.

## Implementazione

- `HuPreflopSolveOptions::preflop_refinement_iterations` configura il secondo clock. Il risultato
  serializza separatamente `postflop_training_iterations`, `preflop_refinement_iterations` e il
  totale `iterations`.
- Il clock Linear MCCFR prosegue senza reset. Dopo gli update batch della prima fase, il trainer
  esegue il refinement sequenziale con External Sampling. Le somme preflop ricevono il peso
  lineare globale delle iterazioni `2.000.001–4.000.000`.
- Nei terminali postflop il traversal legge la strategia media dalla tabella congelata. Non chiama
  il solver postflop standalone e non applica delta a regret, strategy sum o bucket postflop.
- La combinazione batch più refinement è accettata soltanto per Linear MCCFR. Il percorso rifiuta
  esplicitamente External Sampling standard, DCFR e Chance-Sampled CFR con questo secondo clock.
- La CLI espone `--preflop-refinement-iterations`. L'identificatore dell'algoritmo include
  `frozen_postflop_rollout_refinement_v1`.

Il refinement V18 usa un solo campione di continuation per traversal e non applica K=4 o Common
Random Numbers alle azioni preflop. Questa scelta era preregistrata ed è ora il principale limite
misurato della versione.

## Test

Il test dedicato verifica:

- contatori `24 + 24`, suffisso dell'algoritmo e modifica effettiva della strategia preflop;
- uguaglianza esatta delle entry postflop prima e dopo il refinement;
- risultato numerico bit-identico quando la prima fase usa uno oppure otto worker;
- rifiuto del refinement batch con algoritmo diverso da Linear MCCFR.

Risultati Release:

```text
HU_PREFLOP_TREE_TESTS=PASS assertions=15950
gtosd_hu_preflop_decomposition PASS
CTest HU: 11/11 passed, 100%, 488.13 s
```

## Run complete

Entrambe le run rispettano il requisito di almeno due milioni di iterazioni per versione: sono
`2M` di training postflop più `2M` di refinement preflop, non una singola run da `4M` che continua
ad aggiornare il postflop.

| Seed | Solve | Infoset | EV CO ± SE | Policy postflop | JSON SHA-256 |
| --- | ---: | ---: | ---: | --- | --- |
| 1 | 2.443,17 s | 1.567.910 | −0,07324 ± 0,04844a | `fnv1a64:72bc7759a68c7426` | `B046837B…29EE` |
| 2 | 2.614,09 s | 1.564.841 | −0,04323 ± 0,05059a | `fnv1a64:d91366491d632854` | `06835466…E9E8` |

Il fingerprint dell'albero è `fnv1a64:a68337fa567aa2d9`. L'audit della policy binaria passa su
entrambi i file e conferma la tabella esatta `fnv1a64:fe73211ffab94a00`.

## Viewer al momento della run

Il viewer contiene entrambi i seed V18 e usa il seed 2 come sorgente iniziale, perché ha la WMAE
singola più bassa disponibile. Lo stato mostrato è `PROMISING / 10% GATE NOT MET`: l'inclusione
serve a confrontare le chart e non promuove V18 a soluzione qualificata.

La decisione successiva respinge V18 come candidata principale e ripristina V17 come baseline.
Il viewer non viene modificato nell'aggiornamento documentale e continua temporaneamente ad aprire
V18. La sincronizzazione dell'interfaccia è un'attività separata.

Il server postflop usa la policy binaria V18 seed 2. Health check e risposta delle query distinguono
`2.000.000` update postflop, `2.000.000` update di refinement e `4.000.000` update totali.

Validazione del viewer:

```text
chart export V18 seed 1: PASS, 20 nodi, 1.620 righe
chart export V18 seed 2: PASS, 20 nodi, 1.620 righe
postflop public tree: PASS, 9 entry, 10.060 nodi decisionali, 25.944 archi
JavaScript syntax: PASS
HTTP root: 200
POST /api/postflop: PASS, griglia 81 classi
```

## Limiti

La run non certifica NashConv. La configurazione Monker resta incompleta per albero postflop,
abstraction, build, stopping rule e metrica di convergenza. Gli EV d'azione campionati hanno
ancora errori standard elevati nei rami a bassa reach. Il verdetto scientifico è nel gate V18.
La decisione finale è registrata in
[V18_REJECTION_AND_NEXT_GATE_DECISION_2026-09-13.md](V18_REJECTION_AND_NEXT_GATE_DECISION_2026-09-13.md).
