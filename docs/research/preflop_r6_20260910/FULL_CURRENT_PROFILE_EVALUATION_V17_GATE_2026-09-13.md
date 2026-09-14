# R6 — Gate V17: profilo corrente completo

Data: 2026-09-13  
Esito: `ENGINEERING_PASS / COST_PASS / ALL_IN_AUDIT_PASS / CURRENT_PROFILE_GATE_FAIL / QUALITY_GATE_FAIL`

## Risultato

V17 completa due run indipendenti da 2.000.000 di iterazioni. La strategia appresa è bit-identica
a V15: la versione aggiunge soltanto valutazione e persistenza del profilo corrente completo.

Il costo di training resta sotto 60 minuti per seed. La TV fra seed rimane `10,9453 pp` per la
media CFR e `11,1472 pp` per la corrente finale, entrambe sopra `5 pp`. La WMAE contro il
riferimento resta `14,1312/13,7631 pp`. V17 non entra nel viewer e R6 resta aperta.

## Coppia 2M

| Metrica | Seed 1 | Seed 2 | Media/coppia |
| --- | ---: | ---: | ---: |
| Iterazioni | 2.000.000 | 2.000.000 | — |
| Solve | 2.234,87 s | 2.101,53 s | 2.168,20 s |
| Solve | 37,25 min | 35,03 min | 36,14 min |
| Oracle preflop | 59,10 s | 66,27 s | 62,68 s |
| EV root media ± SE | −0,12239 ± 0,05103a | −0,13887 ± 0,05101a | — |
| EV root corrente ± SE | −0,15377 ± 0,04942a | −0,14118 ± 0,05029a | — |
| Infoset training | 1.567.910 | 1.564.841 | — |
| WMAE contro Monker | 14,1312 pp | 13,7631 pp | **13,9472 pp** |
| TV contro Monker | 35,3280 pp | 34,4078 pp | **34,8679 pp** |
| TV media fra seed | — | — | **10,9453 pp** |
| TV corrente fra seed | — | — | **11,1472 pp** |

Il confronto esterno resta `REJECTED / REFERENCE_CONFIG_INCOMPLETE`. Il trainer non legge Monker
e il confronto non prova che i due giochi postflop coincidano.

## AA al CO root

La media CFR sceglie call al `99,9978%` nel seed 1 e al `99,9263%` nel seed 2. V17 non dimostra
che questa scelta sia sbagliata:

- seed 1: call `6,0667 ± 0,9950a`; raise 6a `6,7814 ± 0,7445a`;
- seed 2: call `8,6250 ± 1,0159a`; raise 10a `8,2592 ± 0,6917a`.

Nel seed 1 il vantaggio puntuale di raise 6a sul call è `0,7147a`, inferiore all'incertezza
combinata marginale di circa `1,24a`. Nel seed 2 il call è già il massimo puntuale. Le percentuali
derivano dai regret accumulati durante il training, non da una softmax degli EV post-hoc.

## Audit Call/Fold esatto

L'oracolo esatto controlla 10 nodi all-in e 810 classi per seed.

| Controllo | Seed 1 | Seed 2 |
| --- | ---: | ---: |
| Righe medie materialmente inferiori | 156 | 159 |
| Righe medie con reach pubblica ≥1% | 6 | 1 |
| Righe correnti materialmente inferiori | 2 | 2 |
| Righe correnti con reach pubblica ≥1% | **0** | **0** |
| Righe medie passate al best nella corrente | 155 | 157 |

L'errore Call-vs-Fold mostrato nelle versioni precedenti non persiste nella corrente sui rami
pubblicamente raggiunti. La media conserva però decisioni storiche inferiori: è un segnale di
convergenza lenta, non un errore dell'enumeratore.

## Audit corrente contro corrente

Il profilo corrente completo non supera il gate preregistrato.

| Controllo | Seed 1 | Seed 2 |
| --- | ---: | ---: |
| Righe con EV corrente | 1.616 | 1.615 |
| Azioni inferiori materiali, frequenza ≥5% | 587 | 554 |
| Su percorso pubblico con reach ≥1% | 302 | 281 |
| Separate da almeno 2 SE approssimative | 35 | 24 |
| Separate dopo Bonferroni 5% | **7** | **4** |
| Perdita pesata sul deal root | 0,85779a | 0,76881a |

La corrente finale è un singolo iterato MCCFR, non la soluzione. Le violazioni Bonferroni si
concentrano in rami come `CO Call → BTN Raise 6a`, `CO Call → BTN Raise 10a` e nella risposta BTN
dopo `CO Raise 10a`. Questo spiega perché mostrarla come chart principale produrrebbe azioni
nettamente EV-negative. La media CFR resta la policy da pubblicare.

## Risposte campionate

Le risposte usano 5.000 iterazioni di training e 10.000 deal di valutazione. Tutte e quattro le
stime risultano peggiori dell'EV del profilo fissato, quindi il limite inferiore individualmente
clampato è `0a` per media e corrente in entrambi i seed. Questo non significa NashConv zero: la
risposta è sotto-addestrata e non certificata. V17 rende il limite semanticamente corretto, ma non
chiude la convergenza interna.

## Gate

| Requisito preregistrato | Esito |
| --- | --- |
| Build, test, persistenza e compatibilità | PASS |
| Due run 2M sotto 60 minuti per solve | PASS |
| EV corrente usa corrente su tutto l'albero | PASS |
| Nessuna azione corrente inferiore sui rami con reach ≥1% | **FAIL** |
| TV media e corrente fra seed ≤5 pp | **FAIL** |
| WMAE per seed ≤5 pp | **FAIL** |
| Limite di risposta finito con SE | PASS semantico, non certificante |

## Artefatti principali

| Artefatto | SHA-256 |
| --- | --- |
| candidato seed 1 | `E7831172E58CAA2F60912E2CC32E56BE91AC7CAFC4DB079BDF647CCDA52987E0` |
| policy seed 1 | `CF4973D1DA09317895945B61A20BF28E66860F6F3DE7E67F8311053AAD097215` |
| candidato seed 2 | `A1C1ED88FCA6122664FC87DDF9BD3E9EF821EB1BB6420A843694D2264138A5A0` |
| policy seed 2 | `B28C21C296E5F29E805C65A8E917CAC2FEDFB31B08BC5C6ADFE2E9B210E12825` |
| analisi coppia | `5AE332E02DC36BAA57FCC98EC1E36F141DA83DB99FEBEC5AB2CCE12271F7E988` |
| audit Call/Fold | `7EADEF016C5FC0301376B861E93F112B37529501EEF7A7B53870D6768F4B6451` |
| decomposizione TV | `26D57D3C8446AEBB918681C34ACCF0C46829FAE38A908A80272E691B88B7FD50` |

## Decisione

V17 resta una modalità diagnostica disattivata per default. Non sostituisce V13 nel viewer.

Il prossimo candidato R6 deve mantenere i 2M postflop di V15 ma aggiungere una fase separata di
refinement del solo preflop contro la policy postflop media congelata. L'obiettivo è rimuovere la
massa storica Call/Fold ormai inferiore e ridurre la TV senza raddoppiare il costo degli all-in
esatti. Il refinement deve prima acquisire una semantica batch verificata; non verrà attivato
aggirando il controllo corrente.

## Decisione successiva a V18

V18 ha eseguito il refinement previsto, ma ha peggiorato la TV fra seed e l'audit EV senza
raggiungere il miglioramento esterno del `10%`. V17 torna quindi a essere la baseline corrente:
la strategia media coincide con V15 e questo formato conserva anche la policy corrente completa.

Il nuovo gate e la proposta tecnica non autorizzata sono in
[V18_REJECTION_AND_NEXT_GATE_DECISION_2026-09-13.md](V18_REJECTION_AND_NEXT_GATE_DECISION_2026-09-13.md).
