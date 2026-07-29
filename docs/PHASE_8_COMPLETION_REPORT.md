# Fase 8 — Storage della soluzione

Aggiornato: 2026-07-29

## 1. Esito

La Fase 8 è **completata localmente**. Il nuovo modulo pubblico
`gtosd::storage` salva, protegge, verifica e riapre le soluzioni postflop F7
nel container `.gtsd` 1.0. Strategia, regret necessario al resume, EV,
certificazione e configurazione conservano la rappresentazione `double`
lossless nel percorso predefinito.

La strategia quantizzata `uint16` è disponibile soltanto come API
sperimentale. Non viene attivata silenziosamente e non è usata nei risultati
exact dichiarati.

## 2. Copertura delle attività

| Attività roadmap | Stato | Implementazione |
|---|---:|---|
| Schema `.gtsd` | Completata | Header, feature flags, indice, chunk, footer |
| Chunk index | Completata | Entry fisse da 64 B, offset e size validate |
| Zstandard | Completata | Frame indipendente per chunk |
| libsodium secretstream | Completata | Uno stream XChaCha20-Poly1305 indipendente per chunk |
| Random access | Completata | Apertura indice e lettura lazy del solo chunk richiesto |
| Atomic save | Completata | Temporary sibling, flush, reopen/verify, replace atomico |
| Migration framework | Completata | Reader versionato e riscrittura verso destinazione separata |
| Verification tool | Completata | `gto_cli storage verify` |
| Quantizzazione | Sperimentale | Quantizzazione `uint16` con massa intera 65.535 |
| Dizionario Zstd | Completata | Training deterministico e chunk `DICTIONARY` autonomo |
| Catalogo SQLite | Completata | `.gtsddb` esterno con upsert/list |

## 3. Contratto del container

### 3.1 Layout fisico

```text
HEADER
CHUNK_INDEX
ENCRYPTED_CHUNK[0..N)
FOOTER
```

L'indice interno è autenticato con BLAKE2b keyed usando la chiave del file.
Ogni entry registra tipo, flag dizionario, offset, dimensione cifrata,
dimensione compressa, dimensione raw e header secretstream.

| Chunk | Contenuto F8 |
|---|---|
| `CONFIG` | Configurazione postflop JSON canonica |
| `TREE` | Versione dello schema di rigenerazione del public tree |
| `ISOMORPHISM` | Versione del mapping globale lossless |
| `STRATEGY` | Regret cumulativo e strategia cumulativa lossless |
| `EV` | EV profilo, best response e NashConv |
| `RANGES` | Identità del dominio fisico a 630 combo |
| `NODELOCKS` | Schema nodelock; `none` per F8 |
| `METRICS` | Iterazioni, delay, action count e fingerprint |
| `DICTIONARY` | Dizionario Zstandard, compresso senza autoreferenzialità |

### 3.2 Versione

| Campo | Valore |
|---|---:|
| API GTOSD | `0.8.0` |
| `.gtsd` major | `1` |
| `.gtsd` minor | `0` |
| Checkpoint postflop incorporato | `1.0` |

Un major futuro produce `UnsupportedVersion`. Feature sconosciute producono
`UnsupportedFeature`. Header, indice, offset, size e footer vengono validati
prima di allocare un payload.

## 4. Compressione e cifratura

La CLI addestra un dizionario da 32 KiB su campioni distribuiti dei chunk.
Il chunk dizionario viene cifrato e compresso senza dizionario; gli altri
chunk lo referenziano tramite flag nell'indice.

Ogni chunk usa:

1. compressione Zstandard;
2. secretstream XChaCha20-Poly1305 indipendente;
3. associated data con versione, feature, tipo e dimensione raw;
4. tag `FINAL` obbligatorio.

L'indipendenza degli stream consente random access autenticato. La chiave da
32 byte è fornita dal layer licenza. Il formato non accetta né memorizza una
password utente.

## 5. Atomicità e migrazioni

Il writer:

1. prepara un file temporaneo nella directory della destinazione;
2. scrive header, indice, chunk e footer;
3. esegue flush durevole;
4. riapre, autentica e decomprime tutti i chunk;
5. sostituisce atomicamente la destinazione.

Un errore prima del commit lascia intatta l'ultima soluzione valida. La
migrazione richiede source e destination differenti; l'originale non viene
sovrascritto.

## 6. Indice interno e catalogo SQLite

I due indici hanno responsabilità distinte:

| Livello | Tecnologia | Responsabilità |
|---|---|---|
| Singolo file | Chunk index `.gtsd` | Offset e random access autenticato |
| Libreria soluzioni | SQLite `.gtsddb` | Path, fingerprint, size, mtime e NashConv |

SQLite non è necessario per aprire o verificare un `.gtsd`.

## 7. Gate F8

| Gate | Esito |
|---|---:|
| Round-trip completo | PASS |
| Bit flip rilevato | PASS, `AuthenticationFailed` |
| File troncato senza crash | PASS, `TruncatedFile` |
| Root senza full load | PASS |
| Atomic save | PASS |
| Versione futura rifiutata | PASS |
| Migrazione preserva source | PASS |
| Strategia/EV lossless | PASS |
| Catalogo SQLite | PASS |
| Mutazioni autenticate | PASS, 256 casi |

I test F8 eseguono 309 asserzioni. Il corpus di mutazione copre magic, header,
versione, indice, ciphertext e footer; un JSON config autenticato ma
semanticamente invalido viene respinto durante il restore tipizzato.

## 8. Benchmark PF-F1 storage

Fixture: PF-F1 completa, flop `As Qd 7c`, pot 10 ante, stack 20 ante, size
50%, raise depth 1, rake zero.

| Metrica | Valore |
|---|---:|
| Nodi pubblici | 165.774 |
| Infoset exact | 30.873.216 |
| Azioni exact | 66.756.096 |
| Iterazioni benchmark storage | 1 |
| Payload logico | 1.068.121.299 B |
| Payload compresso | 5.617.344 B |
| Payload cifrato | 5.617.497 B |
| File `.gtsd` | **5.618.173 B** |
| Rapporto compresso/raw | 0,00525909 |
| Metadata residenti all'open | **676 B** |
| Chunk | 9 |

Il risultato è sotto l'obiettivo commerciale di 250 MB, ma la misura usa una
sola iterazione sulla topologia completa. I valori delle strategie dopo 125
iterazioni possono avere comprimibilità diversa. Questa misura non sostituisce
la certificazione F7 a `NashConv / pot = 0,741405%`.

## 9. Test file fisico da 250 MB

Un payload pseudo-casuale incomprimibile ha verificato l'apertura indicizzata
di un file fisico vicino a 250 MiB:

| Metrica | Valore |
|---|---:|
| Raw | 262.144.000 B |
| Compresso | 262.150.010 B |
| File cifrato | 262.150.191 B |
| Metadata residenti all'open | 164 B |
| Esito | PASS |

## 10. Verifiche eseguite

| Verifica | Esito |
|---|---:|
| Build Debug completa | PASS |
| CTest Debug | PASS, 12/12 |
| Build Release completa | PASS |
| CTest Release escluso F4 | PASS, 11/11 |
| F4 Release exhaustive separato | PASS, 351.930 asserzioni |
| MSVC ASan F8 | PASS |
| clang-format globale | PASS |
| CLI solve/pack/verify/query/migrate/catalog | PASS |
| Install tree con DLL/licenze | PASS |
| Consumer `find_package(gtosd)` | PASS |
| File fisico 250 MB | PASS |
| PF-F1 topology storage | PASS |

## 11. Comandi production

```powershell
$cli = ".\out\build\windows-release\apps\gto_cli\gto_cli.exe"
$key = & $cli storage keygen

& $cli storage pack out\pf-f1.json out\pf-f1.chk out\pf-f1.gtsd $key
& $cli storage verify out\pf-f1.gtsd $key
& $cli storage query out\pf-f1.gtsd $key 0 100
& $cli storage migrate out\pf-f1.gtsd out\pf-f1-v1.gtsd $key
& $cli storage catalog-add out\solutions.gtsddb out\pf-f1.gtsd $key
& $cli storage catalog-list out\solutions.gtsddb
```

## 12. Prossima fase

La prossima milestone è **Fase 9 — Prototipo e scelta GUI**. Deve confrontare
Qt 6 e Dear ImGui sul navigator F7/F8 usando l'API query del file `.gtsd`,
misurare 10.000 righe e validare high-DPI, accessibility e packaging.
