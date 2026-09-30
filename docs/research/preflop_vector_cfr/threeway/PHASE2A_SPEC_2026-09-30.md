> Specifica di progetto della fase 2a del 3-way (tabella esatta di equity a tre giocatori) così come è stata scritta nella notte del 30 settembre (in inglese, sola lettura del repository), con in fondo la critica indipendente (scritta dalle 00:16 alle 00:46 del 30 settembre, dopo il progetto delle 23:53-00:16; l'orario "~01:30" nel titolo della critica è sbagliato); lo stato di quanto fatto è nella sezione 9 di [MONKER_RECIPE_REPRODUCTION_2026-09-28.md](../MONKER_RECIPE_REPRODUCTION_2026-09-28.md).

# Phase 2a: three-player checkdown equity table (design spec)

Written 30 September 2026, 00:00-00:40. This is a design only: the repository was read, not edited, built or
tested. Code anchors refer to `feat/monker-step1-checkdown` HEAD `c7d6ba0`. The rake workflow is editing the
checkdown program, `compiled_game.*` and `game_config.*` right now, so the anchors in those files are HEAD
lines. In the working tree the checkdown program body sits 3 lines lower, because its header comment grew.
Numbers marked *estimate* have not been measured. Every other number cites its source.

---

## 0. Summary

- **What.** An exact table of 3-player showdown outcomes at the granularity of the **81 preflop classes**.
  There is one entry per ordered class triple (hero class H, first opponent class A, second opponent class B),
  81^3 = 531,441 entries in all. Each entry is computed for a fixed representative hero combo h of H. It
  stores two things:
  - N, the number of combo pairs (a in A, b in B) that are mutually disjoint with h;
  - 9 counts over the C(30,5) = 142,506 runouts: the hero's result against the first opponent (better, tie,
    worse) crossed with the result against the second.

  Size: 40 B per entry, **21.3 MB** in memory and on disk.
- **Why 81 classes and not 630 combos.** The class table is **exact for the step-1 trainer, not an
  abstraction**. The checkdown preflop game is suit-symmetric, so a class-level DCFR is the combo-level DCFR
  (section 2.1). A combo-level tensor would take 330 times the memory (about 7 GB) and give the same numbers.
- **Algorithm.** The build is hero-major: for each hero class and each board, it runs one pass over the pairs
  of opponents disjoint from the hero and the board, and increments a 236 KB per-thread accumulator that stays
  in cache.
  - Total work: 1.85e12 pair steps.
  - i3-10100F at 8 threads: *expected 10-20 min*. The upper bound is **40 min**, the time this work would take
    at the per-pair cost of the measured HU all-in build (53 s, `P2_EXACT_RESOURCES.md:85`).
  - 104-thread server: *1-2 min*.
  - An optional symmetry option cuts the work by 3.1 times.
  - Exact is cheaper than sampling at any useful precision, and has no noise.
- **Coverage.** The table serves every terminal of a 3-way step-1 tree:
  - 3-way checkdown and 3-way preflop all-in: the engine's 7 winner-mask rows;
  - 2-way showdown after a fold: the folded hand's cards are removed from the deck exactly;
  - fold terminals: N.

  Rake stays in the engine's payoff rows. The table itself has no rake in it.
- **Validation.**
  - Exact integer identities, including one against the HU table: summed over the folded hand, the 2-way
    counts equal 351 times the counts in `preflop_all_in_v1.bin`.
  - Checks that every combo in a class gives the same entry.
  - Brute force with the independent evaluator.
  - The user's equity-calculator-web-app, which enumerates 3 hands exactly. It has to be called directly,
    because its Python wrapper enumerates only for 2 hands.
- **Estimate.**
  - One agent: 7-10 h wall clock, plus under 1 h of CPU.
  - Two agents: 5.5-8 h.
  - Started after midnight, it closes on 30 September at about 05:00-07:00. Started at 09:00, it closes by
    about 14:30-17:00.
  - **Nothing in phase 2a needs 1 October** (section 5.5).

---

## 1. What exists for heads-up

### 1.1 Ranking rules (engine evaluator)

- `include/gtosd/equity/evaluator.hpp:14-24`: the `HandCategory` order is HighCard < Pair < TwoPair <
  ThreeOfAKind < **Straight** < FullHouse < **Flush** < FourOfAKind < StraightFlush. So **the flush beats the
  full house** and **the straight beats trips**.
- `libs/equity/src/evaluator.cpp:21-35` (`straight_high`): A-6-7-8-9 is the lowest straight (nine-high).
- `evaluator.cpp:86-129` (`evaluate_five_unchecked`): checks straight flush first, then quads, flush, full
  house, straight, trips.
- The user's web app has the same ranking in code:
  - `equity-calculator-web-app/cpp/src/hand_evaluator.cpp:10-18`: STRAIGHT_BASE 4,000,000 is above
    THREE_KIND_BASE 3,000,000, and FLUSH_BASE is above FULL_HOUSE_BASE.
  - `src/lib/handEvaluator.ts:17-25` has the same constants.
  - The wheel is at `hand_evaluator.cpp:95-99`.
- Its README says the opposite: `README.md:494` claims "Il tris batte la scala". The diary already records that
  the README is wrong and the code is right (`MONKER_RECIPE_REPRODUCTION_2026-09-28.md:691-693`). If a
  mismatch shows up on straight-against-trips boards during validation, check this first.

### 1.2 Rank table

- Declared in `include/gtosd/card_abstraction/rank_table.hpp:12-20`. It holds 16-bit ordinal ranks for all
  8,347,680 seven-card sets (colex index, `:31-32`), derived from the exact 5-card evaluator.
- `rank_of(hand, board)` is at `:45-46`.
- File: `out/preflop_blueprint_resources/rank_table_v1.bin`, 17,449,440 B. Built in 1.4 s
  (`P2_EXACT_RESOURCES.md:84`).
- Checked against the evaluator by `benchmarks/preflop_blueprint_resources.cpp:42` (`verify_against_oracle`,
  called at `:119`).

### 1.3 HU all-in / checkdown table (`AllInTable`)

- **Format** (`include/gtosd/card_abstraction/all_in_table.hpp:10-73`):
  - one `PairOutcome {wins, ties, losses}` of 3 x uint32 for every unordered combo pair;
  - triangular index `combo_pair_index` (`:35-44`), 198,135 entries;
  - 176,715 disjoint pairs, each summing to C(32,5) = 201,376 runouts (`:28-32`); overlapping pairs stay zero;
  - stored for the lower combo id; `outcome(hero, opp)` swaps wins and losses when needed
    (`all_in_table.cpp:114-124`).
- **File.** Resource kind `preflop_all_in_pairs` v1 (`all_in_table.cpp:16`, save and load at `:126-164`). The
  fnv1a64 fingerprint is chained to the rank-table fingerprint (`:100-112`).
  `out/preflop_blueprint_resources/preflop_all_in_v1.bin` = 2,377,708 B.
- **Build** (`all_in_table.cpp:18-98`):
  - board-major over all C(36,5) = 376,992 boards (`:67`);
  - per board, rank the 465 live hands, then compare every pair with an overlap check (`:36-54`);
  - per-thread counter arrays of 2.4 MB, merged at `:84-90`;
  - 4.07e10 pair steps, **53.2-53.4 s at 8 threads on the i3** (`P2_EXACT_RESOURCES.md:79-92`).
- **Combos and classes.** `ComboTable` (`include/gtosd/card_abstraction/showdown_counts.hpp:122-128`) holds
  cards, masks and `hand_class` for the 630 combos in `all_combos()` order.
  - Class ids come from `libs/core/src/ranges.cpp:19-34`: 0-8 are pairs (AA = 0), 9-44 suited, 45-80 offsuit.
  - `class_mass` gives 6, 4 and 12 combos per class (`ranges.cpp:36-44`).
  - `preflop_hand_classes = 81` (`card_abstraction.hpp:25`).

### 1.4 How the HU step-1 program uses it

`benchmarks/preflop_blueprint_checkdown.cpp` (HEAD lines):

- **Tree.** Heads-up only (`:505-507`). Compiled with `CompileOptions::checkdown_at_flop` (`:508-510`). Every
  flop entry becomes a `TerminalShowdown` with 5 cards to come (`compiled_game.cpp:124-128`). A preflop all-in
  is also a `TerminalShowdown` with `remaining_board_cards` = 5.
- **Payoff matrices.** For each showdown terminal and each seat there is a 630 x 630 matrix (`:124-158`). An
  entry is `(wins * p_win + ties * p_tie + losses * p_lose) / total`, where each p is
  `showdown_payoffs(node, winner_mask)[seat]` for the masks {seat}, {both} and {other}.
- **Traversal.**
  - Fold terminal: payoff times the disjoint mass of the opponent's reach (`:230-241`).
  - Showdown: a matrix-vector product (`:242-253`).
  - Vector DCFR with alternating updates (`:165-178`); exact best response takes the maximum over actions
    (`:283-289`).
  - Values are normalized by 630 x 561 (`:45`, `:182-190`).
- **Loading and charts.** The table is loaded at `:535-538`. Class charts are aggregated from the combos,
  weighted by own reach (`write_charts`, `:375-459`).
- **Performance.** HU50: 5,000 iterations in 75 s, gap 3e-5 % of the pot
  (`MONKER_RECIPE_REPRODUCTION_2026-09-28.md:74-76`).

### 1.5 Engine facts the 3-way table must match

- **Showdown payoff rows.** A showdown terminal has one payoff row per non-empty winner subset of its active
  players, 2^k - 1 rows (`compiled_game.hpp:139-146`, `compiled_game.cpp:294-301`, `:340-366`). With 3 active
  players that is 7 rows; with 2 active it is 3.
- **One pot, no side pots.**
  - `split_pot` splits one called pot, minus rake, among the winners, with odd chips as remainder
    (`libs/core/src/game.cpp:614-650`).
  - `GameConfig` has a single `effective_stack` (`game_config.hpp:44`). So every active player at a showdown
    has committed the same amount.
  - Therefore the **winner set** is all the payoff depends on, and a player outside the winner set always gets
    minus their commitment.
- **Rake.** `calculate_rake(config, pot, flop_dealt)` is `min(pct * pot, cap)` (`game.cpp:601-612`). It is
  applied inside `settle_terminal` (`game.cpp:664-684`), so it is already in every payoff row.
  - At HEAD a v1 config accepts only a disabled rake (`game_config.hpp:83`), and a checkdown leaf "would
    settle as no-flop-no-drop" (`compiled_game.hpp:108-111`).
  - The running rake workflow changes this: checkdown leaves and preflop all-ins are raked (the flop is
    dealt), preflop folds are not.
  - **The table does not depend on any of this.**

---

## 2. Design of the 3-player table

### 2.1 Granularity: 81 classes, and why that is exact

The step-1 game (preflop betting with a checkdown to the river) is invariant under the 24 suit permutations.

1. For any two combos h and h' of one class there is a permutation that maps h to h' and maps every class onto
   itself.
2. So if both opponents play class-constant strategies, and their reach is therefore class-constant, a hero
   combo's terminal value depends only on its class.
3. DCFR started from uniform strategies therefore stays class-constant at every iteration: equal values give
   equal regret updates.
4. The best response is class-constant too.

A class-level trainer is the combo-level trainer up to floating-point rounding. It needs only
W(H, A, B) = the sum over disjoint (a in A, b in B) of the outcome for a representative h of H.

Costs compared with the alternatives:

- **Combo level.** An ordered combo-triple tensor has 630 x 561 x 496 = 175,301,280 entries (7 GB at the same
  40 B). It would cost about 330 times the memory and trainer work for identical results.
- **Monte Carlo.** A standard error of 1e-3 on the share of every class triple needs about 2.5e5 deals per
  triple, about 1.3e11 triple evaluations. That is heavier than the exact build, whose enumeration is shared
  across triples, and it is noisy.

The user accepts sampling for 3-way (memory `multiway-exactness-not-required`), but here exactness is the cheaper
option.

The one assumption is class-constant opponent reach. It holds for the class-level trainer and for locked
MonkerSolver charts, which are class rows. It would fail only if strategies were ever made per combo inside a
class.

### 2.2 Content of an entry

For each ordered triple (H, A, B), with h = the lowest combo id of class H (fixed and stored in the file):

- `N(H,A,B)`: ordered pairs (a in A, b in B) with h, a and b mutually disjoint, a != b. It is at most 144
  (12 x 12).
- `cell[x][y](H,A,B)`: summed over those pairs and over the 142,506 boards drawn from the 30 remaining cards.
  - x is the hero's result against the first opponent a: 0 = hero better (W), 1 = tie (T), 2 = hero worse (L).
  - y is the hero's result against the second opponent b, with the same encoding.
  - Each cell is at most 144 x 142,506 = 20.5M, so uint32 is safe.
  - Invariant: the 9 cells sum to N x 142,506.

How the trainer reads an entry (hero seat s, other seats o1 and o2):

| Terminal | What it needs | From the table |
|---|---|---|
| 3 active, winners {s} | a and b both worse | W,W |
| 3 active, winners {s, o1} | tie with a, beats b | T,W |
| 3 active, winners {s, o2} | beats a, tie with b | W,T |
| 3 active, winners {s, o1, o2} | ties both | T,T |
| 3 active, s not a winner (any of the 3 masks) | payoff = -commitment, the same in every such mask | N x R minus the 4 cells above |
| 2 active (s vs o, f folded, f's cards dead) | win / tie / lose vs o | entry [H][class of o][class of f]: win = W,W + W,T + W,L; tie = the T row; lose = the rest |
| fold terminal, or s folded | constant payoff | N |

Here R = 142,506.

The 2-active case is exact **with the folded hand's cards removed from the deck**, because the 3-way
enumeration draws boards from the 30 cards outside h, a and b. It costs nothing extra.

Opponent-against-opponent results that the hero never needs (the L,L cell does not split who wins between a
and b) are not stored. Each seat reads its own slice with itself as the hero. With equal stacks nothing else is
needed (section 1.5).

### 2.3 Builder algorithm

```
for each hero class H  (tasks = (H, board chunk); per-task accumulator acc[81][81][3][3] uint32 = 236 KB)
  h = representative combo of H
  for each 5-card board beta from the 34 cards outside h   (C(34,5) = 278,256 boards)
    r_h = rank(h, beta)
    opps = the 406 combos disjoint from h and beta, each as (class, mask, state):
           state = W if rank(o, beta) < r_h, T if equal, L if greater      (407 rank lookups)
    for i < j in opps:                        (82,215 pairs; 71,253 of them disjoint)
      if mask_i & mask_j: continue            (branch-free: send overlapping pairs to a dummy slot)
      acc[c_i][c_j][s_i][s_j] += 1            (one increment per unordered pair)
  merge the chunks, then fold the unordered pairs into ordered views:
    cell[x][y](H,A,B) = acc[A][B][x][y] + acc[B][A][y][x]              (correct also for A == B)
  N(H,A,B) = count of disjoint (a, b)       (combinatorics only, no boards)
```

- **Work.** 81 x 278,256 x 82,215 = **1.853e12 pair steps**, plus 9.17e9 rank lookups.
- **Parallelism.** Split each hero class into board chunks, about 16 chunks per class (1,296 tasks). That
  keeps 8 or 104 threads busy.
- **Determinism.** All sums are integers, so the output bytes and the fingerprint do not depend on the thread
  count or the chunking. A test checks this.
- **Optional symmetry option.** Suit permutations that fix h map boards to boards with identical per-class
  contributions. There are 4 such permutations for a pair representative, 6 for suited and 2 for offsuit.
  - Enumerate only the lowest board of each orbit, and increment by the orbit size.
  - Averaged over the 81 classes this is 26.25/81 = 0.32 of the work, 3.1 times faster.
  - It is about 40 lines of code, and must give byte-identical output to the plain build on a subset.
  - Add it only if the plain full build exceeds about 30 min.
- **Not chosen.**
  - Inclusion-exclusion over shared cards: for each (hero, board) it costs about 134k multiply-adds against
    the pair loop's 82k steps. It only wins with SIMD, about 4-8 times. Keep it as plan B.
  - Board-major order with all heroes per board: 81 accumulators per thread (19 MB) would thrash the cache.

### 2.4 Size and format

- **Resource.** Kind `preflop_three_way_classes`, version 1, written through
  `libs/card_abstraction/src/resource_file.hpp`, like `AllInTable`.
- **Payload.**
  - uint64 entry count (531,441);
  - uint16[81] representative combo ids;
  - then entries in H-major, A, B order, each uint32 N followed by uint32 cell[3][3] (40 B).
  - Total 21,257,810 B plus the resource header, about **21.3 MB (20.3 MiB)** on disk and in RAM.
- **Fingerprint.** fnv1a64 over `"gtosd.card_abstraction.preflop_three_way_classes.v1|"`, then the rank-table
  fingerprint, then the payload (the pattern of `all_in_table.cpp:100-112`).
- **Location.** `out/preflop_blueprint_resources/preflop_three_way_v1.bin`. It is a portable resource: it
  depends only on the deck and the ranking (`card_abstraction.hpp:19-20`). Build it once on any machine and
  copy it.
- **Build memory.** Under 100 MB: the rank table (17 MB) plus 236 KB per thread.

### 2.5 Compute time

The upper bound scales from the measured HU build: 4.07e10 steps in 53.3 s is 7.6e8 steps per second at 8
threads. The HU build increments random slots of a 2.4 MB per-thread array (19 MB over 8 threads, more than the
6 MB L3). This design increments inside a 236 KB array that stays in L2/L3, hence the expected speed-up of 2-4
times.

| Step | i3-10100F, 8 threads | 2 x Xeon, 52 cores / 104 threads |
|---|---|---|
| Plain build | *expected 10-20 min*, upper bound 40 min | *1-2 min*, bound about 5 min |
| With the symmetry option | *3-7 min*, bound 13 min | under 1 min |
| Rank lookups (included above) | about 1-2 min | seconds |
| Identities V1-V7 on the full table (section 3) | under 1 min | seconds |
| Evaluator brute force, 50 class triples (V8) | *about 10-15 min* | *about 1-2 min* |
| Web-app enumeration, 300 combo triples (V9, single-threaded DLL) | *about 2-5 min* | the same |

- **Server figure.** 52 cores at about 0.7 of an i3 core's speed gives about 9 times the i3 (*estimate*).
- **First real number.** Build a subset of 3 hero classes (one pair, one suited, one offsuit) and multiply by
  81 over 3, weighting the class types, before launching the full build.

---

## 3. Validation plan (the phase 2a gate)

All checks are exact integer equalities unless stated otherwise.

- **V1. Cell total.** For every entry, the 9 cells sum to N x 142,506.
- **V2. Pair counts.**
  - N(H,A,B) = N(H,B,A).
  - |H|·N(H,A,B) = |A|·N(A,H,B) = |B|·N(B,A,H). Each side is the number of disjoint combo triples of those
    classes.
- **V3. Transpose.** cell[x][y](H,A,B) = cell[y][x](H,B,A).
- **V4. Swapping hero and first opponent.**
  - |H| · (sum over y of cell[W][y](H,A,B)) = |A| · (sum over y of cell[L][y](A,H,B)).
  - The same identity holds for T against T.
- **V5. Shares sum to the pot (3-way zero-sum before rake).** Define
  S(H;A,B) = 6·(W,W) + 3·((T,W) + (W,T)) + 2·(T,T), which is 6 times the hero's pot share. Then
  |H|·S(H;A,B) + |A|·S(A;H,B) + |B|·S(B;H,A) = 6 · 142,506 · |H| · N(H,A,B).
- **V6. Consistency with the HU table** (the "one hand folds" check). For every (H, A):
  - summed over F of (the W row of entry (H,A,F)) = **351** x the sum over a in A disjoint from h of
    `AllInTable::outcome(h, a).wins`;
  - the same for ties and for losses;
  - summed over F of N(H,A,F) = 496 x the number of a in A disjoint from h.

  Why 351: every board of 5 cards from the 32 cards outside h and a leaves C(27,2) = 351 possible folded hands.
  This checks the new builder against `preflop_all_in_v1.bin` built by the old one.

  As a diagnostic, also report the largest equity difference between the exact dead-card 2-way result and the
  HU value for the same (H, A), over F. That is the size of the folded-cards effect.
- **V7. Same entry for every combo of a class.** For 5 classes (1 pair, 2 suited, 2 offsuit), rebuild the
  entries with every combo of the class as hero. All must be byte-identical to the entry of the representative.
  This checks the class reduction directly.
- **V8. Independent brute force.** Pick 50 class triples:
  - random ones;
  - AA vs AA vs KK (shared ranks, many ties);
  - three suited hands of one suit, and same-suit collisions (flush over full house);
  - hands around A-6-7-8-9 (wheel);
  - trips-against-straight textures.

  For each, enumerate every combo pair and all 142,506 boards with `gtosd::evaluate_showdown`
  (`include/gtosd/equity/showdown.hpp`, the evaluator, not the rank table). Compare all 9 cells and N exactly.
  Tests run a smaller sample of about 5 triples.
- **V9. External check (equity-calculator-web-app).**
  - The DLL export `compute_equity_enumeration` (`cpp/src/equity_api.cpp:418-450`) calls
    `runTrueEnumerationAPI` (`:243-377`). That function enumerates exactly for up to 4 hands when the runouts
    are at most 1,000,000 (`:306-309`); 3 hands preflop have 142,506.
  - Call it with **3 combos directly** through ctypes. `cpp/equity_calculator.py:121-124` sends only 2-hand
    requests to enumeration; 3 hands go to Monte Carlo.
  - Compare each player's pot share with ours on about 300 random combo triples, tolerance 1e-12. The CLI needs
    a `--dump-triple` mode for this.
  - Guards:
    - The DLL's catch-all returns 1/n for every player (`equity_api.cpp:445-449`), so reject exact 1/3
      answers unless expected.
    - `cpp/equity_calculator.dll` is dated July 2025. Check that the export exists, or rebuild it from source.
- **V10. File integrity.**
  - Save/load round trip is equal.
  - The fingerprint is stable across 1 and 8 threads on a subset build.
  - File size as expected.

**Gate 2a.** All of the following must hold:

- V1-V7 hold on the full table;
- V8 shows 0 mismatches;
- V9 shows no difference above 1e-12;
- V10 passes;
- the measured build time and file size are reported.

---

## 4. How the 3-way checkdown step-1 trainer uses the table (phase 2b)

- **Tree.** Same compile path as HU: `checkdown_at_flop = true`. 3-way checkdown leaves and 3-way preflop
  all-ins are both `TerminalShowdown` with 5 cards to come, so one table serves both. After a fold the tree has
  2-active showdowns and fold terminals.
- **State.** Class-level vector DCFR over 81 classes.
  - Regrets and strategy sums are stored per (node, class, action).
  - Each seat has a reach vector rho_s[81], the per-combo reach of the class.
  - Iterate and discount as in `preflop_blueprint_checkdown.cpp:165-178`, alternating over the 3 seats.
- **Terminal precomputation** (the HU pattern of `:124-158`, generalized). For each showdown terminal t and
  each active seat s, build one double tensor K_{t,s}[H][A][B], where A is the first other seat's class and B
  the second's:
  - 3 active: (sum over the winner masks with s of payoff(mask)[s] x cell) / R, plus
    p_lose x (N - (W,W + T,W + W,T + T,T)/R). Mask mapping as in section 2.2. At load time, assert that
    `showdown_payoffs(t, mask)[s]` is equal for the 3 masks without s.
  - 2 active (s and o active, f folded): win, tie and loss counts from entry [H][class o][class f], divided by
    R, times the 3 rows. **Folded hand**: dead cards by default (exact). As a switch, `--folded-cards ignore`
    uses the HU table times N. It is not known which convention MonkerSolver uses, so the switch lets the two
    be compared against the user's charts.
  - s folded earlier, and fold terminals: payoff[s] x N[H][A][B]. The payoff is constant, so the N tensor is
    used directly and no per-terminal tensor is needed.
- **Values.** V_s(H) = sum over A and B of rho_o1[A] · rho_o2[B] · K[H][A][B]: build the 6,561-entry outer
  product once, then take 81 dot products.
  - Seat EV = sum over H of |H|·V_s(H) / 175,301,280, the number of ordered disjoint deals (HU uses 630 x 561,
    `:182-190`).
  - Best response: the maximum over actions per class, exact.
  - Report each seat's best-response gain in antes and as % of the pot. With 3 players CFR has no Nash
    guarantee, so the per-seat gain is the quality measure.
- **Rake on checkdown pots.** Nothing new in the table: it is all in the engine rows.
  - Check at load that for every terminal row the seats' payoffs sum to minus the rake of that terminal. This
    generalizes the rake workflow's new check to 3 seats.
  - Report the sum of the three EVs = minus the expected rake. This is exact because the traversal is exact.
  - Open point: `RakeConfig` has a single cap (`core/game.hpp:112-118`). Real sites cap by the number of players
    dealt, so a 3-handed config must carry the 3-handed cap (question Q2).
- **Cost** (*estimate*). One tensor is 531,441 doubles, 4.25 MB.
  - With about 30 three-active and 40 two-active showdown terminals: about 170 tensors, 0.7 GB (0.36 GB in
    float).
  - Per iteration each tensor is read once: about 0.7 GB of memory traffic, 35-50 ms on the i3.
  - 5,000 iterations take about 3-5 min; 20,000 take about 12-17 min. The server needs a fraction of that.
  - The actual terminal count is fixed by the 3-way tree. The user's 3-way 50a MonkerSolver set has 54 decision
    charts under UTG, CO and BTN.
- **Equivalence gate first.** Run a 2-player class-level mode that aggregates `preflop_all_in_v1.bin` to
  81 x 81 at load; it needs no new resource. On HU50 it must reproduce the combo-level
  `gtosd_preflop_blueprint_checkdown`: charts identical to 3 decimals, EV and gains within 1e-9 a. This checks
  aggregation, normalization and charts before any 3-way number exists.
- **Charts.** Class rows directly, in the MonkerSolver format writer of `:375-459`, with a 3-seat forward reach
  pass.
- **Limits.**
  - Unequal stacks (side pots) would need the full ordering of the 3 hands (13 weak orders); winner sets would
    not be enough.
  - 4-way: 81^4 = 43M entries, and 27 hero-centric cells would be 4.6 GB. A pair-loop build becomes a
    triple-loop build of about 1.6e14 steps, which is not feasible as is.
  - 4-6 way therefore needs sampling, buckets or the inclusion-exclusion sweep. That is outside phase 2a.

---

## 5. Implementation plan, estimate, parallelism

### 5.1 Files, and conflicts with the rake workflow

New files only, in a separate worktree and branch cut from `c7d6ba0` (for example `feat/threeway-table`):

- `include/gtosd/card_abstraction/three_way_table.hpp` and `libs/card_abstraction/src/three_way_table.cpp`:
  build (hero-class subset and thread count), the reference `count_combo_triple`, save/load, accessors,
  fingerprint.
- `benchmarks/preflop_blueprint_three_way_table.cpp`: build, V1-V7, V6 against `preflop_all_in_v1.bin`,
  `--dump-triple`, JSON summary.
- `tests/card_abstraction_three_way_tests.cpp` and `tools/three_way_webapp_check.py`.

Shared lines:

- `libs/card_abstraction/CMakeLists.txt`: not touched by the rake workflow.
- `benchmarks/CMakeLists.txt` and `tests/CMakeLists.txt`: **both modified by the rake workflow**. Each needs
  about 3 lines added; merge after the rake commit.

Phase 2b must be a **new** program file, because the rake workflow is editing
`preflop_blueprint_checkdown.cpp`.

### 5.2 Tasks and hours (agent wall clock, one agent)

| # | Task | Hours | CPU |
|---|---|---|---|
| T1 | Library (table, builder, reference triple counter, resource I/O, fingerprint), about 350 lines | 2.0-3.0 | none |
| T2 | CLI: build, identities, dump mode, JSON | 1.0-1.5 | none |
| T3 | Unit tests (2-class subset build, 5 brute-force triples, thread determinism, round trip), with MSVC `/W4 /WX` fix cycles | 1.5-2.0 | incremental builds 2-5 min each; a new build directory first compiles the libraries once, *10-20 min* |
| T4 | Web-app check script (direct 3-hand call, fallback guard, DLL check) | 0.75-1.0 | minutes |
| T5 | Timing probe (3 classes), full build, V8/V9 runs | 0.5 of work + 0.5-1.0 of waiting | full build 10-40 min at 8 threads, checks 15-25 min |
| T6 | Code review, fixes, diary and progress doc, commit on the branch | 1.0-1.5 | none |
| | **Total** | **7-10 h** (5.5-8 h with T4 and the brute-force test given to a second agent) | under 1 h on 8 threads |

Buffer: +30-50 % if an identity fails and needs debugging. The identities make such failures precise, not
vague.

For context, the step-1 trainer (phase 2b) is about one day:

- class-level 2-player mode plus the equivalence gate on HU50: 3-4 h;
- 3-way terminals, rake checks, per-seat best response and 3-position charts: 3-5 h after phase 2a;
- real 3-way 50a runs also need the phase 1 tree features: raise of 100 % of the pot and the sparse
  cold-call rules (`MONKER_RECIPE_REPRODUCTION_2026-09-28.md:739-744`).

The earlier "step 1 in 3-way about 3 days" (`:697-698`) comes down to about 2 days if the streams below run in
parallel.

### 5.3 What can run in parallel

- **Inside phase 2a.**
  - T1-T3 (builder) runs alongside T4 plus the V8 test (validation). Both code against the interface fixed in
    section 2.4.
  - The build itself is embarrassingly parallel over (hero class, board chunk).
- **Phase 2a alongside phase 2b, part 1.** The class-level 2-player trainer and its equivalence gate against the
  HU program need no 3-way table. Only the 3-way terminals wait for phase 2a.
- **Phase 2a alongside phase 1 (the 3-way tree).** Independent. But phase 1 edits `game_config.*` and
  `compiled_game.*`, which the rake workflow is editing now. Start phase 1 after the rake merge; phase 2a can
  start now.
- **Compute.** The full build needs the i3 for 10-40 min. It fits between runs. A 3-4 thread build during a
  training run takes roughly twice as long and slows the run. The server is not needed.

### 5.4 Timeline for 30 September (phase 2a)

**Machine.** The i3 is busy until **about 01:30** with the test 5 extension. Training resumed at 00:01 and was
at 29,500 iterations at 00:13, about 7.6 iterations per second. 40,000 iterations therefore end at about 01:00.
Then come two exact evaluations of about 13 min each (573 flops at about 1.4 s,
`out/monker/variants/HU50_lock_all/monker_values_exact.log`). Confirm with
`out/monker/variants/chain.log` ("evaluated test 5 at 64000") before any CPU-heavy step.

Scenario A: start now (agent 1 = builder, agent 2 = validation).

| Time | Agent 1 | Agent 2 | i3 |
|---|---|---|---|
| 00:30-01:45 | T1 and T2 in the new worktree (writing code, no heavy CPU) | T4 script, V8 brute-force test code | test 5 chain until about 01:30 |
| 01:45-02:45 | build the needed targets, T3 tests on a 2-class subset, fixes | DLL export check or rebuild, dry run of V9 on 20 triples | free |
| 02:45-03:00 | timing probe on 3 classes, extrapolate the full build | | |
| 03:00-03:40 | full build (expected 10-20 min, bound 40) | | 8 threads |
| 03:40-04:10 | V1-V7 (under 1 min), V8 on 50 triples (10-15 min) | V9 on 300 triples (2-5 min) | |
| 04:10-05:00 | T6: review, fixes, diary, commit | | |

Phase 2a done at **about 05:00**; with the buffer, 07:00 at the latest.

Scenario B: start at 09:00 in the daytime. The same durations give **14:30-17:00**. The only step that needs the
i3 free is the 30-40 min build window, around 12:00-13:00.

### 5.5 Why phase 2a does not need 1 October

- The work is 7-10 h of coding (5.5-8 h with two agents) plus under 1 h of CPU.
- Its inputs already exist: the rank table, `preflop_all_in_v1.bin` and the evaluator.
- It does not depend on phase 1 (the tree) or on the rake workflow, apart from 3 CMake lines merged
  afterwards.

A 1 October slot is justified only by one of these:

1. phase 2a is queued behind phase 1 or the rake merge on a single agent;
2. the i3 is booked by long runs all day on 30 September and the build is not run on 3-4 threads or on the
   server;
3. the old start of "after 03:00 on 30 September", daytime-only work and a buffer push it past the evening.

With the user's "proceed now" and a separate worktree, phase 2a closes on 30 September. Moving it from 1 October
to 30 September changes the schedule, so announce it explicitly (memory rule `roadmap-changes-explicit`).

---

## 6. Open points for the user

- **Q1. The folded player's cards at 2-way showdowns in 3-way trees.** Exact dead cards (proposed default), or
  ignored as in the HU table? Both are supported by a switch. It is unknown which one MonkerSolver uses, and
  the switch lets the two be compared against the 3-way charts.
- **Q2. Rake for 3-handed configs.** `RakeConfig` has a single cap. Which site and stake cap should the 3-way
  configs use?
- **Q3. The symmetry option in the builder.** Only if the first measured build exceeds about 30 min. Proposed:
  skip it.

## 7. Risks

- **Build-time uncertainty.** The per-pair cost is extrapolated, not measured. The 3-class timing probe settles
  it before the full build. Plan B is the symmetry option (3.1 times), then SIMD inclusion-exclusion.
- **The web-app DLL may be stale.** Its catch-all returns 1/n. Guarded in V9; if needed, rebuild the DLL from
  `cpp/`.
- **The class reduction.** It rests on class-constant strategies, and V7 checks it directly. Any future
  per-combo locking inside a class would break it. MonkerSolver charts are class rows, so they do not.
- **Merge friction** with the rake workflow on two CMakeLists files. The additions are trivial; merge after the
  rake commit.

---

## Critique (independent review, 30 September ~01:30)

Method: read-only. I re-derived the counts, read the HEAD code (AllInTable build, the checkdown program, core
settlement) and the web-app enumeration, and counted the terminals of the real 3WAY50 checkdown tree with the
phase 1 oracle. I also probed the Monker EV columns with the web-app DLL (`ev_convention_probe.py` in this
folder, single-threaded, about 35 s per hand).

### V-A. Confirmed

- **Counts and sizes.**
  - Work: 81 × C(34,5) = 278,256 boards × C(406,2) = 82,215 pairs = 1.853e12 pair steps.
  - Disjoint pairs per board: 406 × 351 / 2 = 71,253.
  - Rank lookups: 81 × 278,256 × 407 = 9.17e9.
  - Payload: 21,257,810 B.
- **§2.3, unordered pairs folded into ordered cells.** `cell[x][y](H,A,B) = acc[A][B][x][y] + acc[B][A][y][x]`
  is correct, also for A = B: the pair {i, j} lands in cell[s_i][s_j] through the first term and in
  cell[s_j][s_i] through the second.
- **Identities.** V5 is pot conservation with shares 6/3/2. The V6 factors C(27,2) = 351 and C(32,2) = 496 are
  right. `AllInTable::outcome` swaps wins and losses for the higher id (all_in_table.cpp:114-124), as V6
  assumes.
- **§2.1, the class reduction.** It is exact given class-constant opponent reach, and V7 checks it directly.
- **Equal stacks mean the winner set is enough.** `split_pot` gives odd chips to the lowest-seat winners
  (game.cpp:636-646), so the winners' payoffs can differ by a unit. The trainer must read the payoff per mask
  and per seat, as §4 says; it must not derive one share from another.
- **Web-app anchors.**
  - `runTrueEnumerationAPI` is at cpp/src/equity_api.cpp:243-377. It falls back to Monte Carlo above 1,000,000
    runouts or 4 players (:306).
  - `compute_equity_enumeration` is at :418-450, with the 1/n catch-all.
  - Tie shares accumulate as doubles. The error for 3-way chops is about 1e-14 or less, so the 1e-12
    tolerance of V9 holds.

### V-B. Corrections

1. **Terminal count (§4 "Cost").** The real 3WAY50 checkdown tree has:
   - 13 three-active showdown terminals (9 all-in runouts and 4 checkdown leaves);
   - 38 two-active showdown terminals (27 and 11);
   - 25 folds.

   That is 3·13 + 2·38 = **115 tensors, not about 170**: 0.49 GB in double, 0.24 GB in float. Per iteration
   that is about 6.1e7 multiply-adds, about 25-35 ms on the i3. The 5,000 and 20,000 iteration times in §4
   stay about the same.
2. **Which DLL to use.** `cpp/equity_calculator.dll` (July 2025) is not the one the Python wrapper loads.
   `cpp/equity_calculator.py` loads `cpp/build/Release/equity_calculator.dll`, built 14 May 2026.
   - That DLL exports `compute_equity_enumeration` and works through ctypes: I used it for the probe (about
     35 ms per 2-hand matchup).
   - No rebuild is needed. V9 should point at this path and assert the export exists.
3. **Chart writer (§4 "Charts").** Do not reuse `preflop_blueprint_checkdown.cpp:375-459`. It has its own
   naming walk (:387-401) without the multiway fold rule, so 19 of the 54 names would be wrong.
   - Use `mc::chart_nodes` / `mc::write_charts` after phase 1c.
   - So phase 2b depends on 1b **and 1c**, not only on "the tree".
4. **Stale dependency (§5.2, last bullet).** "Sparse cold-call rules" are not needed. Phase 1 rebuilt all 54
   files with the pot raise alone and found no cold-call filters. Remove the item.
5. **Q2 is already answered for reproduction.** The user's statement of 29/09 23:00 covers the HU and 3-way 50a
   charts: 5 %, cap 3a, no flop no drop. A cap per player count matters only for product configs, so the
   single cap of `RakeConfig` is fine for 3WAY50.
6. **Q1 can be decided from data, not only by chart comparison.** The 3-way 60a solve has Call_EV and Fold_EV
   columns. It sits in both the "40a" and "60a" folders, which are byte-identical, and its EVs fit a 60a stack;
   see the phase 1 critique, K2.
   - **Two-way nodes.** HU all-in call nodes with a folded third player (`BTN/UTG_AllIn_BTN`,
     `BTN/CO_AllIn_BTN`, …) depend only on the shover's and the folder's chart rows. So each class's call EV
     can be computed exactly from the new table, under each convention: {folded cards dead, ignored} × {rake
     3a, none}, with a stack of 60a including the ante.
   - **Three-way nodes.** The 3-way all-in nodes (for example `UTG/UTG_7.0ante_CO_AllIn_BTN_Call_UTG`, where
     AA's Call_EV is 43.786) test the 3-active cells against Monker directly.
   - **First probe.** Heads-up equities, folded cards ignored:
     - with the cost fixed at 60a, the fitted pot after rake is 118.85a (3a rake gives 118);
     - residuals are ±0.3a;
     - without rake the residuals are a consistent −1.3a.
   - **What to expect.** The exact convention should reproduce Monker's 3-decimal EVs to about 0.01a, and the
     wrong ones should miss by 0.3-1.4a.
   - **V11 (new).** Add this check as V11: about 1-1.5 h of work, seconds of CPU. It is not a gate for 2a; it is
     the gate for the terminal model of 2b. It also weakly tests the hand ranking.
   - **Memory note correction.** `user-equity-calculator.md` says "trips > straight", but the web-app code
     (`hand_evaluator.cpp:14-15`) and our evaluator both put the straight above trips. The README is the outlier,
     as §1.1 says. Correct the note.
   - **Caveat.** The 60a set is from August 2026 and the 50a set from September 2025, so the conventions may
     differ between solves.
7. **Equivalence gate threshold (§4).** 1e-9 a between combo-level and class-level DCFR after 5,000 iterations
   is too tight. The summation order differs, and regret matching amplifies differences wherever a regret
   crosses zero. Use 1e-6 a on EVs and gains and 0.001 on chart frequencies. If you want a tight check too,
   use 1e-9 a after 100 iterations.
8. **Seat mapping in two-active tensors.** When the folded seat is the first other seat (o1), the entry needed
   is (H, class of o2, class of o1), while the reach product is ρ_o1 ⊗ ρ_o2, so it must be transposed. This is
   an easy bug. Add a test that takes each of the three seats as the folder and compares against a brute-force
   combo-level value on a small random reach.
9. **Build time (§2.5).**
   - **"Upper bound 40 min" is not a bound.** It extrapolates the HU per-pair cost: 53.3 s × 4 cores × about
     4 GHz / 4.07e10 is about 21 cycles per pair, mostly mispredicted rank branches and a 2.4 MB random
     scatter.
   - **The new loop is cheaper per pair, but has its own limit.** It is branch-free, but if the opponent list is
     sorted by class it increments the same slot several times in a row, and store-to-load forwarding (about 5
     cycles) then limits it. It is still cheaper per pair than the HU loop, so 10-20 min on an idle i3 is
     plausible.
   - **The i3 will not be idle.** After the rake commit the HU rake runs are queued for about 2.5-3 h. At 4
     threads next to a training run, expect **20-80 min**. The 3-class probe first stays right.
   - **Cheaper plan B than SIMD inclusion-exclusion.**
     - Store `acc[c_i][s_i][c_j][s_j]`.
     - For each opponent i, add one 243-entry vector: the counts per (class, state) of the later opponents,
       minus the at most 54 of them that share a card with i.
     - Keep the suffix counts by decrementing as i advances.
     - That is about 23k operations per board instead of 82k scalar pair steps (about 3x), with no change to
       the output bytes.
10. **V8 cost.** One class triple needs up to 144 × 142,506 × 3 = 6.2e7 seven-card evaluations. At an
    unmeasured 0.3-1 µs per `evaluate_showdown`, 50 triples take 15-50 min on one thread, or 3-8 min at 8
    threads. Thread it, or cut V8 to 20 triples plus the special textures.
11. **New build directory (T3).** On a CPU shared with a training run, the first library build takes 20-40 min,
    not 10-20. If the vcpkg binary cache misses in the new worktree, add up to 30 min.

### V-C. Timeline: internal inconsistency and corrected times

- §5.2 sums T1-T6 to 7-10 h (5.5-8 h with two agents), but the Scenario A table fits them into 4.5 h
  (00:30-05:00):
  - T1+T2 in 1.25 h, against 3-4.5 h in §5.2;
  - T3 in 1 h, against 1.5-2 h;
  - T6 in 50 min, against 1-1.5 h.
- It also marks the i3 "free" from 01:45 and ignores the HU rake runs queued after the rake commit.
- §0 ("05:00-07:00") inherits both errors.

Corrected plan, two agents, starting about 01:00 (R = rake commit, expected 02:00-03:00):

| Time (30/09) | Agent 1 (builder) | Agent 2 (validation, then phase 2b part 1) | i3 |
|---|---|---|---|
| 01:00-04:30 | T1 + T2 in a new worktree; first library build (20-40 min, shared CPU) | T4 (DLL from build/Release), V8 brute-force code; then 2b part 1: class-level HU mode + HU50 equivalence gate (3-4 h, needs no 3-way table) | test-5 extension until about 01:30; R; HU rake runs from about R + 15 min for 2.5-3 h |
| 04:30-06:30 | T3 tests and fix cycles | 2b part 1 continues | HU rake runs |
| 06:30-08:00 | 3-class probe, full build (20-80 min at 4 threads; 10-40 min if idle) | V9 dry run, 2b part 1 review | mostly free after about 06:00 |
| 08:00-08:45 | V1-V7, V8 (threaded), V9 on 300 triples | | |
| 08:45-10:15 | T6: review, fixes, diary, commit on the branch | | |

- **Phase 2a:** done about **09:00-10:30** on 30/09. Worst case about 12:30, if an identity fails and needs
  debugging.
- **One agent:** 7-10 h in sequence plus CPU waits gives about 10:00-13:00.

### V-D. Why phase 2a was placed on 1 October

- Nothing in its inputs or dependencies requires it. The rank table, `preflop_all_in_v1.bin` and the
  evaluator exist, and it shares only about 3 CMake lines with the rake diff.
- The slot is an artifact of a single-lane queue. Queued behind the rake commit and phase 1 on one agent, it
  would start at about 10:00-11:00 and take 7-10 h plus CPU waits. That ends late on 30/09, and with a buffer
  it becomes 1/10.
- With its own worktree and a second agent, it closes on the morning of 30/09.
- Moving it from 1/10 to 30/09 is a roadmap change, so announce it explicitly
  (`roadmap-changes-explicit`).

### V-E. Consequence for the first 3-way step-1 charts (phase 2b)

- **What 2b part 2 is.** 3-way terminals, rake row checks, per-seat best response, 3-position charts through
  `chart_nodes`, and V11.
- **What it needs.** Phase 1b + 1c (about 06:00-09:00 with the corrected phase 1 plan) and 2a (about
  09:00-10:30).
- **Timing.** It starts about 10:30 and takes 4-6 h of coding plus 1-1.5 h of review. The step-1 runs take
  minutes, and the comparison with the 54 charts (`compare_charts.py` works for 3-way) takes 0.5-1 h.
- **First 3-way step-1 charts:** about **16:30-19:30** on 30/09. Worst case about 22:00. If the equivalence
  gate or V11 exposes a terminal-model problem, it slips to 1/10.
- **The earlier "16-18" target** holds only if the three lanes (phase 1, 2a builder, 2a validation + 2b
  part 1) start now.
