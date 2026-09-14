# R6 — Implementazione V17: profilo corrente completo

Data: 2026-09-13  
Esito: `ENGINEERING_PASS`

## Obiettivo

V17 rende coerente la diagnostica `corrente finale`. Prima di questa modifica, le frequenze
preflop provenivano dal regret matching finale, ma le continuation e gli EV postflop usavano la
media CFR. Il risultato era un profilo ibrido.

Il training non cambia. V17 valuta ed esporta due profili distinti:

- `media CFR`: strategia media a ogni infoset preflop e postflop;
- `corrente finale`: regret matching corrente a ogni infoset preflop e postflop.

## Implementazione

`HuPreflopSampledPostflopPolicy` passa al formato `1.11` quando contiene la policy corrente. Ogni
entry conserva le probabilità medie e correnti con lo stesso numero e ordine di azioni. I file
storici `1.5–1.10` restano caricabili; una query corrente su un file privo del campo restituisce
`UnsupportedVersion` invece di sostituire la media in silenzio.

Il solver supporta ora `HuPreflopSampledPolicyView::{Average, Current}` lungo l'intera traversata
di valutazione. Con `--evaluate-current-profile` produce:

- EV root, EV delle cinque azioni root ed EV di tutti i 20 nodi preflop;
- errori standard e campioni separati dal profilo medio;
- due risposte campionate indipendenti contro il profilo corrente;
- limite inferiore campionato della risposta, con scope non certificato esplicito.

Le matrici root opzionali del profilo corrente sono allocate dinamicamente. Questo evita lo stack
overflow Windows `0xC00000FD` osservato nel primo test senza aumentare lo stack del processo.

I due script di audit ricostruiscono la reach della sequenza con la stessa policy per entrambi i
giocatori. Il riepilogo generale pesa ogni classe per la sua massa fisica `6/4/12`; il controllo
statistico riporta sia la separazione a due SE sia una soglia Bonferroni al 5% per i confronti
simultanei. Nessuna di queste due misure viene etichettata come exploitability.

## Compatibilità e invarianti

- default invariato: senza il flag non viene serializzata la policy corrente;
- strategia media V17 bit-identica a V15 per ciascun seed;
- stesso albero, payoff, range, size, rake, bucket e backend V15;
- probabilità finite, non negative e normalizzate per entrambi i profili;
- policy media e corrente interrogabili senza collegare il solver postflop standalone;
- checksum e fingerprint includono in modo versionato la presenza della policy corrente.

## Validazione

| Controllo | Esito |
| --- | --- |
| Build MSVC Release con warning come errori | PASS |
| `gtosd_hu_preflop_sampling_tests` | PASS, 34.150 assertion |
| `gtosd_hu_preflop_abstraction_tests` | PASS, 91.774 assertion |
| `gtosd_hu_preflop_parallel_tests` | PASS, 2.100 assertion |
| Suite HU Release | PASS, 13/13 in 503,76 s |
| Round trip policy 1.11 e checksum | PASS |
| Caricamento policy storica e rifiuto query corrente | PASS |
| Determinismo 1/8 worker | PASS bit per bit |
| Smoke CLI, 20 nodi e tipi JSON scalari | PASS |

Il test ridotto costruisce una policy in cui media e corrente differiscono e verifica EV distinti.
La suite parallela confronta anche le risposte campionate dei due profili fra uno e otto worker.

## Limiti

V17 non rende la policy corrente una soluzione pubblicabile. Un singolo iterato MCCFR può essere
puro e oscillare anche quando la media è più stabile. Gli EV di azione usano 20.000 deal totali:
alla root una coppia come AA riceve circa 190–207 campioni per azione, non 20.000 campioni per
classe. Il gate quantitativo resta nel report dedicato.
