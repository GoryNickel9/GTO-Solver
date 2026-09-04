# Baseline benchmark di prodotto per bucketing e subgame solving — 2026-09-05

## Scopo

Questo documento congela i valori precedenti all'ottimizzazione dello schema
`next-street-equity-quantiles-16-l2-v2`. I numeri sono separati per benchmark e
per contratto algoritmico: la baseline exact usa DCFR e target dEV, mentre la
baseline bucketed usa CFR+ Float64 e exact combo best response. Le due metriche
non sono intercambiabili.

Il vincolo di accettazione richiesto è doppio:

1. nessun peggioramento del percorso exact nei suoi test e benchmark;
2. per il percorso bucketed, una nuova configurazione production deve
   raggiungere `NashConv/pot < 1%` senza superare **né** il numero di iterazioni
   **né** il wall time della migliore baseline bucketed già qualificata per lo
   stesso benchmark. I confronti diagnostici a K uguale devono inoltre non
   peggiorare la curva ai checkpoint comuni e il wall all'orizzonte comune.

Non è ammesso scegliere K, feature o parametri in funzione dell'ID della
fixture. AHKHQH, TH7D6S e TSTC9D sono prove di uno stesso prodotto globale.

## AHKHQH

### Exact production

| Algoritmo | Thread | Target | Iterazioni | dEV finale | Solver mediano/p95 | Wall mediano/p95 |
|---|---:|---|---:|---:|---:|---:|
| DCFR alternato `1.5/0/2` | 8 | dEV `<1%` | 80 | 0,951423% | 0,758705 / 0,790918 s | 6,231883 / 6,468465 s |

### Bucketed CFR+

| Schema | K | Processi | Iterazioni eseguite | Primo checkpoint `<1%` | NashConv/pot finale | Wall | Esito |
|---|---:|---:|---:|---:|---:|---:|---|
| W/T/L/equity v1 | 16 | 5 | 500 | 200 | 0,6029051267% | mediana 9,936001 s | point PASS |
| W/T/L/equity v1 | 32 | 1 | 800 | 200 | 0,4994523166% | 15,550082 s | pre-gate PASS |

Curva K32 v1 a `100/200/300/400/500/600/700/800`:
`1,7391685/0,7897556/0,6426673/0,5838888/0,5568662/0,5374612/0,5195513/0,4994523%`.

Baseline di promozione bucketed AHK: massimo **200 iterazioni al primo
checkpoint qualificato** e, in un run target-driven equivalente, wall non
superiore al run qualificato K16 da 500 iterazioni (`9,936001 s` mediano).

## TH7D6S

### Exact production

| Algoritmo | Thread | Target | Iterazioni | dEV finale | Solver mediano/p95 | Wall mediano/p95 |
|---|---:|---|---:|---:|---:|---:|
| DCFR alternato `1.5/0/2` | 8 | dEV `<1%` | 80 | 0,807956% | 19,948228 / 24,192260 s | 35,208170 / 41,015117 s |

### Bucketed CFR+

| Schema | K | Processi | Iterazioni eseguite | Primo checkpoint `<1%` | NashConv/pot finale | Wall | Esito |
|---|---:|---:|---:|---:|---:|---:|---|
| W/T/L/equity v1 | 16 | 1 | 800 | mai | 5,9243166824% | 780,828139 s | FAIL |
| W/T/L/equity v1 | 32 | 1 | 800 | mai | 2,8595946893% | 752,519128 s | FAIL |
| W/T/L/equity v1 | 128 | 5 | 400 | 400 | 0,8816749004% | mediana 435,349473 s | point PASS |

Curva K32 v1 a `100/200/300/400/500/600/700/800`:
`4,623514/3,073163/2,861689/2,828140/2,830521/2,839221/2,849348/2,859595%`.

Curva K128 v1 a `100/200/300/400`:
`3,359022/1,420934/1,015981/0,881675%`.

Baseline di promozione bucketed TH: massimo **400 iterazioni** e wall mediano
massimo **435,349473 s**. Per un confronto K32 a orizzonte 800, il wall non può
superare `752,519128 s` e la curva non può peggiorare ai checkpoint comuni.

## TSTC9D

### Exact production

| Algoritmo | Thread | Target | Iterazioni | dEV finale | Solver mediano/p95 | Wall mediano/p95 |
|---|---:|---|---:|---:|---:|---:|
| DCFR alternato `1.5/0/2` | 8 | dEV `<1%` | 160 | 0,904505% | 184,095930 / 197,865030 s | 208,403423 / 221,888253 s |

### Bucketed CFR+

Non esiste ancora una solve bucketed TST qualificata: il gate globale K32 v1
ha eseguito preflight e cache ma ha saltato la solve dopo il FAIL TH, secondo
l'early-reject dichiarato. I valori disponibili sono soltanto di fattibilità:

| Schema | K | State Float64 upper bound | Peak RAM stimato | Cache reale | Solve |
|---|---:|---:|---:|---:|---|
| W/T/L/equity v1 | 32 | 813.955.072 B | 1.302.740.760 B | 38.950.977 B | non eseguita |

Di conseguenza non si inventa una baseline CFR+ confrontando dEV exact con
NashConv bucketed. La prima solve TST bucketed valida dovrà essere dichiarata
come baseline iniziale, mentre il percorso exact dovrà conservare separatamente
`160` iterazioni, `184,095930 s` solver mediani e `208,403423 s` wall mediani
senza regressioni.

## Fonti autorevoli

- exact production: `docs/DCFR_EPOCH_RESET_GAMMA3_FEASIBILITY_2026-09-01.md`;
- qualifica locale e gate K16/K32:
  `docs/CARD_ABSTRACTION_SUBGAME_PRODUCTION_QUALIFICATION_2026-09-04.md`;
- artefatti AHK K16:
  `out/qualification/card-abstraction-8t/ahk-flop-k16-final/`;
- artefatti TH K16/K128: `out/qualification/card-abstraction-8t/`;
- gate globale K32 v1:
  `out/qualification/global-k32-20260904-b3b1a1d/`.

Gli artefatti `out/` sono evidenza locale ignorata da Git; i valori necessari
alla governance sono duplicati qui affinché il vincolo resti versionato.
