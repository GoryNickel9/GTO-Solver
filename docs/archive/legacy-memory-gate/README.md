# Archivio legacy del falso gate memoria

Stato: **storico / non normativo**

Questa directory conserva i report il cui oggetto principale era il confronto
fra Peak RSS e i valori GTO+ `8/399/2.000 MB`, oppure l'adozione di un cap
desktop 2 GB/2 GiB. La correzione semantica del 2026-09-04 ha stabilito che:

- i tre valori provengono dal campo UI GTO+ “Memory needed for solving”;
- quel campo non è Peak RSS, working set totale o private bytes totale;
- non è mai esistito un limite desktop indipendente `<2 GiB`;
- la composizione interna della stima GTO+ resta non identificata;
- i PASS/FAIL memoria dei report archiviati non sono comparabili;
- i campioni OS, i profili, i conteggi e gli altri risultati grezzi restano
  evidenza storica e non vengono riscritti.

Autorità corrente:
[`GTO_PLUS_SOLVER_MEMORY_SEMANTICS_AND_GATE_CORRECTION_PLAN_2026-09-03.md`](../../GTO_PLUS_SOLVER_MEMORY_SEMANTICS_AND_GATE_CORRECTION_PLAN_2026-09-03.md).

Documenti conservati:

- `PEAK_RSS_AUDIT_2026-09-03.md`: chiusura storica del falso gate Peak RSS;
- `STRICT_2GB_EXACT_ALGORITHM_RECHECK_2026-09-01.md`: recheck algoritmico sotto
  il falso cap decimale;
- `TST_STRICT_2GB_BOTTLENECK_ATTRIBUTION_AND_FEASIBILITY_LOOP_2026-08-31.md`:
  profiling e feasibility TST sotto il falso cap;
- `TWO_GIB_RESOURCE_CONTRACT_AND_FRONTIER_RECHECK_2026-09-01.md`: adozione
  storica del cap 2 GiB successivamente ritirato.

Nessun documento è stato cancellato perché ciascuno contiene evidenza univoca.
Il loro posizionamento in archivio impedisce che vengano interpretati come
specifiche o gate correnti.
