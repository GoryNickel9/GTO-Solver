#include "gtosd/solver/reference_games.hpp"
#include "gtosd/solver/tre_validation.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::uint64_t assertions = 0U;

void require(const bool condition, const std::string_view message) {
  ++assertions;
  if (!condition) {
    throw std::runtime_error(std::string{message});
  }
}

void require_near(const double actual, const double expected, const double tolerance,
                  const std::string_view message) {
  require(std::abs(actual - expected) <= tolerance, message);
}

void test_generic_stratified_estimator() {
  gtosd::TreStratifiedLayout layout;
  layout.total_outcomes = 4U;
  layout.strata = {{"low", 0U, 2U, 0.25}, {"high", 2U, 2U, 0.75}};
  const auto estimate = gtosd::estimate_tre_stratified_mean({1.0, 3.0, 10.0, 14.0}, layout, 2.0);
  require(estimate.has_value(), "generic stratified estimate succeeds");
  require_near(estimate.value().mean, 9.5, 1.0e-12,
               "generic stratum weights determine the global mean");
  require_near(estimate.value().standard_error, std::sqrt(2.3125), 1.0e-12,
               "generic estimator combines within-stratum sample variance");
  require_near(estimate.value().confidence_half_width, 2.0 * std::sqrt(2.3125), 1.0e-12,
               "generic estimator applies the supplied critical value");

  auto invalid = layout;
  invalid.strata[1].weight = 0.5;
  const auto rejected = gtosd::estimate_tre_stratified_mean({1.0, 3.0, 10.0, 14.0}, invalid, 2.0);
  require(!rejected && rejected.error() == gtosd::SolverError::InvalidConfiguration,
          "generic estimator rejects non-normalized stratum weights");
}

void test_generic_paired_evaluation() {
  const auto game = gtosd::make_kuhn_poker_game();
  const auto baseline = gtosd::reference_equilibrium_strategy(game);
  require(baseline.has_value(), "Kuhn baseline is available");
  const std::array responses{baseline.value(), baseline.value()};
  gtosd::TreStratifiedLayout layout;
  layout.total_outcomes = 6U;
  layout.strata = {{"deal_pair_0", 0U, 2U, 1.0 / 3.0},
                   {"deal_pair_1", 2U, 2U, 1.0 / 3.0},
                   {"deal_pair_2", 4U, 2U, 1.0 / 3.0}};
  const auto confidence = gtosd::make_tre_confidence_plan(0.05, 5U, 2U);
  require(confidence.has_value(), "generic simultaneous confidence plan builds");
  require_near(confidence.value().critical_value, 2.807033768343811, 1.0e-10,
               "Bonferroni plan covers five metrics over two looks");
  const auto evaluation =
      gtosd::evaluate_tre_paired_look(game, baseline.value(), responses, layout, confidence.value(),
                                      gtosd::PolicyCompletionRule::UniformUnseenV1);
  require(evaluation.has_value(), "generic paired T/R/E evaluator handles Kuhn");
  require_near(evaluation.value().profile_value[0].mean, -1.0 / 18.0, 1.0e-12,
               "generic paired evaluator preserves Kuhn profile EV");
  require_near(evaluation.value().candidate_response_gain[0].mean, 0.0, 1.0e-12,
               "identical frozen response has zero paired gain for player zero");
  require_near(evaluation.value().candidate_response_gain[1].mean, 0.0, 1.0e-12,
               "identical frozen response has zero paired gain for player one");
}

void test_generic_adaptive_controller() {
  gtosd::TreAdaptiveConfig config;
  config.maximum_training_samples_per_stratum = 8U;
  config.maximum_response_samples_per_stratum = 8U;
  config.maximum_evaluation_samples_per_stratum = 16U;
  config.minimum_policy_reach_coverage = 0.9;
  config.minimum_response_reach_coverage = 0.8;
  config.normalized_confidence_half_width_target = 0.01;
  config.maximum_normalized_candidate_gain = 0.01;
  const gtosd::TreAdaptiveState state{2U, 2U, 4U};
  gtosd::TrePairedEvaluation observation;
  observation.baseline_coverage.reach_weighted_coverage = 0.5;
  observation.response_coverage[0].reach_weighted_coverage_by_player[0] = 1.0;
  observation.response_coverage[1].reach_weighted_coverage_by_player[1] = 1.0;
  const auto grow_training = gtosd::decide_tre_adaptive_step(config, state, observation, 3.0);
  require(grow_training.has_value() &&
              grow_training.value().action == gtosd::TreAdaptiveAction::IncreaseTraining &&
              grow_training.value().next.training_samples_per_stratum == 4U,
          "controller increases training support first");

  observation.baseline_coverage.reach_weighted_coverage = 1.0;
  observation.response_coverage[0].reach_weighted_coverage_by_player[0] = 0.4;
  const auto grow_response = gtosd::decide_tre_adaptive_step(config, state, observation, 3.0);
  require(grow_response.has_value() &&
              grow_response.value().action == gtosd::TreAdaptiveAction::IncreaseResponse &&
              grow_response.value().next.response_samples_per_stratum == 4U,
          "controller increases response support second");

  observation.response_coverage[0].reach_weighted_coverage_by_player[0] = 1.0;
  observation.maximum_normalized_confidence_half_width = 0.02;
  const auto grow_evaluation = gtosd::decide_tre_adaptive_step(config, state, observation, 3.0);
  require(grow_evaluation.has_value() &&
              grow_evaluation.value().action == gtosd::TreAdaptiveAction::IncreaseEvaluation &&
              grow_evaluation.value().next.evaluation_samples_per_stratum == 8U,
          "controller increases evaluation precision after coverage passes");

  observation.maximum_normalized_confidence_half_width = 0.0;
  observation.candidate_response_gain[0].confidence_lower = 0.04;
  const auto reject = gtosd::decide_tre_adaptive_step(config, state, observation, 3.0);
  require(reject.has_value() && reject.value().action == gtosd::TreAdaptiveAction::Reject,
          "controller rejects a replicated candidate gain above the normalized target");

  observation.candidate_response_gain[0].confidence_lower = 0.0;
  observation.candidate_response_gain[0].confidence_upper = 0.01;
  observation.candidate_response_gain[1].confidence_upper = 0.01;
  const auto pass = gtosd::decide_tre_adaptive_step(config, state, observation, 3.0);
  require(pass.has_value() && pass.value().action == gtosd::TreAdaptiveAction::Pass,
          "controller passes when coverage, precision and gain bounds meet their targets");
}

} // namespace

int main() {
  try {
    test_generic_stratified_estimator();
    test_generic_paired_evaluation();
    test_generic_adaptive_controller();
    std::cout << "TRE_VALIDATION_TESTS=PASS\nassertions=" << assertions << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "TRE_VALIDATION_TESTS=FAIL: " << error.what() << '\n';
    return 1;
  }
}
