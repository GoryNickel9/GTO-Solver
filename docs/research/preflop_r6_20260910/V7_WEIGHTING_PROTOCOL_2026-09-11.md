# R6 — Protocollo di weighting V7

## Obiettivo

Verificare se il peso lineare applicato a regret e averaging amplifica la sensibilità agli ultimi campioni. Il challenger usa External Sampling senza peso lineare. Albero, astrazione, sampling fisico e schedule restano invariati.

## Contratto congelato

- gioco CO40, contratto monetario revisione 2;
- V7 strutturata, MC8, `32/128/512`;
- otto worker, batch 32;
- gli stessi due training seed, evaluation seed e partition seed dei protocolli precedenti;
- 250.000 iterazioni;
- evaluation/response diagnostic: 1.000;
- baseline control-variate disattivata;
- nessun export completo delle chart.

## Gate

External Sampling può avanzare a 500.000 iterazioni se soddisfa almeno una condizione:

1. riduce la TV seed-vs-seed di almeno il `20%` senza peggiorare la WMAE media di oltre `0,5 pp`;
2. riduce la WMAE media di almeno `0,5 pp` senza peggiorare la TV seed-vs-seed di oltre `0,5 pp`.

Tree fingerprint, normalizzazione, limiti di memoria e validità numerica devono restare invariati. Il confronto non autorizza modifiche all'astrazione.
