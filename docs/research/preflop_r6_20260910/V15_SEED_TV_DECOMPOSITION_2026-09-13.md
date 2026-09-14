# R6 — Decomposizione della TV fra i seed V15

Data: 2026-09-12  
Esito: `DIAGNOSTIC_PASS / V15_SEED_TV_ABOVE_5PP`

## Risultato

La TV root fra i due seed V15 è `10,9453 pp`
per la strategia media e `11,1472 pp`
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
| TV fra seed | 10,9453 pp | 11,1472 pp |
| TV con gap medio ≤0,1a | 10,2475 pp | 11,1472 pp |
| TV con gap medio ≥0,5a | 0,0001 pp | 0,0000 pp |
| TV entro due SE marginali | 6,3119 pp | 9,2957 pp |
| Massa TV × gap EV | 0,00497a | 0,00307a |

Differenza corrente meno media: `0,2019 pp`.

La policy corrente è più instabile della media. L'averaging ritardato ridurrebbe la finestra di mediazione e non è il primo intervento da promuovere. Il prossimo test deve ridurre la varianza dei continuation value e migliorare la copertura dei rami.

## Trasferimenti aggregati — strategia media

| Da seed 1 | A seed 2 | Contributo TV | Gap EV medio | Massa gap EV |
| --- | --- | ---: | ---: | ---: |
| call | raise_6 | 1,324 pp | 0,049a | 0,0006a |
| call | fold | 1,316 pp | 0,039a | 0,0005a |
| all_in | raise_6 | 1,195 pp | 0,046a | 0,0005a |
| raise_6 | all_in | 1,189 pp | 0,036a | 0,0004a |
| raise_6 | call | 0,911 pp | 0,027a | 0,0002a |
| all_in | call | 0,841 pp | 0,057a | 0,0005a |
| call | all_in | 0,837 pp | 0,061a | 0,0005a |
| call | raise_10 | 0,677 pp | 0,064a | 0,0004a |
| raise_10 | call | 0,573 pp | 0,063a | 0,0004a |
| all_in | raise_10 | 0,512 pp | 0,052a | 0,0003a |
| raise_10 | all_in | 0,509 pp | 0,033a | 0,0002a |
| fold | call | 0,415 pp | 0,034a | 0,0001a |
| raise_10 | raise_6 | 0,287 pp | 0,030a | 0,0001a |
| raise_6 | raise_10 | 0,159 pp | 0,029a | 0,0000a |
| raise_6 | fold | 0,107 pp | 0,026a | 0,0000a |
| raise_10 | fold | 0,058 pp | 0,036a | 0,0000a |
| fold | raise_6 | 0,035 pp | 0,106a | 0,0000a |
| fold | raise_10 | 0,000 pp | 0,274a | 0,0000a |
| all_in | fold | 0,000 pp | 0,952a | 0,0000a |
| fold | all_in | 0,000 pp | 0,875a | 0,0000a |

## Contributo per famiglia

| Famiglia | Strategia media | Policy corrente |
| --- | ---: | ---: |
| Coppie | 1,4581 pp | 0,7588 pp |
| Suited | 4,2098 pp | 3,4893 pp |
| Offsuit | 5,2774 pp | 6,8992 pp |

## Tutte le 81 classi — strategia media

Il nodo root ha reach pubblica `100%`. La colonna reach mostra la frequenza dell'azione sorgente
nel seed 1 e dell'azione destinazione nel seed 2 per lo spostamento principale.

| Combo | Massa | TV | Contributo | Spostamento principale | Massa spostata | Gap EV | Reach azione S1 → S2 | TV gap ≤0,1a | TV entro 2 SE |
| --- | ---: | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 98o | 12 | 33,29 pp | 0,634 pp | raise_6→call | 19,47 pp | 0,007a | 20,0% → 60,1% | 100,0% | 90,8% |
| T9o | 12 | 31,75 pp | 0,605 pp | call→raise_10 | 13,57 pp | 0,028a | 46,4% → 17,5% | 100,0% | 66,1% |
| AQo | 12 | 29,31 pp | 0,558 pp | raise_6→all_in | 27,61 pp | 0,055a | 30,1% → 93,7% | 99,9% | 5,7% |
| T8s | 4 | 82,03 pp | 0,521 pp | all_in→call | 50,44 pp | 0,039a | 50,9% → 98,5% | 100,0% | 74,0% |
| QQ | 6 | 48,80 pp | 0,465 pp | raise_10→all_in | 31,11 pp | 0,021a | 40,5% → 39,4% | 100,0% | 100,0% |
| 66 | 6 | 46,88 pp | 0,447 pp | call→fold | 46,88 pp | 0,022a | 97,4% → 49,5% | 100,0% | 100,0% |
| A8o | 12 | 22,06 pp | 0,420 pp | call→all_in | 21,20 pp | 0,046a | 36,5% → 83,9% | 100,0% | 0,4% |
| T9s | 4 | 60,85 pp | 0,386 pp | call→raise_6 | 49,26 pp | 0,064a | 50,5% → 76,3% | 100,0% | 95,6% |
| Q8o | 12 | 18,56 pp | 0,353 pp | call→fold | 18,56 pp | 0,025a | 39,1% → 79,4% | 100,0% | 100,0% |
| A7o | 12 | 17,76 pp | 0,338 pp | all_in→call | 13,52 pp | 0,089a | 96,5% → 14,4% | 100,0% | 4,0% |
| Q7s | 4 | 47,01 pp | 0,299 pp | call→fold | 47,01 pp | 0,063a | 49,4% → 97,7% | 100,0% | 0,0% |
| KQo | 12 | 15,44 pp | 0,294 pp | all_in→raise_6 | 11,50 pp | 0,045a | 95,0% → 15,0% | 94,9% | 20,3% |
| ATo | 12 | 14,30 pp | 0,272 pp | raise_6→all_in | 11,65 pp | 0,014a | 15,4% → 95,7% | 100,0% | 81,5% |
| KTs | 4 | 40,71 pp | 0,258 pp | call→raise_10 | 24,48 pp | 0,121a | 71,5% → 29,0% | 20,3% | 0,0% |
| Q6s | 4 | 39,32 pp | 0,250 pp | fold→call | 37,45 pp | 0,034a | 55,7% → 81,6% | 95,2% | 95,2% |
| KJs | 4 | 38,33 pp | 0,243 pp | raise_6→call | 17,28 pp | 0,039a | 23,2% → 89,3% | 100,0% | 100,0% |
| Q9o | 12 | 12,76 pp | 0,243 pp | call→raise_6 | 12,76 pp | 0,033a | 100,0% → 12,8% | 100,0% | 100,0% |
| K9o | 12 | 12,46 pp | 0,237 pp | call→raise_6 | 12,46 pp | 0,023a | 100,0% → 12,5% | 100,0% | 100,0% |
| 97s | 4 | 36,65 pp | 0,233 pp | call→fold | 22,81 pp | 0,056a | 74,5% → 25,1% | 100,0% | 37,8% |
| AKo | 12 | 11,61 pp | 0,221 pp | all_in→raise_6 | 9,79 pp | 0,035a | 76,9% → 14,4% | 97,4% | 13,0% |
| TT | 6 | 21,17 pp | 0,202 pp | raise_6→call | 13,32 pp | 0,041a | 13,4% → 99,9% | 96,7% | 96,7% |
| 98s | 4 | 31,24 pp | 0,198 pp | all_in→raise_6 | 15,94 pp | 0,018a | 17,9% → 47,1% | 91,4% | 91,4% |
| QTs | 4 | 31,21 pp | 0,198 pp | all_in→raise_10 | 22,73 pp | 0,033a | 86,2% → 34,0% | 100,0% | 89,8% |
| J8s | 4 | 31,16 pp | 0,198 pp | call→raise_6 | 29,81 pp | 0,081a | 98,2% → 31,6% | 95,7% | 0,0% |
| JJ | 6 | 20,76 pp | 0,198 pp | all_in→raise_6 | 19,37 pp | 0,065a | 22,1% → 74,7% | 100,0% | 6,7% |
| AQs | 4 | 30,59 pp | 0,194 pp | all_in→raise_6 | 24,11 pp | 0,022a | 53,4% → 49,0% | 100,0% | 100,0% |
| A8s | 4 | 29,35 pp | 0,186 pp | call→all_in | 17,50 pp | 0,147a | 19,3% → 91,1% | 40,4% | 1,9% |
| A7s | 4 | 28,03 pp | 0,178 pp | all_in→raise_10 | 21,28 pp | 0,065a | 94,0% → 22,3% | 75,9% | 0,0% |
| KTo | 12 | 9,20 pp | 0,175 pp | all_in→raise_10 | 5,22 pp | 0,049a | 97,3% → 7,0% | 100,0% | 0,1% |
| JTo | 12 | 8,94 pp | 0,170 pp | raise_6→all_in | 8,44 pp | 0,003a | 9,5% → 8,4% | 99,2% | 94,3% |
| KQs | 4 | 26,50 pp | 0,168 pp | raise_6→call | 12,39 pp | 0,004a | 27,7% → 64,2% | 100,0% | 100,0% |
| KJo | 12 | 8,65 pp | 0,165 pp | call→raise_10 | 5,96 pp | 0,102a | 6,0% → 10,0% | 31,1% | 14,5% |
| K8o | 12 | 8,05 pp | 0,153 pp | raise_6→fold | 5,59 pp | 0,025a | 8,2% → 90,8% | 100,0% | 100,0% |
| KK | 6 | 15,16 pp | 0,144 pp | raise_6→raise_10 | 8,81 pp | 0,040a | 88,8% → 11,4% | 100,0% | 100,0% |
| J8o | 12 | 5,86 pp | 0,112 pp | fold→call | 5,85 pp | 0,010a | 13,6% → 92,3% | 100,0% | 99,7% |
| ATs | 4 | 16,01 pp | 0,102 pp | all_in→raise_6 | 10,09 pp | 0,081a | 28,3% → 81,6% | 99,8% | 0,0% |
| AKs | 4 | 15,57 pp | 0,099 pp | call→raise_10 | 7,92 pp | 0,003a | 37,1% → 38,4% | 100,0% | 63,4% |
| QJo | 12 | 4,61 pp | 0,088 pp | raise_10→call | 3,84 pp | 0,090a | 9,2% → 89,5% | 100,0% | 16,8% |
| A6s | 4 | 13,58 pp | 0,086 pp | raise_10→all_in | 9,05 pp | 0,070a | 13,9% → 94,1% | 69,5% | 2,9% |
| QTo | 12 | 4,51 pp | 0,086 pp | call→all_in | 3,60 pp | 0,021a | 14,7% → 88,4% | 100,0% | 79,8% |
| AJs | 4 | 12,68 pp | 0,080 pp | raise_10→raise_6 | 8,57 pp | 0,041a | 14,9% → 14,3% | 84,8% | 84,8% |
| A9s | 4 | 9,41 pp | 0,060 pp | all_in→raise_6 | 6,86 pp | 0,036a | 23,7% → 79,0% | 93,4% | 93,4% |
| K8s | 4 | 8,82 pp | 0,056 pp | call→raise_6 | 8,77 pp | 0,019a | 91,3% → 17,4% | 99,4% | 99,4% |
| A9o | 12 | 2,66 pp | 0,051 pp | all_in→raise_10 | 2,13 pp | 0,090a | 98,5% → 3,0% | 98,6% | 0,0% |
| T7s | 4 | 7,66 pp | 0,049 pp | fold→call | 6,34 pp | 0,051a | 12,4% → 93,7% | 82,8% | 0,0% |
| J7s | 4 | 7,35 pp | 0,047 pp | call→fold | 7,35 pp | 0,038a | 31,4% → 76,0% | 100,0% | 100,0% |
| T8o | 12 | 2,00 pp | 0,038 pp | call→raise_6 | 1,95 pp | 0,045a | 99,5% → 2,4% | 99,9% | 2,2% |
| K7s | 4 | 5,28 pp | 0,034 pp | raise_6→call | 4,94 pp | 0,115a | 5,3% → 99,7% | 0,0% | 0,0% |
| AJo | 12 | 1,40 pp | 0,027 pp | all_in→raise_10 | 1,22 pp | 0,080a | 99,4% → 1,8% | 99,2% | 0,0% |
| Q9s | 4 | 3,59 pp | 0,023 pp | call→all_in | 2,42 pp | 0,191a | 98,7% → 3,2% | 0,0% | 0,0% |
| T6s | 4 | 3,18 pp | 0,020 pp | call→fold | 2,46 pp | 0,132a | 3,2% → 99,2% | 22,7% | 0,0% |
| K6s | 4 | 2,49 pp | 0,016 pp | raise_6→call | 1,63 pp | 0,092a | 1,7% → 99,9% | 100,0% | 0,0% |
| A6o | 12 | 0,81 pp | 0,015 pp | fold→raise_6 | 0,55 pp | 0,084a | 0,6% → 96,4% | 99,6% | 0,0% |
| J9s | 4 | 2,32 pp | 0,015 pp | all_in→raise_6 | 1,02 pp | 0,019a | 1,0% → 2,4% | 44,0% | 44,0% |
| K7o | 12 | 0,48 pp | 0,009 pp | fold→call | 0,48 pp | 0,095a | 99,7% → 0,7% | 100,0% | 0,0% |
| 96s | 4 | 1,00 pp | 0,006 pp | fold→call | 1,00 pp | 0,140a | 99,9% → 1,1% | 0,0% | 0,0% |
| 97o | 12 | 0,30 pp | 0,006 pp | call→fold | 0,30 pp | 0,187a | 0,4% → 99,9% | 0,0% | 0,0% |
| Q7o | 12 | 0,11 pp | 0,002 pp | call→fold | 0,11 pp | 0,281a | 0,2% → 99,9% | 0,0% | 0,0% |
| QJs | 4 | 0,25 pp | 0,002 pp | raise_6→call | 0,19 pp | 0,148a | 0,2% → 99,9% | 25,0% | 0,1% |
| 86s | 4 | 0,23 pp | 0,001 pp | call→fold | 0,19 pp | 0,305a | 0,4% → 99,8% | 0,0% | 0,0% |
| K9s | 4 | 0,20 pp | 0,001 pp | raise_10→call | 0,16 pp | 0,246a | 0,2% → 100,0% | 0,0% | 0,0% |
| T7o | 12 | 0,06 pp | 0,001 pp | fold→raise_6 | 0,04 pp | 0,266a | 100,0% → 0,0% | 0,0% | 0,0% |
| 77 | 6 | 0,12 pp | 0,001 pp | fold→call | 0,12 pp | 0,087a | 0,6% → 99,5% | 99,6% | 0,0% |
| 99 | 6 | 0,12 pp | 0,001 pp | raise_6→call | 0,12 pp | 0,123a | 0,1% → 100,0% | 0,0% | 0,0% |
| J9o | 12 | 0,06 pp | 0,001 pp | call→raise_6 | 0,06 pp | 0,055a | 100,0% → 0,1% | 99,7% | 0,0% |
| Q8s | 4 | 0,16 pp | 0,001 pp | call→raise_6 | 0,13 pp | 0,092a | 99,0% → 1,1% | 83,7% | 0,0% |
| AA | 6 | 0,07 pp | 0,001 pp | call→raise_6 | 0,07 pp | 0,218a | 100,0% → 0,1% | 0,0% | 0,0% |
| J6s | 4 | 0,11 pp | 0,001 pp | call→fold | 0,11 pp | 0,159a | 0,2% → 99,9% | 0,0% | 0,0% |
| 87o | 12 | 0,03 pp | 0,000 pp | call→fold | 0,03 pp | 0,352a | 0,0% → 100,0% | 0,0% | 0,0% |
| 87s | 4 | 0,07 pp | 0,000 pp | fold→call | 0,07 pp | 0,166a | 100,0% → 0,1% | 0,1% | 0,1% |
| K6o | 12 | 0,02 pp | 0,000 pp | fold→call | 0,02 pp | 0,171a | 100,0% → 0,0% | 0,0% | 0,0% |
| 76s | 4 | 0,05 pp | 0,000 pp | call→fold | 0,05 pp | 0,346a | 0,1% → 100,0% | 0,0% | 0,0% |
| Q6o | 12 | 0,02 pp | 0,000 pp | fold→call | 0,02 pp | 0,289a | 100,0% → 0,0% | 0,0% | 0,0% |
| JTs | 4 | 0,05 pp | 0,000 pp | raise_10→call | 0,03 pp | 0,287a | 0,0% → 100,0% | 0,0% | 0,0% |
| J7o | 12 | 0,00 pp | 0,000 pp | call→fold | 0,00 pp | 0,236a | 0,0% → 100,0% | 0,0% | 0,0% |
| 96o | 12 | 0,00 pp | 0,000 pp | call→fold | 0,00 pp | 0,369a | 0,0% → 100,0% | 0,1% | 0,0% |
| 88 | 6 | 0,00 pp | 0,000 pp | raise_6→call | 0,00 pp | 0,359a | 0,0% → 100,0% | 0,0% | 0,0% |
| T6o | 12 | 0,00 pp | 0,000 pp | fold→call | 0,00 pp | 0,392a | 100,0% → 0,0% | 0,0% | 0,0% |
| J6o | 12 | 0,00 pp | 0,000 pp | fold→call | 0,00 pp | 0,411a | 100,0% → 0,0% | 0,3% | 0,0% |
| 86o | 12 | 0,00 pp | 0,000 pp | call→fold | 0,00 pp | 0,502a | 0,0% → 100,0% | 0,2% | 0,0% |
| 76o | 12 | 0,00 pp | 0,000 pp | raise_6→fold | 0,00 pp | 0,699a | 0,0% → 100,0% | 0,0% | 0,0% |

## Tutte le 81 classi — policy corrente

| Combo | Massa | TV | Contributo | Spostamento principale | Massa spostata | Gap EV | Reach azione S1 → S2 | TV gap ≤0,1a | TV entro 2 SE |
| --- | ---: | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| ATo | 12 | 92,45 pp | 1,761 pp | all_in→raise_6 | 92,45 pp | 0,014a | 100,0% → 92,4% | 100,0% | 100,0% |
| A8o | 12 | 66,11 pp | 1,259 pp | call→all_in | 66,11 pp | 0,046a | 66,1% → 100,0% | 100,0% | 0,0% |
| T9o | 12 | 60,10 pp | 1,145 pp | call→all_in | 53,58 pp | 0,028a | 53,6% → 100,0% | 100,0% | 89,2% |
| Q8o | 12 | 51,00 pp | 0,971 pp | call→fold | 51,00 pp | 0,025a | 51,0% → 100,0% | 100,0% | 100,0% |
| 98o | 12 | 43,85 pp | 0,835 pp | raise_6→fold | 24,72 pp | 0,017a | 38,5% → 72,9% | 100,0% | 87,8% |
| T8s | 4 | 94,49 pp | 0,600 pp | raise_6→call | 40,53 pp | 0,049a | 40,5% → 100,0% | 100,0% | 78,9% |
| K9o | 12 | 25,27 pp | 0,481 pp | call→raise_6 | 25,27 pp | 0,023a | 100,0% → 25,3% | 100,0% | 100,0% |
| 98s | 4 | 66,29 pp | 0,421 pp | raise_10→raise_6 | 47,02 pp | 0,014a | 47,0% → 99,1% | 100,0% | 100,0% |
| AKo | 12 | 22,02 pp | 0,419 pp | raise_10→all_in | 22,02 pp | 0,001a | 63,6% → 58,5% | 100,0% | 100,0% |
| K8s | 4 | 64,74 pp | 0,411 pp | call→raise_6 | 64,74 pp | 0,019a | 100,0% → 64,7% | 100,0% | 100,0% |
| QTs | 4 | 58,01 pp | 0,368 pp | all_in→raise_10 | 58,01 pp | 0,033a | 100,0% → 58,0% | 100,0% | 100,0% |
| 97s | 4 | 52,98 pp | 0,336 pp | call→fold | 31,76 pp | 0,056a | 78,8% → 36,5% | 100,0% | 40,1% |
| J7s | 4 | 49,10 pp | 0,312 pp | fold→call | 49,10 pp | 0,038a | 100,0% → 49,1% | 100,0% | 100,0% |
| KJs | 4 | 47,46 pp | 0,301 pp | all_in→call | 17,52 pp | 0,066a | 17,5% → 100,0% | 100,0% | 100,0% |
| Q6s | 4 | 46,33 pp | 0,294 pp | fold→call | 46,33 pp | 0,034a | 46,3% → 100,0% | 100,0% | 100,0% |
| 66 | 6 | 29,08 pp | 0,277 pp | call→fold | 29,08 pp | 0,022a | 100,0% → 29,1% | 100,0% | 100,0% |
| QQ | 6 | 25,83 pp | 0,246 pp | raise_10→all_in | 19,98 pp | 0,021a | 74,6% → 38,5% | 100,0% | 100,0% |
| T9s | 4 | 38,06 pp | 0,242 pp | call→all_in | 21,31 pp | 0,058a | 38,1% → 46,7% | 100,0% | 100,0% |
| JJ | 6 | 24,75 pp | 0,236 pp | call→raise_10 | 20,74 pp | 0,025a | 29,4% → 24,8% | 100,0% | 83,8% |
| AQs | 4 | 14,26 pp | 0,091 pp | all_in→raise_10 | 12,06 pp | 0,025a | 65,7% → 23,4% | 100,0% | 100,0% |
| KQs | 4 | 12,96 pp | 0,082 pp | all_in→raise_10 | 6,97 pp | 0,006a | 23,5% → 31,4% | 100,0% | 100,0% |
| AKs | 4 | 4,89 pp | 0,031 pp | raise_6→raise_10 | 4,62 pp | 0,011a | 45,6% → 32,8% | 100,0% | 100,0% |
| J8o | 12 | 1,41 pp | 0,027 pp | call→fold | 1,41 pp | 0,010a | 100,0% → 1,4% | 100,0% | 100,0% |
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
| A6s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| A7o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| A7s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| A8s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| A9o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| A9s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| AA | 6 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| AJo | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| AJs | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| AQo | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| ATs | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
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
| K8o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| K9s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| KJo | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| KK | 6 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| KQo | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| KTo | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| KTs | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| Q6o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| Q7o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| Q7s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| Q8s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| Q9o | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| Q9s | 4 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
| QJo | 12 | 0,00 pp | 0,000 pp | — | — | — | — | 0,0% | 0,0% |
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

La policy corrente è più instabile della media. L'averaging ritardato ridurrebbe la finestra di mediazione e non è il primo intervento da promuovere. Il prossimo test deve ridurre la varianza dei continuation value e migliorare la copertura dei rami.

Il prossimo esperimento deve essere pre-registrato contro questa decomposizione. Un candidato passa
soltanto se riduce la TV media verso `5 pp`, non peggiora la WMAE e conserva gli audit monetari e
Call/Fold di V15.
