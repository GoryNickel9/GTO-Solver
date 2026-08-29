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

Ultima verifica focalizzata (2026-08-14): build Release di `gto_cli` e
`gtosd_gto_plus_reference_tests`, quindi test di riferimento PASS con 24
asserzioni, fallback fisico per range asimmetrici a differenza regret/strategy
zero e root lock esterno PASS. Non è una nuova esecuzione dei 22 test GUI né
della suite CTest completa; il 16/16 Release del 2026-08-13 resta una prova
precedente separata.

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
## Stato packed a 24 bit

I test Release coprono allocazione e certificazione exact dello stato core
`Float13RegretFloat11Strategy`, validazione dei valori finiti e persistenza
autenticata con round-trip byte-for-byte. La suite completa successiva al
relink e cleanup passa 16/16 in 219,94 s. I tre benchmark RAM sono integration gate
separati dalla suite e dai time gate.
