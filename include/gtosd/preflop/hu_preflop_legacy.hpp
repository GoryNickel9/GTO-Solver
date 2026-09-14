#pragma once

#include "gtosd/postflop/postflop_solver.hpp"
#include "gtosd/postflop/root_values.hpp"
#include "gtosd/preflop/hu_preflop.hpp"

#include <array>
#include <memory>
#include <span>
#include <vector>

namespace gtosd {

// Immutable policy handle for long-running profile/BR sweeps. Construction
// performs the full payload and fingerprint validation once. Subsequent
// evaluators check the bound identities without rescanning every policy row.
struct HuPreflopValidatedSampledPostflopPolicy {
  std::shared_ptr<const HuPreflopSampledPostflopPolicy> policy;
  std::string tree_fingerprint;
  std::string policy_fingerprint;
  std::uint64_t iterations{0U};
  HuPreflopSampledPolicyLookupMode lookup_mode{
      HuPreflopSampledPolicyLookupMode::AllowConfiguredFallback};
};

// Counts trained policy rows independently from physical public-root work.
// A small row/context ratio only bounds policy-lookup reuse; it does not imply
// an equivalent reduction of card-removal or terminal-payoff work.
struct HuPreflopSampledPolicyReuseCensus {
  std::string policy_fingerprint;
  std::uint64_t trained_information_sets{0U};
  std::uint64_t trained_decision_contexts{0U};
  std::array<std::uint64_t, 3> trained_information_sets_by_street{};
  std::array<std::array<std::uint64_t, 2>, 3> trained_information_sets_by_street_player{};
  std::array<std::uint64_t, 3> trained_decision_contexts_by_street{};
  std::array<std::array<std::uint64_t, 2>, 3> trained_decision_contexts_by_street_player{};
  std::array<std::uint64_t, 3> distinct_public_histories_by_street{};
  std::uint64_t minimum_information_sets_per_context{0U};
  std::uint64_t maximum_information_sets_per_context{0U};
  double mean_information_sets_per_context{0.0};
};

struct HuPreflopRiverProfileBestResponseEvaluation {
  PostflopRootCounterfactualValues profile;
  PostflopRootCounterfactualValues best_response;
};

// Exact root results produced in input order while sharing all immutable work
// for one physical River board. Roots must belong to the same postflop entry
// and board, but may use different River betting histories.
struct HuPreflopRiverBoardBatchEvaluation {
  std::uint32_t entry_node{0U};
  std::array<CardId, 5> board{};
  std::vector<HuPreflopRiverProfileBestResponseEvaluation> roots;
};

[[nodiscard]] Result<HuPreflopValidatedSampledPostflopPolicy, HuPreflopError>
make_hu_preflop_validated_sampled_postflop_policy(
    const HuPreflopTree &tree, HuPreflopSampledPostflopPolicy policy,
    HuPreflopSampledPolicyLookupMode lookup_mode =
        HuPreflopSampledPolicyLookupMode::AllowConfiguredFallback);

[[nodiscard]] Result<HuPreflopSampledPolicyReuseCensus, HuPreflopError>
analyze_hu_preflop_sampled_policy_reuse(
    const HuPreflopValidatedSampledPostflopPolicy &validated);

// Legacy decomposition adapter. The core preflop trainer does not include
// this header; callers that bridge preflop boundaries to the standalone
// postflop solver opt into these contracts explicitly.
// Validated bridge from one exported sampled-policy snapshot to the exact
// physical reach propagation used by R9-C at a River resolver root.
[[nodiscard]] Result<HuPreflopRiverConditionedReach, HuPreflopError>
derive_hu_preflop_river_conditioned_reach_from_sampled_policy(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopRiverResolverRoot &root, const HuPreflopSampledPostflopPolicy &policy);
[[nodiscard]] Result<HuPreflopRiverConditionedReach, HuPreflopError>
derive_hu_preflop_river_conditioned_reach_from_sampled_policy(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopRiverResolverRoot &root,
    const HuPreflopValidatedSampledPostflopPolicy &policy);

// Evaluates one exact Flop/Turn terminal contribution with the exported
// sampled policy, binding iteration and continuation identity to the snapshot.
[[nodiscard]] Result<HuPreflopUpperStreetTerminalContribution, HuPreflopError>
evaluate_hu_preflop_upper_street_terminal_contribution_from_sampled_policy(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    std::uint64_t task_span_index, const std::array<CardId, 3> &flop,
    std::uint64_t terminal_ordinal, const HuPostflopUpperStreetTerminalShape &terminal_shape,
    const HuPreflopSampledPostflopPolicy &policy);
[[nodiscard]] Result<HuPreflopUpperStreetTerminalContribution, HuPreflopError>
evaluate_hu_preflop_upper_street_terminal_contribution_from_sampled_policy(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    std::uint64_t task_span_index, const std::array<CardId, 3> &flop,
    std::uint64_t terminal_ordinal, const HuPostflopUpperStreetTerminalShape &terminal_shape,
    const HuPreflopValidatedSampledPostflopPolicy &policy);

// Supplies the fixed opponent policy for one exact upper-street best-response
// terminal while leaving the responding player's action choice unrestricted.
[[nodiscard]] Result<HuPreflopUpperStreetBestResponseTerminalContribution, HuPreflopError>
evaluate_hu_preflop_upper_street_best_response_terminal_contribution_from_sampled_policy(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopRiverRootCatalog &catalog, std::uint64_t task_span_index,
    const HuPostflopUpperStreetTerminalShape &terminal_shape,
    const HuPreflopRiverTurnGroup *turn_group, const HuPreflopSampledPostflopPolicy &policy,
    std::uint8_t responding_player);
[[nodiscard]] Result<HuPreflopUpperStreetBestResponseTerminalContribution, HuPreflopError>
evaluate_hu_preflop_upper_street_best_response_terminal_contribution_from_sampled_policy(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopRiverRootCatalog &catalog, std::uint64_t task_span_index,
    const HuPostflopUpperStreetTerminalShape &terminal_shape,
    const HuPreflopRiverTurnGroup *turn_group,
    const HuPreflopValidatedSampledPostflopPolicy &policy, std::uint8_t responding_player);

[[nodiscard]] Result<PostflopRootCounterfactualValues, HuPreflopError>
evaluate_hu_preflop_sampled_policy_river_root(const HuPreflopTree &tree,
                                              const HuPreflopDecompositionPlan &decomposition,
                                              const HuPreflopRiverResolverRoot &root,
                                              const HuPreflopSampledPostflopPolicy &policy,
                                              PostflopRootValueMode mode);
[[nodiscard]] Result<PostflopRootCounterfactualValues, HuPreflopError>
evaluate_hu_preflop_sampled_policy_river_root(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopRiverResolverRoot &root,
    const HuPreflopValidatedSampledPostflopPolicy &policy, PostflopRootValueMode mode);

// Evaluates every live responder combo in one matrix traversal. This removes
// the scalar evaluator's repeated opponent-policy lookup for each target hand.
// The scalar path remains available as a differential oracle.
[[nodiscard]] Result<PostflopRootCounterfactualValues, HuPreflopError>
evaluate_hu_preflop_sampled_policy_river_root_vectorized(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopRiverResolverRoot &root,
    const HuPreflopValidatedSampledPostflopPolicy &policy, PostflopRootValueMode mode);

// Reuses conditioned reach, physical hand values and private abstraction keys
// across profile and BR evaluation of the same public River root.
[[nodiscard]] Result<HuPreflopRiverProfileBestResponseEvaluation, HuPreflopError>
evaluate_hu_preflop_sampled_policy_river_root_vectorized_pair(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopRiverResolverRoot &root,
    const HuPreflopValidatedSampledPostflopPolicy &policy);

// Cross-root board batch. It prepares live combos, exact showdown values and
// private abstraction keys once, then evaluates every supplied public-history
// root against the same frozen policy. This is the measurable primitive used
// by the whole-game feasibility gate; upper-street BR reduction remains a
// separate step and must aggregate information sets before maximizing.
[[nodiscard]] Result<HuPreflopRiverBoardBatchEvaluation, HuPreflopError>
evaluate_hu_preflop_sampled_policy_river_board_batch_vectorized_pair(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    std::span<const HuPreflopRiverResolverRoot> roots,
    const HuPreflopValidatedSampledPostflopPolicy &policy);

[[nodiscard]] Result<PostflopTreeConfig, HuPreflopError>
make_hu_preflop_postflop_config(const HuPreflopTree &tree, std::uint32_t entry_node,
                                const std::array<CardId, 3> &flop);

[[nodiscard]] Result<PostflopTreeConfig, HuPreflopError>
make_hu_preflop_river_postflop_config(const HuPreflopTree &tree,
                                      const HuPreflopRiverResolverRoot &root);

[[nodiscard]] Result<HuPreflopRiverRootBoundary, HuPreflopError>
build_hu_preflop_river_root_boundary(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopRiverResolverRoot &root, const PostflopRootCounterfactualValues &postflop_values,
    const HuPreflopComboReach &exact_resolving_sequence_reach,
    const HuPreflopComboReach &opponent_postflop_action_sequence_reach,
    std::string continuation_fingerprint);

} // namespace gtosd
