// Phase 3 (3-way step 2), lane T smoke of the 3-seat trainer path (task T1;
// the gate tests V4-V7, V9, V9b, V10, V12, V13 are T3/K5). On 3WAY50 with the
// small 200/500/1000 per-board buckets (about 5 M cells, so it runs in seconds):
//  - creation: 3 traversal seats, 3 heroes, skipped units for every hero;
//  - census per hero and board pass: visited + pruned + shortcut-skipped
//    terminals = every terminal of the tree (no prune at iteration 1);
//  - finite tables, thread determinism (1, 2 and 4 threads at partition
//    target 64), NaN-poisoned skipped units change nothing;
//  - V6 in small (board_kernels mode on an exact list of 4 boards, where the
//    walk below a preflop fold is board-restricted like the shortcut): the
//    hero-folded shortcut on and off give the same regrets after one
//    simultaneous iteration (row-scaled 1e-10), and the same subtree values
//    through TrainerAccess::subtree_values3 on a non-uniform policy;
//  - V7 in small: a heads-up game through the 3-seat path with the virtual
//    third seat (three_seat_harness) gives the heads-up regrets after one
//    simultaneous iteration (row-scaled 1e-10);
//  - board_kernels mode on the checkdown-compiled 3WAY50 (refused without
//    validation), and the class-cache mode on the same tree;
//  - zero-sum identity through terminal3 (TrainerAccess::terminal3_values) on
//    the 3-active showdowns of the rake-free 3WAY50, every seat as hero, scaled
//    by the joint reach mass times the largest payoff (a royal flush board ties
//    every hand, where a ratio to sum|terms| would compare rounding noise).
// Needs preflop_three_way_v1.bin (complete) under --resources-dir; SKIP (exit
// 77) without it.
#include "preflop_blueprint_test_support.hpp"

#include "gtosd/card_abstraction/three_way_table.hpp"
#include "gtosd/preflop_blueprint/trainer_access.hpp"

#include <bit>
#include <cmath>
#include <iomanip>
#include <random>

using namespace pb_test;

namespace {

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

std::uint64_t terminal_census(const pb::CompiledGame &game) {
  return game.stats().terminal_folds + game.stats().terminal_showdowns;
}

bool all_finite(const std::vector<double> &values) {
  return std::all_of(values.begin(), values.end(), [](const double value) {
    return std::isfinite(value);
  });
}

// Largest |a - b| over a row divided by the largest |a| of the row, over every
// decision row of the layout (rows of zeros compare exactly).
double row_scaled_difference(const pb::CompiledGame &game, const pb::StateLayout &layout,
                             const std::vector<double> &left, const std::vector<double> &right) {
  double worst = 0.0;
  for (const auto &node : game.nodes()) {
    if (node.kind != pb::NodeKind::Decision)
      continue;
    const auto rows = pb::StateLayout::rows_for(node.street, layout.flop_capacity,
                                                layout.turn_capacity, layout.river_capacity);
    for (std::uint32_t row = 0; row < rows; ++row) {
      const auto offset = layout.offsets[node.id] + static_cast<std::uint64_t>(row) * node.action_count;
      double scale = 0.0;
      double difference = 0.0;
      for (std::uint8_t action = 0; action < node.action_count; ++action) {
        scale = std::max({scale, std::abs(left[offset + action]), std::abs(right[offset + action])});
        difference = std::max(difference, std::abs(left[offset + action] - right[offset + action]));
      }
      if (difference == 0.0)
        continue;
      worst = std::max(worst, scale > 0.0 ? difference / scale : HUGE_VAL);
    }
  }
  return worst;
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

void test_basics(const Resources &resources, const ca::ThreeWayTable &table,
                 const pb::CompiledGame &game) {
  auto view = resources.view();
  view.three_way = &table;
  auto config = base_config(resources);
  config.detailed_profile = true;
  auto trainer = make_trainer(game, view, config);
  require(trainer->traversal_seats() == 3U && trainer->heroes() == 3U, "3 seats, 3 heroes");
  std::cout << "3WAY50 (rake 2.5%/2a), 200/500/1000: " << trainer->cell_count() << " cells, "
            << trainer->partition().unit_roots.size() << " units at target 64, skipped units "
            << trainer->skipped_unit_count(0U) << "/" << trainer->skipped_unit_count(1U) << "/"
            << trainer->skipped_unit_count(2U) << ", class cache "
            << trainer->memory_breakdown().class_cache_bytes << " bytes\n";
  for (std::uint8_t hero = 0U; hero < 3U; ++hero)
    require(trainer->skipped_unit_count(hero) > 0U, "every hero skips some units");
  const auto census = terminal_census(game);
  for (int iteration = 1; iteration <= 3; ++iteration) {
    const auto telemetry = trainer->iterate();
    require(telemetry.has_value(), "3-seat iteration succeeds");
    const auto &value = telemetry.value();
    for (std::size_t hero = 0; hero < 3U; ++hero) {
      const auto visited = value.terminals_visited_by_hero[hero];
      const auto pruned = value.terminals_pruned_by_hero[hero];
      const auto skipped = value.terminals_shortcut_by_hero[hero];
      std::cout << "  iteration " << iteration << " hero " << hero << ": visited " << visited
                << ", pruned " << pruned << ", shortcut " << skipped << " (census " << census
                << " x " << config.batch_boards << "), units run "
                << value.units_run_by_hero[hero] << ", skipped " << value.units_skipped_by_hero[hero]
                << '\n';
      require(visited + pruned + skipped == census * config.batch_boards,
              "census = visited + pruned + shortcut-skipped");
      require(iteration > 1 || pruned == 0U, "no zero-reach prune at iteration 1");
      require(skipped > 0U, "the shortcut skips terminals");
    }
    std::cout << "  iteration " << iteration << ": " << std::fixed << std::setprecision(3)
              << value.seconds << " s (traversal " << value.traversal_seconds << ", class cache "
              << value.class_cache_seconds << ", refresh " << value.policy_refresh_seconds << ")\n"
              << std::defaultfloat;
  }
  require(all_finite(trainer->regrets()) && all_finite(trainer->strategy_sums()),
          "3-seat tables are finite");
  const auto reference = trainer->state_fingerprint();
  for (const unsigned threads : {1U, 4U}) {
    auto other_config = base_config(resources);
    other_config.threads = threads;
    auto other = make_trainer(game, view, other_config);
    for (int iteration = 0; iteration < 3; ++iteration)
      require(other->iterate().has_value(), "iteration succeeds");
    require(other->identity() == trainer->identity(), "identity independent of threads/profile");
    require(other->state_fingerprint() == reference, "3-seat state identical across threads");
  }
  auto poisoned_config = base_config(resources);
  poisoned_config.poison_skipped_units = true;
  auto poisoned = make_trainer(game, view, poisoned_config);
  for (int iteration = 0; iteration < 3; ++iteration)
    require(poisoned->iterate().has_value(), "poisoned iteration succeeds");
  require(poisoned->state_fingerprint() == reference,
          "NaN in the skipped units' buffers changes nothing");
  std::cout << "basics: fingerprint " << reference << " on 1, 2 and 4 threads and with poison\n";
}

void test_shortcut_off(const Resources &resources, const ca::ThreeWayTable &table,
                       const pb::CompiledGame &game) {
  auto view = resources.view();
  view.three_way = &table;
  auto config = base_config(resources);
  config.update_mode = pb::UpdateMode::Simultaneous;
  auto refused = config;
  refused.hero_folded_shortcut = false;
  require(!pb::Trainer::create(game, view, refused).has_value(),
          "the shortcut cannot be switched off without validation");
  // In class-cache mode a preflop fold's shortcut is the board-free class
  // value, equal to the walk of its subtree only in expectation: the exact
  // comparison runs with the board-kernel preflop terminals on an exact list.
  refused.validation = true;
  require(!pb::Trainer::create(game, view, refused).has_value(),
          "the shortcut cannot be switched off in class-cache mode");
  pb::TrainingBoards boards;
  boards.histories = {make_history({"Ks", "Qd", "7c", "9h", "6s"}),
                      make_history({"As", "Ad", "Ac", "Kh", "Ks"}),
                      make_history({"Ts", "Js", "Qs", "Ks", "As"}),
                      make_history({"6h", "6d", "8s", "8c", "Th"})};
  boards.weights = {1.0, 1.0, 2.0, 3.0};
  boards.sample = false;
  config.validation = true;
  config.poison_skipped_units = true;
  config.preflop_terminals = pb::PreflopTerminals::BoardKernels;
  auto with = make_trainer(game, view, config, &boards);
  config.hero_folded_shortcut = false;
  auto without = make_trainer(game, view, config, &boards);
  require(with->identity() != without->identity(), "the shortcut switch is in the identity");
  require(without->skipped_unit_count(0U) == 0U, "no unit is skipped without the shortcut");
  require(with->iterate().has_value() && without->iterate().has_value(), "one iteration each");
  const auto regret_difference =
      row_scaled_difference(game, with->layout(), with->regrets(), without->regrets());
  const auto sum_difference =
      row_scaled_difference(game, with->layout(), with->strategy_sums(), without->strategy_sums());
  std::cout << "shortcut on/off, board_kernels, 4 listed boards, one simultaneous iteration: "
               "regrets row-scaled "
            << regret_difference << ", strategy sums " << sum_difference << '\n';
  require(regret_difference <= 1e-10 && sum_difference <= 1e-10,
          "the hero-folded shortcut equals the full walk");

  // Subtree values on a non-uniform policy through the test hook: random
  // regrets (some rows with exact zeros), sparse non-constant injected reach,
  // a limped 3-way flop entry, every hero.
  std::mt19937_64 random(20261001ULL);
  std::uniform_real_distribution<double> uniform(-1.0, 1.0);
  auto fresh = config;
  fresh.hero_folded_shortcut = true;
  auto on = make_trainer(game, view, fresh, &boards);
  fresh.hero_folded_shortcut = false;
  auto off = make_trainer(game, view, fresh, &boards);
  std::vector<double> regrets(on->cell_count());
  for (auto &regret : regrets) {
    const double draw = uniform(random);
    regret = draw < -0.3 ? 0.0 : draw;
  }
  std::uint32_t entry = pb::no_node;
  for (const auto id : game.postflop_entries()) {
    if (game.nodes()[id].active_mask == 7U) {
      entry = id;
      break;
    }
  }
  require(entry != pb::no_node, "a 3-way flop entry exists");
  const auto board = make_history({"Ks", "Qd", "7c", "9h", "6s"});
  std::array<std::vector<double>, 3> reach;
  std::uniform_int_distribution<int> hand_draw(0, static_cast<int>(pb::live_hand_count) - 1);
  std::uniform_real_distribution<double> weight(0.1, 1.0);
  for (auto &seat : reach) {
    seat.assign(pb::live_hand_count, 0.0);
    for (int pick = 0; pick < 20; ++pick)
      seat[static_cast<std::size_t>(hand_draw(random))] = weight(random);
  }
  struct CountingSink final : pb::HeroDecisionSink {
    std::uint64_t calls{0U};
    void record(const pb::HeroDecisionTrace &trace) override {
      ++calls;
      require(trace.values.size() == pb::live_hand_count &&
                  trace.action_values.size() ==
                      static_cast<std::size_t>(trace.actions) * pb::live_hand_count,
              "trace spans");
    }
  };
  for (std::uint8_t hero = 0U; hero < 3U; ++hero) {
    // The same current policy for both walks (a hook call writes regrets).
    require(pb::TrainerAccess::set_regrets(*on, regrets).has_value() &&
                pb::TrainerAccess::set_regrets(*off, regrets).has_value(),
            "regrets set");
    CountingSink sink;
    const auto left = pb::TrainerAccess::subtree_values3(*on, entry, reach, hero, board, &sink);
    const auto right = pb::TrainerAccess::subtree_values3(*off, entry, reach, hero, board);
    require(left.has_value() && right.has_value(), "subtree_values3 runs");
    double scale = 0.0;
    double difference = 0.0;
    for (std::size_t hand = 0; hand < pb::live_hand_count; ++hand) {
      require(std::isfinite(left.value().values[hand]), "subtree values finite");
      scale = std::max(scale, std::abs(right.value().values[hand]));
      difference =
          std::max(difference, std::abs(left.value().values[hand] - right.value().values[hand]));
    }
    std::cout << "  subtree_values3 at entry " << entry << " hero " << int{hero} << ": "
              << sink.calls << " hero decisions traced, units run " << left.value().units_run
              << " (skipped " << left.value().units_skipped << "), max |v| " << scale
              << ", on/off difference " << difference << '\n';
    require(sink.calls > 0U && left.value().units_skipped > 0U, "trace and skipped units");
    require(difference <= 1e-10 * std::max(scale, 1e-300), "subtree values: shortcut = full walk");
    require(all_finite(on->regrets()) && all_finite(off->regrets()), "hook regrets finite");
    const auto hook_difference =
        row_scaled_difference(game, on->layout(), on->regrets(), off->regrets());
    std::cout << "  regrets after the hook call, on/off row-scaled " << hook_difference << '\n';
    require(hook_difference <= 1e-10, "hook regret updates: shortcut = full walk");
  }
}

void test_harness(const Resources &resources) {
  const auto game = compile(load_fixture("preflop_blueprint_hu10_reduced_v1.json"));
  auto config = base_config(resources);
  config.update_mode = pb::UpdateMode::Simultaneous;
  auto heads_up = make_trainer(game, resources.view(), config);
  config.three_seat_harness = true;
  auto harness = make_trainer(game, resources.view(), config);
  require(harness->traversal_seats() == 3U && harness->heroes() == 2U, "harness seats");
  require(heads_up->traversal_seats() == 2U, "heads-up path keeps 2 seats");
  require(harness->identity() != heads_up->identity(), "the harness is in the identity");
  require(heads_up->iterate().has_value() && harness->iterate().has_value(), "one iteration each");
  const auto difference =
      row_scaled_difference(game, heads_up->layout(), heads_up->regrets(), harness->regrets());
  const auto sums = row_scaled_difference(game, heads_up->layout(), heads_up->strategy_sums(),
                                          harness->strategy_sums());
  std::cout << "harness vs heads-up, one simultaneous iteration (HU10 reduced): regrets row-scaled "
            << difference << ", strategy sums " << sums << '\n';
  require(difference <= 1e-10 && sums <= 1e-10, "the 3-seat path with a dead seat is heads-up");
  pb::TrainerConfig refused = base_config(resources);
  refused.three_seat_harness = true;
  const auto three = compile(load_monker("3WAY50_donk_rake25cap2.json"));
  require(!pb::Trainer::create(three, resources.view(), refused).has_value(),
          "the harness is refused on a 3-player game");
}

void test_checkdown(const Resources &resources, const ca::ThreeWayTable &table) {
  const auto game = compile(load_monker("3WAY50_donk_rake25cap2.json"), true);
  pb::TrainingBoards boards;
  boards.histories = {make_history({"Ks", "Qd", "7c", "9h", "6s"}),
                      make_history({"As", "Ad", "Ac", "Kh", "Ks"}),
                      make_history({"Ts", "Js", "Qs", "Ks", "As"})};
  boards.weights = {1.0, 2.0, 3.0};
  boards.sample = false;
  auto view = resources.view();
  auto config = base_config(resources);
  config.preflop_terminals = pb::PreflopTerminals::BoardKernels;
  require(!pb::Trainer::create(game, view, config, &boards).has_value(),
          "board_kernels needs validation");
  config.validation = true;
  require(!pb::Trainer::create(game, view, config).has_value(),
          "board_kernels needs an exact board list");
  auto kernels = make_trainer(game, view, config, &boards);
  view.three_way = &table;
  auto cache_config = base_config(resources);
  auto cache = make_trainer(game, view, cache_config, &boards);
  for (int iteration = 0; iteration < 3; ++iteration)
    require(kernels->iterate().has_value() && cache->iterate().has_value(), "checkdown iterates");
  require(all_finite(kernels->regrets()) && all_finite(cache->regrets()),
          "checkdown tables finite");
  std::cout << "checkdown 3WAY50 (3 boards, exact list): board_kernels and class_cache modes run, "
            << game.stats().preflop_all_in_runouts << " preflop showdowns\n";
}

void test_zero_sum_terminals(const Resources &resources, const ca::ThreeWayTable &table) {
  const auto game = compile(load_monker("3WAY50_donk.json"));
  auto view = resources.view();
  view.three_way = &table;
  auto trainer = make_trainer(game, view, base_config(resources));
  std::vector<std::uint32_t> terminals;
  for (const auto &node : game.nodes())
    if (node.kind == pb::NodeKind::TerminalShowdown && node.street != gtosd::Street::Preflop &&
        node.active_mask == 7U)
      terminals.push_back(node.id);
  require(!terminals.empty(), "3-active postflop showdowns exist");
  std::mt19937_64 random(7ULL);
  std::uniform_real_distribution<double> weight(0.0, 1.0);
  std::array<std::vector<double>, 3> reach;
  for (auto &seat : reach) {
    seat.assign(pb::live_hand_count, 0.0);
    for (auto &value : seat)
      value = weight(random) < 0.3 ? 0.0 : weight(random);
  }
  // Scale of the identity: the joint reach mass times the largest payoff of the
  // terminal, an upper bound of every |<r_s, V_s>| (the D-scaled tolerance of
  // spec section 7). Sum|terms| is not a usable scale: where every hand ties
  // (a royal flush on board) the exact values are 0 up to odd chips and the
  // computed ones are rounding residues of the loss mass, so their ratio to
  // their own sum of magnitudes is noise over noise.
  double reach_product = 1.0;
  for (const auto &seat : reach) {
    double total = 0.0;
    for (const auto value : seat)
      total += value;
    reach_product *= total;
  }
  double worst = 0.0;
  double worst_magnitude_ratio = 0.0;
  for (const auto &texts : {std::array<std::string_view, 5>{"Ks", "Qd", "7c", "9h", "6s"},
                            std::array<std::string_view, 5>{"As", "Ad", "Ac", "Kh", "Ks"},
                            std::array<std::string_view, 5>{"Ts", "Js", "Qs", "Ks", "As"}}) {
    const auto board = make_history(texts);
    std::array<std::vector<std::vector<double>>, 3> values;
    for (std::uint8_t hero = 0U; hero < 3U; ++hero) {
      auto computed = pb::TrainerAccess::terminal3_values(*trainer, terminals, reach, hero, board);
      require(computed.has_value(), "terminal3_values runs");
      values[hero] = std::move(computed.value());
    }
    for (std::size_t index = 0; index < terminals.size(); ++index) {
      double payoff = 0.0;
      for (std::uint8_t winners = 1U; winners <= 7U; ++winners)
        for (std::size_t seat = 0; seat < 3U; ++seat)
          payoff = std::max(payoff, std::abs(static_cast<double>(game.showdown_payoffs(
                                                 terminals[index], winners)[seat])) /
                                        static_cast<double>(gtosd::Money::units_per_ante));
      double sum = 0.0;
      double magnitude = 0.0;
      for (std::size_t hero = 0; hero < 3U; ++hero) {
        for (std::size_t hand = 0; hand < pb::live_hand_count; ++hand) {
          const double term = reach[hero][hand] * values[hero][index][hand];
          sum += term;
          magnitude += std::abs(term);
        }
      }
      worst = std::max(worst, std::abs(sum) / (reach_product * std::max(payoff, 1e-300)));
      if (magnitude > 0.0)
        worst_magnitude_ratio = std::max(worst_magnitude_ratio, std::abs(sum) / magnitude);
    }
  }
  std::cout << "zero-sum identity through terminal3 on " << terminals.size()
            << " 3-active postflop showdowns x 3 boards (incl. a royal flush on board): worst "
               "|sum| / (reach product x max |payoff|) "
            << worst << " (|sum| / sum|terms| " << worst_magnitude_ratio << ")\n";
  require(worst <= 1e-12, "sum over seats of <r_s, V_s> = 0 without rake");
}

} // namespace

int main(const int argc, char **argv) {
  try {
    std::filesystem::path resources_dir;
    std::filesystem::path buckets_dir;
    for (int index = 1; index + 1 < argc; index += 2) {
      const std::string_view name = argv[index];
      const std::string_view value = argv[index + 1];
      if (name == "--resources-dir")
        resources_dir = value;
      else if (name == "--buckets-dir")
        buckets_dir = value;
      else
        throw std::runtime_error("unknown argument " + std::string(name));
    }
    auto table = ca::ThreeWayTable::load(resources_dir / std::string(ca::three_way_table_file_name));
    if (!table) {
      std::cout << "PREFLOP_BLUEPRINT_TRAINER3_PATH_TESTS=SKIP (no complete three-way table)\n";
      return 77;
    }
    const auto resources = load_resources(resources_dir, buckets_dir);
    const auto game = compile(load_monker("3WAY50_donk_rake25cap2.json"));
    test_basics(resources, table.value(), game);
    test_shortcut_off(resources, table.value(), game);
    test_harness(resources);
    test_checkdown(resources, table.value());
    test_zero_sum_terminals(resources, table.value());
    std::cout << "PREFLOP_BLUEPRINT_TRAINER3_PATH_TESTS=PASS assertions=" << assertions << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "PREFLOP_BLUEPRINT_TRAINER3_PATH_TESTS=FAIL " << error.what() << " after "
              << assertions << " assertions\n";
    return 1;
  }
}
