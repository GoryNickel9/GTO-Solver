# Formato dell'albero

## Configurazione dichiarativa

`PostflopTreeConfig` versione 1 è la sorgente serializzabile del public tree.
Contiene board iniziale, pot, stack effettivo, rake e configurazione delle
azioni per flop/turn/river, giocatore e scenario.

Gli scenari sono:

- `Lead`: primo giocatore della street;
- `AfterCheck`: risposta dopo check;
- `FacingBet`: risposta a una puntata o raise.

Ogni scenario dichiara size aggressive, profondità raise, modalità/soglia
all-in e minimum bet. Lo schema JSON installato è
`schemas/postflop_tree_config.schema.json`.

## Validazione

Prima della costruzione vengono respinti:

- versione non supportata;
- board invalido o duplicato;
- pot, stack, percentuali o minimum bet invalidi;
- troppe size o raise depth non supportata;
- combinazioni di street e board incoerenti;
- configurazioni che non possono produrre azioni legali coerenti.

Size differenti arrotondate allo stesso importo vengono deduplicate. La
generazione applica minimum raise, stack residuo, call parziale, full/incomplete
raise e soglia all-in secondo il contratto delle regole.

## Public tree fisico

`PublicTree` versione 1.0 contiene:

- config originale;
- `root` e vettore di nodi con `NodeId` stabile nella build deterministica;
- statistiche fisiche;
- `betting_tree_hash`.

I nodi sono `Decision`, `Chance`, `TerminalFold` o `TerminalShowdown`. Gli edge
sono azioni oppure carte chance. Ogni chance edge conserva outcome fisici e
numero totale di outcome legali, necessari quando gli isomorfismi comprimono
carte equivalenti.

Un limite di build predefinito di 20 milioni di nodi protegge da allocazioni
accidentali. Superarlo produce un errore esplicito, non un albero troncato.

## Chance condizionata

Il public tree non conosce le hole cards. Durante il traversal,
`condition_chance_edges` rimuove board, carte private e dead cards e ricalcola
la massa degli outcome compatibili. Non è valido dividere uniformemente sugli
edge canonici ignorando le molteplicità fisiche.

## Identità e canonicalizzazione

L'identità semantica comprende almeno:

- ruleset e aritmetica;
- board iniziale;
- pot e stack;
- rake;
- tutte le azioni configurate e la loro semantica;
- versioni di tree e isomorfismo.

Il hash deve cambiare quando cambia il gioco. L'isomorfismo usa tutte le
permutazioni globali dei quattro semi e sceglie una rappresentazione canonica
lossless. La stessa permutazione si applica a board, hole cards e runout; una
chiave basata sul solo board è vietata.

## DAG canonico e infoset

Il public tree fisico è l'oracolo strutturale. Il canonical public DAG può
condividere stati pubblici equivalenti; gli infoset restano distinti per
giocatore e combo privata canonica. Le statistiche devono riportare
separatamente nodi fisici, nodi canonici, infoset e action slots.

Il DAG è un'ottimizzazione lossless: disabilitarlo deve preservare azioni,
utility ed EV entro la tolleranza numerica.

## Evoluzione del formato

Ogni cambiamento incompatibile a parsing, semantica delle size o identità dei
nodi richiede una nuova versione major o una migrazione esplicita. Aggiunte
retrocompatibili richiedono default serializzati e test round-trip. Un config
vecchio non deve acquisire nuove azioni per effetto di un default non dichiarato.

Il formato corrente copre HU postflop. Preflop e multiway avranno versioni o
tipi distinti, non campi opzionali reinterpretati senza fingerprint.
