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
