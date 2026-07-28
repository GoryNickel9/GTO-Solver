#pragma once

#include "gtosd/solver/finite_game.hpp"

namespace gtosd {

[[nodiscard]] FiniteGame make_matching_pennies_game();
[[nodiscard]] FiniteGame make_kuhn_poker_game();
[[nodiscard]] FiniteGame make_leduc_poker_game();
[[nodiscard]] Result<FiniteGame, SolverError>
make_short_deck_river_toy_game(double rake_fraction = 0.0);

[[nodiscard]] Result<StrategyProfile, SolverError>
reference_equilibrium_strategy(const FiniteGame &game);

} // namespace gtosd
