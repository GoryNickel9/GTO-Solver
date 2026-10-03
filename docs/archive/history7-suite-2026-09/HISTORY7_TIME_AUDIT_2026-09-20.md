# Audit del tempo di `history7` su HU20

Data: 2026-09-20. Stato: HU20 PASS esatto, artefatti completi salvati.

Nota del 2026-09-21: i riferimenti a 12 GiB descrivono il vincolo vigente
durante questi run; 25 GiB fu il tetto del successivo audit HU30. Il requisito
di prodotto corrente è un picco non superiore a **8 GiB**. Tempi, picchi
osservati e conclusioni numeriche di questo audit restano invariati; il PASS
matematico di HU20 non soddisfa il nuovo requisito di memoria.

## Risultato

HU20 `history7` a 16.000 iterazioni conserva il risultato certificato storico
e chiude training, persistenza e best response fisica esatta in 74 min 54 s:

| Metrica | Valore |
|---|---:|
| Albero | `fnv1a64:f5b432de223744cc` |
| Mappa `history7` | `fnv1a64:3c9ee76ca6aad23b` |
| Righe flop / turn / river | 7.585 / 222.865 / 1.539.270 |
| Iterazioni / board | 16.000 / 1.024.000 |
| Policy | `fnv1a64:362045ee45623b7a` |
| Max gain fisico esatto | 0,027999588711995 ante |
| NashConv | 0,0299431715909233 ante |
| Soglia | 0,03 ante, 1 % del piatto iniziale |
| Esito | **PASS** |
| Working set finale del training | 12.285.771.776 byte, circa 11,44 GiB |

Il max gain coincide con il riferimento storico `0,0279995887` alla precisione
pubblicata. La modifica numerica descritta sotto non ha peggiorato la NashConv.

## Perché la previsione cresceva durante il run

Il primo run batch 32 standard proiettava 59 min 29 s dopo 500 iterazioni. La
stima saliva a 64 min 28 s a 1.000, 68 min 38 s a 1.500 e 76 min 19 s a 3.000.
Sommando circa 15 minuti di BR, il run superava il gate di 90 minuti ed è stato
fermato. I blocchi da 500 costavano 111,531, 130,241, 144,266, 155,825 e
162,956 secondi.

La quantità di lavoro dichiarata restava lineare: 64 board per iterazione e
192.000 board a 3.000. Il working set passava soltanto da 12.309.938.176 a
12.310.196.224 byte. Non era crescita dell'albero o della RAM.

Un run diagnostico da 1.000 iterazioni ha separato le fasi ogni 250:

| Iterazioni | Refresh policy | Board | Traversata | Totale blocco |
|---|---:|---:|---:|---:|
| 1–250 | 17,900 s | 7,910 s | 26,029 s | 51,839 s |
| 251–500 | 22,479 s | 7,843 s | 26,352 s | 56,674 s |
| 501–750 | 26,418 s | 7,740 s | 26,306 s | 60,464 s |
| 751–1.000 | 34,785 s | 8,396 s | 32,046 s | 75,229 s |

La crescita era nel refresh. `materialize_row()` applicava il discount DCFR
con un ciclo da `last + 1` all'iterazione corrente. Una riga rara pagava una
moltiplicazione per ogni iterazione saltata, per ogni azione e sia sui regret
sia sulle somme strategiche. Il costo era quindi proporzionale all'età della
riga. Con l'avanzare del solve, gli intervalli delle righe rare aumentavano.

## Correzione conservata

La modalità `lazy-discount-v2-hybrid` tratta separatamente i tre casi:

1. i regret positivi conservano il ciclo e l'ordine originale delle
   moltiplicazioni, perché il loro arrotondamento alimenta gli update CFR;
2. i regret non positivi usano `ldexp`, equivalente alla moltiplicazione per
   `0,5^n` richiesta da DCFR con `beta = 0`;
3. le somme strategiche usano il rapporto fra prodotti prefissi dei fattori.
   Queste somme costruiscono la policy media ma non alimentano i regret.

L'identità del trainer è passata a `lazy-discount-v2-hybrid`; i checkpoint v1
non possono essere ripresi silenziosamente. Il test eager contro lazy misura:

- differenza massima regret: 0;
- differenza massima strategy sum: `8,32667e-17`;
- differenza massima policy media: `4,44089e-16`;
- risultato identico con 1 e 2 thread e dopo resume.

La prima variante, che cumulava anche i fattori dei regret positivi, è stata
scartata. Dopo 25 iterazioni produceva differenza regret 0,0110507 e differenza
policy 1,0: l'associazione numerica modificava la traiettoria CFR.

## Benchmark prima e dopo

Sul confronto sostenuto a 3.000 iterazioni, stesso seed, batch 32, otto thread
e partizione target 64:

| Versione | Training | s/iter | Refresh | Risparmio |
|---|---:|---:|---:|---:|
| lazy v1 | 858,616 s | 0,286205 | non stampato per fase | riferimento |
| lazy v2 16k definitivo, prime 3.000 | 680,659 s | 0,226886 | 258,057 s | 20,73 % |

Il run diagnostico v2 da 1.000 iterazioni ha misurato 208,475 s contro
244,206 s della v1, con refresh 74,432 s contro 101,582 s. I quattro blocchi
v2 da 250 richiedevano 18,454, 17,521, 17,735 e 20,722 secondi di refresh. Il
costo non cresceva più con l'indice dell'iterazione.

## Run definitivo per fase

Configurazione: HU20 congelato, DCFR 1,5/0/2, update alternati, batch 32, otto
thread, partizione target 64, refresh delle righe attive e
`lazy-discount-v2-hybrid`.

| Fase | Secondi | Durata | Quota training |
|---|---:|---:|---:|
| Inizializzazione trainer | 5,979 | 0m06s | esclusa |
| Discount | 0,017 | <0m01s | 0,0005 % |
| Refresh policy | 1.436,170 | 23m56s | 41,39 % |
| Preparazione board | 387,171 | 6m27s | 11,16 % |
| Traversata CFR | 1.646,170 | 27m26s | 47,45 % |
| **Training** | **3.469,540** | **57m50s** | **100 %** |
| Materializzazione, checksum e scrittura | 95,191 | 1m35s | esclusa |
| **Trainer completo** | **3.570,710** | **59m31s** | esclusa |
| Preparazione certificatore | 19,827 | 0m20s | esclusa |
| BR fisica esatta | 903,558 | 15m04s | esclusa |
| **End-to-end** | **4.494,095** | **74m54s** | esclusa |

La differenza di pochi millisecondi fra somma delle fasi e totale deriva
dall'arrotondamento del log. La BR ha valutato 573 flop canonici e 605.088
board; il processo del certificatore occupava 3.962.064.896 byte alla fine.

## Artefatti

Directory: `out/history7_optimized`.

| File | Byte | SHA-256 |
|---|---:|---|
| `hu20_history7_lazy_v2_16k_20260920_checkpoint.bin` | 7.475.229.272 | `271DC4CF0DBF604A3390B8AD05B9BA5597FDA1270CF00657C64E43A901B74D02` |
| `hu20_history7_lazy_v2_16k_20260920_policy.bin` | 3.737.614.732 | `3E12422678AB75D1F072EA82BD906559EE78D566DD29AFCFC03E13F9AB74A734` |
| `hu20_history7_lazy_v2_16k_20260920_certificate.json` | 6.344 | `E582CF189F0F56874BAABD02C9CE0BFC322128D4C03063928334EFFF3D8470B1` |
| `hu20_history7_lazy_v2_16k_20260920_cert_state.bin` | 35.064.782 | `1A58FAB1C858F4AF1EE3309449CD670AB234C74472686CCB0CA06BA96F4F1690` |

Il log del trainer è
`hu20_history7_lazy_v2_16k_20260920_train.jsonl`. Il certificato dichiara
`exact = true`, `sampled = false`, `partial = false` e `passes_target = true`.

## Tentativi scartati

| Tentativo | Risultato misurato | Decisione |
|---|---|---|
| Gerarchia compatta HR2 `8/8` | 3,43 GB; BR fisica 0,219175 e astratta 0,154556 a 8k | Non conserva la NashConv `history7` |
| Batch 512 | 13.154.013.184 byte | Supera 12 GiB |
| Batch 320, 200 iterazioni | BR astratta 0,172035 contro 0,142510 a pari 128.000 board | Peggiore convergenza |
| Batch 64 | raddoppia le board per iterazione rispetto al run certificato batch 32 | Profilo non comparabile al riferimento 16k |
| Quattro thread | 45,049 s/100 contro circa 22 s/100 con otto thread | Più lento |
| Partizione target 8 | 0,531683 s/iter | Più lenta |
| Cache policy locale in `double` | guadagno di traversata restituito dal costo di costruzione; circa 135 MB in più | Rimossa |
| Policy `float32` | 29,466 s/100 contro 28,906 s/100 in `double` | Più lenta e numericamente diversa |
| Cache all-in con sola massa netta | circa 1,5 % sul profilo breve, traiettoria diversa | Beneficio insufficiente |
| Riutilizzo policy per solo attore | più rapido, ma cambia l'associazione dei discount | Rimane sperimentale, non usato nel 16k |
| Cumulo dei regret positivi | differenza regret 0,0110507 e policy 1,0 dopo 25 iterazioni | Rimosso |
| Ordinamento radix, mark generazionali, piccoli buffer, `/arch:AVX2` | nessun guadagno ripetibile | Rimossi |

Restano valide la preparazione parallela delle board, il pool persistente, la
bitmap delle righe attive, i buffer board riusati, la cache densa delle equity
all-in, gli offset delle celle per livello e lo scratch riusato nella BR.

## Validazione

- trainer: PASS, 21.310.536 assertion;
- kernel: PASS, 1.636.010 assertion;
- certificatore: PASS, 146.545 assertion;
- determinismo trainer: stesso fingerprint con 1/2/4/8 thread e partizione
  fine;
- resume: PASS;
- BR fisica finale: PASS esatto a 0,027999588711995 ante.

## Conseguenza per HU30 e HU40

Il bug temporale era generale per ogni layout lazy con righe visitate di rado,
quindi la correzione riduce anche il rischio sui due stack successivi. Non
dimostra però che 16.000 iterazioni bastino a HU30 o HU40. Il numero di
iterazioni deve essere deciso dalla metrica di convergenza e confermato dalla
BR fisica esatta, non dal solo tempo del training.
