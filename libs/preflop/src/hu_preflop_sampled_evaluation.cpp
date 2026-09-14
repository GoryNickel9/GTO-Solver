#include "gtosd/preflop/hu_preflop_legacy.hpp"

#include "gtosd/equity/evaluator.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <numeric>
#include <vector>

namespace gtosd {
namespace {

void compensated_add(const double value, double &sum, double &compensation) {
  const auto corrected = value - compensation;
  const auto next = sum + corrected;
  compensation = (next - sum) - corrected;
  sum = next;
}

Result<std::array<double, 2>, HuPreflopError>
terminal_payoff_antes(const PublicState &state, const RakeConfig &rake,
                      const std::uint8_t winner_mask) {
  const auto settlement = settle_terminal(state, rake, winner_mask);
  if (!settlement) {
    return Result<std::array<double, 2>, HuPreflopError>::failure(HuPreflopError::GameFailure);
  }
  return Result<std::array<double, 2>, HuPreflopError>::success(
      {static_cast<double>(settlement.value().payoff_units[0]) /
           static_cast<double>(Money::units_per_ante),
       static_cast<double>(settlement.value().payoff_units[1]) /
           static_cast<double>(Money::units_per_ante)});
}

class SampledRiverPolicyEvaluator {
public:
  SampledRiverPolicyEvaluator(const HuPreflopTree &tree, const HuPreflopRiverResolverRoot &root,
                              const HuPreflopSampledPostflopPolicy &policy,
                              const HuPreflopRiverConditionedReach &conditioned,
                              const HuPreflopSampledPolicyLookupMode lookup_mode)
      : tree_(tree), root_(root), policy_(policy), conditioned_(conditioned), combos_(all_combos()),
        board_{root.flop[0], root.flop[1], root.flop[2], root.turn, root.river},
        lookup_mode_(lookup_mode) {
    action_config_.aggressive_sizes.assign(tree.config.postflop_sizes.begin(),
                                           tree.config.postflop_sizes.end());
    action_config_.raise_depth = maximum_core_raise_depth;
    action_config_.minimum_bet = tree.config.postflop_minimum_bet;
    action_config_.all_in_mode = AllInMode::Add;
    action_config_.all_in_threshold = PotPercentage::from_basis_points(100'000).value();
  }

  [[nodiscard]] Result<bool, HuPreflopError> initialize();
  [[nodiscard]] Result<PostflopRootCounterfactualValues, HuPreflopError>
  evaluate(PostflopRootValueMode mode);

private:
  [[nodiscard]] Result<double, HuPreflopError>
  evaluate_state(std::uint8_t player, ComboId target_combo, PostflopRootValueMode mode,
                 const PublicState &state, const std::vector<double> &opponent_reach,
                 std::vector<Action> &action_prefix) const;
  [[nodiscard]] Result<double, HuPreflopError>
  terminal_value(std::uint8_t player, ComboId target_combo, const PublicState &state,
                 const std::vector<double> &opponent_reach) const;

  const HuPreflopTree &tree_;
  const HuPreflopRiverResolverRoot &root_;
  const HuPreflopSampledPostflopPolicy &policy_;
  const HuPreflopRiverConditionedReach &conditioned_;
  std::array<Combo, 630U> combos_{};
  std::array<std::uint64_t, 630U> masks_{};
  std::array<HandValue, 630U> hand_values_{};
  std::array<bool, 630U> live_{};
  std::array<CardId, 5> board_{};
  ActionConfig action_config_{};
  std::size_t live_combo_count_{0U};
  HuPreflopSampledPolicyLookupMode lookup_mode_{
      HuPreflopSampledPolicyLookupMode::AllowConfiguredFallback};
};

struct PreparedSampledRiverBoard {
  std::uint32_t entry_node{0U};
  std::array<Combo, 630U> combos{};
  std::array<std::uint64_t, 630U> masks{};
  std::array<HandValue, 630U> hand_values{};
  std::vector<ComboId> live_combos;
  std::array<std::vector<HuPreflopSampledPostflopPolicyKey>, 2> private_policy_keys;
  std::array<CardId, 5> board{};
  std::uint64_t board_mask{0U};
};

class VectorizedSampledRiverPolicyEvaluator {
public:
  VectorizedSampledRiverPolicyEvaluator(
      const HuPreflopTree &tree, const HuPreflopRiverResolverRoot &root,
      const HuPreflopSampledPostflopPolicy &policy,
      const HuPreflopRiverConditionedReach &conditioned,
      const PreparedSampledRiverBoard &prepared,
      const HuPreflopSampledPolicyLookupMode lookup_mode)
      : tree_(tree), root_(root), policy_(policy), conditioned_(conditioned), prepared_(prepared),
        lookup_mode_(lookup_mode) {
    action_config_.aggressive_sizes.assign(tree.config.postflop_sizes.begin(),
                                           tree.config.postflop_sizes.end());
    action_config_.raise_depth = maximum_core_raise_depth;
    action_config_.minimum_bet = tree.config.postflop_minimum_bet;
    action_config_.all_in_mode = AllInMode::Add;
    action_config_.all_in_threshold = PotPercentage::from_basis_points(100'000).value();
  }

  [[nodiscard]] Result<bool, HuPreflopError> initialize();
  [[nodiscard]] Result<PostflopRootCounterfactualValues, HuPreflopError>
  evaluate(PostflopRootValueMode mode);

private:
  [[nodiscard]] Result<std::vector<double>, HuPreflopError>
  evaluate_state(std::uint8_t player, PostflopRootValueMode mode, const PublicState &state,
                 const std::vector<double> &opponent_reach,
                 std::vector<Action> &action_prefix) const;
  [[nodiscard]] Result<std::vector<double>, HuPreflopError>
  terminal_value(std::uint8_t player, const PublicState &state,
                 const std::vector<double> &opponent_reach) const;

  const HuPreflopTree &tree_;
  const HuPreflopRiverResolverRoot &root_;
  const HuPreflopSampledPostflopPolicy &policy_;
  const HuPreflopRiverConditionedReach &conditioned_;
  const PreparedSampledRiverBoard &prepared_;
  ActionConfig action_config_{};
  HuPreflopSampledPolicyLookupMode lookup_mode_{
      HuPreflopSampledPolicyLookupMode::AllowConfiguredFallback};
};

Result<bool, HuPreflopError> SampledRiverPolicyEvaluator::initialize() {
  if (std::popcount(root_.state.board_mask) != 5) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
  }
  for (std::size_t combo = 0U; combo < combos_.size(); ++combo) {
    masks_[combo] = combos_[combo].first.mask() | combos_[combo].second.mask();
    if ((masks_[combo] & root_.state.board_mask) != 0U) {
      continue;
    }
    const std::array cards{combos_[combo].first,
                           combos_[combo].second,
                           board_[0],
                           board_[1],
                           board_[2],
                           board_[3],
                           board_[4]};
    const auto value = evaluate_seven(cards);
    if (!value) {
      return Result<bool, HuPreflopError>::failure(HuPreflopError::EquityFailure);
    }
    hand_values_[combo] = value.value();
    live_[combo] = true;
    ++live_combo_count_;
  }
  return live_combo_count_ == 465U
             ? Result<bool, HuPreflopError>::success(true)
             : Result<bool, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
}

Result<double, HuPreflopError>
SampledRiverPolicyEvaluator::terminal_value(const std::uint8_t player, const ComboId target_combo,
                                            const PublicState &state,
                                            const std::vector<double> &opponent_reach) const {
  if (state.status == HandStatus::Folded) {
    const auto payoff = terminal_payoff_antes(state, tree_.config.rake, 0U);
    if (!payoff) {
      return Result<double, HuPreflopError>::failure(payoff.error());
    }
    const auto total_reach = std::accumulate(opponent_reach.begin(), opponent_reach.end(), 0.0);
    return Result<double, HuPreflopError>::success(total_reach * payoff.value()[player]);
  }
  if (state.status != HandStatus::AllInRunout && state.status != HandStatus::Showdown) {
    return Result<double, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  const auto opponent = static_cast<std::uint8_t>(1U - player);
  const auto player_win =
      terminal_payoff_antes(state, tree_.config.rake, static_cast<std::uint8_t>(1U << player));
  const auto opponent_win =
      terminal_payoff_antes(state, tree_.config.rake, static_cast<std::uint8_t>(1U << opponent));
  const auto tie = terminal_payoff_antes(state, tree_.config.rake, 0b11U);
  if (!player_win || !opponent_win || !tie) {
    return Result<double, HuPreflopError>::failure(HuPreflopError::GameFailure);
  }
  double sum = 0.0;
  double compensation = 0.0;
  for (std::size_t opponent_combo = 0U; opponent_combo < combos_.size(); ++opponent_combo) {
    const auto reach = opponent_reach[opponent_combo];
    if (reach == 0.0) {
      continue;
    }
    const auto utility =
        hand_values_[target_combo] > hand_values_[opponent_combo]   ? player_win.value()[player]
        : hand_values_[opponent_combo] > hand_values_[target_combo] ? opponent_win.value()[player]
                                                                    : tie.value()[player];
    compensated_add(reach * utility, sum, compensation);
  }
  const auto value = sum + compensation;
  return std::isfinite(value)
             ? Result<double, HuPreflopError>::success(value)
             : Result<double, HuPreflopError>::failure(HuPreflopError::NumericalFailure);
}

Result<double, HuPreflopError> SampledRiverPolicyEvaluator::evaluate_state(
    const std::uint8_t player, const ComboId target_combo, const PostflopRootValueMode mode,
    const PublicState &state, const std::vector<double> &opponent_reach,
    std::vector<Action> &action_prefix) const {
  if (state.status == HandStatus::Folded || state.status == HandStatus::AllInRunout ||
      state.status == HandStatus::Showdown) {
    return terminal_value(player, target_combo, state, opponent_reach);
  }
  if (state.status == HandStatus::StreetComplete) {
    const auto advanced = advance_street(state);
    return advanced ? evaluate_state(player, target_combo, mode, advanced.value(), opponent_reach,
                                     action_prefix)
                    : Result<double, HuPreflopError>::failure(HuPreflopError::GameFailure);
  }
  if (state.status != HandStatus::InProgress || state.player_to_act > 1U) {
    return Result<double, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  const auto actions = legal_actions(state, action_config_);
  if (!actions || actions.value().empty() ||
      actions.value().size() > hu_preflop_sampled_postflop_maximum_actions) {
    return Result<double, HuPreflopError>::failure(HuPreflopError::GameFailure);
  }

  const auto actor = state.player_to_act;
  const auto opponent = static_cast<std::uint8_t>(1U - player);
  double node_value = actor == player && mode == PostflopRootValueMode::ExactBestResponse
                          ? -std::numeric_limits<double>::infinity()
                          : 0.0;
  for (const auto &action : actions.value()) {
    const auto next = apply_action(state, action, action_config_);
    if (!next) {
      return Result<double, HuPreflopError>::failure(HuPreflopError::GameFailure);
    }
    double actor_probability = 1.0;
    std::vector<double> child_reach;
    const std::vector<double> *reach = &opponent_reach;
    if (actor == player) {
      if (mode == PostflopRootValueMode::AverageStrategy) {
        const auto probability = query_hu_preflop_sampled_postflop_action_probability(
            tree_, policy_, root_.entry_node, board_, state, action_prefix, action, target_combo,
            lookup_mode_);
        if (!probability) {
          return Result<double, HuPreflopError>::failure(probability.error());
        }
        actor_probability = probability.value();
      }
    } else {
      if (actor != opponent) {
        return Result<double, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
      }
      child_reach = opponent_reach;
      for (std::size_t combo = 0U; combo < child_reach.size(); ++combo) {
        if (child_reach[combo] == 0.0) {
          continue;
        }
        const auto probability = query_hu_preflop_sampled_postflop_action_probability(
            tree_, policy_, root_.entry_node, board_, state, action_prefix, action,
            static_cast<ComboId>(combo), lookup_mode_);
        if (!probability) {
          return Result<double, HuPreflopError>::failure(probability.error());
        }
        child_reach[combo] *= probability.value();
      }
      reach = &child_reach;
    }
    action_prefix.push_back(action);
    const auto child =
        evaluate_state(player, target_combo, mode, next.value(), *reach, action_prefix);
    action_prefix.pop_back();
    if (!child) {
      return child;
    }
    if (actor == player && mode == PostflopRootValueMode::ExactBestResponse) {
      node_value = std::max(node_value, child.value());
    } else {
      node_value += actor_probability * child.value();
    }
  }
  return std::isfinite(node_value)
             ? Result<double, HuPreflopError>::success(node_value)
             : Result<double, HuPreflopError>::failure(HuPreflopError::NumericalFailure);
}

Result<PostflopRootCounterfactualValues, HuPreflopError>
SampledRiverPolicyEvaluator::evaluate(const PostflopRootValueMode mode) {
  PostflopRootCounterfactualValues report;
  report.game_fingerprint = policy_.fingerprint;
  report.blueprint_iterations = policy_.iterations;
  report.mode = mode;
  for (std::uint8_t player = 0U; player < 2U; ++player) {
    const auto opponent = static_cast<std::uint8_t>(1U - player);
    auto &rows = report.players[player];
    rows.reserve(live_combo_count_);
    double direct_sum = 0.0;
    double direct_compensation = 0.0;
    double row_sum = 0.0;
    double row_compensation = 0.0;
    for (std::size_t target_combo = 0U; target_combo < combos_.size(); ++target_combo) {
      if (!live_[target_combo]) {
        continue;
      }
      std::vector<double> opponent_reach(combos_.size(), 0.0);
      double counterfactual_reach = 0.0;
      for (std::size_t opponent_combo = 0U; opponent_combo < combos_.size(); ++opponent_combo) {
        const auto source_reach = conditioned_.exact_sequence_reach[opponent][opponent_combo];
        if (!std::isfinite(source_reach) || source_reach < 0.0 ||
            (!live_[opponent_combo] && source_reach != 0.0)) {
          return Result<PostflopRootCounterfactualValues, HuPreflopError>::failure(
              HuPreflopError::NumericalFailure);
        }
        if (live_[opponent_combo] && (masks_[target_combo] & masks_[opponent_combo]) == 0U) {
          opponent_reach[opponent_combo] = source_reach;
          counterfactual_reach += source_reach;
        }
      }
      if (!std::isfinite(counterfactual_reach) || counterfactual_reach < 0.0) {
        return Result<PostflopRootCounterfactualValues, HuPreflopError>::failure(
            HuPreflopError::NumericalFailure);
      }
      std::vector<Action> action_prefix = root_.action_history;
      const auto weighted_utility = evaluate_state(player, static_cast<ComboId>(target_combo), mode,
                                                   root_.state, opponent_reach, action_prefix);
      if (!weighted_utility) {
        return Result<PostflopRootCounterfactualValues, HuPreflopError>::failure(
            weighted_utility.error());
      }
      const auto positive = counterfactual_reach > 0.0;
      const auto conditional_value =
          positive ? weighted_utility.value() / counterfactual_reach : 0.0;
      const auto source_range_weight = conditioned_.exact_sequence_reach[player][target_combo];
      if (!std::isfinite(source_range_weight) || source_range_weight < 0.0 ||
          !std::isfinite(conditional_value)) {
        return Result<PostflopRootCounterfactualValues, HuPreflopError>::failure(
            HuPreflopError::NumericalFailure);
      }
      rows.push_back({static_cast<ComboId>(target_combo), source_range_weight, counterfactual_reach,
                      conditional_value, positive});
      compensated_add(source_range_weight * weighted_utility.value(), direct_sum,
                      direct_compensation);
      compensated_add(source_range_weight * counterfactual_reach * conditional_value, row_sum,
                      row_compensation);
    }
    if (rows.size() != live_combo_count_) {
      return Result<PostflopRootCounterfactualValues, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    const auto direct =
        (direct_sum + direct_compensation) / conditioned_.compatible_joint_reach_mass;
    const auto recomposed = (row_sum + row_compensation) / conditioned_.compatible_joint_reach_mass;
    if (!std::isfinite(direct) || !std::isfinite(recomposed)) {
      return Result<PostflopRootCounterfactualValues, HuPreflopError>::failure(
          HuPreflopError::NumericalFailure);
    }
    report.recomposed_value_antes[player] = recomposed;
    report.maximum_recomposition_error_antes =
        std::max(report.maximum_recomposition_error_antes, std::abs(direct - recomposed));
  }
  return report.maximum_recomposition_error_antes <= 1.0e-9
             ? Result<PostflopRootCounterfactualValues, HuPreflopError>::success(std::move(report))
             : Result<PostflopRootCounterfactualValues, HuPreflopError>::failure(
                   HuPreflopError::NumericalFailure);
}

Result<std::unique_ptr<PreparedSampledRiverBoard>, HuPreflopError>
prepare_sampled_river_board(const HuPreflopTree &tree,
                            const HuPreflopRiverResolverRoot &root,
                            const HuPreflopSampledPostflopPolicy &policy) {
  if (std::popcount(root.state.board_mask) != 5) {
    return Result<std::unique_ptr<PreparedSampledRiverBoard>, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  auto prepared = std::make_unique<PreparedSampledRiverBoard>();
  prepared->entry_node = root.entry_node;
  prepared->combos = all_combos();
  prepared->board = {root.flop[0], root.flop[1], root.flop[2], root.turn, root.river};
  prepared->board_mask = root.state.board_mask;
  prepared->live_combos.reserve(465U);
  for (std::size_t combo = 0U; combo < prepared->combos.size(); ++combo) {
    prepared->masks[combo] =
        prepared->combos[combo].first.mask() | prepared->combos[combo].second.mask();
    if ((prepared->masks[combo] & prepared->board_mask) != 0U) {
      continue;
    }
    const std::array cards{prepared->combos[combo].first,
                           prepared->combos[combo].second,
                           prepared->board[0],
                           prepared->board[1],
                           prepared->board[2],
                           prepared->board[3],
                           prepared->board[4]};
    const auto value = evaluate_seven(cards);
    if (!value) {
      return Result<std::unique_ptr<PreparedSampledRiverBoard>, HuPreflopError>::failure(
          HuPreflopError::EquityFailure);
    }
    prepared->hand_values[combo] = value.value();
    prepared->live_combos.push_back(static_cast<ComboId>(combo));
  }
  if (prepared->live_combos.size() != 465U) {
    return Result<std::unique_ptr<PreparedSampledRiverBoard>, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  const auto root_decision = derive_hu_preflop_sampled_postflop_public_decision(
      tree, root.entry_node, prepared->board, root.state, root.action_history);
  if (!root_decision || root_decision.value().street != Street::River) {
    return Result<std::unique_ptr<PreparedSampledRiverBoard>, HuPreflopError>::failure(
        root_decision ? HuPreflopError::IntegrityFailure : root_decision.error());
  }
  for (std::uint8_t player = 0U; player < 2U; ++player) {
    prepared->private_policy_keys[player].reserve(prepared->live_combos.size());
    auto player_decision = root_decision.value();
    player_decision.player = player;
    for (const auto combo : prepared->live_combos) {
      auto key = derive_hu_preflop_sampled_postflop_policy_key(
          policy, player_decision, prepared->board, combo);
      if (!key) {
        return Result<std::unique_ptr<PreparedSampledRiverBoard>, HuPreflopError>::failure(
            key.error());
      }
      key.value().public_history = 0U;
      prepared->private_policy_keys[player].push_back(std::move(key.value()));
    }
  }
  return Result<std::unique_ptr<PreparedSampledRiverBoard>, HuPreflopError>::success(
      std::move(prepared));
}

Result<bool, HuPreflopError> VectorizedSampledRiverPolicyEvaluator::initialize() {
  const std::array root_board{root_.flop[0], root_.flop[1], root_.flop[2], root_.turn,
                              root_.river};
  const bool keys_complete = std::ranges::all_of(
      prepared_.private_policy_keys,
      [&](const auto &keys) { return keys.size() == prepared_.live_combos.size(); });
  return root_.entry_node == prepared_.entry_node && root_board == prepared_.board &&
                 root_.state.board_mask == prepared_.board_mask &&
                 prepared_.live_combos.size() == 465U && keys_complete
             ? Result<bool, HuPreflopError>::success(true)
             : Result<bool, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
}

Result<std::vector<double>, HuPreflopError>
VectorizedSampledRiverPolicyEvaluator::terminal_value(
    const std::uint8_t player, const PublicState &state,
    const std::vector<double> &opponent_reach) const {
  const auto live_count = prepared_.live_combos.size();
  if (opponent_reach.size() != live_count * live_count) {
    return Result<std::vector<double>, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  std::vector<double> values(live_count, 0.0);
  if (state.status == HandStatus::Folded) {
    const auto payoff = terminal_payoff_antes(state, tree_.config.rake, 0U);
    if (!payoff) {
      return Result<std::vector<double>, HuPreflopError>::failure(payoff.error());
    }
    for (std::size_t target = 0U; target < live_count; ++target) {
      double reach_sum = 0.0;
      double reach_compensation = 0.0;
      const auto row_offset = target * live_count;
      for (std::size_t opponent = 0U; opponent < live_count; ++opponent) {
        compensated_add(opponent_reach[row_offset + opponent], reach_sum, reach_compensation);
      }
      values[target] = (reach_sum + reach_compensation) * payoff.value()[player];
    }
    return Result<std::vector<double>, HuPreflopError>::success(std::move(values));
  }
  if (state.status != HandStatus::AllInRunout && state.status != HandStatus::Showdown) {
    return Result<std::vector<double>, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  const auto opponent_player = static_cast<std::uint8_t>(1U - player);
  const auto player_win =
      terminal_payoff_antes(state, tree_.config.rake, static_cast<std::uint8_t>(1U << player));
  const auto opponent_win = terminal_payoff_antes(
      state, tree_.config.rake, static_cast<std::uint8_t>(1U << opponent_player));
  const auto tie = terminal_payoff_antes(state, tree_.config.rake, 0b11U);
  if (!player_win || !opponent_win || !tie) {
    return Result<std::vector<double>, HuPreflopError>::failure(HuPreflopError::GameFailure);
  }
  for (std::size_t target = 0U; target < live_count; ++target) {
    const auto target_combo = prepared_.live_combos[target];
    const auto row_offset = target * live_count;
    double value_sum = 0.0;
    double value_compensation = 0.0;
    for (std::size_t opponent = 0U; opponent < live_count; ++opponent) {
      const auto reach = opponent_reach[row_offset + opponent];
      if (reach == 0.0) {
        continue;
      }
      const auto opponent_combo = prepared_.live_combos[opponent];
      const auto utility =
          prepared_.hand_values[target_combo] > prepared_.hand_values[opponent_combo]
              ? player_win.value()[player]
          : prepared_.hand_values[opponent_combo] > prepared_.hand_values[target_combo]
              ? opponent_win.value()[player]
              : tie.value()[player];
      compensated_add(reach * utility, value_sum, value_compensation);
    }
    values[target] = value_sum + value_compensation;
  }
  return std::ranges::all_of(values, [](const auto value) { return std::isfinite(value); })
             ? Result<std::vector<double>, HuPreflopError>::success(std::move(values))
             : Result<std::vector<double>, HuPreflopError>::failure(
                   HuPreflopError::NumericalFailure);
}

Result<std::vector<double>, HuPreflopError>
VectorizedSampledRiverPolicyEvaluator::evaluate_state(
    const std::uint8_t player, const PostflopRootValueMode mode, const PublicState &state,
    const std::vector<double> &opponent_reach, std::vector<Action> &action_prefix) const {
  if (state.status == HandStatus::Folded || state.status == HandStatus::AllInRunout ||
      state.status == HandStatus::Showdown) {
    return terminal_value(player, state, opponent_reach);
  }
  if (state.status == HandStatus::StreetComplete) {
    const auto advanced = advance_street(state);
    return advanced ? evaluate_state(player, mode, advanced.value(), opponent_reach, action_prefix)
                    : Result<std::vector<double>, HuPreflopError>::failure(
                          HuPreflopError::GameFailure);
  }
  if (state.status != HandStatus::InProgress || state.player_to_act > 1U) {
    return Result<std::vector<double>, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  const auto actions = legal_actions(state, action_config_);
  if (!actions || actions.value().empty() ||
      actions.value().size() > hu_preflop_sampled_postflop_maximum_actions) {
    return Result<std::vector<double>, HuPreflopError>::failure(HuPreflopError::GameFailure);
  }

  const auto live_count = prepared_.live_combos.size();
  const auto actor = state.player_to_act;
  const auto opponent_player = static_cast<std::uint8_t>(1U - player);
  const bool responder_maximizes =
      actor == player && mode == PostflopRootValueMode::ExactBestResponse;
  const bool actor_uses_policy = actor == opponent_player ||
                                 (actor == player &&
                                  mode == PostflopRootValueMode::AverageStrategy);
  using Strategy =
      std::array<double, hu_preflop_sampled_postflop_maximum_actions>;
  std::vector<Strategy> actor_strategies;
  if (actor_uses_policy) {
    actor_strategies.resize(live_count);
    if (lookup_mode_ == HuPreflopSampledPolicyLookupMode::RequireTrainedInfoset) {
      for (std::size_t combo = 0U; combo < live_count; ++combo) {
        const auto strategy = query_hu_preflop_sampled_postflop_strategy(
            tree_, policy_, root_.entry_node, prepared_.board, state, action_prefix,
            prepared_.live_combos[combo], lookup_mode_);
        if (!strategy) {
          return Result<std::vector<double>, HuPreflopError>::failure(strategy.error());
        }
        actor_strategies[combo] = strategy.value();
      }
    } else {
      const auto decision = derive_hu_preflop_sampled_postflop_public_decision(
          tree_, root_.entry_node, prepared_.board, state, action_prefix);
      if (!decision || decision.value().player != actor ||
          decision.value().street != Street::River ||
          decision.value().action_count != actions.value().size() ||
          prepared_.private_policy_keys[actor].size() != live_count) {
        return Result<std::vector<double>, HuPreflopError>::failure(
            decision ? HuPreflopError::IntegrityFailure : decision.error());
      }
      for (std::size_t combo = 0U; combo < live_count; ++combo) {
        auto key = prepared_.private_policy_keys[actor][combo];
        key.public_history = decision.value().public_history;
        const auto strategy = query_hu_preflop_sampled_postflop_policy(
            policy_, key, decision.value().action_count);
        if (!strategy) {
          return Result<std::vector<double>, HuPreflopError>::failure(strategy.error());
        }
        actor_strategies[combo] = strategy.value();
      }
    }
  }
  std::vector<double> node_values(
      live_count, responder_maximizes ? -std::numeric_limits<double>::infinity() : 0.0);
  for (std::size_t action_index = 0U; action_index < actions.value().size(); ++action_index) {
    const auto &action = actions.value()[action_index];
    const auto next = apply_action(state, action, action_config_);
    if (!next) {
      return Result<std::vector<double>, HuPreflopError>::failure(HuPreflopError::GameFailure);
    }
    std::vector<double> actor_probabilities(live_count, 1.0);
    std::vector<double> child_reach;
    const std::vector<double> *reach = &opponent_reach;
    if (actor == player && mode == PostflopRootValueMode::AverageStrategy) {
      for (std::size_t target = 0U; target < live_count; ++target) {
        actor_probabilities[target] = actor_strategies[target][action_index];
      }
    } else if (actor == opponent_player) {
      child_reach = opponent_reach;
      for (std::size_t target = 0U; target < live_count; ++target) {
        const auto row_offset = target * live_count;
        for (std::size_t opponent = 0U; opponent < live_count; ++opponent) {
          child_reach[row_offset + opponent] *= actor_strategies[opponent][action_index];
        }
      }
      reach = &child_reach;
    } else if (actor != player) {
      return Result<std::vector<double>, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    action_prefix.push_back(action);
    auto child = evaluate_state(player, mode, next.value(), *reach, action_prefix);
    action_prefix.pop_back();
    if (!child) {
      return child;
    }
    for (std::size_t target = 0U; target < live_count; ++target) {
      if (responder_maximizes) {
        node_values[target] = std::max(node_values[target], child.value()[target]);
      } else {
        node_values[target] += actor_probabilities[target] * child.value()[target];
      }
    }
  }
  return std::ranges::all_of(node_values, [](const auto value) { return std::isfinite(value); })
             ? Result<std::vector<double>, HuPreflopError>::success(std::move(node_values))
             : Result<std::vector<double>, HuPreflopError>::failure(
                   HuPreflopError::NumericalFailure);
}

Result<PostflopRootCounterfactualValues, HuPreflopError>
VectorizedSampledRiverPolicyEvaluator::evaluate(const PostflopRootValueMode mode) {
  PostflopRootCounterfactualValues report;
  report.game_fingerprint = policy_.fingerprint;
  report.blueprint_iterations = policy_.iterations;
  report.mode = mode;
  const auto live_count = prepared_.live_combos.size();
  for (std::uint8_t player = 0U; player < 2U; ++player) {
    const auto opponent_player = static_cast<std::uint8_t>(1U - player);
    std::vector<double> opponent_reach(live_count * live_count, 0.0);
    for (std::size_t target = 0U; target < live_count; ++target) {
      const auto target_combo = prepared_.live_combos[target];
      const auto row_offset = target * live_count;
      for (std::size_t opponent = 0U; opponent < live_count; ++opponent) {
        const auto opponent_combo = prepared_.live_combos[opponent];
        if ((prepared_.masks[target_combo] & prepared_.masks[opponent_combo]) == 0U) {
          opponent_reach[row_offset + opponent] =
              conditioned_.exact_sequence_reach[opponent_player][opponent_combo];
        }
      }
    }
    std::vector<Action> action_prefix = root_.action_history;
    const auto weighted_utilities =
        evaluate_state(player, mode, root_.state, opponent_reach, action_prefix);
    if (!weighted_utilities) {
      return Result<PostflopRootCounterfactualValues, HuPreflopError>::failure(
          weighted_utilities.error());
    }
    auto &rows = report.players[player];
    rows.reserve(live_count);
    double recomposed_sum = 0.0;
    double recomposed_compensation = 0.0;
    for (std::size_t target = 0U; target < live_count; ++target) {
      const auto target_combo = prepared_.live_combos[target];
      const auto row_offset = target * live_count;
      double counterfactual_reach = 0.0;
      double reach_compensation = 0.0;
      for (std::size_t opponent = 0U; opponent < live_count; ++opponent) {
        compensated_add(opponent_reach[row_offset + opponent], counterfactual_reach,
                        reach_compensation);
      }
      counterfactual_reach += reach_compensation;
      const auto positive = counterfactual_reach > 0.0;
      const auto conditional_value =
          positive ? weighted_utilities.value()[target] / counterfactual_reach : 0.0;
      const auto source_range_weight = conditioned_.exact_sequence_reach[player][target_combo];
      if (!std::isfinite(source_range_weight) || source_range_weight < 0.0 ||
          !std::isfinite(counterfactual_reach) || counterfactual_reach < 0.0 ||
          !std::isfinite(conditional_value)) {
        return Result<PostflopRootCounterfactualValues, HuPreflopError>::failure(
            HuPreflopError::NumericalFailure);
      }
      rows.push_back({target_combo, source_range_weight, counterfactual_reach, conditional_value,
                      positive});
      compensated_add(source_range_weight * weighted_utilities.value()[target], recomposed_sum,
                      recomposed_compensation);
    }
    report.recomposed_value_antes[player] =
        (recomposed_sum + recomposed_compensation) / conditioned_.compatible_joint_reach_mass;
    if (!std::isfinite(report.recomposed_value_antes[player])) {
      return Result<PostflopRootCounterfactualValues, HuPreflopError>::failure(
          HuPreflopError::NumericalFailure);
    }
  }
  return Result<PostflopRootCounterfactualValues, HuPreflopError>::success(std::move(report));
}

} // namespace

Result<PostflopRootCounterfactualValues, HuPreflopError>
evaluate_hu_preflop_sampled_policy_river_root(const HuPreflopTree &tree,
                                              const HuPreflopDecompositionPlan &decomposition,
                                              const HuPreflopRiverResolverRoot &root,
                                              const HuPreflopSampledPostflopPolicy &policy,
                                              const PostflopRootValueMode mode) {
  if ((mode != PostflopRootValueMode::AverageStrategy &&
       mode != PostflopRootValueMode::ExactBestResponse) ||
      root.state.street != Street::River || root.state.status != HandStatus::InProgress) {
    return Result<PostflopRootCounterfactualValues, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  const auto conditioned = derive_hu_preflop_river_conditioned_reach_from_sampled_policy(
      tree, decomposition, root, policy);
  if (!conditioned || conditioned.value().compatible_joint_reach_mass <= 0.0) {
    return Result<PostflopRootCounterfactualValues, HuPreflopError>::failure(
        conditioned ? HuPreflopError::InvalidConfiguration : conditioned.error());
  }
  try {
    auto evaluator =
        std::make_unique<SampledRiverPolicyEvaluator>(
            tree, root, policy, conditioned.value(),
            HuPreflopSampledPolicyLookupMode::AllowConfiguredFallback);
    const auto initialized = evaluator->initialize();
    return initialized ? evaluator->evaluate(mode)
                       : Result<PostflopRootCounterfactualValues, HuPreflopError>::failure(
                             initialized.error());
  } catch (const std::bad_alloc &) {
    return Result<PostflopRootCounterfactualValues, HuPreflopError>::failure(
        HuPreflopError::MemoryFailure);
  }
}

Result<PostflopRootCounterfactualValues, HuPreflopError>
evaluate_hu_preflop_sampled_policy_river_root(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopRiverResolverRoot &root,
    const HuPreflopValidatedSampledPostflopPolicy &validated,
    const PostflopRootValueMode mode) {
  if (!validated.policy || validated.tree_fingerprint != tree.fingerprint ||
      validated.policy_fingerprint != validated.policy->fingerprint ||
      validated.tree_fingerprint != validated.policy->tree_fingerprint ||
      validated.iterations != validated.policy->iterations ||
      (mode != PostflopRootValueMode::AverageStrategy &&
       mode != PostflopRootValueMode::ExactBestResponse) ||
      root.state.street != Street::River || root.state.status != HandStatus::InProgress) {
    return Result<PostflopRootCounterfactualValues, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  const auto conditioned = derive_hu_preflop_river_conditioned_reach_from_sampled_policy(
      tree, decomposition, root, validated);
  if (!conditioned || conditioned.value().compatible_joint_reach_mass <= 0.0) {
    return Result<PostflopRootCounterfactualValues, HuPreflopError>::failure(
        conditioned ? HuPreflopError::InvalidConfiguration : conditioned.error());
  }
  try {
    auto evaluator = std::make_unique<SampledRiverPolicyEvaluator>(
        tree, root, *validated.policy, conditioned.value(), validated.lookup_mode);
    const auto initialized = evaluator->initialize();
    return initialized ? evaluator->evaluate(mode)
                       : Result<PostflopRootCounterfactualValues, HuPreflopError>::failure(
                             initialized.error());
  } catch (const std::bad_alloc &) {
    return Result<PostflopRootCounterfactualValues, HuPreflopError>::failure(
        HuPreflopError::MemoryFailure);
  }
}

Result<PostflopRootCounterfactualValues, HuPreflopError>
evaluate_hu_preflop_sampled_policy_river_root_vectorized(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopRiverResolverRoot &root,
    const HuPreflopValidatedSampledPostflopPolicy &validated,
    const PostflopRootValueMode mode) {
  if (!validated.policy || validated.tree_fingerprint != tree.fingerprint ||
      validated.policy_fingerprint != validated.policy->fingerprint ||
      validated.tree_fingerprint != validated.policy->tree_fingerprint ||
      validated.iterations != validated.policy->iterations ||
      (mode != PostflopRootValueMode::AverageStrategy &&
       mode != PostflopRootValueMode::ExactBestResponse) ||
      root.state.street != Street::River || root.state.status != HandStatus::InProgress) {
    return Result<PostflopRootCounterfactualValues, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  const auto conditioned = derive_hu_preflop_river_conditioned_reach_from_sampled_policy(
      tree, decomposition, root, validated);
  if (!conditioned || conditioned.value().compatible_joint_reach_mass <= 0.0) {
    return Result<PostflopRootCounterfactualValues, HuPreflopError>::failure(
        conditioned ? HuPreflopError::InvalidConfiguration : conditioned.error());
  }
  try {
    auto prepared = prepare_sampled_river_board(tree, root, *validated.policy);
    if (!prepared) {
      return Result<PostflopRootCounterfactualValues, HuPreflopError>::failure(
          prepared.error());
    }
    auto evaluator = std::make_unique<VectorizedSampledRiverPolicyEvaluator>(
        tree, root, *validated.policy, conditioned.value(), *prepared.value(),
        validated.lookup_mode);
    const auto initialized = evaluator->initialize();
    return initialized ? evaluator->evaluate(mode)
                       : Result<PostflopRootCounterfactualValues, HuPreflopError>::failure(
                             initialized.error());
  } catch (const std::bad_alloc &) {
    return Result<PostflopRootCounterfactualValues, HuPreflopError>::failure(
        HuPreflopError::MemoryFailure);
  }
}

Result<HuPreflopRiverProfileBestResponseEvaluation, HuPreflopError>
evaluate_hu_preflop_sampled_policy_river_root_vectorized_pair(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const HuPreflopRiverResolverRoot &root,
    const HuPreflopValidatedSampledPostflopPolicy &validated) {
  if (!validated.policy || validated.tree_fingerprint != tree.fingerprint ||
      validated.policy_fingerprint != validated.policy->fingerprint ||
      validated.tree_fingerprint != validated.policy->tree_fingerprint ||
      validated.iterations != validated.policy->iterations ||
      root.state.street != Street::River || root.state.status != HandStatus::InProgress) {
    return Result<HuPreflopRiverProfileBestResponseEvaluation, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  const auto conditioned = derive_hu_preflop_river_conditioned_reach_from_sampled_policy(
      tree, decomposition, root, validated);
  if (!conditioned || conditioned.value().compatible_joint_reach_mass <= 0.0) {
    return Result<HuPreflopRiverProfileBestResponseEvaluation, HuPreflopError>::failure(
        conditioned ? HuPreflopError::InvalidConfiguration : conditioned.error());
  }
  try {
    auto prepared = prepare_sampled_river_board(tree, root, *validated.policy);
    if (!prepared) {
      return Result<HuPreflopRiverProfileBestResponseEvaluation, HuPreflopError>::failure(
          prepared.error());
    }
    auto evaluator = std::make_unique<VectorizedSampledRiverPolicyEvaluator>(
        tree, root, *validated.policy, conditioned.value(), *prepared.value(),
        validated.lookup_mode);
    const auto initialized = evaluator->initialize();
    if (!initialized) {
      return Result<HuPreflopRiverProfileBestResponseEvaluation, HuPreflopError>::failure(
          initialized.error());
    }
    auto profile = evaluator->evaluate(PostflopRootValueMode::AverageStrategy);
    auto best_response = evaluator->evaluate(PostflopRootValueMode::ExactBestResponse);
    if (!profile || !best_response) {
      return Result<HuPreflopRiverProfileBestResponseEvaluation, HuPreflopError>::failure(
          profile ? best_response.error() : profile.error());
    }
    return Result<HuPreflopRiverProfileBestResponseEvaluation, HuPreflopError>::success(
        {std::move(profile.value()), std::move(best_response.value())});
  } catch (const std::bad_alloc &) {
    return Result<HuPreflopRiverProfileBestResponseEvaluation, HuPreflopError>::failure(
        HuPreflopError::MemoryFailure);
  }
}

Result<HuPreflopRiverBoardBatchEvaluation, HuPreflopError>
evaluate_hu_preflop_sampled_policy_river_board_batch_vectorized_pair(
    const HuPreflopTree &tree, const HuPreflopDecompositionPlan &decomposition,
    const std::span<const HuPreflopRiverResolverRoot> roots,
    const HuPreflopValidatedSampledPostflopPolicy &validated) {
  if (!validated.policy || validated.tree_fingerprint != tree.fingerprint ||
      validated.policy_fingerprint != validated.policy->fingerprint ||
      validated.tree_fingerprint != validated.policy->tree_fingerprint ||
      validated.iterations != validated.policy->iterations || roots.empty()) {
    return Result<HuPreflopRiverBoardBatchEvaluation, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  const auto &first = roots.front();
  const std::array board{first.flop[0], first.flop[1], first.flop[2], first.turn, first.river};
  if (first.state.street != Street::River || first.state.status != HandStatus::InProgress ||
      std::ranges::any_of(roots, [&](const auto &root) {
        const std::array candidate_board{root.flop[0], root.flop[1], root.flop[2], root.turn,
                                         root.river};
        return root.entry_node != first.entry_node || candidate_board != board ||
               root.state.board_mask != first.state.board_mask ||
               root.state.street != Street::River || root.state.status != HandStatus::InProgress;
      })) {
    return Result<HuPreflopRiverBoardBatchEvaluation, HuPreflopError>::failure(
        HuPreflopError::InvalidConfiguration);
  }
  try {
    auto prepared = prepare_sampled_river_board(tree, first, *validated.policy);
    if (!prepared) {
      return Result<HuPreflopRiverBoardBatchEvaluation, HuPreflopError>::failure(
          prepared.error());
    }
    HuPreflopRiverBoardBatchEvaluation result;
    result.entry_node = first.entry_node;
    result.board = board;
    result.roots.reserve(roots.size());
    for (const auto &root : roots) {
      const auto conditioned = derive_hu_preflop_river_conditioned_reach_from_sampled_policy(
          tree, decomposition, root, validated);
      if (!conditioned || conditioned.value().compatible_joint_reach_mass <= 0.0) {
        return Result<HuPreflopRiverBoardBatchEvaluation, HuPreflopError>::failure(
            conditioned ? HuPreflopError::InvalidConfiguration : conditioned.error());
      }
      auto evaluator = std::make_unique<VectorizedSampledRiverPolicyEvaluator>(
          tree, root, *validated.policy, conditioned.value(), *prepared.value(),
          validated.lookup_mode);
      const auto initialized = evaluator->initialize();
      if (!initialized) {
        return Result<HuPreflopRiverBoardBatchEvaluation, HuPreflopError>::failure(
            initialized.error());
      }
      auto profile = evaluator->evaluate(PostflopRootValueMode::AverageStrategy);
      auto best_response = evaluator->evaluate(PostflopRootValueMode::ExactBestResponse);
      if (!profile || !best_response) {
        return Result<HuPreflopRiverBoardBatchEvaluation, HuPreflopError>::failure(
            profile ? best_response.error() : profile.error());
      }
      result.roots.push_back(
          {std::move(profile.value()), std::move(best_response.value())});
    }
    return Result<HuPreflopRiverBoardBatchEvaluation, HuPreflopError>::success(
        std::move(result));
  } catch (const std::bad_alloc &) {
    return Result<HuPreflopRiverBoardBatchEvaluation, HuPreflopError>::failure(
        HuPreflopError::MemoryFailure);
  }
}

} // namespace gtosd
