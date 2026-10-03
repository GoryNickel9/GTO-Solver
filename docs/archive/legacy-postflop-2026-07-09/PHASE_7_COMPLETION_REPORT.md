# Fase 7 — HU postflop CLI production

Aggiornato: 2026-07-28

## 1. Esito

La Fase 7 è **completata localmente**. Il modulo pubblico
`gtosd::postflop` integra il public tree fisico, le 630 combo Short Deck,
card removal, showdown e settlement exact in un traversal CFR+ production.
Il percorso non usa sampling o bucketing.

Il benchmark PF-F1 è stato risolto e ricertificato a 125 iterazioni con
`NashConv / pot = 0,741405%`, sotto il gate dell'1%. La certificazione usa una
best response exact infoset-aware: il risultato non deriva dalla sola EV del
profilo.

## 2. Copertura delle attività

| # | Requisito F7 | Stato | Implementazione |
|---:|---|---:|---|
| 1 | Algoritmo e memoria vincitori | Completato | CFR+ alternato con layout lazy in-RAM; fallback exact out-of-core paginato |
| 2 | Validate/estimate | Completato | Validazione tipizzata, conteggi exact e scelta preventiva del backend |
| 3 | Solve/pause/resume/cancel | Completato | Comandi CLI, file di controllo e checkpoint atomico a ogni certificazione |
| 4 | Progress flushato | Completato | Riga di progresso con `std::endl` per ogni intervallo configurato |
| 5 | Exact BR periodica | Completato | EV, BR per CO/BTN, NashConv e normalizzazione sul pot |
| 6 | Final verification | Completato | `postflop certify`, exit code non-zero se il gate dell'1% fallisce |
| 7 | Strategy query API | Completato | Query per `public_node` e combo fisica `0..629` |
| 8 | Report Markdown/JSON | Completato | Stato, backend, iterazioni, tree, EV, BR, NashConv, errore di normalizzazione, RSS e tempo |
| 9 | Benchmark GTO+ importabile | Completato | Schema normalizzato e comando `compare-gto-plus` con fingerprint obbligatorio |
| 10 | OOM preventivo | Completato | Budget RAM/disco verificati prima di allocare o creare il backing file |

## 3. Contratto exact del finite game

| Proprietà | Valore |
|---|---:|
| Combo fisiche totali | 630 |
| Combo legali flop / turn / river | 528 / 496 / 465 |
| Probabilità chance condizionata HU | `1/29` turn, `1/28` river |
| Posizioni | CO e BTN |
| Evaluator | Short Deck exact, colore sopra full |
| Payoff | Settlement F1, rake incluso quando configurato |
| Strategia media | CFR+ con delay persistito nel checkpoint |
| Best response | Exact e infoset-aware |
| Approssimazioni | Nessun sampling, nessun bucketing |

## 4. Evidenza PF-F1

Fixture: flop `As Qd 7c`, pot 10 ante, stack 20 ante, size 50%, raise
depth 1, rake zero.

| Metrica | Valore |
|---|---:|
| Fingerprint | `fnv1a64:dbfadc54d095e2c1` |
| Nodi pubblici | 165.774 |
| Edge | 165.773 |
| Decision node | 66.336 |
| Infoset exact | 30.873.216 |
| Azioni exact | 66.756.096 |
| Iterazioni certificate | 125 |
| EV CO / BTN | -0,313564 / +0,313564 ante |
| BR CO / BTN | -0,275868 / +0,350009 ante |
| NashConv | 0,0741405 ante |
| NashConv / pot | **0,00741405 = 0,741405%** |
| Somma payoff | `2,05391e-15` ante |
| Esito gate `<1%` | **PASS** |

La ricertificazione è stata eseguita indipendentemente dal solve tramite:

```powershell
gto_cli postflop certify out\pf-f1.json out\pf-f1.chk
```

Il segmento finale di resume da 100 a 125 iterazioni ha impiegato 91,393 s
sulla macchina locale. È una misura osservata, non uno SLA e non rappresenta
il tempo cumulativo dell'intero solve segmentato.

## 5. Backend e checkpoint

| Verifica | Risultato |
|---|---:|
| Lazy checkpoint PF-F1 | 1.068.097.627 B |
| Out-of-core action buffer PF-F1 | 1.068.097.536 B |
| Manifest out-of-core | 96 B |
| Peak RSS out-of-core PF-F1 | 113.893.376 B |
| Parità alla prima iterazione | EV, BR e NashConv identici al lazy |
| Resume | Stato alla seconda iterazione equivalente al percorso continuo nei test |
| Corruzione inline | Bit flip rilevato dal checksum |
| Scrittura atomica | File temporaneo e replace atomico |

Il fallback out-of-core usa una cache LRU di 64 pagine da 64 KiB, non una
mappatura residente dell'intero file. Il backing file resta esterno al
manifest ed è validato per dimensione; cifratura, compressione e hardening
storage appartengono alla Fase 8.

## 6. Comandi production

```powershell
gto_cli postflop validate config.json
gto_cli postflop estimate config.json 12 8
gto_cli postflop benchmark-config pf-f1 out\pf-f1.json
gto_cli postflop solve out\pf-f1.json 125 out\pf-f1.chk out\pf-f1-report 12 8 25
gto_cli postflop resume out\pf-f1.json 125 out\pf-f1.chk out\pf-f1-report 12 8 25
gto_cli postflop pause out\pf-f1.chk
gto_cli postflop cancel out\pf-f1.chk
gto_cli postflop query out\pf-f1.json out\pf-f1.chk 0 42
gto_cli postflop certify out\pf-f1.json out\pf-f1.chk
gto_cli postflop compare-gto-plus out\pf-f1.json out\pf-f1.chk reference.json
```

## 7. Verifiche finali

| Verifica | Risultato |
|---|---:|
| MSVC Release `/W4 /WX` | PASS |
| CTest Release completo | PASS, 11/11 in 83,71 s |
| Suite F7 Release | PASS, 34 asserzioni in 7,76 s |
| Suite F7 Debug | PASS in 73,38 s |
| Suite F7 MSVC ASan | PASS in 77,17 s |
| clang-format dry-run | PASS |
| `git diff --check` | PASS |
| Install tree | PASS, libreria/header/schema/CLI ed export CMake |
| PF-F1 final certify | PASS, 0,741405% |
| PF-F1 out-of-core smoke | PASS, 113.893.376 B peak RSS |

Non viene dichiarato un esito remoto GitHub Actions in questo rapporto.
Non è stato eseguito un confronto numerico con GTO+ perché non è stato
fornito un export esterno equivalente; sono stati implementati lo schema
normalizzato, il controllo del fingerprint e il comando di confronto
richiesti dal gate.

## 8. Prossima fase

La prossima milestone è **Fase 8 — Storage della soluzione**: formato `.gtsd`,
compressione, cifratura, migrazioni atomiche, indice SQLite e API di
navigazione sopra la strategy query F7.
