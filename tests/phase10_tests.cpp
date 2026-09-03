#include "gtosd/memory/memory.hpp"
#include "gtosd/storage/storage.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

std::size_t assertions = 0;

void require(const bool condition, const std::string &message) {
  ++assertions;
  if (!condition) {
    throw std::runtime_error(message);
  }
}

gtosd::PostflopTreeConfig make_small_config() {
  auto config = gtosd::make_postflop_benchmark_config(gtosd::PostflopBenchmark::PfF1).value();
  for (auto &street : config.streets) {
    for (auto &player : street.players) {
      for (auto &scenario : player) {
        scenario.aggressive_sizes.clear();
        scenario.raise_depth = 0;
        scenario.all_in_mode = gtosd::AllInMode::Disabled;
      }
    }
  }
  return config;
}

gtosd::PostflopRanges make_weighted_ranges(const gtosd::PostflopTreeConfig &config) {
  auto ranges = gtosd::make_uniform_postflop_ranges();
  const auto zero = gtosd::RangeWeight::from_basis_points(0).value();
  const auto half = gtosd::RangeWeight::from_basis_points(5'000).value();
  const auto combos = gtosd::all_combos();
  const auto board = config.flop[0].mask() | config.flop[1].mask() | config.flop[2].mask();
  for (std::size_t combo = 0; combo < combos.size(); ++combo) {
    const auto mask = combos[combo].first.mask() | combos[combo].second.mask();
    if ((mask & board) == 0U) {
      ranges.players[0][combo] = combo % 2U == 0U ? half : zero;
    }
  }
  return ranges;
}

gtosd::PostflopSolveResult solve(const gtosd::PostflopTreeConfig &config,
                                 const gtosd::PostflopRanges *ranges = nullptr) {
  gtosd::PostflopSolveOptions options;
  options.iterations = 1U;
  options.certification_interval = 1U;
  const auto result = ranges == nullptr ? gtosd::solve_postflop_exact(config, options)
                                        : gtosd::solve_postflop_exact(config, *ranges, options);
  require(result.has_value(), "small exact solve succeeds");
  return result.value();
}

void test_weighted_reach_and_fingerprint() {
  const auto config = make_small_config();
  const auto uniform = gtosd::make_uniform_postflop_ranges();
  const auto weighted = make_weighted_ranges(config);
  require(gtosd::validate_postflop_ranges(config, uniform).has_value() &&
              gtosd::validate_postflop_ranges(config, weighted).has_value(),
          "uniform and fractional physical ranges are valid");

  const auto legacy = solve(config);
  const auto explicit_uniform = solve(config, &uniform);
  const auto weighted_result = solve(config, &weighted);
  require(legacy.checkpoint.game_fingerprint == explicit_uniform.checkpoint.game_fingerprint,
          "explicit uniform ranges preserve the F7 checkpoint fingerprint");
  require(weighted_result.checkpoint.game_fingerprint != legacy.checkpoint.game_fingerprint,
          "physical range weights are part of the game fingerprint");
  require(weighted_result.convergence.back().profile_value_antes !=
              legacy.convergence.back().profile_value_antes,
          "weighted root reach changes the exact profile EV");
  const auto batch =
      gtosd::query_postflop_strategies(config, weighted, weighted_result.checkpoint, 0U);
  require(batch.has_value() && !batch.value().empty(),
          "all legal physical combos are queryable with one layout build");
  const auto single = gtosd::query_postflop_strategy(config, weighted, weighted_result.checkpoint,
                                                     0U, batch.value().front().combo);
  require(single.has_value() && single.value().probabilities == batch.value().front().probabilities,
          "batch and single-combo strategy queries agree exactly");
  const auto public_tree = gtosd::build_public_tree(config);
  require(public_tree.has_value(), "asymmetric-range analysis tree builds");
  const auto &root = public_tree.value().nodes[public_tree.value().root];
  const auto check = std::ranges::find_if(
      root.edges, [](const auto &edge) { return edge.action.type == gtosd::ActionType::Check; });
  require(check != root.edges.end(), "small asymmetric-range root exposes check");
  const auto checked =
      gtosd::analyze_postflop_node(config, weighted, weighted_result.checkpoint, check->child);
  require(checked.has_value(), checked.has_value()
                                   ? "asymmetric-range child analysis succeeds"
                                   : std::string{"asymmetric-range child analysis failed: "} +
                                         gtosd::postflop_solver_error_name(checked.error()));
  require(
      !gtosd::certify_postflop_checkpoint(config, uniform, weighted_result.checkpoint) &&
          gtosd::certify_postflop_checkpoint(config, uniform, weighted_result.checkpoint).error() ==
              gtosd::PostflopSolverError::CheckpointMismatch,
      "a checkpoint cannot be silently reused with different ranges");
}

void test_range_storage_round_trip() {
  const auto config = make_small_config();
  const auto ranges = make_weighted_ranges(config);
  const auto solved = solve(config, &ranges);
  const auto archive =
      gtosd::make_postflop_solution(config, ranges, solved.checkpoint, solved.convergence.back());
  require(archive.has_value(), "weighted solution archive builds");
  const auto key = gtosd::generate_storage_key();
  const auto path = std::filesystem::current_path() / "phase10_weighted_ranges.gtsd";
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
  require(gtosd::save_solution(path, archive.value(), key).has_value(),
          "weighted solution saves atomically");
  const auto reader = gtosd::open_solution(path, key);
  require(reader.has_value(), "weighted solution opens");
  const auto restored = gtosd::restore_postflop_solution(reader.value());
  require(restored.has_value() && restored.value().ranges == ranges,
          "all 1,260 physical range weights round-trip losslessly");
  require(gtosd::certify_postflop_checkpoint(restored.value().config, restored.value().ranges,
                                             restored.value().checkpoint)
              .has_value(),
          "restored range/checkpoint pair remains certifiable");
  std::filesystem::remove(path, ignored);
}

void test_empty_range_rejected() {
  const auto config = make_small_config();
  gtosd::PostflopRanges empty;
  require(!gtosd::validate_postflop_ranges(config, empty),
          "a range pair with no compatible private deal is rejected");
  gtosd::PostflopSolveOptions options;
  require(!gtosd::solve_postflop_exact(config, empty, options),
          "solver rejects an empty range before traversal");
}

void test_architectural_topology_is_read_only_and_disjoint() {
  const auto config = make_small_config();
  const auto ranges = gtosd::make_uniform_postflop_ranges();
  auto prepared = gtosd::prepare_postflop_tree(config, ranges, true, true, false).value();
  const auto before = gtosd::prepared_postflop_layout_estimate(*prepared);
  const auto topology = gtosd::inspect_postflop_architectural_topology(*prepared);
  const auto after = gtosd::prepared_postflop_layout_estimate(*prepared);
  require(topology.has_value() && !topology.value().river_work_units.empty(),
          "architectural topology inspector finds real river work units");
  require(topology.value().invalid_or_cyclic_units == 0U,
          "architectural topology inspector validates the canonical DAG");
  require(topology.value().control_plan_ops == topology.value().analyzed_public_nodes,
          "compiled architectural control plan covers every analyzed visit");
  require(topology.value().recursive_control_checksum == topology.value().linear_control_checksum,
          "compiled architectural control plan preserves DFS order");
  require(topology.value().recursive_control_seconds >= 0.0 &&
              topology.value().linear_control_seconds >= 0.0,
          "architectural control benchmark publishes nonnegative timings");
  require(before.canonical_public_nodes == after.canonical_public_nodes &&
              before.information_sets == after.information_sets && before.actions == after.actions,
          "architectural topology inspection is read-only");
  for (std::uint8_t player = 0U; player < 2U; ++player) {
    std::vector<const gtosd::PostflopRiverWorkUnit *> intervals;
    for (const auto &unit : topology.value().river_work_units) {
      require(std::popcount(unit.board_mask) == 5,
              "every architectural work unit owns a five-card river board");
      require(unit.update_player < 2U && unit.live_hero_combos > 0U &&
                  unit.live_opponent_combos > 0U,
              "every architectural work unit has explicit player-local capacities");
      if (unit.update_player == player && unit.state_interval_present) {
        intervals.push_back(&unit);
      }
    }
    std::ranges::sort(intervals, {}, &gtosd::PostflopRiverWorkUnit::state_begin);
    for (std::size_t index = 1U; index < intervals.size(); ++index) {
      require(intervals[index - 1U]->state_end <= intervals[index]->state_begin,
              "river work units in one update phase own disjoint state intervals");
    }
  }
}

void test_hs_dcfr30_schedule_and_resume() {
  const auto at_zero = gtosd::hs_dcfr30_schedule(0U);
  const auto at_sixty = gtosd::hs_dcfr30_schedule(60U);
  const auto at_thousand = gtosd::hs_dcfr30_schedule(1'000U);
  const auto clamped = gtosd::hs_dcfr30_schedule(10'000U);
  require(at_zero.alpha == 1.0 && at_zero.beta == -1.0 && at_zero.gamma == 30.0,
          "HS-DCFR(30) starts at the published exponents");
  require(std::abs(at_sixty.alpha - 1.18) < 1.0e-12 && std::abs(at_sixty.beta + 1.12) < 1.0e-12 &&
              std::abs(at_sixty.gamma - 29.7) < 1.0e-12,
          "HS-DCFR(30) follows the published linear schedule");
  require(at_thousand.alpha == 4.0 && at_thousand.beta == -3.0 && at_thousand.gamma == 25.0,
          "HS-DCFR(30) matches the paper's experimental horizon");
  require(clamped.alpha == 5.0 && clamped.beta == -5.0 && clamped.gamma == 5.0,
          "unbounded HS-DCFR(30) remains inside the proven parameter bounds");

  const auto config = make_small_config();
  const auto ranges = gtosd::make_uniform_postflop_ranges();
  auto continuous_tree = gtosd::prepare_postflop_tree(config, ranges, true, true, false).value();
  gtosd::PostflopSolveOptions options;
  options.iterations = 6U;
  options.certification_interval = 6U;
  options.algorithm = gtosd::PostflopAlgorithm::HsDcfr30;
  options.state_precision = gtosd::PostflopStatePrecision::ScaledUint16RegretStrategy;
  const auto continuous = gtosd::solve_postflop_exact(*continuous_tree, options);
  require(continuous.has_value(), continuous.has_value()
                                      ? "HS-DCFR(30) continuous solve succeeds"
                                      : std::string{"HS-DCFR(30) continuous solve failed: "} +
                                            gtosd::postflop_solver_error_name(continuous.error()));

  auto resumed_tree = gtosd::prepare_postflop_tree(config, ranges, true, true, false).value();
  auto partial_options = options;
  partial_options.iterations = 3U;
  partial_options.certification_interval = 3U;
  const auto partial = gtosd::solve_postflop_exact(*resumed_tree, partial_options);
  require(partial.has_value(), partial.has_value()
                                   ? "HS-DCFR(30) partial solve succeeds"
                                   : std::string{"HS-DCFR(30) partial solve failed: "} +
                                         gtosd::postflop_solver_error_name(partial.error()));
  const auto resumed =
      gtosd::solve_postflop_exact(*resumed_tree, options, &partial.value().checkpoint);
  require(resumed.has_value(), resumed.has_value()
                                   ? "HS-DCFR(30) checkpoint resumes"
                                   : std::string{"HS-DCFR(30) checkpoint resume failed: "} +
                                         gtosd::postflop_solver_error_name(resumed.error()));
  require(continuous.value().checkpoint.cumulative_regret_uint16 ==
                  resumed.value().checkpoint.cumulative_regret_uint16 &&
              continuous.value().checkpoint.cumulative_strategy_uint16 ==
                  resumed.value().checkpoint.cumulative_strategy_uint16 &&
              continuous.value().checkpoint.regret_node_scale ==
                  resumed.value().checkpoint.regret_node_scale &&
              continuous.value().checkpoint.strategy_node_scale ==
                  resumed.value().checkpoint.strategy_node_scale,
          "HS-DCFR(30) resume is byte-equivalent to a continuous run");
}

void test_production_dcfr_schedule_and_resume() {
  const auto at_one = gtosd::production_dcfr_schedule(1U);
  const auto at_two = gtosd::production_dcfr_schedule(2U);
  const auto at_three = gtosd::production_dcfr_schedule(3U);
  const auto at_five = gtosd::production_dcfr_schedule(5U);
  const auto at_seventeen = gtosd::production_dcfr_schedule(17U);
  const auto at_sixty_five = gtosd::production_dcfr_schedule(65U);
  const auto at_sixty_six = gtosd::production_dcfr_schedule(66U);
  const auto at_one_twenty = gtosd::production_dcfr_schedule(120U);
  const auto at_two_fifty_seven = gtosd::production_dcfr_schedule(257U);
  require(at_one.reset_average_strategy && at_one.epoch_start_iteration == 1U &&
              at_one.regret_discount_iteration == 0U &&
              at_one.average_strategy_weight == 1.0,
          "production DCFR initializes the first epoch");
  require(at_two.reset_average_strategy && at_two.epoch_start_iteration == 2U &&
              at_two.regret_discount_iteration == 1U &&
              at_two.average_strategy_weight == 1.0,
          "production DCFR resets at iteration 2 on the production alpha clock");
  require(!at_three.reset_average_strategy && at_three.epoch_index == 1U &&
              at_three.regret_discount_iteration == 2U &&
              at_three.average_strategy_weight == 8.0,
          "production DCFR uses cubic weights before the clock cutover");
  require(at_five.reset_average_strategy && at_five.epoch_start_iteration == 5U &&
              at_seventeen.reset_average_strategy &&
              at_seventeen.epoch_start_iteration == 17U &&
              at_sixty_five.reset_average_strategy &&
              at_sixty_five.epoch_start_iteration == 65U,
          "production DCFR resets only at the qualified bounded epochs");
  require(!at_sixty_six.reset_average_strategy && at_sixty_six.epoch_index == 1U &&
              at_sixty_six.regret_discount_iteration == 64U &&
              at_sixty_six.average_strategy_weight == 8.0,
          "production DCFR switches to the upstream alpha clock after iteration 65");
  require(!at_one_twenty.reset_average_strategy && at_one_twenty.epoch_index == 55U &&
              at_one_twenty.regret_discount_iteration == 118U &&
              at_one_twenty.average_strategy_weight == 175'616.0 &&
              !at_two_fifty_seven.reset_average_strategy &&
              at_two_fifty_seven.epoch_start_iteration == 65U &&
              at_two_fifty_seven.average_strategy_weight == 7'189'057.0,
          "production DCFR retains the final epoch beyond the former reset at 257");

  // The additive cubic weights are a common-scale rewrite of
  // S_k=(k/(k+1))^3*S_{k-1}+sigma_k. Verify the identity independently.
  double discounted = 0.0;
  double weighted = 0.0;
  for (std::uint64_t k = 0U; k < 12U; ++k) {
    const double sample = static_cast<double>(k + 2U) / 13.0;
    const double current = static_cast<double>(k);
    const double discount = k == 0U ? 0.0 : std::pow(current / (current + 1.0), 3.0);
    discounted = discounted * discount + sample;
    const double weight = static_cast<double>((k + 1U) * (k + 1U) * (k + 1U));
    weighted += weight * sample;
    require(std::abs(discounted - weighted / weight) < 1.0e-12,
            "cubic additive weights equal the recursive gamma=3 discount up to common scale");
  }

  const auto config = make_small_config();
  const auto ranges = gtosd::make_uniform_postflop_ranges();
  auto continuous_tree = gtosd::prepare_postflop_tree(config, ranges, true, true, false).value();
  gtosd::PostflopSolveOptions options;
  options.iterations = 70U;
  options.certification_interval = 70U;
  options.algorithm = gtosd::PostflopAlgorithm::ProductionDcfr;
  options.state_precision = gtosd::PostflopStatePrecision::ScaledUint16RegretStrategy;
  const auto continuous = gtosd::solve_postflop_exact(*continuous_tree, options);
  require(continuous.has_value(), continuous.has_value()
                                      ? "production DCFR continuous solve succeeds"
                                      : std::string{"production DCFR solve failed: "} +
                                            gtosd::postflop_solver_error_name(continuous.error()));

  auto resumed_tree = gtosd::prepare_postflop_tree(config, ranges, true, true, false).value();
  auto partial_options = options;
  partial_options.iterations = 64U;
  partial_options.certification_interval = 64U;
  const auto partial = gtosd::solve_postflop_exact(*resumed_tree, partial_options);
  require(partial.has_value(), partial.has_value()
                                   ? "production DCFR partial solve succeeds"
                                   : std::string{"production DCFR partial solve failed: "} +
                                         gtosd::postflop_solver_error_name(partial.error()));
  const auto resumed =
      gtosd::solve_postflop_exact(*resumed_tree, options, &partial.value().checkpoint);
  require(resumed.has_value(), resumed.has_value()
                                   ? "production DCFR checkpoint resumes"
                                   : std::string{"production DCFR resume failed: "} +
                                         gtosd::postflop_solver_error_name(resumed.error()));
  require(continuous.value().checkpoint.cumulative_regret_uint16 ==
                  resumed.value().checkpoint.cumulative_regret_uint16 &&
              continuous.value().checkpoint.cumulative_strategy_uint16 ==
                  resumed.value().checkpoint.cumulative_strategy_uint16 &&
              continuous.value().checkpoint.regret_node_scale ==
                  resumed.value().checkpoint.regret_node_scale &&
              continuous.value().checkpoint.strategy_node_scale ==
                  resumed.value().checkpoint.strategy_node_scale,
          "production DCFR resume across the final reset is byte-equivalent");
  require(continuous.value().checkpoint.algorithm == gtosd::PostflopAlgorithm::ProductionDcfr &&
              static_cast<std::uint8_t>(continuous.value().checkpoint.algorithm) == 11U,
          "production DCFR preserves the qualified checkpoint identity");
}

void test_explicit_resident_working_set_budget_is_byte_exact() {
  const auto config = make_small_config();
  const auto ranges = gtosd::make_uniform_postflop_ranges();
  gtosd::PostflopSolveOptions resident_options;
  resident_options.iterations = 4U;
  resident_options.certification_interval = 4U;
  resident_options.algorithm = gtosd::PostflopAlgorithm::ProductionDcfr;
  resident_options.state_precision = gtosd::PostflopStatePrecision::ScaledUint16RegretStrategy;

  auto resident_tree = gtosd::prepare_postflop_tree(config, ranges, true, true, false).value();
  const auto resident = gtosd::solve_postflop_exact(*resident_tree, resident_options);
  require(resident.has_value() && resident.value().checkpoint.runtime_state == nullptr,
          "unbounded exact state remains resident");

  auto budgeted_options = resident_options;
  budgeted_options.resident_working_set_budget_bytes = 1U;
  auto budgeted_tree = gtosd::prepare_postflop_tree(config, ranges, true, true, false).value();
  const auto budgeted = gtosd::solve_postflop_exact(*budgeted_tree, budgeted_options);
  require(budgeted.has_value() && budgeted.value().checkpoint.runtime_state != nullptr &&
              budgeted.value().checkpoint.cumulative_regret_uint16.empty() &&
              budgeted.value().checkpoint.cumulative_strategy_uint16.empty() &&
              budgeted.value().checkpoint.regret_node_scale.empty() &&
              budgeted.value().checkpoint.strategy_node_scale.empty(),
          "tight working-set budget selects only page-backed exact state");

  auto materialized = budgeted.value().checkpoint;
  require(gtosd::materialize_postflop_checkpoint_state(materialized).has_value() &&
              materialized.runtime_state == nullptr,
          "page-backed state materializes explicitly for persistence");
  require(materialized.cumulative_regret_uint16 ==
                  resident.value().checkpoint.cumulative_regret_uint16 &&
              materialized.cumulative_strategy_uint16 ==
                  resident.value().checkpoint.cumulative_strategy_uint16 &&
              materialized.regret_node_scale == resident.value().checkpoint.regret_node_scale &&
              materialized.strategy_node_scale ==
                  resident.value().checkpoint.strategy_node_scale,
          "resident and page-backed production DCFR states are byte-identical");
  const auto archive = gtosd::make_postflop_solution(
      config, ranges, materialized, budgeted.value().convergence.back());
  require(archive.has_value(), "materialized page-backed state is persistable");
  require(budgeted.value().convergence.back().profile_value_antes ==
                  resident.value().convergence.back().profile_value_antes &&
              budgeted.value().convergence.back().best_response_value_antes ==
                  resident.value().convergence.back().best_response_value_antes &&
              budgeted.value().convergence.back().nash_conv_antes ==
                  resident.value().convergence.back().nash_conv_antes,
          "resident and page-backed exact certifications are identical");
}

#if defined(GTOSD_ENABLE_REAL_NODE_REPLAY)
void test_real_node_replay_capture_is_bounded_and_authoritative() {
  const auto config = make_small_config();
  const auto ranges = gtosd::make_uniform_postflop_ranges();
  auto prepared = gtosd::prepare_postflop_tree(config, ranges, true, true, false).value();
  gtosd::PostflopRealNodeReplayCapture capture;
  capture.maximum_samples = 12U;
  capture.samples_per_stratum = 2U;
  capture.sampling_modulus = 1U;
  capture.iterations = {1U, 2U};
  gtosd::PostflopSolveOptions options;
  options.iterations = 2U;
  options.certification_interval = 2U;
  options.algorithm = gtosd::PostflopAlgorithm::Dcfr;
  options.state_precision = gtosd::PostflopStatePrecision::ScaledUint16RegretStrategy;
  options.diagnostic_real_node_replay = &capture;
  const auto solved = gtosd::solve_postflop_exact(*prepared, options);
  require(solved.has_value() && solved.value().diagnostic_real_node_replay.has_value(),
          "replay-gated DCFR solve returns a corpus");
  const auto &corpus = *solved.value().diagnostic_real_node_replay;
  require(!corpus.samples.empty() && corpus.samples.size() <= capture.maximum_samples &&
              corpus.retained_updates == corpus.samples.size(),
          "real-node replay capture is nonempty and bounded");
  for (const auto &sample : corpus.samples) {
    const auto entries = static_cast<std::size_t>(sample.action_count) * sample.hand_count;
    require(sample.iteration == 1U || sample.iteration == 2U,
            "replay only retains selected relative iterations");
    require(sample.producers.size() == sample.action_count &&
                sample.old_regret_codes.size() == entries &&
                sample.old_strategy_codes.size() == entries &&
                sample.current_policy.size() == entries && sample.action_values.size() == entries &&
                sample.resulting_regret_values.size() == entries &&
                sample.resulting_strategy_values.size() == entries &&
                sample.resulting_regret_codes.size() == entries &&
                sample.resulting_strategy_codes.size() == entries &&
                sample.current_values.size() == sample.hand_count &&
                sample.parent_returned_values.size() == sample.hand_count,
            "replay sample stores complete action-major input and authoritative output");
    require(std::bit_cast<std::uint32_t>(sample.old_regret_scale) !=
                    std::bit_cast<std::uint32_t>(-0.0F) &&
                std::bit_cast<std::uint32_t>(sample.resulting_regret_scale) !=
                    std::bit_cast<std::uint32_t>(-0.0F),
            "replay scales use canonical nonnegative representation");
  }
}
#endif

} // namespace

int main() {
  try {
    test_weighted_reach_and_fingerprint();
    test_range_storage_round_trip();
    test_empty_range_rejected();
    test_architectural_topology_is_read_only_and_disjoint();
    test_hs_dcfr30_schedule_and_resume();
    test_production_dcfr_schedule_and_resume();
    test_explicit_resident_working_set_budget_is_byte_exact();
#if defined(GTOSD_ENABLE_REAL_NODE_REPLAY)
    test_real_node_replay_capture_is_bounded_and_authoritative();
#endif
    std::cout << "phase10 assertions=" << assertions << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "phase10 failure after assertions=" << assertions << ": " << error.what() << '\n';
    return 1;
  }
}
