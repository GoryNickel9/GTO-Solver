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
- Il public tree fisico è l'oracolo per DAG e isomorfismo lossless.
- Single-thread/parallel, `Float64`/`Float32` e resume/continuous sono confronti
  differenziali con tolleranze esplicite.
- La somma dei payoff deve riflettere zero-sum o rake, mai una compensazione UI.

## Gate GTO+

La fixture corrente è `GTP-AHKHQH-003`: flop `Ah Kh Qh`, range identici
`AA-QQ, AKs-AQs, KQs, AKo-AQo, KQo`, 36 combo fisiche per player, pot 40,
stack 100, bet/raise 50%, un raise per street, all-in `Go` alla soglia corretta,
rake e smoothing disabilitati.

I gate prestazionali sono:

- tempo GTOSD a dEV comparabile non superiore a 1,900000 s;
- stato solver non superiore a 8,888889 MB;
- entrambi sullo stesso commit e con cinque processi indipendenti.

Il gate memoria passa; il gate tempo fallisce nella baseline corrente. Il root
EV passa: `19,163591179` contro `19,1581`, delta `+0,005491179 ante`.

Gli EV BTN dopo check/bet restano diagnostici perché GTO+ e GTOSD producono
posteriori CO differenti. Non sono prova di tree mismatch finché non si impone
la stessa strategia root combo-per-combo. La fase F10.4 è pianificata e non
implementata.

## Convergenza comparabile

Ogni report deve distinguere target, definizione e valore raggiunto. Nel run
GTO+ il target reale 0,10% non è stato raggiunto: dEV era circa 0,11% dopo 245
secondi. Il riferimento 4,20 s appartiene a dEV 0,19%, non a 0,10%.

Una misura censurata viene riportata come `>245 s`. Numero di iterazioni e
tempo senza certificazione non sono equivalenti a convergenza.

## Evidenza richiesta

Un gate scientifico registra commit pulito, compiler/flags, hardware, thread,
fixture hash, precisione, algoritmo, averaging, target, valore finale, tempi
individuali, mediana/p95, memoria solver/transient/RSS separati e suite Release.

I riferimenti esterni devono conservare export o trascrizione, versione del
solver, azioni legali, unità e arrotondamenti. Se un dato non è disponibile, il
report lo marca come limite anziché inventarlo.

## Criterio di avanzamento

F11 e le fasi successive restano bloccate finché il parity journey non soddisfa
il gate concordato o viene modificato con una decisione documentata. Un test
diagnostico F10.4 può classificare la causa del delta BTN, ma non sblocca da
solo il gate velocità.
