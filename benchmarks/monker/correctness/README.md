# Correctness games of the HU50 step-2 path (30 September 2026)

Small HU short-deck games on which the HU50 step-2 code path (`gtosd_preflop_blueprint_train
--board-class-rows --board-texture-map`, DCFR 1.5 / 0 / 2 alternating, 32 boards per player per
iteration, lazy discount) must find the equilibrium: the exact physical NashConv of
`gtosd_preflop_blueprint_monker_values --all-flops` must fall towards 0. With a lossless
abstraction (every information set of the abstract game is one of the real game) the floor is
exactly 0, so a plateau is a bug; a gross abstraction on the same game must plateau (control).

All games: HU (CO then BTN), 6a effective stacks including the 1a ante, BTN blind 1a, initial pot
3a; the pot-size open equals the stack (the all-in), postflop one size of 100 % of the pot, which
is the all-in. Donk bets allowed (with all-in-only bets the rule removes nothing).

| File | Postflop betting | Nodes | Decisions (pre / flop / turn / river) | Tree |
|---|---|---:|---|---|
| `HU6_all.json` | flop, turn, river | 37 | 4 / 4 / 4 / 4 | `fnv1a64:fb76ddcd880fec5f` |
| `HU6_all_rake25cap2.json` | same, rake 2.5 %, cap 2a, no flop no drop | 37 | 4 / 4 / 4 / 4 | `fnv1a64:f226b87d43f28215` |
| `HU6_V0_flop.json` | flop | 25 | 4 / 4 / 2 / 2 check-only | `fnv1a64:cd66796bdbdac5c1` |
| `HU6_V1_flopturn.json` | flop, turn | 31 | 4 / 4 / 4 / 2 check-only | `fnv1a64:da5c6942354ad5ad` |
| `HU6_V2_river.json` | river | 25 | 4 / 2 check-only / 2 check-only / 4 | `fnv1a64:2f109f6f1891d9f2` |

`postflop_betting_streets` (game config key) makes the other streets check-only.

Lossless abstractions (bucket tables under `out/monker/correctness/buckets`, built with
`gtosd_preflop_blueprint_monker_buckets`):

| Run | Game | Tables | Texture map | Why lossless |
|---|---|---|---|---|
| V0 | `HU6_V0_flop` | `out/monker/buckets_flopexact_15x4` (`--flop-exact`) | `identity_texture_map.txt` | flop rows = canonical flop x suit orbit of the hand |
| V1 | `HU6_V1_flopturn` | `v1_flopturn_exact` (`--flop-exact --turn-exact`) | `identity_texture_map.txt` | turn rows = canonical flop+turn x suit orbit (the forgotten flop row is implied) |
| V2 | `HU6_V2_river` | `v2_river_exact` (`--river-exact`) | `identity_river_board_texture_map.txt` (`river-key river-board`) | no decision before the river: river rows = canonical five-card board x suit orbit |

Gross twins: V0-G `g3x1` (`--levels 3 --tiers 1`), V1-G `v1g_flopexact_turn3x1`, V2-G
`v2g_river3` (`--river-levels 3`, river-board map); Stage P runs the literal HU50-style
abstraction on `HU6_all` (`out/monker/buckets_15x4`, identity or TX2 map; the rake variant; the
`g3x1` control). A river keyed by the turn class (HU50) cannot be made lossless with betting on
several streets (it would need about 14,880 ids per turn class, above the 4,096 limit of the
bucket format, and 26 GB of state), hence V2.

Driver: `run_correctness.sh <run dir> <config> <bucket dir> <texture map> <threads> <target>...`
(trains in resumed segments to each target, evaluates each policy snapshot through
`tools/monker_compare/convergence_curve.py`; curve in `<run dir>/convergence.txt`). Run the frozen
copy `out/frozen/run_correctness.sh` and frozen executables (`BIN`, default
`out/monker/bin_correct/c123`; `out/monker/bin_correct/base` is the build before these changes).

Memory measured on the smoke runs (trainer accounted bytes / evaluator peak commit): V0 0.18 /
0.16 GB, V1 1.08 / 0.58 GB, V2 1.40 / 0.73 GB, HU6_all with 15x4 tables 0.25 / 0.19 GB.

Locked-preflop variants (review of 30 September, D1). In these 6a games almost every hand folds
or shoves preflop: at 32k iterations of `HU6_all` the flop is reached in about 0.3 % of the
deals, so a postflop or river error moves the full NashConv very little. `lock_limp_check/`
locks CO to limp and BTN to check behind for all 81 classes (`LOCK_CHARTS=benchmarks/monker/correctness/lock_limp_check
LOCK_NODES=CO/CO_strategy.txt,BTN/CO_Call_BTN_strategy.txt` in the driver, i.e. the trainer's
`--lock-charts/--lock-nodes`, the preflop lock of the HU50 lock runs): every deal reaches the
flop with a 4a pot and full ranges. Gate those runs on the sum of the `gain_lower` columns (the
responder follows the locked preflop and best-responds from the flop on, i.e. the postflop
NashConv of the locked game); the full NashConv stays at the value of shoving against the lock.

The driver checks every segment's start event against the tree fingerprints of the table above
(`EXPECT_TREE` overrides): the pre-edit executables (`bin_correct/base`) ignore
`postflop_betting_streets` and would silently train the tree that bets on every street.
Long runs: frozen copy `out/frozen/run_correctness_lock.sh`.

## Games with bets below the all-in and non-all-in raises (1 October 2026)

HU50's postflop shapes in a lossless game. Same HU setup (1a ante, BTN blind 1a, 3a initial pot),
but the preflop open is 1000 % of the pot, which merges into the all-in: preflop is the same
4-decision tree as HU6, so `lock_limp_check/` applies unchanged and every deal reaches the flop
with a 4a pot. Postflop one size of 100 % of the pot plus the all-in (HU50's size), donk bets on.
With 19a stacks the limped-pot shapes are HU50's: `check;bet_4;all_in`, `fold;call;raise_16;all_in`,
`check;bet_12;all_in` after a bet-call. The lock keeps only the limp-check entry.

| File | Stack | Postflop betting | Nodes | Decisions (pre / flop / turn / river) | Tree |
|---|---:|---|---:|---|---|
| `HU19_B0_flop.json` | 19a | flop (`bet_4`, `raise_16`, all-in) | 73 | 4 / 12 / 10 check-only / 10 check-only | `fnv1a64:b58f4ac0e4cf68b1` |
| `HU19_B2_river.json` | 19a | river (same shapes) | 49 | 4 / 2 check-only / 2 check-only / 12 | `fnv1a64:23b83f3f3b18d1c0` |
| `HU8_B1_flopturn.json` | 8a | flop, turn (turn after a flop bet-call, donk all-in) | 85 | 4 / 8 / 16 / 10 check-only | `fnv1a64:5269db409b4bcf45` |

| Run | Game | Tables | Texture map | Why lossless | Control (one id on the deciding street) |
|---|---|---|---|---|---|
| B0L | `HU19_B0_flop` | `v1g_flopexact_turn1x1` | `identity_texture_map.txt` | exact flop rows; turn and river nodes are check-only (one action) | B0LG1: `g1x1` (`--levels 1 --tiers 1`) |
| B2L | `HU19_B2_river` | `v2_river_exact` | `identity_river_board_texture_map.txt` | no decision before the river; river rows keyed by the five-card board | B2LG1: `v2g_river1` |
| B1L | `HU8_B1_flopturn` | `v1_flopturn_exact` | `identity_texture_map.txt` | exact flop and turn rows; river check-only | B1LG1: `v1g_flopexact_turn1x1` |

All six run locked (`LOCK_CHARTS`/`LOCK_NODES` as above) and are gated on the sum of the
`gain_lower` columns. Start-event smoke values: capacities B0 `[302544, 13761, 206415]`, B2
`[34380, 825660, 9299070]` (texture `river-key=river-board`), B1 `[302544, 6825456, 206415]`;
`preflop_lock.rows` 162. Trainer state (double, 16 B per cell + 2 B per row), trainer peak about
state + 0.11 GB: B0 0.20 GB, B2 5.02 GB, B1 4.29 GB; evaluator about policy (8 B per cell) +
0.12 GB: B0 0.22 GB, B2 2.51 GB, B1 2.15 GB. The controls stay below 0.3 GB.

## Raked twins of V2 (1 October 2026)

`HU6_V2_river` with rake, all other keys unchanged, so the trees are node-identical to V2 and only
the payoffs differ (rake at compile time, paid by the winner, no flop no drop). Same tables, map
and memory as V2; run unlocked (the rake bites mainly on the called preflop all-in).

| File | Rake | Tree | Use |
|---|---|---|---|
| `HU6_V2_river_rake25cap2.json` | 2.5 %, cap 2a | `fnv1a64:cd2e1217488aaea9` | V2R: general-sum convergence on a lossless game |
| `HU6_V2_river_rakeinert.json` | 2.5 %, cap 2a, minimum pot 13a (above every pot: no hand is raked) | `fnv1a64:eb528dbdd5dbe94c` | V2Z: must reproduce V2's policies bit for bit |
| `HU6_V2_river_rake5cap05.json` | 5 %, cap 0.5a (binds on the 12a pots) | `fnv1a64:0133288de510b8f0` | V2R5: the cap in a converged run |

`convergence_curve.py` does not pass `--expected-rake`; run `gtosd_preflop_blueprint_monker_values
... --all-flops --expected-rake --out <file>` on a kept snapshot (`KEEP_POLICIES=1`) for the
per-hero expected rake.

## Second seed

`SEED=<decimal>` passes `--seed` to the trainer; the start line of `run.log` logs it (`seed default`
otherwise, the trainer's built-in seed used by every run before 1 October 2026). The seed is part
of the trainer identity, so a second seed needs a fresh run directory. Compare a replicate with
seed 1 on the same target list: segment ends materialize the lazy discounts, so two target lists
differ at rounding level. Long runs with these options: frozen copy
`out/frozen/run_correctness_v2.sh`.
