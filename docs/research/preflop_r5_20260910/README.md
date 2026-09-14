# R5 — Astrazione consolidata sul layout definitivo

## Stato

`PASS` per il gate R5 con capacità candidata `64/256/1.024`. Il mapping è totale sulle osservazioni legali, riproducibile dopo reload, privo di letture del runout effettivo futuro e compatibile con il budget. La candidata non è ancora qualificata: a 50.000 iterazioni l'EV ha un intervallo troppo ampio e l'errore puntuale rispetto al riferimento resta 0,27234 ante.

## Implementazione

Il formato della policy campionata passa a `1.2` e include nel fingerprint:

- seed di partizione, separato dai seed di training e valutazione;
- capacità Flop/Turn/River;
- versione e ID dell'astrazione;
- representation, equity samples, history pubblica completa e probabilità medie.

La persistenza della policy è stata estratta da `hu_preflop_persistence.cpp` in `hu_preflop_sampled_persistence.cpp`. Il target `gtosd::preflop_trainer` può quindi salvare e rileggere la propria policy senza importare i formati o i simboli della decomposizione legacy.

Il primo sweep ha esposto un difetto del prototipo R2.1: l'hash modulo distribuiva le feature senza preservarne la vicinanza. Prima di ripetere gli esperimenti, il mapping è stato sostituito con una gerarchia a 16 bit:

1. categoria corrente;
2. equity campionata;
3. profilo aggregato contro tre gruppi avversari;
4. varianza dell'esito;
5. categoria futura dominante e texture.

Le capacità ammesse sono potenze di due fra 2 e 32.768. Capacità maggiori conservano un prefisso più lungo dello stesso codice: la partizione è quindi gerarchica. Il seed applica soltanto una permutazione biunivoca degli ID entro la capacità e non cambia quali osservazioni condividono un gruppo.

Le 81 classi preflop e l'intera betting history restano nella chiave. Nella rappresentazione distribuzionale vengono dimenticati la classe preflop e i bucket delle street precedenti dopo l'ingresso postflop; questa memoria imperfetta è esplicita nell'ID e non implica una garanzia di equilibrio del gioco fisico.

## Telemetria

Il risultato strutturato ora riporta per street:

- visite del mapping e calcoli effettivi dopo la cache;
- bucket occupati rispetto alla capacità;
- tempo cumulativo del mapping;
- query della strategia media e query cadute sul fallback uniforme perché lo stato non era addestrato;
- media, massimo e numero di campioni dello spread fra valori d'azione alle visite del traverser.

I deal restano fisici fino al mapping. Card removal e distribuzione congiunta non vengono sostituiti da prodotti marginali o mani rappresentative.

## Validazione

Quattro test dedicati e di regressione passano in Release: dependency check R1, averaging R2, betting/memoria R4 e astrazione R5. Il solo test R5 passa in 0,49 s; verifica capacità multiple, determinismo, blocker, solve fisico, occupancy, massa non addestrata, save/load atomico, fingerprint e query identica dopo reload.

Una hole card ripetuta sul board produce `invalid_configuration`. Una capacità non power-of-two, nulla o maggiore di 32.768 viene rifiutata. Le probabilità della policy sono validate come finite, non negative e normalizzate; un bucket fuori dalla capacità persistita produce `integrity_failure`.

## Sweep CO40 ridotto

Il protocollo è in [EXPERIMENT_PROTOCOL.md](EXPERIMENT_PROTOCOL.md). Le quattro varianti usano seed comuni, 50.000 iterazioni e valutazione fisica indipendente.

| Variante | EV CO e IC 95% | Errore puntuale da −0,30 | Infoset | Payload | Untrained query | Occupancy F/T/R |
|---|---:|---:|---:|---:|---:|---:|
| Categoria/equity | 0,28737 [−0,18130; 0,75603] | 0,58737 | 570.683 | 90.148.464 B | 24/29.232, 0,082% | n/a |
| Distribuzionale coarse | −0,02766 [−0,47074; 0,41542] | 0,27234 | 621.062 | 96.390.144 B | 36/27.742, 0,130% | 27/119/206 |
| Distribuzionale standard | 0,48987 [0,01765; 0,96209] | 0,78987 | 823.633 | 126.985.392 B | 76/29.038, 0,262% | 94/228/412 |
| Distribuzionale fine | 0,60464 [0,13632; 1,07295] | 0,90464 | 900.380 | 138.250.224 B | 95/28.268, 0,336% | 193/458/414 |

| Variante | Solve / wall | Peak working set | Peak private bytes | Mapping | Spread medio / massimo |
|---|---:|---:|---:|---:|---:|
| Categoria/equity | 19,898 / 20,285 s | 197.652.480 B | 199.016.448 B | 13,913 s | 36,5864 / 79,9562 ante |
| Distribuzionale coarse | 19,436 / 19,653 s | 204.431.360 B | 205.897.728 B | 13,783 s | 36,0227 / 79,9562 ante |
| Distribuzionale standard | 19,693 / 19,927 s | 241.999.872 B | 244.097.024 B | 13,750 s | 36,9905 / 79,9562 ante |
| Distribuzionale fine | 19,713 / 20,004 s | 255.688.704 B | 257.859.584 B | 13,771 s | 37,2905 / 79,9562 ante |

Gli intervalli di categoria/equity e coarse si sovrappongono: il run non dimostra una superiorità statistica. La coarse dimezza però l'errore puntuale, mantiene il fallback allo 0,13% e costa circa 6,7 MiB di working set in più. Standard e fine aumentano memoria e massa non addestrata senza migliorare l'errore root; l'occupancy River satura presto perché il codice delle feature osservate usa solo una parte del dominio teorico.

## Decisioni e limiti

La capacità coarse passa a R6 come candidata sostenibile; categoria/equity resta baseline. Standard e fine vengono escluse dal primo confronto algoritmico perché, allo stesso budget, peggiorano copertura ed errore puntuale. Questo non prova che siano intrinsecamente peggiori: dimostra che 50.000 iterazioni non addestrano il loro stato aggiuntivo in modo utile.

Lo spread dei valori d'azione è una diagnostica aggregata per visita, non la varianza condizionata completa di ogni bucket. I valori della tabella sono stati rigenerati dopo aver corretto il punto di raccolta: ora comprendono solo nodi postflop del traverser. I conteggi sono rispettivamente 1.569.502, 1.472.646, 1.543.968 e 1.495.022. La prima misura, che includeva per errore i nodi preflop, non viene usata. L'EV self-play fisico non è exploitability e `normalized_abstract_nashconv=0` resta una risposta appresa non certificata.

## Addendum emerso in R6

Il primo sweep R5 non confrontava le frequenze esterne perché quel percorso di report appartiene a R7. La diagnosi R6 a 500.000 iterazioni ha aggiunto tre capacità più piccole senza cambiare mapping: `2/8/32`, `8/32/128` e `32/128/512`. La variante `32/128/512` ha ridotto l'errore medio pesato delle azioni a 18,76 punti percentuali, contro 20,70 per `64/256/1.024`, usando 346.472.448 B private contro 407.322.624 B. Diventa quindi la partizione di sviluppo preferita; non è qualificata e non modifica l'esito del gate di correttezza R5.

## Revisione richiesta da R6

Il plateau R6 ha riaperto il contratto di recall. Il formato policy è ora `1.5` e distingue tre modalità distribuzionali: osservazione corrente, storia dei bucket e perfect recall completo. I test verificano quali bucket e classi preflop restano nella chiave, oltre a save/load e checksum.

I due challenger sono respinti. Perfect recall completo crea 2.060.410 infoset e usa 474.755.072 B già a 50.000 iterazioni, con errore medio azioni 23,99 pp. Bucket-history usa 460.066.816 B a 500.000 iterazioni DCFR e arriva a 17,32 pp, senza superare il mapping corrente. Una diversa allocazione delle feature nella capacità arriva a 17,64 pp e viene respinta; il mapping v2 resta quello selezionato.

R5 resta `PASS` per correttezza e sostenibilità del mapping selezionato. La revisione non chiude il gate scientifico R6.

## Correzione del contratto informativo

Un audit successivo ha distinto le feature calcolate da quelle effettivamente conservate. Il mapping legacy compone categoria, equity, profilo avversario, varianza, categoria futura dominante e texture in un codice a 16 bit, ma le capacità `32/128/512` ne prendono soltanto i bit più significativi. Di conseguenza il Flop usa categoria più un bit di equity, il Turn categoria più tre bit di equity e il River categoria, tutti i quattro bit di equity e un bit del profilo avversario. Le feature inferiori non distinguono bucket alle capacità selezionate.

La correttezza, il determinismo e la copertura totale del mapping R5 restano validi. È invece ritirata l'interpretazione secondo cui `32/128/512` rappresenta l'intero profilo distribuzionale. Un challenger versionato che conserva l'equity e miscela tutte le feature residue è stato implementato e verificato in R6; su due seed peggiora la WMAE media da 18,647735 a 19,351477 pp e raddoppia lo stato. Il report [DISTRIBUTIONAL_PROFILE_V6_GATE_2026-09-11.md](../preflop_r6_20260910/DISTRIBUTIONAL_PROFILE_V6_GATE_2026-09-11.md) registra implementazione, test e rifiuto.
