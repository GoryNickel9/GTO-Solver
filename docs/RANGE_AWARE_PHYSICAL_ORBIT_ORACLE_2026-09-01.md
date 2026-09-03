# Range-aware physical-orbit oracle — 2026-09-01

> **CORREZIONE SEMANTICA 2026-09-04.** Il controesempio di correttezza resta
> valido. Il successivo rinvio al contratto strict-2GB è invece storico e non
> governa il prossimo lavoro. Vedere il
> [`piano di correzione`](GTO_PLUS_SOLVER_MEMORY_SEMANTICS_AND_GATE_CORRECTION_PLAN_2026-09-03.md).

## Analisi

### Obiettivo

Verificare, prima di qualunque benchmark lungo o integrazione production, se il
riuso di un solo subtree fisico per chance outcome collegati da un
automorfismo dei semi preserva il contratto solver con range asimmetrici.

Il gate richiede identità di gioco e layout e confronta separatamente:

- valori restituiti dai child trasformati;
- profile EV e best-response EV;
- cumulative regret;
- cumulative strategy.

### Stato iniziale

| Campo | Valore |
|---|---|
| branch | `research/range-aware-orbit-oracle-20260901` |
| base | `77677b2f0be711b588bebdbd2256fe8a28f4b863` |
| main utente | fixture modificate e artifact untracked preservati |
| production | physical-orbit aggregation disabilitata |
| benchmark lunghi | non autorizzati prima dell'oracle |

Il codice contieneva già il percorso sperimentale `PhysicalOrbitContext`, ma
`DenseLayout::uses_range_aware_physical_orbits` era forzato a `false`.

### Invarianti

Baseline e candidato devono avere gli stessi:

- board, pot, stack, rake, sizing e range;
- algoritmo, iterazioni, averaging e precisione;
- fingerprint, action count e information-set count;
- blocker e card-removal implicati dal medesimo physical tree.

L'unica variabile è il riuso degli orbit nella traversal fisica.

## Piano

1. Compilare un secondo target postflop soltanto quando
   `GTOSD_BUILD_RANGE_ORBIT_ORACLE=ON`.
2. Lasciare la normale libreria `gtosd_postflop` senza il macro diagnostico.
3. Eseguire baseline e candidato nello stesso test e nello stesso processo.
4. Cercare il primo controesempio deterministico.
5. Fermare la famiglia al primo errore di equivalenza.

## Implementazione

Il target `gtosd_postflop_range_orbit_oracle` ricompila il postflop core con
`GTOSD_ENABLE_RANGE_ORBIT_ORACLE=1`. Soltanto in quel binary la variabile
`GTOSD_RANGE_ORBIT_ORACLE=1` può attivare il percorso rifiutato.

La configurazione predefinita mantiene entrambe le opzioni assenti; pertanto
il percorso production continua a compilare direttamente:

```text
uses_range_aware_physical_orbits = false
```

L'oracle usa:

- board `Ah Kh Qh`;
- range iniziali `AA, KK, QQ, AKs, AQs, KQs, AKo, AQo, KQo`;
- rimozione di `AA` dal solo player 1, creando range asimmetrici;
- physical public tree con lossless infoset isomorphism;
- `DCFR+`, stato `Float32`, averaging immediato;
- tre iterazioni e una certification finale.

## Validazione

### Controesempio minimo

| Metrica | Differenza massima baseline/candidato |
|---|---:|
| child value rappresentativo/trasformato | `5.0104216770877757e-05` |
| profile EV | `0.000100178` ante |
| best-response EV | `1.32975e-07` ante |
| cumulative regret | `2.2076` |
| cumulative strategy | `0.000389099` |

Fingerprint, action count e information-set count sono identici. La
divergenza non deriva quindi da un altro gioco o layout: nasce dal riuso del
subtree e dal differente ordine/contesto di reach e update.

### Test eseguiti

- configurazione e build Release MSVC `/W4 /WX`: PASS;
- `gtosd_gto_plus_reference_tests`: PASS, `89.19 s` nel full finale;
- `gtosd_range_orbit_oracle_tests`: PASS, `10.68 s` nel full finale;
- test combinati: `2/2 PASS`, `100.48 s`.
- full CTest finale con runtime vcpkg nel `PATH`: `19/19 PASS`, `209.25 s`.

Il primo full CTest aveva cinque `0xc0000135` perché la nuova build non aveva
le DLL vcpkg nel `PATH`. Gli stessi cinque test sono passati `5/5` dopo la
correzione ambientale; il successivo full run uniforme è passato `19/19`.

Il PASS dell'oracle significa **controesempio riprodotto**. Non significa che
il candidato sia corretto.

### Casi non verificati

- nessun target-driven AHK/TH/TST;
- nessuna misura RAM o timing production del candidato;
- nessuna five-process certification;
- nessun tentativo di correggere il riuso dopo il kill gate matematico.

## Decisioni

**Outcome unico: C. RANGE-AWARE PHYSICAL-ORBIT FAMILY CLOSED FOR PRODUCTION.**

Il candidato è rifiutato prima del benchmark perché non preserva il contratto
DCFR su range asimmetrici. Non è lecito compensare la divergenza con un
moltiplicatore uniforme, riordinare gli action index o accettare soltanto la
vicinanza dell'EV finale: child value, BR e stato cumulativo sono autorità
separate.

Compromesso: viene mantenuto un target diagnostico default-OFF per impedire
che la stessa ipotesi venga riaperta senza riprodurre e superare il
controesempio. Nessun default, checkpoint o dispatch production cambia.

## Passo successivo

Tornare al gate di governance del contratto strict-2GB: nessun'altra
integrazione isomorfica è autorizzata senza una nuova prova matematica che
preservi il valore controfattuale di ogni chance occurrence asimmetrica.
