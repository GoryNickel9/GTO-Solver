#pragma once

#include "gtosd/preflop_blueprint/compiled_game.hpp"

#include <cstdint>
#include <string>
#include <vector>

// Stable textual identifiers of actions and preflop nodes, shared by the
// chart export (P8), the query API and the comparator:
//
//   fold | check | call | all_in | raise_<antes> | bet_<antes>
//
// with the amount in antes, decimals separated by an underscore (10.5 antes
// is "raise_10_5"); the ids of the legacy CO40 references (raise_6, raise_10,
// call, fold, all_in) are reproduced. When two edges of one node would share
// a label the requested basis points are appended ("bet_2_5_bp3300"). A
// preflop node id lists the acting positions and the actions on its path,
// "CO", "CO_raise_6_BTN", "CO_raise_6_BTN_raise_10_5_CO"; the history steps
// are the (position, action) pairs of that path.
namespace gtosd::preflop_blueprint {

struct HistoryStep {
  std::string player;
  std::string action;
};

[[nodiscard]] std::string action_label(const Action &action);
// Labels of the edges of a decision node, unique within the node.
[[nodiscard]] std::vector<std::string> edge_labels(const CompiledGame &game, std::uint32_t node);
// Path from the root to the node as (edge index at each ancestor).
[[nodiscard]] std::vector<std::uint32_t> path_edges(const CompiledGame &game, std::uint32_t node);
[[nodiscard]] std::vector<HistoryStep> history_steps(const CompiledGame &game, std::uint32_t node);
[[nodiscard]] std::string node_path_id(const CompiledGame &game, std::uint32_t node);
// Preflop decision nodes in preorder.
[[nodiscard]] std::vector<std::uint32_t> preflop_decision_nodes(const CompiledGame &game);
// Position name of a player index ("CO", "BTN", ...).
[[nodiscard]] std::string position_name(const CompiledGame &game, std::uint8_t player);

} // namespace gtosd::preflop_blueprint
