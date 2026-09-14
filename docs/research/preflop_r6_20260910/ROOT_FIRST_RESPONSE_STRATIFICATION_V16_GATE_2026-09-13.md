# R6 — Gate V16: stratificazione della prima risposta

Data: 2026-09-13  
Esito: `ENGINEERING_PASS / COST_PASS / LOCAL_AUDIT_PASS / QUALITY_GATE_FAIL`

## Risultato

V16 completa due solve indipendenti da 2.000.000 di iterazioni con tabella a sette carte, quattro
rollout, update simmetrici, Common Random Numbers e stratificazione della prima risposta.

Il costo resta vicino a V15: `36,30` e `38,19 minuti` per solve. La TV fra seed peggiora però da
`10,9453` a `13,0557 pp` per la strategia media e da `11,1472` a `12,9262 pp` per la policy
corrente. La stratificazione non supera il gate di `5 pp`.

## Coppia 2M

| Metrica | Seed 1 | Seed 2 | Media/coppia |
| --- | ---: | ---: | ---: |
| Iterazioni | 2.000.000 | 2.000.000 | — |
| Solve | 2.178,28 s | 2.291,50 s | 2.234,89 s |
| Solve | 36,30 min | 38,19 min | 37,25 min |
| Root EV ± SE | −0,24990 ± 0,05101a | −0,11910 ± 0,04966a | — |
| Infoset | 1.568.878 | 1.565.330 | — |
| Payload numerico | 240.952.608 B | 239.732.784 B | — |
| WMAE contro Monker | 14,0805 pp | 13,2535 pp | **13,6670 pp** |
| TV contro Monker | 35,2014 pp | 33,1337 pp | **34,1675 pp** |
| P95 TV | 96,6344 pp | 96,6396 pp | **96,6370 pp** |
| TV media fra seed | — | — | **13,0557 pp** |
| TV corrente fra seed | — | — | **12,9262 pp** |

Il seed 2 ottiene una WMAE di `13,2535 pp`, inferiore di `0,0187 pp` al singolo seed V13 mostrato
nel viewer. Il miglioramento è troppo piccolo per compensare il fallimento preregistrato della
coppia e non autorizza la sostituzione del viewer.

## Confronto V15/V16

| Metrica di coppia | V15 | V16 | V16 − V15 |
| --- | ---: | ---: | ---: |
| WMAE media | 13,9472 pp | 13,6670 pp | −0,2802 pp |
| TV media contro Monker | 34,8679 pp | 34,1675 pp | −0,7004 pp |
| TV media fra seed | **10,9453 pp** | 13,0557 pp | +2,1104 pp |
| TV corrente fra seed | **11,1472 pp** | 12,9262 pp | +1,7790 pp |
| Tempo medio | 37,36 min | **37,25 min** | −0,11 min |

V16 migliora leggermente l'accordo medio esterno ma peggiora entrambe le misure causali di
stabilità. Non raggiunge nessun limite assoluto di qualità.

## Decomposizione della TV

| Componente | Strategia media | Policy corrente |
| --- | ---: | ---: |
| TV totale | 13,0557 pp | 12,9262 pp |
| Gap medio non superiore a 0,1a | 12,4934 pp | 12,9262 pp |
| Gap medio almeno 0,5a | 0,0001 pp | 0,0000 pp |
| Entro due SE marginali | 7,1656 pp | 11,1754 pp |
| Massa TV × gap EV | 0,00516a | 0,00297a |

Il `95,69%` della TV media e il `100%` della TV corrente ricadono ancora fra azioni quasi
indifferenti. La prima risposta avversaria non è il termine di varianza dominante.

## Audit Call/Fold esatto

| Controllo | Seed 1 | Seed 2 |
| --- | ---: | ---: |
| Righe medie materiali | 170 | 173 |
| Righe correnti materiali | 1 | 1 |
| Correnti con reach propria almeno 1% | 0 | 1 |
| Correnti con reach pubblica almeno 1% | **0** | **0** |
| Righe medie passate all'azione migliore nella corrente | 169 | 172 |

L'audit locale passa. Le frequenze materialmente inferiori appartengono ancora alla media storica
o a nodi senza reach pubblica materiale; la policy corrente non conserva errori Call/Fold sui rami
pubblicamente raggiunti.

## Gate

| Controllo | Esito |
| --- | --- |
| Build e suite HU | PASS |
| Due run complete da 2M | PASS |
| Backend evaluator identico a V13/V15 | PASS |
| Export 20 nodi, 1.620 righe e massa 630 per seed | PASS |
| Normalizzazione massima | PASS, `3,33e-16` |
| Tree fingerprint corrente | PASS |
| Tempo massimo 60 minuti per solve | PASS |
| Audit corrente su reach pubblica almeno 1% | PASS, `0/0` |
| TV media e corrente fra seed non superiore a 5 pp | **FAIL** |
| WMAE non superiore a 5 pp per seed | **FAIL** |

Il confronto Monker resta `REJECTED / REFERENCE_CONFIG_INCOMPLETE`: il contratto postflop esterno
non è noto e non viene usato come segnale di training.

## Artefatti

| Artefatto | SHA-256 |
| --- | --- |
| candidato seed 1 | `B24D3549B262049C88932A6B930035FFDDD1DD084A4FE8E7D38E2E891AACB147` |
| policy seed 1 | `CA2FBBEAE0EEFBDD4C6E552CDC9D5C8D2F35818717DB2DB47B21B89364A22DE7` |
| candidato seed 2 | `5E58E3D89075180CF59A22F36AA87D6D7E7A3D3E005B981CE9C80FD05F042149` |
| policy seed 2 | `690930DC6A61842B3DFEFFAA9A1A82B44397F473187973AC4DE1AF496DA45F59` |
| analisi coppia | `50325A8B939E6E8BBBF981EFA0F815FD00B84F55CCC96E88A1D8996F5EEE12C3` |
| audit policy | `4F2F354714C641522F1FF7E806428C40C28D5FBA889611DE44D19EA09A2C7D7F` |
| decomposizione TV | `E7F1623358B157DD90B3DD22D2E93C95108C3B468488EA8D75E38367366273AA` |

## Decisione

V16 resta una modalità sperimentale disattivata per default. Non entra nel viewer e non sostituisce
V13. La stratificazione della sola prima risposta è respinta come intervento per chiudere il gate
R6.

Il passo successivo non aumenta rollout o iterazioni. Deve rivalutare le azioni contro la policy
corrente completa, anziché combinare frequenze correnti con continuation medie, e aggiungere una
misura interna di best response. Questo separa una mancata convergenza locale da una diversa
selezione fra azioni quasi equivalenti.
