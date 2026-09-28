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

The identity map gives exactly the rows of `--board-class-rows` without a map, with the same
fingerprint (`board-class-rows-v1|...`), trainer identity and checkpoints. Any other map changes
the rows fingerprint (`board-class-rows-v2|...|texture=fnv1a64:...`), so a checkpoint or a policy
of another texture is refused.

`generate_texture_maps.py` regenerates the five files byte for byte in a few seconds
(`--check` compares them instead); its docstring defines the rules. The rule study, the proxy
scores and the run plan are in the research notes of 28 September 2026 (turn texture merge on
HU50 step 2). Keep LF line ends (`.gitattributes`); the loader also accepts CRLF.
