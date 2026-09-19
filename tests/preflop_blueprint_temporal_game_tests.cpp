// Enumerable information-timing witness, not a Short Deck performance model.
#include "../benchmarks/preflop_blueprint_temporal_distance.hpp"
#include "gtosd/solver/best_response.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace gtosd;
void require(const bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}
GameNodeId append(FiniteGame &game, GameNode node) {
  const auto id = static_cast<GameNodeId>(game.nodes.size());
  game.nodes.push_back(std::move(node));
  return id;
}
GameNodeId terminal(FiniteGame &game, const double value) {
  GameNode node;
  node.payoff = {value, -value};
  return append(game, std::move(node));
}
GameEdge edge(const GameNodeId child, const std::uint32_t action, const double probability = 0.0) {
  return {{action, std::to_string(action)}, child, probability};
}

// A player sees the type, then may decline for zero or buy an option for S/4.
// At the next signal, abandoning loses the fee; investing additionally wins or
// loses S equiprobably. Type 0 reveals the outcome before investing; type 1
// reveals nothing. Both types have the same final Bernoulli(1/2) marginal.
// Buy values: +S/4 and -S/4. Original optimum: S/8; shared initial choice: 0.
FiniteGame make_game(const double stake, const bool flat) {
  FiniteGame game;
  game.game_id = flat ? "timing.flat.v1" : "timing.potential.v1";
  GameNode root;
  root.kind = GameNodeKind::Chance;
  for (int type = 0; type < 2; ++type) {
    GameNode signal;
    signal.kind = GameNodeKind::Chance;
    for (int outcome = 0; outcome < 2; ++outcome) {
      GameNodeId investment = 0;
      if (type == 1) {
        GameNode hidden;
        hidden.kind = GameNodeKind::Chance;
        hidden.edges = {edge(terminal(game, -1.25 * stake), 0, 0.5),
                        edge(terminal(game, 0.75 * stake), 1, 0.5)};
        investment = append(game, std::move(hidden));
      } else {
        investment = terminal(game, (outcome == 0 ? -1.25 : 0.75) * stake);
      }
      GameNode turn;
      turn.kind = GameNodeKind::Decision;
      turn.information_set =
          "turn/" + std::to_string(type) + "/" + std::to_string(type == 0 ? outcome : 0);
      turn.edges = {edge(terminal(game, -0.25 * stake), 0), edge(investment, 1)};
      signal.edges.push_back(
          edge(append(game, std::move(turn)), static_cast<std::uint32_t>(outcome), 0.5));
    }
    GameNode initial;
    initial.kind = GameNodeKind::Decision;
    initial.information_set = flat ? "initial/shared" : "initial/" + std::to_string(type);
    initial.edges = {edge(terminal(game, 0), 0), edge(append(game, std::move(signal)), 1)};
    root.edges.push_back(
        edge(append(game, std::move(initial)), static_cast<std::uint32_t>(type), 0.5));
  }
  game.root = append(game, std::move(root));
  return game;
}
StrategyProfile lift(const StrategyProfile &source) {
  auto result = source;
  const auto initial = result.at("initial/shared");
  result.erase("initial/shared");
  result.emplace("initial/0", initial);
  result.emplace("initial/1", initial);
  return result;
}
} // namespace

int main() {
  try {
    const std::array<research::EquityHistogram, 2> revealed{{{2, 0}, {0, 2}}};
    const std::array<research::EquityHistogram, 2> unresolved{{{1, 1}, {1, 1}}};
    const auto distance = research::temporal_distance(revealed, unresolved);
    require(distance.marginal == 0 && distance.potential == 0.5,
            "flat features merge the types, temporal features distinguish them");
    for (const double stake : {1.0, 10.0, 40.0}) {
      const auto original = make_game(stake, false);
      const auto flat = make_game(stake, true);
      require(validate_finite_game(original).has_value() && validate_finite_game(flat).has_value(),
              "both enumerable games validate");
      const auto uniform = uniform_strategy_profile(original);
      const auto flat_uniform = uniform_strategy_profile(flat);
      require(uniform.has_value() && flat_uniform.has_value(), "complete profiles exist");
      const auto optimum = exact_best_response(original, uniform.value(), 0);
      const auto flat_optimum = exact_best_response(flat, flat_uniform.value(), 0);
      require(optimum.has_value() && flat_optimum.has_value(), "exact best responses compute");
      require(std::abs(optimum.value().value - stake / 8) < 1e-12 &&
                  std::abs(flat_optimum.value().value) < 1e-12,
              "enumeration agrees with independent analytic values S/8 and zero");
      SolverConfig config;
      config.algorithm = SolverAlgorithm::LinearCfr;
      config.iterations = 8000;
      config.thread_count = 1;
      const auto solved_flat = solve_finite_game(flat, config);
      const auto solved_temporal = solve_finite_game(original, config);
      require(solved_flat.has_value() && solved_temporal.has_value(), "both CFR runs complete");
      const auto internal = calculate_nash_conv(flat, solved_flat.value().average_strategy);
      const auto physical =
          calculate_nash_conv(original, lift(solved_flat.value().average_strategy));
      const auto temporal = calculate_nash_conv(original, solved_temporal.value().average_strategy);
      require(internal.has_value() && physical.has_value() && temporal.has_value(),
              "all three metrics enumerate exactly");
      require(internal.value().nash_conv < stake * 1e-6, "flat game converges internally");
      require(std::abs(physical.value().nash_conv - stake / 8) < stake * 1e-6,
              "physical loss survives abstract convergence and scales with exposure");
      require(temporal.value().nash_conv < stake * 1e-6,
              "distinguishing information timing removes the loss on this toy");
      std::cout << "stake=" << stake << " flat_internal=" << internal.value().nash_conv
                << " flat_physical=" << physical.value().nash_conv
                << " temporal_physical=" << temporal.value().nash_conv << '\n';
    }
    std::cout << "TEMPORAL_GAME_TESTS=PASS scope=analytic_mechanism_only\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "TEMPORAL_GAME_TESTS=FAIL " << error.what() << '\n';
    return 1;
  }
}
