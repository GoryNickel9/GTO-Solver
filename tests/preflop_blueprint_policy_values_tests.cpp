// Phase 3b, V11 (PHASE3_SPEC_2026-09-30 sections 6.2 and 7): part A through
// Trainer::evaluate_policy_values. Every reference below is exact, so the
// comparisons are at 1e-9 antes (the spec's tolerance for one evaluation).
//  - v11hucd: the heads-up path. HU50_rake compiled as a checkdown (every
//    terminal preflop), a random class policy, the 19,998 canonical 5-card
//    boards with orbit weights: every seat's EV and preflop best-response
//    gain equal the step-1 class solver's (checkdown_classes.hpp,
//    heads_up_terminals); the rake and root identities hold; the per-class
//    and per-combo conventions agree on a suit-closed list (every suit image
//    of a sample of the canonical boards, see suit_closed_boards).
//  - v11three: the 3-seat path. The checkdown-compiled 3WAY50_donk_rake25cap2
//    with a random class policy, board_kernels mode over the same list:
//    EVs and per-seat gains equal the class solver's (three_way_terminals,
//    folded cards dead); the rake and root identities; the reach identities
//    of the first two seats (see reach_identity); per-class = per-combo on
//    the suit-closed list; the same list in class_cache mode gives the same
//    EVs (plumbing).
//  Conventions: a canonical list with orbit weights is not closed under the
//  suit permutations, so its per-combo means (value_sum / live_sum) are not
//  suit-symmetric and differ from the physical per-combo values; the pooled
//  class values are the exact per-combo values there (a preflop class is one
//  suit orbit of combos). The two conventions agree only on a suit-closed
//  list (02/10, T4: the first build showed a 0.25 a gap on the canonical list).
//  - v11hu: sampled smoke of the heads-up path on HU10 reduced with rake (the
//    200/500/1000 buckets, 3 physical flops): finite values, the rake and
//    root identities, bit-identical sums at 1 and 2 threads.
//  - v11state: a pass stopped after one chunk and resumed from its state
//    file reproduces the uninterrupted sums bit for bit; a state of another
//    board list is refused.
//  - v11smoke: the step-2 3WAY50 tree (200/500/1000 buckets, random policy),
//    one sampled flop in class_cache mode: finite values, the root identity,
//    the reach identities of the first two seats (see reach_identity),
//    bit-identical sums at 1 and 2 threads; the rake identity residual is
//    printed only (the cache's board-free preflop terminal values satisfy it
//    in expectation over the boards, spec 3.9, not on one flop).
//  - v11smokebk: the same policy and flop as an explicit list of its 1,056
//    runouts in board_kernels mode (every value board-restricted): the rake
//    identity at 1e-9 and the root identity; in class_cache mode the list
//    reproduces v11smoke's sampled pass bit for bit.
//  - --long, v11huexact: HU10 reduced with rake over the exact list (573
//    canonical flops, every runout, orbit weights) against the physical
//    evaluator's exact pass (BestResponseEvaluator, the engine of
//    gtosd_preflop_blueprint_monker_values, which expands every canonical
//    flop into its suit images): the pooled (per-class) combo values,
//    opponent reach, root values, EVs, the per-combo preflop response and the
//    expected rake (the rake view pass); the per-combo means of the canonical
//    list are printed for information only. The sampled conventions differ (the physical
//    evaluator keeps preflop terminals and the opponent reach at the deck
//    level), so only the exact list compares; 15-30 minutes.
#include "preflop_blueprint_test_support.hpp"

#include "../benchmarks/checkdown_classes.hpp"
#include "../benchmarks/monker_chart_exploitation.hpp"
#include "../benchmarks/monker_chart_values.hpp"

#include "gtosd/card_abstraction/deterministic_random.hpp"
#include "gtosd/card_abstraction/showdown_counts.hpp"
#include "gtosd/card_abstraction/three_way_table.hpp"
#include "gtosd/preflop_blueprint/trainer.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace pb_test;
namespace mc = gtosd::monker_charts;
namespace cdc = gtosd::checkdown_classes;

constexpr double tolerance = 1e-9;
constexpr std::size_t combos = mc::hero_combos;
constexpr std::size_t classes = mc::class_count;

pb::GameConfig load_monker(const std::string_view name) {
  const auto path =
      std::filesystem::path(GTOSD_SOURCE_DIR) / "benchmarks" / "monker" / std::string(name);
  const auto parsed = pb::parse_game_config_json(read_file(path));
  require(parsed.has_value(), std::string("monker config parses: ") + std::string(name));
  return parsed.value();
}

pb::CompiledGame compile(const pb::GameConfig &config, const bool checkdown = false) {
  pb::CompileOptions options;
  options.checkdown_at_flop = checkdown;
  auto game = pb::CompiledGame::compile(config, options);
  require(game.has_value(), "game compiles");
  return std::move(game.value());
}

double seconds_since(const Clock::time_point started) {
  return std::chrono::duration<double>(Clock::now() - started).count();
}

pb::BucketPolicy random_policy(const pb::CompiledGame &game, const Resources &resources,
                               const std::uint64_t seed) {
  const auto layout = pb::layout_state(game, resources.flop->capacity(),
                                       resources.turn->capacity(), resources.river->capacity());
  pb::BucketPolicy policy(game, layout);
  ca::DeterministicRandom random(seed);
  for (const auto &node : game.nodes()) {
    if (node.kind != pb::NodeKind::Decision)
      continue;
    const auto rows = pb::StateLayout::rows_for(node.street, layout.flop_capacity,
                                                layout.turn_capacity, layout.river_capacity);
    for (std::uint32_t row = 0; row < rows; ++row) {
      auto probabilities = policy.row(node.id, row);
      double total = 0.0;
      for (auto &probability : probabilities) {
        probability = 0.05 + random.uniform_unit();
        total += probability;
      }
      for (auto &probability : probabilities)
        probability /= total;
    }
  }
  return policy;
}

pb::BucketPolicy copy_policy(const pb::CompiledGame &game, const pb::BucketPolicy &policy) {
  return pb::BucketPolicy(game, policy.layout(), policy.table());
}

pb::TrainingBoards canonical_river_boards(const ca::BoardCatalog &catalog) {
  pb::TrainingBoards boards;
  boards.sample = false;
  for (const auto &board : catalog.river_boards()) {
    auto cards = board.cards;
    std::sort(cards.begin(), cards.end());
    ca::BoardHistory history;
    history.flop = {cards[0], cards[1], cards[2]};
    history.turn = cards[3];
    history.river = cards[4];
    boards.histories.push_back(history);
    boards.weights.push_back(static_cast<double>(board.multiplicity));
  }
  return boards;
}

// A suit-closed list: every distinct image under the 24 suit permutations of
// every `stride`-th canonical 5-card board, weight 1 each (the union of whole
// orbits, uniform over its physical boards). On such a list the per-combo
// means are suit-symmetric, so they equal the pooled class values.
pb::TrainingBoards suit_closed_boards(const ca::BoardCatalog &catalog, const std::size_t stride) {
  pb::TrainingBoards boards;
  boards.sample = false;
  const auto &river = catalog.river_boards();
  for (std::size_t index = 0; index < river.size(); index += stride) {
    std::vector<std::array<gtosd::CardId, 5>> images;
    for (const auto &permutation : ca::all_suit_permutations()) {
      std::array<gtosd::CardId, 5> cards{};
      for (std::size_t card = 0; card < cards.size(); ++card)
        cards[card] = ca::permute_card(river[index].cards[card], permutation);
      std::sort(cards.begin(), cards.end());
      if (std::find(images.begin(), images.end(), cards) != images.end())
        continue;
      images.push_back(cards);
      ca::BoardHistory history;
      history.flop = {cards[0], cards[1], cards[2]};
      history.turn = cards[3];
      history.river = cards[4];
      boards.histories.push_back(history);
      boards.weights.push_back(1.0);
    }
    require(images.size() == river[index].multiplicity,
            "suit_closed_boards: the distinct images are the board's orbit");
  }
  return boards;
}

// Our preflop rows of a hero (edge order), as the tools read them.
mc::PreflopStrategy policy_rows(const pb::BucketPolicy &policy,
                                const std::vector<std::uint32_t> &nodes) {
  mc::PreflopStrategy rows(nodes.size());
  for (std::size_t slot = 0; slot < nodes.size(); ++slot) {
    rows[slot].resize(classes);
    for (std::size_t hand_class = 0; hand_class < classes; ++hand_class) {
      const auto row = policy.row(nodes[slot], static_cast<std::uint32_t>(hand_class));
      rows[slot][hand_class].assign(row.begin(), row.end());
    }
  }
  return rows;
}

// The hero's action values from the sums, in the per-class (pooled) or the
// per-combo convention (the CLI's --values).
pb::PreflopActionValues action_values(const pb::PolicyValues &values, const std::size_t hero,
                                      const bool per_class) {
  const auto &table = ca::combo_table();
  const auto &totals = values.heroes[hero];
  std::vector<double> class_weight(classes, 0.0);
  for (std::size_t combo = 0; combo < combos; ++combo)
    class_weight[table.hand_class[combo]] += values.live_sum[combo];
  const auto mean = [&](const std::vector<double> &sums, const std::size_t offset) {
    std::vector<double> numerator(classes, 0.0);
    for (std::size_t combo = 0; combo < combos; ++combo)
      numerator[table.hand_class[combo]] += sums[offset + combo];
    std::vector<double> result(combos, 0.0);
    for (std::size_t combo = 0; combo < combos; ++combo) {
      const auto hand_class = table.hand_class[combo];
      // A combo (or class) dead on every board of the list keeps 0, as the
      // physical evaluator leaves an incompatible combo.
      if (per_class) {
        result[combo] = class_weight[hand_class] > 0.0 ? numerator[hand_class] / class_weight[hand_class]
                                                       : 0.0;
      } else {
        result[combo] =
            values.live_sum[combo] > 0.0 ? sums[offset + combo] / values.live_sum[combo] : 0.0;
      }
    }
    return result;
  };
  pb::PreflopActionValues pav;
  pav.hero = static_cast<std::uint8_t>(hero);
  pav.groups = values.chunks_done;
  pav.nodes = totals.nodes;
  const auto slots = totals.nodes.size();
  pav.combo_values.resize(slots);
  pav.opponent_reach.resize(slots);
  pav.class_ev.resize(slots);
  pav.class_se.resize(slots);
  pav.class_weight.resize(slots);
  for (std::size_t slot = 0; slot < slots; ++slot) {
    const auto actions = totals.actions[slot];
    pav.opponent_reach[slot] = mean(totals.reach_sum, slot * combos);
    pav.class_weight[slot].assign(classes, 0.0);
    for (std::size_t combo = 0; combo < combos; ++combo)
      pav.class_weight[slot][table.hand_class[combo]] += pav.opponent_reach[slot][combo];
    pav.combo_values[slot].resize(actions);
    pav.class_ev[slot].assign(actions, std::vector<double>(classes, 0.0));
    pav.class_se[slot].assign(actions, std::vector<double>(classes, 0.0));
    for (std::size_t action = 0; action < actions; ++action) {
      pav.combo_values[slot][action] = mean(totals.value_sum, totals.value_offset[slot] + action * combos);
      std::vector<double> numerator(classes, 0.0);
      for (std::size_t combo = 0; combo < combos; ++combo)
        numerator[table.hand_class[combo]] += pav.combo_values[slot][action][combo];
      for (std::size_t hand_class = 0; hand_class < classes; ++hand_class)
        pav.class_ev[slot][action][hand_class] =
            pav.class_weight[slot][hand_class] > 0.0
                ? numerator[hand_class] / pav.class_weight[slot][hand_class]
                : 0.0;
    }
  }
  pav.root_values = mean(totals.root_sum, 0U);
  return pav;
}

double mean_of(const std::vector<double> &values) {
  double total = 0.0;
  for (const auto value : values)
    total += value;
  return total / static_cast<double>(values.size());
}

double max_difference(const std::vector<double> &left, const std::vector<double> &right) {
  require(left.size() == right.size(), "vectors of equal size");
  double worst = 0.0;
  for (std::size_t index = 0; index < left.size(); ++index)
    worst = std::max(worst, std::abs(left[index] - right[index]));
  return worst;
}

// The largest gap between two conventions of the same pass: root values,
// opponent reach and combo values of every node.
double convention_gap(const pb::PreflopActionValues &left, const pb::PreflopActionValues &right) {
  require(left.nodes == right.nodes, "the same preflop nodes in both conventions");
  double worst = max_difference(left.root_values, right.root_values);
  for (std::size_t slot = 0; slot < left.nodes.size(); ++slot) {
    worst = std::max(worst, max_difference(left.opponent_reach[slot], right.opponent_reach[slot]));
    for (std::size_t action = 0; action < left.combo_values[slot].size(); ++action)
      worst = std::max(worst, max_difference(left.combo_values[slot][action],
                                             right.combo_values[slot][action]));
  }
  return worst;
}

bool all_finite(const pb::PreflopActionValues &pav) {
  const auto finite = [](const std::vector<double> &values) {
    return std::all_of(values.begin(), values.end(),
                       [](const double value) { return std::isfinite(value); });
  };
  if (!finite(pav.root_values))
    return false;
  for (std::size_t slot = 0; slot < pav.nodes.size(); ++slot) {
    if (!finite(pav.opponent_reach[slot]))
      return false;
    for (const auto &action : pav.combo_values[slot])
      if (!finite(action))
        return false;
  }
  return true;
}

// The root identity of monker_in_our_game.py: root(h) - sum over the top
// nodes of V_ours(m, h) - c (1 - sum over the top nodes of the opponent reach),
// c the hero's payoff where everybody before it folds (0 when it always acts).
double root_identity(const pb::CompiledGame &game, const mc::HeroTree &tree,
                     const pb::PreflopActionValues &pav, const mc::PreflopStrategy &ours,
                     const std::size_t hero) {
  const auto &table = ca::combo_table();
  double ended_payoff = 0.0;
  std::uint32_t node_id = game.root();
  while (true) {
    const auto &node = game.nodes()[node_id];
    if (node.kind == pb::NodeKind::TerminalFold) {
      ended_payoff = static_cast<double>(game.fold_payoffs(node.id)[hero]) /
                     static_cast<double>(gtosd::Money::units_per_ante);
      break;
    }
    if (node.kind != pb::NodeKind::Decision || node.actor == hero)
      break;
    node_id = game.edges_of(node_id)[0].child;
  }
  double worst = 0.0;
  for (std::size_t combo = 0; combo < combos; ++combo) {
    const auto hand_class = table.hand_class[combo];
    double residual = pav.root_values[combo];
    double ended = 1.0;
    for (const auto slot : tree.top) {
      for (std::size_t action = 0; action < pav.combo_values[slot].size(); ++action)
        residual -= ours[slot][hand_class][action] * pav.combo_values[slot][action][combo];
      ended -= pav.opponent_reach[slot][combo];
    }
    worst = std::max(worst, std::abs(residual - ended_payoff * ended));
  }
  return worst;
}

// D3 of the part-A review (02/10): the 3-seat opponent reach, pooled per class.
// The root identity involves the reach only through c (1 - sum of the top-node
// reaches), and c = 0 for the seats that always act (UTG and CO), so a wrong
// scale or disjointness of their reach escapes it. With the other seats' reach
// at the hero's first preflop decisions (the top nodes):
//  - the seat acting at the root has one top node, the root, which the other
//    seats reach with probability 1 on every combo;
//  - every action of that seat leads to a first decision of the second seat,
//    so the second seat's top-node reaches sum to 1 on every combo;
//  - the third seat's top-node reaches plus P(the earlier seats fold | h) sum
//    to 1: that is the root identity's ended term (c != 0), checked there.
// Every combo of a class live on the list is checked (a dead class keeps 0).
struct ReachIdentity {
  // The seat's place in the first round, along the fold line from the root
  // (the walk of root_identity).
  std::size_t position{0U};
  // First two seats: the largest |reach - 1| and the combos checked.
  double gap{0.0};
  std::size_t checked{0U};
  // Third seat (information): the range of 1 - sum of the top-node reaches.
  double ended_min{std::numeric_limits<double>::infinity()};
  double ended_max{-std::numeric_limits<double>::infinity()};
};

ReachIdentity reach_identity(const pb::CompiledGame &game, const mc::HeroTree &tree,
                             const pb::PreflopActionValues &pav, const pb::PolicyValues &values,
                             const std::size_t hero) {
  ReachIdentity result;
  std::uint32_t node_id = game.root();
  while (true) {
    const auto &node = game.nodes()[node_id];
    if (node.kind != pb::NodeKind::Decision || node.actor == hero)
      break;
    ++result.position;
    node_id = game.edges_of(node_id)[0].child;
  }
  if (result.position == 0U)
    require(tree.top.size() == 1U && pav.nodes[tree.top[0]] == game.root(),
            "reach identity: the first seat's only top node is the root");
  const auto &table = ca::combo_table();
  std::vector<double> class_live(classes, 0.0);
  for (std::size_t combo = 0; combo < combos; ++combo)
    class_live[table.hand_class[combo]] += values.live_sum[combo];
  for (std::size_t combo = 0; combo < combos; ++combo) {
    if (class_live[table.hand_class[combo]] <= 0.0)
      continue;
    double reach = 0.0;
    for (const auto slot : tree.top)
      reach += pav.opponent_reach[slot][combo];
    if (result.position < 2U) {
      result.gap = std::max(result.gap, std::abs(reach - 1.0));
      ++result.checked;
    } else {
      result.ended_min = std::min(result.ended_min, 1.0 - reach);
      result.ended_max = std::max(result.ended_max, 1.0 - reach);
    }
  }
  return result;
}

// Prints the reach identity of a 3-seat hero and requires it for the first two
// seats (1e-9).
void check_reach_identity(const pb::CompiledGame &game, const mc::HeroTree &tree,
                          const pb::PreflopActionValues &pooled, const pb::PolicyValues &values,
                          const std::size_t hero, const std::string &label) {
  const auto reach = reach_identity(game, tree, pooled, values, hero);
  std::cout << "  " << label << " seat " << hero << ": ";
  if (reach.position == 0U) {
    std::cout << "first to act, opponent reach at the root - 1: largest gap " << reach.gap
              << " over " << reach.checked << " combos\n";
    require(reach.checked > 0U && reach.gap <= tolerance,
            label + ": the first seat's opponent reach at the root is 1 on every combo");
  } else if (reach.position == 1U) {
    std::cout << "second to act, sum of the top-node opponent reaches - 1: largest gap "
              << reach.gap << " over " << reach.checked << " combos\n";
    require(reach.checked > 0U && reach.gap <= tolerance,
            label + ": the second seat's top-node opponent reaches sum to 1 on every combo");
  } else {
    std::cout << "third to act, top-node reaches + P(earlier seats fold) = 1 is the root "
                 "identity's ended term; P(earlier seats fold) in ["
              << reach.ended_min << ", " << reach.ended_max << "]\n";
  }
}

bool same_sums(const pb::PolicyValues &left, const pb::PolicyValues &right) {
  if (left.chunks_done != right.chunks_done || left.boards != right.boards ||
      left.live_sum != right.live_sum || left.rake_sum != right.rake_sum ||
      left.heroes.size() != right.heroes.size())
    return false;
  for (std::size_t hero = 0; hero < left.heroes.size(); ++hero) {
    const auto &a = left.heroes[hero];
    const auto &b = right.heroes[hero];
    if (a.nodes != b.nodes || a.value_sum != b.value_sum || a.reach_sum != b.reach_sum ||
        a.root_sum != b.root_sum || a.ev_direct != b.ev_direct)
      return false;
  }
  return true;
}

pb::BestResponseResources response_resources(const Resources &resources) {
  pb::BestResponseResources view;
  view.ranks = &resources.ranks.value();
  view.all_in = &resources.all_in.value();
  view.catalog = &resources.catalog.value();
  view.flop = &resources.flop.value();
  view.turn = &resources.turn.value();
  view.river = &resources.river.value();
  return view;
}

// ---------------------------------------------------------------- the class-solver legs

// A checkdown game (2 or 3 seats) with a random class policy over the 19,998
// canonical boards against the class solver locked on that policy: EV, the
// per-class preflop gain, the rake and root identities; per-class = per-combo
// on a suit-closed list (the canonical list's per-combo means are printed for
// information: that list is not suit-closed, see the header).
void test_checkdown_leg(const Resources &resources, const ca::ThreeWayTable *table,
                        const std::string_view name, const unsigned threads,
                        const std::string_view label) {
  const auto started = Clock::now();
  const auto game = compile(load_monker(name), true);
  const auto seats = static_cast<std::size_t>(game.config().player_count);
  const auto policy = random_policy(game, resources, 0x5041'0003ULL + seats);
  const auto boards = canonical_river_boards(resources.catalog.value());
  auto view = resources.view();
  view.three_way = table;
  auto config = resources.config();
  config.threads = threads;
  config.partition_target_nodes = 64U;
  if (seats == 3U) {
    config.validation = true;
    config.preflop_terminals = pb::PreflopTerminals::BoardKernels;
  }
  pb::PolicyValuesOptions options;
  options.boards = pb::PolicyValuesBoardKind::List;
  options.list = &boards;
  options.chunk_boards = 1'056U;
  const auto pass_started = Clock::now();
  auto evaluated = pb::Trainer::evaluate_policy_values(game, view, config,
                                                       copy_policy(game, policy), options);
  require(evaluated.has_value(), std::string("part A evaluates (") + std::string(label) + "): " +
                                     (evaluated ? "" : pb::trainer_error_name(evaluated.error())));
  const double pass_seconds = seconds_since(pass_started);
  const auto &values = evaluated.value();
  require(values.complete && values.boards == boards.histories.size(), "every board evaluated");
  require(std::abs(values.weight - 1.0) <= 1e-12, "the weights sum to one");

  // The class solver with the policy locked on every row.
  const auto terminals =
      seats == 3U ? cdc::three_way_terminals(game, *table, resources.all_in.value(),
                                             ca::FoldedCards::Dead)
                  : cdc::heads_up_terminals(game, resources.all_in.value());
  cdc::ClassSolver solver(game, terminals, 1.5, 0.0, 2.0, threads);
  for (const auto &node : game.nodes()) {
    if (node.kind != pb::NodeKind::Decision)
      continue;
    for (std::size_t hand_class = 0; hand_class < classes; ++hand_class) {
      const auto row = policy.row(node.id, static_cast<std::uint32_t>(hand_class));
      solver.lock_row(node.id, hand_class, std::vector<double>(row.begin(), row.end()));
    }
  }
  double ev_sum = 0.0;
  double ev_direct_sum = 0.0;
  for (std::size_t hero = 0; hero < seats; ++hero) {
    const auto reference_ev = solver.value(static_cast<std::uint8_t>(hero), cdc::Mode::Average);
    const auto reference_gain =
        solver.value(static_cast<std::uint8_t>(hero), cdc::Mode::BestResponse) - reference_ev;
    const auto pooled = action_values(values, hero, true);
    const auto per_combo = action_values(values, hero, false);
    const auto rows = policy_rows(policy, pooled.nodes);
    const auto tree = mc::hero_tree(game, pooled.nodes, static_cast<std::uint8_t>(hero));
    const auto response = mc::preflop_response(tree, pooled, rows);
    const double ev = mean_of(pooled.root_values);
    const double ev_gap = std::abs(ev - reference_ev);
    const double gain_gap = std::abs(response.gain_per_class - reference_gain);
    const double combo_gap = std::abs(response.gain_per_combo - response.gain_per_class);
    const double canonical_gap = convention_gap(pooled, per_combo);
    const double identity = root_identity(game, tree, pooled, rows, hero);
    ev_sum += ev;
    ev_direct_sum += values.heroes[hero].ev_direct / values.weight;
    std::cout << "  " << label << " seat " << hero << ": EV " << ev << " (class solver "
              << reference_ev << ", gap " << ev_gap << "), preflop gain " << response.gain_per_class
              << " (class solver " << reference_gain << ", gap " << gain_gap
              << "), per combo vs per class " << combo_gap
              << ", canonical-list convention gap (information) " << canonical_gap
              << ", root identity " << identity << '\n';
    require(ev_gap <= tolerance, std::string(label) + ": EV equals the class solver's");
    require(gain_gap <= tolerance, std::string(label) + ": preflop gain equals the class solver's");
    require(combo_gap <= tolerance, std::string(label) + ": per-combo and per-class responses agree");
    require(identity <= tolerance, std::string(label) + ": root identity");
    if (seats == 3U)
      check_reach_identity(game, tree, pooled, values, hero, std::string(label));
  }
  const double rake = values.rake_sum / values.weight;
  std::cout << "  " << label << ": EV sum " << ev_sum << " (direct " << ev_direct_sum
            << "), expected rake " << rake << ", residuals " << ev_sum + rake << " / "
            << ev_direct_sum + rake << "; pass " << std::fixed << std::setprecision(1)
            << pass_seconds << " s" << std::defaultfloat << std::setprecision(6) << '\n';
  require(rake > 0.0, std::string(label) + ": the fixture has rake");
  require(std::abs(ev_sum + rake) <= tolerance, std::string(label) + ": the pooled EVs sum to minus the rake");
  require(std::abs(ev_direct_sum + rake) <= tolerance,
          std::string(label) + ": the direct EVs sum to minus the rake");

  {
    // The two conventions on a suit-closed list (whole orbits of every 199th
    // canonical board, same mode and policy): per-combo = per-class.
    const auto closed = suit_closed_boards(resources.catalog.value(), 199U);
    auto closed_options = options;
    closed_options.list = &closed;
    const auto closed_started = Clock::now();
    auto closed_values = pb::Trainer::evaluate_policy_values(game, view, config,
                                                             copy_policy(game, policy), closed_options);
    require(closed_values.has_value(),
            std::string("part A evaluates (") + std::string(label) + ", suit-closed list): " +
                (closed_values ? "" : pb::trainer_error_name(closed_values.error())));
    require(closed_values.value().complete &&
                closed_values.value().boards == closed.histories.size(),
            "every board of the suit-closed list evaluated");
    double closed_gap = 0.0;
    for (std::size_t hero = 0; hero < seats; ++hero)
      closed_gap = std::max(closed_gap, convention_gap(action_values(closed_values.value(), hero, true),
                                                       action_values(closed_values.value(), hero, false)));
    std::cout << "  " << label << ": suit-closed list (" << closed.histories.size()
              << " boards) per-class vs per-combo gap " << closed_gap << " (" << std::fixed
              << std::setprecision(1) << seconds_since(closed_started) << " s)" << std::defaultfloat
              << std::setprecision(6) << '\n';
    require(closed_gap <= tolerance,
            std::string(label) + ": per-class and per-combo values agree on a suit-closed list");
  }

  if (seats == 3U) {
    // Plumbing: the same list in class_cache mode (every value from the cache).
    config.validation = false;
    config.preflop_terminals = pb::PreflopTerminals::ClassCache;
    auto cache = pb::Trainer::evaluate_policy_values(game, view, config, copy_policy(game, policy),
                                                     options);
    require(cache.has_value(), std::string("part A evaluates (3-way, class_cache): ") +
                                   (cache ? "" : pb::trainer_error_name(cache.error())));
    double cache_gap = 0.0;
    for (std::size_t hero = 0; hero < seats; ++hero) {
      const auto pooled = action_values(cache.value(), hero, true);
      cache_gap = std::max(cache_gap, std::abs(mean_of(pooled.root_values) -
                                               solver.value(static_cast<std::uint8_t>(hero),
                                                            cdc::Mode::Average)));
    }
    std::cout << "  " << label << ": class_cache EV gap " << cache_gap << ", rake residual "
              << ev_sum + cache.value().rake_sum / cache.value().weight << '\n';
    require(cache_gap <= tolerance, std::string(label) + ": class_cache EVs equal the class solver's");
    // Proposal (T5 D.1, 02/10): on the exact list the class_cache values satisfy the rake identity.
    double cache_direct = 0.0;
    for (std::size_t hero = 0; hero < seats; ++hero)
      cache_direct += cache.value().heroes[hero].ev_direct / cache.value().weight;
    const double cache_residual = cache_direct + cache.value().rake_sum / cache.value().weight;
    std::cout << "  " << label << ": class_cache direct EV sum " << cache_direct
              << ", residual " << cache_residual << " (gated, proposal)\n";
    require(std::abs(cache_residual) <= tolerance,
            std::string(label) + ": class_cache, the direct EVs sum to minus the rake on the exact list");
  }
  std::cout << "  " << label << " total " << seconds_since(started) << " s\n";
}

// ---------------------------------------------------------------- sampled smokes

// A sampled pass: finite values, the rake and root identities, bit-identical
// sums at 1 and 2 threads.
void test_sampled_smoke(const Resources &resources, const ca::ThreeWayTable *table,
                        const pb::CompiledGame &game, const std::uint64_t seed,
                        const std::uint32_t flops, const std::string_view label) {
  const auto started = Clock::now();
  const auto policy = random_policy(game, resources, seed);
  auto view = resources.view();
  view.three_way = table;
  pb::PolicyValuesOptions options;
  options.boards = pb::PolicyValuesBoardKind::PhysicalFlops;
  options.flops = flops;
  options.seed = seed + 1U;
  const auto seats = static_cast<std::size_t>(game.config().player_count);
  std::optional<pb::PolicyValues> single;
  for (const unsigned threads : {1U, 2U}) {
    auto config = resources.config();
    config.threads = threads;
    config.partition_target_nodes = 64U;
    const auto run_started = Clock::now();
    auto result = pb::Trainer::evaluate_policy_values(game, view, config, copy_policy(game, policy),
                                                      options);
    require(result.has_value(), std::string("part A evaluates (") + std::string(label) + "): " +
                                    (result ? "" : pb::trainer_error_name(result.error())));
    const auto &values = result.value();
    require(values.complete && values.boards == flops * 1056U, "every runout of every flop");
    double ev_sum = 0.0;
    for (std::size_t hero = 0; hero < seats; ++hero) {
      const auto pooled = action_values(values, hero, true);
      const auto rows = policy_rows(policy, pooled.nodes);
      const auto tree = mc::hero_tree(game, pooled.nodes, static_cast<std::uint8_t>(hero));
      require(all_finite(pooled), "finite values");
      const double identity = root_identity(game, tree, pooled, rows, hero);
      const auto response = mc::preflop_response(tree, pooled, rows);
      ev_sum += values.heroes[hero].ev_direct / values.weight;
      std::cout << "  " << label << " threads " << threads << " seat " << hero << ": EV "
                << mean_of(pooled.root_values) << ", preflop gain " << response.gain_per_class
                << ", root identity " << identity << '\n';
      require(identity <= tolerance, std::string(label) + ": root identity");
      if (seats == 3U)
        check_reach_identity(game, tree, pooled, values, hero,
                             std::string(label) + " threads " + std::to_string(threads));
    }
    const double rake = values.rake_sum / values.weight;
    // Heads-up every value is board-restricted: the identity is exact on the
    // sampled flop. The 3-seat pass runs in class_cache mode, whose board-free
    // preflop terminal values satisfy it only in expectation over the boards
    // (printed; v11smokebk checks the 3-seat identity in board_kernels mode).
    std::cout << "  " << label << " threads " << threads << ": direct EV sum " << ev_sum
              << ", expected rake " << rake << ", residual " << ev_sum + rake
              << (seats == 2U ? "" : " (class_cache, sampled: information)") << " ("
              << std::fixed << std::setprecision(1) << seconds_since(run_started) << " s)"
              << std::defaultfloat << std::setprecision(6) << '\n';
    require(rake > 0.0, std::string(label) + ": the fixture has rake");
    if (seats == 2U)
      require(std::abs(ev_sum + rake) <= tolerance,
              std::string(label) + ": the direct EVs sum to minus the rake");
    if (!single)
      single = values;
    else
      require(same_sums(*single, values),
              std::string(label) + ": 1 and 2 threads give bit-identical sums");
  }
  std::cout << "  " << label << " total " << seconds_since(started) << " s\n";
}

// v11smokebk: the step-2 3WAY50 tree of v11smoke (same policy, same sampled
// flop) over the explicit list of the flop's 1,056 ordered runouts, once in
// board_kernels mode (validation; every preflop terminal on the listed board)
// and once in class_cache mode. In board_kernels mode every value is
// board-restricted, so the direct EVs sum to minus the expected rake on the
// list itself (1e-9). In class_cache mode the preflop terminals carry the
// cache's board-free class values (spec 3.9), whose sum over the seats equals
// minus the rake only in expectation over the boards (tower property): the
// residual is printed, and the pass must reproduce v11smoke's sampled pass bit
// for bit (same boards, same weights).
void test_step2_board_kernels(const Resources &resources, const ca::ThreeWayTable &table,
                              const unsigned threads) {
  const auto started = Clock::now();
  const auto game = compile(load_monker("3WAY50_donk_rake25cap2.json"));
  const std::uint64_t seed = 0x5041'0007ULL;
  const auto policy = random_policy(game, resources, seed);
  ca::DeterministicRandom random(seed + 1U);
  const auto flop = resources.catalog->sample_physical_history(random).flop;
  pb::TrainingBoards boards;
  boards.sample = false;
  {
    std::uint64_t mask = 0U;
    for (const auto card : flop)
      mask |= std::uint64_t{1} << card.value();
    for (std::uint8_t turn = 0; turn < 36U; ++turn) {
      if ((mask & (std::uint64_t{1} << turn)) != 0U)
        continue;
      for (std::uint8_t river = 0; river < 36U; ++river) {
        if (river == turn || (mask & (std::uint64_t{1} << river)) != 0U)
          continue;
        ca::BoardHistory history;
        history.flop = flop;
        history.turn = gtosd::CardId::from_index(turn).value();
        history.river = gtosd::CardId::from_index(river).value();
        boards.histories.push_back(history);
        boards.weights.push_back(1.0);
      }
    }
  }
  require(boards.histories.size() == 1'056U, "v11smokebk: 1,056 runouts");
  auto view = resources.view();
  view.three_way = &table;
  pb::PolicyValuesOptions options;
  options.boards = pb::PolicyValuesBoardKind::List;
  options.list = &boards;
  options.chunk_boards = 1'056U;
  pb::PolicyValuesOptions sampled;
  sampled.boards = pb::PolicyValuesBoardKind::PhysicalFlops;
  sampled.flops = 1U;
  sampled.seed = seed + 1U;
  for (const bool kernels : {true, false}) {
    auto config = resources.config();
    config.threads = threads;
    config.partition_target_nodes = 64U;
    if (kernels) {
      config.validation = true;
      config.preflop_terminals = pb::PreflopTerminals::BoardKernels;
    }
    const auto run_started = Clock::now();
    auto result = pb::Trainer::evaluate_policy_values(game, view, config, copy_policy(game, policy),
                                                      options);
    require(result.has_value(), std::string("part A evaluates (v11smokebk, ") +
                                    (kernels ? "board_kernels" : "class_cache") + "): " +
                                    (result ? "" : pb::trainer_error_name(result.error())));
    const auto &values = result.value();
    require(values.complete && values.boards == 1'056U, "v11smokebk: every runout");
    double ev_sum = 0.0;
    for (std::size_t hero = 0; hero < 3U; ++hero) {
      const auto pooled = action_values(values, hero, true);
      const auto rows = policy_rows(policy, pooled.nodes);
      const auto tree = mc::hero_tree(game, pooled.nodes, static_cast<std::uint8_t>(hero));
      require(all_finite(pooled), "finite values");
      const double identity = root_identity(game, tree, pooled, rows, hero);
      ev_sum += values.heroes[hero].ev_direct / values.weight;
      std::cout << "  v11smokebk " << (kernels ? "board_kernels" : "class_cache") << " seat " << hero
                << ": direct EV " << values.heroes[hero].ev_direct / values.weight
                << ", root identity " << identity << '\n';
      require(identity <= tolerance, "v11smokebk: root identity");
    }
    const double rake = values.rake_sum / values.weight;
    std::cout << "  v11smokebk " << (kernels ? "board_kernels" : "class_cache")
              << ": direct EV sum " << ev_sum << ", expected rake " << rake << ", residual "
              << ev_sum + rake << " (" << std::fixed << std::setprecision(1)
              << seconds_since(run_started) << " s)" << std::defaultfloat << std::setprecision(6)
              << '\n';
    if (kernels) {
      require(rake > 0.0 && std::abs(ev_sum + rake) <= tolerance,
              "v11smokebk: board_kernels, the direct EVs sum to minus the rake");
    } else {
      auto reference = pb::Trainer::evaluate_policy_values(game, view, config,
                                                           copy_policy(game, policy), sampled);
      require(reference.has_value(), "v11smokebk: the sampled pass evaluates");
      require(reference.value().rake_sum == values.rake_sum &&
                  reference.value().heroes[0].ev_direct == values.heroes[0].ev_direct &&
                  reference.value().heroes[1].ev_direct == values.heroes[1].ev_direct &&
                  reference.value().heroes[2].ev_direct == values.heroes[2].ev_direct,
              "v11smokebk: the explicit list reproduces the sampled flop bit for bit");
    }
  }
  std::cout << "  v11smokebk total " << seconds_since(started) << " s\n";
}

// ---------------------------------------------------------------- v11state

void test_v11_state(const Resources &resources, const std::filesystem::path &scratch,
                    const unsigned threads) {
  const auto started = Clock::now();
  const auto game = compile(load_fixture("preflop_blueprint_hu10_reduced_rake_v1.json"));
  const auto policy = random_policy(game, resources, 0x5041'0005ULL);
  auto config = resources.config();
  config.threads = threads;
  config.partition_target_nodes = 64U;
  pb::PolicyValuesOptions options;
  options.boards = pb::PolicyValuesBoardKind::PhysicalFlops;
  options.flops = 3U;
  options.seed = 23U;
  auto whole = pb::Trainer::evaluate_policy_values(game, resources.view(), config,
                                                   copy_policy(game, policy), options);
  require(whole.has_value(), "uninterrupted pass");
  std::filesystem::create_directories(scratch);
  const auto state = scratch / "v11state.bin";
  std::filesystem::remove(state);
  options.state_path = state;
  options.stop_after_chunks = 1U;
  auto first = pb::Trainer::evaluate_policy_values(game, resources.view(), config,
                                                   copy_policy(game, policy), options);
  require(first.has_value() && !first.value().complete && first.value().chunks_done == 1U,
          "the first call stops after one chunk");
  require(std::filesystem::exists(state), "the state file exists");
  options.stop_after_chunks = 0U;
  auto second = pb::Trainer::evaluate_policy_values(game, resources.view(), config,
                                                    copy_policy(game, policy), options);
  require(second.has_value() && second.value().complete && second.value().resumed,
          "the second call resumes and completes");
  require(same_sums(whole.value(), second.value()), "v11state: the resumed sums are bit-identical");
  require(whole.value().series.size() == second.value().series.size() &&
              whole.value().series[1].rake == second.value().series[1].rake,
          "v11state: the chunk series is the same");
  // Another board list must be refused.
  options.seed = 24U;
  auto other = pb::Trainer::evaluate_policy_values(game, resources.view(), config,
                                                   copy_policy(game, policy), options);
  require(!other.has_value() && other.error() == pb::TrainerError::IntegrityFailure,
          "v11state: a state of another board list is refused");
  std::cout << "  v11state: resumed pass bit-identical, foreign state refused ("
            << seconds_since(started) << " s)\n";
}

// ---------------------------------------------------------------- v11huexact (--long)

void test_v11_hu_exact(const Resources &resources, const unsigned threads) {
  const auto started = Clock::now();
  const auto game = compile(load_fixture("preflop_blueprint_hu10_reduced_rake_v1.json"));
  const auto policy = random_policy(game, resources, 0x5041'0009ULL);
  auto config = resources.config();
  config.threads = threads;
  config.partition_target_nodes = 64U;
  pb::PolicyValuesOptions options;
  options.boards = pb::PolicyValuesBoardKind::CanonicalFlops;
  options.flops = 0U;
  options.progress = [](const pb::PolicyValuesProgress &progress) {
    if (progress.chunks_done % 50U == 0U || progress.chunks_done == progress.chunks_total)
      std::cout << "    part A flops " << progress.chunks_done << "/" << progress.chunks_total
                << " (" << std::fixed << std::setprecision(0) << progress.seconds << " s)"
                << std::defaultfloat << std::setprecision(6) << '\n';
  };
  const auto pass_started = Clock::now();
  auto mine = pb::Trainer::evaluate_policy_values(game, resources.view(), config,
                                                  copy_policy(game, policy), options);
  require(mine.has_value(), std::string("part A evaluates (HU exact): ") +
                                (mine ? "" : pb::trainer_error_name(mine.error())));
  const double pass_seconds = seconds_since(pass_started);
  const auto &values = mine.value();
  require(values.complete && values.chunks_done == 573U && values.boards == 605'088U,
          "every runout of every canonical flop");

  // The physical evaluator's exact pass (canonical flops standing for their orbits).
  const auto view = response_resources(resources);
  const auto evaluator = pb::BestResponseEvaluator::create(game, policy, view);
  require(evaluator.has_value(), "physical evaluator creates");
  std::vector<pb::FlopGroup> groups;
  for (const auto &flop : resources.catalog->flops()) {
    auto cards = flop.cards;
    std::sort(cards.begin(), cards.end());
    groups.push_back(pb::full_runouts(cards));
  }
  const auto physical_started = Clock::now();
  auto flop_values = pb::evaluate_flops(evaluator.value(), groups, threads);
  require(flop_values.has_value(), "physical flops evaluate");
  std::vector<const pb::FlopValues *> pointers;
  for (auto &entry : flop_values.value()) {
    entry.images = pb::flop_images(entry.flop);
    pointers.push_back(&entry);
  }
  const auto report = evaluator.value().aggregate(pointers, true);
  require(report.has_value(), "physical exact aggregate");
  const auto rake_pass =
      mc::evaluate_policy(game.rake_view(), policy, view, groups, true, true, threads,
                          pb::RiverEngine::Joint, pb::DeviationStreet::Preflop);
  const double physical_seconds = seconds_since(physical_started);

  double ev_direct_sum = 0.0;
  for (std::size_t hero = 0; hero < 2U; ++hero) {
    const auto reference =
        evaluator.value().preflop_action_values(pointers, static_cast<std::uint8_t>(hero));
    require(reference.has_value(), "physical preflop action values");
    const auto &theirs = reference.value();
    for (const bool per_class : {true, false}) {
      const auto ours = action_values(values, hero, per_class);
      require(ours.nodes == theirs.nodes, "the same preflop nodes");
      double worst_value = 0.0;
      double worst_reach = 0.0;
      for (std::size_t slot = 0; slot < ours.nodes.size(); ++slot) {
        worst_reach = std::max(worst_reach, max_difference(ours.opponent_reach[slot],
                                                           theirs.opponent_reach[slot]));
        for (std::size_t action = 0; action < ours.combo_values[slot].size(); ++action)
          worst_value = std::max(worst_value, max_difference(ours.combo_values[slot][action],
                                                             theirs.combo_values[slot][action]));
      }
      const double worst_root = max_difference(ours.root_values, theirs.root_values);
      const double ev = mean_of(ours.root_values);
      const auto rows = policy_rows(policy, ours.nodes);
      const auto tree = mc::hero_tree(game, ours.nodes, static_cast<std::uint8_t>(hero));
      const auto response = mc::preflop_response(tree, ours, rows);
      const double gain_gap = std::abs(response.gain_per_combo - report.value().gain_preflop[hero]);
      const double ev_gap = std::abs(ev - report.value().ev[hero]);
      std::cout << "  v11huexact hero " << hero
                << (per_class ? " per class" : " per combo (canonical list, information)")
                << ": combo values " << worst_value << ", opponent reach " << worst_reach
                << ", root values " << worst_root << ", EV " << ev << " (physical "
                << report.value().ev[hero] << ", gap " << ev_gap << "), preflop gain "
                << response.gain_per_combo << " (physical " << report.value().gain_preflop[hero]
                << ", gap " << gain_gap << ")\n";
      // The physical evaluator's per-combo values average every suit image of
      // a canonical flop: they equal our pooled class values. Our per-combo
      // means of the canonical list are not suit-symmetric (see the header).
      if (!per_class)
        continue;
      require(worst_value <= tolerance, "v11huexact: combo values equal the physical evaluator's");
      require(worst_reach <= tolerance, "v11huexact: opponent reach equals the physical evaluator's");
      require(worst_root <= tolerance, "v11huexact: root values equal the physical evaluator's");
      require(ev_gap <= tolerance, "v11huexact: EV equals the physical evaluator's");
      require(gain_gap <= tolerance, "v11huexact: the preflop response gain equals the aggregate's");
    }
    ev_direct_sum += values.heroes[hero].ev_direct / values.weight;
  }
  const double rake = values.rake_sum / values.weight;
  const double physical_rake = -rake_pass.report.ev[0];
  std::cout << "  v11huexact: direct EV sum " << ev_direct_sum << ", expected rake " << rake
            << " (physical rake view " << physical_rake << ", gap " << std::abs(rake - physical_rake)
            << "), residual " << ev_direct_sum + rake << "; part A " << std::fixed
            << std::setprecision(1) << pass_seconds << " s, physical " << physical_seconds << " s ("
            << seconds_since(started) << " s)" << std::defaultfloat << std::setprecision(6) << '\n';
  require(std::abs(rake - physical_rake) <= tolerance,
          "v11huexact: the expected rake equals the physical rake view's");
  require(std::abs(ev_direct_sum + rake) <= tolerance,
          "v11huexact: the direct EVs sum to minus the rake");
}

} // namespace

int main(const int argc, char **argv) {
  try {
    std::filesystem::path resources_dir;
    std::filesystem::path buckets_dir;
    std::filesystem::path scratch_dir;
    std::string only;
    unsigned threads = 2U;
    bool long_run = false;
    for (int index = 1; index < argc; ++index) {
      const std::string_view name = argv[index];
      if (name == "--long") {
        long_run = true;
        continue;
      }
      if (index + 1 >= argc)
        throw std::runtime_error("missing value for " + std::string(name));
      const std::string value = argv[++index];
      if (name == "--resources-dir")
        resources_dir = value;
      else if (name == "--buckets-dir")
        buckets_dir = value;
      else if (name == "--scratch-dir")
        scratch_dir = value;
      else if (name == "--only")
        only = value;
      else if (name == "--threads")
        threads = static_cast<unsigned>(std::stoul(value));
      else
        throw std::runtime_error("unknown argument " + std::string(name));
    }
    if (scratch_dir.empty())
      scratch_dir = std::filesystem::temp_directory_path() / "gtosd_policy_values_tests";
    auto table = ca::ThreeWayTable::load(resources_dir / std::string(ca::three_way_table_file_name));
    if (!table) {
      std::cout << "PREFLOP_BLUEPRINT_POLICY_VALUES_TESTS=SKIP (no complete three-way table)\n";
      return 77;
    }
    const auto resources = load_resources(resources_dir, buckets_dir);
    const auto run = [&](const std::string_view test) { return only.empty() || only == test; };
    const auto timed = [](const std::string_view label, const auto &function) {
      const auto started = Clock::now();
      function();
      std::cout << "  (" << label << " " << std::fixed << std::setprecision(1)
                << seconds_since(started) << " s)" << std::defaultfloat << std::setprecision(6)
                << '\n';
    };
    if (long_run) {
      if (run("v11huexact"))
        timed("V11 HU exact", [&] { test_v11_hu_exact(resources, threads); });
    } else {
      if (run("v11hu"))
        timed("V11 HU sampled smoke", [&] {
          test_sampled_smoke(resources, nullptr,
                             compile(load_fixture("preflop_blueprint_hu10_reduced_rake_v1.json")),
                             0x5041'0001ULL, 3U, "v11hu");
        });
      if (run("v11state"))
        timed("V11 state", [&] { test_v11_state(resources, scratch_dir, threads); });
      if (run("v11hucd"))
        timed("V11 HU checkdown", [&] {
          test_checkdown_leg(resources, nullptr, "HU50_rake.json", threads, "v11hucd");
        });
      if (run("v11smoke"))
        timed("V11 3-way smoke", [&] {
          test_sampled_smoke(resources, &table.value(),
                             compile(load_monker("3WAY50_donk_rake25cap2.json")), 0x5041'0007ULL,
                             1U, "v11smoke");
        });
      if (run("v11smokebk"))
        timed("V11 3-way step 2, board kernels", [&] {
          test_step2_board_kernels(resources, table.value(), threads);
        });
      if (run("v11three"))
        timed("V11 3-way checkdown", [&] {
          test_checkdown_leg(resources, &table.value(), "3WAY50_donk_rake25cap2.json", threads,
                             "v11three");
        });
    }
    std::cout << "PREFLOP_BLUEPRINT_POLICY_VALUES_TESTS=PASS assertions=" << assertions << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "PREFLOP_BLUEPRINT_POLICY_VALUES_TESTS=FAIL " << error.what() << " after "
              << assertions << " assertions\n";
    return 1;
  }
}
