// Phase 3 (3-way step 2), V4 at the level of the preflop class cache
// (include/gtosd/preflop_blueprint/preflop_class_cache.hpp):
//  - census: terms only at street == Preflop nodes; on the step-2 3WAY50 trees
//    exactly 36 all-in terminals (9 with 3 active seats, 27 with 2) and 25
//    folds, 81 tensors; on the checkdown-compiled 3WAY50 the same plus the 15
//    checkdown leaves (13 + 38 showdowns, 115 tensors); every preflop terminal
//    has a term for every seat, every postflop node none, and every
//    non-terminal node right after a fold a term for the seat that folded;
//  - the tensors equal those of step 1 (three_way_terminals, folded cards
//    dead) byte for byte per preflop node id and seat on the checkdown-compiled
//    3WAY50, and the Deal payoffs equal step 1's fold and folder scales;
//  - compute_reach against an independent walk of the preflop tree; the values
//    of contract against step 1's contraction (contract_interleaved,
//    three_way_deal_values) times C(30,5)/C(34,5); the same bytes on 1 and 4
//    threads;
//  - the scale (V4, "0.512140"): for hero combos of several classes and sparse
//    class-constant reach of the other seats, the mean over every board where
//    the hero combo is live (C(34,5) = 278,256 boards) of the board-restricted
//    value (opponents live on the board, ranks from the rank table, payoffs per
//    winner set) equals the cache value, within 1e-12 * max(1, D3) * max(1,
//    |payoff|): a 3-active all-in, a 2-active all-in with each seat in turn as
//    the folder and every active seat as hero, a fold for the winner and the
//    folder, and a non-terminal node after a fold;
//  - errors: a heads-up game and an empty table are refused; the fingerprint
//    is stable and differs between rake fixtures.
// Needs the complete preflop_three_way_v1.bin, preflop_all_in_v1.bin and
// rank_table_v1.bin under --resources-dir; without them the test reports SKIP
// (exit 77).
#include "../benchmarks/checkdown_classes.hpp"

#include "gtosd/card_abstraction/all_in_table.hpp"
#include "gtosd/card_abstraction/deterministic_random.hpp"
#include "gtosd/card_abstraction/rank_table.hpp"
#include "gtosd/card_abstraction/showdown_counts.hpp"
#include "gtosd/card_abstraction/three_way_table.hpp"
#include "gtosd/preflop_blueprint/compiled_game.hpp"
#include "gtosd/preflop_blueprint/game_config.hpp"
#include "gtosd/preflop_blueprint/preflop_class_cache.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace ca = gtosd::card_abstraction;
namespace cc = gtosd::checkdown_classes;
namespace pb = gtosd::preflop_blueprint;
using Clock = std::chrono::steady_clock;
using Cache = pb::PreflopClassCache;

std::uint64_t assertions = 0U;
constexpr std::size_t classes = Cache::class_count;
constexpr double units_per_ante = static_cast<double>(gtosd::Money::units_per_ante);

void require(const bool condition, const std::string_view message) {
  ++assertions;
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

std::string read_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot open " + path.string());
  }
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

pb::CompiledGame compile(const std::string_view name, const bool checkdown) {
  const auto path =
      std::filesystem::path(GTOSD_SOURCE_DIR) / "benchmarks" / "monker" / std::string(name);
  const auto config = pb::parse_game_config_json(read_file(path));
  require(config.has_value(), std::string("configuration parses: ") + std::string(name));
  pb::CompileOptions options;
  options.checkdown_at_flop = checkdown;
  auto game = pb::CompiledGame::compile(config.value(), options);
  require(game.has_value(), std::string("tree compiles: ") + std::string(name));
  return std::move(game.value());
}

// Uniform in (0, 1].
double draw(ca::DeterministicRandom &random) {
  return static_cast<double>((random.next() >> 11U) + 1U) * 0x1.0p-53;
}

std::array<std::uint8_t, 2> other_seats(const std::uint8_t seat) {
  return {static_cast<std::uint8_t>(seat == 0U ? 1U : 0U),
          static_cast<std::uint8_t>(seat == 2U ? 1U : 2U)};
}

bool is_terminal(const pb::CompiledNode &node) {
  return node.kind == pb::NodeKind::TerminalFold || node.kind == pb::NodeKind::TerminalShowdown;
}

// A seat that just folded into a non-terminal preflop node.
bool folded_child(const pb::CompiledGame &game, const pb::CompiledNode &node, std::uint8_t &seat) {
  if (node.street != gtosd::Street::Preflop || is_terminal(node) || node.parent == pb::no_node) {
    return false;
  }
  seat = game.nodes()[node.parent].actor;
  return (node.active_mask & static_cast<std::uint8_t>(1U << seat)) == 0U;
}

// Census of the terms: which (node, seat) must have one.
void test_census(const pb::CompiledGame &game, const Cache &cache, const bool checkdown,
                 const std::string_view name) {
  const auto &counts = cache.counts();
  require(counts.all_in_terminals == 36U && counts.fold_terminals == 25U,
          std::string(name) + ": 36 preflop all-in terminals and 25 preflop folds");
  require(counts.checkdown_leaves == (checkdown ? 15U : 0U),
          std::string(name) + ": 15 checkdown leaves on the checkdown tree, none on step 2");
  require(counts.three_active_showdowns == (checkdown ? 13U : 9U) &&
              counts.two_active_showdowns == (checkdown ? 38U : 27U),
          std::string(name) + ": 3-active and 2-active showdowns (9 + 27; 13 + 38 with leaves)");
  require(counts.tensors == (checkdown ? 115U : 81U) && cache.tensors().size() == counts.tensors,
          std::string(name) + ": 3 x 3-active + 2 x 2-active tensors (81; 115 with leaves)");
  require(cache.tensor_bytes() == counts.tensors * 531'441ULL * 8ULL,
          std::string(name) + ": 4.25 MB per tensor");
  std::uint32_t folded = 0U;
  std::array<std::uint32_t, 3> by_seat{};
  for (const auto &node : game.nodes()) {
    std::uint8_t folder = 0U;
    const bool after_fold = folded_child(game, node, folder);
    folded += after_fold ? 1U : 0U;
    for (std::uint8_t seat = 0U; seat < 3U; ++seat) {
      const auto &term = cache.term(node.id, seat);
      const bool expected = node.street == gtosd::Street::Preflop &&
                            (is_terminal(node) || (after_fold && seat == folder));
      require((term.kind != pb::PreflopTermKind::None) == expected,
              std::string(name) + ": a term exactly at preflop terminals and after a fold");
      require((cache.values(node.id, seat) != nullptr) == expected,
              std::string(name) + ": a value slot exactly where a term is");
      require((cache.reach(node.id, seat) != nullptr) == (node.street == gtosd::Street::Preflop),
              std::string(name) + ": reach exactly at preflop nodes");
      by_seat[seat] += expected ? 1U : 0U;
      if (node.kind == pb::NodeKind::TerminalShowdown && node.street == gtosd::Street::Preflop) {
        const bool active = (node.active_mask & (1U << seat)) != 0U;
        require((term.kind == pb::PreflopTermKind::Tensor) == active,
                std::string(name) + ": a tensor exactly for the active seats of a showdown");
      }
    }
  }
  require(counts.folded_children == folded,
          std::string(name) + ": one term per non-terminal node after a fold");
  require(counts.terms_by_seat == by_seat, std::string(name) + ": terms per seat");
  std::cout << name << ": " << counts.all_in_terminals << " all-ins, " << counts.checkdown_leaves
            << " leaves, " << counts.fold_terminals << " folds, " << counts.folded_children
            << " nodes after a fold, " << counts.tensors << " tensors ("
            << static_cast<double>(cache.tensor_bytes()) / 1073741824.0 << " GiB), terms per seat "
            << by_seat[0] << "/" << by_seat[1] << "/" << by_seat[2] << ", fingerprint "
            << cache.fingerprint() << '\n';
}

// The tensors and Deal payoffs of step 1 per preflop terminal and seat.
void test_step_one_tensors(const pb::CompiledGame &game, const Cache &cache,
                           const cc::CheckdownTerminals &step_one, const std::string_view name) {
  std::size_t compared = 0U;
  for (const auto &node : game.nodes()) {
    if (!is_terminal(node)) {
      continue;
    }
    for (std::uint8_t seat = 0U; seat < 3U; ++seat) {
      const auto &ours = cache.term(node.id, seat);
      const auto &theirs = step_one.terms[node.id][seat];
      if (theirs.tensor == 0U) {
        require(ours.kind == pb::PreflopTermKind::Deal && ours.payoff == theirs.scale,
                std::string(name) + ": a step-1 deal term is a Deal term with the same payoff");
        continue;
      }
      require(ours.kind == pb::PreflopTermKind::Tensor,
              std::string(name) + ": a step-1 tensor is a Tensor term");
      const auto &mine = cache.tensors()[ours.tensor];
      const auto &reference = step_one.tensors[theirs.tensor];
      require(mine.size() == reference.size() &&
                  std::memcmp(mine.data(), reference.data(), mine.size() * sizeof(double)) == 0,
              std::string(name) + ": tensor bytes equal step 1's at node " +
                  std::to_string(node.id) + " seat " + std::to_string(seat));
      ++compared;
    }
  }
  require(compared == cache.tensors().size(), std::string(name) + ": every tensor compared");
  std::cout << name << ": " << compared << " tensors byte-identical to three_way_terminals\n";
}

// A random dense preflop policy (rows of the 81 classes per preflop decision,
// some actions exactly zero) and its offsets.
struct DensePolicy {
  std::vector<double> table;
  std::vector<std::uint64_t> offsets;
};

DensePolicy random_policy(const pb::CompiledGame &game, ca::DeterministicRandom &random) {
  DensePolicy policy;
  policy.offsets.assign(game.nodes().size(), 0U);
  for (const auto &node : game.nodes()) {
    if (node.kind != pb::NodeKind::Decision || node.street != gtosd::Street::Preflop) {
      continue;
    }
    policy.offsets[node.id] = policy.table.size();
    for (std::size_t hand_class = 0; hand_class < classes; ++hand_class) {
      std::vector<double> row(node.action_count, 0.0);
      double total = 0.0;
      for (auto &value : row) {
        value = draw(random) < 0.2 ? 0.0 : draw(random);
        total += value;
      }
      if (total == 0.0) {
        row[0] = 1.0;
        total = 1.0;
      }
      for (const auto value : row) {
        policy.table.push_back(value / total);
      }
    }
  }
  return policy;
}

// Reach of every seat per class at every preflop node, by recursion.
void walk_reach(const pb::CompiledGame &game, const DensePolicy &policy, const std::uint32_t id,
                const cc::Reach &reach, std::vector<cc::Reach> &output) {
  output[id] = reach;
  const auto &node = game.nodes()[id];
  if (node.kind != pb::NodeKind::Decision || node.street != gtosd::Street::Preflop) {
    return;
  }
  const auto edges = game.edges_of(id);
  const double *rows = policy.table.data() + policy.offsets[id];
  for (std::size_t edge = 0; edge < edges.size(); ++edge) {
    auto next = reach;
    for (std::size_t hand_class = 0; hand_class < classes; ++hand_class) {
      next[node.actor][hand_class] *= rows[hand_class * edges.size() + edge];
    }
    walk_reach(game, policy, edges[edge].child, next, output);
  }
}

bool close(const double value, const double reference, const double tolerance) {
  return std::abs(value - reference) <= tolerance * std::max(1.0, std::abs(reference));
}

// compute_reach against the walk; contract against step 1's contraction; 1
// and 4 threads byte-identical.
void test_values(const pb::CompiledGame &game, Cache &cache,
                 const cc::CheckdownTerminals *step_one, const std::uint64_t seed,
                 const std::string_view name) {
  ca::DeterministicRandom random(seed);
  const auto policy = random_policy(game, random);
  const auto reach_started = Clock::now();
  cache.compute_reach(policy.table, policy.offsets);
  const double reach_seconds = std::chrono::duration<double>(Clock::now() - reach_started).count();
  std::vector<cc::Reach> walked(game.nodes().size());
  walk_reach(game, policy, game.root(), cc::Reach(3U, std::vector<double>(classes, 1.0)), walked);
  for (const auto &node : game.nodes()) {
    if (node.street != gtosd::Street::Preflop) {
      continue;
    }
    for (std::uint8_t seat = 0U; seat < 3U; ++seat) {
      require(std::memcmp(cache.reach(node.id, seat), walked[node.id][seat].data(),
                          classes * sizeof(double)) == 0,
              std::string(name) + ": compute_reach equals the walk of the preflop tree");
    }
  }
  double contract_seconds = 0.0;
  std::size_t checked = 0U;
  for (std::uint8_t hero = 0U; hero < 3U; ++hero) {
    const auto started = Clock::now();
    cache.contract(hero, 1U);
    contract_seconds += std::chrono::duration<double>(Clock::now() - started).count();
    std::vector<std::vector<double>> single;
    for (const auto &node : game.nodes()) {
      const double *values = cache.values(node.id, hero);
      if (values == nullptr) {
        continue;
      }
      single.emplace_back(values, values + classes);
      const auto &term = cache.term(node.id, hero);
      std::vector<double> reference;
      if (term.kind == pb::PreflopTermKind::Tensor) {
        reference = cc::contract_interleaved(cache.tensors()[term.tensor], Cache::board_scale,
                                             cc::others_outer(walked[node.id], hero));
        require(std::memcmp(values, reference.data(), classes * sizeof(double)) == 0,
                std::string(name) + ": a tensor value equals step 1's contraction x scale");
      } else {
        reference = cc::three_way_deal_values(walked[node.id], hero);
        std::vector<double> mine(classes);
        Cache::class_deal_values(walked[node.id][other_seats(hero)[0]].data(),
                                 walked[node.id][other_seats(hero)[1]].data(), mine.data());
        require(std::memcmp(mine.data(), reference.data(), classes * sizeof(double)) == 0,
                std::string(name) + ": class D3 equals three_way_deal_values");
        for (std::size_t hand_class = 0; hand_class < classes; ++hand_class) {
          require(close(values[hand_class],
                        reference[hand_class] * term.payoff * Cache::board_scale, 1e-14),
                  std::string(name) + ": a Deal value is payoff x scale x D3");
        }
        if (step_one != nullptr && is_terminal(node)) {
          require(step_one->terms[node.id][hero].tensor == 0U &&
                      step_one->terms[node.id][hero].scale == term.payoff,
                  std::string(name) + ": the Deal payoff is step 1's scale");
        }
      }
      for (std::size_t hand_class = 0; hand_class < classes; ++hand_class) {
        require(std::isfinite(values[hand_class]), std::string(name) + ": finite values");
      }
      ++checked;
    }
    cache.contract(hero, 4U);
    std::size_t index = 0U;
    for (const auto &node : game.nodes()) {
      const double *values = cache.values(node.id, hero);
      if (values == nullptr) {
        continue;
      }
      require(std::memcmp(values, single[index].data(), classes * sizeof(double)) == 0,
              std::string(name) + ": 1 and 4 threads give the same bytes");
      ++index;
    }
  }
  std::cout << name << ": " << checked << " seat values checked; compute_reach "
            << reach_seconds * 1e3 << " ms, contract " << contract_seconds * 1e3 / 3.0
            << " ms per hero (1 thread)\n";
}

// Sum with compensation (Neumaier), so the brute force rounds far below the
// tolerance.
struct Accumulator {
  double sum{0.0};
  double compensation{0.0};
  void add(const double value) {
    const double total = sum + value;
    compensation += std::abs(sum) >= std::abs(value) ? (sum - total) + value
                                                      : (value - total) + sum;
    sum = total;
  }
  [[nodiscard]] double value() const { return sum + compensation; }
};

struct ScaleCase {
  std::string label;
  std::uint32_t node{0U};
  std::uint8_t hero{0U};
  std::uint16_t combo{0U};
};

// Payoff of `seat` when it no longer plays below `node` (constant: the cache
// checked it), read at the first terminal of the subtree.
double folded_payoff(const pb::CompiledGame &game, const std::uint32_t id,
                     const std::uint8_t seat) {
  for (auto node = id; node < game.nodes()[id].subtree_end; ++node) {
    const auto &terminal = game.nodes()[node];
    if (terminal.kind == pb::NodeKind::TerminalFold) {
      return static_cast<double>(game.fold_payoffs(node)[seat]) / units_per_ante;
    }
    if (terminal.kind == pb::NodeKind::TerminalShowdown) {
      return static_cast<double>(game.showdown_payoffs(node, terminal.active_mask)[seat]) /
             units_per_ante;
    }
  }
  throw std::runtime_error("no terminal below a node");
}

// Mean over the 278,256 boards where the hero combo is live of the
// board-restricted value; also the mean deal mass (D3 on the board).
void board_mean(const pb::CompiledGame &game, const ca::RankTable &ranks, const ScaleCase &item,
                const std::vector<double> &lower_reach, const std::vector<double> &higher_reach,
                double &value_mean, double &deal_mean, double &payoff_scale) {
  const auto &combos = ca::combo_table();
  const auto &node = game.nodes()[item.node];
  const auto hero_bit = static_cast<std::uint8_t>(1U << item.hero);
  const auto seats = other_seats(item.hero);
  const auto hero_mask = combos.masks[item.combo];
  enum class Kind { Constant, ThreeActive, TwoActive } kind = Kind::Constant;
  double constant = 0.0;
  std::uint8_t opponent = 0U;
  if (node.kind == pb::NodeKind::TerminalShowdown && (node.active_mask & hero_bit) != 0U) {
    kind = node.active_mask == 7U ? Kind::ThreeActive : Kind::TwoActive;
    opponent = static_cast<std::uint8_t>(std::countr_zero(
        static_cast<unsigned>(node.active_mask & static_cast<std::uint8_t>(~hero_bit))));
  } else if (node.kind == pb::NodeKind::TerminalFold) {
    constant = static_cast<double>(game.fold_payoffs(item.node)[item.hero]) / units_per_ante;
  } else {
    constant = folded_payoff(game, item.node, item.hero);
  }
  payoff_scale = std::max(1.0, std::abs(constant));
  if (kind != Kind::Constant) {
    for (std::uint8_t winners = 1U; winners < 8U; ++winners) {
      if ((winners & static_cast<std::uint8_t>(~node.active_mask)) == 0U) {
        payoff_scale = std::max(payoff_scale,
                                std::abs(static_cast<double>(
                                    game.showdown_payoffs(item.node, winners)[item.hero])) /
                                    units_per_ante);
      }
    }
  }
  // Candidate combos of the other seats: positive class reach, disjoint from
  // the hero.
  std::array<std::vector<std::uint16_t>, 2> candidates;
  std::array<const std::vector<double> *, 2> reach{&lower_reach, &higher_reach};
  for (std::size_t side = 0; side < 2U; ++side) {
    for (std::uint16_t combo = 0U; combo < ca::combo_count; ++combo) {
      if ((*reach[side])[combos.hand_class[combo]] > 0.0 && (combos.masks[combo] & hero_mask) == 0U) {
        candidates[side].push_back(combo);
      }
    }
  }
  std::vector<std::uint8_t> rest;
  for (std::uint8_t card = 0U; card < ca::deck_cards; ++card) {
    if (((hero_mask >> card) & 1U) == 0U) {
      rest.push_back(card);
    }
  }
  require(rest.size() == 34U, "34 cards around the hero combo");
  Accumulator value_sum;
  Accumulator deal_sum;
  std::uint64_t boards = 0U;
  std::array<std::uint8_t, 5> board{};
  std::array<std::vector<std::uint16_t>, 2> live;
  std::array<std::vector<std::uint16_t>, 2> live_rank;
  for (std::size_t a = 0; a < rest.size(); ++a) {
    for (std::size_t b = a + 1U; b < rest.size(); ++b) {
      for (std::size_t c = b + 1U; c < rest.size(); ++c) {
        for (std::size_t d = c + 1U; d < rest.size(); ++d) {
          for (std::size_t e = d + 1U; e < rest.size(); ++e) {
            board = {rest[a], rest[b], rest[c], rest[d], rest[e]};
            std::uint64_t board_mask = 0U;
            for (const auto card : board) {
              board_mask |= std::uint64_t{1} << card;
            }
            ++boards;
            const auto hero_rank =
                kind == Kind::Constant ? std::uint16_t{0} : ranks.rank_of(combos.cards[item.combo], board);
            for (std::size_t side = 0; side < 2U; ++side) {
              live[side].clear();
              live_rank[side].clear();
              for (const auto combo : candidates[side]) {
                if ((combos.masks[combo] & board_mask) != 0U) {
                  continue;
                }
                live[side].push_back(combo);
                live_rank[side].push_back(kind == Kind::Constant
                                              ? std::uint16_t{0}
                                              : ranks.rank_of(combos.cards[combo], board));
              }
            }
            double board_value = 0.0;
            double board_deals = 0.0;
            for (std::size_t i = 0; i < live[0].size(); ++i) {
              const auto first = live[0][i];
              const double r1 = lower_reach[combos.hand_class[first]];
              for (std::size_t j = 0; j < live[1].size(); ++j) {
                const auto second = live[1][j];
                if ((combos.masks[first] & combos.masks[second]) != 0U) {
                  continue;
                }
                const double weight = r1 * higher_reach[combos.hand_class[second]];
                board_deals += weight;
                double payoff = constant;
                if (kind == Kind::ThreeActive) {
                  const auto best = std::max({hero_rank, live_rank[0][i], live_rank[1][j]});
                  unsigned mask = 0U;
                  mask |= hero_rank == best ? (1U << item.hero) : 0U;
                  mask |= live_rank[0][i] == best ? (1U << seats[0]) : 0U;
                  mask |= live_rank[1][j] == best ? (1U << seats[1]) : 0U;
                  const auto winners = static_cast<std::uint8_t>(mask);
                  payoff = static_cast<double>(game.showdown_payoffs(item.node, winners)[item.hero]) /
                           units_per_ante;
                } else if (kind == Kind::TwoActive) {
                  // The opponent's hand is that of its own side; the folder's
                  // hand only blocks cards (dead).
                  const auto opponent_rank = opponent == seats[0] ? live_rank[0][i] : live_rank[1][j];
                  const auto opponent_bit = static_cast<std::uint8_t>(1U << opponent);
                  const std::uint8_t winners =
                      hero_rank > opponent_rank   ? hero_bit
                      : hero_rank == opponent_rank ? static_cast<std::uint8_t>(hero_bit | opponent_bit)
                                                   : opponent_bit;
                  payoff = static_cast<double>(game.showdown_payoffs(item.node, winners)[item.hero]) /
                           units_per_ante;
                }
                board_value += weight * payoff;
              }
            }
            value_sum.add(board_value);
            deal_sum.add(board_deals);
          }
        }
      }
    }
  }
  require(boards == 278'256U, "C(34,5) boards around the hero combo");
  value_mean = value_sum.value() / static_cast<double>(boards);
  deal_mean = deal_sum.value() / static_cast<double>(boards);
}

void test_scale(const pb::CompiledGame &game, const Cache &cache, const ca::RankTable &ranks) {
  const auto &nodes = game.nodes();
  const auto &combos = ca::combo_table();
  std::vector<ScaleCase> cases;
  ca::DeterministicRandom random(0x5343414c45ULL);
  // A random combo of a random class (not always the representative).
  const auto pick_combo = [&]() {
    return static_cast<std::uint16_t>(random.next() % ca::combo_count);
  };
  bool three = false;
  std::array<bool, 3> two_by_folder{};
  bool fold_winner = false;
  bool fold_folder = false;
  bool after_fold = false;
  for (const auto &node : nodes) {
    if (node.street != gtosd::Street::Preflop) {
      continue;
    }
    if (node.kind == pb::NodeKind::TerminalShowdown && node.active_mask == 7U && !three) {
      three = true;
      for (std::uint8_t hero = 0U; hero < 3U; ++hero) {
        cases.push_back({"3-active all-in, hero " + std::to_string(hero), node.id, hero, pick_combo()});
      }
    } else if (node.kind == pb::NodeKind::TerminalShowdown && node.active_mask != 7U) {
      const auto folder = static_cast<std::uint8_t>(
          std::countr_zero(static_cast<unsigned>(~node.active_mask & 7U)));
      if (two_by_folder[folder]) {
        continue;
      }
      two_by_folder[folder] = true;
      for (std::uint8_t hero = 0U; hero < 3U; ++hero) {
        cases.push_back({"2-active all-in, folder " + std::to_string(folder) + ", hero " +
                             std::to_string(hero),
                         node.id, hero, pick_combo()});
      }
    } else if (node.kind == pb::NodeKind::TerminalFold && (!fold_winner || !fold_folder)) {
      const auto winner =
          static_cast<std::uint8_t>(std::countr_zero(static_cast<unsigned>(node.active_mask)));
      const auto folder = nodes[node.parent].actor;
      if (!fold_winner) {
        fold_winner = true;
        cases.push_back({"fold, hero the winner", node.id, winner, pick_combo()});
      }
      if (!fold_folder && folder != winner) {
        fold_folder = true;
        cases.push_back({"fold, hero the folder", node.id, folder, pick_combo()});
      }
    } else if (!after_fold && !is_terminal(node) && node.parent != pb::no_node) {
      const auto folder = nodes[node.parent].actor;
      if ((node.active_mask & (1U << folder)) == 0U) {
        after_fold = true;
        cases.push_back({"node after a fold, hero the folder", node.id, folder, pick_combo()});
      }
    }
  }
  require(three && two_by_folder[0] && two_by_folder[1] && two_by_folder[2] && fold_winner &&
              fold_folder && after_fold,
          "every kind of preflop term has a scale case");
  std::vector<double> outer(classes * classes);
  double largest = 0.0;
  for (const auto &item : cases) {
    // Sparse class reach: 3 random classes per other seat, the rest zero.
    std::vector<double> lower(classes, 0.0);
    std::vector<double> higher(classes, 0.0);
    for (int index = 0; index < 3; ++index) {
      lower[random.next() % classes] = draw(random);
      higher[random.next() % classes] = draw(random);
    }
    const auto started = Clock::now();
    double value_mean = 0.0;
    double deal_mean = 0.0;
    double payoff_scale = 1.0;
    board_mean(game, ranks, item, lower, higher, value_mean, deal_mean, payoff_scale);
    const auto hand_class = combos.hand_class[item.combo];
    const auto &term = cache.term(item.node, item.hero);
    std::vector<double> values(classes);
    if (term.kind == pb::PreflopTermKind::Tensor) {
      Cache::contract_tensor(cache.tensors()[term.tensor], Cache::board_scale, lower.data(),
                             higher.data(), outer.data(), values.data());
    } else {
      require(term.kind == pb::PreflopTermKind::Deal, item.label + ": a term exists");
      Cache::class_deal_values(lower.data(), higher.data(), values.data());
      for (auto &value : values) {
        value *= term.payoff * Cache::board_scale;
      }
    }
    std::vector<double> deals(classes);
    Cache::class_deal_values(lower.data(), higher.data(), deals.data());
    const double scaled_deals = deals[hand_class] * Cache::board_scale;
    const double tolerance = 1e-12 * std::max(1.0, deal_mean) * payoff_scale;
    const double difference = std::abs(values[hand_class] - value_mean);
    largest = std::max(largest, difference / (std::max(1.0, deal_mean) * payoff_scale));
    std::cout << "  scale " << item.label << " (node " << item.node << ", combo " << item.combo
              << "): cache " << values[hand_class] << ", board mean " << value_mean << ", D3 "
              << scaled_deals << " vs " << deal_mean << ", "
              << std::chrono::duration<double>(Clock::now() - started).count() << " s\n";
    require(std::abs(scaled_deals - deal_mean) <= 1e-12 * std::max(1.0, deal_mean),
            item.label + ": the class D3 x C(30,5)/C(34,5) is the board mean of the deal mass");
    require(difference <= tolerance,
            item.label + ": the cache value is the board mean of the board-restricted value");
  }
  std::cout << "scale: " << cases.size() << " cases, largest difference / (max(1, D3) x payoff) "
            << largest << '\n';
}

void test_errors(const ca::ThreeWayTable &table) {
  const auto heads_up = compile("HU50_step2_donk.json", false);
  const auto refused = Cache::build(heads_up, table);
  require(!refused && refused.error() == pb::PreflopClassCacheError::NotThreePlayers,
          "a heads-up game is refused");
  const auto game = compile("3WAY50_donk.json", false);
  const ca::ThreeWayTable empty{};
  const auto incomplete = Cache::build(game, empty);
  require(!incomplete && incomplete.error() == pb::PreflopClassCacheError::IncompleteTable,
          "an empty table is refused");
  require(std::string(pb::preflop_class_cache_error_name(pb::PreflopClassCacheError::PayoffMismatch)) ==
              "payoff_mismatch",
          "error names");
}

} // namespace

int main(const int argc, char **argv) {
  try {
    const auto started = Clock::now();
    std::filesystem::path resources_dir;
    for (int index = 1; index + 1 < argc; index += 2) {
      const std::string_view name = argv[index];
      if (name == "--resources-dir") {
        resources_dir = argv[index + 1];
      } else {
        throw std::runtime_error("unknown argument " + std::string{name});
      }
    }
    auto three_way =
        ca::ThreeWayTable::load(resources_dir / std::string(ca::three_way_table_file_name));
    auto all_in = ca::AllInTable::load(resources_dir / "preflop_all_in_v1.bin");
    auto ranks = ca::RankTable::load(resources_dir / "rank_table_v1.bin");
    if (!three_way || !all_in || !ranks) {
      std::cout << "PREFLOP_BLUEPRINT_CLASS_CACHE_TESTS=SKIP resources missing under "
                << resources_dir.string() << '\n';
      return 77;
    }
    const auto &table = three_way.value();
    test_errors(table);

    std::string first_fingerprint;
    for (const auto *name : {"3WAY50_donk_rake25cap2.json", "3WAY50_donk_rake.json",
                             "3WAY50_donk.json", "3WAY50_donk_rake5cap075.json"}) {
      {
        // Step 2: the census, the values and (first fixture) the scale.
        const auto game = compile(name, false);
        const auto build_started = Clock::now();
        auto built = Cache::build(game, table);
        require(built.has_value(), std::string(name) + ": the cache builds");
        auto &cache = built.value();
        std::cout << name << " (step 2): built in "
                  << std::chrono::duration<double>(Clock::now() - build_started).count() << " s, "
                  << static_cast<double>(cache.memory_bytes()) / 1048576.0 << " MiB\n";
        test_census(game, cache, false, std::string(name) + " step 2");
        {
          const auto again = Cache::build(game, table);
          require(again.has_value() && again.value().fingerprint() == cache.fingerprint(),
                  std::string(name) + ": the fingerprint is stable");
        }
        if (first_fingerprint.empty()) {
          first_fingerprint = cache.fingerprint();
        } else {
          require(cache.fingerprint() != first_fingerprint,
                  std::string(name) + ": another game, another fingerprint");
        }
        test_values(game, cache, nullptr, 0x5354455032ULL, std::string(name) + " step 2");
        if (std::string_view(name) == "3WAY50_donk_rake25cap2.json") {
          test_scale(game, cache, ranks.value());
        }
      }
      // Checkdown-compiled: step 1's tensors byte for byte.
      const auto checkdown = compile(name, true);
      auto checkdown_built = Cache::build(checkdown, table);
      require(checkdown_built.has_value(), std::string(name) + ": the checkdown cache builds");
      test_census(checkdown, checkdown_built.value(), true, std::string(name) + " checkdown");
      const auto step_one =
          cc::three_way_terminals(checkdown, table, all_in.value(), ca::FoldedCards::Dead);
      test_step_one_tensors(checkdown, checkdown_built.value(), step_one,
                            std::string(name) + " checkdown");
      test_values(checkdown, checkdown_built.value(), &step_one, 0x434b444eULL,
                  std::string(name) + " checkdown");
    }
    std::cout << "PREFLOP_BLUEPRINT_CLASS_CACHE_TESTS=PASS assertions=" << assertions
              << " seconds=" << std::chrono::duration<double>(Clock::now() - started).count()
              << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "PREFLOP_BLUEPRINT_CLASS_CACHE_TESTS=FAIL " << error.what() << '\n';
    return 1;
  }
}
