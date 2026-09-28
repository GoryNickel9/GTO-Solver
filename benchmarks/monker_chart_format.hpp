#pragma once

// Preflop charts in the MonkerSolver text format, shared by the research tools
// that compare our solutions with MonkerSolver charts: one tab-separated file
// per preflop decision node, named after the line that reaches it (for
// example CO_Call_BTN_5.0ante_CO_strategy.txt in the folder of the acting
// position), one row per hand class in lexicographic order, the frequency of
// every action and the total. A class outside the acting range at the node is
// an all-zero row, as MonkerSolver writes it.
#include "gtosd/card_abstraction/showdown_counts.hpp"
#include "gtosd/preflop_blueprint/compiled_game.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gtosd::monker_charts {

// Hand class label of a combo: "AA", "AKs", "AKo" (short deck ranks 6..A).
inline std::string class_label(const std::array<std::uint8_t, 2> &cards) {
  static constexpr std::string_view rank_chars = "6789TJQKA";
  const auto first_rank = static_cast<std::size_t>(cards[0] / 4U);
  const auto second_rank = static_cast<std::size_t>(cards[1] / 4U);
  const auto high = std::max(first_rank, second_rank);
  const auto low = std::min(first_rank, second_rank);
  std::string label{rank_chars[high], rank_chars[low]};
  if (high != low) {
    label += (cards[0] % 4U) == (cards[1] % 4U) ? 's' : 'o';
  }
  return label;
}

// Combos of every hand class label, and the label of every preflop class id.
struct HandClasses {
  std::map<std::string, std::vector<std::size_t>> combos_by_label;
  std::map<std::uint8_t, std::string> label_by_class;
};

inline HandClasses hand_classes() {
  const auto &table = card_abstraction::combo_table();
  HandClasses result;
  for (std::size_t combo = 0; combo < table.cards.size(); ++combo) {
    const auto label = class_label(table.cards[combo]);
    result.combos_by_label[label].push_back(combo);
    result.label_by_class[table.hand_class[combo]] = label;
  }
  return result;
}

inline std::string format_antes(const std::int64_t units) {
  std::ostringstream text;
  text << std::fixed << std::setprecision(1)
       << static_cast<double>(units) / static_cast<double>(Money::units_per_ante) << "ante";
  return text.str();
}

// MonkerSolver action token: Fold, Check, Call, AllIn, or the total street
// commitment of a bet or raise, e.g. "5.0ante".
inline std::string action_token(const preflop_blueprint::CompiledGame &game,
                                const std::uint8_t actor,
                                const preflop_blueprint::CompiledEdge &edge) {
  switch (edge.action.type) {
  case ActionType::Fold:
    return "Fold";
  case ActionType::Check:
    return "Check";
  case ActionType::Call:
    return "Call";
  case ActionType::AllIn:
    return "AllIn";
  case ActionType::Bet:
  case ActionType::Raise:
    return format_antes(game.states()[edge.child].committed_this_street[actor].units());
  }
  return "Unknown";
}

// Column order: all-in, raise sizes in increasing order, call, check, fold.
inline int column_rank(const std::string &token) {
  if (token == "AllIn") {
    return 0;
  }
  if (token == "Call") {
    return 2;
  }
  if (token == "Check") {
    return 3;
  }
  if (token == "Fold") {
    return 4;
  }
  return 1;
}

// Strategy of a hand class at a preflop decision node, in edge order, or
// nullopt when the class is outside the acting range there.
using ClassStrategy = std::function<std::optional<std::vector<double>>(
    std::uint32_t node, const std::string &label)>;

// Writes one chart per preflop decision node under `directory` and returns
// the relative paths written.
inline std::vector<std::string> write_charts(const preflop_blueprint::CompiledGame &game,
                                             const std::vector<std::string> &labels,
                                             const ClassStrategy &strategy,
                                             const std::filesystem::path &directory) {
  const auto &config = game.config();
  std::vector<std::string> written;
  std::vector<std::pair<std::uint32_t, std::string>> stack{{game.root(), std::string{}}};
  while (!stack.empty()) {
    const auto [node_id, prefix] = stack.back();
    stack.pop_back();
    const auto &node = game.nodes()[node_id];
    if (node.kind != preflop_blueprint::NodeKind::Decision || node.street != Street::Preflop) {
      continue;
    }
    const auto &position = config.positions.at(node.actor);
    const auto edges = game.edges_of(node_id);
    std::vector<std::string> tokens;
    for (const auto &edge : edges) {
      tokens.push_back(action_token(game, node.actor, edge));
      stack.emplace_back(edge.child, prefix + position + "_" + tokens.back() + "_");
    }
    std::vector<std::size_t> order(tokens.size());
    for (std::size_t index = 0; index < order.size(); ++index) {
      order[index] = index;
    }
    std::stable_sort(order.begin(), order.end(),
                     [&](const std::size_t left, const std::size_t right) {
                       const auto left_rank = column_rank(tokens[left]);
                       const auto right_rank = column_rank(tokens[right]);
                       if (left_rank != right_rank) {
                         return left_rank < right_rank;
                       }
                       return game.states()[edges[left].child]
                                  .committed_this_street[node.actor]
                                  .units() < game.states()[edges[right].child]
                                                 .committed_this_street[node.actor]
                                                 .units();
                     });
    const auto folder = directory / position;
    std::filesystem::create_directories(folder);
    const auto name = prefix + position + "_strategy.txt";
    std::ofstream output(folder / name, std::ios::binary);
    if (!output) {
      throw std::runtime_error("cannot write " + (folder / name).string());
    }
    output << "Combination";
    for (const auto index : order) {
      output << '\t' << tokens[index];
    }
    output << "\tTotal\n" << std::fixed << std::setprecision(3);
    for (const auto &label : labels) {
      const auto frequencies = strategy(node_id, label);
      double total = 0.0;
      output << label;
      for (const auto index : order) {
        const double value = frequencies ? (*frequencies)[index] : 0.0;
        output << '\t' << value;
        total += value;
      }
      output << '\t' << total << '\n';
    }
    if (!output) {
      throw std::runtime_error("cannot write " + (folder / name).string());
    }
    written.push_back((std::filesystem::path(position) / name).generic_string());
  }
  return written;
}

} // namespace gtosd::monker_charts
