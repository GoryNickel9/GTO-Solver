# Fase 6 — Report prototipo C: Out-of-core

## 1. Contratto

Il prototipo usa pagine memory-mapped da 64 KiB, cache residente configurabile,
chunk indirizzabili su backing file e staging incrementale di una pagina. Il backend
di prova serializza e rilegge i buffer senza perdita; la sonda di residenza
crea la capacità logica richiesta e attraversa finestre distribuite sul file.

Il percorso non introduce sampling o bucketing. La compressione e il container
commerciale appartengono a F8.

## 2. Risultati stimati

Configurazione cache: 64 pagine da 64 KiB più una pagina di staging.

| Benchmark | Backing store exact | Working set configurato | Byte/infoset |
|---|---:|---:|---:|
| PF-F1 | 4.554.172.152 B, 4,241 GiB | 4.259.840 B | 147,214 |
| PF-F2 | 253.561.000.536 B, 236,147 GiB | 4.259.840 B | 163,559 |
| PF-F3 | 2.081.963.392.536 B, 1,894 TiB | 4.259.840 B | 167,432 |

## 3. Sonda RSS PF-F1

Comando:

```powershell
.\out\build\windows-release\apps\gto_cli\gto_cli.exe `
  memory-probe pf-f1 .\out\gtosd_pf_f1_memory_probe.bin
```

| Metrica | Valore |
|---|---:|
| Capacità logica del backing file | 4.554.172.152 B |
| Pagine scritte e lette | 128 + 192 |
| Byte logici distinti effettivamente toccati | 8.388.608 B |
| Peak RSS misurato del processo | 17.739.776 B, 16,918 MiB |
| Tempo | 9,675 s |

Il file temporaneo della verifica è stato rimosso dopo il run.

Questa è una prova di capacità e residenza del prototype store, non un solve
PF-F1: il traversal poker completo e le misure di page-fault/iterazioni al
secondo appartengono a F7.

## 4. Scaling del planner

| Thread | Report PF-F1/s |
|---:|---:|
| 1 | 9.412 |
| 2 | 14.055 |
| 4 | 26.323 |
| 8 | 35.910 |

## 5. Decisione

Il prototipo è selezionato come **fallback exact** quando il preflight supera
il budget in-RAM. PF-F2 e soprattutto PF-F3 hanno requisiti disco troppo alti
per essere abilitati automaticamente: F7 dovrà rifiutare il solve se RAM e
spazio libero non soddisfano il preflight.
