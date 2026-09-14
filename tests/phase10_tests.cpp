#include "gtosd/memory/memory.hpp"
#include "gtosd/storage/storage.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

#ifndef GTOSD_SOURCE_DIR
#define GTOSD_SOURCE_DIR "."
#endif

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

gtosd::PostflopTreeConfig make_validation_river_config() {
  auto config = make_small_config();
  std::uint64_t board_mask = 0U;
  for (const auto card : config.flop) {
    board_mask |= card.mask();
  }
  for (std::uint8_t index = 0U; index < 36U && (!config.turn || !config.river); ++index) {
    const auto card = gtosd::CardId::from_index(index).value();
    if ((card.mask() & board_mask) != 0U) {
      continue;
    }
    if (!config.turn) {
      config.turn = card;
    } else {
      config.river = card;
    }
    board_mask |= card.mask();
  }
  const auto half_pot = gtosd::PotPercentage::from_basis_points(5'000).value();
  for (auto &player : config.streets[2].players) {
    for (auto &scenario : player) {
      scenario.aggressive_sizes = {half_pot};
    }
  }
  return config;
}

gtosd::PostflopTreeConfig load_tree_config_fixture(const std::string &relative_path) {
  const auto path = std::filesystem::path(GTOSD_SOURCE_DIR) / relative_path;
  std::ifstream input(path, std::ios::binary);
  require(static_cast<bool>(input), "postflop D/V fixture opens");
  std::ostringstream serialized;
  serialized << input.rdbuf();
  const auto parsed = gtosd::parse_tree_config_json(serialized.str());
  require(parsed.has_value(), "postflop D/V fixture parses");
  return parsed.value();
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
  const auto root_cfvs = gtosd::derive_postflop_root_counterfactual_values(
      config, weighted, weighted_result.checkpoint);
  require(root_cfvs.has_value(), "weighted average policy exposes exact root boundary CFVs");
  require(root_cfvs.value().game_fingerprint == weighted_result.checkpoint.game_fingerprint &&
              root_cfvs.value().blueprint_iterations ==
                  weighted_result.checkpoint.completed_iterations &&
              root_cfvs.value().mode == gtosd::PostflopRootValueMode::AverageStrategy,
          "root boundary CFVs identify the exact game and average-policy iteration");
  require(!root_cfvs.value().players[0].empty() && !root_cfvs.value().players[1].empty(),
          "root boundary contains both players' live physical combos");
  require(root_cfvs.value().maximum_recomposition_error_antes <= 1e-9,
          "physical-combo root CFVs recompose the authoritative profile value");
  for (const auto &player_values : root_cfvs.value().players) {
    for (const auto &value : player_values) {
      require(std::isfinite(value.source_range_weight) &&
                  std::isfinite(value.counterfactual_reach) &&
                  std::isfinite(value.conditional_value_antes),
              "every root boundary value is finite");
      require(value.positive_reach == (value.counterfactual_reach > 0.0),
              "zero counterfactual reach is represented explicitly");
    }
  }
  const auto root_best_responses = gtosd::derive_postflop_root_best_response_values(
      config, weighted, weighted_result.checkpoint);
  require(root_best_responses.has_value(),
          "weighted average policy exposes exact per-combo best-response values");
  require(root_best_responses.value().mode ==
                  gtosd::PostflopRootValueMode::ExactBestResponse &&
              root_best_responses.value().game_fingerprint ==
                  weighted_result.checkpoint.game_fingerprint &&
              root_best_responses.value().players[0].size() ==
                  root_cfvs.value().players[0].size() &&
              root_best_responses.value().players[1].size() ==
                  root_cfvs.value().players[1].size(),
          "best-response root values retain game identity and physical combo coverage");
  require(root_best_responses.value().maximum_recomposition_error_antes <= 1e-9,
          "per-combo best-response values recompose the authoritative exact BR");
  for (std::size_t player = 0U; player < 2U; ++player) {
    require(std::abs(root_best_responses.value().recomposed_value_antes[player] -
                     weighted_result.convergence.back().best_response_value_antes[player]) <=
                1e-9,
            "recomposed per-combo best response matches certification");
  }
  require(!gtosd::derive_postflop_root_counterfactual_values(
               config, uniform, weighted_result.checkpoint),
          "root boundary extraction rejects a checkpoint from different ranges");
  require(!gtosd::derive_postflop_root_best_response_values(
               config, uniform, weighted_result.checkpoint),
          "best-response extraction rejects a checkpoint from different ranges");
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

void test_production_dcfr_bucket_update_oracle() {
  const std::array<double, 2> old_regret{4.0, -2.0};
  const std::array<double, 2> old_average{10.0, 5.0};
  const std::array<double, 4> action_values{3.0, 1.0, -1.0, 5.0};
  const std::array<double, 2> counterfactual_weights{0.25, 0.75};
  const std::array<double, 2> own_reach_weights{0.1, 0.4};
  const auto update = gtosd::detail::production_dcfr_bucket_update(
      old_regret, old_average, action_values, counterfactual_weights, own_reach_weights, 2U, 2.0,
      3.0, 0.8, 0.0);
  require(update.has_value(), "weighted ProductionDcfr bucket update succeeds");
  require(update.value().strategy == std::vector<double>({1.0, 0.0}) &&
              update.value().current_values == std::vector<double>({3.0, -1.0}) &&
              update.value().immediate_regret_delta == std::vector<double>({0.0, 4.0}) &&
              update.value().updated_regret == std::vector<double>({3.2, 8.0}) &&
              update.value().updated_average_strategy == std::vector<double>({11.5, 5.0}),
          "bucket oracle applies shared regret matching and distinct regret/average weights");

  const std::array<double, 4> permuted_values{-1.0, 5.0, 3.0, 1.0};
  const std::array<double, 2> permuted_counterfactual{0.75, 0.25};
  const std::array<double, 2> permuted_own_reach{0.4, 0.1};
  const auto permuted = gtosd::detail::production_dcfr_bucket_update(
      old_regret, old_average, permuted_values, permuted_counterfactual, permuted_own_reach, 2U,
      2.0, 3.0, 0.8, 0.0);
  require(permuted.has_value() &&
              permuted.value().updated_regret == update.value().updated_regret &&
              permuted.value().updated_average_strategy == update.value().updated_average_strategy,
          "bucket aggregation is invariant to physical-member order");

  const std::array<double, 2> zero_regret{};
  const std::array<double, 2> zero_average{};
  const std::array<double, 2> identity_values{2.0, 4.0};
  const std::array<double, 1> unit_weight{1.0};
  const auto identity = gtosd::detail::production_dcfr_bucket_update(
      zero_regret, zero_average, identity_values, unit_weight, unit_weight, 2U, 1.0, 1.0, 1.0, 1.0);
  require(identity.has_value() && identity.value().strategy == std::vector<double>({0.5, 0.5}) &&
              identity.value().current_values == std::vector<double>({3.0}) &&
              identity.value().updated_regret == std::vector<double>({-1.0, 1.0}) &&
              identity.value().updated_average_strategy == std::vector<double>({0.5, 0.5}),
          "one-member bucket matches the exact scalar regret and averaging equations");

  auto invalid_values = identity_values;
  invalid_values[0] = std::numeric_limits<double>::quiet_NaN();
  require(!gtosd::detail::production_dcfr_bucket_update(zero_regret, zero_average, invalid_values,
                                                        unit_weight, unit_weight, 2U, 1.0, 1.0, 1.0,
                                                        1.0),
          "bucket oracle rejects non-finite traversal values");
}

void test_postflop_identity_abstraction_is_exact_and_implicit() {
  const auto config = make_small_config();
  const auto ranges = gtosd::make_uniform_postflop_ranges();
  auto implicit = gtosd::prepare_postflop_tree(config, ranges, true, true, false);
  gtosd::PostflopCardAbstractionPolicy explicit_identity;
  auto explicit_tree =
      gtosd::prepare_postflop_tree(config, ranges, true, true, false, explicit_identity);
  require(implicit.has_value() && explicit_tree.has_value(),
          "implicit and explicit postflop identity policies prepare");
  const auto implicit_summary = gtosd::prepared_postflop_card_abstraction(*implicit.value());
  const auto explicit_summary = gtosd::prepared_postflop_card_abstraction(*explicit_tree.value());
  require(implicit_summary.has_value() && explicit_summary.has_value() &&
              implicit_summary.value() == explicit_summary.value(),
          "implicit and explicit identity policies expose the same layout contract");
  const auto &summary = implicit_summary.value();
  require(summary.exact_information_sets > 0U &&
              summary.exact_information_sets == summary.abstract_information_sets &&
              summary.exact_action_entries == summary.abstract_action_entries &&
              summary.mapping_bytes == 0U && summary.perfect_recall_verified &&
              summary.checkpoint_identity_preserved &&
              summary.fingerprint == "postflop-card-abstraction/1.0/exact-identity",
          "postflop identity is lossless, versioned and has no proportional mapping storage");
  const auto first = gtosd::map_postflop_exact_infoset(summary, 0U);
  const auto last = gtosd::map_postflop_exact_infoset(summary, summary.exact_information_sets - 1U);
  require(first.has_value() && first.value() == 0U && last.has_value() &&
              last.value() == summary.exact_information_sets - 1U &&
              !gtosd::map_postflop_exact_infoset(summary, summary.exact_information_sets),
          "implicit mapping is the exact identity and rejects out-of-range infosets");

  gtosd::PostflopSolveOptions options;
  options.iterations = 4U;
  options.certification_interval = 4U;
  options.algorithm = gtosd::PostflopAlgorithm::ProductionDcfr;
  options.state_precision = gtosd::PostflopStatePrecision::ScaledUint16RegretStrategy;
  options.dcfr_average_exponent = 3.0;
  const auto implicit_solve = gtosd::solve_postflop_exact(*implicit.value(), options);
  options.card_abstraction = explicit_identity;
  const auto explicit_solve = gtosd::solve_postflop_exact(*explicit_tree.value(), options);
  require(implicit_solve.has_value() && explicit_solve.has_value(),
          "implicit and explicit identity policies solve with ProductionDcfr");
  const auto &implicit_checkpoint = implicit_solve.value().checkpoint;
  const auto &explicit_checkpoint = explicit_solve.value().checkpoint;
  require(implicit_checkpoint.game_fingerprint == explicit_checkpoint.game_fingerprint,
          "explicit identity preserves the exact checkpoint fingerprint");
  require(implicit_checkpoint.cumulative_regret_uint16 ==
                  explicit_checkpoint.cumulative_regret_uint16 &&
              implicit_checkpoint.cumulative_strategy_uint16 ==
                  explicit_checkpoint.cumulative_strategy_uint16,
          "explicit identity preserves exact checkpoint action codes");
  require(implicit_checkpoint.regret_node_scale == explicit_checkpoint.regret_node_scale &&
              implicit_checkpoint.strategy_node_scale == explicit_checkpoint.strategy_node_scale,
          "explicit identity preserves exact checkpoint scales");
  const auto identity_path =
      std::filesystem::current_path() / "phase10_postflop_identity_checkpoint.bin";
  std::error_code ignored;
  std::filesystem::remove(identity_path, ignored);
  const auto saved_identity =
      gtosd::save_postflop_checkpoint(explicit_checkpoint, identity_path.string());
  const auto restored_identity =
      saved_identity
          ? gtosd::load_postflop_checkpoint(identity_path.string())
          : gtosd::Result<gtosd::PostflopCheckpoint, gtosd::PostflopSolverError>::failure(
                gtosd::PostflopSolverError::IoFailure);
  require(
      restored_identity.has_value() &&
          restored_identity.value().game_fingerprint == explicit_checkpoint.game_fingerprint &&
          restored_identity.value().cumulative_regret_uint16 ==
              explicit_checkpoint.cumulative_regret_uint16 &&
          restored_identity.value().cumulative_strategy_uint16 ==
              explicit_checkpoint.cumulative_strategy_uint16 &&
          restored_identity.value().regret_node_scale == explicit_checkpoint.regret_node_scale &&
          restored_identity.value().strategy_node_scale == explicit_checkpoint.strategy_node_scale,
      "explicit identity checkpoint round-trips without state drift");
  std::filesystem::remove(identity_path, ignored);

  auto unsupported = explicit_identity;
  unsupported.minor = 1U;
  require(!gtosd::prepare_postflop_tree(config, ranges, true, true, false, unsupported),
          "unsupported postflop abstraction version is rejected");
  unsupported = explicit_identity;
  unsupported.mode = static_cast<gtosd::PostflopCardAbstractionMode>(255U);
  require(!gtosd::prepare_postflop_tree(config, ranges, true, true, false, unsupported),
          "unsupported postflop abstraction mode is rejected");
}

void test_postflop_made_hand_value_mapping_is_deterministic_and_not_solvable() {
  constexpr double bucket_oracle_tolerance = 1.0e-10;
  const auto config = make_validation_river_config();
  const auto ranges = gtosd::make_uniform_postflop_ranges();
  gtosd::PostflopCardAbstractionPolicy policy;
  policy.mode = gtosd::PostflopCardAbstractionMode::MadeHandValue;
  const auto first = gtosd::prepare_postflop_tree(config, ranges, true, true, false, policy);
  const auto second = gtosd::prepare_postflop_tree(config, ranges, true, true, false, policy);
  require(first.has_value() && second.has_value(),
          "made-hand-value policy prepares on the validation river game");
  const auto first_summary = gtosd::prepared_postflop_card_abstraction(*first.value());
  const auto second_summary = gtosd::prepared_postflop_card_abstraction(*second.value());
  require(first_summary.has_value() && second_summary.has_value() &&
              first_summary.value() == second_summary.value(),
          "made-hand-value mapping and fingerprint are deterministic");
  const auto &summary = first_summary.value();
  require(summary.exact_information_sets > 0U && summary.abstract_information_sets > 0U &&
              summary.abstract_information_sets < summary.exact_information_sets &&
              summary.abstract_action_entries < summary.exact_action_entries &&
              summary.exact_information_sets % 465U == 0U &&
              summary.exact_information_sets_by_street[0] == 0U &&
              summary.exact_information_sets_by_street[1] == 0U &&
              summary.exact_information_sets_by_street[2] == summary.exact_information_sets &&
              summary.abstract_information_sets_by_street[2] == summary.abstract_information_sets,
          "made-hand-value buckets reduce only live river private infosets and action entries");
  require(summary.exact_to_abstract_bytes ==
                  summary.exact_information_sets * sizeof(std::uint16_t) &&
              summary.aggregation_weight_bytes == 0U && summary.decision_offset_bytes > 0U &&
              summary.bucket_member_offset_bytes > 0U &&
              summary.bucket_member_index_bytes ==
                  summary.exact_information_sets * sizeof(std::uint16_t) &&
              summary.mapping_bytes ==
                  summary.exact_to_abstract_bytes + summary.aggregation_weight_bytes +
                      summary.decision_offset_bytes + summary.bucket_member_offset_bytes +
                      summary.bucket_member_index_bytes &&
              !summary.perfect_recall_verified && !summary.checkpoint_identity_preserved &&
              !summary.solver_supported && summary.fingerprint.starts_with("fnv1a64:"),
          "coarse mapping publishes its complete byte model and unsupported solve status");

  bool observed_collision = false;
  std::vector<std::uint8_t> seen(static_cast<std::size_t>(summary.abstract_information_sets), 0U);
  for (std::uint64_t exact = 0U; exact < summary.exact_information_sets; ++exact) {
    const auto entry = gtosd::prepared_postflop_card_abstraction_entry(*first.value(), exact);
    require(entry.has_value() && entry.value().exact_information_set == exact &&
                entry.value().abstract_information_set < summary.abstract_information_sets &&
                entry.value().aggregation_weight_basis_points == 10'000U,
            "every exact infoset has a valid weighted coarse mapping entry");
    auto &bucket_seen = seen[static_cast<std::size_t>(entry.value().abstract_information_set)];
    observed_collision = observed_collision || bucket_seen != 0U;
    bucket_seen = 1U;
  }
  require(observed_collision &&
              std::ranges::all_of(seen, [](const auto value) { return value != 0U; }),
          "coarse mapping merges at least one hand and covers every abstract bucket");

  const auto weighted_config = make_small_config();
  const auto weighted_ranges = make_weighted_ranges(weighted_config);
  const auto weighted_tree =
      gtosd::prepare_postflop_tree(weighted_config, weighted_ranges, true, true, false, policy);
  require(weighted_tree.has_value(), "fractional ranges prepare with made-hand buckets");
  const auto weighted_summary = gtosd::prepared_postflop_card_abstraction(*weighted_tree.value());
  require(weighted_summary.has_value(), "fractional made-hand mapping publishes a summary");
  bool observed_half_weight = false;
  bool observed_full_weight = false;
  for (std::uint64_t exact = 0U; exact < weighted_summary.value().exact_information_sets; ++exact) {
    const auto entry =
        gtosd::prepared_postflop_card_abstraction_entry(*weighted_tree.value(), exact);
    require(entry.has_value() && (entry.value().aggregation_weight_basis_points == 5'000U ||
                                  entry.value().aggregation_weight_basis_points == 10'000U),
            "fractional mapping entry derives its weight from the canonical range");
    observed_half_weight =
        observed_half_weight || entry.value().aggregation_weight_basis_points == 5'000U;
    observed_full_weight =
        observed_full_weight || entry.value().aggregation_weight_basis_points == 10'000U;
  }
  require(observed_half_weight && observed_full_weight,
          "fractional mapping preserves both weighted and full-range players");
  const auto weighted_probe =
      gtosd::run_postflop_bucket_traversal_probe(*weighted_tree.value(), 8U);
  require(weighted_probe.has_value() &&
              weighted_probe.value().maximum_oracle_deviation <= bucket_oracle_tolerance,
          "fractional bucket traversal remains equivalent to the ProductionDcfr update oracle");

  gtosd::PostflopSolveOptions options;
  options.iterations = 1U;
  options.certification_interval = 1U;
  options.algorithm = gtosd::PostflopAlgorithm::ProductionDcfr;
  options.state_precision = gtosd::PostflopStatePrecision::ScaledUint16RegretStrategy;
  options.dcfr_average_exponent = 3.0;
  options.card_abstraction = policy;
  const auto solve = gtosd::solve_postflop_exact(*first.value(), options);
  require(!solve && solve.error() == gtosd::PostflopSolverError::InvalidConfiguration,
          "coarse mapping cannot enter traversal before weighted bucket updates exist");

  const auto first_probe = gtosd::run_postflop_bucket_traversal_probe(*first.value(), 32U);
  const auto second_probe = gtosd::run_postflop_bucket_traversal_probe(*second.value(), 32U);
  require(first_probe.has_value() && second_probe.has_value(),
          "fixed-river made-hand buckets execute in the isolated Float64 traversal");
  require(first_probe.value().completed_iterations == 32U &&
              first_probe.value().state_bytes ==
                  summary.abstract_action_entries * 2U * sizeof(double),
          "bucket traversal reports its iteration and abstract-state byte count");
  std::ostringstream oracle_message;
  oracle_message << "bucket traversal oracle deviation: " << std::setprecision(17)
                 << first_probe.value().maximum_oracle_deviation;
  require(first_probe.value().maximum_oracle_deviation <= bucket_oracle_tolerance,
          oracle_message.str());
  require(first_probe.value().oracle_updates_checked > 0U &&
              first_probe.value().oracle_updates_total >=
                  first_probe.value().oracle_updates_checked,
          "bucket traversal reports bounded oracle coverage");
  require(first_probe.value().state_fingerprint == second_probe.value().state_fingerprint &&
              first_probe.value().certification.profile_value_antes ==
                  second_probe.value().certification.profile_value_antes &&
              first_probe.value().certification.best_response_value_antes ==
                  second_probe.value().certification.best_response_value_antes,
          "bucket traversal state and original-game certification are deterministic");
  require(std::isfinite(first_probe.value().certification.normalized_nash_conv) &&
              first_probe.value().certification.normalized_nash_conv >= 0.0 &&
              std::abs(first_probe.value().certification.expected_payoff_sum_antes) <= 1.0e-10,
          "lifted coarse strategy is certified in the original zero-sum river game");
  const auto partial_probe = gtosd::run_postflop_bucket_traversal_probe(*first.value(), 16U);
  require(partial_probe.has_value(), "partial bucket traversal produces a checkpoint");
  const auto checkpoint_path =
      std::filesystem::current_path() / "phase10_postflop_bucket_checkpoint.bin";
  std::error_code ignored;
  std::filesystem::remove(checkpoint_path, ignored);
  require(gtosd::save_postflop_bucket_checkpoint(partial_probe.value().checkpoint,
                                                 checkpoint_path.string())
              .has_value(),
          "bucket checkpoint saves atomically");
  const auto restored = gtosd::load_postflop_bucket_checkpoint(checkpoint_path.string());
  require(restored.has_value() && restored.value() == partial_probe.value().checkpoint,
          "bucket checkpoint round-trips bit-for-bit with both fingerprints");
  const auto resumed =
      gtosd::run_postflop_bucket_traversal_probe(*first.value(), 32U, &restored.value());
  require(resumed.has_value() && resumed.value().checkpoint == first_probe.value().checkpoint &&
              resumed.value().state_fingerprint == first_probe.value().state_fingerprint &&
              resumed.value().certification.profile_value_antes ==
                  first_probe.value().certification.profile_value_antes &&
              resumed.value().certification.best_response_value_antes ==
                  first_probe.value().certification.best_response_value_antes,
          "bucket checkpoint resume is byte-equivalent to a continuous run");
  auto mismatched = restored.value();
  mismatched.abstraction_fingerprint.push_back('x');
  const auto rejected_resume =
      gtosd::run_postflop_bucket_traversal_probe(*first.value(), 32U, &mismatched);
  require(!rejected_resume &&
              rejected_resume.error() == gtosd::PostflopSolverError::CheckpointMismatch,
          "bucket resume rejects an abstraction fingerprint mismatch");
  {
    std::fstream corrupt(checkpoint_path, std::ios::binary | std::ios::in | std::ios::out);
    require(static_cast<bool>(corrupt), "bucket checkpoint opens for corruption test");
    const char invalid_magic = 'X';
    corrupt.write(&invalid_magic, 1);
  }
  require(!gtosd::load_postflop_bucket_checkpoint(checkpoint_path.string()),
          "bucket checkpoint corruption is rejected");
  std::filesystem::remove(checkpoint_path, ignored);
  std::cout << "POSTFLOP_COARSE_TRAVERSAL iterations=" << first_probe.value().completed_iterations
            << " state_bytes=" << first_probe.value().state_bytes
            << " traversal_seconds=" << first_probe.value().traversal_seconds
            << " operational_seconds=" << first_probe.value().operational_seconds
            << " normalized_nash_conv=" << first_probe.value().certification.normalized_nash_conv
            << " oracle_deviation=" << first_probe.value().maximum_oracle_deviation
            << " oracle_updates_checked=" << first_probe.value().oracle_updates_checked
            << " oracle_updates_total=" << first_probe.value().oracle_updates_total
            << " state_fingerprint=" << first_probe.value().state_fingerprint << '\n';
}

void test_postflop_made_hand_mapping_on_frozen_development_and_validation() {
  constexpr double bucket_oracle_tolerance = 1.0e-10;
  const std::array<std::pair<const char *, const char *>, 2> fixtures{{
      {"D-RIVER-CHECK-001", "tests/fixtures/postflop_check_only.json"},
      {"V-RIVER-BET-001", "tests/fixtures/postflop_river_bet.json"},
  }};
  gtosd::PostflopCardAbstractionPolicy policy;
  policy.mode = gtosd::PostflopCardAbstractionMode::MadeHandValue;
  std::array<std::string, 2> fingerprints;
  for (std::size_t fixture = 0U; fixture < fixtures.size(); ++fixture) {
    const auto config = load_tree_config_fixture(fixtures[fixture].second);
    const auto ranges = gtosd::make_uniform_postflop_ranges();
    const auto started = std::chrono::steady_clock::now();
    const auto prepared = gtosd::prepare_postflop_tree(config, ranges, true, true, false, policy);
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    require(prepared.has_value(), "frozen D/V fixture builds a coarse mapping");
    const auto summary = gtosd::prepared_postflop_card_abstraction(*prepared.value());
    require(summary.has_value() && summary.value().exact_information_sets > 0U &&
                summary.value().abstract_information_sets > 0U &&
                summary.value().abstract_information_sets <
                    summary.value().exact_information_sets &&
                summary.value().abstract_action_entries < summary.value().exact_action_entries &&
                summary.value().mapping_bytes > 0U && !summary.value().solver_supported,
            "D/V coarse mapping reduces state shape but remains feasibility-only");
    const auto initial_probe = gtosd::run_postflop_bucket_traversal_probe(*prepared.value(), 1U);
    const auto trained_probe = gtosd::run_postflop_bucket_traversal_probe(*prepared.value(), 32U);
    require(initial_probe.has_value(),
            initial_probe.has_value()
                ? "D/V one-iteration coarse traversal succeeds"
                : std::string{"D/V one-iteration coarse traversal failed: "} +
                      gtosd::postflop_solver_error_name(initial_probe.error()));
    require(trained_probe.has_value(),
            trained_probe.has_value()
                ? "D/V trained coarse traversal succeeds"
                : std::string{"D/V trained coarse traversal failed: "} +
                      gtosd::postflop_solver_error_name(trained_probe.error()));
    require(trained_probe.value().state_bytes ==
                    summary.value().abstract_action_entries * 2U * sizeof(double) &&
                trained_probe.value().maximum_oracle_deviation <= bucket_oracle_tolerance &&
                trained_probe.value().oracle_updates_checked > 0U &&
                trained_probe.value().oracle_updates_total >=
                    trained_probe.value().oracle_updates_checked,
            "D/V coarse traversal uses abstract Float64 state and matches the update oracle");
    require(trained_probe.value().traversal_owned_capacity_bytes > 0U,
            "D/V coarse traversal reports owned scratch capacity");
    require(trained_probe.value().state_update_passes == 64U,
            "D/V coarse traversal reports both alternating player passes");
    require(trained_probe.value().traversal_work_counters.visited_nodes > 0U,
            "D/V coarse traversal reports training work counters");
    require(trained_probe.value().layout_preparation_seconds >= 0.0 &&
                trained_probe.value().abstraction_preparation_seconds >= 0.0 &&
                trained_probe.value().layout_preparation_seconds +
                        trained_probe.value().abstraction_preparation_seconds <=
                    trained_probe.value().preparation_seconds + 1.0e-9 &&
                trained_probe.value().rank_preparation_seconds >= 0.0 &&
                trained_probe.value().state_initialization_seconds >= 0.0 &&
                trained_probe.value().traversal_initialization_seconds >= 0.0 &&
                trained_probe.value().rank_preparation_seconds +
                        trained_probe.value().state_initialization_seconds +
                        trained_probe.value().traversal_initialization_seconds <=
                    trained_probe.value().setup_seconds + 1.0e-9 &&
                trained_probe.value().finalization_seconds >= 0.0,
            "D/V coarse traversal reports additive R3 phase timings");
    require(std::isfinite(initial_probe.value().certification.normalized_nash_conv) &&
                std::isfinite(trained_probe.value().certification.normalized_nash_conv) &&
                trained_probe.value().certification.normalized_nash_conv <=
                    initial_probe.value().certification.normalized_nash_conv + 1.0e-12 &&
                std::abs(trained_probe.value().certification.expected_payoff_sum_antes) <= 1.0e-10,
            "D/V lifted strategy is zero-sum and does not regress original-game NashConv");
    const auto exact_prepare_started = std::chrono::steady_clock::now();
    const auto exact_prepared = gtosd::prepare_postflop_tree(config, ranges, true, true, false);
    const double exact_preparation_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - exact_prepare_started)
            .count();
    gtosd::PostflopProductionSolveRequest exact_request;
    exact_request.iterations = 32U;
    const auto exact_options = gtosd::resolve_postflop_production_options(exact_request);
    require(exact_prepared.has_value() && exact_options.has_value(),
            "D/V exact ProductionDcfr comparator prepares");
    const auto exact_solve =
        gtosd::solve_postflop_exact(*exact_prepared.value(), exact_options.value());
    require(exact_solve.has_value() && !exact_solve.value().convergence.empty(),
            "D/V exact ProductionDcfr comparator solves and certifies");
    const auto &exact_certification = exact_solve.value().convergence.back();
    require(std::isfinite(exact_certification.normalized_nash_conv) &&
                exact_certification.normalized_nash_conv <= 0.01 &&
                trained_probe.value().certification.normalized_nash_conv <= 0.01,
            "D/V exact and lifted bucket strategies satisfy the original-game one-percent gate");
    double maximum_profile_delta = 0.0;
    double maximum_best_response_delta = 0.0;
    for (std::uint8_t player = 0U; player < 2U; ++player) {
      maximum_profile_delta =
          std::max(maximum_profile_delta,
                   std::abs(trained_probe.value().certification.profile_value_antes[player] -
                            exact_certification.profile_value_antes[player]));
      maximum_best_response_delta =
          std::max(maximum_best_response_delta,
                   std::abs(trained_probe.value().certification.best_response_value_antes[player] -
                            exact_certification.best_response_value_antes[player]));
    }
    fingerprints[fixture] = summary.value().fingerprint;
    std::cout << "POSTFLOP_COARSE_MAPPING fixture=" << fixtures[fixture].first
              << " exact_infosets=" << summary.value().exact_information_sets
              << " abstract_infosets=" << summary.value().abstract_information_sets
              << " exact_actions=" << summary.value().exact_action_entries
              << " abstract_actions=" << summary.value().abstract_action_entries
              << " mapping_bytes=" << summary.value().mapping_bytes
              << " cold_build_seconds=" << seconds
              << " layout_preparation_seconds="
              << trained_probe.value().layout_preparation_seconds
              << " abstraction_preparation_seconds="
              << trained_probe.value().abstraction_preparation_seconds
              << " setup_seconds=" << trained_probe.value().setup_seconds
              << " rank_preparation_seconds=" << trained_probe.value().rank_preparation_seconds
              << " state_initialization_seconds="
              << trained_probe.value().state_initialization_seconds
              << " traversal_initialization_seconds="
              << trained_probe.value().traversal_initialization_seconds
              << " probe_seconds=" << trained_probe.value().traversal_seconds
              << " certification_seconds=" << trained_probe.value().certification_seconds
              << " finalization_seconds=" << trained_probe.value().finalization_seconds
              << " operational_seconds=" << trained_probe.value().operational_seconds
              << " iteration1_nash_conv="
              << initial_probe.value().certification.normalized_nash_conv
              << " iteration32_nash_conv="
              << trained_probe.value().certification.normalized_nash_conv
              << " exact_iteration32_nash_conv=" << exact_certification.normalized_nash_conv
              << " maximum_profile_delta_antes=" << maximum_profile_delta
              << " maximum_best_response_delta_antes=" << maximum_best_response_delta
              << " exact_solver_seconds=" << exact_solve.value().timings.run_solver_seconds
              << " exact_preparation_seconds=" << exact_preparation_seconds
              << " exact_operational_seconds="
              << exact_preparation_seconds + exact_solve.value().timings.run_solver_seconds
              << " probe_state_bytes=" << trained_probe.value().state_bytes
              << " traversal_owned_capacity_bytes="
              << trained_probe.value().traversal_owned_capacity_bytes
              << " state_update_passes=" << trained_probe.value().state_update_passes
              << " strategy_reset_passes=" << trained_probe.value().strategy_reset_passes
              << " traversal_visited_nodes="
              << trained_probe.value().traversal_work_counters.visited_nodes
              << " traversal_decision_nodes="
              << trained_probe.value().traversal_work_counters.decision_node_evaluations
              << " traversal_chance_nodes="
              << trained_probe.value().traversal_work_counters.chance_node_evaluations
              << " traversal_chance_outcomes="
              << trained_probe.value().traversal_work_counters.chance_outcome_evaluations
              << " traversal_terminals="
              << trained_probe.value().traversal_work_counters.terminal_evaluations
              << " traversal_showdowns="
              << trained_probe.value().traversal_work_counters.showdown_terminal_evaluations
              << " traversal_regret_updates="
              << trained_probe.value().traversal_work_counters.regret_update_entries
              << " traversal_strategy_updates="
              << trained_probe.value().traversal_work_counters.strategy_update_entries
              << " exact_visited_nodes=" << exact_solve.value().work_counters.visited_nodes
              << " exact_showdowns="
              << exact_solve.value().work_counters.showdown_terminal_evaluations
              << " oracle_deviation=" << trained_probe.value().maximum_oracle_deviation
              << " oracle_updates_checked=" << trained_probe.value().oracle_updates_checked
              << " oracle_updates_total=" << trained_probe.value().oracle_updates_total
              << " fingerprint=" << summary.value().fingerprint << '\n';
  }
  require(fingerprints[0] != fingerprints[1],
          "development and validation mappings retain distinct game identities");
}

void test_r6_production_rbp_read_only_on_development_and_validation() {
  const std::array<std::pair<const char *, const char *>, 2> fixtures{{
      {"D-RIVER-CHECK-001", "tests/fixtures/postflop_check_only.json"},
      {"V-RIVER-BET-001", "tests/fixtures/postflop_river_bet.json"},
  }};
  for (const auto &[fixture, path] : fixtures) {
    const auto config = load_tree_config_fixture(path);
    const auto ranges = gtosd::make_uniform_postflop_ranges();
    const auto prepared = gtosd::prepare_postflop_tree(config, ranges, true, true, false);
    gtosd::PostflopProductionSolveRequest request;
    request.iterations = 32U;
    auto options = gtosd::resolve_postflop_production_options(request);
    require(prepared.has_value() && options.has_value(),
            "R6 ProductionDcfr telemetry fixture prepares");
    gtosd::RbpReadOnlyTelemetry telemetry;
    std::vector<gtosd::RbpReadOnlySnapshot> snapshots;
    options.value().checkpoint_callback =
        [&telemetry, &snapshots, &prepared](const gtosd::PostflopCertification &,
                                            const gtosd::PostflopCheckpoint &checkpoint) {
          auto observed = telemetry.observe(*prepared.value(), checkpoint);
          if (!observed) {
            return false;
          }
          snapshots.push_back(std::move(observed.value()));
          return true;
        };
    const auto solved = gtosd::solve_postflop_exact(*prepared.value(), options.value());
    require(solved.has_value() && snapshots.size() == 2U &&
                snapshots[0].iteration == 20U && snapshots[1].iteration == 32U,
            "R6 ProductionDcfr telemetry observes interval and final checkpoints");
    for (const auto &snapshot : snapshots) {
      const auto candidates = snapshot.players[0].original_formula_candidates +
                              snapshot.players[1].original_formula_candidates;
      const auto persistent = snapshot.players[0].persistent_candidates +
                              snapshot.players[1].persistent_candidates;
      const auto negative = snapshot.players[0].negative_regret_actions +
                            snapshot.players[1].negative_regret_actions;
      const auto structural_nodes = snapshot.players[0].structural_upper_bound.public_nodes +
                                    snapshot.players[1].structural_upper_bound.public_nodes;
      std::cout << "R6_RBP_READ_ONLY fixture=" << fixture
                << " iteration=" << snapshot.iteration << " negative_regrets=" << negative
                << " original_formula_candidates=" << candidates
                << " persistent_candidates=" << persistent
                << " structural_upper_bound_nodes=" << structural_nodes
                << " metadata_bytes=" << snapshot.metadata_bytes
                << " exact_zero_reach_available="
                << (snapshot.exact_zero_counterfactual_reach_available ? "true" : "false")
                << '\n';
    }
  }
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
              at_one.regret_discount_iteration == 0U && at_one.average_strategy_weight == 1.0,
          "production DCFR initializes the first epoch");
  require(at_two.reset_average_strategy && at_two.epoch_start_iteration == 2U &&
              at_two.regret_discount_iteration == 1U && at_two.average_strategy_weight == 1.0,
          "production DCFR resets at iteration 2 on the production alpha clock");
  require(!at_three.reset_average_strategy && at_three.epoch_index == 1U &&
              at_three.regret_discount_iteration == 2U && at_three.average_strategy_weight == 8.0,
          "production DCFR uses cubic weights before the clock cutover");
  require(at_five.reset_average_strategy && at_five.epoch_start_iteration == 5U &&
              at_seventeen.reset_average_strategy && at_seventeen.epoch_start_iteration == 17U &&
              at_sixty_five.reset_average_strategy && at_sixty_five.epoch_start_iteration == 65U,
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
  options.dcfr_average_exponent = 3.0;
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

void test_shared_production_profile_and_checkpoint_round_trip() {
  gtosd::PostflopProductionSolveRequest request;
  request.iterations = 20U;
  const auto resolved = gtosd::resolve_postflop_production_options(request);
  require(resolved.has_value(), "shared production profile resolves");
  const auto &options = resolved.value();
  require(options.algorithm == gtosd::PostflopAlgorithm::ProductionDcfr &&
              options.state_precision ==
                  gtosd::PostflopStatePrecision::ScaledUint16RegretStrategy &&
              options.averaging_delay == 0U && options.certification_interval == 20U &&
              options.strict_target && options.parallel_action_depth == 7U &&
              options.dcfr_positive_regret_exponent == 1.5 && options.dcfr_average_exponent == 3.0,
          "shared production profile freezes algorithm, codec, schedule, gate and workers");

  gtosd::PostflopCheckpoint legacy;
  require(!gtosd::resolve_postflop_production_options(request, &legacy) &&
              gtosd::resolve_postflop_production_options(request, &legacy).error() ==
                  gtosd::PostflopSolverError::CheckpointMismatch,
          "shared production profile rejects ambiguous CFR+ resume");
  auto invalid_request = request;
  invalid_request.memory_backend = gtosd::MemoryPrototype::OutOfCore;
  require(!gtosd::resolve_postflop_production_options(invalid_request),
          "shared production profile rejects unsupported Float64 out-of-core backend");
  auto incompatible_production = legacy;
  incompatible_production.algorithm = gtosd::PostflopAlgorithm::ProductionDcfr;
  incompatible_production.state_precision =
      gtosd::PostflopStatePrecision::ScaledUint16RegretStrategy;
  incompatible_production.averaging_delay = 0U;
  incompatible_production.dcfr_positive_regret_exponent = 1.5;
  incompatible_production.dcfr_average_exponent = 2.0;
  require(!gtosd::resolve_postflop_production_options(request, &incompatible_production),
          "shared production profile rejects a ProductionDcfr checkpoint outside 1.5/0/3");

  const auto config = make_small_config();
  auto prepared =
      gtosd::prepare_postflop_tree(config, gtosd::make_uniform_postflop_ranges(), true, true, false)
          .value();
  const auto state_bytes = gtosd::prepared_postflop_solver_state_bytes(
      *prepared, gtosd::PostflopStatePrecision::ScaledUint16RegretStrategy);
  const auto solved = gtosd::solve_postflop_exact(*prepared, options);
  require(solved.has_value(), "shared production profile solves a reduced game");
  require(state_bytes.has_value() &&
              state_bytes.value() ==
                  solved.value().checkpoint.cumulative_regret_uint16.size() * 2U *
                          sizeof(std::uint16_t) +
                      solved.value().checkpoint.regret_node_scale.size() * 2U * sizeof(float),
          "production state byte model matches the materialized checkpoint payload");

  const auto path = std::filesystem::current_path() / "phase10_production_profile.chk";
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
  require(gtosd::save_postflop_checkpoint(solved.value().checkpoint, path.string()).has_value(),
          "ProductionDcfr checkpoint saves in the versioned binary format");
  const auto loaded = gtosd::load_postflop_checkpoint(path.string());
  require(loaded.has_value() &&
              loaded.value().algorithm == gtosd::PostflopAlgorithm::ProductionDcfr &&
              loaded.value().state_precision ==
                  gtosd::PostflopStatePrecision::ScaledUint16RegretStrategy &&
              loaded.value().dcfr_positive_regret_exponent == 1.5 &&
              loaded.value().dcfr_average_exponent == 3.0 &&
              loaded.value().cumulative_regret_uint16 ==
                  solved.value().checkpoint.cumulative_regret_uint16 &&
              loaded.value().cumulative_strategy_uint16 ==
                  solved.value().checkpoint.cumulative_strategy_uint16 &&
              loaded.value().regret_node_scale == solved.value().checkpoint.regret_node_scale &&
              loaded.value().strategy_node_scale == solved.value().checkpoint.strategy_node_scale,
          "ProductionDcfr checkpoint restores algorithm identity and state byte-for-byte");
  auto invalid_checkpoint = solved.value().checkpoint;
  invalid_checkpoint.dcfr_average_exponent = 2.0;
  require(!gtosd::save_postflop_checkpoint(invalid_checkpoint, path.string()),
          "ProductionDcfr checkpoint writer rejects state outside the fixed 1.5/0/3 contract");
  std::filesystem::remove(path, ignored);
}

void test_explicit_resident_working_set_budget_is_byte_exact() {
  const auto config = make_small_config();
  const auto ranges = gtosd::make_uniform_postflop_ranges();
  gtosd::PostflopSolveOptions resident_options;
  resident_options.iterations = 4U;
  resident_options.certification_interval = 4U;
  resident_options.algorithm = gtosd::PostflopAlgorithm::ProductionDcfr;
  resident_options.state_precision = gtosd::PostflopStatePrecision::ScaledUint16RegretStrategy;
  resident_options.dcfr_average_exponent = 3.0;

  auto resident_tree = gtosd::prepare_postflop_tree(config, ranges, true, true, false).value();
  const auto exact_state_bytes = gtosd::prepared_postflop_solver_state_bytes(
      *resident_tree, gtosd::PostflopStatePrecision::ScaledUint16RegretStrategy);
  require(exact_state_bytes.has_value() && exact_state_bytes.value() > 1U,
          "production residency dispatch has a structural byte estimate");
  const auto resident = gtosd::solve_postflop_exact(*resident_tree, resident_options);
  require(resident.has_value() && resident.value().checkpoint.runtime_state == nullptr,
          "unbounded exact state remains resident");

  auto budgeted_options = resident_options;
  budgeted_options.resident_working_set_budget_bytes = exact_state_bytes.value() - 1U;
  auto budgeted_tree = gtosd::prepare_postflop_tree(config, ranges, true, true, false).value();
  const auto budgeted = gtosd::solve_postflop_exact(*budgeted_tree, budgeted_options);
  require(budgeted.has_value() && budgeted.value().checkpoint.runtime_state != nullptr &&
              budgeted.value().checkpoint.cumulative_regret_uint16.empty() &&
              budgeted.value().checkpoint.cumulative_strategy_uint16.empty() &&
              budgeted.value().checkpoint.regret_node_scale.empty() &&
              budgeted.value().checkpoint.strategy_node_scale.empty(),
          "tight working-set budget selects only page-backed exact state");

  auto boundary_options = resident_options;
  boundary_options.resident_working_set_budget_bytes = exact_state_bytes.value();
  auto boundary_tree = gtosd::prepare_postflop_tree(config, ranges, true, true, false).value();
  const auto boundary = gtosd::solve_postflop_exact(*boundary_tree, boundary_options);
  require(boundary.has_value() && boundary.value().checkpoint.runtime_state != nullptr,
          "a state-only byte estimate is not misread as a total process working-set budget");

  auto materialized = budgeted.value().checkpoint;
  require(gtosd::materialize_postflop_checkpoint_state(materialized).has_value() &&
              materialized.runtime_state == nullptr,
          "page-backed state materializes explicitly for persistence");
  require(materialized.cumulative_regret_uint16 ==
                  resident.value().checkpoint.cumulative_regret_uint16 &&
              materialized.cumulative_strategy_uint16 ==
                  resident.value().checkpoint.cumulative_strategy_uint16 &&
              materialized.regret_node_scale == resident.value().checkpoint.regret_node_scale &&
              materialized.strategy_node_scale == resident.value().checkpoint.strategy_node_scale,
          "resident and page-backed production DCFR states are byte-identical");
  auto boundary_materialized = boundary.value().checkpoint;
  require(gtosd::materialize_postflop_checkpoint_state(boundary_materialized).has_value(),
          "state-only boundary dispatch remains explicitly materializable");
  require(boundary_materialized.cumulative_regret_uint16 ==
                  resident.value().checkpoint.cumulative_regret_uint16 &&
              boundary_materialized.cumulative_strategy_uint16 ==
                  resident.value().checkpoint.cumulative_strategy_uint16 &&
              boundary_materialized.regret_node_scale ==
                  resident.value().checkpoint.regret_node_scale &&
              boundary_materialized.strategy_node_scale ==
                  resident.value().checkpoint.strategy_node_scale,
          "residency selection near the structural threshold does not alter the DCFR trajectory");

  const auto temporary_backing =
      std::filesystem::current_path() / "phase10_scaled_runtime_state.tmp";
  std::error_code ignored;
  std::filesystem::remove(temporary_backing, ignored);
  {
    auto file_backed_options = budgeted_options;
    file_backed_options.backing_file = temporary_backing.string();
    auto file_backed_tree =
        gtosd::prepare_postflop_tree(config, ranges, true, true, false).value();
    const auto file_backed =
        gtosd::solve_postflop_exact(*file_backed_tree, file_backed_options);
    require(file_backed.has_value() &&
                file_backed.value().checkpoint.runtime_state != nullptr &&
                std::filesystem::exists(temporary_backing),
            "explicit tight-budget backing file owns the scaled runtime mapping");
    auto file_materialized = file_backed.value().checkpoint;
    require(gtosd::materialize_postflop_checkpoint_state(file_materialized).has_value() &&
                file_materialized.cumulative_regret_uint16 ==
                    resident.value().checkpoint.cumulative_regret_uint16 &&
                file_materialized.cumulative_strategy_uint16 ==
                    resident.value().checkpoint.cumulative_strategy_uint16 &&
                file_materialized.regret_node_scale ==
                    resident.value().checkpoint.regret_node_scale &&
                file_materialized.strategy_node_scale ==
                    resident.value().checkpoint.strategy_node_scale,
            "temporary file-backed and resident ProductionDcfr states are byte-identical");
  }
  require(!std::filesystem::exists(temporary_backing),
          "temporary scaled runtime backing is deleted when its checkpoint owner closes");

  const auto archive = gtosd::make_postflop_solution(config, ranges, materialized,
                                                     budgeted.value().convergence.back());
  require(archive.has_value(), "materialized page-backed state is persistable");
  require(budgeted.value().convergence.back().profile_value_antes ==
                  resident.value().convergence.back().profile_value_antes &&
              budgeted.value().convergence.back().best_response_value_antes ==
                  resident.value().convergence.back().best_response_value_antes &&
              budgeted.value().convergence.back().nash_conv_antes ==
                  resident.value().convergence.back().nash_conv_antes,
          "resident and page-backed exact certifications are identical");
}

void test_six_action_river_dispatch_reached_from_turn() {
  auto config = gtosd::make_postflop_benchmark_config(gtosd::PostflopBenchmark::PfF1).value();
  std::uint64_t board_mask = 0U;
  for (const auto card : config.flop) {
    board_mask |= card.mask();
  }
  config.turn.reset();
  config.river.reset();
  for (std::uint8_t card = 0U; card < 36U; ++card) {
    const auto candidate = gtosd::CardId::from_index(card).value();
    if ((candidate.mask() & board_mask) == 0U) {
      config.turn = candidate;
      board_mask |= candidate.mask();
      break;
    }
  }
  config.initial_pot = gtosd::Money::from_antes(4).value();
  config.effective_stack = gtosd::Money::from_antes(38).value();
  const std::array sizes{gtosd::PotPercentage::from_basis_points(3'300).value(),
                         gtosd::PotPercentage::from_basis_points(6'600).value(),
                         gtosd::PotPercentage::from_basis_points(12'000).value()};
  for (std::size_t street = 1U; street < config.streets.size(); ++street) {
    for (auto &player : config.streets[street].players) {
      for (auto &scenario : player) {
        scenario.aggressive_sizes.assign(sizes.begin(), sizes.end());
        scenario.raise_depth = 4U;
        scenario.all_in_mode = gtosd::AllInMode::Add;
        scenario.all_in_threshold =
            gtosd::PotPercentage::from_basis_points(100'000).value();
        scenario.minimum_bet = gtosd::Money::from_antes(1).value();
      }
    }
  }
  const auto public_tree = gtosd::build_public_tree(config);
  require(public_tree.has_value() &&
              (public_tree.value()
                   .stats.decision_nodes_by_street_player_action[2][0][6] > 0U ||
               public_tree.value()
                       .stats.decision_nodes_by_street_player_action[2][1][6] > 0U),
          "Turn-start regression contains a reachable six-action River decision");

  gtosd::PostflopRanges ranges;
  const auto full = gtosd::RangeWeight::from_basis_points(10'000).value();
  const auto combos = gtosd::all_combos();
  std::optional<std::size_t> first;
  std::optional<std::size_t> second;
  for (std::size_t index = 0U; index < combos.size() && !second; ++index) {
    const auto mask = combos[index].first.mask() | combos[index].second.mask();
    if ((mask & board_mask) != 0U) {
      continue;
    }
    if (!first) {
      first = index;
      continue;
    }
    const auto first_mask = combos[*first].first.mask() | combos[*first].second.mask();
    if ((mask & first_mask) == 0U) {
      second = index;
    }
  }
  require(first.has_value() && second.has_value(),
          "Turn-start regression finds a compatible private deal");
  ranges.players[0][*first] = full;
  ranges.players[1][*second] = full;

  gtosd::PostflopSolveOptions options;
  options.iterations = 1U;
  options.certification_interval = 1U;
  options.algorithm = gtosd::PostflopAlgorithm::ProductionDcfr;
  options.state_precision = gtosd::PostflopStatePrecision::ScaledUint16RegretStrategy;
  options.dcfr_average_exponent = 3.0;
  const auto solved = gtosd::solve_postflop_exact(config, ranges, options);
  require(solved.has_value() && solved.value().checkpoint.completed_iterations == 1U &&
              !solved.value().convergence.empty(),
          "specialized River traversal supports six legal actions from a Turn root");
}

void test_solver_memory_accounting_is_observational() {
  const auto config = make_small_config();
  const auto ranges = gtosd::make_uniform_postflop_ranges();
  gtosd::PostflopSolveOptions enabled_options;
  enabled_options.iterations = 4U;
  enabled_options.certification_interval = 4U;
  enabled_options.algorithm = gtosd::PostflopAlgorithm::ProductionDcfr;
  enabled_options.state_precision = gtosd::PostflopStatePrecision::ScaledUint16RegretStrategy;
  enabled_options.dcfr_average_exponent = 3.0;

  auto enabled_tree = gtosd::prepare_postflop_tree(config, ranges, true, true, false).value();
  const auto enabled = gtosd::solve_postflop_exact(*enabled_tree, enabled_options);
  require(enabled.has_value() && enabled.value().solver_memory_accounting_complete &&
              enabled.value().solver_memory_accounting.has_value(),
          "enabled solver memory accounting returns a complete ledger");
  const auto &snapshot = *enabled.value().solver_memory_accounting;
  std::uint64_t current_logical_sum = 0U;
  std::uint64_t current_allocated_sum = 0U;
  for (const auto &category : snapshot.categories) {
    current_logical_sum += category.current_logical_bytes;
    current_allocated_sum += category.current_allocated_bytes;
  }
  require(current_logical_sum == snapshot.current_logical_bytes &&
              current_allocated_sum == snapshot.current_allocated_bytes &&
              snapshot.maximum_allocated_bytes >= snapshot.maximum_logical_bytes &&
              snapshot.maximum_logical_bytes >= enabled.value().checkpoint.action_count,
          "ledger category totals and peaks satisfy accounting invariants");

  auto disabled_options = enabled_options;
  disabled_options.enable_detailed_memory_accounting = false;
  auto disabled_tree = gtosd::prepare_postflop_tree(config, ranges, true, true, false).value();
  const auto disabled = gtosd::solve_postflop_exact(*disabled_tree, disabled_options);
  require(disabled.has_value() && !disabled.value().solver_memory_accounting.has_value() &&
              !disabled.value().solver_memory_accounting_complete,
          "disabled accounting returns no detailed telemetry");
  require(enabled.value().checkpoint.game_fingerprint ==
                  disabled.value().checkpoint.game_fingerprint &&
              enabled.value().checkpoint.cumulative_regret_uint16 ==
                  disabled.value().checkpoint.cumulative_regret_uint16 &&
              enabled.value().checkpoint.cumulative_strategy_uint16 ==
                  disabled.value().checkpoint.cumulative_strategy_uint16 &&
              enabled.value().checkpoint.regret_node_scale ==
                  disabled.value().checkpoint.regret_node_scale &&
              enabled.value().checkpoint.strategy_node_scale ==
                  disabled.value().checkpoint.strategy_node_scale &&
              enabled.value().convergence.back().profile_value_antes ==
                  disabled.value().convergence.back().profile_value_antes &&
              enabled.value().convergence.back().best_response_value_antes ==
                  disabled.value().convergence.back().best_response_value_antes,
          "accounting toggle does not change fingerprint, state, strategy, or exact EVs");
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

int main(const int argc, const char *const argv[]) {
  try {
    if (argc == 2 && std::string{argv[1]} == "--r6-rbp-only") {
      test_r6_production_rbp_read_only_on_development_and_validation();
      std::cout << "phase10 R6-RBP assertions=" << assertions << '\n';
      return 0;
    }
    if (argc == 2 && std::string{argv[1]} == "--bucket-profile-only") {
      test_postflop_made_hand_mapping_on_frozen_development_and_validation();
      std::cout << "phase10 bucket-profile assertions=" << assertions << '\n';
      return 0;
    }
    if (argc == 2 && std::string{argv[1]} == "--bucket-only") {
      test_production_dcfr_bucket_update_oracle();
      test_postflop_made_hand_value_mapping_is_deterministic_and_not_solvable();
      test_postflop_made_hand_mapping_on_frozen_development_and_validation();
      std::cout << "phase10 bucket assertions=" << assertions << '\n';
      return 0;
    }
    test_weighted_reach_and_fingerprint();
    test_range_storage_round_trip();
    test_empty_range_rejected();
    test_production_dcfr_bucket_update_oracle();
    test_postflop_identity_abstraction_is_exact_and_implicit();
    test_postflop_made_hand_value_mapping_is_deterministic_and_not_solvable();
    test_postflop_made_hand_mapping_on_frozen_development_and_validation();
    test_r6_production_rbp_read_only_on_development_and_validation();
    test_architectural_topology_is_read_only_and_disjoint();
    test_hs_dcfr30_schedule_and_resume();
    test_production_dcfr_schedule_and_resume();
    test_shared_production_profile_and_checkpoint_round_trip();
    test_explicit_resident_working_set_budget_is_byte_exact();
    test_six_action_river_dispatch_reached_from_turn();
    test_solver_memory_accounting_is_observational();
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
