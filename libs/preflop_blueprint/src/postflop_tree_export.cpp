#include "gtosd/preflop_blueprint/postflop_tree_export.hpp"

#include "gtosd/preflop_blueprint/action_labels.hpp"

#include <algorithm>
#include <functional>
#include <iomanip>
#include <sstream>

namespace gtosd::preflop_blueprint {
namespace {

constexpr double ante_scale = 1.0 / static_cast<double>(Money::units_per_ante);

std::string number(const double value) {
  std::ostringstream stream;
  stream << std::setprecision(17) << value;
  return stream.str();
}

std::string quote(const std::string &value) {
  std::string out = "\"";
  for (const auto character : value) {
    if (character == '"' || character == '\\') {
      out.push_back('\\');
    }
    out.push_back(character);
  }
  out.push_back('"');
  return out;
}

double antes(const Money value) { return static_cast<double>(value.units()) * ante_scale; }

const char *action_type_name(const ActionType type) {
  switch (type) {
  case ActionType::Fold:
    return "fold";
  case ActionType::Check:
    return "check";
  case ActionType::Call:
    return "call";
  case ActionType::Bet:
    return "bet";
  case ActionType::Raise:
    return "raise";
  case ActionType::AllIn:
    return "all_in";
  }
  return "unknown";
}

std::string state_json(const CompiledGame &game, const std::uint32_t node) {
  const auto &state = game.states()[node];
  const auto &entry = game.nodes()[node];
  const auto actor = entry.kind == NodeKind::Decision ? entry.actor : state.player_to_act;
  const auto to_call = state.current_bet.units() - state.committed_this_street[actor].units();
  std::string json = "{\"street\": " + quote(street_name(entry.street)) +
                     ", \"player\": " + quote(position_name(game, actor)) +
                     ", \"potAnte\": " + number(antes(state.pot)) +
                     ", \"toCallAnte\": " + number(static_cast<double>(std::max<std::int64_t>(0, to_call)) * ante_scale) +
                     ", \"remainingStackAnte\": [";
  for (std::uint8_t player = 0; player < state.player_count; ++player) {
    json += (player > 0U ? ", " : "") + number(antes(state.remaining_stacks[player]));
  }
  json += "], \"committedTotalAnte\": [";
  for (std::uint8_t player = 0; player < state.player_count; ++player) {
    json += (player > 0U ? ", " : "") + number(antes(state.committed_total[player]));
  }
  json += "], \"committedStreetAnte\": [";
  for (std::uint8_t player = 0; player < state.player_count; ++player) {
    json += (player > 0U ? ", " : "") + number(antes(state.committed_this_street[player]));
  }
  json += "], \"raiseCount\": " + std::to_string(state.raise_count_this_street) + "}";
  return json;
}

} // namespace

PostflopTreeExport export_postflop_tree(const CompiledGame &game) {
  PostflopTreeExport result;
  const auto &nodes = game.nodes();
  result.export_id.assign(nodes.size(), no_node);
  // Contiguous ids of the postflop decision nodes in preorder.
  std::uint32_t next_id = 0U;
  for (const auto &node : nodes) {
    if (node.postflop_entry != no_entry && node.kind == NodeKind::Decision) {
      result.export_id[node.id] = next_id++;
    }
  }
  const auto entries = game.postflop_entries();
  const auto entry_of = [&](const std::uint32_t node) -> std::uint32_t {
    const auto index = nodes[node].postflop_entry;
    return index == no_entry ? no_node : entries[index];
  };

  std::function<std::string(std::uint32_t)> target_json;
  target_json = [&](const std::uint32_t node) -> std::string {
    const auto &entry = nodes[node];
    switch (entry.kind) {
    case NodeKind::Decision:
      return "{\"kind\": \"decision\", \"node\": " + std::to_string(result.export_id[node]) +
             ", \"compiledNode\": " + std::to_string(node) + "}";
    case NodeKind::Chance: {
      const auto child = game.edges_of(node)[0].child;
      return "{\"kind\": \"chance\", \"deals\": " + quote(street_name(nodes[child].street)) +
             ", \"compiledNode\": " + std::to_string(node) + ", \"next\": " + target_json(child) + "}";
    }
    case NodeKind::TerminalFold:
      return "{\"kind\": \"terminal\", \"status\": \"folded\", \"compiledNode\": " +
             std::to_string(node) + ", \"state\": " + state_json(game, node) + "}";
    case NodeKind::TerminalShowdown:
      return std::string("{\"kind\": \"terminal\", \"status\": ") +
             (entry.remaining_board_cards > 0U ? "\"all_in_runout\"" : "\"showdown\"") +
             ", \"compiledNode\": " + std::to_string(node) + ", \"state\": " + state_json(game, node) + "}";
    }
    return "{}";
  };

  // Counts over the postflop part.
  for (const auto &node : nodes) {
    if (node.postflop_entry == no_entry) {
      continue;
    }
    ++result.stats.represented_nodes;
    result.stats.maximum_raise_count =
        std::max<std::uint32_t>(result.stats.maximum_raise_count, game.states()[node.id].raise_count_this_street);
    switch (node.kind) {
    case NodeKind::Decision:
      ++result.stats.decision_nodes;
      result.stats.action_edges += node.action_count;
      break;
    case NodeKind::Chance:
      ++result.stats.chance_frontiers;
      break;
    case NodeKind::TerminalFold:
      ++result.stats.terminal_folds;
      break;
    case NodeKind::TerminalShowdown:
      if (node.remaining_board_cards > 0U) {
        ++result.stats.terminal_all_in_runouts;
      } else {
        ++result.stats.terminal_showdowns;
      }
      break;
    }
  }

  std::string json = "{\n";
  json += "  \"schema\": \"gtosd.hu_postflop_public_tree.v1\",\n";
  json += "  \"configId\": " + quote(game.config().id) + ",\n";
  json += "  \"treeFingerprint\": " + quote(game.fingerprint()) + ",\n";
  json += "  \"positions\": [";
  for (std::size_t index = 0; index < game.config().positions.size(); ++index) {
    json += (index > 0U ? ", " : "") + quote(game.config().positions[index]);
  }
  json += "],\n  \"entries\": [\n";
  for (std::size_t index = 0; index < entries.size(); ++index) {
    const auto entry = entries[index];
    const auto steps = history_steps(game, entry);
    json += std::string(index > 0U ? ",\n" : "") + "    {\"preflopNode\": " + std::to_string(entry) +
            ", \"id\": " + quote(node_path_id(game, entry)) + ", \"history\": [";
    for (std::size_t step = 0; step < steps.size(); ++step) {
      json += (step > 0U ? ", " : "") + std::string("{\"player\": ") + quote(steps[step].player) +
              ", \"action\": " + quote(steps[step].action) + "}";
    }
    json += "], \"target\": " + target_json(entry) + "}";
  }
  json += "\n  ],\n  \"nodes\": [\n";
  bool first = true;
  for (const auto &node : nodes) {
    if (node.postflop_entry == no_entry || node.kind != NodeKind::Decision) {
      continue;
    }
    const auto entry = entry_of(node.id);
    const auto &state = game.states()[node.id];
    const auto &entry_state = game.states()[entry];
    static_cast<void>(entry_state);
    json += std::string(first ? "" : ",\n") + "    {\"id\": " + std::to_string(result.export_id[node.id]) +
            ", \"compiledNode\": " + std::to_string(node.id) + ", \"entryNode\": " + std::to_string(entry) +
            ", \"depth\": " + std::to_string(node.depth - nodes[entry].depth) +
            ", \"state\": " + state_json(game, node.id) + ", \"actions\": [";
    first = false;
    const auto edges = game.edges_of(node.id);
    const auto labels = edge_labels(game, node.id);
    for (std::size_t action = 0; action < edges.size(); ++action) {
      const auto &edge = edges[action];
      const auto &child_state = game.states()[edge.child];
      const auto payment = child_state.committed_total[node.actor].units() -
                           state.committed_total[node.actor].units();
      json += std::string(action > 0U ? ", " : "") + "{\"type\": " + quote(action_type_name(edge.action.type)) +
              ", \"id\": " + quote(labels[action]) +
              ", \"targetCommitmentAnte\": " + number(antes(child_state.committed_this_street[node.actor])) +
              ", \"paymentAnte\": " + number(static_cast<double>(payment) * ante_scale) +
              ", \"requestedBasisPoints\": " + std::to_string(edge.action.requested_basis_points) +
              ", \"target\": " + target_json(edge.child) + "}";
    }
    json += "]}";
    result.stats.maximum_depth =
        std::max<std::uint32_t>(result.stats.maximum_depth, node.depth - nodes[entry].depth);
  }
  json += "\n  ],\n  \"stats\": {\"representedNodes\": " + std::to_string(result.stats.represented_nodes) +
          ", \"decisionNodes\": " + std::to_string(result.stats.decision_nodes) +
          ", \"actionEdges\": " + std::to_string(result.stats.action_edges) +
          ", \"chanceFrontiers\": " + std::to_string(result.stats.chance_frontiers) +
          ", \"terminalFolds\": " + std::to_string(result.stats.terminal_folds) +
          ", \"terminalShowdowns\": " + std::to_string(result.stats.terminal_showdowns) +
          ", \"terminalAllInRunouts\": " + std::to_string(result.stats.terminal_all_in_runouts) +
          ", \"maximumDepth\": " + std::to_string(result.stats.maximum_depth) +
          ", \"maximumRaiseCount\": " + std::to_string(result.stats.maximum_raise_count) + "}\n}\n";
  result.json = std::move(json);
  return result;
}

} // namespace gtosd::preflop_blueprint
