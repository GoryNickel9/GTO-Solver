# Amendment R0 — ante, button blind e comparabilità postflop

## Stato

`SUPERSEDED` dall'amendment monetario dell'11 settembre 2026. L'affermazione precedente secondo cui il chiarimento non cambiava i payoff era errata.

## Contratto corretto

- CO versa `1a` di ante.
- BTN versa `1a` di ante e `1a` di button blind, per `2a` totali.
- Il pot root è `3a`.
- Le due ante sono denaro morto: `initial_pot=2a`, una ante attribuita a ogni giocatore.
- Il solo button blind è live: CO `0a`, BTN `1a`; il CO paga `1a` per chiamarlo.
- I target `6a`, `10a`, `10,5a` e `14,5a` indicano commitment live e non includono l'ante.
- Un raise live a `6a` corrisponde quindi a `7a` investite in totale; il relativo fold EV è `-7a`.
- L'albero preflop esterno è confermato; l'albero postflop Monker è `AWAITING_INFORMATION`.
- L'all-in è sempre disponibile nel postflop Monker.

La fixture distingue ora `target_live_commitment_ante` da `target_total_contribution_ante`. Per il call root i valori sono rispettivamente `1a` e `2a`; per il raise a `6a` sono `6a` e `7a`.

## Dati Monker non disponibili

Restano sconosciuti le size non all-in dell'albero postflop, metodo e quantità dei bucket per street, abstraction dipendenti dalla history, versione della build modificata, iterazioni, stopping rule, metrica di convergenza, tempo esatto, CPU e RAM. L'ipotesi di stopping all'1% non è trattata come dato osservato.

## Identità corrente

- Fingerprint del riferimento: `fnv1a64:f78898249087ee6d`.
- Fingerprint dell'albero locale: `fnv1a64:a68337fa567aa2d9`.
- Tutti i solve prodotti con l'albero `fnv1a64:0f9919d7d6030cf0` sono obsoleti e non possono superare un gate corrente.
- Frequenze Monker, EV Monker e soglie numeriche restano dati sorgente invariati; cambia il contratto con cui vengono interpretate le size e i payoff.

## Validazione

Il validatore JSON verifica ante, blind, commitment live, contributo totale e call incrementale. Il preflight C++ ricostruisce lo stato root e rifiuta l'unione fra ante morte e blind live.

```text
validate_hu_preflop_r0.mjs              PASS
gtosd_hu_preflop_tests co40_monetary_ledger  PASS, 295 assertion
gtosd_hu_preflop_tests co40_tree_contract    PASS, 25 assertion
gtosd_hu_preflop_reference --preflight-only  PASS
validate_hu_preflop_r0.mjs                    PASS
```

La preflight conferma 81 classi, 630 combo fisiche, cinque azioni root, EV CO `-0,3a` e gli stessi marginali esterni. I report R5/R6 precedenti restano evidenza storica del contratto errato. I quattro solve V6/V7 a 2M sono stati rigenerati sul contratto v2 e sostituiscono i vecchi dataset nel chart viewer.
