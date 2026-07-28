# Fase 5 — Solver laboratory

## 1. Esito

Il gate locale F5 è completato. Il repository dispone di un laboratorio
extensive-form a due giocatori con chance esatta, infoset espliciti, cinque
varianti CFR, best response exact infoset-aware, NashConv general-sum,
checkpoint deterministici e curve di convergenza.

La selezione misurata per i reference game correnti è:

| Ruolo | Algoritmo | Motivazione |
|---|---|---|
| Primario del laboratorio | CFR+ full traversal | NashConv inferiore a CFR e DCFR su Kuhn e Leduc |
| Fallback exact | DCFR parametrico | Convergenza valida, supporto rake e sweep riproducibile |
| Oracle di implementazione | Vanilla CFR | Regret update più semplice e interamente enumerato |
| Ricerca soltanto | External-sampling MCCFR | Varianza misurata; vietato come solver finale exact |

Questo risultato non certifica ancora una strategia GTO per l'albero Short
Deck postflop completo. F5 certifica il motore sugli oracle piccoli; il
collegamento fra public tree F3/F4, range fisici e traversal poker appartiene
alle fasi successive.

## 2. Moduli e API

| Modulo | Responsabilità |
|---|---|
| `gtosd::solver` | Gioco finite extensive-form, validazione, CFR, checkpoint e reference game |
| `gtosd::best_response` | EV exact, best response infoset-aware, NashConv e solve certificato |
| `gto_cli solver-lab` | Curve, EV, BR, NashConv, throughput e dimensione checkpoint |
| `gto_cli dcfr-sweep` | Sweep riproducibile dei parametri `alpha/beta/gamma` |

La versione API è stata incrementata da `0.4.0` a `0.5.0`.

### 2.1 Contratto extensive-form

| Elemento | Contratto |
|---|---|
| Player | Esattamente due, payoff separati |
| Nodi | Terminal, chance o decision |
| Chance | Probabilità finite, positive e con somma `1 ± 1e-12` |
| Infoset | Stesso player, stesso ordine e stessi identificatori di azione |
| Strategia | Distribuzione completa, finita e normalizzata |
| Game fingerprint | FNV-1a 64 bit versionato e deterministico |
| Errori | `SolverError` tipizzato; nessun fallback uniforme o sampled silenzioso |

La strategia corrente viene congelata all'inizio di ogni iterazione. Regret e
strategy delta vengono accumulati separatamente e applicati dopo il traversal,
evitando che una visita successiva dello stesso infoset osservi aggiornamenti
parziali della stessa iterazione.

### 2.2 Algoritmi

| Algoritmo | Regret | Average strategy | Chance |
|---|---|---|---|
| Vanilla CFR | Somma standard | Peso unitario | Enumerazione completa |
| CFR+ | Regret negativo troncato a zero | Peso lineare dopo `averaging_delay` | Enumerazione completa |
| Linear CFR | Delta pesato per iterazione | Peso lineare | Enumerazione completa |
| DCFR | Discount separato positivo/negativo | Discount `gamma` | Enumerazione completa |
| External-sampling MCCFR | Update del traverser | Percorso campionato | Solo laboratorio, un thread |

I full traversal supportano `1`, `2`, `4` e `8` worker. I worker sono
persistenti durante il solve; ciascuno accumula delta locali e la riduzione
avviene in ordine deterministico. MCCFR rifiuta esplicitamente più di un
thread, perché cambiare l'ordine del RNG dietro lo stesso seed violerebbe il
contratto di riproducibilità.

## 3. Best response e NashConv

La best response:

- propaga soltanto reach di chance e avversario;
- raggruppa tutti i nodi appartenenti allo stesso infoset;
- sceglie una sola azione per infoset;
- non usa la carta privata avversaria per differenziare l'azione;
- usa il payoff specifico del responder, anche nei giochi general-sum;
- produce policy e witness dell'azione scelta.

Le metriche restituite sono:

| Metrica | Definizione |
|---|---|
| Profile value | EV dei due player sotto la strategia fornita |
| BR value | EV exact della migliore risposta di ciascun player |
| NashConv | Somma dei due miglioramenti unilaterali |
| Normalized NashConv | `NashConv / initial_pot` |
| Zero-sum exploitability | `NashConv / 2`, soltanto se la somma EV è zero |
| Expected payoff sum | Evidenzia il rake atteso nei giochi general-sum |

Per il rake toy, `zero_sum_exploitability` è deliberatamente `NaN` nell'API e
`not_applicable_general_sum` nella CLI: non viene inventata una metrica
zero-sum per un gioco che non lo è.

## 4. Reference game

| Gioco | Nodi | Infoset | Profondità | Scopo |
|---|---:|---:|---:|---|
| Matching Pennies | 7 | 2 | 2 | Regret matching elementare |
| Kuhn Poker | 55 | 12 | 4 | Informazione imperfetta, oracle analitico e BR |
| Leduc Poker | 9.451 | 288 | 10 | Chance pubblica e due round di betting |
| Short Deck river/rake toy | 19 | 8 | 4 | Showdown F2, size 50%, rake e general-sum |

Il toy Short Deck usa carte fisiche reali:

| Campo | Valore |
|---|---|
| Board | `As Qd 9c 8h 6s` |
| Mano forte | `7d Kc`, scala `A-6-7-8-9` |
| Mano debole | `Ah Qc`, doppia coppia |
| Piatto iniziale | 2 unità |
| Bet | 1 unità, cioè 50% pot |
| Rake test | 5% del pot chiamato |

Il winner mask viene prodotto da `evaluate_showdown`; non è scritto a mano nel
game tree.

Matching Pennies e Kuhn dispongono di strategie analitiche indipendenti dal
solver. Il valore Kuhn atteso è:

```text
EV CO  = -1/18
EV BTN = +1/18
```

Queste fixture coprono l'oracle EV richiesto dal gate. Un adapter OpenSpiel o
un solver sequence-form generalizzato non è una dipendenza del prodotto e
rimane un cross-check test-only futuro; non è stato simulato o dichiarato
eseguito.

## 5. Checkpoint

Il formato `GTOSD_CFR_CHECKPOINT 1.0` conserva:

| Campo | Presente |
|---|---|
| Game fingerprint | Sì |
| Algoritmo e parametri DCFR | Sì |
| Iterazione completata | Sì |
| Seed e stato RNG | Sì |
| Thread count | Sì |
| Regret cumulativi | Sì, `float64` lossless |
| Strategy sum | Sì, `float64` lossless |
| Azioni e ownership infoset | Sì |

Major o minor futuri non conosciuti vengono rifiutati. Un checkpoint non può
essere caricato su un gioco con fingerprint differente. I test confrontano
byte per byte run continuo e resume, sia con un worker sia con due worker.

## 6. Risultati degli esperimenti

### 6.1 Confronto algoritmi

| Gioco e budget | CFR | CFR+ | DCFR default |
|---|---:|---:|---:|
| Kuhn, 20.000 iterazioni | `0,00326525` | **`0,00151855`** | `0,00173130` |
| Leduc, 2.000 iterazioni | `0,0554628` | **`0,00778160`** | `0,00948708` |
| Short Deck rake toy, 20.000 iterazioni | Non selezionato | **`0`** | `8,45e-12` |

I valori sono NashConv; più basso è migliore.

Su Kuhn, CFR/CFR+/DCFR sono stati ripetuti con 10 seed. Essendo traversal
exact, ogni seed ha prodotto lo stesso risultato. MCCFR è stato eseguito con
30 seed e 100.000 iterazioni:

| Algoritmo | Seed | NashConv media | Minimo | Massimo |
|---|---:|---:|---:|---:|
| MCCFR external sampling | 30 | `0,00521521` | `0,00175289` | `0,00849382` |

La varianza conferma il ruolo esclusivamente sperimentale di MCCFR.

### 6.2 Sweep DCFR

| Parametri `(alpha,beta,gamma)` | Kuhn 20k | Leduc 1k |
|---|---:|---:|
| `(1,0,1)` | `0,00221924` | `0,0487649` |
| `(1.5,0,2)` | `0,00173130` | `0,0187726` |
| `(2,0,2)` | **`0,00168992`** | `0,0146559` |
| `(1.5,-0.5,2)` | `0,00180429` | **`0,0140008`** |
| `(1.5,0.5,2)` | `0,00227863` | `0,0318065` |
| `(1.5,0,3)` | `0,00197531` | `0,0192384` |

Non esiste un singolo candidato DCFR vincente su entrambi i giochi; il default
resta `(1.5,0,2)` come baseline documentata, non come vincitore.

### 6.3 Scaling worker

Leduc, CFR+, 1.000 iterazioni:

| Worker | Tempo | Iterazioni/s | NashConv |
|---:|---:|---:|---:|
| 1 | 2,597 s | 385,1 | `0,0128528` |
| 2 | **2,080 s** | **480,7** | `0,0130721` |
| 4 | 2,157 s | 463,6 | `0,0131805` |
| 8 | 2,276 s | 439,4 | `0,0127657` |

Due worker sono il punto migliore di questa fixture. Sul toy da 19 nodi il
barrier overhead domina, quindi un worker resta più veloce. Il runtime non
forza il parallelismo quando non porta beneficio misurato.

### 6.4 Benchmark

Google Benchmark Release, `BM_KuhnDcfrIterations`, 500 iterazioni per sample:

| Metrica | Valore |
|---|---:|
| Mediana | circa 64.000 iterazioni/s |
| Macchina | 8 logical CPU, 3,6 GHz, L3 6 MiB |

È una baseline locale, non uno SLA.

## 7. Gate F5

| Criterio | Esito | Evidenza |
|---|---:|---|
| EV reference entro `1e-6` | PASS | Matching `0`; Kuhn `±1/18` entro `1e-12` |
| BR infoset-aware | PASS | Test negativo con due nodi e una sola azione per infoset |
| NashConv corretta con rake | PASS | Somma EV `-0,1`; CFR+ NashConv `0`; exploitability zero-sum non applicabile |
| Resume equivalente | PASS | Checkpoint finale byte-identico, 1 e 2 worker |
| Algoritmo primario scelto | PASS | CFR+ vince Kuhn e Leduc nel report riproducibile |

## 8. Verifiche

| Verifica | Risultato |
|---|---|
| MSVC Release `/W4 /WX` | PASS |
| Suite Release completa | PASS, 9/9 |
| Suite F5 Release | PASS, 79 asserzioni |
| Suite F5 Debug | PASS |
| Suite F5 MSVC AddressSanitizer | PASS, nessuna diagnostica |
| Suite F5 GCC 13.3 UBSan | PASS con `-fno-sanitize-recover=all` |
| clang-format | PASS, `--dry-run --Werror` |
| clang-tidy | PASS sui moduli F5 e CLI, zero warning first-party finali |
| Install tree | PASS, `gtosd::solver`, `gtosd::best_response`, header e CLI |
| CLI installata | PASS, `self-check` 0.5.0 e Matching Pennies NashConv zero |

I risultati GitHub Actions remoti non vengono dichiarati: la workflow aggiornata
deve ancora essere eseguita dopo un push.

## 9. Comandi

```powershell
.\out\build\windows-release\apps\gto_cli\gto_cli.exe `
  solver-lab kuhn cfr+ 20000 1 1

.\out\build\windows-release\apps\gto_cli\gto_cli.exe `
  solver-lab short-deck-rake-toy cfr+ 20000 1 1

.\out\build\windows-release\apps\gto_cli\gto_cli.exe `
  dcfr-sweep leduc 1000
```

## 10. Prossima fase

La prossima milestone è **Fase 6 — Prototipi memoria exact**. Deve confrontare
lazy in-RAM, street decomposition e out-of-core senza introdurre bucketing o
sampling nel percorso principale.
