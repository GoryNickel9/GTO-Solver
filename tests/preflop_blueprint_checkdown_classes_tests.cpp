// Class-level checkdown solver (benchmarks/checkdown_classes.hpp), phase 2b
// part 1: the aggregation of preflop_all_in_v1.bin to 81 x 81 classes (pair
// counts, runout totals, the swap identities, the same row for every one of
// the 630 combos), the terminal tensors of HU50 with rake against the
// combo-level payoff matrices of preflop_blueprint_checkdown.cpp on random
// class-constant reach for both seats, the 3-seat layout of the reach product
// and of the contraction (the transpose trap of the 2-active tensors), the
// N-seat rake check and terminal census of the 3WAY50 checkdown tree, and a
// short solve: best response at least the average, the EV sum minus the
// expected rake by reach (zero without rake).
//
// Phase 2b part 2 (3 seats): the 3-way terminal values against a combo-level
// brute force on a small random reach (a 3-active showdown and a fold for
// every seat, a 2-active showdown with each of the three seats as the folder,
// folded cards dead and ignored), the inclusion-exclusion deal values against
// the dense contraction of N, the rake identity of every terminal (the seats'
// reach-weighted values sum to minus its rake times its deals), and short
// solves with and without rake: the same values on 1 and 2 threads, best
// response at least the average, local gains non-negative, a locked row kept;
// with rake, a short solve's charts locked back on every node of a fresh
// solver through the 3-seat chart lock (every row classified, every seat's
// rows on their own node and class, kept through iterations, the source's EVs
// and gains reproduced), written under --scratch-dir.
// The complete three-player table is loaded from the resources; without it a
// subset is built for the brute-force hero classes and the checks that need
// every class are skipped.
#include "../benchmarks/checkdown_classes.hpp"
#include "../benchmarks/monker_chart_lock.hpp"

#include "gtosd/card_abstraction/all_in_table.hpp"
#include "gtosd/card_abstraction/deterministic_random.hpp"
#include "gtosd/card_abstraction/rank_table.hpp"
#include "gtosd/card_abstraction/showdown_counts.hpp"
#include "gtosd/card_abstraction/three_way_table.hpp"
#include "gtosd/preflop_blueprint/compiled_game.hpp"
#include "gtosd/preflop_blueprint/game_config.hpp"
#include "gtosd/preflop_blueprint/game_model.hpp"

#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace ca = gtosd::card_abstraction;
namespace cc = gtosd::checkdown_classes;
namespace mc = gtosd::monker_charts;
namespace pb = gtosd::preflop_blueprint;
using Clock = std::chrono::steady_clock;

std::uint64_t assertions = 0U;
constexpr std::size_t classes = cc::class_count;
constexpr std::size_t hands = ca::combo_count;

void require(const bool condition, const std::string_view message) {
  ++assertions;
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

bool close(const double left, const double right, const double tolerance) {
  return std::abs(left - right) <= tolerance * std::max(1.0, std::abs(right));
}

std::string read_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot open " + path.string());
  }
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

pb::CompiledGame compile_checkdown(const std::string_view name) {
  const auto path =
      std::filesystem::path(GTOSD_SOURCE_DIR) / "benchmarks" / "monker" / std::string(name);
  const auto config = pb::parse_game_config_json(read_file(path));
  require(config.has_value(), std::string("configuration parses: ") + std::string(name));
  pb::CompileOptions options;
  options.checkdown_at_flop = true;
  auto game = pb::CompiledGame::compile(config.value(), options);
  require(game.has_value(), std::string("checkdown tree compiles: ") + std::string(name));
  return std::move(game.value());
}

// Uniform in (0, 1].
double draw(ca::DeterministicRandom &random) {
  return static_cast<double>((random.next() >> 11U) + 1U) * 0x1.0p-53;
}

cc::Reach random_reach(ca::DeterministicRandom &random, const std::size_t seats) {
  cc::Reach reach(seats, std::vector<double>(classes, 0.0));
  for (auto &seat : reach) {
    for (auto &value : seat) {
      value = draw(random);
    }
  }
  return reach;
}

// The aggregation: row H is the row of the representative; every combo of H
// has the same row; pair counts and runout totals; the swap identities
// |H| W[H][A] = |A| L[A][H] and |H| T[H][A] = |A| T[A][H].
void test_aggregation(const ca::AllInTable &table) {
  const auto rows = cc::heads_up_class_outcomes(table);
  const auto masses = cc::class_masses();
  const auto &combos = ca::combo_table();
  require(rows.size() == classes, "81 aggregated rows");
  double mass_total = 0.0;
  for (const auto mass : masses) {
    mass_total += mass;
  }
  require(mass_total == 630.0, "the class masses sum to 630 combos");
  for (std::uint16_t combo = 0U; combo < hands; ++combo) {
    require(cc::heads_up_row(table, combo) == rows[combos.hand_class[combo]],
            "every combo of a class has the row of its representative");
  }
  for (std::size_t hero = 0; hero < classes; ++hero) {
    const auto representative =
        ca::ThreeWayTable::representative_of(static_cast<std::uint8_t>(hero));
    require(combos.hand_class[representative] == hero, "the representative is in its class");
    std::uint32_t opponents = 0U;
    for (std::size_t opponent = 0; opponent < classes; ++opponent) {
      const auto &entry = rows[hero][opponent];
      std::uint32_t disjoint = 0U;
      for (std::uint16_t combo = 0U; combo < hands; ++combo) {
        disjoint +=
            static_cast<std::uint32_t>(combos.hand_class[combo] == opponent &&
                                       (combos.masks[combo] & combos.masks[representative]) == 0U);
      }
      require(entry.opponents == disjoint, "opponent count by brute force");
      require(entry.wins + entry.ties + entry.losses ==
                  std::uint64_t{entry.opponents} * ca::all_in_runout_count,
              "wins, ties and losses cover every runout of every disjoint opponent");
      const auto &mirror = rows[opponent][hero];
      const auto hero_mass = static_cast<std::uint64_t>(masses[hero]);
      const auto opponent_mass = static_cast<std::uint64_t>(masses[opponent]);
      require(hero_mass * entry.opponents == opponent_mass * mirror.opponents,
              "|H| D[H][A] = |A| D[A][H]");
      require(hero_mass * entry.wins == opponent_mass * mirror.losses, "|H| W[H][A] = |A| L[A][H]");
      require(hero_mass * entry.ties == opponent_mass * mirror.ties, "|H| T[H][A] = |A| T[A][H]");
      opponents += entry.opponents;
    }
    require(opponents == 561U, "561 opponent combos disjoint from every hero combo");
  }
  std::cout << "aggregation: 81 x 81 rows equal for all 630 combos, identities hold\n";
}

// Terminal tensors of HU50 with rake against the combo-level values: the
// payoff matrix of preflop_blueprint_checkdown.cpp (showdown) or the payoff
// times the disjoint opponents' reach (fold), for every combo, with the
// opponent's reach constant over each class.
void test_heads_up_terminals(const ca::AllInTable &table) {
  const auto game = compile_checkdown("HU50_rake.json");
  require(game.config().rake.enabled, "HU50_rake has rake");
  cc::check_terminal_rake(game);
  const auto terminals = cc::heads_up_terminals(game, table);
  require(terminals.seats == 2U && terminals.deals == cc::heads_up_deals,
          "two seats and 630 x 561 deals");
  std::uint64_t folds = 0U;
  std::uint64_t showdowns = 0U;
  for (const auto &node : game.nodes()) {
    folds += node.kind == pb::NodeKind::TerminalFold ? 1U : 0U;
    showdowns += node.kind == pb::NodeKind::TerminalShowdown ? 1U : 0U;
  }
  require(terminals.fold_terminals == folds && terminals.showdown_terminals == showdowns &&
              terminals.tensors.size() == 1U + 2U * showdowns,
          "one count tensor and one tensor per seat and showdown");
  const auto &combos = ca::combo_table();
  ca::DeterministicRandom random(20260930U);
  double largest = 0.0;
  std::uint64_t compared = 0U;
  for (int trial = 0; trial < 3; ++trial) {
    const auto reach = random_reach(random, 2U);
    for (const auto &node : game.nodes()) {
      if (node.kind != pb::NodeKind::TerminalFold && node.kind != pb::NodeKind::TerminalShowdown) {
        continue;
      }
      for (std::uint8_t seat = 0U; seat < 2U; ++seat) {
        const auto other = static_cast<std::uint8_t>(1U - seat);
        const auto &term = terminals.terms[node.id][seat];
        const auto values =
            cc::contract(terminals.tensors[term.tensor], term.scale, cc::others_outer(reach, seat));
        double win = 0.0;
        double tie = 0.0;
        double lose = 0.0;
        const bool showdown = node.kind == pb::NodeKind::TerminalShowdown;
        if (showdown) {
          const auto seat_bit = static_cast<std::uint8_t>(1U << seat);
          win = static_cast<double>(game.showdown_payoffs(node.id, seat_bit)[seat]) /
                cc::units_per_ante;
          tie = static_cast<double>(game.showdown_payoffs(node.id, node.active_mask)[seat]) /
                cc::units_per_ante;
          lose = static_cast<double>(game.showdown_payoffs(
                     node.id, static_cast<std::uint8_t>(node.active_mask & ~seat_bit))[seat]) /
                 cc::units_per_ante;
        }
        const double fold =
            showdown ? 0.0
                     : static_cast<double>(game.fold_payoffs(node.id)[seat]) / cc::units_per_ante;
        for (std::uint16_t hero = 0U; hero < hands; ++hero) {
          double expected = 0.0;
          for (std::uint16_t opponent = 0U; opponent < hands; ++opponent) {
            if ((combos.masks[hero] & combos.masks[opponent]) != 0U) {
              continue;
            }
            const double weight = reach[other][combos.hand_class[opponent]];
            if (!showdown) {
              expected += fold * weight;
              continue;
            }
            const auto outcome = table.outcome(hero, opponent);
            expected +=
                weight *
                (static_cast<double>(outcome.wins) * win + static_cast<double>(outcome.ties) * tie +
                 static_cast<double>(outcome.losses) * lose) /
                static_cast<double>(outcome.total());
          }
          const double actual = values[combos.hand_class[hero]];
          largest = std::max(largest, std::abs(actual - expected));
          require(close(actual, expected, 1e-12), "class terminal value equals the combo value");
          ++compared;
        }
      }
    }
  }
  std::cout << "heads-up terminals: " << folds << " folds, " << showdowns << " showdowns, "
            << compared << " combo values, largest difference " << largest << " antes\n";
}

// Three seats: the reach product puts the lower other seat major, and the
// contraction reads tensor[H][A][B] with A the lower and B the higher other
// seat, for every hero seat (a 2-active tensor built as (H, opponent,
// folded) must be transposed when the folder is the lower seat).
void test_three_seat_layout() {
  ca::DeterministicRandom random(31U);
  const auto reach = random_reach(random, 3U);
  std::vector<double> tensor(classes * classes * classes, 0.0);
  for (auto &value : tensor) {
    value = draw(random) - 0.5;
  }
  const double scale = -1.75;
  for (std::uint8_t hero = 0U; hero < 3U; ++hero) {
    const auto first = static_cast<std::size_t>(hero == 0U ? 1U : 0U);
    const auto second = static_cast<std::size_t>(hero == 2U ? 1U : 2U);
    const auto outer = cc::others_outer(reach, hero);
    require(outer.size() == classes * classes, "6,561 entries in the 3-seat reach product");
    for (std::size_t a = 0; a < classes; ++a) {
      for (std::size_t b = 0; b < classes; ++b) {
        require(outer[a * classes + b] == reach[first][a] * reach[second][b],
                "the lower other seat is major in the reach product");
      }
    }
    const auto values = cc::contract(tensor, scale, outer);
    for (std::size_t h = 0; h < classes; ++h) {
      double expected = 0.0;
      for (std::size_t a = 0; a < classes; ++a) {
        for (std::size_t b = 0; b < classes; ++b) {
          expected += tensor[(h * classes + a) * classes + b] * reach[first][a] * reach[second][b];
        }
      }
      require(close(values[h], scale * expected, 1e-12), "3-seat contraction by brute force");
    }
  }
  std::cout << "3-seat layout: reach product and contraction checked for every hero seat\n";
}

// The N-seat rake check on the 3-way checkdown tree with rake, and its
// terminals by active players (the 2-active showdowns follow a fold).
void test_three_way_tree() {
  const auto game = compile_checkdown("3WAY50_donk_rake.json");
  require(game.config().player_count == 3U && game.postflop_entries().empty(),
          "3WAY50_donk_rake compiles to a 3-seat checkdown tree");
  cc::check_terminal_rake(game);
  std::uint64_t folds = 0U;
  std::array<std::uint64_t, 4> showdowns{};
  for (const auto &node : game.nodes()) {
    if (node.kind == pb::NodeKind::TerminalFold) {
      ++folds;
    } else if (node.kind == pb::NodeKind::TerminalShowdown) {
      require(node.remaining_board_cards == 5U, "every 3-way checkdown showdown deals five cards");
      ++showdowns[static_cast<std::size_t>(std::popcount(node.active_mask))];
    }
  }
  require(folds == 25U && showdowns[3] == 13U && showdowns[2] == 38U,
          "3WAY50 checkdown: 25 folds, 13 three-active and 38 two-active showdowns");
  std::cout << "3-way tree: " << folds << " folds, " << showdowns[3] << " three-active and "
            << showdowns[2] << " two-active showdowns, rake rows checked\n";
}

// A short solve with and without rake.
void test_solver(const ca::AllInTable &table) {
  for (const std::string_view name : {"HU50.json", "HU50_rake.json"}) {
    const auto game = compile_checkdown(name);
    const auto terminals = cc::heads_up_terminals(game, table);
    cc::ClassSolver solver(game, terminals, 1.5, 0.0, 2.0);
    for (std::uint64_t iteration = 1U; iteration <= 20U; ++iteration) {
      solver.iterate(iteration);
    }
    double ev_sum = 0.0;
    for (std::uint8_t seat = 0U; seat < 2U; ++seat) {
      const double average = solver.value(seat, cc::Mode::Average);
      const double best = solver.value(seat, cc::Mode::BestResponse);
      require(best >= average - 1e-12, "the best response is at least the average");
      ev_sum += average;
    }
    const auto reach = solver.own_reach();
    const double rake = cc::expected_rake_by_reach(game, terminals, reach);
    require(std::abs(ev_sum + rake) <= 1e-12, "the EV sum is minus the expected rake by reach");
    require(game.config().rake.enabled ? rake > 0.0 : rake == 0.0,
            "expected rake positive with rake, zero without");
    for (const auto &node : game.nodes()) {
      if (node.kind != pb::NodeKind::Decision) {
        continue;
      }
      for (std::size_t hand_class = 0; hand_class < classes; ++hand_class) {
        double total = 0.0;
        for (const auto value : solver.average_strategy(node.id, hand_class)) {
          require(value >= 0.0, "average strategy non-negative");
          total += value;
        }
        require(close(total, 1.0, 1e-12), "average strategy sums to one");
      }
    }
    std::cout << name << ": 20 iterations, EV sum " << ev_sum << ", expected rake by reach " << rake
              << '\n';
  }
}

std::uint8_t class_of(const std::string_view label) {
  for (const auto &[hand_class, name] : gtosd::monker_charts::hand_classes().label_by_class) {
    if (name == label) {
      return hand_class;
    }
  }
  throw std::runtime_error("unknown hand class " + std::string(label));
}

bool disjoint(const std::uint16_t first, const std::uint16_t second) {
  const auto &combos = ca::combo_table();
  return (combos.masks[first] & combos.masks[second]) == 0U;
}

// Outcome counts of combo deals (hero, first, second) over their 142,506
// runouts through the rank table (count_combo_triple: no class reduction, no
// board symmetry), counted once per unordered opponent pair and returned in
// the requested opponent order.
class TripleCounts {
public:
  explicit TripleCounts(const ca::RankTable &ranks) : ranks_(ranks) {}

  ca::ComboTripleCounts get(const std::uint16_t hero, const std::uint16_t first,
                            const std::uint16_t second) {
    const auto low = std::min(first, second);
    const auto high = std::max(first, second);
    const std::array<std::uint16_t, 3> key{hero, low, high};
    auto found = cache_.find(key);
    if (found == cache_.end()) {
      const auto counted = ca::count_combo_triple(ranks_, hero, low, high);
      require(counted.has_value(), "a disjoint combo deal is counted");
      found = cache_.emplace(key, counted.value()).first;
    }
    if (first == low) {
      return found->second;
    }
    // The opponents swapped: the transposed cells and the winner masks with
    // bits 1 and 2 exchanged.
    const auto &counts = found->second;
    ca::ComboTripleCounts swapped;
    for (std::size_t x = 0; x < 3U; ++x) {
      for (std::size_t y = 0; y < 3U; ++y) {
        swapped.cells[3U * x + y] = counts.cells[3U * y + x];
      }
    }
    for (std::uint32_t mask = 1U; mask <= 7U; ++mask) {
      const auto other = (mask & 1U) | ((mask & 2U) << 1U) | ((mask & 4U) >> 1U);
      swapped.by_winners[other - 1U] = counts.by_winners[mask - 1U];
    }
    return swapped;
  }

  [[nodiscard]] std::size_t counted() const noexcept { return cache_.size(); }

private:
  const ca::RankTable &ranks_;
  std::map<std::array<std::uint16_t, 3>, ca::ComboTripleCounts> cache_;
};

// Combo-level value of `seat` holding `hero` at a terminal of the 3-way
// checkdown tree: every combo deal of the other seats disjoint from the hero
// and from each other, weighted by their class reach, with the payoff of the
// seat read for the winner set of every runout (3 active), for the result
// against the other active seat with the folded hand's cards dead or ignored
// (2 active), or constant (a fold, a folded seat).
double brute_force_value(const pb::CompiledGame &game, const std::uint32_t node_id,
                         const std::uint8_t seat, const std::uint16_t hero, const cc::Reach &reach,
                         const ca::FoldedCards folded_cards, const ca::AllInTable &heads_up,
                         TripleCounts &counts) {
  const auto &node = game.nodes()[node_id];
  const auto &combos = ca::combo_table();
  const auto seats = cc::other_seats(seat);
  const auto payoff = [&](const std::uint32_t winners) {
    return cc::payoff_antes(game, node_id, static_cast<std::uint8_t>(winners), seat);
  };
  std::array<std::vector<std::uint16_t>, 2> held_by;
  for (std::size_t side = 0; side < 2U; ++side) {
    for (std::uint16_t combo = 0U; combo < hands; ++combo) {
      if (reach[seats[side]][combos.hand_class[combo]] != 0.0 && disjoint(hero, combo)) {
        held_by[side].push_back(combo);
      }
    }
  }
  constexpr auto runouts = static_cast<double>(ca::three_way_runout_count);
  const auto seat_bit = 1U << seat;
  double total = 0.0;
  for (const auto first : held_by[0]) {
    for (const auto second : held_by[1]) {
      if (!disjoint(first, second)) {
        continue;
      }
      const double weight =
          reach[seats[0]][combos.hand_class[first]] * reach[seats[1]][combos.hand_class[second]];
      std::array<std::uint16_t, 3> held{};
      held[seat] = hero;
      held[seats[0]] = first;
      held[seats[1]] = second;
      if (node.kind == pb::NodeKind::TerminalFold) {
        total +=
            weight * static_cast<double>(game.fold_payoffs(node_id)[seat]) / cc::units_per_ante;
        continue;
      }
      if ((node.active_mask & seat_bit) == 0U) {
        total += weight * payoff(node.active_mask);
        continue;
      }
      if (node.active_mask == 7U) {
        const auto deal = counts.get(hero, first, second);
        double sum = 0.0;
        for (std::uint32_t local = 1U; local <= 7U; ++local) {
          const auto winners = ((local & 1U) != 0U ? seat_bit : 0U) |
                               ((local & 2U) != 0U ? 1U << seats[0] : 0U) |
                               ((local & 4U) != 0U ? 1U << seats[1] : 0U);
          sum += static_cast<double>(deal.by_winners[local - 1U]) * payoff(winners);
        }
        total += weight * sum / runouts;
        continue;
      }
      const auto opponent_bit = node.active_mask & ~seat_bit;
      const auto opponent = static_cast<std::size_t>(std::countr_zero(opponent_bit));
      const auto folded = static_cast<std::size_t>(std::countr_zero(~node.active_mask & 7U));
      const double win = payoff(seat_bit);
      const double tie = payoff(node.active_mask);
      const double lose = payoff(opponent_bit);
      if (folded_cards == ca::FoldedCards::Dead) {
        const auto deal = counts.get(hero, held[opponent], held[folded]);
        const auto &c = deal.cells;
        const double wins = static_cast<double>(c[0]) + c[1] + c[2];
        const double ties = static_cast<double>(c[3]) + c[4] + c[5];
        const double losses = static_cast<double>(c[6]) + c[7] + c[8];
        total += weight * (wins * win + ties * tie + losses * lose) / runouts;
      } else {
        const auto outcome = heads_up.outcome(hero, held[opponent]);
        total +=
            weight *
            (static_cast<double>(outcome.wins) * win + static_cast<double>(outcome.ties) * tie +
             static_cast<double>(outcome.losses) * lose) /
            static_cast<double>(outcome.total());
      }
    }
  }
  return total;
}

// The terminals the brute force checks: a 3-active showdown, a 2-active
// showdown with each seat as the folder, a fold.
std::vector<std::uint32_t> brute_force_terminals(const pb::CompiledGame &game) {
  std::optional<std::uint32_t> three_active;
  std::optional<std::uint32_t> fold;
  std::array<std::optional<std::uint32_t>, 3> by_folder;
  for (const auto &node : game.nodes()) {
    if (node.kind == pb::NodeKind::TerminalFold && !fold) {
      fold = node.id;
    }
    if (node.kind != pb::NodeKind::TerminalShowdown) {
      continue;
    }
    if (node.active_mask == 7U) {
      three_active = three_active.value_or(node.id);
      continue;
    }
    const auto folded = static_cast<std::size_t>(std::countr_zero(~node.active_mask & 7U));
    by_folder[folded] = by_folder[folded].value_or(node.id);
  }
  require(three_active && fold && by_folder[0] && by_folder[1] && by_folder[2],
          "the 3WAY50 checkdown tree has a 3-active showdown, a fold and a 2-active showdown "
          "after a fold of every seat");
  return {*three_active, *by_folder[0], *by_folder[1], *by_folder[2], *fold};
}

// The class values of every seat at the chosen terminals against the combo
// brute force on a small random reach (AA and AKs for every seat, so the
// opponents block each other's aces, and QQ for CO only), for the
// representative of each hero class; the deal count terms also through the
// inclusion-exclusion path.
void test_three_way_brute_force(const pb::CompiledGame &game,
                                const cc::CheckdownTerminals &terminals,
                                const ca::FoldedCards folded_cards, const ca::AllInTable &heads_up,
                                TripleCounts &counts,
                                const std::vector<std::uint8_t> &hero_classes) {
  const auto started = Clock::now();
  ca::DeterministicRandom random(2026093002U);
  cc::Reach reach(3U, std::vector<double>(classes, 0.0));
  for (std::size_t seat = 0; seat < 3U; ++seat) {
    for (const std::string_view label : {"AA", "AKs"}) {
      reach[seat][class_of(label)] = draw(random);
    }
  }
  reach[1][class_of("QQ")] = draw(random);
  double largest = 0.0;
  std::uint64_t compared = 0U;
  for (const auto node : brute_force_terminals(game)) {
    for (std::uint8_t seat = 0U; seat < 3U; ++seat) {
      const auto &term = terminals.terms[node][seat];
      const auto values =
          cc::contract(terminals.tensors[term.tensor], term.scale, cc::others_outer(reach, seat));
      std::optional<std::vector<double>> deal_values;
      if (term.tensor == 0U) {
        deal_values = cc::three_way_deal_values(reach, seat);
      }
      for (const auto hand_class : hero_classes) {
        const double expected =
            brute_force_value(game, node, seat, ca::ThreeWayTable::representative_of(hand_class),
                              reach, folded_cards, heads_up, counts);
        const auto where = "node " + std::to_string(node) + " seat " + std::to_string(seat) +
                           " class " + std::to_string(hand_class);
        largest = std::max(largest, std::abs(values[hand_class] - expected));
        require(close(values[hand_class], expected, 1e-12),
                "3-way class value equals the combo brute force at " + where);
        if (deal_values) {
          require(close(term.scale * (*deal_values)[hand_class], expected, 1e-12),
                  "3-way deal values equal the combo brute force at " + where);
        }
        ++compared;
      }
    }
  }
  std::cout << "3-way brute force (folded cards "
            << (folded_cards == ca::FoldedCards::Dead ? "dead" : "ignored") << "): " << compared
            << " values, " << counts.counted() << " combo deals counted, largest difference "
            << largest << " antes, "
            << std::chrono::duration<double>(Clock::now() - started).count() << " s\n";
}

// Inclusion-exclusion deal values against the dense contraction of N, on a
// full and on a sparse random reach, for every hero seat.
void test_three_way_deal_values(const cc::CheckdownTerminals &terminals) {
  ca::DeterministicRandom random(77U);
  for (int trial = 0; trial < 2; ++trial) {
    auto reach = random_reach(random, 3U);
    if (trial == 1) {
      for (auto &seat : reach) {
        for (auto &value : seat) {
          value = draw(random) < 0.6 ? 0.0 : value;
        }
      }
    }
    for (std::uint8_t hero = 0U; hero < 3U; ++hero) {
      const auto dense = cc::contract(terminals.tensors[0], 1.0, cc::others_outer(reach, hero));
      const auto fast = cc::three_way_deal_values(reach, hero);
      for (std::size_t hand_class = 0; hand_class < classes; ++hand_class) {
        require(close(fast[hand_class], dense[hand_class], 1e-12),
                "inclusion-exclusion deal values equal the dense contraction of N");
      }
    }
  }
  std::cout << "3-way deal values: inclusion-exclusion equals the dense N contraction\n";
}

// For every terminal and a random full reach: the seats' reach-weighted
// values sum to minus the terminal's rake times its reach-weighted deals
// (zero without rake). A wrong seat mapping or transpose breaks it.
void test_three_way_rake_identity(const pb::CompiledGame &game,
                                  const cc::CheckdownTerminals &terminals) {
  ca::DeterministicRandom random(4242U);
  const auto reach = random_reach(random, 3U);
  const auto masses = cc::class_masses();
  double largest = 0.0;
  std::uint64_t raked = 0U;
  for (const auto &node : game.nodes()) {
    if (node.kind != pb::NodeKind::TerminalFold && node.kind != pb::NodeKind::TerminalShowdown) {
      continue;
    }
    double sum = 0.0;
    double scale = 0.0;
    for (std::uint8_t seat = 0U; seat < 3U; ++seat) {
      const auto &term = terminals.terms[node.id][seat];
      const auto values =
          cc::contract(terminals.tensors[term.tensor], term.scale, cc::others_outer(reach, seat));
      double seat_sum = 0.0;
      for (std::size_t hand_class = 0; hand_class < classes; ++hand_class) {
        seat_sum += masses[hand_class] * reach[seat][hand_class] * values[hand_class];
      }
      sum += seat_sum;
      scale += std::abs(seat_sum);
    }
    const auto deals = cc::contract(terminals.tensors[0], 1.0, cc::others_outer(reach, 0U));
    double deal_mass = 0.0;
    for (std::size_t hand_class = 0; hand_class < classes; ++hand_class) {
      deal_mass += masses[hand_class] * reach[0][hand_class] * deals[hand_class];
    }
    const auto rake = gtosd::calculate_rake(game.config().rake, game.states()[node.id].pot,
                                            node.kind == pb::NodeKind::TerminalShowdown);
    require(rake.has_value(), "rake computed");
    const double expected =
        -static_cast<double>(rake.value().units()) / cc::units_per_ante * deal_mass;
    raked += rake.value().units() > 0 ? 1U : 0U;
    const double difference = std::abs(sum - expected);
    largest = std::max(largest, difference / std::max(1.0, scale));
    require(difference <= 1e-12 * std::max(1.0, scale),
            "the seats' values of a terminal sum to minus its rake times its deals");
  }
  require(game.config().rake.enabled ? raked == 51U : raked == 0U,
          "the 51 showdowns raked with rake, none without");
  std::cout << "3-way rake identity (" << game.config().id << "): " << raked
            << " raked terminals, largest relative difference " << largest << '\n';
}

// Short solves: the same EVs on 1 and 2 threads, best response at least the
// average, local gains non-negative, the EV sum minus the expected rake by
// reach, and a locked row kept as the average strategy.
void test_three_way_solver(const pb::CompiledGame &game, const cc::CheckdownTerminals &terminals) {
  constexpr std::uint64_t iterations = 3U;
  cc::ClassSolver one(game, terminals, 1.5, 0.0, 2.0, 1U);
  cc::ClassSolver two(game, terminals, 1.5, 0.0, 2.0, 2U);
  cc::ClassSolver locked(game, terminals, 1.5, 0.0, 2.0, 2U);
  const auto root = game.root();
  const auto edges = game.edges_of(root);
  std::vector<double> shove(edges.size(), 0.0);
  for (std::size_t action = 0; action < edges.size(); ++action) {
    shove[action] = edges[action].action.type == gtosd::ActionType::AllIn ? 1.0 : 0.0;
  }
  const auto aces = class_of("AA");
  locked.lock_row(root, aces, shove);
  require(locked.locked_rows() == 1U, "one locked row");
  for (std::uint64_t iteration = 1U; iteration <= iterations; ++iteration) {
    one.iterate(iteration);
    two.iterate(iteration);
    locked.iterate(iteration);
  }
  double ev_sum = 0.0;
  for (std::uint8_t seat = 0U; seat < 3U; ++seat) {
    const double average = two.value(seat, cc::Mode::Average);
    require(one.value(seat, cc::Mode::Average) == average,
            "1 and 2 threads give the same EV (every contraction in one piece)");
    const double best = two.value(seat, cc::Mode::BestResponse);
    require(best >= average - 1e-12, "the 3-way best response is at least the average");
    ev_sum += average;
    const double locked_average = locked.value(seat, cc::Mode::Average);
    require(locked.value(seat, cc::Mode::BestResponse) >= locked_average - 1e-12,
            "the best response against a locked profile is at least the average");
  }
  for (const auto &node : game.nodes()) {
    if (node.kind == pb::NodeKind::Decision) {
      require(two.local_gain(node.id) >= -1e-12, "local gains are non-negative");
    }
  }
  require(locked.average_strategy(root, aces) == shove, "the locked row is the average strategy");
  const double rake = cc::expected_rake_by_reach(game, terminals, two.own_reach());
  require(std::abs(ev_sum + rake) <= 1e-10, "the 3-way EV sum is minus the expected rake by reach");
  require(game.config().rake.enabled ? rake > 0.0 : rake == 0.0,
          "3-way expected rake positive with rake, zero without");
  std::cout << game.config().id << ": " << iterations << " iterations on 1 and 2 threads, EV sum "
            << ev_sum << ", expected rake by reach " << rake << ", locked AA shove kept\n";
}

// The 3-seat chart lock (the path of the MonkerSolver evaluation): a short
// solve written as charts (write_class_charts) and locked back on every chart
// node of a fresh solver (chart_lock_seats). Every class row of the 54 nodes
// is a chart row, a row outside the range or a zero row reached under the
// charts; each seat gets a chart row exactly where the source wrote a
// non-zero row; every locked row is the source's average strategy of the
// same node and class up to the three printed decimals (normalizing a row
// of k printed values moves a cell by at most k x 0.0005); the fresh solver
// holds every chart row once and keeps it through iterations, below the root
// too; the locked profile's EVs and best-response gains are the source's
// within 0.005 antes (the printed decimals move them by about 6e-4 after 20
// iterations; any one seat left unlocked, uniform, moves them by 1.4 to 2.1
// antes).
void test_three_way_chart_lock(const pb::CompiledGame &game,
                               const cc::CheckdownTerminals &terminals,
                               const std::filesystem::path &scratch) {
  constexpr std::uint64_t iterations = 20U;
  cc::ClassSolver source(game, terminals, 1.5, 0.0, 2.0, 2U);
  for (std::uint64_t iteration = 1U; iteration <= iterations; ++iteration) {
    source.iterate(iteration);
  }
  const auto directory = scratch / "three_way_chart_lock";
  std::filesystem::remove_all(directory);
  const auto written = cc::write_class_charts(game, source, directory);
  const auto nodes = mc::chart_nodes(game);
  require(written.size() == 54U && nodes.size() == 54U, "3WAY50: 54 charts written");
  const auto lock = mc::chart_lock_seats(game, directory, {"all"}, true);
  require(lock.files.size() == nodes.size(), "every chart node locked");
  require(static_cast<std::size_t>(lock.chart_rows) + lock.outside_range_rows +
                  lock.fallback_rows ==
              nodes.size() * classes,
          "every class row of every locked node classified once");
  const auto source_reach = source.own_reach();
  std::array<std::size_t, 3> written_rows{};
  for (const auto &chart : nodes) {
    const auto actor = game.nodes()[chart.node].actor;
    for (std::size_t hand_class = 0; hand_class < classes; ++hand_class) {
      written_rows[actor] +=
          source_reach[chart.node][actor][hand_class] >= cc::out_of_range_reach ? 1U : 0U;
    }
  }
  cc::ClassSolver locked(game, terminals, 1.5, 0.0, 2.0, 2U);
  std::array<std::size_t, 3> locked_rows{};
  double largest = 0.0;
  for (const auto &row : lock.lock.rows) {
    const auto actor = game.nodes()[row.node].actor;
    ++locked_rows[actor];
    const auto expected = source.average_strategy(row.node, row.hand_class);
    require(row.frequencies.size() == expected.size(), "a locked row has its node's actions");
    const double tolerance = 5e-4 * static_cast<double>(expected.size()) + 1e-9;
    for (std::size_t action = 0; action < expected.size(); ++action) {
      const double difference = std::abs(row.frequencies[action] - expected[action]);
      largest = std::max(largest, difference);
      require(difference <= tolerance,
              "a locked row is the source row of its node and class up to the printed decimals");
    }
    locked.lock_row(row.node, row.hand_class, row.frequencies);
  }
  for (std::size_t seat = 0; seat < 3U; ++seat) {
    require(locked_rows[seat] > 0U && locked_rows[seat] == written_rows[seat],
            "every seat's chart rows are the rows its charts wrote");
  }
  require(locked.locked_rows() == lock.chart_rows, "the solver holds every chart row once");
  double ev_difference = 0.0;
  double gain_difference = 0.0;
  for (std::uint8_t seat = 0U; seat < 3U; ++seat) {
    const double average = locked.value(seat, cc::Mode::Average);
    const double source_average = source.value(seat, cc::Mode::Average);
    ev_difference = std::max(ev_difference, std::abs(average - source_average));
    const double gain = locked.value(seat, cc::Mode::BestResponse) - average;
    const double source_gain = source.value(seat, cc::Mode::BestResponse) - source_average;
    gain_difference = std::max(gain_difference, std::abs(gain - source_gain));
  }
  require(ev_difference <= 5e-3 && gain_difference <= 5e-3,
          "the locked charts reproduce the source's EVs and gains within 0.005 antes");
  for (std::uint64_t iteration = 1U; iteration <= 2U; ++iteration) {
    locked.iterate(iteration);
  }
  for (const auto &row : lock.lock.rows) {
    require(locked.average_strategy(row.node, row.hand_class) == row.frequencies,
            "every locked row stays the average strategy through iterations");
  }
  std::cout << game.config().id << " chart lock: " << lock.chart_rows << " chart rows (seats "
            << locked_rows[0] << ", " << locked_rows[1] << ", " << locked_rows[2] << "), "
            << lock.outside_range_rows << " outside the range, " << lock.fallback_rows
            << " zero rows reached; largest row difference " << largest << ", EV " << ev_difference
            << ", gain " << gain_difference << " antes\n";
}

} // namespace

int main(const int argc, char **argv) {
  try {
    const auto started = Clock::now();
    std::filesystem::path resources_dir;
    std::filesystem::path scratch_dir =
        std::filesystem::temp_directory_path() / "gtosd_checkdown_classes_tests";
    for (int index = 1; index + 1 < argc; index += 2) {
      const std::string_view name = argv[index];
      if (name == "--resources-dir") {
        resources_dir = argv[index + 1];
      } else if (name == "--scratch-dir") {
        scratch_dir = argv[index + 1];
      } else {
        throw std::runtime_error("unknown argument " + std::string{name});
      }
    }
    std::optional<ca::AllInTable> table;
    if (!resources_dir.empty()) {
      auto loaded = ca::AllInTable::load(resources_dir / "preflop_all_in_v1.bin");
      if (loaded) {
        table.emplace(std::move(loaded.value()));
      }
    }
    const bool loaded = table.has_value();
    if (!table) {
      std::optional<ca::RankTable> ranks;
      if (!resources_dir.empty()) {
        auto loaded_ranks = ca::RankTable::load(resources_dir / "rank_table_v1.bin");
        if (loaded_ranks) {
          ranks.emplace(std::move(loaded_ranks.value()));
        }
      }
      if (!ranks) {
        auto built_ranks = ca::RankTable::build();
        require(built_ranks.has_value(), "rank table builds");
        ranks.emplace(std::move(built_ranks.value()));
      }
      auto built = ca::AllInTable::build(ranks.value(), 2U);
      require(built.has_value(), "heads-up all-in table builds");
      table.emplace(std::move(built.value()));
    }
    std::cout << "heads-up table " << (loaded ? "loaded" : "built") << '\n';
    test_aggregation(table.value());
    test_heads_up_terminals(table.value());
    test_three_seat_layout();
    test_three_way_tree();
    test_solver(table.value());

    // 3 seats: the rank table for the brute force, the complete three-player
    // table (or a subset of the brute-force hero classes).
    std::optional<ca::RankTable> ranks;
    if (!resources_dir.empty()) {
      auto loaded_ranks = ca::RankTable::load(resources_dir / "rank_table_v1.bin");
      if (loaded_ranks) {
        ranks.emplace(std::move(loaded_ranks.value()));
      }
    }
    if (!ranks) {
      auto built_ranks = ca::RankTable::build();
      require(built_ranks.has_value(), "rank table builds");
      ranks.emplace(std::move(built_ranks.value()));
    }
    const std::vector<std::uint8_t> hero_classes{class_of("AA"), class_of("AKs"), class_of("KQs")};
    std::optional<ca::ThreeWayTable> three_way;
    if (!resources_dir.empty()) {
      auto loaded_three_way =
          ca::ThreeWayTable::load(resources_dir / std::string(ca::three_way_table_file_name));
      if (loaded_three_way) {
        three_way.emplace(std::move(loaded_three_way.value()));
      }
    }
    const bool complete = three_way.has_value();
    if (!three_way) {
      auto built =
          ca::ThreeWayTable::build(ranks.value(), ca::ThreeWayBuildOptions{2U, true, hero_classes});
      require(built.has_value(), "subset three-player table builds");
      three_way.emplace(std::move(built.value()));
    }
    std::cout << "three-player table "
              << (complete ? "loaded (complete)" : "absent: subset of AA, AKs, KQs built") << '\n';
    TripleCounts counts(ranks.value());
    {
      const auto game = compile_checkdown("3WAY50_donk_rake.json");
      for (const auto convention : {ca::FoldedCards::Dead, ca::FoldedCards::Ignored}) {
        const auto terminals =
            cc::three_way_terminals(game, three_way.value(), table.value(), convention);
        require(terminals.tensors.size() == 116U && terminals.showdown_terminals == 51U &&
                    terminals.fold_terminals == 25U,
                "3WAY50: N and 115 showdown tensors (3 x 13 + 2 x 38), 25 folds");
        test_three_way_brute_force(game, terminals, convention, table.value(), counts,
                                   hero_classes);
        if (!complete) {
          continue;
        }
        require(terminals.deals == cc::three_way_deals, "630 x 561 x 496 ordered 3-way deals");
        test_three_way_rake_identity(game, terminals);
        if (convention == ca::FoldedCards::Dead) {
          test_three_way_deal_values(terminals);
          test_three_way_solver(game, terminals);
          test_three_way_chart_lock(game, terminals, scratch_dir);
        }
      }
    }
    if (complete) {
      const auto game = compile_checkdown("3WAY50_donk.json");
      const auto terminals =
          cc::three_way_terminals(game, three_way.value(), table.value(), ca::FoldedCards::Dead);
      test_three_way_rake_identity(game, terminals);
      test_three_way_solver(game, terminals);
    } else {
      std::cout << "3-way rake identity, deal values, solves and chart lock skipped: no complete "
                   "table\n";
    }
    std::cout << "PREFLOP_BLUEPRINT_CHECKDOWN_CLASSES_TESTS=PASS assertions=" << assertions
              << " seconds=" << std::chrono::duration<double>(Clock::now() - started).count()
              << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "PREFLOP_BLUEPRINT_CHECKDOWN_CLASSES_TESTS=FAIL " << error.what() << '\n';
    return 1;
  }
}
