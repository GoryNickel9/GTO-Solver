# Validazione

## Principio

Una percentuale plausibile non valida un solver. Le prove seguono una gerarchia
in cui ogni livello controlla errori differenti:

1. formula e convenzioni documentate;
2. unit test e proprietà;
3. enumerazione esaustiva quando fattibile;
4. giochi noti e implementazione di riferimento;
5. best response/NashConv;
6. differenziale tra percorsi equivalenti;
7. confronto esterno con fixture completa;
8. workflow installato e riproducibilità.

## Oracoli interni

- L'evaluator viene confrontato con un oracle indipendente ed enumerazioni.
- Kuhn e Leduc validano CFR, averaging, checkpoint e best response.
- Il public tree fisico è l'oracolo per chance tree canonico e isomorfismo lossless.
- Il compilatore layout-only verifica separatamente i due range, restringe lo
  stabilizzatore a ogni chance card e pubblica conteggi per street. Il test
  asimmetrico `TsTc9d` richiede 2 automorfismi, 25 turn canonici e 664 runout
  ordinati canonici; il mapping inverso dei CFV resta un gate del traversal.
- Single-thread/parallel, `Float64`/`Float32` e resume/continuous sono confronti
  differenziali con tolleranze esplicite.
- La somma dei payoff deve riflettere zero-sum o rake, mai una compensazione UI.
- Il bucketing viene validato contro card removal, determinismo, serializzazione,
  riduzione reale degli infoset e NashConv della strategia rialzata sul gioco
  non astratto.
- La feature cache 1.0 viene validata con ordine d'input differente,
  round-trip bit-exact, corruzione rilevata e replace atomico. Nel bridge nativo
  il percorso diretto e quello cache devono avere fingerprint di gioco e
  checkpoint bit-identici; una cache da config/range differenti viene rifiutata.
- Lo sweep turn `K={1,2,3,6,12}` riusa la stessa cache, richiede stato solver
  non decrescente e weighted MSE non crescente fino all'identità senza errore.
- Il bridge HU postflop aggiunge range frazionari/asimmetrici, aggregazione
  reach-weighted, API exact/abstracted non intercambiabili, checkpoint
  Float64 round-trip, resume bit-identico, query combo→bucket e rifiuto di
  precisione/algoritmo/parallelismo non qualificati.
- Il test CLI costruisce atomicamente il manifest, risolve e riprende un fixed
  river bucketed riusando la cache, verifica fingerprint e tempi nel report e
  richiede query e certificazione combo-level exact; checkpoint exact o
  granularità differente devono fallire per fingerprint mismatch.
- Un frontier subgame che taglia un information set viene rifiutato. Il guard
  safe accetta il candidato soltanto dopo due best response full-game esatte;
  il test di regressione verifica anche il fallback a blueprint invariato.
- Le prove di prodotto devono usare il backend CPU/RAM-only. L'accelerazione
  GPU della GUI non può partecipare a tree building, traversal, best response
  o certificazione e non può essere inclusa in un risultato solver.

## Gate GTO+

La suite corrente comprende `GTP-AHKHQH-101` (equivalente v4 dello scenario
congelato `003`), `GTP-TH7D6S-101` e `GTP-TSTC9D-101`. AHK usa flop `Ah Kh Qh`, range identici
`AA-QQ, AKs-AQs, KQs, AKo-AQo, KQo`, 36 combo fisiche per player, pot 40,
stack 100, bet/raise 50%, un raise per street, all-in `Go` alla soglia corretta,
rake e smoothing disabilitati.

I gate prestazionali della suite sono:

- tempo GTOSD a dEV comparabile non superiore a 1,900000 / 19,622222 /
  128,988889 s; per TSTC9D il riferimento grezzo è il primo punto GTO+
  strettamente sotto soglia, `116,09 s` a `0,91%` (`0,146 ante`);
- correttezza e tempo valutati separatamente sullo stesso checkpoint;
- memoria GTO+ in stato `NOT_EVALUATED_COMPARABILITY_UNRESOLVED`: `8 MB`,
  `399 MB` e `2.000 MB` sono i valori visualizzati da “Memory needed for
  solving”, non soglie Peak RSS;
- `solver_state_bytes`, Peak RSS e private bytes pubblicati separatamente come
  diagnostica, senza un cap desktop implicito;
- fixture v4 obbligatorie nel wrapper multiprocesso; report v4 con
  `memory_comparison.status=not_evaluated`, `passed=null`, vettori residenti e
  nessun budget derivato dal riferimento GTO+;
- cinque processi indipendenti per la promozione temporale finale e, in futuro,
  per una promozione memoria soltanto dopo equivalenza semantica dimostrata.

Il final-head production 2026-09-01 usa cinque processi indipendenti, ciascuno
con tutti e tre i benchmark. Passa dEV, root EV, layout, exact outcomes,
e i controlli strutturali su `15/15` solve. Iterazioni e dEV
deterministici AHK/TH/TST sono `80/0,951423%`, `80/0,807956%` e
`160/0,904505%`; mediane/p95 solver `0,758705/0,790918 s`,
`19,948228/24,192260 s`, `184,095930/197,865030 s`. TH e TST falliscono il
tempo rispettivamente del `1,661%` e `42,722%` sulla mediana. Le precedenti
classificazioni memoria di quel checkpoint sono ritirate.

Il recheck Release del 2026-09-03 conserva iterazioni, dEV e correttezza e
misura Peak RSS AHKHQH max cinque processi `7.790.592 B`, TH7D6S max cinque
processi `363.569.152 B`, TSTC9D full convergence `1.534.152.704 B`. Questi
sono contatori OS diagnostici e non dimostrano parità con il campo GTO+.
L'uso page-backed di AHK era selezionato dal riferimento da 8 MB erroneamente
trattato come working-set target; la classificazione `3/3 PASS` è quindi
invalida. I gate tempo TH/TST restano rossi.

Gli EV BTN dopo check/bet restano diagnostici nel percorso non vincolato perché
GTO+ e GTOSD producono posteriori CO differenti. F10.4 è stata implementata e
validata come esperimento `diagnostic_external_root_lock`: con il root CO
bloccato combo-per-combo i delta BTN documentati sono `+0,0348` e `+0,0366`
ante. Questo classifica il mismatch, ma non dimostra equilibrio del gioco
originale e non costituisce node locking di prodotto.

## Convergenza comparabile

Ogni report deve distinguere target, definizione e valore raggiunto. Nel run
GTO+ il target reale 0,10% non è stato raggiunto: dEV era circa 0,11% dopo 245
secondi. Il riferimento 4,20 s appartiene a dEV 0,19%, non a 0,10%.

Una misura censurata viene riportata come `>245 s`. Numero di iterazioni e
tempo senza certificazione non sono equivalenti a convergenza.

La suite comparativa corrente non configura `maximum_iterations`: usa la
modalità target-driven del solver core e richiede `Target dEV < 1%` con
confronto stretto. L'intervallo di certificazione determina soltanto quando
viene calcolata la exact best response dopo che l'averaging ha almeno un
campione; prima di `averaging_delay + 1` le sole certificazioni ammesse sono
quelle forzate da termine finito, pausa o cancellazione. Nel final-head TSTC9D
corrente il solver continua senza iteration cap e termina a 160 con 0,904505%;
valori intermedi sopra 1% non possono arrestarlo.

## Evidenza richiesta

Un gate scientifico registra commit pulito, compiler/flags, hardware, thread,
fixture hash, precisione, algoritmo, averaging, target, valore finale, tempi
individuali, mediana/p95, memoria solver-owned/transient/processo separata e
suite Release. Per la memoria esterna registra anche label UI, unità, precisione
e stato di comparabilità.

La precisione production `ScaledUint16RegretStrategy` è validata su tutti e tre
gli scenari mediante exact BR target-driven e root EV esterno. Il formato è
coperto da finiteness check, resume in-memory byte-equivalent, scale goldens e
round-trip storage byte-for-byte. RSS, stato persistente e buffer transienti
restano metriche distinte.

Il backend a residenza budgeted, se conservato come funzione esplicita di
prodotto, deve essere confrontato con quello residente
sugli stessi codici e scale, certificazione exact e payload materializzato.
Può usare paging locale del sistema operativo, ma non GPU o acceleratori; il
report deve pubblicare questa scelta e non presentarla come compressione dello
stato logico o come parità con “Memory needed for solving”.

I riferimenti esterni devono conservare export o trascrizione, versione del
solver, azioni legali, unità e arrotondamenti. Se un dato non è disponibile, il
report lo marca come limite anziché inventarlo.

## Criterio di avanzamento

La decisione di prodotto 2026-09-04 sospende il vincolo che subordinava F11+
alla parity exact GTO+. Card abstraction, safe subgame solving e il primo bridge
HU postflop CFR+ sono ora implementati; il path exact resta l'oracolo. Cache e
sweep ridotto sono ora qualificati, ma prima di riprendere la parity servono
fixture flop/turn/preflop rappresentative e una decisione esplicita su quale
percorso commerciale confrontare. F10.4 non è una prova di tale integrazione.
