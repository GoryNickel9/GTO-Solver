# R6 — Diagnosi della divergenza tra seed v7

## Esito

`CAUSE_NARROWED / STRATEGY_NOT_STABLE / R6_GATE_FAIL`.

La distanza tra le due run v7 non deriva dal viewer né dal seed di partizione, che è identico. È instabilità della policy appresa: a due milioni di iterazioni la TV pesata tra seed al CO root è ancora `27,9000 pp`, e ogni seed cambia di oltre `17 pp` tra uno e due milioni di iterazioni.

## Andamento con le iterazioni

| Iterazioni | TV tra seed al CO root | WMAE v7 seed 1 | WMAE v7 seed 2 |
|---:|---:|---:|---:|
| 250.000 | 35,3724 pp | 19,3262 pp | 16,5799 pp |
| 500.000 | 31,3614 pp | 20,9184 pp | 19,7454 pp |
| 1.000.000 | 30,7036 pp | 20,3307 pp | 19,3176 pp |
| 2.000.000 | 27,9000 pp | 18,3298 pp | 18,1872 pp |

Il calo tra seed è lento e non monotono rispetto all'accordo con Monker. Inoltre:

- seed 1 cambia di `17,5009 pp` tra 1M e 2M;
- seed 2 cambia di `18,7075 pp` tra 1M e 2M;
- il massimo delta marginale tra i due seed a 2M è `3,0476 pp`;
- le frequenze aggregate nascondono spostamenti molto più ampi sulle singole combo.

Una replay del seed 1 con la stessa telemetria del seed 2 riproduce in modo bit-identico la strategia root e lo stesso numero di infoset dell'export originale. Il confronto diretto separa strategia media e corrente:

| Misura a 2M | Risultato |
|---|---:|
| TV tra seed, strategia media | 27,9000 pp |
| TV tra seed, strategia corrente | **30,9242 pp** |
| TV corrente contro media, seed 1 | 19,2667 pp |
| TV corrente contro media, seed 2 | 17,4069 pp |
| classi con azione corrente dominante diversa | 27/81 |
| combo fisiche nelle classi discordanti | 192/630 |

La strategia corrente è più distante tra seed della strategia media. L'averaging lineare attenua quindi parte della differenza, ma non la causa: sono ancora diversi i rimpianti correnti. Al seed 1, 49 classi hanno una sola azione a rimpianto positivo e 32 ne hanno da due a cinque; al seed 2, 51 classi ne hanno una e 30 ne hanno due o tre. Nessuna classe ricade nel fallback uniforme.

## Controlli isolati a 250.000 iterazioni

| Controllo | WMAE media | TV media contro Monker | TV tra seed | Decisione |
|---|---:|---:|---:|---|
| v7 batch 32 | 17,9530 pp | 44,8826 pp | 35,3724 pp | baseline |
| aggiornamento online, un worker | 20,5127 pp | 51,2817 pp | 35,8437 pp | respinto |
| baseline per l'azione avversaria | 19,4702 pp | 48,6756 pp | 34,4993 pp | respinto |
| averaging non lineare | 18,9692 pp | 47,4230 pp | 33,1052 pp | respinto |
| astrazione street-adaptive v8 | 19,9818 pp | 49,9546 pp | 34,9185 pp | respinta |

Nessun controllo riduce la TV tra seed del `20%`; ogni variante peggiora l'accordo medio con Monker. Il batching non è la causa principale. La baseline e l'averaging attenuano poco la varianza. Il v8 dimostra che aumentare l'occupazione dei bucket senza preservarne la semantica peggiora il bias.

## Perché AA mostra 95,53% call

La schermata mostra due misure prodotte in momenti diversi:

1. le frequenze derivano dalla strategia media accumulata durante due milioni di iterazioni;
2. gli EV per azione sono una valutazione fisica post-hoc su 20.000 deal complessivi.

Per AA, la valutazione post-hoc dispone di soli `189` deal per azione:

| Azione | Frequenza media | EV post-hoc | ±SE |
|---|---:|---:|---:|
| All-in | 0,0138% | +8,4127a | 1,4194a |
| Raise 6a | 4,0826% | +10,9711a | 1,5948a |
| Raise 10a | 0,3730% | +8,6331a | 1,4799a |
| Call | 95,5306% | +11,2796a | 1,6250a |
| Fold | circa 0% | −1,0000a | 0 |

Il call ha anche l'EV puntuale più alto della tabella. Il vantaggio sul raise 6a è però soltanto `0,3086a`, molto più piccolo dell'incertezza marginale mostrata. Per un confronto formale serve la varianza della differenza sugli stessi deal, non la somma ingenua dei due SE.

Il fatto che quattro azioni siano EV-positive non implica che debbano ricevere frequenza. `EV > 0` confronta l'azione con lo zero monetario; regret matching confronta ogni azione con il valore della strategia al nodo.

## Come il solver calcola le percentuali

Linear MCCFR non applica una softmax agli EV visualizzati. A ogni iterazione `t` aggiorna il rimpianto cumulativo con:

`R_t(a) = R_(t-1)(a) + t × [Q_t(a) − V_t]`

La strategia corrente usa regret matching:

`sigma_t(a) = max(R_t(a), 0) / sum_b max(R_t(b), 0)`

Se tutti i rimpianti sono non positivi, usa la distribuzione uniforme. La chart espone invece la strategia media lineare:

`sigma_media(a) = strategy_sum(a) / sum_b strategy_sum(b)`

Alla root il `strategy_sum` accumula `t × sigma_t(a)` nel passaggio in cui CO è l'avversario del traverser. Gli EV post-hoc non partecipano a questo calcolo; servono soltanto per interpretare la policy già appresa.

La telemetria della run v7 seed 2 a 2M conferma il meccanismo:

| Azione AA | Rimpianto cumulativo pesato | Strategia corrente | Peso medio cumulativo | Strategia media |
|---|---:|---:|---:|---:|
| All-in | −12.772.042.150,62 | 0% | 2.655.253,16 | 0,0138% |
| Raise 6a | −3.003.943.752,09 | 0% | 788.389.898,56 | 4,0826% |
| Raise 10a | −9.998.557.104,38 | 0% | 72.024.927,22 | 0,3730% |
| Call | +359.241.388,15 | 100% | 18.447.753.352,46 | 95,5306% |
| Fold | −151.619.203.763,62 | 0% | 175,61 | circa 0% |

All'ultimo aggiornamento dell'infoset AA (`1.999.977`), soltanto call ha rimpianto positivo: la strategia corrente è quindi `100% call`. Il `95,53%` visualizzato conserva le strategie storiche visitate e pesate linearmente.

Il seed 1 porta alla stessa decisione corrente per AA: call `100%`. La sua strategia media è però call `91,5008%` e raise 6a `7,5693%`, contro call `95,5306%` e raise 6a `4,0826%` del seed 2. Per AA la decisione corrente è stabile; la differenza residua riguarda la traiettoria storica. Questo caso non va generalizzato alle altre 80 classi, perché 27 hanno ancora un'azione dominante diversa tra i due seed.

## Albero postflop nel viewer

È possibile visualizzarlo, ma non è contenuto negli attuali JSON delle chart. Quei file esportano l'intero albero preflop e gli EV delle sue decisioni; le policy postflop non sono state salvate.

Il formato del solver può già persistere la policy postflop sparsa, indicizzata da history pubblica, bucket, player e street. Per una navigazione utile servono però:

1. una nuova run con export esplicito della policy postflop;
2. ricostruzione lazy del ramo a partire dall'entry preflop e dal board scelto;
3. selettori board, street, action path e bucket/combo;
4. una valutazione EV separata per le azioni postflop, perché la policy da sola contiene frequenze, non EV.

Materializzare ogni board e ogni nodo in un singolo JSON non è praticabile. La soluzione corretta è un browser lazy: genera soltanto il ramo richiesto. L'albero strutturale si può mostrare senza un nuovo solve; frequenze ed EV richiedono una policy esportata e una valutazione dedicata.

## Decisione

Il v7 resta il miglior profilo corrente per accordo medio con Monker e memoria, ma non è stabile né qualificato. R7 resta bloccato. Il prossimo esperimento deve misurare una riduzione della varianza dei valori d'azione durante il training, senza cambiare contemporaneamente schedule, averaging e astrazione.

Artefatto aggiuntivo: `v7_regret_diagnostic_2m_seed1_monetary_v2.json`, SHA-256 `E1986CB2B1810738DF209AEDA4DE163BF44BB4E43E30E6E8AE8E823BAF267631`; il comparatore lo rifiuta come atteso con `REFERENCE_CONFIG_INCOMPLETE`.
