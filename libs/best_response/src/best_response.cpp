#include "gtosd/solver/best_response.hpp"
#include "gtosd/solver/enumerated_best_response.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace gtosd {
namespace {

constexpr double probability_tolerance = 1.0e-12;

const InformationSetStrategy *find_strategy(const StrategyProfile &profile,
                                            const std::string &information_set) {
  const auto found = profile.find(information_set);
  return found == profile.end() ? nullptr : &found->second;
}

Result<std::array<double, 2>, SolverError>
evaluate_node(const FiniteGame &game, const GameNodeId node_id, const StrategyProfile &profile) {
  const auto &node = game.nodes[node_id];
  if (node.kind == GameNodeKind::Terminal) {
    return Result<std::array<double, 2>, SolverError>::success(node.payoff);
  }
  std::array<double, 2> value{0.0, 0.0};
  if (node.kind == GameNodeKind::Chance) {
    for (const auto &edge : node.edges) {
      const auto child = evaluate_node(game, edge.child, profile);
      if (!child) {
        return child;
      }
      value[0] += edge.probability * child.value()[0];
      value[1] += edge.probability * child.value()[1];
    }
    return Result<std::array<double, 2>, SolverError>::success(value);
  }

  const auto *const strategy = find_strategy(profile, node.information_set);
  if (strategy == nullptr || strategy->probabilities.size() != node.edges.size()) {
    return Result<std::array<double, 2>, SolverError>::failure(SolverError::InvalidStrategy);
  }
  for (std::size_t index = 0; index < node.edges.size(); ++index) {
    const auto child = evaluate_node(game, node.edges[index].child, profile);
    if (!child) {
      return child;
    }
    value[0] += strategy->probabilities[index] * child.value()[0];
    value[1] += strategy->probabilities[index] * child.value()[1];
  }
  return Result<std::array<double, 2>, SolverError>::success(value);
}

struct InformationSetNodes {
  std::uint32_t maximum_depth{0};
  std::vector<GameNodeId> nodes;
};

struct TraversalLocation {
  GameNodeId node{0};
  std::uint32_t depth{0};
};

void collect_depths(const FiniteGame &game, const TraversalLocation location,
                    const std::uint8_t responder,
                    std::map<std::string, InformationSetNodes> &information_sets) {
  const auto &node = game.nodes[location.node];
  if (node.kind == GameNodeKind::Decision && node.player == responder) {
    auto &entry = information_sets[node.information_set];
    entry.maximum_depth = std::max(entry.maximum_depth, location.depth);
    entry.nodes.push_back(location.node);
  }
  for (const auto &edge : node.edges) {
    collect_depths(game, {edge.child, location.depth + 1U}, responder, information_sets);
  }
}

void propagate_counterfactual_reach(const FiniteGame &game, const GameNodeId node_id,
                                    const StrategyProfile &profile, const std::uint8_t responder,
                                    const double reach, std::vector<double> &counterfactual_reach) {
  counterfactual_reach[node_id] += reach;
  const auto &node = game.nodes[node_id];
  if (node.kind == GameNodeKind::Terminal) {
    return;
  }
  if (node.kind == GameNodeKind::Chance) {
    for (const auto &edge : node.edges) {
      propagate_counterfactual_reach(game, edge.child, profile, responder, reach * edge.probability,
                                     counterfactual_reach);
    }
    return;
  }
  if (node.player == responder) {
    for (const auto &edge : node.edges) {
      propagate_counterfactual_reach(game, edge.child, profile, responder, reach,
                                     counterfactual_reach);
    }
    return;
  }
  const auto &strategy = profile.at(node.information_set);
  for (std::size_t index = 0; index < node.edges.size(); ++index) {
    propagate_counterfactual_reach(game, node.edges[index].child, profile, responder,
                                   reach * strategy.probabilities[index], counterfactual_reach);
  }
}

Result<double, SolverError>
best_response_node_value(const FiniteGame &game, const GameNodeId node_id,
                         const StrategyProfile &profile, const std::uint8_t responder,
                         const std::map<std::string, GameActionId> &policy) {
  const auto &node = game.nodes[node_id];
  if (node.kind == GameNodeKind::Terminal) {
    return Result<double, SolverError>::success(node.payoff[responder]);
  }
  if (node.kind == GameNodeKind::Chance) {
    double value = 0.0;
    for (const auto &edge : node.edges) {
      const auto child = best_response_node_value(game, edge.child, profile, responder, policy);
      if (!child) {
        return child;
      }
      value += edge.probability * child.value();
    }
    return Result<double, SolverError>::success(value);
  }
  if (node.player == responder) {
    const auto selected = policy.find(node.information_set);
    if (selected == policy.end()) {
      return Result<double, SolverError>::failure(SolverError::InvalidInformationSet);
    }
    const auto edge = std::ranges::find_if(node.edges, [&](const GameEdge &candidate) {
      return candidate.action.id == selected->second;
    });
    if (edge == node.edges.end()) {
      return Result<double, SolverError>::failure(SolverError::InvalidInformationSet);
    }
    return best_response_node_value(game, edge->child, profile, responder, policy);
  }

  const auto *const strategy = find_strategy(profile, node.information_set);
  if (strategy == nullptr || strategy->probabilities.size() != node.edges.size()) {
    return Result<double, SolverError>::failure(SolverError::InvalidStrategy);
  }
  double value = 0.0;
  for (std::size_t index = 0; index < node.edges.size(); ++index) {
    const auto child =
        best_response_node_value(game, node.edges[index].child, profile, responder, policy);
    if (!child) {
      return child;
    }
    value += strategy->probabilities[index] * child.value();
  }
  return Result<double, SolverError>::success(value);
}

bool valid_partial_strategy(const InformationSetStrategy &strategy) {
  if (strategy.player > 1U || strategy.actions.empty() ||
      strategy.actions.size() != strategy.probabilities.size()) {
    return false;
  }
  std::set<GameActionId> actions;
  double probability_sum = 0.0;
  for (std::size_t index = 0U; index < strategy.actions.size(); ++index) {
    const double probability = strategy.probabilities[index];
    if (!actions.insert(strategy.actions[index]).second || !std::isfinite(probability) ||
        probability < 0.0 || probability > 1.0) {
      return false;
    }
    probability_sum += probability;
  }
  return std::abs(probability_sum - 1.0) <= probability_tolerance;
}

Result<bool, SolverError>
populate_reach_coverage(const FiniteGame &game, const StrategyProfile &profile,
                        const std::set<std::string> &unseen_information_sets,
                        PolicyCoverageAudit &audit) {
  std::vector<std::size_t> incoming_edges(game.nodes.size(), 0U);
  for (const auto &node : game.nodes) {
    for (const auto &edge : node.edges) {
      ++incoming_edges[edge.child];
    }
  }

  std::vector<GameNodeId> ready;
  ready.reserve(game.nodes.size());
  for (GameNodeId node_id = 0U; node_id < game.nodes.size(); ++node_id) {
    if (incoming_edges[node_id] == 0U) {
      ready.push_back(node_id);
    }
  }
  std::vector<GameNodeId> order;
  order.reserve(game.nodes.size());
  while (!ready.empty()) {
    const GameNodeId node_id = ready.back();
    ready.pop_back();
    order.push_back(node_id);
    for (const auto &edge : game.nodes[node_id].edges) {
      if (--incoming_edges[edge.child] == 0U) {
        ready.push_back(edge.child);
      }
    }
  }
  if (order.size() != game.nodes.size()) {
    return Result<bool, SolverError>::failure(SolverError::InvalidGame);
  }

  std::vector<double> reach(game.nodes.size(), 0.0);
  reach[game.root] = 1.0;
  for (const GameNodeId node_id : order) {
    const auto &node = game.nodes[node_id];
    const double node_reach = reach[node_id];
    if (!std::isfinite(node_reach) || node_reach < 0.0) {
      return Result<bool, SolverError>::failure(SolverError::NumericalFailure);
    }
    if (node.kind == GameNodeKind::Terminal) {
      continue;
    }
    if (node.kind == GameNodeKind::Chance) {
      for (const auto &edge : node.edges) {
        reach[edge.child] += node_reach * edge.probability;
      }
      continue;
    }

    ++audit.decision_nodes;
    audit.total_decision_reach_mass += node_reach;
    audit.total_decision_reach_mass_by_player[node.player] += node_reach;
    if (unseen_information_sets.contains(node.information_set)) {
      ++audit.unseen_decision_nodes;
      audit.unseen_decision_reach_mass += node_reach;
      audit.unseen_decision_reach_mass_by_player[node.player] += node_reach;
    }
    const auto &strategy = profile.at(node.information_set);
    for (std::size_t index = 0U; index < node.edges.size(); ++index) {
      reach[node.edges[index].child] += node_reach * strategy.probabilities[index];
    }
  }

  const auto coverage = [](const double total, const double unseen) {
    return total == 0.0 ? 1.0 : std::clamp(1.0 - unseen / total, 0.0, 1.0);
  };
  audit.reach_weighted_coverage =
      coverage(audit.total_decision_reach_mass, audit.unseen_decision_reach_mass);
  for (std::size_t player = 0U; player < 2U; ++player) {
    audit.reach_weighted_coverage_by_player[player] =
        coverage(audit.total_decision_reach_mass_by_player[player],
                 audit.unseen_decision_reach_mass_by_player[player]);
  }
  return Result<bool, SolverError>::success(true);
}

} // namespace

Result<CompletedStrategyProfile, SolverError>
complete_strategy_profile(const FiniteGame &game, const StrategyProfile &partial_profile,
                          const PolicyCompletionRule completion_rule) {
  if (completion_rule != PolicyCompletionRule::RejectMissing &&
      completion_rule != PolicyCompletionRule::UniformUnseenV1) {
    return Result<CompletedStrategyProfile, SolverError>::failure(
        SolverError::InvalidConfiguration);
  }
  auto target_profile = uniform_strategy_profile(game);
  if (!target_profile) {
    return Result<CompletedStrategyProfile, SolverError>::failure(target_profile.error());
  }
  for (const auto &[key, strategy] : partial_profile) {
    static_cast<void>(key);
    if (!valid_partial_strategy(strategy)) {
      return Result<CompletedStrategyProfile, SolverError>::failure(SolverError::InvalidStrategy);
    }
  }

  CompletedStrategyProfile result;
  result.profile = std::move(target_profile.value());
  result.coverage.completion_rule = completion_rule;
  result.coverage.target_game_fingerprint = finite_game_fingerprint(game);
  result.coverage.supplied_information_sets = partial_profile.size();
  result.coverage.required_information_sets = result.profile.size();
  std::set<std::string> unseen_information_sets;

  for (auto &[key, expected] : result.profile) {
    ++result.coverage.required_information_sets_by_player[expected.player];
    const auto found = partial_profile.find(key);
    if (found == partial_profile.end()) {
      if (completion_rule == PolicyCompletionRule::RejectMissing) {
        return Result<CompletedStrategyProfile, SolverError>::failure(SolverError::InvalidStrategy);
      }
      unseen_information_sets.insert(key);
      ++result.coverage.unseen_information_sets;
      ++result.coverage.unseen_information_sets_by_player[expected.player];
      continue;
    }
    if (found->second.player != expected.player || found->second.actions != expected.actions) {
      return Result<CompletedStrategyProfile, SolverError>::failure(SolverError::InvalidStrategy);
    }
    expected = found->second;
    ++result.coverage.matched_information_sets;
  }

  result.coverage.unused_supplied_information_sets =
      result.coverage.supplied_information_sets - result.coverage.matched_information_sets;
  result.coverage.exact_key_coverage =
      result.coverage.required_information_sets == 0U
          ? 1.0
          : static_cast<double>(result.coverage.matched_information_sets) /
                static_cast<double>(result.coverage.required_information_sets);
  const auto reach =
      populate_reach_coverage(game, result.profile, unseen_information_sets, result.coverage);
  if (!reach) {
    return Result<CompletedStrategyProfile, SolverError>::failure(reach.error());
  }
  return Result<CompletedStrategyProfile, SolverError>::success(std::move(result));
}

const char *policy_completion_rule_name(const PolicyCompletionRule rule) noexcept {
  switch (rule) {
  case PolicyCompletionRule::RejectMissing:
    return "reject_missing";
  case PolicyCompletionRule::UniformUnseenV1:
    return "uniform_unseen_v1";
  }
  return "unknown_policy_completion";
}

Result<std::array<double, 2>, SolverError>
evaluate_strategy_profile(const FiniteGame &game, const StrategyProfile &profile) {
  const auto valid = validate_strategy_profile(game, profile);
  if (!valid) {
    return Result<std::array<double, 2>, SolverError>::failure(valid.error());
  }
  return evaluate_node(game, game.root, profile);
}

Result<RootChanceProfileEvaluation, SolverError>
evaluate_strategy_profile_by_root_chance(const FiniteGame &game, const StrategyProfile &profile) {
  const auto valid = validate_strategy_profile(game, profile);
  if (!valid) {
    return Result<RootChanceProfileEvaluation, SolverError>::failure(valid.error());
  }
  const auto &root = game.nodes[game.root];
  if (root.kind != GameNodeKind::Chance) {
    return Result<RootChanceProfileEvaluation, SolverError>::failure(
        SolverError::InvalidConfiguration);
  }

  RootChanceProfileEvaluation result;
  result.outcome_values.reserve(root.edges.size());
  result.probabilities.reserve(root.edges.size());
  for (const auto &edge : root.edges) {
    const auto outcome = evaluate_node(game, edge.child, profile);
    if (!outcome) {
      return Result<RootChanceProfileEvaluation, SolverError>::failure(outcome.error());
    }
    if (!std::isfinite(outcome.value()[0]) || !std::isfinite(outcome.value()[1])) {
      return Result<RootChanceProfileEvaluation, SolverError>::failure(
          SolverError::NumericalFailure);
    }
    result.outcome_values.push_back(outcome.value());
    result.probabilities.push_back(edge.probability);
    result.profile_value[0] += edge.probability * outcome.value()[0];
    result.profile_value[1] += edge.probability * outcome.value()[1];
  }
  if (!std::isfinite(result.profile_value[0]) || !std::isfinite(result.profile_value[1])) {
    return Result<RootChanceProfileEvaluation, SolverError>::failure(SolverError::NumericalFailure);
  }
  return Result<RootChanceProfileEvaluation, SolverError>::success(std::move(result));
}

Result<BestResponseResult, SolverError> exact_best_response(const FiniteGame &game,
                                                            const StrategyProfile &opponent_profile,
                                                            const std::uint8_t best_responder) {
  if (best_responder > 1U) {
    return Result<BestResponseResult, SolverError>::failure(SolverError::InvalidConfiguration);
  }
  const auto valid = validate_strategy_profile(game, opponent_profile);
  if (!valid) {
    return Result<BestResponseResult, SolverError>::failure(valid.error());
  }
  const auto recall = has_perfect_recall(game, best_responder);
  if (!recall || !recall.value()) {
    return Result<BestResponseResult, SolverError>::failure(
        recall ? SolverError::UnsupportedInformationStructure : recall.error());
  }

  std::map<std::string, InformationSetNodes> information_sets;
  collect_depths(game, {game.root, 0}, best_responder, information_sets);
  std::vector<double> counterfactual_reach(game.nodes.size(), 0.0);
  propagate_counterfactual_reach(game, game.root, opponent_profile, best_responder, 1.0,
                                 counterfactual_reach);

  std::vector<std::pair<std::string, InformationSetNodes>> ordered(information_sets.begin(),
                                                                   information_sets.end());
  std::ranges::sort(ordered, [](const auto &left, const auto &right) {
    if (left.second.maximum_depth != right.second.maximum_depth) {
      return left.second.maximum_depth > right.second.maximum_depth;
    }
    return left.first < right.first;
  });

  BestResponseResult result;
  result.player = best_responder;
  for (const auto &[information_set, definition] : ordered) {
    const auto &representative = game.nodes[definition.nodes.front()];
    double best_value = -std::numeric_limits<double>::infinity();
    GameActionId best_action = representative.edges.front().action.id;
    for (const auto &representative_edge : representative.edges) {
      double action_value = 0.0;
      for (const GameNodeId node_id : definition.nodes) {
        const auto &node = game.nodes[node_id];
        const auto edge = std::ranges::find_if(node.edges, [&](const GameEdge &candidate) {
          return candidate.action.id == representative_edge.action.id;
        });
        if (edge == node.edges.end()) {
          return Result<BestResponseResult, SolverError>::failure(
              SolverError::InvalidInformationSet);
        }
        const auto child = best_response_node_value(game, edge->child, opponent_profile,
                                                    best_responder, result.policy);
        if (!child) {
          return Result<BestResponseResult, SolverError>::failure(child.error());
        }
        action_value += counterfactual_reach[node_id] * child.value();
      }
      if (action_value > best_value) {
        best_value = action_value;
        best_action = representative_edge.action.id;
      }
    }
    result.policy.emplace(information_set, best_action);
    result.witnesses.push_back({information_set, best_action, best_value});
  }

  const auto root_value =
      best_response_node_value(game, game.root, opponent_profile, best_responder, result.policy);
  if (!root_value || !std::isfinite(root_value.value())) {
    return Result<BestResponseResult, SolverError>::failure(
        root_value ? SolverError::NumericalFailure : root_value.error());
  }
  result.value = root_value.value();
  return Result<BestResponseResult, SolverError>::success(std::move(result));
}

Result<NashConvResult, SolverError> calculate_nash_conv(const FiniteGame &game,
                                                        const StrategyProfile &profile) {
  const auto profile_value = evaluate_strategy_profile(game, profile);
  if (!profile_value) {
    return Result<NashConvResult, SolverError>::failure(profile_value.error());
  }
  const auto co_response = exact_best_response(game, profile, 0);
  const auto btn_response = exact_best_response(game, profile, 1);
  if (!co_response || !btn_response) {
    return Result<NashConvResult, SolverError>::failure(co_response ? btn_response.error()
                                                                    : co_response.error());
  }

  NashConvResult result;
  result.profile_value = profile_value.value();
  result.best_response_value = {co_response.value().value, btn_response.value().value};
  result.nash_conv = (result.best_response_value[0] - result.profile_value[0]) +
                     (result.best_response_value[1] - result.profile_value[1]);
  if (result.nash_conv < 0.0 && result.nash_conv > -1.0e-12) {
    result.nash_conv = 0.0;
  }
  result.normalized_nash_conv = result.nash_conv / game.initial_pot;
  result.expected_payoff_sum = result.profile_value[0] + result.profile_value[1];
  if (std::abs(result.expected_payoff_sum) <= 1.0e-12) {
    result.zero_sum_exploitability = result.nash_conv / 2.0;
  } else {
    result.zero_sum_exploitability = std::numeric_limits<double>::quiet_NaN();
  }
  if (!std::isfinite(result.nash_conv) || !std::isfinite(result.normalized_nash_conv)) {
    return Result<NashConvResult, SolverError>::failure(SolverError::NumericalFailure);
  }
  return Result<NashConvResult, SolverError>::success(result);
}

Result<CertifiedSolveResult, SolverError>
solve_with_certification(const FiniteGame &game, const SolverConfig &config,
                         const std::uint64_t certification_interval,
                         const SolverCheckpoint *resume_from) {
  if (certification_interval == 0U) {
    return Result<CertifiedSolveResult, SolverError>::failure(SolverError::InvalidConfiguration);
  }

  CertifiedSolveResult certified;
  SolverCheckpoint checkpoint;
  bool has_checkpoint = resume_from != nullptr;
  if (has_checkpoint) {
    checkpoint = *resume_from;
  }
  std::uint64_t completed = has_checkpoint ? checkpoint.completed_iterations : 0U;
  std::uint64_t traversed_nodes = 0;
  while (completed < config.iterations) {
    SolverConfig segment = config;
    segment.iterations = std::min(config.iterations, completed + certification_interval);
    const auto solved = solve_finite_game(game, segment, has_checkpoint ? &checkpoint : nullptr);
    if (!solved) {
      return Result<CertifiedSolveResult, SolverError>::failure(solved.error());
    }
    certified.solve = solved.value();
    traversed_nodes += solved.value().traversed_nodes;
    certified.solve.traversed_nodes = traversed_nodes;
    checkpoint = solved.value().checkpoint;
    has_checkpoint = true;
    completed = checkpoint.completed_iterations;
    const auto metrics = calculate_nash_conv(game, solved.value().average_strategy);
    if (!metrics) {
      return Result<CertifiedSolveResult, SolverError>::failure(metrics.error());
    }
    certified.convergence.push_back(
        {completed, metrics.value().profile_value, metrics.value().nash_conv});
  }
  if (certified.convergence.empty()) {
    const auto average = average_strategy_profile(checkpoint);
    if (!average) {
      return Result<CertifiedSolveResult, SolverError>::failure(average.error());
    }
    certified.solve.checkpoint = checkpoint;
    certified.solve.average_strategy = average.value();
    const auto metrics = calculate_nash_conv(game, average.value());
    if (!metrics) {
      return Result<CertifiedSolveResult, SolverError>::failure(metrics.error());
    }
    certified.convergence.push_back(
        {completed, metrics.value().profile_value, metrics.value().nash_conv});
  }
  return Result<CertifiedSolveResult, SolverError>::success(std::move(certified));
}

} // namespace gtosd
