# R6 — Coerenza della policy e divario degli all-in con Monker

Data: 2026-09-12  
Esito: `DIAGNOSTIC_PASS / CURRENT_VS_CURRENT_FAIL / MONKER_ALLIN_CAUSE_PARTIALLY_IDENTIFIED / R6_REMAINS_FAIL`

## Obiettivo

Questo controllo separa tre domande che il viewer precedente sovrapponeva:

1. quale frequenza usa la strategia media o la policy corrente;
2. contro quale policy avversaria viene calcolata l'EV di un'azione;
3. perché Monker usa l'all-in al root CO molto più spesso delle candidate interne.

Non sono stati modificati solver, astrazioni o viewer. L'attività produce una correzione
scientifica dei report esistenti e identifica il prossimo esperimento misurabile.

## Q8s: perché `-7,60a` e `-8,82a` sono diversi

Spot: `CO Raise 10a → BTN All-in → CO to act`, classe `Q8s`, V12 seed 1.

| Quantità | Risultato |
| --- | ---: |
| Frequenza Call, strategia media | `99,9896%` |
| Frequenza Call, policy corrente finale | `100%` |
| EV Call mostrata dal viewer | `-7,5996a ± 0,5518a` |
| Campioni grezzi/equivalenti | `122 / 30,485` |
| EV Fold | `-11,0000a` |
| Equity esatta contro il range medio BTN | `38,3277%` |
| EV esatta contro il range medio BTN | `-9,3379a` |
| Equity esatta contro il range corrente BTN | `38,9779%` |
| EV esatta contro il range corrente BTN | **`-8,8177a`** |

In questo terminale l'EV del Call è:

```text
EV(Call) = 80a × equity - 40a
```

Con l'equity corrente si ottiene `80 × 0,3897785 - 40 = -8,8177a`. Il calcolo manuale
`-8,82a` è quindi corretto. Il `-7,60a` non rappresenta la policy corrente: è una stima Monte
Carlo esportata contro la continuazione media, con un errore standard ancora visibile.

V12 usa già l'enumerazione esatta per l'all-in preflop durante il training. Aggiungere
l'enumerazione esatta degli all-in postflop non cambierebbe questo spot. Per visualizzarlo senza
ambiguità servono EV riesportate con lo stesso contratto della frequenza selezionata:
`media contro media` oppure `corrente contro corrente`.

## Audit completo dei dieci nodi Call/Fold

L'audit ricostruisce il range avversario condizionato dalla sequenza di azioni, conserva il card
removal fisico e usa le equity esatte delle `6.561` coppie di classi. Copre `10 × 81 = 810`
decisioni per seed. Una decisione è materiale quando assegna almeno il `5%` all'azione inferiore
e il gap esatto è almeno `0,1a`.

Il percorso `media contro media` riproduce l'audit C++ salvato con errore massimo
`2,66e-14a` nel seed 1 e `2,49e-14a` nel seed 2. L'errore massimo di normalizzazione è
`2,22e-16` per la media e zero per la corrente.

| Policy valutata | Seed | Casi materiali | Reach propria ≥1% | Reach pubblica ≥1% | Perdita locale media |
| --- | ---: | ---: | ---: | ---: | ---: |
| Media contro media | 1 | 154 | 10 | 8 | `0,31485a` |
| Media contro media | 2 | 158 | 18 | 8 | `0,26355a` |
| Corrente contro corrente | 1 | **65** | **20** | **11** | `0,21153a` |
| Corrente contro corrente | 2 | **40** | **18** | **14** | `0,15678a` |

L'audit precedente riportava `3/0` casi materiali per la policy corrente e zero casi con reach
pubblica almeno `1%`. Quel risultato confrontava le frequenze correnti con EV calcolate contro
il **range medio avversario**. Non era un audit `corrente contro corrente` e non poteva sostenere
il gate dichiarato. Il report V12 e il protocollo sono stati corretti esplicitamente.

Fra i casi correnti e raggiunti del seed 1, al nodo 13 `KQo` chiama il `100%`: l'EV esatta del
Call è `-14,4774a`, contro `-7a` del Fold, con reach pubblica `1,0002%`. `TT` chiama ancora il
`100%`, con gap `6,1468a` e reach pubblica `1,7542%`. Nel seed 2 gli errori correnti raggiunti
restano 14. Non sono residui solo grafici della strategia media.

Questo non rende automaticamente errato CFR: la policy istantanea può oscillare e non è la
soluzione pubblicabile prevista dalla teoria. Dimostra però che 2M iterazioni non hanno prodotto
né una media qualificata né una policy finale localmente stabile.

## Quanto più spesso Monker va all-in

Le frequenze seguenti pesano le 81 classi per le 630 combo fisiche del root CO.

| Azione root CO | Monker | V8 media 2M | V12 media 2M | V12 corrente finale |
| --- | ---: | ---: | ---: | ---: |
| All-in | **40,83%** | `20,49%` | `18,72%` | `22,36%` |
| Raise 6a | `2,37%` | `8,65%` | `8,77%` | `6,65%` |
| Raise 10a | `4,70%` | `4,99%` | `7,51%` | `7,00%` |
| Call | `7,56%` | **`31,92%`** | **`31,42%`** | **`30,02%`** |
| Fold | `44,55%` | `33,96%` | `33,59%` | `33,97%` |

Il divario non descrive un solver interno semplicemente più prudente. Rispetto a Monker, V8
folda circa `10,60 pp` in meno e chiama circa `24,36 pp` in più. Gran parte del range che Monker
porta direttamente all-in viene quindi deviata verso il Call, non verso il Fold.

Il deficit all-in di V8 è `20,34 pp`: `10,38 pp` provengono dalle mani offsuit, `8,09 pp` dalle
suited e `1,87 pp` dalle coppie. I contributi maggiori sono `QJo`, `JTo`, `AKo`, `A8o`, `A6o`,
`A7o`, `T9o`, `KTo`, `KJs` e `KK`. In V12 il deficit medio sale a `22,10 pp`.

La risposta BTN al jam CO è invece vicina: Monker chiama il `27,55%`, V8 circa `29,6–30,1%` e
V12 circa `29,8–30,0%` nella strategia media. Una differenza di circa due punti nella risposta
BTN non basta a spiegare venti punti di jam mancanti al CO.

## Perché il divario può diventare enorme

Molte mani che Monker manda all-in sono quasi indifferenti fra più azioni. Per `JTo`, Monker
riporta circa `-0,401a` per All-in, `-0,408a` per Raise 6a e `-0,403a` per Call. Per `A6o`, le
quattro continuazioni sono comprese fra `-0,899a` e `-0,893a`. Per `KK`, All-in, Raise 6a e
Raise 10a differiscono di circa `0,007a`.

Quando il gap reale è di pochi millesimi di ante, un piccolo errore nelle continuation value può
spostare quasi tutta la frequenza da All-in a Call senza produrre una grande differenza di EV.
Nel nostro export V12 l'errore assoluto medio delle EV root rispetto a Monker è circa
`0,32–0,34a` per All-in, ma `0,62–0,63a` per Call e `0,64–0,86a` per i raise. Anche l'errore
standard medio è maggiore sui rami non all-in. Il ramo jam, che termina senza decisioni
postflop, è quindi molto più stabile dei rami Call/Raise.

L'ipotesi principale è che le continuation value postflop del nostro solver sovrastimino Call e,
in parte, i raise. Le cause compatibili con i dati sono:

1. astrazione postflop e imperfect recall che accorpano stati con valori diversi;
2. pochi campioni effettivi nei rami postflop profondi anche dopo 2M iterazioni;
3. oscillazione della policy istantanea e media CFR non ancora stabilizzata;
4. albero o astrazione postflop Monker non noti, che impediscono l'attribuzione univoca.

L'albero preflop, il contratto monetario e l'equity degli all-in preflop sono già stati verificati.
Monker non usa una sola azione non all-in al root: l'export contiene Raise 6a, Raise 10a e
All-in. La sola action abstraction preflop non spiega quindi il fenomeno.

## Enumerazione esatta degli all-in postflop

Può migliorare i rami che arrivano a un all-in sul Flop o sul Turn, eliminando la varianza del
runout successivo: 406 runout dal Flop, 28 dal Turn e un solo showdown dal River. È una modifica
sensata da profilare, soprattutto sul Turn.

Non corregge da sola:

- l'EV Q8s, perché lo spot è già un all-in preflop esatto;
- il mescolamento fra frequenze correnti ed EV medie nel viewer;
- l'errore di astrazione dei rami che non finiscono subito all-in;
- l'oscillazione corrente e l'averaging della policy.

Per questo l'enumerazione postflop resta il prossimo esperimento isolato, non una spiegazione già
dimostrata del divario Monker.

## Decisione

V12 resta bocciata. Il vecchio sub-gate sulla policy corrente è ritirato perché usava policy
avversarie non coerenti; il nuovo audit `corrente contro corrente` fallisce. Il divario degli
all-in Monker è attribuito con buona evidenza a continuation value postflop troppo favorevoli al
Call, amplificate da molte quasi-indifferenze. L'origine precisa fra campionamento, astrazione e
albero Monker resta non identificabile finché manca il contratto postflop esterno.

Il prossimo passo è profilare l'enumerazione esatta degli all-in postflop e accettarla solo se
riduce errore e varianza a parità di albero, astrazione e coppia di run da 2M.
