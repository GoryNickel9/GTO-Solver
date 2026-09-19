#pragma once

#include "gtosd/solver/finite_game.hpp"
#include <nlohmann/json.hpp>

namespace gtosd::research {
inline nlohmann::json profile_json(const StrategyProfile &profile) {
  nlohmann::json result = nlohmann::json::object();
  for (const auto &[key, strategy] : profile) {
    result[key] = {{"player", strategy.player},
                   {"actions", strategy.actions},
                   {"probabilities", strategy.probabilities}};
  }
  return result;
}
inline nlohmann::json finite_game_json(const FiniteGame &game) {
  nlohmann::json result{{"game_id", game.game_id},
                        {"root", game.root},
                        {"initial_pot", game.initial_pot},
                        {"fingerprint", finite_game_fingerprint(game)}};
  result["nodes"] = nlohmann::json::array();
  for (const auto &node : game.nodes) {
    nlohmann::json edges = nlohmann::json::array();
    for (const auto &edge : node.edges) {
      edges.push_back(
          {{"action", edge.action.id}, {"child", edge.child}, {"probability", edge.probability}});
    }
    result["nodes"].push_back({{"kind", static_cast<unsigned>(node.kind)},
                               {"player", node.player},
                               {"information_set", node.information_set},
                               {"edges", std::move(edges)},
                               {"payoff", node.payoff}});
  }
  return result;
}
} // namespace gtosd::research
