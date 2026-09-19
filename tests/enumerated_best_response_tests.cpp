#include "../benchmarks/recall_witness.hpp"
#include "gtosd/solver/enumerated_best_response.hpp"
#include "gtosd/solver/reference_games.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace gtosd;
void require(const bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}
void near(const double actual, const double expected, const char *message) {
  require(std::abs(actual - expected) < 1e-11, message);
}

using research::forgotten_type;

// Independent reference: recursively generate profiles, use the existing
// recursive profile evaluator (not the new DP or mixed-radix enumerator).
double brute_force(const FiniteGame &game, StrategyProfile profile, const std::uint8_t player) {
  std::vector<std::string> keys;
  for (const auto &[key, strategy] : profile) {
    if (strategy.player == player) {
      keys.push_back(key);
    }
  }
  std::function<double(std::size_t)> visit = [&](const std::size_t depth) -> double {
    if (depth == keys.size()) {
      return evaluate_strategy_profile(game, profile).value()[player];
    }
    auto &strategy = profile.at(keys[depth]);
    double best = -std::numeric_limits<double>::infinity();
    for (std::size_t action = 0; action < strategy.actions.size(); ++action) {
      std::fill(strategy.probabilities.begin(), strategy.probabilities.end(), 0);
      strategy.probabilities[action] = 1;
      best = std::max(best, visit(depth + 1));
    }
    return best;
  };
  return visit(0);
}

void test_joint_deviation() {
  for (const bool remember : {false, true}) {
    const auto game = forgotten_type(remember);
    const auto profile = uniform_strategy_profile(game).value();
    const auto recall = has_perfect_recall(game, 0);
    require(recall.has_value() && recall.value() == remember, "recall classification");
    const auto result = enumerated_best_response(game, profile, 0);
    require(result.has_value(), "global response completes");
    near(result.value().response.value, 1, "joint optimum is one");
    near(result.value().response.value, brute_force(game, profile, 0), "independent enumeration");
    require(result.value().work.policies == (remember ? 16U : 8U), "all policies visited");
    require(result.value().response.policy.at("L_before") == 20 &&
                result.value().response.policy.at("R_before") == 10,
            "optimal early choices are coupled to continuation");
    const auto legacy = exact_best_response(game, profile, 0);
    if (remember) {
      require(legacy.has_value(), "perfect-recall response computes");
      near(legacy.value().value, 1, "perfect-recall response agrees");
    } else {
      require(!legacy && legacy.error() == SolverError::UnsupportedInformationStructure,
              "greedy response must not certify an imperfect-recall game");
      const auto nash = calculate_nash_conv(game, profile);
      require(!nash && nash.error() == SolverError::UnsupportedInformationStructure,
              "NashConv must propagate unsupported information structure");
    }
    std::cout << "remember=" << remember << " global=" << result.value().response.value
              << " perfect_recall_api=" << (legacy ? "accepted" : "rejected") << '\n';
  }
}

void test_reference_and_opponent() {
  for (const auto &game : {make_matching_pennies_game(), make_kuhn_poker_game()}) {
    auto profile = uniform_strategy_profile(game).value();
    for (auto &[key, strategy] : profile) {
      static_cast<void>(key);
      strategy.probabilities = {0.23, 0.77};
    }
    for (const std::uint8_t player : {std::uint8_t{0}, std::uint8_t{1}}) {
      require(has_perfect_recall(game, player).value(), "reference game has perfect recall");
      const auto result = enumerated_best_response(game, profile, player);
      require(result.has_value(), "reference enumeration succeeds");
      near(result.value().response.value, brute_force(game, profile, player), "recursive oracle");
      near(result.value().response.value, exact_best_response(game, profile, player).value().value,
           "perfect recall BR parity");
      auto lifted = profile;
      for (const auto &[key, action] : result.value().response.policy) {
        auto &strategy = lifted.at(key);
        for (std::size_t i = 0; i < strategy.actions.size(); ++i) {
          strategy.probabilities[i] = strategy.actions[i] == action ? 1 : 0;
        }
      }
      near(evaluate_strategy_profile(game, lifted).value()[player], result.value().response.value,
           "returned policy realizes reported value");
    }
  }
}

void test_absent_minded_and_limits() {
  FiniteGame game;
  game.game_id = "absent-minded";
  game.nodes.resize(5);
  for (const auto id : {0U, 2U}) {
    game.nodes[id].kind = GameNodeKind::Decision;
    game.nodes[id].information_set = "same";
    game.nodes[id].edges = {{{0, "exit"}, id + 1, 0}, {{1, "continue"}, id + 2, 0}};
  }
  game.nodes[3].payoff = {4, -4};
  game.nodes[4].payoff = {1, -1};
  const auto profile = uniform_strategy_profile(game).value();
  near(brute_force(game, profile, 0), 1, "best pure absent-minded value");
  near(evaluate_strategy_profile(game, profile).value()[0], 1.25, "mixed exceeds every pure");
  const auto rejected = enumerated_best_response(game, profile, 0);
  require(!rejected && rejected.error() == SolverError::UnsupportedInformationStructure,
          "absent-minded behavioral optimum cannot be certified by pure enumeration");
  require(!has_perfect_recall(game, 0).value(), "absent-minded is not perfect recall");

  const auto kuhn = make_kuhn_poker_game();
  const auto kuhn_profile = uniform_strategy_profile(kuhn).value();
  const auto count = estimate_best_response_enumeration(kuhn, 0).value();
  require(count.policies == 64 && !count.overflow, "Kuhn count");
  for (const auto limits : {BestResponseEnumerationLimits{63, 100'000},
                            BestResponseEnumerationLimits{64, count.node_evaluations - 1}}) {
    const auto limited = enumerated_best_response(kuhn, kuhn_profile, 0, limits);
    require(!limited && limited.error() == SolverError::ResourceLimitExceeded,
            "limits refuse before enumeration");
  }
  require(enumerated_best_response(kuhn, kuhn_profile, 0, {64, count.node_evaluations}).has_value(),
          "inclusive exact budget succeeds");
  auto invalid = kuhn_profile;
  invalid.begin()->second.probabilities[0] = std::numeric_limits<double>::quiet_NaN();
  require(!enumerated_best_response(kuhn, invalid, 0), "invalid opponent profile rejected");
  require(!enumerated_best_response(kuhn, kuhn_profile, 2), "invalid player rejected");

  FiniteGame large;
  large.game_id = "policy-count-overflow";
  large.nodes.resize(66);
  for (GameNodeId id = 0; id < 64; ++id) {
    auto &node = large.nodes[id];
    node.kind = GameNodeKind::Decision;
    node.information_set = std::to_string(id);
    node.edges = {{{0, "stop"}, 65, 0}, {{1, "continue"}, id + 1, 0}};
  }
  const auto overflow = estimate_best_response_enumeration(large, 0);
  require(overflow.has_value() && overflow.value().overflow, "overflow diagnosed, never wrapped");
  const auto no_work = enumerated_best_response(large, uniform_strategy_profile(large).value(), 0);
  require(!no_work && no_work.error() == SolverError::ResourceLimitExceeded,
          "overflow rejects exhaustive search");
}

void test_dag_and_scaling() {
  auto game = forgotten_type(false);
  // Share a zero terminal between distinct histories. This is not absent-mindedness.
  game.nodes[6].edges[0].child = 2;
  game.nodes.erase(game.nodes.begin() + 7);
  for (auto &node : game.nodes) {
    for (auto &edge : node.edges) {
      if (edge.child > 7) {
        --edge.child;
      }
    }
  }
  // Reverse storage order: evaluation must use topology, not numeric node IDs.
  const auto size = static_cast<GameNodeId>(game.nodes.size());
  std::reverse(game.nodes.begin(), game.nodes.end());
  game.root = size - 1;
  for (auto &node : game.nodes) {
    for (auto &edge : node.edges) {
      edge.child = size - 1 - edge.child;
    }
  }
  const auto profile = uniform_strategy_profile(game).value();
  for (const double scale : {0.001, 1.0, 40.0}) {
    auto scaled = game;
    for (auto &node : scaled.nodes) {
      for (auto &payoff : node.payoff) {
        payoff *= scale;
      }
    }
    const auto result = enumerated_best_response(scaled, profile, 0);
    require(result.has_value(), "shared terminal and reverse node order supported");
    near(result.value().response.value, scale, "positive payoff scaling");
    near(result.value().response.value, brute_force(scaled, profile, 0), "DAG independent oracle");
    const auto passive = enumerated_best_response(scaled, profile, 1);
    require(passive.has_value() && passive.value().work.policies == 1,
            "zero own information sets means one policy");
    near(passive.value().response.value, evaluate_strategy_profile(scaled, profile).value()[1],
         "passive player value");
  }
}
} // namespace

int main() {
  try {
    test_joint_deviation();
    test_reference_and_opponent();
    test_absent_minded_and_limits();
    test_dag_and_scaling();
    std::cout << "ENUMERATED_BEST_RESPONSE_TESTS=PASS\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "ENUMERATED_BEST_RESPONSE_TESTS=FAIL " << error.what() << '\n';
    return 1;
  }
}
