#pragma once

#include "gtosd/solver/solver.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <string>

namespace gtosd {

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

[[nodiscard]] Result<std::array<double, 2>, SolverError>
evaluate_strategy_profile(const FiniteGame &game, const StrategyProfile &profile);

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
