#include "gtosd/preflop_blueprint/best_response.hpp"
#include "gtosd/preflop_blueprint/action_labels.hpp"
#include "gtosd/preflop_blueprint/history_bucket_rows.hpp"

#include "gtosd/card_abstraction/card_abstraction.hpp"
#include "gtosd/card_abstraction/combinatorics.hpp"
#include "gtosd/card_abstraction/showdown_counts.hpp"
#include "gtosd/preflop_blueprint/kernels.hpp"
#include "gtosd/preflop_blueprint/river_engine.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <emmintrin.h>
#include <functional>
#include <map>
#include <memory>
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

static_assert(
    static_cast<unsigned>(DeviationStreet::Preflop) == static_cast<unsigned>(Street::Preflop) &&
        static_cast<unsigned>(DeviationStreet::Flop) == static_cast<unsigned>(Street::Flop) &&
        static_cast<unsigned>(DeviationStreet::Turn) == static_cast<unsigned>(Street::Turn) &&
        static_cast<unsigned>(DeviationStreet::River) == static_cast<unsigned>(Street::River) &&
        static_cast<unsigned>(DeviationStreet::None) > static_cast<unsigned>(Street::River),
    "deviation streets follow the street order, None after the river");

// Rule of the street-restricted responder at its own decisions on `street`:
// the maximum (response_mode) from its first deviating street on, the average
// strategy (average_mode) before it.
std::size_t street_rule(const DeviationStreet deviation_from, const Street street) noexcept {
  return static_cast<unsigned>(street) >= static_cast<unsigned>(deviation_from) ? response_mode
                                                                                 : average_mode;
}

// Live combos for a board prefix with their policy rows at one street.
struct Universe {
  std::vector<std::uint16_t> combos;
  std::vector<std::array<std::uint8_t, 2>> cards;
  std::array<std::uint16_t, combo_total> index{};
  std::vector<std::uint32_t> rows;
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
                        const Result<ca::CanonicalLookup, CardError> &lookup,
                        const ClassBucketRows *class_rows) {
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
    universe.rows[hand] =
        class_rows == nullptr
            ? bucket
            : class_rows->row(table.street(), ca::combo_table().hand_class[universe.combos[hand]],
                              bucket);
    if (universe.rows[hand] == ca::no_bucket) {
      return false;
    }
  }
  return true;
}

// MonkerSolver-style rows, as BoardContext::build assigns them: the bucket is
// read at the canonical index of the board (the row of the bucket table), the
// class of the board is the texture class of that index (the index itself
// without a texture) and the row class * groups + bucket. The rows are
// 32-bit and every value is a real row (no_bucket among them), so validity
// is checked on the bucket instead.
bool assign_board_class_rows(Universe &universe, const ca::BucketTable &table,
                             const Result<ca::CanonicalLookup, CardError> &lookup,
                             const BoardClassRows &board_class_rows) {
  if (!lookup) {
    return false;
  }
  const auto canonical_index = lookup.value().index;
  const auto board_class = board_class_rows.board_class(table.street(), canonical_index);
  const auto &permutation = lookup.value().permutation;
  for (std::size_t hand = 0; hand < universe.size(); ++hand) {
    const auto first = ca::permute_card(CardId::from_index(universe.cards[hand][0]).value(), permutation);
    const auto second =
        ca::permute_card(CardId::from_index(universe.cards[hand][1]).value(), permutation);
    const auto bucket = table.bucket(canonical_index, ca::combo_index(first, second));
    if (bucket == ca::no_bucket || bucket >= table.capacity()) {
      return false;
    }
    universe.rows[hand] = board_class_rows.row(table.street(), board_class, bucket);
  }
  return true;
}

// Rows of a flop or turn universe with the street abstraction of the
// resources (history rows are assigned by assign_history_prefix).
bool assign_street_rows(Universe &universe, const BestResponseResources &resources,
                        const ca::BucketTable &table,
                        const Result<ca::CanonicalLookup, CardError> &lookup) {
  return resources.board_class_rows != nullptr
             ? assign_board_class_rows(universe, table, lookup, *resources.board_class_rows)
             : assign_bucket_rows(universe, table, lookup, resources.class_rows);
}

bool assign_history_prefix(Universe &universe, const BestResponseResources &resources,
                           const std::array<CardId, 3> &flop,
                           const std::optional<CardId> turn = std::nullopt) {
  for (std::size_t hand = 0; hand < universe.size(); ++hand) {
    const std::array<CardId, 2> cards{CardId::from_index(universe.cards[hand][0]).value(),
                                      CardId::from_index(universe.cards[hand][1]).value()};
    const auto fb = ca::lookup_flop_bucket(*resources.catalog, *resources.flop, flop, cards);
    if (!fb)
      return false;
    std::uint16_t tb = ca::no_bucket;
    if (turn) {
      const auto found =
          ca::lookup_turn_bucket(*resources.catalog, *resources.turn, flop, *turn, cards);
      if (!found)
        return false;
      tb = found.value().bucket;
    }
    universe.rows[hand] = resources.history_rows->row(
        turn ? Street::Turn : Street::Flop, ca::combo_table().hand_class[universe.combos[hand]],
        fb.value().bucket, tb, ca::no_bucket);
    if (universe.rows[hand] == no_history_row)
      return false;
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
                                            const std::uint32_t row_index) const {
    return average->row(node, row_index);
  }
};

// Values per node for one universe; nodes untouched stay empty.
using NodeVectors = std::vector<std::vector<double>>;

class StreetEvaluator {
public:
  StreetEvaluator(const CompiledGame &game, const Policies &policies, NodeProbe *probe = nullptr)
      : game_(game), policies_(policies), probe_(probe),
        propagate_scratch_(static_cast<std::size_t>(game.stats().maximum_depth) + 2U),
        value_scratch_(static_cast<std::size_t>(game.stats().maximum_depth) + 2U) {}

  // Records the opponent reach of the probed node (scaled to a probability
  // over the allowed opponent hands disjoint from every combo).
  void record_reach(const std::uint32_t node, const Universe &universe, const std::uint8_t hero,
                    const std::vector<double> &reach, const std::vector<double> &allowed_opponent) const {
    if (probe_ == nullptr || node != probe_->node || hero != probe_->hero) {
      return;
    }
    std::vector<double> restricted(universe.size(), 0.0);
    for (std::size_t hand = 0; hand < universe.size(); ++hand) {
      restricted[hand] = allowed_opponent[universe.combos[hand]];
    }
    std::vector<double> opponent_count;
    fold_mass_universe(universe, restricted, opponent_count);
    std::vector<double> disjoint_mass;
    fold_mass_universe(universe, reach, disjoint_mass);
    probe_->opponent_mass.assign(combo_total, 0.0);
    probe_->opponent_reach.assign(combo_total, 0.0);
    for (std::size_t hand = 0; hand < universe.size(); ++hand) {
      probe_->opponent_mass[universe.combos[hand]] =
          opponent_count[hand] > 0.0 ? disjoint_mass[hand] / opponent_count[hand] : 0.0;
      probe_->opponent_reach[universe.combos[hand]] = reach[hand];
    }
    probe_->found = true;
  }

  // Propagates the opponent reach from `node` through the decision nodes of
  // its street and records the reach at every leaf (chance node or terminal).
  void propagate(const std::uint32_t node, const Universe &universe, const std::uint8_t hero,
                 const std::vector<double> &reach, NodeVectors &leaf_reach,
                 const std::uint32_t depth = 0U) {
    const auto &entry = game_.nodes()[node];
    if (probe_ != nullptr && node == probe_->node && hero == probe_->hero && allowed_ != nullptr) {
      record_reach(node, universe, hero, reach, *allowed_);
    }
    if (entry.kind != NodeKind::Decision) {
      leaf_reach[node] = reach;
      return;
    }
    const auto edges = game_.edges_of(node);
    if (entry.actor == hero) {
      for (const auto &edge : edges) {
        propagate(edge.child, universe, hero, reach, leaf_reach, depth + 1U);
      }
      return;
    }
    if (propagate_scratch_.size() <= depth)
      propagate_scratch_.resize(static_cast<std::size_t>(depth) + 1U);
    auto &child = propagate_scratch_[depth];
    child.resize(universe.size());
    for (std::size_t action = 0; action < edges.size(); ++action) {
      for (std::size_t hand = 0; hand < universe.size(); ++hand) {
        child[hand] = reach[hand] == 0.0
                          ? 0.0
                          : reach[hand] * policies_.row(node, universe.rows[hand])[action];
      }
      propagate(edges[action].child, universe, hero, child, leaf_reach, depth + 1U);
    }
  }

  // Value of the subtree of `node` within its street from the leaf values,
  // taking the maximum (response) or the average strategy (average) at the
  // hero's decisions. Records the response choice when `choice` is given.
  void value(const std::uint32_t node, const Universe &universe, const std::uint8_t hero,
             const std::size_t mode, const NodeVectors &leaf_values, std::vector<double> &out,
             std::vector<std::vector<std::uint8_t>> *choice,
             const std::uint32_t depth = 0U) {
    const auto &entry = game_.nodes()[node];
    if (entry.kind != NodeKind::Decision) {
      out = leaf_values[node];
      if (out.size() != universe.size()) {
        out.assign(universe.size(), 0.0);
      }
      return;
    }
    const auto edges = game_.edges_of(node);
    if (entry.actor != hero) {
      out.assign(universe.size(), 0.0);
      if (value_scratch_.size() <= depth)
        value_scratch_.resize(static_cast<std::size_t>(depth) + 1U);
      auto &child = value_scratch_[depth];
      for (const auto &edge : edges) {
        value(edge.child, universe, hero, mode, leaf_values, child, choice, depth + 1U);
        for (std::size_t hand = 0; hand < universe.size(); ++hand) {
          out[hand] += child[hand];
        }
      }
      return;
    }
    if (choice != nullptr) {
      (*choice)[node].assign(universe.size(), 0U);
    }
    if (value_scratch_.size() <= depth)
      value_scratch_.resize(static_cast<std::size_t>(depth) + 1U);
    auto &child = value_scratch_[depth];
    const bool record_actions =
        probe_ != nullptr && node == probe_->node && hero == probe_->hero && mode == average_mode;
    if (record_actions)
      probe_->action_values.assign(edges.size(), std::vector<double>(combo_total, 0.0));
    if (mode == response_mode) {
      value(edges[0].child, universe, hero, mode, leaf_values, out, choice, depth + 1U);
      for (std::size_t action = 1; action < edges.size(); ++action) {
        value(edges[action].child, universe, hero, mode, leaf_values, child, choice, depth + 1U);
        for (std::size_t hand = 0; hand < universe.size(); ++hand) {
          if (child[hand] > out[hand]) {
            out[hand] = child[hand];
            if (choice != nullptr)
              (*choice)[node][hand] = static_cast<std::uint8_t>(action);
          }
        }
      }
      return;
    }
    out.assign(universe.size(), 0.0);
    for (std::size_t action = 0; action < edges.size(); ++action) {
      value(edges[action].child, universe, hero, mode, leaf_values, child, choice, depth + 1U);
      for (std::size_t hand = 0; hand < universe.size(); ++hand) {
        if (record_actions)
          probe_->action_values[action][universe.combos[hand]] = child[hand];
        const auto probabilities = policies_.row(node, universe.rows[hand]);
        out[hand] += probabilities[action] * child[hand];
      }
    }
  }

  void set_allowed_opponent(const std::vector<double> *allowed) noexcept { allowed_ = allowed; }

private:
  const CompiledGame &game_;
  const Policies &policies_;
  NodeProbe *probe_{nullptr};
  const std::vector<double> *allowed_{nullptr};
  std::vector<std::vector<double>> propagate_scratch_;
  std::vector<std::vector<double>> value_scratch_;
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

// Joint river pass of one flop group (RiverEngine::Joint). For every board of
// a turn it evaluates each turn->river subtree once for both heroes and both
// modes, and each turn or flop all-in runout with one pair sweep, then adds
// the values to the leaf accumulators with the expressions of the reference
// loop in evaluate_flop_group, board after board in the same order: every
// accumulator entry receives the same additions of the same doubles, so the
// flop values are bit-identical to the reference path.
class JointRivers {
public:
  JointRivers(const BestResponseEvaluator::Impl &context, const Universe &flop_universe,
              const std::vector<double> &flop_weight,
              const std::array<NodeVectors, 2> &flop_leaf_reach,
              std::array<std::array<NodeVectors, 2>, 2> &flop_leaf_values)
      : context_(context), flop_universe_(flop_universe), flop_weight_(flop_weight),
        flop_leaf_reach_(flop_leaf_reach), flop_leaf_values_(flop_leaf_values),
        traversal_(*context.game, *context.average) {
    tables_.catalog = context.resources.catalog;
    tables_.flop = context.resources.flop;
    tables_.turn = context.resources.turn;
    tables_.river = context.resources.river;
    tables_.class_rows = context.resources.class_rows;
    tables_.history_rows = context.resources.history_rows;
    tables_.board_class_rows = context.resources.board_class_rows;
    const auto &nodes = context.game->nodes();
    for (std::uint32_t node = 0; node < nodes.size(); ++node) {
      const auto lanes = present_lanes(flop_leaf_reach, node);
      if (lanes != 0U && nodes[node].kind == NodeKind::TerminalShowdown) {
        flop_all_ins_.push_back({node, lanes});
      }
    }
  }

  // Adds the rivers of one turn to its leaves (river chance nodes and all-in
  // runouts) and to the flop all-in runouts.
  [[nodiscard]] bool accumulate_turn(const std::array<CardId, 3> &flop, const CardId turn,
                                     const Universe &turn_universe,
                                     const std::vector<double> &turn_weight,
                                     const std::array<NodeVectors, 2> &turn_leaf_reach,
                                     std::array<std::array<NodeVectors, 2>, 2> &turn_leaf_values,
                                     const std::vector<const WeightedBoard *> &boards) {
    // The prefix repeats the lookups that BoardContext::build makes with each
    // board's own flop, which is in increasing order (any other order is
    // rejected), and RiverBoard::assign requires the two flops to match:
    // sorting keeps a group flop listed in another order working as in the
    // reference.
    std::array<CardId, 3> sorted_flop = flop;
    std::sort(sorted_flop.begin(), sorted_flop.end());
    if (!prefix_.assign(sorted_flop, turn, tables_)) {
      return false;
    }
    const auto &game = *context_.game;
    chance_leaves_.clear();
    all_in_leaves_.clear();
    for (std::uint32_t node = 0; node < game.nodes().size(); ++node) {
      const auto lanes = present_lanes(turn_leaf_reach, node);
      const auto kind = game.nodes()[node].kind;
      if (lanes == 0U || kind == NodeKind::TerminalFold) {
        continue;
      }
      (kind == NodeKind::Chance ? chance_leaves_ : all_in_leaves_).push_back({node, lanes});
    }
    for (const auto *board : boards) {
      if (!board_.assign(board->history, *context_.resources.ranks, &prefix_)) {
        return false;
      }
      prepare_board(turn_universe, turn_weight);
      for (const auto &leaf : chance_leaves_) {
        restrict_pair(turn_leaf_reach, leaf, turn_index_);
        if (!traversal_.evaluate(game.edges_of(leaf.node)[0].child, board_, reach_, response_,
                                 average_)) {
          return false;
        }
        add_river_values(leaf, board->weight, turn_leaf_values);
      }
      for (const auto &leaf : all_in_leaves_) {
        restrict_pair(turn_leaf_reach, leaf, turn_index_);
        add_showdown_values(leaf, board->weight, turn_shares_, turn_leaf_values);
      }
      for (const auto &leaf : flop_all_ins_) {
        restrict_pair(flop_leaf_reach_, leaf, flop_index_);
        add_showdown_values(leaf, board->weight, flop_shares_, flop_leaf_values_);
      }
    }
    return true;
  }

private:
  // A leaf and the heroes (bit h for hero h) whose leaf vector exists: the
  // reference visits the leaf for those heroes only.
  struct Leaf {
    std::uint32_t node{no_node};
    unsigned lanes{0U};
  };
  // A live hand whose accumulator entry `index` the reference updates for the
  // heroes in `lanes`, with each hero's denominator (universe weight times
  // opponent count); unused lanes hold 1.0.
  struct Share {
    std::uint16_t hand{0U};
    std::uint16_t index{no_hand};
    unsigned lanes{0U};
    std::array<double, hero_lanes> denominator{1.0, 1.0};
  };

  static unsigned present_lanes(const std::array<NodeVectors, 2> &leaf_reach,
                                const std::size_t node) {
    unsigned lanes = 0U;
    for (std::size_t hero = 0; hero < hero_lanes; ++hero) {
      if (!leaf_reach[hero][node].empty()) {
        lanes |= 1U << hero;
      }
    }
    return lanes;
  }

  // Board inputs of the reference loop: the universe index of every live
  // hand, the opponent counts of both heroes (restrict_to_board of the
  // allowed opponent hands, then fold_mass) and the accumulation shares.
  void prepare_board(const Universe &turn_universe, const std::vector<double> &turn_weight) {
    const auto combos = board_.combo_ids();
    for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
      turn_index_[hand] = turn_universe.index[combos[hand]];
      flop_index_[hand] = flop_universe_.index[combos[hand]];
      const auto preflop_index = context_.preflop.index[combos[hand]];
      for (std::size_t hero = 0; hero < hero_lanes; ++hero) {
        reach_[hero_lanes * hand + hero] =
            preflop_index == no_hand ? 0.0 : context_.allowed[1U - hero][preflop_index];
      }
    }
    fold_mass_pair(board_, reach_, river_count_);
    collect_shares(turn_index_, turn_weight, turn_shares_);
    collect_shares(flop_index_, flop_weight_, flop_shares_);
  }

  // The hands the reference skips: outside the universe, without opponent
  // mass or without universe weight.
  void collect_shares(const std::array<std::uint16_t, live_hand_count> &index,
                      const std::vector<double> &weight, std::vector<Share> &shares) const {
    shares.clear();
    for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
      if (index[hand] == no_hand) {
        continue;
      }
      Share share;
      share.hand = static_cast<std::uint16_t>(hand);
      share.index = index[hand];
      const double universe_weight = weight[index[hand]];
      for (std::size_t hero = 0; hero < hero_lanes; ++hero) {
        const double count = river_count_[hero_lanes * hand + hero];
        if (count <= 0.0 || universe_weight <= 0.0) {
          continue;
        }
        share.lanes |= 1U << hero;
        share.denominator[hero] = universe_weight * count;
      }
      if (share.lanes != 0U) {
        shares.push_back(share);
      }
    }
  }

  // restrict_to_board of both heroes' leaf reach, in the pair layout; a hero
  // without the leaf gets zero reach and no accumulation.
  void restrict_pair(const std::array<NodeVectors, 2> &leaf_reach, const Leaf &leaf,
                     const std::array<std::uint16_t, live_hand_count> &index) {
    for (std::size_t hero = 0; hero < hero_lanes; ++hero) {
      if (((leaf.lanes >> hero) & 1U) == 0U) {
        for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
          reach_[hero_lanes * hand + hero] = 0.0;
        }
        continue;
      }
      const auto &source = leaf_reach[hero][leaf.node];
      for (std::size_t hand = 0; hand < live_hand_count; ++hand) {
        reach_[hero_lanes * hand + hero] = index[hand] == no_hand ? 0.0 : source[index[hand]];
      }
    }
  }

  // River chance leaf: target[index] += value * weight / denominator, per
  // mode, with the river values of the joint traversal.
  void add_river_values(const Leaf &leaf, const double board_weight,
                        std::array<std::array<NodeVectors, 2>, 2> &leaf_values) {
    const __m128d weight = _mm_set1_pd(board_weight);
    std::array<double, hero_lanes> response{};
    std::array<double, hero_lanes> average{};
    for (const auto &share : turn_shares_) {
      const unsigned lanes = share.lanes & leaf.lanes;
      if (lanes == 0U) {
        continue;
      }
      const std::size_t offset = hero_lanes * share.hand;
      const __m128d denominator = _mm_loadu_pd(share.denominator.data());
      _mm_storeu_pd(response.data(),
                    _mm_div_pd(_mm_mul_pd(_mm_loadu_pd(response_.data() + offset), weight),
                               denominator));
      _mm_storeu_pd(average.data(),
                    _mm_div_pd(_mm_mul_pd(_mm_loadu_pd(average_.data() + offset), weight),
                               denominator));
      for (std::size_t hero = 0; hero < hero_lanes; ++hero) {
        if (((lanes >> hero) & 1U) != 0U) {
          leaf_values[hero][response_mode][leaf.node][share.index] += response[hero];
          leaf_values[hero][average_mode][leaf.node][share.index] += average[hero];
        }
      }
    }
  }

  // Turn or flop all-in runout: payoffs[0] * worse + payoffs[1] * tied +
  // payoffs[2] * better, weighted as a river value and added to both modes.
  void add_showdown_values(const Leaf &leaf, const double board_weight,
                           const std::vector<Share> &shares,
                           std::array<std::array<NodeVectors, 2>, 2> &leaf_values) {
    showdown_masses_pair(board_, reach_, worse_, tied_, better_);
    const auto &game = *context_.game;
    std::array<std::array<double, 3>, hero_lanes> payoffs{};
    for (std::uint8_t hero = 0; hero < hero_lanes; ++hero) {
      terminal_payoff(game, game.nodes()[leaf.node], hero, payoffs[hero]);
    }
    const __m128d win = _mm_set_pd(payoffs[1][0], payoffs[0][0]);
    const __m128d tie = _mm_set_pd(payoffs[1][1], payoffs[0][1]);
    const __m128d lose = _mm_set_pd(payoffs[1][2], payoffs[0][2]);
    const __m128d weight = _mm_set1_pd(board_weight);
    std::array<double, hero_lanes> share_value{};
    for (const auto &share : shares) {
      const unsigned lanes = share.lanes & leaf.lanes;
      if (lanes == 0U) {
        continue;
      }
      const std::size_t offset = hero_lanes * share.hand;
      const __m128d value =
          _mm_add_pd(_mm_add_pd(_mm_mul_pd(win, _mm_loadu_pd(worse_.data() + offset)),
                                _mm_mul_pd(tie, _mm_loadu_pd(tied_.data() + offset))),
                     _mm_mul_pd(lose, _mm_loadu_pd(better_.data() + offset)));
      _mm_storeu_pd(share_value.data(),
                    _mm_div_pd(_mm_mul_pd(value, weight), _mm_loadu_pd(share.denominator.data())));
      for (std::size_t hero = 0; hero < hero_lanes; ++hero) {
        if (((lanes >> hero) & 1U) != 0U) {
          leaf_values[hero][response_mode][leaf.node][share.index] += share_value[hero];
          leaf_values[hero][average_mode][leaf.node][share.index] += share_value[hero];
        }
      }
    }
  }

  const BestResponseEvaluator::Impl &context_;
  const Universe &flop_universe_;
  const std::vector<double> &flop_weight_;
  const std::array<NodeVectors, 2> &flop_leaf_reach_;
  std::array<std::array<NodeVectors, 2>, 2> &flop_leaf_values_;
  AbstractionTables tables_{};
  RiverPrefix prefix_;
  RiverBoard board_;
  JointRiverTraversal traversal_;
  std::vector<Leaf> flop_all_ins_;
  std::vector<Leaf> chance_leaves_;
  std::vector<Leaf> all_in_leaves_;
  std::array<std::uint16_t, live_hand_count> turn_index_{};
  std::array<std::uint16_t, live_hand_count> flop_index_{};
  std::vector<Share> turn_shares_;
  std::vector<Share> flop_shares_;
  std::array<double, pair_values> reach_{};
  std::array<double, pair_values> response_{};
  std::array<double, pair_values> average_{};
  std::array<double, pair_values> worse_{};
  std::array<double, pair_values> tied_{};
  std::array<double, pair_values> better_{};
  std::array<double, pair_values> river_count_{};
};

FlopValues evaluate_flop_group(const BestResponseEvaluator::Impl &context, const FlopGroup &group,
                               const RiverEngine river_engine,
                               const DeviationStreet deviation_from, bool &ok,
                               NodeProbe *probe = nullptr) {
  const auto &game = *context.game;
  const auto &resources = context.resources;
  const auto node_count = game.nodes().size();
  FlopValues result;
  result.flop = group.flop;
  result.weight = group.weight;
  result.boards = static_cast<std::uint32_t>(group.boards.size());
  ok = true;
  StreetEvaluator evaluator(game, context.policies, probe);
  if (probe != nullptr) {
    evaluator.set_allowed_opponent(&context.allowed[1U - probe->hero]);
  }
  const HeadsUpShowdownKernel kernel;

  std::uint64_t flop_mask = 0U;
  for (const auto card : group.flop) {
    flop_mask |= card.mask();
  }
  auto flop_universe = make_universe(flop_mask);
  if (!(resources.history_rows ? assign_history_prefix(flop_universe, resources, group.flop)
                               : assign_street_rows(flop_universe, resources, *resources.flop,
                                                    resources.catalog->lookup_flop(group.flop)))) {
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

  // Street-restricted response: a third set of values built from the leaves
  // of the two modes, never changing them. The river values it needs are the
  // response ones (the average ones for None), which both river engines
  // already compute; fold and all-in leaves are equal in both modes. Its flop
  // chance leaves accumulate the restricted turn values below, with the same
  // expression and order as the two modes, so a restriction whose rules
  // coincide with a mode (Flop, None) reproduces that mode bit for bit.
  const bool street_restricted = deviation_from != DeviationStreet::Preflop;
  const auto river_rule = street_rule(deviation_from, Street::River);
  const auto turn_rule = street_rule(deviation_from, Street::Turn);
  const auto flop_rule = street_rule(deviation_from, Street::Flop);
  std::array<NodeVectors, 2> flop_restricted_leaves{};
  if (street_restricted) {
    for (std::uint8_t hero = 0; hero < 2U; ++hero) {
      flop_restricted_leaves[hero].assign(node_count, {});
      for (std::size_t node = 0; node < node_count; ++node) {
        if (!flop_leaf_reach[hero][node].empty() && game.nodes()[node].kind == NodeKind::Chance) {
          flop_restricted_leaves[hero][node].assign(flop_universe.size(), 0.0);
        }
      }
    }
  }

  // Joint river engine: all the rivers of a turn in one call, both heroes and
  // both modes at once; the reference loop below then visits no board.
  std::unique_ptr<JointRivers> joint_rivers;
  if (river_engine == RiverEngine::Joint) {
    joint_rivers = std::make_unique<JointRivers>(context, flop_universe, flop_weight,
                                                 flop_leaf_reach, flop_leaf_values);
  }
  const std::vector<const WeightedBoard *> no_boards;
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
    if (!(resources.history_rows
              ? assign_history_prefix(turn_universe, resources, group.flop, turn)
              : assign_street_rows(turn_universe, resources, *resources.turn,
                                   resources.catalog->lookup_flop_turn(group.flop, turn)))) {
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
    if (joint_rivers &&
        !joint_rivers->accumulate_turn(group.flop, turn, turn_universe, turn_weight,
                                       turn_leaf_reach, turn_leaf_values, turn_boards)) {
      ok = false;
      return result;
    }
    const auto &reference_boards = joint_rivers ? no_boards : turn_boards;
    AbstractionTables tables;
    tables.catalog = resources.catalog;
    tables.flop = resources.flop;
    tables.turn = resources.turn;
    tables.river = resources.river;
    tables.class_rows = resources.class_rows;
    tables.history_rows = resources.history_rows;
    tables.board_class_rows = resources.board_class_rows;
    for (const auto *board : reference_boards) {
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
    // Restricted turn values: the river leaves of the river rule, the turn
    // rule at the hero's turn decisions.
    if (street_restricted) {
      for (std::uint8_t hero = 0; hero < 2U; ++hero) {
        for (std::size_t node = 0; node < node_count; ++node) {
          if (flop_leaf_reach[hero][node].empty() || game.nodes()[node].kind != NodeKind::Chance) {
            continue;
          }
          evaluator.value(game.edges_of(static_cast<std::uint32_t>(node))[0].child, turn_universe,
                          hero, turn_rule, turn_leaf_values[hero][river_rule], turn_values,
                          nullptr);
          auto &target = flop_restricted_leaves[hero][node];
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
  // Restricted entry values: the flop fold and all-in leaves of the response
  // set (equal in both modes), the flop rule at the hero's flop decisions.
  if (street_restricted) {
    for (std::uint8_t hero = 0; hero < 2U; ++hero) {
      for (std::size_t node = 0; node < node_count; ++node) {
        if (game.nodes()[node].kind != NodeKind::Chance &&
            !flop_leaf_values[hero][response_mode][node].empty()) {
          flop_restricted_leaves[hero][node] = flop_leaf_values[hero][response_mode][node];
        }
      }
      auto &per_entry = result.entry_values[hero][restricted_mode];
      per_entry.resize(context.entries.size());
      for (std::size_t entry = 0; entry < context.entries.size(); ++entry) {
        evaluator.value(game.edges_of(context.entries[entry])[0].child, flop_universe, hero,
                        flop_rule, flop_restricted_leaves[hero], flop_values, nullptr);
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

const char *river_engine_name(const RiverEngine engine) noexcept {
  switch (engine) {
  case RiverEngine::Joint:
    return "joint";
  case RiverEngine::Reference:
    return "reference";
  }
  return "unknown";
}

const char *deviation_street_name(const DeviationStreet street) noexcept {
  switch (street) {
  case DeviationStreet::Preflop:
    return "preflop";
  case DeviationStreet::Flop:
    return "flop";
  case DeviationStreet::Turn:
    return "turn";
  case DeviationStreet::River:
    return "river";
  case DeviationStreet::None:
    return "none";
  }
  return "unknown";
}

std::optional<DeviationStreet> parse_deviation_street(const std::string_view text) noexcept {
  for (const auto street : {DeviationStreet::Preflop, DeviationStreet::Flop, DeviationStreet::Turn,
                            DeviationStreet::River, DeviationStreet::None}) {
    if (text == deviation_street_name(street)) {
      return street;
    }
  }
  return std::nullopt;
}

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
  const std::array<const ca::BucketTable *, 3> row_tables{resources.flop, resources.turn,
                                                          resources.river};
  const std::array<std::uint32_t, 3> policy_counts{average.layout().flop_capacity,
                                                   average.layout().turn_capacity,
                                                   average.layout().river_capacity};
  for (std::size_t index = 0; index < row_tables.size(); ++index) {
    const auto &table = *row_tables[index];
    if (resources.class_rows != nullptr && !resources.class_rows->matches(table)) {
      return Outcome::failure(KernelError::InvalidInput);
    }
    if (resources.history_rows && (resources.class_rows || !resources.history_rows->matches(table)))
      return Outcome::failure(KernelError::InvalidInput);
    if (resources.board_class_rows &&
        (resources.class_rows || resources.history_rows ||
         !resources.board_class_rows->matches(table)))
      return Outcome::failure(KernelError::InvalidInput);
    const auto count = resources.board_class_rows ? resources.board_class_rows->count(table.street())
                       : resources.history_rows   ? resources.history_rows->count(table.street())
                       : resources.class_rows == nullptr
                           ? table.capacity()
                           : resources.class_rows->count(table.street());
    if (count != policy_counts[index]) {
      return Outcome::failure(KernelError::InvalidInput);
    }
  }
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
  auto values = evaluate_flop_group(*impl_, group, river_engine_, deviation_from_, ok);
  if (!ok) {
    return Outcome::failure(KernelError::InvalidInput);
  }
  return Outcome::success(std::move(values));
}

namespace {

using ComboSources = std::vector<std::array<std::uint16_t, combo_total>>;

// Combo preimages of every image of every group.
std::vector<ComboSources> group_sources(const std::vector<const FlopValues *> &flops) {
  std::vector<ComboSources> sources(flops.size());
  for (std::size_t index = 0; index < flops.size(); ++index) {
    for (const auto &permutation : flops[index]->images) {
      sources[index].push_back(combo_preimage(permutation));
    }
  }
  return sources;
}

// Entry leaf values from a subset of the groups: for every combo the
// weighted mean of the flop values over the compatible images of the subset.
void entry_leaves_for(const BestResponseEvaluator::Impl &context,
                      const std::vector<const FlopValues *> &flops,
                      const std::vector<ComboSources> &sources, const std::vector<std::size_t> &subset,
                      const std::uint8_t hero, const std::size_t mode, NodeVectors &leaf_values) {
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
  for (std::size_t entry = 0; entry < context.entries.size(); ++entry) {
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
}

// Preflop terminals (fold and all-in): exact and common to every group.
void preflop_terminals(const BestResponseEvaluator::Impl &context, const std::uint8_t hero,
                       const std::vector<double> &opponent_count, NodeVectors &terminals) {
  const auto &game = *context.game;
  const auto node_count = game.nodes().size();
  const auto &preflop = context.preflop;
  std::vector<double> disjoint_mass;
  terminals.assign(node_count, {});
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
        const auto outcome = context.resources.all_in->outcome(combo, other);
        const auto total = static_cast<double>(outcome.total());
        value += reach[other] *
                 (payoffs[0] * outcome.wins + payoffs[1] * outcome.ties +
                  payoffs[2] * outcome.losses) /
                 total;
      }
      target[combo] = value / opponent_count[combo];
    }
  }
}

// Opponent reach at every preflop decision node (630 entries).
void preflop_node_reach(const BestResponseEvaluator::Impl &context, const std::uint8_t hero,
                        NodeVectors &node_reach) {
  const auto &game = *context.game;
  node_reach.assign(game.nodes().size(), {});
  std::function<void(std::uint32_t, const std::vector<double> &)> descend;
  descend = [&](const std::uint32_t node, const std::vector<double> &reach) {
    const auto &entry = game.nodes()[node];
    if (entry.kind != NodeKind::Decision) {
      return;
    }
    node_reach[node] = reach;
    const auto edges = game.edges_of(node);
    if (entry.actor == hero) {
      for (const auto &edge : edges) {
        descend(edge.child, reach);
      }
      return;
    }
    std::vector<double> child(combo_total);
    for (std::size_t action = 0; action < edges.size(); ++action) {
      for (std::size_t combo = 0; combo < combo_total; ++combo) {
        child[combo] = reach[combo] == 0.0
                           ? 0.0
                           : reach[combo] * context.policies.row(node, context.preflop.rows[combo])[action];
      }
      descend(edges[action].child, child);
    }
  };
  descend(game.root(), context.allowed[1U - hero]);
}

} // namespace

Result<PreflopActionValues, KernelError>
BestResponseEvaluator::preflop_action_values(const std::vector<const FlopValues *> &flops,
                                             const std::uint8_t hero) const {
  using Outcome = Result<PreflopActionValues, KernelError>;
  if (!impl_ || flops.empty() || hero > 1U) {
    return Outcome::failure(KernelError::InvalidInput);
  }
  const auto &context = *impl_;
  const auto &game = *context.game;
  const auto &preflop = context.preflop;
  const auto node_count = game.nodes().size();
  const auto group_count = flops.size();
  const auto sources = group_sources(flops);
  const auto opponent = static_cast<std::uint8_t>(1U - hero);
  std::vector<double> opponent_count;
  fold_mass_universe(preflop, context.allowed[opponent], opponent_count);
  NodeVectors terminals;
  preflop_terminals(context, hero, opponent_count, terminals);
  NodeVectors node_reach;
  preflop_node_reach(context, hero, node_reach);
  StreetEvaluator evaluator(game, context.policies);

  PreflopActionValues result;
  result.hero = hero;
  result.groups = static_cast<std::uint32_t>(group_count);
  for (const auto &node : game.nodes()) {
    if (node.street == Street::Preflop && node.kind == NodeKind::Decision && node.actor == hero) {
      result.nodes.push_back(node.id);
    }
  }
  const auto hero_nodes = result.nodes.size();
  std::vector<std::size_t> node_slot(node_count, hero_nodes);
  for (std::size_t slot = 0; slot < hero_nodes; ++slot) {
    node_slot[result.nodes[slot]] = slot;
  }
  const auto action_count = [&](const std::size_t slot) {
    return static_cast<std::size_t>(game.nodes()[result.nodes[slot]].action_count);
  };

  // Opponent reach given the combo and per-class weights.
  const auto &table = ca::combo_table();
  result.opponent_reach.resize(hero_nodes);
  result.class_weight.assign(hero_nodes, std::vector<double>(ca::preflop_hand_classes, 0.0));
  std::vector<double> disjoint_mass;
  for (std::size_t slot = 0; slot < hero_nodes; ++slot) {
    fold_mass_universe(preflop, node_reach[result.nodes[slot]], disjoint_mass);
    result.opponent_reach[slot].assign(combo_total, 0.0);
    for (std::size_t combo = 0; combo < combo_total; ++combo) {
      const double reach = opponent_count[combo] > 0.0 ? disjoint_mass[combo] / opponent_count[combo] : 0.0;
      result.opponent_reach[slot][combo] = reach;
      result.class_weight[slot][table.hand_class[combo]] += context.allowed[hero][combo] * reach;
    }
  }

  // Values with the leaves of one subset of groups: children recorded at the
  // hero's decision nodes.
  std::vector<std::vector<std::vector<double>>> recorded(hero_nodes);
  std::function<void(std::uint32_t, const NodeVectors &, std::vector<double> &)> value;
  value = [&](const std::uint32_t node, const NodeVectors &leaves, std::vector<double> &out) {
    const auto &entry = game.nodes()[node];
    if (entry.kind != NodeKind::Decision) {
      out = leaves[node];
      if (out.size() != combo_total) {
        out.assign(combo_total, 0.0);
      }
      return;
    }
    const auto edges = game.edges_of(node);
    std::vector<std::vector<double>> children(edges.size());
    for (std::size_t action = 0; action < edges.size(); ++action) {
      value(edges[action].child, leaves, children[action]);
    }
    out.assign(combo_total, 0.0);
    if (entry.actor != hero) {
      for (const auto &child : children) {
        for (std::size_t combo = 0; combo < combo_total; ++combo) {
          out[combo] += child[combo];
        }
      }
      return;
    }
    if (node_slot[node] < hero_nodes) {
      recorded[node_slot[node]] = children;
    }
    for (std::size_t combo = 0; combo < combo_total; ++combo) {
      const auto probabilities = context.policies.row(node, preflop.rows[combo]);
      double total = 0.0;
      for (std::size_t action = 0; action < children.size(); ++action) {
        total += probabilities[action] * children[action][combo];
      }
      out[combo] = total;
    }
  };

  // Means over all groups (per-combo normalization of entry_leaves_for).
  std::vector<std::size_t> all_groups(group_count);
  for (std::size_t index = 0; index < group_count; ++index) {
    all_groups[index] = index;
  }
  {
    NodeVectors leaves = terminals;
    entry_leaves_for(context, flops, sources, all_groups, hero, average_mode, leaves);
    value(game.root(), leaves, result.root_values);
  }
  result.combo_values = recorded;
  result.class_ev.resize(hero_nodes);
  result.class_se.resize(hero_nodes);
  for (std::size_t slot = 0; slot < hero_nodes; ++slot) {
    const auto actions = action_count(slot);
    result.class_ev[slot].assign(actions, std::vector<double>(ca::preflop_hand_classes, 0.0));
    result.class_se[slot].assign(actions, std::vector<double>(ca::preflop_hand_classes, 0.0));
    for (std::size_t action = 0; action < actions; ++action) {
      for (std::size_t combo = 0; combo < combo_total; ++combo) {
        const auto hand_class = table.hand_class[combo];
        const double mass = result.opponent_reach[slot][combo];
        const double weight = context.allowed[hero][combo] * mass;
        if (weight > 0.0) {
          // Conditional EV of the combo: counterfactual value over the
          // probability that the opponent reaches the node; the class mean
          // weights combos by that probability.
          result.class_ev[slot][action][hand_class] +=
              weight * (result.combo_values[slot][action][combo] / mass);
        }
      }
      for (std::size_t hand_class = 0; hand_class < ca::preflop_hand_classes; ++hand_class) {
        const double weight = result.class_weight[slot][hand_class];
        result.class_ev[slot][action][hand_class] =
            weight > 0.0 ? result.class_ev[slot][action][hand_class] / weight : 0.0;
      }
    }
  }
  if (group_count < 2U) {
    return Outcome::success(std::move(result));
  }

  // Per-group class values for the standard errors.
  std::vector<std::vector<std::vector<std::vector<double>>>> per_group(hero_nodes);
  for (std::size_t slot = 0; slot < hero_nodes; ++slot) {
    per_group[slot].assign(action_count(slot),
                           std::vector<std::vector<double>>(ca::preflop_hand_classes,
                                                            std::vector<double>()));
  }
  for (std::size_t index = 0; index < group_count; ++index) {
    NodeVectors leaves = terminals;
    entry_leaves_for(context, flops, sources, {index}, hero, average_mode, leaves);
    std::vector<double> unused;
    value(game.root(), leaves, unused);
    std::vector<std::uint8_t> compatible(combo_total, 0U);
    for (const auto &source : sources[index]) {
      for (std::size_t combo = 0; combo < combo_total; ++combo) {
        if (flops[index]->compatible[source[combo]] != 0U) {
          compatible[combo] = 1U;
        }
      }
    }
    for (std::size_t slot = 0; slot < hero_nodes; ++slot) {
      for (std::size_t action = 0; action < action_count(slot); ++action) {
        std::vector<double> sums(ca::preflop_hand_classes, 0.0);
        std::vector<double> weights(ca::preflop_hand_classes, 0.0);
        for (std::size_t combo = 0; combo < combo_total; ++combo) {
          if (compatible[combo] == 0U) {
            continue;
          }
          const auto hand_class = table.hand_class[combo];
          const double mass = result.opponent_reach[slot][combo];
          const double weight = context.allowed[hero][combo] * mass;
          if (weight <= 0.0) {
            continue;
          }
          sums[hand_class] += weight * (recorded[slot][action][combo] / mass);
          weights[hand_class] += weight;
        }
        for (std::size_t hand_class = 0; hand_class < ca::preflop_hand_classes; ++hand_class) {
          if (weights[hand_class] > 0.0) {
            per_group[slot][action][hand_class].push_back(sums[hand_class] / weights[hand_class]);
          }
        }
      }
    }
  }
  for (std::size_t slot = 0; slot < hero_nodes; ++slot) {
    for (std::size_t action = 0; action < action_count(slot); ++action) {
      for (std::size_t hand_class = 0; hand_class < ca::preflop_hand_classes; ++hand_class) {
        const auto &series = per_group[slot][action][hand_class];
        if (series.size() < 2U) {
          continue;
        }
        double mean = 0.0;
        for (const auto entry : series) {
          mean += entry;
        }
        mean /= static_cast<double>(series.size());
        double variance = 0.0;
        for (const auto entry : series) {
          variance += (entry - mean) * (entry - mean);
        }
        variance /= static_cast<double>(series.size() - 1U);
        result.class_se[slot][action][hand_class] =
            std::sqrt(variance / static_cast<double>(series.size()));
      }
    }
  }
  return Outcome::success(std::move(result));
}

Result<NodeProbe, KernelError> BestResponseEvaluator::probe_node(const FlopGroup &group,
                                                                const std::uint32_t node,
                                                                const std::uint8_t hero) const {
  using Outcome = Result<NodeProbe, KernelError>;
  if (!impl_ || hero > 1U || node >= impl_->game->nodes().size()) {
    return Outcome::failure(KernelError::InvalidInput);
  }
  NodeProbe probe;
  probe.node = node;
  probe.hero = hero;
  bool ok = true;
  // The probe records average-mode action values only: the restricted set is
  // neither needed nor allowed to overwrite them.
  static_cast<void>(
      evaluate_flop_group(*impl_, group, river_engine_, DeviationStreet::Preflop, ok, &probe));
  if (!ok) {
    return Outcome::failure(KernelError::InvalidInput);
  }
  return Outcome::success(std::move(probe));
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
  const auto node_count = game.nodes().size();
  const auto entry_count = context.entries.size();
  const auto group_count = flops.size();
  // Best responder under the street restriction: its entry values are the
  // restricted set and its preflop rule the average strategy, since it never
  // deviates at the preflop. Without a restriction both are the full
  // response's and every expression below is the unrestricted one.
  const bool street_restricted = deviation_from_ != DeviationStreet::Preflop;
  const std::size_t responder_values = street_restricted ? restricted_mode : response_mode;
  const std::size_t preflop_rule = street_rule(deviation_from_, Street::Preflop);

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
    // Flop values evaluated without the restriction lack the restricted set.
    if (street_restricted) {
      for (std::uint8_t hero = 0; hero < 2U; ++hero) {
        if (values.entry_values[hero][restricted_mode].size() != entry_count) {
          return Outcome::failure(KernelError::InvalidInput);
        }
        for (const auto &entry : values.entry_values[hero][restricted_mode]) {
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
  report.deviation_from = deviation_from_;
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
  for (std::uint8_t hero = 0; hero < 2U; ++hero) {
    const auto opponent = static_cast<std::uint8_t>(1U - hero);
    fold_mass_universe(preflop, context.allowed[opponent], opponent_count);
    double hero_hands = 0.0;
    for (const auto allowed : context.allowed[hero]) {
      hero_hands += allowed;
    }
    const double hero_scale = hero_hands > 0.0 ? 1.0 / hero_hands : 0.0;

    NodeVectors terminals;
    preflop_terminals(context, hero, opponent_count, terminals);

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
      // The responder's pass reads its own entry values and preflop rule; a
      // restricted responder records no preflop choice.
      const auto value_set = mode == response_mode ? responder_values : mode;
      const auto rule = mode == response_mode ? preflop_rule : mode;
      NodeVectors leaf_values = terminals;
      entry_leaves(hero, value_set, all_groups, leaf_values);
      std::vector<double> root_values;
      evaluator.value(game.root(), preflop, hero, rule, leaf_values, root_values,
                      rule == response_mode ? &choice : nullptr);
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
        hero_reach_under(hero, rule == response_mode ? &choice : nullptr, hero_reach);
        const double groups_count = static_cast<double>(group_count);
        std::vector<double> per_group(group_count, 0.0);
        for (std::size_t index = 0; index < group_count; ++index) {
          const auto &values = *flops[index];
          double sum = 0.0;
          for (std::size_t entry = 0; entry < entry_count; ++entry) {
            const auto node = context.entries[entry];
            const auto &entry_values = values.entry_values[hero][value_set][entry];
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
    // at the preflop and best-responds from the flop on. A restricted
    // responder makes no preflop selection: its bound is its own value.
    if (street_restricted) {
      report.best_response_lower[hero] = report.best_response[hero];
    } else {
      NodeVectors leaf_values = terminals;
      entry_leaves(hero, response_mode, all_groups, leaf_values);
      NodeVectors hero_reach;
      hero_reach_under(hero, nullptr, hero_reach);
      report.best_response_lower[hero] = value_under(hero_reach, leaf_values);
    }
    // Preflop-only deviation: the entries are valued with the average strategy,
    // so the maximisation at the preflop sees what the blueprint will really do
    // after the flop instead of what a best responder could do there. The
    // restricted responder's preflop rule is the average strategy, so it has
    // no preflop-only deviation: the pass repeats the ev pass and gives ev.
    {
      NodeVectors leaf_values = terminals;
      entry_leaves(hero, average_mode, all_groups, leaf_values);
      std::vector<double> root_values;
      evaluator.value(game.root(), preflop, hero, preflop_rule, leaf_values, root_values,
                      nullptr);
      double total = 0.0;
      for (std::size_t combo = 0; combo < combo_total; ++combo) {
        total += context.allowed[hero][combo] * root_values[combo];
      }
      report.best_response_preflop[hero] = total * hero_scale;
    }
    // Where the best response's preflop differs from the blueprint's: the
    // response pass above left its per-combo choice in `choice`.
    {
      constexpr std::size_t class_count = 81U;
      for (std::uint32_t node = 0; node < node_count; ++node) {
        const auto &entry = game.nodes()[node];
        if (entry.kind != NodeKind::Decision || entry.street != Street::Preflop ||
            entry.actor != hero || choice[node].empty()) {
          continue;
        }
        std::vector<int> per_class(class_count, -1);
        std::vector<std::uint8_t> split(class_count, 0U);
        for (std::size_t hand = 0; hand < preflop.size(); ++hand) {
          if (context.allowed[hero][preflop.combos[hand]] == 0.0) {
            continue;
          }
          const auto hand_class = static_cast<std::size_t>(preflop.rows[hand]);
          const int action = static_cast<int>(choice[node][hand]);
          if (per_class[hand_class] < 0) {
            per_class[hand_class] = action;
          } else if (per_class[hand_class] != action) {
            split[hand_class] = 1U;
          }
        }
        BestResponseReport::PreflopChoiceMix mix;
        mix.node = node;
        mix.hero = hero;
        mix.action_count = entry.action_count;
        std::size_t seen = 0;
        for (std::size_t index = 0; index < class_count; ++index) {
          if (per_class[index] < 0) {
            continue;
          }
          ++seen;
          mix.split_classes += split[index];
          mix.frequency[static_cast<std::size_t>(per_class[index])] += 1.0;
        }
        if (seen > 0) {
          for (auto &value : mix.frequency) {
            value /= static_cast<double>(seen);
          }
        }
        report.best_response_preflop_mix.push_back(mix);
      }
      // A restricted responder follows the average strategy at the preflop and
      // left `choice` empty: its mix is the policy's, every class present
      // adding its row, and one row per class leaves no class to split.
      if (street_restricted) {
        for (std::uint32_t node = 0; node < node_count; ++node) {
          const auto &entry = game.nodes()[node];
          if (entry.kind != NodeKind::Decision || entry.street != Street::Preflop ||
              entry.actor != hero) {
            continue;
          }
          std::vector<std::uint8_t> present(class_count, 0U);
          for (std::size_t hand = 0; hand < preflop.size(); ++hand) {
            if (context.allowed[hero][preflop.combos[hand]] != 0.0) {
              present[static_cast<std::size_t>(preflop.rows[hand])] = 1U;
            }
          }
          BestResponseReport::PreflopChoiceMix mix;
          mix.node = node;
          mix.hero = hero;
          mix.action_count = entry.action_count;
          std::size_t seen = 0;
          for (std::size_t index = 0; index < class_count; ++index) {
            if (present[index] == 0U) {
              continue;
            }
            ++seen;
            const auto probabilities =
                context.policies.row(node, static_cast<std::uint32_t>(index));
            const auto actions = std::min(probabilities.size(), mix.frequency.size());
            for (std::size_t action = 0; action < actions; ++action) {
              mix.frequency[action] += probabilities[action];
            }
          }
          if (seen > 0) {
            for (auto &value : mix.frequency) {
              value /= static_cast<double>(seen);
            }
          }
          report.best_response_preflop_mix.push_back(mix);
        }
      }
    }
    // Follow the FULL response's preflop choices, not the preflop-only BR.
    // This preserves joint deviations that deliberately enter a branch only
    // because the responder will also change its postflop continuation. A
    // restricted responder's preflop policy is the average strategy, so its
    // route is the average one.
    {
      NodeVectors average_leaves = terminals;
      entry_leaves(hero, average_mode, all_groups, average_leaves);
      NodeVectors response_leaves = terminals;
      entry_leaves(hero, responder_values, all_groups, response_leaves);
      NodeVectors average_reach, response_reach;
      hero_reach_under(hero, nullptr, average_reach);
      hero_reach_under(hero, preflop_rule == response_mode ? &choice : nullptr, response_reach);
      report.best_response_route_average_value[hero] = value_under(response_reach, average_leaves);
      for (const auto node : context.entries) {
        std::vector<double> entry_mass;
        fold_mass_universe(preflop, context.preflop_leaf_reach[hero][node], entry_mass);
        BestResponseReport::PostflopEntryRoute route;
        route.node = node;
        route.hero = hero;
        route.path = node_path_id(game, node);
        for (std::size_t combo = 0; combo < combo_total; ++combo) {
          const double prior = context.allowed[hero][combo] * hero_scale;
          if (opponent_count[combo] > 0.0) {
            const double probability = prior * entry_mass[combo] / opponent_count[combo];
            route.average_probability += probability * average_reach[node][combo];
            route.response_probability += probability * response_reach[node][combo];
          }
          route.postflop_gain_on_response_route +=
              prior * response_reach[node][combo] *
              (response_leaves[node][combo] - average_leaves[node][combo]);
        }
        report.postflop_entry_route.push_back(route);
      }
    }
    // Postflop quality independent of how often the blueprint gets there.
    {
      NodeVectors average_leaves = terminals;
      entry_leaves(hero, average_mode, all_groups, average_leaves);
      NodeVectors response_leaves = terminals;
      entry_leaves(hero, responder_values, all_groups, response_leaves);
      for (const auto node : game.postflop_entries()) {
        if (average_leaves[node].empty() || response_leaves[node].empty()) {
          continue;
        }
        double gain = 0.0;
        double weight = 0.0;
        for (std::size_t combo = 0; combo < combo_total; ++combo) {
          const double allowed = context.allowed[hero][combo];
          if (allowed == 0.0) {
            continue;
          }
          gain += allowed * (response_leaves[node][combo] - average_leaves[node][combo]);
          weight += allowed;
        }
        double opponent_mass = 0.0;
        const auto &reach = context.preflop_leaf_reach[hero][node];
        for (std::size_t hand = 0; hand < reach.size(); ++hand) {
          opponent_mass += reach[hand];
        }
        // P(entry | hero h) = sum_{o disjoint h} allowed(o) * reach_path(o)
        //                      / sum_{o disjoint h} allowed(o).
        // Average over the same hero prior used by mean_gain. In the full
        // uniform 36-card game this reduces to opponent_mass / C(36, 2),
        // but that shortcut is false for restricted hero/opponent ranges.
        std::vector<double> entry_mass;
        fold_mass_universe(preflop, reach, entry_mass);
        double probability_sum = 0.0;
        for (std::size_t combo = 0; combo < combo_total; ++combo) {
          if (opponent_count[combo] > 0.0) {
            probability_sum +=
                context.allowed[hero][combo] * entry_mass[combo] / opponent_count[combo];
          }
        }
        BestResponseReport::PostflopEntryLoss loss;
        loss.node = node;
        loss.hero = hero;
        loss.mean_gain = weight > 0.0 ? gain / weight : 0.0;
        loss.opponent_reach = opponent_mass;
        loss.entry_probability = weight > 0.0 ? probability_sum / weight : 0.0;
        const auto full_range = [](const std::vector<double> &range) {
          return std::all_of(range.begin(), range.end(),
                             [](const double value) { return value == 1.0; });
        };
        if (exact && full_range(context.allowed[0]) && full_range(context.allowed[1]) &&
            loss.entry_probability > 0.0) {
          loss.conditional_gain = loss.mean_gain / loss.entry_probability;
        }
        report.postflop_entry_loss.push_back(loss);
      }
    }
    report.gain[hero] = report.best_response[hero] - report.ev[hero];
    report.gain_lower[hero] = report.best_response_lower[hero] - report.ev[hero];
    report.gain_preflop[hero] = report.best_response_preflop[hero] - report.ev[hero];
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

Result<std::vector<FlopValues>, KernelError>
evaluate_flops(const BestResponseEvaluator &evaluator, const std::vector<FlopGroup> &groups,
               const unsigned threads) {
  using Outcome = Result<std::vector<FlopValues>, KernelError>;
  std::vector<FlopValues> values(groups.size());
  std::atomic<bool> failed{false};
  run_parallel(threads, groups.size(), [&](const std::size_t index) {
    auto result = evaluator.evaluate_flop(groups[index]);
    if (!result) {
      failed.store(true);
      return;
    }
    values[index] = std::move(result.value());
  });
  if (failed.load()) {
    return Outcome::failure(KernelError::InvalidInput);
  }
  return Outcome::success(std::move(values));
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
  evaluator.value().set_river_engine(options.river_engine);
  evaluator.value().set_deviation_from(options.deviation_from);
  auto values = evaluate_flops(evaluator.value(), groups, options.threads);
  if (!values) {
    return Outcome::failure(values.error());
  }
  std::vector<const FlopValues *> pointers;
  pointers.reserve(values.value().size());
  for (const auto &entry : values.value()) {
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
