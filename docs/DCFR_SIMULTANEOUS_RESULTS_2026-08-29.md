# DCFR alternating vs simultaneous - risultati 2026-08-29

## Decisione

**REJECT.** Il percorso simultaneous corretto semanticamente non supera il primo
gate prestazionale su TH7D6S e TSTC9D. La traversata per iterazione e' piu lenta
del 95,21% su TH7D6S e del 65,72% su TSTC9D; anche la curva dEV/tempo e' peggiore.
Come prescritto dal piano, i full lunghi e la certificazione a cinque processi non
sono stati eseguiti.

## Provenienza e configurazione

- HEAD iniziale: `7284f3dce57bdbe0a1f075169080fa26777f21fd`.
- HEAD finale del codice sperimentale: `f89270a` (il commit che contiene questo
  report e' intenzionalmente successivo e non puo' auto-referenziare il proprio
  hash).
- Build: MSVC Release, directory `out/build/windows-release-current`.
- Fixture ufficiali: `GTP-AHKHQH-003`, `GTP-TH7D6S-101`,
  `GTP-TSTC9D-101`.
- L'unica differenza algoritmica nei confronti e' `alternating` contro
  `GTOSD_DIAGNOSTIC_SIMULTANEOUS=1`; fixture, backend, precisione, thread,
  isomorfismi e parametri DCFR sono invariati.
- I run corti sono diagnostici a iterazioni fisse. Una mancata `correctness` del
  report indica che il target finale della fixture non e' stato raggiunto, non un
  errore di esecuzione.

## SIMULTANEOUS_SEMANTICS_AUDIT

1. Entrambi gli update usano la strategia corrente caricata una sola volta nel
   decision node, prima della ricorsione sui figli.
2. I CFV dei due player sono prodotti nello stesso traversal pubblico; il regret
   del solo actor del nodo viene applicato postorder, dopo tutti i figli.
3. L'average strategy viene aggiornata una volta per actor e iterazione, con il
   reach dell'actor e con lo stesso peso DCFR usato dal percorso production.
4. Gli scratch buffer sono riutilizzati tramite lease locali al traversal; non
   esiste un regret buffer condiviso fra i due player.
5. Un terminale pubblico viene visitato una volta, ma non viene calcolato una
   volta sola: il codice invoca ancora un payoff kernel separato per ciascun
   player.
6. Di conseguenza i due output CFV non sono fusi nello stesso kernel terminale.
   La telemetria conta separatamente visita pubblica e due valutazioni terminali.
7. Prima della correzione il backend `ScaledUint16RegretStrategy` non era
   semanticamente valido: usava clipping CFR+ su codici signed e saltava i
   discount DCFR. Ora usa lo stesso updater signed del percorso production,
   applicando positive/negative discount, regret weight e average weight una
   sola volta.
8. Limiti: solo DAG pubblico canonico; il root lock con backend signed resta non
   supportato; scheduler e kernel terminali non sono stati modificati.

## Correzioni e test

- Corretto l'update signed DCFR del traversal simultaneous.
- Aggiunti work counter completi per decision, chance, terminali, regret e
  strategy update.
- Corretto `update_mode` nel JSON del benchmark.
- Aggiunti override diagnostici validati per limite iterazioni, intervallo di
  certificazione e run a iterazioni fisse.
- Aggiunto un test production con nodi a 2 e 3 azioni, fold, showdown, chance,
  regret negativi signed, average manuale alla prima iterazione, resume 1->3,
  canonicalizzazione e range asimmetrici.

Validazione eseguita:

```text
ctest --test-dir out/build/windows-release-current -C Release --output-on-failure
100% tests passed, 19/19, 196.73 s
```

## Risultati sintetici

Nessun simultaneous ha raggiunto `<1%` nei checkpoint previsti. AHK alternating
ha raggiunto 0,951640% a 60 iterazioni e 0,756197% a 80; questo non cambia il
gate, che richiede un vantaggio simultaneous su TH e TST.

| Benchmark | Mode | Iter <1% | Traversal (s) | Cert (s) | Elapsed (s) | dEV finale | Root EV fixture (ante) | Peak RSS (B) |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| AHKHQH | alternating | 60 | 0.842 | 0.161 | 1.024 | 0.756197% @80 | 19.401659 | 167,211,008 |
| AHKHQH | simultaneous | non raggiunto | 1.343 | 0.160 | 1.521 | 4.303931% @80 | 19.769957 | 166,948,864 |
| TH7D6S | alternating | non raggiunto | 34.184 | 5.548 | 40.467 | 11.717920% @100 | 8.199081 | 711,262,208 |
| TH7D6S | simultaneous | non raggiunto | 66.731 | 5.140 | 72.583 | 4.382883% @100 | 8.194648 | 711,561,216 |
| TSTC9D | alternating | non raggiunto | 124.956 | 43.699 | 169.151 | 1.685303% @120 | 8.476903 | 1,969,549,312 |
| TSTC9D | simultaneous | non raggiunto | 207.082 | 43.005 | 250.499 | 8.333544% @120 | 8.371643 | 1,969,737,728 |

`Root EV fixture` e' il valore del controllo GTO+ del report finale. Nelle tabelle
di checkpoint `Root profile` e' invece `profile_value_co_antes`, la utility netta
del profilo certificato.

## Curve dEV per iterazione e tempo

TH7D6S usa `averaging_delay=40`: a 20 e 40 iterazioni la strategy sum e' vuota
e la certificazione usa il fallback uniforme. Quei due punti, uguali a 149,425%,
sono riportati per completezza ma non descrivono la convergenza dopo l'avvio
dell'averaging.

| Benchmark | Iter | Alt dEV | Sim dEV | Alt solver (s) | Sim solver (s) | Alt nodes | Sim nodes |
|---|---:|---:|---:|---:|---:|---:|---:|
| AHKHQH | 20 | 5.255564% | 18.141830% | 0.254 | 0.341 | 939,337 | 921,300 |
| AHKHQH | 40 | 1.451943% | 9.997276% | 0.507 | 0.724 | 1,905,416 | 1,842,600 |
| AHKHQH | 60 | 0.951640% | 6.516300% | 0.765 | 1.103 | 2,853,818 | 2,763,900 |
| AHKHQH | 80 | 0.756197% | 4.303931% | 1.019 | 1.515 | 3,790,459 | 3,685,200 |
| TH7D6S | 20 | 149.424740% | 149.424740% | 7.058 | 13.402 | 7,157,978 | 7,576,680 |
| TH7D6S | 40 | 149.424740% | 149.424740% | 12.316 | 25.207 | 14,580,928 | 15,153,360 |
| TH7D6S | 60 | 13.727450% | 12.580360% | 20.540 | 38.265 | 21,975,508 | 22,730,040 |
| TH7D6S | 80 | 12.187675% | 5.832578% | 30.624 | 55.272 | 29,263,747 | 30,306,720 |
| TH7D6S | 100 | 11.717920% | 4.382883% | 40.115 | 72.230 | 36,517,704 | 37,883,400 |
| TSTC9D | 20 | 16.817241% | 55.585821% | 25.931 | 41.398 | 29,490,340 | 35,172,480 |
| TSTC9D | 40 | 7.676917% | 23.584481% | 53.506 | 83.766 | 60,881,457 | 70,344,960 |
| TSTC9D | 80 | 3.265924% | 12.051265% | 109.762 | 166.911 | 123,048,668 | 140,689,920 |
| TSTC9D | 120 | 1.685303% | 8.333544% | 169.150 | 250.497 | 184,421,319 | 211,034,880 |

La curva dEV/tempo non supporta una proiezione favorevole. Su TSTC9D, a circa
167 s il simultaneous e' ancora a 12,05%, mentre alternating a circa 169 s e'
gia' a 1,685%. Su TH7D6S il simultaneous migliora la convergenza per iterazione
dopo l'averaging, ma a 38,27 s e' a 12,58%, sostanzialmente peggio del 11,72%
raggiunto da alternating in 40,11 s. Non esiste quindi il vantaggio dEV/time
richiesto come alternativa al gate di traversal.

## Costo e lavoro per iterazione

I valori usano l'ultimo run corto di ogni fixture. Uno speedup negativo indica
una regressione.

| Benchmark | Alt ms/iter | Sim ms/iter | Speedup | Alt nodes/iter | Sim nodes/iter |
|---|---:|---:|---:|---:|---:|
| AHKHQH | 10.531 | 16.782 | -59.36% | 47,381 | 46,065 |
| TH7D6S | 341.842 | 667.314 | -95.21% | 365,177 | 378,834 |
| TSTC9D | 1,041.298 | 1,725.681 | -65.72% | 1,536,844 | 1,758,624 |

| Benchmark | Mode | Decision/iter | Chance outcome/iter | Showdown/iter | Regret entries/iter | Strategy entries/iter |
|---|---|---:|---:|---:|---:|---:|
| AHKHQH | alternating | 33,813 | 19,744 | 12,531 | 1,168,664 | 1,033,966 |
| AHKHQH | simultaneous | 18,438 | 10,245 | 33,174 | 1,288,290 | 1,127,254 |
| TH7D6S | alternating | 275,322 | 83,278 | 85,414 | 76,153,744 | 45,784,087 |
| TH7D6S | simultaneous | 147,256 | 43,593 | 272,448 | 83,318,592 | 49,991,155 |
| TSTC9D | alternating | 1,174,841 | 386,200 | 340,532 | 339,707,774 | 339,707,774 |
| TSTC9D | simultaneous | 630,596 | 203,555 | 1,284,176 | 366,890,152 | 366,890,152 |

Il simultaneous dimezza circa decision e chance outcome, ma valuta il payoff di
entrambi i player separatamente a ogni terminale non potato. L'aumento delle
valutazioni showdown e degli update entry, insieme alla perdita del pruning
player-specifico, supera il risparmio di traversal pubblico.

## Certificazione dei checkpoint

| Benchmark | Mode | Iter | NashConv | Root profile | BR CO | BR BTN | Peak RSS (B) | State (B) |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| AHKHQH | alt | 20 | 0.089416 | -1.076407 | 1.025818 | 2.550836 | 167,211,008 | 3,864,870 |
| AHKHQH | alt | 40 | 0.028639 | -0.901414 | -0.320637 | 1.466427 | 167,211,008 | 3,864,870 |
| AHKHQH | alt | 60 | 0.017669 | -0.885097 | -0.558997 | 1.265642 | 167,211,008 | 3,864,870 |
| AHKHQH | alt | 80 | 0.013531 | -0.875982 | -0.637217 | 1.177968 | 167,211,008 | 3,864,870 |
| AHKHQH | sim | 20 | 0.359824 | -0.505846 | 6.630390 | 7.762578 | 166,948,864 | 3,864,870 |
| AHKHQH | sim | 40 | 0.181089 | -0.894542 | 2.350118 | 4.893452 | 166,948,864 | 3,864,870 |
| AHKHQH | sim | 60 | 0.129786 | -1.061533 | 1.523374 | 3.668053 | 166,948,864 | 3,864,870 |
| AHKHQH | sim | 80 | 0.083734 | -0.942009 | 0.779563 | 2.569798 | 166,948,864 | 3,864,870 |
| TH7D6S | alt | 20 | 2.628459 | 1.475486 | 23.025506 | 26.915215 | 711,475,200 | 249,955,776 |
| TH7D6S | alt | 40 | 2.628459 | 1.475486 | 23.025506 | 26.915215 | 711,409,664 | 249,955,776 |
| TH7D6S | alt | 60 | 0.153543 | -1.301459 | 1.306757 | 1.610558 | 711,135,232 | 249,955,776 |
| TH7D6S | alt | 80 | 0.132691 | -1.279564 | 1.036094 | 1.485033 | 711,135,232 | 249,955,776 |
| TH7D6S | alt | 100 | 0.124587 | -1.276157 | 0.950247 | 1.416913 | 711,262,208 | 249,955,776 |
| TH7D6S | sim | 20 | 2.628459 | 1.475486 | 23.025506 | 26.915215 | 711,380,992 | 249,955,776 |
| TH7D6S | sim | 40 | 2.628459 | 1.475486 | 23.025506 | 26.915215 | 711,188,480 | 249,955,776 |
| TH7D6S | sim | 60 | 0.220943 | -1.429260 | 0.961008 | 3.236905 | 711,561,216 | 249,955,776 |
| TH7D6S | sim | 80 | 0.115175 | -1.273419 | -0.165229 | 2.353562 | 711,561,216 | 249,955,776 |
| TH7D6S | sim | 100 | 0.080280 | -1.275669 | -0.583098 | 2.108416 | 711,561,216 | 249,955,776 |
| TSTC9D | alt | 20 | 0.325841 | 0.445936 | 2.968639 | 2.244823 | 1,969,299,456 | 1,472,605,376 |
| TSTC9D | alt | 40 | 0.131839 | 0.372969 | 1.601275 | 0.508153 | 1,969,299,456 | 1,472,605,376 |
| TSTC9D | alt | 80 | 0.049602 | 0.442153 | 0.964701 | -0.171074 | 1,969,311,744 | 1,472,605,376 |
| TSTC9D | alt | 120 | 0.026284 | 0.476903 | 0.746552 | -0.326002 | 1,969,549,312 | 1,472,605,376 |
| TSTC9D | sim | 20 | 1.065302 | 0.570760 | 8.721865 | 8.322972 | 1,969,500,160 | 1,472,605,376 |
| TSTC9D | sim | 40 | 0.455172 | 0.536617 | 4.310134 | 2.972616 | 1,969,500,160 | 1,472,605,376 |
| TSTC9D | sim | 80 | 0.238195 | 0.391588 | 2.274506 | 1.536615 | 1,969,696,768 | 1,472,605,376 |
| TSTC9D | sim | 120 | 0.158937 | 0.371643 | 1.705010 | 0.837981 | 1,969,737,728 | 1,472,605,376 |

Tutti i valori sono finiti e l'exact best response e' stata eseguita a ogni
checkpoint. Il gate RAM TSTC9D passa in entrambi i modi (`<2,000,000,000 B`) e
`solver_state_bytes` e' invariato; il margine simultaneous e' pero' soltanto
30,262,272 B.

## Full run e correctness finale

- Full TH7D6S: **non eseguito**, per fallimento del primo gate.
- Full TSTC9D: **non eseguito**, per regressione strutturale gia' dimostrata su
  TH e confermata dal corto TST.
- Cinque processi indipendenti: **non eseguiti**, perche' il candidato non ha
  raggiunto il gate `<=128.988889 s` ne' il target exact `<1%`.
- Nessuna tolleranza e' stata ampliata. Fixture fingerprint, layout, state size
  e hard gate RAM sono rimasti quelli ufficiali.

Il percorso rimane dietro `GTOSD_DIAGNOSTIC_SIMULTANEOUS`; non viene promosso a
production. Le correzioni semantiche e la copertura di test vengono conservate
per impedire che il percorso diagnostico produca risultati DCFR signed errati.

## Collo di bottiglia e passo successivo

Il risparmio sul DAG pubblico non compensa i payoff terminali duplicati e il
lavoro sulle due reach. Il prossimo esperimento ad alta priorita' e' una roadmap
separata per **batched/wavefront showdown evaluation per board canonica**, con
vettorizzazione across terminals. Non va riaperta la serie di micro-ottimizzazioni
rank/prefix gia' respinte e non va modificato lo scheduler prima di una nuova
misurazione mirata.
