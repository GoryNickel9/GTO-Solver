# Protocollo benchmark R3

Il confronto trainer usa la fixture CO40, rappresentazione fisica exact, external sampling v2, 100.000 iterazioni, 20.000 deal di valutazione, 5.000 iterazioni di risposta e 10.000 deal per risposta. Seed, ordine del sampling e cache del vincitore sono identici. Cambia soltanto il backend dell'evaluator: oracle combinatorio oppure tabella verificata.

Il tempo wall include avvio e caricamento della tabella. `solve_seconds` inizia dopo il costruttore del solver e quindi esclude il caricamento; entrambi vengono riportati. Lo scenario cold della risorsa somma costruzione, salvataggio, caricamento e solve wall. Lo scenario warm usa una risorsa già distribuita, ma continua a includere caricamento e checksum.

Il backend tabellare viene mantenuto soltanto se produce la stessa policy e utility nei test e riduce il tempo wall del trainer senza superare il costo fisso di 33.390.720 byte.
