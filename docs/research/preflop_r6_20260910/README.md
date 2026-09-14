# R6 — Algoritmo e parallelismo limitato

## Stato

`ENGINEERING_PASS / SCIENTIFIC_GATE_FAIL / V19_GLOBAL_CRN_REJECTED`.

La root decision trace V20 è completa. Sul V17 seed 1 da 2M, Call è l'unica azione con regret
cumulativo positivo per `JTo`, `QJo` e `J9s`, quindi il regret matching assegna a Call il 100%
della policy corrente. La rivalutazione paired congelata, con 2.000 deal per azione, non separa
Call dalla migliore alternativa in nessuno dei sei confronti average/current. Il meccanismo è
identificato, ma l'EV relativo resta inconclusivo. Il contratto completo Monker non sarà
disponibile: le sue frequenze restano diagnostiche e non possono certificare correttezza o
convergenza. Evidenza e lavoro successivo:
[`V20_ROOT_DECISION_TRACE_REPORT_2026-09-14.md`](V20_ROOT_DECISION_TRACE_REPORT_2026-09-14.md) e
[`V20_POST_TRACE_SOLVER_IMPROVEMENT_ROADMAP_2026-09-14.md`](V20_POST_TRACE_SOLVER_IMPROVEMENT_ROADMAP_2026-09-14.md).

Il controllo successivo su V18 seed 1 usa 10.000 deal `AA` per azione. Il vantaggio originario di
Raise 6, misurato su 188 campioni, non si replica: con la continuation media Call precede Raise 6
di `0,2267a`, con IC simultaneo `[-0,1426a; +0,5960a]`. Il risultato è inconclusivo e non
autorizza modifiche al trainer. La best response resta campionata e la NashConv non è certificata.
Protocollo e report:
[`V20_V18_AA_PAIRED_TRACE_PROTOCOL_2026-09-14.md`](V20_V18_AA_PAIRED_TRACE_PROTOCOL_2026-09-14.md) e
[`V20_V18_AA_PAIRED_TRACE_REPORT_2026-09-14.md`](V20_V18_AA_PAIRED_TRACE_REPORT_2026-09-14.md).

La Fase A di V19 aggiunge telemetria action-conditioned opt-in con schema JSON versionato e
verifica OFF/ON bit-identica per policy, regret, strategy sum, EV, contatori e fingerprint. La
Fase B aggiunge CRN globale come percorso sperimentale, con fingerprint dedicato, fixture paired
e riproducibilità fra uno e otto worker. La coppia C2 da `2M + 2M` termina entro 44,1 minuti e
2,81 GB private per seed: il vecchio `memory_failure` proveniva da un run non matched in perfect
recall. Il candidato migliora la WMAE media da `13,9472 pp` a `13,6347 pp`, ma peggiora la TV fra
seed da `10,9453 pp` a `12,1140 pp` e gli errori Call/Fold raggiunti da 7 a 10. Il Gate C fallisce;
V17 resta la baseline. Dettagli:
[`V19_FASE_A_AUDIT_2026-09-13.md`](V19_FASE_A_AUDIT_2026-09-13.md),
[`V19_FASE_B_CRN_AUDIT_2026-09-14.md`](V19_FASE_B_CRN_AUDIT_2026-09-14.md) e
[`V19_COMPLETION_REPORT_2026-09-14.md`](V19_COMPLETION_REPORT_2026-09-14.md).

Il percorso parallelo è verificato, riproducibile e contenuto nei budget. Nessuna candidata raggiunge però una qualità sufficiente rispetto ai range esterni. V18 ha aggiunto due milioni di update preflop con policy postflop congelata ai due milioni di update V15/V17. La WMAE media scende a `13,3838 pp`, un miglioramento del `4,04%`, ma la TV fra seed sale a `13,0159 pp` e l'audit EV peggiora. V18 è respinta; V17 torna a essere la baseline corrente. R7 non può iniziare.

## Implementazione

Il trainer dedicato supporta un batch con policy congelata e da uno a otto worker. Ogni job riceve seed, iterazione e traverser deterministici; legge soltanto la tabella al confine del batch e produce delta sparsi locali. Il thread principale applica i delta nell'ordine stabile dei job. La tabella completa di regret e somme non viene replicata.

I limiti sono espliciti:

- massimo otto worker;
- cap per il numero di aggiornamenti di ogni job;
- budget aggregato conservativo per lo scratch del batch;
- cache di mapping ripartita fra i worker entro il cap globale;
- errore `memory_failure` quando un job esaurisce il proprio buffer.

La CLI espone `--threads`, `--training-batch-iterations`, `--maximum-parallel-updates-per-job` e `--maximum-parallel-scratch-bytes`. Il JSON registra worker, batch, picco degli aggiornamenti e payload scratch osservato. DCFR e refinement preflop vengono rifiutati nel percorso parallelo: i rispettivi clock non hanno una semantica batch verificata.

Il challenger control-variate conserva in una tabella separata e limitata la media dei valori osservati per azione avversaria. La baseline è congelata durante il batch. Per un'azione campionata `a`, lo stimatore restituisce `sum(sigma * b) + v(a) - b(a)`; il campione aggiorna la baseline soltanto durante la riduzione ordinata. Il cap predefinito è 512 MiB e l'esaurimento restituisce `memory_failure`.

Il challenger public-board stratified campiona un board ordinato uniforme per batch. Per ogni traverser percorre senza ripetizioni fino a 465 hole-card combo compatibili e campiona uniformemente la mano avversaria fra le 406 combinazioni residue. Il JSON distingue questo contratto dal deal fisico indipendente. Un batch oltre 465 viene rifiutato.

Il challenger chance-sampled CFR campiona soltanto il deal fisico ed enumera tutte le azioni di entrambi i giocatori. Il regret usa la reach controfattuale avversaria; la media usa la reach del giocatore attivo. È un percorso online separato e non accetta il batch External Sampling.

## Validazione numerica

Il test R6 esegue External Sampling e Linear MCCFR con 1, 2, 4 e 8 worker. Strategia root, EV, errore standard, risposte apprese, NashConv astratta, cardinalità e blueprint preflop risultano bit-identici fra tutti i conteggi di worker e in una replica a otto worker. Il picco osservato nel run External Sampling 50k è 360 aggiornamenti per job e 1.520.160 B di payload scratch, contro un budget di 536.870.912 B.

Un test algebrico enumera tutte le azioni di un nodo e verifica che l'attesa dello stimatore control-variate coincida con il valore esatto. Con baseline perfetta la varianza enumerata scende a zero; con baseline nulla resta positiva. Il test trainer verifica inoltre equivalenza numerica 1/8 worker, payload osservabile e rifiuto della modalità senza policy congelata.

Il test public-board enumera tutte le 465 combo del traverser su un board fisso: non trova collisioni e ricostruisce esattamente la somma di una funzione delle hole card. La soluzione è bit-identica fra uno e otto worker. Il test chance-sampled CFR verifica separatamente i pesi di regret e averaging e completa un solve fisico ridotto con policy valida.

CTest Release:

```text
gtosd_hu_preflop_trainer_dependency_check PASS
gtosd_external_sampling_tests              PASS
gtosd_hu_preflop_sampling_tests          PASS
gtosd_hu_preflop_compiled_tests          PASS
gtosd_hu_preflop_abstraction_tests       PASS
gtosd_hu_preflop_parallel_tests          PASS
6/6 passed
```

La regressione completa del sottosistema preflop passa inoltre `11/11` in `511,53 s`. Il totale include il test esaustivo R0/R1 in `502,97 s`, reference preflight, tree, trainer e decomposizione oltre alla batteria R6 riportata sopra.

Il controllo di dipendenza conferma che il target trainer non collega il solver postflop standalone. Il test rifiuta più di otto worker, esecuzione multi-worker senza batch, DCFR batch, scratch impossibile, buffer per-job esaurito e cap della baseline esaurito.

## Scaling a 50.000 iterazioni

Contratto: `64/256/1.024`, batch 64, stessi seed e stessa valutazione fisica.

| Algoritmo | Worker | Wall | Speedup su 1 worker | Peak private | EV CO ± SE | Errore medio azioni |
|---|---:|---:|---:|---:|---:|---:|
| External Sampling | 1 | 22,006 s | 1,00× | 202.858.496 B | 0,29165 ± 0,23136 | 22,24 pp |
| External Sampling | 2 | 15,851 s | 1,39× | 206.802.944 B | 0,29165 ± 0,23136 | 22,24 pp |
| External Sampling | 4 | 10,646 s | 2,07× | 207.642.624 B | 0,29165 ± 0,23136 | 22,24 pp |
| External Sampling | 8 | 9,188 s | 2,40× | 208.293.888 B | 0,29165 ± 0,23136 | 22,24 pp |
| Linear MCCFR | 1 | 21,671 s | 1,00× | 201.072.640 B | 0,24488 ± 0,22205 | 21,33 pp |
| Linear MCCFR | 2 | 15,580 s | 1,39× | 204.320.768 B | 0,24488 ± 0,22205 | 21,33 pp |
| Linear MCCFR | 4 | 10,553 s | 2,05× | 205.307.904 B | 0,24488 ± 0,22205 | 21,33 pp |
| Linear MCCFR | 8 | 8,518 s | 2,54× | 206.651.392 B | 0,24488 ± 0,22205 | 21,33 pp |

Lo scaling è reale ma sublineare. La qualità è identica fra conteggi di worker perché cambia soltanto l'assegnazione dei job, non l'ordine degli aggiornamenti.

## Confronto qualità a 500.000 iterazioni

| Algoritmo e partizione | Wall | Peak private | EV CO e IC 95% | Errore EV | Errore medio azioni | TV media per classe | Stato |
|---|---:|---:|---:|---:|---:|---:|---|
| External, 8 worker, `64/256/1.024` | 81,540 s | 407.322.624 B | −0,11989 [−0,35514; 0,11537] | 0,18011 ante al punto, target incluso nell'IC | 20,70 pp | 51,74 pp | fail range |
| Linear, 8 worker, `64/256/1.024` | 86,630 s | 416.952.320 B | −0,22146 [−0,45531; 0,01239] | 0,07854 ante al punto, target incluso nell'IC | 21,32 pp | 53,30 pp | fail range |
| DCFR, online, `64/256/1.024` | 149,389 s | 423.247.872 B | 0,00857 [−0,25579; 0,27293] | 0,30857 ante | **17,01 pp** | **42,53 pp** | fail range, fail EV point |
| External, 8 worker, `32/128/512` | 76,192 s | 346.472.448 B | 0,03381 [−0,19919; 0,26681] | 0,33381 ante | 18,76 pp | 46,91 pp | fail range, fail EV point |
| DCFR, online, `32/128/512` | 147,769 s | 359.690.240 B | 0,12016 ± 0,13363 | 0,42016 ante | 17,44 pp | 43,59 pp | fail range, fail EV point |
| External + baseline, 8 worker, `32/128/512` | 96,076 s | 496.095.232 B | −0,24491 [−0,48174; −0,00809] | 0,05509 ante | 18,54 pp | 46,34 pp | fail range; EV inconclusivo rispetto alla soglia 0,05 |

Il comparatore legacy marca tutti i run `REJECTED` anche perché richiede NashConv certificata. Qui i suoi valori di distanza sono usati soltanto come diagnostica; la configurazione del riferimento esterno resta incompleta.

Dopo il chiarimento semantico su ante e button blind, tutti i report R5/R6 sono stati rigenerati contro il fingerprint `fnv1a64:e8c2cecc49fb63d2`. Le metriche non cambiano: il chiarimento descrive la composizione dei 2a del BTN e il costo incrementale di 1a del call CO, già rappresentati nello stato numerico.

## Diagnosi della capacità

| Capacità | Infoset | Payload numerico | Peak private | Errore medio azioni |
|---|---:|---:|---:|---:|
| `2/8/32` | 96.373 | 17.512.128 B | 166.445.056 B | 26,52 pp |
| `8/32/128` | 411.390 | 72.310.176 B | 228.683.776 B | 20,01 pp |
| `32/128/512` | 1.026.407 | 168.259.968 B | 346.472.448 B | **18,76 pp** |
| `64/256/1.024` | 1.273.587 | 206.811.504 B | 407.322.624 B | 20,70 pp |

La capacità minima perde informazione; quella massima diluisce il training. `32/128/512` è il miglior compromesso osservato per External Sampling, ma non risolve lo scarto strutturale. DCFR migliora le frequenze sulla partizione più grande al costo di 1,83× il wall e senza compatibilità EV conclusiva.

## Esperimenti successivi al primo gate fallito

| Candidato | Iterazioni | Wall | Peak private | EV CO ± SE | Errore medio azioni | Esito |
|---|---:|---:|---:|---:|---:|---|
| Public-board stratified, External, `32/128/512` | 500.000 | 73,392 s | 346.337.280 B | −0,25467 ± 0,12065 | 20,00 pp | EV point pass; fail range |
| DCFR, `64/256/1.024` | 2.000.000 | 575,805 s | 496.668.672 B | −0,06698 ± 0,12202 | **16,94 pp** | fail range; fail EV point |
| Bucket-history DCFR, `8/32/128` | 500.000 | 142,740 s | 460.066.816 B | 0,16229 ± 0,13323 | 17,32 pp | fail range; fail EV point |
| Balanced-strength DCFR, `32/128/512` | 500.000 | 152,561 s | 466.497.536 B | −0,07887 ± 0,13258 | 17,64 pp | fail range; mapping respinto |
| DCFR fine, `256/1.024/4.096` | 500.000 | 154,362 s | 531.345.408 B | −0,01061 ± 0,13541 | 17,46 pp | fail range; fail EV point; peggiore del candidato standard |
| DCFR + refinement preflop congelato, `64/256/1.024` | 500.000 + 500.000 | 183,962 s | 430.219.264 B | 0,04819 ± 0,13051 | 17,75 pp | fail range; il refinement conserva l'errore delle continuazioni |
| Chance-sampled full-action CFR, `32/128/512` | 1.000 | 19,728 s | 230.666.240 B | 0,48822 ± 0,29486 | 24,36 pp | fail range; stato cresce troppo per la qualità |

Un controllo accoppiato a 100.000 update ha confrontato inoltre MC8 e MC32 sulla partizione `64/256/1.024`. MC8 ottiene 19,24 pp in 35,566 s e 286.560.256 B private; MC32 ottiene 19,84 pp in 103,238 s e 289.787.904 B private. Aumentare i campioni delle feature quadruplica gli showdown di mapping, triplica quasi il wall e peggiora di 0,60 pp il criterio primario.

Il passaggio DCFR da 500.000 a 2.000.000 iterazioni migliora l'errore di soli 0,07 punti. Il public-board sampling chiude il gate EV puntuale ma peggiora le frequenze. Conservare la storia dei bucket, conservare tutto il perfect recall, redistribuire le feature, aumentare la capacità a `256/1.024/4.096` o portare le feature da 8 a 32 campioni non produce un vantaggio; il mapping v2 MC8 resta quello selezionato.

## Stabilità fra seed

Una seconda traiettoria conserva partizione e protocollo ma usa seed indipendenti per training e valutazione:

| Iterazioni | Seed | Wall | Peak private | EV CO ± SE | Errore medio azioni |
|---:|---|---:|---:|---:|---:|
| 500.000 | primario | 149,389 s | 423.247.872 B | 0,00857 ± 0,13488 | 17,01 pp |
| 500.000 | indipendente | 139,997 s | 419.385.344 B | 0,18602 ± 0,13430 | 17,45 pp |
| 2.000.000 | primario | 575,805 s | 496.668.672 B | −0,06698 ± 0,12202 | **16,94 pp** |
| 2.000.000 | indipendente | 554,068 s | 498.163.712 B | −0,10686 ± 0,11992 | 17,39 pp |

Le due policy root distano 7,33 pp WMAE a 500k e 5,90 pp a 2M. La loro media diagnostica, costruita senza consultare il target, ottiene 17,02 pp contro il riferimento e non supera il miglior seed. Non è una candidata: non combina la policy delle continuazioni e il suo EV non è stato valutato come profilo completo.

## Follow-up Linear MCCFR

Gli esperimenti successivi hanno fissato batch 32 e partizione `32/128/512`. Due run Linear MCCFR 4M producono:

| Seed | Solve | Peak private | Errore medio azioni | TV media | P95 TV | Massimo errore root aggregato |
|---|---:|---:|---:|---:|---:|---:|
| 1 | 734,178 s | 435.499.008 B | 16,4997 pp | 41,2492 pp | 89,9526 pp | 23,0471 pp |
| 2 | 735,201 s | 434.880.512 B | 15,0324 pp | 37,5810 pp | 92,8990 pp | 23,0498 pp |
| Media | — | — | **15,7661 pp** | 39,4151 pp | — | 23,0485 pp |

La distanza fra i due seed è 8,7998 pp WMAE. Il candidato migliora il vecchio Linear 4M di 0,9013 pp, ma resta lontano dai gate finali di 1 pp WMAE, 2 pp TV media, 5 pp P95 e 1 pp root aggregata.

Gli screen su baseline control-variate, public-board stratified, MC16, capacità più piccole o più grandi, bucket history e perfect recall non hanno prodotto un vantaggio ripetibile. I report dedicati conservano parametri, confronti e ragioni di esclusione.

L'audit del mapping ha mostrato che il legacy `32/128/512` conserva soprattutto categoria ed equity: le feature distribuzionali meno significative non raggiungono i bucket a queste capacità. Il challenger versionato `DistributionalStrengthProfileV6` conserva tutti i quattro bit di equity e usa i bit residui per un hash delle altre feature. Su due seed a 100.000 iterazioni peggiora la WMAE media da 18,6477 a 19,3515 pp, la TV da 46,6193 a 48,3787 pp e usa 2,20 volte gli infoset. È quindi escluso; il formato policy passa a `1.5` e il mapping legacy resta invariato. Un replay corrente conferma che la strategia legacy seed 1 è bit-identica prima e dopo l'estensione. Dettagli in [DISTRIBUTIONAL_PROFILE_V6_GATE_2026-09-11.md](DISTRIBUTIONAL_PROFILE_V6_GATE_2026-09-11.md).

## Diagnostica EV root

Il runner esporta ora EV, errore standard e campioni per tutte le cinque azioni root e le 81 classi. Due valutazioni fisiche da 200.000 deal mostrano fold esatto a −1 ante e bias aggregati medi contenuti rispetto agli EV Monker: +0,0075 all-in, −0,0371 raise-to 6a, −0,2322 raise-to 10a e +0,0960 call.

Le frequenze restano molto diverse perché molte azioni hanno valore vicino. Applicando la tabella EV Monker alle strategie 4M, la perdita locale rispetto alla migliore azione Monker è 0,0375 ante per entrambi i seed, nonostante 15,7661 pp di errore medio nelle frequenze. Questo controllo è diagnostico: non prova che gli alberi postflop coincidano e non sostituisce una valutazione del profilo locale.

Dettagli e tabella completa: `ROOT_ACTION_EV_DIAGNOSTIC_IMPLEMENTATION_2026-09-11.md` e `CO_MONKER_LINEAR_MCCFR_500K_ROOT_ACTION_EV_COMPARISON.md`.

Il confronto delle frequenze Monker con il profilo v6, aggregato per azione e completo per tutte le 81 combo, è in [CO_MONKER_PROFILE_V6_ROOT_STRATEGY_COMPARISON_2026-09-11.md](CO_MONKER_PROFILE_V6_ROOT_STRATEGY_COMPARISON_2026-09-11.md).

Il challenger successivo è congelato prima del codice in [DISTRIBUTIONAL_STRUCTURED_V7_PROTOCOL_2026-09-11.md](DISTRIBUTIONAL_STRUCTURED_V7_PROTOCOL_2026-09-11.md). Conserva la categoria corrente, sostituisce l'hash v6 con coordinate ordinate e impone uno screen progressivo a due seed.

Il relativo [gate v7](DISTRIBUTIONAL_STRUCTURED_V7_GATE_2026-09-11.md) è `ENGINEERING_PASS / SCIENTIFIC_GATE_FAIL`: migliora il legacy a 100.000 iterazioni, ma peggiora WMAE e TV a 250.000. Il run da 500.000 non è autorizzato dal protocollo e R7 resta bloccato.

Successivamente, l'utente ha riaperto esplicitamente il budget per produrre chart complete V6/V7 ad almeno 2M. Quattro run diagnostici a 2.000.000 di iterazioni, due seed per profilo, sono stati rigenerati sul contratto monetario v2 ed esportano tutti i 20 nodi preflop con EV condizionati per combo. V7 ottiene 18,3298 e 18,1872 pp WMAE; V6 18,8595 e 18,5052 pp. L'estensione non cambia il verdetto scientifico: tutti i confronti restano `REJECTED / REFERENCE_CONFIG_INCOMPLETE` e R7 rimane bloccato. Protocollo e risultati sono in [PREFLOP_FULL_TREE_CHART_EXPORT_PROTOCOL_2026-09-11.md](PREFLOP_FULL_TREE_CHART_EXPORT_PROTOCOL_2026-09-11.md), [V6_V7_2M_MONETARY_V2_RERUN_2026-09-11.md](V6_V7_2M_MONETARY_V2_RERUN_2026-09-11.md) e [CHART_VIEWER_IMPLEMENTATION_2026-09-11.md](CHART_VIEWER_IMPLEMENTATION_2026-09-11.md).

La [documentazione del chart viewer](CHART_VIEWER_IMPLEMENTATION_2026-09-11.md) descrive il sito statico navigabile, le convenzioni EV, le sorgenti incluse e i limiti di comparabilità.

## Diagnosi v7 e challenger v8

La telemetria dei rimpianti dimostra che le percentuali della chart non derivano dagli EV post-hoc. Per AA nella run v7 seed 2 a 2M, soltanto call conserva rimpianto cumulativo positivo all'ultimo aggiornamento: la strategia corrente è 100% call, mentre la media storica lineare è 95,5306% call. Gli EV della schermata usano invece 189 deal per azione; call è il valore puntuale più alto, ma supera raise 6a di appena 0,3086a con SE marginali di circa 1,6a.

La divergenza v7 tra seed scende da 35,3724 pp a 250k a 27,9000 pp a 2M, ma le singole traiettorie cambiano ancora di 17,5009 e 18,7075 pp tra 1M e 2M. La TV tra le strategie correnti è ancora più alta, 30,9242 pp, con azione dominante diversa in 27/81 classi: l'averaging attenua il problema, non lo causa. Aggiornamento online, baseline control-variate e averaging non lineare non raggiungono la riduzione minima del 20% e peggiorano l'accordo con Monker. Dettagli in [V7_SEED_DIVERGENCE_GATE_2026-09-11.md](V7_SEED_DIVERGENCE_GATE_2026-09-11.md).

Il challenger street-adaptive v8 usa 29/121–123/198–201 bucket contro 15/56/136–138 del v7. A 250k, però, peggiora la WMAE media da 17,9530 a 19,9818 pp e riduce la TV tra seed soltanto dell'1,28%. Il gate vieta quindi la conferma a 500k. Protocollo e risultati sono in [DISTRIBUTIONAL_STREET_ADAPTIVE_V8_PROTOCOL_2026-09-11.md](DISTRIBUTIONAL_STREET_ADAPTIVE_V8_PROTOCOL_2026-09-11.md) e [DISTRIBUTIONAL_STREET_ADAPTIVE_V8_GATE_2026-09-11.md](DISTRIBUTIONAL_STREET_ADAPTIVE_V8_GATE_2026-09-11.md).

La telemetria pesata del vantaggio d'azione root è definita in [V7_ROOT_ACTION_ADVANTAGE_VARIANCE_PROTOCOL_2026-09-11.md](V7_ROOT_ACTION_ADVANTAGE_VARIANCE_PROTOCOL_2026-09-11.md) e verificata in [V7_ROOT_ACTION_ADVANTAGE_VARIANCE_GATE_2026-09-11.md](V7_ROOT_ACTION_ADVANTAGE_VARIANCE_GATE_2026-09-11.md). Le replay 2M sono bit-identiche alle policy precedenti e ricostruiscono i regret con errore zero. Tutte le 27 classi con azione corrente discordante hanno un margine inferiore a due errori standard accoppiati in almeno un seed; 57/81 e 62/81 classi non separano nemmeno la migliore dalla seconda azione a quella soglia. Il prossimo esperimento deve quindi ridurre la varianza dei valori root prima di cambiare ancora astrazione.

Il controllo-varianza [root multi-rollout](V7_ROOT_MULTI_ROLLOUT_PROTOCOL_2026-09-11.md) media quattro continuation value senza applicare gli update shadow agli infoset profondi. Lo screen 250k fallisce: il SE mediano scende del 50,52%, ma TV fra seed e WMAE peggiorano. Su richiesta esplicita, l'[esito](V7_ROOT_MULTI_ROLLOUT_GATE_2026-09-11.md) include anche due conferme a 2M. Il budget lungo cambia il verdetto comparativo: TV fra seed da 27,9000 a 16,6353 pp, SE mediano −51,30% e WMAE media da 18,2585 a 16,9571 pp. K=4 resta disattivato per default e non supera il gate R6 di 1 pp, ma è il challenger di ricerca selezionato.

Il follow-up [continuation-mean CO](V7_ROOT_CONTINUATION_MEAN_UPDATE_PROTOCOL_2026-09-11.md) applica con peso `1/4` anche i delta postflop dei quattro rollout. Lo [screen 250k](V7_ROOT_CONTINUATION_MEAN_UPDATE_GATE_2026-09-11.md) riduce la TV fra seed del 17,41%, ma peggiora la WMAE media da 18,9918 a 23,0776 pp. La variante CO-only è respinta e non passa a 2M; il prossimo controllo deve mediare simmetricamente anche il pass BTN.

La [media simmetrica delle traversate](V7_SYMMETRIC_TRAVERSER_MEAN_PROTOCOL_2026-09-11.md) recupera la regressione CO-only. Il relativo [gate](V7_SYMMETRIC_TRAVERSER_MEAN_GATE_2026-09-12.md) produce a 2M la migliore WMAE sul contratto corrente, 14,8844 pp, contro 16,9571 pp del K=4 read-only. La TV fra seed peggiora però da 16,6353 a 18,6455 pp e il costo relativo è 1,54× a 2M. Il candidato resta sperimentale; una proiezione a 4M è ancora lontana dal gate e non giustifica un altro raddoppio.

Il [retest v8 con traverser simmetrici](V8_SYMMETRIC_TRAVERSER_PROTOCOL_2026-09-12.md) isola il mapping con lo stesso trainer K=4. Lo [screen 250k](V8_SYMMETRIC_TRAVERSER_GATE_2026-09-12.md) conferma il rifiuto: WMAE media `18,7001 → 19,3457 pp` e TV fra seed `25,8938 → 26,4529 pp`. I due artefatti includono tutti i 20 nodi e gli EV per combo, ma il gate vieta il passaggio a 2M.

Il [capacity uplift v7](V7_PROFILE_CAPACITY_UPLIFT_PROTOCOL_2026-09-12.md) conserva la categoria esatta e aggiunge un bit di forza e uno di profilo per street con capacità `128/512/2048`. Anche questo [screen](V7_PROFILE_CAPACITY_UPLIFT_GATE_2026-09-12.md) fallisce: WMAE media `19,5810 pp`, TV fra seed `30,7244 pp` e payload massimo `271.061.856 B`. Più risoluzione frammenta gli update senza migliorare la root; nessun run 2M è autorizzato.

La [selective-history v9](DISTRIBUTIONAL_SELECTIVE_HISTORY_V9_PROTOCOL_2026-09-12.md) conserva il bucket corrente e quello della street precedente. Build, persistenza 1.8 e CTest preflop `13/13` passano, ma il [primo screen 250k](DISTRIBUTIONAL_SELECTIVE_HISTORY_V9_GATE_2026-09-12.md) raggiunge 5.406.120 infoset e `783.548.208 B` di payload numerico. Il gate RAM ferma seed 2 e run più lunghi. La memoria successiva deve essere più compatta di un bucket intero.

L'albero postflop strutturale può essere ricostruito dal tree builder. Le frequenze postflop richiedono una nuova run con export della policy sparsa; gli EV per azione richiedono inoltre una valutazione dedicata. Il viewer dovrà caricare i rami in modo lazy per board e action path, senza materializzare l'intero spazio fisico in un unico JSON.

## Enumerazione all-in esatta V12/V13

V12 sostituisce il runout campionato degli all-in preflop con una tabella esatta `81×81`. V13
estende l'attesa esatta agli all-in chiusi sul Flop e sul Turn: `406` completamenti dal Flop, `28`
dal Turn e uno showdown dal River. Una cache bounded lazy, canonicalizzata sulle 24 permutazioni
globali dei semi, evita di materializzare miliardi di stati fisici.

La coppia V13 da 2M riduce la TV fra seed a `12,6370 pp`; l'audit corrente-contro-corrente non
trova azioni Call/Fold materialmente inferiori sui rami con reach pubblica almeno `1%`. Il gate
esterno resta fallito: WMAE media `14,0837 pp`, TV media `35,2092 pp` e P95 media `96,7074 pp`.
Il viewer usa V13 seed 2 come sorgente iniziale perché è il seed 2M più vicino al riferimento, ma
la marca esplicitamente `RESEARCH / NOT QUALIFIED`.

Protocollo e risultati: [protocollo V13](EXACT_POSTFLOP_ALL_IN_V13_PROTOCOL_2026-09-12.md) e
[gate V13](EXACT_POSTFLOP_ALL_IN_V13_GATE_2026-09-12.md).

V14 K=8 ha impiegato `139,43 minuti`, ma il lancio aveva omesso la tabella precalcolata a sette
carte e usava l'oracle combinatorio. Il confronto di costo con V13 era quindi confuso e non misura
il costo isolato di K=8. V15 ripristina esplicitamente lo stesso evaluator di V13 e usa Common
Random Numbers con K=4. I due seed 2M richiedono `39,15` e `35,58 minuti`: la TV media fra seed
scende a `10,9453 pp` e quella corrente a `11,1472 pp`, ma entrambe restano sopra `5 pp`. La WMAE
media migliora solo a `13,9472 pp`. V15 conserva l'audit corrente sui rami pubblicamente raggiunti,
ma fallisce il gate e non sostituisce V13 nel viewer.

Protocollo e risultati: [protocollo V15](ROOT_COMMON_RANDOM_NUMBERS_V15_PROTOCOL_2026-09-13.md),
[gate V15](ROOT_COMMON_RANDOM_NUMBERS_V15_GATE_2026-09-13.md) e
[decomposizione TV V15](V15_SEED_TV_DECOMPOSITION_2026-09-13.md).

V16 stratifica la prima risposta avversaria nei quattro rollout senza aumentare il costo. La
[specifica preregistrata](ROOT_FIRST_RESPONSE_STRATIFICATION_V16_PROTOCOL_2026-09-13.md),
l'[implementazione](ROOT_FIRST_RESPONSE_STRATIFICATION_V16_IMPLEMENTATION_2026-09-13.md) e il
[gate](ROOT_FIRST_RESPONSE_STRATIFICATION_V16_GATE_2026-09-13.md) documentano due nuove run da 2M.
Il tempo resta `36,30–38,19 minuti`, ma la TV media fra seed peggiora a `13,0557 pp` e quella
corrente a `12,9262 pp`. La WMAE media scende a `13,6670 pp`, ancora lontana da `5 pp`. L'audit
corrente sui rami con reach pubblica almeno `1%` passa; V16 resta disattivata e non entra nel
viewer.

V17 separa finalmente il profilo medio dal profilo corrente lungo tutto l'albero. Il formato
postflop `1.11` salva entrambe le policy, mantiene leggibili i file `1.5–1.10` e rifiuta una query
corrente quando il dato storico non esiste. Build Release e suite HU `13/13` passano. Le due run
2M sono bit-identiche a V15 per la strategia appresa e richiedono `37,25/35,03 minuti` di solve.
L'audit Call/Fold corrente non trova violazioni con reach pubblica almeno `1%`, ma l'audit generale
del singolo iterato trova `7/4` azioni inferiori ancora separate dopo la correzione Bonferroni.
La TV resta `10,9453 pp` per la media e `11,1472 pp` per la corrente; la WMAE resta
`14,1312/13,7631 pp`. V17 è quindi diagnostica, non una nuova soluzione del viewer.

Protocollo, implementazione e verdetto sono in
[FULL_CURRENT_PROFILE_EVALUATION_V17_PROTOCOL_2026-09-13.md](FULL_CURRENT_PROFILE_EVALUATION_V17_PROTOCOL_2026-09-13.md),
[FULL_CURRENT_PROFILE_EVALUATION_V17_IMPLEMENTATION_2026-09-13.md](FULL_CURRENT_PROFILE_EVALUATION_V17_IMPLEMENTATION_2026-09-13.md) e
[FULL_CURRENT_PROFILE_EVALUATION_V17_GATE_2026-09-13.md](FULL_CURRENT_PROFILE_EVALUATION_V17_GATE_2026-09-13.md).

V18 congela la policy postflop dopo i due milioni di update V15/V17 e continua per altri due
milioni aggiornando soltanto i 20 nodi preflop. Engineering, integrità e due run complete passano.
Il seed 1 migliora WMAE e TV esterna del `4,27%`, il seed 2 del `3,80%`; la media accoppiata è
`4,04%`, sotto il gate richiesto del `10%`. La TV fra seed peggiora del `18,92%` e l'audit esatto
Call/Fold sui percorsi pubblicamente raggiunti passa da 7 a 8 casi. V18 viene respinta come
candidata principale. Gli artefatti restano disponibili per riprodurre il confronto, ma non
guidano il prossimo esperimento.

La strategia accumulata soltanto nella seconda fase ottiene `13,3997 pp` WMAE e `16,6728 pp` TV
fra seed. Più iterazioni con lo stesso stimatore K=1 non mostrano quindi una traiettoria verso il
10%. Il prossimo candidato proposto riparte dal trainer V17 e usa Common Random Numbers fra tutte
le azioni preflop durante il training principale, mantenendo il postflop adattivo. Non cambia
l'albero e non usa le frequenze Monker durante il training. Non è stato implementato né avviato.

Protocollo, implementazione, gate e decomposizione sono in
[FROZEN_POSTFLOP_PREFLOP_REFINEMENT_V18_PROTOCOL_2026-09-13.md](FROZEN_POSTFLOP_PREFLOP_REFINEMENT_V18_PROTOCOL_2026-09-13.md),
[FROZEN_POSTFLOP_PREFLOP_REFINEMENT_V18_IMPLEMENTATION_2026-09-13.md](FROZEN_POSTFLOP_PREFLOP_REFINEMENT_V18_IMPLEMENTATION_2026-09-13.md),
[FROZEN_POSTFLOP_PREFLOP_REFINEMENT_V18_GATE_2026-09-13.md](FROZEN_POSTFLOP_PREFLOP_REFINEMENT_V18_GATE_2026-09-13.md) e
[V18_FROZEN_POSTFLOP_PREFLOP_REFINEMENT_SEED_TV_DECOMPOSITION_2026-09-13.md](V18_FROZEN_POSTFLOP_PREFLOP_REFINEMENT_SEED_TV_DECOMPOSITION_2026-09-13.md).
La decisione successiva è in
[V18_REJECTION_AND_NEXT_GATE_DECISION_2026-09-13.md](V18_REJECTION_AND_NEXT_GATE_DECISION_2026-09-13.md).

## Decisione

R6 non è chiuso. V18 seed 2 ottiene il minimo singolo corrente, `13,2400 pp`, ma la versione è
respinta perché migliora la WMAE media soltanto del `4,04%` e peggiora TV fra seed e audit EV.
V17 torna a essere la baseline: la sua strategia media coincide con V15, conserva la migliore TV
fra seed della linea corrente (`10,9453 pp`) ed espone anche la policy corrente completa. Il
precedente Linear MCCFR 4M a `15,7661 pp` usa il vecchio fingerprint e resta evidenza storica.
External Sampling resta la baseline algoritmica verificata.

V21 aggiunge la prima calibrazione NashConv end-to-end su una fixture Short Deck enumerabile che
attraversa tutte le street. La NashConv esatta scende da `0,0014765486` a `0,0001654188`; resume,
best response e normalizzazione passano i gate. Un probe sulla policy V17 reale introduce uno
snapshot immutabile e un evaluator River vettoriale `99,82x` più rapido dell'oracolo scalare,
ma non certifica ancora il gioco CO40: 322.199.856 root River richiedono un reducer batch con
riuso fra board e shape.

Dettagli: [protocollo V21](V21_WHOLE_GAME_NASHCONV_CALIBRATION_PROTOCOL_2026-09-14.md) e
[report V21](V21_WHOLE_GAME_NASHCONV_IMPLEMENTATION_REPORT_2026-09-14.md).

V22 rende il probe riutilizzabile per configurazioni HU diverse, incluso un futuro stack 50a, e
aggiunge a ogni solve una stima NashConv esplicitamente non certificante. Sul checkpoint V17, il
riuso di query, chiavi River e inizializzazione riduce profilo più BR da `0,8421401 s` a
`0,2410329 s` per root senza modificare gli EV. La proiezione esatta resta però `2,46 anni`
seriali: la fase 1 si chiude con `INFEASIBLE_EXACT_ROOT_BY_ROOT`, non con una certificazione CO40.

Dettagli: [protocollo V22](V22_GENERIC_NASHCONV_CERTIFIER_PROTOCOL_2026-09-14.md) e
[report fase 1 V22](V22_GENERIC_NASHCONV_CERTIFIER_PHASE1_REPORT_2026-09-14.md).

Il [reducer board-batched V22](V22_CROSS_ROOT_BOARD_BATCHED_REDUCER_REPORT_2026-09-14.md) valuta
insieme 633 history sullo stesso board e riduce la loro materializzazione da circa `493 s` a
`0,814670 s`. La traversata resta però `0,313943019 s` per sottogioco: il solo River proietta
`3,2053 anni` seriali e fallisce il gate exact con `INFEASIBLE_EXACT_BOARD_BATCHED`.

Il blocker residuo è la varianza dei valori d'azione campionati, la rappresentazione delle continuazioni o la comparabilità del riferimento esterno. Sono già stati provati capacità, recall, feature, sampling, baseline, schedule, averaging e durata. V18 mostra che lo stimatore sequenziale K=1 non separa in modo stabile le azioni quasi equivalenti: il `94,39%` della TV media fra seed ricade su gap EV al massimo `0,1a`. Il prossimo gate richiede TV media fra seed non superiore a `9,8508 pp`, WMAE non superiore a `13,9472 pp` e nessuna regressione negli audit EV. Il chiarimento su ante e button blind ha richiesto la correzione del ledger, ora coperta da test e dal fingerprint `fnv1a64:a68337fa567aa2d9`. Il contratto postflop Monker completo non sarà disponibile; restano ignoti versione del solver, bucket, abstraction dipendenti dalla history, stopping rule, metrica di convergenza, CPU, RAM e tempo preciso. Da V20 il confronto Monker è quindi descrittivo e non può bloccare o autorizzare una promozione. R7 richiede una candidata stabile e gate interni di EV e convergenza.
