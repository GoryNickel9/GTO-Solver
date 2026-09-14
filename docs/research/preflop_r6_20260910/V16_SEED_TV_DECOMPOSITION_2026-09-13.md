# R6 — Decomposizione della TV fra i seed V13

Data: 2026-09-12  
Esito: `DIAGNOSTIC_PASS / V13_SEED_TV_ABOVE_5PP`

## Risultato

La TV root fra i due seed V13 è `13,0557 pp`
per la strategia media e `12,9262 pp`
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
| TV fra seed | 13,0557 pp | 12,9262 pp |
| TV con gap medio ≤0,1a | 12,4934 pp | 12,9262 pp |
| TV con gap medio ≥0,5a | 0,0001 pp | 0,0000 pp |
| TV entro due SE marginali | 7,1656 pp | 11,1754 pp |
| Massa TV × gap EV | 0,00516a | 0,00297a |

Differenza corrente meno media: `-0,1296 pp`.

La policy corrente è più stabile della media. Un averaging ritardato è il primo challenger coerente, perché può rimuovere massa storica senza cambiare i regret.

## Trasferimenti aggregati — strategia media

| Da seed 1 | A seed 2 | Contributo TV | Gap EV medio | Massa gap EV |
| --- | --- | ---: | ---: | ---: |
| call | fold | 1,720 pp | 0,038a | 0,0007a |
| raise_6 | all_in | 1,339 pp | 0,039a | 0,0005a |
| fold | call | 1,297 pp | 0,032a | 0,0004a |
| raise_10 | call | 1,164 pp | 0,039a | 0,0005a |
| call | raise_6 | 1,128 pp | 0,056a | 0,0006a |
| raise_10 | all_in | 1,077 pp | 0,020a | 0,0002a |
| call | all_in | 0,888 pp | 0,051a | 0,0004a |
| all_in | raise_6 | 0,858 pp | 0,042a | 0,0004a |
| raise_6 | call | 0,820 pp | 0,052a | 0,0004a |
| call | raise_10 | 0,575 pp | 0,056a | 0,0003a |
| all_in | raise_10 | 0,563 pp | 0,024a | 0,0001a |
| all_in | call | 0,426 pp | 0,049a | 0,0002a |
| raise_6 | raise_10 | 0,416 pp | 0,008a | 0,0000a |
| raise_10 | raise_6 | 0,404 pp | 0,034a | 0,0001a |
| raise_6 | fold | 0,369 pp | 0,042a | 0,0002a |
| fold | raise_6 | 0,011 pp | 0,286a | 0,0000a |
| raise_10 | fold | 0,000 pp | 0,416a | 0,0000a |
| fold | raise_10 | 0,000 pp | 0,449a | 0,0000a |
| fold | all_in | 0,000 pp | 1,232a | 0,0000a |
| all_in | fold | 0,000 pp | 0,600a | 0,0000a |

## Contributo per famiglia

| Famiglia | Strategia media | Policy corrente |
| --- | ---: | ---: |
| Coppie | 0,9648 pp | 1,1193 pp |
| Suited | 4,7838 pp | 5,3603 pp |
| Offsuit | 7,3071 pp | 6,4465 pp |

## Tutte le 81 classi — strategia media

Il nodo root ha reach pubblica `100%`. La colonna reach mostra la frequenza dell'azione sorgente
nel seed 1 e dell'azione destinazione nel seed 2 per lo spostamento principale.

| Combo | Massa | TV | Contributo | Spostamento principale | Massa spostata | Gap EV | Reach azione S1 → S2 | TV gap ≤0,1a | TV entro 2 SE |
| --- | ---: | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| AKo | 12 | 45,27 pp | 0,862 pp | raise_10→all_in | 27,36 pp | 0,009a | 28,4% → 84,5% | 99,6% | 99,6% |
| K8o | 12 | 41,62 pp | 0,793 pp | call→fold | 25,30 pp | 0,032a | 28,2% → 93,7% | 100,0% | 0,0% |
| 98o | 12 | 36,38 pp | 0,693 pp | fold→call | 27,82 pp | 0,034a | 63,1% → 64,6% | 100,0% | 23,5% |
| QJo | 12 | 34,71 pp | 0,661 pp | raise_10→call | 34,49 pp | 0,018a | 36,0% → 98,5% | 100,0% | 99,8% |
| T8s | 4 | 90,49 pp | 0,575 pp | raise_10→call | 50,42 pp | 0,084a | 52,7% → 97,1% | 100,0% | 44,3% |
| KTo | 12 | 26,80 pp | 0,510 pp | raise_6→all_in | 14,45 pp | 0,067a | 14,9% → 96,8% | 100,0% | 0,0% |
| T9o | 12 | 25,86 pp | 0,493 pp | call→raise_10 | 10,73 pp | 0,019a | 36,7% → 19,1% | 100,0% | 70,9% |
| 66 | 6 | 50,18 pp | 0,478 pp | call→fold | 50,18 pp | 0,044a | 95,4% → 54,8% | 100,0% | 0,0% |
| KQs | 4 | 74,51 pp | 0,473 pp | raise_6→call | 55,87 pp | 0,029a | 83,3% → 68,9% | 100,0% | 100,0% |
| J7s | 4 | 71,15 pp | 0,452 pp | fold→call | 69,05 pp | 0,026a | 91,8% → 77,2% | 100,0% | 97,1% |
| 98s | 4 | 70,15 pp | 0,445 pp | all_in→raise_6 | 35,96 pp | 0,013a | 47,3% → 72,4% | 100,0% | 100,0% |
| AQo | 12 | 19,02 pp | 0,362 pp | raise_10→all_in | 9,79 pp | 0,018a | 13,7% → 94,0% | 99,9% | 51,5% |
| J8o | 12 | 18,82 pp | 0,359 pp | call→fold | 18,82 pp | 0,012a | 86,4% → 32,4% | 100,0% | 100,0% |
| K9o | 12 | 18,59 pp | 0,354 pp | call→raise_6 | 18,59 pp | 0,024a | 100,0% → 18,6% | 100,0% | 100,0% |
| A8o | 12 | 18,19 pp | 0,347 pp | call→all_in | 14,22 pp | 0,038a | 33,3% → 80,4% | 100,0% | 21,8% |
| T9s | 4 | 53,29 pp | 0,338 pp | call→raise_6 | 31,61 pp | 0,041a | 33,3% → 77,0% | 100,0% | 68,0% |
| KJs | 4 | 48,01 pp | 0,305 pp | call→raise_6 | 23,41 pp | 0,146a | 78,7% → 23,6% | 28,3% | 0,0% |
| JTo | 12 | 15,44 pp | 0,294 pp | raise_6→all_in | 8,08 pp | 0,024a | 14,1% → 8,1% | 91,5% | 52,3% |
| A6s | 4 | 40,98 pp | 0,260 pp | call→all_in | 31,56 pp | 0,029a | 39,5% → 90,4% | 100,0% | 94,2% |
| KQo | 12 | 13,61 pp | 0,259 pp | all_in→raise_6 | 9,16 pp | 0,028a | 97,0% → 9,2% | 99,1% | 99,1% |
| AQs | 4 | 36,66 pp | 0,233 pp | raise_6→raise_10 | 17,21 pp | 0,004a | 19,0% → 46,7% | 100,0% | 100,0% |
| QQ | 6 | 24,03 pp | 0,229 pp | raise_10→all_in | 15,86 pp | 0,043a | 25,2% → 42,7% | 100,0% | 34,0% |
| Q7s | 4 | 34,19 pp | 0,217 pp | call→fold | 34,17 pp | 0,059a | 50,8% → 83,4% | 100,0% | 0,1% |
| Q6s | 4 | 31,90 pp | 0,203 pp | fold→call | 31,90 pp | 0,013a | 81,6% → 50,3% | 100,0% | 100,0% |
| KTs | 4 | 31,67 pp | 0,201 pp | call→all_in | 18,48 pp | 0,055a | 49,7% → 55,3% | 100,0% | 100,0% |
| KJo | 12 | 10,37 pp | 0,197 pp | all_in→raise_10 | 8,98 pp | 0,005a | 92,7% → 16,1% | 87,8% | 86,7% |
| A7o | 12 | 10,31 pp | 0,196 pp | all_in→call | 6,05 pp | 0,047a | 96,0% → 7,0% | 100,0% | 0,0% |
| QTo | 12 | 9,46 pp | 0,180 pp | call→all_in | 7,22 pp | 0,048a | 17,6% → 89,2% | 93,2% | 0,0% |
| ATs | 4 | 28,19 pp | 0,179 pp | all_in→raise_6 | 24,36 pp | 0,047a | 62,1% → 61,6% | 99,2% | 99,2% |
| J9o | 12 | 8,48 pp | 0,162 pp | raise_6→call | 8,48 pp | 0,058a | 8,5% → 100,0% | 100,0% | 0,0% |
| AKs | 4 | 24,02 pp | 0,153 pp | call→raise_6 | 15,67 pp | 0,035a | 45,3% → 62,8% | 100,0% | 65,6% |
| T8o | 12 | 7,49 pp | 0,143 pp | call→raise_6 | 7,49 pp | 0,061a | 100,0% → 7,5% | 100,0% | 0,0% |
| AJs | 4 | 21,89 pp | 0,139 pp | all_in→raise_6 | 17,76 pp | 0,078a | 93,8% → 19,1% | 98,6% | 0,0% |
| QTs | 4 | 21,04 pp | 0,134 pp | call→raise_10 | 16,77 pp | 0,090a | 44,6% → 17,0% | 91,7% | 12,0% |
| JJ | 6 | 13,55 pp | 0,129 pp | all_in→raise_10 | 8,92 pp | 0,020a | 13,6% → 11,4% | 100,0% | 100,0% |
| A8s | 4 | 19,60 pp | 0,124 pp | raise_6→all_in | 12,89 pp | 0,031a | 13,4% → 75,5% | 81,4% | 81,4% |
| Q8o | 12 | 5,44 pp | 0,104 pp | call→fold | 5,44 pp | 0,043a | 16,9% → 88,5% | 100,0% | 0,0% |
| KK | 6 | 10,31 pp | 0,098 pp | call→raise_6 | 7,86 pp | 0,110a | 29,1% → 73,7% | 23,8% | 23,8% |
| ATo | 12 | 4,71 pp | 0,090 pp | raise_6→raise_10 | 2,45 pp | 0,004a | 6,2% → 10,8% | 100,0% | 52,1% |
| T7s | 4 | 11,40 pp | 0,072 pp | call→fold | 7,20 pp | 0,025a | 80,4% → 26,5% | 63,2% | 63,2% |
| A6o | 12 | 3,45 pp | 0,066 pp | fold→call | 2,79 pp | 0,016a | 2,8% → 5,7% | 99,2% | 80,9% |
| A7s | 4 | 9,99 pp | 0,063 pp | all_in→raise_10 | 6,54 pp | 0,094a | 95,3% → 10,7% | 100,0% | 34,6% |
| K7s | 4 | 9,38 pp | 0,060 pp | raise_6→call | 9,25 pp | 0,100a | 9,3% → 100,0% | 98,6% | 0,0% |
| K7o | 12 | 2,88 pp | 0,055 pp | fold→call | 2,88 pp | 0,091a | 100,0% → 2,9% | 100,0% | 0,0% |
| A9o | 12 | 2,50 pp | 0,048 pp | all_in→raise_6 | 2,14 pp | 0,049a | 98,7% → 2,6% | 99,0% | 0,0% |
| 97s | 4 | 5,97 pp | 0,038 pp | raise_6→fold | 4,74 pp | 0,011a | 14,8% → 6,1% | 100,0% | 94,1% |
| Q9o | 12 | 1,68 pp | 0,032 pp | raise_6→call | 1,66 pp | 0,047a | 1,7% → 100,0% | 98,6% | 0,0% |
| TT | 6 | 2,83 pp | 0,027 pp | raise_6→call | 1,42 pp | 0,063a | 21,1% → 80,3% | 75,0% | 50,3% |
| K6s | 4 | 3,19 pp | 0,020 pp | raise_6→call | 2,86 pp | 0,122a | 3,2% → 99,1% | 10,2% | 10,2% |
| QJs | 4 | 2,86 pp | 0,018 pp | raise_6→call | 1,74 pp | 0,163a | 1,7% → 99,9% | 0,0% | 0,0% |
| K6o | 12 | 0,93 pp | 0,018 pp | call→fold | 0,93 pp | 0,170a | 1,0% → 99,9% | 0,0% | 0,0% |
| J9s | 4 | 2,68 pp | 0,017 pp | call→all_in | 1,59 pp | 0,087a | 98,3% → 2,4% | 86,1% | 26,9% |
| A9s | 4 | 2,64 pp | 0,017 pp | raise_6→raise_10 | 1,53 pp | 0,091a | 57,3% → 1,6% | 85,4% | 27,4% |
| Q9s | 4 | 2,12 pp | 0,013 pp | call→raise_6 | 1,40 pp | 0,129a | 99,5% → 1,4% | 0,0% | 0,0% |
| 96s | 4 | 2,08 pp | 0,013 pp | fold→raise_6 | 1,80 pp | 0,287a | 100,0% → 1,8% | 0,0% | 0,0% |
| 97o | 12 | 0,67 pp | 0,013 pp | call→fold | 0,66 pp | 0,195a | 0,8% → 99,9% | 0,0% | 0,0% |
| T7o | 12 | 0,57 pp | 0,011 pp | fold→call | 0,56 pp | 0,199a | 99,9% → 0,7% | 0,0% | 0,0% |
| Q8s | 4 | 1,23 pp | 0,008 pp | call→raise_6 | 1,16 pp | 0,097a | 99,2% → 1,8% | 94,8% | 0,0% |
| AJo | 12 | 0,24 pp | 0,005 pp | raise_6→raise_10 | 0,15 pp | 0,010a | 0,3% → 0,7% | 62,8% | 62,8% |
| K9s | 4 | 0,62 pp | 0,004 pp | raise_6→call | 0,58 pp | 0,154a | 0,6% → 99,9% | 0,2% | 0,0% |
| J8s | 4 | 0,61 pp | 0,004 pp | raise_6→call | 0,39 pp | 0,090a | 1,0% → 99,4% | 63,8% | 0,0% |
| T6s | 4 | 0,53 pp | 0,003 pp | fold→call | 0,52 pp | 0,133a | 99,7% → 0,9% | 2,2% | 0,0% |
| Q7o | 12 | 0,08 pp | 0,002 pp | call→fold | 0,08 pp | 0,299a | 0,1% → 100,0% | 0,0% | 0,0% |
| AA | 6 | 0,15 pp | 0,001 pp | call→raise_6 | 0,15 pp | 0,121a | 99,7% → 0,4% | 0,0% | 0,0% |
| 77 | 6 | 0,13 pp | 0,001 pp | call→fold | 0,13 pp | 0,078a | 99,3% → 0,8% | 99,6% | 0,0% |
| 99 | 6 | 0,10 pp | 0,001 pp | raise_6→call | 0,10 pp | 0,271a | 0,1% → 100,0% | 0,0% | 0,0% |
| 86s | 4 | 0,11 pp | 0,001 pp | call→fold | 0,11 pp | 0,295a | 0,1% → 100,0% | 0,0% | 0,0% |
| JTs | 4 | 0,08 pp | 0,000 pp | call→raise_6 | 0,05 pp | 0,269a | 100,0% → 0,1% | 35,3% | 35,3% |
| K8s | 4 | 0,07 pp | 0,000 pp | call→fold | 0,04 pp | 0,223a | 100,0% → 0,0% | 44,5% | 0,0% |
| J6s | 4 | 0,07 pp | 0,000 pp | call→fold | 0,07 pp | 0,159a | 0,6% → 99,5% | 0,0% | 0,0% |
| 87o | 12 | 0,02 pp | 0,000 pp | fold→call | 0,02 pp | 0,365a | 100,0% → 0,0% | 0,0% | 0,0% |
| 87s | 4 | 0,05 pp | 0,000 pp | call→fold | 0,03 pp | 0,161a | 0,1% → 99,9% | 0,0% | 0,0% |
| 88 | 6 | 0,03 pp | 0,000 pp | call→fold | 0,03 pp | 0,225a | 100,0% → 0,0% | 0,1% | 0,0% |
| J7o | 12 | 0,01 pp | 0,000 pp | fold→call | 0,01 pp | 0,242a | 100,0% → 0,0% | 0,0% | 0,0% |
| T6o | 12 | 0,01 pp | 0,000 pp | call→fold | 0,01 pp | 0,389a | 0,0% → 100,0% | 0,2% | 0,2% |
| J6o | 12 | 0,01 pp | 0,000 pp | fold→call | 0,01 pp | 0,406a | 100,0% → 0,0% | 0,0% | 0,0% |
| 96o | 12 | 0,01 pp | 0,000 pp | call→fold | 0,01 pp | 0,376a | 0,0% → 100,0% | 0,0% | 0,0% |
| 76s | 4 | 0,01 pp | 0,000 pp | call→fold | 0,01 pp | 0,347a | 0,0% → 100,0% | 0,0% | 0,0% |
| Q6o | 12 | 0,00 pp | 0,000 pp | call→fold | 0,00 pp | 0,311a | 0,0% → 100,0% | 0,0% | 0,0% |
| 86o | 12 | 0,00 pp | 0,000 pp | call→fold | 0,00 pp | 0,508a | 0,0% → 100,0% | 0,2% | 0,0% |
| 76o | 12 | 0,00 pp | 0,000 pp | call→fold | 0,00 pp | 0,567a | 0,0% → 100,0% | 0,0% | 0,0% |

## Tutte le 81 classi — policy corrente

| Combo | Massa | TV | Contributo | Spostamento principale | Massa spostata | Gap EV | Reach azione S1 → S2 | TV gap ≤0,1a | TV entro 2 SE |
| --- | ---: | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| AKo | 12 | 100,00 pp | 1,905 pp | raise_10→all_in | 100,00 pp | 0,009a | 100,0% → 100,0% | 100,0% | 100,0% |
| J8o | 12 | 69,04 pp | 1,315 pp | call→fold | 69,04 pp | 0,012a | 100,0% → 69,0% | 100,0% | 100,0% |
| KJo | 12 | 53,67 pp | 1,022 pp | all_in→raise_10 | 53,67 pp | 0,005a | 53,7% → 100,0% | 100,0% | 100,0% |
| JJ | 6 | 71,15 pp | 0,678 pp | raise_6→call | 57,06 pp | 0,014a | 85,9% → 71,1% | 100,0% | 100,0% |
| QJo | 12 | 35,30 pp | 0,672 pp | raise_10→call | 35,30 pp | 0,018a | 35,3% → 100,0% | 100,0% | 100,0% |
| Q6s | 4 | 100,00 pp | 0,635 pp | fold→call | 100,00 pp | 0,013a | 100,0% → 100,0% | 100,0% | 100,0% |
| K8o | 12 | 32,30 pp | 0,615 pp | call→fold | 32,30 pp | 0,032a | 32,3% → 100,0% | 100,0% | 0,0% |
| AQo | 12 | 27,94 pp | 0,532 pp | raise_10→all_in | 27,94 pp | 0,018a | 27,9% → 100,0% | 100,0% | 100,0% |
| T8s | 4 | 78,94 pp | 0,501 pp | raise_10→call | 45,03 pp | 0,084a | 45,0% → 100,0% | 100,0% | 42,9% |
| A6s | 4 | 74,59 pp | 0,474 pp | call→all_in | 74,59 pp | 0,029a | 74,6% → 100,0% | 100,0% | 100,0% |
| AQs | 4 | 70,53 pp | 0,448 pp | all_in→raise_10 | 50,54 pp | 0,021a | 100,0% → 50,5% | 100,0% | 100,0% |
| T9s | 4 | 70,01 pp | 0,444 pp | call→all_in | 62,02 pp | 0,034a | 62,0% → 71,8% | 100,0% | 100,0% |
| 98s | 4 | 60,69 pp | 0,385 pp | all_in→raise_6 | 60,69 pp | 0,013a | 100,0% → 60,7% | 100,0% | 100,0% |
| T9o | 12 | 20,20 pp | 0,385 pp | raise_6→all_in | 20,20 pp | 0,050a | 20,2% → 100,0% | 100,0% | 0,0% |
| KQs | 4 | 56,74 pp | 0,360 pp | raise_6→raise_10 | 29,42 pp | 0,005a | 29,4% → 56,7% | 100,0% | 100,0% |
| J7s | 4 | 51,06 pp | 0,324 pp | fold→call | 51,06 pp | 0,026a | 100,0% → 51,1% | 100,0% | 100,0% |
| KK | 6 | 32,75 pp | 0,312 pp | raise_10→raise_6 | 32,75 pp | 0,051a | 32,7% → 100,0% | 100,0% | 0,0% |
| AKs | 4 | 48,31 pp | 0,307 pp | raise_10→call | 42,23 pp | 0,011a | 42,2% → 48,3% | 100,0% | 100,0% |
| KTs | 4 | 45,84 pp | 0,291 pp | call→all_in | 35,82 pp | 0,055a | 100,0% → 35,8% | 100,0% | 100,0% |
| QTs | 4 | 45,28 pp | 0,287 pp | call→all_in | 45,28 pp | 0,023a | 45,3% → 100,0% | 100,0% | 100,0% |
| A8s | 4 | 42,14 pp | 0,268 pp | raise_6→raise_10 | 23,47 pp | 0,026a | 23,5% → 65,8% | 100,0% | 100,0% |
| 97s | 4 | 36,46 pp | 0,231 pp | call→fold | 34,93 pp | 0,053a | 100,0% → 34,9% | 100,0% | 100,0% |
| ATs | 4 | 28,36 pp | 0,180 pp | raise_6→all_in | 28,36 pp | 0,047a | 100,0% → 28,4% | 100,0% | 100,0% |
| QQ | 6 | 13,63 pp | 0,130 pp | raise_6→all_in | 13,63 pp | 0,036a | 13,6% → 100,0% | 100,0% | 100,0% |
| KJs | 4 | 17,49 pp | 0,111 pp | call→raise_10 | 17,49 pp | 0,098a | 100,0% → 17,5% | 100,0% | 0,0% |
| A9s | 4 | 11,23 pp | 0,071 pp | raise_6→all_in | 11,23 pp | 0,012a | 37,1% → 74,2% | 100,0% | 100,0% |
| Q7s | 4 | 6,59 pp | 0,042 pp | call→fold | 6,59 pp | 0,059a | 6,6% → 100,0% | 100,0% | 0,0% |
| 66 | 6 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| 76o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| 76s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| 77 | 6 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| 86o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| 86s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| 87o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| 87s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| 88 | 6 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| 96o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| 96s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| 97o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| 98o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| 99 | 6 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| A6o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| A7o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| A7s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| A8o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| A9o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| AA | 6 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| AJo | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| AJs | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| ATo | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| J6o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| J6s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| J7o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| J8s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| J9o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| J9s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| JTo | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| JTs | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| K6o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| K6s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| K7o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| K7s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| K8s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| K9o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| K9s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| KQo | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| KTo | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| Q6o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| Q7o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| Q8o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| Q8s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| Q9o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| Q9s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| QJs | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| QTo | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| T6o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| T6s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| T7o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| T7s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| T8o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| TT | 6 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |

## Contratto e limiti

- entrambi gli input hanno 2.000.000 di iterazioni e lo stesso fingerprint dell'albero;
- la TV usa la strategia media realization-weighted, come il gate esistente;
- la policy corrente è riportata come diagnostica di convergenza, non come soluzione pubblicabile;
- i gap non usano Monker e non modificano training, bucket, frequenze o payoff;
- `Massa TV × gap EV` è una misura descrittiva del trasporto scelto, non exploitability.

## Decisione

La policy corrente è più stabile della media. Un averaging ritardato è il primo challenger coerente, perché può rimuovere massa storica senza cambiare i regret.

Il prossimo esperimento deve essere pre-registrato contro questa decomposizione. Un candidato passa
soltanto se riduce la TV media verso `5 pp`, non peggiora la WMAE e conserva gli audit monetari e
Call/Fold di V13.
