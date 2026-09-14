# R6 — Decomposizione della TV fra i seed V18

Data: 2026-09-13  
Esito: `DIAGNOSTIC_PASS / V18_SEED_TV_ABOVE_5PP`

## Risultato

La TV root fra i due seed V18 è `13,0159 pp`
per la strategia media e `22,7363 pp`
per la policy corrente. Il nuovo gate richiede al massimo `5 pp`.

La decomposizione usa le 81 classi e le relative masse fisiche `6/4/12`. Per ogni classe costruisce
un flusso deterministico dalla probabilità in eccesso del seed 1 a quella in eccesso del seed 2.
La somma dei flussi ricostruisce esattamente la TV. Il pairing non è unico; serve a indicare quali
azioni scambiano massa, non a definire una distanza matematica diversa.

I gap EV provengono dalla diagnostica accoppiata degli action advantage durante il training. Le SE
sono marginali e descrittive: i campioni sono serialmente dipendenti e la misura non è un intervallo
di confidenza formale.

## Sintesi

| Metrica | Strategia media | Policy corrente |
| --- | ---: | ---: |
| TV fra seed | 13,0159 pp | 22,7363 pp |
| TV con gap medio ≤0,1a | 12,2863 pp | 21,7168 pp |
| TV con gap medio ≥0,5a | 0,0000 pp | 0,0000 pp |
| TV entro due SE marginali | 7,5046 pp | 14,1303 pp |
| Massa TV × gap EV | 0,00552a | 0,00955a |

Differenza corrente meno media: `9,7204 pp`.

La policy corrente è più instabile della media. L'averaging ritardato ridurrebbe la finestra di mediazione e non è il primo intervento da promuovere. Il prossimo test deve ridurre la varianza dei continuation value e migliorare la copertura dei rami.

## Trasferimenti aggregati — strategia media

| Da seed 1 | A seed 2 | Contributo TV | Gap EV medio | Massa gap EV |
| --- | --- | ---: | ---: | ---: |
| raise_10 | all_in | 1,366 pp | 0,037a | 0,0005a |
| raise_6 | all_in | 1,335 pp | 0,032a | 0,0004a |
| raise_6 | call | 1,312 pp | 0,049a | 0,0006a |
| call | fold | 1,271 pp | 0,017a | 0,0002a |
| fold | call | 1,199 pp | 0,052a | 0,0006a |
| all_in | raise_6 | 1,183 pp | 0,031a | 0,0004a |
| call | raise_6 | 1,083 pp | 0,077a | 0,0008a |
| all_in | call | 0,970 pp | 0,045a | 0,0004a |
| all_in | raise_10 | 0,779 pp | 0,052a | 0,0004a |
| raise_10 | call | 0,707 pp | 0,054a | 0,0004a |
| call | all_in | 0,648 pp | 0,048a | 0,0003a |
| raise_6 | raise_10 | 0,486 pp | 0,029a | 0,0001a |
| call | raise_10 | 0,317 pp | 0,038a | 0,0001a |
| raise_10 | raise_6 | 0,275 pp | 0,024a | 0,0001a |
| fold | raise_10 | 0,053 pp | 0,015a | 0,0000a |
| raise_6 | fold | 0,027 pp | 0,026a | 0,0000a |
| fold | raise_6 | 0,004 pp | 0,098a | 0,0000a |
| raise_10 | fold | 0,000 pp | 0,260a | 0,0000a |
| all_in | fold | 0,000 pp | 0,998a | 0,0000a |
| fold | all_in | 0,000 pp | 0,786a | 0,0000a |

## Contributo per famiglia

| Famiglia | Strategia media | Policy corrente |
| --- | ---: | ---: |
| Coppie | 1,6783 pp | 3,2734 pp |
| Suited | 4,5636 pp | 9,1617 pp |
| Offsuit | 6,7740 pp | 10,3012 pp |

## Tutte le 81 classi — strategia media

Il nodo root ha reach pubblica `100%`. La colonna reach mostra la frequenza dell'azione sorgente
nel seed 1 e dell'azione destinazione nel seed 2 per lo spostamento principale.

| Combo | Massa | TV | Contributo | Spostamento principale | Massa spostata | Gap EV | Reach azione S1 → S2 | TV gap ≤0,1a | TV entro 2 SE |
| --- | ---: | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| ATo | 12 | 40,39 pp | 0,769 pp | raise_10→all_in | 26,85 pp | 0,029a | 28,5% → 87,9% | 100,0% | 33,5% |
| J8o | 12 | 39,84 pp | 0,759 pp | call→fold | 39,84 pp | 0,010a | 71,5% → 68,3% | 100,0% | 100,0% |
| T9o | 12 | 34,12 pp | 0,650 pp | call→all_in | 21,32 pp | 0,028a | 38,4% → 57,4% | 100,0% | 62,5% |
| 98o | 12 | 32,38 pp | 0,617 pp | raise_10→call | 15,60 pp | 0,026a | 20,2% → 40,2% | 100,0% | 100,0% |
| Q9o | 12 | 28,57 pp | 0,544 pp | raise_6→call | 28,57 pp | 0,033a | 38,8% → 89,7% | 100,0% | 100,0% |
| 77 | 6 | 55,10 pp | 0,525 pp | fold→call | 55,10 pp | 0,087a | 55,2% → 99,9% | 100,0% | 0,0% |
| KTo | 12 | 25,58 pp | 0,487 pp | all_in→call | 23,10 pp | 0,041a | 99,3% → 23,3% | 100,0% | 90,3% |
| A7s | 4 | 73,10 pp | 0,464 pp | all_in→raise_10 | 69,67 pp | 0,065a | 96,6% → 71,8% | 95,3% | 0,0% |
| AKo | 12 | 23,91 pp | 0,455 pp | all_in→raise_6 | 22,53 pp | 0,035a | 86,6% → 27,7% | 99,7% | 5,5% |
| KJo | 12 | 22,13 pp | 0,422 pp | raise_10→all_in | 12,29 pp | 0,031a | 14,8% → 97,5% | 93,3% | 0,0% |
| 66 | 6 | 41,64 pp | 0,397 pp | call→fold | 41,64 pp | 0,022a | 62,5% → 79,1% | 100,0% | 100,0% |
| Q8o | 12 | 20,31 pp | 0,387 pp | fold→call | 20,31 pp | 0,025a | 78,6% → 41,7% | 100,0% | 100,0% |
| JJ | 6 | 40,52 pp | 0,386 pp | raise_6→raise_10 | 25,24 pp | 0,051a | 71,1% → 33,8% | 100,0% | 37,7% |
| A6s | 4 | 60,11 pp | 0,382 pp | raise_10→all_in | 44,13 pp | 0,070a | 45,3% → 98,5% | 73,6% | 0,2% |
| QJo | 12 | 19,71 pp | 0,375 pp | raise_6→all_in | 10,22 pp | 0,017a | 21,6% → 20,6% | 100,0% | 98,9% |
| QTs | 4 | 56,62 pp | 0,360 pp | all_in→call | 45,37 pp | 0,039a | 66,0% → 54,6% | 100,0% | 92,5% |
| 98s | 4 | 54,98 pp | 0,349 pp | all_in→raise_6 | 39,90 pp | 0,018a | 42,3% → 80,7% | 98,8% | 98,8% |
| K6s | 4 | 47,51 pp | 0,302 pp | raise_6→call | 47,29 pp | 0,092a | 47,3% → 100,0% | 100,0% | 0,0% |
| QJs | 4 | 45,36 pp | 0,288 pp | call→raise_6 | 45,35 pp | 0,148a | 99,9% → 45,4% | 0,0% | 0,0% |
| T8s | 4 | 42,64 pp | 0,271 pp | raise_10→call | 21,65 pp | 0,091a | 27,3% → 85,8% | 100,0% | 49,2% |
| AQo | 12 | 11,78 pp | 0,224 pp | all_in→raise_10 | 9,78 pp | 0,040a | 71,3% → 18,5% | 100,0% | 17,0% |
| A8s | 4 | 34,90 pp | 0,222 pp | all_in→raise_6 | 27,47 pp | 0,030a | 55,4% → 62,9% | 89,3% | 89,3% |
| KQs | 4 | 33,10 pp | 0,210 pp | raise_6→all_in | 15,19 pp | 0,004a | 33,1% → 22,4% | 100,0% | 100,0% |
| K8s | 4 | 30,53 pp | 0,194 pp | call→raise_6 | 30,52 pp | 0,019a | 96,0% → 34,5% | 100,0% | 100,0% |
| QTo | 12 | 10,16 pp | 0,194 pp | call→raise_10 | 9,11 pp | 0,052a | 25,9% → 9,1% | 100,0% | 8,4% |
| A9o | 12 | 9,16 pp | 0,175 pp | raise_6→all_in | 8,62 pp | 0,053a | 14,7% → 93,7% | 99,9% | 0,0% |
| A9s | 4 | 26,78 pp | 0,170 pp | all_in→raise_6 | 26,13 pp | 0,036a | 30,4% → 94,7% | 99,4% | 99,4% |
| T9s | 4 | 25,69 pp | 0,163 pp | call→raise_6 | 18,04 pp | 0,064a | 22,2% → 45,5% | 100,0% | 84,8% |
| AQs | 4 | 25,33 pp | 0,161 pp | raise_6→all_in | 21,25 pp | 0,022a | 59,2% → 37,5% | 100,0% | 100,0% |
| KTs | 4 | 24,33 pp | 0,155 pp | raise_10→call | 17,72 pp | 0,121a | 26,4% → 66,3% | 27,2% | 9,0% |
| AJs | 4 | 23,30 pp | 0,148 pp | raise_6→all_in | 22,04 pp | 0,075a | 28,6% → 89,7% | 98,0% | 3,3% |
| J7s | 4 | 22,64 pp | 0,144 pp | fold→call | 22,64 pp | 0,038a | 73,2% → 49,4% | 100,0% | 100,0% |
| A6o | 12 | 7,50 pp | 0,143 pp | call→raise_6 | 7,36 pp | 0,073a | 8,2% → 99,1% | 100,0% | 0,0% |
| KK | 6 | 14,72 pp | 0,140 pp | call→raise_6 | 8,27 pp | 0,063a | 21,3% → 82,0% | 100,0% | 100,0% |
| AKs | 4 | 18,54 pp | 0,118 pp | raise_6→call | 15,03 pp | 0,014a | 50,7% → 55,4% | 100,0% | 92,2% |
| KQo | 12 | 5,83 pp | 0,111 pp | raise_6→raise_10 | 5,55 pp | 0,000a | 12,9% → 6,8% | 96,6% | 95,3% |
| TT | 6 | 11,38 pp | 0,108 pp | all_in→call | 7,17 pp | 0,035a | 23,2% → 55,1% | 98,5% | 98,5% |
| 97s | 4 | 16,91 pp | 0,107 pp | fold→raise_10 | 8,29 pp | 0,015a | 22,1% → 21,4% | 100,0% | 100,0% |
| A8o | 12 | 5,45 pp | 0,104 pp | call→all_in | 2,88 pp | 0,046a | 9,2% → 93,2% | 100,0% | 47,0% |
| T8o | 12 | 5,10 pp | 0,097 pp | call→raise_6 | 5,09 pp | 0,045a | 96,7% → 8,4% | 100,0% | 0,2% |
| QQ | 6 | 9,86 pp | 0,094 pp | raise_10→raise_6 | 5,02 pp | 0,045a | 33,0% → 22,7% | 100,0% | 100,0% |
| A7o | 12 | 4,47 pp | 0,085 pp | raise_10→call | 2,83 pp | 0,017a | 2,8% → 3,6% | 100,0% | 63,2% |
| K9o | 12 | 4,45 pp | 0,085 pp | call→raise_6 | 4,45 pp | 0,023a | 98,7% → 5,7% | 100,0% | 100,0% |
| KJs | 4 | 12,15 pp | 0,077 pp | raise_10→raise_6 | 8,03 pp | 0,001a | 18,2% → 14,8% | 100,0% | 100,0% |
| J9s | 4 | 11,84 pp | 0,075 pp | raise_6→call | 11,26 pp | 0,108a | 11,9% → 99,3% | 0,0% | 0,0% |
| Q6s | 4 | 10,81 pp | 0,069 pp | call→fold | 10,35 pp | 0,034a | 72,4% → 37,9% | 95,7% | 95,7% |
| J8s | 4 | 8,03 pp | 0,051 pp | call→raise_6 | 7,69 pp | 0,081a | 99,5% → 8,1% | 95,8% | 0,0% |
| JTo | 12 | 2,13 pp | 0,041 pp | raise_6→all_in | 1,37 pp | 0,003a | 2,4% → 2,1% | 100,0% | 64,2% |
| T7s | 4 | 6,20 pp | 0,039 pp | call→fold | 5,88 pp | 0,051a | 96,8% → 9,0% | 94,8% | 0,0% |
| K8o | 12 | 2,02 pp | 0,039 pp | raise_6→fold | 1,42 pp | 0,025a | 2,1% → 97,7% | 100,0% | 100,0% |
| 99 | 6 | 2,98 pp | 0,028 pp | raise_6→call | 2,98 pp | 0,123a | 3,0% → 100,0% | 0,0% | 0,0% |
| ATs | 4 | 2,86 pp | 0,018 pp | raise_6→all_in | 1,50 pp | 0,081a | 58,3% → 39,3% | 75,9% | 0,0% |
| K7s | 4 | 1,32 pp | 0,008 pp | raise_6→call | 1,23 pp | 0,115a | 1,3% → 99,9% | 0,0% | 0,0% |
| AJo | 12 | 0,35 pp | 0,007 pp | all_in→raise_10 | 0,31 pp | 0,080a | 99,9% → 0,4% | 99,2% | 0,0% |
| Q7s | 4 | 1,00 pp | 0,006 pp | call→fold | 1,00 pp | 0,063a | 13,2% → 87,8% | 100,0% | 0,0% |
| Q9s | 4 | 0,92 pp | 0,006 pp | call→all_in | 0,62 pp | 0,191a | 99,7% → 0,8% | 0,0% | 0,0% |
| T6s | 4 | 0,80 pp | 0,005 pp | call→fold | 0,62 pp | 0,132a | 0,8% → 99,8% | 22,7% | 0,0% |
| K7o | 12 | 0,12 pp | 0,002 pp | fold→call | 0,12 pp | 0,095a | 99,9% → 0,2% | 100,0% | 0,0% |
| 96s | 4 | 0,25 pp | 0,002 pp | fold→call | 0,25 pp | 0,140a | 100,0% → 0,3% | 0,0% | 0,0% |
| 97o | 12 | 0,08 pp | 0,001 pp | call→fold | 0,08 pp | 0,187a | 0,1% → 100,0% | 0,0% | 0,0% |
| Q7o | 12 | 0,03 pp | 0,001 pp | call→fold | 0,03 pp | 0,281a | 0,0% → 100,0% | 0,0% | 0,0% |
| 86s | 4 | 0,06 pp | 0,000 pp | call→fold | 0,05 pp | 0,305a | 0,1% → 100,0% | 0,0% | 0,0% |
| K9s | 4 | 0,05 pp | 0,000 pp | raise_10→call | 0,04 pp | 0,246a | 0,0% → 100,0% | 0,0% | 0,0% |
| T7o | 12 | 0,02 pp | 0,000 pp | fold→raise_6 | 0,01 pp | 0,266a | 100,0% → 0,0% | 0,0% | 0,0% |
| J9o | 12 | 0,01 pp | 0,000 pp | call→raise_6 | 0,01 pp | 0,055a | 100,0% → 0,0% | 99,7% | 0,0% |
| Q8s | 4 | 0,04 pp | 0,000 pp | call→raise_6 | 0,03 pp | 0,092a | 99,8% → 0,3% | 82,6% | 0,0% |
| AA | 6 | 0,02 pp | 0,000 pp | call→raise_6 | 0,02 pp | 0,218a | 100,0% → 0,0% | 0,0% | 0,0% |
| J6s | 4 | 0,03 pp | 0,000 pp | call→fold | 0,03 pp | 0,159a | 0,1% → 100,0% | 0,0% | 0,0% |
| 87o | 12 | 0,01 pp | 0,000 pp | call→fold | 0,01 pp | 0,352a | 0,0% → 100,0% | 0,0% | 0,0% |
| 87s | 4 | 0,02 pp | 0,000 pp | fold→call | 0,02 pp | 0,166a | 100,0% → 0,0% | 0,1% | 0,1% |
| K6o | 12 | 0,01 pp | 0,000 pp | fold→call | 0,01 pp | 0,171a | 100,0% → 0,0% | 0,0% | 0,0% |
| 76s | 4 | 0,01 pp | 0,000 pp | call→fold | 0,01 pp | 0,346a | 0,0% → 100,0% | 0,0% | 0,0% |
| Q6o | 12 | 0,00 pp | 0,000 pp | fold→call | 0,00 pp | 0,289a | 100,0% → 0,0% | 0,0% | 0,0% |
| JTs | 4 | 0,01 pp | 0,000 pp | raise_10→call | 0,01 pp | 0,287a | 0,0% → 100,0% | 0,0% | 0,0% |
| J7o | 12 | 0,00 pp | 0,000 pp | call→fold | 0,00 pp | 0,236a | 0,0% → 100,0% | 0,0% | 0,0% |
| 96o | 12 | 0,00 pp | 0,000 pp | call→fold | 0,00 pp | 0,369a | 0,0% → 100,0% | 0,1% | 0,0% |
| 88 | 6 | 0,00 pp | 0,000 pp | raise_6→call | 0,00 pp | 0,359a | 0,0% → 100,0% | 0,0% | 0,0% |
| T6o | 12 | 0,00 pp | 0,000 pp | fold→call | 0,00 pp | 0,392a | 100,0% → 0,0% | 0,0% | 0,0% |
| J6o | 12 | 0,00 pp | 0,000 pp | fold→call | 0,00 pp | 0,411a | 100,0% → 0,0% | 0,2% | 0,0% |
| 86o | 12 | 0,00 pp | 0,000 pp | call→fold | 0,00 pp | 0,502a | 0,0% → 100,0% | 0,2% | 0,0% |
| 76o | 12 | 0,00 pp | 0,000 pp | raise_6→fold | 0,00 pp | 0,699a | 0,0% → 100,0% | 0,0% | 0,0% |

## Tutte le 81 classi — policy corrente

| Combo | Massa | TV | Contributo | Spostamento principale | Massa spostata | Gap EV | Reach azione S1 → S2 | TV gap ≤0,1a | TV entro 2 SE |
| --- | ---: | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| T9o | 12 | 88,83 pp | 1,692 pp | call→all_in | 55,53 pp | 0,028a | 88,8% → 55,5% | 100,0% | 100,0% |
| KJo | 12 | 79,75 pp | 1,519 pp | raise_10→all_in | 79,75 pp | 0,031a | 79,7% → 100,0% | 100,0% | 0,0% |
| QTo | 12 | 60,91 pp | 1,160 pp | all_in→call | 60,91 pp | 0,021a | 100,0% → 60,9% | 100,0% | 100,0% |
| A9o | 12 | 59,10 pp | 1,126 pp | raise_6→all_in | 59,10 pp | 0,053a | 59,1% → 100,0% | 100,0% | 0,0% |
| Q9o | 12 | 52,81 pp | 1,006 pp | raise_6→call | 52,81 pp | 0,033a | 52,8% → 100,0% | 100,0% | 100,0% |
| Q8o | 12 | 52,54 pp | 1,001 pp | fold→call | 52,54 pp | 0,025a | 100,0% → 52,5% | 100,0% | 100,0% |
| 77 | 6 | 100,00 pp | 0,952 pp | fold→call | 100,00 pp | 0,087a | 100,0% → 100,0% | 100,0% | 0,0% |
| JJ | 6 | 74,45 pp | 0,709 pp | all_in→raise_6 | 38,12 pp | 0,065a | 46,1% → 63,7% | 100,0% | 48,8% |
| QJo | 12 | 36,48 pp | 0,695 pp | raise_10→all_in | 12,94 pp | 0,003a | 39,1% → 36,5% | 100,0% | 67,1% |
| QQ | 6 | 72,63 pp | 0,692 pp | all_in→raise_10 | 42,14 pp | 0,021a | 100,0% → 42,1% | 100,0% | 100,0% |
| A7s | 4 | 100,00 pp | 0,635 pp | all_in→raise_10 | 100,00 pp | 0,065a | 100,0% → 100,0% | 100,0% | 0,0% |
| AJs | 4 | 100,00 pp | 0,635 pp | raise_6→raise_10 | 79,23 pp | 0,041a | 79,2% → 100,0% | 100,0% | 100,0% |
| J7s | 4 | 100,00 pp | 0,635 pp | fold→call | 100,00 pp | 0,038a | 100,0% → 100,0% | 100,0% | 100,0% |
| K6s | 4 | 100,00 pp | 0,635 pp | raise_6→call | 100,00 pp | 0,092a | 100,0% → 100,0% | 100,0% | 0,0% |
| KQs | 4 | 100,00 pp | 0,635 pp | call→raise_10 | 38,56 pp | 0,002a | 100,0% → 38,6% | 100,0% | 100,0% |
| QJs | 4 | 100,00 pp | 0,635 pp | call→raise_6 | 100,00 pp | 0,148a | 100,0% → 100,0% | 0,0% | 0,0% |
| AQo | 12 | 32,93 pp | 0,627 pp | all_in→raise_6 | 22,80 pp | 0,055a | 32,9% → 54,4% | 100,0% | 0,0% |
| 98o | 12 | 30,11 pp | 0,574 pp | fold→call | 30,11 pp | 0,011a | 100,0% → 30,1% | 100,0% | 100,0% |
| TT | 6 | 56,51 pp | 0,538 pp | call→raise_6 | 56,51 pp | 0,041a | 56,5% → 100,0% | 100,0% | 100,0% |
| 97s | 4 | 79,83 pp | 0,507 pp | fold→call | 52,75 pp | 0,056a | 52,8% → 100,0% | 100,0% | 33,9% |
| A6s | 4 | 76,91 pp | 0,488 pp | raise_10→all_in | 75,97 pp | 0,070a | 76,0% → 100,0% | 98,8% | 0,0% |
| ATo | 12 | 25,07 pp | 0,477 pp | raise_6→all_in | 23,01 pp | 0,014a | 25,1% → 73,1% | 100,0% | 100,0% |
| AQs | 4 | 71,05 pp | 0,451 pp | raise_6→raise_10 | 37,11 pp | 0,003a | 71,1% → 66,1% | 100,0% | 100,0% |
| Q6s | 4 | 66,03 pp | 0,419 pp | call→fold | 66,03 pp | 0,034a | 93,2% → 72,9% | 100,0% | 100,0% |
| AKo | 12 | 21,98 pp | 0,419 pp | all_in→raise_10 | 21,98 pp | 0,001a | 100,0% → 22,0% | 100,0% | 100,0% |
| 98s | 4 | 62,97 pp | 0,400 pp | all_in→raise_6 | 39,26 pp | 0,018a | 39,3% → 100,0% | 100,0% | 100,0% |
| QTs | 4 | 60,79 pp | 0,386 pp | raise_10→call | 22,31 pp | 0,006a | 22,3% → 56,0% | 100,0% | 64,7% |
| KJs | 4 | 59,71 pp | 0,379 pp | call→all_in | 59,71 pp | 0,066a | 100,0% → 59,7% | 100,0% | 100,0% |
| T7s | 4 | 57,25 pp | 0,364 pp | call→fold | 57,25 pp | 0,051a | 100,0% → 57,3% | 100,0% | 0,0% |
| K8s | 4 | 55,30 pp | 0,351 pp | call→raise_6 | 55,30 pp | 0,019a | 100,0% → 55,3% | 100,0% | 100,0% |
| KTs | 4 | 53,30 pp | 0,338 pp | raise_10→call | 34,54 pp | 0,121a | 68,3% → 66,2% | 35,2% | 0,0% |
| ATs | 4 | 48,96 pp | 0,311 pp | call→raise_10 | 25,08 pp | 0,202a | 25,1% → 49,0% | 48,8% | 48,7% |
| AKs | 4 | 43,76 pp | 0,278 pp | raise_6→call | 43,76 pp | 0,014a | 90,6% → 53,2% | 100,0% | 100,0% |
| T8s | 4 | 39,23 pp | 0,249 pp | call→all_in | 31,87 pp | 0,039a | 68,0% → 63,9% | 100,0% | 81,2% |
| KK | 6 | 21,79 pp | 0,207 pp | raise_6→call | 21,79 pp | 0,063a | 70,3% → 51,5% | 100,0% | 100,0% |
| A9s | 4 | 29,51 pp | 0,187 pp | all_in→raise_6 | 29,51 pp | 0,036a | 29,5% → 100,0% | 100,0% | 100,0% |
| 66 | 6 | 18,33 pp | 0,175 pp | fold→call | 18,33 pp | 0,022a | 51,0% → 67,3% | 100,0% | 100,0% |
| A8s | 4 | 21,74 pp | 0,138 pp | all_in→raise_6 | 18,91 pp | 0,030a | 21,7% → 62,9% | 100,0% | 87,0% |
| T9s | 4 | 16,62 pp | 0,106 pp | all_in→raise_6 | 16,62 pp | 0,006a | 53,3% → 63,3% | 100,0% | 100,0% |
| J8o | 12 | 0,32 pp | 0,006 pp | call→fold | 0,32 pp | 0,010a | 0,3% → 100,0% | 100,0% | 100,0% |
| 76o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| 76s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| 86o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| 86s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| 87o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| 87s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| 88 | 6 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| 96o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| 96s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| 97o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| 99 | 6 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| A6o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| A7o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| A8o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| AA | 6 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| AJo | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| J6o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| J6s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| J7o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| J8s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| J9o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| J9s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| JTo | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| JTs | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| K6o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| K7o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| K7s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| K8o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| K9o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| K9s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| KQo | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| KTo | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| Q6o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| Q7o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| Q7s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| Q8s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| Q9s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| T6o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| T6s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| T7o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| T8o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |

## Contratto e limiti

- entrambi gli input hanno 2.000.000 di iterazioni e lo stesso fingerprint dell'albero;
- la TV usa la strategia media realization-weighted, come il gate esistente;
- la policy corrente è riportata come diagnostica di convergenza, non come soluzione pubblicabile;
- i gap non usano Monker e non modificano training, bucket, frequenze o payoff;
- `Massa TV × gap EV` è una misura descrittiva del trasporto scelto, non exploitability.

## Decisione

La policy corrente è più instabile della media. L'averaging ritardato ridurrebbe la finestra di mediazione e non è il primo intervento da promuovere. Il prossimo test deve ridurre la varianza dei continuation value e migliorare la copertura dei rami.

Il prossimo esperimento deve essere pre-registrato contro questa decomposizione. Un candidato passa
soltanto se riduce la TV media verso `5 pp`, non peggiora la WMAE e conserva gli audit monetari e
Call/Fold di V18.
