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
| Tree builder | Pot/stack/rake, pannelli separati CO/OOP e BTN/IP, override per street e board Short Deck visuale da 3 a 5 carte |
| Range editor | Matrice quadrata 9×9 paint-on-click/drag, slider 0,01%, default 0%, blocker applicati alle combo fisiche |
| Estimate dialog | Nodi fisici/canonici, infoset e azioni materializzati, memoria persistente regret+strategy e selezione RAM/out-of-core |
| Solve monitor | Tempo trascorso, iterazione continua, EV CO/BTN, NashConv, intervallo di certificazione, chart, pausa e cancel |
| Solution browser | Albero azioni orizzontale con frequenze, selettore esplicito delle carte turn/river; tabella combo con distribuzione/equity/reach, heatmap 9×9 read-only e distribuzione del valore mano |
| Save/open | Container `.gtsd` 1.0 cifrato, chiave locale trasparente, import con chiave solo da altro PC |
| Settings solve | Target dEV GTO+ 1%; NashConv/Pot separato; certificazione ogni 20 iterazioni e a fine solve; budget e backend automatici |
| Errori/recovery | Errori tipizzati visibili; recovery `.gtsd` atomico riapribile all'avvio |
| Localizzazione | UI italiana; catalogo sorgente `gtosd_en.ts` predisposto |
| Diagnostica | Log JSONL persistente e cartella apribile dalla toolbar |

### Semantica dei controlli di calcolo (aggiornata 2026-08-01)

- **Target dEV GTO+**: massimo guadagno di deviazione unilaterale diviso per il
  pot iniziale; il default è 1%. NashConv/Pot resta diagnostico e separato.
- **Certificazione**: best response e NashConv sono calcolati ogni 20 iterazioni
  e sempre all'ultima; il monitor distingue tempo totale, iterazioni CFR+ e
  certificazioni.
- **Risorse**: il preflight usa automaticamente l'80% della RAM fisica e dello
  spazio temporaneo disponibile, passando da lazy RAM a out-of-core quando
  necessario.
- **Pausa/annulla**: entrambi restano disponibili nella toolbar e nel monitor
  durante il worker; il controllo avviene al confine sicuro di iterazione.

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

L'analisi del nodo ricostruisce il reach lungo la history usando la strategia
media, applica i blocker chance e calcola l'equity enumerando esattamente i
runout Short Deck. La heatmap aggrega per classe usando il reached range
dell'attore; una classe a massa zero resta `—`.

## 4. Gate F10

| Gate | Esito | Evidenza |
|---|---|---|
| Crea→solve→salva→riapri→naviga→resume | PASS | `gtosd_phase10_product_e2e` |
| Dati per classe/combo | PASS | combo raggiunte, equity exact, distribuzione azioni, heatmap 81 classi e valore mano |
| Progress continuo | PASS | iteration counter aggiornato dal control callback |
| Nessun freeze durante solve | PASS | heartbeat massimo 12,0331 ms, soglia test 100 ms |
| Recovery | PASS | ogni certificazione produce `.gtsd` atomico autenticato |
| Range effettivi | PASS | test reach/EV, mismatch fingerprint e round-trip 1.260 pesi |
| Navigazione chance | PASS | E2E seleziona un turn dopo check-check e raggiunge il nodo decisionale di turn |
| Matrice strategia | PASS | `NoEditTriggers` e `NoSelection`, incluso doppio click |

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

Retest del 2026-07-29 dopo la correzione memoria/tempo e il nuovo browser:
Release completa **22/22 PASS in 144,33 s**; E2E prodotto mirato **PASS in
30,75 s**. L'E2E ora comprende anche l'analisi exact di equity e valore mano.

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
- il preflight e l'analisi exact del nodo sono ancora sincroni; il gate
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

## 9. Prossimo gate

La prosecuzione verso F11 è congelata. Il prossimo lavoro è il gate bloccante
di parità corretto `GTP-AHKHQH-003`, mantenuto in
[`GTO_PLUS_PARITY_JOURNEY.md`](GTO_PLUS_PARITY_JOURNEY.md). Il root EV è ora
in parità entro `0,0055 ante`; la fase immediata F10.4 controllerà i posteriori
BTN imponendo nel solo test la strategia root GTO+ combo-per-combo. F10.4 non è
ancora implementata e non equivale al node locking di prodotto F11.
