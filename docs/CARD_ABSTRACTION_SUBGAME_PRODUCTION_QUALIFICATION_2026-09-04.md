# Qualifica production card abstraction e subgame solving — 2026-09-04

## Esito

Il percorso HU postflop bucketed è qualificato per due fixture flop distinte:

- AHKHQH con `K=16`, CFR+ Float64, 500 iterazioni e otto thread;
- TH7D6S con `K=128`, CFR+ Float64, 400 iterazioni e otto thread.

In entrambi i casi cinque processi indipendenti passano il gate
`NashConv/pot < 1%`, usano la stessa cache exact-feature e producono la stessa
metrica. Un run seriale separato verifica che la differenza dovuta al
parallelismo resti `<= 1e-4` in frazione.

Il `SubgameSolver` safe è qualificato sul contratto installabile `FiniteGame`:
frontier reach-weighted e infoset-closed, minimizer CFR+, merge nel blueprint,
guard full-game exact-NashConv e fallback. Il default production è otto thread.
Il benchmark usa Leduc, la cui chance root ha abbastanza rami da occupare il
pool completo.

Il bridge successivo collega inoltre un singolo frontier canonico mid-tree al
`DenseLayout` postflop, con range privati condizionati dal blueprint, card
removal, snapshot limitato agli action slot del sottogioco e rollback governato
da exact-NashConv sul gioco completo. Non sceglie un singolo K universale e non
dichiara supporto preflop: il tree/layout preflop non esiste ancora. L'exact
resta un oracle differenziale, non il percorso commerciale predefinito per
alberi grandi.

## Contratto qualificato

- CPU locale, nessuna GPU;
- otto thread solver: sette worker più il thread chiamante;
- otto worker per la costruzione delle partizioni exact-feature;
- CFR+ alternato, `averaging_delay=0`, stato Float64;
- runout e best response combo-level esatti;
- k-means deterministico per partizione board/player;
- cache exact-feature 1.0 condivisa e indipendente da K;
- checkpoint/fingerprint distinti tra exact e bucketed;
- budget RAM e disco passati esplicitamente dal chiamante.

Il valore sperimentale RAM `2.000.000.000 B` usato sotto non è un cap desktop
di prodotto e non è il campo GTO+ “Memory needed for solving”. Serve soltanto
a verificare il comportamento del preflight e del runner con un budget
esplicito. La comparabilità della memoria con GTO+ resta irrisolta.

## Preflight e cache

| Fixture | Partizioni | Osservazioni | Cache reale | Upper bound corretto | Build 8 worker | Peak RSS |
|---|---:|---:|---:|---:|---:|---:|
| AHKHQH flop | 404 | 12.984 | 2.788.551 B | 3.142.291 B | 1,57246 s | 23.076.864 B |
| TH7D6S flop | 1.124 | 280.308 | 59.982.478 B | 66.237.265 B | 169,398 s | 314.556.416 B |
| TSTC9D flop | 780 | 180.760 | non costruita | 42.667.422 B | non eseguita | non misurato |

Il builder assegna una partizione per job, usa fino a otto worker e unisce i
risultati nell'ordine canonico. Due build dello stesso input sono bit-identiche.
Il limite di 2.000.000 osservazioni viene verificato prima di avviare i worker.

L'upper bound testuale conta le due occorrenze del combo id e i bit pattern
decimali completi di peso e feature. Il gate disco combinato include lo state
page-backed più entrambe le copie temporaneamente presenti durante la
sostituzione atomica della cache. Per TH K=128 il totale corretto è:

```text
686.571.520 B state + 132.474.530 B cache atomica = 819.046.050 B
```

## Qualifica CFR+ a otto thread

| Fixture | K | Iterazioni | Compressione | Weighted MSE | Max L2 | NashConv/pot | Wall mediano | Peak RSS mediano |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| AHKHQH | 16 | 500 | 2,181764x | 0,0000194186 | 0,108253 | 0,6029051267% | 9,936001 s | 25.227.264 B |
| TH7D6S | 128 | 400 | 2,322746x | 0,000000775706 | 0,070882 | 0,8816749004% | 435,349473 s | 694.796.288 B |

AHKHQH usa cinque wall time `9,453350–10,878896 s` e Peak RSS
`24.956.928–25.325.568 B`. TH usa `416,819046–457,633666 s` e
`694.530.048–694.960.128 B`. Tutti i report dichiarano
`solver_threads=8`, `parallel_action_workers=7`, exact outcomes e exact combo
best response.

TH K=16 viene respinto: a 800 iterazioni misura ancora
`NashConv/pot=5,924317%`. Con K=128 la curva è
`3,359022/1,420934/1,015981/0,881675%` a
`100/200/300/400` iterazioni; il primo checkpoint strettamente sotto 1% è
quindi 400. Questo dimostra che un K valido su AHK non può essere promosso
come default universale.

## Oracle seriale

| Fixture | NashConv/pot 8 thread | NashConv/pot seriale | Differenza assoluta | Tolleranza | Wall seriale | Speedup su mediana 8 thread |
|---|---:|---:|---:|---:|---:|---:|
| AHKHQH | 0,6029051267% | 0,6099145321% | 0,0000700941 | 0,0001 | 31,051574 s | 3,13x |
| TH7D6S | 0,8816749004% | 0,8770330545% | 0,0000464185 | 0,0001 | 1.493,513047 s | 3,43x |

L'oracolo usa lo stesso K, la stessa cache e lo stesso numero di iterazioni;
cambia soltanto `solver_threads=1`. Non è un candidato production.

## TSTC9D: sola fattibilità

Con il budget sperimentale esplicito di `2.000.000.000 B`, il preflight
corretto produce:

| K | State Float64 upper bound | Peak RAM stimato | Esito budget |
|---:|---:|---:|---|
| 16 | 406.977.536 B | 895.738.264 B | fattibile |
| 32 | 813.955.072 B | 1.302.740.760 B | fattibile |
| 64 | 1.627.910.144 B | 2.116.745.752 B | respinto |
| 128 | 3.255.820.288 B | 3.744.755.736 B | respinto |

Questa tabella non qualifica convergenza o abstraction error di TSTC9D e non
costituisce confronto memoria con GTO+.

## Subgame solving

I test verificano:

- derivazione del reach di frontiera da chance e strategie di entrambi i
  player;
- rifiuto di radici sovrapposte e information set tagliati;
- checkpoint CFR+ con `thread_count=8`;
- accettazione di un miglioramento dopo due best response full-game;
- fallback byte-equivalente al blueprint quando il candidato peggiora;
- composizione abstraction → solve → lift → guard sul gioco esatto.

Il benchmark Release Leduc exact-guarded a 1.000 iterazioni, cinque
ripetizioni, misura `2.645,542 ms` wall mediano nel run completo. È una
baseline funzionale del percorso safe a otto thread, non una promessa di
latenza per un subgame Short Deck commerciale.

### Bridge nativo postflop

Il nuovo comando `edges-bucketed` rende navigabile il DAG canonico e il comando
`resolve-bucketed` applica una storia `edge:outcome` a un checkpoint blueprint
senza sovrascriverlo. Il resolver rifiuta frontiere terminali, con più ingressi
o con discendenti decisionali condivisi oltre il confine; richiede CFR+
Float64, cache/config coerenti, sette worker più coordinatore e budget RAM,
disco e snapshot espliciti.

Sul fixed-river K=8 a 100 iterazioni:

| Metrica | Valore |
|---|---:|
| Root frontier | nodo canonico 1, dopo root-check |
| Reach pubblico blueprint | 0,6335648385 |
| Decision node / action entry interessati | 2 / 32 |
| Snapshot rollback | 512 B |
| CFR+ locale, 100 iterazioni | 8,59 ms |
| Ciclo totale con due BR exact | 11,66 ms |
| Blueprint NashConv/pot | 0,7208569471% |
| Candidate NashConv/pot | 0,7332005210% |
| Deployment | `blueprint_fallback` |
| Peak RSS diagnostico | 8.364.032 B |

Il test nativo attraversa separatamente un vero chance node turn→river e
verifica automorfismi, card removal, reach strettamente sotto uno, prior privato
condizionato, budget prima dell'allocazione, fallback byte-identico oppure
accept coerente col guard e round-trip del checkpoint risultante.

Con questa prova il prerequisito card abstraction/subgame solving del prodotto
HU postflop corrente è chiuso. La parity GTO+ può riprendere mantenendo il path
exact come oracle. Il solver preflop completo resta Fase 14: beneficerà degli
stessi moduli, ma non viene dichiarato implementato né usato come condizione
retroattiva per la parity postflop.

## Benchmark ridotto abstraction

Sul fixed-river da 48 infoset esatti, 1.000 iterazioni e cache riusata:

| K | Stato | Compressione | Weighted MSE | Wall mediano | NashConv/pot |
|---:|---:|---:|---:|---:|---:|
| 1 | 128 B | 12x | 0,294077 | 8,146 ms | 15,151551% |
| 2 | 256 B | 6x | 0,069559 | 8,798 ms | 10,315783% |
| 3 | 384 B | 4x | 0,026860 | 9,250 ms | 5,339393% |
| 6 | 768 B | 2x | 0,002066 | 9,443 ms | 0,001636% |
| 12 | 1.536 B | 1x | 0 | 10,414 ms | 0,001968% |

Exact usa 1.536 B e `12,673 ms`. La cache turn a otto worker misura
`21,289 ms` per 66 partizioni e 744 osservazioni. Il benchmark dimostra il
trade-off memoria/errore su un caso ridotto; non seleziona K per un gioco
grande.

## Validazione eseguita

- build MSVC Release completa: PASS;
- `clang-format --dry-run --Werror` sui file C++ modificati: PASS;
- test mirati abstraction/subgame/preflight/CLI: `6/6 PASS`;
- CTest Release completa: `35/35 PASS` in `208,99 s`;
- benchmark abstraction/subgame, cinque ripetizioni: PASS;
- AHK suite finale, cinque processi più oracle: PASS;
- TH suite, cinque processi più oracle: PASS.

Dopo la suite TH sono cambiati soltanto l'accounting conservativo del
preflight e il percorso di preparazione feature senza cache esterna. Il solve
con cache esterna, CFR+, clustering, traversal e best response misurati da TH
non sono cambiati. AHK, build e CTest sono stati rieseguiti sul sorgente finale.

## Limiti e decisione

- il percorso diretto senza cache esterna costruisce ora internamente la stessa
  cache deterministica a otto worker; non esiste più un fallback feature
  seriale implicito;
- `thread_count=1` resta accessibile soltanto come oracle diagnostico esplicito;
- K è configurabile per workload; AHK K=16 e TH K=128 impediscono un default
  unico non supportato dai dati;
- `.gtsd`, selettore GUI, cache binaria compressa/memory-mapped e preflop
  rappresentativo restano attività distinte; il resolver nativo corrente è
  single-frontier e non implementa ancora multi-root/continual resolving;
- la parity GTO+ non deve reinterpretare Peak RSS come “Memory needed for
  solving”.

## Riproduzione

```powershell
tools/run_card_abstraction_qualification.ps1 `
  -GtoCli out/build/codex-release/apps/gto_cli/gto_cli.exe `
  -Specification benchmarks/fixtures/gto_plus_ahkhqh_101.json `
  -OutputDirectory out/qualification/card-abstraction-8t/ahk-flop-k16-final `
  -Buckets 16 -Iterations 500 -Repetitions 5 `
  -RamBytes 2000000000 -DiskBytes 10737418240

ctest --test-dir out/build/codex-release -C Release --output-on-failure
```

Gli artefatti grezzi restano sotto `out/qualification/card-abstraction-8t/` e
non fanno parte del sorgente versionato.
