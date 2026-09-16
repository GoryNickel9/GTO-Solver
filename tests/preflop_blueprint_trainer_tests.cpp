#include "preflop_blueprint_test_support.hpp"

#include <algorithm>
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

void test_finite_game_oracle(const Resources &resources) {
  const auto started = Clock::now();
  const auto game = pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_reduced_v1.json"));
  require(game.has_value(), "HU10 reduced compiles");
  const auto boards = oracle_boards();
  const auto subsets = oracle_subsets();
  constexpr std::uint64_t iterations = 25U;

  FiniteGameBuilder builder(game.value(), resources);
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
  auto trainer = pb::Trainer::create(game.value(), resources.view(), config, &boards, &subsets);
  require(trainer.has_value(),
          std::string("trainer creates: ") +
              (trainer ? "" : pb::trainer_error_name(trainer.error())));
  for (std::uint64_t iteration = 0; iteration < iterations; ++iteration) {
    require(trainer.value()->iterate().has_value(), "exact-mode iteration succeeds");
  }
  const auto &layout = trainer.value()->layout();
  const auto &regrets = trainer.value()->regrets();
  const auto &sums = trainer.value()->strategy_sums();
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
  const auto nash_conv =
      gtosd::calculate_nash_conv(finite, solved.value().average_strategy);
  require(nash_conv.has_value(), "FiniteGame NashConv computes");
  // The profile values coincide. The trainer's best response is the physical
  // one (per hand on every board) and therefore dominates the FiniteGame
  // best response, which is constrained to one action per bucket.
  require(close(exact.value().ev[0], nash_conv.value().profile_value[0], 1e-9) &&
              close(exact.value().ev[1], nash_conv.value().profile_value[1], 1e-9),
          "exact profile values equal calculate_nash_conv within 1e-9");
  require(exact.value().best_response[0] >= nash_conv.value().best_response_value[0] - 1e-9 &&
              exact.value().best_response[1] >= nash_conv.value().best_response_value[1] - 1e-9 &&
              exact.value().nashconv >= nash_conv.value().nash_conv - 1e-9,
          "physical best response dominates the abstract-game best response");
  std::cout << "oracle: finite game " << summary.value().nodes << " nodes, "
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
    for (std::size_t cell = 0; cell < reference_regrets.size(); ++cell) {
      if (plain.value()->regrets()[cell] != reference_regrets[cell] ||
          plain.value()->strategy_sums()[cell] != reference_sums[cell]) {
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
  for (std::size_t cell = 0; cell < reference_regrets.size(); ++cell) {
    const auto regret = trainer.value()->regrets()[cell];
    const auto sum = trainer.value()->strategy_sums()[cell];
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

// The physical best response of the lifted strategy: the responder decides
// per hand and per public prefix, never per full board. On the reduced game
// the exact value is the best response of the lossless FiniteGame (information
// sets by combo and cards dealt so far) against the lifted strategy profile.
void test_physical_best_response(const Resources &resources) {
  const auto started = Clock::now();
  const auto game = pb::CompiledGame::compile(load_fixture("preflop_blueprint_hu10_reduced_v1.json"));
  require(game.has_value(), "HU10 reduced compiles");
  const auto boards = oracle_boards();
  const auto subsets = oracle_subsets();
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
  }
  require(close(report.value().nashconv, oracle.value().nash_conv, 1e-9),
          "nashconv equals the lossless FiniteGame");

  // The trainer reports the same exact evaluation on its board list.
  const auto estimate = trainer.value()->estimate_exploitability(0U);
  require(estimate.has_value() && estimate.value().exact && estimate.value().boards == 3U &&
              estimate.value().flops == 3U,
          "trainer evaluates the listed boards exactly");
  require(close(estimate.value().max_gain, report.value().max_gain, 1e-12) &&
              close(estimate.value().ev[0], report.value().ev[0], 1e-12),
          "trainer estimate equals the evaluator report");

  // The bucket-constrained best response of the abstract game is dominated.
  FiniteGameBuilder bucketed(game.value(), resources, false, &average);
  const auto abstract = bucketed.build(boards, subsets);
  const auto abstract_oracle = gtosd::calculate_nash_conv(abstract, bucketed.profile());
  require(abstract_oracle.has_value(), "abstract NashConv computes");
  require(report.value().nashconv >= abstract_oracle.value().nash_conv - 1e-9,
          "physical best response dominates the abstract-game best response");
  std::cout << "physical best response: EV [" << report.value().ev[0] << ", "
            << report.value().ev[1] << "], BR [" << report.value().best_response[0] << ", "
            << report.value().best_response[1] << "], nashconv " << report.value().nashconv
            << " (lossless oracle " << oracle.value().nash_conv << ", abstract oracle "
            << abstract_oracle.value().nash_conv << "), lossless game " << summary.value().nodes
            << " nodes, " << summary.value().information_sets << " information sets, "
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

} // namespace

int main(const int argc, char **argv) {
  try {
    std::filesystem::path resources_dir;
    std::filesystem::path buckets_dir;
    for (int index = 1; index + 1 < argc; index += 2) {
      const std::string_view name = argv[index];
      if (name == "--resources-dir") {
        resources_dir = argv[index + 1];
      } else if (name == "--buckets-dir") {
        buckets_dir = argv[index + 1];
      } else {
        throw std::runtime_error("unknown argument " + std::string{name});
      }
    }
    const auto resources = load_resources(resources_dir, buckets_dir);
    std::cout << "resources " << (resources.loaded ? "loaded" : "built") << ", bucket tables "
              << (resources.buckets_loaded ? "loaded" : "built") << " ("
              << resources.flop->capacity() << "/" << resources.turn->capacity() << "/"
              << resources.river->capacity() << ")\n";
    test_finite_game_oracle(resources);
    test_determinism(resources);
    test_resume(resources);
    test_physical_best_response(resources);
    test_exploitability_decreases(resources);
    std::cout << "PREFLOP_BLUEPRINT_TRAINER_TESTS=PASS assertions=" << assertions << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "PREFLOP_BLUEPRINT_TRAINER_TESTS=FAIL " << error.what() << " after " << assertions
              << " assertions\n";
    return 1;
  }
}
