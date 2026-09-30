# Turn texture maps of the board class rows

Maps in the format `gtosd-board-texture-v1` (`include/gtosd/preflop_blueprint/board_texture.hpp`),
read by `gtosd_preflop_blueprint_train --board-class-rows --board-texture-map FILE` and by
`gtosd_preflop_blueprint_monker_values --board-class-rows --board-texture-map FILE`. A map merges
canonical flop+turn boards into turn classes that share their rows (`class * groups + bucket`);
the bucket tables do not change. The flop section is the identity and the river follows its
turn (`river-key turn`).

| File | Name | Turn classes | HU50 step 2 capacities (120/120/30 groups) | sha256 |
|---|---|---|---|---|
| `identity_texture_map.txt` | identity | 13,761 | 68,760 / 1,651,320 / 412,830 | `6b8a0004abd4bc29...` |
| `texture_map_TX0_suit_only.txt` | TX0_suit_only | 8,217 | 68,760 / 986,040 / 246,510 | `2330a9d8daa6e39f...` |
| `texture_map_TX1_fallback.txt` | TX1_fallback | 6,768 | 68,760 / 812,160 / 203,040 | `e325ee934f9956e1...` |
| `texture_map_TX2_recommended.txt` | TX2_recommended | 4,482 | 68,760 / 537,840 / 134,460 | `68efc814c00dbe62...` |
| `texture_map_TX3_aggressive.txt` | TX3_aggressive | 2,680 | 68,760 / 321,600 / 80,400 | `67f7cf8b4881bc91...` |
| `texture_map_TXM_monker_like.txt` | TXM_monker_like | 1,899 | 68,760 / 227,880 / 56,970 | `977081d612ad5d2c...` |
| `texture_map_TXM2_monker_like.txt` | TXM2_monker_like | 2,151 | 68,760 / 258,120 / 64,530 | `87e12b5ed291a626...` |

`TXM_monker_like` (30 September 2026) imitates what is known of MonkerSolver's turn textures:
classes global per street, blind to which card came on the turn and to the flop. Its key is the
unordered 4-card board: the sorted multiset of the 4 ranks and the suit pattern (sorted suit
counts, 4 / 3+1 / 2+2 / 2+1+1 / 1+1+1+1), without which ranks share a suit. That gives 1,899
classes against 3,663 unordered suit-canonical turn boards and a scaled estimate of about 1,990
for Monker's "Large" (keeping the ranks of a 2+2 pair of suits would give 2,151, the ranks of a
3+ card suit 2,277). The exact Monker rule is unknown. Monker's river classes depend on the
5-card board (about 540-760 short-deck classes estimated); the engine supports only
`river-key turn`, so here the river takes the class of its turn (1,899 classes), and a 5-card
river key would need code changes. On HU50 step 2 donk with 30 x 4 buckets the trainer state is
966,809,248 bytes against 2,168,834,128 with TX2 (0.45x).

`TXM2_monker_like` adds to the TXM key, on 2+2 boards only, which ranks share each suit
(2,151 classes). The same rule on Hold'em unordered turns gives 8,996 classes, 0.6 % from
Monker's 8,942 "Large" in the user's settings screenshot, while the plain TXM rule gives 7,566:
by that one data point TXM2 is the closer imitation. Trainer state on HU50 step 2 donk with
30 x 4 buckets: 1,084,079,968 bytes.

The identity map gives exactly the rows of `--board-class-rows` without a map, with the same
fingerprint (`board-class-rows-v1|...`), trainer identity and checkpoints. Any other map changes
the rows fingerprint (`board-class-rows-v2|...|texture=fnv1a64:...`), so a checkpoint or a policy
of another texture is refused.

`generate_texture_maps.py` regenerates the seven files byte for byte in a few seconds
(`--check` compares them instead); its docstring defines the rules. The rule study, the proxy
scores and the run plan are in the research notes of 28 September 2026 (turn texture merge on
HU50 step 2). Keep LF line ends (`.gitattributes`); the loader also accepts CRLF.
