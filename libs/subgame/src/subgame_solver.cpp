#include "gtosd/subgame/subgame_solver.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <utility>

namespace gtosd {
namespace {

constexpr double probability_tolerance = 1.0e-12;

std::vector<SubgameRoot> canonical_roots(std::vector<SubgameRoot> roots) {
  std::ranges::sort(roots, {}, &SubgameRoot::node);
  return roots;
}

void mark_reachable(const FiniteGame &game, const GameNodeId node_id,
                    std::vector<bool> &reachable) {
  if (reachable[node_id]) {
    return;
  }
  reachable[node_id] = true;
  for (const auto &edge : game.nodes[node_id].edges) {
    mark_reachable(game, edge.child, reachable);
  }
}

bool roots_overlap(const FiniteGame &game, const std::vector<SubgameRoot> &roots) {
  std::set<GameNodeId> root_ids;
  for (const auto &root : roots) {
    if (!root_ids.insert(root.node).second) {
      return true;
    }
  }
  for (const auto &root : roots) {
    std::vector<bool> descendants(game.nodes.size(), false);
    mark_reachable(game, root.node, descendants);
    for (const auto other : root_ids) {
      if (other != root.node && descendants[other]) {
        return true;
      }
    }
  }
  return false;
}

Result<std::vector<bool>, SubgameError> validate_frontier(const FiniteGame &game,
                                                          const std::vector<SubgameRoot> &roots) {
  if (roots.empty()) {
    return Result<std::vector<bool>, SubgameError>::failure(SubgameError::InvalidFrontier);
  }
  std::vector<bool> full_reachable(game.nodes.size(), false);
  mark_reachable(game, game.root, full_reachable);
  double total_weight = 0.0;
  for (const auto &root : roots) {
    if (root.node >= game.nodes.size() || !full_reachable[root.node] ||
        !std::isfinite(root.reach_weight) || root.reach_weight <= 0.0) {
      return Result<std::vector<bool>, SubgameError>::failure(SubgameError::InvalidFrontier);
    }
    total_weight += root.reach_weight;
  }
  if (!std::isfinite(total_weight) || total_weight <= 0.0) {
    return Result<std::vector<bool>, SubgameError>::failure(SubgameError::InvalidFrontier);
  }
  if (roots_overlap(game, roots)) {
    return Result<std::vector<bool>, SubgameError>::failure(SubgameError::OverlappingRoots);
  }

  std::vector<bool> inside(game.nodes.size(), false);
  for (const auto &root : roots) {
    mark_reachable(game, root.node, inside);
  }
  std::map<std::string, std::pair<bool, bool>> boundary;
  for (std::size_t node_id = 0; node_id < game.nodes.size(); ++node_id) {
    if (!full_reachable[node_id] || game.nodes[node_id].kind != GameNodeKind::Decision) {
      continue;
    }
    auto &location = boundary[game.nodes[node_id].information_set];
    if (inside[node_id]) {
      location.first = true;
    } else {
      location.second = true;
    }
  }
  if (std::ranges::any_of(
          boundary, [](const auto &entry) { return entry.second.first && entry.second.second; })) {
    return Result<std::vector<bool>, SubgameError>::failure(
        SubgameError::InformationSetCrossesBoundary);
  }
  return Result<std::vector<bool>, SubgameError>::success(std::move(inside));
}

FiniteGame extract_subgame(const FiniteGame &game, const std::vector<SubgameRoot> &roots) {
  FiniteGame subgame;
  subgame.initial_pot = game.initial_pot;
  std::map<GameNodeId, GameNodeId> cloned;
  std::function<GameNodeId(GameNodeId)> clone = [&](const GameNodeId source_id) {
    const auto existing = cloned.find(source_id);
    if (existing != cloned.end()) {
      return existing->second;
    }
    const auto target_id = static_cast<GameNodeId>(subgame.nodes.size());
    cloned.emplace(source_id, target_id);
    subgame.nodes.emplace_back();
    GameNode node = game.nodes[source_id];
    for (auto &edge : node.edges) {
      edge.child = clone(edge.child);
    }
    subgame.nodes[target_id] = std::move(node);
    return target_id;
  };

  std::vector<GameNodeId> cloned_roots;
  cloned_roots.reserve(roots.size());
  for (const auto &root : roots) {
    cloned_roots.push_back(clone(root.node));
  }
  if (roots.size() == 1U) {
    subgame.root = cloned_roots.front();
  } else {
    double total_weight = 0.0;
    for (const auto &root : roots) {
      total_weight += root.reach_weight;
    }
    GameNode chance;
    chance.kind = GameNodeKind::Chance;
    for (std::size_t index = 0; index < roots.size(); ++index) {
      chance.edges.push_back(
          {{static_cast<GameActionId>(index), "frontier_" + std::to_string(roots[index].node)},
           cloned_roots[index],
           roots[index].reach_weight / total_weight});
    }
    subgame.root = static_cast<GameNodeId>(subgame.nodes.size());
    subgame.nodes.push_back(std::move(chance));
  }

  std::uint64_t hash = 14'695'981'039'346'656'037ULL;
  constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;
  const auto hash_byte = [&](const std::uint8_t byte) {
    hash ^= byte;
    hash *= fnv_prime;
  };
  for (const auto &root : roots) {
    for (const auto byte : std::bit_cast<std::array<std::uint8_t, sizeof(root.node)>>(root.node)) {
      hash_byte(byte);
    }
    const auto weight_bits = std::bit_cast<std::uint64_t>(root.reach_weight);
    for (const auto byte :
         std::bit_cast<std::array<std::uint8_t, sizeof(weight_bits)>>(weight_bits)) {
      hash_byte(byte);
    }
  }
  std::ostringstream suffix;
  suffix << std::hex << std::setfill('0') << std::setw(16) << hash;
  subgame.game_id = game.game_id + "|subgame=" + suffix.str();
  return subgame;
}

Result<StrategyProfile, SubgameError>
merge_candidate(const FiniteGame &full_game, const StrategyProfile &blueprint,
                const StrategyProfile &subgame_strategy,
                std::vector<std::string> &replaced_information_sets) {
  StrategyProfile candidate = blueprint;
  for (const auto &[information_set, strategy] : subgame_strategy) {
    const auto found = candidate.find(information_set);
    if (found == candidate.end() || found->second.player != strategy.player ||
        found->second.actions != strategy.actions) {
      return Result<StrategyProfile, SubgameError>::failure(SubgameError::InvalidBlueprint);
    }
    found->second = strategy;
    replaced_information_sets.push_back(information_set);
  }
  if (!validate_strategy_profile(full_game, candidate)) {
    return Result<StrategyProfile, SubgameError>::failure(SubgameError::InvalidBlueprint);
  }
  return Result<StrategyProfile, SubgameError>::success(std::move(candidate));
}

} // namespace

Result<std::vector<SubgameRoot>, SubgameError>
derive_reach_weighted_subgame_roots(const FiniteGame &game, const StrategyProfile &blueprint,
                                    const std::vector<GameNodeId> &frontier) {
  if (!validate_finite_game(game)) {
    return Result<std::vector<SubgameRoot>, SubgameError>::failure(SubgameError::InvalidGame);
  }
  if (!validate_strategy_profile(game, blueprint)) {
    return Result<std::vector<SubgameRoot>, SubgameError>::failure(SubgameError::InvalidBlueprint);
  }
  std::vector<SubgameRoot> roots;
  roots.reserve(frontier.size());
  for (const auto node : frontier) {
    roots.push_back({node, 0.0});
  }
  // Validate ids, duplicates, and ancestor/descendant overlap before traversal.
  auto validation_roots = roots;
  for (auto &root : validation_roots) {
    root.reach_weight = 1.0;
  }
  const auto boundary = validate_frontier(game, validation_roots);
  if (!boundary) {
    return Result<std::vector<SubgameRoot>, SubgameError>::failure(boundary.error());
  }
  std::map<GameNodeId, std::size_t> positions;
  for (std::size_t index = 0; index < roots.size(); ++index) {
    positions.emplace(roots[index].node, index);
  }
  std::function<void(GameNodeId, double)> traverse = [&](const GameNodeId node_id,
                                                         const double reach) {
    const auto target = positions.find(node_id);
    if (target != positions.end()) {
      roots[target->second].reach_weight += reach;
      return;
    }
    const auto &node = game.nodes[node_id];
    if (node.kind == GameNodeKind::Terminal) {
      return;
    }
    if (node.kind == GameNodeKind::Chance) {
      for (const auto &edge : node.edges) {
        traverse(edge.child, reach * edge.probability);
      }
      return;
    }
    const auto &strategy = blueprint.at(node.information_set);
    for (std::size_t index = 0; index < node.edges.size(); ++index) {
      traverse(node.edges[index].child, reach * strategy.probabilities[index]);
    }
  };
  traverse(game.root, 1.0);
  if (std::ranges::any_of(roots, [](const SubgameRoot &root) {
        return !std::isfinite(root.reach_weight) || root.reach_weight <= probability_tolerance;
      })) {
    return Result<std::vector<SubgameRoot>, SubgameError>::failure(SubgameError::InvalidFrontier);
  }
  return Result<std::vector<SubgameRoot>, SubgameError>::success(canonical_roots(std::move(roots)));
}

Result<SubgameSolveResult, SubgameError> solve_subgame(const FiniteGame &full_game,
                                                       const StrategyProfile &blueprint,
                                                       const std::vector<SubgameRoot> &roots,
                                                       const SubgameSolveConfig &config) {
  if (config.major != SubgameSolveConfig::format_major ||
      config.minor > SubgameSolveConfig::format_minor) {
    return Result<SubgameSolveResult, SubgameError>::failure(SubgameError::UnsupportedVersion);
  }
  if (!std::isfinite(config.safety_tolerance) || config.safety_tolerance < 0.0 ||
      config.solver.iterations == 0U ||
      (config.safety != SubgameSafetyMode::UnsafeIsolated &&
       config.safety != SubgameSafetyMode::ExactNashConvGuard)) {
    return Result<SubgameSolveResult, SubgameError>::failure(SubgameError::InvalidConfiguration);
  }
  if (!validate_finite_game(full_game)) {
    return Result<SubgameSolveResult, SubgameError>::failure(SubgameError::InvalidGame);
  }
  if (!validate_strategy_profile(full_game, blueprint)) {
    return Result<SubgameSolveResult, SubgameError>::failure(SubgameError::InvalidBlueprint);
  }
  const auto ordered_roots = canonical_roots(roots);
  const auto frontier = validate_frontier(full_game, ordered_roots);
  if (!frontier) {
    return Result<SubgameSolveResult, SubgameError>::failure(frontier.error());
  }

  SubgameSolveResult result;
  result.subgame = extract_subgame(full_game, ordered_roots);
  if (!validate_finite_game(result.subgame)) {
    return Result<SubgameSolveResult, SubgameError>::failure(SubgameError::InvalidGame);
  }
  const auto solved = solve_finite_game(result.subgame, config.solver);
  if (!solved) {
    return Result<SubgameSolveResult, SubgameError>::failure(SubgameError::SolverFailure);
  }
  result.solve = solved.value();
  const auto candidate = merge_candidate(full_game, blueprint, solved.value().average_strategy,
                                         result.replaced_information_sets);
  if (!candidate) {
    return Result<SubgameSolveResult, SubgameError>::failure(candidate.error());
  }
  result.candidate_strategy = candidate.value();
  if (config.safety == SubgameSafetyMode::UnsafeIsolated) {
    result.deployed_strategy = result.candidate_strategy;
    result.deployment = SubgameDeployment::CandidateAccepted;
    return Result<SubgameSolveResult, SubgameError>::success(std::move(result));
  }
  const auto baseline_metrics = calculate_nash_conv(full_game, blueprint);
  const auto candidate_metrics = calculate_nash_conv(full_game, result.candidate_strategy);
  if (!baseline_metrics || !candidate_metrics) {
    return Result<SubgameSolveResult, SubgameError>::failure(SubgameError::CertificationFailure);
  }
  result.baseline_metrics = baseline_metrics.value();
  result.candidate_metrics = candidate_metrics.value();
  if (!std::isfinite(result.baseline_metrics->nash_conv) ||
      !std::isfinite(result.candidate_metrics->nash_conv)) {
    return Result<SubgameSolveResult, SubgameError>::failure(SubgameError::NumericalFailure);
  }

  const bool accepted = result.candidate_metrics->nash_conv <=
                        result.baseline_metrics->nash_conv + config.safety_tolerance;
  if (accepted) {
    result.deployed_strategy = result.candidate_strategy;
    result.deployed_metrics = *result.candidate_metrics;
    result.deployment = SubgameDeployment::CandidateAccepted;
  } else {
    result.deployed_strategy = blueprint;
    result.deployed_metrics = *result.baseline_metrics;
    result.deployment = SubgameDeployment::BlueprintFallback;
  }
  return Result<SubgameSolveResult, SubgameError>::success(std::move(result));
}

Result<AbstractSubgameSolveResult, SubgameError>
solve_abstract_subgame(const FiniteGame &exact_game, const CardAbstraction &abstraction,
                       const StrategyProfile &abstract_blueprint,
                       const std::vector<SubgameRoot> &abstract_roots,
                       const SubgameSolveConfig &config) {
  const auto abstract_game = apply_card_abstraction(exact_game, abstraction);
  if (!abstract_game) {
    return Result<AbstractSubgameSolveResult, SubgameError>::failure(
        SubgameError::AbstractionFailure);
  }
  const auto exact_blueprint =
      lift_card_abstraction_strategy(exact_game, abstraction, abstract_blueprint);
  if (!exact_blueprint) {
    return Result<AbstractSubgameSolveResult, SubgameError>::failure(
        SubgameError::AbstractionFailure);
  }

  // Always obtain the abstract candidate. The only deploy decision for the
  // composed path is made below, after lifting to the exact game.
  auto candidate_config = config;
  candidate_config.safety = SubgameSafetyMode::UnsafeIsolated;
  const auto abstract_solve =
      solve_subgame(abstract_game.value(), abstract_blueprint, abstract_roots, candidate_config);
  if (!abstract_solve) {
    return Result<AbstractSubgameSolveResult, SubgameError>::failure(abstract_solve.error());
  }
  const auto exact_candidate = lift_card_abstraction_strategy(
      exact_game, abstraction, abstract_solve.value().candidate_strategy);
  if (!exact_candidate) {
    return Result<AbstractSubgameSolveResult, SubgameError>::failure(
        SubgameError::AbstractionFailure);
  }

  AbstractSubgameSolveResult result;
  result.abstraction = abstraction;
  result.abstract_solve = abstract_solve.value();
  result.exact_candidate_strategy = exact_candidate.value();
  if (config.safety == SubgameSafetyMode::UnsafeIsolated) {
    result.deployed_abstract_strategy = result.abstract_solve.candidate_strategy;
    result.exact_deployed_strategy = result.exact_candidate_strategy;
    result.deployment = SubgameDeployment::CandidateAccepted;
    return Result<AbstractSubgameSolveResult, SubgameError>::success(std::move(result));
  }
  const auto exact_baseline_metrics = calculate_nash_conv(exact_game, exact_blueprint.value());
  const auto exact_candidate_metrics = calculate_nash_conv(exact_game, exact_candidate.value());
  if (!exact_baseline_metrics || !exact_candidate_metrics) {
    return Result<AbstractSubgameSolveResult, SubgameError>::failure(
        SubgameError::CertificationFailure);
  }
  result.exact_baseline_metrics = exact_baseline_metrics.value();
  result.exact_candidate_metrics = exact_candidate_metrics.value();
  const bool accepted = result.exact_candidate_metrics->nash_conv <=
                        result.exact_baseline_metrics->nash_conv + config.safety_tolerance;
  if (accepted) {
    result.deployed_abstract_strategy = result.abstract_solve.candidate_strategy;
    result.exact_deployed_strategy = result.exact_candidate_strategy;
    result.exact_deployed_metrics = *result.exact_candidate_metrics;
    result.deployment = SubgameDeployment::CandidateAccepted;
  } else {
    result.deployed_abstract_strategy = abstract_blueprint;
    result.exact_deployed_strategy = exact_blueprint.value();
    result.exact_deployed_metrics = *result.exact_baseline_metrics;
    result.deployment = SubgameDeployment::BlueprintFallback;
  }
  return Result<AbstractSubgameSolveResult, SubgameError>::success(std::move(result));
}

const char *subgame_safety_mode_name(const SubgameSafetyMode mode) noexcept {
  switch (mode) {
  case SubgameSafetyMode::UnsafeIsolated:
    return "unsafe_isolated";
  case SubgameSafetyMode::ExactNashConvGuard:
    return "exact_nash_conv_guard";
  }
  return "unknown";
}

const char *subgame_error_name(const SubgameError error) noexcept {
  switch (error) {
  case SubgameError::InvalidConfiguration:
    return "invalid_configuration";
  case SubgameError::InvalidGame:
    return "invalid_game";
  case SubgameError::InvalidBlueprint:
    return "invalid_blueprint";
  case SubgameError::InvalidFrontier:
    return "invalid_frontier";
  case SubgameError::OverlappingRoots:
    return "overlapping_roots";
  case SubgameError::InformationSetCrossesBoundary:
    return "information_set_crosses_boundary";
  case SubgameError::SolverFailure:
    return "solver_failure";
  case SubgameError::CertificationFailure:
    return "certification_failure";
  case SubgameError::AbstractionFailure:
    return "abstraction_failure";
  case SubgameError::NumericalFailure:
    return "numerical_failure";
  case SubgameError::UnsupportedVersion:
    return "unsupported_version";
  }
  return "unknown_subgame_error";
}

} // namespace gtosd
