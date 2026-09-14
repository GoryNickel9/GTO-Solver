# R6 — Protocollo V12 con payoff all-in preflop esatto

Data: 2026-09-12  
Stato: protocollo congelato; coppia 2M completata; risultati nel report di gate

## Obiettivo

V12 verifica se l'integrazione esatta dei runout nei terminali all-in preflop riduce
la varianza che lascia azioni localmente dominate nella policy corrente. Il confronto usa V8 come
baseline perché è la coppia 2M con la WMAE più vicina a Monker. Non cambia albero, astrazione
postflop, averaging, parallelismo o action abstraction.

## Modifica isolata

V8 valuta un terminale all-in sul solo board fisico campionato dalla traversata. V12 sostituisce
quel valore con:

```text
EV = P(win) * payoff_win + P(tie) * payoff_tie + P(loss) * payoff_loss
```

Le probabilità provengono dalla tabella esatta per coppia di classi. La tabella integra tutte le
relazioni fisiche dei semi e tutti i runout legali; la classe privata avversaria resta campionata
dal MCCFR. I tre payoff passano da `settle_terminal`, quindi conservano commitment, side pot e
rake del ruleset invece di applicare una correzione successiva.

L'oracolo è attivo solo sui `TerminalAllIn` preflop. Fold e terminali postflop non cambiano.

## Evidenza indipendente

La tabella del solver ha fingerprint `fnv1a64:fe73211ffab94a00` ed è stata confrontata con la
tabella Poker-Quant `preflop_hu_exact_sixplus_v1.tsv`, SHA-256
`2FAD50C45560F78896D509B9887D1E8E91F28AB7DD3B50F89CCB240ABF96A61B`.

Il controllo differenziale ricostruisce `353.430` matchup fisici ordinati e confronta tutte le
`6.561` coppie di classi. Esito: PASS, nessun mismatch di conteggio, errore assoluto massimo di
equity `1,2212453270876722e-15`.

## Configurazione congelata

- `2.000.000` di iterazioni per ciascuno dei due seed pubblicati;
- Linear MCCFR, batch `32`, otto worker e quattro rollout medi per entrambi i traverser;
- V8 street-adaptive, MC8, capacità Flop/Turn/River `32/128/512`;
- albero `fnv1a64:a68337fa567aa2d9`, fixture monetaria v2 e rake disabilitato;
- stessi training, partition ed evaluation seed delle coppie V8/V10/V11.

## Gate

1. Build Release e test HU preflop devono passare; entrambe le run devono dichiarare l'oracolo e
   il fingerprint esatto.
2. L'audit deve coprire tutti i dieci nodi Call/Fold contro all-in e non trovare azioni inferiori
   con frequenza corrente almeno `5%`, gap esatto almeno `0,1a` e reach pubblica almeno `1%`.
3. Ogni export deve contenere 20 nodi, 1.620 righe normalizzate ed EV finiti per ogni azione.
4. WMAE, TV contro Monker, P95 e TV fra seed devono essere confrontate con V8/2M; il confronto
   Monker resta diagnostico perché il contratto postflop esterno è incompleto.
5. Il gate R6 finale resta `WMAE <= 1 pp`, `TV media <= 2 pp`, `P95 <= 5 pp` ed errore root
   aggregato `<= 1 pp`.

### Correzione del 2026-09-12

Il punto 2 richiede che frequenze ed EV appartengano allo stesso profilo: `media contro media`
oppure `corrente contro corrente`. L'audit originario applicava le frequenze correnti a EV
calcolate contro il range medio avversario. Quel controllo ibrido resta una diagnostica, ma non
può determinare il gate della policy corrente.

La rivalutazione corrente contro corrente trova 65/40 casi materiali nei due seed, dei quali
11/14 con reach pubblica almeno `1%`. Il punto 2 è quindi `FAIL`; dettagli in
[R6_POLICY_CONDITIONING_AND_MONKER_ALLIN_DIAGNOSTIC_2026-09-12.md](R6_POLICY_CONDITIONING_AND_MONKER_ALLIN_DIAGNOSTIC_2026-09-12.md).

## Esclusione intenzionale

L'enumerazione degli all-in postflop non appartiene a V12. Verrà profilata e testata separatamente:
dal Flop richiede 406 runout fisici per matchup, dal Turn 28 e dal River un solo showdown. Unirla
a V12 impedirebbe di attribuire l'eventuale miglioramento alla modifica corretta.

Risultati e decisione: [EXACT_ALL_IN_EXPECTATION_V12_GATE_2026-09-12.md](EXACT_ALL_IN_EXPECTATION_V12_GATE_2026-09-12.md).
