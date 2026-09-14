# V21 — Protocollo di calibrazione NashConv whole-game

Data: 2026-09-14  
Stato: `PREREGISTERED_BEFORE_IMPLEMENTATION / EXECUTION RECORDED`

L'esecuzione e gli scostamenti dal protocollo sono registrati nel
[report V21](V21_WHOLE_GAME_NASHCONV_IMPLEMENTATION_REPORT_2026-09-14.md). I gate definiti qui
restano invariati: la calibrazione ridotta è distinta dalla certificazione CO40 completa.

## Obiettivo

Validare il percorso `MCCFR -> strategia media -> best response esatta -> NashConv` su un gioco
Short Deck ridotto che attraversi preflop, flop, turn e river. Il confronto deve usare due
implementazioni indipendenti: l'oracolo monolitico `FiniteGame` e il percorso di certificazione
destinato alla decomposizione preflop/postflop.

Il gate controlla il calcolo della metrica. Non misura la NashConv di V17, V18 o di un futuro
candidato V20/V21.

## Contratto della fixture

La fixture deve conservare:

- due giocatori con informazione privata non degenerata;
- almeno due mani fisiche per giocatore e almeno quattro deal privati compatibili;
- card removal reale;
- una sequenza pubblica con chance node a flop, turn e river;
- decisioni su tutte e quattro le street;
- fold, call, raise o bet e almeno una linea all-in;
- showdown tramite l'evaluator Short Deck del prodotto;
- information set che non includano carte private avversarie o runout futuri;
- utility finite e regole di rake esplicite.

Il numero ridotto di outcome appartiene soltanto alla fixture. La successiva certificazione del
checkpoint completo dovrà includere tutti i range e gli outcome definiti dal contratto HU CO40.

## Algoritmo e checkpoint

Il training usa `LinearMccfr` con seed fisso e strategia media. La curva minima contiene tre
checkpoint incrementali ottenuti tramite resume byte-coerente. Ogni checkpoint viene valutato in
sola lettura.

La best response esatta deve:

1. sommare gli outcome chance;
2. applicare la strategia congelata ai nodi dell'avversario;
3. aggregare tutte le history dello stesso information set;
4. scegliere una sola azione massimizzante per information set del responder;
5. eseguire separatamente `BR_CO` e `BR_BTN`.

## Metriche

Per ogni checkpoint registrare:

- iterazioni MCCFR;
- EV del profilo per entrambi i giocatori;
- EV delle due best response;
- deviation gain per giocatore;
- NashConv;
- NashConv normalizzata sul piatto iniziale;
- errore massimo di normalizzazione della strategia;
- fingerprint del gioco e del checkpoint.

## Gate di accettazione

L'implementazione passa soltanto se:

- il gioco e la strategia superano i validatori esistenti;
- ogni best-response value è almeno pari al profile value entro la tolleranza numerica;
- NashConv è finita e non negativa;
- la NashConv finale è inferiore a quella del primo checkpoint;
- il resume MCCFR coincide byte per byte con il run continuo;
- l'oracolo monolitico e il percorso candidato coincidono per profile EV, BR e NashConv entro
  `1e-10`;
- i test Release e il controllo del diff non segnalano regressioni.

Un risultato campionato senza limite sull'errore di ottimizzazione non può impostare
`nashconv_certified=true`.

## Passaggio al gioco completo

Dopo il gate ridotto, il runner whole-game deve consumare un unico checkpoint medio composto da
blueprint preflop e policy postflop. Ogni artefatto intermedio deve portare gli stessi fingerprint
di tree, checkpoint e continuation. Il certificatore deve fallire se manca una boundary
raggiungibile, se viene applicata una fallback non dichiarata o se due task provengono da
checkpoint diversi.

La prima esecuzione completa sarà preceduta da un probe di throughput su task facili, mediani e
pesanti. Il probe determinerà il tempo del run; non viene preregistrata una stima non misurata.
