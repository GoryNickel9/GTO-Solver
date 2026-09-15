# V23 — Report NashConv astratta HU10

Data: 2026-09-15
Stato: `CERTIFIED_ABSTRACT_EMPIRICAL_CHANCE_TARGET_MET`

## Risultato

Il solver ha prodotto il primo certificato NashConv whole-game per il gioco HU10 V23. Il
certificatore calcola profile EV e due best response esatte sullo stesso `FiniteGame` usato da
Linear MCCFR.

A 5.000.000 iterazioni il massimo guadagno unilaterale normalizzato è `0,0081173483`, sotto il
target preregistrato `0,01`. Il primo checkpoint sotto soglia è quello da 4.500.000 iterazioni.

Il risultato non è una NashConv fisica. È esatto per un corpus empirico congelato di otto deal
per ciascuna delle 81 classi CO.

La sensibilità successiva K=2/4/8 raggiunge il target interno su tutti e tre i giochi finiti, ma
non stabilizza il profile EV CO. Il certificato astratto è quindi valido per ciascun fingerprint;
il corpus chance non è ancora abbastanza fitto per stimare in modo robusto il valore del gioco
fisico.

## Identità certificata

| Campo | Valore |
| --- | --- |
| Gioco | `HU-PREFLOP-HU10-CALIBRATION-001` |
| Rappresentazione | V23 perfect recall |
| Bucket | Flop 32, Turn 128, River 512 |
| Campioni equity | MC8 |
| Deal per classe CO | 8 |
| Seed solver | 23170 |
| Seed corpus | 23171 |
| Seed partizione | 23172 |
| Fingerprint corpus | `fnv1a64:139ff63a167164ef` |
| Fingerprint gioco finito | `fnv1a64:7567a4a5a10b9393` |

## Census

| Campo | Valore |
| --- | ---: |
| Nodi | 1.334.233 |
| Terminali | 727.056 |
| Decisioni | 526.176 |
| Chance node | 81.001 |
| Information set | 490.050 |
| Profondità massima | 16 |
| Payload materializzato minimo | 224.170.652 B |
| Checkpoint finale | 59,57 MiB |

Il payload minimo somma strutture, edge, chiavi e label. Non è un picco RSS e non comprende tutto
l'overhead degli allocator.

## Convergenza

| Iterazioni | NashConv (ante) | `normalized_dev` | Esito |
| ---: | ---: | ---: | --- |
| 100.000 | 2,7797958100 | 0,5449654171 | FAIL |
| 500.000 | 0,5239819972 | 0,1066051493 | FAIL |
| 1.000.000 | 0,2303495945 | 0,0468122284 | FAIL |
| 3.000.000 | 0,0701137538 | 0,0142740774 | FAIL |
| 4.000.000 | 0,0542985159 | 0,0106799403 | FAIL |
| 4.500.000 | 0,0492800398 | 0,0093218629 | PASS |
| 5.000.000 | 0,0436142329 | 0,0081173483 | PASS |

Metriche finali:

| Campo | CO | BTN |
| --- | ---: | ---: |
| Profile EV (ante) | -0,2985411462 | +0,2985411462 |
| Deviation gain (ante) | 0,0243520449 | 0,0192621880 |

La somma dei profile EV è zero. Entrambe le best response superano o eguagliano il profilo
congelato; la massima deviazione è quella CO.

## Ottimizzazione MCCFR

Il primo benchmark ha mostrato un costo lineare nel numero totale di information set: il solver
ricostruiva l'intera strategia e applicava delta nulli a ogni iterazione. Il percorso campionato
ora calcola regret matching solo sugli information set visitati e aggiorna solo le chiavi presenti
nei delta.

| Benchmark | Prima | Dopo | Speedup training |
| --- | ---: | ---: | ---: |
| K=2, 512 iterazioni | 64,2828711 s | 1,7173435 s | 37,43× |
| K=8, 256 iterazioni, quattro checkpoint | 133,1556727 s | 26,4107377 s | 5,04× |
| K=8, 100.000 iterazioni | 22,5572053 s | 12,5287590 s | 1,80× |

I checkpoint prima e dopo l'ottimizzazione hanno lo stesso SHA-256. Per K=2/512 l'hash è
`0D2BDBE6CA756BB38A20A6CCA6C3F0A837FCF501EE772C045E712ACED957736D`; per K=8/256 è
`630184A7351C5B6C80F23333431B3FCABADD54E0B8205F61DE35A91E111C8A79`; per K=8/100k è
`CB80885C8C79814FF9DEC6C2E495034602312B4D52843F283D07210E2D3BF395`.

L'ultimo intervento precompila il puntatore `node -> information set`, usa delta indicizzati dal
buffer e campiona il chance root tramite distribuzione cumulativa. Elimina le ricerche ripetute
nella mappa di stringhe senza cambiare ordine dei campioni, checkpoint o risultato numerico.

## Tempi del segmento finale

Il segmento ripreso da 1M a 5M ha richiesto:

| Fase | Secondi |
| --- | ---: |
| Compilazione deterministica | 60,9599053 |
| Linear MCCFR | 543,4216028 |
| Otto NashConv esatte | 119,6139745 |
| Totale | 749,2181383 |

Il compilatore ricostruisce ancora corpus e `FiniteGame` a ogni resume. Il checkpoint contiene lo
stato del solver, non il gioco compilato.

Dopo questo run il certificatore è stato corretto: `--iterations` rappresenta ora un limite massimo
e l'esecuzione si arresta al primo checkpoint che supera il target. Il JSON registra iterazioni
richieste, iterazioni completate e arresto anticipato. `--continue-after-target` conserva il
comportamento precedente soltanto per benchmark di ricerca espliciti.

## Validazione

- il gioco passa `validate_finite_game`;
- il certificato JSON conserva fingerprint separati di regole, tree, astrazione, corpus e gioco;
- il resume K=2 256→512 è byte-identico al run continuo;
- il percorso MCCFR sparse è byte-identico al percorso denso su K=2 e K=8;
- la normalizzazione massima finale è `5,5511151231e-16`;
- `nashconv_certified=true` e `physical_nashconv_certified=false` sono serializzati separatamente.

La build mirata Release passa con `/W4 /WX`. La regressione HU completa passa 13/13 test in
515,65 s; il test V23 aggiornato passa 14 asserzioni in 19,22 s. La regressione mirata di solver,
best response, card abstraction e subgame passa 6/6 test in 33,37 s. Il validatore JSON Schema
Draft 2020-12 accetta sia il certificato K8 v1 esistente sia lo smoke v2 con coverage audit.

## Policy completion e copertura

Il certificato corrente v2 include il contratto `uniform_unseen_v1`. Sullo stesso gioco finito
usato dal training non interviene alcun fallback: `exact_key_coverage=1`,
`reach_weighted_coverage=1` e `unseen_information_sets=0`. Il costo misurato sullo smoke K=1 è
`0,3526849 s` per 65.290 information set e 65.772 nodi decisionali.

Il test cross-corpus compila due giochi V23 K=1 con seed chance differenti e porta la policy
congelata dal primo al secondo. Misura `exact_key_coverage=0,149305`,
`reach_weighted_coverage=0,765321` e 55.450 information set mancanti; le chiavi sorgente estranee
al target sono conteggiate separatamente. Il contratto `reject_missing` rifiuta lo stesso
trasferimento. Il runner statistico indipendente `training/response/evaluation` è ora implementato
e documentato in
[`V23_TRE_VALIDATION_IMPLEMENTATION_2026-09-15.md`](V23_TRE_VALIDATION_IMPLEMENTATION_2026-09-15.md).

## Limiti e decisione

Il certificato dimostra la convergenza della policy media sul fingerprint indicato. Non si
trasferisce a un corpus diverso, a K diverso, a HU20/HU40 o alla distribuzione completa dei deal
fisici.

V17 resta la baseline CO40. Il secondo corpus seed e la sensibilità K=2/4/8 sono completati. La
variazione del profile EV impedisce di trattare K=8 come una stima stabile del gioco fisico. La
persistenza autonoma del `FiniteGame` rimane un'ottimizzazione, non un requisito matematico del
certificato corrente.

## Secondo corpus seed

Il run K=8 con corpus seed 23173 parte da zero, richiede al massimo 6.000.000 iterazioni e si
arresta automaticamente a 5.000.000 quando raggiunge `normalized_dev=0,0094551018`.

| Campo | Seed 23171 | Seed 23173 |
| --- | ---: | ---: |
| Iterazioni al certificato | 4.500.000 | 5.000.000 |
| `normalized_dev` a 5M | 0,0081173483 | 0,0094551018 |
| NashConv a 5M | 0,0436142329 ante | 0,0511984885 ante |
| Profile EV CO a 5M | -0,2985411462 ante | -0,2162089857 ante |
| Information set | 490.050 | 490.444 |

I due corpus convergono sotto la soglia, ma il profile EV CO differisce di `0,0823321605a`.
Questo delta non invalida le certificazioni dei due giochi finiti; mostra che K=8 non sostituisce
una stima di errore rispetto alla distribuzione fisica.

Il secondo run richiede 974,3938186 s end-to-end: 63,3990482 s di compilazione, 733,6671772 s di
training e 150,434362 s per dieci NashConv esatte. Una singola NashConv costa quindi circa 15,04 s.

## Sensibilità K=2/4/8

Tutti i run usano MC8 e gli stessi seed solver, corpus e partizione. `--iterations=6M` è un limite:
il certificatore si arresta al primo checkpoint da 500k sotto `normalized_dev < 0,01`.

| K | Nodi | Iterazioni | `normalized_dev` | NashConv (ante) | Profile EV CO (ante) | Tempo totale |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 2 | 333.559 | 1.500.000 | 0,0093071292 | 0,0513288638 | +0,0863017485 | 80,96 s |
| 4 | 667.117 | 3.000.000 | 0,0088252007 | 0,0526144992 | -0,1436597714 | 196,92 s |
| 8 | 1.334.233 | 5.000.000 | 0,0081173483 | 0,0436142329 | -0,2985411462 | 749,22 s |

Il target NashConv riguarda la convergenza all'interno di ciascun gioco. Non misura l'errore della
distribuzione chance empirica. Il profile EV CO cambia di `0,2299615199` ante fra K=2 e K=4 e di
`0,1548813747` ante fra K=4 e K=8. Anche i due corpus K=8 differiscono di `0,0823321605` ante.
La sensibilità è quindi completata con esito `NOT_STABLE_FOR_PHYSICAL_VALUE_ESTIMATION`.

## Proiezione HU20 e HU40

Il probe HU20 modifica soltanto lo stack da 10a a 20a e conserva i sizing HU10. Il dato HU40 usa
la configurazione CO40 reale. Il rapporto usa i nodi pubblici postflop e non sostituisce la futura
compilazione completa.

| Gioco | Nodi pubblici | Rapporto vs HU10 | Nodi finiti K=8 stimati | Payload minimo stimato |
| --- | ---: | ---: | ---: | ---: |
| HU10 | 2.010 | 1,00× | 1,334 M | 0,224 GB |
| HU20 stack-only | 9.846 | 4,90× | 6,54 M | 1,10 GB |
| HU40 CO40 | 27.012 | 13,44× | 17,93 M | 3,01 GB |

Proiezione sullo stesso hardware:

| Gioco | Solve fino al target | Una NashConv | Compilazione + una NashConv |
| --- | ---: | ---: | ---: |
| HU20 stack-only | 60–100 min | circa 75 s | circa 6 min |
| HU40 CO40 | 2,5–4 h | 3–4 min | 17–20 min |

Il tempo di solve assume che le iterazioni richieste crescano circa con gli information set. Il
payload è un minimo strutturale: allocator, mappe, strategie e workspace possono portare HU40 oltre
il gate di 8 GiB. Queste righe sono stime, non benchmark di qualificazione.
