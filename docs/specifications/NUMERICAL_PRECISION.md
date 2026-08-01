# Precisione numerica

## Unità e tipi

Gli importi di gioco usano `Money`, un intero con `10.000` unità per ante.
Percentuali di pot e pesi di range usano valori validati in basis point ai
confini di input.
EV, equity, reach, regret, strategy sum e metriche di convergenza sono calcolati
in floating point.

La modalità di massima accuratezza conserva regret e strategy sum in
`double`. La modalità a memoria ridotta `Float32` conserva lo stato cumulativo
in `float`, ma conversioni, normalizzazione e certificazione usano accumulatori
in precisione adeguata. Le due modalità devono essere nominate nei report: non
si può attribuire a `Float32` l'identità numerica di `Float64`.

## Invarianti

Ogni solve deve preservare:

- nessun NaN o infinito in reach, regret, probabilità, EV o certificazione;
- probabilità non negative entro la tolleranza numerica;
- somma delle azioni pari a uno entro la tolleranza dichiarata;
- range e chance normalizzati senza inventare massa;
- stesso fingerprint, layout e precisione quando si riprende un checkpoint;
- payoff sum coerente con zero-sum o rake configurato.

`maximum_normalization_error` registra il massimo scostamento osservato nelle
distribuzioni prodotte dal solver.

## Determinismo

Il tree builder, l'evaluator, la canonicalizzazione dei semi, l'ordine delle
azioni e il solve esatto single-thread devono essere deterministici per input e
versione del motore uguali. Gli algoritmi campionati usano un seed serializzato.
Ogni futura riduzione parallela deve definire l'ordine o misurare la deriva
rispetto alla baseline deterministica.

## Tolleranze

Le tolleranze appartengono al test, non a un valore globale arbitrario:

- identità discrete (carte, azioni, nodi, fingerprint): confronto esatto;
- parsing/serializzazione di interi: confronto esatto;
- probabilità ed EV unitari: tolleranza specifica motivata dal percorso;
- parità esterna: unità e denominatore espliciti;
- regressioni `Float32`: confronto contro `Float64` e non solo auto-consistenza.

Nel gate GTO+ corrente il root EV viene confrontato in ante e come percentuale
del pot. Gli EV BTN condizionali non sono un gate autonomo finché i posteriori
combo-per-combo non coincidono.

## Convergenza e stopping

`target_normalized_nash_conv` e `target_normalized_max_deviation` sono frazioni
del pot; la GUI presenta percentuali. `0.001` nel core equivale a `0.10%` in UI.
Il report deve distinguere target richiesto, metrica realmente raggiunta e
tempo censurato quando il target non viene raggiunto.

Un solve interrotto a 0,11% non è registrato come solve a 0,10%. Nel riferimento
GTO+ AhKhQh il tentativo a 0,10% è rimasto a circa 0,11% dopo 245 secondi: il
tempo comparabile è quindi `>245 s`, non 245 s e non 4,20 s.

## Errori e stabilità

Il core ritorna un errore numerico esplicito su stato non finito o
normalizzazione impossibile. Non sono ammessi clamp silenziosi che cambiano
utility o range. Le somme lunghe e le best response devono usare `double`; una
eventuale compensated summation sarà introdotta solo con un benchmark che mostri
un errore rilevante.

## Compressione

La quantizzazione `uint16` delle probabilità è sperimentale e non è il formato
exact predefinito. Una soluzione quantizzata deve avere un feature bit distinto,
misurare errore di strategia/EV/convergenza e non essere descritta come lossless.
La compressione Zstandard del payload è lossless e non cambia la precisione
matematica.
