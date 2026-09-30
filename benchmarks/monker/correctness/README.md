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
