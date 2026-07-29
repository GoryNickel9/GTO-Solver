# Fase 10 — GUI HU postflop

Data: 2026-07-29
Versione: 0.10.0
Stato: **completata localmente**

## 1. Esito

F10 introduce `gto_gui`, applicazione prodotto Qt 6 Widgets separata dai
prototipi F9. Il workflow automatizzato usa soltanto path production:

```text
crea → stima → solve exact → checkpoint → salva → riapri → naviga
```

La GUI non esegue CFR sul thread Qt. Un worker conserva config, range e key,
pubblica iterazione e certificazioni, riceve pause/cancel e produce recovery
autenticato. Il timer UI è rimasto attivo per l'intero solve E2E.

## 2. Implementazione

| Attività F10 | Implementazione |
|---|---|
| Home/recenti | Nuovo progetto, lista persistente degli ultimi dieci `.gtsd`, apertura con chiave locale automatica |
| Tree builder | Controlli visuali per pot/stack/rake, 18 scenari postflop e board iniziale flop/turn/river |
| Range editor | Matrice 9×9 paint-on-click/drag, peso 0,01%, blocker applicati alle combo fisiche |
| Estimate dialog | Nodi, infoset, azioni, picco RAM, backing store e hard preflight budget |
| Solve monitor | Iterazione continua, EV CO/BTN, NashConv, chart, pausa e cancel |
| Solution browser | Public tree fisico, heatmap strategy per classe e frequenze per combo |
| Save/open | Container `.gtsd` 1.0 cifrato, chiave locale trasparente, import con chiave solo da altro PC |
| Settings hardware | Budget RAM/disco e backend lazy/out-of-core |
| Errori/recovery | Errori tipizzati visibili; recovery `.gtsd` atomico riapribile all'avvio |
| Localizzazione | UI italiana; catalogo sorgente `gtosd_en.ts` predisposto |
| Diagnostica | Log JSONL persistente e cartella apribile dalla toolbar |

### Semantica dei controlli di calcolo

- **Iterazioni target**: numero massimo di aggiornamenti CFR+; non costituisce
  da solo prova di convergenza.
- **Calcola convergenza ogni**: frequenza del calcolo più costoso di best
  response e NashConv.
- **Limite RAM/disco**: soglie di sicurezza del preflight, non memoria
  prenotata in anticipo.
- **Modalità memoria**: `RAM lazy` alloca in memoria su richiesta;
  `Out-of-core` usa backing storage su disco con un costo prestazionale.

## 3. Correzione del contratto range

Prima di F10 il finite game F7 inizializzava implicitamente tutti i combo
compatibili a peso `1,0`, mentre il chunk F8 `RANGES` conteneva un marker.
Presentare un editor sopra quel path avrebbe prodotto una falsa configurazione.

F10 aggiunge `PostflopRanges` con `2 × 630` `RangeWeight`:

- ogni peso resta in basis point `0..10.000`;
- i blocker sono esclusi fisicamente;
- la normalizzazione iniziale usa il prodotto dei reach CO/BTN compatibili;
- lo stesso range entra in CFR, BR, EV, NashConv e fingerprint;
- un checkpoint con range differente è rifiutato;
- il chunk `RANGES` conserva tutti i 1.260 pesi lossless;
- i file F8/F9 col marker uniform-range restano leggibili;
- il fingerprint legacy resta identico quando entrambi i range sono uniformi.

La query batch costruisce il layout una sola volta e restituisce la strategia
media per tutti i combo legali del nodo. La heatmap aggrega per classe usando
i pesi sorgente dell'attore; una classe a massa zero resta `—` e non viene
rinormalizzata silenziosamente.

## 4. Gate F10

| Gate | Esito | Evidenza |
|---|---|---|
| Crea→solve→salva→riapri→naviga→resume | PASS | `gtosd_phase10_product_e2e` |
| Dati per classe/combo | PASS | heatmap 81 classi, query batch legali, dettaglio combo 0–629 |
| Progress continuo | PASS | iteration counter aggiornato dal control callback |
| Nessun freeze durante solve | PASS | heartbeat massimo 12,0331 ms, soglia test 100 ms |
| Recovery | PASS | ogni certificazione produce `.gtsd` atomico autenticato |
| Range effettivi | PASS | test reach/EV, mismatch fingerprint e round-trip 1.260 pesi |

## 5. Benchmark E2E

Fixture: PF-F1 con tutte le size disabilitate, due iterazioni e resume alla
terza, range CO frazionario deterministico, BTN uniforme. Questa fixture misura
integrazione e responsività; non è il benchmark di convergenza PF-F1.

| Campo | Valore |
|---|---:|
| CPU | Intel Core i3-10100F, 4 core / 8 logical |
| Frequenza nominale | 3.600 MHz |
| RAM fisica host | 34.294.738.944 B |
| Durata workflow installato | 1.937 ms |
| Peak RSS processo | 104.239.104 B |
| Massimo gap heartbeat UI durante solve | 12,0331 ms |
| Heartbeat osservati | 165 |
| File soluzione | 72.286 B |
| Sampling/bucketing | Nessuno |

Il PC non emula una CPU a 2 GHz né 16 GB di RAM. La qualifica sull'hardware
minimo esatto resta un gate di release e non viene dichiarata superata.

## 6. Validazione

| Controllo | Risultato |
|---|---|
| Release completa | PASS, 21/21 in 96,61 s |
| F10 Release | PASS, core + E2E |
| F10 Debug | PASS, 37,03 s |
| F10 MSVC ASan core | PASS, 49,24 s |
| F10 MSVC ASan GUI E2E | PASS, 26,97 s |
| clang-format | PASS |
| Install tree | PASS, 75 file / 89.080.903 B |
| E2E dall'eseguibile installato | PASS, 1,937 s; massimo gap solve 12,0331 ms |

Il run Release completo include F4 exhaustive, F7 exact e F8 storage. PF-F1
non è stato risolto nuovamente per 125 iterazioni: la certificazione pubblicata
in F7 rimane `NashConv / pot = 0,741405%`.

## 7. Riproduzione

```powershell
cmake --preset windows-gui-release
cmake --build --preset windows-gui-release
ctest --preset windows-gui-release -L phase10
ctest --preset windows-gui-release
cmake --install out/build/windows-gui-release `
  --prefix C:\absolute\path\to\gtosd-f10

$env:QT_QPA_PLATFORM = "minimal"
Start-Process C:\absolute\path\to\gtosd-f10\bin\gto_gui.exe `
  -ArgumentList "--e2e", "C:\absolute\path\to\report.json" -Wait
```

## 8. Limiti dichiarati

- la chart EV è a livello di profilo/certificazione; action EV per combo non è
  ancora una vista dedicata;
- la heatmap usa il range root sorgente; node reach e reached range non sono
  ancora materializzati come analytics separati;
- il preflight e la query batch del browser sono ancora sincroni; il gate
  no-freeze certifica solve e resume, non queste operazioni on-demand;
- il catalogo inglese è predisposto come `.ts`, non ancora compilato/caricato
  come `.qm`;
- la key recovery è conservata nelle impostazioni locali Qt; una release
  commerciale deve delegarla al license/key layer o al keystore di sistema;
- il collaudo automatico non sostituisce il collaudo personale del workflow e
  dell'ergonomia da parte dell'utente;
- F11 nodelock, F12 trainer e F13 database non fanno parte di F10;
- la qualifica 4-core/2 GHz/16 GB esatta resta esterna a questo host.

Questi limiti non alterano CFR, BR, NashConv, range, checkpoint o strategia
salvata; delimitano le viste e il packaging di release.

## 9. Prossima fase

La prossima milestone è **Fase 11 — Nodelock globale**.
