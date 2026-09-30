# tools/independent: independent references for the correctness checks S1-S6

These scripts check the parts of the game that the trainer (`gtosd_preflop_blueprint_train`) and the exact
evaluator (`gtosd_preflop_blueprint_monker_values`) share. NashConv cannot see a bug in a shared part: both
programs would solve and measure the same wrong game. So each part is rebuilt here in Python, from the
rules of short-deck no-limit hold'em, and compared with the engine's files or outputs.

The design is `shared_components.md` (coverage study of 30/09/2026, section 3), with the critic's
corrections. S5b (per-flop brute force) and S7 (rules against MonkerSolver) are not implemented. The
critic dropped S7: the recipe diary already confirms that MonkerSolver ranks a straight above trips.

## Independence rule

- **Written from the rules.** The game logic is written from rules stated in the module docstrings:
  hand ranking, the betting rules, pots, uncalled returns, payoffs, rake, chance, suit symmetry and board
  orbits. It is not translated from the C++.
- **What was read in the C++.** Only the choices that are not rules: file formats, index orders, the
  config format, and the engine's betting conventions.
- **Every such choice is a named convention in a docstring:**
  - `cards.py`: C1-C5, the card index, colex subsets, combo order, pair index and class order.
  - `resources.py`: F1-F3, the resource container, the rank table and the all-in table.
  - `rules.py`: C1-C16, the betting and settlement conventions. 11 of them can be switched to an
    alternative.
  - `boards.py`: K1-K6, the permutation order, packed codes, the canonical representative, and the
    catalog and bucket-table formats.
  - `policy.py`: P1-P4, the `policy.bin` format and its layout.
  - `step1.py`: K1-K3, the LP scaling, unreached information sets and clipping.
  - `sd_designer_policies.py`: O1-O6, the evaluator's output definitions.
- **Hand ranking is fully independent.** `ranking.py` was written without reading `evaluator.cpp`,
  `rank_table.cpp` or the board loop of `all_in_table.cpp` beyond the file format.
- **The betting referee is only partly independent.** Its author read `game.cpp`, `game_model.cpp` and
  `compiled_game.cpp` before writing `rules.py`. R1-R13 are the standard no-limit rules, and the code is
  structured differently from the engine (must-act set, no-re-raise set, net against gross accounting).
  The abstraction conventions C3-C10 follow the engine on purpose. For those, the referee is a
  consistency check between config and tree, not an independent check of the sizing against
  MonkerSolver.

Short-deck rules used by the engine and by `ranking.py`:

- 36 cards, 6 to A.
- Categories: straight flush > quads > flush > full house > straight > trips > two pair > pair > high
  card.
- A-6-7-8-9 is the lowest straight and the lowest straight flush.
- `--straight-vs-trips trips` switches the straight and trips order. It exists only to show that the
  check can fail.

## Package `sdref`

| Module | Content | Used by |
|---|---|---|
| `cards.py` | cards, 24 suit permutations, colex subsets, the 630 combos in engine order, the 81 classes | all |
| `ranking.py` | `evaluate_five` / `evaluate_seven` from the rules, `RankTables`, board ranking, all-in counts (`pair_counts_direct`, `allin_counts_exhaustive`, `class_pair_counts`) | S1, S2 |
| `resources.py` | readers of `rank_table_v1.bin` and `preflop_all_in_v1.bin` (FNV-1a trailer and fingerprint recomputed), `write_python_allin` / `read_python_allin` for `allin_counts_python.npz` | S1, S2, S4, S5a, S6 |
| `rules.py` | config parsing, betting rules R1-R13, action abstraction, transitions, settlement and rake; conventions C1-C16 | S3, S4, S5a |
| `step1.py` | step-1 tree, sequence-form LP (scipy HiGHS), combo-level certificate | S4 |
| `policy.py` | `policy.bin` header, layout and writer | S5a |
| `boards.py` | orbits, stabilizers, Burnside counts, readers of `board_catalog_v1.bin` and the bucket tables | S6 |

`sdref/__init__.py` imports only `cards`, `ranking` and `resources`. The other modules are imported
explicitly (`from sdref import rules`).

## The checks

The times are for one core on this machine with no other load. They get longer while the night runs use
the CPU. The build and ctest (S3) are the only steps that need anything compiled.

### S1 `sd_rank_check.py`: hand ranking, `rank_table_v1.bin`

**What it proves.** Every ranking decision that the trainer and evaluator can make follows the rules:
river showdowns, all-in runouts and the all-in table.

**Checks:**
- **Python against itself:**
  - P0 cards invariants.
  - P1 per category, the five-card set counts and the number of distinct values equal closed forms
    derived from the rules (1,404 distinct values in all).
  - P2 the direct 7-card evaluator equals the 5-card evaluator on all 376,992 five-card sets.
  - P3 the direct evaluator equals the best of the 21 five-card subsets on all 8,347,680 seven-card sets.
- **Engine comparisons:**
  - E0 the file parses, its trailer checks out and its fingerprint is recomputed.
  - E1 the five-card ordinals match exactly, and E2 the seven-card ordinals match exactly.
  - E3 the weak orders are equal (1,404 and 752 levels).
  - V1 the combo labels and classes of an evaluator `values.json` match. The default file is the P1
    smoke `it_50`.

**How it can fail.** With `--straight-vs-trips trips` it reports 22,248 five-card and 1,776,128
seven-card mismatches.

**Cost:**
- about 2 min: reading the file with its checksum 8 s, tables 16 s, P2 4 s, P3 about 85-95 s, engine
  checks 7 s;
- 0.7 GB commit;
- output in `out/monker/correctness/independent/S1_rank/`.

### S2 `sd_allin_check.py`: heads-up all-in table, `preflop_all_in_v1.bin`

**What it proves.** Every preflop all-in pair outcome is right, and so is the checkdown leaf of step 1.

**Engine file only:**
- A0 the file parses, and its fingerprint derives from the rank table's fingerprint.
- A1 every disjoint pair sums to W + T + L = 201,376, and the overlapping pairs are 0.
- A2 the table is invariant under all 24 suit permutations.

**Compared with Python:**
- S (`--sampled N`) N random pairs plus 10 edge pairs, each enumerated on its own boards. The integers
  must be equal.
- X (`--exhaustive`) every board, every pair: exact equality on all 176,715 disjoint pairs, first
  checking the fast ranking path against the sort-based one.
- X2 the sampled pairs equal their exhaustive counts.
- The exhaustive run writes `allin_counts_python.npz`, the input of S4 and S5a.

The Python 7-card ordinals are rebuilt from the rules (about 20 s) unless `--python-ordinals` is given.
They never come from the engine.

**Cost:**
- `--sampled 2000 --exhaustive --processes 4` takes about 4 min: the sampled pairs at about 41 ms each
  (about 85 s), then the exhaustive pass at about 0.75 ms per board (about 280 CPU-s, 70-100 s on 4
  processes);
- memory: parent 0.5 GB and each worker 0.3 GB.

**Don't edit** `tools/independent/` during the first minute. The worker processes import `sdref` when
they start.

### S3 `sd_referee.py`: tree, amounts, pots, uncalled returns, payoffs and rake

**Input.** A JSON Lines dump of `gtosd_preflop_blueprint_game --dump-nodes`. The option is in
`benchmarks/preflop_blueprint_game.cpp`, since commit fdf8114, and needs the build.

**Checks:**
- **D, dump structure.** It also checks that the dump's `tree_fingerprint` equals the one recorded for
  the runs, so the conclusions on the new build transfer to the runs. The recorded fingerprints are
  built in for the HU6 family of `benchmarks/monker/correctness/README.md`; `--expect-tree
  STEM[#MODE]=FP` adds more.
- **R, replay with the rules.** It checks every action's legality and amount, the state at every node,
  and every payoff row, rake included.
- **A, action abstraction.** Each node's actions equal the configured abstraction, and the engine's path
  set equals the referee's own enumeration.
- **C, labels and edge order.**

**Classification.** A failure that goes away when one convention takes an alternative value, without
adding a new failure, is reported as a convention mismatch.

**Exit codes:** 0 pass; 1 rule, abstraction or dump failure; 3 only conventions differ (ctest counts this
as a failure); 77 skip.

**How it can fail.** `--self-test` needs no engine. It passes dumps of the referee's own trees, catches a
payoff changed by +1 and a call changed by +1, and attributes every alternative-convention dump to its
convention. It takes 2-17 s per config.

**Registered in CTest** with labels `preflop_blueprint;independent`:
- `gtosd_preflop_blueprint_independent_referee_selftest` needs Python only;
- `gtosd_preflop_blueprint_independent_referee` covers 17 configs: the HU6 family, the HU50_step2
  family, 2size_donk, CO40 test, `HU50_rake#checkdown` and `3WAY50_donk_rake25cap2#preflop_only`.

**Conventions no config exercises:**
- C2 rounding: every percentage comes out exact;
- C8 cap boundary;
- C11 odd chip: heads-up pots always split evenly.

### S4 `sd_step1_lp.py`: exact value of the heads-up step-1 game

**Method:**
- The tree comes from `rules.py` and the chance weights and showdowns from S2's Python counts.
- Two sequence-form LPs are solved with HiGHS.
- A certificate is computed at combo level (630 combos, exact card removal):
  lower = BTN's best response to the CO LP strategy, upper = CO's best response to the BTN LP strategy.
  It holds whatever the LP's accuracy.
- The certified value must fall inside the engine's window [EV0 - gain1, EV0 + gain0], from
  `out/monker/step1/HU50/summary.json`.

**How it can fail.** Three wrong games must fall outside the window: no card removal, ties paid to the
BTN, and stacks one ante shorter. If one falls inside, the window cannot tell the games apart and the
case is INCONCLUSIVE (exit 4).

**Results:**
- HU50: v* = -0.121044566662 a, 43 % of the way across a window 1.6e-6 a wide. This was VERIFIED with
  the engine table standing in for the Python counts.
- HU6_all: VALUE_ONLY, because no engine step-1 summary exists for it.
- Raked configs are refused: the game is not zero-sum.

**Exit codes:** 0 PASS or VALUE_ONLY, 1 FAIL, 2 input error (for example, no npz), 4 inconclusive.

**Cost:** about 5 s, 0.22 GB.

### S5a `sd_designer_policies.py`: step-2 EV identities with designer policies

**Method.**
- Python writes `policy.bin` files whose postflop rows ignore the cards: policies a-h on HU6_all and
  HU6_all_rake25cap2.
- It runs the frozen exact evaluator (`out/monker/bin_correct/c123`, `--all-flops`).
- It compares the evaluator's results with a closed form computed from S2's counts, the referee's payoff
  rows and the preflop reach, to within 1e-9 a:
  - EV0 and EV1;
  - the 630 root values;
  - at each preflop node: strategy read back, opponent reach, class weights, combo values, class EVs;
  - best_response_preflop and gain_preflop, with the expected rake or the zero sum.
- When a `--dump-nodes` build exists, the engine dump must equal the referee's tree, payoff rows
  included.

**What it proves.** The step-2 payoff composition over 605,088 boards is right: per-board ranks, the
river kernels, the runout aggregation, card removal and the ante scale.

**What it does not prove.** Ranking effects that depend on the board under hand-dependent postflop play
(that is S5b).

**Checks on the closed form itself.** `--self-test` needs no engine and takes about 6 s. It checks that
the recursion equals a separate sum over terminal paths, the zero sum, the symmetric lines, and a hand
calculation.

**Cost:**
- about 30 min for 16 evaluations at `--threads 2` (INFERRED from P1's 23.5 s at 8 threads);
- 68 MB per policy, deleted once it passes;
- the script uses about 0.4 GB, the evaluator about 0.2 GB.

### S6 `sd_catalog_check.py`: board catalog and exact bucket tables

**Checks:**
- **Python against itself:**
  - P0 invariants.
  - P1 the Burnside counts equal the orbits found by enumerating every physical object, including all
    7,539,840 histories.
- **Catalog:**
  - C0 the file parses and its fingerprint is recomputed.
  - C1 one entry per suit-isomorphism class, weighted by its orbit size. This check depends on no
    convention.
  - C2 the convention for the canonical representative.
  - C3 the cross references.
  - C4 the chance weights 33x, 33·32x, 32x and 20x.
- **Every table under `out/monker/correctness/buckets`:**
  - T0 the file parses, and its catalog fingerprint equals the recomputed one.
  - T1 exactly the overlapping combos hold 0xFFFF.
  - T2 ids are constant on stabilizer orbits.
  - T3 on streets flagged exact, the id partition equals the orbit partition, and the capacity equals the
    largest orbit count.
  - T4 random physical boards, trying every permutation that maps the board to its canonical form.

**How it can fail.** Tested in memory: merging two orbits fails T3 and T4, splitting one fails T2 and
T3, an id on a dead combo fails T1, and swapping two rows fails T1 to T4.

**Cost:** about 3.5-4 min, 0.9 GB peak (river tables). `--skip-checksum` makes it much faster.

## Run order after the build window (Git Bash, repo root)

```
export OPENBLAS_NUM_THREADS=1; D=out/monker/correctness/independent; mkdir -p $D
python tools/independent/sd_rank_check.py > $D/S1_rank.log 2>&1; echo "S1 exit $?"
python tools/independent/sd_allin_check.py --sampled 2000 --exhaustive --processes 4 > $D/S2_allin.log 2>&1; echo "S2 exit $?"
python tools/independent/sd_step1_lp.py > $D/S4_step1.log 2>&1; echo "S4 exit $?"          # needs S2's npz
python tools/independent/sd_catalog_check.py > $D/S6_catalog.log 2>&1; echo "S6 exit $?"
# after `cmake --build out/build/windows-release-suite --target gtosd_preflop_blueprint_game` (vsdev wrapper):
ctest --test-dir out/build/windows-release-suite -R "gtosd_preflop_blueprint_independent_referee|gtosd_preflop_blueprint_game_report" --output-on-failure
python tools/independent/sd_referee.py --executable out/build/windows-release-suite/benchmarks/gtosd_preflop_blueprint_game.exe \
    --scratch-dir $D/S3_referee --report $D/S3_referee/report_HU20_deep.json <scratchpad>/correctness/deep/HU20_deep.json
python tools/independent/sd_designer_policies.py --threads 2 > $D/S5a_designer.log 2>&1; echo "S5a exit $?"   # needs S2 and the build
```

**Ordering and cost:**
- S1, S2 and S6 do not depend on each other or on the build.
- S4 needs S2's npz.
- S5a needs S2 and, to cross-check the engine dump, the build. Don't run it while the build is writing
  the game executable.
- All of them are single-core except S2's 4 workers and the evaluator's `--threads`.

**Exit codes** of S1, S2, S5a and S6: 0 pass, 1 mismatch, 2 input error.
