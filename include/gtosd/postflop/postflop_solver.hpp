#pragma once

#include "gtosd/core/ranges.hpp"
#include "gtosd/equity/evaluator.hpp"
#include "gtosd/memory/memory.hpp"
#include "gtosd/postflop/root_values.hpp"
#include "gtosd/postflop/solver_memory_ledger.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gtosd {

enum class PostflopSolverError : std::uint8_t;

namespace detail {
class PostflopRuntimeState;
}

namespace detail {

// Shared signed-regret matching primitives used by the production canonical
// current-policy loaders and their regression tests. Codes are stored in a
// uint16 container but always interpreted semantically as int16.
[[nodiscard]] bool regret_match_signed_codes(std::span<const std::uint16_t> raw_codes,
                                             std::span<double> strategy) noexcept;
[[nodiscard]] bool
regret_match_signed_action_major(std::span<const std::uint16_t *const> action_sources,
                                 std::span<float *const> action_strategies,
                                 std::size_t hand_count) noexcept;

// Double-precision S1 oracle for one abstract-infoset update. Every row in
// member_action_values is one physical private state and every column is one
// shared action. Counterfactual weights aggregate regret deltas; own reach
// weights aggregate the average strategy. The production dense traversal may
// use a compressed/vectorized implementation only after matching this oracle.
struct ProductionDcfrBucketUpdate {
  std::vector<double> strategy;
  std::vector<double> current_values;
  std::vector<double> immediate_regret_delta;
  std::vector<double> updated_regret;
  std::vector<double> updated_average_strategy;
};

[[nodiscard]] Result<ProductionDcfrBucketUpdate, PostflopSolverError> production_dcfr_bucket_update(
    std::span<const double> old_regret, std::span<const double> old_average_strategy,
    std::span<const double> member_action_values,
    std::span<const double> counterfactual_member_weights,
    std::span<const double> own_reach_member_weights, std::size_t action_count,
    double regret_update_weight, double average_strategy_weight, double positive_regret_discount,
    double negative_regret_discount);

} // namespace detail

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
enum class PostflopStatePrecision : std::uint8_t {
  Float64,
  Float32,
  Float24RegretFloat16Strategy,
  Float13RegretFloat11Strategy,
  ScaledUint16RegretStrategy,
  ActionMajorFloat13RegretFloat11Strategy
};
enum class PostflopAlgorithm : std::uint8_t {
  CfrPlus = 0,
  DcfrPlus = 1,
  Dcfr = 2,
  HsDcfr30 = 3,
  // Value 11 preserves checkpoint compatibility with the qualified research
  // candidate that became the common production schedule.
  ProductionDcfr = 11
};

// S1 postflop boundary. ExactIdentity names the existing lossless logical
// infoset space explicitly without allocating a per-infoset map. Additional
// modes require a distinct implementation, fingerprint and checkpoint format.
enum class PostflopCardAbstractionMode : std::uint8_t { ExactIdentity = 0, MadeHandValue = 1 };

struct PostflopCardAbstractionPolicy {
  static constexpr std::uint32_t format_major = 1;
  static constexpr std::uint32_t format_minor = 0;

  std::uint32_t major{format_major};
  std::uint32_t minor{format_minor};
  PostflopCardAbstractionMode mode{PostflopCardAbstractionMode::ExactIdentity};

  friend bool operator==(const PostflopCardAbstractionPolicy &,
                         const PostflopCardAbstractionPolicy &) = default;
};

struct PostflopCardAbstractionSummary {
  PostflopCardAbstractionPolicy policy{};
  std::uint64_t exact_information_sets{0};
  std::uint64_t abstract_information_sets{0};
  std::uint64_t exact_action_entries{0};
  std::uint64_t abstract_action_entries{0};
  std::array<std::uint64_t, 3> exact_information_sets_by_street{};
  std::array<std::uint64_t, 3> abstract_information_sets_by_street{};
  std::uint64_t exact_to_abstract_bytes{0};
  std::uint64_t aggregation_weight_bytes{0};
  std::uint64_t decision_offset_bytes{0};
  std::uint64_t bucket_member_offset_bytes{0};
  std::uint64_t bucket_member_index_bytes{0};
  std::uint64_t mapping_bytes{0};
  bool perfect_recall_verified{false};
  bool checkpoint_identity_preserved{false};
  bool solver_supported{false};
  std::string fingerprint;

  friend bool operator==(const PostflopCardAbstractionSummary &,
                         const PostflopCardAbstractionSummary &) = default;
};

[[nodiscard]] bool
validate_postflop_card_abstraction_policy(const PostflopCardAbstractionPolicy &policy) noexcept;
[[nodiscard]] const char *postflop_card_abstraction_name(PostflopCardAbstractionMode mode) noexcept;

struct PostflopCardAbstractionEntry {
  std::uint64_t exact_information_set{0};
  std::uint64_t abstract_information_set{0};
  std::uint16_t aggregation_weight_basis_points{0};
};

[[nodiscard]] constexpr bool
is_production_dcfr_algorithm(const PostflopAlgorithm algorithm) noexcept {
  return algorithm == PostflopAlgorithm::ProductionDcfr;
}

[[nodiscard]] constexpr bool
is_signed_scaled_dcfr_algorithm(const PostflopAlgorithm algorithm) noexcept {
  return algorithm == PostflopAlgorithm::Dcfr || algorithm == PostflopAlgorithm::HsDcfr30 ||
         is_production_dcfr_algorithm(algorithm);
}

// Qualified common production schedule. The average strategy resets at
// one-based iterations 1, 2, 5, 17 and 65, then retains the final epoch.
// Cubic additive weights are exactly equivalent up to common scale to the
// recursive gamma=3 average discount. Signed DCFR uses alpha=1.5, beta=0;
// its clock switches to the upstream one-step lag after iteration 65.
struct ProductionDcfrSchedulePoint {
  std::uint64_t epoch_start_iteration{0};
  std::uint64_t epoch_index{0};
  std::uint64_t regret_discount_iteration{0};
  double average_strategy_weight{1.0};
  bool reset_average_strategy{true};
};

[[nodiscard]] ProductionDcfrSchedulePoint
production_dcfr_schedule(std::uint64_t iteration) noexcept;

// Training-free Hyperparameter Schedule from Zhang, McAleer and Sandholm,
// "Faster Game Solving via Hyperparameter Schedules". The production
// implementation clamps the published linear schedule at the theorem's
// admissible bounds; this is identical to the paper throughout its 1,000
// iteration experimental horizon and remains well-defined for unbounded runs.
struct HsDcfrSchedulePoint {
  double alpha{1.0};
  double beta{-1.0};
  double gamma{30.0};
};

[[nodiscard]] HsDcfrSchedulePoint hs_dcfr30_schedule(std::uint64_t iteration) noexcept;
struct PostflopCertification;
struct PostflopCheckpoint;
class PostflopPreparedTree;

// Read-only diagnostic for the original CFR regret-based-pruning condition.
// It never participates in traversal, regret updates, strategy averaging or
// checkpoint serialization.  In particular, observations made on DCFR state
// are eligibility proxies only: the original CFR proof does not cover the
// production discount schedule.
struct RbpReadOnlyWorkEstimate {
  std::uint64_t public_nodes{0};
  std::uint64_t decision_nodes{0};
  std::uint64_t chance_nodes{0};
  std::uint64_t chance_outcomes{0};
  std::uint64_t fold_terminals{0};
  std::uint64_t showdown_terminals{0};
  std::uint64_t regret_entries{0};
  std::uint64_t strategy_entries{0};
};

struct RbpReadOnlyPlayerSnapshot {
  std::uint64_t decisions{0};
  std::uint64_t actions{0};
  std::uint64_t zero_policy_actions{0};
  std::uint64_t negative_regret_actions{0};
  std::uint64_t original_formula_candidates{0};
  std::uint64_t decisions_with_candidates{0};
  std::uint64_t decisions_with_all_but_one_candidate{0};
  std::uint64_t persistent_candidates{0};
  std::uint64_t new_candidates{0};
  std::uint64_t reactivated_candidates{0};
  std::array<std::uint64_t, 3> candidates_by_street{};
  std::array<std::uint64_t, 9> candidates_by_decision_action_count{};
  double minimum_candidate_regret_antes{0.0};
  double mean_candidate_regret_antes{0.0};
  double maximum_candidate_regret_antes{0.0};
  double minimum_threshold_multiple{0.0};
  double mean_threshold_multiple{0.0};
  double maximum_threshold_multiple{0.0};
  double mean_negative_regret_threshold_multiple{0.0};
  double maximum_negative_regret_threshold_multiple{0.0};
  RbpReadOnlyWorkEstimate structural_upper_bound{};
};

struct RbpReadOnlySnapshot {
  std::uint64_t iteration{0};
  std::array<RbpReadOnlyPlayerSnapshot, 2> players{};
  std::uint64_t structurally_unreachable_action_entries{0};
  // Exact opponent counterfactual zero reach is deliberately not inferred
  // from a zero current-policy action.  This post-state audit does not retain
  // the traversal reach vectors, so the metric is explicitly unavailable.
  bool exact_zero_counterfactual_reach_available{false};
  std::uint64_t exact_zero_counterfactual_reach_actions{0};
  std::uint64_t metadata_bytes{0};
  double metadata_bytes_per_action{0.0};
  double metadata_bytes_per_decision{0.0};
};

class RbpReadOnlyTelemetry final {
public:
  RbpReadOnlyTelemetry();
  ~RbpReadOnlyTelemetry();
  RbpReadOnlyTelemetry(RbpReadOnlyTelemetry &&) noexcept;
  RbpReadOnlyTelemetry &operator=(RbpReadOnlyTelemetry &&) noexcept;
  RbpReadOnlyTelemetry(const RbpReadOnlyTelemetry &) = delete;
  RbpReadOnlyTelemetry &operator=(const RbpReadOnlyTelemetry &) = delete;

  [[nodiscard]] Result<RbpReadOnlySnapshot, PostflopSolverError>
  observe(const PostflopPreparedTree &prepared, const PostflopCheckpoint &checkpoint);
  [[nodiscard]] std::uint64_t metadata_bytes() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> implementation_;
};

// F10.4 diagnostic external root lock: fixes the tree-root strategy of the
// CO player to externally observed combo-per-combo probabilities. This is a
// diagnostic-only constraint (controlled posteriors); it never changes the
// default unconstrained solver behavior.
struct DiagnosticRootLockEntry {
  Combo combo{};
  std::vector<std::string> action_labels;
  std::vector<double> probabilities;

  friend bool operator==(const DiagnosticRootLockEntry &,
                         const DiagnosticRootLockEntry &) = default;
};

struct DiagnosticRootLock {
  std::string source_description;
  double source_dev_percent{0.0};
  std::vector<DiagnosticRootLockEntry> entries;
};

enum class PostflopReplayProducer : std::uint8_t {
  Fold,
  Showdown,
  DecisionSubtree,
  Chance,
  TransformedChanceOrSubtree
};

// Diagnostic-only, bounded capture of real production decision-node updates.
// It is inert unless the replay build gate and this runtime option are both on.
struct PostflopRealNodeReplayCapture {
  std::uint64_t seed{0x47544f5344524e52ULL};
  std::uint64_t maximum_samples{384};
  std::uint64_t samples_per_stratum{2};
  std::uint64_t sampling_modulus{257};
  std::vector<std::uint64_t> iterations;
};

struct PostflopRealNodeReplaySample {
  std::uint64_t iteration{0};
  NodeId representative_node{0};
  std::uint64_t board_mask{0};
  std::uint64_t structural_signature{0};
  std::uint8_t street{0};
  std::uint8_t update_player{0};
  std::uint8_t actor{0};
  std::uint8_t action_count{0};
  std::uint16_t hand_count{0};
  double regret_update_weight{1.0};
  double strategy_weight{0.0};
  double positive_regret_discount{1.0};
  double negative_regret_discount{1.0};
  float old_regret_scale{0.0F};
  float old_strategy_scale{0.0F};
  float resulting_regret_scale{0.0F};
  float resulting_strategy_scale{0.0F};
  std::vector<PostflopReplayProducer> producers;
  std::vector<std::uint16_t> old_regret_codes;
  std::vector<std::uint16_t> old_strategy_codes;
  std::vector<float> current_policy;
  std::vector<float> actor_reach;
  std::vector<float> opponent_reach;
  std::vector<float> action_values;
  std::vector<float> current_values;
  std::vector<float> immediate_regret_delta;
  std::vector<float> average_contribution;
  std::vector<float> resulting_regret_values;
  std::vector<float> resulting_strategy_values;
  std::vector<std::uint16_t> resulting_regret_codes;
  std::vector<std::uint16_t> resulting_strategy_codes;
  std::vector<float> parent_returned_values;
};

struct PostflopRealNodeReplayCorpus {
  std::uint32_t format_major{1};
  std::uint32_t format_minor{0};
  std::string game_fingerprint;
  std::uint64_t seed{0};
  std::uint64_t maximum_samples{0};
  std::uint64_t eligible_updates{0};
  std::uint64_t retained_updates{0};
  std::vector<PostflopRealNodeReplaySample> samples;
};

struct PostflopSolveOptions {
  // Zero means target-driven with no iteration limit. This mode requires one
  // convergence target and stops only when a certification satisfies it, or
  // when the caller pauses/cancels or a real solver error occurs.
  std::uint64_t iterations{1};
  std::uint64_t averaging_delay{0};
  std::uint64_t certification_interval{1};
  std::optional<double> target_normalized_nash_conv;
  std::optional<double> target_normalized_max_deviation;
  // Selects the mathematical boundary of the convergence gate. Existing
  // callers retain <=; GTO+ Target dEV benchmarks use strict <.
  bool strict_target{false};
  // Optional user-configured resident-working-set budget. Zero preserves the
  // ordinary resident backend. A nonzero value explicitly opts into a local,
  // OS-page-backed exact state when the state plus the already-resident layout
  // cannot fit. This changes residency only: codec, update schedule and
  // checkpoint bytes remain unchanged. Persisting that runtime state requires
  // explicit materialization. External solver-memory references must never
  // populate this field.
  std::uint64_t resident_working_set_budget_bytes{0};
  MemoryPrototype memory_backend{MemoryPrototype::LazyInRam};
  PostflopStatePrecision state_precision{PostflopStatePrecision::Float64};
  PostflopAlgorithm algorithm{PostflopAlgorithm::CfrPlus};
  PostflopCardAbstractionPolicy card_abstraction{};
  // DCFR+ keeps CFR+'s non-negative regret projection. DCFR and ProductionDcfr
  // store signed regrets. ProductionDcfr uses the fixed qualified schedule;
  // dcfr_average_exponent is ignored.
  // HsDcfr30 uses the published dynamic alpha/beta/gamma schedule and ignores
  // both fixed exponents. The parameters are also ignored by CfrPlus.
  double dcfr_positive_regret_exponent{1.5};
  double dcfr_average_exponent{2.0};
  bool enable_lossless_isomorphism{true};
  bool enable_canonical_public_dag{true};
  // Observational only: disabling detailed accounting must not alter solver
  // allocations, traversal, checkpoint identity, or mathematical results.
  bool enable_detailed_memory_accounting{true};
  std::uint8_t parallel_action_depth{0};
  // F10.4: when non-null, the tree-root CO strategy is locked to these
  // external probabilities for the whole solve. The pointer must outlive the
  // solve call. Default nullptr = unconstrained solve.
  const DiagnosticRootLock *diagnostic_root_lock{nullptr};
  const PostflopRealNodeReplayCapture *diagnostic_real_node_replay{nullptr};
  // Research-only, default-off Pure-CFR trajectory probe. It reinterprets
  // the signed scaled regret payload as cumulative Q, selects a deterministic
  // argmax action per hand, performs one uncompressed Pure-CFR update per
  // iteration, and records the Sync-PCFR pursuit horizon. It does not apply
  // phase compression and is intentionally unavailable for resume/checkpoint
  // promotion decisions.
  bool diagnostic_pure_cfr_trajectory{false};
  std::uint64_t diagnostic_pure_cfr_phase_cap{1'000'000U};
  // OutOfCore uses this as its persistent Float64 buffer. LazyInRam may also
  // set it together with a nonzero resident budget to back the exact scaled
  // runtime mapping with a temporary local file instead of system commit.
  // The latter path is experimental, delete-on-close, and is not serialized.
  std::string backing_file;
  std::function<void(const PostflopCertification &)> progress_callback;
  std::function<bool(const PostflopCertification &, const PostflopCheckpoint &)>
      checkpoint_callback;
  std::function<PostflopControlCommand(std::uint64_t)> control_callback;
};

// Versioned application profile used by new production solves. Mathematical
// identity and numerical storage are intentionally absent from this request:
// resolve_postflop_production_options fixes them in one shared place. Callers
// may select only runtime boundaries that do not change ProductionDcfr.
struct PostflopProductionSolveRequest {
  static constexpr std::uint32_t profile_major = 1;
  static constexpr std::uint32_t profile_minor = 0;
  static constexpr std::uint64_t certification_interval = 20;
  static constexpr std::uint8_t parallel_action_depth = 7;

  std::uint64_t iterations{0};
  std::optional<double> target_normalized_nash_conv;
  std::optional<double> target_normalized_max_deviation;
  std::uint64_t resident_working_set_budget_bytes{0};
  MemoryPrototype memory_backend{MemoryPrototype::LazyInRam};
  bool enable_detailed_memory_accounting{true};
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
  // Number of decision-node scale pairs in the action-major uint16 backend.
  // Zero for all legacy state formats.
  std::uint64_t decision_node_count{0};
  PostflopStatePrecision state_precision{PostflopStatePrecision::Float64};
  PostflopAlgorithm algorithm{PostflopAlgorithm::CfrPlus};
  double dcfr_positive_regret_exponent{1.5};
  double dcfr_average_exponent{2.0};
  std::string external_buffer_file;
  std::vector<double> cumulative_regret;
  std::vector<double> cumulative_strategy;
  std::vector<float> cumulative_regret_float32;
  std::vector<float> cumulative_strategy_float32;
  // Explicit compressed mode: each regret is an IEEE float32 rounded to its
  // upper 24 bits (8-bit exponent + 15-bit fraction), while average-strategy
  // values use IEEE binary16. Computation remains float64.
  std::vector<std::uint8_t> cumulative_regret_float24;
  std::vector<std::uint16_t> cumulative_strategy_float16;
  // Packed three-byte/action mode: unsigned regret float13 (E8M5; CFR+
  // regrets are non-negative) and unsigned average-strategy float11 (E5M6).
  // Traversal and payoff computation remain float64.
  std::vector<std::uint8_t> cumulative_compact_state;
  // Node-scaled action-major representation. For a decision with L local
  // hands and A actions, entry (action, local) is stored at
  // action_base + action * L + local. Decoded values are code * node scale;
  // the common scale cancels during regret matching/policy normalization.
  std::vector<std::uint16_t> cumulative_regret_uint16;
  std::vector<std::uint16_t> cumulative_strategy_uint16;
  std::vector<float> regret_node_scale;
  std::vector<float> strategy_node_scale;
  // Runtime-only owner for the exact OS-page-backed representation selected
  // by an explicit working-set target. It is deliberately absent from the
  // serialized checkpoint identity; public query/analysis APIs consume it
  // through the same ActionBuffers view as resident vectors.
  std::shared_ptr<detail::PostflopRuntimeState> runtime_state;
};

// Resolves the only profile accepted by product API/CLI/GUI entry points for a
// new solve. A resume must identify the same fixed 1.5/0/3 contract. CFR+ and
// ambiguous legacy checkpoints are rejected.
[[nodiscard]] Result<PostflopSolveOptions, PostflopSolverError>
resolve_postflop_production_options(const PostflopProductionSolveRequest &request,
                                    const PostflopCheckpoint *resume_from = nullptr);

[[nodiscard]] const char *postflop_algorithm_name(PostflopAlgorithm algorithm) noexcept;
[[nodiscard]] const char *postflop_state_precision_name(PostflopStatePrecision precision) noexcept;

struct PostflopWorkCounters {
  std::uint64_t visited_nodes{0};
  std::uint64_t decision_node_evaluations{0};
  std::uint64_t chance_node_evaluations{0};
  std::uint64_t chance_outcome_evaluations{0};
  std::uint64_t terminal_evaluations{0};
  std::uint64_t fold_terminal_evaluations{0};
  std::uint64_t showdown_terminal_evaluations{0};
  std::uint64_t regret_update_entries{0};
  std::uint64_t strategy_update_entries{0};
};

struct PostflopCertification {
  std::uint64_t iteration{0};
  std::array<double, 2> profile_value_antes{0.0, 0.0};
  std::array<double, 2> best_response_value_antes{0.0, 0.0};
  double nash_conv_antes{0.0};
  double normalized_nash_conv{0.0};
  double expected_payoff_sum_antes{0.0};
  // Runtime-only telemetry captured immediately after the exact
  // certification. These fields are intentionally excluded from checkpoint
  // identity: they describe the execution, not the mathematical state.
  double solver_elapsed_seconds{0.0};
  double traversal_elapsed_seconds{0.0};
  double certification_elapsed_seconds{0.0};
  std::uint64_t traversed_nodes{0};
  PostflopWorkCounters work_counters;
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

struct PostflopPureCfrTrajectoryPoint {
  std::uint64_t iteration{0};
  std::uint64_t minimum_phase{1};
  std::uint64_t finite_pursuits{0};
  std::uint64_t unit_pursuits{0};
  double pursuit_scan_seconds{0.0};
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
  PostflopWorkCounters work_counters;
  double maximum_normalization_error{0.0};
  PostflopSolveTimings timings;
  std::optional<SolverMemoryLedgerSnapshot> solver_memory_accounting;
  std::uint64_t predicted_solver_managed_logical_bytes{0};
  std::uint64_t predicted_solver_managed_allocated_bytes{0};
  bool solver_memory_accounting_complete{false};
  PostflopStopReason stop_reason{PostflopStopReason::Completed};
  std::optional<PostflopRealNodeReplayCorpus> diagnostic_real_node_replay;
  std::vector<PostflopPureCfrTrajectoryPoint> diagnostic_pure_cfr_trajectory;
};

// S1 feasibility runner. It executes the made-hand-value mapping on the
// physical postflop tree with a double-precision bucket state, then evaluates
// the lifted average strategy and its best responses in the original game. It
// is deliberately separate from PostflopCheckpoint and the product solver.
struct PostflopBucketCheckpoint {
  static constexpr std::uint32_t format_major = 1;
  static constexpr std::uint32_t format_minor = 0;

  std::uint32_t major{format_major};
  std::uint32_t minor{format_minor};
  std::string game_fingerprint;
  std::string abstraction_fingerprint;
  std::uint64_t completed_iterations{0};
  std::uint64_t action_count{0};
  PostflopAlgorithm algorithm{PostflopAlgorithm::ProductionDcfr};
  PostflopStatePrecision state_precision{PostflopStatePrecision::Float64};
  double dcfr_positive_regret_exponent{1.5};
  double dcfr_average_exponent{3.0};
  std::vector<double> cumulative_regret;
  std::vector<double> cumulative_strategy;

  friend bool operator==(const PostflopBucketCheckpoint &,
                         const PostflopBucketCheckpoint &) = default;
};

struct PostflopBucketTraversalProbeResult {
  PostflopCardAbstractionSummary abstraction;
  std::uint64_t completed_iterations{0};
  std::uint64_t state_bytes{0};
  std::uint64_t traversal_owned_capacity_bytes{0};
  std::uint64_t state_update_passes{0};
  std::uint64_t strategy_reset_passes{0};
  double preparation_seconds{0.0};
  double layout_preparation_seconds{0.0};
  double abstraction_preparation_seconds{0.0};
  double setup_seconds{0.0};
  double rank_preparation_seconds{0.0};
  double state_initialization_seconds{0.0};
  double traversal_initialization_seconds{0.0};
  double traversal_seconds{0.0};
  double certification_seconds{0.0};
  double finalization_seconds{0.0};
  double operational_seconds{0.0};
  double maximum_oracle_deviation{0.0};
  std::uint64_t oracle_updates_checked{0};
  std::uint64_t oracle_updates_total{0};
  PostflopWorkCounters traversal_work_counters;
  PostflopCertification certification;
  PostflopBucketCheckpoint checkpoint;
  std::string state_fingerprint;
};

struct PostflopStrategyQuery {
  NodeId public_node{0};
  ComboId combo{0};
  std::vector<Action> actions;
  std::vector<double> probabilities;
};

// Compact prepared-tree navigation for decision histories.  It exposes the
// canonical solver topology without materializing a second PublicTree with a
// full PublicState in every node.  Chance navigation remains represented by
// the existing physical browser tree until its card-conditioned handle is
// migrated to the same compact API.
struct PostflopPreparedActionEdge {
  Action action{};
  NodeId child{0};
};

struct PostflopLayoutEstimate {
  PublicTreeStats physical_public_tree;
  std::uint64_t canonical_public_nodes{0};
  std::uint64_t information_sets{0};
  std::uint64_t actions{0};
  std::uint64_t regret_bytes{0};
  std::uint64_t strategy_bytes{0};
};

// Read-only topology row for architectural traversal feasibility studies.
// A work unit is one canonical turn-to-river representative subtree evaluated
// for one alternating-update player.  No solver state is allocated or read.
struct PostflopRiverWorkUnit {
  std::uint32_t source_chance_node{0};
  std::uint32_t river_root_node{0};
  std::uint64_t board_mask{0};
  std::uint64_t structural_signature{0};
  std::uint64_t relaxed_signature{0};
  std::uint64_t payoff_signature{0};
  std::uint64_t state_shape_signature{0};
  std::uint64_t state_begin{0};
  std::uint64_t state_end{0};
  std::uint64_t showdown_work{0};
  std::uint64_t action_entries{0};
  std::uint64_t value_entries{0};
  std::uint64_t state_entries{0};
  std::uint64_t terminal_bytes{0};
  std::uint64_t value_bytes{0};
  std::uint64_t state_bytes{0};
  std::uint64_t reach_bytes{0};
  std::uint32_t public_nodes{0};
  std::uint32_t decision_nodes{0};
  std::uint32_t showdown_terminals{0};
  std::uint32_t chance_descendants{0};
  std::uint32_t identity_transforms{0};
  std::uint32_t nonidentity_transforms{0};
  std::uint16_t live_hero_combos{0};
  std::uint16_t live_opponent_combos{0};
  std::uint16_t rank_count{0};
  std::uint16_t source_sibling_count{0};
  std::uint8_t actor{0};
  std::uint8_t update_player{0};
  std::uint8_t action_count{0};
  std::uint8_t terminal_child_pattern{0};
  bool state_interval_present{false};
  bool root_transform_identity{true};
};

struct PostflopArchitecturalTopology {
  std::vector<PostflopRiverWorkUnit> river_work_units;
  std::uint64_t analyzed_public_nodes{0};
  std::uint64_t temporary_metadata_bytes{0};
  std::uint64_t invalid_or_cyclic_units{0};
  std::uint64_t control_plan_ops{0};
  std::uint64_t control_plan_bytes{0};
  std::uint64_t recursive_control_checksum{0};
  std::uint64_t linear_control_checksum{0};
  double recursive_control_seconds{0.0};
  double linear_control_seconds{0.0};
};

struct PostflopArchitecturalShadowSample {
  std::string workload_class;
  std::uint8_t update_player{0};
  std::uint8_t batch_width{0};
  std::uint64_t structural_signature{0};
  std::uint64_t modeled_bytes{0};
  std::uint64_t copied_state_bytes{0};
  std::uint64_t copied_scale_bytes{0};
  std::uint64_t descriptor_bytes{0};
  double local_schedule_seconds{0.0};
  double global_frontier_seconds{0.0};
  double speedup{0.0};
  bool parent_values_bit_equal{false};
  bool regret_codes_bit_equal{false};
  bool strategy_codes_bit_equal{false};
  bool regret_scales_bit_equal{false};
  bool strategy_scales_bit_equal{false};
  std::vector<std::uint32_t> river_root_nodes;
  std::vector<std::uint32_t> source_chance_nodes;
};

struct PostflopArchitecturalShadowReport {
  std::vector<PostflopArchitecturalShadowSample> samples;
  std::uint64_t repetitions{0};
  std::uint64_t additional_bytes{0};
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
  prepare_postflop_tree(const PostflopTreeConfig &, const PostflopRanges &, bool, bool, bool,
                        PostflopCardAbstractionPolicy);
  friend Result<PostflopSolveResult, PostflopSolverError>
  solve_postflop_exact(PostflopPreparedTree &, const PostflopSolveOptions &,
                       const PostflopCheckpoint *);
  friend Result<PostflopNodeAnalysis, PostflopSolverError>
  analyze_postflop_node(PostflopPreparedTree &, const PostflopCheckpoint &, NodeId);
  friend PostflopLayoutEstimate prepared_postflop_layout_estimate(const PostflopPreparedTree &);
  friend Result<PostflopCardAbstractionSummary, PostflopSolverError>
  prepared_postflop_card_abstraction(const PostflopPreparedTree &);
  friend Result<PostflopCardAbstractionEntry, PostflopSolverError>
  prepared_postflop_card_abstraction_entry(const PostflopPreparedTree &, std::uint64_t);
  friend Result<PostflopBucketTraversalProbeResult, PostflopSolverError>
  run_postflop_bucket_traversal_probe(PostflopPreparedTree &, std::uint64_t,
                                      const PostflopBucketCheckpoint *);
  friend Result<std::uint64_t, PostflopSolverError>
  prepared_postflop_solver_state_bytes(const PostflopPreparedTree &, PostflopStatePrecision);
  friend Result<std::vector<PostflopPreparedActionEdge>, PostflopSolverError>
  prepared_postflop_action_edges(const PostflopPreparedTree &, NodeId);
  friend Result<PostflopArchitecturalTopology, PostflopSolverError>
  inspect_postflop_architectural_topology(const PostflopPreparedTree &);
  friend Result<PostflopArchitecturalShadowReport, PostflopSolverError>
  benchmark_postflop_architectural_shadow(PostflopPreparedTree &, PostflopCheckpoint &,
                                          std::uint8_t, std::uint64_t);
  friend class RbpReadOnlyTelemetry;
  friend std::shared_ptr<const PublicTree>
  prepared_postflop_public_tree(const std::shared_ptr<PostflopPreparedTree> &);
};

[[nodiscard]] Result<std::shared_ptr<PostflopPreparedTree>, PostflopSolverError>
prepare_postflop_tree(const PostflopTreeConfig &config, const PostflopRanges &ranges,
                      bool enable_lossless_isomorphism = true,
                      bool enable_canonical_public_dag = true, bool prepare_analysis = false,
                      PostflopCardAbstractionPolicy card_abstraction = {});

[[nodiscard]] PostflopLayoutEstimate
prepared_postflop_layout_estimate(const PostflopPreparedTree &prepared);

// The identity policy uses an implicit mapping and therefore consumes zero
// bytes regardless of the number of exact infosets.
[[nodiscard]] Result<PostflopCardAbstractionSummary, PostflopSolverError>
prepared_postflop_card_abstraction(const PostflopPreparedTree &prepared);

[[nodiscard]] Result<PostflopCardAbstractionEntry, PostflopSolverError>
prepared_postflop_card_abstraction_entry(const PostflopPreparedTree &prepared,
                                         std::uint64_t exact_information_set);

[[nodiscard]] Result<PostflopBucketTraversalProbeResult, PostflopSolverError>
run_postflop_bucket_traversal_probe(PostflopPreparedTree &prepared, std::uint64_t iterations,
                                    const PostflopBucketCheckpoint *resume_from = nullptr);

[[nodiscard]] Result<bool, PostflopSolverError>
save_postflop_bucket_checkpoint(const PostflopBucketCheckpoint &checkpoint,
                                const std::string &path);

[[nodiscard]] Result<PostflopBucketCheckpoint, PostflopSolverError>
load_postflop_bucket_checkpoint(const std::string &path);

[[nodiscard]] Result<std::uint64_t, PostflopSolverError>
map_postflop_exact_infoset(const PostflopCardAbstractionSummary &summary,
                           std::uint64_t exact_information_set);

// Logical solver-owned regret + average-strategy payload for the selected
// numerical profile. This excludes prepared layout, ranks, worker scratch,
// process overhead and persistence, which remain separate memory scopes.
[[nodiscard]] Result<std::uint64_t, PostflopSolverError>
prepared_postflop_solver_state_bytes(const PostflopPreparedTree &prepared,
                                     PostflopStatePrecision precision);

[[nodiscard]] Result<std::vector<PostflopPreparedActionEdge>, PostflopSolverError>
prepared_postflop_action_edges(const PostflopPreparedTree &prepared, NodeId node);

// Explicit external inspector used by architecture tooling. It does not
// participate in traversal and its temporary rows are released by the caller.
[[nodiscard]] Result<PostflopArchitecturalTopology, PostflopSolverError>
inspect_postflop_architectural_topology(const PostflopPreparedTree &prepared);

// Diagnostic-only Level-1 shadow. It snapshots and restores the selected
// disjoint state ranges, compares current local scheduling with a bounded
// global frontier on real production reach/state/board data, and never leaves
// checkpoint bytes modified.
[[nodiscard]] Result<PostflopArchitecturalShadowReport, PostflopSolverError>
benchmark_postflop_architectural_shadow(PostflopPreparedTree &prepared,
                                        PostflopCheckpoint &checkpoint,
                                        std::uint8_t batch_width = 8U,
                                        std::uint64_t repetitions = 3U);

[[nodiscard]] std::shared_ptr<const PublicTree>
prepared_postflop_public_tree(const std::shared_ptr<PostflopPreparedTree> &prepared);

[[nodiscard]] Result<PostflopSolveResult, PostflopSolverError>
solve_postflop_exact(PostflopPreparedTree &prepared, const PostflopSolveOptions &options,
                     const PostflopCheckpoint *resume_from = nullptr);

// Explicitly copies a runtime OS-page-backed exact state into the ordinary
// checkpoint vectors. This is intended for persistence/export and may raise
// the caller's working set by the full logical state size.
[[nodiscard]] Result<bool, PostflopSolverError>
materialize_postflop_checkpoint_state(PostflopCheckpoint &checkpoint);

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

// Exact root CFVs under the checkpoint's average strategy. Each conditional
// value divides out the compatible opponent reach used by the traversal; the
// accompanying counterfactual reach makes the normalization explicit.
[[nodiscard]] Result<PostflopRootCounterfactualValues, PostflopSolverError>
derive_postflop_root_counterfactual_values(const PostflopTreeConfig &config,
                                           const PostflopRanges &ranges,
                                           const PostflopCheckpoint &checkpoint);

// Exact per-combo values when each player independently best-responds to the
// checkpoint's average strategy. This report is deliberately mode-tagged and
// cannot be consumed as an average-policy decomposition boundary.
[[nodiscard]] Result<PostflopRootCounterfactualValues, PostflopSolverError>
derive_postflop_root_best_response_values(const PostflopTreeConfig &config,
                                          const PostflopRanges &ranges,
                                          const PostflopCheckpoint &checkpoint);

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
