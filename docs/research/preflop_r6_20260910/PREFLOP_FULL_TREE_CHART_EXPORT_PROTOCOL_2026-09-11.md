# Protocollo per l'export completo delle chart preflop

## Obiettivo

Esportare ogni nodo decisionale preflop del solve HU CO40, non soltanto la root CO. Ogni chart deve contenere, per tutte le 81 classi, frequenza ed EV di ciascuna azione, EV della strategia e incertezza campionaria.

## Contratto

1. La strategia proviene dalla `HuPreflopBlueprint` media prodotta dallo stesso solve. Non viene ricostruita dal riferimento Monker.
2. L'EV di un'azione in un nodo è la utility dell'attore dopo aver forzato quell'azione e aver seguito la policy media nelle continuazioni.
3. Per una classe privata dell'attore, i deal sono pesati con la probabilità delle sole azioni precedenti dell'avversario. Le azioni precedenti dell'attore si cancellano quando si condiziona sulla sua classe e sulla history pubblica.
4. La media pesata usa `sum(w * payoff) / sum(w)`. L'errore standard usa varianza pesata ed effective sample size `sum(w)^2 / sum(w^2)`.
5. Un valore con peso nullo non viene inventato: è esportato come non raggiungibile.

Questa è una valutazione Monte Carlo condizionata della policy del nostro solver. Non rende comparabile il gioco con Monker finché il contratto postflop esterno resta sconosciuto.

## Gate di implementazione

- l'opzione è esplicita e non aumenta il costo dei run che non richiedono le chart complete;
- numero degli export uguale al numero di nodi decisionali preflop;
- azioni, attore e history derivati dall'albero, non da nomi hardcoded;
- probabilità finite, non negative e normalizzate;
- EV e SE finiti quando il peso è positivo;
- root completa coerente con la diagnostica root esistente;
- determinismo a seed e configurazione invariati;
- build Release `/W4 /WX` e regressione preflop verdi prima di un run lungo.

## Sequenza dei run

1. solve ridotto con export completo;
2. controllo automatico dei 20 nodi e delle 81 classi;
3. V6 Linear MCCFR 2M su due seed;
4. V7 Linear MCCFR 2M sugli stessi due seed;
5. integrazione nel viewer soltanto dopo il superamento dei controlli.

I run a 2M sono una riapertura diagnostica richiesta esplicitamente dall'utente dopo il fallimento dei gate progressivi V6/V7. Non promuovono le rappresentazioni e non sbloccano R7 senza una nuova validazione scientifica.
