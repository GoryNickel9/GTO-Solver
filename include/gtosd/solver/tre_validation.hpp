#pragma once

#include "gtosd/solver/best_response.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gtosd {

struct TreStratum {
  std::string id;
  std::size_t outcome_offset{0U};
  std::uint32_t outcome_count{0U};
  double weight{0.0};
};

struct TreStratifiedLayout {
  std::vector<TreStratum> strata;
  std::size_t total_outcomes{0U};
};

struct TreEstimate {
  double mean{0.0};
  double standard_error{0.0};
  double confidence_half_width{0.0};
  double confidence_lower{0.0};
  double confidence_upper{0.0};
};

struct TreConfidencePlan {
  double family_wise_alpha{0.05};
  std::uint32_t simultaneous_metrics{5U};
  std::uint32_t planned_looks{1U};
  std::uint32_t bonferroni_comparisons{5U};
  double per_interval_alpha{0.01};
  double critical_value{2.5758293035489004};
};

struct TrePairedEvaluation {
  std::array<TreEstimate, 2> profile_value;
  std::array<TreEstimate, 2> candidate_response_gain;
  TreEstimate candidate_response_gain_sum;
  PolicyCoverageAudit baseline_coverage;
  std::array<PolicyCoverageAudit, 2> response_coverage;
  double maximum_normalized_confidence_half_width{0.0};
};

enum class TreAdaptiveAction : std::uint8_t {
  Pass,
  Reject,
  IncreaseTraining,
  IncreaseResponse,
  IncreaseEvaluation,
  ResourceLimit
};

struct TreAdaptiveConfig {
  std::uint32_t maximum_training_samples_per_stratum{8U};
  std::uint32_t maximum_response_samples_per_stratum{8U};
  std::uint32_t maximum_evaluation_samples_per_stratum{64U};
  double minimum_policy_reach_coverage{0.90};
  double minimum_response_reach_coverage{0.80};
  double normalized_confidence_half_width_target{0.01};
  double maximum_normalized_candidate_gain{0.01};
};

struct TreAdaptiveState {
  std::uint32_t training_samples_per_stratum{1U};
  std::uint32_t response_samples_per_stratum{1U};
  std::uint32_t evaluation_samples_per_stratum{2U};
};

struct TreAdaptiveDecision {
  TreAdaptiveAction action{TreAdaptiveAction::ResourceLimit};
  TreAdaptiveState next{};
  std::string reason;
};

[[nodiscard]] Result<TreConfidencePlan, SolverError>
make_tre_confidence_plan(double family_wise_alpha, std::uint32_t simultaneous_metrics,
                         std::uint32_t planned_looks);

[[nodiscard]] Result<TreEstimate, SolverError>
estimate_tre_stratified_mean(const std::vector<double> &observations,
                             const TreStratifiedLayout &layout, double critical_value);

[[nodiscard]] Result<TrePairedEvaluation, SolverError>
evaluate_tre_paired_look(const FiniteGame &game, const StrategyProfile &baseline_partial_profile,
                         const std::array<StrategyProfile, 2> &candidate_response_profiles,
                         const TreStratifiedLayout &layout, const TreConfidencePlan &confidence,
                         PolicyCompletionRule completion_rule);

[[nodiscard]] Result<TreAdaptiveDecision, SolverError>
decide_tre_adaptive_step(const TreAdaptiveConfig &config, const TreAdaptiveState &state,
                         const TrePairedEvaluation &evaluation, double initial_pot);

[[nodiscard]] const char *tre_adaptive_action_name(TreAdaptiveAction action) noexcept;

} // namespace gtosd
