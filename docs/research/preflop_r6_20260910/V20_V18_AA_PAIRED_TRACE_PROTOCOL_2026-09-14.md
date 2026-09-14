# V20 — Protocollo paired V18 per AA

Data: 2026-09-14  
Stato: `PREREGISTERED BEFORE RUN / RUN COMPLETE`  
Baseline analizzata: `V18 seed 1`

## Domanda

Stabilire se V18 assegna quasi tutta la massa di `AA` a Call mentre Raise 6 produce un EV
maggiore contro la stessa continuation finale. Il viewer riporta, sulla continuation media,
`8,2008a` per Raise 6 e `7,2405a` per Call, ma ciascuna stima usa soltanto 188 deal `AA`.

## Ordine delle verifiche

Il test paired viene prima della NashConv globale. Isola una singola decisione, usa la policy V18
congelata e misura direttamente il guadagno di una deviazione root. Una NashConv esatta richiede
la best response dell'intero gioco astratto; la sua assenza non impedisce di rilevare una scelta
root subottimale. Il run continuerà a serializzare la best response campionata già prevista dal
contratto V18, ma non la presenterà come NashConv certificata.

## Contratto congelato

| Campo | Valore |
| --- | --- |
| Training | Linear MCCFR, 2.000.000 iterazioni |
| Refinement | 2.000.000 iterazioni solo preflop |
| Seed training | `5923736619020283393` |
| Seed partizione | `5923736619020287489` |
| Seed valutazione | `5923736619020279297` |
| Worker e batch | 8 / 32 |
| Root rollout training | 4, CRN root, update simmetrici |
| Astrazione | V8, capacità `32/128/512` |
| All-in | preflop exact; flop e turn exact |
| Trace | `AA`, 10.000 deal condizionati per azione |
| Policy valutate | media e corrente |

La trace forza All-in, Raise 6, Raise 10, Call e Fold sugli stessi deal fisici. Ogni azione parte
dallo stesso seed di continuation. La valutazione avviene dopo il training e non aggiorna regret,
strategy sum o policy.

## Ipotesi e gate

Il confronto primario è `Call − Raise 6` con la continuation media.

- `IC 95% interamente sotto zero`: V18 sceglie Call mentre Raise 6 ha EV maggiore in questo
  profilo congelato.
- `IC 95% contenente zero`: risultato inconclusivo; non si modifica il trainer.
- `IC 95% interamente sopra zero`: il precedente vantaggio puntuale di Raise 6 era rumore.

Come controllo secondario, Call viene confrontato con tutte le altre azioni usando un intervallo
simultaneo Bonferroni al 95% dentro ciascun profilo. Verranno riportati EV, errore standard paired,
rami terminali, reach delle street e bucket con contributo maggiore.

## Limiti

- Il test misura una deviazione root contro continuation congelate; non è una NashConv.
- La continuation usa l'astrazione imperfect-recall V8.
- Lo stesso seed iniziale riduce la varianza, ma azioni diverse possono consumare sequenze casuali
  diverse dopo la biforcazione.
- Monker resta una stima esterna per WMAE, TV e priorità diagnostica. Non entra nel gate interno di
  questo test.

## Artefatti previsti

- `.tmp/v20_v18_seed1_aa10k_candidate.json`
- `.tmp/v20_v18_seed1_aa10k_trace.json`
- `.tmp/v20_v18_seed1_aa10k.log`
- `V20_V18_AA_PAIRED_TRACE_REPORT_2026-09-14.md`

Il protocollo è stato congelato prima dell'avvio. Il risultato e la validazione sono registrati
nel report previsto sopra.
