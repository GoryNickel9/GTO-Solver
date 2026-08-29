# Rapporto di completamento — Fase 1

## 1. Identificazione

| Campo | Valore |
|---|---|
| Progetto | GTOSD — GTO Solver Short Deck |
| Roadmap | `ROADMAP_HU_SHORT_DECK_GTO_SOLVER.md` |
| Milestone | Fase 1 — Carte, fixed-point e regole |
| Data di chiusura | 27 luglio 2026 |
| Stato | **Completata** |
| Linguaggio | C++20 |
| Toolchain primaria | MSVC 19.51.36248.0, Visual Studio 18 Community |
| Toolchain sanitizer overflow | GCC tramite WSL, `-fsanitize=undefined` |
| Piattaforma verificata | Windows x64; compilazione/test UBSan Linux tramite WSL |

## 2. Decisione del gate

Il gate F1 è superato.

| Criterio roadmap | Esito | Evidenza |
|---|---|---|
| 100% test regole verdi | Superato | Sezioni 22.2, 22.4, 22.5 e 22.6 coperte; CTest 2/2 |
| Zero overflow UBSan nei fuzz/property test | Superato | GCC UBSan con `-fno-sanitize-recover=all`; 100.000 transizioni completate |
| Somma payoff corretta senza rake | Superato | Test HU: somma payoff `0` |
| Somma payoff corretta con rake | Superato | Test HU: somma payoff `-rake` |
| Nessun pot o stack negativo | Superato | Validazione a ogni transizione randomizzata |
| Serializzazione deterministica | Superato | Confronto ripetuto per ogni transizione e golden prefix versionato |

Questo gate certifica regole e chip accounting. Non certifica evaluator,
albero postflop, isomorfismi, best response, NashConv o convergenza GTO.

## 3. Tracciabilità delle attività F1

| # | Attività della roadmap | Stato | Implementazione |
|---:|---|---|---|
| 1 | `CardId` compatto | Completata | Tipo da 1 byte, dominio fisico `0..35` |
| 2 | Deck mask a 64 bit | Completata | Mask carte usate e mask delle carte rimanenti |
| 3 | Parser e formatter | Completata | Formato canonico `Rs`, per esempio `As`, `Tc`, `6h` |
| 4 | `Money` fixed-point | Completata | `int64_t`, 10.000 unità per ante |
| 5 | Percentuali pot e range | Completata | `PotPercentage` `0..100.000`; `RangeWeight` `0..10.000` |
| 6 | Stato pubblico HU | Completata | Stato versionabile, deterministico, con sei slot player |
| 7 | Ante e button blind | Completata | CO 1 ante, BTN 2 ante, pot root 3 ante, CO primo |
| 8 | Legal action generator | Completata | Fold/check/call/bet/raise/all-in |
| 9 | Min-bet e min-raise | Completata | Size illegali scartate, mai corrette silenziosamente |
| 10 | Add/Go all-in | Completata | Soglia stretta e preservazione delle azioni passive |
| 11 | Rake e cap | Completata | Percentuale, cap, minimum pot, no-flop-no-drop |
| 12 | Terminal fold | Completata | Vincitore, uncalled return e stato terminale |
| 13 | Distribuzione pot e split | Completata | Rake prima dello split e resto deterministico |
| 14 | Sei player slot e active mask | Completata | Array da sei; percorso HU specializzato a due player |

## 4. Carte

### 4.1 Dominio

| Proprietà | Contratto |
|---|---|
| Rank | `6, 7, 8, 9, T, J, Q, K, A` |
| Semi | `c, d, h, s` |
| Numero carte | 36 |
| Intervallo `CardId` | `0..35` |
| Dimensione `CardId` | 1 byte |
| Mask | 64 bit; vengono usati i bit `0..35` |

Il mapping è deterministico:

```text
CardId = rank_index * 4 + suit_index
```

Il parser rifiuta:

- rank `2..5`;
- semi non riconosciuti;
- stringhe con lunghezza diversa da due;
- notazione con maiuscole/minuscole non canonica;
- carte duplicate nelle collezioni validate.

### 4.2 Invarianti verificate

| Invariante | Prova |
|---|---|
| Tutti i `CardId` sono unici | Popcount del mazzo completo uguale a 36 |
| Sette dead card lasciano 29 carte | Popcount della remaining mask uguale a 29 |
| Round-trip binario/testuale | Tutti i 36 valori |
| Permutazioni del mazzo | 1.000 shuffle deterministici, 36 carte per shuffle |
| Duplicati | Errore `DuplicateCard` |

## 5. Aritmetica fixed-point

### 5.1 Tipi

| Tipo | Storage | Dominio |
|---|---:|---:|
| `Money` | `int64_t` | Valori non negativi |
| `RangeWeight` | `uint16_t` | `0..10.000` basis point |
| `PotPercentage` | `uint32_t` | `0..100.000` basis point |

Una ante equivale a:

```text
1 ante = 10.000 unità
```

La rappresentazione pubblica usa sempre quattro decimali. Esempio:

```text
123456 unità = 12.3456 ante
```

### 5.2 Moltiplicazione percentuale

Il calcolo usa divisione in quoziente e resto:

```text
value = whole * divisor + remainder
result = whole * basis_points
       + round_half_up(remainder * basis_points / divisor)
```

Questo evita un prodotto intermedio `value × basis_points` non rappresentabile
in 64 bit. Ogni addizione e sottrazione di chip nel path production passa
attraverso funzioni checked.

| Caso | Risultato verificato |
|---|---:|
| Pot 100, size 50% | 50 ante |
| Pot 3, size 33,33% | 0,9999 ante |
| Pot 150, call 50, raise 50% | Azione totale 150 ante |
| Overbet 1001% | Rifiutata |
| Percentuale negativa | Rifiutata |
| Prodotto vicino a `INT64_MAX` | Calcolato senza overflow |
| Prodotto realmente fuori range | Errore `Overflow` |

## 6. Stato pubblico

`PublicState` contiene:

```text
street
status
board_mask
player_count
player_to_act
initial_pot
pot
returned_uncalled
current_bet
last_full_raise_increment
initial_pot_contributions[6]
remaining_stacks[6]
committed_this_street[6]
committed_total[6]
returned_uncalled_by_player[6]
active_players_mask
all_in_players_mask
acted_players_mask
terminal_winner_mask
raise_count_this_street
```

### 6.1 Stati della mano

| Stato | Significato |
|---|---|
| `InProgress` | Esiste un player attivo, non all-in, che deve agire |
| `StreetComplete` | Tutti i player azionabili hanno agito e pareggiato |
| `Folded` | È rimasto un solo player attivo |
| `AllInRunout` | Betting chiuso con almeno un all-in prima dello showdown |
| `Showdown` | Betting river completato |

### 6.2 Invarianti validate

La funzione `validate_state()` controlla a ogni transizione:

1. `player_count` compreso fra 2 e 6;
2. mask limitate ai player esistenti;
3. board mask limitata alle 36 carte Short Deck;
4. actor valido, attivo e non all-in;
5. player all-in con stack esattamente zero;
6. player attivo con stack zero marcato all-in;
7. commitment di street non superiore al commitment totale;
8. restituzione per player non superiore al commitment lordo;
9. somma delle quote del pot iniziale uguale al pot iniziale;
10. somma delle restituzioni per player uguale alla restituzione totale;
11. conservazione delle chip:

```text
initial_pot + sum(committed_total)
  == pot + returned_uncalled
```

12. massimo quattro raise non all-in per street;
13. commitment pareggiati negli stati di chiusura;
14. winner mask coerente col terminal fold.

## 7. Regole Heads-Up

### 7.1 Root preflop

| Voce | Valore |
|---|---:|
| Ante CO | 1 ante |
| Ante BTN | 1 ante |
| Button blind BTN | 1 ante |
| Commitment CO | 1 ante |
| Commitment BTN | 2 ante |
| Pot root | 3 ante |
| Importo da chiamare CO | 1 ante |
| Primo attore | CO |

Il call del CO non chiude il preflop. Il BTN conserva l’opzione di check o
raise. Il preflop si chiude dopo il successivo check del BTN oppure dopo la
risoluzione di una linea aggressiva.

### 7.2 Chiusura street

| Sequenza | Esito |
|---|---|
| Check–check flop/turn | `StreetComplete` |
| Bet–call flop/turn | `StreetComplete` |
| Raise–call flop/turn | `StreetComplete` |
| Check–check o call river | `Showdown` |
| All-in–call prima del river | `AllInRunout` |
| Fold | `Folded` |

`advance_street()`:

- avanza di una sola street;
- rimette CO come primo attore;
- azzera commitment, bet, acted mask e raise count della street;
- conserva pot, stack e commitment totali.

Le carte chance non vengono distribuite in F1: l’enumerazione fisica di turn e
river appartiene alla Fase 3.

## 8. Azioni legali

### 8.1 Azioni passive

| Condizione | Azioni |
|---|---|
| `amount_to_call == 0` | Check |
| `amount_to_call > 0` | Fold e Call |
| Stack minore del call | Call stack-capped con `AllInKind::Call` |

Fold non viene offerto quando il check è gratuito. Check non viene offerto
quando esiste un importo da chiamare.

### 8.2 Bet e raise

Bet:

```text
bet = round_half_up(current_pot × size)
```

Raise:

```text
pot_after_call = current_pot + amount_to_call
raise_increment = round_half_up(pot_after_call × size)
total_action = amount_to_call + raise_increment
```

Una size:

- sotto il minimum bet viene scartata;
- sotto il last full raise increment viene scartata;
- uguale a un’altra size dopo il rounding viene deduplicata;
- superiore allo stack viene convertita in all-in;
- non viene mai aumentata silenziosamente al minimo legale.

### 8.3 Raise depth

`raise_count_this_street` conta soltanto i `Raise` non all-in:

- il primo bet non consuma il limite;
- un all-in non consuma il limite;
- `raise_depth = 0` consente il bet iniziale ma vieta il raise;
- `raise_depth = 4` vieta il quinto raise non all-in.

Le size possono essere uniformi a tutte le profondità oppure dichiarate con
un calendario `sizes_by_raise_count_bp`: indice zero per il primo raise,
indice uno per il re-raise e così via. Il calendario, se presente, copre
esattamente la profondità configurata ed è serializzato nel tree config; la
lista uniforme resta il fallback compatibile con i file precedenti.

### 8.4 Add/Go all-in

La soglia usa:

```text
push_increment = stack_before_action - amount_to_call
push_percent = push_increment / pot_after_call
trigger = push_percent < threshold
```

La disuguaglianza è stretta.

| Push | Soglia | Trigger verificato |
|---:|---:|---|
| 99,99% | 100% | Sì |
| 100,00% | 100% | No |
| 100,01% | 100% | No |

| Modalità | Comportamento |
|---|---|
| Disabled | Nessun push automatico |
| Add | Mantiene le size normali e aggiunge il push |
| Go | Rimuove bet/raise normali e conserva il push |

Fold, check e call non vengono mai rimossi da `Go`.

### 8.5 Applicazione sicura

`apply_action()` rigenera le azioni legali e rifiuta ogni azione non presente
nella firma generata. Non è quindi possibile applicare direttamente una size
forgiata, un check contro una bet o un raise oltre la profondità.

## 9. Uncalled, rake e payoff

### 9.1 Restituzione dell’uncalled

Quando una parte del commitment non viene chiamata:

1. viene rimossa dal pot;
2. viene restituita allo stack dell’aggressore;
3. viene registrata nel totale e per player;
4. resta nel commitment lordo per preservare l’audit contabile;
5. non partecipa al rake.

Caso root verificato:

```text
Pot prima del fold CO: 3 ante
Button blind non chiamato restituito: 1 ante
Called pot: 2 ante
```

### 9.2 Rake

| Campo | Implementazione |
|---|---|
| Enabled | Boolean |
| Percentuale | `RangeWeight`, massimo 100% |
| Cap | `Money` |
| Minimum pot | `Money` |
| No-flop-no-drop | Applicato sui terminali preflop |

Il rake viene calcolato sul called pot dopo la restituzione dell’uncalled e
prima dello split.

### 9.3 Split e resto

Il pot netto viene diviso in unità fixed-point. L’eventuale resto viene
assegnato deterministicamente ai winner seat in ordine crescente: CO, BTN,
quindi gli slot successivi.

Esempio:

```text
10 unità / 3 winner = 4, 3, 3
```

### 9.4 Payoff

Per ogni player:

```text
payoff =
    payout
  + returned_uncalled
  - committed_total
  - initial_pot_contribution
```

Ne deriva:

```text
sum(payoff) = -rake
```

Senza rake la somma è zero. Con rake attivo la somma è esattamente il negativo
del rake, fino alla singola unità da `0,0001` ante.

Per uno stato postflop creato direttamente, il pot iniziale viene attribuito
50/50. Se il numero di unità è dispari, l’unità residua viene attribuita al CO
in modo deterministico.

## 10. Serializzazione deterministica

La serializzazione pubblica ha prefisso:

```text
GTOSD-PUBLIC-1
```

Tutti i campi scalari e tutti i sei elementi di ogni array vengono emessi in
ordine fisso, come interi in unità canoniche. La serializzazione:

- non dipende dalla locale;
- non usa indirizzi o ordine di container hash;
- cambia quando cambia lo stato pubblico;
- produce byte identici per lo stesso stato.

Il formato è un audit format F1. Non sostituisce il futuro container `.gtsd`.

## 11. Copertura test

### 11.1 Sezione 22.2 — carte

| Test roadmap | Copertura |
|---|---|
| 36 carte uniche | Sì |
| Parsing `As` | Sì |
| Rank `2..5` rifiutati | Sì, tutti e quattro i semi |
| Duplicati | Sì |
| Remaining deck dopo 7 carte = 29 | Sì |
| Round-trip tutti i CardId | Sì |
| Permutazioni casuali | 1.000 permutazioni complete |

### 11.2 Sezione 22.4 — fixed-point e size

Tutti i casi tabellari, i tre confini della soglia all-in, Add, Go,
deduplicazione, min-bet, min-raise e overflow sono coperti.

### 11.3 Sezione 22.5 — rake

Sono coperti:

- disabled;
- 5% con cap;
- 5% senza raggiungere il cap;
- fold con uncalled;
- no-flop-no-drop;
- rake preflop esplicitamente abilitato;
- rake prima dello split;
- resto deterministico;
- somma payoff HU.

### 11.4 Sezione 22.6 — legal actions

La matrice minima è stata eseguita integralmente:

```text
4 street
× 2 player
× 2 scenari
× 5 raise depth
× 3 all-in mode
= 240 combinazioni
```

Per ogni azione generata sono stati verificati:

- applicabilità;
- validità dello stato risultante;
- stack non negativi;
- commitment non negativi;
- coerenza delle azioni passive/aggressive.

### 11.5 Property test

| Campo | Valore |
|---|---:|
| Seed | `0x5EEDF1` |
| Transizioni | 100.000 |
| Size | 25%, 50%, 100% |
| Raise depth | 4 |
| All-in mode | Add |
| Soglia | 100% |
| Start | Flop, pot 10, stack 100 |

Ogni iterazione:

1. genera le azioni legali;
2. seleziona un’azione tramite PRNG deterministico;
3. applica l’azione;
4. valida lo stato;
5. controlla chip e stack;
6. controlla la serializzazione;
7. avanza la street o avvia una nuova mano quando necessario.

## 12. Risultati misurati

| Profilo | Risultato | Tempo CTest osservato |
|---|---|---:|
| MSVC Release `/W4 /WX` | 2/2 test verdi | 3,54 s |
| MSVC Debug `/W4 /WX` | 2/2 test verdi | 28,10 s |
| MSVC AddressSanitizer | 2/2 test verdi | 28,80 s |
| GCC UBSan `-fno-sanitize-recover=all` | Entrambi gli executable verdi | Comando completo 27,9 s |

Output della suite F1:

```text
F1_RULE_TESTS=PASS
assertions=641528
randomized_transitions=100000
parameterized_combinations=240
```

Il numero di asserzioni include controlli eseguiti dentro la matrice e il
property test; non rappresenta 641.528 test case CTest indipendenti.

## 13. Comandi di riproduzione

### 13.1 Release Windows

```powershell
cmake -S . -B out/build/windows-release-vs `
  -G "Visual Studio 18 2026" -A x64 `
  -DGTOSD_WARNINGS_AS_ERRORS=ON
cmake --build out/build/windows-release-vs --config Release --parallel 4
ctest --test-dir out/build/windows-release-vs -C Release --output-on-failure
```

### 13.2 Debug Windows

```powershell
cmake -S . -B out/build/windows-debug-vs `
  -G "Visual Studio 18 2026" -A x64 `
  -DGTOSD_WARNINGS_AS_ERRORS=ON
cmake --build out/build/windows-debug-vs --config Debug --parallel 4
ctest --test-dir out/build/windows-debug-vs -C Debug --output-on-failure
```

### 13.3 UBSan WSL

```bash
g++ -std=c++20 -O1 -g \
  -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror \
  -fsanitize=undefined -fno-sanitize-recover=all \
  -Iinclude \
  libs/core/src/cards.cpp \
  libs/core/src/money.cpp \
  libs/core/src/game.cpp \
  tests/phase1_tests.cpp \
  -o /tmp/gtosd_phase1_ubsan

/tmp/gtosd_phase1_ubsan
```

La CI contiene anche un job Ubuntu UBSan permanente.

## 14. File principali

| File | Responsabilità |
|---|---|
| `include/gtosd/core/cards.hpp` | Tipi e API carte |
| `libs/core/src/cards.cpp` | Parser, formatter e mask |
| `include/gtosd/core/money.hpp` | Money e percentuali tipizzate |
| `libs/core/src/money.cpp` | Aritmetica checked e formatting |
| `include/gtosd/core/game.hpp` | Stato, azioni, rake e settlement |
| `libs/core/src/game.cpp` | Regole e transizioni |
| `tests/phase1_tests.cpp` | Suite completa F1 |
| `tests/core_tests.cpp` | Regressioni core e avvio F2 |
| `.github/workflows/ci.yml` | Debug/Release Windows e UBSan |

## 15. Confini della milestone

Le seguenti funzioni non appartengono alla Fase 1 e restano intenzionalmente
fuori da questo gate:

| Funzione | Fase prevista |
|---|---:|
| Best-five-of-seven certificato contro oracle | F2 |
| Winner mask exhaustive/nightly | F2 |
| Chance turn e river | F3 |
| Public tree completo | F3 |
| Isomorfismo globale dei semi | F4 |
| CFR, CFR+, DCFR | F5 |
| Best response infoset-aware | F5 |
| NashConv | F5 |
| Checkpoint e soluzione `.gtsd` | F7–F8 |

Di conseguenza nessuna strategia viene chiamata “GTO” al termine di F1.

## 16. Prossimo ingresso

La Fase 2 può partire usando direttamente:

- `CardId` e le mask validate;
- `Money` e lo split fixed-point;
- `Settlement` per trasformare il winner mask in payoff;
- gli errori tipizzati e la policy senza fallback silenziosi.

Il gate F2 richiederà oracle indipendente, confronto exhaustive delle categorie,
input malformati, winner mask 2–6 player e un milione di deal deterministici
nightly.
