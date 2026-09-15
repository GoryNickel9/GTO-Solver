#pragma once

#include "gtosd/solver/solver.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <string>

namespace gtosd {

enum class PolicyCompletionRule : std::uint8_t { RejectMissing, UniformUnseenV1 };

struct PolicyCoverageAudit {
  PolicyCompletionRule completion_rule{PolicyCompletionRule::RejectMissing};
  std::string target_game_fingerprint;
  std::uint64_t supplied_information_sets{0};
  std::uint64_t required_information_sets{0};
  std::uint64_t matched_information_sets{0};
  std::uint64_t unseen_information_sets{0};
  std::uint64_t unused_supplied_information_sets{0};
  std::array<std::uint64_t, 2> required_information_sets_by_player{0U, 0U};
  std::array<std::uint64_t, 2> unseen_information_sets_by_player{0U, 0U};
  std::uint64_t decision_nodes{0};
  std::uint64_t unseen_decision_nodes{0};
  double exact_key_coverage{1.0};
  double total_decision_reach_mass{0.0};
  double unseen_decision_reach_mass{0.0};
  double reach_weighted_coverage{1.0};
  std::array<double, 2> total_decision_reach_mass_by_player{0.0, 0.0};
  std::array<double, 2> unseen_decision_reach_mass_by_player{0.0, 0.0};
  std::array<double, 2> reach_weighted_coverage_by_player{1.0, 1.0};
};

struct CompletedStrategyProfile {
  StrategyProfile profile;
  PolicyCoverageAudit coverage;
};

struct RootChanceProfileEvaluation {
  std::vector<std::array<double, 2>> outcome_values;
  std::vector<double> probabilities;
  std::array<double, 2> profile_value{0.0, 0.0};
};

struct BestResponseWitness {
  std::string information_set;
  GameActionId selected_action{0};
  double counterfactual_action_value{0.0};
};

struct BestResponseResult {
  std::uint8_t player{0};
  double value{0.0};
  std::map<std::string, GameActionId> policy;
  std::vector<BestResponseWitness> witnesses;
};

struct NashConvResult {
  std::array<double, 2> profile_value{0.0, 0.0};
  std::array<double, 2> best_response_value{0.0, 0.0};
  double nash_conv{0.0};
  double normalized_nash_conv{0.0};
  double zero_sum_exploitability{0.0};
  double expected_payoff_sum{0.0};
};

struct CertifiedSolveResult {
  SolveResult solve{};
  std::vector<ConvergencePoint> convergence;
};

// Completes a policy for a target finite game and reports exact-key and
// on-policy reach coverage. Extra source keys are retained only in the audit;
// the returned profile contains exactly the target game's information sets.
[[nodiscard]] Result<CompletedStrategyProfile, SolverError>
complete_strategy_profile(const FiniteGame &game, const StrategyProfile &partial_profile,
                          PolicyCompletionRule completion_rule);

[[nodiscard]] const char *policy_completion_rule_name(PolicyCompletionRule rule) noexcept;

[[nodiscard]] Result<std::array<double, 2>, SolverError>
evaluate_strategy_profile(const FiniteGame &game, const StrategyProfile &profile);

// Evaluates every direct root-chance outcome without changing its probability.
// This is intended for paired and stratified validation on a frozen corpus.
[[nodiscard]] Result<RootChanceProfileEvaluation, SolverError>
evaluate_strategy_profile_by_root_chance(const FiniteGame &game, const StrategyProfile &profile);

[[nodiscard]] Result<BestResponseResult, SolverError>
exact_best_response(const FiniteGame &game, const StrategyProfile &opponent_profile,
                    std::uint8_t best_responder);

[[nodiscard]] Result<NashConvResult, SolverError>
calculate_nash_conv(const FiniteGame &game, const StrategyProfile &profile);

[[nodiscard]] Result<CertifiedSolveResult, SolverError>
solve_with_certification(const FiniteGame &game, const SolverConfig &config,
                         std::uint64_t certification_interval,
                         const SolverCheckpoint *resume_from = nullptr);

} // namespace gtosd
