# Error handling e versionamento

## Error handling

Il codice first-party usa `Result<T, Error>` per ogni errore recuperabile che
attraversa il confine di un modulo.

| Caso | Regola |
|---|---|
| Input utente o configurazione invalida | `Result::failure` con enum tipizzato |
| Stato di gioco illegale | `Result::failure`, mai correzione silenziosa |
| Overflow o underflow | Errore aritmetico esplicito |
| Fallback approssimato | Vietato nei path dichiarati exact |
| Eccezione di libreria | Tradotta al confine del modulo, senza `catch (...)` che inventi dati |
| Invariante interna impossibile | Fail-fast nel processo di sviluppo; errore tipizzato nelle API pubbliche |

Il chiamante deve verificare `has_value()` prima di leggere `value()`. I test
possono leggere direttamente `value()` soltanto dopo una precondizione già
asserita o per rendere immediatamente visibile una violazione.

## Semantic versioning API

La versione API segue `MAJOR.MINOR.PATCH` ed è definita una sola volta dal
comando CMake `project()`. CMake genera `gtosd/version.hpp`.

| Incremento | Significato |
|---|---|
| Major | Modifica incompatibile di API o semantica pubblica |
| Minor | Funzione retrocompatibile |
| Patch | Correzione retrocompatibile |

La versione corrente è `0.6.0`: la minor F6 introduce il modulo pubblico
`gtosd::memory`, i preflight PF-F1/PF-F2/PF-F3 e i prototype backend di
memoria exact. Durante lo sviluppo pre-1.0 una modifica
incompatibile richiede almeno un incremento minor e una nota di migrazione.

## Versionamento file

Ogni formato persistente ha una coppia `major/minor` indipendente:

| Formato | Versione iniziale |
|---|---:|
| Stato pubblico di audit | 1.0 |
| Canonical key isomorfismo | 1.0 |
| Soluzione `.gtsd` | 1.0 |
| Checkpoint `.gtsdckpt` | 1.0 |

- un reader rifiuta un major futuro;
- un minor futuro può essere accettato soltanto se tutte le feature richieste
  sono comprese;
- una modifica incompatibile incrementa il major;
- una nuova sezione opzionale incrementa il minor;
- ogni file conserva magic, versione e feature flags;
- le migrazioni non sovrascrivono l'originale prima della verifica completa.
