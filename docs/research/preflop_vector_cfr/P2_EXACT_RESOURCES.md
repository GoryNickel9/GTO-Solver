# P2 — Risorse esatte

> **Stato al 2026-10-03.** Questo è il report del gate di P2, di settembre 2026, ed è
> congelato. Il modulo che descrive è ancora in produzione nel solver preflop blueprint.
> I riferimenti a HU10, CO40, P9 e alla best response astratta come lavoro o gate correnti
> sono però storici:
>
> - P9 è in [`archive/preflop-blueprint-research-2026-09/`](../../archive/preflop-blueprint-research-2026-09/README.md);
> - history7 e la suite HU10-HU40 sono stati tolti il 2026-10-03, e l'ultimo albero che li
>   contiene è al tag `history7-final`.
>
> Lo stato corrente è nel [diario](PROGRESS_LOG.md) e nella
> [ricetta di MonkerSolver](MONKER_RECIPE_REPRODUCTION_2026-09-28.md).

Data: 2026-09-15
Esito del gate: **PASS**
Roadmap: [PREFLOP_VECTOR_CFR_CODER_ROADMAP_2026-09-15.md](../../archive/preflop-blueprint-research-2026-09/PREFLOP_VECTOR_CFR_CODER_ROADMAP_2026-09-15.md), fase P2

## Identità

| Campo | Valore |
|---|---|
| Branch di fase | `feature/preflop-blueprint-p2-resources` (da `feature/preflop-blueprint` a `9f0a140`) |
| Commit del codice P2 | `9f8a6d3` |
| Fingerprint catalogo | `fnv1a64:51879f40626cb7dd` |
| Fingerprint rank table | `fnv1a64:379442deddede23b` |
| Fingerprint all-in | `fnv1a64:f51f219338f34669` |
| Fingerprint gruppi | `fnv1a64:6c3b127a4735f6f8` |
| Fingerprint feature flop / turn / river | `fnv1a64:0c8d163bc8511741` / `fnv1a64:96bcf9c24706d9b6` / `fnv1a64:0d83223071252d2f` |
| Build | Release `/W4 /permissive- /WX`, MSVC 19.51, 8 thread |

## Consegne

1. **Tabella di rank ordinali** (`rank_table.hpp`): ogni insieme di 5 carte riceve la posizione
   ordinale del proprio `HandValue` fra i valori distinti restituiti dall'evaluator esatto a 5
   carte; ogni insieme di 7 carte riceve il massimo dei 21 sottoinsiemi. Confrontare due ordinali
   equivale a confrontare i `HandValue` di `evaluate_seven`. 1.404 rank distinti; indice colex
   identico a quello della tabella R3; 17,4 MB a 16 bit; costruzione 1,4 s.
2. **Kernel di conteggio degli esiti** (`showdown_counts.hpp`): per tutte le mani vive di un board
   conta vittorie, pareggi e sconfitte contro le mani avversarie disgiunte con una sola
   ordinazione per rank e conteggi correnti per carta (n log n); implementazione pairwise di
   riferimento; tabella delle 630 combo con maschere e classi; mani vive per maschera del board.
3. **Tabella all-in preflop** (`all_in_table.hpp`): conteggi esatti `win/tie/lose` sui 201.376
   runout per ciascuna delle 176.715 coppie di combo disgiunte, in una tabella triangolare da
   198.135 voci (le coppie sovrapposte restano vuote); costruzione parallela sui 376.992 board
   con accumulatori per thread; 2,4 MB.
4. **Gruppi avversari** (`OpponentGroups`): equity all-in per classe contro mano casuale
   (media sulle coppie disgiunte), classi ordinate per equity e tagliate in 8 gruppi di massa
   simile: `78, 74, 84, 78, 76, 78, 88, 74` combo. AA è la classe più forte, con equity 0,7308.
5. **Feature flop** (`FlopFeatureTable`): per ciascuno dei 573 flop canonici e delle 630 combo
   nel frame canonico, istogramma a 16 bin dell'equity esatta al river sui 465 runout; 11,6 MB;
   2,1–2,4 s.
6. **Feature turn** (`TurnFeatureTable`): per ciascuno dei 13.761 flop+turn canonici, istogramma
   a 16 bin sui 30 river; 138,7 MB; 3,3–4,1 s.
7. **Feature river** (`RiverFeatureTable`): per ciascuno dei 19.998 board canonici, equity esatta
   in virgola fissa a 16 bit contro tutte le 406 mani disgiunte e contro ciascuno degli 8 gruppi;
   226,8 MB; 4,7–4,8 s.
8. **Contenitore di risorse** versionato (kind, versione, fingerprint, checksum FNV-1a,
   scrittura atomica) e **eseguibile** `gtosd_preflop_blueprint_resources` con report,
   verifica dell'oracolo a campione, scrittura e ricaricamento.

## Verifiche

| Controllo | Esito | Evidenza |
|---|---|---|
| Build Release dei target P2, warning come errori | PASS | nessun warning |
| `gtosd_card_abstraction_features_tests` | PASS | 9.868.560 asserzioni, 84–87 s |
| `gtosd_preflop_blueprint_resources` (20.000 campioni oracolo) | PASS | 0 discrepanze |
| Eseguibile con `--output-dir` e 200.000 campioni oracolo | PASS | 0 discrepanze; 7 file scritti e ricaricati |
| Regressione `preflop_blueprint` P0–P2 | PASS | 7/7 in 171 s |

Contenuto del test:

- rank table: 200.000 coppie di insiemi casuali con ordine dei rank uguale all'ordine dei
  `HandValue` dell'oracolo; indice colex uguale a `seven_card_combination_index`; colore sopra
  full house e scala A-6-7-8-9 riconosciuta e sotto un full house;
- kernel: 300 board casuali, sweep uguale al pairwise per tutte le 465 mani, 406 avversari ciascuna;
- all-in: coppie sovrapposte vuote, 176.715 coppie disgiunte con `win+tie+lose = 201.376`,
  antisimmetria; 40 coppie contro l'enumerazione diretta dei 201.376 runout con la rank table;
  3 coppie contro l'enumerazione diretta con l'oracolo `evaluate_seven`;
- gruppi: masse fra 48 e 120, somma 630, AA nel gruppo 0 e con equity massima, gruppi ordinati
  per equity;
- feature flop: 300 osservazioni contro il calcolo diretto (465 runout, pairwise su 406 mani),
  somma 465; 200 combo sovrapposte con istogramma vuoto; 300 permutazioni congiunte di flop e
  mano con istogramma invariante attraverso il lookup canonico;
- feature turn: 200 osservazioni contro il calcolo diretto, somma 30;
- feature river: 300 osservazioni con le 9 equity uguali al calcolo diretto;
- persistenza: round trip di tutte e sei le risorse, file corrotto rifiutato, kind sbagliato
  rifiutato.

## Tempi e dimensioni (8 thread, i3-10100F)

| Risorsa | Costruzione | File |
|---|---:|---:|
| Catalogo dei board | 2,6 s | 7.711.176 B |
| Rank table | 1,4 s | 17.449.440 B |
| All-in preflop | 53,2–53,4 s | 2.377.708 B |
| Gruppi avversari | < 0,01 s | 820 B |
| Feature flop | 2,1–2,4 s | 11.551.849 B |
| Feature turn | 3,3–4,1 s | 138.711.048 B |
| Feature river | 4,7–4,8 s | 226.777.492 B |
| Totale (con verifica oracolo 200k, salvataggio 5,6 s e ricaricamento 4,5 s) | 82 s | 404.579.533 B |

Il costo è dominato dalla tabella all-in (4·10^10 confronti di coppia). Il gate di 60 minuti è
rispettato con ampio margine; nessuna ottimizzazione è necessaria prima di P3.

## Contratti fissati per P3

- Le feature sono indicizzate da (indice canonico della street, combo nel frame canonico). Per
  un'osservazione fisica: `lookup_*` del catalogo restituisce indice e permutazione; la combo si
  ottiene con `combo_index` sulle carte permutate.
- Istogrammi: 16 bin di larghezza 1/16 sull'equity `(win + tie/2) / (win + tie + lose)`;
  l'equity 1 cade nell'ultimo bin. Conteggi interi: 465 al flop, 30 al turn.
- River: 9 valori in virgola fissa (`equity × 65535` arrotondata): contro tutti, poi contro i
  gruppi 0–7; 0,5 quando nessun avversario disgiunto appartiene al gruppo.
- Le feature dipendono solo dalle carte visibili al giocatore nella street; nessuna informazione
  sul runout effettivo di un deal entra nella feature.

## Decisioni prese dall'agent

| Decisione | Motivazione |
|---|---|
| Rank ordinali a 16 bit derivati dall'oracolo a 5 carte, al posto del caricamento della tabella R3 a 32 bit | stesso ordine dei `HandValue`, metà della memoria, costruzione in 1,4 s senza file esterno; l'oracolo resta l'unico evaluator |
| Flop e turn con il kernel sweep, river con il pairwise | il river richiede il conteggio per gruppo avversario, il pairwise resta semplice ed esatto e funge da controllo indipendente del kernel |
| File delle feature (365 MB) come artefatti offline, non distribuiti | servono solo al clustering; i bucket distribuiti sono le tabelle di P3 |
| Tabella all-in triangolare con voci vuote per le coppie sovrapposte | indirizzamento O(1) senza mappa; 0,5 MB in più rispetto alla forma compatta |
| Equity river in virgola fissa a 16 bit | 226,8 MB invece di 453 MB in float32; errore di quantizzazione 1/131070 |

## Fallimenti registrati

- Il conteggio atteso delle coppie disgiunte era stato stimato in 156.240 (C(32,2) invece di
  C(34,2)); il test lo ha rifiutato al primo run. Corretto a 176.715 nella costante e nel
  registro delle decisioni.
- La soglia di plausibilità dell'equity di AA (> 0,80) era calibrata sul mazzo intero; nello
  Short Deck vale 0,7308. Sostituita da un controllo strutturale (AA è la classe con equity
  massima) e da un intervallo largo.

## Comandi riproducibili

```text
cmake --build out\build\windows-release --target gtosd_card_abstraction gtosd_card_abstraction_features_tests gtosd_preflop_blueprint_resources
ctest --test-dir out\build\windows-release -L p2 --output-on-failure -V
out\build\windows-release\benchmarks\gtosd_preflop_blueprint_resources.exe --threads 8 --verify-oracle 200000 --output-dir out\preflop_blueprint_resources
```
