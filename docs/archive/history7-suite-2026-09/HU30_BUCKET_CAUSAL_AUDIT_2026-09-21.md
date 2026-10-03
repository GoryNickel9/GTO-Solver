# Audit causale dei bucket su HU30

Nota successiva: questo audit fu eseguito con un tetto diagnostico di 25 GiB.
Il requisito di prodotto corrente è un picco non superiore a **8 GiB**. Cap 23
resta evidenza causale, ma non è una soluzione candidabile.

Data: 2026-09-21. Stato: **audit concluso; nessuna modifica al solver e nessun
nuovo training**.

## Esito

Il difetto non è spiegato da una sola causa già dimostrata. I dati sostengono
soprattutto una combinazione fra **numero insufficiente di bucket** e
**compressione river imposta a `history7` dal precedente limite di memoria**.
Il clustering fermato troppo presto non è una causa materiale. Le feature flop,
prese senza quantizzazione, non mostrano una perdita rilevante nel campione; le
feature turn e river restano da separare con valori d'azione delle rispettive
street.

| Ipotesi | Esito dell'audit | Evidenza |
|---|---|---|
| Feature dei bucket | Non supportata sul flop; aperta su turn/river | raggruppare solo stati con la stessa feature flop e la stessa classe perde da `2,06e-8` a `0,000391` a, al massimo l'1,93% della perdita dei 200 bucket |
| Numero di bucket | Contributo locale dimostrato | 500 bucket flop recuperano dal 31,6% al 57,3% della perdita decisionale misurata con 200 bucket |
| Clustering | Escluso come causa primaria per mancata convergenza | flop: altre 20 iterazioni riducono l'inerzia dello 0,0225%; turn già convergente; river: un passo Lloyd esatto riduce l'inerzia dello 0,0243% |
| Memoria limitata di `history7` | Contributo plausibile e forte, non isolato causalmente | il cap 7 elimina il 63,77% delle righe river della storia completa; cap 23 riduce la distorsione geometrica dell'88,42%, ma non esiste un confronto addestrato a pari lavoro e pari maturità |
| Combinazione | Spiegazione meglio sostenuta | il 79,19% del massimo guadagno fisico HU30 non compare nella BR astratta; il flop mostra già un costo del numero di bucket e il river aggiunge la compressione cap 7 |

Questa classificazione non promuove un candidato. In particolare, non prova che
500 bucket o cap 23 bastino a superare il gate fisico di `0,03` ante.

## Input vincolati

| Elemento | Identità |
|---|---|
| Configurazione | `PREFLOP-BLUEPRINT-HU30-TEST-001` |
| Albero | `fnv1a64:17dc5c7d07ea30c2` |
| Policy media a 32.000 | `fnv1a64:e52d2f110dbd2b34` |
| Mappa `history7` | `fnv1a64:3c9ee76ca6aad23b` |
| Feature flop | `fnv1a64:0c8d163bc8511741` |
| Bucket flop 200 | `fnv1a64:33f06cf437f8f26d` |
| Bucket flop 500 | `fnv1a64:1e5539e63c0d1d4e` |

Il diagnostico rifiuta policy, albero, tabelle o mappa con fingerprint diversi
dal certificato. Sono stati selezionati senza rimpiazzo 16 flop canonici con
seed `20260921`. Per ciascuno sono stati enumerati tutti i turn e river. Gli
otto nodi `4, 5, 133, 226, 322, 323, 448, 449` coprono limp/check, risposta a
bet e piatto 3-bettato per entrambi i giocatori.

I valori sono perdite locali condizionate alla reach dell'eroe, con avversario
e continuazione congelati. Non sono NashConv e non si sommano fra nodi. Il
confronto interno con `bucket_decision_gap` coincide entro `1,02e-14`.

## Feature e numero dei bucket

Per ogni decisione sono state confrontate quattro partizioni delle stesse
osservazioni fisiche:

1. mano e board distinti;
2. stessa classe preflop e stessa feature esatta flop a 16 bin;
3. stessa riga `history7`, equivalente sul flop a classe più bucket da 200;
4. stessa classe e bucket flop da 500.

| Nodo | Perdita feature esatta | Perdita 200 | Perdita 500 | Recupero 500 |
|---:|---:|---:|---:|---:|
| 4 | 0,00000992 | 0,01471887 | 0,00990870 | 32,68% |
| 5 | 0,00004603 | 0,01165960 | 0,00564741 | 51,56% |
| 133 | 0,00002425 | 0,02900448 | 0,01448579 | 50,06% |
| 226 | 0,00039142 | 0,02024223 | 0,00863515 | 57,34% |
| 322 | 0,00000002 | 0,01912098 | 0,01172455 | 38,68% |
| 323 | 0,00007636 | 0,01902730 | 0,01074341 | 43,54% |
| 448 | 0,00022417 | 0,02169697 | 0,01483728 | 31,62% |
| 449 | 0,00000740 | 0,05767675 | 0,02752188 | 52,28% |

Le collisioni fra feature flop identiche non spiegano la perdita dei 200
bucket. La quantizzazione successiva alle feature la spiega localmente. Il
confronto da 500 bucket cambia insieme capacità e partizione, quindi non misura
una soluzione end-to-end. Il precedente confronto HU20 a 2.000 iterazioni
migliorava la BR fisica solo dell'1,8%; più bucket da soli non sono già una
soluzione verificata.

## Clustering

Il controllo da 200 bucket flop mantiene feature, capacità, seed, dieci
riavvii e campione di screening. Cambia soltanto il massimo di iterazioni da 25
a 100. Il processo converge a 45 iterazioni:

| Misura flop | 25 iterazioni | Convergenza a 45 |
|---|---:|---:|
| Inerzia | 567.951.864 | 567.823.972 |
| Distanza media | 150,653559 | 150,619634 |
| Riduzione relativa | — | 0,022518% |

Il turn della tabella di produzione era già convergente a 8 iterazioni. Sul
river è stato eseguito un passo Lloyd completo dalla tabella salvata da 1.000
bucket: 9.299.070 osservazioni e peso 175.301.280. L'inerzia passa da
`16.848.614.183.349.340` a `16.844.523.265.759.748`, una riduzione dello
0,024280%. Cambia bucket lo 0,180631% delle righe, pari allo 0,163262% del peso.

Questi numeri escludono l'arresto a 25 iterazioni come spiegazione materiale.
Non confrontano famiglie di distanza diverse e non dimostrano l'ottimo globale
del problema di clustering.

## Limite di memoria di `history7`

`history7` conserva l'intera storia fino al turn e comprime soltanto i figli
river di ogni genitore turn:

| Layout | Righe river | Stato numerico HU30 | Distorsione river pesata |
|---|---:|---:|---:|
| cap 7, policy certificata | 1.539.270 | 11.552.641.608 byte | 171.840.688,97 |
| cap 23, solo candidato | 3.398.989 | 24.407.019.336 byte | 19.898.848,17 |
| storia completa | 4.248.476 | 31.138.638.936 byte per R+S+policy | 0 |

Il cap 7 comprime del 63,77% la storia river completa. Cap 23 comprime ancora
del 20,00%, ma riduce la distorsione geometrica dell'88,42% rispetto al cap 7.
La storia completa richiede circa 29,0 GiB per le sole tre tabelle numeriche e
superava il tetto diagnostico di 25 GiB prima di mappe e temporanei. Supera
anche il successivo requisito di prodotto di 8 GiB.

Il run cap 23 a 2.000 iterazioni non isola l'effetto della memoria: ha più del
doppio delle righe, riceve lo stesso numero di board e resta molto più
sotto-allenato. Il suo peggioramento non confuta cap 23 e non ne dimostra
l'utilità. Per questo l'audit classifica la memoria come causa plausibile, non
come causa già certificata.

## Relazione con le due best response

Sulla stessa policy HU30 a 32.000:

| Metrica esatta | Giocatore 0 | Giocatore 1 |
|---|---:|---:|
| BR astratta `history7` | 0,034874091 | 0,018797865 |
| BR fisica | 0,167619129 | 0,065056648 |

Per il giocatore peggiore il divario è `0,132745038` ante, il 79,19% del
guadagno fisico. Questo prova che proseguire il training nella stessa
rappresentazione non può spiegare da solo il FAIL fisico. Non separa, da solo,
bucket base e cap river.

## Limiti dell'audit

- Il confronto decisionale usa 16 dei 573 flop; tutti i futuri sotto quei flop
  sono esatti, ma il campione non è una certificazione globale.
- La perdita delle feature è isolata sul flop. Turn e river richiedono lo stesso
  confronto sui loro nodi prima di dichiarare sufficiente la famiglia di
  feature.
- Il controllo del clustering misura la convergenza della metrica corrente, non
  la qualità decisionale di metriche alternative.
- Nessuna tabella candidata è stata salvata nel solver, nessuna policy è stata
  addestrata e nessun parametro di produzione è stato modificato.

## Artefatti

- `out/audits/hu30_bucket_20260921/flop16_components.json`
- `out/audits/hu30_bucket_20260921/flop16_components_raw.json`
- `out/audits/hu30_bucket_20260921/flop_clustering_25_vs_100.json`
- `out/audits/hu30_bucket_20260921/river_clustering_one_step.json`

## Validazione

| Controllo | Esito |
|---|---:|
| Build Release dei tre diagnostici, MSVC `/W4 /WX` | PASS |
| Diagnostico decisionale, 16 flop × 8 nodi | PASS, 229,650 s |
| Clustering flop fino a convergenza | PASS, 46,792 s |
| Passo Lloyd river | PASS, 10,510 s |
| `gtosd_preflop_blueprint_decision_gap_tests` | PASS |
| Dati del riepilogo contro i sette artefatti sorgente | 7/7 coerenti |
| JSON in `docs/research` | 18/18 validi |
| Link locali nei 23 Markdown di `docs/research` | 0 mancanti |
| `git diff --check` | PASS |
