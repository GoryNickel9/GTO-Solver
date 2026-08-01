# Modello matematico

## Gioco estensivo

GTOSD rappresenta il sottogioco poker come gioco estensivo a informazione
imperfetta. Un'istanza è definita da:

- stati pubblici `s` (board, pot, stack, history e giocatore attivo);
- carte private `h_i` e range pesati iniziali `r_i(h_i)`;
- nodi chance con probabilità condizionate alle carte ancora disponibili;
- azioni legali `A(s)`;
- infoset `I=(s,h_i)` per il giocatore attivo;
- utility terminali `u_i(z)` in ante nette.

Nel dominio HU senza rake e senza drop vale `u_0(z)+u_1(z)=0`. Con rake il
gioco a due giocatori non conserva questa identità: la somma è la perdita verso
la casa.

## Range e reach

Un range è una misura non negativa sulle combo private. I pesi non vengono
rinormalizzati silenziosamente. Per una history pubblica `a_1...a_k`, il reach
di una coppia compatibile di mani è

```text
pi(h_0,h_1,s) = r_0(h_0) r_1(h_1) pi_c(s) product_j sigma_j(a_j | I_j)
```

dove `pi_c` include card removal e chance. Per aggiornare il giocatore `i`, il
counterfactual reach esclude le probabilità delle sue azioni e include quelle
dell'avversario e della chance.

Il range posteriore a un'azione non è la sola frequenza aggregata. Per esempio,
al nodo BTN dopo una bet CO:

```text
P(h_CO | bet, h_BTN) proportional to
  r_CO(h_CO) * sigma_CO(bet | root,h_CO) * compatible(h_CO,h_BTN,board)
```

Per questo due equilibri con lo stesso EV root possono avere EV condizionali BTN
diversi se selezionano strategie root combo-per-combo differenti.

## Strategia e averaging

Una strategia comportamentale assegna a ogni infoset una distribuzione valida:

```text
sigma(a|I) >= 0
sum_a sigma(a|I) = 1
```

Il regret matching usa i regret cumulativi positivi; se nessun regret è
positivo, applica la distribuzione uniforme sulle azioni legali. Questa è una
regola algoritmica locale, non un fallback per range mancanti.

La strategia mostrata e certificata è la strategia media derivata da
`cumulative_strategy`, salvo indicazione esplicita. Strategia corrente e media
non sono intercambiabili.

## Valore ed EV

Il valore del profilo a un nodo è l'attesa della utility terminale condizionata
al raggiungimento di quel nodo. La convenzione interna è payoff netto rispetto
alle chip investite. La vista compatibile con GTO+ aggiunge la quota di pot
iniziale del giocatore:

```text
EV_GTO+(i,node) = profile_value(i,node) + initial_contribution_i
```

Questa trasformazione cambia l'origine contabile, non la strategia. Equity è la
probabilità di vincita/tie allo showdown sotto il range condizionale; non è EV e
non include fold equity, size o rake.

Gli EV di figli condizionali ricompongono il root tramite la reach probability
pubblica. Nel fixture AhKhQh senza rake:

```text
EV_CO(root) + EV_BTN(root) = pot iniziale
```

nella convenzione di display GTO+.

## Counterfactual value e regret

Per l'azione `a` nell'infoset `I` del giocatore `i`, il valore
counterfactuale è la somma delle utility pesata dal reach di chance e
avversario. Il regret istantaneo è

```text
r_t(I,a) = v_i(sigma_{I->a}, I) - v_i(sigma, I)
```

e il regret cumulativo dipende dalla variante CFR. Gli update HU postflop
production sono esatti sul tree costruito: nessun campionamento e nessun
bucketing.

## Best response, NashConv e dEV

Per un profilo medio `sigma`, il best-response gain del giocatore `i` è

```text
g_i = BR_i(sigma_-i) - v_i(sigma)
```

e

```text
NashConv = g_0 + g_1
normalized_nash_conv = NashConv / pot_iniziale
```

La GUI espone target dEV come percentuale del pot. Il controllo operativo può
anche usare il massimo gain normalizzato individuale:

```text
normalized_max_deviation = max(g_0,g_1) / pot_iniziale
```

La metrica, la normalizzazione e il criterio di arresto devono essere sempre
riportati insieme. Un numero di iterazioni, da solo, non dimostra convergenza.

## Equilibri multipli e confronti esterni

La vicinanza dell'EV root è una proprietà del valore del gioco; non obbliga due
solver a scegliere la stessa strategia in regioni quasi indifferenti. La
comparazione combo-per-combo o degli EV condizionali è significativa come gate
di correttezza soltanto se anche i posteriori che raggiungono il nodo sono
allineati. Il test controllato F10.4 previsto nel parity journey bloccherà la
strategia root GTO+ esclusivamente come diagnostica del gioco vincolato.
