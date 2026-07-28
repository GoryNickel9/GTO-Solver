#pragma once

#include "gtosd/core/ranges.hpp"
#include "gtosd/memory/memory.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace gtosd {

enum class PostflopSolverError : std::uint8_t {
  InvalidConfiguration,
  TreeFailure,
  MemoryFailure,
  EquityFailure,
  SettlementFailure,
  NumericalFailure,
  CheckpointMismatch,
  InvalidCheckpoint,
  UnsupportedCheckpointVersion,
  IoFailure
};

enum class PostflopControlCommand : std::uint8_t { Continue, Pause, Cancel };
enum class PostflopStopReason : std::uint8_t { Completed, Paused, Cancelled };
struct PostflopCertification;
struct PostflopCheckpoint;

struct PostflopSolveOptions {
  std::uint64_t iterations{1};
  std::uint64_t averaging_delay{0};
  std::uint64_t certification_interval{1};
  MemoryPrototype memory_backend{MemoryPrototype::LazyInRam};
  std::string backing_file;
  std::function<void(const PostflopCertification &)> progress_callback;
  std::function<bool(const PostflopCertification &, const PostflopCheckpoint &)>
      checkpoint_callback;
  std::function<PostflopControlCommand(std::uint64_t)> control_callback;
};

struct PostflopCheckpoint {
  static constexpr std::uint32_t format_major = 1;
  static constexpr std::uint32_t format_minor = 0;

  std::uint32_t major{format_major};
  std::uint32_t minor{format_minor};
  std::string game_fingerprint;
  std::uint64_t completed_iterations{0};
  std::uint64_t averaging_delay{0};
  std::uint64_t action_count{0};
  std::string external_buffer_file;
  std::vector<double> cumulative_regret;
  std::vector<double> cumulative_strategy;
};

struct PostflopCertification {
  std::uint64_t iteration{0};
  std::array<double, 2> profile_value_antes{0.0, 0.0};
  std::array<double, 2> best_response_value_antes{0.0, 0.0};
  double nash_conv_antes{0.0};
  double normalized_nash_conv{0.0};
  double expected_payoff_sum_antes{0.0};
};

struct PostflopSolveResult {
  PostflopCheckpoint checkpoint;
  std::vector<PostflopCertification> convergence;
  PublicTreeStats public_tree;
  std::uint64_t information_sets{0};
  std::uint64_t actions{0};
  std::uint64_t traversed_nodes{0};
  double maximum_normalization_error{0.0};
  PostflopStopReason stop_reason{PostflopStopReason::Completed};
};

struct PostflopStrategyQuery {
  NodeId public_node{0};
  ComboId combo{0};
  std::vector<Action> actions;
  std::vector<double> probabilities;
};

[[nodiscard]] Result<PostflopSolveResult, PostflopSolverError>
solve_postflop_exact(const PostflopTreeConfig &config, const PostflopSolveOptions &options,
                     const PostflopCheckpoint *resume_from = nullptr);

[[nodiscard]] Result<PostflopCertification, PostflopSolverError>
certify_postflop_checkpoint(const PostflopTreeConfig &config, const PostflopCheckpoint &checkpoint);

[[nodiscard]] Result<PostflopStrategyQuery, PostflopSolverError>
query_postflop_strategy(const PostflopTreeConfig &config, const PostflopCheckpoint &checkpoint,
                        NodeId public_node, ComboId combo);

[[nodiscard]] Result<std::string, PostflopSolverError>
serialize_postflop_checkpoint(const PostflopCheckpoint &checkpoint);

[[nodiscard]] Result<PostflopCheckpoint, PostflopSolverError>
deserialize_postflop_checkpoint(const std::string &serialized);

[[nodiscard]] Result<bool, PostflopSolverError>
save_postflop_checkpoint(const PostflopCheckpoint &checkpoint, const std::string &path);

[[nodiscard]] Result<PostflopCheckpoint, PostflopSolverError>
load_postflop_checkpoint(const std::string &path);

[[nodiscard]] const char *postflop_solver_error_name(PostflopSolverError error) noexcept;

} // namespace gtosd
