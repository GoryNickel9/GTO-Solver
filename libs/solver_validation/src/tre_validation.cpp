#include "gtosd/solver/tre_validation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace gtosd {
namespace {

constexpr double validation_tolerance = 1.0e-12;

double inverse_standard_normal(const double probability) {
  constexpr std::array a{-3.969683028665376e+01, 2.209460984245205e+02,  -2.759285104469687e+02,
                         1.383577518672690e+02,  -3.066479806614716e+01, 2.506628277459239e+00};
  constexpr std::array b{-5.447609879822406e+01, 1.615858368580409e+02, -1.556989798598866e+02,
                         6.680131188771972e+01, -1.328068155288572e+01};
  constexpr std::array c{-7.784894002430293e-03, -3.223964580411365e-01, -2.400758277161838e+00,
                         -2.549732539343734e+00, 4.374664141464968e+00,  2.938163982698783e+00};
  constexpr std::array d{7.784695709041462e-03, 3.224671290700398e-01, 2.445134137142996e+00,
                         3.754408661907416e+00};
  constexpr double low = 0.02425;
  constexpr double high = 1.0 - low;

  double quantile = 0.0;
  if (probability < low) {
    const double q = std::sqrt(-2.0 * std::log(probability));
    quantile = (((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) /
               ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1.0);
  } else if (probability <= high) {
    const double q = probability - 0.5;
    const double r = q * q;
    quantile = (((((a[0] * r + a[1]) * r + a[2]) * r + a[3]) * r + a[4]) * r + a[5]) * q /
               (((((b[0] * r + b[1]) * r + b[2]) * r + b[3]) * r + b[4]) * r + 1.0);
  } else {
    const double q = std::sqrt(-2.0 * std::log(1.0 - probability));
    quantile = -(((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) /
               ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1.0);
  }

  constexpr double sqrt_two = 1.4142135623730950488;
  constexpr double sqrt_two_pi = 2.5066282746310005024;
  const double error = 0.5 * std::erfc(-quantile / sqrt_two) - probability;
  const double correction = error * sqrt_two_pi * std::exp(0.5 * quantile * quantile);
  return quantile - correction / (1.0 + 0.5 * quantile * correction);
}

Result<bool, SolverError> validate_layout(const TreStratifiedLayout &layout,
                                          const std::size_t observation_count) {
  if (layout.strata.empty() || layout.total_outcomes != observation_count) {
    return Result<bool, SolverError>::failure(SolverError::InvalidConfiguration);
  }
  std::size_t expected_offset = 0U;
  double weight_sum = 0.0;
  for (const auto &stratum : layout.strata) {
    if (stratum.id.empty() || stratum.outcome_offset != expected_offset ||
        stratum.outcome_count < 2U || !std::isfinite(stratum.weight) || stratum.weight <= 0.0) {
      return Result<bool, SolverError>::failure(SolverError::InvalidConfiguration);
    }
    if (stratum.outcome_count > layout.total_outcomes - expected_offset) {
      return Result<bool, SolverError>::failure(SolverError::InvalidConfiguration);
    }
    expected_offset += stratum.outcome_count;
    weight_sum += stratum.weight;
  }
  if (expected_offset != layout.total_outcomes ||
      std::abs(weight_sum - 1.0) > validation_tolerance) {
    return Result<bool, SolverError>::failure(SolverError::InvalidConfiguration);
  }
  return Result<bool, SolverError>::success(true);
}

std::uint32_t grow_samples(const std::uint32_t current, const std::uint32_t maximum) {
  return current > maximum / 2U ? maximum : current * 2U;
}

bool finite_probability(const double value) {
  return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}

} // namespace

Result<TreConfidencePlan, SolverError>
make_tre_confidence_plan(const double family_wise_alpha, const std::uint32_t simultaneous_metrics,
                         const std::uint32_t planned_looks) {
  if (!std::isfinite(family_wise_alpha) || family_wise_alpha <= 0.0 || family_wise_alpha >= 1.0 ||
      simultaneous_metrics == 0U || planned_looks == 0U ||
      simultaneous_metrics > std::numeric_limits<std::uint32_t>::max() / planned_looks) {
    return Result<TreConfidencePlan, SolverError>::failure(SolverError::InvalidConfiguration);
  }
  TreConfidencePlan result;
  result.family_wise_alpha = family_wise_alpha;
  result.simultaneous_metrics = simultaneous_metrics;
  result.planned_looks = planned_looks;
  result.bonferroni_comparisons = simultaneous_metrics * planned_looks;
  result.per_interval_alpha =
      family_wise_alpha / static_cast<double>(result.bonferroni_comparisons);
  result.critical_value = inverse_standard_normal(1.0 - result.per_interval_alpha / 2.0);
  if (!std::isfinite(result.critical_value) || result.critical_value <= 0.0) {
    return Result<TreConfidencePlan, SolverError>::failure(SolverError::NumericalFailure);
  }
  return Result<TreConfidencePlan, SolverError>::success(result);
}

Result<TreEstimate, SolverError>
estimate_tre_stratified_mean(const std::vector<double> &observations,
                             const TreStratifiedLayout &layout, const double critical_value) {
  const auto valid = validate_layout(layout, observations.size());
  if (!valid || !std::isfinite(critical_value) || critical_value <= 0.0) {
    return Result<TreEstimate, SolverError>::failure(valid ? SolverError::InvalidConfiguration
                                                           : valid.error());
  }

  TreEstimate result;
  double variance_of_mean = 0.0;
  for (const auto &stratum : layout.strata) {
    double mean = 0.0;
    for (std::uint32_t sample = 0U; sample < stratum.outcome_count; ++sample) {
      const double observation = observations[stratum.outcome_offset + sample];
      if (!std::isfinite(observation)) {
        return Result<TreEstimate, SolverError>::failure(SolverError::NumericalFailure);
      }
      mean += observation;
    }
    mean /= static_cast<double>(stratum.outcome_count);
    double squared_deviation = 0.0;
    for (std::uint32_t sample = 0U; sample < stratum.outcome_count; ++sample) {
      const double difference = observations[stratum.outcome_offset + sample] - mean;
      squared_deviation += difference * difference;
    }
    const double sample_variance =
        squared_deviation / static_cast<double>(stratum.outcome_count - 1U);
    result.mean += stratum.weight * mean;
    variance_of_mean += stratum.weight * stratum.weight * sample_variance /
                        static_cast<double>(stratum.outcome_count);
  }
  result.standard_error = std::sqrt(std::max(0.0, variance_of_mean));
  result.confidence_half_width = critical_value * result.standard_error;
  result.confidence_lower = result.mean - result.confidence_half_width;
  result.confidence_upper = result.mean + result.confidence_half_width;
  if (!std::isfinite(result.mean) || !std::isfinite(result.confidence_half_width)) {
    return Result<TreEstimate, SolverError>::failure(SolverError::NumericalFailure);
  }
  return Result<TreEstimate, SolverError>::success(result);
}

Result<TrePairedEvaluation, SolverError>
evaluate_tre_paired_look(const FiniteGame &game, const StrategyProfile &baseline_partial_profile,
                         const std::array<StrategyProfile, 2> &candidate_response_profiles,
                         const TreStratifiedLayout &layout, const TreConfidencePlan &confidence,
                         const PolicyCompletionRule completion_rule) {
  const auto baseline = complete_strategy_profile(game, baseline_partial_profile, completion_rule);
  if (!baseline) {
    return Result<TrePairedEvaluation, SolverError>::failure(baseline.error());
  }
  const auto baseline_outcomes =
      evaluate_strategy_profile_by_root_chance(game, baseline.value().profile);
  if (!baseline_outcomes) {
    return Result<TrePairedEvaluation, SolverError>::failure(baseline_outcomes.error());
  }
  const auto valid_layout =
      validate_layout(layout, baseline_outcomes.value().outcome_values.size());
  if (!valid_layout) {
    return Result<TrePairedEvaluation, SolverError>::failure(valid_layout.error());
  }

  const auto outcome_count = baseline_outcomes.value().outcome_values.size();
  std::array<std::vector<double>, 2> profile_observations;
  std::array<std::vector<double>, 2> gain_observations;
  for (std::size_t player = 0U; player < 2U; ++player) {
    profile_observations[player].reserve(outcome_count);
    gain_observations[player].reserve(outcome_count);
  }
  for (const auto &outcome : baseline_outcomes.value().outcome_values) {
    profile_observations[0].push_back(outcome[0]);
    profile_observations[1].push_back(outcome[1]);
  }

  TrePairedEvaluation result;
  result.baseline_coverage = baseline.value().coverage;
  for (std::uint8_t player = 0U; player < 2U; ++player) {
    const auto response =
        complete_strategy_profile(game, candidate_response_profiles[player], completion_rule);
    if (!response) {
      return Result<TrePairedEvaluation, SolverError>::failure(response.error());
    }
    result.response_coverage[player] = response.value().coverage;
    auto deviated = baseline.value().profile;
    for (const auto &[information_set, strategy] : response.value().profile) {
      if (strategy.player == player) {
        deviated.at(information_set) = strategy;
      }
    }
    const auto deviated_outcomes = evaluate_strategy_profile_by_root_chance(game, deviated);
    if (!deviated_outcomes || deviated_outcomes.value().outcome_values.size() != outcome_count) {
      return Result<TrePairedEvaluation, SolverError>::failure(
          deviated_outcomes ? SolverError::InvalidGame : deviated_outcomes.error());
    }
    for (std::size_t outcome = 0U; outcome < outcome_count; ++outcome) {
      gain_observations[player].push_back(
          deviated_outcomes.value().outcome_values[outcome][player] -
          baseline_outcomes.value().outcome_values[outcome][player]);
    }
  }

  std::vector<double> gain_sum;
  gain_sum.reserve(outcome_count);
  for (std::size_t outcome = 0U; outcome < outcome_count; ++outcome) {
    gain_sum.push_back(gain_observations[0][outcome] + gain_observations[1][outcome]);
  }
  for (std::size_t player = 0U; player < 2U; ++player) {
    const auto profile = estimate_tre_stratified_mean(profile_observations[player], layout,
                                                      confidence.critical_value);
    const auto gain =
        estimate_tre_stratified_mean(gain_observations[player], layout, confidence.critical_value);
    if (!profile || !gain) {
      return Result<TrePairedEvaluation, SolverError>::failure(profile ? gain.error()
                                                                       : profile.error());
    }
    result.profile_value[player] = profile.value();
    result.candidate_response_gain[player] = gain.value();
    if (std::abs(profile.value().mean - baseline_outcomes.value().profile_value[player]) >
        1.0e-10) {
      return Result<TrePairedEvaluation, SolverError>::failure(SolverError::NumericalFailure);
    }
  }
  const auto sum = estimate_tre_stratified_mean(gain_sum, layout, confidence.critical_value);
  if (!sum) {
    return Result<TrePairedEvaluation, SolverError>::failure(sum.error());
  }
  result.candidate_response_gain_sum = sum.value();
  result.maximum_normalized_confidence_half_width =
      std::max({result.profile_value[0].confidence_half_width,
                result.profile_value[1].confidence_half_width,
                result.candidate_response_gain[0].confidence_half_width,
                result.candidate_response_gain[1].confidence_half_width,
                result.candidate_response_gain_sum.confidence_half_width}) /
      game.initial_pot;
  return Result<TrePairedEvaluation, SolverError>::success(std::move(result));
}

Result<TreAdaptiveDecision, SolverError>
decide_tre_adaptive_step(const TreAdaptiveConfig &config, const TreAdaptiveState &state,
                         const TrePairedEvaluation &evaluation, const double initial_pot) {
  if (state.training_samples_per_stratum == 0U || state.response_samples_per_stratum == 0U ||
      state.evaluation_samples_per_stratum < 2U ||
      state.training_samples_per_stratum > config.maximum_training_samples_per_stratum ||
      state.response_samples_per_stratum > config.maximum_response_samples_per_stratum ||
      state.evaluation_samples_per_stratum > config.maximum_evaluation_samples_per_stratum ||
      !finite_probability(config.minimum_policy_reach_coverage) ||
      !finite_probability(config.minimum_response_reach_coverage) ||
      !std::isfinite(config.normalized_confidence_half_width_target) ||
      config.normalized_confidence_half_width_target <= 0.0 ||
      !std::isfinite(config.maximum_normalized_candidate_gain) ||
      config.maximum_normalized_candidate_gain < 0.0 || !std::isfinite(initial_pot) ||
      initial_pot <= 0.0) {
    return Result<TreAdaptiveDecision, SolverError>::failure(SolverError::InvalidConfiguration);
  }

  TreAdaptiveDecision decision;
  decision.next = state;
  if (evaluation.baseline_coverage.reach_weighted_coverage < config.minimum_policy_reach_coverage) {
    if (state.training_samples_per_stratum < config.maximum_training_samples_per_stratum) {
      decision.action = TreAdaptiveAction::IncreaseTraining;
      decision.next.training_samples_per_stratum = grow_samples(
          state.training_samples_per_stratum, config.maximum_training_samples_per_stratum);
      decision.reason = "baseline_policy_reach_coverage_below_target";
    } else {
      decision.action = TreAdaptiveAction::ResourceLimit;
      decision.reason = "training_sample_limit_before_policy_coverage_target";
    }
    return Result<TreAdaptiveDecision, SolverError>::success(std::move(decision));
  }
  for (std::size_t player = 0U; player < 2U; ++player) {
    if (evaluation.response_coverage[player].reach_weighted_coverage_by_player[player] <
        config.minimum_response_reach_coverage) {
      if (state.response_samples_per_stratum < config.maximum_response_samples_per_stratum) {
        decision.action = TreAdaptiveAction::IncreaseResponse;
        decision.next.response_samples_per_stratum = grow_samples(
            state.response_samples_per_stratum, config.maximum_response_samples_per_stratum);
        decision.reason = "candidate_response_reach_coverage_below_target";
      } else {
        decision.action = TreAdaptiveAction::ResourceLimit;
        decision.reason = "response_sample_limit_before_response_coverage_target";
      }
      return Result<TreAdaptiveDecision, SolverError>::success(std::move(decision));
    }
  }
  for (const auto &gain : evaluation.candidate_response_gain) {
    if (gain.confidence_lower / initial_pot > config.maximum_normalized_candidate_gain) {
      decision.action = TreAdaptiveAction::Reject;
      decision.reason = "candidate_response_gain_lower_bound_exceeds_target";
      return Result<TreAdaptiveDecision, SolverError>::success(std::move(decision));
    }
  }

  bool upper_bound_above_target = false;
  for (const auto &gain : evaluation.candidate_response_gain) {
    upper_bound_above_target =
        upper_bound_above_target ||
        gain.confidence_upper / initial_pot > config.maximum_normalized_candidate_gain;
  }
  if (evaluation.maximum_normalized_confidence_half_width >
          config.normalized_confidence_half_width_target ||
      upper_bound_above_target) {
    if (state.evaluation_samples_per_stratum < config.maximum_evaluation_samples_per_stratum) {
      decision.action = TreAdaptiveAction::IncreaseEvaluation;
      decision.next.evaluation_samples_per_stratum = grow_samples(
          state.evaluation_samples_per_stratum, config.maximum_evaluation_samples_per_stratum);
      decision.reason = upper_bound_above_target ? "candidate_response_upper_bound_above_target"
                                                 : "confidence_half_width_above_target";
    } else {
      decision.action = TreAdaptiveAction::ResourceLimit;
      decision.reason = "evaluation_sample_limit_before_statistical_target";
    }
    return Result<TreAdaptiveDecision, SolverError>::success(std::move(decision));
  }

  decision.action = TreAdaptiveAction::Pass;
  decision.reason = "coverage_and_candidate_response_targets_met";
  return Result<TreAdaptiveDecision, SolverError>::success(std::move(decision));
}

const char *tre_adaptive_action_name(const TreAdaptiveAction action) noexcept {
  switch (action) {
  case TreAdaptiveAction::Pass:
    return "pass";
  case TreAdaptiveAction::Reject:
    return "reject";
  case TreAdaptiveAction::IncreaseTraining:
    return "increase_training";
  case TreAdaptiveAction::IncreaseResponse:
    return "increase_response";
  case TreAdaptiveAction::IncreaseEvaluation:
    return "increase_evaluation";
  case TreAdaptiveAction::ResourceLimit:
    return "resource_limit";
  }
  return "unknown";
}

} // namespace gtosd
