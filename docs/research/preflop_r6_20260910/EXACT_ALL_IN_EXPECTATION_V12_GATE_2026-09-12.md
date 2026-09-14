# R6 — Gate V12 con payoff all-in preflop esatto

Data: 2026-09-12  
Esito: `ENGINEERING_PASS / PAIRED_2M_PASS / POLICY_CONDITIONING_AUDIT_FAIL / COMPARATIVE_GATE_FAIL / R6_GATE_FAIL`

## Risultato

V12 sostituisce il singolo runout campionato nei terminali all-in preflop con l'attesa esatta
win/tie/loss per coppia di classi. La modifica riduce nettamente la varianza degli EV. Non
migliora però la strategia root media rispetto a V8, non risolve l'inerzia della media CFR e non
supera il controllo coerente `policy corrente contro policy corrente`.

V12 non sostituisce V8 nel viewer e non supera R6.

## Come viene calcolata l'equity

La primitiva di Poker-Quant è mano fisica contro mano fisica. La tabella del solver aggrega tutte
le mani fisiche compatibili nella coppia di classi, per esempio `K7o` contro `AA`, mantenendo il
card removal e tutte le relazioni di semi. Durante il MCCFR la classe avversaria resta campionata;
il range nasce dalla distribuzione dei deal e dalle reach probability delle azioni.

L'audit locale è invece classe contro range raggiunto: somma gli esiti esatti contro ogni classe
avversaria, pesati per reach e numero di matchup fisici compatibili.

Per ogni terminale V12 usa:

```text
EV = P(win) * payoff_win + P(tie) * payoff_tie + P(loss) * payoff_loss
```

`settle_terminal` calcola i tre payoff. Fold resta uguale al denaro già investito con segno
negativo. L'equity non viene trasformata direttamente in frequenza: regret matching aggiorna la
policy corrente; la strategia pubblicata è la media lineare realization-weighted delle policy.

## Implementazione

- `HuPreflopAllInTrainingOracle`: matrice compatta `81×81` con probabilità esatte di vittoria e
  pareggio;
- `make_hu_preflop_all_in_training_oracle`: conversione verificata dai conteggi interi della
  tabella all-in;
- `exact_preflop_all_in_payoff`: attesa dei tre settlement nei percorsi sequenziale, parallelo e
  di valutazione;
- `--exact-preflop-all-in-expectation`: opzione esplicita del runner; default di produzione
  invariato;
- metadati JSON: flag, fingerprint della tabella e tempo separato di costruzione dell'oracolo.

## Verifica dell'equity

| Controllo | Risultato |
| --- | ---: |
| Board canonici interni | 19.998 |
| Board fisici non ordinati rappresentati | 376.992 |
| Esiti interni rappresentati | 1.423.446.393.600 |
| Matchup canonici Poker-Quant | 10.215 |
| Matchup fisici ordinati ricostruiti | 353.430 |
| Coppie di classi confrontate | 6.561/6.561 |
| Errore assoluto massimo di equity | `1,2212453270876722e-15` |
| Mismatch di conteggio | 0 |

La tabella Poker-Quant ha SHA-256
`2FAD50C45560F78896D509B9887D1E8E91F28AB7DD3B50F89CCB240ABF96A61B`; la tabella interna ha
fingerprint `fnv1a64:fe73211ffab94a00`. Poker-Quant resta un oracle differenziale esterno, non una
dipendenza runtime: la sua DLL restituisce `50/50` in caso di eccezione, mentre il solver deve
propagare un errore esplicito.

## Coppia 2M

| Metrica | Seed 1 | Seed 2 | Media/coppia |
| --- | ---: | ---: | ---: |
| Iterazioni | 2.000.000 | 2.000.000 | — |
| Costruzione oracolo | 63,63 s | 60,60 s | 62,12 s |
| Solve | 1.673,79 s | 1.570,07 s | 1.621,93 s |
| WMAE contro Monker | 15,2415 pp | 15,7204 pp | **15,4810 pp** |
| TV contro Monker | 38,1038 pp | 39,3009 pp | **38,7024 pp** |
| P95 TV | 90,8002 pp | 95,7295 pp | **93,2648 pp** |
| TV fra seed | — | — | **17,3695 pp** |
| Root EV ± SE | −0,1083 ± 0,0634a | −0,0729 ± 0,0671a | — |
| Infoset | 1.556.453 | 1.554.321 | — |
| Payload numerico | 238.678.416 B | 237.893.616 B | — |

V8/2M resta migliore: WMAE media `14,6858 pp` e TV fra seed `16,9686 pp`. V12 peggiora le due
metriche rispettivamente di `0,7951 pp` e `0,4009 pp`. La P95 media migliora di `1,2867 pp`, ma
non compensa il peggioramento delle metriche primarie.

## Audit locale Call/Fold contro il range medio

Il controllo enumera tutti i dieci nodi preflop in cui un giocatore risponde a un all-in. Una riga
materiale assegna almeno il `5%` all'azione esattamente inferiore con gap almeno `0,1a`.

| Controllo | V8 seed 1 | V12 seed 1 | V12 seed 2 |
| --- | ---: | ---: | ---: |
| Righe materiali nella strategia media | 207 | 154 | 158 |
| Frequenza corrente materiale contro range medio | 26 | 3 | 0 |
| Frequenza corrente materiale contro range medio, reach pubblica ≥1% | 3 | **0** | **0** |
| Media che passa all'azione migliore nella corrente | 189 | 151 | 158 |

Questo controllo passa soltanto nel contratto ibrido usato dall'audit originario: frequenza
corrente contro EV derivate dal range medio avversario. Non è un test `corrente contro corrente`
e non può qualificare la policy corrente finale.

La strategia media conserva però decisioni storiche inferiori anche su rami raggiunti. Per esempio,
nel seed 1 `QJo` al nodo 28 mantiene il `35,47%` di Call con gap esatto `0,5250a` e reach pubblica
`7,59%`, mentre la policy corrente chiama `0%`. L'equity esatta corregge il regret finale, non
cancella retroattivamente la massa accumulata dalla media CFR.

### Correzione del contratto di policy

L'audit coerente ricostruisce anche il range avversario dalla policy corrente finale. Copre le
stesse 810 righe per seed e usa la stessa equity esatta.

| Controllo corrente contro corrente | Seed 1 | Seed 2 |
| --- | ---: | ---: |
| Righe materiali | **65** | **40** |
| Materiali con reach propria ≥1% | **20** | **18** |
| Materiali con reach pubblica ≥1% | **11** | **14** |
| Perdita locale media | `0,21153a` | `0,15678a` |

Il sub-gate corrente è quindi `FAIL`. La policy istantanea può oscillare e non sostituisce la
strategia media CFR; questi numeri provano comunque che a 2M non è localmente stabile. Metodo,
caso Q8s e risultati completi sono documentati in
[R6_POLICY_CONDITIONING_AND_MONKER_ALLIN_DIAGNOSTIC_2026-09-12.md](R6_POLICY_CONDITIONING_AND_MONKER_ALLIN_DIAGNOSTIC_2026-09-12.md).

## Validazione

| Gate | Esito |
| --- | --- |
| Build Release dei target modificati | PASS |
| Test HU preflop | PASS, 15.915 assertion |
| Differential test Poker-Quant | PASS |
| Due run complete da 2M | PASS |
| Flag e fingerprint dell'oracolo | PASS per entrambi i seed |
| Export completi | PASS, 20 nodi, 1.620 righe e 4.617 EV per seed |
| Viewer locale | PASS, V12 seed 1 e seed 2 selezionabili; entrambe marcate 2M |
| Errore massimo di normalizzazione | `3,33e-16` |
| Audit ibrido: frequenza corrente contro range medio | PASS, zero errori materiali raggiunti |
| Audit coerente: policy corrente contro policy corrente | **FAIL, 11/14 errori materiali raggiunti** |
| Gate comparativo con V8 | FAIL |
| Gate finale R6 | FAIL |

## Artefatti

| Artefatto | Dimensione | SHA-256 |
| --- | ---: | --- |
| candidato seed 1 | 2.930.102 B | `3E62DE9771071EAD3B7104042032A72A176DDF480F2713633EA606072B5B215F` |
| candidato seed 2 | 2.930.916 B | `67A0E4531E612245C4E00B7A422D2055CC73FDA5B984705AEEA7DDA1E8DAEEE9` |
| policy seed 1 | 368.629.063 B | `A396A924BED007F582833368B24ACA138B57BE884346AD9D794A55E1C3DB86A1` |
| policy seed 2 | 368.113.855 B | `5FE29ABF18DA60212DD01744087CB729ACD4ABF47C90ABBDA6D435DDBFF1164A` |

Gli altri artefatti sono `v12_exact_allin_2m_pair_policy_audit_v1.json`, i due report
`v12_exact_allin_2m_seed*_training_diagnostic_v1.json` e
`poker_quant_exact_all_in_equity_differential_v1.json`.

Il viewer pubblica V12 senza promuoverla: V8 resta la sorgente predefinita in base alla WMAE media
della coppia 2M. Per V12 si può alternare fra strategia media CFR e policy corrente finale. In
modalità corrente, le frequenze provengono dall'ultima policy; gli EV azione restano le stime contro
le continuazioni medie esportate e l'interfaccia dichiara questo limite.

## Decisione

L'attesa esatta dell'all-in preflop è corretta e utile: resta disponibile come opzione di ricerca e
come base per ridurre la varianza. V12 non viene promossa perché la strategia media root peggiora,
la media CFR continua a mostrare azioni storiche inferiori e la policy corrente coerentemente
valutata non supera il gate locale.

Il prossimo esperimento deve restare isolato. Prima di un'altra coppia 2M occorre profilare
l'enumerazione all-in postflop: 406 runout dal Flop, 28 dal Turn e uno showdown dal River. Il gate
deciderà se abilitarla su Flop+Turn o soltanto sul Turn; nessuna run ridotta verrà pubblicata come
versione.
