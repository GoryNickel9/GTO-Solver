#include "gtosd/preflop/hu_preflop.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <numeric>
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

gtosd::HuPreflopSolveOptions tiny_options(const gtosd::HuPreflopSamplingAlgorithm algorithm) {
  gtosd::HuPreflopSolveOptions options;
  options.iterations = 4U;
  options.evaluation_deals = 8U;
  options.best_response_iterations = 2U;
  options.best_response_evaluation_deals = 8U;
  options.equity_samples_per_bucket = 0U;
  options.seed = 0x5232'5052'4546'4C50ULL;
  options.export_postflop_policy = true;
  options.maximum_exported_postflop_policy_payload_bytes = 64ULL * 1024ULL * 1024ULL;
  options.sampling_algorithm = algorithm;
  options.postflop_representation = gtosd::HuPreflopPostflopRepresentation::ExactPhysical;
  return options;
}

void require_valid_result(const gtosd::HuPreflopTree &tree,
                          const gtosd::HuPreflopSolveResult &result,
                          const std::string_view algorithm_prefix) {
  require(result.algorithm_id.starts_with(algorithm_prefix), "algorithm identifier matches mode");
  require(result.preflop_blueprint.algorithm == result.algorithm_id,
          "blueprint records the selected sampling algorithm");
  require(result.postflop_policy.algorithm == result.algorithm_id,
          "postflop policy records the selected sampling algorithm");
  require(gtosd::validate_hu_preflop_blueprint(tree, result.preflop_blueprint).has_value(),
          "preflop blueprint remains valid");
  require(
      gtosd::validate_hu_preflop_sampled_postflop_policy(tree, result.postflop_policy).has_value(),
      "sampled postflop policy remains valid");
  for (const auto &row : result.root_strategy) {
    const auto sum = std::accumulate(row.begin(), row.end(), 0.0);
    require(std::isfinite(sum) && std::abs(sum - 1.0) <= 1.0e-9,
            "each exact preflop class has a normalized average strategy");
  }
  for (std::size_t class_id = 0U; class_id < result.root_strategy.size(); ++class_id) {
    const auto current_sum = std::accumulate(result.root_current_strategy[class_id].begin(),
                                             result.root_current_strategy[class_id].end(), 0.0);
    const auto average_weight_sum =
        std::accumulate(result.root_cumulative_average_weight[class_id].begin(),
                        result.root_cumulative_average_weight[class_id].end(), 0.0);
    double positive_regret_sum = 0.0;
    for (const auto regret : result.root_cumulative_weighted_regret[class_id]) {
      require(std::isfinite(regret), "root cumulative weighted regret remains finite");
      positive_regret_sum += std::max(0.0, regret);
    }
    require(std::isfinite(current_sum) && std::abs(current_sum - 1.0) <= 1.0e-9,
            "each root current strategy remains normalized");
    require(result.root_information_last_iteration[class_id] <= result.iterations,
            "root diagnostic last update cannot exceed the solve iteration");
    for (std::size_t action = 0U; action < result.root_strategy[class_id].size(); ++action) {
      const auto expected_current =
          positive_regret_sum > 0.0
              ? std::max(0.0, result.root_cumulative_weighted_regret[class_id][action]) /
                    positive_regret_sum
              : 1.0 / static_cast<double>(result.root_strategy[class_id].size());
      require(std::abs(result.root_current_strategy[class_id][action] - expected_current) <=
                  1.0e-12,
              "root current strategy is positive cumulative regret normalized");
      if (average_weight_sum > 0.0) {
        const auto expected_average =
            result.root_cumulative_average_weight[class_id][action] / average_weight_sum;
        require(std::abs(result.root_strategy[class_id][action] - expected_average) <= 1.0e-12,
                "root average strategy is cumulative average weight normalized");
      }
    }

    const auto &diagnostic = result.root_action_advantage_diagnostics[class_id];
    require(
        std::isfinite(diagnostic.weight_sum) && diagnostic.weight_sum >= 0.0 &&
            std::isfinite(diagnostic.effective_samples) && diagnostic.effective_samples >= 0.0 &&
            diagnostic.effective_samples <= static_cast<double>(diagnostic.observations) + 1.0e-9,
        "root action-advantage sample weights remain valid");
    require((diagnostic.observations == 0U) == (diagnostic.weight_sum == 0.0),
            "unobserved root action advantages retain zero total weight");
    for (std::size_t action = 0U; action < result.root_strategy[class_id].size(); ++action) {
      require(std::isfinite(diagnostic.weighted_mean_ante[action]) &&
                  std::isfinite(diagnostic.weighted_standard_deviation_ante[action]) &&
                  diagnostic.weighted_standard_deviation_ante[action] >= 0.0 &&
                  std::isfinite(diagnostic.weighted_standard_error_ante[action]) &&
                  diagnostic.weighted_standard_error_ante[action] >= 0.0 &&
                  std::isfinite(diagnostic.cumulative_regret_reconstruction_error_ante[action]),
              "root action-advantage moments remain finite");
      if (algorithm_prefix.starts_with("linear_mccfr") && diagnostic.observations > 0U) {
        const auto tolerance =
            1.0e-10 *
            std::max(1.0, std::abs(result.root_cumulative_weighted_regret[class_id][action]));
        require(std::abs(diagnostic.cumulative_regret_reconstruction_error_ante[action]) <=
                    tolerance,
                "linear weighted advantages reconstruct cumulative root regret");
      }
      for (std::size_t other = 0U; other < result.root_strategy[class_id].size(); ++other) {
        require(
            std::isfinite(diagnostic.pairwise_weighted_mean_ante[action][other]) &&
                std::isfinite(diagnostic.pairwise_weighted_standard_error_ante[action][other]) &&
                diagnostic.pairwise_weighted_standard_error_ante[action][other] >= 0.0 &&
                diagnostic.pairwise_weighted_standard_error_ante[action][other] ==
                    diagnostic.pairwise_weighted_standard_error_ante[other][action] &&
                std::abs(diagnostic.pairwise_weighted_mean_ante[action][other] +
                         diagnostic.pairwise_weighted_mean_ante[other][action]) <= 1.0e-12,
            "paired root action diagnostics preserve symmetry");
      }
      require(diagnostic.pairwise_weighted_mean_ante[action][action] == 0.0 &&
                  diagnostic.pairwise_weighted_standard_error_ante[action][action] == 0.0,
              "paired root action diagnostic diagonal is exactly zero");
    }
  }
  for (std::size_t action = 0U; action < 5U; ++action) {
    std::uint64_t action_samples = 0U;
    for (std::size_t class_id = 0U; class_id < 81U; ++class_id) {
      const auto samples = result.root_action_ev_samples[class_id][action];
      action_samples += samples;
      require(std::isfinite(result.root_action_ev_ante[class_id][action]) &&
                  std::isfinite(result.root_action_ev_standard_error_ante[class_id][action]) &&
                  result.root_action_ev_standard_error_ante[class_id][action] >= 0.0,
              "root action EV estimates and standard errors remain finite");
      require(samples != 0U || (result.root_action_ev_ante[class_id][action] == 0.0 &&
                                result.root_action_ev_standard_error_ante[class_id][action] == 0.0),
              "unobserved root classes remain explicit zero-sample diagnostics");
    }
    require(action_samples == 8U, "every evaluation deal contributes to every forced root action");
  }
}

void test_preflop_sampling_modes() {
  const auto tree = gtosd::build_hu_preflop_tree(gtosd::make_hu_co40_benchmark_config());
  require(tree.has_value(), "CO40 tree builds for R2 sampling tests");

  const auto external = gtosd::solve_hu_preflop_sampled(
      tree.value(), tiny_options(gtosd::HuPreflopSamplingAlgorithm::ExternalSampling));
  require(external.has_value(), "verified external-sampling baseline runs in the preflop trainer");
  require_valid_result(tree.value(), external.value(),
                       "external_sampling_v2_opponent_pass_average");

  const auto linear = gtosd::solve_hu_preflop_sampled(
      tree.value(), tiny_options(gtosd::HuPreflopSamplingAlgorithm::LinearMccfr));
  require(linear.has_value(), "Linear MCCFR runs in the preflop trainer");
  require_valid_result(tree.value(), linear.value(), "linear_mccfr_v1_opponent_pass_average");

  const auto discounted = gtosd::solve_hu_preflop_sampled(
      tree.value(), tiny_options(gtosd::HuPreflopSamplingAlgorithm::DiscountedMccfr1503));
  require(discounted.has_value(), "legacy discounted MCCFR remains explicit and runnable");
  require_valid_result(tree.value(), discounted.value(),
                       "external_sampling_dcfr_1.5_0_3_alternating_v2_opponent_pass_average");

  auto full_action_options = tiny_options(gtosd::HuPreflopSamplingAlgorithm::ChanceSampledCfr);
  full_action_options.iterations = 1U;
  const auto full_action = gtosd::solve_hu_preflop_sampled(tree.value(), full_action_options);
  require(full_action.has_value(), "chance-sampled full-action CFR completes a reduced run");
  require_valid_result(tree.value(), full_action.value(),
                       "chance_sampled_full_action_cfr_v1_alternating");

  auto invalid = tiny_options(gtosd::HuPreflopSamplingAlgorithm::ExternalSampling);
  invalid.sampling_algorithm = static_cast<gtosd::HuPreflopSamplingAlgorithm>(255U);
  const auto rejected = gtosd::solve_hu_preflop_sampled(tree.value(), invalid);
  require(!rejected && rejected.error() == gtosd::HuPreflopError::InvalidConfiguration,
          "unknown sampling algorithms fail explicitly");
}

void test_full_tree_chart_evaluation() {
  const auto tree = gtosd::build_hu_preflop_tree(gtosd::make_hu_co40_benchmark_config());
  require(tree.has_value(), "CO40 tree builds for full-tree chart evaluation");
  auto options = tiny_options(gtosd::HuPreflopSamplingAlgorithm::LinearMccfr);
  options.export_postflop_policy = false;
  options.evaluate_preflop_decisions = true;
  const auto solved = gtosd::solve_hu_preflop_sampled(tree.value(), options);
  require(solved.has_value(), "full-tree chart evaluation completes");
  require(solved.value().preflop_decision_evaluations.size() == tree.value().stats.decision_nodes &&
              solved.value().preflop_decision_evaluations.size() ==
                  solved.value().preflop_blueprint.decisions.size(),
          "full-tree chart evaluation covers every preflop decision");

  const auto root_evaluation = std::find_if(
      solved.value().preflop_decision_evaluations.begin(),
      solved.value().preflop_decision_evaluations.end(),
      [&tree](const auto &evaluation) { return evaluation.node_id == tree.value().root; });
  require(root_evaluation != solved.value().preflop_decision_evaluations.end(),
          "full-tree chart evaluation contains the root");
  const auto &root = tree.value().nodes[tree.value().root];
  for (std::size_t source = 0U; source < root.edges.size(); ++source) {
    const auto &action = root.edges[source].action;
    const auto target = root.state.committed_this_street[0].units() + action.amount.units();
    const auto destination = action.type == gtosd::ActionType::AllIn ? 0U
                             : action.type == gtosd::ActionType::Raise &&
                                     target == tree.value().config.open_targets[0].units()
                                 ? 1U
                             : action.type == gtosd::ActionType::Raise &&
                                     target == tree.value().config.open_targets[1].units()
                                 ? 2U
                             : action.type == gtosd::ActionType::Call ? 3U
                             : action.type == gtosd::ActionType::Fold ? 4U
                                                                      : 5U;
    require(destination < 5U, "root chart action maps to the stable root action order");
    for (std::size_t class_id = 0U; class_id < 81U; ++class_id) {
      require(root_evaluation->action_ev_ante[class_id][source] ==
                      solved.value().root_action_ev_ante[class_id][destination] &&
                  root_evaluation->action_ev_standard_error_ante[class_id][source] ==
                      solved.value().root_action_ev_standard_error_ante[class_id][destination] &&
                  root_evaluation->action_ev_samples[class_id][source] ==
                      solved.value().root_action_ev_samples[class_id][destination],
              "full-tree root diagnostics reuse the verified root action evaluation");
    }
  }

  for (const auto &evaluation : solved.value().preflop_decision_evaluations) {
    require(evaluation.node_id < tree.value().nodes.size() && evaluation.player < 2U &&
                evaluation.action_count > 0U &&
                evaluation.action_count <= gtosd::hu_preflop_maximum_actions,
            "full-tree chart evaluation records a valid decision identity");
    for (std::size_t class_id = 0U; class_id < 81U; ++class_id) {
      for (std::size_t action = 0U; action < evaluation.action_count; ++action) {
        const auto effective = evaluation.action_ev_effective_samples[class_id][action];
        const auto samples = evaluation.action_ev_samples[class_id][action];
        require(std::isfinite(effective) && effective >= 0.0 &&
                    effective <= static_cast<double>(samples) + 1.0e-9,
                "importance-weighted effective samples stay inside the raw sample count");
        require(effective == 0.0 ||
                    (samples > 0U && std::isfinite(evaluation.action_ev_ante[class_id][action]) &&
                     std::isfinite(evaluation.action_ev_standard_error_ante[class_id][action]) &&
                     evaluation.action_ev_standard_error_ante[class_id][action] >= 0.0),
                "reachable full-tree action EV and uncertainty remain finite");
      }
    }
  }
}

void test_full_current_profile_evaluation_and_policy_format() {
  const auto tree = gtosd::build_hu_preflop_tree(gtosd::make_hu_co40_benchmark_config());
  require(tree.has_value(), "CO40 tree builds for full-current-profile evaluation");
  auto options = tiny_options(gtosd::HuPreflopSamplingAlgorithm::LinearMccfr);
  options.evaluate_current_profile = true;
  options.evaluate_preflop_decisions = true;
  const auto solved = gtosd::solve_hu_preflop_sampled(tree.value(), options);
  require(solved.has_value(), "full-current-profile evaluation completes");
  const auto &result = solved.value();
  require(result.current_profile_evaluated &&
              result.algorithm_id.find("_full_current_profile_evaluation_v1") !=
                  std::string::npos &&
              result.current_profile_preflop_decision_evaluations.size() ==
                  tree.value().stats.decision_nodes,
          "current profile is explicit and covers every preflop decision");
  require(std::isfinite(result.current_profile_root_ev_ante) &&
              std::isfinite(result.current_profile_root_ev_standard_error_ante) &&
              result.current_profile_root_ev_standard_error_ante >= 0.0 &&
              std::isfinite(result.sampled_response_lower_bound_ante) &&
              result.sampled_response_lower_bound_ante >= 0.0 &&
              std::isfinite(result.current_profile_sampled_response_lower_bound_ante) &&
              result.current_profile_sampled_response_lower_bound_ante >= 0.0,
          "average and current response diagnostics are finite lower bounds");

  const auto &policy = result.postflop_policy;
  require(policy.minor == 11U && policy.current_policy_present && !policy.entries.empty() &&
              gtosd::validate_hu_preflop_sampled_postflop_policy(tree.value(), policy)
                  .has_value(),
          "format 1.11 carries a valid current postflop policy");
  const auto &entry = policy.entries.front();
  const auto average = gtosd::query_hu_preflop_sampled_postflop_policy(
      policy, entry.key, entry.action_count, gtosd::HuPreflopSampledPolicyView::Average);
  const auto current = gtosd::query_hu_preflop_sampled_postflop_policy(
      policy, entry.key, entry.action_count, gtosd::HuPreflopSampledPolicyView::Current);
  require(average.has_value() && current.has_value() &&
              average.value() == entry.probabilities &&
              current.value() == entry.current_probabilities,
          "format 1.11 selects average and current probabilities without fallback");

  const auto path =
      std::filesystem::temp_directory_path() / "gtosd_full_current_profile_policy.bin";
  require(gtosd::save_hu_preflop_sampled_postflop_policy(policy, path.string()).has_value(),
          "format 1.11 saves atomically");
  const auto loaded = gtosd::load_hu_preflop_sampled_postflop_policy(tree.value(), path.string());
  require(loaded.has_value() && loaded.value() == policy,
          "format 1.11 survives checksum validation and reload");
  std::filesystem::remove(path);

  auto historical = policy;
  historical.minor = gtosd::HuPreflopSampledPostflopPolicy::minimum_supported_minor;
  historical.current_policy_present = false;
  for (auto &historical_entry : historical.entries) {
    historical_entry.current_probabilities = {};
  }
  historical.fingerprint =
      gtosd::fingerprint_hu_preflop_sampled_postflop_policy(historical);
  require(gtosd::validate_hu_preflop_sampled_postflop_policy(tree.value(), historical)
                  .has_value() &&
              gtosd::save_hu_preflop_sampled_postflop_policy(historical, path.string())
                  .has_value(),
          "historical average-only policy remains valid and serializable");
  const auto loaded_historical =
      gtosd::load_hu_preflop_sampled_postflop_policy(tree.value(), path.string());
  require(loaded_historical.has_value() && !loaded_historical.value().current_policy_present,
          "historical payload reloads without inventing current probabilities");
  const auto unavailable_current = gtosd::query_hu_preflop_sampled_postflop_policy(
      loaded_historical.value(), entry.key, entry.action_count,
      gtosd::HuPreflopSampledPolicyView::Current);
  require(!unavailable_current &&
              unavailable_current.error() == gtosd::HuPreflopError::UnsupportedVersion,
          "historical payload rejects a current-policy query explicitly");
  std::filesystem::remove(path);
}

void test_distributional_prototype() {
  constexpr std::uint64_t partition_seed = 0x5232'5041'5254'0001ULL;
  const std::array hole{gtosd::parse_card("Ah").value(), gtosd::parse_card("Kh").value()};
  const std::array board{gtosd::parse_card("Qh").value(), gtosd::parse_card("9c").value(),
                         gtosd::parse_card("7d").value(), gtosd::parse_card("8s").value(),
                         gtosd::parse_card("6c").value()};
  const std::array suit_permuted_hole{gtosd::parse_card("As").value(),
                                      gtosd::parse_card("Ks").value()};
  const std::array suit_permuted_board{
      gtosd::parse_card("Qs").value(), gtosd::parse_card("9d").value(),
      gtosd::parse_card("7h").value(), gtosd::parse_card("8c").value(),
      gtosd::parse_card("6d").value()};
  auto alternate_future = board;
  alternate_future[3] = gtosd::parse_card("Jc").value();
  alternate_future[4] = gtosd::parse_card("Td").value();
  constexpr std::array streets{gtosd::Street::Flop, gtosd::Street::Turn, gtosd::Street::River};
  constexpr std::array<std::uint16_t, 3> capacities{256U, 1'024U, 4'096U};
  for (std::size_t index = 0U; index < streets.size(); ++index) {
    const auto first = gtosd::compute_hu_preflop_distributional_strength_bucket(
        hole, board, streets[index], 32U, partition_seed);
    const auto repeated = gtosd::compute_hu_preflop_distributional_strength_bucket(
        hole, board, streets[index], 32U, partition_seed);
    const auto permuted = gtosd::compute_hu_preflop_distributional_strength_bucket(
        suit_permuted_hole, suit_permuted_board, streets[index], 32U, partition_seed);
    require(first.has_value() && repeated.has_value() && permuted.has_value() &&
                repeated.value() == first.value() && permuted.value() == first.value(),
            "distributional mapping is deterministic and globally suit invariant");
    require(first.value() < capacities[index], "distributional bucket respects street capacity");
  }
  const auto original_flop = gtosd::compute_hu_preflop_distributional_strength_bucket(
      hole, board, gtosd::Street::Flop, 32U, partition_seed);
  const auto changed_future = gtosd::compute_hu_preflop_distributional_strength_bucket(
      hole, alternate_future, gtosd::Street::Flop, 32U, partition_seed);
  require(original_flop.has_value() && changed_future.has_value() &&
              changed_future.value() == original_flop.value(),
          "Flop mapping does not read the actual future runout");

  const auto tree = gtosd::build_hu_preflop_tree(gtosd::make_hu_co40_benchmark_config());
  require(tree.has_value(), "CO40 tree builds for the distributional prototype");
  auto options = tiny_options(gtosd::HuPreflopSamplingAlgorithm::ExternalSampling);
  options.equity_samples_per_bucket = 4U;
  options.partition_seed = partition_seed;
  options.evaluation_seed = 0x5232'4556'414C'0001ULL;
  options.postflop_representation =
      gtosd::HuPreflopPostflopRepresentation::DistributionalStrengthPrototype;
  const auto solved = gtosd::solve_hu_preflop_sampled(tree.value(), options);
  require(solved.has_value(), "distributional prototype completes a sampled solve");
  require(solved.value().abstraction_id.find("capacity_256_1024_4096") != std::string::npos &&
              solved.value().partition_seed == partition_seed &&
              solved.value().evaluation_seed == options.evaluation_seed,
          "distributional solve records capacities and independent seeds");
  require_valid_result(tree.value(), solved.value(), "external_sampling_v2_opponent_pass_average");
}

} // namespace

int main() {
  try {
    test_preflop_sampling_modes();
    test_full_tree_chart_evaluation();
    test_full_current_profile_evaluation_and_policy_format();
    test_distributional_prototype();
    std::cout << "R2_HU_PREFLOP_SAMPLING_TESTS=PASS\n"
              << "assertions=" << assertions << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "R2_HU_PREFLOP_SAMPLING_TESTS=FAIL: " << error.what() << '\n';
    return 1;
  } catch (...) {
    std::cerr << "R2_HU_PREFLOP_SAMPLING_TESTS=FAIL: unknown exception\n";
    return 1;
  }
}
