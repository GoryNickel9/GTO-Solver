#pragma once

#include "gtosd/solver/finite_game.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace gtosd {

enum class BoundaryReachStatus : std::uint8_t { Positive, Zero };

struct PublicSubgameDefinition {
  static constexpr std::uint32_t format_major = 1;
  static constexpr std::uint32_t format_minor = 0;

  std::uint32_t major{format_major};
  std::uint32_t minor{format_minor};
  std::string public_state_id;
  std::string entry_history;
  std::uint8_t resolving_player{0};
  std::vector<GameNodeId> roots;
  friend bool operator==(const PublicSubgameDefinition &,
                         const PublicSubgameDefinition &) = default;
};

struct SubgameBoundaryValue {
  std::string opponent_information_set;
  double counterfactual_reach{0.0};
  double blueprint_counterfactual_value{0.0};
  BoundaryReachStatus reach_status{BoundaryReachStatus::Zero};
  friend bool operator==(const SubgameBoundaryValue &, const SubgameBoundaryValue &) = default;
};

struct SubgameRootReach {
  GameNodeId root{0};
  std::string opponent_information_set;
  double counterfactual_reach{0.0};
  friend bool operator==(const SubgameRootReach &, const SubgameRootReach &) = default;
};

struct SubgameBoundary {
  static constexpr std::uint32_t format_major = 1;
  static constexpr std::uint32_t format_minor = 1;

  std::uint32_t major{format_major};
  std::uint32_t minor{format_minor};
  std::string game_fingerprint;
  std::string blueprint_fingerprint;
  std::string public_state_id;
  std::string algorithm;
  std::uint64_t blueprint_iterations{0};
  std::uint8_t opponent{0};
  std::vector<SubgameBoundaryValue> values;
  std::vector<SubgameRootReach> root_reaches;
  friend bool operator==(const SubgameBoundary &, const SubgameBoundary &) = default;
};

struct PublicSubgameSummary {
  std::uint64_t root_nodes{0};
  std::uint64_t subtree_nodes{0};
  std::uint64_t boundary_information_sets{0};
  std::uint8_t opponent{0};
  bool information_set_closed{false};
};

struct SubgameBoundaryByteModel {
  std::uint64_t dense_record_bytes{0};
  std::uint64_t serialized_boundary_bytes{0};
  std::uint64_t minimum_runtime_bytes{0};
};

[[nodiscard]] Result<PublicSubgameSummary, SolverError>
validate_public_subgame(const FiniteGame &game, const PublicSubgameDefinition &definition);

[[nodiscard]] Result<bool, SolverError>
validate_subgame_boundary(const FiniteGame &game, const PublicSubgameDefinition &definition,
                          const SubgameBoundary &boundary);

[[nodiscard]] Result<std::string, SolverError>
strategy_profile_fingerprint(const FiniteGame &game, const StrategyProfile &profile);

[[nodiscard]] Result<SubgameBoundary, SolverError>
derive_subgame_boundary(const FiniteGame &game, const PublicSubgameDefinition &definition,
                        const StrategyProfile &blueprint, std::uint64_t blueprint_iterations);

[[nodiscard]] Result<FiniteGame, SolverError>
build_safe_resolving_gadget(const FiniteGame &game, const PublicSubgameDefinition &definition,
                            const SubgameBoundary &boundary);

[[nodiscard]] Result<StrategyProfile, SolverError>
splice_resolved_subgame_strategy(const FiniteGame &game, const PublicSubgameDefinition &definition,
                                 const SubgameBoundary &boundary, const StrategyProfile &blueprint,
                                 const StrategyProfile &gadget_strategy);

[[nodiscard]] Result<std::string, SolverError>
serialize_public_subgame_definition(const PublicSubgameDefinition &definition);

[[nodiscard]] Result<PublicSubgameDefinition, SolverError>
deserialize_public_subgame_definition(const std::string &serialized);

[[nodiscard]] Result<std::string, SolverError>
serialize_subgame_boundary(const SubgameBoundary &boundary);

[[nodiscard]] Result<SubgameBoundary, SolverError>
deserialize_subgame_boundary(const std::string &serialized);

[[nodiscard]] Result<bool, SolverError>
save_public_subgame_definition(const PublicSubgameDefinition &definition, const std::string &path);

[[nodiscard]] Result<PublicSubgameDefinition, SolverError>
load_public_subgame_definition(const std::string &path);

[[nodiscard]] Result<bool, SolverError> save_subgame_boundary(const SubgameBoundary &boundary,
                                                              const std::string &path);

[[nodiscard]] Result<SubgameBoundary, SolverError> load_subgame_boundary(const std::string &path);

[[nodiscard]] Result<SubgameBoundaryByteModel, SolverError>
estimate_subgame_boundary_bytes(const FiniteGame &game, const PublicSubgameDefinition &definition,
                                const SubgameBoundary &boundary);

[[nodiscard]] const char *boundary_reach_status_name(BoundaryReachStatus status) noexcept;

} // namespace gtosd
