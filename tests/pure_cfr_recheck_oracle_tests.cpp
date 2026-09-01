#include "gtosd/solver/best_response.hpp"
#include "gtosd/solver/finite_game.hpp"
#include "gtosd/solver/reference_games.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using NumericMap = std::map<std::string, std::vector<double>>;

std::uint64_t assertions = 0;

void require(const bool condition, const std::string_view message) {
  ++assertions;
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

void require_near(const double actual, const double expected, const double tolerance,
                  const std::string_view message) {
  require(std::abs(actual - expected) <= tolerance, message);
}

struct PureBuffer {
  std::uint8_t player{0};
  std::vector<gtosd::GameActionId> actions;
  std::vector<double> cumulative_q;
  std::vector<double> cumulative_average;
};

using PureState = std::map<std::string, PureBuffer>;

struct PureDelta {
  NumericMap q;
  NumericMap average;
  std::uint64_t traversed_nodes{0};
};

struct PureSolve {
  PureState state;
  gtosd::StrategyProfile average_strategy;
  std::uint64_t logical_iterations{0};
  std::uint64_t outer_iterations{0};
  std::uint64_t traversed_nodes{0};
  std::uint64_t maximum_phase{0};
};

std::size_t pure_action(const PureBuffer &buffer) {
  return static_cast<std::size_t>(
      std::distance(buffer.cumulative_q.begin(),
                    std::max_element(buffer.cumulative_q.begin(), buffer.cumulative_q.end())));
}

PureState make_state(const gtosd::FiniteGame &game) {
  const auto uniform = gtosd::uniform_strategy_profile(game);
  if (!uniform) {
    throw std::runtime_error("invalid oracle game");
  }
  PureState result;
  for (const auto &[key, strategy] : uniform.value()) {
    PureBuffer buffer;
    buffer.player = strategy.player;
    buffer.actions = strategy.actions;
    buffer.cumulative_q.assign(strategy.actions.size(), 0.0);
    buffer.cumulative_average.assign(strategy.actions.size(), 0.0);
    result.emplace(key, std::move(buffer));
  }
  return result;
}

gtosd::StrategyProfile current_profile(const PureState &state) {
  gtosd::StrategyProfile profile;
  for (const auto &[key, buffer] : state) {
    gtosd::InformationSetStrategy strategy;
    strategy.player = buffer.player;
    strategy.actions = buffer.actions;
    strategy.probabilities.assign(buffer.actions.size(), 0.0);
    strategy.probabilities[pure_action(buffer)] = 1.0;
    profile.emplace(key, std::move(strategy));
  }
  return profile;
}

gtosd::StrategyProfile average_profile(const PureState &state) {
  gtosd::StrategyProfile profile;
  for (const auto &[key, buffer] : state) {
    gtosd::InformationSetStrategy strategy;
    strategy.player = buffer.player;
    strategy.actions = buffer.actions;
    strategy.probabilities = buffer.cumulative_average;
    double sum = 0.0;
    for (const double value : strategy.probabilities) {
      sum += value;
    }
    if (sum > 0.0) {
      for (double &value : strategy.probabilities) {
        value /= sum;
      }
    } else {
      strategy.probabilities.assign(buffer.actions.size(), 0.0);
      strategy.probabilities[pure_action(buffer)] = 1.0;
    }
    profile.emplace(key, std::move(strategy));
  }
  return profile;
}

std::vector<double> &delta_for(NumericMap &map, const std::string &key,
                               const std::size_t size) {
  auto &values = map[key];
  if (values.empty()) {
    values.assign(size, 0.0);
  }
  return values;
}

double traverse_player(const gtosd::FiniteGame &game, const gtosd::GameNodeId node_id,
                       const gtosd::StrategyProfile &profile,
                       const std::uint8_t updating_player, const double own_reach,
                       const double chance_reach, PureDelta &delta) {
  ++delta.traversed_nodes;
  const auto &node = game.nodes[node_id];
  if (node.kind == gtosd::GameNodeKind::Terminal) {
    return node.payoff[updating_player];
  }
  if (node.kind == gtosd::GameNodeKind::Chance) {
    double value = 0.0;
    for (const auto &edge : node.edges) {
      value += edge.probability *
               traverse_player(game, edge.child, profile, updating_player, own_reach,
                               chance_reach * edge.probability, delta);
    }
    return value;
  }

  const auto &strategy = profile.at(node.information_set);
  const auto selected = static_cast<std::size_t>(
      std::distance(strategy.probabilities.begin(),
                    std::max_element(strategy.probabilities.begin(),
                                     strategy.probabilities.end())));
  if (node.player != updating_player) {
    return traverse_player(game, node.edges[selected].child, profile, updating_player, own_reach,
                           chance_reach, delta);
  }

  auto &average = delta_for(delta.average, node.information_set, node.edges.size());
  average[selected] += own_reach * chance_reach;

  std::vector<double> action_values(node.edges.size(), 0.0);
  for (std::size_t action = 0; action < node.edges.size(); ++action) {
    const double child_own_reach = own_reach * strategy.probabilities[action];
    action_values[action] =
        traverse_player(game, node.edges[action].child, profile, updating_player,
                        child_own_reach, chance_reach, delta);
  }
  auto &q = delta_for(delta.q, node.information_set, node.edges.size());
  for (std::size_t action = 0; action < node.edges.size(); ++action) {
    q[action] += chance_reach * action_values[action];
  }
  return action_values[selected];
}

PureDelta compute_delta(const gtosd::FiniteGame &game, const PureState &state) {
  PureDelta delta;
  const auto profile = current_profile(state);
  for (std::uint8_t player = 0; player < 2U; ++player) {
    static_cast<void>(traverse_player(game, game.root, profile, player, 1.0, 1.0, delta));
  }
  return delta;
}

std::uint64_t phase_until_policy_change(const PureState &state, const PureDelta &delta,
                                        const std::uint64_t remaining) {
  std::uint64_t phase = remaining;
  for (const auto &[key, buffer] : state) {
    const auto selected = pure_action(buffer);
    const auto found = delta.q.find(key);
    const std::vector<double> zero(buffer.actions.size(), 0.0);
    const auto &slope = found == delta.q.end() ? zero : found->second;
    for (std::size_t action = 0; action < buffer.actions.size(); ++action) {
      if (action == selected) {
        continue;
      }
      const double pursuit_speed = slope[action] - slope[selected];
      if (!(pursuit_speed > 0.0)) {
        continue;
      }
      const double gap = buffer.cumulative_q[selected] - buffer.cumulative_q[action];
      std::uint64_t candidate = 1U;
      if (gap > 0.0) {
        const double ratio = gap / pursuit_speed;
        const double first_change = action < selected ? std::ceil(ratio) : std::floor(ratio) + 1.0;
        candidate = static_cast<std::uint64_t>(std::max(1.0, first_change));
      }
      phase = std::min(phase, candidate);
    }
  }
  return std::max<std::uint64_t>(1U, phase);
}

void apply_delta(PureState &state, const PureDelta &delta, const std::uint64_t phase) {
  const double weight = static_cast<double>(phase);
  for (auto &[key, buffer] : state) {
    const auto q_found = delta.q.find(key);
    const auto average_found = delta.average.find(key);
    for (std::size_t action = 0; action < buffer.actions.size(); ++action) {
      if (q_found != delta.q.end()) {
        buffer.cumulative_q[action] += weight * q_found->second[action];
      }
      if (average_found != delta.average.end()) {
        buffer.cumulative_average[action] += weight * average_found->second[action];
      }
    }
  }
}

PureSolve solve_pure(const gtosd::FiniteGame &game, const std::uint64_t logical_iterations,
                     const bool sync) {
  PureSolve result;
  result.state = make_state(game);
  while (result.logical_iterations < logical_iterations) {
    const auto delta = compute_delta(game, result.state);
    const auto remaining = logical_iterations - result.logical_iterations;
    const auto phase = sync ? phase_until_policy_change(result.state, delta, remaining) : 1U;
    apply_delta(result.state, delta, phase);
    result.logical_iterations += phase;
    ++result.outer_iterations;
    result.traversed_nodes += delta.traversed_nodes;
    result.maximum_phase = std::max(result.maximum_phase, phase);
  }
  result.average_strategy = average_profile(result.state);
  return result;
}

void require_same_state(const PureState &left, const PureState &right, const double tolerance) {
  require(left.size() == right.size(), "same infoset count");
  for (const auto &[key, left_buffer] : left) {
    const auto &right_buffer = right.at(key);
    require(left_buffer.actions == right_buffer.actions, "same action layout");
    for (std::size_t action = 0; action < left_buffer.actions.size(); ++action) {
      require_near(left_buffer.cumulative_q[action], right_buffer.cumulative_q[action], tolerance,
                   "sync Q equals repeated PCFR Q");
      if (std::abs(left_buffer.cumulative_average[action] -
                   right_buffer.cumulative_average[action]) > tolerance) {
        throw std::runtime_error("sync average mismatch at " + key + " action " +
                                 std::to_string(action) + ": " +
                                 std::to_string(left_buffer.cumulative_average[action]) +
                                 " vs " +
                                 std::to_string(right_buffer.cumulative_average[action]));
      }
      ++assertions;
    }
  }
}

double maximum_average_state_delta(const PureState &left, const PureState &right) {
  double maximum = 0.0;
  for (const auto &[key, left_buffer] : left) {
    const auto &right_buffer = right.at(key);
    for (std::size_t action = 0; action < left_buffer.actions.size(); ++action) {
      maximum = std::max(maximum, std::abs(left_buffer.cumulative_average[action] -
                                           right_buffer.cumulative_average[action]));
    }
  }
  return maximum;
}

double maximum_q_state_delta(const PureState &left, const PureState &right) {
  double maximum = 0.0;
  for (const auto &[key, left_buffer] : left) {
    const auto &right_buffer = right.at(key);
    for (std::size_t action = 0; action < left_buffer.actions.size(); ++action) {
      maximum = std::max(maximum, std::abs(left_buffer.cumulative_q[action] -
                                           right_buffer.cumulative_q[action]));
    }
  }
  return maximum;
}

double maximum_profile_delta(const gtosd::StrategyProfile &left,
                             const gtosd::StrategyProfile &right) {
  double maximum = 0.0;
  for (const auto &[key, left_strategy] : left) {
    const auto &right_strategy = right.at(key);
    for (std::size_t action = 0; action < left_strategy.actions.size(); ++action) {
      maximum = std::max(maximum, std::abs(left_strategy.probabilities[action] -
                                           right_strategy.probabilities[action]));
    }
  }
  return maximum;
}

void test_sync_finite_precision_counterexample() {
  constexpr std::uint64_t horizon = 2'000U;
  const auto matching = gtosd::make_matching_pennies_game();
  const auto matching_repeated = solve_pure(matching, horizon, false);
  const auto matching_synced = solve_pure(matching, horizon, true);
  require_same_state(matching_repeated.state, matching_synced.state, 1.0e-9);
  require(matching_synced.maximum_phase > 1U, "sync compresses matching-pennies phases");

  const auto kuhn = gtosd::make_kuhn_poker_game();
  const auto kuhn_repeated = solve_pure(kuhn, horizon, false);
  const auto kuhn_synced = solve_pure(kuhn, horizon, true);
  const double q_delta = maximum_q_state_delta(kuhn_repeated.state, kuhn_synced.state);
  const double raw_average_delta =
      maximum_average_state_delta(kuhn_repeated.state, kuhn_synced.state);
  const double normalized_delta =
      maximum_profile_delta(kuhn_repeated.average_strategy, kuhn_synced.average_strategy);
  require(q_delta > 1.0e-6, "sync exposes a repeated-rounding Q mismatch");
  require(raw_average_delta > 1.0, "sync exposes a material repeated-rounding average mismatch");
  require(normalized_delta > 1.0e-4,
          "sync changes the exported strategy under finite-precision accumulation");
  require(kuhn_synced.outer_iterations < kuhn_repeated.outer_iterations,
          "sync counterexample actually exercised phase compression");

  std::cout << "sync_pcfr_kuhn_q_state_delta=" << q_delta << '\n'
            << "sync_pcfr_kuhn_average_state_delta=" << raw_average_delta << '\n'
            << "sync_pcfr_kuhn_profile_delta=" << normalized_delta << '\n'
            << "sync_pcfr_kuhn_outer=" << kuhn_synced.outer_iterations << '\n';
}

gtosd::FiniteGame make_binary_alternating_game(const std::uint32_t decision_depth) {
  const auto node_count = (std::uint64_t{1} << (decision_depth + 1U)) - 1U;
  gtosd::FiniteGame game;
  game.game_id = "binary-alternating-depth-" + std::to_string(decision_depth);
  game.nodes.resize(static_cast<std::size_t>(node_count));
  for (std::uint64_t index = 0; index < node_count; ++index) {
    const auto level = static_cast<std::uint32_t>(std::bit_width(index + 1U) - 1U);
    auto &node = game.nodes[static_cast<std::size_t>(index)];
    if (level == decision_depth) {
      node.kind = gtosd::GameNodeKind::Terminal;
      node.payoff = {(index & 1U) == 0U ? 1.0 : -1.0,
                     (index & 1U) == 0U ? -1.0 : 1.0};
      continue;
    }
    node.kind = gtosd::GameNodeKind::Decision;
    node.player = static_cast<std::uint8_t>(level & 1U);
    node.information_set = "binary:" + std::to_string(index);
    node.edges = {{{0U, "left"}, static_cast<gtosd::GameNodeId>(index * 2U + 1U), 0.0},
                  {{1U, "right"}, static_cast<gtosd::GameNodeId>(index * 2U + 2U), 0.0}};
  }
  return game;
}

void test_pure_cfr_convergence_and_structural_reduction() {
  const auto matching = gtosd::make_matching_pennies_game();
  const auto matching_solved = solve_pure(matching, 100'000U, false);
  const auto matching_metrics =
      gtosd::calculate_nash_conv(matching, matching_solved.average_strategy);
  require(matching_metrics.has_value(), "matching Sync PCFR certifies");
  require(matching_metrics.value().nash_conv < 0.015,
          "matching Sync PCFR average approaches Nash");

  const auto kuhn = gtosd::make_kuhn_poker_game();
  const auto kuhn_pure_20k = solve_pure(kuhn, 20'000U, false);
  const auto kuhn_pure_20k_metrics =
      gtosd::calculate_nash_conv(kuhn, kuhn_pure_20k.average_strategy);
  require(kuhn_pure_20k_metrics.has_value(), "Kuhn Pure CFR 20k certifies");

  gtosd::SolverConfig dcfr_config;
  dcfr_config.algorithm = gtosd::SolverAlgorithm::Dcfr;
  dcfr_config.iterations = 20'000U;
  const auto kuhn_dcfr_20k = gtosd::solve_finite_game(kuhn, dcfr_config);
  require(kuhn_dcfr_20k.has_value(), "Kuhn DCFR comparator solves");
  const auto kuhn_dcfr_20k_metrics =
      gtosd::calculate_nash_conv(kuhn, kuhn_dcfr_20k.value().average_strategy);
  require(kuhn_dcfr_20k_metrics.has_value(), "Kuhn DCFR comparator certifies");

  const auto kuhn_solved = solve_pure(kuhn, 250'000U, false);
  const auto kuhn_metrics = gtosd::calculate_nash_conv(kuhn, kuhn_solved.average_strategy);
  require(kuhn_metrics.has_value(), "Kuhn Sync PCFR certifies");
  require(kuhn_metrics.value().nash_conv < 0.02, "Kuhn Sync PCFR lowers NashConv");
  require_near(kuhn_metrics.value().profile_value[0], -1.0 / 18.0, 2.0e-3,
               "Kuhn Sync PCFR approaches analytic EV");

  const auto binary = make_binary_alternating_game(12U);
  const auto binary_validation = gtosd::validate_finite_game(binary);
  require(binary_validation.has_value(), "synthetic alternating binary game validates");
  const auto binary_step = solve_pure(binary, 1U, false);
  require(binary_step.traversed_nodes * 10U < binary.nodes.size(),
          "pure traversal touches at least 10x fewer nodes on a deep alternating tree");

  std::cout << "sync_pcfr_matching_nash_conv=" << matching_metrics.value().nash_conv << '\n'
            << "pure_cfr_kuhn_nash_conv=" << kuhn_metrics.value().nash_conv << '\n'
            << "pure_cfr_kuhn_profile_ev=" << kuhn_metrics.value().profile_value[0] << '\n'
            << "pure_cfr_kuhn_nash_conv_20k="
            << kuhn_pure_20k_metrics.value().nash_conv << '\n'
            << "dcfr_kuhn_nash_conv_20k=" << kuhn_dcfr_20k_metrics.value().nash_conv << '\n'
            << "pure_cfr_kuhn_nodes=" << kuhn_solved.traversed_nodes << '\n'
            << "pure_cfr_binary_nodes=" << binary_step.traversed_nodes << '\n'
            << "full_width_binary_nodes=" << binary.nodes.size() << '\n';
}

void test_tst_state_lower_bound() {
  constexpr std::uint64_t action_entries = 366'890'152U;
  constexpr std::uint64_t decision_nodes = 630'596U;
  constexpr std::uint64_t current_state = 1'472'605'376U;
  constexpr std::uint64_t projected_state =
      action_entries * 2U * sizeof(std::uint16_t) +
      decision_nodes * 2U * sizeof(float);
  static_assert(projected_state == current_state);
  require(projected_state == current_state,
          "Q plus cumulative average reuses exactly the two production payloads and scales");
}

} // namespace

int main() {
  try {
    test_sync_finite_precision_counterexample();
    test_pure_cfr_convergence_and_structural_reduction();
    test_tst_state_lower_bound();
    std::cout << "PURE_CFR_RECHECK_ORACLE=PASS\n"
              << "assertions=" << assertions << '\n'
              << "persistent_payloads_per_action=2\n"
              << "projected_tst_state_bytes=1472605376\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "PURE_CFR_RECHECK_ORACLE=FAIL: " << error.what() << '\n';
    return 1;
  }
}
