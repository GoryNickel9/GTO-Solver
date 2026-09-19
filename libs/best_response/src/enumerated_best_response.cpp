#include "gtosd/solver/enumerated_best_response.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>
#include <utility>

namespace gtosd {
namespace {

std::vector<GameNodeId> topological_order(const FiniteGame &game) {
  std::vector<std::size_t> incoming(game.nodes.size());
  for (const auto &node : game.nodes) {
    for (const auto &edge : node.edges) {
      ++incoming[edge.child];
    }
  }
  std::vector<GameNodeId> order{game.root};
  for (std::size_t index = 0; index < order.size(); ++index) {
    for (const auto &edge : game.nodes[order[index]].edges) {
      if (--incoming[edge.child] == 0) {
        order.push_back(edge.child);
      }
    }
  }
  return order;
}

std::uint64_t multiply(const std::uint64_t a, const std::uint64_t b, bool &overflow) {
  if (b != 0 && a > std::numeric_limits<std::uint64_t>::max() / b) {
    overflow = true;
    return std::numeric_limits<std::uint64_t>::max();
  }
  return a * b;
}

BestResponseEnumerationEstimate estimate(const FiniteGame &game, const StrategyProfile &profile,
                                         const std::uint8_t responder) {
  BestResponseEnumerationEstimate result;
  for (const auto &[key, strategy] : profile) {
    static_cast<void>(key);
    if (strategy.player == responder) {
      ++result.information_sets;
      result.policies = multiply(result.policies, strategy.actions.size(), result.overflow);
    }
  }
  result.node_evaluations = multiply(result.policies, game.nodes.size(), result.overflow);
  return result;
}

} // namespace

Result<BestResponseEnumerationEstimate, SolverError>
estimate_best_response_enumeration(const FiniteGame &game, const std::uint8_t responder) {
  if (responder > 1) {
    return Result<BestResponseEnumerationEstimate, SolverError>::failure(
        SolverError::InvalidConfiguration);
  }
  const auto profile = uniform_strategy_profile(game);
  if (!profile) {
    return Result<BestResponseEnumerationEstimate, SolverError>::failure(profile.error());
  }
  return Result<BestResponseEnumerationEstimate, SolverError>::success(
      estimate(game, profile.value(), responder));
}

Result<bool, SolverError> has_perfect_recall(const FiniteGame &game, const std::uint8_t responder) {
  if (responder > 1) {
    return Result<bool, SolverError>::failure(SolverError::InvalidConfiguration);
  }
  const auto valid = validate_finite_game(game);
  if (!valid) {
    return Result<bool, SolverError>::failure(valid.error());
  }
  // Intern own histories. Two distinct incoming histories remain ambiguous
  // until a later own decision proves a recall violation. A shared terminal
  // reached via different own actions is legal and must not be rejected.
  constexpr auto unset = std::numeric_limits<std::size_t>::max();
  constexpr auto ambiguous = unset - 1;
  std::vector<std::size_t> history(game.nodes.size(), unset);
  history[game.root] = 0;
  std::map<std::tuple<std::size_t, std::string, GameActionId>, std::size_t> interned;
  std::map<std::string, std::size_t> expected;
  for (const auto id : topological_order(game)) {
    const auto &node = game.nodes[id];
    const bool own = node.kind == GameNodeKind::Decision && node.player == responder;
    if (own) {
      if (history[id] == ambiguous) {
        return Result<bool, SolverError>::success(false);
      }
      const auto [entry, inserted] = expected.emplace(node.information_set, history[id]);
      if (!inserted && entry->second != history[id]) {
        return Result<bool, SolverError>::success(false);
      }
    }
    for (const auto &edge : node.edges) {
      auto next = history[id];
      if (own) {
        next = interned
                   .emplace(std::make_tuple(next, node.information_set, edge.action.id),
                            interned.size() + 1)
                   .first->second;
      }
      auto &destination = history[edge.child];
      if (destination == unset) {
        destination = next;
      } else if (destination != next) {
        destination = ambiguous;
      }
    }
  }
  return Result<bool, SolverError>::success(true);
}

Result<EnumeratedBestResponse, SolverError>
enumerated_best_response(const FiniteGame &game, const StrategyProfile &profile,
                         const std::uint8_t responder, const BestResponseEnumerationLimits limits) {
  using Output = Result<EnumeratedBestResponse, SolverError>;
  if (responder > 1 || limits.maximum_policies == 0 || limits.maximum_node_evaluations == 0) {
    return Output::failure(SolverError::InvalidConfiguration);
  }
  const auto valid = validate_strategy_profile(game, profile);
  if (!valid) {
    return Output::failure(valid.error());
  }
  const auto work = estimate(game, profile, responder);
  if (work.overflow || work.policies > limits.maximum_policies ||
      work.node_evaluations > limits.maximum_node_evaluations) {
    return Output::failure(SolverError::ResourceLimitExceeded);
  }
  const auto order = topological_order(game);
  std::vector<std::string> keys;
  std::vector<const InformationSetStrategy *> definitions;
  std::map<std::string, std::size_t> indices;
  for (const auto &[key, strategy] : profile) {
    if (strategy.player == responder) {
      indices.emplace(key, keys.size());
      keys.push_back(key);
      definitions.push_back(&strategy);
    }
  }
  // Detect repetition along a path, not merely sharing across different paths.
  // Forced single-action sets cannot affect multi-affinity and may repeat.
  std::vector<bool> seen(game.nodes.size());
  for (std::size_t index = 0; index < keys.size(); ++index) {
    if (definitions[index]->actions.size() == 1) {
      continue;
    }
    std::fill(seen.begin(), seen.end(), false);
    for (const auto id : order) {
      const auto &node = game.nodes[id];
      const bool here = node.kind == GameNodeKind::Decision && node.player == responder &&
                        node.information_set == keys[index];
      if (here && seen[id]) {
        return Output::failure(SolverError::UnsupportedInformationStructure);
      }
      for (const auto &edge : node.edges) {
        seen[edge.child] = seen[edge.child] || seen[id] || here;
      }
    }
  }

  std::vector<std::size_t> own_index(game.nodes.size());
  std::vector<const std::vector<double> *> opponent(game.nodes.size(), nullptr);
  for (const auto id : order) {
    const auto &node = game.nodes[id];
    if (node.kind == GameNodeKind::Decision) {
      if (node.player == responder) {
        own_index[id] = indices.at(node.information_set);
      } else {
        opponent[id] = &profile.at(node.information_set).probabilities;
      }
    }
  }
  std::vector<std::size_t> actions(keys.size(), 0), best_actions;
  std::vector<double> values(game.nodes.size());
  double best = -std::numeric_limits<double>::infinity();
  for (std::uint64_t policy = 0; policy < work.policies; ++policy) {
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
      const auto id = *it;
      const auto &node = game.nodes[id];
      double value = 0;
      if (node.kind == GameNodeKind::Terminal) {
        value = node.payoff[responder];
      } else if (node.kind == GameNodeKind::Decision && node.player == responder) {
        value = values[node.edges[actions[own_index[id]]].child];
      } else {
        for (std::size_t a = 0; a < node.edges.size(); ++a) {
          const double probability =
              opponent[id] == nullptr ? node.edges[a].probability : (*opponent[id])[a];
          value += probability * values[node.edges[a].child];
        }
      }
      if (!std::isfinite(value)) {
        return Output::failure(SolverError::NumericalFailure);
      }
      values[id] = value;
    }
    if (values[game.root] > best) {
      best = values[game.root];
      best_actions = actions;
    }
    for (std::size_t index = 0; index < actions.size(); ++index) {
      if (++actions[index] < definitions[index]->actions.size()) {
        break;
      }
      actions[index] = 0;
    }
  }
  EnumeratedBestResponse result;
  result.work = work;
  result.response.player = responder;
  result.response.value = best;
  for (std::size_t index = 0; index < keys.size(); ++index) {
    result.response.policy.emplace(keys[index], definitions[index]->actions[best_actions[index]]);
  }
  return Output::success(std::move(result));
}

} // namespace gtosd
