#include "gtosd/preflop_blueprint/best_response.hpp"

#include "gtosd/card_abstraction/combinatorics.hpp"
#include "gtosd/card_abstraction/showdown_counts.hpp"
#include "gtosd/preflop_blueprint/kernels.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <map>
#include <optional>
#include <thread>
#include <utility>

namespace gtosd::preflop_blueprint {
namespace {

namespace ca = card_abstraction;
using Clock = std::chrono::steady_clock;
constexpr std::size_t combo_total = 630U;
constexpr double ante_scale = 1.0 / static_cast<double>(Money::units_per_ante);

bool disjoint(const std::array<std::uint8_t, 2> &left,
              const std::array<std::uint8_t, 2> &right) noexcept {
  return left[0] != right[0] && left[0] != right[1] && left[1] != right[0] &&
         left[1] != right[1];
}

std::array<std::uint8_t, 2> cards_of(const std::uint16_t combo) {
  return ca::combo_table().cards[combo];
}

// Live combos for a board prefix with their policy rows at one street.
struct Universe {
  std::vector<std::uint16_t> combos;
  std::vector<std::array<std::uint8_t, 2>> cards;
  std::array<std::uint16_t, combo_total> index{};
  std::vector<std::uint16_t> rows;
  std::uint64_t mask{0U};

  [[nodiscard]] std::size_t size() const noexcept { return combos.size(); }
};

Universe make_universe(const std::uint64_t mask) {
  Universe universe;
  universe.mask = mask;
  universe.index.fill(no_hand);
  const auto &table = ca::combo_table();
  for (std::uint16_t combo = 0; combo < combo_total; ++combo) {
    if ((table.masks[combo] & mask) != 0U) {
      continue;
    }
    universe.index[combo] = static_cast<std::uint16_t>(universe.combos.size());
    universe.combos.push_back(combo);
    universe.cards.push_back(table.cards[combo]);
  }
  universe.rows.assign(universe.size(), ca::no_bucket);
  return universe;
}

void assign_class_rows(Universe &universe) {
  const auto &table = ca::combo_table();
  for (std::size_t hand = 0; hand < universe.size(); ++hand) {
    universe.rows[hand] = table.hand_class[universe.combos[hand]];
  }
}

bool assign_bucket_rows(Universe &universe, const ca::BucketTable &table,
                        const Result<ca::CanonicalLookup, CardError> &lookup) {
  if (!lookup) {
    return false;
  }
  const auto row = lookup.value().index;
  const auto &permutation = lookup.value().permutation;
  for (std::size_t hand = 0; hand < universe.size(); ++hand) {
    const auto first = ca::permute_card(CardId::from_index(universe.cards[hand][0]).value(), permutation);
    const auto second =
        ca::permute_card(CardId::from_index(universe.cards[hand][1]).value(), permutation);
    const auto bucket = table.bucket(row, ca::combo_index(first, second));
    if (bucket == ca::no_bucket) {
      return false;
    }
    universe.rows[hand] = bucket;
  }
  return true;
}

// Sum over the live hands disjoint from each hand of reach (per-card sums).
void fold_mass_universe(const Universe &universe, const std::vector<double> &reach,
                        std::vector<double> &out) {
  double total = 0.0;
  std::array<double, 36> per_card{};
  for (std::size_t hand = 0; hand < universe.size(); ++hand) {
    total += reach[hand];
    per_card[universe.cards[hand][0]] += reach[hand];
    per_card[universe.cards[hand][1]] += reach[hand];
  }
  out.resize(universe.size());
  for (std::size_t hand = 0; hand < universe.size(); ++hand) {
    out[hand] =
        total - per_card[universe.cards[hand][0]] - per_card[universe.cards[hand][1]] + reach[hand];
  }
}

struct Policies {
  const BucketPolicy *average{nullptr};
  [[nodiscard]] std::span<const double> row(const std::uint32_t node,
                                            const std::uint16_t row_index) const {
    return average->row(node, row_index);
  }
};

// Values per node for one universe; nodes untouched stay empty.
using NodeVectors = std::vector<std::vector<double>>;

class StreetEvaluator {
public:
  StreetEvaluator(const CompiledGame &game, const Policies &policies)
      : game_(game), policies_(policies) {}

  // Propagates the opponent reach from `node` through the decision nodes of
  // its street and records the reach at every leaf (chance node or terminal).
  void propagate(const std::uint32_t node, const Universe &universe, const std::uint8_t hero,
                 const std::vector<double> &reach, NodeVectors &leaf_reach) const {
    const auto &entry = game_.nodes()[node];
    if (entry.kind != NodeKind::Decision) {
      leaf_reach[node] = reach;
      return;
    }
    const auto edges = game_.edges_of(node);
    if (entry.actor == hero) {
      for (const auto &edge : edges) {
        propagate(edge.child, universe, hero, reach, leaf_reach);
      }
      return;
    }
    std::vector<double> child(universe.size());
    for (std::size_t action = 0; action < edges.size(); ++action) {
      for (std::size_t hand = 0; hand < universe.size(); ++hand) {
        child[hand] = reach[hand] == 0.0
                          ? 0.0
                          : reach[hand] * policies_.row(node, universe.rows[hand])[action];
      }
      propagate(edges[action].child, universe, hero, child, leaf_reach);
    }
  }

  // Value of the subtree of `node` within its street from the leaf values,
  // taking the maximum (response) or the average strategy (average) at the
  // hero's decisions. Records the response choice when `choice` is given.
  void value(const std::uint32_t node, const Universe &universe, const std::uint8_t hero,
             const std::size_t mode, const NodeVectors &leaf_values, std::vector<double> &out,
             std::vector<std::vector<std::uint8_t>> *choice) const {
    const auto &entry = game_.nodes()[node];
    if (entry.kind != NodeKind::Decision) {
      out = leaf_values[node];
      if (out.size() != universe.size()) {
        out.assign(universe.size(), 0.0);
      }
      return;
    }
    const auto edges = game_.edges_of(node);
    std::vector<std::vector<double>> children(edges.size());
    for (std::size_t action = 0; action < edges.size(); ++action) {
      value(edges[action].child, universe, hero, mode, leaf_values, children[action], choice);
    }
    out.assign(universe.size(), 0.0);
    if (entry.actor != hero) {
      for (const auto &child : children) {
        for (std::size_t hand = 0; hand < universe.size(); ++hand) {
          out[hand] += child[hand];
        }
      }
      return;
    }
    if (choice != nullptr) {
      (*choice)[node].assign(universe.size(), 0U);
    }
    for (std::size_t hand = 0; hand < universe.size(); ++hand) {
      if (mode == response_mode) {
        double best = children[0][hand];
        std::uint8_t best_action = 0U;
        for (std::size_t action = 1; action < children.size(); ++action) {
          if (children[action][hand] > best) {
            best = children[action][hand];
            best_action = static_cast<std::uint8_t>(action);
          }
        }
        out[hand] = best;
        if (choice != nullptr) {
          (*choice)[node][hand] = best_action;
        }
      } else {
        const auto probabilities = policies_.row(node, universe.rows[hand]);
        double total = 0.0;
        for (std::size_t action = 0; action < children.size(); ++action) {
          total += probabilities[action] * children[action][hand];
        }
        out[hand] = total;
      }
    }
  }

private:
  const CompiledGame &game_;
  const Policies &policies_;
};

double terminal_payoff(const CompiledGame &game, const CompiledNode &node, const std::uint8_t hero,
                       std::array<double, 3> &showdown) {
  if (node.kind == NodeKind::TerminalFold) {
    return static_cast<double>(game.fold_payoffs(node.id)[hero]) * ante_scale;
  }
  const auto hero_bit = static_cast<std::uint8_t>(std::uint8_t{1} << hero);
  const auto opponents = static_cast<std::uint8_t>(node.active_mask & ~hero_bit);
  showdown[0] = static_cast<double>(game.showdown_payoffs(node.id, hero_bit)[hero]) * ante_scale;
  showdown[1] =
      static_cast<double>(game.showdown_payoffs(node.id, node.active_mask)[hero]) * ante_scale;
  showdown[2] = static_cast<double>(game.showdown_payoffs(node.id, opponents)[hero]) * ante_scale;
  return 0.0;
}

// Restricts a reach vector from a wider universe to the live hands of a board.
void restrict_to_board(const Universe &universe, const std::vector<double> &reach,
                       const BoardContext &context, std::array<double, live_hand_count> &out) {
  const auto combos = context.combo_ids();
  for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
    const auto index = universe.index[combos[hand]];
    out[hand] = index == no_hand ? 0.0 : reach[index];
  }
}

void restrict_to_universe(const Universe &from, const std::vector<double> &reach,
                          const Universe &to, std::vector<double> &out) {
  out.assign(to.size(), 0.0);
  for (std::size_t hand = 0; hand < to.size(); ++hand) {
    const auto index = from.index[to.combos[hand]];
    if (index != no_hand) {
      out[hand] = reach[index];
    }
  }
}

template <typename Function>
void run_parallel(const unsigned threads, const std::size_t count, Function &&function) {
  const auto workers = static_cast<std::size_t>(std::max(1U, threads));
  if (workers <= 1U || count <= 1U) {
    for (std::size_t index = 0; index < count; ++index) {
      function(index);
    }
    return;
  }
  std::atomic<std::size_t> next{0U};
  const auto worker = [&] {
    for (std::size_t index = next.fetch_add(1U); index < count; index = next.fetch_add(1U)) {
      function(index);
    }
  };
  std::vector<std::thread> pool;
  const auto spawned = std::min(workers, count) - 1U;
  for (std::size_t thread = 0; thread < spawned; ++thread) {
    pool.emplace_back(worker);
  }
  worker();
  for (auto &thread : pool) {
    thread.join();
  }
}

// Combo of sigma^-1(h) for every combo h.
std::array<std::uint16_t, combo_total> combo_preimage(const ca::SuitPermutation &permutation) {
  const auto inverse = ca::inverse_permutation(permutation);
  std::array<std::uint16_t, combo_total> map{};
  for (std::uint16_t combo = 0; combo < combo_total; ++combo) {
    const auto cards = cards_of(combo);
    const auto first = ca::permute_card(CardId::from_index(cards[0]).value(), inverse);
    const auto second = ca::permute_card(CardId::from_index(cards[1]).value(), inverse);
    map[combo] = ca::combo_index(first, second);
  }
  return map;
}

} // namespace

struct BestResponseEvaluator::Impl {
  const CompiledGame *game{nullptr};
  const BucketPolicy *average{nullptr};
  BestResponseResources resources;
  std::array<std::vector<double>, 2> allowed{}; // per player, 630 entries of 0/1
  Universe preflop;
  // Opponent reach at the preflop leaves, per hero.
  std::array<NodeVectors, 2> preflop_leaf_reach{};
  std::vector<std::uint32_t> entries;
  Policies policies;
};

namespace {

FlopValues evaluate_flop_group(const BestResponseEvaluator::Impl &context, const FlopGroup &group,
                               bool &ok) {
  const auto &game = *context.game;
  const auto &resources = context.resources;
  const auto node_count = game.nodes().size();
  FlopValues result;
  result.flop = group.flop;
  result.weight = group.weight;
  result.boards = static_cast<std::uint32_t>(group.boards.size());
  ok = true;
  StreetEvaluator evaluator(game, context.policies);
  const HeadsUpShowdownKernel kernel;

  std::uint64_t flop_mask = 0U;
  for (const auto card : group.flop) {
    flop_mask |= card.mask();
  }
  auto flop_universe = make_universe(flop_mask);
  if (!assign_bucket_rows(flop_universe, *resources.flop, resources.catalog->lookup_flop(group.flop))) {
    ok = false;
    return result;
  }
  result.compatible.assign(combo_total, 0U);
  for (const auto combo : flop_universe.combos) {
    result.compatible[combo] = 1U;
  }

  // Board weights compatible with every hand at the flop level and per turn.
  std::map<std::uint8_t, std::vector<const WeightedBoard *>> by_turn;
  for (const auto &board : group.boards) {
    by_turn[board.history.turn.value()].push_back(&board);
  }
  std::vector<double> flop_weight(flop_universe.size(), 0.0);
  for (const auto &board : group.boards) {
    const auto turn = board.history.turn.value();
    const auto river = board.history.river.value();
    for (std::size_t hand = 0; hand < flop_universe.size(); ++hand) {
      const auto &cards = flop_universe.cards[hand];
      if (cards[0] != turn && cards[1] != turn && cards[0] != river && cards[1] != river) {
        flop_weight[hand] += board.weight;
      }
    }
  }

  // Opponent reach at the flop leaves, per hero.
  std::array<NodeVectors, 2> flop_leaf_reach{NodeVectors(node_count), NodeVectors(node_count)};
  std::array<std::array<NodeVectors, 2>, 2> flop_leaf_values{};
  for (auto &per_hero : flop_leaf_values) {
    for (auto &per_mode : per_hero) {
      per_mode.assign(node_count, {});
    }
  }
  std::vector<double> scratch;
  for (std::uint8_t hero = 0; hero < 2U; ++hero) {
    for (const auto entry : context.entries) {
      restrict_to_universe(context.preflop, context.preflop_leaf_reach[hero][entry], flop_universe,
                           scratch);
      evaluator.propagate(game.edges_of(entry)[0].child, flop_universe, hero, scratch,
                          flop_leaf_reach[hero]);
    }
  }
  // Flop fold terminals and the accumulators of the other flop leaves.
  std::vector<double> disjoint_mass;
  std::vector<double> opponent_count;
  for (std::uint8_t hero = 0; hero < 2U; ++hero) {
    const auto opponent = static_cast<std::uint8_t>(1U - hero);
    restrict_to_universe(context.preflop, context.allowed[opponent], flop_universe, scratch);
    fold_mass_universe(flop_universe, scratch, opponent_count);
    for (std::size_t node = 0; node < node_count; ++node) {
      if (flop_leaf_reach[hero][node].empty()) {
        continue;
      }
      const auto &entry = game.nodes()[node];
      for (auto mode : {response_mode, average_mode}) {
        flop_leaf_values[hero][mode][node].assign(flop_universe.size(), 0.0);
      }
      if (entry.kind != NodeKind::TerminalFold) {
        continue;
      }
      std::array<double, 3> unused{};
      const double payoff = terminal_payoff(game, entry, hero, unused);
      fold_mass_universe(flop_universe, flop_leaf_reach[hero][node], disjoint_mass);
      for (std::size_t hand = 0; hand < flop_universe.size(); ++hand) {
        const double value =
            opponent_count[hand] > 0.0 ? payoff * disjoint_mass[hand] / opponent_count[hand] : 0.0;
        for (auto mode : {response_mode, average_mode}) {
          flop_leaf_values[hero][mode][node][hand] = value;
        }
      }
    }
  }

  std::optional<ValueTraversal> traversal;
  std::array<double, live_hand_count> reach465{};
  std::array<double, live_hand_count> values465{};
  std::array<double, live_hand_count> worse{};
  std::array<double, live_hand_count> tied{};
  std::array<double, live_hand_count> better{};
  std::array<double, live_hand_count> river_count{};

  for (const auto &[turn_card, turn_boards] : by_turn) {
    const auto turn = CardId::from_index(turn_card).value();
    auto turn_universe = make_universe(flop_mask | turn.mask());
    if (!assign_bucket_rows(turn_universe, *resources.turn,
                            resources.catalog->lookup_flop_turn(group.flop, turn))) {
      ok = false;
      return result;
    }
    std::vector<double> turn_weight(turn_universe.size(), 0.0);
    for (const auto *board : turn_boards) {
      const auto river = board->history.river.value();
      for (std::size_t hand = 0; hand < turn_universe.size(); ++hand) {
        const auto &cards = turn_universe.cards[hand];
        if (cards[0] != river && cards[1] != river) {
          turn_weight[hand] += board->weight;
        }
      }
    }
    std::array<NodeVectors, 2> turn_leaf_reach{NodeVectors(node_count), NodeVectors(node_count)};
    std::array<std::array<NodeVectors, 2>, 2> turn_leaf_values{};
    for (auto &per_hero : turn_leaf_values) {
      for (auto &per_mode : per_hero) {
        per_mode.assign(node_count, {});
      }
    }
    for (std::uint8_t hero = 0; hero < 2U; ++hero) {
      for (std::size_t node = 0; node < node_count; ++node) {
        if (flop_leaf_reach[hero][node].empty() || game.nodes()[node].kind != NodeKind::Chance) {
          continue;
        }
        restrict_to_universe(flop_universe, flop_leaf_reach[hero][node], turn_universe, scratch);
        evaluator.propagate(game.edges_of(static_cast<std::uint32_t>(node))[0].child, turn_universe, hero, scratch,
                            turn_leaf_reach[hero]);
      }
      for (std::size_t node = 0; node < node_count; ++node) {
        if (turn_leaf_reach[hero][node].empty()) {
          continue;
        }
        for (auto mode : {response_mode, average_mode}) {
          turn_leaf_values[hero][mode][node].assign(turn_universe.size(), 0.0);
        }
      }
    }
    // Turn fold terminals.
    for (std::uint8_t hero = 0; hero < 2U; ++hero) {
      const auto opponent = static_cast<std::uint8_t>(1U - hero);
      restrict_to_universe(context.preflop, context.allowed[opponent], turn_universe, scratch);
      fold_mass_universe(turn_universe, scratch, opponent_count);
      for (std::size_t node = 0; node < node_count; ++node) {
        if (turn_leaf_reach[hero][node].empty() ||
            game.nodes()[node].kind != NodeKind::TerminalFold) {
          continue;
        }
        std::array<double, 3> unused{};
        const double payoff = terminal_payoff(game, game.nodes()[node], hero, unused);
        fold_mass_universe(turn_universe, turn_leaf_reach[hero][node], disjoint_mass);
        for (std::size_t hand = 0; hand < turn_universe.size(); ++hand) {
          const double value =
              opponent_count[hand] > 0.0 ? payoff * disjoint_mass[hand] / opponent_count[hand] : 0.0;
          for (auto mode : {response_mode, average_mode}) {
            turn_leaf_values[hero][mode][node][hand] = value;
          }
        }
      }
    }

    // Rivers: river-street best responses and showdown terminals.
    AbstractionTables tables;
    tables.catalog = resources.catalog;
    tables.flop = resources.flop;
    tables.turn = resources.turn;
    tables.river = resources.river;
    for (const auto *board : turn_boards) {
      const auto built = BoardContext::build(board->history, *resources.ranks, &tables);
      if (!built) {
        ok = false;
        return result;
      }
      const auto &board_context = built.value();
      if (!traversal) {
        traversal.emplace(game, board_context, kernel, nullptr);
      } else {
        traversal->rebind(board_context, nullptr);
      }
      const auto river_combos = board_context.combo_ids();
      for (std::uint8_t hero = 0; hero < 2U; ++hero) {
        const auto opponent = static_cast<std::uint8_t>(1U - hero);
        restrict_to_board(context.preflop, context.allowed[opponent], board_context, reach465);
        fold_mass(board_context, reach465, river_count);
        // Turn leaves reached with the river: chance nodes and all-in runouts.
        for (std::size_t node = 0; node < node_count; ++node) {
          if (turn_leaf_reach[hero][node].empty()) {
            continue;
          }
          const auto &entry = game.nodes()[node];
          if (entry.kind == NodeKind::TerminalFold) {
            continue;
          }
          restrict_to_board(turn_universe, turn_leaf_reach[hero][node], board_context, reach465);
          if (entry.kind == NodeKind::Chance) {
            for (auto mode : {response_mode, average_mode}) {
              TraversalOptions options;
              options.best_response = mode == response_mode;
              if (!traversal->evaluate_from(game.edges_of(static_cast<std::uint32_t>(node))[0].child, *context.average, hero,
                                            reach465, values465, options)) {
                ok = false;
                return result;
              }
              auto &target = turn_leaf_values[hero][mode][node];
              for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
                const auto index = turn_universe.index[river_combos[hand]];
                if (index == no_hand || river_count[hand] <= 0.0 || turn_weight[index] <= 0.0) {
                  continue;
                }
                target[index] += values465[hand] * board->weight / (turn_weight[index] * river_count[hand]);
              }
            }
          } else {
            std::array<double, 3> payoffs{};
            terminal_payoff(game, entry, hero, payoffs);
            showdown_masses(board_context, reach465, worse, tied, better);
            for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
              const auto index = turn_universe.index[river_combos[hand]];
              if (index == no_hand || river_count[hand] <= 0.0 || turn_weight[index] <= 0.0) {
                continue;
              }
              const double value = payoffs[0] * worse[hand] + payoffs[1] * tied[hand] +
                                   payoffs[2] * better[hand];
              for (auto mode : {response_mode, average_mode}) {
                turn_leaf_values[hero][mode][node][index] +=
                    value * board->weight / (turn_weight[index] * river_count[hand]);
              }
            }
          }
        }
        // Flop all-in runouts need the full board too.
        for (std::size_t node = 0; node < node_count; ++node) {
          if (flop_leaf_reach[hero][node].empty() ||
              game.nodes()[node].kind != NodeKind::TerminalShowdown) {
            continue;
          }
          restrict_to_board(flop_universe, flop_leaf_reach[hero][node], board_context, reach465);
          std::array<double, 3> payoffs{};
          terminal_payoff(game, game.nodes()[node], hero, payoffs);
          showdown_masses(board_context, reach465, worse, tied, better);
          for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
            const auto index = flop_universe.index[river_combos[hand]];
            if (index == no_hand || river_count[hand] <= 0.0 || flop_weight[index] <= 0.0) {
              continue;
            }
            const double value =
                payoffs[0] * worse[hand] + payoffs[1] * tied[hand] + payoffs[2] * better[hand];
            for (auto mode : {response_mode, average_mode}) {
              flop_leaf_values[hero][mode][node][index] +=
                  value * board->weight / (flop_weight[index] * river_count[hand]);
            }
          }
        }
      }
    }

    // Turn street values feed the flop chance leaves.
    std::vector<double> turn_values;
    for (std::uint8_t hero = 0; hero < 2U; ++hero) {
      for (auto mode : {response_mode, average_mode}) {
        for (std::size_t node = 0; node < node_count; ++node) {
          if (flop_leaf_reach[hero][node].empty() || game.nodes()[node].kind != NodeKind::Chance) {
            continue;
          }
          evaluator.value(game.edges_of(static_cast<std::uint32_t>(node))[0].child, turn_universe, hero, mode,
                          turn_leaf_values[hero][mode], turn_values, nullptr);
          auto &target = flop_leaf_values[hero][mode][node];
          for (std::size_t hand = 0; hand < turn_universe.size(); ++hand) {
            const auto index = flop_universe.index[turn_universe.combos[hand]];
            if (index == no_hand || flop_weight[index] <= 0.0) {
              continue;
            }
            target[index] += turn_values[hand] * turn_weight[hand] / flop_weight[index];
          }
        }
      }
    }
  }

  // Flop street values per entry.
  std::vector<double> flop_values;
  for (std::uint8_t hero = 0; hero < 2U; ++hero) {
    for (auto mode : {response_mode, average_mode}) {
      auto &per_entry = result.entry_values[hero][mode];
      per_entry.resize(context.entries.size());
      for (std::size_t entry = 0; entry < context.entries.size(); ++entry) {
        evaluator.value(game.edges_of(context.entries[entry])[0].child, flop_universe, hero, mode,
                        flop_leaf_values[hero][mode], flop_values, nullptr);
        auto &target = per_entry[entry];
        target.assign(combo_total, 0.0);
        for (std::size_t hand = 0; hand < flop_universe.size(); ++hand) {
          target[flop_universe.combos[hand]] = flop_values[hand];
        }
      }
    }
  }
  return result;
}

} // namespace

FlopGroup full_runouts(const std::array<CardId, 3> &flop, const double weight) {
  FlopGroup group;
  group.flop = flop;
  std::sort(group.flop.begin(), group.flop.end());
  group.weight = weight;
  std::uint64_t mask = 0U;
  for (const auto card : group.flop) {
    mask |= card.mask();
  }
  for (std::uint8_t turn = 0; turn < 36U; ++turn) {
    if (((mask >> turn) & 1U) != 0U) {
      continue;
    }
    for (std::uint8_t river = 0; river < 36U; ++river) {
      if (river == turn || ((mask >> river) & 1U) != 0U) {
        continue;
      }
      WeightedBoard board;
      board.history.flop = group.flop;
      board.history.turn = CardId::from_index(turn).value();
      board.history.river = CardId::from_index(river).value();
      board.weight = 1.0;
      group.boards.push_back(board);
    }
  }
  return group;
}

std::vector<FlopGroup> group_by_flop(const std::vector<WeightedBoard> &boards) {
  std::map<std::array<std::uint8_t, 3>, FlopGroup> groups;
  for (const auto &board : boards) {
    std::array<std::uint8_t, 3> key{board.history.flop[0].value(), board.history.flop[1].value(),
                                    board.history.flop[2].value()};
    std::sort(key.begin(), key.end());
    auto &group = groups[key];
    if (group.boards.empty()) {
      group.flop = {CardId::from_index(key[0]).value(), CardId::from_index(key[1]).value(),
                    CardId::from_index(key[2]).value()};
      group.weight = 0.0;
    }
    group.boards.push_back(board);
    group.weight += board.weight;
  }
  std::vector<FlopGroup> result;
  result.reserve(groups.size());
  for (auto &[key, group] : groups) {
    static_cast<void>(key);
    result.push_back(std::move(group));
  }
  return result;
}

std::vector<ca::SuitPermutation> flop_images(const std::array<CardId, 3> &flop) {
  std::vector<ca::SuitPermutation> images;
  std::vector<std::array<std::uint8_t, 3>> seen;
  for (const auto &permutation : ca::all_suit_permutations()) {
    std::array<std::uint8_t, 3> image{};
    for (std::size_t index = 0; index < 3U; ++index) {
      image[index] = ca::permute_card(flop[index], permutation).value();
    }
    std::sort(image.begin(), image.end());
    if (std::find(seen.begin(), seen.end(), image) != seen.end()) {
      continue;
    }
    seen.push_back(image);
    images.push_back(permutation);
  }
  // Keep the identity first (it is the representative of the flop itself).
  const auto identity = std::find(images.begin(), images.end(), ca::identity_permutation);
  if (identity != images.end() && identity != images.begin()) {
    std::iter_swap(images.begin(), identity);
  }
  return images;
}

Result<BestResponseEvaluator, KernelError>
BestResponseEvaluator::create(const CompiledGame &game, const BucketPolicy &average,
                              const BestResponseResources &resources,
                              const std::array<std::vector<std::uint16_t>, 2> &hand_subsets) {
  using Outcome = Result<BestResponseEvaluator, KernelError>;
  if (resources.ranks == nullptr || resources.catalog == nullptr || resources.flop == nullptr ||
      resources.turn == nullptr || resources.river == nullptr ||
      game.config().player_count != 2U) {
    return Outcome::failure(KernelError::MissingTable);
  }
  const auto &stats = game.stats();
  if (stats.preflop_all_in_runouts > 0U && resources.all_in == nullptr) {
    return Outcome::failure(KernelError::MissingTable);
  }
  auto impl = std::make_shared<Impl>();
  impl->game = &game;
  impl->average = &average;
  impl->resources = resources;
  impl->preflop = make_universe(0U);
  impl->entries = game.postflop_entries();
  impl->policies.average = &average;
  assign_class_rows(impl->preflop);
  for (std::uint8_t player = 0; player < 2U; ++player) {
    impl->allowed[player].assign(combo_total, 1.0);
    if (!hand_subsets[player].empty()) {
      impl->allowed[player].assign(combo_total, 0.0);
      for (const auto combo : hand_subsets[player]) {
        if (combo >= combo_total) {
          return Outcome::failure(KernelError::InvalidInput);
        }
        impl->allowed[player][combo] = 1.0;
      }
    }
  }
  const auto node_count = game.nodes().size();
  StreetEvaluator evaluator(game, impl->policies);
  for (std::uint8_t hero = 0; hero < 2U; ++hero) {
    impl->preflop_leaf_reach[hero].assign(node_count, {});
    evaluator.propagate(game.root(), impl->preflop, hero, impl->allowed[1U - hero],
                        impl->preflop_leaf_reach[hero]);
  }
  BestResponseEvaluator result;
  result.impl_ = std::move(impl);
  return Outcome::success(std::move(result));
}

std::size_t BestResponseEvaluator::entry_count() const noexcept {
  return impl_ ? impl_->entries.size() : 0U;
}

Result<FlopValues, KernelError> BestResponseEvaluator::evaluate_flop(const FlopGroup &group) const {
  using Outcome = Result<FlopValues, KernelError>;
  if (!impl_) {
    return Outcome::failure(KernelError::MissingTable);
  }
  bool ok = true;
  auto values = evaluate_flop_group(*impl_, group, ok);
  if (!ok) {
    return Outcome::failure(KernelError::InvalidInput);
  }
  return Outcome::success(std::move(values));
}

Result<BestResponseReport, KernelError>
BestResponseEvaluator::aggregate(const std::vector<const FlopValues *> &flops,
                                 const bool exact) const {
  using Outcome = Result<BestResponseReport, KernelError>;
  const auto started = Clock::now();
  if (!impl_ || flops.empty()) {
    return Outcome::failure(KernelError::InvalidInput);
  }
  const auto &context = *impl_;
  const auto &game = *context.game;
  const auto &resources = context.resources;
  const auto node_count = game.nodes().size();
  const auto entry_count = context.entries.size();
  const auto group_count = flops.size();

  // Per group: combo preimages of every image; total compatible weight per combo.
  std::vector<std::vector<std::array<std::uint16_t, combo_total>>> sources(group_count);
  std::vector<double> total_weight(combo_total, 0.0);
  for (std::size_t index = 0; index < group_count; ++index) {
    const auto &values = *flops[index];
    if (values.compatible.size() != combo_total || values.images.empty()) {
      return Outcome::failure(KernelError::InvalidInput);
    }
    for (std::uint8_t hero = 0; hero < 2U; ++hero) {
      for (auto mode : {response_mode, average_mode}) {
        if (values.entry_values[hero][mode].size() != entry_count) {
          return Outcome::failure(KernelError::InvalidInput);
        }
        for (const auto &entry : values.entry_values[hero][mode]) {
          if (entry.size() != combo_total) {
            return Outcome::failure(KernelError::InvalidInput);
          }
        }
      }
    }
    for (const auto &permutation : values.images) {
      sources[index].push_back(combo_preimage(permutation));
    }
    for (const auto &source : sources[index]) {
      for (std::size_t combo = 0; combo < combo_total; ++combo) {
        if (values.compatible[source[combo]] != 0U) {
          total_weight[combo] += values.weight;
        }
      }
    }
  }
  if (exact) {
    for (std::size_t combo = 1; combo < combo_total; ++combo) {
      if (std::abs(total_weight[combo] - total_weight[0]) > 1e-9 * std::max(1.0, total_weight[0])) {
        return Outcome::failure(KernelError::InvalidInput);
      }
    }
  }
  bool equal_weights = !exact;
  for (std::size_t index = 0; index < group_count; ++index) {
    if (std::abs(flops[index]->weight - flops[0]->weight) >
        1e-12 * std::max(1.0, flops[0]->weight)) {
      equal_weights = false;
    }
  }
  std::vector<std::size_t> all_groups(group_count);
  for (std::size_t index = 0; index < group_count; ++index) {
    all_groups[index] = index;
  }

  BestResponseReport report;
  report.flops = static_cast<std::uint32_t>(group_count);
  for (const auto *values : flops) {
    report.boards += values->boards;
  }
  const auto &preflop = context.preflop;
  StreetEvaluator evaluator(game, context.policies);

  // Entry leaf values from a subset of the groups: for every combo the
  // weighted mean of the flop values over the compatible images of the subset.
  const auto entry_leaves = [&](const std::uint8_t hero, const std::size_t mode,
                                const std::vector<std::size_t> &subset, NodeVectors &leaf_values) {
    std::vector<double> subset_weight(combo_total, 0.0);
    for (const auto index : subset) {
      const auto &values = *flops[index];
      for (const auto &source : sources[index]) {
        for (std::size_t combo = 0; combo < combo_total; ++combo) {
          if (values.compatible[source[combo]] != 0U) {
            subset_weight[combo] += values.weight;
          }
        }
      }
    }
    for (std::size_t entry = 0; entry < entry_count; ++entry) {
      auto &target = leaf_values[context.entries[entry]];
      target.assign(combo_total, 0.0);
      for (const auto index : subset) {
        const auto &values = *flops[index];
        const auto &entry_values = values.entry_values[hero][mode][entry];
        for (const auto &source : sources[index]) {
          for (std::size_t combo = 0; combo < combo_total; ++combo) {
            const auto preimage = source[combo];
            if (values.compatible[preimage] != 0U && subset_weight[combo] > 0.0) {
              target[combo] += entry_values[preimage] * values.weight / subset_weight[combo];
            }
          }
        }
      }
    }
  };

  // Hero reach at every preflop leaf under a fixed preflop policy: the
  // recorded best-response choice, or the average strategy.
  const auto hero_reach_under = [&](const std::uint8_t hero,
                                    const std::vector<std::vector<std::uint8_t>> *choice,
                                    NodeVectors &hero_reach) {
    hero_reach.assign(node_count, {});
    std::vector<double> unit(combo_total, 1.0);
    std::function<void(std::uint32_t, const std::vector<double> &)> descend;
    descend = [&](const std::uint32_t node, const std::vector<double> &reach) {
      const auto &entry = game.nodes()[node];
      if (entry.kind != NodeKind::Decision) {
        hero_reach[node] = reach;
        return;
      }
      const auto edges = game.edges_of(node);
      if (entry.actor != hero) {
        for (const auto &edge : edges) {
          descend(edge.child, reach);
        }
        return;
      }
      std::vector<double> child(combo_total);
      for (std::size_t action = 0; action < edges.size(); ++action) {
        for (std::size_t combo = 0; combo < combo_total; ++combo) {
          double probability = 0.0;
          if (choice != nullptr) {
            probability = (*choice)[node][combo] == action ? 1.0 : 0.0;
          } else {
            probability = context.policies.row(node, preflop.rows[combo])[action];
          }
          child[combo] = reach[combo] * probability;
        }
        descend(edges[action].child, child);
      }
    };
    descend(game.root(), unit);
  };

  std::vector<double> opponent_count;
  std::vector<double> disjoint_mass;
  for (std::uint8_t hero = 0; hero < 2U; ++hero) {
    const auto opponent = static_cast<std::uint8_t>(1U - hero);
    fold_mass_universe(preflop, context.allowed[opponent], opponent_count);
    double hero_hands = 0.0;
    for (const auto allowed : context.allowed[hero]) {
      hero_hands += allowed;
    }
    const double hero_scale = hero_hands > 0.0 ? 1.0 / hero_hands : 0.0;

    // Preflop terminals: fold and all-in, exact and common to every group.
    NodeVectors terminals(node_count);
    for (std::size_t node = 0; node < node_count; ++node) {
      const auto &reach = context.preflop_leaf_reach[hero][node];
      if (reach.empty()) {
        continue;
      }
      const auto &entry = game.nodes()[node];
      if (entry.kind == NodeKind::Chance) {
        continue;
      }
      auto &target = terminals[node];
      target.assign(combo_total, 0.0);
      std::array<double, 3> payoffs{};
      const double fold_payoff = terminal_payoff(game, entry, hero, payoffs);
      if (entry.kind == NodeKind::TerminalFold) {
        fold_mass_universe(preflop, reach, disjoint_mass);
        for (std::size_t combo = 0; combo < combo_total; ++combo) {
          target[combo] = opponent_count[combo] > 0.0
                              ? fold_payoff * disjoint_mass[combo] / opponent_count[combo]
                              : 0.0;
        }
        continue;
      }
      for (std::uint16_t combo = 0; combo < combo_total; ++combo) {
        if (opponent_count[combo] <= 0.0) {
          continue;
        }
        double value = 0.0;
        for (std::uint16_t other = 0; other < combo_total; ++other) {
          if (other == combo || reach[other] == 0.0 || !disjoint(cards_of(combo), cards_of(other))) {
            continue;
          }
          const auto outcome = resources.all_in->outcome(combo, other);
          const auto total = static_cast<double>(outcome.total());
          value += reach[other] *
                   (payoffs[0] * outcome.wins + payoffs[1] * outcome.ties +
                    payoffs[2] * outcome.losses) /
                   total;
        }
        target[combo] = value / opponent_count[combo];
      }
    }

    // Value of the hero under a fixed hero reach at the preflop leaves.
    const auto value_under = [&](const NodeVectors &hero_reach, const NodeVectors &leaf_values) {
      double total = 0.0;
      for (std::size_t node = 0; node < node_count; ++node) {
        if (hero_reach[node].empty() || leaf_values[node].empty()) {
          continue;
        }
        for (std::size_t combo = 0; combo < combo_total; ++combo) {
          total += context.allowed[hero][combo] * hero_reach[node][combo] * leaf_values[node][combo];
        }
      }
      return total * hero_scale;
    };

    std::vector<std::vector<std::uint8_t>> choice(node_count);
    for (auto mode : {response_mode, average_mode}) {
      NodeVectors leaf_values = terminals;
      entry_leaves(hero, mode, all_groups, leaf_values);
      std::vector<double> root_values;
      evaluator.value(game.root(), preflop, hero, mode, leaf_values, root_values,
                      mode == response_mode ? &choice : nullptr);
      double total = 0.0;
      for (std::size_t combo = 0; combo < combo_total; ++combo) {
        total += context.allowed[hero][combo] * root_values[combo];
      }
      const double value = total * hero_scale;
      if (mode == response_mode) {
        report.best_response[hero] = value;
      } else {
        report.ev[hero] = value;
      }

      // Standard error over equally weighted groups: per-group value of the
      // hero policy fixed by the evaluation above.
      if (equal_weights && group_count > 1U) {
        NodeVectors hero_reach;
        hero_reach_under(hero, mode == response_mode ? &choice : nullptr, hero_reach);
        const double groups_count = static_cast<double>(group_count);
        std::vector<double> per_group(group_count, 0.0);
        for (std::size_t index = 0; index < group_count; ++index) {
          const auto &values = *flops[index];
          double sum = 0.0;
          for (std::size_t entry = 0; entry < entry_count; ++entry) {
            const auto node = context.entries[entry];
            const auto &entry_values = values.entry_values[hero][mode][entry];
            for (const auto &source : sources[index]) {
              for (std::size_t combo = 0; combo < combo_total; ++combo) {
                const auto preimage = source[combo];
                if (values.compatible[preimage] == 0U || total_weight[combo] <= 0.0) {
                  continue;
                }
                sum += context.allowed[hero][combo] * hero_reach[node][combo] *
                       entry_values[preimage] * values.weight / total_weight[combo];
              }
            }
          }
          per_group[index] = groups_count * sum * hero_scale;
        }
        double mean = 0.0;
        for (const auto value_of_group : per_group) {
          mean += value_of_group;
        }
        mean /= groups_count;
        double variance = 0.0;
        for (const auto value_of_group : per_group) {
          variance += (value_of_group - mean) * (value_of_group - mean);
        }
        variance /= (groups_count - 1.0);
        const double standard_error = std::sqrt(variance / groups_count);
        if (mode == response_mode) {
          report.best_response_standard_error[hero] = standard_error;
        } else {
          report.ev_standard_error[hero] = standard_error;
        }
      }
    }

    // Lower bound without selection: the hero follows the average strategy
    // at the preflop and best-responds from the flop on.
    {
      NodeVectors leaf_values = terminals;
      entry_leaves(hero, response_mode, all_groups, leaf_values);
      NodeVectors hero_reach;
      hero_reach_under(hero, nullptr, hero_reach);
      report.best_response_lower[hero] = value_under(hero_reach, leaf_values);
    }
    report.gain[hero] = report.best_response[hero] - report.ev[hero];
    report.gain_lower[hero] = report.best_response_lower[hero] - report.ev[hero];
  }
  const auto worst = report.gain[0] >= report.gain[1] ? 0U : 1U;
  report.max_gain = report.gain[worst];
  report.max_gain_lower = std::max(report.gain_lower[0], report.gain_lower[1]);
  report.max_gain_half_width =
      1.96 * std::sqrt(report.best_response_standard_error[worst] *
                           report.best_response_standard_error[worst] +
                       report.ev_standard_error[worst] * report.ev_standard_error[worst]);
  report.nashconv = report.gain[0] + report.gain[1];
  report.seconds = std::chrono::duration<double>(Clock::now() - started).count();
  return Outcome::success(report);
}

Result<BestResponseReport, KernelError>
evaluate_best_response(const CompiledGame &game, const BucketPolicy &average,
                       const BestResponseResources &resources,
                       const std::vector<FlopGroup> &groups, const BestResponseOptions &options) {
  using Outcome = Result<BestResponseReport, KernelError>;
  const auto started = Clock::now();
  if (groups.empty()) {
    return Outcome::failure(KernelError::MissingTable);
  }
  auto evaluator = BestResponseEvaluator::create(game, average, resources, options.hand_subsets);
  if (!evaluator) {
    return Outcome::failure(evaluator.error());
  }
  std::vector<FlopValues> values(groups.size());
  std::atomic<bool> failed{false};
  run_parallel(options.threads, groups.size(), [&](const std::size_t index) {
    auto result = evaluator.value().evaluate_flop(groups[index]);
    if (!result) {
      failed.store(true);
      return;
    }
    values[index] = std::move(result.value());
  });
  if (failed.load()) {
    return Outcome::failure(KernelError::InvalidInput);
  }
  std::vector<const FlopValues *> pointers;
  pointers.reserve(values.size());
  for (const auto &entry : values) {
    pointers.push_back(&entry);
  }
  auto report = evaluator.value().aggregate(pointers, false);
  if (!report) {
    return report;
  }
  report.value().seconds = std::chrono::duration<double>(Clock::now() - started).count();
  return report;
}

} // namespace gtosd::preflop_blueprint
