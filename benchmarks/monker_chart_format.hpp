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

// The chart of one preflop decision node: its file (`position`/`name`,
// `relative` = both joined with '/'), the token of every edge in edge order
// and the edges in the column order of the file.
struct ChartNode {
  std::uint32_t node{0U};
  std::string position;
  std::string name;
  std::string relative;
  std::vector<std::string> tokens;
  std::vector<std::size_t> columns;
};

// Every preflop decision node with its chart, in the order write_charts
// writes them (depth first, last edge first).
inline std::vector<ChartNode> chart_nodes(const preflop_blueprint::CompiledGame &game) {
  const auto &config = game.config();
  std::vector<ChartNode> charts;
  std::vector<std::pair<std::uint32_t, std::string>> stack{{game.root(), std::string{}}};
  while (!stack.empty()) {
    const auto [node_id, prefix] = stack.back();
    stack.pop_back();
    const auto &node = game.nodes()[node_id];
    if (node.kind != preflop_blueprint::NodeKind::Decision || node.street != Street::Preflop) {
      continue;
    }
    ChartNode chart;
    chart.node = node_id;
    chart.position = config.positions.at(node.actor);
    const auto edges = game.edges_of(node_id);
    auto &tokens = chart.tokens;
    for (const auto &edge : edges) {
      tokens.push_back(action_token(game, node.actor, edge));
      stack.emplace_back(edge.child, prefix + chart.position + "_" + tokens.back() + "_");
    }
    auto &order = chart.columns;
    order.resize(tokens.size());
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
    chart.name = prefix + chart.position + "_strategy.txt";
    chart.relative = (std::filesystem::path(chart.position) / chart.name).generic_string();
    charts.push_back(std::move(chart));
  }
  return charts;
}

// Writes one chart per preflop decision node under `directory` and returns
// the relative paths written.
inline std::vector<std::string> write_charts(const preflop_blueprint::CompiledGame &game,
                                             const std::vector<std::string> &labels,
                                             const ClassStrategy &strategy,
                                             const std::filesystem::path &directory) {
  std::vector<std::string> written;
  for (const auto &chart : chart_nodes(game)) {
    const auto folder = directory / chart.position;
    std::filesystem::create_directories(folder);
    std::ofstream output(folder / chart.name, std::ios::binary);
    if (!output) {
      throw std::runtime_error("cannot write " + (folder / chart.name).string());
    }
    output << "Combination";
    for (const auto index : chart.columns) {
      output << '\t' << chart.tokens[index];
    }
    output << "\tTotal\n" << std::fixed << std::setprecision(3);
    for (const auto &label : labels) {
      const auto frequencies = strategy(chart.node, label);
      double total = 0.0;
      output << label;
      for (const auto index : chart.columns) {
        const double value = frequencies ? (*frequencies)[index] : 0.0;
        output << '\t' << value;
        total += value;
      }
      output << '\t' << total << '\n';
    }
    if (!output) {
      throw std::runtime_error("cannot write " + (folder / chart.name).string());
    }
    written.push_back(chart.relative);
  }
  return written;
}

// A chart file read back: the action tokens of its columns and, per hand
// class label, the frequencies in column order (the Total column dropped).
struct ChartFile {
  std::vector<std::string> columns;
  std::map<std::string, std::vector<double>> rows;
};

// Reads a chart in the MonkerSolver text format (tab separated, LF or CRLF
// line ends, first column the hand class, optional last column Total).
inline ChartFile read_chart(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot open " + path.string());
  }
  const auto split = [](const std::string &line) {
    std::vector<std::string> fields;
    std::size_t start = 0U;
    while (true) {
      const auto tab = line.find('\t', start);
      fields.push_back(line.substr(start, tab == std::string::npos ? std::string::npos
                                                                   : tab - start));
      if (tab == std::string::npos) {
        return fields;
      }
      start = tab + 1U;
    }
  };
  ChartFile chart;
  bool header = true;
  bool with_total = false;
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty()) {
      continue;
    }
    const auto fields = split(line);
    if (header) {
      if (fields.size() < 2U) {
        throw std::runtime_error("chart header without actions: " + path.string());
      }
      with_total = fields.back() == "Total";
      chart.columns.assign(fields.begin() + 1, fields.end() - (with_total ? 1 : 0));
      header = false;
      continue;
    }
    if (fields.size() != chart.columns.size() + 1U + (with_total ? 1U : 0U)) {
      throw std::runtime_error("chart row with " + std::to_string(fields.size()) +
                               " fields in " + path.string());
    }
    std::vector<double> values;
    for (std::size_t column = 0; column < chart.columns.size(); ++column) {
      const auto &text = fields[column + 1U];
      std::size_t consumed = 0U;
      const double value = std::stod(text, &consumed);
      if (consumed != text.size() || !(value >= 0.0)) {
        throw std::runtime_error("invalid frequency '" + text + "' in " + path.string());
      }
      values.push_back(value);
    }
    if (!chart.rows.emplace(fields[0], std::move(values)).second) {
      throw std::runtime_error("hand class " + fields[0] + " twice in " + path.string());
    }
  }
  if (header) {
    throw std::runtime_error("empty chart " + path.string());
  }
  return chart;
}

// Row of a class at a chart node in edge order, normalized by its total, or
// nullopt for a row outside the acting range (total below one half: an
// all-zero row). The columns are matched to the edges by token, never by
// position.
inline std::optional<std::vector<double>> chart_row(const ChartNode &node, const ChartFile &chart,
                                                    const std::string &label,
                                                    const std::filesystem::path &path) {
  std::vector<std::size_t> column_of(node.tokens.size(), chart.columns.size());
  bool same = chart.columns.size() == node.tokens.size();
  for (std::size_t edge = 0; same && edge < node.tokens.size(); ++edge) {
    const auto found = std::find(chart.columns.begin(), chart.columns.end(), node.tokens[edge]);
    same = found != chart.columns.end();
    if (same) {
      column_of[edge] = static_cast<std::size_t>(found - chart.columns.begin());
    }
  }
  if (!same) {
    std::string ours;
    std::string theirs;
    for (const auto &token : node.tokens) {
      ours += (ours.empty() ? "" : ",") + token;
    }
    for (const auto &token : chart.columns) {
      theirs += (theirs.empty() ? "" : ",") + token;
    }
    throw std::runtime_error("actions of " + path.string() + " (" + theirs +
                             ") differ from the game (" + ours + ")");
  }
  const auto row = chart.rows.find(label);
  if (row == chart.rows.end()) {
    throw std::runtime_error("hand class " + label + " missing in " + path.string());
  }
  double total = 0.0;
  for (const auto value : row->second) {
    total += value;
  }
  if (total < 0.5) {
    return std::nullopt;
  }
  std::vector<double> frequencies(node.tokens.size(), 0.0);
  for (std::size_t edge = 0; edge < node.tokens.size(); ++edge) {
    frequencies[edge] = row->second[column_of[edge]] / total;
  }
  return frequencies;
}

// Normalized strategy of a preflop node for a hand class (the preflop rows of
// a policy or of a trainer), in edge order.
using RowStrategy =
    std::function<std::vector<double>(std::uint32_t node, std::uint8_t hand_class)>;

// Heads-up charts from preflop rows: the own reach of each seat per node and
// hand class follows the rows, and a class whose reach at the node is below
// out_of_range_reach is written as an all-zero row (outside the range).
inline std::vector<std::string> write_row_charts(const preflop_blueprint::CompiledGame &game,
                                                 const RowStrategy &row,
                                                 const std::filesystem::path &directory,
                                                 const double out_of_range_reach = 5e-4) {
  if (game.config().player_count != 2U) {
    throw std::runtime_error("the chart export is heads-up only");
  }
  const auto classes = hand_classes();
  std::map<std::string, std::uint8_t> class_of_label;
  for (const auto &[hand_class, label] : classes.label_by_class) {
    class_of_label[label] = hand_class;
  }
  std::vector<std::string> labels;
  for (const auto &entry : classes.combos_by_label) {
    labels.push_back(entry.first);
  }
  const auto class_count = classes.label_by_class.size();
  std::map<std::uint32_t, std::array<std::vector<double>, 2>> reach;
  std::function<void(std::uint32_t, const std::array<std::vector<double>, 2> &)> forward =
      [&](const std::uint32_t node_id, const std::array<std::vector<double>, 2> &node_reach) {
        const auto &node = game.nodes()[node_id];
        if (node.kind != preflop_blueprint::NodeKind::Decision || node.street != Street::Preflop) {
          return;
        }
        reach[node_id] = node_reach;
        std::vector<std::vector<double>> strategies(class_count);
        for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
          strategies[hand_class] = row(node_id, static_cast<std::uint8_t>(hand_class));
        }
        const auto edges = game.edges_of(node_id);
        for (std::size_t action = 0; action < edges.size(); ++action) {
          auto next = node_reach;
          for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
            next[node.actor][hand_class] *= strategies[hand_class][action];
          }
          forward(edges[action].child, next);
        }
      };
  forward(game.root(), {std::vector<double>(class_count, 1.0),
                        std::vector<double>(class_count, 1.0)});
  const ClassStrategy strategy =
      [&](const std::uint32_t node, const std::string &label) -> std::optional<std::vector<double>> {
    const auto hand_class = class_of_label.at(label);
    const auto actor = game.nodes()[node].actor;
    if (reach.at(node)[actor][hand_class] < out_of_range_reach) {
      return std::nullopt;
    }
    return row(node, hand_class);
  };
  return write_charts(game, labels, strategy, directory);
}

} // namespace gtosd::monker_charts
