# Fase 7 — HU postflop CLI production

## 1. Stato

La Fase 7 è **in corso**. Questo incremento implementa il preflight production
config-specifico e non dichiara ancora un solve Short Deck GTO.

## 2. Funzioni completate

| Funzione | Stato | Evidenza |
|---|---:|---|
| Validazione config production | Completata | `gto_cli postflop validate` |
| Stima exact per config | Completata | `analyze_postflop_config` usa tree fisico, combo 528/496/465 e layout F6 |
| Selezione backend | Completata | Lazy in-RAM se entra in RAM, altrimenti out-of-core se entrano working set e backing |
| Rifiuto preventivo | Completata | Exit code 3 quando né RAM né disco soddisfano il preflight |
| Nessun bucketing/sampling | Completata | Flag espliciti nel report CLI e test PF-F1 |

## 3. Comandi

```powershell
gto_cli postflop validate config.json
gto_cli postflop estimate config.json 12 8
```

I budget di `estimate` sono espressi in GiB interi. L'output contiene nodi,
infoset, azioni, peak lazy, working set out-of-core, backing store e backend
selezionato.

## 4. Evidenza PF-F1

| Metrica | Valore |
|---|---:|
| Infoset exact | 30.873.216 |
| Azioni exact | 66.756.096 |
| Lazy peak | 5.622.269.688 B |
| Out-of-core backing | 4.554.172.152 B |

## 5. Lavoro residuo del gate F7

1. costruire il finite game Short Deck con range fisici e infoset;
2. collegarlo al traversal CFR+ sul layout lazy/out-of-core;
3. implementare solve, pause, resume e cancel production;
4. aggiungere progress flushato, BR/NashConv periodiche e final verification;
5. implementare strategy query e report Markdown/JSON;
6. dimostrare PF-F1 sotto l'1% del pot.

Fino al completamento di questi punti il gate F7 resta aperto.
