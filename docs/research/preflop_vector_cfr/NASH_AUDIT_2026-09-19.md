# Audit della convergenza — 2026-09-19

## Perimetro e stato

Base: `744113c69342a82f3b920add498106af2b763d52`. Branch:
`codex/nash-convergence-audit`, worktree separato. L'utente ha autorizzato il piano
come unico goal della giornata. Regole, size, utility e soglie restano invariate.

| Fase | Stato | Evidenza richiesta |
|---|---|---|
| Normalizzazione del diagnostico | PASS | enumerazione indipendente, regressioni, ricalcolo del certificato |
| Conflitti decisionali nei bucket | NOT_RUN | valori a continuazione congelata, pesi e copertura |
| Meccanismo su gioco enumerabile | NOT_RUN | confronto con riferimento senza perdita d'informazione |
| Candidato potential-aware offline | NOT_RUN | si avvia solo con meccanismo verificato |
| Confronto matched HU20/CO40 | NOT_RUN | tre seed, costi, certificazione fisica esatta |

Questo PASS riguarda il diagnostico, non la qualificazione di CO40.

## 1. Massa e probabilita dell'ingresso

Il campo storico `opponent_reach` somma i pesi delle combo avversarie dopo le
azioni preflop dell'avversario. Le azioni dell'eroe sul percorso sono forzate.
Ogni combo parte da peso 1: la somma non e una probabilita.

Siano A(h) il peso di ammissibilita dell'eroe (0 o 1 nell'API attuale), O(o) quello
dell'avversario e r(o) la massa avversaria propagata, che comprende O(o):

```text
p(entry | h) = sum[o disjoint h] r(o) / sum[o disjoint h] O(o)
p(entry)     = sum[h] A(h) p(entry | h) / sum[h] A(h)
conditional_gain = mean_gain / p(entry), se p(entry) > 0
```

Il prior e quello uniforme sulle combo ammesse dell'eroe del diagnostico storico.
Non comprende la reach preflop della sua strategia. Con i range completi,
ciascuna delle 630 combo ha 561 avversarie compatibili. Ogni combo avversaria
compare in 561 coppie disgiunte: `p(entry) = opponent_reach / 630`. Questa
semplificazione non vale per sottoinsiemi arbitrari; il codice usa i blocker.

La tabella "Perdita postflop a reach fissato su CO40" del diario del 2026-09-18
divideva direttamente per `opponent_reach`: i valori erano 630 volte inferiori
al rapporto corretto nel caso esatto a range completi.

| Ingresso | CO corretto (ante/ingresso) | BTN corretto (ante/ingresso) |
|---|---:|---:|
| limp-check | 1,172574924 | 1,132333007 |
| limp-bet-call | 1,372247263 | 1,145197437 |
| open-call | 1,421923837 | 1,134191379 |
| open-3bet-call | 0,730602369 | 0,842305039 |

Artefatto: [certificato ricalcolato](CO40_NORMALIZATION_AUDIT_2026-09-19.json).
Questi valori non usano i range condizionati effettivi di entrambi i giocatori,
non sono additivi fra ingressi e non decompongono causalmente l'exploitability.
La conclusione che il postflop fosse irrilevante perche perdeva circa 0,002 ante
va ritirata; la correzione non prova da sola la causa delle scelte preflop.

### Disponibilita e compatibilita

`mean_gain` e `opponent_reach` mantengono valori e significato. Il report aggiunge
`entry_probability` e `conditional_gain`. Quest'ultimo e opzionale, serializzato
come `null` per ingressi irraggiungibili, valutazioni non esatte o range ristretti.
Per questi ultimi, anche la distribuzione dei board richiede una verifica:
il rapporto non e ancora validato come EV condizionata fisica.
Nessun cambiamento ai binari di policy/checkpoint/stato del certificatore.
Lo schema JSON v1 mantiene i campi precedenti; i consumatori devono ignorare
i nuovi campi opzionali quando non li usano.

### Verifiche eseguite

Build `windows-release`, MSVC 19.51.36248.0, C++20, `/W4 /WX`, otto job.
Dipendenze e risorse gia disponibili; nessuna ricostruzione delle tabelle.

```powershell
ctest --preset windows-release -R '^gtosd_preflop_blueprint_certifier_tests$' --output-on-failure
ctest --preset windows-release -R '^gtosd_preflop_blueprint_(trainer|export)_tests$' --output-on-failure
```

| Suite | Esito | Tempo |
|---|---|---:|
| Certificatore | PASS, 138.974 assert | 42,46 s |
| Trainer | PASS | 76,66 s |
| Export | PASS | 48,80 s |

Il nuovo oracolo enumera le coppie disgiunte e moltiplica le probabilita sul
percorso pubblico senza la propagazione o le somme per carta della produzione.
Copre percorsi frazionari, strategie deterministiche, blocker, sottoinsiemi e
probabilita zero. Inietta valori analitici di foglia per isolare l'aggregazione:
una perdita di due ante per ingresso deve restituire due ante, non due / 630.
Il fixture sintetico non viene presentato come gioco fisico.

CO40 e stato ricalcolato da una copia dei 573 flop di
`out/baseline_20260917/cert_state.bin`, con policy originale in lettura.
Tempo: 1,4004197 s. EV, best response, guadagni globali e preflop/postflop,
NashConv e fingerprint di policy/albero sono identici come valori double al JSON
precedente. `max_gain` resta 0,82717651588212859 ante. Il ricalcolo riusa le
valutazioni salvate; non e un nuovo solve ne una nuova enumerazione dei runout.

## 2. Conclusioni precedenti da riesaminare

L'EV di self-play monotona non certifica un equilibrio. Cinque certificazioni
esatte sulla stessa traiettoria non dimostrano un limite asintotico: il training
resta campionato. Il confronto `class`/`recall32` citato come miglioramento
dell'1,7% riguarda alberi diversi. Il numero di righe da solo non prova che ogni
riga sia sufficientemente allenata.

Gli istogrammi flop attuali descrivono l'equity al river, perdendo l'associazione
alle distribuzioni condizionate ai turn. In
[Ganzfried e Sandholm (2014)](https://www.cs.cmu.edu/~sandholm/potential-aware_imperfect-recall.aaai14.pdf)
questa e la rappresentazione distribution-aware; quella potential-aware descrive
anche le transizioni fra street. Non e gia stata esclusa dall'implementazione
attuale. Non e garantito che aggiungerla basti per il gate.

## 3. Regole degli esperimenti successivi

Con avversario e continuazione congelati, confrontare la strategia corrente con
la migliore azione comune del bucket, poi con scelte separate per stati fisici.
Integrare l'informazione nascosta prima del massimo. Esporre pesi, copertura e
perdite locali senza sommarle come exploitability globale.

Se prevale l'errore della strategia comune, verificare il training prima delle
feature. Se prevale il conflitto di rappresentazione, verificarne il meccanismo
su un gioco ridotto. Un risultato ambiguo consente un solo ampliamento diagnostico
motivato, poi resta INCONCLUSIVE. Costruire tabelle nuove solo con evidenza
favorevole; minore distanza di clustering senza vantaggio decisionale non basta.
Il confronto completo usa tre seed appaiati, checkpoint prestabiliti e
certificati esatti. Nessuna scelta del seed migliore.
