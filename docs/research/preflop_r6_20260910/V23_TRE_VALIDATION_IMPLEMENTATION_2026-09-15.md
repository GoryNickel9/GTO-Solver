# V23 HU10 training/response/evaluation validator

Data: 2026-09-15
Stato: engineering pass; qualifica numerica non eseguita

## Obiettivo

Il runner misura se una policy V23 e le risposte trovate contro di essa generalizzano oltre il
corpus usato per ottimizzarle. Separa i dati in tre parti:

1. T addestra la strategia media con Linear MCCFR;
2. R cerca una best response esatta per CO e BTN contro la policy T congelata;
3. E valuta la policy e le risposte R senza modificarle.

Questa separazione evita di stimare sullo stesso campione usato per scegliere la risposta. Il
risultato non è una NashConv fisica: R contiene chance empirica e la risposta trovata su R può non
coprire gli information set di E.

## Implementazione

Il nuovo eseguibile è `gtosd_hu_preflop_tre_validation`. Accetta la fixture HU10 e parametri K
separati per T, R ed E. I tre seed chance devono essere distinti.

Il flusso è il seguente:

1. compila T e addestra la policy media V23;
2. distrugge il gioco T dopo aver conservato policy e identità, per limitare il picco RAM;
3. compila R, applica `uniform_unseen_v1` e calcola le due best response esatte;
4. converte ciascuna risposta in una policy deterministica e la congela;
5. compila E e valuta baseline e deviazione sullo stesso outcome di chance.

`evaluate_strategy_profile_by_root_chance` restituisce gli EV ordinati per edge del chance root.
Il runner usa questi valori per il delta paired:

```text
gain_i(x) = u_i(response_i, sigma_-i; x) - u_i(sigma_i, sigma_-i; x)
```

Gli outcome sono ordinati per classe CO e poi per deal. Per ogni classe il runner calcola media e
varianza condizionale; la stima globale usa `class_mass / 630`. La stessa ricomposizione deve
coincidere entro `1e-10` con l'EV pesato direttamente dal chance root.

## Stop adattivo e intervalli

E parte dal K iniziale e raddoppia fino al limite. Lo stesso seed conserva il prefisso di ogni
classe CO, proprietà coperta da un test. Lo stop considera cinque quantità:

- profile EV CO;
- profile EV BTN;
- candidate response gain CO;
- candidate response gain BTN;
- somma dei due candidate response gain.

Il run si ferma quando tutte le semiampiezze normalizzate sono sotto il target. Gli intervalli
usano l'approssimazione normale della media stratificata. La correzione Bonferroni divide `0,05`
per cinque metriche e per il numero di look pianificati. Il certificato registra K, critical value,
errore standard, intervallo e copertura a ogni look.

L'approssimazione normale può essere scarsa con K piccolo. Lo smoke verifica il software, non la
copertura statistica in regime asintotico.

## Contratto del certificato

Lo schema è `gtosd.hu_preflop_tre_validation.v1`. Il JSON distingue:

- NashConv esatta della policy completata nel gioco finito R;
- gain delle risposte candidate su E con intervallo;
- copertura per chiavi e reach di T su R/E;
- copertura per chiavi e reach di ciascuna risposta R su E;
- `physical_nashconv_certified=false`.

Il gain su E non è troncato a zero. Una risposta ottima su R può perdere EV su E quando sfrutta
rumore campionario o quando il fallback uniforme copre troppi information set.

## Smoke Release

Comando:

```text
gtosd_hu_preflop_tre_validation \
  --config benchmarks/fixtures/hu_preflop_hu10_calibration_v1.json \
  --output .tmp/v23_hu10_tre_smoke.json \
  --training-deals-per-co-class 1 \
  --response-deals-per-co-class 1 \
  --evaluation-initial-deals-per-co-class 2 \
  --evaluation-maximum-deals-per-co-class 4 \
  --evaluation-normalized-half-width-target 0.000000000001 \
  --equity-samples 4 --iterations 16 --maximum-nodes 800000
```

Il target è volutamente irraggiungibile per forzare entrambi i look.

| Campo | Risultato |
| --- | ---: |
| Nodi T/R | 166.780 / 166.780 |
| Nodi E finale K4 | 667.117 |
| Iterazioni T | 16 |
| NashConv esatta su R | 7,2406675101 ante |
| Copertura T→E, chiavi | 0,0209403081 |
| Copertura T→E, reach | 0,6939306140 |
| Gain candidato CO su E | -0,3163961237 ante |
| Gain candidato BTN su E | -0,2052874545 ante |
| Tempo totale | 58,3846614 s |

I valori negativi sono coerenti con una risposta R che non generalizza. Con 16 iterazioni e K
ridotti non descrivono la qualità del solver.

## Validazione eseguita

- build Release `/W4 /WX` del runner e dei test modificati;
- `phase5_tests`: 109 asserzioni passate;
- `hu_preflop_abstract_game_tests`: 15 asserzioni passate;
- due look adattivi completati con `K=2` e `K=4`;
- certificato accettato da JSON Schema Draft 2020-12;
- scrittura atomica del certificato.

## Limiti

La best response è esatta soltanto nel gioco finito R. La valutazione E stima il gain di quella
risposta congelata; non cerca l'azione migliore su ogni chance fisica. Il fallback uniforme è
esplicito, ma non recupera comportamento appreso negli information set mancanti. Una qualifica
HU10 richiede K e iterazioni maggiori, copertura cross-corpus sufficiente e intervalli stretti.
