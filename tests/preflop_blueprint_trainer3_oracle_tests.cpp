// Phase 3 (3-way step 2), task K5 of PHASE3_SPEC_2026-09-30 section 7: the
// independent oracles of the 3-seat trainer path, on 3WAY50 with the small
// 200/500/1000 per-board buckets (the tree, the payoffs and the 3-seat code
// are those of the production run; only the bucket tables are smaller).
//  - V13: trainer-level brute force of the 3-seat postflop traversal. A scalar
//    recursive walker enumerates explicit deals (h, o_lower, o_higher), walks
//    the subtree with scalar probabilities from the same policy rows (regret
//    matching of the injected regrets), ranks the 7-card hands with the equity
//    evaluator (not BoardContext's rank table) and reads fold_payoffs and
//    showdown_payoffs per winner set. Compared with TrainerAccess::
//    subtree_values3 (production top phase and units, partition target 64, 2
//    threads): v_a[h] at every hero decision and v[h] at the root for 20 hero
//    hands within 1e-12 of the larger of the hand's sum of absolute terms and
//    the node's largest (D-scaled: the kernels compute a hand with few live
//    deals from the node's totals by inclusion-exclusion, so its rounding
//    residue follows the node's joint mass, spec 3.5; both ratios are logged);
//    the regret delta of every cell of the subtree against sum_h weight (v_a -
//    v) from the traced values, within 1e-12 of the cell's sum of absolute
//    terms plus the rounding of the table (one per added hand).
//    Roots: the limped 3-way flop entry, a 2-way flop entry for each preflop
//    folder, and a node right after a postflop fold in a 3-way pot with each
//    seat as folder; heroes 0, 1, 2 at each root (an inactive hero through the
//    validation trainer with the shortcut off: payoff x D3); sparse,
//    non-constant reach (folded seats included); random policies with exact
//    zeros so that prunes occur; 5 boards (the board plays, a flush board, a
//    paired board, two random).
//  - V8 through terminal3: on the three 3WAY50 rake fixtures, 20 boards, every
//    postflop terminal, random reach for the three seats, each seat as hero
//    (TrainerAccess::terminal3_values; a folded seat's value is its folded
//    payoff times the kernel D3): sum_s <r_s, V_s> = sum over winner sets of
//    the joint mass times the payoffs (minus the rake), the masses from the
//    kernel API; tolerance relative to the sum of absolute terms.
//  - V9b, terminal level: for every preflop node with a cache term (the 61
//    preflop terminals and the nodes right after a preflop fold), random
//    class reach for the three seats and each seat as hero, the board-weighted
//    mean over the 19,998 suit-canonical 5-card boards (orbit weights) of the
//    board value of the hands of a class (terminal3 of a board_kernels trainer
//    where the hero is active, folded payoff x kernel D3 where it folded)
//    equals the class cache value within 1e-9 (scaled by the largest |value|
//    of the node): exact by suit symmetry, and it checks the 142,506 / 278,256
//    scale, the transposes and the payoffs of the cache.
// Needs preflop_three_way_v1.bin (complete) under --resources-dir; SKIP (exit
// 77) without it. --quick runs V9b on one fixture only.
#include "preflop_blueprint_test_support.hpp"

#include "gtosd/card_abstraction/three_way_table.hpp"
#include "gtosd/equity/evaluator.hpp"
#include "gtosd/preflop_blueprint/multiway_kernels.hpp"
#include "gtosd/preflop_blueprint/preflop_class_cache.hpp"
#include "gtosd/preflop_blueprint/trainer_access.hpp"

#include <bit>
#include <cmath>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>

using namespace pb_test;

namespace {

constexpr std::size_t hands = pb::live_hand_count;
constexpr double ante_scale = 1.0 / static_cast<double>(gtosd::Money::units_per_ante);

pb::GameConfig load_monker(const std::string_view name) {
  const auto path =
      std::filesystem::path(GTOSD_SOURCE_DIR) / "benchmarks" / "monker" / std::string(name);
  const auto parsed = pb::parse_game_config_json(read_file(path));
  require(parsed.has_value(), std::string("monker config parses: ") + std::string(name));
  return parsed.value();
}

pb::CompiledGame compile(const pb::GameConfig &config) {
  auto game = pb::CompiledGame::compile(config);
  require(game.has_value(), "game compiles");
  return std::move(game.value());
}

std::uint8_t bit(const std::uint8_t seat) { return static_cast<std::uint8_t>(1U << seat); }

std::array<std::uint8_t, 2> others(const std::uint8_t hero) {
  return {static_cast<std::uint8_t>(hero == 0U ? 1U : 0U),
          static_cast<std::uint8_t>(hero == 2U ? 1U : 2U)};
}

pb::TrainerConfig base_config(const Resources &resources) {
  auto config = resources.config();
  config.batch_boards = 4U;
  config.threads = 2U;
  config.partition_target_nodes = 64U;
  config.scheme = pb::WeightingScheme::Dcfr;
  config.update_mode = pb::UpdateMode::Alternating;
  config.lazy_discount = true;
  return config;
}

std::unique_ptr<pb::Trainer> make_trainer(const pb::CompiledGame &game,
                                          const pb::TrainerResources &view,
                                          const pb::TrainerConfig &config,
                                          const pb::TrainingBoards *boards = nullptr) {
  auto trainer = pb::Trainer::create(game, view, config, boards);
  require(trainer.has_value(), std::string("3-seat trainer creates: ") +
                                   (trainer ? "" : pb::trainer_error_name(trainer.error())));
  return std::move(trainer.value());
}

// The validation trainer: preflop terminals on the board (any exact list), the
// hero-folded shortcut switched off, so that a subtree whose root has the hero
// inactive can be walked (every terminal pays the hero its folded payoff).
std::unique_ptr<pb::Trainer> make_walk_trainer(const pb::CompiledGame &game,
                                               const pb::TrainerResources &view,
                                               const Resources &resources) {
  auto config = base_config(resources);
  config.validation = true;
  config.preflop_terminals = pb::PreflopTerminals::BoardKernels;
  config.hero_folded_shortcut = false;
  pb::TrainingBoards boards;
  boards.histories = {make_history({"Ks", "Qd", "7c", "9h", "6s"})};
  boards.weights = {1.0};
  boards.sample = false;
  return make_trainer(game, view, config, &boards);
}

pb::BoardContext context_of(const ca::BoardHistory &history, const Resources &resources,
                            const bool with_rows) {
  pb::AbstractionTables tables;
  tables.catalog = &resources.catalog.value();
  tables.flop = &resources.flop.value();
  tables.turn = &resources.turn.value();
  tables.river = &resources.river.value();
  auto context =
      pb::BoardContext::build(history, resources.ranks.value(), with_rows ? &tables : nullptr);
  require(context.has_value(), "board context builds");
  return std::move(context.value());
}

bool hands_disjoint(const std::array<std::uint8_t, 2> &left, const std::array<std::uint8_t, 2> &right) {
  return left[0] != right[0] && left[0] != right[1] && left[1] != right[0] && left[1] != right[1];
}

double dot(const std::vector<double> &left, const double *right) {
  double sum = 0.0;
  for (std::size_t hand = 0; hand < hands; ++hand)
    sum += left[hand] * right[hand];
  return sum;
}

pb::ConstHandSpan cspan(const std::vector<double> &values) {
  return pb::ConstHandSpan(values.data(), hands);
}

pb::HandSpan mspan(std::vector<double> &values) { return pb::HandSpan(values.data(), hands); }

// ---------------------------------------------------------------- V13 walker

// Scalar recursive walker of a subtree for one hero, deal by deal. Policy rows
// are the regret matching of the injected regrets (positive parts normalized,
// uniform without a positive regret), the rows those of the board context;
// showdowns rank the 7-card hands with the equity evaluator.
class ScalarWalker {
public:
  struct Decision {
    std::uint8_t actions{0U};
    std::vector<double> values; // actions x hands
    std::vector<double> absolute;
  };

  ScalarWalker(const pb::CompiledGame &game, const pb::StateLayout &layout,
               const std::vector<double> &regrets, const pb::BoardContext &context,
               const std::uint8_t hero)
      : game_(game), layout_(layout), regrets_(regrets), context_(context), hero_(hero),
        slot_(game.nodes().size(), -1) {
    const auto board = context.board();
    for (std::size_t hand = 0; hand < hands; ++hand) {
      std::array<gtosd::CardId, 7> cards{};
      for (std::size_t index = 0; index < 5U; ++index)
        cards[index] = board[index];
      for (std::size_t index = 0; index < 2U; ++index) {
        const auto parsed = gtosd::CardId::from_index(context.cards()[hand][index]);
        require(parsed.has_value(), "hand card index");
        cards[5U + index] = parsed.value();
      }
      const auto value = gtosd::evaluate_seven(cards);
      require(value.has_value(), "equity evaluator ranks the hand");
      strength_[hand] = value.value();
    }
  }

  // Values of the hero hands in `hero_hands` at `root` under the injected reach.
  void run(const std::uint32_t root, const std::array<std::vector<double>, 3> &reach,
           const std::vector<std::uint16_t> &hero_hands) {
    root_values_.assign(hands, 0.0);
    root_absolute_.assign(hands, 0.0);
    const auto seats = others(hero_);
    const auto cards = context_.cards();
    for (const auto hero_hand : hero_hands) {
      hand_ = hero_hand;
      for (std::uint16_t lower = 0U; lower < hands; ++lower) {
        if (reach[seats[0]][lower] == 0.0 || !hands_disjoint(cards[hero_hand], cards[lower]))
          continue;
        for (std::uint16_t higher = 0U; higher < hands; ++higher) {
          if (reach[seats[1]][higher] == 0.0 || !hands_disjoint(cards[hero_hand], cards[higher]) ||
              !hands_disjoint(cards[lower], cards[higher]))
            continue;
          deal_[hero_] = hero_hand;
          deal_[seats[0]] = lower;
          deal_[seats[1]] = higher;
          const double weight = reach[seats[0]][lower] * reach[seats[1]][higher];
          const auto [value, absolute] = walk(root, weight);
          root_values_[hero_hand] += weight * value;
          root_absolute_[hero_hand] += weight * absolute;
        }
      }
    }
  }

  [[nodiscard]] const std::vector<double> &root_values() const noexcept { return root_values_; }
  [[nodiscard]] const std::vector<double> &root_absolute() const noexcept { return root_absolute_; }
  // The accumulated action values of a hero decision node, or nullptr when no
  // deal reached it with a positive weight.
  [[nodiscard]] const Decision *decision(const std::uint32_t node) const noexcept {
    return slot_[node] < 0 ? nullptr : &decisions_[static_cast<std::size_t>(slot_[node])];
  }
  [[nodiscard]] std::size_t decisions() const noexcept { return decisions_.size(); }

private:
  void policy(const std::uint32_t node_id, const std::uint16_t hand, double *out) const {
    const auto &node = game_.nodes()[node_id];
    const auto row = context_.row(node.street, hand);
    const double *regrets =
        regrets_.data() + layout_.offsets[node_id] + static_cast<std::uint64_t>(row) * node.action_count;
    double positive = 0.0;
    for (std::uint8_t action = 0; action < node.action_count; ++action)
      positive += std::max(0.0, regrets[action]);
    for (std::uint8_t action = 0; action < node.action_count; ++action)
      out[action] = positive > 0.0 ? std::max(0.0, regrets[action]) / positive
                                   : 1.0 / static_cast<double>(node.action_count);
  }

  [[nodiscard]] double payoff(const std::uint32_t node_id) const {
    const auto &node = game_.nodes()[node_id];
    if (node.kind == pb::NodeKind::TerminalFold)
      return static_cast<double>(game_.fold_payoffs(node_id)[hero_]) * ante_scale;
    std::optional<gtosd::HandValue> best;
    std::uint8_t winners = 0U;
    for (std::uint8_t seat = 0U; seat < 3U; ++seat) {
      if ((node.active_mask & bit(seat)) == 0U)
        continue;
      const auto &value = strength_[deal_[seat]];
      if (!best || value > *best) {
        best = value;
        winners = bit(seat);
      } else if (value == *best) {
        winners = static_cast<std::uint8_t>(winners | bit(seat));
      }
    }
    return static_cast<double>(game_.showdown_payoffs(node_id, winners)[hero_]) * ante_scale;
  }

  // Expected hero payoff below `node_id` for the current deal (the hero's own
  // policy included) and the expected absolute payoff; adds weight x the
  // child value to the action values of every hero decision.
  std::pair<double, double> walk(const std::uint32_t node_id, const double weight) {
    const auto &node = game_.nodes()[node_id];
    switch (node.kind) {
    case pb::NodeKind::TerminalFold:
    case pb::NodeKind::TerminalShowdown: {
      const double value = payoff(node_id);
      return {value, std::abs(value)};
    }
    case pb::NodeKind::Chance:
      return walk(game_.edges_of(node_id)[0].child, weight);
    case pb::NodeKind::Decision:
      break;
    }
    std::array<double, pb::maximum_actions> probabilities{};
    policy(node_id, deal_[node.actor], probabilities.data());
    const auto edges = game_.edges_of(node_id);
    double value = 0.0;
    double absolute = 0.0;
    if (node.actor == hero_) {
      if (slot_[node_id] < 0) {
        slot_[node_id] = static_cast<std::int32_t>(decisions_.size());
        Decision entry;
        entry.actions = node.action_count;
        entry.values.assign(static_cast<std::size_t>(node.action_count) * hands, 0.0);
        entry.absolute.assign(static_cast<std::size_t>(node.action_count) * hands, 0.0);
        decisions_.push_back(std::move(entry));
      }
      const auto slot = static_cast<std::size_t>(slot_[node_id]);
      for (std::uint8_t action = 0; action < node.action_count; ++action) {
        const auto [child, child_absolute] = walk(edges[action].child, weight);
        auto &entry = decisions_[slot];
        entry.values[static_cast<std::size_t>(action) * hands + hand_] += weight * child;
        entry.absolute[static_cast<std::size_t>(action) * hands + hand_] += weight * child_absolute;
        value += probabilities[action] * child;
        absolute += probabilities[action] * child_absolute;
      }
      return {value, absolute};
    }
    for (std::uint8_t action = 0; action < node.action_count; ++action) {
      if (probabilities[action] == 0.0)
        continue;
      const auto [child, child_absolute] =
          walk(edges[action].child, weight * probabilities[action]);
      value += probabilities[action] * child;
      absolute += probabilities[action] * child_absolute;
    }
    return {value, absolute};
  }

  const pb::CompiledGame &game_;
  const pb::StateLayout &layout_;
  const std::vector<double> &regrets_;
  const pb::BoardContext &context_;
  std::uint8_t hero_;
  std::array<gtosd::HandValue, hands> strength_{};
  std::array<std::uint16_t, 3> deal_{};
  std::uint16_t hand_{0U};
  std::vector<std::int32_t> slot_;
  std::vector<Decision> decisions_;
  std::vector<double> root_values_;
  std::vector<double> root_absolute_;
};

struct TraceSink final : pb::HeroDecisionSink {
  struct Entry {
    std::uint8_t actions{0U};
    std::vector<double> action_values;
    std::vector<double> values;
    std::vector<double> weight;
  };
  std::map<std::uint32_t, Entry> nodes;
  std::uint64_t duplicates{0U};
  void record(const pb::HeroDecisionTrace &trace) override {
    if (nodes.contains(trace.node))
      ++duplicates;
    Entry entry;
    entry.actions = trace.actions;
    entry.action_values.assign(trace.action_values.begin(), trace.action_values.end());
    entry.values.assign(trace.values.begin(), trace.values.end());
    entry.weight.assign(trace.regret_weight.begin(), trace.regret_weight.end());
    nodes[trace.node] = std::move(entry);
  }
};

struct RootChoice {
  std::string label;
  std::uint32_t node{pb::no_node};
};

std::uint32_t subtree_size(const pb::CompiledGame &game, const std::uint32_t node) {
  return game.nodes()[node].subtree_end - node;
}

// Subtree roots of V13 (spec section 7): the limped 3-way flop entry, the
// smallest 2-way flop entry for each preflop folder, and the smallest postflop
// decision node right after a postflop fold of each seat in a 3-way pot.
std::vector<RootChoice> v13_roots(const pb::CompiledGame &game) {
  const auto &nodes = game.nodes();
  std::vector<RootChoice> roots;
  std::uint32_t limped = pb::no_node;
  for (const auto id : game.postflop_entries()) {
    if (nodes[id].active_mask == 7U && nodes[id].limped_pot &&
        (limped == pb::no_node || subtree_size(game, id) < subtree_size(game, limped)))
      limped = id;
  }
  require(limped != pb::no_node, "a limped 3-way flop entry exists");
  roots.push_back({"limped 3-way flop entry", limped});
  for (std::uint8_t folder = 0U; folder < 3U; ++folder) {
    const auto mask = static_cast<std::uint8_t>(7U & ~bit(folder));
    std::uint32_t best = pb::no_node;
    for (const auto id : game.postflop_entries())
      if (nodes[id].active_mask == mask &&
          (best == pb::no_node || subtree_size(game, id) < subtree_size(game, best)))
        best = id;
    require(best != pb::no_node, "a 2-way flop entry exists for every preflop folder");
    roots.push_back({"2-way flop entry, seat " + std::to_string(folder) + " folded preflop", best});
  }
  for (std::uint8_t folder = 0U; folder < 3U; ++folder) {
    const auto mask = static_cast<std::uint8_t>(7U & ~bit(folder));
    std::uint32_t best = pb::no_node;
    for (const auto &node : nodes) {
      if (node.kind != pb::NodeKind::Decision || node.street == gtosd::Street::Preflop ||
          node.active_mask != mask || node.parent == pb::no_node)
        continue;
      const auto &parent = nodes[node.parent];
      if (parent.kind != pb::NodeKind::Decision || parent.active_mask != 7U ||
          parent.actor != folder || parent.street == gtosd::Street::Preflop)
        continue;
      // Prefer a subtree with more than a single street of play but small.
      if (subtree_size(game, node.id) < 8U)
        continue;
      if (best == pb::no_node || subtree_size(game, node.id) < subtree_size(game, best))
        best = node.id;
    }
    require(best != pb::no_node, "a node after a postflop fold exists for every seat");
    roots.push_back({"after a postflop fold of seat " + std::to_string(folder), best});
  }
  return roots;
}

void test_v13(const Resources &resources, const ca::ThreeWayTable &table) {
  const auto game = compile(load_monker("3WAY50_donk_rake.json"));
  auto view = resources.view();
  view.three_way = &table;
  auto production = make_trainer(game, view, base_config(resources));
  auto walk_trainer = make_walk_trainer(game, view, resources);
  const auto &layout = production->layout();
  require(walk_trainer->layout().entries == layout.entries, "same layout in both trainers");
  std::mt19937_64 random(20261013ULL);
  std::uniform_real_distribution<double> uniform(-1.0, 1.0);
  std::uniform_real_distribution<double> positive(0.1, 1.0);
  std::uniform_int_distribution<int> hand_draw(0, static_cast<int>(hands) - 1);

  // Boards: the board plays (broadway, rainbow), a flush board, a paired board,
  // two random boards.
  std::vector<ca::BoardHistory> boards = {make_history({"Th", "Jd", "Qs", "Kc", "Ah"}),
                                          make_history({"6h", "8h", "Jh", "Kh", "7c"}),
                                          make_history({"9c", "9d", "Kh", "7s", "6c"})};
  {
    std::vector<int> deck(36);
    for (int index = 0; index < 36; ++index)
      deck[static_cast<std::size_t>(index)] = index;
    for (int draw = 0; draw < 2; ++draw) {
      std::shuffle(deck.begin(), deck.end(), random);
      std::array<std::string, 5> texts;
      for (std::size_t index = 0; index < 5U; ++index)
        texts[index] = gtosd::format_card(
            gtosd::CardId::from_index(static_cast<std::uint8_t>(deck[index])).value());
      boards.push_back(make_history({texts[0], texts[1], texts[2], texts[3], texts[4]}));
    }
  }
  const auto roots = v13_roots(game);
  for (const auto &root : roots)
    std::cout << "V13 root: " << root.label << " = node " << root.node << " (subtree "
              << subtree_size(game, root.node) << " nodes, active mask "
              << int{game.nodes()[root.node].active_mask} << ")\n";

  double worst_root = 0.0;
  double worst_root_own = 0.0;
  double worst_action = 0.0;
  double worst_regret = 0.0;
  double worst_residue = 0.0;
  double worst_action_own = 0.0;
  std::uint64_t compared_values = 0U;
  std::uint64_t compared_cells = 0U;
  std::uint64_t traced_nodes = 0U;
  std::uint64_t inactive_roots = 0U;
  std::uint64_t hook_skipped_units = 0U;
  std::uint64_t zero_probability_rows = 0U;
  for (std::size_t board_index = 0; board_index < boards.size(); ++board_index) {
    const auto &history = boards[board_index];
    const auto context = context_of(history, resources, true);
    // A fresh random current policy per board: random regrets, about 30 % of
    // the cells exactly zero (some rows entirely: uniform).
    std::vector<double> regrets(layout.entries);
    for (auto &regret : regrets) {
      const double draw = uniform(random);
      regret = draw < -0.3 ? 0.0 : draw;
    }
    for (const auto &root : roots) {
      // Sparse non-constant reach: 20 random hands per seat.
      std::array<std::vector<double>, 3> reach;
      for (auto &seat : reach) {
        seat.assign(hands, 0.0);
        for (int pick = 0; pick < 20; ++pick)
          seat[static_cast<std::size_t>(hand_draw(random))] = positive(random);
      }
      std::vector<std::uint16_t> hero_hands;
      while (hero_hands.size() < 20U) {
        const auto hand = static_cast<std::uint16_t>(hand_draw(random));
        if (std::find(hero_hands.begin(), hero_hands.end(), hand) == hero_hands.end())
          hero_hands.push_back(hand);
      }
      for (std::uint8_t hero = 0U; hero < 3U; ++hero) {
        const bool active = (game.nodes()[root.node].active_mask & bit(hero)) != 0U;
        auto &trainer = active ? *production : *walk_trainer;
        require(pb::TrainerAccess::set_regrets(trainer, regrets).has_value(), "regrets set");
        TraceSink sink;
        const auto computed =
            pb::TrainerAccess::subtree_values3(trainer, root.node, reach, hero, history, &sink);
        require(computed.has_value(), "subtree_values3 runs");
        require(sink.duplicates == 0U, "every hero decision is traced once");
        const auto after = trainer.regrets();
        hook_skipped_units += computed.value().units_skipped;
        inactive_roots += active ? 0U : 1U;
        ScalarWalker walker(game, layout, regrets, context, hero);
        walker.run(root.node, reach, hero_hands);
        // Root values.
        double root_scale = 0.0;
        for (const auto hand : hero_hands)
          root_scale = std::max(root_scale, walker.root_absolute()[hand]);
        for (const auto hand : hero_hands) {
          const double expected = walker.root_values()[hand];
          const double scale = std::max(walker.root_absolute()[hand], 1e-300);
          const double got = computed.value().values[hand];
          require(std::isfinite(got), "V13 root value finite");
          const double error = std::abs(got - expected) / std::max(scale, root_scale);
          worst_root = std::max(worst_root, walker.root_absolute()[hand] > 0.0 ? error : 0.0);
          worst_root_own = std::max(worst_root_own, walker.root_absolute()[hand] > 0.0
                                                        ? std::abs(got - expected) / scale
                                                        : 0.0);
          if (walker.root_absolute()[hand] == 0.0) {
            worst_residue = std::max(worst_residue, std::abs(got) / std::max(root_scale, 1e-300));
            require(std::abs(got) <= 1e-12 * root_scale, "V13: zero oracle root value is zero");
          } else
            require(error <= 1e-12, "V13 root value equals the scalar walker (" + root.label +
                                        ", hero " + std::to_string(hero) + ", board " +
                                        std::to_string(board_index) + "): " +
                                        std::to_string(error));
          ++compared_values;
        }
        // Action values at every traced hero decision; a hero decision the
        // walker reached with positive weight must be traced.
        traced_nodes += sink.nodes.size();
        // Scale of an entry whose exact value is 0 (no deal of the hand reaches
        // the node with positive weight): the kernels' inclusion-exclusion
        // leaves a rounding residue of the other seats' joint mass there, so
        // it is compared with the node's magnitude (the D-scaled tolerance of
        // spec section 7), or the root's when no evaluated hand reached it.
        for (const auto &[node, entry] : sink.nodes) {
          const auto *oracle = walker.decision(node);
          double node_scale = root_scale;
          if (oracle != nullptr) {
            node_scale = 0.0;
            for (const auto value : oracle->absolute)
              node_scale = std::max(node_scale, value);
          }
          for (std::uint8_t action = 0; action < entry.actions; ++action) {
            for (const auto hand : hero_hands) {
              const auto index = static_cast<std::size_t>(action) * hands + hand;
              const double got = entry.action_values[index];
              const double expected = oracle != nullptr ? oracle->values[index] : 0.0;
              const double scale = oracle != nullptr ? oracle->absolute[index] : 0.0;
              require(std::isfinite(got), "V13 action value finite");
              if (scale == 0.0) {
                worst_residue = std::max(worst_residue, std::abs(got) / std::max(node_scale, 1e-300));
                if (std::abs(got) > 1e-12 * node_scale) {
                  const auto &where = game.nodes()[node];
                  std::ostringstream detail;
                  detail << std::setprecision(17)
                         << "V13: action value with zero oracle mass is zero: node " << node
                         << " (street " << static_cast<int>(where.street) << ", actor "
                         << int{where.actor} << ", active " << int{where.active_mask}
                         << ", depth " << where.depth << ") action " << int{action} << " child "
                         << game.edges_of(node)[action].child << " (kind "
                         << static_cast<int>(game.nodes()[game.edges_of(node)[action].child].kind)
                         << ", active "
                         << int{game.nodes()[game.edges_of(node)[action].child].active_mask}
                         << ") hand " << hand << " value " << got << " oracle "
                         << (oracle != nullptr ? "reached" : "never reached") << " ("
                         << root.label << ", hero " << int{hero} << ", board " << board_index
                         << ")";
                  require(false, detail.str());
                }
                continue;
              }
              // Gate: relative to the larger of the hand's sum of |terms| and
              // the node's magnitude (D-scaled: a hand with few live deals is
              // computed from the node's totals by inclusion-exclusion).
              const double error = std::abs(got - expected) / std::max(scale, node_scale);
              worst_action = std::max(worst_action, error);
              worst_action_own = std::max(worst_action_own, std::abs(got - expected) / scale);
              if (!(error <= 1e-12)) {
                std::ostringstream detail;
                detail << std::setprecision(17) << "V13 action value equals the scalar walker at node "
                       << node << " action " << int{action} << " child "
                       << game.edges_of(node)[action].child << " hand " << hand << " ("
                       << root.label << ", hero " << int{hero} << ", board " << board_index
                       << "): got " << got << " expected " << expected << " |terms| " << scale
                       << " error " << error;
                require(false, detail.str());
              }
              ++compared_values;
            }
          }
        }
        for (std::size_t node = 0; node < game.nodes().size(); ++node) {
          const auto *oracle = walker.decision(static_cast<std::uint32_t>(node));
          if (oracle == nullptr || sink.nodes.contains(static_cast<std::uint32_t>(node)))
            continue;
          // Reached by the walker but not traced: only where every deal had
          // zero weight there (a prune of the trainer).
          for (const auto value : oracle->absolute)
            require(value == 0.0, "V13: a hero decision with oracle mass is traced");
        }
        // Regret deltas over every cell of the subtree's decisions: the traced
        // sum_h weight (v_a - v) per row, nothing elsewhere.
        const auto &nodes = game.nodes();
        for (auto node = root.node; node < nodes[root.node].subtree_end; ++node) {
          const auto &entry_node = nodes[node];
          if (entry_node.kind != pb::NodeKind::Decision)
            continue;
          const auto rows = pb::StateLayout::rows_for(entry_node.street, layout.flop_capacity,
                                                      layout.turn_capacity, layout.river_capacity);
          const auto base = layout.offsets[node];
          const auto actions = entry_node.action_count;
          std::vector<double> expected(static_cast<std::size_t>(rows) * actions, 0.0);
          std::vector<double> absolute(expected.size(), 0.0);
          std::vector<std::uint32_t> terms(expected.size(), 0U);
          const auto traced = sink.nodes.find(node);
          if (traced != sink.nodes.end()) {
            const auto &entry = traced->second;
            for (std::uint16_t hand = 0U; hand < hands; ++hand) {
              const double weight = entry.weight[hand];
              if (weight == 0.0)
                continue;
              const auto row = context.row(entry_node.street, hand);
              for (std::uint8_t action = 0; action < actions; ++action) {
                const double term =
                    weight *
                    (entry.action_values[static_cast<std::size_t>(action) * hands + hand] -
                     entry.values[hand]);
                expected[static_cast<std::size_t>(row) * actions + action] += term;
                ++terms[static_cast<std::size_t>(row) * actions + action];
                absolute[static_cast<std::size_t>(row) * actions + action] +=
                    weight *
                    (std::abs(entry.action_values[static_cast<std::size_t>(action) * hands + hand]) +
                     std::abs(entry.values[hand]));
              }
            }
          }
          for (std::size_t cell = 0; cell < expected.size(); ++cell) {
            const double before = regrets[base + cell];
            const double now = after[base + cell];
            const double delta = now - before;
            // One rounding of the table per added hand (the trainer adds each
            // hand's increment to the cell in turn).
            const double rounding = 2.0 * static_cast<double>(terms[cell] + 1U) *
                                    std::numeric_limits<double>::epsilon() *
                                    (std::abs(before) + std::abs(now));
            const double error = std::abs(delta - expected[cell]);
            if (absolute[cell] == 0.0) {
              require(now == before, "V13: a cell without traced terms is unchanged");
              continue;
            }
            worst_regret = std::max(worst_regret, std::max(0.0, error - rounding) / absolute[cell]);
            if (!(error <= 1e-12 * absolute[cell] + rounding)) {
              std::ostringstream detail;
              detail << std::setprecision(17) << "V13 regret delta equals sum_h weight (v_a - v) at node "
                     << node << " cell " << cell << " (" << root.label << ", hero " << int{hero}
                     << ", board " << board_index << "): before " << before << " after " << now
                     << " delta " << delta << " expected " << expected[cell] << " |terms| "
                     << absolute[cell] << " hands in the row " << terms[cell];
              require(false, detail.str());
            }
            ++compared_cells;
          }
        }
      }
    }
    // Rows whose policy has an exact zero (prunes possible).
    for (const auto &node : game.nodes()) {
      if (node.kind != pb::NodeKind::Decision || node.street == gtosd::Street::Preflop)
        continue;
      const auto base = layout.offsets[node.id];
      for (std::uint8_t action = 0; action < node.action_count; ++action)
        zero_probability_rows += regrets[base + action] <= 0.0 ? 1U : 0U;
    }
  }
  std::cout << "V13: " << roots.size() << " roots x 3 heroes x " << boards.size()
            << " boards (" << inactive_roots << " hero-inactive walks), " << traced_nodes
            << " traced hero decisions, " << compared_values << " values and " << compared_cells
            << " regret cells compared, " << hook_skipped_units
            << " skipped units below the roots, " << zero_probability_rows
            << " postflop policy cells at probability 0 (non-positive regret; prunes); worst root "
            << worst_root << ", action value "
            << worst_action
            << " (relative to max(the hand's sum of |terms|, the node's largest), gate 1e-12; "
               "relative to the hand's own sum of |terms| alone: root "
            << worst_root_own << ", action " << worst_action_own << "), regret delta "
            << worst_regret
            << " (relative to the cell's sum of |terms|, gate 1e-12); zero-mass entries: residue "
               "/ node magnitude "
            << worst_residue << " (gate 1e-12)\n";
  require(hook_skipped_units > 0U, "V13 crosses skipped units");
}

// ---------------------------------------------------------------- V8 through terminal3

std::vector<ca::BoardHistory> v8_boards(std::mt19937_64 &random) {
  std::vector<ca::BoardHistory> boards = {
      make_history({"Th", "Jd", "Qs", "Kc", "Ah"}), // broadway plays
      make_history({"As", "Ad", "Ac", "Kh", "Ks"}), // full house on board
      make_history({"9s", "9h", "9d", "9c", "Ks"}), // quads on board
      make_history({"6h", "8h", "Jh", "Kh", "Ah"}), // flush on board
      make_history({"6s", "7s", "8s", "9s", "Ts"}), // straight flush on board
      make_history({"As", "6d", "7c", "8h", "9s"}), // A-6-7-8-9
      make_history({"Qh", "Qd", "8s", "8c", "Th"}), // two pair on board
      make_history({"7h", "7d", "Kc", "Ts", "6h"}), // paired
  };
  std::vector<int> deck(36);
  for (int index = 0; index < 36; ++index)
    deck[static_cast<std::size_t>(index)] = index;
  while (boards.size() < 20U) {
    std::shuffle(deck.begin(), deck.end(), random);
    std::array<std::string, 5> texts;
    for (std::size_t index = 0; index < 5U; ++index)
      texts[index] = gtosd::format_card(
          gtosd::CardId::from_index(static_cast<std::uint8_t>(deck[index])).value());
    boards.push_back(make_history({texts[0], texts[1], texts[2], texts[3], texts[4]}));
  }
  return boards;
}

void test_v8_terminal3(const Resources &resources, const ca::ThreeWayTable &table) {
  std::mt19937_64 random(808ULL);
  const auto boards = v8_boards(random);
  std::uniform_real_distribution<double> weight(0.0, 1.0);
  for (const auto name :
       {"3WAY50_donk_rake.json", "3WAY50_donk_rake25cap2.json", "3WAY50_donk_rake5cap075.json"}) {
    const auto game = compile(load_monker(name));
    auto view = resources.view();
    view.three_way = &table;
    auto trainer = make_trainer(game, view, base_config(resources));
    const auto &nodes = game.nodes();
    std::vector<std::uint32_t> terminals;
    for (const auto &node : nodes)
      if ((node.kind == pb::NodeKind::TerminalFold || node.kind == pb::NodeKind::TerminalShowdown) &&
          node.street != gtosd::Street::Preflop)
        terminals.push_back(node.id);
    std::array<std::vector<std::uint32_t>, 3> active_terminals;
    for (const auto id : terminals)
      for (std::uint8_t seat = 0U; seat < 3U; ++seat)
        if ((nodes[id].active_mask & bit(seat)) != 0U)
          active_terminals[seat].push_back(id);
    double worst = 0.0;
    double rake_mass = 0.0;
    std::uint64_t checks = 0U;
    pb::MultiwayScratch scratch;
    for (const auto &history : boards) {
      const auto context = context_of(history, resources, false);
      std::array<std::vector<double>, 3> reach;
      for (auto &seat : reach) {
        seat.assign(hands, 0.0);
        for (auto &value : seat)
          value = weight(random) < 0.3 ? 0.0 : weight(random);
      }
      // V_s for the terminals where s is active, through terminal3.
      std::array<std::map<std::uint32_t, std::vector<double>>, 3> values;
      for (std::uint8_t seat = 0U; seat < 3U; ++seat) {
        auto computed =
            pb::TrainerAccess::terminal3_values(*trainer, active_terminals[seat], reach, seat, history);
        require(computed.has_value(), "terminal3_values runs");
        for (std::size_t index = 0; index < active_terminals[seat].size(); ++index)
          values[seat][active_terminals[seat][index]] = std::move(computed.value()[index]);
      }
      // D3 of every seat (kernel API), for folded seats and folds.
      std::array<std::vector<double>, 3> deal;
      for (std::uint8_t seat = 0U; seat < 3U; ++seat) {
        const auto pair = others(seat);
        deal[seat].assign(hands, 0.0);
        pb::three_seat_deal_mass(context, cspan(reach[pair[0]]), cspan(reach[pair[1]]),
                                 mspan(deal[seat]), scratch);
      }
      std::vector<double> win(hands), tie_lower(hands), tie_higher(hands), tie_both(hands),
          lose(hands), tie(hands);
      for (const auto id : terminals) {
        const auto &node = nodes[id];
        // Expected: sum over winner sets of the joint mass times the payoff sum.
        std::map<std::uint8_t, double> mass;
        if (node.kind == pb::NodeKind::TerminalFold) {
          mass[0U] = dot(reach[0], deal[0].data());
        } else if (node.active_mask == 7U) {
          const auto masses = [&](const std::uint8_t hero) {
            const auto pair = others(hero);
            pb::three_active_masses(context, cspan(reach[pair[0]]), cspan(reach[pair[1]]),
                                    {mspan(win), mspan(tie_lower), mspan(tie_higher),
                                     mspan(tie_both), mspan(lose)},
                                    scratch);
          };
          masses(0U);
          mass[1U] = dot(reach[0], win.data());
          mass[3U] = dot(reach[0], tie_lower.data());
          mass[5U] = dot(reach[0], tie_higher.data());
          mass[7U] = dot(reach[0], tie_both.data());
          masses(1U);
          mass[2U] = dot(reach[1], win.data());
          mass[6U] = dot(reach[1], tie_higher.data());
          masses(2U);
          mass[4U] = dot(reach[2], win.data());
        } else {
          std::uint8_t first = 0xFFU;
          std::uint8_t second = 0xFFU;
          std::uint8_t folded = 0xFFU;
          for (std::uint8_t seat = 0U; seat < 3U; ++seat) {
            if ((node.active_mask & bit(seat)) == 0U)
              folded = seat;
            else if (first == 0xFFU)
              first = seat;
            else
              second = seat;
          }
          pb::two_active_masses(context, cspan(reach[second]), cspan(reach[folded]),
                                {mspan(win), mspan(tie), mspan(lose)}, scratch);
          mass[bit(first)] = dot(reach[first], win.data());
          mass[static_cast<std::uint8_t>(bit(first) | bit(second))] = dot(reach[first], tie.data());
          mass[bit(second)] = dot(reach[first], lose.data());
        }
        double expected = 0.0;
        double absolute = 0.0;
        double rake = 0.0;
        for (const auto &[winners, joint] : mass) {
          const auto payoffs =
              node.kind == pb::NodeKind::TerminalFold ? game.fold_payoffs(id) : game.showdown_payoffs(id, winners);
          double sum = 0.0;
          for (std::uint8_t seat = 0U; seat < 3U; ++seat) {
            sum += static_cast<double>(payoffs[seat]) * ante_scale;
            absolute += std::abs(static_cast<double>(payoffs[seat]) * ante_scale * joint);
          }
          expected += sum * joint;
          rake -= sum * joint;
        }
        double got = 0.0;
        for (std::uint8_t seat = 0U; seat < 3U; ++seat) {
          if ((node.active_mask & bit(seat)) != 0U) {
            const auto &vector = values[seat].at(id);
            for (std::size_t hand = 0; hand < hands; ++hand) {
              got += reach[seat][hand] * vector[hand];
              absolute += std::abs(reach[seat][hand] * vector[hand]);
            }
          } else {
            // The folded seat: its folded payoff over every winner set.
            const auto payoffs = node.kind == pb::NodeKind::TerminalFold
                                     ? game.fold_payoffs(id)
                                     : game.showdown_payoffs(id, node.active_mask);
            const double folded = static_cast<double>(payoffs[seat]) * ante_scale;
            for (std::size_t hand = 0; hand < hands; ++hand) {
              got += reach[seat][hand] * folded * deal[seat][hand];
              absolute += std::abs(reach[seat][hand] * folded * deal[seat][hand]);
            }
          }
        }
        rake_mass += rake;
        const double error = absolute > 0.0 ? std::abs(got - expected) / absolute : 0.0;
        worst = std::max(worst, error);
        require(error <= 1e-12, std::string("V8 through terminal3: sum_s <r_s, V_s> = winner-set "
                                            "masses x payoffs (") +
                                    name + ", node " + std::to_string(id) + "): " +
                                    std::to_string(error));
        ++checks;
      }
    }
    std::cout << "V8 through terminal3, " << name << ": " << terminals.size()
              << " postflop terminals x " << boards.size() << " boards (" << checks
              << " identities, every seat as hero), expected rake mass " << rake_mass
              << ", worst error relative to sum|terms| " << worst << '\n';
    require(rake_mass > 0.0, "V8: the rake fixtures take rake");
  }
}

// ---------------------------------------------------------------- V9b terminal level

void test_v9b_terminal(const Resources &resources, const ca::ThreeWayTable &table,
                       const std::vector<std::string_view> &fixtures) {
  for (const auto name : fixtures) {
    const auto started = Clock::now();
    const auto game = compile(load_monker(name));
    auto built = pb::PreflopClassCache::build(game, table);
    require(built.has_value(), "class cache builds");
    const auto &cache = built.value();
    auto view = resources.view();
    auto config = base_config(resources);
    config.validation = true;
    config.preflop_terminals = pb::PreflopTerminals::BoardKernels;
    pb::TrainingBoards one;
    one.histories = {make_history({"Ks", "Qd", "7c", "9h", "6s"})};
    one.weights = {1.0};
    one.sample = false;
    auto trainer = make_trainer(game, view, config, &one);
    const auto &nodes = game.nodes();
    // Random class reach per seat (about 15 % of the classes at zero).
    std::mt19937_64 random(99ULL);
    std::uniform_real_distribution<double> uniform(0.0, 1.0);
    std::array<std::array<double, 81>, 3> class_reach{};
    for (auto &seat : class_reach)
      for (auto &value : seat)
        value = uniform(random) < 0.15 ? 0.0 : uniform(random);
    // Cache side: the terms of every seat at every preflop node with a term,
    // contracted with the injected class reach (static forms of the cache).
    struct Term {
      std::uint32_t node{0U};
      std::uint8_t seat{0U};
      bool active{false};
      std::array<double, 81> cache{};
      std::array<double, 81> numerator{};
    };
    std::vector<Term> terms;
    std::vector<double> outer(81U * 81U);
    for (const auto &node : nodes) {
      if (node.street != gtosd::Street::Preflop)
        continue;
      for (std::uint8_t seat = 0U; seat < 3U; ++seat) {
        const auto &term = cache.term(node.id, seat);
        if (term.kind == pb::PreflopTermKind::None)
          continue;
        const auto pair = others(seat);
        Term entry;
        entry.node = node.id;
        entry.seat = seat;
        entry.active = (node.active_mask & bit(seat)) != 0U &&
                       (node.kind == pb::NodeKind::TerminalFold ||
                        node.kind == pb::NodeKind::TerminalShowdown);
        if (term.kind == pb::PreflopTermKind::Tensor) {
          pb::PreflopClassCache::contract_tensor(cache.tensors()[term.tensor],
                                                 pb::PreflopClassCache::board_scale,
                                                 class_reach[pair[0]].data(),
                                                 class_reach[pair[1]].data(), outer.data(),
                                                 entry.cache.data());
        } else {
          pb::PreflopClassCache::class_deal_values(class_reach[pair[0]].data(),
                                                   class_reach[pair[1]].data(), entry.cache.data());
          for (auto &value : entry.cache)
            value *= term.payoff * pb::PreflopClassCache::board_scale;
        }
        terms.push_back(entry);
      }
    }
    std::array<std::vector<std::uint32_t>, 3> active_nodes;
    std::array<std::vector<std::size_t>, 3> active_terms;
    for (std::size_t index = 0; index < terms.size(); ++index) {
      if (!terms[index].active)
        continue;
      active_nodes[terms[index].seat].push_back(terms[index].node);
      active_terms[terms[index].seat].push_back(index);
    }
    std::uint32_t terminal_terms = 0U;
    for (const auto &term : terms)
      terminal_terms += nodes[term.node].kind != pb::NodeKind::Decision &&
                                nodes[term.node].kind != pb::NodeKind::Chance
                            ? 1U
                            : 0U;
    // Board side: the 19,998 canonical 5-card boards with orbit weights.
    const auto &catalog = resources.catalog.value();
    std::array<double, 81> denominator{};
    pb::MultiwayScratch scratch;
    std::vector<double> deal(hands);
    double total_weight = 0.0;
    for (const auto &board : catalog.river_boards()) {
      auto cards = board.cards;
      std::sort(cards.begin(), cards.end());
      ca::BoardHistory history;
      history.flop = {cards[0], cards[1], cards[2]};
      history.turn = cards[3];
      history.river = cards[4];
      const double weight = static_cast<double>(board.multiplicity);
      total_weight += weight;
      const auto context = context_of(history, resources, false);
      const auto classes = context.hand_classes();
      std::array<std::vector<double>, 3> reach;
      for (std::uint8_t seat = 0U; seat < 3U; ++seat) {
        reach[seat].assign(hands, 0.0);
        for (std::size_t hand = 0; hand < hands; ++hand)
          reach[seat][hand] = class_reach[seat][classes[hand]];
      }
      for (std::size_t hand = 0; hand < hands; ++hand)
        denominator[classes[hand]] += weight;
      for (std::uint8_t seat = 0U; seat < 3U; ++seat) {
        if (!active_nodes[seat].empty()) {
          auto computed =
              pb::TrainerAccess::terminal3_values(*trainer, active_nodes[seat], reach, seat, history);
          require(computed.has_value(), "terminal3_values runs on preflop terminals");
          for (std::size_t index = 0; index < active_terms[seat].size(); ++index) {
            auto &term = terms[active_terms[seat][index]];
            const auto &values = computed.value()[index];
            for (std::size_t hand = 0; hand < hands; ++hand)
              term.numerator[classes[hand]] += weight * values[hand];
          }
        }
        // Inactive seats (folded preflop): folded payoff x the board D3.
        const auto pair = others(seat);
        pb::three_seat_deal_mass(context, cspan(reach[pair[0]]), cspan(reach[pair[1]]), mspan(deal),
                                 scratch);
        for (auto &term : terms) {
          if (term.seat != seat || term.active)
            continue;
          const double payoff = cache.term(term.node, seat).payoff;
          for (std::size_t hand = 0; hand < hands; ++hand)
            term.numerator[classes[hand]] += weight * payoff * deal[hand];
        }
      }
    }
    require(total_weight == 376'992.0, "the canonical 5-card boards weigh 376,992");
    double worst = 0.0;
    double worst_entry = 0.0;
    for (const auto &term : terms) {
      double scale = 0.0;
      for (const auto value : term.cache)
        scale = std::max(scale, std::abs(value));
      for (std::size_t hand_class = 0; hand_class < 81U; ++hand_class) {
        require(denominator[hand_class] > 0.0, "every class is live on some board");
        const double mean = term.numerator[hand_class] / denominator[hand_class];
        const double error = std::abs(mean - term.cache[hand_class]);
        worst = std::max(worst, scale > 0.0 ? error / scale : error);
        if (std::abs(term.cache[hand_class]) > 1e-6 * scale && scale > 0.0)
          worst_entry = std::max(worst_entry, error / std::abs(term.cache[hand_class]));
        require(error <= 1e-9 * std::max(scale, 1e-300) || (scale == 0.0 && error == 0.0),
                std::string("V9b terminal level: board-weighted kernel mean = class cache (") +
                    std::string(name) + ", node " + std::to_string(term.node) + ", seat " +
                    std::to_string(term.seat) + ", class " + std::to_string(hand_class) + "): " +
                    std::to_string(error / std::max(scale, 1e-300)));
      }
    }
    std::cout << "V9b terminal level, " << name << ": " << terms.size() << " (node, seat) terms ("
              << terminal_terms << " at the " << cache.counts().all_in_terminals << " + "
              << cache.counts().fold_terminals << " preflop terminals, the rest after a fold), "
              << catalog.river_boards().size()
              << " canonical boards; worst |mean - cache| / max|cache of the node| " << worst
              << ", worst per entry " << worst_entry << " ("
              << std::chrono::duration<double>(Clock::now() - started).count() << " s)\n";
  }
}

} // namespace

int main(const int argc, char **argv) {
  try {
    std::filesystem::path resources_dir;
    std::filesystem::path buckets_dir;
    bool quick = false;
    std::string only;
    for (int index = 1; index < argc; ++index) {
      const std::string_view name = argv[index];
      if (name == "--quick") {
        quick = true;
        continue;
      }
      if (index + 1 >= argc)
        throw std::runtime_error("missing value for " + std::string(name));
      const std::string_view value = argv[++index];
      if (name == "--resources-dir")
        resources_dir = value;
      else if (name == "--buckets-dir")
        buckets_dir = value;
      else if (name == "--only")
        only = value;
      else
        throw std::runtime_error("unknown argument " + std::string(name));
    }
    auto table = ca::ThreeWayTable::load(resources_dir / std::string(ca::three_way_table_file_name));
    if (!table) {
      std::cout << "PREFLOP_BLUEPRINT_TRAINER3_ORACLE_TESTS=SKIP (no complete three-way table)\n";
      return 77;
    }
    const auto resources = load_resources(resources_dir, buckets_dir);
    const auto run = [&](const std::string_view test) { return only.empty() || only == test; };
    if (run("v13")) {
      const auto started = Clock::now();
      test_v13(resources, table.value());
      std::cout << "  (V13 " << std::chrono::duration<double>(Clock::now() - started).count()
                << " s)\n";
    }
    if (run("v8")) {
      const auto started = Clock::now();
      test_v8_terminal3(resources, table.value());
      std::cout << "  (V8 " << std::chrono::duration<double>(Clock::now() - started).count()
                << " s)\n";
    }
    if (run("v9b")) {
      std::vector<std::string_view> fixtures{"3WAY50_donk_rake25cap2.json"};
      if (!quick)
        fixtures.push_back("3WAY50_donk_rake.json");
      test_v9b_terminal(resources, table.value(), fixtures);
    }
    std::cout << "PREFLOP_BLUEPRINT_TRAINER3_ORACLE_TESTS=PASS assertions=" << assertions << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "PREFLOP_BLUEPRINT_TRAINER3_ORACLE_TESTS=FAIL " << error.what() << " after "
              << assertions << " assertions\n";
    return 1;
  }
}
