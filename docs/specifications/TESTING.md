# Testing

## Strati

La suite copre:

- unit test di carte, money, range, azioni, rake e settlement;
- evaluator/showdown e casi Short Deck limite;
- tree builder, chance, hash e serialization;
- isomorfismo globale lossless;
- solver laboratory, best response e checkpoint;
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

Il preset GUI Release registra 22 test CTest: E2E Qt/ImGui a più scale DPI,
product E2E, infrastruttura, core, fasi 1-10, riferimento GTO+ e benchmark
smoke. Test lunghi o esaustivi possono avere label/preset separati, ma il report
deve dire con precisione cosa è stato escluso.

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

## F10.4

Il futuro test controlled-posterior dovrà coprire esattamente le 36 combo GTO+
root, azioni e somme, input duplicati/mancanti/bloccati, posteriori bet/check,
isolamento del checkpoint, convergenza del gioco vincolato e nessuna regressione
del percorso standard. Finché tali test non esistono, F10.4 è pianificata.

## Reporting

Un comando interrotto per timeout non viene dichiarato come run completo. È
accettabile completare una suite in segmenti deterministici, riportando
esplicitamente i segmenti e l'esito totale. Test locali, package consumer, E2E
installato e CI remota sono prove diverse e non vanno fuse in un unico “PASS”.
