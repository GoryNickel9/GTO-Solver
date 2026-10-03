# Exact State Representation Feasibility Loop — 2026-08-30

> **CORREZIONE SEMANTICA 2026-09-04 — REPORT STORICO.** Le prove di identità,
> byte traffic e costo locale restano valide. Ogni conclusione che assume il
> vecchio gate memoria GTO+ o un cap desktop 2 GB è ritirata e va rivalutata
> sulla metrica solver-owned. Vedere il
> [`piano di correzione`](../../GTO_PLUS_SOLVER_MEMORY_SEMANTICS_AND_GATE_CORRECTION_PLAN_2026-09-03.md).

## 1. Conclusione

**EXACT REPRESENTATION BLOCKER PROVEN.** Con
`ScaledUint16RegretStrategy` byte-identico, la fase
`produce -> node-global max -> revisit -> encode` non è eliminabile con un
guadagno economicamente sufficiente. Un retain exact richiede almeno 32 bit
per regret e 31 bit per strategy pre-quantizzata, contro i 64 bit dei due
`float` correnti: saving informativo ideale 1,5625%, prima di metadata e
unpack. Recompute e mixed sono exact, ma misurano `0,896x` e `1,024x`, sotto il
gate `1,3x`.

Production, codec, checkpoint e memory gate GTO+ sono invariati. Nessuna
rebaseline o five-process è stata eseguita.

## 2. Stato iniziale e blocker ereditato

- HEAD/`main` iniziale: `3982e1353a7a4e4aab0428c81ccb49ed1b490886`.
- Dopo `git fetch origin main`, `origin/main` era lo stesso commit.
- Preservati `0d9a09b6`, `3982e135`, `.reasonix/` e `.tmp/`; nessun reset.
- Non ripetuti wavefront W4/W8, compiled traversal, continuation, AoSoA,
  supernode, sink, width-2, hero tiling, same-reach, persistent scale,
  state-pass fusion e partial/hierarchical encode già chiusi.
- Congelati exact alternating DCFR `alpha=1.5`, `beta=0`, `gamma=2`, delay
  zero, average reach-weighted `t^2`, signed regret, node-global scale,
  rounding, exact BR, fixture e otto thread.

Baseline TST: 202 iterazioni, traversal `174,926380 s`, certification
`32,780800 s`, solver `208,111772 s`. Serve traversal `<=96,208089 s`, cioè
`1,818x` e circa `45%` di riduzione.

## 3. Loop e obiettivo

```text
FORMALIZE -> OBSERVE -> DERIVE LOWER BOUNDS -> GENERATE REPRESENTATIONS
-> PROVE / FALSIFY -> RANK -> SHADOW PROTOTYPE -> BITWISE ORACLE
-> BENCHMARK -> ACCEPT / REJECT -> UPDATE MODEL -> REPEAT
```

```text
F = (mathematical_equivalence_fail, byte_identity_fail,
     hard_constraint_fail_count, feasibility_fail,
     projected_traversal_speedup_gap, transient_bytes_per_entry,
     measured_shadow_speedup)
```

Exactness e byte identity hanno sempre preceduto performance.

## 4. Equazioni exact del codec

Siano `c` il regret code letto come `int16_t`, `q` lo strategy code
`uint16_t`, `s_r/s_a` le vecchie scale `float` e `t` l'iterazione:

```text
R_old = float(int16(c)) * s_r
d+    = (t - 1)^1.5 / ((t - 1)^1.5 + 1)
d-    = 0.5
R_pre = float(R_old * (R_old > 0 ? d+ : d-)
              + (action_value - current_value))

A_old = float(q) * s_a
A_pre = float(A_old + t^2 * actor_reach * current_strategy)

M_r   = max_node(abs(R_pre))
M_a   = max_node(A_pre)
s'_r  = M_r > 0 ? float(M_r / 32767) : +0.0f
s'_a  = M_a > 0 ? float(M_a / 65535) : +0.0f
```

Con scala positiva, AVX usa `float(1/double(s'))`, moltiplicazione `float`,
round-to-nearest-even e saturazione; la coda usa
`nearbyint(double(value)/double(s'))`. I domini sono regret signed
`[-32767,32767]` nel container `uint16_t` e strategy `[0,65535]`; `-32768` non
è prodotto. Scala non positiva scrive zero. NaN/infinito sono errori a monte.

## 5. Dependency graph e impossibilità one-pass

```text
old code/scale ----> discounted old ----\
immediate/action values ----------------+--> prequantized entry --\
reach * current strategy * t^2 ---------/                         |
                                                                    v
                                                               global max
                                                                    |
                                                                    v
                                                             canonical scale
                                                                    |
prequantized entry -------------------------------------------------+--> code
```

Prima del max, un valore futuro maggiore può spostare la scala attraverso una
boundary `k+0,5` e cambiare qualunque code già emesso. Senza un upper bound
stretto sul massimo futuro, `safe_early_finalized` è rigorosamente `0%`.

## 6. Lower bound e costruzione avversaria

Per due `float` finiti distinti `x<y`, si può scegliere una scala futura `s`
con una half-boundary strettamente fra `x/s` e `y/s`, e aggiungere al node un
massimo valido `M=capacity*s`. Quindi `encode(x,s) != encode(y,s)`: ogni summary
che collide sui due valori non è exact.

Il test concreto usa `x=1.0f`, il `float` successivo `y`, un summary che scarta
il bit basso della mantissa e una scala con boundary fra i due. Summary uguale,
code diverso. La costruzione falsifica mantissa subset, bucket, interval non
singleton, provisional ratio troncato, delta troncato e residual incompleto.

Il regret signed deve quindi distinguere il bit pattern `float32` completo:
32 bit. Strategy è non negativa, quindi il segno noto porta il bound a 31 bit.
Il totale ideale è 63 bit/entry contro 64: saving massimo 1,5625%. Il journal
byte-aligned minimo resta due `float` da 8 byte.

## 7. Code stability, sparsità e rounding sensitivity

Telemetry reale, aggregata sui due alternating pass finali:

| Dataset | Entry | regret pre changed | strategy pre changed | regret code changed | strategy code changed | scale changed | boundary <=1/16 R/A |
|---|---:|---:|---:|---:|---:|---:|---:|
| AHK @20 | 1.173.034 | 99,971% | 38,612% | 86,398% | 76,469% | 94,236% | 11,324% / 10,556% |
| AHK @80 | 1.141.380 | 99,863% | 40,851% | 81,874% | 58,301% | 92,980% | 12,253% / 8,956% |
| TH @20 | 77.390.915 | 99,910% | 31,188% | 91,904% | 73,688% | 94,120% | 11,619% / 10,131% |
| TH @80 | 74.714.140 | 99,941% | 23,376% | 86,261% | 46,596% | 94,810% | 11,267% / 8,152% |
| TST @20 | 354.192.001 | 99,976% | 31,108% | 94,974% | 78,039% | 94,750% | 12,017% / 10,594% |
| TST @80 | 339.678.594 | 99,960% | 22,769% | 90,484% | 52,149% | 92,527% | 11,788% / 8,150% |
| TST @202 | 329.991.197 | 99,945% | 21,583% | 83,018% | 37,757% | 88,525% | 11,352% / 7,211% |

Le bucket di distanza dalla nearest half-boundary sono `<=2^-20`, `<=2^-16`,
`<=2^-12`, `<=2^-8`, `<=2^-4`, `<=0,25`, `>0,25`; l'ultima colonna somma le
prime cinque. Essere lontano consente bounds conservativi soltanto dopo aver
ristretto la scala; non fornisce l'upper bound sul max futuro. Late TST cambia
ancora l'83,018% dei regret code: sparse deve classificare tutto e trasportare
code/bitmap per la grande maggioranza.

## 8. Producer provenance

TST@202: fold `29,950%`, showdown `34,810%`, decision subtree `34,821%`, chance
`0,419%`, transformed direct `0%`. È la radice diretta dell'action-value;
trasformazioni interne restano nella famiglia subtree. Soltanto fold è cheap;
il 70,05% richiederebbe ripetere showdown/decision/chance per eliminare anche
gli action-value materializzati.

## 9. Candidate pool e byte/FLOP model

| Famiglia | Exact information | Transient | Recomputation | Decision |
|---|---|---:|---:|---|
| Compact journal | 32+31 bit | >=7,875 B/entry ideale | 0 | REJECT: <=1,5625% |
| Exponent + residual | tutti i bit | 8 B/entry | unpack | REJECT: `1,024x` |
| Tile provisional + correction | code + residual injectivo | >=8 B + metadata | correction | REJECT: collisione o nessun saving |
| Recompute | max soltanto | 0 B journal | producer due volte | REJECT: `0,896x` |
| Mixed retain/recompute | average bits, regret recomputed | 4 B/entry | parziale | REJECT: `1,024x` |
| Hierarchical/delayed | valori unresolved exact | >=lower bound | branch | REJECT: safe early `0%` |
| Sparse changed-code | code + bitmap/index | data-dependent | encode completo | REJECT: densità alta |
| Exact rounding-cell | cell id per ogni scala | injectivo | interval logic | REJECT: bound 63 bit |

Il modello misurato è circa `24,02–24,03 B/entry` fra state e scratch pass.
Il lower bound non riduce materialmente gli 8 byte dei due pre-values.
Recompute li elimina, ma raddoppia decode/discount/delta/average e relativi
load. Eliminare anche gli action values ripete i producer sopra.

## 10. Shadow implementation, oracle e performance

`gtosd_exact_state_representation_benchmark` include producer, summary write,
max, scale, finalization, encode e output. L'oracle richiede code e bit delle
scale uguali e include segno, zero, saturation, vector/tail semantics, domini
ridotti esaustivi e collisioni boundary.

Release `/W4 /WX`, Ryzen 4C/8T 3,6 GHz, 7 ripetizioni:

| Shadow | Mediana CPU | Speedup | Oracle |
|---|---:|---:|---|
| Legacy retain | 25,000 us | 1,000x | reference |
| Exponent + full residual | 24,414 us | 1,024x | PASS |
| Recompute ideale | 27,902 us | 0,896x | PASS |
| Mixed | 24,414 us | 1,024x | PASS |

Nessuno supera `1,3x`; non è stato costruito un path production.

## 11. Amdahl, RAM e feasibility

Con quota value/update autorevole 22%, anche applicare ottimisticamente
`1,024x` all'intera quota dà:

```text
S_traversal = 1 / (0.78 + 0.22 / 1.024) = 1.0052x
projected traversal ~= 174.02 s
projected solver    ~= 207.21 s
remaining required TST speedup ~= 174.02 / 96.208089 = 1.809x
```

Eliminare idealmente tutta la quota produce solo `1/0,78=1,282x`, sotto
`1,818x`. Per guadagnare 15% traversal servirebbe almeno `2,456x` sulla quota,
non `1,024x`.

Telemetry: counter thread-local, nessun duplicate state. Shadow: due journal
di 4 byte per 1.890 entry, 15.120 B più output. Nessuna seconda copia TST.

## 12. Ledger

| Loop | Hypothesis | Exact info | Extra bytes | Recompute | Expected gain | Oracle | Benchmark | Decision | Remaining TST speedup |
|---:|---|---|---:|---:|---:|---|---|---|---:|
| 1 | float bit journal | 64 bit | 8 B/e | 0 | <=1,5625% | PASS | reference | REJECT | 1,818x |
| 2 | exponent+residual | 64 bit | 8 B/e | unpack | <15% | PASS | 1,024x | REJECT | 1,818x |
| 3 | tile correction | injective residual | >=8 B/e | correction | <15% | proof | pre-gate | REJECT | 1,818x |
| 4 | recompute | max | bounded | full twice | uncertain | PASS | 0,896x | REJECT | 1,818x |
| 5 | mixed | retained average | 4 B/e | partial | uncertain | PASS | 1,024x | REJECT | 1,818x |
| 6 | hierarchical | unresolved values | >=bound | branches | 0% early | proof | pre-gate | REJECT | 1,818x |
| 7 | sparse codes | changed+map | variable | full encode | <15% | telemetry | pre-gate | REJECT | 1,818x |

## 13. Validazione e decisione production

- Build diagnostico Release `/W4 /WX`: PASS.
- Phase 7: PASS, 215 assertion.
- Phase 10: PASS, 10.593 assertion.
- Reference GTO+: PASS, 24 assertion; differenziale seriale/parallelo con
  massimo delta regret e strategy uguale a zero.
- Shadow oracle adversarial/exhaustive: PASS.
- Full Release CTest: 22/22 PASS, incluso lo smoke exact-state.
- AHK @20/@80, TH @20/@80, TST @20/@80/@202: completati.
- I fixed @20 non convergenti sono dataset diagnostici, non certification.
- Nessun candidate supera shadow `1,3x` o projected traversal 10–15%.
- Nessuna integrazione production, TST @5/@20 A/B o five-process, per gate.
- I quattro buffer, scale e checkpoint production non sono stati modificati.

La seconda fase globale è una conseguenza strutturale del contratto corrente:
o si conserva informazione equivalente ai due `float`, o la si ricomputa a un
costo almeno pari al traffic evitato. Codec o scale semantics differenti
appartengono alla task separata **New Production State Representation Study**.
