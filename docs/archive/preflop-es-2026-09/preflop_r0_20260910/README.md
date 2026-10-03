# R0: contratto HU preflop CO40

Stato del gate: **PASS**, con la riconciliazione del fingerprint storico annotata sotto.

Questo registro congela il working tree dirty del 10 settembre 2026 per le fasi R0–R1. Non certifica performance, convergenza, NashConv o indipendenza dal solver postflop. Non sono stati eseguiti run di solving lunghi.

## Identità iniziale congelata

- Commit Git: `ffb208a0dad018dc537288c42239322ff7dfde6d`.
- Working tree: dirty; 35 file tracciati modificati e 3.894 file non tracciati al momento della registrazione, inclusi i cinque artefatti R0 e i file diagnostici già presenti.
- Fixture gioco: `benchmarks/fixtures/hu_preflop_co40_game_v1.json`, SHA-256 `87af6c5d763baf0862c33e385e576f1925e3340b2a55038a1b9a66f141b8bc73`.
- Fixture riferimento: `benchmarks/fixtures/hu_preflop_co40_reference_v1.json`, SHA-256 `5e8aae5be43d4a4a19ed95cff2b435550dcb01b4efa74338309058e8cff39eb1`.
- Tree Release registrato nello snapshot R0: `fnv1a64:0f9919d7d6030cf0`.
- Decomposizione corrente: `fnv1a64:fd74e36ab417d1da`.
- Fingerprint storico riportato nel documento benchmark: `fnv1a64:c87905ceb69551a1`. Non viene riutilizzato come identità del tree corrente.

L'amendment corrente usa fixture gioco SHA-256 `5BA2FC78567CFD109B34BFDC9A208D13BCD08B99CDBB986FE155E407879E0619`, fixture riferimento SHA-256 `D835898479493B24FAC3CE2686B43119CBBED40B5CEF40A01E25BC62B2DBE811` e albero `fnv1a64:a68337fa567aa2d9`.

## Contratto verificato

La configurazione JSON passa il validatore R0 con `monetary_contract_revision=2`. La preflight conferma 81 classi exact, 630 combo fisiche, 5 azioni, EV root CO `-0,3a`, rake disabilitata e fingerprint del riferimento `fnv1a64:f78898249087ee6d`. Il manifest iniziale resta una fotografia storica; [CONTRACT_AMENDMENT_2026-09-10.md](CONTRACT_AMENDMENT_2026-09-10.md) documenta la correzione dell'11 settembre.

Il contratto congelato è: Short Deck HU 36 carte; stack effettivo 40 ante; una ante morta per giocatore; button blind live `1a`; call root incrementale `1a`; target live `6a`, `10a`, `10,5a`, `14,5a` e all-in live `39a`. Le due re-raise a `10,5a` e `14,5a` sono eccezioni incomplete esplicite. La rake è zero. Il candidato locale usa postflop `33%/66%/120%/all-in`; l'equivalenza dell'albero postflop Monker resta sconosciuta.

Le righe della strategia esterna restano dati arrotondati interi. Le 14 classi con totale `101%` sono `JJ`, `TT`, `88`, `77`, `K9s`, `K8s`, `K7s`, `QJs`, `Q9s`, `Q8s`, `J8s`, `T7s`, `QTo`, `98o`. Il confronto normalizza ogni riga dividendo per il totale riportato e non riscrive i valori sorgente.

## Ambiente e build

- CPU: Intel Core i3-10100F, 4 core fisici, 8 processori logici.
- Memoria registrata: 34.294.738.944 byte fisici totali, 14.350.233.600 byte disponibili nel rilevamento R0.
- Build: `out/build/codex-release-20260907`, CMake/Ninja, Release, MSVC 19.51.36248.0 x64, `/std:c++20 /EHsc /O2 /Ob2 /DNDEBUG /MD /W4 /permissive-`.
- Ricostruzione target: `gtosd_hu_preflop_reference` e `gtosd_hu_preflop_tree`, con `VsDevCmd.bat -arch=x64`; esito `0`.
- Verifica tree: 58 nodi, 20 decisioni, 9 ingressi postflop, 19 fold terminali, 10 all-in terminali, profondità 15, massimo 4 raise per street, terminazione naturale `PROVEN`.

Il primo tentativo senza ambiente MSVC ha fallito su `type_traits`; non è un errore del codice. La ricostruzione con l’ambiente corretto ha superato il gate dei target R0.

## Inventario e protezioni

Il dettaglio riproducibile è in `manifest.json`. Sono stati conservati senza reset, clean o sovrascrittura `.reasonix/`, `.tmp/`, `benchmark GTO+/`, `benchmarks/results/`, il corpus di ricerca precedente e tutti i file non tracciati già presenti.

Per R1 restano protetti da modifiche ordinarie `libs/postflop/`, `libs/postflop_subgame/`, `include/gtosd/postflop/`, i default del solver postflop in `libs/solver/src/solver.cpp` e i benchmark legacy di decomposizione. La lista non implica che le modifiche preesistenti dell’utente siano state rimosse: alcune di queste aree erano già dirty prima di R0.

## Comandi di riproduzione

```text
node tools/validate_hu_preflop_r0.mjs --game benchmarks/fixtures/hu_preflop_co40_game_v1.json --reference benchmarks/fixtures/hu_preflop_co40_reference_v1.json
out/build/codex-release-20260907/benchmarks/gtosd_hu_preflop_reference.exe --fixture benchmarks/fixtures/hu_preflop_co40_reference_v1.json --preflight-only
out/build/codex-release-20260907/benchmarks/gtosd_hu_preflop_tree.exe
```

Il prossimo gate è R1: isolare il nuovo trainer e il suo target minimo dagli header e dalla libreria del solver postflop standalone, mantenendo separato l’adapter legacy.
