#include "gtosd/preflop_blueprint/policy_file.hpp"
#include "gtosd/solver/enumerated_best_response.hpp"
#include "preflop_blueprint_test_support.hpp"
#include <map>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace pb_test;

struct ParsedKey {
  std::uint8_t player;
  std::uint32_t node;
  std::uint32_t row;
};

ParsedKey parse_key(const std::string &key) {
  const auto node_position = key.find("|n");
  const auto row_position = key.find("|r");
  require(key.size() > 2U && key[0] == 'p' && node_position != std::string::npos &&
              row_position != std::string::npos,
          "information set key parses");
  ParsedKey parsed;
  parsed.player = static_cast<std::uint8_t>(std::stoul(key.substr(1, node_position - 1)));
  parsed.node = static_cast<std::uint32_t>(
      std::stoul(key.substr(node_position + 2, row_position - node_position - 2)));
  parsed.row = static_cast<std::uint32_t>(std::stoul(key.substr(row_position + 2)));
  return parsed;
}

pb::TrainingBoards branching_boards() {
  pb::TrainingBoards boards;
  for (const auto &flop : {std::array<std::string_view, 3>{"6s", "7d", "8c"},
                           std::array<std::string_view, 3>{"6d", "7h", "8s"}}) {
    for (const auto turn : {"9h", "9c"}) {
      for (const auto river : {"As", "Ad"}) {
        boards.histories.push_back(make_history({flop[0], flop[1], flop[2], turn, river}));
        boards.weights.push_back(static_cast<double>(boards.weights.size() + 1U));
      }
    }
  }
  boards.sample = false;
  return boards;
}

pb::HandSubsets deep_subsets() {
  auto subsets = oracle_subsets();
  for (auto &combos : subsets.combos) {
    combos = {combos.front(), combos.back()};
  }
  return subsets;
}

// A matching scalar CFR implementation is not a convergence proof when both
// use an imperfect-recall partition. Here the optimal deviation is explicit,
// so the witness does not rely on a best-response algorithm for such games.
void test_forgotten_information_witness() {
  for (const bool remember : {false, true}) {
    gtosd::FiniteGame game;
    game.game_id = remember ? "remembered_type" : "forgotten_type";
    game.nodes.resize(11U);
    game.nodes[0].kind = gtosd::GameNodeKind::Chance;
    game.nodes[0].edges = {{{0U, "L"}, 1U, 0.5}, {{1U, "R"}, 6U, 0.5}};
    for (const auto before : {1U, 6U}) {
      auto &first = game.nodes[before];
      first.kind = gtosd::GameNodeKind::Decision;
      first.information_set = before == 1U ? "L_before" : "R_before";
      first.edges = {{{0U, "quit"}, before + 1U, 0.0}, {{1U, "enter"}, before + 2U, 0.0}};
      auto &second = game.nodes[before + 2U];
      second.kind = gtosd::GameNodeKind::Decision;
      second.information_set = remember ? (before == 1U ? "L_after" : "R_after") : "merged_after";
      second.edges = {{{0U, "a"}, before + 3U, 0.0}, {{1U, "b"}, before + 4U, 0.0}};
      const double a = before == 1U ? 2.0 : -3.0;
      const double b = before == 1U ? -1.0 : 0.0;
      game.nodes[before + 3U].payoff = {a, -a};
      game.nodes[before + 4U].payoff = {b, -b};
    }
    auto deviation = gtosd::uniform_strategy_profile(game);
    require(deviation.has_value(), "recall witness profile builds");
    for (auto &[key, strategy] : deviation.value()) {
      strategy.probabilities =
          key == "L_before" ? std::vector<double>{0.0, 1.0} : std::vector<double>{1.0, 0.0};
    }
    const auto deviation_value = gtosd::evaluate_strategy_profile(game, deviation.value());
    require(deviation_value.has_value() && close(deviation_value.value()[0], 1.0, 1e-12),
            "enter in L, quit in R, then a achieves the known optimum in both partitions");
    gtosd::SolverConfig config;
    config.algorithm = gtosd::SolverAlgorithm::LinearCfr;
    config.iterations = 10'000U;
    const auto solved = gtosd::solve_finite_game(game, config);
    require(solved.has_value(), "recall witness solves");
    const auto value = gtosd::evaluate_strategy_profile(game, solved.value().average_strategy);
    require(value.has_value(), "recall witness average evaluates");
    const double gap = deviation_value.value()[0] - value.value()[0];
    if (remember) {
      require(gap < 3e-8, "retaining the earlier information recovers convergence");
    } else {
      require(
          close(gap, 0.75, 1e-7),
          "CFR can stall with a representable profitable deviation after forgetting information");
    }
    std::cout << game.game_id << ": EV=" << value.value()[0] << " explicit deviation gain=" << gap
              << '\n';
  }
}

void test_diagnostic_contracts(const Resources &resources) {
  const auto game = pb::CompiledGame::compile(load_fixture("preflop_blueprint_co40_test_v1.json"));
  require(game.has_value(), "diagnostic contract game compiles");
  const auto config = resources.config();
  const auto boards = branching_boards();
  const auto subsets = deep_subsets();
  auto source = pb::Trainer::create(game.value(), resources.view(), config, &boards, &subsets);
  require(source.has_value(), "diagnostic source creates");
  const auto directory = std::filesystem::temp_directory_path() / "gtosd_preflop_blueprint_tests";
  std::filesystem::create_directories(directory);
  const auto path = directory / "diagnostic_identity.bin";
  require(source.value()->save_checkpoint(path).has_value(), "diagnostic checkpoint saves");
  for (const auto change : {0U, 1U, 2U}) {
    auto changed_boards = boards;
    auto changed_subsets = subsets;
    if (change == 0U) {
      changed_boards.histories[0].river = card("Ac");
    } else if (change == 1U) {
      changed_boards.weights[0] += 1.0;
    } else {
      changed_subsets.combos[0][0] = ca::combo_index(card("Ts"), card("Js"));
    }
    auto changed = pb::Trainer::create(game.value(), resources.view(), config, &changed_boards,
                                       &changed_subsets);
    require(changed.has_value(), "changed diagnostic game creates");
    require(changed.value()->identity() != source.value()->identity(),
            "board contents, probabilities and private support identify distinct games");
    const auto loaded = changed.value()->load_checkpoint(path);
    require(!loaded && loaded.error() == pb::TrainerError::IntegrityFailure,
            "a same-size diagnostic game cannot resume another game's regrets");
  }

  auto incompatible = pb::Trainer::create(game.value(), resources.view(), config, &boards);
  require(incompatible.has_value(), "board-conditioned diagnostic training remains available");
  const auto estimate = incompatible.value()->estimate_exploitability(0U);
  require(!estimate && estimate.error() == pb::TrainerError::UnsupportedBoardPrior,
          "a fixed-board private prior cannot be certified as the evaluator's physical prior");
  std::cout << "diagnostic contracts: incompatible prior and changed checkpoint inputs rejected\n";
}

// Condition on player 0 having sampled board A. The expectation of player 1's
// update must then equal an exact traversal against that updated policy, with
// chance still averaging A and B. Reusing A makes player 1's target biased.
// rake: the rake of the raked correctness runs (2.5 %, cap 2 antes, no flop
// no drop; rake study U3), a general-sum game: the exact reference updates
// player 1 on its own payoff, so a sampled alternating update that used
// minus the opponent's payoff would show as a mismatch.
void test_alternating_conditional_expectation(const Resources &resources, const int stack,
                                              const bool rake = false) {
  auto fixture = load_fixture("preflop_blueprint_co40_test_v1.json");
  fixture.effective_stack =
      gtosd::Money::from_units(static_cast<std::int64_t>(stack) * gtosd::Money::units_per_ante)
          .value();
  if (rake) {
    fixture.rake.enabled = true;
    fixture.rake.percentage = gtosd::RangeWeight::from_basis_points(250).value();
    fixture.rake.cap = gtosd::Money::from_antes(2).value();
    fixture.rake.no_flop_no_drop = true;
    fixture.rake.minimum_pot = gtosd::Money{};
  }
  const auto game = pb::CompiledGame::compile(fixture);
  require(game.has_value(), "conditional-expectation game compiles");
  std::uint64_t raked_showdowns = 0U;
  std::uint64_t capped_showdowns = 0U;
  if (rake) {
    for (const auto &node : game.value().nodes()) {
      if (node.kind != pb::NodeKind::TerminalShowdown) {
        continue;
      }
      const auto payoffs = game.value().showdown_payoffs(node.id, std::uint8_t{1});
      const auto taken = -(payoffs[0] + payoffs[1]);
      raked_showdowns += taken > 0 ? 1U : 0U;
      capped_showdowns += taken == fixture.rake.cap.units() ? 1U : 0U;
    }
    require(raked_showdowns > 0U, "the raked conditional-expectation game is general-sum");
  }
  pb::TrainingBoards boards;
  boards.histories = {make_history({"6s", "7d", "8c", "6c", "Tc"}),
                      make_history({"6s", "7d", "8c", "6c", "Kc"})};
  boards.weights = {1.0, 1.0};
  const auto subsets = deep_subsets();
  auto first_board = boards;
  first_board.histories.resize(1U);
  first_board.weights.resize(1U);
  FiniteGameBuilder first_builder(game.value(), resources);
  const auto first_finite = first_builder.build(first_board, subsets);
  gtosd::SolverConfig reference_config;
  reference_config.algorithm = gtosd::SolverAlgorithm::LinearCfr;
  reference_config.iterations = 1U;
  const auto first = gtosd::solve_finite_game(first_finite, reference_config);
  require(first.has_value(), "first player's conditional reference solves");

  FiniteGameBuilder full_builder(game.value(), resources);
  const auto full_finite = full_builder.build(boards, subsets);
  const auto initial = gtosd::solve_finite_game(full_finite, reference_config);
  require(initial.has_value(), "full conditional reference initializes");
  auto checkpoint = initial.value().checkpoint;
  checkpoint.completed_iterations = 0U;
  for (auto &[key, buffer] : checkpoint.information_sets) {
    std::fill(buffer.cumulative_strategy.begin(), buffer.cumulative_strategy.end(), 0.0);
    std::fill(buffer.cumulative_regret.begin(), buffer.cumulative_regret.end(), 0.0);
    const auto found = first.value().checkpoint.information_sets.find(key);
    if (buffer.player == 0U && found != first.value().checkpoint.information_sets.end()) {
      buffer.cumulative_regret = found->second.cumulative_regret;
    }
  }
  const auto expected = gtosd::solve_finite_game(full_finite, reference_config, &checkpoint);
  require(expected.has_value(), "conditional exact second-player update computes");

  std::array<std::vector<double>, 2> regrets;
  std::array<std::vector<double>, 2> sums;
  pb::StateLayout layout;
  boards.sample = true;
  for (std::size_t second = 0U; second < 2U; ++second) {
    std::uint64_t seed = 0U;
    for (;; ++seed) {
      ca::DeterministicRandom random(seed);
      const bool first_is_a = random.uniform_unit() < 0.5;
      const bool second_is_a = random.uniform_unit() < 0.5;
      if (first_is_a && second_is_a == (second == 0U)) {
        break;
      }
      require(seed < 1000U, "conditional seed found within a bounded search");
    }
    auto config = resources.config();
    config.batch_boards = 1U;
    config.training_seed = seed;
    config.scheme = pb::WeightingScheme::Linear;
    config.update_mode = pb::UpdateMode::Alternating;
    auto trainer = pb::Trainer::create(game.value(), resources.view(), config, &boards, &subsets);
    require(trainer.has_value(), "conditional sampled trainer creates");
    const auto telemetry = trainer.value()->iterate();
    require(telemetry.has_value(), "conditional sampled iteration succeeds");
    require(telemetry.value().boards == 2U && trainer.value()->boards_processed() == 2U,
            "telemetry counts the two independently drawn boards");
    layout = trainer.value()->layout();
    regrets[second] = trainer.value()->regrets();
    sums[second] = trainer.value()->strategy_sums();
  }
  double max_error = 0.0;
  double max_sum_error = 0.0;
  for (const auto &[key, buffer] : expected.value().checkpoint.information_sets) {
    if (buffer.player != 1U) {
      continue;
    }
    const auto parsed = parse_key(key);
    const auto &node = game.value().nodes()[parsed.node];
    const auto offset =
        layout.offsets[parsed.node] + static_cast<std::uint64_t>(parsed.row) * node.action_count;
    for (std::size_t action = 0U; action < node.action_count; ++action) {
      const auto cell = offset + action;
      const double actual = 0.5 * (regrets[0][cell] + regrets[1][cell]);
      max_error = std::max(max_error, std::abs(actual - buffer.cumulative_regret[action]));
      max_sum_error = std::max(max_sum_error, std::abs(0.5 * (sums[0][cell] + sums[1][cell]) -
                                                       buffer.cumulative_strategy[action]));
    }
  }
  std::cout << "alternating conditional expectation stack=" << stack;
  if (rake) {
    std::cout << " rake 2.5% cap 2a (raked showdowns " << raked_showdowns << ", at the cap "
              << capped_showdowns << ")";
  }
  std::cout << ": max regret error=" << max_error << " max strategy-sum error=" << max_sum_error
            << std::endl;
  require(max_error < 1e-10, "conditional regret expectation equals the exact traversal");
  require(max_sum_error < 1e-10, "conditional strategy-sum expectation equals the exact traversal");
}

// rake: the HU10 reduced fixture with rake (5 %, cap 0.5 ante, no flop no
// drop), a general-sum game: the oracle's CFR updates each player on its own
// payoff, so any zero-sum shortcut in the trainer would show as a mismatch.
void test_finite_game_oracle(const Resources &resources, const int stack = 0,
                             const bool class_mode = false, const bool rake = false) {
  const auto started = Clock::now();
  auto fixture = load_fixture(stack != 0 ? "preflop_blueprint_co40_test_v1.json"
                              : rake     ? "preflop_blueprint_hu10_reduced_rake_v1.json"
                                         : "preflop_blueprint_hu10_reduced_v1.json");
  if (stack != 0) {
    fixture.effective_stack =
        gtosd::Money::from_units(static_cast<std::int64_t>(stack) * gtosd::Money::units_per_ante)
            .value();
  }
  const auto game = pb::CompiledGame::compile(fixture);
  require(game.has_value(), "HU10 reduced compiles");
  const auto boards = stack == 0 ? oracle_boards() : branching_boards();
  const auto subsets = stack == 0 ? oracle_subsets() : deep_subsets();
  std::cout << "oracle stack=" << fixture.effective_stack.units() / gtosd::Money::units_per_ante
            << " public_nodes=" << game.value().nodes().size() << std::endl;
  constexpr std::uint64_t iterations = 25U;

  std::optional<pb::ClassBucketRows> class_rows;
  if (class_mode) {
    auto mapped = pb::ClassBucketRows::build(*resources.flop, *resources.turn, *resources.river);
    require(mapped.has_value(), "class rows build for trainer oracle");
    class_rows.emplace(std::move(mapped.value()));
  }
  FiniteGameBuilder builder(game.value(), resources, false, nullptr,
                            class_rows ? &*class_rows : nullptr);
  const auto finite = builder.build(boards, subsets);
  const auto summary = gtosd::validate_finite_game(finite);
  require(summary.has_value(),
          std::string("finite game validates: ") +
              (summary ? "" : gtosd::solver_error_name(summary.error())));
  gtosd::SolverConfig solver_config;
  solver_config.algorithm = gtosd::SolverAlgorithm::LinearCfr;
  solver_config.iterations = iterations;
  solver_config.thread_count = 1U;
  const auto solved = gtosd::solve_finite_game(finite, solver_config);
  require(solved.has_value(), "finite game solves");

  auto config = resources.config();
  config.threads = 2U;
  config.scheme = pb::WeightingScheme::Linear;
  config.update_mode = pb::UpdateMode::Simultaneous;
  auto training_resources = resources.view();
  if (class_rows) {
    training_resources.class_rows = &*class_rows;
    config.flop_capacity = class_rows->count(ca::BucketStreet::Flop);
    config.turn_capacity = class_rows->count(ca::BucketStreet::Turn);
    config.river_capacity = class_rows->count(ca::BucketStreet::River);
    auto invalid = config;
    ++invalid.river_capacity;
    require(!pb::Trainer::create(game.value(), training_resources, invalid, &boards, &subsets),
            "class row capacity mismatch is rejected before traversal");
    require(!pb::Trainer::create(game.value(), resources.view(), config, &boards, &subsets),
            "class capacity cannot be interpreted as plain buckets");
  }
  auto trainer = pb::Trainer::create(game.value(), training_resources, config, &boards, &subsets);
  require(trainer.has_value(),
          std::string("trainer creates: ") +
              (trainer ? "" : pb::trainer_error_name(trainer.error())));
  for (std::uint64_t iteration = 0; iteration < iterations; ++iteration) {
    require(trainer.value()->iterate().has_value(), "exact-mode iteration succeeds");
  }
  const auto &layout = trainer.value()->layout();
  const auto regrets = trainer.value()->regrets();
  const auto sums = trainer.value()->strategy_sums();
  const auto average = trainer.value()->average_policy();
  std::vector<std::uint8_t> covered(regrets.size(), 0U);
  double maximum_regret_error = 0.0;
  double maximum_strategy_error = 0.0;
  std::uint64_t compared = 0U;
  for (const auto &[key, buffer] : solved.value().checkpoint.information_sets) {
    const auto parsed = parse_key(key);
    const auto &node = game.value().nodes()[parsed.node];
    require(node.kind == pb::NodeKind::Decision && node.actor == parsed.player &&
                buffer.actions.size() == node.action_count,
            "oracle information set maps onto a compiled decision");
    const auto offset = layout.offsets[parsed.node] +
                        static_cast<std::uint64_t>(parsed.row) * node.action_count;
    const auto average_row = average.row(parsed.node, parsed.row);
    const auto &oracle_average = solved.value().average_strategy.at(key);
    for (std::size_t action = 0; action < node.action_count; ++action) {
      covered[offset + action] = 1U;
      maximum_regret_error = std::max(
          maximum_regret_error, std::abs(regrets[offset + action] - buffer.cumulative_regret[action]));
      maximum_strategy_error =
          std::max(maximum_strategy_error,
                   std::abs(sums[offset + action] - buffer.cumulative_strategy[action]));
      if (!close(regrets[offset + action], buffer.cumulative_regret[action], 1e-9)) {
        std::cout << "first mismatch " << key << " action=" << action
                  << " regret=" << regrets[offset + action]
                  << " reference=" << buffer.cumulative_regret[action] << std::endl;
      }
      require(close(regrets[offset + action], buffer.cumulative_regret[action], 1e-9),
              "cumulative regret equals the FiniteGame oracle within 1e-9");
      require(close(sums[offset + action], buffer.cumulative_strategy[action], 1e-9),
              "cumulative strategy equals the FiniteGame oracle within 1e-9");
      require(close(average_row[action], oracle_average.probabilities[action], 1e-9),
              "average strategy equals the FiniteGame oracle within 1e-9");
      ++compared;
    }
  }
  for (std::size_t cell = 0; cell < regrets.size(); ++cell) {
    if (covered[cell] == 0U) {
      require(regrets[cell] == 0.0 && sums[cell] == 0.0,
              "cells outside the reduced game stay untouched");
    }
  }
  const auto exact = trainer.value()->estimate_exploitability(0U);
  require(exact.has_value() && exact.value().exact, "exact evaluation on the listed boards");
  const auto bucket_value =
      gtosd::evaluate_strategy_profile(finite, solved.value().average_strategy);
  require(bucket_value.has_value(), "bucket profile evaluates without a recall assumption");
  // A bucket partition may forget earlier information. The old greedy BR was
  // not an exact constrained oracle on these games. Compare the trainer with
  // the independently built physical game, lifting the SAME bucket policy.
  FiniteGameBuilder physical_builder(game.value(), resources, true, &average,
                                     class_rows ? &*class_rows : nullptr);
  const auto physical_game = physical_builder.build(boards, subsets);
  const auto nash_conv = gtosd::calculate_nash_conv(physical_game, physical_builder.profile());
  require(nash_conv.has_value(), "lossless FiniteGame NashConv computes");
  require(close(bucket_value.value()[0], nash_conv.value().profile_value[0], 1e-9) &&
              close(bucket_value.value()[1], nash_conv.value().profile_value[1], 1e-9),
          "lifting preserves both profile values");
  require(close(exact.value().ev[0], nash_conv.value().profile_value[0], 1e-9) &&
              close(exact.value().ev[1], nash_conv.value().profile_value[1], 1e-9),
          "exact profile values equal calculate_nash_conv within 1e-9");
  require(
      close(exact.value().best_response[0], nash_conv.value().best_response_value[0], 1e-9) &&
          close(exact.value().best_response[1], nash_conv.value().best_response_value[1], 1e-9) &&
          close(exact.value().nashconv, nash_conv.value().nash_conv, 1e-9),
      "both physical best responses and NashConv match the independent lossless oracle");
  if (rake) {
    require(nash_conv.value().expected_payoff_sum < -1e-6 &&
                close(exact.value().ev[0] + exact.value().ev[1],
                      nash_conv.value().expected_payoff_sum, 1e-9) &&
                std::isnan(nash_conv.value().zero_sum_exploitability),
            "with rake the EVs sum to minus the expected rake, not to zero");
  }
  for (const std::uint8_t player : {std::uint8_t{0}, std::uint8_t{1}}) {
    const auto recall = gtosd::has_perfect_recall(finite, player);
    const auto estimate = gtosd::estimate_best_response_enumeration(finite, player);
    require(recall.has_value() && estimate.has_value(), "bucket BR preflight computes");
    std::cout << "bucket BR preflight player=" << static_cast<unsigned>(player)
              << " perfect_recall=" << recall.value()
              << " information_sets=" << estimate.value().information_sets
              << " policies=" << estimate.value().policies
              << " overflow=" << estimate.value().overflow << '\n';
  }
  std::cout << "oracle" << (rake ? " (rake)" : "") << ": finite game " << summary.value().nodes
            << " nodes, "
            << summary.value().information_sets << " information sets, " << compared
            << " cells compared, max regret error " << maximum_regret_error
            << ", max strategy error " << maximum_strategy_error << ", nashconv "
            << exact.value().nashconv << " (oracle " << nash_conv.value().nash_conv << "), EV ["
            << exact.value().ev[0] << ", " << exact.value().ev[1] << "], "
            << std::chrono::duration<double>(Clock::now() - started).count() << " s\n";
}

void test_determinism(const Resources &resources) {
  const auto game = pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_reduced_v1.json"));
  require(game.has_value(), "HU10 reduced compiles");
  std::string reference;
  std::vector<double> reference_regrets;
  std::vector<double> reference_sums;
  for (const unsigned threads : {1U, 2U, 4U, 8U}) {
    auto config = resources.config();
    config.threads = threads;
    config.batch_boards = 8U;
    auto trainer = pb::Trainer::create(game.value(), resources.view(), config);
    require(trainer.has_value(), "trainer creates");
    for (int iteration = 0; iteration < 4; ++iteration) {
      require(trainer.value()->iterate().has_value(), "iteration succeeds");
    }
    const auto fingerprint = trainer.value()->state_fingerprint();
    if (reference.empty()) {
      reference = fingerprint;
      reference_regrets = trainer.value()->regrets();
      reference_sums = trainer.value()->strategy_sums();
    }
    require(trainer.value()->regrets() == reference_regrets &&
                trainer.value()->strategy_sums() == reference_sums,
            "tables are bit-identical across thread counts");
    require(fingerprint == reference, "state is bit-identical across thread counts");
  }
  {
    auto plain_config = resources.config();
    plain_config.threads = 1U;
    plain_config.batch_boards = 8U;
    plain_config.partition_target_nodes = 1'000'000U;
    auto plain = pb::Trainer::create(game.value(), resources.view(), plain_config);
    require(plain.has_value(), "single-unit trainer creates");
    for (int iteration = 0; iteration < 4; ++iteration) {
      require(plain.value()->iterate().has_value(), "iteration succeeds");
    }
    std::size_t plain_differing = 0U;
    const auto plain_regrets = plain.value()->regrets();
    const auto plain_sums = plain.value()->strategy_sums();
    for (std::size_t cell = 0; cell < reference_regrets.size(); ++cell) {
      if (plain_regrets[cell] != reference_regrets[cell] ||
          plain_sums[cell] != reference_sums[cell]) {
        ++plain_differing;
      }
    }
    std::cout << "single-unit partition (" << plain.value()->partition().unit_roots.size()
              << " units, " << plain.value()->partition().top_nodes << " top nodes) vs default: "
              << plain_differing << " cells differ\n";
  }
  auto config = resources.config();
  config.threads = 3U;
  config.batch_boards = 8U;
  config.partition_target_nodes = 24U;
  auto trainer = pb::Trainer::create(game.value(), resources.view(), config);
  require(trainer.has_value(), "trainer with a fine partition creates");
  require(trainer.value()->partition().unit_roots.size() > 8U, "fine partition has many units");
  std::cout << "fine partition: " << trainer.value()->partition().unit_roots.size() << " units, "
            << trainer.value()->partition().top_nodes << " top nodes, largest unit "
            << trainer.value()->partition().largest_unit_nodes << '\n';
  for (int iteration = 0; iteration < 4; ++iteration) {
    require(trainer.value()->iterate().has_value(), "iteration succeeds");
  }
  std::size_t differing = 0U;
  double maximum_difference = 0.0;
  const auto fine_regrets = trainer.value()->regrets();
  const auto fine_sums = trainer.value()->strategy_sums();
  for (std::size_t cell = 0; cell < reference_regrets.size(); ++cell) {
    const auto regret = fine_regrets[cell];
    const auto sum = fine_sums[cell];
    if (regret != reference_regrets[cell] || sum != reference_sums[cell]) {
      if (differing < 6U) {
        std::cout << "partition mismatch cell " << cell << ": regret " << regret << " vs "
                  << reference_regrets[cell] << ", sum " << sum << " vs " << reference_sums[cell]
                  << '\n';
      }
      ++differing;
      maximum_difference = std::max({maximum_difference, std::abs(regret - reference_regrets[cell]),
                                     std::abs(sum - reference_sums[cell])});
    }
  }
  if (differing != 0U) {
    std::cout << "partition mismatch: " << differing << " cells differ, max difference "
              << maximum_difference << '\n';
  }
  require(differing == 0U && trainer.value()->state_fingerprint() == reference,
          "state does not depend on the subtree partition");
  std::cout << "determinism: fingerprint " << reference << " for 1/2/4/8 threads and a "
            << trainer.value()->partition().unit_roots.size() << "-unit partition\n";
}

void test_resume(const Resources &resources) {
  const auto game = pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_reduced_v1.json"));
  require(game.has_value(), "HU10 reduced compiles");
  auto config = resources.config();
  config.threads = 4U;
  config.batch_boards = 8U;
  config.scheme = pb::WeightingScheme::Dcfr;
  config.update_mode = pb::UpdateMode::Alternating;
  auto continuous = pb::Trainer::create(game.value(), resources.view(), config);
  require(continuous.has_value(), "continuous trainer creates");
  for (int iteration = 0; iteration < 6; ++iteration) {
    require(continuous.value()->iterate().has_value(), "iteration succeeds");
  }

  auto first = pb::Trainer::create(game.value(), resources.view(), config);
  require(first.has_value(), "first trainer creates");
  for (int iteration = 0; iteration < 3; ++iteration) {
    require(first.value()->iterate().has_value(), "iteration succeeds");
  }
  const auto directory = std::filesystem::temp_directory_path() / "gtosd_preflop_blueprint_tests";
  std::filesystem::create_directories(directory);
  const auto path = directory / "trainer_checkpoint.bin";
  require(first.value()->save_checkpoint(path).has_value(), "checkpoint saves");
  require(!std::filesystem::exists(path.string() + ".tmp"), "temporary file renamed away");

  auto resumed = pb::Trainer::create(game.value(), resources.view(), config);
  require(resumed.has_value(), "resumed trainer creates");
  require(resumed.value()->load_checkpoint(path).has_value(), "checkpoint loads");
  require(resumed.value()->iteration() == 3U, "iteration restored");
  for (int iteration = 0; iteration < 3; ++iteration) {
    require(resumed.value()->iterate().has_value(), "iteration succeeds");
  }
  require(resumed.value()->regrets() == continuous.value()->regrets() &&
              resumed.value()->strategy_sums() == continuous.value()->strategy_sums(),
          "resume from checkpoint equals the continuous run bit for bit");
  require(resumed.value()->state_fingerprint() == continuous.value()->state_fingerprint(),
          "iteration, RNG states and tables coincide after the resume");
  const auto continuous_estimate = continuous.value()->estimate_exploitability(50U);
  const auto resumed_estimate = resumed.value()->estimate_exploitability(50U);
  require(continuous_estimate.has_value() && resumed_estimate.has_value() &&
              resumed_estimate.value().max_gain == continuous_estimate.value().max_gain,
          "evaluation RNG restored: identical sampled estimate");

  // Corruption and identity mismatch are rejected.
  {
    auto bytes = read_file(path);
    bytes[bytes.size() / 2U] = static_cast<char>(bytes[bytes.size() / 2U] ^ 0x5A);
    std::ofstream output(path.string() + ".corrupt", std::ios::binary);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  }
  const auto corrupt = resumed.value()->load_checkpoint(path.string() + ".corrupt");
  require(!corrupt.has_value() && corrupt.error() == pb::TrainerError::IntegrityFailure,
          "corrupted checkpoint rejected");
  auto other_config = config;
  other_config.training_seed += 1U;
  auto other = pb::Trainer::create(game.value(), resources.view(), other_config);
  require(other.has_value(), "trainer with another seed creates");
  const auto mismatch = other.value()->load_checkpoint(path);
  require(!mismatch.has_value() && mismatch.error() == pb::TrainerError::IntegrityFailure,
          "checkpoint of another identity rejected");
  std::cout << "resume: checkpoint " << std::filesystem::file_size(path) << " bytes, fingerprint "
            << resumed.value()->state_fingerprint() << '\n';
}

void test_lazy_dcfr_discount(const Resources &resources) {
  const auto game = pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_reduced_v1.json"));
  require(game.has_value(), "lazy-discount fixture compiles");
  auto eager_config = resources.config();
  eager_config.threads = 2U;
  eager_config.batch_boards = 8U;
  eager_config.batch_policy_refresh = true;
  eager_config.scheme = pb::WeightingScheme::Dcfr;
  eager_config.update_mode = pb::UpdateMode::Alternating;
  auto lazy_config = eager_config;
  lazy_config.lazy_discount = true;
  auto single_config = lazy_config;
  single_config.threads = 1U;
  auto eager = pb::Trainer::create(game.value(), resources.view(), eager_config);
  auto lazy = pb::Trainer::create(game.value(), resources.view(), lazy_config);
  auto single = pb::Trainer::create(game.value(), resources.view(), single_config);
  require(eager.has_value() && lazy.has_value() && single.has_value(),
          "eager and lazy DCFR trainers create");

  double maximum_regret_difference = 0.0;
  double maximum_sum_difference = 0.0;
  double maximum_policy_difference = 0.0;
  for (int iteration = 0; iteration < 25; ++iteration) {
    require(eager.value()->iterate().has_value() && lazy.value()->iterate().has_value() &&
                single.value()->iterate().has_value(),
            "eager and lazy DCFR iterations succeed");
  }
  const auto eager_policy = eager.value()->average_policy();
  const auto lazy_policy = lazy.value()->average_policy();
  const auto single_policy = single.value()->average_policy();
  const auto eager_regrets = eager.value()->regrets();
  const auto eager_sums = eager.value()->strategy_sums();
  const auto lazy_regrets = lazy.value()->regrets();
  const auto lazy_sums = lazy.value()->strategy_sums();
  require(eager_regrets.size() == lazy_regrets.size() && eager_sums.size() == lazy_sums.size(),
          "lazy layout matches eager layout");
  for (std::size_t cell = 0; cell < eager_regrets.size(); ++cell) {
      maximum_regret_difference =
          std::max(maximum_regret_difference,
                   std::abs(eager_regrets[cell] - lazy_regrets[cell]));
      maximum_sum_difference =
          std::max(maximum_sum_difference,
                   std::abs(eager_sums[cell] - lazy_sums[cell]));
      maximum_policy_difference =
          std::max(maximum_policy_difference,
                   std::abs(eager_policy.table()[cell] - lazy_policy.table()[cell]));
  }
  std::cout << "lazy hybrid DCFR differences: regret " << maximum_regret_difference
            << ", sum " << maximum_sum_difference << ", policy "
            << maximum_policy_difference << '\n';
  // The lazy path applies the skipped positive factors as one ratio of prefix products
  // instead of one multiply per iteration. Regret matching is discontinuous at zero, so a
  // rounding-level difference can change the trajectory (the differences above are
  // reported, not bounded); what is checked is that the applied factor equals the
  // product of the per-iteration factors to rounding, and that the lazy path itself is
  // thread-deterministic bit for bit.
  double maximum_ratio_error = 0.0;
  for (const std::uint64_t last : {0ULL, 1ULL, 2ULL, 5ULL, 10ULL, 100ULL, 1000ULL, 5000ULL}) {
    for (const std::uint64_t iteration : {1ULL, 2ULL, 3ULL, 6ULL, 11ULL, 101ULL, 1001ULL,
                                          5001ULL, 16000ULL, 20000ULL}) {
      if (iteration <= last)
        continue;
      double sequential = 1.0;
      for (std::uint64_t step = last + 1U; step <= iteration; ++step) {
        const double power = std::pow(static_cast<double>(step), lazy_config.dcfr_alpha);
        sequential *= power / (power + 1.0);
      }
      const double ratio = lazy.value()->positive_discount_ratio(last, iteration);
      maximum_ratio_error =
          std::max(maximum_ratio_error, std::abs(ratio - sequential) / sequential);
    }
  }
  std::cout << "lazy positive discount ratio: max relative error " << maximum_ratio_error
            << " against the sequential product\n";
  require(maximum_ratio_error <= 1.0e-12,
          "lazy positive discount ratio equals the sequential product to rounding");
  require(lazy.value()->regrets() == single.value()->regrets() &&
              lazy.value()->strategy_sums() == single.value()->strategy_sums() &&
              lazy_policy.table() == single_policy.table(),
          "hybrid lazy DCFR is thread-deterministic");

  const auto directory = std::filesystem::temp_directory_path() / "gtosd_preflop_blueprint_tests";
  std::filesystem::create_directories(directory);
  const auto path = directory / "lazy_trainer_checkpoint.bin";
  require(lazy.value()->save_checkpoint(path).has_value(), "lazy checkpoint saves");
  auto resumed = pb::Trainer::create(game.value(), resources.view(), lazy_config);
  require(resumed.has_value() && resumed.value()->load_checkpoint(path).has_value(),
          "lazy checkpoint loads");
  require(resumed.value()->state_fingerprint() == lazy.value()->state_fingerprint(),
          "lazy checkpoint restores its fully materialized state");
  for (int iteration = 0; iteration < 5; ++iteration) {
    require(lazy.value()->iterate().has_value() && resumed.value()->iterate().has_value(),
            "continuous and resumed lazy DCFR iterations succeed");
  }
  require(resumed.value()->state_fingerprint() == lazy.value()->state_fingerprint(),
          "lazy checkpoint resume is bit-identical after further training");

  auto invalid = lazy_config;
  invalid.scheme = pb::WeightingScheme::Linear;
  require(!pb::Trainer::create(game.value(), resources.view(), invalid),
          "lazy discount rejects non-DCFR weighting");
  invalid.scheme = pb::WeightingScheme::Dcfr;
  invalid.dcfr_beta = 0.5;
  require(!pb::Trainer::create(game.value(), resources.view(), invalid),
          "lazy discount rejects unsupported nonzero beta");
  std::cout << "lazy DCFR: max regret difference " << maximum_regret_difference
            << ", sum difference " << maximum_sum_difference << ", policy difference "
            << maximum_policy_difference << '\n';
}

// Chart snapshots read average rows from the live tables with pending lazy discounts:
// the read must not perturb the training (bit-identical state) and must equal the
// rows of the exported average policy, which materializes every discount first.
void test_average_strategy_row_snapshot(const Resources &resources) {
  const auto game = pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_reduced_v1.json"));
  require(game.has_value(), "snapshot fixture compiles");
  auto config = resources.config();
  config.threads = 2U;
  config.batch_boards = 8U;
  config.batch_policy_refresh = true;
  config.scheme = pb::WeightingScheme::Dcfr;
  config.update_mode = pb::UpdateMode::Alternating;
  config.lazy_discount = true;
  auto reference = pb::Trainer::create(game.value(), resources.view(), config);
  auto snapshot = pb::Trainer::create(game.value(), resources.view(), config);
  require(reference.has_value() && snapshot.has_value(), "snapshot trainers create");
  const auto &layout = snapshot.value()->layout();
  const auto read_all = [&](const pb::Trainer &trainer) {
    std::vector<double> rows;
    std::array<double, pb::maximum_actions> values{};
    for (const auto &node : game.value().nodes()) {
      if (node.kind != pb::NodeKind::Decision)
        continue;
      const auto count = pb::StateLayout::rows_for(node.street, layout.flop_capacity,
                                                    layout.turn_capacity, layout.river_capacity);
      for (std::uint32_t row = 0; row < count; ++row) {
        trainer.average_strategy_row(node.id, row, values.data());
        rows.insert(rows.end(), values.begin(), values.begin() + node.action_count);
      }
    }
    return rows;
  };
  for (int iteration = 0; iteration < 12; ++iteration) {
    require(reference.value()->iterate().has_value() && snapshot.value()->iterate().has_value(),
            "snapshot iterations succeed");
    if (iteration == 5)
      (void)read_all(*snapshot.value());
  }
  // Read before anything materializes the discounts: state_fingerprint() and
  // average_policy() both apply every pending discount first.
  const auto live = read_all(*snapshot.value());
  std::uint64_t pending = 0U;
  for (const auto &node : game.value().nodes()) {
    if (node.kind != pb::NodeKind::Decision)
      continue;
    const auto count = pb::StateLayout::rows_for(node.street, layout.flop_capacity,
                                                  layout.turn_capacity, layout.river_capacity);
    for (std::uint32_t row = 0; row < count; ++row) {
      const auto last = snapshot.value()->discount_last_iteration(node.id, row);
      if (last > 0U && last + 1U < snapshot.value()->iteration())
        ++pending;
    }
  }
  require(pending > 0U, "the snapshot test reads rows with pending discounts");
  const auto policy = snapshot.value()->average_policy();
  std::size_t index = 0;
  double largest = 0.0;
  for (const auto &node : game.value().nodes()) {
    if (node.kind != pb::NodeKind::Decision)
      continue;
    const auto count = pb::StateLayout::rows_for(node.street, layout.flop_capacity,
                                                  layout.turn_capacity, layout.river_capacity);
    for (std::uint32_t row = 0; row < count; ++row) {
      const auto exported = policy.row(node.id, row);
      for (std::uint8_t action = 0; action < node.action_count; ++action) {
        largest = std::max(largest, std::abs(live[index++] - exported[action]));
      }
    }
  }
  require(index == live.size(), "snapshot reads every row of the layout");
  require(largest <= 1e-12, "live average rows equal the exported average policy");
  require(snapshot.value()->state_fingerprint() == reference.value()->state_fingerprint(),
          "reading average rows does not perturb the training");
  std::cout << "average-row snapshot: rows read " << index << ", pending " << pending
            << ", largest difference " << largest
            << '\n';
}

// Policy snapshots of the trainer CLI (--policy-snapshots): the average policy file
// written without modifying the state at iteration k must leave the training
// bit-identical (state after 2k equals a run without the snapshot) and must be the
// file a trainer exports at iteration k with save_average_policy, byte for byte, with
// eager and with pending lazy discounts, in every table storage (a narrow storage
// rounds the materialized cells, which the snapshot has to reproduce).
void test_average_policy_snapshot(const Resources &resources) {
  const auto game = pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_reduced_v1.json"));
  require(game.has_value(), "policy snapshot fixture compiles");
  const auto directory = std::filesystem::temp_directory_path() / "gtosd_preflop_blueprint_tests";
  std::filesystem::create_directories(directory);
  constexpr int k = 7;
  // The last case rebases the 16-bit discount epoch at iteration 5, so the snapshot
  // reads timestamps relative to a nonzero epoch base.
  struct SnapshotCase {
    pb::TableStorage storage;
    const char *storage_name;
    bool lazy;
    std::uint32_t epoch;
  };
  const std::array<SnapshotCase, 7> cases{
      {{pb::TableStorage::Double, "double", false, 65'535U},
       {pb::TableStorage::Double, "double", true, 65'535U},
       {pb::TableStorage::MixedFloatSums, "mixed", false, 65'535U},
       {pb::TableStorage::MixedFloatSums, "mixed", true, 65'535U},
       {pb::TableStorage::Float32, "float32", false, 65'535U},
       {pb::TableStorage::Float32, "float32", true, 65'535U},
       {pb::TableStorage::Float32, "float32", true, 4U}}};
  for (const auto &[storage, storage_name, lazy, epoch] : cases) {
    auto config = resources.config();
    config.threads = 2U;
    config.batch_boards = 8U;
    config.batch_policy_refresh = true;
    config.scheme = pb::WeightingScheme::Dcfr;
    config.update_mode = pb::UpdateMode::Alternating;
    config.lazy_discount = lazy;
    config.storage = storage;
    config.lazy_discount_epoch = epoch;
    auto snapshot = pb::Trainer::create(game.value(), resources.view(), config);
    auto plain = pb::Trainer::create(game.value(), resources.view(), config);
    auto exported = pb::Trainer::create(game.value(), resources.view(), config);
    require(snapshot.has_value() && plain.has_value() && exported.has_value(),
            "policy snapshot trainers create");
    for (int iteration = 0; iteration < k; ++iteration) {
      require(snapshot.value()->iterate().has_value() && plain.value()->iterate().has_value() &&
                  exported.value()->iterate().has_value(),
              "policy snapshot iterations succeed");
    }
    std::uint64_t pending = 0U;
    const auto &layout = snapshot.value()->layout();
    for (const auto &node : game.value().nodes()) {
      if (node.kind != pb::NodeKind::Decision)
        continue;
      const auto count = pb::StateLayout::rows_for(node.street, layout.flop_capacity,
                                                    layout.turn_capacity, layout.river_capacity);
      for (std::uint32_t row = 0; row < count; ++row) {
        const auto last = snapshot.value()->discount_last_iteration(node.id, row);
        if (last > 0U && last + 1U < snapshot.value()->iteration())
          ++pending;
      }
    }
    require(!lazy || pending > 0U, "the lazy snapshot is written with pending discounts");
    require(epoch == 65'535U || snapshot.value()->discount_epoch_base() > 0U,
            "the short-epoch snapshot is written after an epoch rebase");
    const std::string name =
        std::string(storage_name) + (lazy ? "_lazy" : "_eager") + "_" + std::to_string(epoch);
    const auto snapshot_path = directory / ("policy_snapshot_" + name + ".bin");
    const auto export_path = directory / ("policy_export_" + name + ".bin");
    const std::string source = snapshot.value()->identity() + "|iteration=" + std::to_string(k);
    const auto written = snapshot.value()->save_average_policy_snapshot(snapshot_path, source);
    require(written.has_value(), "policy snapshot writes");
    require(!std::filesystem::exists(snapshot_path.string() + ".tmp"),
            "policy snapshot leaves no temporary file");
    const auto saved = exported.value()->save_average_policy(export_path, source);
    require(saved.has_value(), "reference policy exports at the snapshot iteration");
    for (int iteration = 0; iteration < k; ++iteration) {
      require(snapshot.value()->iterate().has_value() && plain.value()->iterate().has_value(),
              "training continues after the policy snapshot");
    }
    require(snapshot.value()->state_fingerprint() == plain.value()->state_fingerprint(),
            "a policy snapshot does not perturb the training");

    const auto snapshot_info = pb::read_policy_info(snapshot_path);
    const auto export_info = pb::read_policy_info(export_path);
    require(snapshot_info.has_value() && export_info.has_value(), "both policy files read back");
    require(snapshot_info.value().source == source && export_info.value().source == source &&
                snapshot_info.value().tree_fingerprint == export_info.value().tree_fingerprint &&
                snapshot_info.value().flop_capacity == export_info.value().flop_capacity &&
                snapshot_info.value().turn_capacity == export_info.value().turn_capacity &&
                snapshot_info.value().river_capacity == export_info.value().river_capacity &&
                snapshot_info.value().entries == export_info.value().entries,
            "the snapshot carries the header of the exported policy");
    require(snapshot_info.value().policy_fingerprint == written.value(),
            "the snapshot returns the fingerprint of its table");
    const auto snapshot_policy = pb::load_policy(snapshot_path, game.value());
    const auto export_policy = pb::load_policy(export_path, game.value());
    require(snapshot_policy.has_value() && export_policy.has_value(), "both policies load");
    const auto &left = snapshot_policy.value()->table();
    const auto &right = export_policy.value()->table();
    require(left.size() == right.size(), "snapshot and export have the same entries");
    double largest = 0.0;
    std::uint64_t different = 0U;
    for (std::size_t entry = 0; entry < left.size(); ++entry) {
      largest = std::max(largest, std::abs(left[entry] - right[entry]));
      different += left[entry] != right[entry] ? 1U : 0U;
    }
    const bool identical_bytes = read_file(snapshot_path) == read_file(export_path);
    require(identical_bytes && written.value() == saved.value() && different == 0U,
            "the policy snapshot is the exported file byte for byte");
    std::cout << "policy snapshot (" << storage_name << ", " << (lazy ? "lazy" : "eager")
              << " discount" << (epoch == 65'535U ? "" : ", epoch " + std::to_string(epoch))
              << "): entries " << left.size() << ", pending rows " << pending
              << ", largest difference " << largest << ", entries not bit-identical " << different
              << ", identical file " << (identical_bytes ? "yes" : "no") << '\n';
  }
}

// The 16-bit lazy-discount timestamps live in epochs. A run shorter than the epoch never
// rebases and is bit-identical to unbounded timestamps; a rebase materializes every touched
// row to the target, so a rebase whose target is also the final materialization target
// leaves the state bit-identical (one ratio of prefix products per row in both trainers).
void test_lazy_discount_epoch(const Resources &resources) {
  const auto game = pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_reduced_v1.json"));
  require(game.has_value(), "lazy-epoch fixture compiles");
  auto config = resources.config();
  config.threads = 2U;
  config.batch_boards = 8U;
  config.batch_policy_refresh = true;
  config.scheme = pb::WeightingScheme::Dcfr;
  config.update_mode = pb::UpdateMode::Alternating;
  config.lazy_discount = true;
  auto epoch_config = config;
  epoch_config.lazy_discount_epoch = 4U;
  auto reference = pb::Trainer::create(game.value(), resources.view(), config);
  auto epochs = pb::Trainer::create(game.value(), resources.view(), epoch_config);
  require(reference.has_value() && epochs.has_value(), "lazy-epoch trainers create");
  // Iterations 1-5 have the discount targets 0-4: the epoch of 4 rebases at the start of
  // iteration 5 to target 4, which is also the target of the final materialization.
  for (int iteration = 0; iteration < 5; ++iteration) {
    require(reference.value()->iterate().has_value() && epochs.value()->iterate().has_value(),
            "lazy-epoch iterations succeed");
  }
  require(epochs.value()->discount_epoch_base() == 4U &&
              reference.value()->discount_epoch_base() == 0U,
          "lazy-epoch base moves to the rebase target");
  const auto coverage = epochs.value()->row_coverage();
  const auto reference_coverage = reference.value()->row_coverage();
  std::uint64_t touched = 0U;
  std::uint64_t total = 0U;
  for (std::size_t street = 0; street < 4U; ++street) {
    touched += coverage.rows_touched[street];
    total += coverage.rows_total[street];
    require(coverage.rows_touched[street] <= coverage.rows_total[street] &&
                coverage.regret_pages_touched[street] <= coverage.regret_pages_total[street],
            "lazy-epoch coverage is bounded by the layout");
  }
  require(touched > 0U && touched <= total, "lazy-epoch coverage counts touched rows");
  require(coverage.rows_touched == reference_coverage.rows_touched &&
              coverage.regret_pages_touched == reference_coverage.regret_pages_touched,
          "row coverage does not depend on the epoch");
  std::uint64_t pages_total = 0U;
  for (std::size_t street = 0; street < 4U; ++street)
    pages_total += coverage.regret_pages_total[street];
  require(pages_total == (epochs.value()->cell_count() * sizeof(double) + 4095U) / 4096U,
          "lazy-epoch page totals cover the regret table exactly");
  std::uint64_t decoded_maximum = 0U;
  for (const auto &node : game.value().nodes()) {
    if (node.kind != pb::NodeKind::Decision)
      continue;
    decoded_maximum = std::max(decoded_maximum, epochs.value()->discount_last_iteration(node.id, 0U));
  }
  // The preflop rows are active in every pass, so their decoded timestamp is the target.
  require(decoded_maximum == 4U, "lazy-epoch decoded timestamps reach the target");
  require(epochs.value()->state_fingerprint() == reference.value()->state_fingerprint(),
          "a rebase at the final target is bit-identical to unbounded timestamps");
  std::cout << "lazy-epoch: rows touched " << touched << " of " << total << '\n';
  // Further rebases (targets 8, 12, 16) change the association of the discount
  // products, so the states may differ at rounding level: reported, not asserted.
  for (int iteration = 0; iteration < 12; ++iteration) {
    require(reference.value()->iterate().has_value() && epochs.value()->iterate().has_value(),
            "lazy-epoch iterations succeed after a rebase");
  }
  require(epochs.value()->discount_epoch_base() >= 12U, "lazy-epoch keeps rebasing");
  const auto reference_regrets = reference.value()->regrets();
  const auto epoch_regrets = epochs.value()->regrets();
  require(reference_regrets.size() == epoch_regrets.size(), "lazy-epoch layouts match");
  double maximum_difference = 0.0;
  for (std::size_t cell = 0; cell < reference_regrets.size(); ++cell) {
    maximum_difference = std::max(maximum_difference,
                                  std::abs(reference_regrets[cell] - epoch_regrets[cell]));
  }
  std::cout << "lazy-epoch: max regret difference after 17 iterations with epoch 4: "
            << maximum_difference << '\n';
  // A resumed run keeps the rebase iterations of the continuous run: with epoch 4, save
  // after 6 iterations (target 5, base 4); both trainers then cross the rebase at target 8.
  auto continuous = pb::Trainer::create(game.value(), resources.view(), epoch_config);
  require(continuous.has_value(), "lazy-epoch continuous trainer creates");
  for (int iteration = 0; iteration < 6; ++iteration) {
    require(continuous.value()->iterate().has_value(),
            "lazy-epoch iterations before the save succeed");
  }
  const auto directory = std::filesystem::temp_directory_path() / "gtosd_preflop_blueprint_tests";
  std::filesystem::create_directories(directory);
  const auto path = directory / "lazy_epoch_checkpoint.bin";
  require(continuous.value()->save_checkpoint(path).has_value(), "lazy-epoch checkpoint saves");
  auto resumed = pb::Trainer::create(game.value(), resources.view(), epoch_config);
  require(resumed.has_value() && resumed.value()->load_checkpoint(path).has_value(),
          "lazy-epoch checkpoint loads");
  require(resumed.value()->discount_epoch_base() == 4U &&
              continuous.value()->discount_epoch_base() == 4U,
          "lazy-epoch resume restores the epoch base of the continuous run");
  for (int iteration = 0; iteration < 4; ++iteration) {
    require(continuous.value()->iterate().has_value() && resumed.value()->iterate().has_value(),
            "lazy-epoch iterations after the resume succeed");
  }
  require(continuous.value()->discount_epoch_base() == 8U &&
              resumed.value()->discount_epoch_base() == 8U,
          "lazy-epoch both trainers rebased at target 8");
  require(resumed.value()->state_fingerprint() == continuous.value()->state_fingerprint(),
          "lazy-epoch resume across a rebase is bit-identical to the continuous run");
  // Epoch 1: a rebase at every iteration.
  auto every_config = config;
  every_config.lazy_discount_epoch = 1U;
  auto every = pb::Trainer::create(game.value(), resources.view(), every_config);
  require(every.has_value(), "lazy-epoch trainer with epoch 1 creates");
  for (int iteration = 0; iteration < 6; ++iteration) {
    require(every.value()->iterate().has_value(), "lazy-epoch iterations with epoch 1 succeed");
  }
  require(every.value()->discount_epoch_base() == 5U,
          "lazy-epoch base with epoch 1 follows the target");
  auto invalid = config;
  invalid.lazy_discount_epoch = 0U;
  require(!pb::Trainer::create(game.value(), resources.view(), invalid).has_value(),
          "lazy-epoch rejects an epoch of 0");
  invalid.lazy_discount_epoch = 65'536U;
  require(!pb::Trainer::create(game.value(), resources.view(), invalid).has_value(),
          "lazy-epoch rejects an epoch above 65535");
}

// Street-restricted physical best response (deviation_from) against the same
// lossless game: the responder's information sets on streets before the first
// deviating one pass to the other player with their lifted strategy (they stay
// decisions, so exact zeros need no chance-node rewrite), and the exact best
// response over the remaining ones is the restricted response.
void check_street_restrictions(const pb::CompiledGame &game, const pb::BucketPolicy &average,
                               const gtosd::FiniteGame &physical,
                               const gtosd::StrategyProfile &profile,
                               const pb::BestResponseResources &response_resources,
                               const std::vector<pb::FlopGroup> &groups,
                               const pb::BestResponseOptions &base_options) {
  const auto street_of = [&](const std::string &key) {
    const auto begin = key.find("|n") + 2;
    const auto end = key.find('|', begin);
    const auto compiled_node =
        static_cast<std::uint32_t>(std::stoul(key.substr(begin, end - begin)));
    return game.nodes()[compiled_node].street;
  };
  for (const auto deviation_from :
       {pb::DeviationStreet::Preflop, pb::DeviationStreet::Flop, pb::DeviationStreet::Turn,
        pb::DeviationStreet::River, pb::DeviationStreet::None}) {
    auto options = base_options;
    options.deviation_from = deviation_from;
    const auto report =
        pb::evaluate_best_response(game, average, response_resources, groups, options);
    require(report.has_value(), "street-restricted physical best response evaluates");
    for (std::uint8_t player = 0; player < 2U; ++player) {
      const auto other = static_cast<std::uint8_t>(1U - player);
      auto restricted_game = physical;
      auto restricted_profile = profile;
      for (auto &node : restricted_game.nodes) {
        if (node.kind == gtosd::GameNodeKind::Decision && node.player == player &&
            static_cast<unsigned>(street_of(node.information_set)) <
                static_cast<unsigned>(deviation_from)) {
          node.player = other;
          restricted_profile.at(node.information_set).player = other;
        }
      }
      const auto oracle = gtosd::exact_best_response(restricted_game, restricted_profile, player);
      require(oracle.has_value(), "street-restricted lossless best response computes");
      require(close(report.value().best_response[player], oracle.value().value, 1e-9),
              "street-restricted physical best response equals the lossless FiniteGame within "
              "1e-9");
    }
  }
}

// The physical best response of the lifted strategy: the responder decides
// per hand and per public prefix, never per full board. On the reduced game
// the exact value is the best response of the lossless FiniteGame (information
// sets by combo and cards dealt so far) against the lifted strategy profile.
void test_physical_best_response(const Resources &resources, const int stack = 0,
                                 const bool overlapping_ranges = false, const bool rake = false) {
  const auto started = Clock::now();
  auto fixture = load_fixture(stack != 0 ? "preflop_blueprint_co40_test_v1.json"
                              : rake     ? "preflop_blueprint_hu10_reduced_rake_v1.json"
                                         : "preflop_blueprint_hu10_reduced_v1.json");
  if (stack != 0) {
    fixture.effective_stack =
        gtosd::Money::from_units(static_cast<std::int64_t>(stack) * gtosd::Money::units_per_ante)
            .value();
  }
  const auto game = pb::CompiledGame::compile(fixture);
  require(game.has_value(), "HU10 reduced compiles");
  const auto boards = stack == 0 ? oracle_boards() : branching_boards();
  auto subsets = stack == 0 ? oracle_subsets() : deep_subsets();
  if (overlapping_ranges) {
    subsets.combos[0] = combos_from_cards({"Ts", "Th", "Js", "Jh"});
    subsets.combos[1] = subsets.combos[0];
  }
  auto config = resources.config();
  config.threads = 2U;
  auto trainer = pb::Trainer::create(game.value(), resources.view(), config, &boards, &subsets);
  require(trainer.has_value(), "exact-mode trainer creates");
  for (int iteration = 0; iteration < 10; ++iteration) {
    require(trainer.value()->iterate().has_value(), "iteration succeeds");
  }
  const auto average = trainer.value()->average_policy();

  FiniteGameBuilder lossless(game.value(), resources, true, &average);
  const auto physical = lossless.build(boards, subsets);
  const auto summary = gtosd::validate_finite_game(physical);
  require(summary.has_value(), "lossless finite game validates");
  require(gtosd::validate_strategy_profile(physical, lossless.profile()).has_value(),
          "lifted strategy profile is valid on the lossless game");
  const auto oracle = gtosd::calculate_nash_conv(physical, lossless.profile());
  require(oracle.has_value(), "lossless NashConv computes");

  std::vector<pb::WeightedBoard> weighted;
  for (std::size_t index = 0; index < boards.histories.size(); ++index) {
    weighted.push_back({boards.histories[index], boards.weights[index]});
  }
  pb::BestResponseResources response_resources;
  response_resources.ranks = &resources.ranks.value();
  response_resources.all_in = &resources.all_in.value();
  response_resources.catalog = &resources.catalog.value();
  response_resources.flop = &resources.flop.value();
  response_resources.turn = &resources.turn.value();
  response_resources.river = &resources.river.value();
  pb::BestResponseOptions options;
  options.threads = 2U;
  options.hand_subsets = subsets.combos;
  const auto report = pb::evaluate_best_response(game.value(), average, response_resources,
                                                 pb::group_by_flop(weighted), options);
  require(report.has_value(), "physical best response evaluates");
  for (std::uint8_t player = 0; player < 2U; ++player) {
    require(close(report.value().ev[player], oracle.value().profile_value[player], 1e-9),
            "profile value equals the lossless FiniteGame within 1e-9");
    require(close(report.value().best_response[player], oracle.value().best_response_value[player],
                  1e-9),
            "physical best response equals the lossless FiniteGame best response within 1e-9");
    require(report.value().gain[player] >= -1e-9, "best response never loses to the average");
    const auto response = gtosd::exact_best_response(physical, lossless.profile(), player);
    require(response.has_value(), "independent full physical response policy exists");
    auto route_profile = lossless.profile();
    for (const auto &[key, action] : response.value().policy) {
      const auto begin = key.find("|n") + 2;
      const auto end = key.find('|', begin);
      const auto public_node =
          static_cast<std::uint32_t>(std::stoul(key.substr(begin, end - begin)));
      if (game.value().nodes()[public_node].street != gtosd::Street::Preflop) {
        continue;
      }
      auto &strategy = route_profile.at(key);
      for (std::size_t a = 0; a < strategy.actions.size(); ++a) {
        strategy.probabilities[a] = strategy.actions[a] == action ? 1.0 : 0.0;
      }
    }
    const auto route_ev = gtosd::evaluate_strategy_profile(physical, route_profile);
    require(route_ev.has_value() &&
                close(route_ev.value()[player],
                      report.value().best_response_route_average_value[player], 1e-9),
            "full-BR preflop with average postflop matches independently lifted route policy");
    double route_gain = 0.0;
    for (const auto &route : report.value().postflop_entry_route) {
      if (route.hero == player) {
        require(route.average_probability >= 0 && route.average_probability <= 1 + 1e-12 &&
                    route.response_probability >= 0 && route.response_probability <= 1 + 1e-12,
                "entry route probabilities stay in the unit interval");
        route_gain += route.postflop_gain_on_response_route;
      }
    }
    require(
        close(route_gain, report.value().best_response[player] - route_ev.value()[player], 1e-9),
        "entry postflop gains sum to the whole continuation change on the same BR route");
  }
  require(close(report.value().nashconv, oracle.value().nash_conv, 1e-9),
          "nashconv equals the lossless FiniteGame");
  if (rake) {
    // General-sum: the oracle's EVs sum to minus the expected rake, and the
    // rake view prices the same expectation for each hero.
    require(oracle.value().expected_payoff_sum < -1e-6 &&
                close(report.value().ev[0] + report.value().ev[1],
                      oracle.value().expected_payoff_sum, 1e-9) &&
                std::isnan(oracle.value().zero_sum_exploitability),
            "with rake the EVs sum to minus the expected rake, not to zero");
    const auto view = game.value().rake_view();
    require(view.fingerprint() == game.value().fingerprint() &&
                view.nodes().size() == game.value().nodes().size(),
            "the rake view keeps the tree and its fingerprint");
    const auto view_report = pb::evaluate_best_response(view, average, response_resources,
                                                        pb::group_by_flop(weighted), options);
    require(view_report.has_value(), "the rake view evaluates");
    for (std::uint8_t player = 0; player < 2U; ++player) {
      require(close(view_report.value().ev[player], oracle.value().expected_payoff_sum, 1e-9),
              "the rake view EV of each hero is minus the expected rake");
    }
    std::cout << "rake: expected payoff sum " << oracle.value().expected_payoff_sum
              << ", rake view EV [" << view_report.value().ev[0] << ", "
              << view_report.value().ev[1] << "]\n";
  }
  if (stack == 0 && !overlapping_ranges) {
    check_street_restrictions(game.value(), average, physical, lossless.profile(),
                              response_resources, pb::group_by_flop(weighted), options);
  }

  // The trainer reports the same exact evaluation on its board list.
  const auto estimate = trainer.value()->estimate_exploitability(0U);
  require(estimate.has_value() && estimate.value().exact &&
              estimate.value().boards == boards.histories.size() &&
              estimate.value().flops == (stack == 0 ? 3U : 2U),
          "trainer evaluates the listed boards exactly");
  require(close(estimate.value().max_gain, report.value().max_gain, 1e-12) &&
              close(estimate.value().ev[0], report.value().ev[0], 1e-12),
          "trainer estimate equals the evaluator report");

  // A physical BR dominates every representable response, but the greedy
  // finite-game API certifies the constrained optimum only with perfect recall.
  FiniteGameBuilder bucketed(game.value(), resources, false, &average);
  const auto abstract = bucketed.build(boards, subsets);
  const auto abstract_oracle = gtosd::calculate_nash_conv(abstract, bucketed.profile());
  const auto recall0 = gtosd::has_perfect_recall(abstract, 0);
  const auto recall1 = gtosd::has_perfect_recall(abstract, 1);
  require(recall0.has_value() && recall1.has_value(), "both recall checks compute");
  if (recall0.value() && recall1.value()) {
    require(abstract_oracle.has_value(), "perfect-recall abstract NashConv computes");
    require(report.value().nashconv >= abstract_oracle.value().nash_conv - 1e-9,
            "physical best response dominates the perfect-recall abstract response");
  } else {
    require(!abstract_oracle &&
                abstract_oracle.error() == gtosd::SolverError::UnsupportedInformationStructure,
            "imperfect-recall game is not silently certified by greedy BR");
  }
  const auto abstract_value = gtosd::evaluate_strategy_profile(abstract, bucketed.profile());
  require(abstract_value.has_value() &&
              close(abstract_value.value()[0], report.value().ev[0], 1e-9) &&
              close(abstract_value.value()[1], report.value().ev[1], 1e-9),
          "physical lifting preserves both bucket profile values");
  std::cout << "physical best response (overlapping ranges=" << overlapping_ranges
            << ", rake=" << rake << "): EV ["
            << report.value().ev[0] << ", " << report.value().ev[1] << "], BR ["
            << report.value().best_response[0] << ", " << report.value().best_response[1]
            << "], nashconv " << report.value().nashconv << " (lossless oracle "
            << oracle.value().nash_conv << ", abstract oracle "
            << (abstract_oracle ? std::to_string(abstract_oracle.value().nash_conv)
                                : "unsupported_imperfect_recall")
            << "), lossless game " << summary.value().nodes << " nodes, "
            << summary.value().information_sets << " information sets, "
            << std::chrono::duration<double>(Clock::now() - started).count() << " s\n";
}

void test_exploitability_decreases(const Resources &resources) {
  const auto game = pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_reduced_v1.json"));
  require(game.has_value(), "HU10 reduced compiles");
  auto config = resources.config();
  config.threads = 4U;
  config.batch_boards = 16U;
  auto trainer = pb::Trainer::create(game.value(), resources.view(), config);
  require(trainer.has_value(), "trainer creates");
  std::vector<double> curve;
  double training_seconds = 0.0;
  for (int checkpoint = 0; checkpoint <= 3; ++checkpoint) {
    const auto estimate = trainer.value()->estimate_exploitability(4U);
    require(estimate.has_value(), "estimate succeeds");
    require(estimate.value().flops == 4U && estimate.value().boards == 4U * 33U * 32U,
            "sampled evaluation enumerates every runout of the sampled flops");
    curve.push_back(estimate.value().max_gain);
    std::cout << "curve: iteration " << trainer.value()->iteration() << " max gain "
              << estimate.value().max_gain << " +- " << estimate.value().max_gain_half_width
              << " (cross-fit " << estimate.value().max_gain_lower << ")"
              << " nashconv " << estimate.value().nashconv << " EV [" << estimate.value().ev[0]
              << ", " << estimate.value().ev[1] << "] (training " << training_seconds
              << " s, evaluation " << estimate.value().seconds << " s)\n";
    if (checkpoint == 3) {
      break;
    }
    for (int iteration = 0; iteration < 20; ++iteration) {
      const auto telemetry = trainer.value()->iterate();
      require(telemetry.has_value(), "iteration succeeds");
      training_seconds += telemetry.value().seconds;
    }
  }
  require(curve.back() < curve.front(), "exploitability estimate decreases with training");
  require(curve.front() > 0.1, "the uniform strategy is clearly exploitable");
}

void test_configurable_stop_rule() {
  pb::ExploitabilityEstimate estimate;
  estimate.max_gain = 0.029;
  require(estimate.meets_stop_rule(3.0), "default stop target is one per cent of pot");
  estimate.max_gain = 0.031;
  require(!estimate.meets_stop_rule(3.0), "default stop target rejects gain above one per cent");
  require(estimate.meets_stop_rule(3.0, 2.0), "explicit pot percentage changes stop target");
  estimate.max_gain = 0.025;
  estimate.max_gain_half_width = 0.006;
  require(!estimate.meets_stop_rule(3.0), "estimated stop includes confidence half-width");
}

void test_batch_policy_refresh(const Resources &resources) {
  // The compact per-batch policy replaces the dense snapshot: the trajectory
  // must not depend on the thread count, and the streamed exports must equal
  // the dense exports byte for byte without perturbing the training state.
  const auto game = pb::CompiledGame::compile(load_fixture("preflop_blueprint_co40_test_v1.json"));
  require(game.has_value(), "batch refresh fixture compiles");
  const auto directory = std::filesystem::temp_directory_path() / "gtosd_preflop_blueprint_export";
  std::filesystem::create_directories(directory);
  for (const auto scheme : {pb::WeightingScheme::Linear, pb::WeightingScheme::Dcfr})
    for (const auto update : {pb::UpdateMode::Simultaneous, pb::UpdateMode::Alternating}) {
      auto config = resources.config();
      config.scheme = scheme;
      config.update_mode = update;
      config.batch_boards = 8;
      config.threads = 1;
      auto single = pb::Trainer::create(game.value(), resources.view(), config);
      config.threads = 4;
      auto parallel = pb::Trainer::create(game.value(), resources.view(), config);
      require(single.has_value() && parallel.has_value(), "single and parallel trainers create");
      for (int iteration = 0; iteration < 8; ++iteration) {
        require(single.value()->iterate().has_value() && parallel.value()->iterate().has_value(),
                "both trainers iterate");
        require(single.value()->regrets() == parallel.value()->regrets() &&
                    single.value()->strategy_sums() == parallel.value()->strategy_sums() &&
                    single.value()->state_fingerprint() == parallel.value()->state_fingerprint(),
                "compact per-batch policy is bit-identical for both algorithms and update modes");
        if (iteration == 3) {
          const auto state = parallel.value()->state_fingerprint();
          {
            const auto expected = parallel.value()->average_policy();
            const auto path = directory / "streamed_average.bin";
            const auto streamed = parallel.value()->save_average_policy(path, "test");
            require(streamed.has_value(), "streamed average writes");
            require(streamed.value() == pb::policy_fingerprint(expected),
                    "streamed average fingerprint equals the dense export");
            const auto dense_path = directory / "dense_average.bin";
            require(pb::save_policy(dense_path, game.value(), expected, "test").has_value(),
                    "dense average writes");
            require(read_file(path) == read_file(dense_path),
                    "streamed average file equals the dense file byte for byte");
            const auto loaded = pb::load_policy(path, game.value());
            require(loaded.has_value() && loaded.value()->table() == expected.table(),
                    "streamed average loads back to the dense table");
          }
          {
            const auto expected = parallel.value()->current_policy();
            const auto path = directory / "streamed_current.bin";
            const auto streamed = parallel.value()->save_current_policy(path, "test");
            require(streamed.has_value() &&
                        streamed.value() == pb::policy_fingerprint(expected),
                    "streamed current equals the dense export");
          }
          require(parallel.value()->state_fingerprint() == state,
                  "export does not perturb training state");
        }
      }
    }
  auto rejected_config = resources.config();
  rejected_config.reuse_discount_invariant_policy = true;
  rejected_config.lazy_discount = true;
  rejected_config.scheme = pb::WeightingScheme::Dcfr;
  rejected_config.update_mode = pb::UpdateMode::Alternating;
  require(!pb::Trainer::create(game.value(), resources.view(), rejected_config).has_value(),
          "the removed reuse option is rejected");
}

void test_table_storage(const Resources &resources) {
  // Narrow storage: same layout, same file formats, bounded storage error.
  const auto game = pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_reduced_v1.json"));
  require(game.has_value(), "storage fixture compiles");
  auto config = resources.config();
  config.scheme = pb::WeightingScheme::Dcfr;
  config.update_mode = pb::UpdateMode::Alternating;
  config.lazy_discount = true;
  config.batch_boards = 8U;
  config.threads = 4U;
  auto reference = pb::Trainer::create(game.value(), resources.view(), config);
  require(reference.has_value(), "double trainer creates");
  for (const auto storage : {pb::TableStorage::MixedFloatSums, pb::TableStorage::Float32}) {
    auto narrow_config = config;
    narrow_config.storage = storage;
    auto narrow = pb::Trainer::create(game.value(), resources.view(), narrow_config);
    require(narrow.has_value(), "narrow trainer creates");
    require(narrow.value()->identity() != reference.value()->identity(),
            "narrow storage has its own identity");
    auto reference_run = pb::Trainer::create(game.value(), resources.view(), config);
    require(reference_run.has_value(), "double reference trainer is created for the storage test");
    for (int iteration = 0; iteration < 12; ++iteration) {
      require(reference_run.value()->iterate().has_value() && narrow.value()->iterate().has_value(),
              "double and narrow trainers iterate");
      // Divergence per iteration: storage rounding alone stays near 1e-7 relative; a growing
      // gap shows the regret-matching dynamics amplifying the rounding (not a storage bug).
      const auto step_expected = reference_run.value()->regrets();
      const auto step_narrow = narrow.value()->regrets();
      double step_scale = 0.0;
      for (const auto value : step_expected)
        step_scale = std::max(step_scale, std::abs(value));
      double step_worst = 0.0;
      std::size_t step_over = 0;
      for (std::size_t cell = 0; cell < step_expected.size(); ++cell) {
        const double absolute = std::abs(step_narrow[cell] - step_expected[cell]);
        const double relative = absolute / std::max(std::abs(step_expected[cell]), 1e-6 * step_scale);
        step_worst = std::max(step_worst, relative);
        if (relative > 1e-2)
          ++step_over;
      }
      std::cout << pb::table_storage_name(storage) << " iteration " << (iteration + 1)
                << ": max relative regret error " << step_worst << ", cells over 1e-2: "
                << step_over << ", scale " << step_scale << "\n";
      if (iteration == 0) {
        // Pre-registered per-cell criterion, applied where it measures storage alone: after
        // one iteration both trainers accumulate the same increments from the same uniform
        // policy, so any difference is float32 rounding (cancellation included).
        require(step_worst < 1e-2, "narrow regret storage stays within 1 % after one iteration");
        require(step_over == 0, "no regret cell deviates by more than 1 % after one iteration");
      }
    }
    const auto expected_regrets = reference_run.value()->regrets();
    const auto expected_sums = reference_run.value()->strategy_sums();
    const auto regrets = narrow.value()->regrets();
    const auto sums = narrow.value()->strategy_sums();
    require(regrets.size() == expected_regrets.size() && sums.size() == expected_sums.size(),
            "narrow layout matches the double layout");
    double maximum_relative = 0.0;
    double table_scale = 0.0;
    for (std::size_t cell = 0; cell < regrets.size(); ++cell)
      table_scale = std::max({table_scale, std::abs(expected_regrets[cell]),
                              std::abs(expected_sums[cell])});
    std::size_t worst_cell = 0;
    double worst_absolute = 0.0;
    std::size_t cells_over_1e_2 = 0;
    double maximum_relative_scaled = 0.0;  // floor at 1e-6 of the table scale
    for (std::size_t cell = 0; cell < regrets.size(); ++cell) {
      const double scale = std::max({1e-9, std::abs(expected_regrets[cell]), std::abs(expected_sums[cell])});
      const double absolute = std::max(std::abs(regrets[cell] - expected_regrets[cell]),
                                       std::abs(sums[cell] - expected_sums[cell]));
      const double relative = absolute / scale;
      if (relative > 1e-2)
        ++cells_over_1e_2;
      if (absolute > worst_absolute) {
        worst_absolute = absolute;
        worst_cell = cell;
      }
      maximum_relative = std::max(maximum_relative, relative);
      maximum_relative_scaled =
          std::max(maximum_relative_scaled, absolute / std::max(scale, 1e-6 * table_scale));
    }
    std::cout << pb::table_storage_name(storage) << " storage: max relative cell difference "
              << maximum_relative << " after 12 iterations; table scale " << table_scale
              << "; max absolute difference " << worst_absolute << " at cell " << worst_cell
              << " (double regret " << expected_regrets[worst_cell] << " sum "
              << expected_sums[worst_cell] << "; narrow regret " << regrets[worst_cell] << " sum "
              << sums[worst_cell] << "); cells with relative error > 1e-2: " << cells_over_1e_2
              << " of " << regrets.size() << "; max relative error with floor 1e-6 x scale: "
              << maximum_relative_scaled << "\n";
    if (storage == pb::TableStorage::MixedFloatSums) {
      require(maximum_relative < 1e-2, "mixed storage stays within 1 % of the double tables");
    } else {
      require(std::isfinite(maximum_relative) && std::isfinite(worst_absolute),
              "float32 storage tables stay finite after 12 iterations");
      std::cout << "float32 storage: trajectory divergence after 12 iterations is reported, not "
                   "asserted (regret matching amplifies rounding); quality is judged by the suite\n";
    }
    const auto directory = std::filesystem::temp_directory_path() / "gtosd_preflop_blueprint_storage";
    std::filesystem::create_directories(directory);
    const auto path = directory / (std::string(pb::table_storage_name(storage)) + "_ckpt.bin");
    require(narrow.value()->save_checkpoint(path).has_value(), "narrow checkpoint saves");
    auto resumed = pb::Trainer::create(game.value(), resources.view(), narrow_config);
    require(resumed.has_value() && resumed.value()->load_checkpoint(path).has_value(),
            "narrow checkpoint loads");
    require(resumed.value()->state_fingerprint() == narrow.value()->state_fingerprint(),
            "narrow checkpoint round trip is bit-identical");
    require(!reference_run.value()->load_checkpoint(path).has_value(),
            "a double trainer rejects a narrow checkpoint");
    const auto policy_path = directory / (std::string(pb::table_storage_name(storage)) + "_policy.bin");
    const auto saved = narrow.value()->save_average_policy(policy_path, "test");
    require(saved.has_value(), "narrow average exports");
    const auto loaded = pb::load_policy(policy_path, game.value());
    require(loaded.has_value() && loaded.value()->table() == narrow.value()->average_policy().table(),
            "narrow export loads back as the dense double policy");
  }
}

} // namespace

int main(const int argc, char **argv) {
  try {
    std::filesystem::path resources_dir;
    std::filesystem::path buckets_dir;
    bool deep_only = false;
    bool lazy_only = false;
    for (int index = 1; index < argc; ++index) {
      const std::string_view name = argv[index];
      if (name == "--lazy-only") {
        lazy_only = true;
        continue;
      }
      if (index + 1 >= argc)
        throw std::runtime_error("missing value for " + std::string{name});
      const std::string_view value = argv[++index];
      if (name == "--resources-dir") {
        resources_dir = value;
      } else if (name == "--buckets-dir") {
        buckets_dir = value;
      } else if (name == "--deep-only") {
        deep_only = value == "true";
      } else {
        throw std::runtime_error("unknown argument " + std::string{name});
      }
    }
    const auto resources = load_resources(resources_dir, buckets_dir);
    if (lazy_only) {
      test_lazy_dcfr_discount(resources);
      test_lazy_discount_epoch(resources);
      test_average_strategy_row_snapshot(resources);
      test_average_policy_snapshot(resources);
      return 0;
    }
    test_configurable_stop_rule();
    test_batch_policy_refresh(resources);
    test_table_storage(resources);
    test_forgotten_information_witness();
    test_diagnostic_contracts(resources);
    test_finite_game_oracle(resources, 40, true);
    std::cout << "resources " << (resources.loaded ? "loaded" : "built") << ", bucket tables "
              << (resources.buckets_loaded ? "loaded" : "built") << " ("
              << resources.flop->capacity() << "/" << resources.turn->capacity() << "/"
              << resources.river->capacity() << ")\n";
    for (const int stack : {40, 100, 300}) {
      test_alternating_conditional_expectation(resources, stack);
      test_finite_game_oracle(resources, stack);
      test_physical_best_response(resources, stack);
      test_physical_best_response(resources, stack, true);
    }
    if (deep_only) {
      return 0;
    }
    test_finite_game_oracle(resources);
    test_determinism(resources);
    test_resume(resources);
    test_lazy_dcfr_discount(resources);
    test_lazy_discount_epoch(resources);
    test_average_strategy_row_snapshot(resources);
    test_average_policy_snapshot(resources);
    test_physical_best_response(resources);
    test_finite_game_oracle(resources, 0, false, true);
    test_physical_best_response(resources, 0, false, true);
    // The alternating sampled update with rake: at 40a the largest pot (80a)
    // is raked exactly the 2a cap, at 100a the cap binds (up to 200a).
    for (const int stack : {40, 100}) {
      test_alternating_conditional_expectation(resources, stack, true);
    }
    test_exploitability_decreases(resources);
    std::cout << "PREFLOP_BLUEPRINT_TRAINER_TESTS=PASS assertions=" << assertions << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "PREFLOP_BLUEPRINT_TRAINER_TESTS=FAIL " << error.what() << " after " << assertions
              << " assertions\n";
    return 1;
  }
}
