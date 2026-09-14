# R6 — Diagnostica delle decisioni contro all-in e viewer completo

Data: 2026-09-12  
Stato del lavoro: implementazione diagnostica completata; gate scientifico R6 non superato  
Candidato analizzato: V8, seed 1 e 2, 2.000.000 di iterazioni per seed

> Correzione del 2026-09-12: le righe chiamate “policy corrente” in questo report usano la
> frequenza corrente contro l'EV del range medio avversario. Non costituiscono un confronto
> corrente contro corrente. Il contratto corretto e i nuovi risultati sono in
> [R6_POLICY_CONDITIONING_AND_MONKER_ALLIN_DIAGNOSTIC_2026-09-12.md](R6_POLICY_CONDITIONING_AND_MONKER_ALLIN_DIAGNOSTIC_2026-09-12.md).

## Obiettivo

Questa fase risponde a due problemi separati:

1. verificare perché la strategia visualizzata possa assegnare frequenza a un'azione con EV inferiore al fold;
2. completare il viewer con V10, albero pubblico postflop, griglia 9×9, EV per classe e size arrotondate.

Il caso guida è `CO Raise 6a → BTN All-in → CO to act`, classe `K7o`. Nel vecchio export V8 il Call aveva frequenza `83,19%`, EV stimata `-17,127a ± 13,846a` e il Fold EV `-7a`.

## Audit esatto delle decisioni contro all-in

L'audit non usa l'EV Monte Carlo del viewer per decidere quale azione sia migliore. Enumera l'equity preflop Short Deck tramite il catalogo canonico completo e valuta esattamente Call e Fold contro il range avversario raggiunto.

| Evidenza | Valore |
| --- | ---: |
| Board canonici | 19.998 |
| Board fisici non ordinati rappresentati | 376.992 |
| Esiti matchup rappresentati | 1.423.446.393.600 |
| Fingerprint della tabella equity | `fnv1a64:fe73211ffab94a00` |
| Nodi Call/Fold contro all-in | 10 |
| Righe classe/nodo per seed | 810 |
| Righe materiali nella strategia media, seed 1 | 207 |
| Righe materiali nella strategia media, seed 2 | 206 |

Una riga è materiale quando l'azione inferiore riceve almeno il `5%` e il divario esatto è almeno `0,1a`.

### Controllo indipendente con Poker-Quant

La tabella interna è stata confrontata con
`F:\Poker-Quant\equity_calculator_cpp\data\preflop_hu_exact_sixplus_v1.tsv` senza usare
Poker-Quant nel percorso di calcolo del solver. La sorgente esterna contiene `10.215` matchup
fisici canonici mano-contro-mano ed è identificata dallo SHA-256
`2FAD50C45560F78896D509B9887D1E8E91F28AB7DD3B50F89CCB240ABF96A61B`.

Il controllo ha ricostruito tutti i `353.430` matchup fisici ordinati, li ha aggregati nelle
`6.561` coppie di classi e ha confrontato ogni equity con la tabella interna. L'errore assoluto
massimo è `1,2212453270876722e-15`; nessuna cella supera la tolleranza `1e-12` e nessun conteggio
di runout è diverso. Esito: `PASS`.

## Risultato sul caso K7o

Per V8 seed 1, nodo 40:

| Misura | Risultato |
| --- | ---: |
| EV esatta Call | `-11,583270a` |
| EV esatta Fold | `-7,000000a` |
| Svantaggio esatto del Call | `4,583270a` |
| Strategia media | Call `83,189%`; Fold `16,811%` |
| Strategia corrente finale | Call `0%`; Fold `100%` |
| Reach delle precedenti azioni di K7o | `0,0001286%` |
| Reach della sequenza BTN, condizionata su K7o | `22,3096%` |
| Reach pubblica finale della history | `0,0000287%` |
| Ultimo aggiornamento dell'infoset | iterazione `1.999.906` |

L'errore standard elevato del vecchio export rendeva impreciso il numero `-17,127a`, ma non causava l'ordine errato: l'audit esatto conferma che il Fold è migliore. Il punto decisivo è diverso: il `83,19%` appartiene alla strategia media storica, mentre la strategia corrente finale ha già imparato a foldare sempre.

## Perché la frequenza non segue direttamente l'EV mostrata

Il solver non applica un softmax agli EV finali. Regret matching costruisce la policy corrente dai regret controfattuali cumulati. La policy pubblicata è poi la media delle policy visitate durante il training.

Nel percorso External Sampling usato da V8, l'accumulatore della strategia media riceve, per ogni azione `a` dell'infoset `I`, un incremento equivalente a:

```text
strategy_sum[I,a] += peso_iterazione × reach_proprio(I) × strategia_corrente[I,a]
```

Quando una classe smette quasi del tutto di scegliere una sua azione precedente, il suo `reach_proprio` nel nodo successivo tende a zero. Le iterazioni recenti, anche se hanno una policy corrente corretta, aggiungono quasi niente alla media. La massa accumulata nelle prime iterazioni resta quindi visibile. Per K7o, il ramo `Raise 6a` ha reach proprio pari a circa `1,29 × 10^-6`: la media è congelata, non è la decisione corrente del solver.

Questo comportamento è compatibile con l'averaging realization-weighted di CFR: una policy in un infoset fuori percorso può essere arbitraria senza cambiare materialmente il valore globale del profilo. Non è però una rappresentazione utile quando il viewer forza l'utente dentro quel nodo e confronta localmente le azioni.

## Il problema non è limitato a K7o

Il nuovo export del training state confronta la strategia media con il regret matching finale su tutte le 810 righe:

| Controllo, V8 seed 1 | Righe |
| --- | ---: |
| Azione esattamente inferiore con frequenza materiale nella media | 207 |
| La policy corrente finale sposta l'azione inferiore sotto il 5% | 189 / 207 |
| Azione inferiore ancora materiale nella policy corrente | 26 |
| Policy corrente materiale con reach proprio almeno 1% | 4 |
| Policy corrente materiale con reach pubblica almeno 1% | 3 |

I tre casi ancora materiali e pubblicamente raggiunti sono:

| Nodo | Classe | Azione inferiore | Freq. media | Freq. corrente | Gap esatto | Reach pubblica |
| ---: | --- | --- | ---: | ---: | ---: | ---: |
| 28 | JTo | Call | 80,49% | 100,00% | 0,493307a | 9,999% |
| 55 | T9s | Fold | 66,97% | 64,01% | 0,452115a | 19,785% |
| 28 | KTs | Fold | 41,75% | 38,33% | 0,476661a | 8,211% |

Questi tre casi non si spiegano con il solo congelamento della media nel profilo ibrido allora
usato. La successiva rivalutazione corrente contro corrente ha mostrato che il problema è più
ampio. Di conseguenza V8 resta una candidata di ricerca, non una soluzione qualificata.

## Correzione richiesta prima di V12

V12 non può essere promossa eseguendo soltanto più iterazioni. Il prossimo candidato deve includere:

1. un payoff atteso a varianza ridotta per i terminali preflop all-in, riusando la tabella equity esatta per classe invece di dipendere da un solo board campionato;
2. un gate locale esatto su tutti i 10 nodi Call/Fold, separato dalle metriche root e dalla WMAE contro Monker;
3. una distinzione esplicita nel viewer fra strategia media e strategia corrente, con avviso sui nodi a reach trascurabile;
4. una run completa da almeno 2.000.000 di iterazioni per ogni seed pubblicato.

La strategia corrente non sostituisce globalmente la strategia media: farlo cancellerebbe la
garanzia associata all'averaging di CFR. Serve come diagnostica e mostra che parte delle anomalie
visualizzate è storica. I tre errori qui riportati appartengono però al profilo ibrido; il controllo
corrente contro corrente nel report correttivo sostituisce questa quantificazione.

## Viewer implementato

Il viewer locale ora offre:

- Monker e le coppie complete V8, V10 e V11, tutte a 2M;
- tutti i 20 nodi dell'albero preflop;
- nove ingressi e 10.060 nodi decisionali dell'albero pubblico postflop;
- selezione di Flop, Turn e River, griglia 9×9 e policy per entrambi i giocatori;
- EV di ogni classe e di ogni azione, campioni ed errore standard.

Le size sono arrotondate nel rendering a un massimo di due cifre decimali. I valori interni dell'albero non vengono modificati. Le frequenze postflop provengono dalla policy media V8 seed 1 a 2M; gli EV postflop sono stime Monte Carlo condizionate sul board e sulla history, non exploitability.

## Implementazioni

- `benchmarks/hu_preflop_all_in_policy_audit.cpp`: valutazione esatta di Call e Fold per classe, nodo e range avversario raggiunto;
- `HuPreflopDecisionTrainingDiagnostic`: export di policy corrente, regret cumulati, peso medio e ultima iterazione per ogni decisione preflop;
- `tools/analyze_hu_preflop_allin_diagnostics.py`: confronto media/corrente e reach pubblico con card removal fisico;
- `benchmarks/hu_preflop_postflop_query.cpp`: worker persistente per navigazione e query della policy postflop;
- `tools/hu_preflop_chart_viewer/serve_viewer.py`: server locale e API del viewer.

## Validazione eseguita

| Controllo | Risultato |
| --- | --- |
| Build Release con warning trattati come errori | PASS |
| Test HU preflop | PASS, 15.915 assertion |
| Replica V8 seed 1 a 2M | PASS, root strategy identica all'export originale |
| Audit esatto dei due seed V8 | PASS |
| Differential test con Poker-Quant | PASS, 6.561/6.561 celle entro `1e-12` |
| Analisi media/corrente/reach pubblico | PASS |
| Export preflop | PASS, 20 nodi, 1.620 righe per candidata, 630 combo fisiche |
| Navigazione API Flop/Turn/River | PASS |
| Browser: V10, griglia 9×9, EV per cella, dettaglio per azione | PASS |
| Browser: stack e commitment senza segno spurio | PASS |
| Browser: size `1,32a`, `2,64a`, `4,8a` | PASS |

Nel controllo browser sul Flop `Ac Kd Qh`, la risposta V8 a 2M ha prodotto `PASS · CO Flop · 32 campioni/azione`, 81 celle e il dettaglio completo di AA. Il backend è rimasto residente per evitare di ricaricare la policy da circa 367 MB a ogni query.

## Limiti dichiarati

- Il contratto postflop di Monker non è disponibile; il viewer non inventa un confronto postflop.
- Gli EV postflop dipendono dal numero di campioni e riportano sempre l'errore standard.
- L'audit Call/Fold è esatto rispetto alla policy e all'astrazione preflop esportate, non certifica l'intero gioco.
- V8, V10 e V11 non superano ancora R6. V12 resta bloccata dal nuovo gate locale.
