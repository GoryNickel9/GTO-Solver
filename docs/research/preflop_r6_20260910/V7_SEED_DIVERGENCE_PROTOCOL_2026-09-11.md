# R6 — protocollo di diagnosi della divergenza V7 fra seed

## Obiettivo

Separare mancata convergenza del training e bias dell'astrazione prima di progettare V8. I due run V7 a 2M condividono albero, partizione e mapping; cambia il seed del training. Una divergenza fra loro non può quindi essere attribuita a bucket differenti.

## Esperimento congelato

Eseguire V7 sul contratto monetario v2 a `250k`, `500k` e `1M` per i due seed già usati a `2M`.

Parametri invarianti:

- Linear MCCFR, otto worker e batch 32;
- abstraction V7 `32/128/512`, MC8;
- partition seed `5923736619020287489`;
- fingerprint richiesto `fnv1a64:a68337fa567aa2d9`;
- nessun export completo delle chart, perché il test usa la strategia root;
- 1.000 deal di evaluation e 1.000 iterazioni/deal della risposta appresa, esclusi dai criteri strategici.

## Metriche

Per ogni budget:

1. WMAE e TV della strategia root rispetto a Monker;
2. TV root fra i due seed, pesata con masse fisiche `6/4/12`;
3. TV della stessa strategia fra budget successivi;
4. differenza dei marginali root;
5. infoset, payload e tempo.

La TV usa `0,5 × Σ_a |σ_1(a|h) − σ_2(a|h)|`, poi pesa le 81 classi con la relativa massa fisica.

## Regola di decisione

- TV cross-seed e TV temporale in calo: il limite dominante è sampling/budget; non implementare V8 prima di migliorare la dinamica di training.
- TV stabile ma forte errore rispetto a Monker: l'astrazione o il gioco esterno sono il limite dominante; aprire un protocollo V8.
- TV irregolare e alta: nessuna candidata R6; analizzare weighting lineare, batching e varianza prima di aumentare automaticamente le iterazioni.

Gli EV post-hoc non decidono questo gate. Sono stime indipendenti dalla policy appresa e, con 20.000 deal complessivi, hanno campioni per classe troppo piccoli per spiegare le percentuali.
