# Regole Short Deck

## Scopo

Questo documento definisce il ruleset predefinito e separa le regole già
codificate dalle estensioni previste. Nessuna regola dipendente dalla variante
deve essere implicita nell'evaluator, nel tree builder o nella GUI.

## Mazzo e carte

Il ruleset corrente usa 36 carte:

- ranghi, in ordine: `6 7 8 9 T J Q K A`;
- semi: fiori, quadri, cuori, picche;
- ogni carta ha un identificatore compatto e univoco;
- board e hole cards non possono contenere duplicati;
- una combo bloccata dal board resta distinguibile da una combo con peso zero.

Il parser accetta la notazione di due caratteri, per esempio `Ah`, `Ks`, `6c`.
Ranghi dal due al cinque sono invalidi nel ruleset corrente.

## Ranking predefinito

Dal più forte al più debole:

1. scala colore;
2. poker;
3. colore;
4. full house;
5. scala;
6. tris;
7. doppia coppia;
8. coppia;
9. carta alta.

Il colore batte quindi il full house. La scala corta speciale è
`A-6-7-8-9`; l'asso è basso solo in questa sequenza e alto nelle altre. Il
confronto usa sempre la migliore combinazione di cinque carte disponibile.
Parità esatte dividono il piatto dopo il rake e dopo la restituzione delle chip
non chiamate.

Queste scelte sono parte dell'identità del gioco e devono contribuire al
fingerprint di configurazione. Altri ordinamenti restano previsti ma non sono
selezionabili nel prodotto corrente.

## Posizioni HU

Nel modello corrente i giocatori sono `CO` (indice 0) e `BTN` (indice 1). Nei
sottogiochi postflop il CO è OOP e agisce per primo a ogni street. La struttura
preflop completa non è ancora un workflow supportato; nomi e indici non devono
essere reinterpretati dalla GUI.

## Denaro, pot e stack

Gli importi sono interi in unità fisse da `0,0001 ante` (`10.000` unità per
ante). Lo stato pubblico conserva:

- pot iniziale e pot corrente;
- contributi iniziali e totali per giocatore;
- stack rimanenti;
- importo corrente da pareggiare;
- incremento dell'ultimo full raise;
- chip non chiamate restituite.

Stack e pot non possono essere negativi. Prima del rake vale la conservazione
delle chip. Un all-in parziale può chiamare meno del `to call`; un raise
incompleto non riapre automaticamente l'azione come un full raise.

## Bet, raise e all-in

Le size aggressive sono percentuali del pot e vengono convertite in un importo
monetario deterministico. Size che arrotondano allo stesso importo producono una
sola azione legale.

La semantica della soglia GTO+ è:

- `Disabled`: nessuna azione all-in aggiunta automaticamente;
- `Add`: l'all-in resta disponibile insieme alla size discreta quando la size
  calcolata raggiunge la soglia dello stack residuo;
- `Go`: la size discreta viene sostituita dall'all-in alla stessa condizione.

La soglia si confronta con la frazione di stack impegnata dall'azione, non con
SPR, pot odds o importo da chiamare. Il comportamento è coperto da test di
regressione specifici.

La profondità dei raise è configurabile per scenario e street. Il fixture GTO+
di parità usa una sola size aggressiva per street/giocatore e non usa bucketing;
questo non implica un limite architetturale a una sola size.

## Street e runout

Il postflop parte da flop, turn o river configurato. Quando una street termina:

- flop e turn avanzano a un chance node per ogni carta legale;
- il river termina a showdown;
- un fold termina immediatamente;
- quando non sono possibili ulteriori decisioni, le carte restanti vengono
  enumerate fino allo showdown.

Il card removal condiziona range privati e chance outcome. Gli isomorfismi
possono fondere stati solo tramite permutazioni globali lossless dei semi.

## Rake

Il modello corrente espone:

- abilitazione;
- percentuale;
- cap;
- piatto minimo;
- `no_flop_no_drop`.

Il rake viene calcolato sul called pot e sottratto nella utility terminale, non
come correzione visiva. Le chip non chiamate sono restituite prima del
settlement. Il fixture di parità GTO+ usa rake disabilitato.

Cap dipendente dal numero di giocatori, jackpot drop e rounding per valuta sono
estensioni pianificate, non supporto corrente.

## Multiway e side pot

Lo stato di base riserva spazio fino a sei giocatori, ma solver, metriche e GUI
certificati sono heads-up. Side pot e soluzione multiway non sono dichiarati
supportati finché non saranno definiti utility individuali, metriche e test
separati.
