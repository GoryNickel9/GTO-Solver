# Testing

Il backend sottoposto a unit, integration, differential e benchmark test è
esclusivamente CPU/RAM. I test grafici possono usare una GPU per il rendering,
ma non costituiscono né attivano un percorso di solving GPU.

## Strati

La suite copre:

- unit test di carte, money, range, azioni, rake e settlement;
- evaluator/showdown e casi Short Deck limite;
- tree builder, chance, hash e serialization;
- isomorfismo globale lossless;
- solver laboratory, best response e checkpoint;
- card abstraction, feature exact postflop, lift e CFR+ bucketed;
- subgame frontier, reach, infoset closure, guard/fallback e composizione
  bucketed con certificazione sul gioco esatto;
- memoria e postflop exact;
- storage autenticato e migrazioni;
- GUI prototype/product E2E;
- riferimento e benchmark GTO+.

I bug matematici producono test permanenti. La parità interna non basta se i due
percorsi condividono la stessa formula sbagliata: servono oracle o invarianti
indipendenti.

## Preset canonici Windows

La build completa desktop usa l'ambiente Visual Studio Developer Command:

```powershell
cmake --preset windows-gui-release
cmake --build --preset windows-gui-release
ctest --test-dir out/build/windows-gui-release --output-on-failure
cmake --build --preset windows-gui-release --target format-check
```

Altri preset coprono Debug, Release senza GUI, ASan e nightly. L'assenza degli
header standard in MSVC è un problema di ambiente, non una regressione del
codice: i gate Windows devono essere eseguiti nel developer environment.

## Suite corrente

La suite Release corrente registra 31 test CTest: infrastruttura, core, fasi
1-10, contratto `production_dcfr`, riferimento GTO+, ledger solver-owned,
layout canonico, oracoli (incluso il recheck Pure/Sync-PCFR), CardAbstraction,
SubgameSolver e benchmark smoke. Test GUI, sanitizer, nightly o altri preset
sono prove separate e il report deve dire con precisione cosa è stato escluso.

Ultima verifica completa (2026-09-04): Release `31/31 PASS` in `185,16 s`.
Sono inoltre passati il resume production byte-equivalent attraverso il reset
finale, il contratto delle tre fixture e cinque processi final-head con `15/15`
solve target-driven corretti. Questa evidenza non viene presentata come nuova
esecuzione dei preset GUI o sanitizer.

## Invarianti obbligatori

- nessuna carta duplicata;
- azioni solo legali e stack mai negativo;
- conservazione chip prima del rake;
- distribuzioni non negative e normalizzate;
- determinismo con input/seed uguali;
- chance mass e blocker corretti;
- physical tree e canonical path equivalenti;
- continuous solve e resume equivalenti;
- storage corrotto rifiutato senza crash;
- nessun NaN o infinito.
- una solve core senza iteration limit richiede un target e termina solo alla
  prima certificazione che soddisfa il confronto configurato;
- il rounding aggressivo opera sul target totale impegnato, è serializzabile e
  non dipende dall'identificatore del benchmark.

## F10.4 — root lock diagnostico

Il test `EXTERNAL_ROOT_LOCK_TEST` copre esattamente le 36 combo GTO+ root,
azioni e somme, input duplicati/mancanti/bloccati, posteriori bet/check,
fingerprint invariato, convergenza del gioco vincolato e assenza di regressioni
nel percorso standard. F10.4 è completata come diagnostica test-only; non certifica
il node locking globale di prodotto.

## Reporting

Un comando interrotto per timeout non viene dichiarato come run completo. È
accettabile completare una suite in segmenti deterministici, riportando
esplicitamente i segmenti e l'esito totale. Test locali, package consumer, E2E
installato e CI remota sono prove diverse e non vanno fuse in un unico “PASS”.
## Stato production node-scaled

I test Release coprono allocazione, update e certificazione dello stato core
`ScaledUint16RegretStrategy`, validazione dei valori finiti, scale per decision
node, resume continuo/segmentato byte-equivalent e persistenza autenticata con
round-trip byte-for-byte. La suite corrente passa 31/31. I tre benchmark RAM
restano integration gate separati dalla suite e dai time gate.
