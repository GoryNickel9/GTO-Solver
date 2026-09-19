#include "gtosd/solver/finite_game.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <string_view>

namespace gtosd {
namespace {

constexpr double probability_tolerance = 1.0e-12;

struct InformationSetDefinition {
  std::uint8_t player{0};
  std::vector<GameActionId> actions;
};

void hash_bytes(std::uint64_t &hash, const std::string_view value) {
  constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;
  for (const unsigned char byte : value) {
    hash ^= byte;
    hash *= fnv_prime;
  }
}

template <typename T> void hash_integer(std::uint64_t &hash, const T value) {
  const auto bytes = std::bit_cast<std::array<std::uint8_t, sizeof(T)>>(value);
  hash_bytes(hash, std::string_view(reinterpret_cast<const char *>(bytes.data()), bytes.size()));
}

bool finite_payoff(const std::array<double, 2> &payoff) {
  return std::isfinite(payoff[0]) && std::isfinite(payoff[1]);
}

Result<std::map<std::string, InformationSetDefinition>, SolverError>
collect_information_sets(const FiniteGame &game) {
  std::map<std::string, InformationSetDefinition> definitions;
  for (const auto &node : game.nodes) {
    if (node.kind != GameNodeKind::Decision) {
      continue;
    }
    std::vector<GameActionId> actions;
    actions.reserve(node.edges.size());
    for (const auto &edge : node.edges) {
      actions.push_back(edge.action.id);
    }
    const InformationSetDefinition candidate{node.player, actions};
    const auto [iterator, inserted] = definitions.emplace(node.information_set, candidate);
    if (!inserted &&
        (iterator->second.player != candidate.player || iterator->second.actions != actions)) {
      return Result<std::map<std::string, InformationSetDefinition>, SolverError>::failure(
          SolverError::InvalidInformationSet);
    }
  }
  return Result<std::map<std::string, InformationSetDefinition>, SolverError>::success(
      std::move(definitions));
}

} // namespace

Result<GameSummary, SolverError> validate_finite_game(const FiniteGame &game) {
  if (game.game_id.empty() || game.nodes.empty() || game.root >= game.nodes.size() ||
      !std::isfinite(game.initial_pot) || game.initial_pot <= 0.0) {
    return Result<GameSummary, SolverError>::failure(SolverError::InvalidGame);
  }

  const auto information_sets = collect_information_sets(game);
  if (!information_sets) {
    return Result<GameSummary, SolverError>::failure(information_sets.error());
  }

  GameSummary summary;
  summary.nodes = game.nodes.size();
  summary.information_sets = information_sets.value().size();
  std::vector<std::uint8_t> colors(game.nodes.size(), 0);
  std::vector<std::pair<GameNodeId, std::uint32_t>> stack{{game.root, 0}};
  while (!stack.empty()) {
    const auto [node_id, depth] = stack.back();
    if (colors[node_id] == 2U) {
      stack.pop_back();
      continue;
    }
    const auto &node = game.nodes[node_id];
    if (colors[node_id] == 0U) {
      colors[node_id] = 1U;
      summary.maximum_depth = std::max(summary.maximum_depth, depth);
      if (node.kind == GameNodeKind::Terminal) {
        ++summary.terminal_nodes;
        if (!node.edges.empty() || !node.information_set.empty() || !finite_payoff(node.payoff)) {
          return Result<GameSummary, SolverError>::failure(SolverError::InvalidNode);
        }
      } else {
        if (node.edges.empty()) {
          return Result<GameSummary, SolverError>::failure(SolverError::InvalidNode);
        }
        if (node.kind == GameNodeKind::Chance) {
          ++summary.chance_nodes;
          double probability_sum = 0.0;
          for (const auto &edge : node.edges) {
            if (!std::isfinite(edge.probability) || edge.probability <= 0.0) {
              return Result<GameSummary, SolverError>::failure(
                  SolverError::InvalidChanceProbabilities);
            }
            probability_sum += edge.probability;
          }
          if (std::abs(probability_sum - 1.0) > probability_tolerance) {
            return Result<GameSummary, SolverError>::failure(
                SolverError::InvalidChanceProbabilities);
          }
        } else {
          ++summary.decision_nodes;
          if (node.player > 1U || node.information_set.empty()) {
            return Result<GameSummary, SolverError>::failure(SolverError::InvalidInformationSet);
          }
        }
        std::set<GameActionId> action_ids;
        for (const auto &edge : node.edges) {
          if (edge.child >= game.nodes.size() || edge.action.label.empty() ||
              !action_ids.insert(edge.action.id).second) {
            return Result<GameSummary, SolverError>::failure(SolverError::InvalidNode);
          }
        }
      }
    }

    bool pushed_child = false;
    for (const auto &edge : node.edges) {
      if (colors[edge.child] == 1U) {
        return Result<GameSummary, SolverError>::failure(SolverError::InvalidGame);
      }
      if (colors[edge.child] == 0U) {
        stack.emplace_back(edge.child, depth + 1U);
        pushed_child = true;
        break;
      }
    }
    if (!pushed_child) {
      colors[node_id] = 2U;
      stack.pop_back();
    }
  }

  if (std::ranges::any_of(colors, [](const auto color) { return color == 0U; })) {
    return Result<GameSummary, SolverError>::failure(SolverError::InvalidGame);
  }
  summary.fingerprint = finite_game_fingerprint(game);
  return Result<GameSummary, SolverError>::success(std::move(summary));
}

Result<StrategyProfile, SolverError> uniform_strategy_profile(const FiniteGame &game) {
  const auto validation = validate_finite_game(game);
  if (!validation) {
    return Result<StrategyProfile, SolverError>::failure(validation.error());
  }
  StrategyProfile profile;
  for (const auto &node : game.nodes) {
    if (node.kind != GameNodeKind::Decision || profile.contains(node.information_set)) {
      continue;
    }
    InformationSetStrategy strategy;
    strategy.player = node.player;
    const double probability = 1.0 / static_cast<double>(node.edges.size());
    for (const auto &edge : node.edges) {
      strategy.actions.push_back(edge.action.id);
      strategy.probabilities.push_back(probability);
    }
    profile.emplace(node.information_set, std::move(strategy));
  }
  return Result<StrategyProfile, SolverError>::success(std::move(profile));
}

Result<bool, SolverError> validate_strategy_profile(const FiniteGame &game,
                                                    const StrategyProfile &profile) {
  const auto uniform = uniform_strategy_profile(game);
  if (!uniform || uniform.value().size() != profile.size()) {
    return Result<bool, SolverError>::failure(uniform ? SolverError::InvalidStrategy
                                                      : uniform.error());
  }
  for (const auto &[key, expected] : uniform.value()) {
    const auto found = profile.find(key);
    if (found == profile.end() || found->second.player != expected.player ||
        found->second.actions != expected.actions ||
        found->second.probabilities.size() != expected.actions.size()) {
      return Result<bool, SolverError>::failure(SolverError::InvalidStrategy);
    }
    double sum = 0.0;
    for (const double probability : found->second.probabilities) {
      if (!std::isfinite(probability) || probability < 0.0 || probability > 1.0) {
        return Result<bool, SolverError>::failure(SolverError::InvalidStrategy);
      }
      sum += probability;
    }
    if (std::abs(sum - 1.0) > probability_tolerance) {
      return Result<bool, SolverError>::failure(SolverError::InvalidStrategy);
    }
  }
  return Result<bool, SolverError>::success(true);
}

std::string finite_game_fingerprint(const FiniteGame &game) {
  std::uint64_t hash = 14'695'981'039'346'656'037ULL;
  hash_bytes(hash, "GTOSD_FINITE_GAME_1");
  hash_bytes(hash, game.game_id);
  hash_integer(hash, game.root);
  hash_integer(hash, std::bit_cast<std::uint64_t>(game.initial_pot));
  hash_integer(hash, static_cast<std::uint64_t>(game.nodes.size()));
  for (const auto &node : game.nodes) {
    hash_integer(hash, static_cast<std::uint8_t>(node.kind));
    hash_integer(hash, node.player);
    hash_bytes(hash, node.information_set);
    hash_integer(hash, std::bit_cast<std::uint64_t>(node.payoff[0]));
    hash_integer(hash, std::bit_cast<std::uint64_t>(node.payoff[1]));
    hash_integer(hash, static_cast<std::uint64_t>(node.edges.size()));
    for (const auto &edge : node.edges) {
      hash_integer(hash, edge.action.id);
      hash_bytes(hash, edge.action.label);
      hash_integer(hash, edge.child);
      hash_integer(hash, std::bit_cast<std::uint64_t>(edge.probability));
    }
  }
  std::ostringstream output;
  output << "fnv1a64:" << std::hex << std::setw(16) << std::setfill('0') << hash;
  return output.str();
}

const char *solver_error_name(const SolverError error) noexcept {
  switch (error) {
  case SolverError::InvalidGame:
    return "invalid_game";
  case SolverError::InvalidNode:
    return "invalid_node";
  case SolverError::InvalidChanceProbabilities:
    return "invalid_chance_probabilities";
  case SolverError::InvalidInformationSet:
    return "invalid_information_set";
  case SolverError::InvalidStrategy:
    return "invalid_strategy";
  case SolverError::InvalidConfiguration:
    return "invalid_configuration";
  case SolverError::UnsupportedAlgorithm:
    return "unsupported_algorithm";
  case SolverError::NumericalFailure:
    return "numerical_failure";
  case SolverError::GameMismatch:
    return "game_mismatch";
  case SolverError::InvalidCheckpoint:
    return "invalid_checkpoint";
  case SolverError::UnsupportedCheckpointVersion:
    return "unsupported_checkpoint_version";
  case SolverError::InvalidAbstraction:
    return "invalid_abstraction";
  case SolverError::UnsupportedAbstractionVersion:
    return "unsupported_abstraction_version";
  case SolverError::InvalidSubgame:
    return "invalid_subgame";
  case SolverError::UnsupportedSubgameVersion:
    return "unsupported_subgame_version";
  case SolverError::IoFailure:
    return "io_failure";
  case SolverError::ResourceLimitExceeded:
    return "resource_limit_exceeded";
  case SolverError::UnsupportedInformationStructure:
    return "unsupported_information_structure";
  }
  return "unknown_solver_error";
}

} // namespace gtosd
