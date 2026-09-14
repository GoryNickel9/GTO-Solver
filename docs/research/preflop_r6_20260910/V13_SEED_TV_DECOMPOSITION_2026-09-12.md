# R6 — Decomposizione della TV fra i seed V13

Data: 2026-09-12  
Esito: `DIAGNOSTIC_PASS / V13_SEED_TV_ABOVE_5PP`

## Risultato

La TV root fra i due seed V13 è `12,6370 pp`
per la strategia media e `14,8250 pp`
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
| TV fra seed | 12,6370 pp | 14,8250 pp |
| TV con gap medio ≤0,1a | 12,1314 pp | 14,8250 pp |
| TV con gap medio ≥0,5a | 0,0001 pp | 0,0000 pp |
| TV entro due SE marginali | 7,8579 pp | 13,0161 pp |
| Massa TV × gap EV | 0,00521a | 0,00273a |

Differenza corrente meno media: `2,1880 pp`.

La policy corrente è più instabile della media. L'averaging ritardato ridurrebbe la finestra di mediazione e non è il primo intervento da promuovere. Il prossimo test deve ridurre la varianza dei continuation value e migliorare la copertura dei rami.

## Trasferimenti aggregati — strategia media

| Da seed 1 | A seed 2 | Contributo TV | Gap EV medio | Massa gap EV |
| --- | --- | ---: | ---: | ---: |
| call | fold | 1,952 pp | 0,040a | 0,0008a |
| raise_10 | all_in | 1,902 pp | 0,027a | 0,0005a |
| call | all_in | 1,272 pp | 0,055a | 0,0007a |
| fold | call | 1,230 pp | 0,026a | 0,0003a |
| raise_6 | all_in | 0,955 pp | 0,027a | 0,0003a |
| all_in | raise_10 | 0,692 pp | 0,047a | 0,0003a |
| all_in | call | 0,649 pp | 0,056a | 0,0004a |
| raise_6 | raise_10 | 0,637 pp | 0,037a | 0,0002a |
| raise_6 | call | 0,634 pp | 0,065a | 0,0004a |
| call | raise_10 | 0,609 pp | 0,057a | 0,0003a |
| raise_6 | fold | 0,555 pp | 0,040a | 0,0002a |
| call | raise_6 | 0,537 pp | 0,048a | 0,0003a |
| all_in | raise_6 | 0,420 pp | 0,035a | 0,0001a |
| raise_10 | call | 0,362 pp | 0,043a | 0,0002a |
| fold | raise_6 | 0,125 pp | 0,107a | 0,0001a |
| raise_10 | raise_6 | 0,106 pp | 0,014a | 0,0000a |
| fold | raise_10 | 0,000 pp | 0,176a | 0,0000a |
| raise_10 | fold | 0,000 pp | 0,580a | 0,0000a |
| all_in | fold | 0,000 pp | 0,901a | 0,0000a |
| fold | all_in | 0,000 pp | 0,959a | 0,0000a |

## Contributo per famiglia

| Famiglia | Strategia media | Policy corrente |
| --- | ---: | ---: |
| Coppie | 0,9085 pp | 1,0031 pp |
| Suited | 4,0930 pp | 3,9972 pp |
| Offsuit | 7,6356 pp | 9,8248 pp |

## Tutte le 81 classi — strategia media

Il nodo root ha reach pubblica `100%`. La colonna reach mostra la frequenza dell'azione sorgente
nel seed 1 e dell'azione destinazione nel seed 2 per lo spostamento principale.

| Combo | Massa | TV | Contributo | Spostamento principale | Massa spostata | Gap EV | Reach azione S1 → S2 | TV gap ≤0,1a | TV entro 2 SE |
| --- | ---: | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| K8o | 12 | 45,98 pp | 0,876 pp | call→fold | 25,07 pp | 0,034a | 30,8% → 94,2% | 100,0% | 0,0% |
| Q8o | 12 | 45,36 pp | 0,864 pp | call→fold | 42,14 pp | 0,026a | 58,2% → 83,6% | 100,0% | 92,9% |
| 98o | 12 | 43,25 pp | 0,824 pp | fold→call | 31,84 pp | 0,025a | 65,7% → 58,6% | 100,0% | 100,0% |
| A8o | 12 | 38,50 pp | 0,733 pp | call→all_in | 35,71 pp | 0,036a | 48,9% → 85,9% | 100,0% | 94,1% |
| AQo | 12 | 25,07 pp | 0,477 pp | raise_10→all_in | 17,29 pp | 0,025a | 27,2% → 86,5% | 99,9% | 69,0% |
| AKo | 12 | 24,97 pp | 0,476 pp | raise_10→all_in | 14,25 pp | 0,026a | 15,0% → 86,4% | 97,3% | 97,3% |
| T9o | 12 | 24,12 pp | 0,459 pp | call→raise_10 | 15,68 pp | 0,041a | 55,5% → 17,7% | 100,0% | 80,7% |
| T8s | 4 | 72,03 pp | 0,457 pp | all_in→call | 55,79 pp | 0,040a | 67,0% → 86,9% | 100,0% | 100,0% |
| KTo | 12 | 21,47 pp | 0,409 pp | raise_10→all_in | 14,45 pp | 0,005a | 21,3% → 88,8% | 100,0% | 79,9% |
| Q6s | 4 | 64,18 pp | 0,407 pp | fold→call | 48,65 pp | 0,011a | 92,8% → 55,8% | 75,8% | 75,8% |
| A6o | 12 | 20,85 pp | 0,397 pp | raise_6→call | 14,23 pp | 0,053a | 90,4% → 23,7% | 100,0% | 30,1% |
| QQ | 6 | 35,92 pp | 0,342 pp | raise_10→all_in | 28,21 pp | 0,020a | 43,0% → 51,5% | 100,0% | 81,1% |
| 97s | 4 | 51,44 pp | 0,327 pp | call→raise_10 | 36,13 pp | 0,066a | 88,9% → 44,9% | 100,0% | 83,9% |
| KTs | 4 | 47,98 pp | 0,305 pp | call→all_in | 41,00 pp | 0,064a | 48,7% → 67,1% | 100,0% | 100,0% |
| 66 | 6 | 31,33 pp | 0,298 pp | call→fold | 31,33 pp | 0,033a | 98,0% → 33,3% | 100,0% | 100,0% |
| Q7s | 4 | 44,59 pp | 0,283 pp | call→fold | 44,52 pp | 0,073a | 50,0% → 94,5% | 100,0% | 0,2% |
| 98s | 4 | 40,10 pp | 0,255 pp | raise_10→all_in | 29,71 pp | 0,002a | 67,0% → 37,5% | 99,1% | 99,1% |
| A7o | 12 | 12,62 pp | 0,240 pp | all_in→call | 9,77 pp | 0,074a | 92,6% → 10,0% | 100,0% | 0,0% |
| ATo | 12 | 12,50 pp | 0,238 pp | raise_6→raise_10 | 11,74 pp | 0,012a | 11,8% → 13,4% | 100,0% | 100,0% |
| K9o | 12 | 12,38 pp | 0,236 pp | call→raise_6 | 12,38 pp | 0,003a | 97,5% → 14,9% | 100,0% | 100,0% |
| J9o | 12 | 11,86 pp | 0,226 pp | raise_6→call | 11,86 pp | 0,080a | 11,9% → 100,0% | 100,0% | 0,0% |
| KQo | 12 | 11,76 pp | 0,224 pp | all_in→raise_10 | 9,77 pp | 0,064a | 94,5% → 12,7% | 100,0% | 6,1% |
| A6s | 4 | 33,70 pp | 0,214 pp | raise_10→all_in | 33,16 pp | 0,057a | 34,5% → 95,5% | 98,5% | 0,1% |
| A8s | 4 | 33,40 pp | 0,212 pp | raise_10→all_in | 28,07 pp | 0,056a | 35,8% → 91,6% | 99,1% | 15,1% |
| AKs | 4 | 32,63 pp | 0,207 pp | raise_6→raise_10 | 22,33 pp | 0,068a | 77,2% → 23,1% | 100,0% | 31,6% |
| T8o | 12 | 10,53 pp | 0,201 pp | call→raise_6 | 10,51 pp | 0,071a | 99,9% → 10,6% | 100,0% | 0,1% |
| AQs | 4 | 31,42 pp | 0,200 pp | all_in→raise_10 | 27,81 pp | 0,034a | 53,4% → 41,5% | 100,0% | 89,0% |
| JTo | 12 | 10,13 pp | 0,193 pp | raise_6→all_in | 6,26 pp | 0,003a | 6,4% → 9,6% | 93,7% | 61,8% |
| T9s | 4 | 28,69 pp | 0,182 pp | raise_6→all_in | 18,67 pp | 0,011a | 60,9% → 34,7% | 100,0% | 100,0% |
| A9s | 4 | 26,89 pp | 0,171 pp | all_in→raise_6 | 18,99 pp | 0,008a | 69,5% → 47,5% | 70,6% | 70,6% |
| JJ | 6 | 16,92 pp | 0,161 pp | raise_10→call | 9,04 pp | 0,096a | 17,1% → 22,6% | 100,0% | 8,8% |
| AJs | 4 | 24,41 pp | 0,155 pp | all_in→raise_6 | 14,43 pp | 0,033a | 90,4% → 22,7% | 99,1% | 59,1% |
| KJo | 12 | 8,09 pp | 0,154 pp | call→all_in | 6,27 pp | 0,167a | 6,3% → 91,0% | 22,5% | 0,0% |
| A7s | 4 | 20,30 pp | 0,129 pp | all_in→raise_10 | 10,11 pp | 0,063a | 93,4% → 11,8% | 76,2% | 0,0% |
| T7s | 4 | 19,04 pp | 0,121 pp | fold→call | 15,33 pp | 0,035a | 23,7% → 91,0% | 80,5% | 80,5% |
| KQs | 4 | 18,55 pp | 0,118 pp | raise_6→raise_10 | 9,10 pp | 0,036a | 27,6% → 10,1% | 100,0% | 100,0% |
| J8o | 12 | 5,37 pp | 0,102 pp | raise_6→fold | 5,01 pp | 0,053a | 5,4% → 17,9% | 100,0% | 0,0% |
| QJo | 12 | 5,02 pp | 0,096 pp | all_in→raise_10 | 2,64 pp | 0,036a | 4,5% → 5,0% | 84,6% | 0,0% |
| KJs | 4 | 15,04 pp | 0,095 pp | all_in→raise_10 | 7,73 pp | 0,001a | 9,4% → 17,8% | 99,0% | 99,0% |
| KK | 6 | 8,09 pp | 0,077 pp | raise_6→raise_10 | 7,45 pp | 0,058a | 80,4% → 8,2% | 100,0% | 7,9% |
| QTo | 12 | 3,69 pp | 0,070 pp | all_in→raise_10 | 1,67 pp | 0,039a | 90,5% → 5,3% | 73,8% | 28,5% |
| QTs | 4 | 10,43 pp | 0,066 pp | raise_10→all_in | 6,15 pp | 0,047a | 7,3% → 56,0% | 100,0% | 100,0% |
| ATs | 4 | 10,35 pp | 0,066 pp | raise_6→raise_10 | 6,87 pp | 0,063a | 60,9% → 18,2% | 88,5% | 22,1% |
| A9o | 12 | 2,52 pp | 0,048 pp | raise_10→all_in | 1,60 pp | 0,068a | 5,0% → 94,5% | 99,3% | 0,0% |
| K7o | 12 | 2,45 pp | 0,047 pp | fold→call | 2,45 pp | 0,087a | 99,9% → 2,5% | 100,0% | 0,0% |
| J7s | 4 | 6,10 pp | 0,039 pp | fold→call | 5,89 pp | 0,039a | 91,3% → 14,5% | 96,6% | 96,6% |
| TT | 6 | 1,96 pp | 0,019 pp | raise_6→call | 1,12 pp | 0,064a | 4,9% → 96,3% | 72,4% | 0,0% |
| J9s | 4 | 2,76 pp | 0,018 pp | call→raise_6 | 1,02 pp | 0,054a | 99,5% → 1,3% | 70,8% | 36,8% |
| T6s | 4 | 2,57 pp | 0,016 pp | call→fold | 2,54 pp | 0,126a | 3,4% → 99,1% | 0,9% | 0,0% |
| 97o | 12 | 0,67 pp | 0,013 pp | call→fold | 0,67 pp | 0,197a | 0,7% → 100,0% | 0,0% | 0,0% |
| Q8s | 4 | 1,77 pp | 0,011 pp | call→raise_6 | 1,69 pp | 0,120a | 99,8% → 1,8% | 0,0% | 0,0% |
| Q9o | 12 | 0,59 pp | 0,011 pp | call→raise_6 | 0,58 pp | 0,086a | 100,0% → 0,6% | 98,9% | 0,0% |
| Q9s | 4 | 1,58 pp | 0,010 pp | call→all_in | 0,84 pp | 0,188a | 99,5% → 1,0% | 0,0% | 0,0% |
| AJo | 12 | 0,50 pp | 0,010 pp | raise_10→raise_6 | 0,43 pp | 0,014a | 1,2% → 0,6% | 84,6% | 84,6% |
| J8s | 4 | 1,39 pp | 0,009 pp | call→raise_6 | 1,23 pp | 0,091a | 97,8% → 3,3% | 88,2% | 0,0% |
| AA | 6 | 0,85 pp | 0,008 pp | raise_6→call | 0,85 pp | 0,223a | 0,9% → 100,0% | 0,0% | 0,0% |
| K8s | 4 | 0,85 pp | 0,005 pp | raise_6→call | 0,79 pp | 0,103a | 1,0% → 99,8% | 0,0% | 0,0% |
| K6s | 4 | 0,84 pp | 0,005 pp | fold→call | 0,79 pp | 0,104a | 0,9% → 99,9% | 5,7% | 5,7% |
| K9s | 4 | 0,47 pp | 0,003 pp | raise_6→call | 0,28 pp | 0,188a | 0,3% → 100,0% | 0,0% | 0,0% |
| T7o | 12 | 0,14 pp | 0,003 pp | fold→call | 0,13 pp | 0,192a | 99,9% → 0,2% | 0,0% | 0,0% |
| J7o | 12 | 0,13 pp | 0,003 pp | fold→call | 0,13 pp | 0,237a | 100,0% → 0,1% | 0,0% | 0,0% |
| 86s | 4 | 0,39 pp | 0,002 pp | call→fold | 0,39 pp | 0,270a | 0,4% → 100,0% | 0,0% | 0,0% |
| K6o | 12 | 0,13 pp | 0,002 pp | fold→call | 0,13 pp | 0,168a | 100,0% → 0,2% | 0,7% | 0,7% |
| 87o | 12 | 0,12 pp | 0,002 pp | call→fold | 0,12 pp | 0,358a | 0,1% → 100,0% | 0,0% | 0,0% |
| Q7o | 12 | 0,11 pp | 0,002 pp | call→fold | 0,11 pp | 0,283a | 0,1% → 100,0% | 0,0% | 0,0% |
| 88 | 6 | 0,17 pp | 0,002 pp | call→fold | 0,17 pp | 0,250a | 100,0% → 0,2% | 0,0% | 0,0% |
| QJs | 4 | 0,24 pp | 0,002 pp | call→raise_6 | 0,20 pp | 0,239a | 99,9% → 0,2% | 14,3% | 0,4% |
| K7s | 4 | 0,16 pp | 0,001 pp | call→raise_6 | 0,11 pp | 0,077a | 100,0% → 0,1% | 66,2% | 0,0% |
| 77 | 6 | 0,11 pp | 0,001 pp | fold→call | 0,11 pp | 0,075a | 0,2% → 99,9% | 99,9% | 0,0% |
| JTs | 4 | 0,15 pp | 0,001 pp | raise_6→call | 0,08 pp | 0,226a | 0,1% → 100,0% | 0,0% | 0,0% |
| J6s | 4 | 0,08 pp | 0,000 pp | call→fold | 0,08 pp | 0,148a | 0,7% → 99,4% | 0,0% | 0,0% |
| 96s | 4 | 0,07 pp | 0,000 pp | call→fold | 0,04 pp | 0,154a | 0,1% → 99,9% | 0,0% | 0,0% |
| 99 | 6 | 0,04 pp | 0,000 pp | raise_10→call | 0,02 pp | 0,365a | 0,1% → 100,0% | 0,0% | 0,0% |
| 87s | 4 | 0,03 pp | 0,000 pp | fold→call | 0,03 pp | 0,162a | 99,8% → 0,2% | 5,2% | 5,2% |
| 76s | 4 | 0,03 pp | 0,000 pp | call→fold | 0,03 pp | 0,308a | 0,0% → 100,0% | 0,0% | 0,0% |
| 86o | 12 | 0,00 pp | 0,000 pp | call→fold | 0,00 pp | 0,491a | 0,0% → 100,0% | 0,0% | 0,0% |
| 96o | 12 | 0,00 pp | 0,000 pp | call→fold | 0,00 pp | 0,367a | 0,0% → 100,0% | 0,0% | 0,0% |
| Q6o | 12 | 0,00 pp | 0,000 pp | call→fold | 0,00 pp | 0,288a | 0,1% → 100,0% | 0,0% | 0,0% |
| 76o | 12 | 0,00 pp | 0,000 pp | fold→call | 0,00 pp | 0,542a | 100,0% → 0,0% | 0,0% | 0,0% |
| T6o | 12 | 0,00 pp | 0,000 pp | call→fold | 0,00 pp | 0,382a | 0,0% → 100,0% | 0,0% | 0,0% |
| J6o | 12 | 0,00 pp | 0,000 pp | call→fold | 0,00 pp | 0,396a | 0,0% → 100,0% | 5,1% | 5,1% |

## Tutte le 81 classi — policy corrente

| Combo | Massa | TV | Contributo | Spostamento principale | Massa spostata | Gap EV | Reach azione S1 → S2 | TV gap ≤0,1a | TV entro 2 SE |
| --- | ---: | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| ATo | 12 | 100,00 pp | 1,905 pp | all_in→raise_10 | 100,00 pp | 0,006a | 100,0% → 100,0% | 100,0% | 100,0% |
| K9o | 12 | 72,86 pp | 1,388 pp | call→raise_6 | 72,86 pp | 0,003a | 100,0% → 72,9% | 100,0% | 100,0% |
| KTo | 12 | 54,33 pp | 1,035 pp | raise_10→all_in | 54,33 pp | 0,005a | 54,3% → 100,0% | 100,0% | 100,0% |
| AKo | 12 | 47,02 pp | 0,896 pp | raise_6→all_in | 35,45 pp | 0,010a | 35,5% → 100,0% | 100,0% | 100,0% |
| K8o | 12 | 47,02 pp | 0,896 pp | raise_6→fold | 34,15 pp | 0,033a | 34,2% → 100,0% | 100,0% | 0,0% |
| 98o | 12 | 37,92 pp | 0,722 pp | fold→raise_6 | 22,09 pp | 0,020a | 100,0% → 22,1% | 100,0% | 100,0% |
| Q8o | 12 | 37,88 pp | 0,722 pp | call→fold | 37,88 pp | 0,026a | 37,9% → 100,0% | 100,0% | 100,0% |
| QTo | 12 | 36,13 pp | 0,688 pp | all_in→call | 36,13 pp | 0,022a | 100,0% → 36,1% | 100,0% | 100,0% |
| Q6s | 4 | 100,00 pp | 0,635 pp | fold→call | 100,00 pp | 0,011a | 100,0% → 100,0% | 100,0% | 100,0% |
| A8o | 12 | 31,42 pp | 0,598 pp | call→all_in | 20,60 pp | 0,036a | 20,6% → 100,0% | 100,0% | 65,6% |
| QQ | 6 | 57,73 pp | 0,550 pp | call→all_in | 23,44 pp | 0,015a | 31,7% → 91,7% | 100,0% | 80,6% |
| T9o | 12 | 28,28 pp | 0,539 pp | call→all_in | 28,28 pp | 0,003a | 59,0% → 69,3% | 100,0% | 100,0% |
| 98s | 4 | 74,06 pp | 0,470 pp | raise_6→all_in | 44,93 pp | 0,013a | 100,0% → 44,9% | 100,0% | 100,0% |
| T9s | 4 | 74,03 pp | 0,470 pp | raise_6→all_in | 74,03 pp | 0,011a | 100,0% → 74,0% | 100,0% | 100,0% |
| AQo | 12 | 22,94 pp | 0,437 pp | raise_10→all_in | 22,94 pp | 0,025a | 22,9% → 100,0% | 100,0% | 100,0% |
| T8s | 4 | 59,04 pp | 0,375 pp | all_in→raise_6 | 35,26 pp | 0,009a | 35,3% → 59,0% | 100,0% | 100,0% |
| KK | 6 | 36,48 pp | 0,347 pp | raise_6→call | 36,48 pp | 0,018a | 100,0% → 36,5% | 100,0% | 100,0% |
| A9s | 4 | 47,73 pp | 0,303 pp | all_in→raise_6 | 47,73 pp | 0,008a | 87,1% → 60,6% | 100,0% | 100,0% |
| KTs | 4 | 45,99 pp | 0,292 pp | call→raise_6 | 24,71 pp | 0,061a | 100,0% → 24,7% | 100,0% | 100,0% |
| A6s | 4 | 35,25 pp | 0,224 pp | raise_10→all_in | 35,25 pp | 0,057a | 35,3% → 100,0% | 100,0% | 0,0% |
| KQs | 4 | 34,53 pp | 0,219 pp | raise_6→all_in | 18,16 pp | 0,011a | 34,5% → 18,2% | 100,0% | 100,0% |
| AJs | 4 | 33,85 pp | 0,215 pp | all_in→raise_6 | 33,85 pp | 0,033a | 100,0% → 33,8% | 100,0% | 100,0% |
| 97s | 4 | 32,23 pp | 0,205 pp | call→fold | 28,88 pp | 0,065a | 100,0% → 28,9% | 100,0% | 10,4% |
| ATs | 4 | 30,48 pp | 0,194 pp | raise_6→raise_10 | 30,48 pp | 0,063a | 100,0% → 30,5% | 100,0% | 0,0% |
| AQs | 4 | 26,34 pp | 0,167 pp | all_in→raise_10 | 26,34 pp | 0,034a | 100,0% → 26,3% | 100,0% | 100,0% |
| QTs | 4 | 20,18 pp | 0,128 pp | all_in→raise_6 | 13,06 pp | 0,042a | 71,5% → 13,1% | 100,0% | 100,0% |
| JJ | 6 | 11,11 pp | 0,106 pp | raise_6→call | 11,11 pp | 0,005a | 50,8% → 60,3% | 100,0% | 100,0% |
| AKs | 4 | 15,85 pp | 0,101 pp | call→raise_6 | 15,85 pp | 0,011a | 69,9% → 45,9% | 100,0% | 100,0% |
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
| 99 | 6 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| A6o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| A7o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| A7s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| A8s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| A9o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| AA | 6 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| AJo | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| J6o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| J6s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| J7o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| J7s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| J8o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
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
| K9s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| KJo | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| KJs | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| KQo | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| Q6o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| Q7o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| Q7s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| Q8s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| Q9o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| Q9s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| QJo | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| QJs | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
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

La policy corrente è più instabile della media. L'averaging ritardato ridurrebbe la finestra di mediazione e non è il primo intervento da promuovere. Il prossimo test deve ridurre la varianza dei continuation value e migliorare la copertura dei rami.

Il prossimo esperimento deve essere pre-registrato contro questa decomposizione. Un candidato passa
soltanto se riduce la TV media verso `5 pp`, non peggiora la WMAE e conserva gli audit monetari e
Call/Fold di V13.
