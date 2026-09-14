#pragma once

#include "gtosd/core/game.hpp"
#include "gtosd/core/ranges.hpp"
#include "gtosd/equity/evaluator.hpp"
#include "gtosd/tree/config.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace gtosd {

enum class HuPreflopError : std::uint8_t {
  InvalidConfiguration,
  GameFailure,
  NodeOverflow,
  CountOverflow,
  EquityFailure,
  NumericalFailure,
  MemoryFailure,
  IoFailure,
  IntegrityFailure,
  UnsupportedVersion
};

enum class HuPreflopDecisionStage : std::uint8_t {
  Root,
  LimpOption,
  FacingSmallOpen,
  FacingLargeOpen,
  FinalResponse,
  FacingAllIn
};

enum class HuPreflopNodeKind : std::uint8_t {
  Decision,
  PostflopEntry,
  TerminalFold,
  TerminalAllIn
};

enum class HuPreflopContinuationValueMode : std::uint8_t { AverageStrategy, ExactBestResponse };

struct HuPreflopConfig {
  Money effective_stack{};
  Money ante{};
  std::array<Money, 2> open_targets{};
  std::array<Money, 2> response_targets{};
  std::array<PotPercentage, 3> postflop_sizes{};
  Money postflop_minimum_bet{};
  RakeConfig rake{};
  bool include_all_in{true};
  bool allow_configured_incomplete_raise{false};
};

struct HuPreflopEdge {
  Action action{};
  std::uint32_t child{0};
};

struct HuPreflopNode {
  std::uint32_t id{0};
  HuPreflopNodeKind kind{HuPreflopNodeKind::Decision};
  HuPreflopDecisionStage stage{HuPreflopDecisionStage::Root};
  PublicState state{};
  std::vector<HuPreflopEdge> edges;
};

struct HuPreflopTreeStats {
  std::uint64_t node_count{0};
  std::uint64_t edge_count{0};
  std::uint64_t decision_nodes{0};
  std::uint64_t postflop_entries{0};
  std::uint64_t terminal_folds{0};
  std::uint64_t terminal_all_ins{0};
  std::uint32_t maximum_depth{0};
};

struct HuPreflopTree {
  HuPreflopConfig config{};
  std::uint32_t root{0};
  std::vector<HuPreflopNode> nodes;
  HuPreflopTreeStats stats{};
  std::string fingerprint;
};

struct HuPostflopPublicStats {
  std::uint64_t represented_nodes{0};
  std::uint64_t action_edges{0};
  std::uint64_t decision_nodes{0};
  std::uint64_t chance_frontiers{0};
  std::uint64_t terminal_folds{0};
  std::uint64_t terminal_showdowns{0};
  std::uint64_t terminal_all_in_runouts{0};
  std::uint64_t memoized_states{0};
  std::uint32_t maximum_subtree_depth{0};
  std::uint8_t maximum_observed_raise_count{0};
  bool core_raise_safety_limit_reached{false};
  bool natural_stack_termination_proven{false};
};

constexpr std::size_t hu_preflop_hand_class_count = 81U;
constexpr std::size_t hu_preflop_maximum_actions = 5U;

struct HuPreflopBlueprintDecision {
  std::uint32_t node_id{0};
  std::uint8_t player{0};
  std::uint8_t action_count{0};
  std::array<std::array<double, hu_preflop_maximum_actions>, hu_preflop_hand_class_count>
      strategy{};

  friend bool operator==(const HuPreflopBlueprintDecision &,
                         const HuPreflopBlueprintDecision &) = default;
};

// Dense preflop-only policy. Postflop continuation policies deliberately do
// not belong here: a decomposition boundary binds them through their own
// fingerprint and CFV records.
struct HuPreflopBlueprint {
  static constexpr std::uint32_t format_major = 1U;
  static constexpr std::uint32_t format_minor = 0U;

  std::uint32_t major{format_major};
  std::uint32_t minor{format_minor};
  std::string tree_fingerprint;
  std::string algorithm;
  std::uint64_t iterations{0};
  std::vector<HuPreflopBlueprintDecision> decisions;
  std::string fingerprint;

  friend bool operator==(const HuPreflopBlueprint &, const HuPreflopBlueprint &) = default;
};

// Optional research diagnostic for chart navigation. Values are conditional on
// the public path to node_id and on the acting player's preflop class. Previous
// opponent actions determine the importance weight; previous actions by the
// acting player cancel under that conditioning.
struct HuPreflopDecisionEvaluation {
  std::uint32_t node_id{0U};
  std::uint8_t player{0U};
  std::uint8_t action_count{0U};
  std::array<std::array<double, hu_preflop_maximum_actions>, hu_preflop_hand_class_count>
      action_ev_ante{};
  std::array<std::array<double, hu_preflop_maximum_actions>, hu_preflop_hand_class_count>
      action_ev_standard_error_ante{};
  std::array<std::array<std::uint64_t, hu_preflop_maximum_actions>, hu_preflop_hand_class_count>
      action_ev_samples{};
  std::array<std::array<double, hu_preflop_maximum_actions>, hu_preflop_hand_class_count>
      action_ev_effective_samples{};
};

// Research diagnostic for distinguishing a stale realization-weighted average
// strategy from the current regret-matching policy at every preflop node.
// This is intentionally excluded from the persisted blueprint contract.
struct HuPreflopDecisionTrainingDiagnostic {
  std::uint32_t node_id{0U};
  std::uint8_t player{0U};
  std::uint8_t action_count{0U};
  std::array<std::array<double, hu_preflop_maximum_actions>, hu_preflop_hand_class_count>
      current_strategy{};
  std::array<std::array<double, hu_preflop_maximum_actions>, hu_preflop_hand_class_count>
      cumulative_weighted_regret{};
  std::array<std::array<double, hu_preflop_maximum_actions>, hu_preflop_hand_class_count>
      cumulative_average_weight{};
  std::array<std::uint64_t, hu_preflop_hand_class_count> last_iteration{};
};

enum class HuPreflopTelemetryTerminalType : std::uint8_t {
  PreflopFold,
  PreflopAllInExact,
  PreflopAllInSampled,
  PostflopFold,
  PostflopAllInExact,
  PostflopAllInSampled,
  PostflopShowdown,
  PostflopContinuation
};

struct HuPreflopActionConditionedTelemetry {
  std::uint32_t node_id{0U};
  std::uint8_t player{0U};
  std::uint64_t history{0U};
  HandClassId hand_class{0U};
  double physical_combo_mass{0.0};
  double public_reach{0.0};
  double own_reach{0.0};
  std::uint8_t action_id{0U};
  std::uint64_t sample_count{0U};
  double mean_action_value{0.0};
  double variance_action_value{0.0};
  double standard_error_action_value{0.0};
  double mean_action_advantage{0.0};
  std::uint64_t bucket_key{0U};
  std::uint64_t bucket_occupancy{0U};
  Street postflop_street{Street::Preflop};
  HuPreflopTelemetryTerminalType terminal_type{
      HuPreflopTelemetryTerminalType::PostflopContinuation};
  std::uint64_t all_in_exact_count{0U};
  std::uint64_t all_in_sampled_count{0U};
  double minimum_action_value{0.0};
  double maximum_action_value{0.0};
  double spread_action_value{0.0};

  friend bool operator==(const HuPreflopActionConditionedTelemetry &,
                         const HuPreflopActionConditionedTelemetry &) = default;
};

using HuPreflopComboReach = std::array<double, 630U>;

struct HuPreflopPostflopEntryReach {
  std::uint32_t entry_node{0};
  std::array<HuPreflopComboReach, 2> own_sequence_reach{};
  std::array<std::uint64_t, 2> positive_combo_count{};
  double joint_entry_probability{0.0};
};

struct HuPreflopDecompositionByteModel {
  std::uint64_t reach_template_bytes{0};
  std::uint64_t one_flop_conditioned_range_bytes{0};
  std::uint64_t one_resolver_boundary_bytes{0};
  std::uint64_t all_resolver_boundaries_bytes{0};
  std::uint64_t both_players_boundary_bytes{0};
  std::uint64_t canonical_all_resolver_boundaries_bytes{0};
  std::uint64_t canonical_both_players_boundary_bytes{0};
  std::uint64_t fully_materialized_range_bytes{0};
  std::uint64_t canonical_fully_materialized_range_bytes{0};
  std::uint64_t whole_game_coverage_mask_bytes{0};
  std::uint64_t whole_game_coverage_probability_bytes{0};
  std::uint64_t whole_game_profile_utility_bytes{0};
  std::uint64_t whole_game_local_continuation_identity_bytes{0};
  std::uint64_t whole_game_coverage_payload_bytes{0};
  // Peak payload for task-atomic streaming: current and candidate ledgers plus
  // both resolver boundaries. Container, string and callback overhead excluded.
  std::uint64_t streaming_certification_live_payload_bytes{0};
};

struct HuPreflopCanonicalFlop {
  std::array<CardId, 3> cards{};
  std::uint32_t physical_outcome_count{0};

  friend bool operator==(const HuPreflopCanonicalFlop &, const HuPreflopCanonicalFlop &) = default;
};

struct HuPreflopCanonicalFlopTask {
  std::uint32_t entry_node{0};
  std::array<CardId, 3> flop{};
  std::uint32_t physical_outcome_count{0};
  double joint_probability{0.0};
  std::string fingerprint;
};

struct HuPreflopDecompositionPlan {
  static constexpr std::uint32_t format_major = 1U;
  static constexpr std::uint32_t format_minor = 5U;

  std::uint32_t major{format_major};
  std::uint32_t minor{format_minor};
  std::string tree_fingerprint;
  std::string blueprint_fingerprint;
  std::vector<HuPreflopPostflopEntryReach> entries;
  std::vector<HuPreflopCanonicalFlop> canonical_flop_catalog;
  std::uint64_t physical_private_deals{0};
  std::uint64_t physical_flops{0};
  std::uint64_t canonical_flops{0};
  std::uint64_t public_flop_roots{0};
  std::uint64_t canonical_public_flop_roots{0};
  std::uint64_t physical_deal_flop_histories{0};
  std::uint64_t maximum_live_combos_per_player{0};
  std::uint64_t maximum_compatible_deals_per_flop{0};
  double postflop_entry_probability{0.0};
  double terminal_fold_probability{0.0};
  double terminal_all_in_probability{0.0};
  double total_probability{0.0};
  HuPreflopDecompositionByteModel bytes{};
  std::string fingerprint;
};

struct HuPreflopConditionedRanges {
  std::uint32_t entry_node{0};
  std::array<CardId, 3> flop{};
  std::array<HuPreflopComboReach, 2> exact_relative_reach{};
  std::array<std::uint64_t, 2> live_positive_combo_count{};
  double compatible_joint_reach_mass{0.0};
};

struct HuPreflopQuantizedRanges {
  PostflopRanges ranges{};
  double maximum_absolute_probability_error{0.0};
  double total_absolute_probability_error{0.0};
};

struct HuPreflopFlopBoundaryValue {
  ComboId opponent_combo{0};
  double counterfactual_reach{0.0};
  double blueprint_counterfactual_value_antes{0.0};
  bool positive_reach{false};
};

struct HuPreflopFlopBoundary {
  static constexpr std::uint32_t format_major = 1U;
  static constexpr std::uint32_t format_minor = 1U;

  std::uint32_t major{format_major};
  std::uint32_t minor{format_minor};
  std::string tree_fingerprint;
  std::string blueprint_fingerprint;
  // Per-task assembled artifact identity; equal for the two resolver sides.
  std::string continuation_fingerprint;
  // Shared identity of the postflop average-strategy checkpoint.
  std::string continuation_checkpoint_fingerprint;
  std::uint64_t blueprint_iterations{0};
  std::uint32_t entry_node{0};
  std::array<CardId, 3> flop{};
  std::uint8_t resolving_player{0};
  std::uint8_t opponent{1};
  std::vector<HuPreflopFlopBoundaryValue> values;
  std::string fingerprint;
};

struct HuPreflopGlobalBestResponseEvidence {
  static constexpr std::uint32_t format_major = 1U;
  static constexpr std::uint32_t format_minor = 2U;

  std::uint32_t major{format_major};
  std::uint32_t minor{format_minor};
  std::string tree_fingerprint;
  std::string blueprint_fingerprint;
  std::string continuation_profile_fingerprint;
  std::string profile_evaluation_fingerprint;
  std::string method;
  std::uint64_t blueprint_iterations{0};
  std::uint64_t chance_outcome_count{0};
  std::array<double, 2> best_response_values_antes{};
  std::array<double, 2> profile_values_antes{};
  std::array<double, 2> deviation_gains_antes{};
  double nashconv_antes{0.0};
  double normalized_nashconv{0.0};
  bool exact_best_response{false};
  std::string fingerprint;
};

struct HuPreflopWholeGameCertification {
  std::string tree_fingerprint;
  std::string blueprint_fingerprint;
  std::string plan_fingerprint;
  std::uint64_t expected_boundary_count{0};
  std::uint64_t validated_boundary_count{0};
  std::uint64_t fully_covered_task_count{0};
  std::uint64_t missing_boundary_count{0};
  double covered_postflop_probability{0.0};
  double expected_postflop_probability{0.0};
  std::string continuation_profile_fingerprint;
  bool boundary_coverage_complete{false};
  bool global_best_response_present{false};
  bool global_best_response_exact{false};
  double normalized_nashconv{0.0};
  double target_normalized_nashconv{0.0};
  bool certified{false};
  std::string status;
};

// Streaming coverage ledger for whole-game certification. One two-bit mask is
// retained for each canonical (preflop entry, Flop) task, together with one
// local continuation identity hash; full boundary payloads can therefore be
// validated and released immediately.
struct HuPreflopWholeGameCoverageAccumulator {
  static constexpr std::uint32_t format_major = 1U;
  static constexpr std::uint32_t format_minor = 3U;

  std::uint32_t major{format_major};
  std::uint32_t minor{format_minor};
  std::string tree_fingerprint;
  std::string blueprint_fingerprint;
  std::string plan_fingerprint;
  double target_normalized_nashconv{0.0};
  std::uint64_t blueprint_iterations{0U};
  std::string continuation_checkpoint_fingerprint;
  std::vector<std::uint8_t> task_side_coverage;
  std::vector<double> task_joint_probabilities;
  std::vector<double> task_side_profile_utility_antes;
  // Binds both resolver sides of a task to the same assembled continuation.
  std::vector<std::uint64_t> task_local_continuation_identity_hashes;
  std::uint64_t validated_boundary_count{0U};
  std::uint64_t fully_covered_task_count{0U};
  double covered_postflop_probability{0.0};
  std::uint64_t coverage_state_hash{0U};
  std::uint64_t continuation_profile_hash{0U};
  std::uint64_t profile_utility_state_hash{0U};
  std::uint64_t local_continuation_identity_state_hash{0U};
  std::string contribution_chain_fingerprint;
  std::string fingerprint;
};

struct HuPreflopWholeGameBoundaryRequest {
  std::uint64_t task_index{0U};
  HuPreflopCanonicalFlopTask task{};
  std::uint8_t missing_resolver_mask{0U};
};

using HuPreflopWholeGameBoundaryProvider =
    std::function<Result<std::vector<HuPreflopFlopBoundary>, HuPreflopError>(
        const HuPreflopWholeGameBoundaryRequest &)>;
using HuPreflopWholeGameCheckpointSink =
    std::function<Result<bool, HuPreflopError>(const HuPreflopWholeGameCoverageAccumulator &)>;

// Card-independent betting-history shape for a public Turn/River boundary.
// The physical board is supplied only when the concrete subgame is built.
struct HuPostflopBettingRootShape {
  std::uint32_t entry_node{0};
  Street street{Street::Turn};
  PublicState state{};
  std::vector<Action> action_history;
  std::string history_fingerprint;
};

// Card-independent terminal reached before the River public root. Concrete
// Flop/Turn cards and their orbit lifts are supplied when a task contribution
// is evaluated.
struct HuPostflopUpperStreetTerminalShape {
  std::uint32_t entry_node{0};
  std::uint64_t terminal_ordinal{0};
  Street street{Street::Flop};
  PublicState state{};
  std::vector<Action> action_history;
  std::string history_fingerprint;
};

struct HuPreflopRiverWorkEstimate {
  std::uint64_t physical_ordered_runouts_per_flop{0};
  std::uint64_t physical_public_board_histories{0};
  std::uint64_t canonical_public_board_histories{0};
  std::uint64_t minimum_canonical_runouts_per_flop{0};
  std::uint64_t maximum_canonical_runouts_per_flop{0};
  std::uint64_t river_betting_histories{0};
  std::uint64_t physical_resolver_roots{0};
  std::uint64_t canonical_resolver_roots{0};
  std::uint64_t canonical_roots_for_both_players{0};
  std::uint64_t boundary_values_per_resolver_root{0};
  // Average-policy payload. Exact-BR roots additionally retain one row set
  // for every distinct physical Turn represented by the canonical runout.
  std::uint64_t boundary_bytes_per_resolver_root{0};
  std::uint64_t fully_materialized_boundary_bytes{0};
  std::uint64_t maximum_best_response_turn_components_per_root{0};
  std::uint64_t best_response_boundary_bytes_per_resolver_root{0};
  std::uint64_t fully_materialized_best_response_boundary_bytes{0};
  std::uint64_t maximum_canonical_river_roots_per_physical_turn_leaf{0};
  std::uint64_t best_response_leaf_accumulator_value_bytes{0};
  std::uint64_t maximum_best_response_leaf_manifest_bytes{0};
  // Payload lower bound: one largest BR boundary plus the fixed compensated
  // accumulator and its bounded ordinal manifest. Container metadata excluded.
  std::uint64_t maximum_best_response_leaf_live_payload_bytes{0};
  // Entry-level streaming reduction keeps one completed 528-row Flop task and
  // one compensated 81-class accumulator. Task-recursion scratch and container
  // metadata are deliberately excluded from this lower bound.
  std::uint64_t best_response_task_value_bytes{0};
  std::uint64_t best_response_entry_accumulator_value_bytes{0};
  std::uint64_t best_response_entry_reduction_live_payload_bytes{0};
  // Exact reusable preflop all-in equity kernel. The streaming peak keeps the
  // canonical board catalog, one persistent 81x81 integer table, one
  // board-local contribution table, evaluated hands and live-combo indices.
  std::uint64_t all_in_canonical_complete_boards{0};
  std::uint64_t all_in_board_catalog_payload_bytes{0};
  std::uint64_t all_in_matchup_table_payload_bytes{0};
  std::uint64_t all_in_one_board_scratch_payload_bytes{0};
  std::uint64_t all_in_streaming_live_payload_bytes{0};
  std::string fingerprint;
};

// Bounded-memory execution plan for the nested River work. River boundaries
// are reduced into the persistent Flop/Turn accumulator at the end of every
// batch; materializing every per-root boundary is explicitly forbidden.
struct HuPreflopRiverBatchPlan {
  static constexpr std::uint32_t format_major = 1U;
  static constexpr std::uint32_t format_minor = 5U;

  std::uint32_t major{format_major};
  std::uint32_t minor{format_minor};
  std::string tree_fingerprint;
  std::string blueprint_fingerprint;
  std::string decomposition_plan_fingerprint;
  std::string river_work_fingerprint;
  std::string resolver_root_ordering;
  std::uint64_t resolver_roots{0};
  std::uint64_t target_batch_payload_bytes{0};
  std::uint64_t boundary_bytes_per_resolver_root{0};
  std::uint64_t roots_per_batch{0};
  std::uint64_t batch_count{0};
  std::uint64_t final_batch_root_count{0};
  std::uint64_t maximum_batch_payload_bytes{0};
  std::uint64_t fully_materialized_boundary_bytes{0};
  bool reduce_to_upper_street_accumulator{true};
  bool allow_full_boundary_materialization{false};
  std::vector<std::uint64_t> batch_first_resolver_roots;
  std::vector<std::uint64_t> batch_resolver_root_counts;
  std::vector<std::uint64_t> batch_task_span_indices;
  std::string fingerprint;
};

struct HuPreflopRiverBatch {
  std::uint64_t index{0};
  std::uint64_t task_span_index{0};
  std::uint64_t first_resolver_root{0};
  std::uint64_t resolver_root_count{0};
  std::uint64_t maximum_boundary_payload_bytes{0};
  std::string batch_plan_fingerprint;
  std::string fingerprint;
};

struct HuPreflopRiverResolverRoot {
  std::uint64_t ordinal{0};
  std::uint64_t task_span_index{0};
  std::uint64_t task_first_resolver_root{0};
  std::uint64_t task_resolver_root_count{0};
  std::uint32_t entry_node{0};
  std::array<CardId, 3> flop{};
  CardId turn{};
  CardId river{};
  std::uint8_t resolving_player{0};
  std::uint32_t physical_public_outcome_count{0};
  PublicState state{};
  std::vector<Action> action_history;
  std::string history_fingerprint;
  std::string batch_plan_fingerprint;
  std::string fingerprint;
};

struct HuPreflopCanonicalRiverBoard {
  std::array<CardId, 3> flop{};
  CardId turn{};
  CardId river{};
  std::uint32_t physical_outcome_count{0};

  friend bool operator==(const HuPreflopCanonicalRiverBoard &,
                         const HuPreflopCanonicalRiverBoard &) = default;
};

// One contiguous task owns a single postflop entry and canonical Flop. Keeping
// every River root for that task adjacent lets the executor reduce and release
// its Flop/Turn accumulator before starting the next task.
struct HuPreflopRiverTaskSpan {
  std::uint64_t first_resolver_root{0};
  std::uint64_t resolver_root_count{0};
  std::uint32_t entry_node{0};
  std::uint32_t flop_catalog_index{0};
  std::uint64_t first_shape_index{0};
  std::uint64_t shape_count{0};
  std::uint64_t first_board_index{0};
  std::uint64_t board_count{0};

  friend bool operator==(const HuPreflopRiverTaskSpan &, const HuPreflopRiverTaskSpan &) = default;
};

// Task-local partition of canonical ordered runouts by the Turn card already
// observed by both players. River boards in one group are contiguous in the
// catalog. Keeping this partition explicit prevents an upper-street best
// response from maximizing only after values from strategically distinct Turn
// observations have already been summed.
struct HuPreflopRiverTurnGroup {
  std::string root_catalog_fingerprint;
  std::uint64_t task_span_index{0};
  std::uint32_t entry_node{0};
  std::array<CardId, 3> flop{};
  CardId turn{};
  std::uint64_t first_board_offset{0};
  std::uint64_t canonical_board_count{0};
  std::uint64_t physical_public_outcome_count{0};
  std::string fingerprint;
};

// One contiguous resolver-root slice for a fixed (Turn group, River betting
// history). A complete Turn-major schedule is the Cartesian product of the
// task's Turn groups and River shapes; each slice still keeps both resolver
// sides adjacent.
struct HuPreflopRiverTurnResolverSpan {
  std::string root_catalog_fingerprint;
  std::string turn_group_fingerprint;
  std::uint64_t task_span_index{0};
  std::uint64_t shape_index{0};
  std::uint64_t first_resolver_root{0};
  std::uint64_t resolver_root_count{0};
  std::string fingerprint;
};

struct HuPreflopRiverRootCatalog {
  std::string tree_fingerprint;
  std::string decomposition_plan_fingerprint;
  std::string river_work_fingerprint;
  std::vector<HuPostflopBettingRootShape> river_shapes;
  std::vector<HuPreflopCanonicalRiverBoard> canonical_boards;
  std::vector<HuPreflopRiverTaskSpan> task_spans;
  std::uint64_t physical_public_board_histories{0};
  std::uint64_t resolver_roots{0};
  std::string fingerprint;
};

// One resolver-side result at a canonical River public root. The values are
// indexed by every opponent combo in the representative parent-Flop
// coordinates. Each value already sums the runout orbit that stabilizes that
// Flop, remaps private combos, and applies the parent Flop's orbit weight. The
// task accumulator must therefore not multiply physical_public_outcome_count
// again.
struct HuPreflopRiverBestResponseTurnComponent {
  CardId turn{};
  std::vector<HuPreflopFlopBoundaryValue> values;
};

struct HuPreflopRiverRootBoundary {
  static constexpr std::uint32_t format_major = 1U;
  static constexpr std::uint32_t format_minor = 5U;

  std::uint32_t major{format_major};
  std::uint32_t minor{format_minor};
  std::string tree_fingerprint;
  std::string blueprint_fingerprint;
  std::string batch_plan_fingerprint;
  std::string resolver_root_fingerprint;
  std::string continuation_fingerprint;
  HuPreflopContinuationValueMode value_mode{HuPreflopContinuationValueMode::AverageStrategy};
  std::uint64_t blueprint_iterations{0};
  std::uint64_t resolver_root_ordinal{0};
  std::uint64_t task_span_index{0};
  std::uint32_t entry_node{0};
  std::array<CardId, 3> flop{};
  CardId turn{};
  CardId river{};
  std::uint8_t resolving_player{0};
  std::uint8_t opponent{1};
  std::uint32_t physical_public_outcome_count{0};
  // Exact-BR boundaries retain one component per physical Turn observation.
  // Average-strategy boundaries leave this empty. A BR reducer must maximize
  // inside each component before summing different Turn observations.
  std::vector<HuPreflopRiverBestResponseTurnComponent> best_response_turn_components;
  std::vector<HuPreflopFlopBoundaryValue> values;
  std::string fingerprint;
};

struct HuPreflopRiverConditionedReach {
  std::string resolver_root_fingerprint;
  // Product of only the postflop action probabilities for each physical
  // combo. Keeping this separate from exact_sequence_reach is necessary when
  // a Flop-root CFV is assembled: the target player's own continuation reach
  // belongs in the continuation probability, while its preflop reach does
  // not belong in its counterfactual reach.
  std::array<HuPreflopComboReach, 2> postflop_action_sequence_reach{};
  std::array<HuPreflopComboReach, 2> exact_sequence_reach{};
  std::array<std::uint64_t, 2> live_positive_combo_count{};
  double compatible_joint_reach_mass{0.0};
};

// Supplies the average-strategy probability of the selected action for one
// physical combo at a replayed Flop/Turn decision. The PublicState contains
// the concrete board available on that street; action_prefix excludes the
// selected action. Persistence and solver-specific lookup remain outside this
// mathematical propagation contract.
using HuPostflopActionProbabilityProvider = std::function<Result<double, HuPreflopError>(
    const PublicState &, std::span<const Action>, const Action &, ComboId)>;

struct HuPreflopCompensatedCounterfactualValue {
  double weighted_reach_sum{0.0};
  double weighted_reach_compensation{0.0};
  double weighted_utility_sum_antes{0.0};
  double weighted_utility_compensation_antes{0.0};
};

// Runtime reduction for exactly one contiguous (entry, canonical Flop) task.
// A seal is O(630); ordinary root accumulation is O(number of live combos).
struct HuPreflopRiverTaskAccumulator {
  static constexpr std::uint32_t format_major = 1U;
  static constexpr std::uint32_t format_minor = 4U;

  std::uint32_t major{format_major};
  std::uint32_t minor{format_minor};
  std::string tree_fingerprint;
  std::string blueprint_fingerprint;
  std::string root_catalog_fingerprint;
  std::string batch_plan_fingerprint;
  std::string continuation_checkpoint_fingerprint;
  HuPreflopContinuationValueMode value_mode{HuPreflopContinuationValueMode::AverageStrategy};
  std::uint64_t blueprint_iterations{0};
  std::uint64_t task_span_index{0};
  std::uint64_t first_resolver_root{0};
  std::uint64_t resolver_root_count{0};
  std::uint64_t next_resolver_root{0};
  std::uint32_t entry_node{0};
  std::array<CardId, 3> flop{};
  std::array<std::uint64_t, 2> accumulated_roots_by_resolver{};
  std::array<std::array<HuPreflopCompensatedCounterfactualValue, 630U>, 2> values{};
  bool complete{false};
  std::string contribution_chain_fingerprint;
  std::string fingerprint;
};

struct HuPreflopRiverTaskAggregateValue {
  ComboId opponent_combo{0};
  double weighted_counterfactual_reach{0.0};
  double weighted_counterfactual_utility_antes{0.0};
  double conditional_value_antes{0.0};
  bool positive_reach{false};
};

struct HuPreflopRiverTaskAggregate {
  static constexpr std::uint32_t format_major = 1U;
  static constexpr std::uint32_t format_minor = 4U;

  std::uint32_t major{format_major};
  std::uint32_t minor{format_minor};
  std::string tree_fingerprint;
  std::string blueprint_fingerprint;
  std::string accumulator_fingerprint;
  std::string continuation_checkpoint_fingerprint;
  HuPreflopContinuationValueMode value_mode{HuPreflopContinuationValueMode::AverageStrategy};
  std::uint64_t blueprint_iterations{0};
  std::uint64_t task_span_index{0};
  std::uint32_t entry_node{0};
  std::array<CardId, 3> flop{};
  std::array<std::vector<HuPreflopRiverTaskAggregateValue>, 2> resolver_values;
  std::string fingerprint;
};

// Exact contribution of one terminal Flop/Turn betting history to one
// canonical (preflop entry, Flop) task. Future public cards are enumerated so
// that this payload uses the same ordered-runout units as a River aggregate.
struct HuPreflopUpperStreetTerminalContribution {
  static constexpr std::uint32_t format_major = 1U;
  static constexpr std::uint32_t format_minor = 1U;

  std::uint32_t major{format_major};
  std::uint32_t minor{format_minor};
  std::string tree_fingerprint;
  std::string blueprint_fingerprint;
  std::string continuation_checkpoint_fingerprint;
  std::uint64_t blueprint_iterations{0};
  std::uint64_t task_span_index{0};
  std::uint32_t entry_node{0};
  std::array<CardId, 3> flop{};
  std::uint64_t terminal_ordinal{0};
  Street terminal_street{Street::Flop};
  HandStatus terminal_status{HandStatus::Folded};
  std::string terminal_history_fingerprint;
  std::array<std::vector<HuPreflopRiverTaskAggregateValue>, 2> resolver_values;
  std::string fingerprint;
};

struct HuPreflopBestResponseComboValue {
  ComboId responding_combo{0};
  double weighted_counterfactual_reach{0.0};
  double weighted_counterfactual_utility_antes{0.0};
  double conditional_value_antes{0.0};
  bool positive_reach{false};
};

struct HuPreflopBestResponseTurnComponent {
  CardId turn{};
  std::vector<HuPreflopBestResponseComboValue> values;
};

// Exact terminal leaf for one player's upper-street best response. Opponent
// average-strategy reach is retained, while every action taken by the
// responding player has unit reach: those actions are selected later by the
// max step of the BR recursion. Turn terminals are emitted separately for one
// canonical Turn group so observed public cards are never merged before max.
struct HuPreflopUpperStreetBestResponseTerminalContribution {
  static constexpr std::uint32_t format_major = 1U;
  static constexpr std::uint32_t format_minor = 1U;

  std::uint32_t major{format_major};
  std::uint32_t minor{format_minor};
  std::string tree_fingerprint;
  std::string blueprint_fingerprint;
  std::string root_catalog_fingerprint;
  std::string turn_group_fingerprint;
  // Global identity of the postflop continuation policy used by the
  // opponent-probability provider. It must match the River BR boundaries.
  std::string continuation_fingerprint;
  std::uint64_t blueprint_iterations{0};
  std::uint64_t task_span_index{0};
  std::uint32_t entry_node{0};
  std::array<CardId, 3> flop{};
  CardId turn{};
  bool has_turn{false};
  std::uint8_t responding_player{0};
  std::uint64_t terminal_ordinal{0};
  Street terminal_street{Street::Flop};
  HandStatus terminal_status{HandStatus::Folded};
  std::string terminal_history_fingerprint;
  std::vector<HuPreflopBestResponseTurnComponent> turn_components;
  std::vector<HuPreflopBestResponseComboValue> values;
  std::string fingerprint;
};

enum class HuPreflopBestResponseLeafKind : std::uint8_t { UpperStreetTerminal, RiverContinuation };

// One exact leaf requested by the upper-street best-response recursion. The
// provider returns all 528 parent-Flop combo rows. Flop terminals integrate
// every future ordered runout; Turn terminals and River continuations integrate
// every compatible River for this physical Turn. Values must already include
// opponent action reach and chance multiplicity, but never responder reach.
struct HuPreflopBestResponseLeafQuery {
  HuPreflopBestResponseLeafKind kind{HuPreflopBestResponseLeafKind::UpperStreetTerminal};
  std::uint64_t task_span_index{0};
  std::uint32_t entry_node{0};
  std::array<CardId, 3> flop{};
  CardId turn{};
  bool has_turn{false};
  std::uint8_t responding_player{0};
  PublicState state{};
  std::vector<Action> action_history;
  std::string history_fingerprint;
};

using HuPreflopBestResponseLeafProvider =
    std::function<Result<std::vector<HuPreflopBestResponseComboValue>, HuPreflopError>(
        const HuPreflopBestResponseLeafQuery &)>;

// Runtime sources used by the bounded-memory leaf dispatcher. A terminal
// source returns exactly one contribution matching the requested public
// history. A River source returns one root boundary at a time; the dispatcher
// consumes it immediately and never owns the full root set.
using HuPreflopBestResponseTerminalContributionProvider =
    std::function<Result<HuPreflopUpperStreetBestResponseTerminalContribution, HuPreflopError>(
        const HuPreflopBestResponseLeafQuery &)>;
using HuPreflopBestResponseRiverBoundaryProvider =
    std::function<Result<HuPreflopRiverRootBoundary, HuPreflopError>(
        const HuPreflopBestResponseLeafQuery &, std::uint64_t)>;

// Complete exact postflop best response for one (preflop entry, canonical
// Flop) task. The recursion maximizes before merging strategically distinct
// public observations and therefore remains suitable for whole-game BR/NashConv.
struct HuPreflopBestResponseTaskEvaluation {
  static constexpr std::uint32_t format_major = 1U;
  static constexpr std::uint32_t format_minor = 0U;

  std::uint32_t major{format_major};
  std::uint32_t minor{format_minor};
  std::string tree_fingerprint;
  std::string blueprint_fingerprint;
  std::string continuation_fingerprint;
  std::uint64_t blueprint_iterations{0};
  std::uint64_t task_span_index{0};
  std::uint32_t entry_node{0};
  std::array<CardId, 3> flop{};
  std::uint8_t responding_player{0};
  std::uint64_t decision_nodes_visited{0};
  std::uint64_t responder_decision_nodes_visited{0};
  std::uint64_t opponent_decision_nodes_visited{0};
  std::uint64_t physical_turn_branches_visited{0};
  std::uint64_t upper_terminal_leaves_visited{0};
  std::uint64_t river_continuation_leaves_visited{0};
  std::uint64_t maximum_recursion_depth{0};
  std::vector<HuPreflopBestResponseComboValue> values;
  std::string fingerprint;
};

struct HuPreflopBestResponseHandClassValue {
  HandClassId responding_class{0};
  double weighted_counterfactual_reach{0.0};
  double weighted_counterfactual_utility_antes{0.0};
  double conditional_value_antes{0.0};
  bool positive_reach{false};
};

// Streaming reduction of all canonical Flop task evaluations belonging to one
// preflop entry. Physical combo rows are summed by their lossless 81-class
// preflop symmetry only after every task has independently completed its
// postflop BR. This is the correct boundary for the next preflop max/sum step.
struct HuPreflopBestResponseEntryAccumulator {
  static constexpr std::uint32_t format_major = 1U;
  static constexpr std::uint32_t format_minor = 0U;

  std::uint32_t major{format_major};
  std::uint32_t minor{format_minor};
  std::string tree_fingerprint;
  std::string blueprint_fingerprint;
  std::string continuation_fingerprint;
  std::uint64_t blueprint_iterations{0};
  std::uint64_t entry_index{0};
  std::uint32_t entry_node{0};
  std::uint8_t responding_player{0};
  std::uint64_t first_task_span_index{0};
  std::uint64_t task_count{0};
  std::uint64_t next_task_offset{0};
  std::array<HuPreflopCompensatedCounterfactualValue, hu_preflop_hand_class_count> values{};
  bool complete{false};
  std::string contribution_chain_fingerprint;
  std::string fingerprint;
};

struct HuPreflopBestResponseEntryEvaluation {
  static constexpr std::uint32_t format_major = 1U;
  static constexpr std::uint32_t format_minor = 0U;

  std::uint32_t major{format_major};
  std::uint32_t minor{format_minor};
  std::string tree_fingerprint;
  std::string blueprint_fingerprint;
  std::string continuation_fingerprint;
  std::string accumulator_fingerprint;
  std::uint64_t blueprint_iterations{0};
  std::uint64_t entry_index{0};
  std::uint32_t entry_node{0};
  std::uint8_t responding_player{0};
  std::uint64_t task_count{0};
  std::vector<HuPreflopBestResponseHandClassValue> values;
  std::string fingerprint;
};

enum class HuPreflopBestResponsePreflopLeafKind : std::uint8_t {
  PostflopEntry,
  TerminalFold,
  TerminalAllIn
};

struct HuPreflopBestResponsePreflopLeafQuery {
  HuPreflopBestResponsePreflopLeafKind kind{HuPreflopBestResponsePreflopLeafKind::PostflopEntry};
  std::uint32_t node_id{0};
  std::uint8_t responding_player{0};
};

// Exact preflop terminal value in the same raw private-card/public-runout
// units used by a fully reduced postflop entry. The responder's blueprint
// reach is excluded; the opponent's complete preflop sequence reach is
// retained. This lets the parent recursion maximize for the responder and sum
// opponent branches without applying either policy twice.
struct HuPreflopBestResponsePreflopTerminalEvaluation {
  static constexpr std::uint32_t format_major = 1U;
  static constexpr std::uint32_t format_minor = 0U;

  std::uint32_t major{format_major};
  std::uint32_t minor{format_minor};
  std::string tree_fingerprint;
  std::string blueprint_fingerprint;
  std::string continuation_fingerprint;
  std::uint64_t blueprint_iterations{0};
  std::uint32_t node_id{0};
  HuPreflopBestResponsePreflopLeafKind kind{HuPreflopBestResponsePreflopLeafKind::TerminalFold};
  std::uint8_t responding_player{0};
  std::uint64_t ordered_public_runouts_per_private_deal{0};
  std::vector<HuPreflopBestResponseHandClassValue> values;
  std::string fingerprint;
};

struct HuPreflopCanonicalAllInBoard {
  std::array<CardId, 5> cards{};
  std::uint32_t physical_board_count{0};

  friend bool operator==(const HuPreflopCanonicalAllInBoard &,
                         const HuPreflopCanonicalAllInBoard &) = default;
};

// Suit-isomorphic catalog of complete five-card public boards. One unordered
// board represents 20 legal Flop/Turn/River histories: choose three of its
// cards for the Flop and order the remaining Turn and River.
struct HuPreflopAllInBoardCatalog {
  static constexpr std::uint32_t format_major = 1U;
  static constexpr std::uint32_t format_minor = 0U;

  std::uint32_t major{format_major};
  std::uint32_t minor{format_minor};
  std::string evaluator_contract;
  std::uint64_t physical_unordered_boards{0};
  std::uint64_t ordered_histories_per_unordered_board{0};
  std::vector<HuPreflopCanonicalAllInBoard> boards;
  std::string fingerprint;
};

struct HuPreflopAllInMatchupCount {
  std::uint64_t responding_player_wins{0};
  std::uint64_t ties{0};
  std::uint64_t responding_player_losses{0};
};

struct HuPreflopAllInTrainingEquity {
  double win_probability{0.0};
  double tie_probability{0.0};
};

// Exact class-pair expectation used only at preflop all-in terminals. The
// table integrates physical suit relations and every legal five-card runout;
// opponent private classes remain sampled by MCCFR.
struct HuPreflopAllInTrainingOracle {
  std::array<HuPreflopAllInTrainingEquity,
             hu_preflop_hand_class_count * hu_preflop_hand_class_count>
      matchups{};
  std::string source_equity_table_fingerprint;
};

// Streaming exact-equity accumulator. Index = responder_class * 81 +
// opponent_class. It is independent of stack, action history and blueprint,
// so the completed table is reused by every preflop all-in terminal.
struct HuPreflopAllInEquityAccumulator {
  static constexpr std::uint32_t format_major = 1U;
  static constexpr std::uint32_t format_minor = 0U;

  std::uint32_t major{format_major};
  std::uint32_t minor{format_minor};
  std::string board_catalog_fingerprint;
  std::string evaluator_contract;
  std::uint64_t next_board_ordinal{0};
  std::uint64_t processed_physical_unordered_boards{0};
  std::uint64_t matchup_outcome_count{0};
  std::vector<HuPreflopAllInMatchupCount> matchups;
  bool complete{false};
  std::string contribution_chain_fingerprint;
  std::string fingerprint;
};

struct HuPreflopAllInEquityTable {
  static constexpr std::uint32_t format_major = 1U;
  static constexpr std::uint32_t format_minor = 0U;

  std::uint32_t major{format_major};
  std::uint32_t minor{format_minor};
  std::string board_catalog_fingerprint;
  std::string evaluator_contract;
  std::uint64_t canonical_board_count{0};
  std::uint64_t physical_unordered_boards{0};
  std::uint64_t ordered_public_runouts_per_private_deal{0};
  std::uint64_t matchup_outcome_count{0};
  std::vector<HuPreflopAllInMatchupCount> matchups;
  std::string accumulator_fingerprint;
  std::string fingerprint;
};

[[nodiscard]] Result<HuPreflopAllInTrainingOracle, HuPreflopError>
make_hu_preflop_all_in_training_oracle(const HuPreflopAllInEquityTable &table);
[[nodiscard]] Result<bool, HuPreflopError>
validate_hu_preflop_all_in_training_oracle(const HuPreflopAllInTrainingOracle &oracle);

using HuPreflopBestResponsePreflopLeafProvider =
    std::function<Result<std::vector<HuPreflopBestResponseHandClassValue>, HuPreflopError>(
        const HuPreflopBestResponsePreflopLeafQuery &)>;

// Exact preflop max/sum recursion over globally counterfactual-weighted leaf
// values. Postflop entries come from all 5,157 entry/Flop tasks; fold and all-in
// leaves must use the same opponent blueprint reach and chance units.
struct HuPreflopBestResponsePreflopEvaluation {
  static constexpr std::uint32_t format_major = 1U;
  static constexpr std::uint32_t format_minor = 0U;

  std::uint32_t major{format_major};
  std::uint32_t minor{format_minor};
  std::string tree_fingerprint;
  std::string blueprint_fingerprint;
  std::string continuation_fingerprint;
  std::uint64_t blueprint_iterations{0};
  std::uint8_t responding_player{0};
  std::uint64_t decision_nodes_visited{0};
  std::uint64_t responder_decision_nodes_visited{0};
  std::uint64_t opponent_decision_nodes_visited{0};
  std::uint64_t postflop_entry_leaves_visited{0};
  std::uint64_t terminal_fold_leaves_visited{0};
  std::uint64_t terminal_all_in_leaves_visited{0};
  std::uint64_t maximum_recursion_depth{0};
  std::vector<HuPreflopBestResponseHandClassValue> values;
  std::string fingerprint;
};

struct HuPreflopExactProfileEvaluation {
  static constexpr std::uint32_t format_major = 1U;
  static constexpr std::uint32_t format_minor = 0U;

  std::uint32_t major{format_major};
  std::uint32_t minor{format_minor};
  std::string tree_fingerprint;
  std::string blueprint_fingerprint;
  std::string plan_fingerprint;
  std::string continuation_profile_fingerprint;
  std::string all_in_equity_table_fingerprint;
  std::uint64_t blueprint_iterations{0U};
  std::uint64_t chance_outcome_count{0U};
  std::uint64_t postflop_boundary_count{0U};
  std::uint64_t terminal_fold_count{0U};
  std::uint64_t terminal_all_in_count{0U};
  std::array<double, 2> postflop_values_antes{};
  std::array<double, 2> preflop_terminal_values_antes{};
  std::array<double, 2> total_values_antes{};
  std::string fingerprint;
};

// Streaming reducer for one River-continuation leaf requested by the upper
// BR recursion. Only exact-BR components belonging to one physical Turn and
// one River betting history are retained; canonical River roots are consumed
// in their catalog order and released immediately.
struct HuPreflopRiverBestResponseLeafAccumulator {
  static constexpr std::uint32_t format_major = 1U;
  static constexpr std::uint32_t format_minor = 0U;

  std::uint32_t major{format_major};
  std::uint32_t minor{format_minor};
  std::string tree_fingerprint;
  std::string blueprint_fingerprint;
  std::string root_catalog_fingerprint;
  std::string batch_plan_fingerprint;
  std::string continuation_fingerprint;
  std::uint64_t blueprint_iterations{0};
  std::uint64_t task_span_index{0};
  std::uint32_t entry_node{0};
  std::array<CardId, 3> flop{};
  CardId turn{};
  std::uint8_t responding_player{0};
  PublicState state{};
  std::vector<Action> action_history;
  std::string history_fingerprint;
  std::vector<std::uint64_t> expected_resolver_root_ordinals;
  std::uint64_t next_boundary_index{0};
  std::array<HuPreflopCompensatedCounterfactualValue, 630U> values{};
  bool complete{false};
  std::string contribution_chain_fingerprint;
  std::string fingerprint;
};

using HuPreflopRiverBestResponseLeafCheckpointSink =
    std::function<Result<bool, HuPreflopError>(const HuPreflopRiverBestResponseLeafAccumulator &)>;

// Bounded-memory merger for one canonical Flop task. It starts with the River
// continuation aggregate and accepts every upper-street terminal in manifest
// order. Only a complete and mass-conserving accumulator can emit the two
// Flop boundary sides consumed by whole-game certification.
struct HuPreflopFlopTaskAccumulator {
  static constexpr std::uint32_t format_major = 1U;
  static constexpr std::uint32_t format_minor = 1U;

  std::uint32_t major{format_major};
  std::uint32_t minor{format_minor};
  std::string tree_fingerprint;
  std::string blueprint_fingerprint;
  std::string river_aggregate_fingerprint;
  std::string continuation_checkpoint_fingerprint;
  std::uint64_t blueprint_iterations{0};
  std::uint64_t task_span_index{0};
  std::uint32_t entry_node{0};
  std::array<CardId, 3> flop{};
  std::vector<std::string> expected_terminal_fingerprints;
  std::uint64_t next_terminal_ordinal{0};
  std::array<std::array<HuPreflopCompensatedCounterfactualValue, 630U>, 2> values{};
  std::uint64_t numeric_state_hash{0};
  bool complete{false};
  std::string contribution_chain_fingerprint;
  std::string fingerprint;
};

struct HuPreflopRiverSchedulerCheckpoint {
  static constexpr std::uint32_t format_major = 1U;
  static constexpr std::uint32_t format_minor = 0U;

  std::uint32_t major{format_major};
  std::uint32_t minor{format_minor};
  std::string tree_fingerprint;
  std::string blueprint_fingerprint;
  std::string batch_plan_fingerprint;
  std::uint64_t completed_batch_count{0};
  std::uint64_t completed_resolver_root_count{0};
  std::string upper_street_accumulator_fingerprint;
  bool complete{false};
  std::string fingerprint;
};

enum class HuPreflopPostflopRepresentation : std::uint8_t {
  CategoryEquityMonteCarlo,
  CategoryEquityMonteCarloCurrentStreet,
  CategoryEquityMonteCarloMemoryless,
  ExactPhysical,
  DistributionalStrengthPrototype,
  DistributionalStrengthPerfectRecall,
  DistributionalStrengthBucketHistory,
  DistributionalStrengthProfileV6,
  DistributionalStrengthStructuredV7,
  DistributionalStrengthStreetAdaptiveV8,
  DistributionalStrengthSelectiveHistoryV9,
  DistributionalStrengthCategoryHistoryV10,
  DistributionalStrengthAdaptiveCategoryHistoryV11
};

enum class HuPreflopSamplingAlgorithm : std::uint8_t {
  ExternalSampling,
  LinearMccfr,
  DiscountedMccfr1503,
  ChanceSampledCfr
};

enum class HuPreflopChanceSamplingMode : std::uint8_t {
  IndependentPhysical,
  PublicBoardStratified
};

enum class HuPreflopPostflopAllInExpectationMode : std::uint8_t {
  SampledRunout,
  ExactTurn,
  ExactFlopAndTurn
};

struct HuPreflopPostflopAllInEquity {
  std::uint64_t wins{0U};
  std::uint64_t ties{0U};
  std::uint64_t losses{0U};

  [[nodiscard]] constexpr std::uint64_t runouts() const noexcept {
    return wins + ties + losses;
  }

  friend bool operator==(const HuPreflopPostflopAllInEquity &,
                         const HuPreflopPostflopAllInEquity &) = default;
};

struct HuPreflopPublicBoardPrivateDeal {
  std::array<std::array<CardId, 2>, 2> holes{};
};

struct HuPreflopRootConditionalDeal {
  std::array<std::array<CardId, 2>, 2> holes{};
  std::array<CardId, 5> board{};

  friend bool operator==(const HuPreflopRootConditionalDeal &,
                         const HuPreflopRootConditionalDeal &) = default;
};

constexpr std::size_t hu_preflop_sampled_postflop_maximum_actions = 6U;
constexpr std::uint16_t hu_preflop_sampled_postflop_unset_bucket =
    std::numeric_limits<std::uint16_t>::max();
constexpr std::array<std::uint16_t, 3> hu_preflop_distributional_default_capacities{256U, 1'024U,
                                                                                    4'096U};
constexpr std::uint64_t hu_preflop_sampled_postflop_default_maximum_serialized_bytes =
    2ULL * 1024ULL * 1024ULL * 1024ULL;

enum class HuPreflopSampledPolicyFallback : std::uint8_t { Uniform };
enum class HuPreflopSampledPolicyView : std::uint8_t { Average, Current };
enum class HuPreflopSampledPolicyLookupMode : std::uint8_t {
  AllowConfiguredFallback,
  RequireTrainedInfoset
};

// Stable external form of the research solver's postflop information key.
// public_history binds the preflop entry and the complete ordered public
// action/chance history. ExactPhysical uses physical_cards; abstract modes use
// bucket_history. Neither form includes the opponent's private cards.
struct HuPreflopSampledPostflopPolicyKey {
  std::uint64_t public_history{0U};
  std::uint64_t physical_cards{std::numeric_limits<std::uint64_t>::max()};
  std::array<std::uint16_t, 3> bucket_history{hu_preflop_sampled_postflop_unset_bucket,
                                              hu_preflop_sampled_postflop_unset_bucket,
                                              hu_preflop_sampled_postflop_unset_bucket};
  HandClassId preflop_class{0U};
  std::uint8_t player{0U};
  Street street{Street::Flop};

  friend bool operator==(const HuPreflopSampledPostflopPolicyKey &,
                         const HuPreflopSampledPostflopPolicyKey &) = default;
};

// Card-independent result of replaying one public decision. It lets batch
// evaluators combine the same public history with many precomputed private
// abstraction keys without replaying the action sequence for every combo.
struct HuPreflopSampledPostflopPublicDecision {
  std::uint64_t public_history{0U};
  std::uint64_t public_board_mask{0U};
  std::uint8_t player{0U};
  Street street{Street::Flop};
  std::uint8_t action_count{0U};

  friend bool operator==(const HuPreflopSampledPostflopPublicDecision &,
                         const HuPreflopSampledPostflopPublicDecision &) = default;
};

struct HuPreflopSampledPostflopPolicyEntry {
  HuPreflopSampledPostflopPolicyKey key{};
  std::uint8_t action_count{0U};
  std::array<double, hu_preflop_sampled_postflop_maximum_actions> probabilities{};
  // Present only when the enclosing format declares current_policy_present.
  std::array<double, hu_preflop_sampled_postflop_maximum_actions> current_probabilities{};

  friend bool operator==(const HuPreflopSampledPostflopPolicyEntry &,
                         const HuPreflopSampledPostflopPolicyEntry &) = default;
};

// Research-only sparse average policy. Missing infosets have one explicit,
// versioned fallback instead of silently inheriting solver-local behavior.
struct HuPreflopSampledPostflopPolicy {
  static constexpr std::uint32_t format_major = 1U;
  static constexpr std::uint32_t minimum_supported_minor = 5U;
  static constexpr std::uint32_t format_minor = 11U;

  std::uint32_t major{format_major};
  std::uint32_t minor{format_minor};
  std::string tree_fingerprint;
  std::string algorithm;
  std::string abstraction_id;
  std::uint64_t iterations{0U};
  std::uint64_t seed{0U};
  std::uint64_t partition_seed{0x5041'5254'4954'494FULL};
  std::uint32_t equity_samples_per_bucket{0U};
  std::array<std::uint16_t, 3> distributional_bucket_capacities{
      hu_preflop_distributional_default_capacities};
  HuPreflopPostflopRepresentation representation{
      HuPreflopPostflopRepresentation::CategoryEquityMonteCarlo};
  HuPreflopSampledPolicyFallback missing_infoset_fallback{HuPreflopSampledPolicyFallback::Uniform};
  bool current_policy_present{false};
  std::vector<HuPreflopSampledPostflopPolicyEntry> entries;
  std::string fingerprint;

  bool operator==(const HuPreflopSampledPostflopPolicy &) const = default;
};

struct HuPreflopSolveOptions {
  std::uint64_t iterations{20'000};
  std::uint64_t preflop_refinement_iterations{0};
  std::uint64_t evaluation_deals{20'000};
  std::uint64_t best_response_iterations{5'000};
  std::uint64_t best_response_evaluation_deals{10'000};
  std::uint32_t equity_samples_per_bucket{8};
  std::uint64_t seed{0x4855'434F'3430'0001ULL};
  std::uint64_t partition_seed{0x5041'5254'4954'494FULL};
  std::uint64_t evaluation_seed{0x4556'414C'5541'5445ULL};
  std::array<std::uint16_t, 3> distributional_bucket_capacities{
      hu_preflop_distributional_default_capacities};
  // Empty selects the exact oracle. A non-empty path opts into the R3
  // checksummed seven-card lookup table without changing production defaults.
  std::string seven_card_table_path;
  std::uint64_t maximum_numeric_state_bytes{8ULL * 1024ULL * 1024ULL * 1024ULL};
  std::uint64_t maximum_bucket_cache_entries{1'000'000U};
  // Shared budget across workers for exact postflop all-in equity results.
  // Keys contain only physical cards and the visible street, so cached
  // equities remain independent of ranges, reach probabilities and history.
  std::uint64_t maximum_exact_postflop_all_in_cache_entries{1'000'000U};
  std::uint8_t worker_threads{1U};
  // Zero preserves the sequential online-update schedule. A non-zero value
  // freezes the policy for this many iterations and reduces worker deltas in
  // deterministic job order.
  std::uint32_t training_batch_iterations{0U};
  // R6 experimental variance control. Values above one average independent
  // physical continuations at the CO root while discarding their deeper
  // training deltas. The production-compatible default remains one.
  std::uint8_t root_action_value_rollouts{1U};
  // Experimental companion mode. When enabled, all root-conditioned CO
  // continuation deltas are weighted by 1 / root_action_value_rollouts and
  // reduced into the postflop blueprint. The BTN traversal remains unchanged.
  bool root_continuation_mean_updates{false};
  // Extends root_continuation_mean_updates by averaging the same number of
  // complete BTN-traverser passes. This preserves a symmetric update estimator.
  bool symmetric_traverser_mean_updates{false};
  // Experimental root variance control. Each traverser action keeps its
  // original marginal continuation distribution, but actions from the same
  // root sample reuse one RNG seed. This common-random-number coupling can
  // reduce the variance of pairwise action differences without extra rollouts.
  bool root_common_random_numbers{false};
  // Experimental global variance control. At every traverser decision in the
  // batched sampler, all action branches restore the RNG state captured on
  // entry to the decision. The root-only flag above remains unchanged for
  // compatibility with earlier V15/V17 candidates.
  bool global_common_random_numbers{false};
  // Experimental grouped estimator. The first opponent response following
  // the traverser's first enumerated decision uses one equal-probability CDF
  // stratum per rollout. Valid only when all rollout updates are averaged for
  // both traversers.
  bool root_first_opponent_response_stratification{false};
  std::uint32_t maximum_parallel_updates_per_job{10'000U};
  std::uint64_t maximum_parallel_scratch_bytes{512ULL * 1024ULL * 1024ULL};
  std::uint64_t maximum_variance_baseline_bytes{512ULL * 1024ULL * 1024ULL};
  bool use_opponent_value_baseline{false};
  std::shared_ptr<const HuPreflopAllInTrainingOracle> preflop_all_in_training_oracle;
  HuPreflopPostflopAllInExpectationMode postflop_all_in_expectation_mode{
      HuPreflopPostflopAllInExpectationMode::SampledRunout};
  HuPreflopChanceSamplingMode chance_sampling_mode{
      HuPreflopChanceSamplingMode::IndependentPhysical};
  bool use_compiled_betting{true};
  bool export_postflop_policy{false};
  // Diagnostic-only: evaluates and optionally exports a profile that uses
  // final regret matching at every preflop and postflop information set.
  bool evaluate_current_profile{false};
  // Disabled by default because evaluating every preflop decision requires
  // additional continuation rollouts. Enable only for full-tree chart export.
  bool evaluate_preflop_decisions{false};
  // Opt-in, non-mutating action-conditioned training telemetry for Fase A.
  bool collect_action_conditioned_telemetry{false};
  // Hard bound for the diagnostic telemetry index. New keys are ignored once
  // the bound is reached and the dropped-observation counter is exported.
  std::uint64_t maximum_action_conditioned_telemetry_entries{1'000'000U};
  // Opt-in post-training root audit. Each selected class receives the same
  // conditional physical deals for every forced root action. The audit reads
  // the frozen policy and never updates regret or strategy sums.
  std::vector<HandClassId> root_decision_trace_hand_classes;
  std::uint32_t root_decision_trace_deals_per_class{512U};
  std::uint64_t maximum_exported_postflop_policy_payload_bytes{512ULL * 1024ULL * 1024ULL};
  // The historical research runner used DCFR 1.5/0/3. Keep that default for
  // reproducibility; R2 experiments select the verified vanilla or linear
  // external-sampling schedules explicitly.
  HuPreflopSamplingAlgorithm sampling_algorithm{HuPreflopSamplingAlgorithm::DiscountedMccfr1503};
  HuPreflopPostflopRepresentation postflop_representation{
      HuPreflopPostflopRepresentation::CategoryEquityMonteCarlo};
};

struct HuPreflopRootActionAdvantageDiagnostic {
  std::uint64_t observations{0U};
  double weight_sum{0.0};
  double effective_samples{0.0};
  std::array<double, 5> weighted_mean_ante{};
  std::array<double, 5> weighted_standard_deviation_ante{};
  std::array<double, 5> weighted_standard_error_ante{};
  std::array<double, 5> cumulative_regret_reconstruction_error_ante{};
  std::array<std::array<double, 5>, 5> pairwise_weighted_mean_ante{};
  std::array<std::array<double, 5>, 5> pairwise_weighted_standard_error_ante{};

  bool operator==(const HuPreflopRootActionAdvantageDiagnostic &) const = default;
};

enum class HuPreflopRootDecisionTracePolicyView : std::uint8_t { Average, Current };

// One sampled action after the forced CO root action. node_id and edge_index
// identify the exact public-tree edge without placing strings in the solver.
struct HuPreflopRootDecisionTraceStep {
  std::uint32_t node_id{0U};
  std::uint8_t player{0U};
  std::uint8_t edge_index{0U};

  friend bool operator==(const HuPreflopRootDecisionTraceStep &,
                         const HuPreflopRootDecisionTraceStep &) = default;
};

struct HuPreflopRootDecisionTraceValueSummary {
  std::uint64_t samples{0U};
  double probability{0.0};
  double mean_payoff_ante{0.0};
  double standard_error_ante{0.0};
  double ev_contribution_ante{0.0};

  friend bool operator==(const HuPreflopRootDecisionTraceValueSummary &,
                         const HuPreflopRootDecisionTraceValueSummary &) = default;
};

// Branches partition one forced root action by its complete sampled preflop
// continuation and terminal class. Their EV contributions sum to the action EV.
struct HuPreflopRootDecisionTraceBranch {
  std::vector<HuPreflopRootDecisionTraceStep> preflop_continuation;
  HuPreflopTelemetryTerminalType terminal_type{
      HuPreflopTelemetryTerminalType::PostflopContinuation};
  Street terminal_street{Street::Preflop};
  HuPreflopRootDecisionTraceValueSummary value;

  friend bool operator==(const HuPreflopRootDecisionTraceBranch &,
                         const HuPreflopRootDecisionTraceBranch &) = default;
};

struct HuPreflopRootDecisionTraceStreet {
  Street street{Street::Flop};
  // The value is the final hand payoff conditioned on reaching this street.
  // Street rows overlap and therefore do not form an additive decomposition.
  HuPreflopRootDecisionTraceValueSummary value;

  friend bool operator==(const HuPreflopRootDecisionTraceStreet &,
                         const HuPreflopRootDecisionTraceStreet &) = default;
};

// bucket_key uses the same representation-dependent key as the V19 action
// telemetry. For an exact-physical solve it identifies physical visible cards;
// for an abstract solve it identifies the information bucket history. Buckets
// partition one player/street reach row, not the entire action EV.
struct HuPreflopRootDecisionTraceBucket {
  Street street{Street::Flop};
  std::uint8_t player{0U};
  std::uint64_t bucket_key{0U};
  HuPreflopRootDecisionTraceValueSummary value;

  friend bool operator==(const HuPreflopRootDecisionTraceBucket &,
                         const HuPreflopRootDecisionTraceBucket &) = default;
};

struct HuPreflopRootDecisionTraceAction {
  // Stable CO root order: all-in, raise 6a, raise 10a, call, fold.
  std::uint8_t action_id{0U};
  HuPreflopRootDecisionTraceValueSummary value;
  std::vector<HuPreflopRootDecisionTraceBranch> preflop_branches;
  std::vector<HuPreflopRootDecisionTraceStreet> street_reach;
  std::vector<HuPreflopRootDecisionTraceBucket> buckets;

  friend bool operator==(const HuPreflopRootDecisionTraceAction &,
                         const HuPreflopRootDecisionTraceAction &) = default;
};

struct HuPreflopRootDecisionTracePolicy {
  HuPreflopRootDecisionTracePolicyView policy_view{
      HuPreflopRootDecisionTracePolicyView::Average};
  std::array<HuPreflopRootDecisionTraceAction, 5> actions{};
  std::array<std::array<double, 5>, 5> paired_difference_mean_ante{};
  std::array<std::array<double, 5>, 5> paired_difference_standard_error_ante{};

  friend bool operator==(const HuPreflopRootDecisionTracePolicy &,
                         const HuPreflopRootDecisionTracePolicy &) = default;
};

struct HuPreflopRootDecisionTrace {
  HandClassId hand_class{0U};
  std::uint32_t deals_per_action{0U};
  std::array<double, 5> average_strategy{};
  std::array<double, 5> current_strategy{};
  std::array<double, 5> cumulative_weighted_regret{};
  std::array<double, 5> cumulative_average_weight{};
  std::uint64_t last_update_iteration{0U};
  HuPreflopRootActionAdvantageDiagnostic training_action_advantage;
  std::vector<HuPreflopRootDecisionTracePolicy> policies;

  friend bool operator==(const HuPreflopRootDecisionTrace &,
                         const HuPreflopRootDecisionTrace &) = default;
};

struct HuPreflopSolveResult {
  std::array<std::array<double, 5>, 81> root_strategy{};
  // Training-state diagnostics use the same stable action order as root_strategy.
  // The cumulative values are algorithm state, not post-hoc action EV estimates.
  std::array<std::array<double, 5>, 81> root_current_strategy{};
  std::array<std::array<double, 5>, 81> root_cumulative_weighted_regret{};
  std::array<std::array<double, 5>, 81> root_cumulative_average_weight{};
  std::array<std::uint64_t, 81> root_information_last_iteration{};
  // Weighted moments of the root training samples Q_t(a) - V_t. These are
  // descriptive diagnostics for a changing policy, not confidence intervals
  // or a convergence certificate.
  std::array<HuPreflopRootActionAdvantageDiagnostic, 81> root_action_advantage_diagnostics{};
  std::array<std::array<double, 5>, 81> root_action_ev_ante{};
  std::array<std::array<double, 5>, 81> root_action_ev_standard_error_ante{};
  std::array<std::array<std::uint64_t, 5>, 81> root_action_ev_samples{};
  // Allocated only for the optional full-current diagnostic to keep the
  // frequently stack-allocated SolveResult bounded on Windows.
  std::vector<std::array<double, 5>> current_profile_root_action_ev_ante;
  std::vector<std::array<double, 5>> current_profile_root_action_ev_standard_error_ante;
  std::vector<std::array<std::uint64_t, 5>> current_profile_root_action_ev_samples;
  double root_ev_ante{0.0};
  double root_ev_standard_error_ante{0.0};
  double current_profile_root_ev_ante{0.0};
  double current_profile_root_ev_standard_error_ante{0.0};
  double abstract_nashconv_ante{0.0};
  double normalized_abstract_nashconv{0.0};
  double best_response_co_ev_ante{0.0};
  double best_response_btn_ev_ante{0.0};
  double best_response_co_standard_error_ante{0.0};
  double best_response_btn_standard_error_ante{0.0};
  double sampled_response_lower_bound_ante{0.0};
  double normalized_sampled_response_lower_bound{0.0};
  double current_profile_best_response_co_ev_ante{0.0};
  double current_profile_best_response_btn_ev_ante{0.0};
  double current_profile_best_response_co_standard_error_ante{0.0};
  double current_profile_best_response_btn_standard_error_ante{0.0};
  double current_profile_sampled_response_lower_bound_ante{0.0};
  double current_profile_normalized_sampled_response_lower_bound{0.0};
  bool current_profile_evaluated{false};
  double solve_seconds{0.0};
  std::uint64_t iterations{0};
  std::uint64_t postflop_training_iterations{0};
  std::uint64_t preflop_refinement_iterations{0};
  std::uint64_t information_sets{0};
  std::uint64_t best_response_information_sets{0};
  std::uint64_t current_profile_best_response_information_sets{0};
  std::uint64_t bytes_per_information_set_payload{0};
  std::uint64_t minimum_blueprint_payload_bytes{0};
  std::uint64_t minimum_best_response_payload_bytes{0};
  std::uint64_t minimum_exported_postflop_policy_payload_bytes{0};
  bool allocator_overhead_included{false};
  std::uint64_t seed{0};
  std::uint64_t partition_seed{0};
  std::uint64_t evaluation_seed{0};
  std::uint64_t winner_cache_hits{0};
  std::uint64_t winner_cache_misses{0};
  std::uint64_t evaluator_table_payload_bytes{0};
  std::string evaluator_backend_id;
  std::uint64_t numeric_state_payload_bytes{0};
  std::uint64_t numeric_state_budget_bytes{0};
  std::uint64_t bucket_cache_peak_entries{0};
  std::uint64_t bucket_cache_evictions{0};
  std::array<std::uint64_t, 3> bucket_mapping_visits{};
  std::array<std::uint64_t, 3> bucket_mapping_computations{};
  std::array<std::uint64_t, 3> occupied_distributional_buckets{};
  std::array<std::uint16_t, 3> distributional_bucket_capacities{};
  std::uint64_t average_policy_queries{0};
  std::uint64_t untrained_average_policy_queries{0};
  double bucket_mapping_seconds{0.0};
  std::uint64_t postflop_action_value_spread_samples{0};
  double mean_postflop_action_value_spread_ante{0.0};
  double maximum_postflop_action_value_spread_ante{0.0};
  std::uint8_t worker_threads{1U};
  std::uint32_t training_batch_iterations{0U};
  std::uint8_t root_action_value_rollouts{1U};
  bool root_continuation_mean_updates{false};
  bool symmetric_traverser_mean_updates{false};
  bool root_common_random_numbers{false};
  bool global_common_random_numbers{false};
  bool root_first_opponent_response_stratification{false};
  std::uint64_t peak_parallel_updates_per_job{0U};
  std::uint64_t peak_parallel_shadow_updates_per_worker{0U};
  std::uint64_t peak_parallel_scratch_payload_bytes{0U};
  std::uint64_t variance_baseline_information_sets{0U};
  std::uint64_t variance_baseline_payload_bytes{0U};
  bool exact_preflop_all_in_expectation{false};
  std::string preflop_all_in_equity_table_fingerprint;
  std::string postflop_all_in_expectation_id;
  std::array<std::uint64_t, 2> exact_postflop_all_in_evaluations{};
  std::array<std::uint64_t, 2> exact_postflop_all_in_runouts{};
  double exact_postflop_all_in_seconds{0.0};
  std::uint64_t exact_postflop_all_in_cache_hits{0U};
  std::uint64_t exact_postflop_all_in_cache_misses{0U};
  std::uint64_t exact_postflop_all_in_cache_peak_entries{0U};
  std::uint64_t exact_postflop_all_in_cache_evictions{0U};
  std::string chance_sampling_id;
  std::uint64_t compiled_betting_nodes{0};
  std::uint64_t compiled_betting_bytes{0};
  std::string tree_fingerprint;
  std::string abstraction_id;
  std::string algorithm_id;
  HuPreflopBlueprint preflop_blueprint;
  std::vector<HuPreflopDecisionEvaluation> preflop_decision_evaluations;
  std::vector<HuPreflopDecisionEvaluation> current_profile_preflop_decision_evaluations;
  std::vector<HuPreflopDecisionTrainingDiagnostic> preflop_decision_training_diagnostics;
  std::vector<HuPreflopRootDecisionTrace> root_decision_traces;
  bool action_conditioned_telemetry_enabled{false};
  std::vector<HuPreflopActionConditionedTelemetry> action_conditioned_telemetry;
  std::uint64_t action_conditioned_telemetry_dropped{0U};
  HuPreflopSampledPostflopPolicy postflop_policy;
};

// Deterministic category/equity abstraction primitive used only by the HU
// preflop research solver. Globally suit-isomorphic visible observations must
// map to the same bucket; opponent private cards are never part of the key.
[[nodiscard]] Result<std::uint16_t, HuPreflopError>
compute_hu_preflop_category_equity_bucket(const std::array<CardId, 2> &hole,
                                          const std::array<CardId, 5> &board, Street street,
                                          std::uint32_t samples, std::uint64_t seed);
// Enumerates every legal unordered future board for one physical HU matchup.
// Only the visible board prefix for the selected street is conditioned upon.
[[nodiscard]] Result<HuPreflopPostflopAllInEquity, HuPreflopError>
enumerate_hu_preflop_postflop_all_in_equity(
    const std::array<std::array<CardId, 2>, 2> &holes,
    const std::array<CardId, 5> &board, Street street, const IHandEvaluator &evaluator);
[[nodiscard]] Result<HuPreflopPostflopAllInEquity, HuPreflopError>
enumerate_hu_preflop_postflop_all_in_equity(
    const std::array<std::array<CardId, 2>, 2> &holes,
    const std::array<CardId, 5> &board, Street street);
// Maps visible cards only into the requested Flop/Turn/River capacities. The
// mapping is total for legal observations and frozen by partition_seed; it
// never reads the actual future runout.
[[nodiscard]] Result<std::uint16_t, HuPreflopError>
compute_hu_preflop_distributional_strength_bucket(const std::array<CardId, 2> &hole,
                                                  const std::array<CardId, 5> &board, Street street,
                                                  std::uint32_t samples,
                                                  std::uint64_t partition_seed);
[[nodiscard]] Result<std::uint16_t, HuPreflopError>
compute_hu_preflop_distributional_strength_bucket(const std::array<CardId, 2> &hole,
                                                  const std::array<CardId, 5> &board, Street street,
                                                  std::uint32_t samples,
                                                  std::uint64_t partition_seed,
                                                  const std::array<std::uint16_t, 3> &capacities);
// Versioned R5 challenger. It preserves a quantized equity coordinate and uses
// the remaining bucket bits for a deterministic profile of category, equity
// distribution, opponent groups, future category and board texture.
[[nodiscard]] Result<std::uint16_t, HuPreflopError>
compute_hu_preflop_distributional_profile_v6_bucket(const std::array<CardId, 2> &hole,
                                                    const std::array<CardId, 5> &board,
                                                    Street street, std::uint32_t samples,
                                                    std::uint64_t partition_seed,
                                                    const std::array<std::uint16_t, 3> &capacities);
// Versioned R6 challenger. It preserves the visible hand category, then
// quantizes equity and an ordered distributional coordinate without hashing.
// Capacities below 32 are rejected because they cannot isolate all categories.
[[nodiscard]] Result<std::uint16_t, HuPreflopError>
compute_hu_preflop_distributional_structured_v7_bucket(
    const std::array<CardId, 2> &hole, const std::array<CardId, 5> &board, Street street,
    std::uint32_t samples, std::uint64_t partition_seed,
    const std::array<std::uint16_t, 3> &capacities);
// Versioned R6 challenger. It spends progressively more category bits from
// Flop to River and prioritizes equity before the ordered distributional
// coordinate, avoiding the unused bucket domains produced by structured-v7.
[[nodiscard]] Result<std::uint16_t, HuPreflopError>
compute_hu_preflop_distributional_street_adaptive_v8_bucket(
    const std::array<CardId, 2> &hole, const std::array<CardId, 5> &board, Street street,
    std::uint32_t samples, std::uint64_t partition_seed,
    const std::array<std::uint16_t, 3> &capacities);
// V11 retains this exact V8 mapping for the current street. Its information
// key additionally stores only the exact visible category from the preceding
// street; the wrapper makes the mapping identity directly testable.
[[nodiscard]] Result<std::uint16_t, HuPreflopError>
compute_hu_preflop_distributional_adaptive_category_history_v11_bucket(
    const std::array<CardId, 2> &hole, const std::array<CardId, 5> &board, Street street,
    std::uint32_t samples, std::uint64_t partition_seed,
    const std::array<std::uint16_t, 3> &capacities);

// R6 diagnostic primitive shared with the public-board stratified trainer.
// For sample_count <= C(31,2), every traverser hole combination is unique;
// each position is marginally uniform and the opponent hand is sampled
// uniformly from the 29 cards that remain.
[[nodiscard]] Result<std::vector<HuPreflopPublicBoardPrivateDeal>, HuPreflopError>
sample_hu_preflop_public_board_private_deals(const std::array<CardId, 5> &board,
                                             std::uint8_t traverser, std::uint32_t sample_count,
                                             std::uint64_t seed);
// R6 diagnostic sampler. Each deal is uniform conditional on the traverser's
// requested exact preflop class and contains no duplicate physical cards.
[[nodiscard]] Result<std::vector<HuPreflopRootConditionalDeal>, HuPreflopError>
sample_hu_preflop_root_conditional_deals(HandClassId hand_class_id, std::uint8_t traverser,
                                         std::uint32_t sample_count, std::uint64_t seed);
[[nodiscard]] std::string
fingerprint_hu_preflop_sampled_postflop_policy(const HuPreflopSampledPostflopPolicy &policy);
[[nodiscard]] Result<bool, HuPreflopError>
validate_hu_preflop_sampled_postflop_policy(const HuPreflopTree &tree,
                                            const HuPreflopSampledPostflopPolicy &policy);
[[nodiscard]] Result<std::string, HuPreflopError>
serialize_hu_preflop_sampled_postflop_policy(const HuPreflopSampledPostflopPolicy &policy);
[[nodiscard]] Result<HuPreflopSampledPostflopPolicy, HuPreflopError>
deserialize_hu_preflop_sampled_postflop_policy(
    const HuPreflopTree &tree, const std::string &serialized,
    std::uint64_t maximum_serialized_bytes =
        hu_preflop_sampled_postflop_default_maximum_serialized_bytes);
[[nodiscard]] Result<bool, HuPreflopError>
save_hu_preflop_sampled_postflop_policy(const HuPreflopSampledPostflopPolicy &policy,
                                        const std::string &path);
[[nodiscard]] Result<HuPreflopSampledPostflopPolicy, HuPreflopError>
load_hu_preflop_sampled_postflop_policy(
    const HuPreflopTree &tree, const std::string &path,
    std::uint64_t maximum_serialized_bytes =
        hu_preflop_sampled_postflop_default_maximum_serialized_bytes);
[[nodiscard]] Result<std::array<double, hu_preflop_sampled_postflop_maximum_actions>,
                     HuPreflopError>
query_hu_preflop_sampled_postflop_policy(const HuPreflopSampledPostflopPolicy &policy,
                                         const HuPreflopSampledPostflopPolicyKey &key,
                                         std::uint8_t action_count);
[[nodiscard]] Result<std::array<double, hu_preflop_sampled_postflop_maximum_actions>,
                     HuPreflopError>
query_hu_preflop_sampled_postflop_policy(const HuPreflopSampledPostflopPolicy &policy,
                                         const HuPreflopSampledPostflopPolicyKey &key,
                                         std::uint8_t action_count,
                                         HuPreflopSampledPolicyView view);
[[nodiscard]] Result<HuPreflopSampledPostflopPublicDecision, HuPreflopError>
derive_hu_preflop_sampled_postflop_public_decision(
    const HuPreflopTree &tree, std::uint32_t entry_node,
    const std::array<CardId, 5> &board, const PublicState &state,
    std::span<const Action> action_prefix);
[[nodiscard]] Result<HuPreflopSampledPostflopPolicyKey, HuPreflopError>
derive_hu_preflop_sampled_postflop_policy_key(
    const HuPreflopSampledPostflopPolicy &policy,
    const HuPreflopSampledPostflopPublicDecision &decision,
    const std::array<CardId, 5> &board, ComboId combo);
// Replays one concrete history and returns the complete average strategy in
// legal-action order. Batch evaluators use this form to derive the expensive
// abstraction key once per combo and decision instead of once per action.
[[nodiscard]] Result<std::array<double, hu_preflop_sampled_postflop_maximum_actions>,
                     HuPreflopError>
query_hu_preflop_sampled_postflop_strategy(
    const HuPreflopTree &tree, const HuPreflopSampledPostflopPolicy &policy,
    std::uint32_t entry_node, const std::array<CardId, 5> &board, const PublicState &state,
    std::span<const Action> action_prefix, ComboId combo,
    HuPreflopSampledPolicyLookupMode lookup_mode =
        HuPreflopSampledPolicyLookupMode::AllowConfiguredFallback);
// Replays one concrete postflop history using the same semantic key derivation
// as the sampled trainer, then returns the selected action's average-policy
// probability. The caller can wrap this in HuPostflopActionProbabilityProvider
// after validating the snapshot once.
[[nodiscard]] Result<double, HuPreflopError> query_hu_preflop_sampled_postflop_action_probability(
    const HuPreflopTree &tree, const HuPreflopSampledPostflopPolicy &policy,
    std::uint32_t entry_node, const std::array<CardId, 5> &board, const PublicState &state,
    std::span<const Action> action_prefix, const Action &selected_action, ComboId combo);
[[nodiscard]] Result<double, HuPreflopError> query_hu_preflop_sampled_postflop_action_probability(
    const HuPreflopTree &tree, const HuPreflopSampledPostflopPolicy &policy,
    std::uint32_t entry_node, const std::array<CardId, 5> &board, const PublicState &state,
    std::span<const Action> action_prefix, const Action &selected_action, ComboId combo,
    HuPreflopSampledPolicyLookupMode lookup_mode);
[[nodiscard]] HuPreflopConfig make_hu_co40_benchmark_config();
[[nodiscard]] Result<HuPreflopConfig, HuPreflopError>
deserialize_hu_preflop_config_json(const std::string &serialized);
[[nodiscard]] Result<bool, HuPreflopError>
validate_hu_preflop_config(const HuPreflopConfig &config);
[[nodiscard]] Result<HuPreflopTree, HuPreflopError>
build_hu_preflop_tree(const HuPreflopConfig &config);
[[nodiscard]] Result<HuPostflopPublicStats, HuPreflopError>
analyze_hu_postflop_public_skeleton(const HuPreflopTree &tree);
[[nodiscard]] Result<HuPreflopBlueprint, HuPreflopError>
make_uniform_hu_preflop_blueprint(const HuPreflopTree &tree);
[[nodiscard]] std::string fingerprint_hu_preflop_blueprint(const HuPreflopBlueprint &blueprint);
[[nodiscard]] std::string
fingerprint_hu_preflop_decomposition_plan(const HuPreflopDecompositionPlan &plan);
[[nodiscard]] std::string
fingerprint_hu_preflop_flop_boundary(const HuPreflopFlopBoundary &boundary);
[[nodiscard]] std::string
fingerprint_hu_preflop_global_best_response(const HuPreflopGlobalBestResponseEvidence &evidence);
[[nodiscard]] Result<HuPreflopGlobalBestResponseEvidence, HuPreflopError>
build_hu_preflop_global_best_response_evidence(
    const HuPreflopTree &tree, const HuPreflopBlueprint &blueprint,
    const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopWholeGameCoverageAccumulator &coverage,
    const HuPreflopAllInBoardCatalog &all_in_catalog,
    const HuPreflopAllInEquityTable &all_in_equity_table,
    std::span<const HuPreflopBestResponsePreflopEvaluation> best_responses,
    const HuPreflopExactProfileEvaluation &exact_profile);
[[nodiscard]] Result<bool, HuPreflopError>
validate_hu_preflop_blueprint(const HuPreflopTree &tree, const HuPreflopBlueprint &blueprint);
[[nodiscard]] Result<HuPreflopDecompositionPlan, HuPreflopError>
derive_hu_preflop_decomposition_plan(const HuPreflopTree &tree,
                                     const HuPreflopBlueprint &blueprint);
[[nodiscard]] Result<HuPreflopConditionedRanges, HuPreflopError>
condition_hu_preflop_ranges_on_flop(const HuPreflopDecompositionPlan &plan,
                                    std::uint32_t entry_node, const std::array<CardId, 3> &flop);
[[nodiscard]] Result<std::vector<HuPreflopCanonicalFlopTask>, HuPreflopError>
enumerate_hu_preflop_canonical_flop_tasks(const HuPreflopDecompositionPlan &plan);
[[nodiscard]] Result<HuPreflopQuantizedRanges, HuPreflopError>
quantize_hu_preflop_conditioned_ranges(const HuPreflopConditionedRanges &conditioned);
[[nodiscard]] Result<HuPreflopFlopBoundary, HuPreflopError> build_hu_preflop_flop_boundary(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &plan,
    const HuPreflopConditionedRanges &conditioned, std::uint8_t resolving_player,
    const HuPreflopComboReach &opponent_blueprint_cfv_antes, std::string continuation_fingerprint,
    std::uint64_t blueprint_iterations, std::string continuation_checkpoint_fingerprint = {});
[[nodiscard]] Result<bool, HuPreflopError>
validate_hu_preflop_flop_boundary(const HuPreflopTree &tree, const HuPreflopDecompositionPlan &plan,
                                  const HuPreflopFlopBoundary &boundary);
[[nodiscard]] Result<std::vector<HuPostflopBettingRootShape>, HuPreflopError>
enumerate_hu_postflop_betting_root_shapes(const HuPreflopTree &tree, Street target_street);
[[nodiscard]] Result<PublicState, HuPreflopError>
replay_hu_postflop_betting_root_shape(const HuPreflopTree &tree,
                                      const HuPostflopBettingRootShape &shape);
[[nodiscard]] Result<std::vector<HuPostflopUpperStreetTerminalShape>, HuPreflopError>
enumerate_hu_postflop_upper_street_terminal_shapes(const HuPreflopTree &tree);
[[nodiscard]] Result<PublicState, HuPreflopError>
replay_hu_postflop_upper_street_terminal_shape(const HuPreflopTree &tree,
                                               const HuPostflopUpperStreetTerminalShape &shape);
[[nodiscard]] Result<HuPreflopRiverWorkEstimate, HuPreflopError>
estimate_hu_preflop_river_work(const HuPreflopTree &tree, const HuPreflopDecompositionPlan &plan);
[[nodiscard]] Result<HuPreflopRiverBatchPlan, HuPreflopError> derive_hu_preflop_river_batch_plan(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &plan,
    const HuPreflopRiverWorkEstimate &work, const HuPreflopRiverRootCatalog &catalog,
    std::uint64_t target_batch_payload_bytes);
[[nodiscard]] Result<HuPreflopRiverBatch, HuPreflopError>
hu_preflop_river_batch_at(const HuPreflopRiverBatchPlan &plan, std::uint64_t batch_index);
[[nodiscard]] Result<HuPreflopRiverResolverRoot, HuPreflopError> hu_preflop_river_resolver_root_at(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopRiverWorkEstimate &work, const HuPreflopRiverBatchPlan &batch_plan,
    std::uint64_t resolver_root_ordinal);
[[nodiscard]] Result<HuPreflopRiverRootCatalog, HuPreflopError>
build_hu_preflop_river_root_catalog(const HuPreflopTree &tree,
                                    const HuPreflopDecompositionPlan &decomposition,
                                    const HuPreflopRiverWorkEstimate &work);
[[nodiscard]] Result<std::vector<HuPreflopRiverTurnGroup>, HuPreflopError>
enumerate_hu_preflop_river_turn_groups(const HuPreflopRiverRootCatalog &catalog,
                                       std::uint64_t task_span_index);
[[nodiscard]] Result<std::vector<HuPreflopRiverTurnResolverSpan>, HuPreflopError>
enumerate_hu_preflop_river_turn_group_resolver_spans(const HuPreflopRiverRootCatalog &catalog,
                                                     const HuPreflopRiverTurnGroup &turn_group);
[[nodiscard]] Result<HuPreflopRiverResolverRoot, HuPreflopError>
hu_preflop_river_resolver_root_at(const HuPreflopRiverRootCatalog &catalog,
                                  const HuPreflopRiverBatchPlan &batch_plan,
                                  std::uint64_t resolver_root_ordinal);
[[nodiscard]] Result<std::vector<HuPreflopRiverResolverRoot>, HuPreflopError>
enumerate_hu_preflop_river_batch_roots(const HuPreflopRiverRootCatalog &catalog,
                                       const HuPreflopRiverBatchPlan &batch_plan,
                                       const HuPreflopRiverBatch &batch);
// Materializes one physical board across every River betting shape after a
// single catalog/plan validation. The returned order follows the task's shape
// order and selects exactly one resolver side per public subgame.
[[nodiscard]] Result<std::vector<HuPreflopRiverResolverRoot>, HuPreflopError>
enumerate_hu_preflop_river_board_roots(const HuPreflopRiverRootCatalog &catalog,
                                       const HuPreflopRiverBatchPlan &batch_plan,
                                       std::uint64_t task_span_index,
                                       std::uint64_t canonical_board_offset,
                                       std::uint8_t resolving_player);
[[nodiscard]] Result<HuPreflopRiverSchedulerCheckpoint, HuPreflopError>
make_hu_preflop_river_scheduler_checkpoint(const HuPreflopRiverBatchPlan &plan,
                                           std::string upper_street_accumulator_fingerprint);
[[nodiscard]] Result<HuPreflopRiverSchedulerCheckpoint, HuPreflopError>
advance_hu_preflop_river_scheduler_checkpoint(const HuPreflopRiverBatchPlan &plan,
                                              const HuPreflopRiverSchedulerCheckpoint &checkpoint,
                                              const HuPreflopRiverBatch &completed_batch,
                                              std::string upper_street_accumulator_fingerprint);
[[nodiscard]] std::string
fingerprint_hu_preflop_river_batch_plan(const HuPreflopRiverBatchPlan &plan);
[[nodiscard]] std::string fingerprint_hu_preflop_river_batch(const HuPreflopRiverBatch &batch);
[[nodiscard]] std::string
fingerprint_hu_preflop_river_resolver_root(const HuPreflopRiverResolverRoot &root);
[[nodiscard]] std::string
fingerprint_hu_preflop_river_root_catalog(const HuPreflopRiverRootCatalog &catalog);
[[nodiscard]] std::string
fingerprint_hu_preflop_river_root_boundary(const HuPreflopRiverRootBoundary &boundary);
[[nodiscard]] std::string
fingerprint_hu_preflop_river_task_accumulator(const HuPreflopRiverTaskAccumulator &accumulator);
[[nodiscard]] std::string
fingerprint_hu_preflop_river_task_aggregate(const HuPreflopRiverTaskAggregate &aggregate);
[[nodiscard]] std::string fingerprint_hu_preflop_upper_street_terminal_contribution(
    const HuPreflopUpperStreetTerminalContribution &contribution);
[[nodiscard]] std::string fingerprint_hu_preflop_upper_street_best_response_terminal_contribution(
    const HuPreflopUpperStreetBestResponseTerminalContribution &contribution);
[[nodiscard]] std::string
fingerprint_hu_preflop_flop_task_accumulator(const HuPreflopFlopTaskAccumulator &accumulator);
[[nodiscard]] Result<HuPreflopRiverConditionedReach, HuPreflopError>
condition_hu_preflop_ranges_on_river_root(
    const HuPreflopDecompositionPlan &decomposition, const HuPreflopRiverResolverRoot &root,
    const std::array<HuPreflopComboReach, 2> &postflop_action_sequence_reach);
[[nodiscard]] Result<HuPreflopRiverConditionedReach, HuPreflopError>
derive_hu_preflop_river_conditioned_reach(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopRiverResolverRoot &root,
    const HuPostflopActionProbabilityProvider &probability_provider);
[[nodiscard]] Result<HuPreflopRiverTaskAccumulator, HuPreflopError>
make_hu_preflop_river_task_accumulator(
    const HuPreflopRiverRootCatalog &catalog, const HuPreflopRiverBatchPlan &batch_plan,
    std::uint64_t task_span_index, std::uint64_t blueprint_iterations,
    HuPreflopContinuationValueMode value_mode = HuPreflopContinuationValueMode::AverageStrategy);
[[nodiscard]] Result<bool, HuPreflopError>
accumulate_hu_preflop_river_root_boundary(HuPreflopRiverTaskAccumulator &accumulator,
                                          const HuPreflopRiverRootBoundary &boundary);
[[nodiscard]] Result<bool, HuPreflopError>
seal_hu_preflop_river_task_accumulator(HuPreflopRiverTaskAccumulator &accumulator);
[[nodiscard]] Result<bool, HuPreflopError>
validate_hu_preflop_river_task_accumulator(const HuPreflopRiverTaskAccumulator &accumulator);
[[nodiscard]] Result<bool, HuPreflopError>
validate_hu_preflop_river_task_aggregate(const HuPreflopRiverTaskAggregate &aggregate);
[[nodiscard]] Result<HuPreflopRiverTaskAggregate, HuPreflopError>
finalize_hu_preflop_river_task_accumulator(const HuPreflopRiverTaskAccumulator &accumulator);
[[nodiscard]] Result<HuPreflopUpperStreetTerminalContribution, HuPreflopError>
evaluate_hu_preflop_upper_street_terminal_contribution(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    std::uint64_t task_span_index, const std::array<CardId, 3> &flop,
    std::uint64_t terminal_ordinal, const HuPostflopUpperStreetTerminalShape &terminal_shape,
    const HuPostflopActionProbabilityProvider &probability_provider,
    std::uint64_t blueprint_iterations, std::string continuation_checkpoint_fingerprint);
[[nodiscard]] Result<HuPreflopUpperStreetBestResponseTerminalContribution, HuPreflopError>
evaluate_hu_preflop_upper_street_best_response_terminal_contribution(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopRiverRootCatalog &catalog, std::uint64_t task_span_index,
    const HuPostflopUpperStreetTerminalShape &terminal_shape,
    const HuPreflopRiverTurnGroup *turn_group,
    const HuPostflopActionProbabilityProvider &opponent_probability_provider,
    std::uint8_t responding_player, std::uint64_t blueprint_iterations,
    std::string continuation_fingerprint);
// Selects the exact physical leaf rows from a validated terminal contribution.
[[nodiscard]] Result<std::vector<HuPreflopBestResponseComboValue>, HuPreflopError>
select_hu_preflop_upper_street_best_response_leaf(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopRiverRootCatalog &catalog,
    const HuPreflopUpperStreetBestResponseTerminalContribution &contribution,
    const HuPreflopBestResponseLeafQuery &query);
// Reduces action-child counterfactual values independently for every physical
// responding combo. At a responding-player node, counterfactual reach must be
// action-invariant and the maximum utility child is selected. At an opponent
// node, action-weighted reach and utility are summed.
[[nodiscard]] Result<std::vector<HuPreflopBestResponseComboValue>, HuPreflopError>
reduce_hu_preflop_best_response_action_values(
    std::span<const std::vector<HuPreflopBestResponseComboValue>> action_children,
    const std::array<CardId, 3> &flop, bool responding_player_acts);
[[nodiscard]] Result<HuPreflopBestResponseTaskEvaluation, HuPreflopError>
evaluate_hu_preflop_best_response_task(const HuPreflopTree &tree,
                                       const HuPreflopDecompositionPlan &decomposition,
                                       std::uint64_t task_span_index,
                                       const HuPreflopBestResponseLeafProvider &leaf_provider,
                                       std::string continuation_fingerprint,
                                       std::uint8_t responding_player,
                                       std::uint64_t blueprint_iterations);
[[nodiscard]] std::string fingerprint_hu_preflop_best_response_task_evaluation(
    const HuPreflopBestResponseTaskEvaluation &evaluation);
[[nodiscard]] Result<bool, HuPreflopError> validate_hu_preflop_best_response_task_evaluation(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopBestResponseTaskEvaluation &evaluation);
[[nodiscard]] std::string fingerprint_hu_preflop_river_best_response_leaf_accumulator(
    const HuPreflopRiverBestResponseLeafAccumulator &accumulator);
[[nodiscard]] Result<HuPreflopRiverBestResponseLeafAccumulator, HuPreflopError>
make_hu_preflop_river_best_response_leaf_accumulator(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopRiverRootCatalog &catalog, const HuPreflopRiverBatchPlan &batch_plan,
    const HuPreflopBestResponseLeafQuery &query, std::string continuation_fingerprint,
    std::uint64_t blueprint_iterations);
[[nodiscard]] Result<bool, HuPreflopError> accumulate_hu_preflop_river_best_response_leaf_boundary(
    const HuPreflopRiverRootCatalog &catalog, const HuPreflopRiverBatchPlan &batch_plan,
    HuPreflopRiverBestResponseLeafAccumulator &accumulator,
    const HuPreflopRiverRootBoundary &boundary);
[[nodiscard]] Result<bool, HuPreflopError> validate_hu_preflop_river_best_response_leaf_accumulator(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopRiverRootCatalog &catalog, const HuPreflopRiverBatchPlan &batch_plan,
    const HuPreflopRiverBestResponseLeafAccumulator &accumulator);
[[nodiscard]] Result<std::vector<HuPreflopBestResponseComboValue>, HuPreflopError>
finalize_hu_preflop_river_best_response_leaf_accumulator(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopRiverRootCatalog &catalog, const HuPreflopRiverBatchPlan &batch_plan,
    const HuPreflopRiverBestResponseLeafAccumulator &accumulator);
// Dispatches one BR leaf to its validated terminal source or streams the exact
// River-root manifest through a fixed-Turn accumulator. Only one root boundary
// returned by river_boundary_provider is live at a time.
[[nodiscard]] Result<std::vector<HuPreflopBestResponseComboValue>, HuPreflopError>
resolve_hu_preflop_best_response_leaf_streaming(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopRiverRootCatalog &catalog, const HuPreflopRiverBatchPlan &batch_plan,
    const HuPreflopBestResponseLeafQuery &query,
    const HuPreflopBestResponseTerminalContributionProvider &terminal_provider,
    const HuPreflopBestResponseRiverBoundaryProvider &river_boundary_provider,
    std::string continuation_fingerprint, std::uint64_t blueprint_iterations,
    const HuPreflopRiverBestResponseLeafAccumulator *resume_from = nullptr,
    const HuPreflopRiverBestResponseLeafCheckpointSink &checkpoint_sink = {});
// Runs the complete Flop/Turn BR recursion for one public-root task using the
// bounded-memory dispatcher above.
[[nodiscard]] Result<HuPreflopBestResponseTaskEvaluation, HuPreflopError>
evaluate_hu_preflop_best_response_task_streaming(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopRiverRootCatalog &catalog, const HuPreflopRiverBatchPlan &batch_plan,
    std::uint64_t task_span_index,
    const HuPreflopBestResponseTerminalContributionProvider &terminal_provider,
    const HuPreflopBestResponseRiverBoundaryProvider &river_boundary_provider,
    std::string continuation_fingerprint, std::uint8_t responding_player,
    std::uint64_t blueprint_iterations);
[[nodiscard]] std::string fingerprint_hu_preflop_best_response_entry_accumulator(
    const HuPreflopBestResponseEntryAccumulator &accumulator);
[[nodiscard]] std::string fingerprint_hu_preflop_best_response_entry_evaluation(
    const HuPreflopBestResponseEntryEvaluation &evaluation);
[[nodiscard]] Result<HuPreflopBestResponseEntryAccumulator, HuPreflopError>
make_hu_preflop_best_response_entry_accumulator(const HuPreflopTree &tree,
                                                const HuPreflopDecompositionPlan &decomposition,
                                                std::uint64_t entry_index,
                                                std::string continuation_fingerprint,
                                                std::uint8_t responding_player,
                                                std::uint64_t blueprint_iterations);
[[nodiscard]] Result<bool, HuPreflopError> accumulate_hu_preflop_best_response_task_evaluation(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    HuPreflopBestResponseEntryAccumulator &accumulator,
    const HuPreflopBestResponseTaskEvaluation &evaluation);
[[nodiscard]] Result<bool, HuPreflopError> validate_hu_preflop_best_response_entry_accumulator(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopBestResponseEntryAccumulator &accumulator);
[[nodiscard]] Result<HuPreflopBestResponseEntryEvaluation, HuPreflopError>
finalize_hu_preflop_best_response_entry_accumulator(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopBestResponseEntryAccumulator &accumulator);
[[nodiscard]] Result<bool, HuPreflopError> validate_hu_preflop_best_response_entry_evaluation(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopBestResponseEntryEvaluation &evaluation);
[[nodiscard]] std::string fingerprint_hu_preflop_best_response_preflop_terminal_evaluation(
    const HuPreflopBestResponsePreflopTerminalEvaluation &evaluation);
[[nodiscard]] Result<HuPreflopBestResponsePreflopTerminalEvaluation, HuPreflopError>
evaluate_hu_preflop_best_response_fold_terminal(const HuPreflopTree &tree,
                                                const HuPreflopBlueprint &blueprint,
                                                std::uint32_t node_id,
                                                std::string continuation_fingerprint,
                                                std::uint8_t responding_player,
                                                std::uint64_t blueprint_iterations);
[[nodiscard]] Result<bool, HuPreflopError>
validate_hu_preflop_best_response_preflop_terminal_evaluation(
    const HuPreflopTree &tree, const HuPreflopBlueprint &blueprint,
    const HuPreflopBestResponsePreflopTerminalEvaluation &evaluation);
[[nodiscard]] Result<std::vector<HuPreflopBestResponseHandClassValue>, HuPreflopError>
select_hu_preflop_best_response_preflop_terminal_evaluation(
    const HuPreflopTree &tree, const HuPreflopBlueprint &blueprint,
    const HuPreflopBestResponsePreflopLeafQuery &query,
    const HuPreflopBestResponsePreflopTerminalEvaluation &evaluation,
    std::string continuation_fingerprint, std::uint64_t blueprint_iterations);
[[nodiscard]] std::string
fingerprint_hu_preflop_all_in_board_catalog(const HuPreflopAllInBoardCatalog &catalog);
[[nodiscard]] std::string fingerprint_hu_preflop_all_in_equity_accumulator(
    const HuPreflopAllInEquityAccumulator &accumulator);
[[nodiscard]] std::string
fingerprint_hu_preflop_all_in_equity_table(const HuPreflopAllInEquityTable &table);
[[nodiscard]] Result<HuPreflopAllInBoardCatalog, HuPreflopError>
build_hu_preflop_all_in_board_catalog();
[[nodiscard]] Result<bool, HuPreflopError>
validate_hu_preflop_all_in_board_catalog(const HuPreflopAllInBoardCatalog &catalog);
[[nodiscard]] Result<HuPreflopAllInEquityAccumulator, HuPreflopError>
make_hu_preflop_all_in_equity_accumulator(const HuPreflopAllInBoardCatalog &catalog);
[[nodiscard]] Result<bool, HuPreflopError>
accumulate_hu_preflop_all_in_equity_board(const HuPreflopAllInBoardCatalog &catalog,
                                          HuPreflopAllInEquityAccumulator &accumulator);
[[nodiscard]] Result<bool, HuPreflopError>
validate_hu_preflop_all_in_equity_accumulator(const HuPreflopAllInBoardCatalog &catalog,
                                              const HuPreflopAllInEquityAccumulator &accumulator);
[[nodiscard]] Result<HuPreflopAllInEquityTable, HuPreflopError>
finalize_hu_preflop_all_in_equity_accumulator(const HuPreflopAllInBoardCatalog &catalog,
                                              const HuPreflopAllInEquityAccumulator &accumulator);
[[nodiscard]] Result<HuPreflopAllInEquityTable, HuPreflopError>
build_hu_preflop_all_in_equity_table(const HuPreflopAllInBoardCatalog &catalog);
[[nodiscard]] Result<bool, HuPreflopError>
validate_hu_preflop_all_in_equity_table(const HuPreflopAllInBoardCatalog &catalog,
                                        const HuPreflopAllInEquityTable &table);
[[nodiscard]] Result<HuPreflopBestResponsePreflopTerminalEvaluation, HuPreflopError>
evaluate_hu_preflop_best_response_all_in_terminal(
    const HuPreflopTree &tree, const HuPreflopBlueprint &blueprint,
    const HuPreflopAllInBoardCatalog &catalog, const HuPreflopAllInEquityTable &equity_table,
    std::uint32_t node_id, std::string continuation_fingerprint, std::uint8_t responding_player,
    std::uint64_t blueprint_iterations);
[[nodiscard]] std::string fingerprint_hu_preflop_best_response_preflop_evaluation(
    const HuPreflopBestResponsePreflopEvaluation &evaluation);
[[nodiscard]] Result<HuPreflopBestResponsePreflopEvaluation, HuPreflopError>
evaluate_hu_preflop_best_response_preflop(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopBestResponsePreflopLeafProvider &leaf_provider,
    std::string continuation_fingerprint, std::uint8_t responding_player,
    std::uint64_t blueprint_iterations);
// Certifying whole-game assembler. Every postflop entry and every preflop
// terminal must be present exactly once and share one tree, blueprint,
// continuation profile, responder and checkpoint iteration.
[[nodiscard]] Result<HuPreflopBestResponsePreflopEvaluation, HuPreflopError>
evaluate_hu_preflop_best_response_whole_game(
    const HuPreflopTree &tree, const HuPreflopBlueprint &blueprint,
    const HuPreflopDecompositionPlan &decomposition,
    std::span<const HuPreflopBestResponseEntryEvaluation> postflop_entries,
    std::span<const HuPreflopBestResponsePreflopTerminalEvaluation> preflop_terminals,
    std::string continuation_fingerprint, std::uint8_t responding_player,
    std::uint64_t blueprint_iterations);
[[nodiscard]] Result<bool, HuPreflopError> validate_hu_preflop_best_response_preflop_evaluation(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopBestResponsePreflopEvaluation &evaluation);
[[nodiscard]] std::string
fingerprint_hu_preflop_exact_profile_evaluation(const HuPreflopExactProfileEvaluation &evaluation);
[[nodiscard]] Result<HuPreflopExactProfileEvaluation, HuPreflopError>
evaluate_hu_preflop_exact_profile(const HuPreflopTree &tree, const HuPreflopBlueprint &blueprint,
                                  const HuPreflopDecompositionPlan &decomposition,
                                  const HuPreflopWholeGameCoverageAccumulator &coverage,
                                  const HuPreflopAllInBoardCatalog &all_in_catalog,
                                  const HuPreflopAllInEquityTable &all_in_equity_table);
[[nodiscard]] Result<bool, HuPreflopError>
validate_hu_preflop_exact_profile_evaluation(const HuPreflopTree &tree,
                                             const HuPreflopBlueprint &blueprint,
                                             const HuPreflopDecompositionPlan &decomposition,
                                             const HuPreflopWholeGameCoverageAccumulator &coverage,
                                             const HuPreflopAllInBoardCatalog &all_in_catalog,
                                             const HuPreflopAllInEquityTable &all_in_equity_table,
                                             const HuPreflopExactProfileEvaluation &evaluation);
[[nodiscard]] Result<bool, HuPreflopError> validate_hu_preflop_upper_street_terminal_contribution(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopUpperStreetTerminalContribution &contribution);
[[nodiscard]] Result<bool, HuPreflopError>
validate_hu_preflop_upper_street_best_response_terminal_contribution(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopRiverRootCatalog &catalog,
    const HuPreflopUpperStreetBestResponseTerminalContribution &contribution);
[[nodiscard]] Result<HuPreflopFlopTaskAccumulator, HuPreflopError>
make_hu_preflop_flop_task_accumulator(const HuPreflopTree &tree,
                                      const HuPreflopDecompositionPlan &decomposition,
                                      const HuPreflopRiverTaskAggregate &river_aggregate);
[[nodiscard]] Result<bool, HuPreflopError> accumulate_hu_preflop_upper_street_terminal_contribution(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    HuPreflopFlopTaskAccumulator &accumulator,
    const HuPreflopUpperStreetTerminalContribution &contribution);
[[nodiscard]] Result<bool, HuPreflopError>
validate_hu_preflop_flop_task_accumulator(const HuPreflopTree &tree,
                                          const HuPreflopDecompositionPlan &decomposition,
                                          const HuPreflopFlopTaskAccumulator &accumulator);
[[nodiscard]] Result<std::array<HuPreflopFlopBoundary, 2>, HuPreflopError>
finalize_hu_preflop_flop_task_accumulator(const HuPreflopTree &tree,
                                          const HuPreflopDecompositionPlan &decomposition,
                                          const HuPreflopFlopTaskAccumulator &accumulator);
[[nodiscard]] std::string fingerprint_hu_preflop_river_scheduler_checkpoint(
    const HuPreflopRiverSchedulerCheckpoint &checkpoint);
[[nodiscard]] Result<std::string, HuPreflopError>
serialize_hu_preflop_blueprint(const HuPreflopBlueprint &blueprint);
[[nodiscard]] Result<HuPreflopBlueprint, HuPreflopError>
deserialize_hu_preflop_blueprint(const std::string &serialized);
[[nodiscard]] Result<std::string, HuPreflopError>
serialize_hu_preflop_decomposition_plan(const HuPreflopDecompositionPlan &plan);
[[nodiscard]] Result<HuPreflopDecompositionPlan, HuPreflopError>
deserialize_hu_preflop_decomposition_plan(const std::string &serialized);
[[nodiscard]] Result<std::string, HuPreflopError>
serialize_hu_preflop_flop_boundary(const HuPreflopFlopBoundary &boundary);
[[nodiscard]] Result<HuPreflopFlopBoundary, HuPreflopError>
deserialize_hu_preflop_flop_boundary(const std::string &serialized);
[[nodiscard]] Result<std::string, HuPreflopError>
serialize_hu_preflop_whole_game_coverage_accumulator(
    const HuPreflopWholeGameCoverageAccumulator &accumulator);
[[nodiscard]] Result<HuPreflopWholeGameCoverageAccumulator, HuPreflopError>
deserialize_hu_preflop_whole_game_coverage_accumulator(const std::string &serialized);
[[nodiscard]] Result<std::string, HuPreflopError> serialize_hu_preflop_river_scheduler_checkpoint(
    const HuPreflopRiverSchedulerCheckpoint &checkpoint);
[[nodiscard]] Result<HuPreflopRiverSchedulerCheckpoint, HuPreflopError>
deserialize_hu_preflop_river_scheduler_checkpoint(const std::string &serialized);
[[nodiscard]] Result<std::string, HuPreflopError>
serialize_hu_preflop_river_task_accumulator(const HuPreflopRiverTaskAccumulator &accumulator);
[[nodiscard]] Result<HuPreflopRiverTaskAccumulator, HuPreflopError>
deserialize_hu_preflop_river_task_accumulator(const std::string &serialized);
[[nodiscard]] Result<std::string, HuPreflopError>
serialize_hu_preflop_river_task_aggregate(const HuPreflopRiverTaskAggregate &aggregate);
[[nodiscard]] Result<HuPreflopRiverTaskAggregate, HuPreflopError>
deserialize_hu_preflop_river_task_aggregate(const std::string &serialized);
[[nodiscard]] Result<std::string, HuPreflopError>
serialize_hu_preflop_river_best_response_leaf_accumulator(
    const HuPreflopRiverBestResponseLeafAccumulator &accumulator);
[[nodiscard]] Result<HuPreflopRiverBestResponseLeafAccumulator, HuPreflopError>
deserialize_hu_preflop_river_best_response_leaf_accumulator(const std::string &serialized);
[[nodiscard]] Result<std::string, HuPreflopError>
serialize_hu_preflop_best_response_task_evaluation(
    const HuPreflopBestResponseTaskEvaluation &evaluation);
[[nodiscard]] Result<HuPreflopBestResponseTaskEvaluation, HuPreflopError>
deserialize_hu_preflop_best_response_task_evaluation(const std::string &serialized);
[[nodiscard]] Result<std::string, HuPreflopError>
serialize_hu_preflop_best_response_entry_accumulator(
    const HuPreflopBestResponseEntryAccumulator &accumulator);
[[nodiscard]] Result<HuPreflopBestResponseEntryAccumulator, HuPreflopError>
deserialize_hu_preflop_best_response_entry_accumulator(const std::string &serialized);
[[nodiscard]] Result<std::string, HuPreflopError>
serialize_hu_preflop_best_response_entry_evaluation(
    const HuPreflopBestResponseEntryEvaluation &evaluation);
[[nodiscard]] Result<HuPreflopBestResponseEntryEvaluation, HuPreflopError>
deserialize_hu_preflop_best_response_entry_evaluation(const std::string &serialized);
[[nodiscard]] Result<bool, HuPreflopError>
save_hu_preflop_blueprint(const HuPreflopBlueprint &blueprint, const std::string &path);
[[nodiscard]] Result<HuPreflopBlueprint, HuPreflopError>
load_hu_preflop_blueprint(const std::string &path);
[[nodiscard]] Result<bool, HuPreflopError>
save_hu_preflop_decomposition_plan(const HuPreflopDecompositionPlan &plan, const std::string &path);
[[nodiscard]] Result<HuPreflopDecompositionPlan, HuPreflopError>
load_hu_preflop_decomposition_plan(const std::string &path);
[[nodiscard]] Result<bool, HuPreflopError>
save_hu_preflop_flop_boundary(const HuPreflopFlopBoundary &boundary, const std::string &path);
[[nodiscard]] Result<HuPreflopFlopBoundary, HuPreflopError>
load_hu_preflop_flop_boundary(const std::string &path);
[[nodiscard]] Result<bool, HuPreflopError> save_hu_preflop_whole_game_coverage_accumulator(
    const HuPreflopWholeGameCoverageAccumulator &accumulator, const std::string &path);
[[nodiscard]] Result<HuPreflopWholeGameCoverageAccumulator, HuPreflopError>
load_hu_preflop_whole_game_coverage_accumulator(const std::string &path);
[[nodiscard]] Result<bool, HuPreflopError>
save_hu_preflop_river_scheduler_checkpoint(const HuPreflopRiverSchedulerCheckpoint &checkpoint,
                                           const std::string &path);
[[nodiscard]] Result<HuPreflopRiverSchedulerCheckpoint, HuPreflopError>
load_hu_preflop_river_scheduler_checkpoint(const std::string &path);
[[nodiscard]] Result<bool, HuPreflopError>
save_hu_preflop_river_task_accumulator(const HuPreflopRiverTaskAccumulator &accumulator,
                                       const std::string &path);
[[nodiscard]] Result<HuPreflopRiverTaskAccumulator, HuPreflopError>
load_hu_preflop_river_task_accumulator(const std::string &path);
[[nodiscard]] Result<bool, HuPreflopError>
save_hu_preflop_river_task_aggregate(const HuPreflopRiverTaskAggregate &aggregate,
                                     const std::string &path);
[[nodiscard]] Result<HuPreflopRiverTaskAggregate, HuPreflopError>
load_hu_preflop_river_task_aggregate(const std::string &path);
[[nodiscard]] Result<bool, HuPreflopError> save_hu_preflop_river_best_response_leaf_accumulator(
    const HuPreflopRiverBestResponseLeafAccumulator &accumulator, const std::string &path);
[[nodiscard]] Result<HuPreflopRiverBestResponseLeafAccumulator, HuPreflopError>
load_hu_preflop_river_best_response_leaf_accumulator(const std::string &path);
[[nodiscard]] Result<bool, HuPreflopError>
save_hu_preflop_best_response_task_evaluation(const HuPreflopBestResponseTaskEvaluation &evaluation,
                                              const std::string &path);
[[nodiscard]] Result<HuPreflopBestResponseTaskEvaluation, HuPreflopError>
load_hu_preflop_best_response_task_evaluation(const std::string &path);
[[nodiscard]] Result<bool, HuPreflopError> save_hu_preflop_best_response_entry_accumulator(
    const HuPreflopBestResponseEntryAccumulator &accumulator, const std::string &path);
[[nodiscard]] Result<HuPreflopBestResponseEntryAccumulator, HuPreflopError>
load_hu_preflop_best_response_entry_accumulator(const std::string &path);
[[nodiscard]] Result<bool, HuPreflopError> save_hu_preflop_best_response_entry_evaluation(
    const HuPreflopBestResponseEntryEvaluation &evaluation, const std::string &path);
[[nodiscard]] Result<HuPreflopBestResponseEntryEvaluation, HuPreflopError>
load_hu_preflop_best_response_entry_evaluation(const std::string &path);
[[nodiscard]] Result<HuPreflopWholeGameCertification, HuPreflopError>
certify_hu_preflop_whole_game(const HuPreflopTree &tree, const HuPreflopDecompositionPlan &plan,
                              const std::vector<HuPreflopFlopBoundary> &boundaries,
                              const HuPreflopGlobalBestResponseEvidence *global_best_response,
                              double target_normalized_nashconv);
[[nodiscard]] std::string fingerprint_hu_preflop_whole_game_coverage_accumulator(
    const HuPreflopWholeGameCoverageAccumulator &accumulator);
[[nodiscard]] std::string fingerprint_hu_preflop_continuation_profile(
    const HuPreflopWholeGameCoverageAccumulator &accumulator);
[[nodiscard]] Result<HuPreflopWholeGameCoverageAccumulator, HuPreflopError>
make_hu_preflop_whole_game_coverage_accumulator(const HuPreflopTree &tree,
                                                const HuPreflopDecompositionPlan &plan,
                                                double target_normalized_nashconv);
[[nodiscard]] Result<bool, HuPreflopError> accumulate_hu_preflop_whole_game_boundary(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &plan,
    HuPreflopWholeGameCoverageAccumulator &accumulator, const HuPreflopFlopBoundary &boundary);
// Streams only missing resolver sides. Each canonical Flop task is committed
// atomically after its candidate checkpoint has been accepted by the sink.
[[nodiscard]] Result<std::uint64_t, HuPreflopError> stream_hu_preflop_whole_game_boundaries(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &plan,
    HuPreflopWholeGameCoverageAccumulator &accumulator,
    const HuPreflopWholeGameBoundaryProvider &provider,
    const HuPreflopWholeGameCheckpointSink &checkpoint_sink = {});
[[nodiscard]] Result<bool, HuPreflopError> validate_hu_preflop_whole_game_coverage_accumulator(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &plan,
    const HuPreflopWholeGameCoverageAccumulator &accumulator);
[[nodiscard]] Result<HuPreflopWholeGameCertification, HuPreflopError>
finalize_hu_preflop_whole_game_certification(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &plan,
    const HuPreflopWholeGameCoverageAccumulator &accumulator,
    const HuPreflopGlobalBestResponseEvidence *global_best_response);
[[nodiscard]] Result<HuPreflopSolveResult, HuPreflopError>
solve_hu_preflop_sampled(const HuPreflopTree &tree, const HuPreflopSolveOptions &options = {});
[[nodiscard]] const char *hu_preflop_error_name(HuPreflopError error) noexcept;

} // namespace gtosd
