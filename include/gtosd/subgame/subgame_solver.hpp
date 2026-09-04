#pragma once

#include "gtosd/abstraction/card_abstraction.hpp"
#include "gtosd/core/result.hpp"
#include "gtosd/solver/best_response.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gtosd {

enum class SubgameSafetyMode : std::uint8_t { UnsafeIsolated, ExactNashConvGuard };

enum class SubgameError : std::uint8_t {
  InvalidConfiguration,
  InvalidGame,
  InvalidBlueprint,
  InvalidFrontier,
  OverlappingRoots,
  InformationSetCrossesBoundary,
  SolverFailure,
  CertificationFailure,
  AbstractionFailure,
  NumericalFailure,
  UnsupportedVersion
};

struct SubgameRoot {
  GameNodeId node{0};
  double reach_weight{0.0};
  friend bool operator==(const SubgameRoot &, const SubgameRoot &) = default;
};

struct SubgameSolveConfig {
  static constexpr std::uint32_t format_major = 1;
  static constexpr std::uint32_t format_minor = 0;

  std::uint32_t major{format_major};
  std::uint32_t minor{format_minor};
  SolverConfig solver{SolverAlgorithm::CfrPlus, 10'000, 0x53554247414d4553ULL, 8, 100, {}};
  SubgameSafetyMode safety{SubgameSafetyMode::ExactNashConvGuard};
  // Absolute utility tolerance for candidate_nash_conv <= baseline + tolerance.
  double safety_tolerance{1.0e-12};
};

enum class SubgameDeployment : std::uint8_t { CandidateAccepted, BlueprintFallback };

struct SubgameSolveResult {
  FiniteGame subgame;
  SolveResult solve;
  StrategyProfile candidate_strategy;
  StrategyProfile deployed_strategy;
  // Populated only by ExactNashConvGuard. Unsafe mode deliberately avoids
  // whole-game best responses so isolated resolving remains scalable.
  std::optional<NashConvResult> baseline_metrics;
  std::optional<NashConvResult> candidate_metrics;
  std::optional<NashConvResult> deployed_metrics;
  std::vector<std::string> replaced_information_sets;
  SubgameDeployment deployment{SubgameDeployment::BlueprintFallback};
};

struct AbstractSubgameSolveResult {
  CardAbstraction abstraction;
  SubgameSolveResult abstract_solve;
  StrategyProfile exact_candidate_strategy;
  StrategyProfile exact_deployed_strategy;
  StrategyProfile deployed_abstract_strategy;
  std::optional<NashConvResult> exact_baseline_metrics;
  std::optional<NashConvResult> exact_candidate_metrics;
  std::optional<NashConvResult> exact_deployed_metrics;
  SubgameDeployment deployment{SubgameDeployment::BlueprintFallback};
};

// Computes conditional frontier weights from chance and both blueprint reaches.
// Traversal stops at the frontier, so roots must be pairwise non-overlapping.
[[nodiscard]] Result<std::vector<SubgameRoot>, SubgameError>
derive_reach_weighted_subgame_roots(const FiniteGame &game, const StrategyProfile &blueprint,
                                    const std::vector<GameNodeId> &frontier);

// Solves an infoset-closed subgame and merges its average policy into the
// blueprint. ExactNashConvGuard deploys the candidate only when full-game exact
// best responses prove that NashConv did not increase; otherwise it returns the
// unchanged blueprint as a safe fallback.
[[nodiscard]] Result<SubgameSolveResult, SubgameError>
solve_subgame(const FiniteGame &full_game, const StrategyProfile &blueprint,
              const std::vector<SubgameRoot> &roots, const SubgameSolveConfig &config);

// Composition used by the production abstraction path. The candidate is
// solved in the bucketed game, lifted to every exact private state, and only
// then guarded with exact full-game best responses. An abstract-game metric
// can therefore never authorize deployment by itself.
[[nodiscard]] Result<AbstractSubgameSolveResult, SubgameError>
solve_abstract_subgame(const FiniteGame &exact_game, const CardAbstraction &abstraction,
                       const StrategyProfile &abstract_blueprint,
                       const std::vector<SubgameRoot> &abstract_roots,
                       const SubgameSolveConfig &config);

[[nodiscard]] const char *subgame_safety_mode_name(SubgameSafetyMode mode) noexcept;
[[nodiscard]] const char *subgame_error_name(SubgameError error) noexcept;

} // namespace gtosd
