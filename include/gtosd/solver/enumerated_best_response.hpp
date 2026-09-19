#pragma once

#include "gtosd/solver/best_response.hpp"

namespace gtosd {

struct BestResponseEnumerationLimits {
  std::uint64_t maximum_policies{1'000'000};
  std::uint64_t maximum_node_evaluations{100'000'000};
};

struct BestResponseEnumerationEstimate {
  std::uint64_t information_sets{0};
  // Counts saturate at UINT64_MAX; overflow is reported separately.
  std::uint64_t policies{1};
  std::uint64_t node_evaluations{0};
  bool overflow{false};
};

struct EnumeratedBestResponse {
  BestResponseResult response;
  BestResponseEnumerationEstimate work;
};

[[nodiscard]] Result<BestResponseEnumerationEstimate, SolverError>
estimate_best_response_enumeration(const FiniteGame &game, std::uint8_t responder);

// Globally optimizes all responder information sets against a frozen opponent.
// Exact exhaustive pure-policy search, within floating-point arithmetic. This
// also maximizes behavioral value when no responder information set repeats on
// a single path (multi-affine objective). Rejects absent-minded games and jobs
// exceeding either limit; never reports a truncated search as an optimum.
// The policy is a global witness; local counterfactual witnesses remain empty.
[[nodiscard]] Result<EnumeratedBestResponse, SolverError>
enumerated_best_response(const FiniteGame &game, const StrategyProfile &profile,
                         std::uint8_t responder, BestResponseEnumerationLimits limits = {});

// Perfect recall means equal sequences of prior own information sets/actions
// at every node in each responder information set. Checks all legal paths,
// including paths with zero probability under the supplied opponent policy.
[[nodiscard]] Result<bool, SolverError> has_perfect_recall(const FiniteGame &game,
                                                           std::uint8_t responder);

} // namespace gtosd
