#include "gtosd/preflop/hu_preflop_legacy.hpp"

#include <bit>
#include <memory>
#include <span>

namespace gtosd {
namespace {

const HuPreflopSampledPostflopPolicy *
validated_policy(const HuPreflopTree &tree,
                 const HuPreflopValidatedSampledPostflopPolicy &validated) {
  if (!validated.policy || validated.tree_fingerprint != tree.fingerprint ||
      validated.policy_fingerprint.empty() ||
      validated.policy_fingerprint != validated.policy->fingerprint ||
      validated.tree_fingerprint != validated.policy->tree_fingerprint ||
      validated.iterations == 0U || validated.iterations != validated.policy->iterations) {
    return nullptr;
  }
  return validated.policy.get();
}

Result<HuPreflopRiverConditionedReach, HuPreflopError>
derive_conditioned_reach(const HuPreflopTree &tree,
                         const HuPreflopDecompositionPlan &decomposition,
                         const HuPreflopRiverResolverRoot &root,
                         const HuPreflopSampledPostflopPolicy &policy,
                         const HuPreflopSampledPolicyLookupMode lookup_mode) {
  const std::array board{root.flop[0], root.flop[1], root.flop[2], root.turn, root.river};
  const HuPostflopActionProbabilityProvider provider =
      [&](const PublicState &state, const std::span<const Action> action_prefix,
          const Action &selected_action, const ComboId combo) {
        return query_hu_preflop_sampled_postflop_action_probability(
            tree, policy, root.entry_node, board, state, action_prefix, selected_action, combo,
            lookup_mode);
      };
  return derive_hu_preflop_river_conditioned_reach(tree, decomposition, root, provider);
}

} // namespace

Result<HuPreflopValidatedSampledPostflopPolicy, HuPreflopError>
make_hu_preflop_validated_sampled_postflop_policy(const HuPreflopTree &tree,
                                                   HuPreflopSampledPostflopPolicy policy,
                                                   const HuPreflopSampledPolicyLookupMode
                                                       lookup_mode) {
  if (static_cast<std::uint8_t>(lookup_mode) >
      static_cast<std::uint8_t>(HuPreflopSampledPolicyLookupMode::RequireTrainedInfoset)) {
    return Result<HuPreflopValidatedSampledPostflopPolicy, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  const auto valid = validate_hu_preflop_sampled_postflop_policy(tree, policy);
  if (!valid) {
    return Result<HuPreflopValidatedSampledPostflopPolicy, HuPreflopError>::failure(valid.error());
  }
  HuPreflopValidatedSampledPostflopPolicy result;
  result.tree_fingerprint = policy.tree_fingerprint;
  result.policy_fingerprint = policy.fingerprint;
  result.iterations = policy.iterations;
  result.lookup_mode = lookup_mode;
  try {
    result.policy =
        std::make_shared<const HuPreflopSampledPostflopPolicy>(std::move(policy));
  } catch (const std::bad_alloc &) {
    return Result<HuPreflopValidatedSampledPostflopPolicy, HuPreflopError>::failure(
        HuPreflopError::MemoryFailure);
  }
  return Result<HuPreflopValidatedSampledPostflopPolicy, HuPreflopError>::success(
      std::move(result));
}

Result<HuPreflopRiverConditionedReach, HuPreflopError>
derive_hu_preflop_river_conditioned_reach_from_sampled_policy(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopRiverResolverRoot &root, const HuPreflopSampledPostflopPolicy &policy) {
  const auto valid = validate_hu_preflop_sampled_postflop_policy(tree, policy);
  if (!valid) {
    return Result<HuPreflopRiverConditionedReach, HuPreflopError>::failure(valid.error());
  }
  return derive_conditioned_reach(tree, decomposition, root, policy,
                                  HuPreflopSampledPolicyLookupMode::AllowConfiguredFallback);
}

Result<HuPreflopRiverConditionedReach, HuPreflopError>
derive_hu_preflop_river_conditioned_reach_from_sampled_policy(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopRiverResolverRoot &root,
    const HuPreflopValidatedSampledPostflopPolicy &validated) {
  const auto *policy = validated_policy(tree, validated);
  return policy != nullptr
             ? derive_conditioned_reach(tree, decomposition, root, *policy, validated.lookup_mode)
             : Result<HuPreflopRiverConditionedReach, HuPreflopError>::failure(
                   HuPreflopError::IntegrityFailure);
}

Result<HuPreflopUpperStreetTerminalContribution, HuPreflopError>
evaluate_hu_preflop_upper_street_terminal_contribution_from_sampled_policy(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const std::uint64_t task_span_index, const std::array<CardId, 3> &flop,
    const std::uint64_t terminal_ordinal, const HuPostflopUpperStreetTerminalShape &terminal_shape,
    const HuPreflopSampledPostflopPolicy &policy) {
  const auto valid = validate_hu_preflop_sampled_postflop_policy(tree, policy);
  if (!valid || (terminal_shape.street != Street::Flop && terminal_shape.street != Street::Turn)) {
    return Result<HuPreflopUpperStreetTerminalContribution, HuPreflopError>::failure(
        valid ? HuPreflopError::InvalidConfiguration : valid.error());
  }
  const auto flop_cards_mask = flop[0].mask() | flop[1].mask() | flop[2].mask();
  CardId turn = flop[0];
  if (terminal_shape.street == Street::Turn) {
    const auto turn_mask = terminal_shape.state.board_mask & ~flop_cards_mask;
    if (std::popcount(turn_mask) != 1) {
      return Result<HuPreflopUpperStreetTerminalContribution, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    const auto parsed_turn =
        CardId::from_index(static_cast<std::uint8_t>(std::countr_zero(turn_mask)));
    if (!parsed_turn) {
      return Result<HuPreflopUpperStreetTerminalContribution, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    turn = parsed_turn.value();
  }
  const std::array board{flop[0], flop[1], flop[2], turn, flop[0]};
  const HuPostflopActionProbabilityProvider provider =
      [&](const PublicState &state, const std::span<const Action> action_prefix,
          const Action &selected_action, const ComboId combo) {
        return query_hu_preflop_sampled_postflop_action_probability(
            tree, policy, terminal_shape.entry_node, board, state, action_prefix, selected_action,
            combo);
      };
  return evaluate_hu_preflop_upper_street_terminal_contribution(
      tree, decomposition, task_span_index, flop, terminal_ordinal, terminal_shape, provider,
      policy.iterations, policy.fingerprint);
}

Result<HuPreflopUpperStreetTerminalContribution, HuPreflopError>
evaluate_hu_preflop_upper_street_terminal_contribution_from_sampled_policy(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const std::uint64_t task_span_index, const std::array<CardId, 3> &flop,
    const std::uint64_t terminal_ordinal, const HuPostflopUpperStreetTerminalShape &terminal_shape,
    const HuPreflopValidatedSampledPostflopPolicy &validated) {
  const auto *policy = validated_policy(tree, validated);
  if (policy == nullptr ||
      (terminal_shape.street != Street::Flop && terminal_shape.street != Street::Turn)) {
    return Result<HuPreflopUpperStreetTerminalContribution, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  const auto flop_cards_mask = flop[0].mask() | flop[1].mask() | flop[2].mask();
  CardId turn = flop[0];
  if (terminal_shape.street == Street::Turn) {
    const auto turn_mask = terminal_shape.state.board_mask & ~flop_cards_mask;
    if (std::popcount(turn_mask) != 1) {
      return Result<HuPreflopUpperStreetTerminalContribution, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    const auto parsed_turn =
        CardId::from_index(static_cast<std::uint8_t>(std::countr_zero(turn_mask)));
    if (!parsed_turn) {
      return Result<HuPreflopUpperStreetTerminalContribution, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    turn = parsed_turn.value();
  }
  const std::array board{flop[0], flop[1], flop[2], turn, flop[0]};
  const HuPostflopActionProbabilityProvider provider =
      [&](const PublicState &state, const std::span<const Action> action_prefix,
          const Action &selected_action, const ComboId combo) {
        return query_hu_preflop_sampled_postflop_action_probability(
            tree, *policy, terminal_shape.entry_node, board, state, action_prefix, selected_action,
            combo, validated.lookup_mode);
      };
  return evaluate_hu_preflop_upper_street_terminal_contribution(
      tree, decomposition, task_span_index, flop, terminal_ordinal, terminal_shape, provider,
      validated.iterations, validated.policy_fingerprint);
}

Result<HuPreflopUpperStreetBestResponseTerminalContribution, HuPreflopError>
evaluate_hu_preflop_upper_street_best_response_terminal_contribution_from_sampled_policy(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopRiverRootCatalog &catalog, const std::uint64_t task_span_index,
    const HuPostflopUpperStreetTerminalShape &terminal_shape,
    const HuPreflopRiverTurnGroup *turn_group, const HuPreflopSampledPostflopPolicy &policy,
    const std::uint8_t responding_player) {
  const auto valid = validate_hu_preflop_sampled_postflop_policy(tree, policy);
  if (!valid || task_span_index >= catalog.task_spans.size() || responding_player > 1U ||
      (terminal_shape.street != Street::Flop && terminal_shape.street != Street::Turn)) {
    return Result<HuPreflopUpperStreetBestResponseTerminalContribution, HuPreflopError>::failure(
        valid ? HuPreflopError::InvalidConfiguration : valid.error());
  }
  const auto &span = catalog.task_spans[task_span_index];
  if (span.first_board_index >= catalog.canonical_boards.size()) {
    return Result<HuPreflopUpperStreetBestResponseTerminalContribution, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  const auto flop = catalog.canonical_boards[span.first_board_index].flop;
  const auto turn = turn_group == nullptr ? flop[0] : turn_group->turn;
  const std::array board{flop[0], flop[1], flop[2], turn, flop[0]};
  const HuPostflopActionProbabilityProvider provider =
      [&](const PublicState &state, const std::span<const Action> action_prefix,
          const Action &selected_action, const ComboId combo) {
        return query_hu_preflop_sampled_postflop_action_probability(
            tree, policy, terminal_shape.entry_node, board, state, action_prefix, selected_action,
            combo);
      };
  return evaluate_hu_preflop_upper_street_best_response_terminal_contribution(
      tree, decomposition, catalog, task_span_index, terminal_shape, turn_group, provider,
      responding_player, policy.iterations, policy.fingerprint);
}

Result<HuPreflopUpperStreetBestResponseTerminalContribution, HuPreflopError>
evaluate_hu_preflop_upper_street_best_response_terminal_contribution_from_sampled_policy(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopRiverRootCatalog &catalog, const std::uint64_t task_span_index,
    const HuPostflopUpperStreetTerminalShape &terminal_shape,
    const HuPreflopRiverTurnGroup *turn_group,
    const HuPreflopValidatedSampledPostflopPolicy &validated,
    const std::uint8_t responding_player) {
  const auto *policy = validated_policy(tree, validated);
  if (policy == nullptr || task_span_index >= catalog.task_spans.size() ||
      responding_player > 1U ||
      (terminal_shape.street != Street::Flop && terminal_shape.street != Street::Turn)) {
    return Result<HuPreflopUpperStreetBestResponseTerminalContribution, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  const auto &span = catalog.task_spans[task_span_index];
  if (span.first_board_index >= catalog.canonical_boards.size()) {
    return Result<HuPreflopUpperStreetBestResponseTerminalContribution, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  const auto flop = catalog.canonical_boards[span.first_board_index].flop;
  const auto turn = turn_group == nullptr ? flop[0] : turn_group->turn;
  const std::array board{flop[0], flop[1], flop[2], turn, flop[0]};
  const HuPostflopActionProbabilityProvider provider =
      [&](const PublicState &state, const std::span<const Action> action_prefix,
          const Action &selected_action, const ComboId combo) {
        return query_hu_preflop_sampled_postflop_action_probability(
            tree, *policy, terminal_shape.entry_node, board, state, action_prefix, selected_action,
            combo, validated.lookup_mode);
      };
  return evaluate_hu_preflop_upper_street_best_response_terminal_contribution(
      tree, decomposition, catalog, task_span_index, terminal_shape, turn_group, provider,
      responding_player, validated.iterations, validated.policy_fingerprint);
}

} // namespace gtosd
