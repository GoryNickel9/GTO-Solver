#pragma once

// Class-level solver of the checkdown game (step 1 of MonkerSolver's tree
// building: the preflop tree with an empty postflop) for 2 or 3 seats, shared
// by gtosd_preflop_blueprint_checkdown_classes and its tests.
//
// The checkdown game is invariant under the 24 suit permutations: while every
// seat's reach is constant over the combos of a hand class, every combo of a
// class has the same values, so vector DCFR over the 81 preflop classes is
// the combo-level DCFR of preflop_blueprint_checkdown.cpp up to floating-point
// rounding, not an abstraction. The reach of a class is the reach of each of
// its combos, and a class's values are those of its representative combo
// (the lowest combo id, as in the three-player class table).
//
// Terminals. The values of a seat at a terminal are one tensor over the
// seat's class (major) and the classes of the other seats in seat order,
// contracted with the outer product of the other seats' reach and scaled:
//   values[H] = scale * sum_j tensor[H * 81^(N-1) + j] * outer[j].
// Tensor 0 counts the deals of the other seats' combos disjoint from the
// representative and from each other: D[H][A] heads-up, N[H][A][B] 3-way. A
// seat whose payoff does not depend on the cards (every seat at a fold, a
// seat that folded earlier at a 2-way showdown) uses it scaled by that
// payoff; a seat at a showdown uses a tensor of its own (scale 1): the
// runout-weighted masses of every outcome times the seat's payoff in it,
// divided by the runouts. The heads-up tensors are aggregated from
// preflop_all_in_v1.bin at load; the 3-way ones (phase 2b part 2) come from
// the three-player class table in the same layout.
#include "monker_chart_format.hpp"

#include "gtosd/card_abstraction/all_in_table.hpp"
#include "gtosd/card_abstraction/showdown_counts.hpp"
#include "gtosd/card_abstraction/three_way_table.hpp"
#include "gtosd/core/game.hpp"
#include "gtosd/preflop_blueprint/compiled_game.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace gtosd::checkdown_classes {

namespace ca = card_abstraction;
namespace pb = preflop_blueprint;

inline constexpr std::size_t class_count = 81U;
inline constexpr std::size_t maximum_seats = 3U;
inline constexpr double units_per_ante = static_cast<double>(Money::units_per_ante);
// Mean own reach below which a hand class counts as outside the acting range
// (it prints as 0.000 in the chart format), as in the combo-level program.
inline constexpr double out_of_range_reach = 5e-4;
// Ordered deals of distinct disjoint combos: 630 x C(34,2) heads-up, times
// C(32,2) for a third seat.
inline constexpr double heads_up_deals = 630.0 * 561.0;
inline constexpr double three_way_deals = 630.0 * 561.0 * 496.0;

// Combos per hand class: 6 for a pair, 4 suited, 12 offsuit.
inline std::array<double, class_count> class_masses() {
  std::array<double, class_count> masses{};
  for (const auto hand_class : ca::combo_table().hand_class) {
    masses[hand_class] += 1.0;
  }
  return masses;
}

// Heads-up outcomes of one hero combo against one opponent class: the
// opponent's combos disjoint from the hero and their summed wins, ties and
// losses (the hero's view) over the 201,376 runouts each.
struct ClassOutcome {
  std::uint32_t opponents{0U};
  std::uint64_t wins{0U};
  std::uint64_t ties{0U};
  std::uint64_t losses{0U};
  friend constexpr bool operator==(const ClassOutcome &, const ClassOutcome &) = default;
};
using ClassOutcomeRow = std::array<ClassOutcome, class_count>;

// Row of any hero combo, per opponent class, from preflop_all_in_v1.bin.
inline ClassOutcomeRow heads_up_row(const ca::AllInTable &table, const std::uint16_t hero_combo) {
  const auto &combos = ca::combo_table();
  ClassOutcomeRow row{};
  for (std::uint16_t opponent = 0U; opponent < ca::combo_count; ++opponent) {
    if ((combos.masks[hero_combo] & combos.masks[opponent]) != 0U) {
      continue;
    }
    const auto outcome = table.outcome(hero_combo, opponent);
    if (outcome.total() != ca::all_in_runout_count) {
      throw std::runtime_error("all-in table has no full runout count for a disjoint pair");
    }
    auto &entry = row[combos.hand_class[opponent]];
    ++entry.opponents;
    entry.wins += outcome.wins;
    entry.ties += outcome.ties;
    entry.losses += outcome.losses;
  }
  return row;
}

// preflop_all_in_v1.bin aggregated to 81 x 81: row H is the row of the
// representative of class H. Every combo of H has the same row (suit
// symmetry; the tests check all 630 combos), so the aggregation is exact.
inline std::vector<ClassOutcomeRow> heads_up_class_outcomes(const ca::AllInTable &table) {
  std::vector<ClassOutcomeRow> rows(class_count);
  for (std::size_t hero = 0; hero < class_count; ++hero) {
    rows[hero] =
        heads_up_row(table, ca::ThreeWayTable::representative_of(static_cast<std::uint8_t>(hero)));
  }
  return rows;
}

// Reach of every seat's own actions per hand class, seat major.
using Reach = std::vector<std::vector<double>>;

// Outer product of the reach of every seat except `hero`, in seat order with
// the first other seat major: the opponent's reach heads-up, 6,561 entries
// for 3 seats (index A * 81 + B for the classes A and B of the lower and the
// higher other seat). The terminal tensors use the same layout.
inline std::vector<double> others_outer(const Reach &reach, const std::uint8_t hero) {
  std::vector<double> outer{1.0};
  for (std::size_t seat = 0; seat < reach.size(); ++seat) {
    if (seat == hero) {
      continue;
    }
    std::vector<double> next(outer.size() * class_count, 0.0);
    for (std::size_t index = 0; index < outer.size(); ++index) {
      for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
        next[index * class_count + hand_class] = outer[index] * reach[seat][hand_class];
      }
    }
    outer = std::move(next);
  }
  return outer;
}

// values[H] = scale * sum_j tensor[H * stride + j] * outer[j], with the
// stride the size of the outer product.
inline std::vector<double> contract(const std::vector<double> &tensor, const double scale,
                                    const std::vector<double> &outer) {
  const auto stride = outer.size();
  if (tensor.size() != class_count * stride) {
    throw std::logic_error("terminal tensor and reach product differ in size");
  }
  std::vector<double> values(class_count, 0.0);
  for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
    const double *row = tensor.data() + hand_class * stride;
    double sum = 0.0;
    for (std::size_t index = 0; index < stride; ++index) {
      sum += row[index] * outer[index];
    }
    values[hand_class] = scale * sum;
  }
  return values;
}

// Values of one seat at one terminal: tensors[tensor] times scale.
struct TerminalTerm {
  std::uint32_t tensor{0U};
  double scale{0.0};
};

struct CheckdownTerminals {
  std::size_t seats{0U};
  // tensors[0] is the deal count tensor (D or N), the others belong to one
  // seat at one showdown each.
  std::vector<std::vector<double>> tensors;
  // Per node id (terminals only) and seat.
  std::vector<std::array<TerminalTerm, maximum_seats>> terms;
  // Ordered deals: sum over H of |H| times the row sum of tensors[0].
  double deals{0.0};
  std::uint64_t fold_terminals{0U};
  std::uint64_t showdown_terminals{0U};

  [[nodiscard]] std::uint64_t tensor_bytes() const noexcept {
    std::uint64_t bytes = 0U;
    for (const auto &tensor : tensors) {
      bytes += tensor.size() * sizeof(double);
    }
    return bytes;
  }
};

// Sum over H of |H| times the row sum of a count tensor.
inline double deal_count(const std::vector<double> &counts) {
  const auto masses = class_masses();
  const auto stride = counts.size() / class_count;
  double deals = 0.0;
  for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
    double row = 0.0;
    for (std::size_t index = 0; index < stride; ++index) {
      row += counts[hand_class * stride + index];
    }
    deals += masses[hand_class] * row;
  }
  return deals;
}

// The payoffs of every terminal sum to minus its rake (zero without rake),
// at every winner subset of a showdown's active players. Every node of the
// checkdown tree is preflop: a fold ends the hand before the flop, while an
// all-in runout and a checkdown leaf see the flop.
inline void check_terminal_rake(const pb::CompiledGame &game) {
  const auto &rake = game.config().rake;
  const auto seats = static_cast<std::size_t>(game.config().player_count);
  const auto sum = [seats](const std::span<const std::int64_t> payoffs) {
    std::int64_t total = 0;
    for (std::size_t seat = 0; seat < seats; ++seat) {
      total += payoffs[seat];
    }
    return total;
  };
  for (const auto &node : game.nodes()) {
    if (node.kind != pb::NodeKind::TerminalFold && node.kind != pb::NodeKind::TerminalShowdown) {
      continue;
    }
    const auto expected = calculate_rake(rake, game.states()[node.id].pot,
                                         node.kind == pb::NodeKind::TerminalShowdown);
    if (!expected) {
      throw std::runtime_error("rake computation failed");
    }
    const auto minus_rake = -expected.value().units();
    if (node.kind == pb::NodeKind::TerminalFold) {
      if (sum(game.fold_payoffs(node.id)) != minus_rake) {
        throw std::runtime_error("fold payoffs do not sum to minus the rake");
      }
      continue;
    }
    for (std::uint8_t winners = 1U; winners <= node.active_mask; ++winners) {
      if ((winners & static_cast<std::uint8_t>(~node.active_mask)) != 0U) {
        continue;
      }
      if (sum(game.showdown_payoffs(node.id, winners)) != minus_rake) {
        throw std::runtime_error("showdown payoffs do not sum to minus the rake");
      }
    }
  }
}

// Heads-up terminals from preflop_all_in_v1.bin. A showdown (a preflop
// all-in or a checkdown leaf, five cards to come) gives each seat the tensor
// (W * win + T * tie + L * lose) / 201,376 over (own class, opponent class),
// with W, T and L the aggregated counts and win, tie and lose the seat's
// payoff rows for the winner sets {seat}, {both} and {other}: the combo-level
// payoff matrix of preflop_blueprint_checkdown.cpp summed over the opponent
// class.
inline CheckdownTerminals heads_up_terminals(const pb::CompiledGame &game,
                                             const ca::AllInTable &table) {
  if (game.config().player_count != 2U) {
    throw std::runtime_error("heads-up terminals need a 2-player game");
  }
  const auto rows = heads_up_class_outcomes(table);
  CheckdownTerminals terminals;
  terminals.seats = 2U;
  terminals.terms.assign(game.nodes().size(), {});
  std::vector<double> counts(class_count * class_count, 0.0);
  for (std::size_t hero = 0; hero < class_count; ++hero) {
    for (std::size_t opponent = 0; opponent < class_count; ++opponent) {
      counts[hero * class_count + opponent] = static_cast<double>(rows[hero][opponent].opponents);
    }
  }
  terminals.tensors.push_back(std::move(counts));
  constexpr auto runouts = static_cast<double>(ca::all_in_runout_count);
  for (const auto &node : game.nodes()) {
    if (node.kind == pb::NodeKind::TerminalFold) {
      const auto payoffs = game.fold_payoffs(node.id);
      for (std::size_t seat = 0; seat < 2U; ++seat) {
        terminals.terms[node.id][seat] = {0U, static_cast<double>(payoffs[seat]) / units_per_ante};
      }
      ++terminals.fold_terminals;
      continue;
    }
    if (node.kind != pb::NodeKind::TerminalShowdown) {
      continue;
    }
    if (node.active_mask != 3U || node.remaining_board_cards != 5U) {
      throw std::runtime_error(
          "a heads-up checkdown showdown has both seats and five cards to come");
    }
    for (std::uint8_t seat = 0; seat < 2U; ++seat) {
      const auto seat_bit = static_cast<std::uint8_t>(1U << seat);
      const auto other_bit = static_cast<std::uint8_t>(node.active_mask & ~seat_bit);
      const double win =
          static_cast<double>(game.showdown_payoffs(node.id, seat_bit)[seat]) / units_per_ante;
      const double tie =
          static_cast<double>(game.showdown_payoffs(node.id, node.active_mask)[seat]) /
          units_per_ante;
      const double lose =
          static_cast<double>(game.showdown_payoffs(node.id, other_bit)[seat]) / units_per_ante;
      std::vector<double> tensor(class_count * class_count, 0.0);
      for (std::size_t hero = 0; hero < class_count; ++hero) {
        for (std::size_t opponent = 0; opponent < class_count; ++opponent) {
          const auto &entry = rows[hero][opponent];
          tensor[hero * class_count + opponent] =
              (static_cast<double>(entry.wins) * win + static_cast<double>(entry.ties) * tie +
               static_cast<double>(entry.losses) * lose) /
              runouts;
        }
      }
      terminals.terms[node.id][seat] = {static_cast<std::uint32_t>(terminals.tensors.size()), 1.0};
      terminals.tensors.push_back(std::move(tensor));
    }
    ++terminals.showdown_terminals;
  }
  terminals.deals = deal_count(terminals.tensors[0]);
  if (terminals.deals != heads_up_deals) {
    throw std::runtime_error("the heads-up deal count is not 630 x 561");
  }
  return terminals;
}

enum class Mode : std::uint8_t { Update, Average, BestResponse };

// Vector DCFR over the 81 classes with full traversals, alternating updates
// over every seat and the discounts of preflop_blueprint_checkdown.cpp;
// exact values and best response of the average profile.
class ClassSolver {
public:
  ClassSolver(const pb::CompiledGame &game, const CheckdownTerminals &terminals, const double alpha,
              const double beta, const double gamma)
      : game_(game), terminals_(terminals), masses_(class_masses()), alpha_(alpha), beta_(beta),
        gamma_(gamma) {
    const auto &nodes = game_.nodes();
    if (terminals_.seats != game_.config().player_count || terminals_.seats < 2U ||
        terminals_.seats > maximum_seats || terminals_.terms.size() != nodes.size()) {
      throw std::runtime_error("the terminal tensors do not belong to this game");
    }
    offsets_.assign(nodes.size(), 0U);
    std::size_t cells = 0;
    for (const auto &node : nodes) {
      if (node.kind == pb::NodeKind::Chance) {
        throw std::runtime_error("the checkdown tree still has a chance node");
      }
      if (node.street != Street::Preflop) {
        throw std::runtime_error("the checkdown tree still has a postflop node");
      }
      if (node.kind == pb::NodeKind::Decision) {
        offsets_[node.id] = cells;
        cells += class_count * node.action_count;
      }
    }
    regret_.assign(cells, 0.0);
    strategy_sum_.assign(cells, 0.0);
  }

  [[nodiscard]] std::size_t seats() const noexcept { return terminals_.seats; }

  // One DCFR iteration: every seat in turn (alternating), then the discounts.
  void iterate(const std::uint64_t iteration) {
    const auto ones = uniform_reach();
    for (std::size_t hero = 0; hero < seats(); ++hero) {
      (void)traverse(game_.root(), static_cast<std::uint8_t>(hero), ones, Mode::Update);
    }
    const double t = static_cast<double>(iteration);
    const double positive = std::pow(t, alpha_) / (std::pow(t, alpha_) + 1.0);
    const double negative = std::pow(t, beta_) / (std::pow(t, beta_) + 1.0);
    const double average = std::pow(t / (t + 1.0), gamma_);
    for (std::size_t cell = 0; cell < regret_.size(); ++cell) {
      regret_[cell] *= regret_[cell] > 0.0 ? positive : negative;
      strategy_sum_[cell] *= average;
    }
  }

  // Expected value of a seat per hand, in antes, when the seat plays `mode`
  // (average strategy or best response) against the others' average
  // strategies.
  double value(const std::uint8_t hero, const Mode mode) {
    const auto values = traverse(game_.root(), hero, uniform_reach(), mode);
    double sum = 0.0;
    for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
      sum += masses_[hand_class] * values[hand_class];
    }
    return sum / terminals_.deals;
  }

  // Average strategy of a class at a decision node, in edge order.
  [[nodiscard]] std::vector<double> average_strategy(const std::uint32_t node,
                                                     const std::size_t hand_class) const {
    std::vector<double> row(game_.nodes()[node].action_count, 0.0);
    fill_strategy(node, hand_class, true, row.data());
    return row;
  }

  // Reach of every seat's own actions under the average profile, per node.
  [[nodiscard]] std::vector<Reach> own_reach() const {
    std::vector<Reach> reach(game_.nodes().size());
    forward(game_.root(), uniform_reach(), reach);
    return reach;
  }

private:
  [[nodiscard]] Reach uniform_reach() const {
    return Reach(seats(), std::vector<double>(class_count, 1.0));
  }

  // Current (regret matching) or average strategy of a class at a node;
  // uniform where the sum is zero.
  void fill_strategy(const std::uint32_t node, const std::size_t hand_class, const bool use_average,
                     double *row) const {
    const auto actions = static_cast<std::size_t>(game_.nodes()[node].action_count);
    const auto &source = use_average ? strategy_sum_ : regret_;
    const auto base = offsets_[node] + hand_class * actions;
    double total = 0.0;
    for (std::size_t action = 0; action < actions; ++action) {
      const double entry =
          use_average ? source[base + action] : std::max(source[base + action], 0.0);
      row[action] = entry;
      total += entry;
    }
    for (std::size_t action = 0; action < actions; ++action) {
      row[action] = total > 0.0 ? row[action] / total : 1.0 / static_cast<double>(actions);
    }
  }

  // The strategies of every class at a node, class major.
  [[nodiscard]] std::vector<double> strategy_table(const std::uint32_t node,
                                                   const bool use_average) const {
    const auto actions = static_cast<std::size_t>(game_.nodes()[node].action_count);
    std::vector<double> result(class_count * actions, 0.0);
    for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
      fill_strategy(node, hand_class, use_average, result.data() + hand_class * actions);
    }
    return result;
  }

  std::vector<double> traverse(const std::uint32_t node_id, const std::uint8_t hero,
                               const Reach &reach, const Mode mode) {
    const auto &node = game_.nodes()[node_id];
    if (node.kind == pb::NodeKind::TerminalFold || node.kind == pb::NodeKind::TerminalShowdown) {
      const auto &term = terminals_.terms[node_id][hero];
      return contract(terminals_.tensors[term.tensor], term.scale, others_outer(reach, hero));
    }
    const auto edges = game_.edges_of(node_id);
    const auto actions = edges.size();
    const auto strategies = strategy_table(node_id, mode != Mode::Update);
    std::vector<double> values(class_count, 0.0);
    if (node.actor != hero) {
      for (std::size_t action = 0; action < actions; ++action) {
        auto next = reach;
        for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
          next[node.actor][hand_class] *= strategies[hand_class * actions + action];
        }
        const auto child = traverse(edges[action].child, hero, next, mode);
        for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
          values[hand_class] += child[hand_class];
        }
      }
      return values;
    }
    std::vector<std::vector<double>> children(actions);
    for (std::size_t action = 0; action < actions; ++action) {
      auto next = reach;
      for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
        next[hero][hand_class] *= strategies[hand_class * actions + action];
      }
      children[action] = traverse(edges[action].child, hero, next, mode);
    }
    for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
      if (mode == Mode::BestResponse) {
        double best = children[0][hand_class];
        for (std::size_t action = 1; action < actions; ++action) {
          best = std::max(best, children[action][hand_class]);
        }
        values[hand_class] = best;
        continue;
      }
      const double *strategy = strategies.data() + hand_class * actions;
      double node_value = 0.0;
      for (std::size_t action = 0; action < actions; ++action) {
        node_value += strategy[action] * children[action][hand_class];
      }
      values[hand_class] = node_value;
      if (mode == Mode::Update) {
        const auto base = offsets_[node_id] + hand_class * actions;
        for (std::size_t action = 0; action < actions; ++action) {
          regret_[base + action] += children[action][hand_class] - node_value;
          strategy_sum_[base + action] += reach[hero][hand_class] * strategy[action];
        }
      }
    }
    return values;
  }

  void forward(const std::uint32_t node_id, const Reach &reach, std::vector<Reach> &output) const {
    output[node_id] = reach;
    const auto &node = game_.nodes()[node_id];
    if (node.kind != pb::NodeKind::Decision) {
      return;
    }
    const auto edges = game_.edges_of(node_id);
    const auto actions = edges.size();
    const auto strategies = strategy_table(node_id, true);
    for (std::size_t action = 0; action < actions; ++action) {
      auto next = reach;
      for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
        next[node.actor][hand_class] *= strategies[hand_class * actions + action];
      }
      forward(edges[action].child, next, output);
    }
  }

  const pb::CompiledGame &game_;
  const CheckdownTerminals &terminals_;
  std::array<double, class_count> masses_;
  double alpha_;
  double beta_;
  double gamma_;
  std::vector<std::size_t> offsets_;
  std::vector<double> regret_;
  std::vector<double> strategy_sum_;
};

// The rake of every terminal times the probability that the average profile
// reaches it (every seat's own reach over the disjoint deals of tensor 0),
// independent of the payoffs and of the value traversal: it equals minus the
// sum of the seats' EVs.
inline double expected_rake_by_reach(const pb::CompiledGame &game,
                                     const CheckdownTerminals &terminals,
                                     const std::vector<Reach> &reach) {
  const auto masses = class_masses();
  const auto &rake = game.config().rake;
  double total = 0.0;
  for (const auto &node : game.nodes()) {
    if (node.kind != pb::NodeKind::TerminalFold && node.kind != pb::NodeKind::TerminalShowdown) {
      continue;
    }
    const auto amount = calculate_rake(rake, game.states()[node.id].pot,
                                       node.kind == pb::NodeKind::TerminalShowdown);
    if (!amount) {
      throw std::runtime_error("rake computation failed");
    }
    if (amount.value().units() == 0) {
      continue;
    }
    const auto deals = contract(terminals.tensors[0], 1.0, others_outer(reach[node.id], 0U));
    double mass = 0.0;
    for (std::size_t hand_class = 0; hand_class < class_count; ++hand_class) {
      mass += masses[hand_class] * reach[node.id][0][hand_class] * deals[hand_class];
    }
    total += static_cast<double>(amount.value().units()) / units_per_ante * mass / terminals.deals;
  }
  return total;
}

// One chart per preflop decision node through mc::write_charts, which names
// the files and orders the columns by chart_nodes (MonkerSolver's fold rule
// in a multiway tree). The row of a class is its average strategy; a class
// whose own reach at the node is below out_of_range_reach is an all-zero row.
inline std::vector<std::string> write_class_charts(const pb::CompiledGame &game,
                                                   const ClassSolver &solver,
                                                   const std::filesystem::path &directory) {
  const auto classes = monker_charts::hand_classes();
  std::map<std::string, std::uint8_t> class_of_label;
  for (const auto &[hand_class, label] : classes.label_by_class) {
    class_of_label[label] = hand_class;
  }
  std::vector<std::string> labels;
  for (const auto &entry : classes.combos_by_label) {
    labels.push_back(entry.first);
  }
  const auto reach = solver.own_reach();
  const monker_charts::ClassStrategy strategy =
      [&](const std::uint32_t node_id,
          const std::string &label) -> std::optional<std::vector<double>> {
    const auto hand_class = class_of_label.at(label);
    if (reach[node_id][game.nodes()[node_id].actor][hand_class] < out_of_range_reach) {
      return std::nullopt;
    }
    return solver.average_strategy(node_id, hand_class);
  };
  return monker_charts::write_charts(game, labels, strategy, directory);
}

} // namespace gtosd::checkdown_classes
