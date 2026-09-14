#include "gtosd/preflop/hu_preflop.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

std::uint64_t assertions = 0U;

void require(const bool condition, const std::string_view message) {
  ++assertions;
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

gtosd::HuPreflopSolveOptions test_options() {
  gtosd::HuPreflopSolveOptions options;
  options.iterations = 4U;
  options.evaluation_deals = 8U;
  options.best_response_iterations = 2U;
  options.best_response_evaluation_deals = 8U;
  options.equity_samples_per_bucket = 0U;
  options.seed = 0x5631'3900'0000'0001ULL;
  options.partition_seed = 0x5631'3900'0000'0002ULL;
  options.evaluation_seed = 0x5631'3900'0000'0003ULL;
  options.worker_threads = 1U;
  options.sampling_algorithm = gtosd::HuPreflopSamplingAlgorithm::ExternalSampling;
  options.postflop_representation = gtosd::HuPreflopPostflopRepresentation::ExactPhysical;
  options.use_compiled_betting = true;
  return options;
}

gtosd::HandClassId hand_class_id(const std::string_view name) {
  for (std::uint8_t hand = 0U; hand < gtosd::hu_preflop_hand_class_count; ++hand) {
    if (gtosd::class_name(hand) == name) {
      return hand;
    }
  }
  throw std::runtime_error("unknown test hand class");
}

void require_same_solver_state(const gtosd::HuPreflopSolveResult &without,
                               const gtosd::HuPreflopSolveResult &with) {
  require(without.root_strategy == with.root_strategy, "telemetry preserves root policy");
  require(without.root_current_strategy == with.root_current_strategy,
          "telemetry preserves current root policy");
  require(without.root_cumulative_weighted_regret == with.root_cumulative_weighted_regret,
          "telemetry preserves cumulative regret");
  require(without.root_cumulative_average_weight == with.root_cumulative_average_weight,
          "telemetry preserves cumulative strategy weight");
  require(without.root_information_last_iteration == with.root_information_last_iteration,
          "telemetry preserves information-set counters");
  require(without.root_action_ev_ante == with.root_action_ev_ante,
          "telemetry preserves action EV");
  require(without.root_action_ev_standard_error_ante == with.root_action_ev_standard_error_ante,
          "telemetry preserves action EV standard error");
  require(without.root_action_ev_samples == with.root_action_ev_samples,
          "telemetry preserves action EV sample counters");
  require(without.root_action_advantage_diagnostics == with.root_action_advantage_diagnostics,
          "telemetry preserves root action diagnostics");
  require(without.root_ev_ante == with.root_ev_ante &&
              without.root_ev_standard_error_ante == with.root_ev_standard_error_ante,
          "telemetry preserves root EV");
  require(without.abstract_nashconv_ante == with.abstract_nashconv_ante &&
              without.normalized_abstract_nashconv == with.normalized_abstract_nashconv,
          "telemetry preserves convergence diagnostics");
  require(without.best_response_co_ev_ante == with.best_response_co_ev_ante &&
              without.best_response_btn_ev_ante == with.best_response_btn_ev_ante &&
              without.best_response_co_standard_error_ante ==
                  with.best_response_co_standard_error_ante &&
              without.best_response_btn_standard_error_ante ==
                  with.best_response_btn_standard_error_ante,
          "telemetry preserves best-response EV");
  require(without.information_sets == with.information_sets &&
              without.best_response_information_sets == with.best_response_information_sets &&
              without.postflop_training_iterations == with.postflop_training_iterations &&
              without.preflop_refinement_iterations == with.preflop_refinement_iterations &&
              without.bytes_per_information_set_payload ==
                  with.bytes_per_information_set_payload &&
              without.minimum_blueprint_payload_bytes == with.minimum_blueprint_payload_bytes &&
              without.minimum_best_response_payload_bytes ==
                  with.minimum_best_response_payload_bytes &&
              without.minimum_exported_postflop_policy_payload_bytes ==
                  with.minimum_exported_postflop_policy_payload_bytes &&
              without.numeric_state_payload_bytes == with.numeric_state_payload_bytes &&
              without.numeric_state_budget_bytes == with.numeric_state_budget_bytes &&
              without.winner_cache_hits == with.winner_cache_hits &&
              without.winner_cache_misses == with.winner_cache_misses &&
              without.bucket_cache_peak_entries == with.bucket_cache_peak_entries &&
              without.bucket_cache_evictions == with.bucket_cache_evictions &&
              without.bucket_mapping_visits == with.bucket_mapping_visits &&
              without.bucket_mapping_computations == with.bucket_mapping_computations &&
              without.occupied_distributional_buckets == with.occupied_distributional_buckets &&
              without.average_policy_queries == with.average_policy_queries &&
              without.untrained_average_policy_queries == with.untrained_average_policy_queries &&
              without.postflop_action_value_spread_samples ==
                  with.postflop_action_value_spread_samples &&
              without.worker_threads == with.worker_threads &&
              without.training_batch_iterations == with.training_batch_iterations &&
              without.peak_parallel_updates_per_job == with.peak_parallel_updates_per_job &&
              without.peak_parallel_shadow_updates_per_worker ==
                  with.peak_parallel_shadow_updates_per_worker &&
              without.peak_parallel_scratch_payload_bytes ==
                  with.peak_parallel_scratch_payload_bytes &&
              without.variance_baseline_information_sets ==
                  with.variance_baseline_information_sets &&
              without.variance_baseline_payload_bytes == with.variance_baseline_payload_bytes &&
              without.exact_preflop_all_in_expectation ==
                  with.exact_preflop_all_in_expectation &&
              without.preflop_all_in_equity_table_fingerprint ==
                  with.preflop_all_in_equity_table_fingerprint &&
              without.exact_postflop_all_in_evaluations ==
                  with.exact_postflop_all_in_evaluations &&
              without.exact_postflop_all_in_runouts == with.exact_postflop_all_in_runouts &&
              without.exact_postflop_all_in_cache_hits ==
                  with.exact_postflop_all_in_cache_hits &&
              without.exact_postflop_all_in_cache_misses ==
                  with.exact_postflop_all_in_cache_misses &&
              without.exact_postflop_all_in_cache_peak_entries ==
                  with.exact_postflop_all_in_cache_peak_entries &&
              without.exact_postflop_all_in_cache_evictions ==
                  with.exact_postflop_all_in_cache_evictions &&
              without.compiled_betting_nodes == with.compiled_betting_nodes &&
              without.compiled_betting_bytes == with.compiled_betting_bytes,
          "telemetry preserves deterministic counters");
  require(without.postflop_all_in_expectation_id == with.postflop_all_in_expectation_id &&
              without.chance_sampling_id == with.chance_sampling_id,
          "telemetry preserves estimator identifiers");
  require(without.action_conditioned_telemetry_dropped ==
              with.action_conditioned_telemetry_dropped,
          "telemetry cap accounting is non-mutating");
  require(without.tree_fingerprint == with.tree_fingerprint &&
              without.abstraction_id == with.abstraction_id &&
              without.algorithm_id == with.algorithm_id &&
              without.preflop_blueprint == with.preflop_blueprint &&
              without.postflop_policy == with.postflop_policy,
          "telemetry preserves policy and fingerprints");
}

void validate_telemetry(const gtosd::HuPreflopTree &tree,
                        const gtosd::HuPreflopSolveResult &result) {
  require(result.action_conditioned_telemetry_enabled,
          "telemetry result records the enabled contract");
  require(!result.action_conditioned_telemetry.empty(), "telemetry emits observations");
  for (const auto &row : result.action_conditioned_telemetry) {
    require(row.node_id < tree.nodes.size(), "telemetry node id is in bounds");
    require(row.player < 2U && row.action_id < gtosd::hu_preflop_sampled_postflop_maximum_actions,
            "telemetry actor and action are in bounds");
    require(row.hand_class < gtosd::hu_preflop_hand_class_count,
            "telemetry hand class is in bounds");
    require(row.sample_count > 0U && row.bucket_occupancy == row.sample_count,
            "telemetry sample and occupancy counts are positive and coherent");
    require(std::isfinite(row.physical_combo_mass) &&
                row.physical_combo_mass == static_cast<double>(row.sample_count),
            "telemetry physical combo mass matches observed deals");
    require(std::isfinite(row.public_reach) && std::isfinite(row.own_reach) &&
                row.public_reach >= 0.0 && row.public_reach <= 1.0 && row.own_reach >= 0.0 &&
                row.own_reach <= 1.0,
            "telemetry reach values are finite probabilities");
    require(std::isfinite(row.mean_action_value) &&
                std::isfinite(row.variance_action_value) &&
                std::isfinite(row.standard_error_action_value) &&
                std::isfinite(row.mean_action_advantage),
            "telemetry moments are finite");
    require(row.variance_action_value >= 0.0 && row.standard_error_action_value >= 0.0,
            "telemetry variance and standard error are non-negative");
    require(std::isfinite(row.minimum_action_value) && std::isfinite(row.maximum_action_value) &&
                std::isfinite(row.spread_action_value) && row.maximum_action_value >=
                    row.minimum_action_value && row.spread_action_value >= 0.0 &&
                row.spread_action_value ==
                    row.maximum_action_value - row.minimum_action_value,
            "telemetry action spread is finite and coherent");
    require(row.all_in_exact_count + row.all_in_sampled_count <= row.sample_count,
            "telemetry terminal counters do not exceed observations");
  }
}

void test_off_on_non_mutation(const gtosd::HuPreflopTree &tree) {
  const auto off_options = test_options();
  auto on_options = off_options;
  on_options.collect_action_conditioned_telemetry = true;

  const auto without = gtosd::solve_hu_preflop_sampled(tree, off_options);
  const auto with = gtosd::solve_hu_preflop_sampled(tree, on_options);
  require(without.has_value() && with.has_value(), "OFF and ON solves complete");
  require(!without.value().action_conditioned_telemetry_enabled,
          "OFF result keeps telemetry disabled");
  require(without.value().action_conditioned_telemetry.empty(),
          "OFF result has no telemetry rows");
  require_same_solver_state(without.value(), with.value());
  validate_telemetry(tree, with.value());

  const auto repeated = gtosd::solve_hu_preflop_sampled(tree, on_options);
  require(repeated.has_value(), "repeated ON solve completes");
  require(with.value().action_conditioned_telemetry ==
              repeated.value().action_conditioned_telemetry,
          "single-worker telemetry is deterministic");
}

void test_parallel_non_mutation(const gtosd::HuPreflopTree &tree) {
  auto options = test_options();
  options.iterations = 2U;
  options.training_batch_iterations = 1U;
  options.worker_threads = 2U;
  options.collect_action_conditioned_telemetry = true;
  const auto result = gtosd::solve_hu_preflop_sampled(tree, options);
  require(result.has_value(), "parallel telemetry solve completes");
  validate_telemetry(tree, result.value());
}

void test_global_crn_determinism(const gtosd::HuPreflopTree &tree) {
  auto options = test_options();
  options.iterations = 4U;
  options.training_batch_iterations = 1U;
  options.worker_threads = 1U;
  options.global_common_random_numbers = true;
  options.collect_action_conditioned_telemetry = true;
  const auto first = gtosd::solve_hu_preflop_sampled(tree, options);
  const auto second = gtosd::solve_hu_preflop_sampled(tree, options);
  require(first.has_value() && second.has_value(), "global CRN solves complete");
  require(first.value().global_common_random_numbers &&
              second.value().global_common_random_numbers,
          "global CRN result records the enabled contract");
  require(first.value().algorithm_id.find("global_common_random_numbers_v1") !=
              std::string::npos,
          "global CRN algorithm fingerprint is versioned");
  require(first.value().root_strategy == second.value().root_strategy &&
              first.value().root_cumulative_weighted_regret ==
                  second.value().root_cumulative_weighted_regret &&
              first.value().action_conditioned_telemetry ==
                  second.value().action_conditioned_telemetry,
          "global CRN is deterministic on repeated single-worker batches");

  auto invalid = options;
  invalid.training_batch_iterations = 0U;
  invalid.worker_threads = 1U;
  const auto rejected = gtosd::solve_hu_preflop_sampled(tree, invalid);
  require(!rejected.has_value() && rejected.error() == gtosd::HuPreflopError::InvalidConfiguration,
          "global CRN requires the deterministic batched schedule");
}

template <std::size_t Size>
double sample_variance(const std::array<double, Size> &values) {
  static_assert(Size > 1U);
  double mean = 0.0;
  for (const auto value : values) {
    mean += value;
  }
  mean /= static_cast<double>(Size);

  double squared_deviation = 0.0;
  for (const auto value : values) {
    const auto deviation = value - mean;
    squared_deviation += deviation * deviation;
  }
  return squared_deviation / static_cast<double>(Size - 1U);
}

void test_global_crn_paired_variance_fixture() {
  constexpr std::array<double, 8U> shocks{-3.0, -2.0, -1.0, 0.0,
                                          0.0,  1.0,  2.0, 3.0};
  std::array<double, shocks.size()> paired_a{};
  std::array<double, shocks.size()> paired_b{};
  std::array<double, shocks.size()> independent_a{};
  std::array<double, shocks.size()> independent_b{};
  std::array<double, shocks.size()> paired_difference{};
  std::array<double, shocks.size()> independent_difference{};

  for (std::size_t index = 0U; index < shocks.size(); ++index) {
    paired_a[index] = 2.0 + shocks[index];
    paired_b[index] = -1.0 + 0.5 * shocks[index];
    independent_a[index] = paired_a[index];
    independent_b[index] = -1.0 + 0.5 * shocks[shocks.size() - 1U - index];
    paired_difference[index] = paired_a[index] - paired_b[index];
    independent_difference[index] = independent_a[index] - independent_b[index];
  }

  auto sorted_paired_a = paired_a;
  auto sorted_paired_b = paired_b;
  auto sorted_independent_a = independent_a;
  auto sorted_independent_b = independent_b;
  std::ranges::sort(sorted_paired_a);
  std::ranges::sort(sorted_paired_b);
  std::ranges::sort(sorted_independent_a);
  std::ranges::sort(sorted_independent_b);
  require(sorted_paired_a == sorted_independent_a &&
              sorted_paired_b == sorted_independent_b,
          "paired replay preserves each action marginal exactly");
  require(sample_variance(paired_difference) < sample_variance(independent_difference),
          "paired replay reduces action-difference variance on the finite fixture");
}

void test_global_crn_worker_reproducibility(const gtosd::HuPreflopTree &tree) {
  auto one_worker = test_options();
  one_worker.iterations = 4U;
  one_worker.training_batch_iterations = 1U;
  one_worker.worker_threads = 1U;
  one_worker.global_common_random_numbers = true;
  one_worker.collect_action_conditioned_telemetry = true;
  auto eight_workers = one_worker;
  eight_workers.worker_threads = 8U;

  const auto one = gtosd::solve_hu_preflop_sampled(tree, one_worker);
  const auto eight = gtosd::solve_hu_preflop_sampled(tree, eight_workers);
  require(one.has_value() && eight.has_value(), "global CRN 1/8 worker solves complete");
  require(one.value().root_strategy == eight.value().root_strategy &&
              one.value().root_current_strategy == eight.value().root_current_strategy &&
              one.value().root_cumulative_weighted_regret ==
                  eight.value().root_cumulative_weighted_regret &&
              one.value().root_cumulative_average_weight ==
                  eight.value().root_cumulative_average_weight &&
              one.value().root_ev_ante == eight.value().root_ev_ante &&
              one.value().root_ev_standard_error_ante ==
                  eight.value().root_ev_standard_error_ante &&
              one.value().action_conditioned_telemetry ==
                  eight.value().action_conditioned_telemetry,
          "global CRN preserves policy, EV and telemetry across 1/8 workers");
  require(one.value().tree_fingerprint == eight.value().tree_fingerprint &&
              one.value().abstraction_id == eight.value().abstraction_id &&
              one.value().global_common_random_numbers &&
              eight.value().global_common_random_numbers,
          "global CRN 1/8 worker runs preserve game and algorithm contract");
}

void validate_root_decision_trace(const gtosd::HuPreflopRootDecisionTrace &trace,
                                  const bool expect_current) {
  constexpr double tolerance = 1e-9;
  require(trace.deals_per_action > 0U, "root decision trace records its conditional deal count");
  require(trace.policies.size() == (expect_current ? 2U : 1U),
          "root decision trace contains the requested policy views");
  for (const auto &policy : trace.policies) {
    for (std::size_t action_id = 0U; action_id < policy.actions.size(); ++action_id) {
      const auto &action = policy.actions[action_id];
      require(action.action_id == action_id && action.value.samples == trace.deals_per_action &&
                  std::abs(action.value.probability - 1.0) < tolerance &&
                  std::isfinite(action.value.mean_payoff_ante) &&
                  std::isfinite(action.value.standard_error_ante) &&
                  action.value.standard_error_ante >= 0.0,
              "forced root action has one observation per conditional deal");
      double branch_probability = 0.0;
      double branch_contribution = 0.0;
      for (const auto &branch : action.preflop_branches) {
        require(branch.value.samples > 0U && branch.value.probability > 0.0 &&
                    std::isfinite(branch.value.mean_payoff_ante) &&
                    std::isfinite(branch.value.standard_error_ante),
                "root decision branch contains finite non-empty moments");
        branch_probability += branch.value.probability;
        branch_contribution += branch.value.ev_contribution_ante;
      }
      require(std::abs(branch_probability - 1.0) < tolerance &&
                  std::abs(branch_contribution - action.value.mean_payoff_ante) < tolerance,
              "preflop branch probabilities and EV contributions partition the action");
      for (const auto &street : action.street_reach) {
        require(street.street != gtosd::Street::Preflop && street.value.samples > 0U &&
                    street.value.probability > 0.0 && street.value.probability <= 1.0,
                "street reach is a bounded conditional probability");
        for (std::uint8_t player = 0U; player < 2U; ++player) {
          double bucket_probability = 0.0;
          double bucket_contribution = 0.0;
          for (const auto &bucket : action.buckets) {
            if (bucket.street == street.street && bucket.player == player) {
              bucket_probability += bucket.value.probability;
              bucket_contribution += bucket.value.ev_contribution_ante;
            }
          }
          require(std::abs(bucket_probability - street.value.probability) < tolerance &&
                      std::abs(bucket_contribution - street.value.ev_contribution_ante) <
                          tolerance,
                  "bucket groups partition each reached street for both players");
        }
      }
    }
    for (std::size_t left = 0U; left < policy.actions.size(); ++left) {
      require(policy.paired_difference_mean_ante[left][left] == 0.0 &&
                  policy.paired_difference_standard_error_ante[left][left] == 0.0,
              "paired action difference diagonal is exactly zero");
      for (std::size_t right = 0U; right < policy.actions.size(); ++right) {
        require(std::isfinite(policy.paired_difference_mean_ante[left][right]) &&
                    std::isfinite(
                        policy.paired_difference_standard_error_ante[left][right]) &&
                    policy.paired_difference_standard_error_ante[left][right] >= 0.0 &&
                    std::abs(policy.paired_difference_mean_ante[left][right] +
                             policy.paired_difference_mean_ante[right][left]) < tolerance &&
                    std::abs(policy.paired_difference_standard_error_ante[left][right] -
                             policy.paired_difference_standard_error_ante[right][left]) <
                        tolerance,
                "paired action differences are antisymmetric with symmetric uncertainty");
      }
    }
  }
}

void test_root_decision_trace_non_mutation(const gtosd::HuPreflopTree &tree) {
  auto off_options = test_options();
  off_options.evaluate_current_profile = true;
  auto on_options = off_options;
  on_options.root_decision_trace_hand_classes =
      {hand_class_id("JTo"), hand_class_id("QJo"), hand_class_id("J9s")};
  on_options.root_decision_trace_deals_per_class = 4U;

  const auto without = gtosd::solve_hu_preflop_sampled(tree, off_options);
  const auto with = gtosd::solve_hu_preflop_sampled(tree, on_options);
  require(without.has_value() && with.has_value(), "root decision trace OFF and ON solves complete");
  require(without.value().root_decision_traces.empty() &&
              with.value().root_decision_traces.size() == 3U,
          "root decision trace remains opt-in and class-filtered");
  require(without.value().root_strategy == with.value().root_strategy &&
              without.value().root_current_strategy == with.value().root_current_strategy &&
              without.value().root_cumulative_weighted_regret ==
                  with.value().root_cumulative_weighted_regret &&
              without.value().root_cumulative_average_weight ==
                  with.value().root_cumulative_average_weight &&
              without.value().root_action_advantage_diagnostics ==
                  with.value().root_action_advantage_diagnostics &&
              without.value().preflop_blueprint == with.value().preflop_blueprint &&
              without.value().algorithm_id == with.value().algorithm_id,
          "root decision trace does not mutate training state or policy fingerprints");
  for (const auto &trace : with.value().root_decision_traces) {
    validate_root_decision_trace(trace, true);
  }
  const auto repeated = gtosd::solve_hu_preflop_sampled(tree, on_options);
  require(repeated.has_value() &&
              repeated.value().root_decision_traces == with.value().root_decision_traces,
          "root decision trace is deterministic for fixed seeds");

  auto duplicate = on_options;
  duplicate.root_decision_trace_hand_classes = {hand_class_id("JTo"), hand_class_id("JTo")};
  const auto rejected_duplicate = gtosd::solve_hu_preflop_sampled(tree, duplicate);
  require(!rejected_duplicate.has_value() &&
              rejected_duplicate.error() == gtosd::HuPreflopError::InvalidConfiguration,
          "root decision trace rejects duplicate hand classes");
  auto excessive = on_options;
  excessive.root_decision_trace_deals_per_class = 10'001U;
  const auto rejected_excessive = gtosd::solve_hu_preflop_sampled(tree, excessive);
  require(!rejected_excessive.has_value() &&
              rejected_excessive.error() == gtosd::HuPreflopError::InvalidConfiguration,
          "root decision trace enforces its diagnostic deal bound");
  auto excessive_total = on_options;
  excessive_total.root_decision_trace_deals_per_class = 10'000U;
  const auto rejected_total = gtosd::solve_hu_preflop_sampled(tree, excessive_total);
  require(!rejected_total.has_value() &&
              rejected_total.error() == gtosd::HuPreflopError::InvalidConfiguration,
          "root decision trace bounds aggregate class-deals");
  auto excessive_classes = on_options;
  excessive_classes.root_decision_trace_hand_classes.clear();
  for (gtosd::HandClassId hand = 0U; hand < 17U; ++hand) {
    excessive_classes.root_decision_trace_hand_classes.push_back(hand);
  }
  const auto rejected_classes =
      gtosd::solve_hu_preflop_sampled(tree, excessive_classes);
  require(!rejected_classes.has_value() &&
              rejected_classes.error() == gtosd::HuPreflopError::InvalidConfiguration,
          "root decision trace bounds the number of requested classes");
}

} // namespace

int main() {
  try {
    const auto tree = gtosd::build_hu_preflop_tree(gtosd::make_hu_co40_benchmark_config());
    require(tree.has_value(), "CO40 tree builds for V19 telemetry tests");
    test_off_on_non_mutation(tree.value());
    test_parallel_non_mutation(tree.value());
    test_global_crn_determinism(tree.value());
    test_global_crn_paired_variance_fixture();
    test_global_crn_worker_reproducibility(tree.value());
    test_root_decision_trace_non_mutation(tree.value());
    std::cout << "V19_HU_PREFLOP_TELEMETRY_TESTS=PASS\n"
              << "assertions=" << assertions << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "V19_HU_PREFLOP_TELEMETRY_TESTS=FAIL: " << error.what() << '\n';
    return 1;
  } catch (...) {
    std::cerr << "V19_HU_PREFLOP_TELEMETRY_TESTS=FAIL: unknown exception\n";
    return 1;
  }
}
