#pragma once

#include "gtosd/solver/finite_game.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace gtosd {

enum class SolverAlgorithm : std::uint8_t {
  VanillaCfr,
  CfrPlus,
  LinearCfr,
  Dcfr,
  ExternalSamplingMccfr
};

struct DcfrParameters {
  double positive_regret_exponent{1.5};
  double negative_regret_exponent{0.0};
  double strategy_exponent{2.0};
};

struct SolverConfig {
  SolverAlgorithm algorithm{SolverAlgorithm::VanillaCfr};
  std::uint64_t iterations{1'000};
  std::uint64_t seed{0x47544f5344463541ULL};
  std::uint32_t thread_count{1};
  std::uint64_t averaging_delay{0};
  DcfrParameters dcfr{};
};

struct InformationSetBuffer {
  std::uint8_t player{0};
  std::vector<GameActionId> actions;
  std::vector<double> cumulative_regret;
  std::vector<double> cumulative_strategy;
};

struct SolverCheckpoint {
  static constexpr std::uint32_t format_major = 1;
  static constexpr std::uint32_t format_minor = 0;

  std::uint32_t major{format_major};
  std::uint32_t minor{format_minor};
  std::string game_fingerprint;
  SolverConfig config{};
  std::uint64_t completed_iterations{0};
  std::uint64_t rng_state{0};
  std::map<std::string, InformationSetBuffer> information_sets;
};

struct ConvergencePoint {
  std::uint64_t iteration{0};
  std::array<double, 2> profile_value{0.0, 0.0};
  double nash_conv{0.0};
};

struct SolveResult {
  SolverCheckpoint checkpoint{};
  StrategyProfile average_strategy;
  std::uint64_t traversed_nodes{0};
  double maximum_normalization_error{0.0};
};

[[nodiscard]] Result<SolveResult, SolverError>
solve_finite_game(const FiniteGame &game, const SolverConfig &config,
                  const SolverCheckpoint *resume_from = nullptr);

[[nodiscard]] Result<StrategyProfile, SolverError>
current_strategy_profile(const SolverCheckpoint &checkpoint);

[[nodiscard]] Result<StrategyProfile, SolverError>
average_strategy_profile(const SolverCheckpoint &checkpoint);

[[nodiscard]] Result<std::string, SolverError>
serialize_solver_checkpoint(const SolverCheckpoint &checkpoint);

[[nodiscard]] Result<SolverCheckpoint, SolverError>
deserialize_solver_checkpoint(const std::string &serialized);

[[nodiscard]] Result<bool, SolverError> save_solver_checkpoint(const SolverCheckpoint &checkpoint,
                                                               const std::string &path);

[[nodiscard]] Result<SolverCheckpoint, SolverError> load_solver_checkpoint(const std::string &path);

[[nodiscard]] const char *solver_algorithm_name(SolverAlgorithm algorithm) noexcept;

} // namespace gtosd
