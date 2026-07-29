# Third-party notices

The production library currently contains no copied third-party source code.

## Phase 2 evaluator provenance

The source identified by the roadmap at
`F:\Poker Tracke v4\equity_calculator_cpp` was audited on 2026-07-28. No
`LICENSE`, `COPYING`, or `NOTICE` file was present, so none of that source was
copied into this repository or linked into the production library.

The audited evaluator files were identified by these SHA-256 digests:

| File | SHA-256 |
|---|---|
| `include/hand_evaluator.hpp` | `37154480639A98674BFBB2963E706FFC0370A87FE07297F25AD8C73D38A94F7C` |
| `src/hand_evaluator.cpp` | `D295A28B8A1598265F3C63F8B1693D4101C9BA419BF1749E4BC306D03D2470B1` |

Phase 2 uses an original first-party exact evaluator in `libs/equity`. The
external implementation is neither a production dependency nor a distributed
test oracle. This removes its undocumented-ownership risk from the product
binary; any future reuse still requires explicit provenance and redistribution
authorization before copying.

The vcpkg baseline
`cd61e1e26a038e82d6550a3ebbe0fbbfe7da78e3` resolves the following exact
development dependencies:

| Package | Version | License |
|---|---:|---|
| Google Benchmark | 1.9.5 | Apache-2.0 |
| GoogleTest | 1.17.0 port revision 2 | BSD-3-Clause |
| nlohmann/json | 3.12.0 port revision 2 | MIT |
| spdlog | 1.17.0 | MIT |
| fmt, transitive | 12.2.0 | MIT |
| libsodium | 1.0.22 | ISC |
| SQLite | 3.53.2 port revision 1 | blessing / public-domain dedication |
| Zstandard | 1.5.7 | BSD-3-Clause oppure GPL-2.0-only |

GoogleTest and Google Benchmark are linked only into development targets.
nlohmann/json is linked by the CLI. libsodium, SQLite and Zstandard are linked
by `gtosd::storage`; their authoritative license texts are installed beside
the product under `share/gtosd/licenses`.

Release packaging must copy the authoritative license texts exported by vcpkg
for every dependency actually distributed.
