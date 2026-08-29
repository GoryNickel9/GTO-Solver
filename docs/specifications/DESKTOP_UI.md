# Interfaccia desktop

## Tecnologia e confine

La GUI di prodotto è `gto_gui`, basata su Qt 6 Widgets per Windows. Il core
matematico non dipende da Qt. Il worker di solving non gira sul thread UI e
comunica progress, controlli e risultati attraverso confini thread-safe.
Il worker esegue il solve esclusivamente su CPU e RAM. Qt o il sistema grafico
possono usare accelerazione GPU soltanto per il rendering dell'interfaccia; non
esiste alcun trasferimento di traversal, regret, strategy o best response alla
GPU.

## Workflow supportato

```text
nuovo progetto -> configura -> stima -> solve -> salva -> riapri -> naviga -> resume
```

Il workflow è coperto dall'E2E di prodotto. L'applicazione permette:

- board Short Deck visuale da tre a cinque carte;
- pot, stack e rake;
- pannelli CO/OOP e BTN/IP con override per street;
- range editor 9x9 con pesi e blocker;
- preflight di nodi, infoset, azioni e memoria;
- solve, pausa, cancel e recovery;
- salvataggio/apertura `.gtsd`;
- navigazione di azioni e chance;
- strategia, reach, equity, hand category ed EV al nodo.

Il JSON è formato di persistenza/API, non requisito d'uso per il flusso
interattivo.

## Semantica delle metriche

Il target dEV è mostrato come percentuale del pot e corrisponde al massimo gain
di deviazione unilaterale normalizzato. NashConv/pot resta una metrica distinta.
La UI mostra strategia media salvo etichetta esplicita.

Gli EV interni sono payoff netti; la vista GTO+ aggiunge il contributo iniziale.
Equity non è EV. Un EV condizionale al nodo usa il range posteriore raggiunto,
non il range root non condizionato.

## Browser della soluzione

Il browser usa il vero public tree e le query postflop. La selezione esplicita
di turn/river evita di aggregare chance incompatibili. La tabella combo mostra
solo dati derivati dalla strategia e dal reached range; una classe senza massa
viene mostrata come assente, non come strategia uniforme.

Frequenze aggregate e heatmap sono pesate per reach. La GUI non deve suggerire
che due nodi con history pubblica uguale abbiano lo stesso posteriore privato se
sono stati raggiunti da strategie root differenti.

## Risorse e responsività

Il preflight seleziona RAM o out-of-core in base ai byte stimati e alle risorse
disponibili. Memoria solver, transient e RSS sono concetti distinti. Durante il
solve pausa e cancel vengono applicati a un confine sicuro di iterazione;
recovery autenticato viene aggiornato alle certificazioni previste.

Operazioni analytics on-demand possono ancora essere sincrone: il gate E2E di
responsività certifica solve/resume, non ogni analisi di un nodo grande.

## Errori e sicurezza

Config invalidi, memoria insufficiente, checkpoint incompatibili, chiavi errate,
file corrotti e failure numeriche devono essere visibili con categorie utili.
La UI non silenzia errori né sostituisce un range mancante. Le chiavi locali
sono gestite dalle impostazioni dell'applicazione nel prototipo corrente; una
release commerciale dovrà usare license layer o keystore di sistema.

## Accessibilità e localizzazione

La UI primaria è italiana; un catalogo inglese è predisposto. Scaling 100%,
150% e 200%, navigazione da tastiera e rendering software WARP sono coperti dai
gate F9/F10 pertinenti. La presenza del catalogo sorgente non equivale a una
localizzazione inglese completa.

## Funzioni future

Node locking, trainer, database flop e preflop non fanno parte del workflow F10.
Quando verranno aggiunti, devono mantenere visibile se una soluzione è locked,
astratta, campionata o appartenente a un gioco differente.
