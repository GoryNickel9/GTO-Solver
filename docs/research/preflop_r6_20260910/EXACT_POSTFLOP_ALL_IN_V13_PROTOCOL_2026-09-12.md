# R6 — Protocollo V13 per gli all-in postflop esatti

Data: 2026-09-12  
Stato: screen e coppia 2M completati; gate documentato

## Obiettivo

V13 verifica una sola modifica rispetto a V12: nei terminali all-in raggiunti sul Flop o sul Turn,
il singolo runout campionato viene sostituito dall'attesa esatta dei runout legali. Il River resta
un confronto diretto. L'esperimento misura correttezza, varianza, stabilità tra seed e costo; il
confronto con Monker avviene soltanto dopo la selezione del candidato e non determina il training.

## Contratto matematico

Per quattro hole card fisiche e il board già visibile:

```text
Flop:  C(29, 2) = 406 completamenti non ordinati
Turn:  28 river legali
River: 1 showdown
```

Per ogni terminale, `settle_terminal` calcola separatamente i payoff di vittoria, pareggio e
sconfitta. Il valore restituito è:

```text
EV = (wins * payoff_win + ties * payoff_tie + losses * payoff_loss) / runouts
```

Commitment, pot e rake appartengono allo stato terminale e non alla tabella di equity. Il Fold non
passa dall'enumeratore.

## Adattamento ai range

L'enumerazione è mano fisica contro mano fisica. Il MCCFR continua a campionare carte private,
azioni e reach probability. Quando cambia il range avversario, cambiano frequenza e peso con cui
ogni matchup viene raggiunto; non cambia l'equity dello stesso matchup sullo stesso board.

La cache usa quindi soltanto:

```text
hole CO + hole BTN + board visibile + street -> win/tie/loss
```

La chiave ordina le carte interne alle due mani e il board, poi sceglie la rappresentazione minima
fra le 24 permutazioni globali dei semi. Non include range, history, pot o strategia. L'EV monetaria
viene ricalcolata a ogni terminale.

## Perché non esiste una tabella fisica completa

Con posizioni distinte, le chiavi fisiche grezze sono circa 1,75 miliardi sul Flop, 12,7 miliardi
sul Turn e 71,2 miliardi sul River. La sola divisione teorica per le 24 permutazioni dei semi non
rende conveniente pre-generare tutto. V13 usa una cache bounded lazy: calcola soltanto gli stati
raggiunti e non aumenta la RAM oltre il budget dichiarato.

## Modifica isolata

Restano invariati:

- albero `fnv1a64:a68337fa567aa2d9` e fixture monetaria v2;
- V8 street-adaptive MC8 con capacità `32/128/512`;
- Linear MCCFR, batch 32, otto worker e quattro rollout medi per entrambi i traverser;
- seed di training, partition ed evaluation;
- oracle esatto degli all-in preflop introdotto da V12.

V13 aggiunge l'enumerazione esatta Flop/Turn e un budget cache complessivo di 1.000.000 entry. Non
modifica size, soglie, bucket, range o payoff per avvicinarsi al benchmark Monker.

## Verifica dell'enumeratore e della cache

Il test esaustivo controlla 406/28/1 esiti, complementarità scambiando i giocatori, coerenza fra
Flop e somma dei Turn condizionati, input duplicati e street invalide. Il test di trasparenza esegue
la stessa solve con cache disattivata e attiva: strategia root, action EV e root EV devono essere
identiche bit per bit; hit, miss e capacità sono telemetria separata.

Esito Release:

```text
HU_PREFLOP_TREE_TESTS=PASS assertions=15950
HU_PREFLOP_TEST_BEGIN name=exact_postflop_all_in_cache_is_transparent
HU_PREFLOP_TEST_END name=exact_postflop_all_in_cache_is_transparent assertions=1
```

## Screen accoppiato a 100k

I test a 100k non sono versioni pubblicabili. Servono soltanto a decidere se sostenere il costo di
una coppia a 2M.

| Modalità | Tempo medio | Root SE media | TV fra seed | Varianza relativa | Tempo relativo | Varianza × tempo |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Runout campionato | 75,35 s | 0,08687a | 34,27 pp | 1,000 | 1,000 | 1,000 |
| Turn esatto, senza cache | 97,13 s | 0,08099a | 32,37 pp | 0,869 | 1,289 | 1,120 |
| Flop+Turn esatti, senza cache | 229,43 s | 0,06655a | 31,47 pp | 0,587 | 3,045 | 1,787 |
| Turn esatto, cache | 88,73 s | 0,08099a | 32,37 pp | 0,869 | 1,178 | 1,024 |
| **Flop+Turn esatti, cache** | **109,80 s** | **0,06655a** | **31,47 pp** | **0,587** | **1,457** | **0,855** |

Il Turn esatto resta neutro o leggermente peggiore. Flop+Turn con cache riduce del 14,5% il tempo
stimato per raggiungere la stessa varianza e supera lo screen. I due seed registrano hit rate
`85,62%` e `85,52%`.

## Gate della coppia 2M

V13 può essere pubblicata nel viewer soltanto se:

1. entrambi i seed completano 2.000.000 iterazioni ed esportano 20 nodi preflop e la policy
   postflop;
2. i test Release, la normalizzazione e l'integrità degli export passano;
3. l'enumeratore è dichiarato nei metadati e `hits + misses = evaluations`;
4. TV tra seed, root EV ± SE e audit coerente corrente-contro-corrente sono riportati;
5. Monker viene usato dopo questi controlli come diagnostica esterna, mai come obiettivo di tuning.

Il gate R6 originario resta separato: `WMAE <= 1 pp`, `TV media <= 2 pp`, `P95 <= 5 pp` ed errore
root aggregato `<= 1 pp`.

## Esito

I due seed da 2M sono completi. V13 supera i controlli ingegneristici, l'audit locale della policy
corrente e migliora la stabilità interna fra seed, ma non supera il confronto esterno né il gate
R6. Risultati, hash e decisione sono nel
[report di gate](EXACT_POSTFLOP_ALL_IN_V13_GATE_2026-09-12.md).
