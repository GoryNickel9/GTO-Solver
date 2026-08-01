# Canonical public DAG

Contratto canonico corrente: [`specifications/TREE_FORMAT.md`](specifications/TREE_FORMAT.md).

I conteggi AhKhQh in questo documento appartengono alla fixture storica
`GTP-AHKHQH-001` senza raise. Il gate GTO+ corrente usa la fixture corretta
`GTP-AHKHQH-003`; i suoi golden sono nel Journey canonico.

Aggiornato: 2026-07-29

## Obiettivo

F10.2 riduce il lavoro del solver exact senza modificare il gioco. Il public
tree fisico rimane la sorgente verificabile durante la costruzione, mentre il
traversal usa un DAG ottenuto dalle sole permutazioni globali dei semi che
lasciano invariati:

- board iniziale;
- range CO;
- range BTN;
- action history pubblica;
- ordine delle chance card già distribuite.

Non vengono usati sampling, bucketing, rinormalizzazione o canonicalizzazione
del solo board.

## Rappresentazione

Ogni nodo canonico conserva:

- stato pubblico e board del rappresentante;
- tipo decision, chance o terminale;
- action list nello stesso ordine del tree fisico;
- indice del board range-aware;
- layout dell'information set;
- molteplicità di update CFR;
- gruppi di outcome chance.

Ogni outcome chance conserva separatamente:

- carta fisica;
- molteplicità fisica;
- figlio canonico;
- permutazione globale dal figlio fisico al rappresentante canonico.

Il traversal blocca prima la carta nella rappresentazione del padre, trasforma
entrambi i reach vector verso il figlio e applica il mapping inverso ai valori
restituiti. Outcome dello stesso gruppo condividono una sola visita del figlio
soltanto quando i reach vector trasformati sono identici.

## Update CFR+

Il layout F10.1 condivide già regret e strategy sum tra information set
isomorfi. Nel DAG viene visitato un solo rappresentante pubblico, ma tutte le
combo private fisiche compatibili con quel rappresentante restano enumerate.

La molteplicità applicata all'update è quindi:

```text
occorrenze fisiche totali dell'information set
------------------------------------------------
occorrenze private nel rappresentante pubblico
```

Questo evita di contare due volte le orbite private. Il clipping CFR+ viene
applicato dopo l'accumulo completo del giocatore, come in F10.1.

## Regressione GTO+

Configurazione:

- flop `Ah Kh Qh`;
- range identici `AA-QQ, AKs-AQs, KQs, AKo-AQo, KQo`;
- bet 75%, nessun raise;
- pot 40, stack 100.

| Metrica | Public tree fisico | Public DAG canonico |
|---|---:|---:|
| Nodi pubblici | 52.644 | 14.673 |
| Visite CFR, due giocatori e una iterazione | 105.288 | 29.346 |
| Information set canonici | - | 125.352 |
| Action entry canoniche | - | 250.704 |

Il test differenziale esegue due iterazioni in tre modalità:

1. tree e information set fisici;
2. tree fisico con information set canonici;
3. public DAG con information set canonici.

Profile EV, best-response EV e NashConv coincidono entro `1e-11`. Tra le
modalità 2 e 3 vengono confrontati anche tutti i 250.704 regret e strategy sum.

## Limiti

La costruzione parte ancora dal public tree fisico e solo successivamente lo
compatta. Il peak RSS di costruzione non beneficia quindi dell'intera riduzione
del DAG. Il percorso accuratezza predefinito conserva due array `float64`, pari
a 4.011.264 byte per questa configurazione. Il benchmark GTO+ usa invece la
modalità prestazioni esplicita con stato `float32` e calcolo/certificazione
`float64`, pari a 2.005.632 byte; non è sampling né card abstraction.

Il prossimo miglioramento di memoria richiede costruzione canonica diretta o
storage numerico configurabile con validazione separata della precisione.
