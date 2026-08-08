#pragma once

#include "gtosd/core/ranges.hpp"
#include "gtosd/equity/evaluator.hpp"
#include "gtosd/memory/memory.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
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
enum class PostflopStopReason : std::uint8_t { Completed, Converged, Paused, Cancelled };
enum class PostflopStatePrecision : std::uint8_t { Float64, Float32 };
struct PostflopCertification;
struct PostflopCheckpoint;
class PostflopPreparedTree;

// F10.4 diagnostic external root lock: fixes the tree-root strategy of the
// CO player to externally observed combo-per-combo probabilities. This is a
// diagnostic-only constraint (controlled posteriors); it never changes the
// default unconstrained solver behavior.
struct DiagnosticRootLockEntry {
  Combo combo{};
  std::vector<std::string> action_labels;
  std::vector<double> probabilities;

  friend bool operator==(const DiagnosticRootLockEntry &, const DiagnosticRootLockEntry &) =
      default;
};

struct DiagnosticRootLock {
  std::string source_description;
  double source_dev_percent{0.0};
  std::vector<DiagnosticRootLockEntry> entries;
};

struct PostflopSolveOptions {
  std::uint64_t iterations{1};
  std::uint64_t averaging_delay{0};
  std::uint64_t certification_interval{1};
  std::optional<double> target_normalized_nash_conv;
  std::optional<double> target_normalized_max_deviation;
  MemoryPrototype memory_backend{MemoryPrototype::LazyInRam};
  PostflopStatePrecision state_precision{PostflopStatePrecision::Float64};
  bool enable_lossless_isomorphism{true};
  bool enable_canonical_public_dag{true};
  std::uint8_t parallel_action_depth{0};
  // F10.4: when non-null, the tree-root CO strategy is locked to these
  // external probabilities for the whole solve. The pointer must outlive the
  // solve call. Default nullptr = unconstrained solve.
  const DiagnosticRootLock *diagnostic_root_lock{nullptr};
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
  PostflopStatePrecision state_precision{PostflopStatePrecision::Float64};
  std::string external_buffer_file;
  std::vector<double> cumulative_regret;
  std::vector<double> cumulative_strategy;
  std::vector<float> cumulative_regret_float32;
  std::vector<float> cumulative_strategy_float32;
};

struct PostflopCertification {
  std::uint64_t iteration{0};
  std::array<double, 2> profile_value_antes{0.0, 0.0};
  std::array<double, 2> best_response_value_antes{0.0, 0.0};
  double nash_conv_antes{0.0};
  double normalized_nash_conv{0.0};
  double expected_payoff_sum_antes{0.0};
};

struct PostflopSolveTimings {
  double layout_seconds{0.0};
  double initialization_seconds{0.0};
  double traversal_seconds{0.0};
  double regret_application_seconds{0.0};
  double certification_seconds{0.0};
  double finalization_seconds{0.0};
  double run_solver_seconds{0.0};
  double total_seconds{0.0};
};

[[nodiscard]] Result<double, PostflopSolverError>
normalized_max_deviation_gain(const PostflopCertification &certification, Money initial_pot);

struct PostflopSolveResult {
  PostflopCheckpoint checkpoint;
  std::vector<PostflopCertification> convergence;
  PublicTreeStats public_tree;
  std::uint64_t canonical_public_nodes{0};
  std::uint64_t information_sets{0};
  std::uint64_t actions{0};
  std::uint64_t traversed_nodes{0};
  double maximum_normalization_error{0.0};
  PostflopSolveTimings timings;
  PostflopStopReason stop_reason{PostflopStopReason::Completed};
};

struct PostflopStrategyQuery {
  NodeId public_node{0};
  ComboId combo{0};
  std::vector<Action> actions;
  std::vector<double> probabilities;
};

struct PostflopLayoutEstimate {
  PublicTreeStats physical_public_tree;
  std::uint64_t canonical_public_nodes{0};
  std::uint64_t information_sets{0};
  std::uint64_t actions{0};
  std::uint64_t regret_bytes{0};
  std::uint64_t strategy_bytes{0};
};

struct PostflopComboAnalysis {
  ComboId combo{0};
  double reach_weight{0.0};
  double equity{0.0};
  HandCategory hand_category{HandCategory::HighCard};
  std::vector<double> action_probabilities;
};

struct PostflopNodeAnalysis {
  NodeId public_node{0};
  std::uint8_t player_to_act{0};
  // Conditional values at this public node under the average strategy.  The
  // first array uses the solver's net-payoff convention; the second uses the
  // GTO+ display convention, which adds back each player's initial-pot share.
  std::array<double, 2> profile_value_antes{0.0, 0.0};
  std::array<double, 2> gto_plus_ev_antes{0.0, 0.0};
  std::vector<Action> actions;
  std::vector<double> action_frequencies;
  std::vector<PostflopComboAnalysis> combos;
};

[[nodiscard]] PostflopRanges make_uniform_postflop_ranges();

[[nodiscard]] Result<bool, PostflopSolverError>
validate_postflop_ranges(const PostflopTreeConfig &config, const PostflopRanges &ranges);

[[nodiscard]] Result<PostflopSolveResult, PostflopSolverError>
solve_postflop_exact(const PostflopTreeConfig &config, const PostflopSolveOptions &options,
                     const PostflopCheckpoint *resume_from = nullptr);

[[nodiscard]] Result<PostflopSolveResult, PostflopSolverError>
solve_postflop_exact(const PostflopTreeConfig &config, const PostflopRanges &ranges,
                     const PostflopSolveOptions &options,
                     const PostflopCheckpoint *resume_from = nullptr);

class PostflopPreparedTree final {
public:
  ~PostflopPreparedTree();
  PostflopPreparedTree(PostflopPreparedTree &&) noexcept;
  PostflopPreparedTree &operator=(PostflopPreparedTree &&) noexcept;
  PostflopPreparedTree(const PostflopPreparedTree &) = delete;
  PostflopPreparedTree &operator=(const PostflopPreparedTree &) = delete;

private:
  struct Impl;
  explicit PostflopPreparedTree(std::unique_ptr<Impl> implementation);
  std::unique_ptr<Impl> implementation_;

  friend Result<std::shared_ptr<PostflopPreparedTree>, PostflopSolverError>
  prepare_postflop_tree(const PostflopTreeConfig &, const PostflopRanges &, bool, bool, bool);
  friend Result<PostflopSolveResult, PostflopSolverError>
  solve_postflop_exact(PostflopPreparedTree &, const PostflopSolveOptions &,
                       const PostflopCheckpoint *);
  friend Result<PostflopNodeAnalysis, PostflopSolverError>
  analyze_postflop_node(PostflopPreparedTree &, const PostflopCheckpoint &, NodeId);
  friend PostflopLayoutEstimate prepared_postflop_layout_estimate(const PostflopPreparedTree &);
  friend std::shared_ptr<const PublicTree>
  prepared_postflop_public_tree(const std::shared_ptr<PostflopPreparedTree> &);
};

[[nodiscard]] Result<std::shared_ptr<PostflopPreparedTree>, PostflopSolverError>
prepare_postflop_tree(const PostflopTreeConfig &config, const PostflopRanges &ranges,
                      bool enable_lossless_isomorphism = true,
                      bool enable_canonical_public_dag = true, bool prepare_analysis = false);

[[nodiscard]] PostflopLayoutEstimate
prepared_postflop_layout_estimate(const PostflopPreparedTree &prepared);

[[nodiscard]] std::shared_ptr<const PublicTree>
prepared_postflop_public_tree(const std::shared_ptr<PostflopPreparedTree> &prepared);

[[nodiscard]] Result<PostflopSolveResult, PostflopSolverError>
solve_postflop_exact(PostflopPreparedTree &prepared, const PostflopSolveOptions &options,
                     const PostflopCheckpoint *resume_from = nullptr);

[[nodiscard]] Result<PostflopCertification, PostflopSolverError>
certify_postflop_checkpoint(const PostflopTreeConfig &config, const PostflopCheckpoint &checkpoint);

[[nodiscard]] Result<PostflopCertification, PostflopSolverError>
certify_postflop_checkpoint(const PostflopTreeConfig &config, const PostflopRanges &ranges,
                            const PostflopCheckpoint &checkpoint);

[[nodiscard]] Result<PostflopStrategyQuery, PostflopSolverError>
query_postflop_strategy(const PostflopTreeConfig &config, const PostflopCheckpoint &checkpoint,
                        NodeId public_node, ComboId combo);

[[nodiscard]] Result<PostflopStrategyQuery, PostflopSolverError>
query_postflop_strategy(const PostflopTreeConfig &config, const PostflopRanges &ranges,
                        const PostflopCheckpoint &checkpoint, NodeId public_node, ComboId combo);

[[nodiscard]] Result<std::vector<PostflopStrategyQuery>, PostflopSolverError>
query_postflop_strategies(const PostflopTreeConfig &config, const PostflopRanges &ranges,
                          const PostflopCheckpoint &checkpoint, NodeId public_node);

[[nodiscard]] Result<PostflopLayoutEstimate, PostflopSolverError>
estimate_postflop_layout(const PostflopTreeConfig &config, const PostflopRanges &ranges);

[[nodiscard]] Result<PostflopNodeAnalysis, PostflopSolverError>
analyze_postflop_node(const PostflopTreeConfig &config, const PostflopRanges &ranges,
                      const PostflopCheckpoint &checkpoint, NodeId public_node);

[[nodiscard]] Result<PostflopNodeAnalysis, PostflopSolverError>
analyze_postflop_node(PostflopPreparedTree &prepared, const PostflopCheckpoint &checkpoint,
                      NodeId public_node);

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
