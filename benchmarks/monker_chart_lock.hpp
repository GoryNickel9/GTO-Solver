#pragma once

// Preflop lock of the trainer from MonkerSolver-format charts: the chosen chart
// files (named as chart_nodes names them) become fixed rows of the trainer
// (TrainerResources::preflop_lock). Every class row of a locked node is the
// normalized chart row (chart_row) and applies to every combo of the class.
// An all-zero row (outside the range) is left unlocked when the class cannot
// reach the node under the locked earlier rows of its own player
// (chart_strategy's OutsideRange: the row never affects play and the chart
// export writes it as zero again); otherwise the lock is refused, because the
// chart would not say how the class plays there.
#include "monker_chart_format.hpp"
#include "monker_chart_values.hpp"

#include "gtosd/preflop_blueprint/trainer.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace gtosd::monker_charts {

struct ChartLock {
  preflop_blueprint::PreflopLock lock;
  // Relative chart paths locked, in the order given ("all" expanded).
  std::vector<std::string> files;
  std::uint32_t chart_rows{0U};
  std::uint32_t outside_range_rows{0U};
};

// `files` are relative chart paths ("CO/CO_strategy.txt") or "all".
inline ChartLock chart_lock(const preflop_blueprint::CompiledGame &game,
                            const std::filesystem::path &directory,
                            const std::vector<std::string> &files) {
  if (game.config().player_count != 2U) {
    throw std::runtime_error("the preflop chart lock is heads-up only");
  }
  const auto nodes = chart_nodes(game);
  std::map<std::string, std::size_t> chart_of_name;
  for (std::size_t index = 0; index < nodes.size(); ++index) {
    if (!chart_of_name.emplace(nodes[index].relative, index).second) {
      throw std::runtime_error("two preflop nodes share the chart name " + nodes[index].relative);
    }
  }
  ChartLock result;
  std::set<std::size_t> locked;
  for (const auto &file : files) {
    if (file == "all") {
      for (std::size_t index = 0; index < nodes.size(); ++index) {
        if (locked.insert(index).second) {
          result.files.push_back(nodes[index].relative);
        }
      }
      continue;
    }
    const auto found = chart_of_name.find(file);
    if (found == chart_of_name.end()) {
      std::string valid;
      for (const auto &node : nodes) {
        valid += (valid.empty() ? "" : ", ") + node.relative;
      }
      throw std::runtime_error("unknown chart " + file + " (charts of this game: " + valid + ")");
    }
    if (locked.insert(found->second).second) {
      result.files.push_back(file);
    }
  }
  if (locked.empty()) {
    throw std::runtime_error("no chart to lock");
  }
  const auto classes = hand_classes();
  std::vector<std::string> labels(class_count);
  for (const auto &[hand_class, label] : classes.label_by_class) {
    labels.at(hand_class) = label;
  }
  for (std::uint8_t hero = 0; hero < 2U; ++hero) {
    std::vector<std::uint32_t> hero_nodes;
    std::vector<std::size_t> chart_index;
    for (std::size_t index = 0; index < nodes.size(); ++index) {
      if (game.nodes()[nodes[index].node].actor == hero) {
        hero_nodes.push_back(nodes[index].node);
        chart_index.push_back(index);
      }
    }
    if (hero_nodes.empty()) {
      continue;
    }
    const auto tree = hero_tree(game, hero_nodes, hero);
    // Every action with frequency one: an unlocked node keeps any class that
    // reaches it (its trained reach is unknown here).
    PreflopStrategy ones(hero_nodes.size());
    for (std::size_t slot = 0; slot < hero_nodes.size(); ++slot) {
      ones[slot].assign(class_count,
                        std::vector<double>(game.nodes()[hero_nodes[slot]].action_count, 1.0));
    }
    std::vector<std::optional<ChartFile>> read(hero_nodes.size());
    const ChartLookup lookup =
        [&](const std::size_t slot,
            const std::size_t hand_class) -> std::optional<std::vector<double>> {
      const auto index = chart_index[slot];
      if (!locked.contains(index)) {
        return std::nullopt;
      }
      const auto &chart = nodes[index];
      const auto path = directory / chart.position / chart.name;
      if (!read[slot]) {
        read[slot] = read_chart(path);
      }
      return chart_row(chart, *read[slot], labels[hand_class], path);
    };
    const auto strategy = chart_strategy(tree, ones, lookup);
    for (std::size_t slot = 0; slot < hero_nodes.size(); ++slot) {
      const auto index = chart_index[slot];
      if (!locked.contains(index)) {
        continue;
      }
      for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
        switch (strategy.source[slot][hand_class]) {
        case RowSource::Chart:
          result.lock.rows.push_back({hero_nodes[slot], static_cast<std::uint8_t>(hand_class),
                                      strategy.rows[slot][hand_class]});
          ++result.chart_rows;
          break;
        case RowSource::OutsideRange:
          ++result.outside_range_rows;
          break;
        case RowSource::Fallback:
          throw std::runtime_error("chart " + nodes[index].relative + " has no row for " +
                                   labels[hand_class] +
                                   ", which can reach the node: lock the earlier nodes of " +
                                   nodes[index].position + " too");
        }
      }
    }
  }
  return result;
}

} // namespace gtosd::monker_charts
