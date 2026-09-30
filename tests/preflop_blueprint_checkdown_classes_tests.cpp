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
#include "../benchmarks/checkdown_classes.hpp"

#include "gtosd/card_abstraction/all_in_table.hpp"
#include "gtosd/card_abstraction/deterministic_random.hpp"
#include "gtosd/card_abstraction/rank_table.hpp"
#include "gtosd/card_abstraction/showdown_counts.hpp"
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
    std::cout << "PREFLOP_BLUEPRINT_CHECKDOWN_CLASSES_TESTS=PASS assertions=" << assertions
              << " seconds=" << std::chrono::duration<double>(Clock::now() - started).count()
              << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "PREFLOP_BLUEPRINT_CHECKDOWN_CLASSES_TESTS=FAIL " << error.what() << '\n';
    return 1;
  }
}
