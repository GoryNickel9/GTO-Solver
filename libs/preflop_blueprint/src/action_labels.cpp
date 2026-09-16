#include "gtosd/preflop_blueprint/action_labels.hpp"

#include <algorithm>
#include <cstdint>
#include <string>

namespace gtosd::preflop_blueprint {
namespace {

std::string antes_text(const Money amount) {
  const auto units = amount.units();
  const auto whole = units / Money::units_per_ante;
  auto fraction = units % Money::units_per_ante;
  std::string text = std::to_string(whole);
  if (fraction != 0) {
    // Up to four decimals, trailing zeros removed, underscore separator.
    std::string digits = std::to_string(fraction);
    while (digits.size() < 4U) {
      digits.insert(digits.begin(), '0');
    }
    while (!digits.empty() && digits.back() == '0') {
      digits.pop_back();
    }
    text += "_" + digits;
  }
  return text;
}

} // namespace

std::string action_label(const Action &action) {
  switch (action.type) {
  case ActionType::Fold:
    return "fold";
  case ActionType::Check:
    return "check";
  case ActionType::Call:
    return "call";
  case ActionType::AllIn:
    return "all_in";
  case ActionType::Bet:
    return "bet_" + antes_text(action.amount);
  case ActionType::Raise:
    return "raise_" + antes_text(action.amount);
  }
  return "unknown";
}

std::vector<std::string> edge_labels(const CompiledGame &game, const std::uint32_t node) {
  const auto edges = game.edges_of(node);
  std::vector<std::string> labels;
  labels.reserve(edges.size());
  for (const auto &edge : edges) {
    labels.push_back(action_label(edge.action));
  }
  for (std::size_t index = 0; index < labels.size(); ++index) {
    const auto duplicates = std::count(labels.begin(), labels.end(), labels[index]);
    if (duplicates > 1) {
      labels[index] += "_bp" + std::to_string(edges[index].action.requested_basis_points);
    }
  }
  return labels;
}

std::vector<std::uint32_t> path_edges(const CompiledGame &game, const std::uint32_t node) {
  std::vector<std::uint32_t> reversed;
  auto current = node;
  while (game.nodes()[current].parent != no_node) {
    const auto parent = game.nodes()[current].parent;
    const auto edges = game.edges_of(parent);
    std::uint32_t found = 0U;
    for (std::uint32_t index = 0; index < edges.size(); ++index) {
      if (edges[index].child == current) {
        found = index;
        break;
      }
    }
    reversed.push_back(found);
    current = parent;
  }
  std::reverse(reversed.begin(), reversed.end());
  return reversed;
}

std::string position_name(const CompiledGame &game, const std::uint8_t player) {
  const auto &positions = game.config().positions;
  if (player < positions.size()) {
    return positions[player];
  }
  return "P" + std::to_string(static_cast<unsigned>(player));
}

std::vector<HistoryStep> history_steps(const CompiledGame &game, const std::uint32_t node) {
  std::vector<HistoryStep> steps;
  auto current = game.root();
  for (const auto edge : path_edges(game, node)) {
    const auto &entry = game.nodes()[current];
    if (entry.kind == NodeKind::Decision) {
      steps.push_back({position_name(game, entry.actor), edge_labels(game, current)[edge]});
    }
    current = game.edges_of(current)[edge].child;
  }
  return steps;
}

std::string node_path_id(const CompiledGame &game, const std::uint32_t node) {
  std::string id;
  for (const auto &step : history_steps(game, node)) {
    id += step.player + "_" + step.action + "_";
  }
  const auto &entry = game.nodes()[node];
  if (entry.kind == NodeKind::Decision) {
    id += position_name(game, entry.actor);
  } else {
    id += node_kind_name(entry.kind);
  }
  return id;
}

std::vector<std::uint32_t> preflop_decision_nodes(const CompiledGame &game) {
  std::vector<std::uint32_t> nodes;
  for (const auto &node : game.nodes()) {
    if (node.street == Street::Preflop && node.kind == NodeKind::Decision) {
      nodes.push_back(node.id);
    }
  }
  return nodes;
}

} // namespace gtosd::preflop_blueprint
