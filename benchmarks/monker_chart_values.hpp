#pragma once

// A preflop chart set played inside our game (MonkerSolver charts in our
// step-2 game): the EV one player loses when its preflop decisions follow the
// charts, one row per hand class for every combo of the class, while
// everything else stays our average strategy (its own postflop play, the
// opponent's preflop and postflop play).
//
// The inputs are the action values of the physical evaluator
// (PreflopActionValues): Q(m, a, h), the counterfactual value of action a at
// the hero's preflop node m for combo h, with the opponent reach and chance
// inside and our own play after the action. With s our rows, t the chart rows
// and pi_t(m, c) the product of the chart frequencies of the hero's own
// earlier actions on the path to m, the performance difference identity gives
// exactly
//
//   EV(s) - EV(t) = (1/N) sum_h sum_m pi_t(m, c(h)) sum_a (s - t)(m, c(h), a) Q(m, a, h),
//
// N the number of hero combos (630, full ranges). Each (node, class) term is
// the part of the loss spent there; the same loss by recursion over the
// hero's nodes (D(m) = local term + sum_a t(a) D(next nodes of a)) is the
// self-check. The entry values do not depend on the hero's own preflop
// policy, so one evaluation serves every chart set of both players.
#include "monker_chart_format.hpp"

#include "gtosd/card_abstraction/card_abstraction.hpp"
#include "gtosd/card_abstraction/showdown_counts.hpp"
#include "gtosd/preflop_blueprint/best_response.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <vector>

namespace gtosd::monker_charts {

inline constexpr std::size_t class_count = card_abstraction::preflop_hand_classes;
inline constexpr std::size_t hero_combos = card_abstraction::combo_count;

// [slot][class][action]: a preflop strategy of the hero, slots as in
// PreflopActionValues::nodes, actions in edge order.
using PreflopStrategy = std::vector<std::vector<std::vector<double>>>;

// Combos of every hand class (6 pairs, 4 suited, 12 offsuit).
inline std::vector<double> class_combos() {
  std::vector<double> counts(class_count, 0.0);
  const auto &table = card_abstraction::combo_table();
  for (std::size_t combo = 0; combo < hero_combos; ++combo) {
    counts[table.hand_class[combo]] += 1.0;
  }
  return counts;
}

// The hero's preflop decision nodes as a tree of slots.
struct HeroTree {
  std::vector<std::uint32_t> nodes;
  // [slot][action]: next preflop decisions of the hero below the action,
  // through the opponent's decisions.
  std::vector<std::vector<std::vector<std::size_t>>> next;
  // Slots reached before any decision of the hero.
  std::vector<std::size_t> top;
  // Every slot, parents before children.
  std::vector<std::size_t> order;
  // Parent slot (the slot count for a top slot) and its action.
  std::vector<std::size_t> parent;
  std::vector<std::size_t> parent_action;
};

inline HeroTree hero_tree(const preflop_blueprint::CompiledGame &game,
                          const std::vector<std::uint32_t> &nodes, const std::uint8_t hero) {
  HeroTree tree;
  tree.nodes = nodes;
  const auto slots = nodes.size();
  std::map<std::uint32_t, std::size_t> slot_of;
  for (std::size_t slot = 0; slot < slots; ++slot) {
    slot_of[nodes[slot]] = slot;
  }
  std::function<void(std::uint32_t, std::vector<std::size_t> &)> below =
      [&](const std::uint32_t node_id, std::vector<std::size_t> &out) {
        const auto &node = game.nodes()[node_id];
        if (node.kind != preflop_blueprint::NodeKind::Decision || node.street != Street::Preflop) {
          return;
        }
        if (node.actor == hero) {
          const auto found = slot_of.find(node_id);
          if (found == slot_of.end()) {
            throw std::runtime_error("preflop node of the hero without a slot");
          }
          out.push_back(found->second);
          return;
        }
        for (const auto &edge : game.edges_of(node_id)) {
          below(edge.child, out);
        }
      };
  below(game.root(), tree.top);
  tree.next.resize(slots);
  tree.parent.assign(slots, slots);
  tree.parent_action.assign(slots, 0U);
  for (std::size_t slot = 0; slot < slots; ++slot) {
    const auto edges = game.edges_of(nodes[slot]);
    tree.next[slot].resize(edges.size());
    for (std::size_t action = 0; action < edges.size(); ++action) {
      below(edges[action].child, tree.next[slot][action]);
      for (const auto child : tree.next[slot][action]) {
        tree.parent[child] = slot;
        tree.parent_action[child] = action;
      }
    }
  }
  tree.order = tree.top;
  for (std::size_t index = 0; index < tree.order.size(); ++index) {
    const auto slot = tree.order[index];
    for (const auto &children : tree.next[slot]) {
      tree.order.insert(tree.order.end(), children.begin(), children.end());
    }
  }
  if (tree.order.size() != slots) {
    throw std::runtime_error("preflop nodes of the hero outside its tree");
  }
  return tree;
}

// [slot][class]: product of the strategy's frequencies of the hero's own
// earlier actions on the path to the node.
inline std::vector<std::vector<double>> own_reach(const HeroTree &tree,
                                                  const PreflopStrategy &strategy) {
  const auto slots = tree.nodes.size();
  std::vector<std::vector<double>> reach(slots, std::vector<double>(class_count, 1.0));
  for (const auto slot : tree.order) {
    const auto parent = tree.parent[slot];
    if (parent == slots) {
      continue;
    }
    for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
      reach[slot][hand_class] =
          reach[parent][hand_class] * strategy[parent][hand_class][tree.parent_action[slot]];
    }
  }
  return reach;
}

// Where the row of a class at a node comes from: the chart; our row because
// the chart has none (an all-zero row) and the class never gets there under
// the charts (no effect); or our row although the class gets there under the
// charts (a fallback, reported).
enum class RowSource : std::uint8_t { Chart, OutsideRange, Fallback };

inline const char *row_source_name(const RowSource source) noexcept {
  switch (source) {
  case RowSource::Chart:
    return "chart";
  case RowSource::OutsideRange:
    return "outside_range";
  case RowSource::Fallback:
    return "fallback";
  }
  return "unknown";
}

struct ChartStrategy {
  PreflopStrategy rows;
  std::vector<std::vector<RowSource>> source;
  std::vector<std::vector<double>> reach;
  // Combos (class combos times own reach) that reach a node under the charts
  // where the chart has no row.
  double fallback_reach_combos{0.0};
};

// Chart row of a class at a slot, normalized, in edge order; nullopt when the
// chart has no row for the class there.
using ChartLookup =
    std::function<std::optional<std::vector<double>>(std::size_t slot, std::size_t hand_class)>;

inline ChartStrategy chart_strategy(const HeroTree &tree, const PreflopStrategy &ours,
                                    const ChartLookup &chart) {
  const auto slots = tree.nodes.size();
  const auto combos = class_combos();
  ChartStrategy result;
  result.rows = ours;
  result.source.assign(slots, std::vector<RowSource>(class_count, RowSource::Chart));
  result.reach.assign(slots, std::vector<double>(class_count, 1.0));
  for (const auto slot : tree.order) {
    const auto parent = tree.parent[slot];
    for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
      if (parent != slots) {
        result.reach[slot][hand_class] =
            result.reach[parent][hand_class] *
            result.rows[parent][hand_class][tree.parent_action[slot]];
      }
      const auto row = chart(slot, hand_class);
      if (row) {
        if (row->size() != ours[slot][hand_class].size()) {
          throw std::runtime_error("chart row with another action count");
        }
        result.rows[slot][hand_class] = *row;
        continue;
      }
      const double reach = result.reach[slot][hand_class];
      result.source[slot][hand_class] = reach > 0.0 ? RowSource::Fallback : RowSource::OutsideRange;
      if (reach > 0.0) {
        result.fallback_reach_combos += combos[hand_class] * reach;
      }
    }
  }
  return result;
}

struct ClassTerm {
  double reach_ours{0.0};
  double reach_charts{0.0};
  // Mean over the combos of the class of P(the opponent reaches the node).
  double opponent_reach{0.0};
  // Antes per hand of the class that reaches the node: sum_a (s - t) class EV.
  double local_loss{0.0};
  // Best action's class EV minus our row's.
  double regret_ours{0.0};
  // Share of the loss in antes per hand of the hero (the sum over nodes and
  // classes is the loss).
  double loss{0.0};
  std::size_t best_action{0U};
  RowSource source{RowSource::Chart};
};

struct NodeTerm {
  double loss{0.0};
  double reach_combos_ours{0.0};
  double reach_combos_charts{0.0};
  std::vector<ClassTerm> classes;
};

struct ChartLoss {
  double loss{0.0};
  double loss_recursive{0.0};
  double positive{0.0};
  double negative{0.0};
  double fallback_reach_combos{0.0};
  std::vector<NodeTerm> nodes;
};

inline ChartLoss chart_loss(const HeroTree &tree,
                            const preflop_blueprint::PreflopActionValues &values,
                            const PreflopStrategy &ours, const ChartStrategy &charts) {
  const auto &table = card_abstraction::combo_table();
  const auto slots = tree.nodes.size();
  const auto combos = class_combos();
  const double hands = static_cast<double>(hero_combos);
  const auto reach_ours = own_reach(tree, ours);
  std::vector<std::vector<double>> local(slots, std::vector<double>(hero_combos, 0.0));
  ChartLoss result;
  result.nodes.resize(slots);
  result.fallback_reach_combos = charts.fallback_reach_combos;
  for (std::size_t slot = 0; slot < slots; ++slot) {
    const auto &action_values = values.combo_values[slot];
    const auto actions = action_values.size();
    auto &term = result.nodes[slot];
    term.classes.resize(class_count);
    for (std::size_t combo = 0; combo < hero_combos; ++combo) {
      const auto hand_class = table.hand_class[combo];
      for (std::size_t action = 0; action < actions; ++action) {
        local[slot][combo] +=
            (ours[slot][hand_class][action] - charts.rows[slot][hand_class][action]) *
            action_values[action][combo];
      }
      term.classes[hand_class].loss += charts.reach[slot][hand_class] * local[slot][combo] / hands;
    }
    for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
      auto &entry = term.classes[hand_class];
      entry.reach_ours = reach_ours[slot][hand_class];
      entry.reach_charts = charts.reach[slot][hand_class];
      entry.source = charts.source[slot][hand_class];
      entry.opponent_reach = values.class_weight[slot][hand_class] / combos[hand_class];
      double ours_ev = 0.0;
      double charts_ev = 0.0;
      double best = -std::numeric_limits<double>::infinity();
      for (std::size_t action = 0; action < actions; ++action) {
        const double ev = values.class_ev[slot][action][hand_class];
        ours_ev += ours[slot][hand_class][action] * ev;
        charts_ev += charts.rows[slot][hand_class][action] * ev;
        if (ev > best) {
          best = ev;
          entry.best_action = action;
        }
      }
      entry.local_loss = ours_ev - charts_ev;
      entry.regret_ours = best - ours_ev;
      term.reach_combos_ours += combos[hand_class] * entry.reach_ours;
      term.reach_combos_charts += combos[hand_class] * entry.reach_charts;
      term.loss += entry.loss;
      (entry.loss > 0.0 ? result.positive : result.negative) += entry.loss;
    }
    result.loss += term.loss;
  }
  // D(m, h) = local term + sum_a t(a) sum over the next nodes of a of D.
  std::vector<std::vector<double>> difference(slots, std::vector<double>(hero_combos, 0.0));
  for (auto position = tree.order.rbegin(); position != tree.order.rend(); ++position) {
    const auto slot = *position;
    for (std::size_t combo = 0; combo < hero_combos; ++combo) {
      const auto hand_class = table.hand_class[combo];
      double value = local[slot][combo];
      for (std::size_t action = 0; action < tree.next[slot].size(); ++action) {
        for (const auto child : tree.next[slot][action]) {
          value += charts.rows[slot][hand_class][action] * difference[child][combo];
        }
      }
      difference[slot][combo] = value;
    }
  }
  for (std::size_t combo = 0; combo < hero_combos; ++combo) {
    for (const auto slot : tree.top) {
      result.loss_recursive += difference[slot][combo] / hands;
    }
  }
  return result;
}

// Best preflop response to the same values: the hero maximizes at its
// preflop nodes and plays our average strategy after them. Per class (one
// action for every combo of the class, as a chart) and per combo (the
// evaluator's best_response_preflop, whose gain it must reproduce). The two
// agree on an exact pass, where suit symmetry makes the combos of a class
// equivalent.
struct PreflopResponse {
  double gain_per_class{0.0};
  double gain_per_combo{0.0};
  // [slot][class]: action of the per-class response.
  std::vector<std::vector<std::size_t>> choice;
};

inline PreflopResponse preflop_response(const HeroTree &tree,
                                        const preflop_blueprint::PreflopActionValues &values,
                                        const PreflopStrategy &ours) {
  const auto &table = card_abstraction::combo_table();
  const auto slots = tree.nodes.size();
  const double hands = static_cast<double>(hero_combos);
  PreflopResponse result;
  result.choice.assign(slots, std::vector<std::size_t>(class_count, 0U));
  for (const bool per_class : {true, false}) {
    std::vector<std::vector<double>> gain(slots, std::vector<double>(hero_combos, 0.0));
    for (auto position = tree.order.rbegin(); position != tree.order.rend(); ++position) {
      const auto slot = *position;
      const auto &action_values = values.combo_values[slot];
      const auto actions = action_values.size();
      std::vector<std::vector<double>> response(actions, std::vector<double>(hero_combos, 0.0));
      std::vector<double> average(hero_combos, 0.0);
      for (std::size_t combo = 0; combo < hero_combos; ++combo) {
        const auto hand_class = table.hand_class[combo];
        for (std::size_t action = 0; action < actions; ++action) {
          double value = action_values[action][combo];
          for (const auto child : tree.next[slot][action]) {
            value += gain[child][combo];
          }
          response[action][combo] = value;
          average[combo] += ours[slot][hand_class][action] * action_values[action][combo];
        }
      }
      if (per_class) {
        std::vector<std::vector<double>> sums(actions, std::vector<double>(class_count, 0.0));
        for (std::size_t combo = 0; combo < hero_combos; ++combo) {
          for (std::size_t action = 0; action < actions; ++action) {
            sums[action][table.hand_class[combo]] += response[action][combo];
          }
        }
        for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
          std::size_t best = 0U;
          for (std::size_t action = 1; action < actions; ++action) {
            if (sums[action][hand_class] > sums[best][hand_class]) {
              best = action;
            }
          }
          result.choice[slot][hand_class] = best;
        }
      }
      for (std::size_t combo = 0; combo < hero_combos; ++combo) {
        std::size_t best = 0U;
        if (per_class) {
          best = result.choice[slot][table.hand_class[combo]];
        } else {
          for (std::size_t action = 1; action < actions; ++action) {
            if (response[action][combo] > response[best][combo]) {
              best = action;
            }
          }
        }
        gain[slot][combo] = response[best][combo] - average[combo];
      }
    }
    double total = 0.0;
    for (std::size_t combo = 0; combo < hero_combos; ++combo) {
      for (const auto slot : tree.top) {
        total += gain[slot][combo] / hands;
      }
    }
    (per_class ? result.gain_per_class : result.gain_per_combo) = total;
  }
  return result;
}

// [entry][class]: own reach of the hero at every postflop entry
// (game.postflop_entries() order) under a strategy.
inline std::vector<std::vector<double>> entry_reach(const preflop_blueprint::CompiledGame &game,
                                                    const HeroTree &tree, const std::uint8_t hero,
                                                    const PreflopStrategy &strategy) {
  const auto entries = game.postflop_entries();
  std::map<std::uint32_t, std::size_t> entry_of;
  for (std::size_t index = 0; index < entries.size(); ++index) {
    entry_of[entries[index]] = index;
  }
  std::map<std::uint32_t, std::size_t> slot_of;
  for (std::size_t slot = 0; slot < tree.nodes.size(); ++slot) {
    slot_of[tree.nodes[slot]] = slot;
  }
  std::vector<std::vector<double>> result(entries.size(), std::vector<double>(class_count, 0.0));
  std::function<void(std::uint32_t, const std::vector<double> &)> descend =
      [&](const std::uint32_t node_id, const std::vector<double> &reach) {
        const auto &node = game.nodes()[node_id];
        if (node.kind != preflop_blueprint::NodeKind::Decision) {
          const auto found = entry_of.find(node_id);
          if (found != entry_of.end()) {
            result[found->second] = reach;
          }
          return;
        }
        const auto edges = game.edges_of(node_id);
        if (node.actor != hero) {
          for (const auto &edge : edges) {
            descend(edge.child, reach);
          }
          return;
        }
        const auto slot = slot_of.at(node_id);
        std::vector<double> child(class_count);
        for (std::size_t action = 0; action < edges.size(); ++action) {
          for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
            child[hand_class] = reach[hand_class] * strategy[slot][hand_class][action];
          }
          descend(edges[action].child, child);
        }
      };
  descend(game.root(), std::vector<double>(class_count, 1.0));
  return result;
}

// Per-group series of the entry part of an EV (the standard-error series of
// BestResponseEvaluator::aggregate): for group g, G/N times the sum over the
// entries and combos of reach(entry, class) * value_g(entry, combo) *
// weight_g / W(combo), W the total weight of the groups compatible with the
// combo. With `reach` a difference of two own reaches it is the series of
// the loss (the preflop terminals are the same in every group). Groups must
// stand for their physical flop only (identity image), as in a sampled pass;
// otherwise the series is empty.
inline std::vector<double> group_series(const std::vector<const preflop_blueprint::FlopValues *> &flops,
                                        const std::uint8_t hero,
                                        const std::vector<std::vector<double>> &reach) {
  const auto &table = card_abstraction::combo_table();
  std::vector<double> total_weight(hero_combos, 0.0);
  for (const auto *values : flops) {
    if (values->images.size() != 1U ||
        values->images.front() != card_abstraction::identity_permutation) {
      return {};
    }
    for (std::size_t combo = 0; combo < hero_combos; ++combo) {
      if (values->compatible[combo] != 0U) {
        total_weight[combo] += values->weight;
      }
    }
  }
  const double groups = static_cast<double>(flops.size());
  const double hero_scale = 1.0 / static_cast<double>(hero_combos);
  std::vector<double> series;
  series.reserve(flops.size());
  for (const auto *values : flops) {
    double sum = 0.0;
    const auto &entries = values->entry_values[hero][preflop_blueprint::average_mode];
    for (std::size_t entry = 0; entry < entries.size(); ++entry) {
      for (std::size_t combo = 0; combo < hero_combos; ++combo) {
        if (values->compatible[combo] == 0U || total_weight[combo] <= 0.0) {
          continue;
        }
        sum += 1.0 * reach[entry][table.hand_class[combo]] * entries[entry][combo] *
               values->weight / total_weight[combo];
      }
    }
    series.push_back(groups * sum * hero_scale);
  }
  return series;
}

inline double standard_error(const std::vector<double> &series) {
  if (series.size() < 2U) {
    return 0.0;
  }
  double mean = 0.0;
  for (const auto value : series) {
    mean += value;
  }
  mean /= static_cast<double>(series.size());
  double variance = 0.0;
  for (const auto value : series) {
    variance += (value - mean) * (value - mean);
  }
  variance /= static_cast<double>(series.size() - 1U);
  return std::sqrt(variance / static_cast<double>(series.size()));
}

} // namespace gtosd::monker_charts
