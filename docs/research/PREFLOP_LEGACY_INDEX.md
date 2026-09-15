# Indice del programma preflop external sampling (rimosso il 2026-09-15)

Il programma preflop basato su external sampling MCCFR (fasi R0–R6, versioni V1–V23, 6–15
settembre 2026) è stato chiuso con la decisione registrata in
[PREFLOP_ARCHITECTURE_DECISION_LOG.md](PREFLOP_ARCHITECTURE_DECISION_LOG.md) e sostituito dal
solver vettoriale descritto in
[HU_PREFLOP_ALGORITHM_AND_ABSTRACTION_ANALYSIS_2026-09-15.md](HU_PREFLOP_ALGORITHM_AND_ABSTRACTION_ANALYSIS_2026-09-15.md)
e nella [roadmap P0–P10](PREFLOP_VECTOR_CFR_CODER_ROADMAP_2026-09-15.md).

I documenti del programma chiuso sono stati rimossi dal working tree perché contenevano gate,
baseline e roadmap in contraddizione con le decisioni correnti (D1–D3: gate di exploitability
fisica; Monker descrittivo). **Non vanno usati come riferimento per il nuovo lavoro.**

## Dove si trovano

Tutto resta nella storia git al tag `preflop-legacy-es-2026-09-15` (commit `04aa687`):

```text
git show preflop-legacy-es-2026-09-15:docs/research/preflop_r6_20260910/README.md
git checkout preflop-legacy-es-2026-09-15 -- docs/research/preflop_r6_20260910
```

## Cosa è stato rimosso

| Percorso | Contenuto | Motivo |
|---|---|---|
| `docs/HU_PREFLOP_2H_CODER_ROADMAP_2026-09-10.md` | roadmap R0–R9 del trainer external sampling | superata dalla roadmap P0–P10 |
| `docs/research/HU_PREFLOP_12H_FEASIBILITY_2026-09-10.md` | studio di fattibilità 12 ore | superato dall'analisi del 2026-09-15 |
| `docs/research/HU_PREFLOP_CPU_RAM_2H_ARCHITECTURE_STUDY_2026-09-10.md` | studio architetturale CPU/RAM | idem; la diagnosi utile è ripresa nell'analisi |
| `docs/research/preflop_12h_evidence_20260910/` | 63 file: run, log, controesempio dell'averaging | evidenza del programma chiuso |
| `docs/research/preflop_r1_20260910/` … `preflop_r5_20260910/` | report di fase R1–R5 e protocolli | idem |
| `docs/research/preflop_r6_20260910/` | 103 documenti, 336 JSON, 142 log: versioni V6–V23, gate, protocolli, confronti Monker | gate sulle frequenze Monker e baseline V17, non più in vigore |

Le 16 policy postflop binarie dei run V8–V18 (6,9 GB, mai tracciate da git) sono state spostate in
`benchmarks/results/legacy_preflop_es/` (cartella ignorata da git). Le due policy V17 restano
disponibili fino al gate P9; le altre 14 possono essere cancellate.

## Cosa resta in uso

- `docs/research/preflop_r0_20260910/`: contratto monetario CO40 e hash delle fixture, ancora
  validi.
- `docs/HU_PREFLOP_CO40_BENCHMARK.md`: fixture e contratto; la sezione dei gate rimanda alle
  decisioni correnti.
- `benchmarks/fixtures/hu_preflop_*.json`, `schemas/hu_preflop_*.schema.json`: usati dal codice
  legacy ancora in build (stadio 1 di D20) e dal comparatore.
- Il codice legacy in `libs/preflop/`: oracolo FiniteGame, contratto di averaging, tabella a 7
  carte; leggibile, non modificabile (roadmap §2.2).

## Risultati da ricordare del programma chiuso

- External sampling su deal fisici con bucket MC8: WMAE 13,4–18 pp contro Monker, TV fra seed
  11–13 pp, nessun candidato qualificato in 23 versioni.
- Cause identificate: bucket dominati dal rumore Monte Carlo, varianza per update incompatibile
  con azioni quasi indifferenti, gate sulle frequenze non adatto a misurare la convergenza,
  certificatore con decomposizione per sottogiochi River troppo costosa.
- Componenti riutilizzati dal nuovo programma: contratto di averaging external sampling (R2),
  tabella esatta a 7 carte (R3), FiniteGame con NashConv esatta (V21–V23), fixture CO40 e HU10.
