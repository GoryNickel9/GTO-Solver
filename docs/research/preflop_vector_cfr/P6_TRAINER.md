# P6 — Trainer con campionamento del board

Data: 2026-09-16. Branch di fase: `feature/preflop-blueprint-p6-trainer`. Esito del gate:
**PASS** (§7): oracolo, determinismo e ripresa PASS; HU10 completo raggiunge la regola D3 con la stima
campionata a 1.000 flop. Lo stimatore prescritto da P6.3/§5 era chiaroveggente ed è stato
sostituito (§2, erratum alla roadmap).

## 1. Cosa è stato prodotto

| Componente | File | Contenuto |
|---|---|---|
| Trainer | `include/gtosd/preflop_blueprint/trainer.hpp`, `libs/preflop_blueprint/src/trainer.cpp` | iterazione = batch di `B` board campionati (o lista esplicita pesata in modalità esatta), un passaggio per giocatore, strategia letta da uno snapshot dei regret (Simultaneous: uno snapshot per iterazione; Alternating: nuovo snapshot prima del secondo giocatore), incrementi scritti direttamente nelle celle con il peso dell'iterazione; Linear (`× t`) o DCFR `α/β/γ` (sconto una volta per iterazione); partizione dell'albero in sottoalberi disgiunti indipendente dal numero di thread, parte alta seriale (reach top-down, valori bottom-up), unità in parallelo con un solo scrittore per cella; checkpoint atomico con checksum e identità; telemetria (tempo, nodi, memoria di processo) |
| Best response fisica | `include/gtosd/preflop_blueprint/best_response.hpp`, `libs/preflop_blueprint/src/best_response.cpp` | valutatore di EV e best response della strategia media con la struttura informativa del gioco fisico (§2): universi di mani vive per prefisso (preflop 630, flop 528, turn 496, river 465), reach avversaria propagata per strada, valori aggregati sulle carte future prima del massimo, river con i kernel P5; gruppi di board per flop, pesi, sottoinsiemi di mani, errore standard sui gruppi, parallelo per flop |
| Hook esatti | `TrainingBoards`, `HandSubsets` | lista di board con pesi (tutti i board a ogni iterazione, o campionati dalla lista) e sottoinsiemi uniformi di mani per giocatore, con `P(h)` e `P(o|h)` calcolati per board |
| Test | `tests/preflop_blueprint_trainer_tests.cpp` (317.488 asserzioni) | oracolo `FiniteGame` a bucket, determinismo, ripresa, best response fisica contro il `FiniteGame` lossless, curva |
| Eseguibile | `benchmarks/preflop_blueprint_train.cpp` | training con telemetria JSON, checkpoint, ripresa, valutazione campionata `--eval-flops`, modalità diagnostica a board fissi (`--fixed-boards`, `--permute-suits`) |

Contratti implementati (per board `B` di peso `w_B`, eroe con mano `h`):

```text
R[n][b(h)][a] += w_B · P(h) · P(o|h) · (v_a[h] − v[h])      v = valori controfattuali dei kernel P5
S[n][b(h)][a] += w_B · P(h) · reach_eroe[h] · σ(a | n, b(h))
```

con `P(h) = 1/465` e `P(o|h) = 1/406` nel gioco completo. I valori `v` contengono già la reach
avversaria e i pagamenti in ante; il fattore `cf_reach` della roadmap §5 è quindi dentro `v` e
non viene moltiplicato una seconda volta (decisione 25). Linear moltiplica gli incrementi
dell'iterazione `t` per `t`; DCFR sconta prima degli incrementi: `R⁺ × t^α/(t^α+1)`,
`R⁻ × t^β/(t^β+1)`, `S × (t/(t+1))^γ` con `t` l'iterazione precedente.

## 2. Best response fisica: errore della roadmap e correzione

**Cosa prescriveva la roadmap.** P6.3 e §5: best response per board con i kernel P5 in modalità
`max`, media sui board; "la scomposizione per board è esatta perché il board è pubblico e nessun
information set attraversa due board".

**Perché è sbagliato.** Vale per l'EV della strategia media (lineare nei board), non per la best
response. Gli information set del responder al preflop (mano), al flop (mano + flop) e al turn
(mano + flop + turn) attraversano tutti i board che condividono il prefisso: una scelta per mano
*e per board completo* ai nodi sopra il river è quella di un responder che conosce turn e river
prima di agire. Il valore così ottenuto domina la vera best response e non tende a zero neppure
quando il gioco astratto è risolto.

**Sintomi misurati con lo stimatore chiaroveggente** (HU10 ridotto, DCFR alternato, capacità
200/500/1.000, board fissi, best response esatta sulla lista): 1 board `1·10⁻⁴` a (un solo board:
nessuna carta futura, la chiaroveggenza è vuota); 4 board 0,105 a; 8 board 0,163 a; 64 board
0,473 a; HU10 completo campionato 0,92–1,03 a dopo 1.500 iterazioni, con la stima piatta dalla
prima valutazione. La prima diagnosi ("pavimento dell'astrazione") era errata: la stessa curva
con capacità 50/100/200 era quasi identica, e i regret del trainer coincidono con
`solve_finite_game` entro `1,7·10⁻¹³`.

**Correzione (decisione 30).** Ai nodi del responder i valori delle azioni sono aggregati sulle
carte ancora da distribuire prima del massimo:

```text
river:   v[h] = max_a v_a[h]                                   per board (kernel P5, modalità max)
turn:    v[h] = max_a Σ_river P(river | h, turn) · v_a[h, river]
flop:    v[h] = max_a Σ_turn  P(turn | h, flop)  · v_a[h, turn]
preflop: v[h] = max_a Σ_flop  P(flop | h)        · v_a[h, flop]
```

L'avversario segue la strategia media, i cui bucket dipendono solo dalle carte già distribuite:
il valore di una strada è lineare nei valori delle sue foglie, quindi la somma sui runout può
essere fatta foglia per foglia senza memorizzare nulla per runout sopra il river. Il valutatore
(`evaluate_best_response`) lavora per gruppo di flop: universo delle 528 mani vive al flop,
reach avversaria propagata per strada, per ogni turn l'universo di 496 mani, per ogni river il
`BoardContext` P5 e la traversata dal nodo di chance turn→river in modalità max (best response) e
in modalità strategia media (EV); i terminali all-in del turn e del flop usano `showdown_masses`
sul board completo; i fold per strada usano la massa disgiunta con `P(o|h)` dell'universo. Le
probabilità condizionali sono i pesi dei board compatibili con la mano (`w_B / Σ` sui runout del
prefisso); nel gioco fisico completo `P(river | h, turn) = 1/31`, `P(turn | h, flop) = 1/32`.

**Campionamento (decisione 31).** La stima campiona `M` flop fisici dal catalogo ed enumera tutti
i 33 × 32 runout di ciascuno (1.056 board per flop): la best response al flop e al turn richiede
tutti i runout del prefisso. Errore standard sui gruppi di flop (valore per flop della best
response scelta e della strategia media, pesi uguali), intervallo `1,96 · SE` sul massimo
guadagno: la regola D3 confronta `stima + semiampiezza` con 0,03 a. Una lista esplicita di board
è raggruppata per flop e valutata esattamente con i pesi.

**Bias di selezione al preflop (decisione 32).** Dal flop in poi la best response è esatta per
ogni flop campionato, ma al preflop la scelta per combo massimizza medie su `M` flop e viene
valutata sugli stessi flop: la stima naive è distorta verso l'alto di un termine che scala come
`1/√M` (massimo di rumore su più azioni). Misurato sul checkpoint HU10 ridotto (DCFR, 2.000
iterazioni), massimo guadagno naive per `M = 5 / 10 / 20 / 40 / 80 / 160` flop: 0,239 / 0,171 /
0,093 / 0,066 / 0,045 / 0,037 a, cioè `naive · √M` ≈ 0,53 / 0,54 / 0,42 / 0,42 / 0,40 / 0,46:
la stima è quasi interamente bias e il guadagno vero è compatibile con zero. Una stima cross-fit
(scelta preflop su metà dei flop, valore sull'altra metà) è risultata negativa a ogni `M`
(−0,06 … −0,01 a): una politica preflop pura scelta su pochi flop perde contro la strategia
media più di quanto la best response vera guadagni; scartata. Il valutatore riporta quindi due
numeri: la stima naive (limite superiore in attesa) e il **limite inferiore senza selezione**:
eroe con la strategia media al preflop e best response esatta dal flop in poi, non distorto e mai
sotto l'EV. Con tutti i flop enumerati (P7) i due coincidono con la best response esatta. Per la
regola D3 il gate usa la stima naive più semiampiezza, che richiede `M ≈ 1.000` flop perché il
bias scenda sotto 0,015 a (§7).

**Verifica.** `FiniteGame` lossless del gioco ridotto (information set = mano + carte pubbliche
distribuite: 4.008 information set, 61.672 nodi) con il profilo sollevato dalla strategia media:
EV, best response e nashconv del valutatore coincidono con `calculate_nash_conv` entro `1e-9`
(nashconv 0,0931106 in entrambi); la stima del trainer coincide con il report del valutatore; la
best response fisica domina quella del gioco astratto a bucket.

## 3. Verifiche eseguite

| Verifica | Esito |
|---|---|
| Oracolo esatto: gioco ridotto HU10 (albero ridotto, 3 board con molteplicità 1/2/3, 6 combo per giocatore su carte disgiunte, information set = bucket P3) costruito come `FiniteGame` (61.672 nodi, 1.292 information set) e risolto con `solve_finite_game` Linear per 25 iterazioni; trainer in modalità esatta, Simultaneous, Linear, 2 thread: regret cumulati uguali entro `1,7·10⁻¹³`, somme di strategia entro `1,4·10⁻¹⁴`, strategia media entro `1e-9`; celle fuori dal gioco ridotto a zero; valore del profilo uguale a `calculate_nash_conv` entro `1e-9` | PASS |
| Best response fisica = `calculate_nash_conv` del `FiniteGame` lossless entro `1e-9` (EV, BR, nashconv, entrambi i giocatori); stima del trainer = report del valutatore; fisica ≥ astratta | PASS |
| Bit-identità dello stato con 1, 2, 4, 8 thread; stato identico con partizione a 1 unità, 8 unità (default) e 58 unità | PASS |
| Ripresa da checkpoint (DCFR, Alternating): tabelle e fingerprint identici al run continuo; checkpoint corrotto e checkpoint di altra identità rifiutati | PASS |
| Curva: HU10 ridotto campionato, 4 flop × 1.056 board: la stima decresce da 2,40 a (uniforme) a 0,33–0,41 a fra 20 e 60 iterazioni | PASS |
| `ctest -L p6`: test (187 s) e smoke dell'eseguibile (14 s) | PASS |
| Suite `preflop_blueprint` P0–P6 | PASS: 16/16 in 318 s (`ctest -L preflop_blueprint`, Release, dopo il rebuild finale) |

## 4. Costo

HU10 ridotto (571 nodi): 0,19 s per iterazione con `B = 32`, 8 thread; HU10 completo (2.059 nodi):
0,31 s per iterazione con `B = 32`, 8 thread (32 unità, 10 nodi in alto), 0,26 s con 50/100/200 e
0,34 s con 500/1.000/2.000; memoria di processo in training 217 / 245 / 278 MB per le tre
capacità; stato R+S 7,0 / 34,1 / 68,6 MB. Valutazione non chiaroveggente (2 giocatori × 2 modi,
traversate river dai nodi di chance turn→river, 8 thread): HU10 completo circa 42 ms per board per
thread, cioè 93–122 s per 20 flop (21.120 board) e 338 s per 60 flop; HU10 ridotto circa 8 ms per
board per thread (22 s per 20 flop, 195 s per 160 flop); memoria in sola valutazione 105–167 MB.
Su 2.000 iterazioni con valutazione ogni 250 la valutazione costa più del training (882 s contro
618 s). La passata esatta di P7 su tutti i 573 flop canonici con i runout (605.088 board) costa
quindi circa 50 min su HU10 completo, meno della stima campionata con `M = 1.000` (§7).

## 5. Curve HU10 con lo stimatore corretto

> **Nota (2026-09-16).** Dal pomeriggio del 2026-09-16 le fixture HU10 hanno una sola size preflop
> (open 5 a) e nessuna size di risposta (contro l'open solo fold, call e all-in): gli alberi e i
> conteggi HU10 di questa sezione valgono per le fixture di allora (diario, P8 §9 e §10).

Tutti i run: 2.000 iterazioni, `B = 32`, 8 thread, stima naive con `M = 20` flop ogni 250 iterazioni
(stessi flop di valutazione in ogni run, stesso seme di training). Massimo guadagno naive in ante
per mano (semiampiezza 0,04–0,10 a):

| Iterazione | 250 | 500 | 750 | 1.000 | 1.250 | 1.500 | 1.750 | 2.000 |
|---|---|---|---|---|---|---|---|---|
| HU10 completo, DCFR alternato, 200/500/1.000 | 0,099 | 0,108 | 0,084 | 0,091 | 0,108 | 0,137 | 0,122 | 0,085 |
| HU10 ridotto, DCFR alternato, 200/500/1.000 | 0,100 | 0,108 | 0,084 | 0,091 | 0,108 | 0,137 | 0,122 | 0,085 |
| HU10 completo, DCFR alternato, 50/100/200 | 0,098 | 0,108 | 0,085 | 0,091 | 0,109 | 0,140 | 0,125 | 0,087 |
| HU10 completo, DCFR alternato, 500/1.000/2.000 | 0,104 | 0,111 | 0,087 | 0,093 | 0,111 | 0,140 | 0,126 | 0,087 |
| HU10 completo, Linear simultaneo, 200/500/1.000 | 0,182 | 0,237 | 0,195 | 0,188 | 0,179 | 0,243 | 0,167 | 0,183 |

EV del CO 0,133–0,144 a in tutti i run DCFR (0,136 a a 2.000 iterazioni). Le quattro curve DCFR
coincidono alla terza cifra: la stima a 20 flop misura il rumore di selezione comune (§2, decisione
32), non la strategia. Il Linear simultaneo resta sopra di un fattore 2: lo schema raccomandato
per il blueprint è DCFR alternato (decisione 26 confermata dai dati).

Rivalutazione dei checkpoint finali con `--eval-only`, `M = 60` flop (63.360 board), stessi flop per
tutti (RNG di valutazione ripristinato dal checkpoint):

| Checkpoint (2.000 iterazioni) | naive max gain | semiampiezza | limite inferiore (media preflop + BR dal flop) | EV CO |
|---|---|---|---|---|
| DCFR alternato, 200/500/1.000 | 0,0454 a | 0,0186 a | 0,0013 a (CO), 0,0009 a (BTN) | 0,1362 a |
| DCFR alternato, 50/100/200 | 0,0477 a | 0,0188 a | 0,0019 a, 0,0018 a | 0,1360 a |
| DCFR alternato, 500/1.000/2.000 | 0,0462 a | 0,0190 a | 0,0010 a, 0,0009 a | 0,1363 a |
| Linear simultaneo, 200/500/1.000 | 0,1431 a | 0,0455 a | 0,0031 a, 0,0024 a | 0,1349 a |

Il limite inferiore (best response esatta dal flop in poi contro la strategia media, non distorto)
è di 1–3 millesimi di ante: l'errore di astrazione postflop su HU10 è trascurabile rispetto a D3.
La parte preflop della best response non è misurabile a `M = 60` sotto il bias di 0,35–0,37/√M.

Diagnostica a board fissi (HU10 ridotto, DCFR alternato, `B = 8`, lista esatta, valutazione esatta
sulla lista): 1 board 0,014 a a 100 iterazioni (regola D3 soddisfatta, run fermato); 4 board
0,014 a; 8 board 0,025 a; 64 board 0,111 a, tutti piatti fra 100 e 500 iterazioni (chiaroveggente:
`1·10⁻⁴` / 0,105 / 0,163 / 0,473 a). Il gioco ristretto a pochi board è risolto esattamente dal
trainer, ma il suo valore di best response non misura l'astrazione del gioco completo: con un solo
runout per flop, mani "simili" per i bucket hanno valori di flop molto diversi e il responder le
distingue; nel gioco completo il valore di flop è una media su 930 runout e il limite inferiore
sopra mostra che la perdita è di millesimi. La modalità a board fissi resta utile per verificare la
convergenza del trainer, non per stimare la perdita di astrazione.

## 6. Confronto di capacità (§3.4, secondo candidato)

| Tabelle (flop/turn/river) | Stato R+S | s/iterazione | naive `M = 60` | limite inferiore | Costruzione P3 |
|---|---|---|---|---|---|
| 50/100/200 | 7,0 MB | 0,26 | 0,0477 a | 0,0019 a | 272 s |
| 200/500/1.000 (default) | 34,1 MB | 0,31 | 0,0454 a | 0,0013 a | 1.294 s |
| 500/1.000/2.000 | 68,6 MB | 0,34 | 0,0462 a | 0,0010 a | 2.561 s |

La capacità cambia solo il limite inferiore (perdita postflop 1,9 → 1,3 → 1,0 millesimi di ante),
tutto sotto la risoluzione della regola D3; il default 200/500/1.000 è confermato e il secondo
candidato di §3.4 non è necessario per HU10. Le tabelle 500/1.000/2.000 di CO40 costerebbero
715 MB di stato (P4) senza beneficio misurabile qui.

## 7. Esito del gate

| Criterio (roadmap P6) | Esito |
|---|---|
| Oracolo esatto (`FiniteGame`, regret e strategia media entro `1e-9`) | PASS |
| Determinismo (bit-identità 1/2/4/8 thread e partizioni 1/8/58 unità) | PASS |
| Ripresa da checkpoint uguale al run continuo | PASS |
| HU10 completo raggiunge D3 con tempo e memoria riportati | PASS: checkpoint DCFR alternato 200/500/1.000 a 2.000 iterazioni (training 618 s, 245 MB); stima naive con `M = 1.000` flop (1.056.000 board, 4.796 s, 98 MB): massimo guadagno 0,0187 a più semiampiezza 0,0079 a = 0,0266 a ≤ 0,03 a; guadagni [0,0108, 0,0187] a, limite inferiore senza selezione [0,0014, 0,0009] a; `PREFLOP_BLUEPRINT_TRAIN=CONVERGED` |
| Report con la curva exploitability/tempo | §5 |

La regola D3 è applicata come scritta (stima più semiampiezza sul massimo guadagno). La stima a
`M = 1.000` contiene ancora un bias di selezione di circa 0,01 a (`naive · √M ≈ 0,4–0,6`): la
exploitability vera del checkpoint è compresa fra il limite inferiore (0,0014 a) e la stima naive
(0,0187 a), cioè fra lo 0,05 % e lo 0,6 % del piatto iniziale; P7 la misurerà esattamente con la
passata su tutti i flop canonici, che costa meno della stima campionata a 1.000 flop (§4).
Tempo totale del gate su HU10 completo: 10 min di training più 80 min di valutazione finale;
memoria massima 278 MB (training con 500/1.000/2.000).

## 8. Fallimenti registrati

1. Primo link del test dell'oracolo fallito: `calculate_nash_conv` vive in `gtosd_best_response`,
   non in `gtosd_solver`; aggiunto il target al solo test.
2. Test di indipendenza dalla partizione fallito due volte per errori del test (batch diverso
   dal riferimento; fingerprint che includeva il numero di unità e, nel test di ripresa, lo stato
   dell'RNG di valutazione consumato da una stima intermedia). Il trainer era corretto: 0 celle
   differenti fra partizioni a 1, 8 e 58 unità.
3. `<windows.h>` per la memoria di processo ridefinisce `min`/`max`: aggiunto `NOMINMAX`.
4. Stimatore D3 chiaroveggente (§2): implementato come prescritto da P6.3/§5, ha prodotto un
   plateau di circa 1 a su HU10 completo diagnosticato prima come pavimento dell'astrazione; la
   diagnosi è stata smentita dal confronto di capacità e dal test a board fissi con semi ruotati
   (`1·10⁻⁴` a con due copie dello stesso board) e corretta con il valutatore non chiaroveggente.
   Circa tre ore di esperimenti da scartare; erratum nella roadmap §5.

## 9. Comandi

```text
cmake --build out/build/windows-release --target gtosd_preflop_blueprint gtosd_preflop_blueprint_trainer_tests gtosd_preflop_blueprint_train
ctest --test-dir out/build/windows-release -L p6 --output-on-failure -V
gtosd_preflop_blueprint_train --config benchmarks/fixtures/preflop_blueprint_hu10_full_v1.json --resources-dir out/preflop_blueprint_resources --buckets-dir out/preflop_blueprint_buckets_200_500_1000 --iterations 2000 --batch 32 --threads 8 --eval-flops 20 --eval-every 250 --scheme dcfr --update alternating --checkpoint out/ckpt_full_dcfr_200.bin
gtosd_preflop_blueprint_train --config benchmarks/fixtures/preflop_blueprint_hu10_reduced_v1.json --resources-dir out/preflop_blueprint_resources --buckets-dir out/preflop_blueprint_buckets_200_500_1000 --iterations 500 --batch 8 --threads 8 --eval-every 100 --fixed-boards 64 --scheme dcfr --update alternating
gtosd_preflop_blueprint_buckets --resources-dir out/preflop_blueprint_resources --output-dir out/preflop_blueprint_buckets_500_1000_2000 --threads 8 --flop 500 --turn 1000 --river 2000 --restarts 10 --screening-iterations 10 --max-iterations 25 --screening-sample 500000
```
