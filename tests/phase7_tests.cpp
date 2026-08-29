#include "gtosd/memory/memory.hpp"
#include "gtosd/postflop/postflop_solver.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifndef GTOSD_SOURCE_DIR
#define GTOSD_SOURCE_DIR "."
#endif

namespace {

std::uint64_t assertions = 0;

void require(const bool condition, const std::string_view message) {
  ++assertions;
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

std::uint16_t signed_code(const std::int16_t value) {
  return static_cast<std::uint16_t>(value);
}

void require_near(const double actual, const double expected, const double tolerance,
                  const std::string_view message) {
  require(std::abs(actual - expected) <= tolerance, message);
}

void test_signed_current_regret_matching() {
  const auto check_scalar = [](const std::initializer_list<std::int16_t> signed_codes,
                               const std::initializer_list<double> expected,
                               const std::string_view message) {
    std::vector<std::uint16_t> raw;
    raw.reserve(signed_codes.size());
    for (const auto code : signed_codes) {
      raw.push_back(signed_code(code));
    }
    std::vector<double> strategy(raw.size(), 0.0);
    require(gtosd::detail::regret_match_signed_codes(raw, strategy), message);
    std::size_t action = 0U;
    for (const double probability : expected) {
      require_near(strategy[action++], probability, 1.0e-12, message);
    }
  };

  check_scalar({-10, 5}, {0.0, 1.0}, "signed two-action negative regret clamps to zero");
  check_scalar({-10, -5}, {0.5, 0.5}, "signed two-action all-negative state is uniform");
  check_scalar({5, 15}, {0.25, 0.75}, "signed two-action positives normalize");
  check_scalar({-20, 10, 30}, {0.0, 0.25, 0.75},
               "signed three-action regrets normalize");
  check_scalar({-20, -10, -1}, {1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0},
               "signed three-action all-negative state is uniform");
  check_scalar({-8, 4, 12, 0}, {0.0, 0.25, 0.75, 0.0},
               "signed generic-arity regrets normalize");

  constexpr std::size_t hands = 9U;
  for (const std::size_t action_count : {2U, 3U, 4U}) {
    std::vector<std::vector<std::uint16_t>> storage(
        action_count, std::vector<std::uint16_t>(hands));
    std::vector<std::vector<float>> output(action_count, std::vector<float>(hands, -1.0F));
    std::vector<const std::uint16_t *> sources(action_count);
    std::vector<float *> destinations(action_count);
    for (std::size_t action = 0U; action < action_count; ++action) {
      for (std::size_t hand = 0U; hand < hands; ++hand) {
        const auto magnitude = static_cast<std::int16_t>((action + 1U) * (hand + 1U));
        storage[action][hand] = signed_code((hand + action) % 3U == 0U ? -magnitude : magnitude);
      }
      sources[action] = storage[action].data();
      destinations[action] = output[action].data();
    }
    require(gtosd::detail::regret_match_signed_action_major(sources, destinations, hands),
            "signed action-major SIMD plus scalar-tail decode succeeds");
    for (std::size_t hand = 0U; hand < hands; ++hand) {
      std::vector<std::uint16_t> oracle_codes(action_count);
      std::vector<double> oracle(action_count, 0.0);
      for (std::size_t action = 0U; action < action_count; ++action) {
        oracle_codes[action] = storage[action][hand];
      }
      require(gtosd::detail::regret_match_signed_codes(oracle_codes, oracle),
              "signed scalar oracle succeeds");
      for (std::size_t action = 0U; action < action_count; ++action) {
        require_near(output[action][hand], oracle[action], 2.0e-7,
                     "signed SIMD and scalar oracle agree");
      }
    }
  }
}

void test_config_specific_preflight() {
  const auto config = gtosd::make_postflop_benchmark_config(gtosd::PostflopBenchmark::PfF1);
  require(config.has_value(), "PF-F1 config builds");

  const auto lazy =
      gtosd::analyze_postflop_config(config.value(), gtosd::MemoryPrototype::LazyInRam);
  const auto out_of_core =
      gtosd::analyze_postflop_config(config.value(), gtosd::MemoryPrototype::OutOfCore);
  require(lazy.has_value() && out_of_core.has_value(), "both production backends preflight");
  require(lazy.value().information_sets == 30'873'216U && lazy.value().actions == 66'756'096U,
          "PF-F1 preserves exact physical infosets and actions");
  require(lazy.value().memory.peak_resident_bytes == 5'622'269'688U,
          "lazy PF-F1 estimate remains stable");
  require(out_of_core.value().memory.backing_store_bytes == 4'554'172'152U,
          "out-of-core PF-F1 backing estimate remains stable");
  require(lazy.value().exact_outcomes && !lazy.value().uses_bucketing,
          "production preflight remains exact and unbucketed");
}

void test_invalid_config_is_rejected() {
  gtosd::PostflopTreeConfig invalid;
  const auto report = gtosd::analyze_postflop_config(invalid, gtosd::MemoryPrototype::LazyInRam);
  require(!report && report.error() == gtosd::MemoryError::InvalidConfiguration,
          "invalid production config returns a typed error");
}

void test_invalid_solver_options_are_rejected() {
  const std::string fixture_path =
      std::string(GTOSD_SOURCE_DIR) + "/tests/fixtures/postflop_check_only.json";
  std::ifstream fixture(fixture_path, std::ios::binary);
  const std::string json((std::istreambuf_iterator<char>(fixture)),
                         std::istreambuf_iterator<char>());
  const auto config = gtosd::parse_tree_config_json(json);
  require(static_cast<bool>(fixture) && config.has_value(), "solver option fixture parses");

  gtosd::PostflopSolveOptions options;
  options.memory_backend = gtosd::MemoryPrototype::StreetDecomposition;
  const auto unsupported = gtosd::solve_postflop_exact(config.value(), options);
  require(!unsupported && unsupported.error() == gtosd::PostflopSolverError::InvalidConfiguration,
          "street decomposition is rejected by the production traversal");

  options.memory_backend = gtosd::MemoryPrototype::LazyInRam;
  options.target_normalized_nash_conv = std::numeric_limits<double>::quiet_NaN();
  const auto invalid_target = gtosd::solve_postflop_exact(config.value(), options);
  require(!invalid_target &&
              invalid_target.error() == gtosd::PostflopSolverError::InvalidConfiguration,
          "a non-finite NashConv target is rejected");

  options.target_normalized_nash_conv.reset();
  options.target_normalized_max_deviation = std::numeric_limits<double>::infinity();
  const auto invalid_max_deviation = gtosd::solve_postflop_exact(config.value(), options);
  require(!invalid_max_deviation &&
              invalid_max_deviation.error() == gtosd::PostflopSolverError::InvalidConfiguration,
          "a non-finite maximum-deviation target is rejected");

  options.target_normalized_max_deviation = 0.01;
  options.target_normalized_nash_conv = 0.01;
  const auto ambiguous_target = gtosd::solve_postflop_exact(config.value(), options);
  require(!ambiguous_target &&
              ambiguous_target.error() == gtosd::PostflopSolverError::InvalidConfiguration,
          "NashConv and GTO+ dEV stopping targets cannot be mixed");
}

void test_exact_check_only_solve_and_resume() {
  const std::string fixture_path =
      std::string(GTOSD_SOURCE_DIR) + "/tests/fixtures/postflop_check_only.json";
  std::ifstream fixture(fixture_path, std::ios::binary);
  const std::string json((std::istreambuf_iterator<char>(fixture)),
                         std::istreambuf_iterator<char>());
  const auto config = gtosd::parse_tree_config_json(json);
  require(static_cast<bool>(fixture) && config.has_value(), "check-only config parses");

  gtosd::PostflopSolveOptions options;
  options.iterations = 3;
  options.certification_interval = 1;
  options.control_callback = [](const std::uint64_t iteration) {
    return iteration == 1U ? gtosd::PostflopControlCommand::Pause
                           : gtosd::PostflopControlCommand::Continue;
  };
  const auto solved = gtosd::solve_postflop_exact(config.value(), options);
  require(solved.has_value(), "exact check-only postflop solve succeeds");
  require(solved.value().public_tree.node_count == 3'270U &&
              solved.value().information_sets == 1'015'872U && solved.value().actions == 1'015'872U,
          "solver layout preserves physical public nodes and combo infosets");
  require(solved.value().convergence.size() == 1U &&
              solved.value().stop_reason == gtosd::PostflopStopReason::Paused &&
              solved.value().checkpoint.completed_iterations == 1U &&
              solved.value().maximum_normalization_error < 1e-12 &&
              std::abs(solved.value().convergence[0].nash_conv_antes) < 1e-12 &&
              std::abs(solved.value().convergence[0].expected_payoff_sum_antes) < 1e-12,
          "single-action game certifies at zero NashConv and remains zero-sum");

  const auto serialized = gtosd::serialize_postflop_checkpoint(solved.value().checkpoint);
  require(serialized.has_value(), "production checkpoint serializes");
  auto inconsistent_checkpoint = solved.value().checkpoint;
  ++inconsistent_checkpoint.action_count;
  require(!gtosd::serialize_postflop_checkpoint(inconsistent_checkpoint),
          "checkpoint serialization rejects an inconsistent action count");
  const auto restored = gtosd::deserialize_postflop_checkpoint(serialized.value());
  require(restored.has_value() &&
              restored.value().cumulative_regret == solved.value().checkpoint.cumulative_regret &&
              restored.value().cumulative_strategy == solved.value().checkpoint.cumulative_strategy,
          "production checkpoint round-trip is lossless");
  const auto oversized_text = gtosd::deserialize_postflop_checkpoint(
      "GTOSD_POSTFLOP_CHECKPOINT 1 0\nfingerprint\n1 0 999999999999\n");
  require(!oversized_text &&
              oversized_text.error() == gtosd::PostflopSolverError::InvalidCheckpoint,
          "text checkpoint rejects an impossible value count before allocation");

  const auto checkpoint_path = std::filesystem::current_path() / "gtosd_phase7_checkpoint_test.bin";
  const auto saved =
      gtosd::save_postflop_checkpoint(solved.value().checkpoint, checkpoint_path.string());
  const auto loaded = gtosd::load_postflop_checkpoint(checkpoint_path.string());
  require(saved.has_value() && loaded.has_value() &&
              loaded.value().cumulative_regret == solved.value().checkpoint.cumulative_regret &&
              loaded.value().cumulative_strategy == solved.value().checkpoint.cumulative_strategy,
          "atomic binary checkpoint round-trip is lossless");
  const auto checkpoint_size = std::filesystem::file_size(checkpoint_path);
  {
    std::ofstream append(checkpoint_path, std::ios::binary | std::ios::app);
    append.put('\0');
  }
  const auto oversized_binary = gtosd::load_postflop_checkpoint(checkpoint_path.string());
  require(!oversized_binary &&
              oversized_binary.error() == gtosd::PostflopSolverError::InvalidCheckpoint,
          "binary checkpoint rejects an inconsistent payload size before allocation");
  std::filesystem::resize_file(checkpoint_path, checkpoint_size);

  options.iterations = 2;
  options.control_callback = {};
  const auto resumed = gtosd::solve_postflop_exact(config.value(), options, &loaded.value());
  require(resumed.has_value() && resumed.value().checkpoint.completed_iterations == 2U,
          "production checkpoint resumes to the requested iteration");
  const auto continuous = gtosd::solve_postflop_exact(config.value(), options);
  require(continuous.has_value() &&
              continuous.value().checkpoint.cumulative_regret ==
                  resumed.value().checkpoint.cumulative_regret &&
              continuous.value().checkpoint.cumulative_strategy ==
                  resumed.value().checkpoint.cumulative_strategy,
          "continuous and resumed CFR+ checkpoints are byte-equivalent");

  const auto out_of_core_manifest =
      std::filesystem::current_path() / "gtosd_phase7_out_of_core_test.chk";
  const auto out_of_core_buffers =
      std::filesystem::current_path() / "gtosd_phase7_out_of_core_test.buffers";
  gtosd::PostflopSolveOptions out_of_core_options = options;
  out_of_core_options.iterations = 1;
  out_of_core_options.memory_backend = gtosd::MemoryPrototype::OutOfCore;
  out_of_core_options.backing_file = out_of_core_buffers.string();
  const auto out_of_core = gtosd::solve_postflop_exact(config.value(), out_of_core_options);
  require(out_of_core.has_value() && out_of_core.value().checkpoint.cumulative_regret.empty() &&
              out_of_core.value().checkpoint.external_buffer_file == out_of_core_buffers.string(),
          "out-of-core traversal keeps action buffers outside the checkpoint object");
  const auto external_saved = gtosd::save_postflop_checkpoint(out_of_core.value().checkpoint,
                                                              out_of_core_manifest.string());
  const auto external_loaded = gtosd::load_postflop_checkpoint(out_of_core_manifest.string());
  require(external_saved.has_value() && external_loaded.has_value() &&
              external_loaded.value().action_count == solved.value().actions,
          "out-of-core manifest round-trip preserves the exact buffer contract");
  out_of_core_options.iterations = 2;
  const auto external_resumed =
      gtosd::solve_postflop_exact(config.value(), out_of_core_options, &external_loaded.value());
  require(external_resumed.has_value() &&
              external_resumed.value().checkpoint.completed_iterations == 2U,
          "out-of-core checkpoint resumes");
  const auto external_certified =
      gtosd::certify_postflop_checkpoint(config.value(), external_resumed.value().checkpoint);
  require(external_certified.has_value() &&
              std::abs(external_certified.value().nash_conv_antes) < 1e-12,
          "out-of-core strategy certifies with exact BR");

  const auto combos = gtosd::all_combos();
  const auto flop_mask =
      config.value().flop[0].mask() | config.value().flop[1].mask() | config.value().flop[2].mask();
  gtosd::ComboId query_combo = 0;
  while (query_combo < combos.size() &&
         ((combos[query_combo].first.mask() | combos[query_combo].second.mask()) & flop_mask) !=
             0U) {
    ++query_combo;
  }
  const auto query =
      gtosd::query_postflop_strategy(config.value(), resumed.value().checkpoint, 0, query_combo);
  require(query.has_value() && query.value().actions.size() == 1U &&
              query.value().probabilities.size() == 1U &&
              std::abs(query.value().probabilities[0] - 1.0) < 1e-12,
          "strategy query returns the exact physical-combo policy");

  {
    std::fstream corrupt(checkpoint_path, std::ios::binary | std::ios::in | std::ios::out);
    corrupt.seekg(-1, std::ios::end);
    char byte = '\0';
    corrupt.read(&byte, 1);
    byte ^= 0x01;
    corrupt.seekp(-1, std::ios::end);
    corrupt.write(&byte, 1);
  }
  const auto corrupted = gtosd::load_postflop_checkpoint(checkpoint_path.string());
  require(!corrupted && corrupted.error() == gtosd::PostflopSolverError::InvalidCheckpoint,
          "binary checkpoint checksum detects a bit flip");
  std::error_code remove_error;
  std::filesystem::remove(checkpoint_path, remove_error);
  require(!remove_error, "binary checkpoint test file is removed");
  std::filesystem::remove(out_of_core_manifest, remove_error);
  require(!remove_error, "out-of-core manifest is removed");
  std::filesystem::remove(out_of_core_buffers, remove_error);
  require(!remove_error, "out-of-core buffers are removed");
}

void test_nontrivial_zero_sum_certification() {
  const std::string fixture_path =
      std::string(GTOSD_SOURCE_DIR) + "/tests/fixtures/postflop_river_bet.json";
  std::ifstream fixture(fixture_path, std::ios::binary);
  const std::string json((std::istreambuf_iterator<char>(fixture)),
                         std::istreambuf_iterator<char>());
  const auto config = gtosd::parse_tree_config_json(json);
  require(static_cast<bool>(fixture) && config.has_value(), "river-bet config parses");

  gtosd::PostflopSolveOptions options;
  options.iterations = 1;
  options.certification_interval = 1;
  const auto solved = gtosd::solve_postflop_exact(config.value(), options);
  require(solved.has_value() && solved.value().convergence.size() == 1U,
          "nontrivial exact river betting solve certifies");
  auto prepared = gtosd::prepare_postflop_tree(
      config.value(), gtosd::make_uniform_postflop_ranges(), true, true, true);
  require(prepared.has_value(), "prepared solver and browser layout builds once");
  const auto prepared_estimate = gtosd::prepared_postflop_layout_estimate(*prepared.value());
  const auto prepared_tree = gtosd::prepared_postflop_public_tree(prepared.value());
  const auto prepared_solved = gtosd::solve_postflop_exact(*prepared.value(), options);
  require(prepared_solved.has_value() && prepared_solved.value().timings.layout_seconds == 0.0 &&
              prepared_estimate.actions == prepared_solved.value().actions && prepared_tree &&
              prepared_tree->stats.node_count == prepared_solved.value().public_tree.node_count,
          "prepared solve excludes tree construction and exposes the same physical browser tree");
  const auto prepared_analysis = gtosd::analyze_postflop_node(
      *prepared.value(), prepared_solved.value().checkpoint, prepared_tree->root);
  require(prepared_analysis.has_value() && !prepared_analysis.value().combos.empty(),
          "prepared browser analysis reuses its physical layout");
  const auto &point = solved.value().convergence.front();
  require(std::abs(point.expected_payoff_sum_antes) < 1e-10 &&
              std::abs(point.profile_value_antes[0] + point.profile_value_antes[1]) < 1e-10,
          "zero-rake profile remains zero-sum");
  require(point.nash_conv_antes >= -1e-12 && point.normalized_nash_conv >= -1e-12,
          "exact best responses produce nonnegative NashConv");

  auto compact_options = options;
  compact_options.state_precision =
      gtosd::PostflopStatePrecision::Float13RegretFloat11Strategy;
  const auto compact = gtosd::solve_postflop_exact(config.value(), compact_options);
  require(compact.has_value() &&
              compact.value().checkpoint.cumulative_compact_state.size() ==
                  compact.value().actions * 3U &&
              compact.value().convergence.size() == 1U &&
              std::isfinite(compact.value().convergence.front().normalized_nash_conv),
          "three-byte compact core state remains exactly certifiable");

  options.iterations = 5;
  options.target_normalized_nash_conv = 1'000.0;
  const auto target_stopped = gtosd::solve_postflop_exact(config.value(), options);
  require(target_stopped.has_value() &&
              target_stopped.value().stop_reason == gtosd::PostflopStopReason::Converged &&
              target_stopped.value().checkpoint.completed_iterations == 1U,
          "the exact solver stops at the first certified dEV below the requested target");

  options.target_normalized_nash_conv.reset();
  options.target_normalized_max_deviation = 1'000.0;
  const auto gto_plus_target_stopped = gtosd::solve_postflop_exact(config.value(), options);
  require(gto_plus_target_stopped.has_value() &&
              gto_plus_target_stopped.value().stop_reason == gtosd::PostflopStopReason::Converged &&
              gto_plus_target_stopped.value().checkpoint.completed_iterations == 1U,
          "the exact solver stops on the first certified GTO+ maximum-deviation target");

  options.iterations = 0;
  options.strict_target = true;
  options.target_normalized_max_deviation = 1'000.0;
  const auto unbounded_target_stopped = gtosd::solve_postflop_exact(config.value(), options);
  require(unbounded_target_stopped.has_value() &&
              unbounded_target_stopped.value().stop_reason ==
                  gtosd::PostflopStopReason::Converged &&
              unbounded_target_stopped.value().checkpoint.completed_iterations == 1U,
          "target-driven core solve has no iteration limit and stops below the strict target");

  options.averaging_delay = 2;
  options.certification_interval = 1;
  const auto delayed_average_stopped = gtosd::solve_postflop_exact(config.value(), options);
  require(delayed_average_stopped.has_value() &&
              delayed_average_stopped.value().stop_reason ==
                  gtosd::PostflopStopReason::Converged &&
              delayed_average_stopped.value().checkpoint.completed_iterations == 3U &&
              delayed_average_stopped.value().convergence.size() == 1U &&
              delayed_average_stopped.value().convergence.front().iteration == 3U,
          "periodic convergence certification starts only after average-strategy sampling");

  options.target_normalized_max_deviation.reset();
  require(!gtosd::solve_postflop_exact(config.value(), options),
          "unbounded core solve without a convergence target is rejected");
}

} // namespace

int main() {
  try {
    test_signed_current_regret_matching();
    test_config_specific_preflight();
    test_invalid_config_is_rejected();
    test_invalid_solver_options_are_rejected();
    test_exact_check_only_solve_and_resume();
    test_nontrivial_zero_sum_certification();
    std::cout << "F7_POSTFLOP_PRODUCTION_TESTS=PASS\n"
              << "assertions=" << assertions << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "F7_POSTFLOP_PREFLIGHT_TESTS=FAIL: " << error.what() << '\n';
    return 1;
  }
}
