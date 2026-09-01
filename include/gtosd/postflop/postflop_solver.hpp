#pragma once

#include "gtosd/core/ranges.hpp"
#include "gtosd/equity/evaluator.hpp"
#include "gtosd/memory/memory.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gtosd {

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
enum class PostflopAlgorithm : std::uint8_t { CfrPlus, DcfrPlus, Dcfr, HsDcfr30 };

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
  MemoryPrototype memory_backend{MemoryPrototype::LazyInRam};
  PostflopStatePrecision state_precision{PostflopStatePrecision::Float64};
  PostflopAlgorithm algorithm{PostflopAlgorithm::CfrPlus};
  // DCFR+ keeps CFR+'s non-negative regret projection. DCFR stores signed
  // regrets and applies fixed Brown-Sandholm discounts. HsDcfr30 uses the
  // published dynamic alpha/beta/gamma schedule and ignores these two fixed
  // exponents. The parameters are also ignored by CfrPlus.
  double dcfr_positive_regret_exponent{1.5};
  double dcfr_average_exponent{2.0};
  bool enable_lossless_isomorphism{true};
  bool enable_canonical_public_dag{true};
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
};

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
  PostflopStopReason stop_reason{PostflopStopReason::Completed};
  std::optional<PostflopRealNodeReplayCorpus> diagnostic_real_node_replay;
  std::vector<PostflopPureCfrTrajectoryPoint> diagnostic_pure_cfr_trajectory;
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
  prepare_postflop_tree(const PostflopTreeConfig &, const PostflopRanges &, bool, bool, bool);
  friend Result<PostflopSolveResult, PostflopSolverError>
  solve_postflop_exact(PostflopPreparedTree &, const PostflopSolveOptions &,
                       const PostflopCheckpoint *);
  friend Result<PostflopNodeAnalysis, PostflopSolverError>
  analyze_postflop_node(PostflopPreparedTree &, const PostflopCheckpoint &, NodeId);
  friend PostflopLayoutEstimate prepared_postflop_layout_estimate(const PostflopPreparedTree &);
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
                      bool enable_canonical_public_dag = true, bool prepare_analysis = false);

[[nodiscard]] PostflopLayoutEstimate
prepared_postflop_layout_estimate(const PostflopPreparedTree &prepared);

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
