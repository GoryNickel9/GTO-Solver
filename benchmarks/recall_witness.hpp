#pragma once

#include "gtosd/solver/finite_game.hpp"

namespace gtosd::research {

// Analytic decision problem, not a Short Deck abstraction. Right-action payoff
// -4 makes the old greedy BR failure strict; -3 reproduces the CFR stall.
inline FiniteGame forgotten_type(const bool remember, const double right_a = -4) {
  FiniteGame game;
  game.game_id = remember ? "joint-br.remembered" : "joint-br.forgotten";
  game.nodes.resize(11);
  game.nodes[0].kind = GameNodeKind::Chance;
  game.nodes[0].edges = {{{0, "L"}, 1, 0.5}, {{1, "R"}, 6, 0.5}};
  for (const auto first : {1U, 6U}) {
    auto &before = game.nodes[first];
    before.kind = GameNodeKind::Decision;
    before.information_set = first == 1 ? "L_before" : "R_before";
    before.edges = {{{10, "quit"}, first + 1, 0}, {{20, "enter"}, first + 2, 0}};
    auto &after = game.nodes[first + 2];
    after.kind = GameNodeKind::Decision;
    after.information_set = remember ? (first == 1 ? "L_after" : "R_after") : "merged";
    after.edges = {{{30, "a"}, first + 3, 0}, {{40, "b"}, first + 4, 0}};
    const double a = first == 1 ? 2 : right_a;
    const double b = first == 1 ? -1 : 0;
    game.nodes[first + 3].payoff = {a, -a};
    game.nodes[first + 4].payoff = {b, -b};
  }
  return game;
}

} // namespace gtosd::research
