#pragma once

#include "gtosd/core/result.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace gtosd {

using GameNodeId = std::uint32_t;
using GameActionId = std::uint32_t;

enum class GameNodeKind : std::uint8_t { Terminal, Chance, Decision };

enum class SolverError : std::uint8_t {
  InvalidGame,
  InvalidNode,
  InvalidChanceProbabilities,
  InvalidInformationSet,
  InvalidStrategy,
  InvalidConfiguration,
  UnsupportedAlgorithm,
  NumericalFailure,
  GameMismatch,
  InvalidCheckpoint,
  UnsupportedCheckpointVersion,
  IoFailure
};

struct GameAction {
  GameActionId id{0};
  std::string label;
  friend bool operator==(const GameAction &, const GameAction &) = default;
};

struct GameEdge {
  GameAction action{};
  GameNodeId child{0};
  double probability{0.0};
  friend bool operator==(const GameEdge &, const GameEdge &) = default;
};

struct GameNode {
  GameNodeKind kind{GameNodeKind::Terminal};
  std::uint8_t player{0};
  std::string information_set;
  std::vector<GameEdge> edges;
  std::array<double, 2> payoff{0.0, 0.0};
};

struct FiniteGame {
  std::string game_id;
  GameNodeId root{0};
  std::vector<GameNode> nodes;
  double initial_pot{1.0};
};

struct InformationSetStrategy {
  std::uint8_t player{0};
  std::vector<GameActionId> actions;
  std::vector<double> probabilities;
};

using StrategyProfile = std::map<std::string, InformationSetStrategy>;

struct GameSummary {
  std::uint64_t nodes{0};
  std::uint64_t terminal_nodes{0};
  std::uint64_t chance_nodes{0};
  std::uint64_t decision_nodes{0};
  std::uint64_t information_sets{0};
  std::uint32_t maximum_depth{0};
  std::string fingerprint;
};

[[nodiscard]] Result<GameSummary, SolverError> validate_finite_game(const FiniteGame &game);
[[nodiscard]] Result<StrategyProfile, SolverError> uniform_strategy_profile(const FiniteGame &game);
[[nodiscard]] Result<bool, SolverError> validate_strategy_profile(const FiniteGame &game,
                                                                  const StrategyProfile &profile);
[[nodiscard]] std::string finite_game_fingerprint(const FiniteGame &game);
[[nodiscard]] const char *solver_error_name(SolverError error) noexcept;

} // namespace gtosd
