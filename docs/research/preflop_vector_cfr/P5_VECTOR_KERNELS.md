# P5 — Kernel vettoriale HU

Data: 2026-09-15. Branch di fase: `feature/preflop-blueprint-p5-kernel`. Esito del gate: **PASS**.

## 1. Cosa è stato prodotto

| Componente | File | Contenuto |
|---|---|---|
| Contesto di board | `include/gtosd/preflop_blueprint/board_context.hpp`, `libs/preflop_blueprint/src/board_context.cpp` | per una board history completa: le 465 mani vive (id combo, carte, classe preflop), rank a 7 carte dalla tabella P2, ordine per rank con gruppi di pari rank, indice inverso combo → mano, liste di incidenza per carta (30 mani per carta viva), bucket flop/turn/river dalle tabelle P3 tramite la permutazione canonica del board |
| Kernel | `include/gtosd/preflop_blueprint/kernels.hpp`, `libs/preflop_blueprint/src/kernels.cpp` | fold: `D[h] = S − C[h1] − C[h2] + r[h]`; showdown: passata crescente e decrescente sull'ordine dei rank con somme correnti per carta e gruppi di pari rank (masse del gruppo aggiunte dopo che ogni membro le ha lette), `W`, `T`, `L` per mano; all-in preflop: cache per board delle probabilità esatte di vittoria e pareggio di ogni coppia viva (tabella P2), prodotto matrice-vettore; riferimenti pairwise `O(n²)` per i test; interfaccia `ShowdownKernel` con N vettori di reach e maschera dei giocatori vivi (D13) e implementazione HU |
| Traversata | `include/gtosd/preflop_blueprint/traversal.hpp`, `libs/preflop_blueprint/src/traversal.cpp` | `Policy` (probabilità per nodo e mano), `BucketPolicy` sul layout `(nodo, classe/bucket, azione)` di P4, `HandPolicy` per mano (test e oracoli); `ValueTraversal`: valori controfattuali dell'eroe alla radice per un board, reach avversaria propagata lungo l'albero compilato, nessuna allocazione dopo la costruzione, potatura dei sottoalberi a reach nulla, modalità best response (`max` ai nodi dell'eroe), contatori |
| Sottogiochi | `CompiledGame::compile_subgame` | compila l'albero a partire da uno stato pubblico arbitrario con l'abstraction postflop della configurazione (oracoli) |
| Test | `tests/preflop_blueprint_kernel_tests.cpp` (1.636.010 asserzioni), `tests/preflop_blueprint_oracle_tests.cpp` (179.342 asserzioni) | vedi §2 |
| Eseguibile | `benchmarks/preflop_blueprint_traversal.cpp` | tempo per board di contesto, cache all-in, traversata di policy e di best response; report JSON |

Convenzioni. I valori sono in ante: `v[h] = Σ_z Σ_{o ∌ h} reach_avv(o, z) · u_eroe(h, o, z)`; i fattori di chance `1/465` e `1/406` sono costanti sul board e restano al chiamante (P6). Al preflop all-in la coppia `(h, o)` usa le probabilità esatte sui 201.376 runout (stima non distorta sul board campionato, §5 della roadmap); agli all-in flop e turn e agli showdown river il confronto avviene sul board campionato.

## 2. Verifiche eseguite

| Verifica | Esito |
|---|---|
| Contesto: 465 mani, 31 carte vive con 30 mani ciascuna, indice inverso, ordine dei rank, righe `no_bucket` senza tabelle, board con carta doppia rifiutato (20 board) | PASS |
| Kernel fold e showdown contro il riferimento pairwise: 200 board casuali × 3 pattern di reach (uniforme, sparsa con zeri, tutte a 1), tolleranza `1e-12` relativa; `W + T + L = D`; 406 avversari disgiunti; reach nulla → masse nulle; ogni board contiene gruppi di pari rank | PASS |
| Cache all-in contro il riferimento pairwise sulla tabella P2: 20 board × 2 pattern, `1e-12`; `W + T + L = D` | PASS |
| Traversata contro la ricorsione per coppia `(h, o)` (policy casuale per mano con zeri, reach sparsa): HU10 ridotta 2 board × 2 eroi, HU10 completa 1 board; errore massimo `6,5·10⁻¹³` ante (soglia `1e-9`); determinismo; best response ≥ valore di policy | PASS |
| Policy a bucket contro la stessa strategia espressa per mano su CO40 (tabelle 200/500/1.000): valori uguali entro `1e-12` per entrambi gli eroi | PASS |
| Rifiuti: all-in preflop raggiungibile senza tabella esatta → `missing_table`; eroe non valido; policy a bucket senza bucket nel contesto → `missing_table` | PASS |
| Oracolo postflop (`ProductionDcfr`, solo nel test, D5): tre sottogiochi river con range asimmetrici fissi, 20–40 iterazioni; strategia media copiata per combo nella `HandPolicy`; valore condizionale `v[h] / D[h]` contro `conditional_value_antes` del solver postflop; alberi appaiati nodo per nodo (57, 117, 57 nodi) | PASS, errore massimo `1,4·10⁻¹⁴` ante (soglia `1e-9`) |
| AddressSanitizer (`windows-asan`, RelWithDebInfo, `/fsanitize=address`): test del gioco, dei kernel e dell'oracolo | PASS, nessuna diagnostica |
| `ctest -L p5` in Release: test dei kernel, oracolo, report della traversata | PASS |

> **Nota (2026-09-16).** Su CO40 queste verifiche coprono i kernel per board, non il CFR: i
> sottogiochi dell'oracolo sono tre **river** con al massimo tre rilanci e strategia per mano, e il
> confronto bucket/mano è un'uguaglianza interna fra due implementazioni della stessa policy. I
> valori del CFR vettoriale su CO40 non sono confrontati con nessuna sorgente esatta (P6 §3.1).

## 3. Tempo per board (CO40, 27.061 nodi, 15.922 terminali, policy uniforme)

Misura con `gtosd_preflop_blueprint_traversal --boards 20` a macchina libera, un thread, Release.

| Fase | Millisecondi |
|---|---|
| Contesto di board (rank, ordine, bucket) | 0,06 |
| Cache all-in (465 × 465 coppie dalla tabella P2) | 2,05 |
| Traversata dei valori, policy a bucket, per eroe | 89,8 |
| Traversata dei valori, policy per mano, per eroe | 70,7 |
| Traversata best response, per eroe | 72,5 |
| HU10 completa (2.059 nodi): traversata per eroe | 9,4 (best response 8,1) |

Con la policy uniforme nessun sottoalbero ha reach nulla, quindi la misura è il caso peggiore
per la potatura. Il costo è dominato dai terminali di showdown (due passate sui 465 rank per
ciascuno dei 15.922 terminali) più che dai nodi decisionali. Una iterazione di CFR su un board
richiede due traversate con aggiornamento (P6), quindi circa il doppio del tempo di traversata.

## 4. Osservazioni e dubbi

1. La cache all-in per board costa più della traversata di un piccolo albero; è costruita una
   volta per board e serve ai 10 terminali all-in preflop. In P6 conviene condividerla fra le due
   passate (giocatore 0 e 1) dello stesso board.
2. Il kernel showdown ricalcola le passate per ogni terminale. Terminali diversi dello stesso
   board condividono ordine e rank ma non la reach, quindi le passate non si possono riusare; una
   riduzione del costo passerebbe da versioni float o vettorizzate, da misurare solo dopo P9
   (§3.4, precisione dello stato).
3. Il riferimento della traversata è una ricorsione per coppia e costa 0,15–0,5 s per board sugli
   alberi HU10; su CO40 costerebbe minuti, per questo la verifica CO40 usa l'oracolo postflop
   sui sottogiochi river e il confronto bucket/mano.
4. I sottogiochi dell'oracolo hanno al massimo 3 raise perché il solver postflop limita
   `raise_depth` a 4; l'appaiamento nodo per nodo con stati pubblici uguali garantisce che i due
   alberi coincidano.

## 5. Fallimenti registrati

1. Primo run dell'oracolo fallito con `invalid_configuration` dal solver postflop: le opzioni
   costruite a mano (algoritmo `ProductionDcfr` con esponenti e precisione di default) non sono
   ammesse; il profilo di produzione va risolto con `resolve_postflop_production_options`
   (precisione `ScaledUint16RegretStrategy`, esponenti 1,5/3, intervallo di certificazione 20).
   Corretto al secondo tentativo.

## 6. Comandi

```text
cmake --build out/build/windows-release --target gtosd_preflop_blueprint gtosd_preflop_blueprint_kernel_tests gtosd_preflop_blueprint_oracle_tests gtosd_preflop_blueprint_traversal
ctest --test-dir out/build/windows-release -L p5 --output-on-failure -V
gtosd_preflop_blueprint_traversal --config benchmarks/fixtures/preflop_blueprint_co40_v1.json --resources-dir out/preflop_blueprint_resources --buckets-dir out/preflop_blueprint_buckets_200_500_1000 --boards 20
cmake --build out/build/windows-asan --target gtosd_preflop_blueprint_kernel_tests gtosd_preflop_blueprint_game_tests gtosd_preflop_blueprint_oracle_tests
ctest --test-dir out/build/windows-asan -R "gtosd_preflop_blueprint_(kernel|game|oracle)_tests"
```
