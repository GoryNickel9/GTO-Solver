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
- Le prove di prodotto devono usare il backend CPU/RAM-only. L'accelerazione
  GPU della GUI non può partecipare a tree building, traversal, best response
  o certificazione e non può essere inclusa in un risultato solver.

## Gate GTO+

La suite corrente comprende `GTP-AHKHQH-101` (equivalente v2 dello scenario
congelato `003`), `GTP-TH7D6S-101` e `GTP-TSTC9D-101`. AHK usa flop `Ah Kh Qh`, range identici
`AA-QQ, AKs-AQs, KQs, AKo-AQo, KQo`, 36 combo fisiche per player, pot 40,
stack 100, bet/raise 50%, un raise per street, all-in `Go` alla soglia corretta,
rake e smoothing disabilitati.

I gate prestazionali della suite sono:

- tempo GTOSD a dEV comparabile non superiore a 1,900000 / 19,622222 /
  128,988889 s; per TSTC9D il riferimento grezzo è il primo punto GTO+
  strettamente sotto soglia, `116,09 s` a `0,91%` (`0,146 ante`);
- stato solver non superiore a 8.000.000 / 399.000.000 / 2.000.000.000 B;
- peak RSS del processo strettamente inferiore al cap desktop comune
  `2 GiB = 2.147.483.648 B`, separato dal riferimento esterno di stato;
- correttezza, tempo e memoria separati sullo stesso checkpoint; cinque
  processi indipendenti soltanto per la promozione temporale finale.

Il checkpoint 2026-08-14 passa dEV, root EV e memoria su 3/3 e fallisce il
tempo su 3/3. I tempi singoli sono 4,970917 / 37,810434 / 690,307523 s; gli
stati sono 2.503.908 / 249.955.776 / 1.747.903.656 B. Peak RSS resta distinto.

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
quelle forzate da termine finito, pausa o cancellazione. Nel run TSTC9D
corrente il solver continua senza iteration cap e termina a 200 con 0,983565%;
valori intermedi sopra 1% non possono arrestarlo.

## Evidenza richiesta

Un gate scientifico registra commit pulito, compiler/flags, hardware, thread,
fixture hash, precisione, algoritmo, averaging, target, valore finale, tempi
individuali, mediana/p95, memoria solver/transient/RSS separati e suite Release.

La precisione packed `Float13RegretFloat11Strategy` è validata su tutti e tre
gli scenari mediante exact BR target-driven e root EV esterno. Il formato è
anche coperto da finiteness check, resume in-memory e round-trip storage
byte-for-byte. Il suo `solver_state_bytes` è esattamente `3 * actions`; RSS e
buffer transienti restano metriche distinte.

I riferimenti esterni devono conservare export o trascrizione, versione del
solver, azioni legali, unità e arrotondamenti. Se un dato non è disponibile, il
report lo marca come limite anziché inventarlo.

## Criterio di avanzamento

F11 e le fasi successive restano bloccate finché tutti i tre time gate non
passano insieme a dEV/root/RAM o il criterio viene modificato con una decisione
documentata. F10.4 non sblocca il gate velocità.
