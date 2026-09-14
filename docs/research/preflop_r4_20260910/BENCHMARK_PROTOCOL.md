# Protocollo benchmark R4

Il confronto usa la fixture CO40, l'astrazione distribuzionale R2.1, external sampling v2, 100.000 iterazioni, 20.000 deal di valutazione, 5.000 iterazioni di risposta e 10.000 deal per risposta. Training, partizione e valutazione usano gli stessi seed. Cambia soltanto il percorso delle azioni postflop: piano compilato oppure chiamate di riferimento a `legal_actions` e `apply_action`.

Entrambi i processi hanno un budget di 8 GiB per il payload numerico e 600 entry complessive per le sei partizioni della cache dei bucket. Il test è valido soltanto se strategia, EV, infoset e metriche di risposta coincidono esattamente e se il picco della cache non supera 600 entry. Il tempo wall e il peak working set vengono misurati dal processo esterno; `solve_seconds`, payload, eviction e dimensione minima del piano compilato provengono dal risultato strutturato.

Il gate R4 non richiede uno speedup: richiede equivalenza semantica, transizioni precompilate prima del training, limiti applicati senza fallback silenzioso e misure sufficienti a decidere se mantenere il percorso compilato.
