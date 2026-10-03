# Studio di fattibilità: backend GPU CUDA per GTOSD

Data: 2026-09-18. Stato: **studio**. Nessuna riga di codice scritta, nessuna specifica
modificata, nessun vincolo revocato.

Richiesta dell'utente: capire **come** si implementerebbe l'uso della GPU su tutti i carichi di
calcolo del progetto e **quali benefici** darebbe davvero.

---

## 0. Il vincolo attuale

L'uso della GPU per il calcolo è oggi vietato dalle specifiche, in quattro punti indipendenti:

| Documento | Riga | Formulazione |
|---|---:|---|
| `README.md` | 10 | «Il calcolo del solver è **CPU + RAM only**. GPU, CUDA, ROCm, OpenCL, Vulkan Compute, DirectCompute e altri acceleratori non possono essere usati per tree building, traversal CFR, best response, certificazione o post-processing» |
| `docs/specifications/LIMITATIONS.md` | 34 | «Il solving usa e userà soltanto CPU e RAM. L'assenza della GPU non è una funzione ancora da implementare, ma una **decisione permanente di prodotto e di architettura**» |
| `docs/specifications/ARCHITECTURE.md` | 11 | «Non esiste e non verrà introdotto un backend GPU per costruzione dell'albero, canonicalizzazione, traversal CFR, regret/strategy update, best response, certificazione o analisi della soluzione» |
| `docs/specifications/PERFORMANCE.md` | 447 | «GPU e acceleratori di calcolo non sono una leva presente o futura» |

Il vincolo compare in 38 documenti. Nel codice non esiste una sola riga GPU: `grep` su
`libs/`, `include/`, `apps/`, `tests/`, `benchmarks/`, `schemas/` per
`cuda|gpu|rocm|opencl|vulkan|directcompute` restituisce **zero** occorrenze. Non c'è nessuna
astrazione di backend da riempire: si partirebbe da zero.

Questo studio **non** propone di revocare il vincolo. Descrive cosa costerebbe e cosa renderebbe,
in modo che la decisione sia informata. La §5.8 elenca i documenti da modificare se un giorno si
decidesse di revocarlo.

---

## 1. Hardware misurato di questa macchina

Rilevato il 2026-09-18 con `nvidia-smi --query-gpu` e CIM.

| | Intel i3-10100F | NVIDIA RTX 3050 6 GB |
|---|---|---|
| Unità | 4 core / 8 thread, 3,6 GHz base | compute capability **8,6** e 70 W letti da `nvidia-smi`; da questi due dati la variante è GA107 6 GB, ≈ 2.300 CUDA core, 18 SM (inferito, non letto) |
| Picco **fp32** | ≈ 0,5 TFLOPS (AVX2 FMA) | ≈ 8–9,8 TFLOPS → **≈ 18×** |
| Picco **fp64** | ≈ 0,26 TFLOPS | ≈ 0,15 TFLOPS (rateo 1/64) → **0,6×: la GPU è più lenta** |
| Interi 16/32 bit | AVX2 | throughput alto, `dp4a` disponibile |
| Banda memoria | 32 GB DDR4-2667 dual channel, ≈ 43 GB/s di picco (≈ 30 reali) | GDDR6 14 Gbps, 96 bit → **168 GB/s** → ≈ 4–5× |
| Capacità | 32 GB | 6.144 MiB, di cui **1.490 MiB già occupati** dal desktop → ≈ 4,5 GB utilizzabili |
| Collegamento | — | PCIe **3.0** ×16 (limite della piattaforma Comet Lake) ≈ 12 GB/s reali |
| Limite di potenza | — | **70 W**: sotto carico i clock restano sotto il massimo |

Tre conseguenze che decidono tutto il resto:

1. **L'unico moltiplicatore grande è in fp32** (≈ 18×). In fp64 questa GPU è più lenta della CPU
   che dovrebbe sostituire. Trainer, best response e certificatore del preflop blueprint sono
   scritti **interamente in `double`** (`libs/preflop_blueprint/src/trainer.cpp`,
   `best_response.cpp`): un port fp64 fedele non darebbe alcun guadagno, anzi.
2. **Il moltiplicatore di banda è modesto** (≈ 4–5×). Ogni carico dominato dalla memoria, e
   quasi tutti qui lo sono, ha come tetto quel 4–5×, non il 18×.
3. **La GPU pilota anche lo schermo.** Su Windows WDDM vale il watchdog TDR (2 s di default): un
   kernel più lungo viene ucciso e il driver resettato. Ogni lancio va tenuto sotto ~1 s, oppure
   si alza `TdrDelay` nel registro. Inoltre il desktop occupa già 1,5 GB dei 6.

---

## 2. Regola di valutazione usata in questo studio

Per ogni carico il tetto di guadagno è

```text
s          = min(rapporto_compute, rapporto_banda)        tetto hardware del kernel
speedup    = 1 / ((1 - p) + p / s)                        Amdahl, p = frazione portata su GPU
```

con `rapporto_compute` = 18× solo se si passa a fp32, 0,6× se si resta in fp64, e
`rapporto_banda` = 4–5×. Il secondo termine è quello che morde: portare su GPU il 55 % di un
carico con `s = 5` dà 1,8× complessivo, non 5×.

Un secondo criterio conta quanto il primo: **la baseline giusta non è il codice di oggi, è la
migliore CPU ragionevole.** Il repo contiene i due estremi.

- `libs/postflop/src/postflop_solver.cpp` è **già AVX2 scritto a mano**: intrinsics
  `_mm256_cvtepi16_epi32`, `_mm256_rcp_ps` con raffinamento di Newton, gather
  `_mm256_i32gather_pd` (righe 92–114, 4.392–4.410, 6.219+). Qui la CPU è vicina al suo limite e
  il rapporto hardware è tutto quello che si può guadagnare.
- `libs/preflop_blueprint/src/traversal.cpp` è **scalare in `double` ma già messo bene sul piano
  della memoria**: `ValueTraversal` alloca i buffer una sola volta nel costruttore (righe
  109–114), in array piatti action-major `maximum_actions * live_hand_count`, e `rebind()` cambia
  board senza riallocare (`best_response.cpp:527`). Niente allocazioni nel ciclo caldo. Il margine
  qui non è l'allocatore: è la devirtualizzazione e la vettorizzazione (§3.1).

> **Correzione del 2026-09-18.** Una prima stesura di questo studio indicava come punto caldo le
> allocazioni per nodo di `StreetEvaluator` in `best_response.cpp` (righe 194, 198, 208) e
> stimava 2–4× da un arena allocator. **È sbagliato.** Quel codice gira ai livelli flop e turn
> (righe 402, 476, 614, 636), cioè ~33 volte per flop, non nel ciclo per board: il livello river,
> 1.056 board per flop, passa da `ValueTraversal`, che è già pooled. La stima 2–4× è ritirata.

---

## 3. I quattro carichi, uno per uno

### 3.1 Certificatore / best response esatto — `libs/preflop_blueprint`

**Cosa fa.** Passata esatta sui **573 flop canonici** con tutti i 33 × 32 runout: **605.088
board** (`docs/research/preflop_vector_cfr/P7_CERTIFIER.md`). Per ogni board propaga il reach
dell'avversario sull'albero della street e calcola il valore per mano, per 2 giocatori × 2 modi
(risposta e strategia media), su un universo di 465–528 mani.

**Misure esistenti** (8 thread, i3-10100F, `PROGRESS_LOG.md` righe 1.248–1.257):

| Albero | Nodi | s per flop canonico | Passata esatta |
|---|---:|---:|---:|
| HU10 ridotto | 571 | ~0,3 | 3 min |
| HU10 completo | 2.059 | 3,2 | 35 min |
| CO40 test | 604 | 2,0–2,4 | 20–22 min |
| **CO40 completo** | **26.878** | **64** | **10,2 h** |

Scaling misurato: 13,3 s per flop a 1 thread, 7,3 a 2, 4,2 a 4, 3,2 a 8. Cioè **3,17× sui 4 core
fisici** (79 % di efficienza) e un ulteriore **1,31× dai soli thread SMT**. Un guadagno SMT del
31 % è la firma di un codice che **passa gran parte del tempo in stallo** — miss di cache, catene
dipendenti, allocatore — lasciando le unità di esecuzione libere per il thread gemello. È
esattamente il profilo che una GPU nasconde bene, ma anche quello che si ripara sulla CPU.

**Quanto è lontano dal limite.** I 64 s per flop sono misurati su 8 flop in parallelo su 8
thread: il lavoro monothread per flop è ≈ 512 s. Diviso per 1.056 board × 2 giocatori × 2 modi
restano **≈ 121 ms per traversata**. L'albero CO40 ha 26.878 nodi in tutto, quindi una traversata
visita al più 26.878 × 465 = 12,5 milioni di coppie mano-nodo: il costo è **almeno 9,7 ns
(≈ 40 cicli) per coppia**, e in realtà parecchio di più, perché nessuna traversata visita tutto
l'albero. Per un confronto: il ciclo interno è una manciata di FMA su un array contiguo.

Il margine c'è, ma non è dove lo avevo indicato nella prima stesura. Le tre cause plausibili, in
ordine di sospetto, sono in `traversal.cpp`: la chiamata virtuale per mano (riga 188), la
mancata vettorizzazione del ciclo a strategia media, e il kernel di showdown chiamato a ogni
terminale (riga 261). **Nessuna delle tre è misurata: vanno profilate prima di scrivere codice.**

**Mappatura CUDA proposta.** È il carico con la struttura migliore di tutto il progetto:

- `CompiledGame` è già un **array di nodi in preordine con sottoalberi contigui** (`subtree_end`,
  `compiled_game.hpp`): si copia sul device così com'è, senza puntatori da tradurre.
- **Un blocco CUDA per (board, giocatore, modo)**: 605.088 × 4 = 2,4 milioni di blocchi per
  passata, 4.224 blocchi per singolo flop. Anche un solo flop satura 18 SM.
- **Un thread per mano**: 465–528 mani → 512 thread per blocco, 16 warp, nessuna divergenza
  nell'anello interno (tutte le mani eseguono lo stesso attraversamento).
- Postordine **iterativo** con stack in shared memory (profondità massima 17): per nodo un buffer
  `[azioni][mani]` in fp32 = 4 × 512 × 4 B = 8 KB, dentro i 100 KB di shared memory per SM.
- Le righe di policy (`policy_row(node, hand)`) sono gather indiretti condivisi fra molte mani:
  vanno in memoria globale letti con `__ldg`, la L2 da 2 MB li assorbe.
- `fold_mass_universe` (somma su mani + 36 somme per carta) diventa una riduzione di blocco a
  **ordine fisso** in shared memory, mai `atomicAdd` su float (§5.5).
- Lanci a chunk di board per restare sotto il TDR.

**Stima onesta del beneficio.**

| Confronto | Fattore atteso |
|---|---|
| GPU fp64 contro CPU attuale | 1–2× (la GPU non ha fp64) |
| GPU fp32 contro **CPU attuale** | 8–20× |
| GPU fp32 contro **CPU rifatta bene** (devirtualizzata, vettorizzata, fp32) | **3–5×** |
| CPU rifatta bene, senza GPU | ignoto finché non si profila: la prima stima (3–8×) poggiava su una diagnosi sbagliata |

CO40 completo: 10,2 h → ≈ 2 h con la sola CPU rifatta, ≈ 30–40 min con GPU fp32.
Costo: ~15–25 giorni di lavoro per il percorso GPU, ~5–8 per il percorso CPU.

### 3.2 Trainer vector CFR preflop — `libs/preflop_blueprint/src/trainer.cpp`

**Misure** (`P6_TRAINER.md` righe 150–160): HU10 completo 0,31 s per iterazione con `B = 32` e 8
thread; 2.000 iterazioni = 618 s di training. CO40 completo ≈ 3 s per iterazione. Stato R+S
7,0 / 34,1 / 68,6 MB secondo la capacità: **entra tutto nella VRAM mille volte**.

**Struttura.** Tre famiglie:

1. Traversata per board: come §3.1, stessa mappatura, stessa convenienza.
2. `refresh_policy()` (riga 442): regret matching elemento per elemento su tutta la tabella dei
   regret. Parallelismo perfetto, ma è puro traffico di memoria → tetto 4–5×.
3. `discount_state()` (riga 470): scalatura DCFR di regret e strategy sum. Idem.

**Il problema vero è l'accumulo.** `B = 32` board aggiornano le stesse celle
`(nodo, riga, azione)`. Su GPU servirebbero accumulatori privati per board più una riduzione a
ordine fisso (32 × 68 MB = 2,2 GB, al limite della VRAM libera) oppure una riduzione per chiave
ordinata. Con `atomicAdd` su float il risultato **non è riproducibile**, e la specifica di
determinismo (`NUMERICAL_PRECISION.md` §Determinismo) lo vieta di fatto.

**Beneficio atteso: basso, non vale la pena.** 32 board × 4 = 128 blocchi non saturano la GPU;
il training HU10 completo dura 7 minuti, CO40 poco più. Anche un 5× sposterebbe minuti, mentre
il tempo dominante del ciclo P6 è la **valutazione** (882 s contro 618 s di training a 2.000
iterazioni), che è il carico §3.1. Conviene accelerare il certificatore e lasciare stare il
trainer.

### 3.3 Solver postflop DCFR — `libs/postflop/src/postflop_solver.cpp`

È il carico con il guadagno assoluto più alto e il costo di porting più alto.

**Misure** (checkpoint TSTC9D, `PERFORMANCE.md` righe 340–366, Release 8 thread):

| Metrica | Valore |
|---|---:|
| Traversal, 160 iterazioni | 130,37 s (0,815 s/iter) |
| Certificazione exact BR | 5,44 s |
| Nodi visitati | 245.146.564 |
| `solver_state_bytes` | 1.472.605.376 B |
| Peak RSS | 1.968.742.400 B |

Struttura: 1.758.624 nodi canonici, 145.524.152 infoset, 366.890.152 action entry, stato
`ScaledUint16RegretStrategy` (codici uint16 action-major + scala float32 per nodo).

**Profilo del traversal** (stesso checkpoint):

| Famiglia | Quota | Forma del calcolo | Idoneità GPU |
|---|---:|---|---|
| showdown (valori 27,4 %, rank/card 19,3 %, prefix 9,1 %) | **55,8 %** | celle di rank ordinate + somme prefisse su vettori di mani | **ottima**: scan segmentati con primitive di warp |
| value/update | 22,0 % | FMA densi su vettori di mani | ottima, limitata dalla banda |
| regret matching | 13,6 % | max/somma/reciproco su uint16 | ottima, già AVX2 su CPU |
| chance/board | 8,3 % | gather/scatter fra board | media |
| reach | 0,3 % | — | irrilevante |

**Il fatto decisivo: 1,47 GB di stato entrano nei 4,5 GB di VRAM liberi.** Lo stato resta
residente sul device per tutto il solve e il PCIe 3.0 (il collo di bottiglia peggiore, 12 GB/s)
trasporta solo metriche aggregate. Se lo stato non entrasse, il port sarebbe senza speranza:
1,47 GB per iterazione su PCIe costerebbero 0,12 s di sola copia, contro 0,815 s di calcolo.

Il codec uint16 + scala fp32 è **un vantaggio**: niente fp64, quindi niente rateo 1/64.

**Mappatura CUDA proposta.** Wavefront sui livelli del DAG pubblico (tutti i nodi di uno stesso
livello sono indipendenti), un blocco per nodo, un thread per mano; showdown come scan segmentato
sulle celle di rank con `cub::BlockScan` a ordine fisso; stato in layout SoA action-major già
presente. Riduzioni deterministiche obbligatorie (§5.5).

**Stima onesta del beneficio.** La baseline CPU qui è AVX2 curata, non codice ingenuo, e il
carico è dominato da banda e gather: il tetto è il rapporto di banda, non quello di fp32.

| Confronto | Fattore atteso |
|---|---|
| Tetto teorico di banda | 5,2× |
| Realistico, port completo e curato | **3–6×** sul traversal |
| Realistico, port parziale del solo showdown (55,8 %) | 1,8–1,9× complessivo (Amdahl) |

TSTC9D: 130 s → 25–45 s. Costo: **2–4 mesi**, il modulo ha 21.274 righe e ogni modifica tocca il
gate di memoria, il codec di prodotto, il determinismo e la comparabilità GTO+ (§6).

### 3.4 Equity, valutatore e tabelle di astrazione — `libs/equity`, `libs/card_abstraction`

| Artefatto | Dimensione | Costo di costruzione | Beneficio GPU |
|---|---:|---:|---|
| Tabella 7 carte `C(36,7)` | 8.347.680 voci, ≈ 33 MB | secondi, una tantum | **nullo** |
| Tabella bucket flop | 17,4 MB | 1,4 s | nullo |
| Feature turn | 138,7 MB | 3,3–4,1 s | nullo |
| Equity river 16 bit | 226,8 MB | 4,7–4,8 s | nullo |

Sono precalcoli una tantum che durano secondi e vengono serializzati su disco. Portarli su GPU
non sposta niente. Il vero costo «showdown» del progetto non è qui: è **dentro** il traversal del
solver postflop, cioè §3.3.

**Verdetto: da escludere dal perimetro.**

---

## 4. Tabella riassuntiva dei benefici

| Carico | Tempo oggi | Con GPU fp32 | Guadagno | Costo | Vale? |
|---|---:|---:|---|---:|---|
| Certificatore CO40 completo | 10,2 h | 30–40 min | 8–20× su codice attuale, **3–5×** su CPU rifatta | 15–25 gg | **sì, primo candidato** |
| Certificatore HU10 completo | 35 min | 3–5 min | idem | incluso sopra | sì |
| Trainer vector CFR | 7 min (HU10) | 3–5 min | 1,5–2× reali | 10–15 gg | **no** |
| Solver postflop TSTC9D | 130 s / 160 iter | 25–45 s | 3–6× | 2–4 mesi | solo come programma a sé |
| Equity e tabelle | secondi | secondi | nessuno | — | **no** |

Tutte le righe assumono il passaggio a **fp32**. Restando in fp64 ogni riga di questa tabella
diventa «nessun guadagno o peggioramento», perché la RTX 3050 ha 0,15 TFLOPS fp64 contro i 0,26
del tuo i3.

---

## 5. Come si implementerebbe, in concreto

### 5.1 Toolkit e compilatore

`nvcc` **non è installato** su questa macchina; il driver espone CUDA UMD 13.4, quindi il runtime
c'è ma manca il toolkit (≈ 3 GB di installazione). Compute capability 8,6 → `sm_86`.

Il punto delicato è l'host compiler. Sono installati due toolset:

| Toolset | Versione | Uso per CUDA |
|---|---|---|
| VS 18 Community | MSVC **14.51.36231** | troppo recente, quasi certamente non supportato da CUDA 13.x |
| VS 2022 BuildTools | MSVC **14.44.35207** | **è questo l'host compiler da usare** |

Quindi: `nvcc -ccbin "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64"`,
oppure `-allow-unsupported-compiler` come ripiego sconsigliato. Gli oggetti C++ del resto del
progetto continuano a compilarsi con MSVC 14.51: ABI compatibile, ma il mix va dichiarato nei
report di build.

### 5.2 CMake e struttura dei target

```cmake
option(GTOSD_ENABLE_CUDA "Build the optional CUDA compute backend" OFF)
if(GTOSD_ENABLE_CUDA)
  enable_language(CUDA)
  set(CMAKE_CUDA_ARCHITECTURES 86)
  set(CMAKE_CUDA_SEPARABLE_COMPILATION ON)
  add_subdirectory(libs/compute_cuda)
endif()
```

Regole non negoziabili:

- i file `.cu` vivono **solo** in `libs/compute_cuda/`, mai negli altri target;
- nessun header pubblico include `cuda_runtime.h`: la frontiera è un'interfaccia C++ pura;
- con `GTOSD_ENABLE_CUDA=OFF` (default) l'albero di build deve essere **identico bit per bit** a
  quello di oggi, verificato da un test di CI;
- il toolchain vcpkg resta invariato: CUDA non passa da vcpkg.

### 5.3 Interfaccia di backend

```cpp
namespace gtosd::compute {
class IBackend {
public:
  virtual ~IBackend() = default;
  [[nodiscard]] virtual std::string_view name() const = 0;      // "cpu" | "cuda"
  [[nodiscard]] virtual Precision precision() const = 0;         // Float64 | Float32
  [[nodiscard]] virtual bool available() const = 0;
  virtual Outcome evaluate_boards(const CompiledGame &, std::span<const BoardTask>,
                                  std::span<double> out) = 0;
};
const IBackend &select(std::string_view request);  // fallback esplicito, mai silenzioso
}
```

Selezione a runtime con `--compute-backend=cpu|cuda`, **mai** automatica: un fallback silenzioso
renderebbe irriproducibile un artefatto. Il nome del backend e la modalità di precisione entrano
nel fingerprint del certificato e nei report, come già fanno i codec di stato.

### 5.4 Precisione

`NUMERICAL_PRECISION.md` impone che «nessun formato ridotto ha identità numerica implicita con
`Float64`» e che le modalità siano **nominate**. Quindi non si porta `double` su GPU chiamandolo
allo stesso modo: si dichiara una modalità nuova, per esempio `Float32Cuda`, con la sua
tolleranza pubblicata, e i suoi artefatti non sono confrontabili bit per bit con quelli `Float64`.

Accumuli: somma a coppie o Kahan nelle riduzioni sulle mani, perché in fp32 una somma lineare su
528 termini perde troppi bit.

### 5.5 Determinismo

La specifica richiede che una passata ripresa e una continua coincidano **bit per bit**
(`P7_CERTIFIER.md` §Ripresa). Su GPU questo significa:

- **vietato** `atomicAdd` su `float`/`double`: l'ordine di arrivo varia fra i lanci;
- riduzioni ad albero a ordine fisso in shared memory, o `cub` con `deterministic` esplicito;
- ordine dei flop e dei board fissato dall'indice, mai dall'ordine di completamento dei blocchi;
- niente `--use_fast_math`, niente `-ffast-math`: la CPU non li usa (`/favor:INTEL64 senza
  fast-math`, `PERFORMANCE.md`) e la GPU non deve introdurli.

### 5.6 Test di parità e gate

| Test | Criterio |
|---|---|
| Parità per board | valori CPU fp64 contro GPU fp32 su tutti i board di 4 flop canonici, errore relativo ≤ 1e-5 |
| Parità di scelta | la best response deve scegliere la **stessa azione** della CPU per ogni mano, o il test fallisce anche entro tolleranza |
| Determinismo | due esecuzioni GPU identiche bit per bit; ripresa da stato = passata continua |
| Ordine di orbita | le verifiche di orbita di P7 §3 ripetute sul backend GPU |
| Build OFF | con `GTOSD_ENABLE_CUDA=OFF` la suite attuale passa immutata |

### 5.7 Fasi con criteri di abbandono

| Fase | Contenuto | Criterio di uscita | Criterio di abbandono |
|---|---|---|---|
| G0 | toolkit, `enable_language(CUDA)`, rilevamento device, target vuoto | build OFF invariata, build ON compila | il toolkit non convive con MSVC 14.51/14.44 |
| G1 | **misura** del margine CPU: profilo del certificatore, arena allocator, SoA | si scopre quanto del 10,2 h è overhead | se la CPU rifatta arriva già a 2 h, rivalutare tutto |
| G2 | un solo kernel: valori di street river per board, fp32 | parità §5.6 su 4 flop | errore > 1e-5 o scelte diverse |
| G3 | certificatore completo su GPU a chunk | **≥ 3×** end-to-end sulla CPU rifatta | < 3×: si chiude qui |
| G4 | eventuale trainer | ≥ 2× sul ciclo P6 completo | < 2× |
| G5 | eventuale postflop | ≥ 3× sul traversal TSTC9D | < 3× |

G1 prima di G2 non è burocrazia: senza sapere quanto margine c'è **sulla CPU**, qualsiasi numero
di speedup GPU è un confronto con codice non ottimizzato.

### 5.8 Documenti da modificare se il vincolo venisse revocato

`README.md` (righe 10–14), `docs/specifications/LIMITATIONS.md` (§Vincolo permanente CPU/RAM),
`docs/specifications/ARCHITECTURE.md` (§Modello di esecuzione CPU/RAM e riga 100),
`docs/specifications/PERFORMANCE.md` (§Piano prestazionale CPU/RAM-only),
`docs/specifications/CHANGELOG.md`, più il protocollo di benchmark (§6).

---

## 6. Cosa la GPU **non** risolve

1. **La non convergenza di CO40, cioè il blocco attuale.** P9 ha già dimostrato un errore di
   modello: il riuso del campione negli aggiornamenti alternati
   (`P9_CONVERGENCE_DIAGNOSIS.md`). Più iterazioni peggioravano il risultato: 2.000 → 0,657 a,
   10.000 → 0,841 a. Un solver più veloce raggiunge prima la risposta sbagliata. **Il tempo di
   calcolo non è oggi il collo di bottiglia del programma.**
2. **Il gate GTO+.** Il confronto con GTO+ è definito su CPU: un run GPU non è comparabile e non
   può promuovere il gate. Servirebbe una famiglia di benchmark separata e dichiarata.
3. **Il gate di memoria.** 6 GB di VRAM, di cui 1,5 già occupati dal desktop, sono un vincolo
   **più stretto** dei 32 GB di RAM. Alberi più grandi di TSTC9D non entrano.
4. **La riproducibilità di prodotto.** Un artefatto prodotto su GPU è riproducibile solo su una
   GPU equivalente: il progetto oggi garantisce riproducibilità per input e versione.

---

## 7. Le tre leve più economiche, da fare comunque, GPU o no

1. **Devirtualizzare e vettorizzare il ciclo interno di `ValueTraversal`.**
   `Policy::probabilities` è virtuale puro (`traversal.hpp:27`) e viene chiamata **una volta per
   mano** dentro il ciclo caldo (`traversal.cpp:188`): 465 chiamate indirette per nodo di
   decisione, per un corpo che è due somme e una moltiplicazione (`traversal.cpp:100–104`).
   Blocca inlining e vettorizzazione proprio dove `live_hand_count = 465` è una costante di
   compilazione. Il ramo best response (righe 176–185) non la chiama ed è già vettorizzabile:
   il costo è tutto sul ramo a strategia media. Rimedio: sollevare il lookup della riga fuori dal
   ciclo sulle mani, o rendere la policy un parametro template. Stima **non misurata**, e proprio
   per questo il profilo viene prima.
2. **Layout SoA e fp32 dichiarato nel certificatore.** Un `Float32` nominato secondo le regole di
   §5.4 abilita AVX2 a 8 corsie invece di 4: altri 1,5–2×, sempre su CPU.
3. **Chunk ≥ thread.** Già scoperto (decisione 47): il parallelismo è sui flop del chunk, e con
   `--chunk 1` gli 8 thread non servono a niente. Vale 4,2× ed è gratis.

Queste tre leve, sommate, coprono la maggior parte del guadagno attribuito alla GPU in §3.1, a
una frazione del costo e senza toccare nessun vincolo di architettura.

---

## 8. Raccomandazione

La GPU **è** tecnicamente sensata su questo progetto, ma soltanto su due dei quattro carichi, e
soltanto in fp32: il **certificatore** (10,2 h → ~35 min, ma solo 3–5× contro una CPU rifatta
bene) e il **solver postflop** (3–6×, a 2–4 mesi di lavoro). Il trainer e le tabelle di equity
non ne traggono nulla. In fp64, che è come il preflop blueprint è scritto oggi, questa GPU è più
lenta del tuo i3.

L'ordine sensato sarebbe: prima le tre leve CPU di §7, poi misurare di nuovo, poi decidere se i
3–5× residui del certificatore valgono l'installazione del toolkit, un secondo percorso numerico
da certificare e la revoca di un vincolo dichiarato permanente in 38 documenti.

E soprattutto: nessuna delle due strade tocca il motivo per cui CO40 oggi non converge.
