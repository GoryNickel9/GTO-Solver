#include "gtosd/core/game.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <sstream>

namespace gtosd {
namespace {

constexpr std::uint8_t player_bit(const std::uint8_t player) {
  return static_cast<std::uint8_t>(std::uint8_t{1} << player);
}

bool is_active(const PublicState &state, const std::uint8_t player) {
  return (state.active_players_mask & player_bit(player)) != 0U;
}

bool is_all_in(const PublicState &state, const std::uint8_t player) {
  return (state.all_in_players_mask & player_bit(player)) != 0U;
}

Result<Money, GameError> checked_add(const Money lhs, const Money rhs) {
  const auto result = add_checked(lhs, rhs);
  if (!result) {
    return Result<Money, GameError>::failure(GameError::ArithmeticFailure);
  }
  return Result<Money, GameError>::success(result.value());
}

Result<Money, GameError> checked_subtract(const Money lhs, const Money rhs) {
  const auto result = subtract_checked(lhs, rhs);
  if (!result) {
    return Result<Money, GameError>::failure(GameError::ArithmeticFailure);
  }
  return Result<Money, GameError>::success(result.value());
}

Result<std::int64_t, GameError> sum_units(const std::array<Money, maximum_players> &values,
                                          const std::uint8_t player_count) {
  std::int64_t total = 0;
  for (std::uint8_t player = 0; player < player_count; ++player) {
    if (values[player].units() > std::numeric_limits<std::int64_t>::max() - total) {
      return Result<std::int64_t, GameError>::failure(GameError::ArithmeticFailure);
    }
    total += values[player].units();
  }
  return Result<std::int64_t, GameError>::success(total);
}

void add_unique_aggressive(std::vector<Action> &actions, const Action action) {
  const auto duplicate = std::ranges::find_if(actions, [&](const Action &existing) {
    const bool existing_aggressive = existing.type == ActionType::Bet ||
                                     existing.type == ActionType::Raise ||
                                     existing.type == ActionType::AllIn;
    return existing_aggressive && existing.amount == action.amount;
  });
  if (duplicate == actions.end()) {
    actions.push_back(action);
  }
}

std::uint8_t actionable_mask(const PublicState &state) {
  return static_cast<std::uint8_t>(state.active_players_mask &
                                   static_cast<std::uint8_t>(~state.all_in_players_mask));
}

bool commitments_equal(const PublicState &state) {
  bool initialized = false;
  Money reference{};
  for (std::uint8_t player = 0; player < state.player_count; ++player) {
    if (!is_active(state, player)) {
      continue;
    }
    if (!initialized) {
      reference = state.committed_this_street[player];
      initialized = true;
    } else if (state.committed_this_street[player] != reference) {
      return false;
    }
  }
  return true;
}

void close_betting_round(PublicState &state) {
  if (state.street == Street::River) {
    state.status = HandStatus::Showdown;
  } else if (state.all_in_players_mask != 0U) {
    state.status = HandStatus::AllInRunout;
  } else {
    state.status = HandStatus::StreetComplete;
  }
}

void close_if_complete(PublicState &state) {
  const auto can_act = actionable_mask(state);
  const auto all_actionable_acted = (state.acted_players_mask & can_act) == can_act;
  if (commitments_equal(state) && (can_act == 0U || all_actionable_acted)) {
    close_betting_round(state);
  }
}

Result<bool, GameError> return_uncalled(PublicState &state, const std::uint8_t recipient,
                                        const Money amount) {
  if (amount.units() == 0) {
    return Result<bool, GameError>::success(true);
  }
  const auto pot = checked_subtract(state.pot, amount);
  const auto street_commitment = checked_subtract(state.committed_this_street[recipient], amount);
  const auto stack = checked_add(state.remaining_stacks[recipient], amount);
  const auto returned = checked_add(state.returned_uncalled, amount);
  const auto returned_player = checked_add(state.returned_uncalled_by_player[recipient], amount);
  if (!pot || !street_commitment || !stack || !returned || !returned_player) {
    return Result<bool, GameError>::failure(GameError::ArithmeticFailure);
  }
  state.pot = pot.value();
  state.committed_this_street[recipient] = street_commitment.value();
  state.remaining_stacks[recipient] = stack.value();
  state.returned_uncalled = returned.value();
  state.returned_uncalled_by_player[recipient] = returned_player.value();
  if (state.remaining_stacks[recipient].units() > 0) {
    state.all_in_players_mask &= static_cast<std::uint8_t>(~player_bit(recipient));
  }
  return Result<bool, GameError>::success(true);
}

Result<bool, GameError> settle_unmatched_commitment(PublicState &state) {
  if (state.player_count != 2U) {
    return Result<bool, GameError>::failure(GameError::InvalidConfiguration);
  }
  const auto co = state.committed_this_street[0];
  const auto btn = state.committed_this_street[1];
  if (co == btn) {
    state.current_bet = co;
    return Result<bool, GameError>::success(true);
  }
  const auto recipient = static_cast<std::uint8_t>(co > btn ? 0U : 1U);
  const auto difference =
      Money::from_units(co > btn ? co.units() - btn.units() : btn.units() - co.units()).value();
  const auto returned = return_uncalled(state, recipient, difference);
  if (!returned) {
    return returned;
  }
  state.current_bet = std::min(state.committed_this_street[0], state.committed_this_street[1]);
  return Result<bool, GameError>::success(true);
}

std::uint8_t next_actionable_player(const PublicState &state, const std::uint8_t actor) {
  for (std::uint8_t offset = 1; offset <= state.player_count; ++offset) {
    const auto candidate = static_cast<std::uint8_t>((actor + offset) % state.player_count);
    if (is_active(state, candidate) && !is_all_in(state, candidate)) {
      return candidate;
    }
  }
  return actor;
}

} // namespace

Result<PublicState, GameError> make_hu_preflop_state(const Money effective_stack,
                                                     const Money ante) {
  if (ante.units() <= 0 || ante.units() > std::numeric_limits<std::int64_t>::max() / 3 ||
      effective_stack.units() < ante.units() * 2) {
    return Result<PublicState, GameError>::failure(GameError::InvalidStack);
  }
  PublicState state;
  state.street = Street::Preflop;
  state.player_to_act = static_cast<std::uint8_t>(Player::CO);
  state.initial_pot = Money::from_units(ante.units() * 2).value();
  state.initial_pot_contributions[0] = ante;
  state.initial_pot_contributions[1] = ante;
  state.pot = Money::from_units(ante.units() * 3).value();
  state.current_bet = ante;
  state.last_full_raise_increment = ante;
  state.committed_this_street[0] = Money{};
  state.committed_this_street[1] = ante;
  state.committed_total = state.committed_this_street;
  state.remaining_stacks[0] = Money::from_units(effective_stack.units() - ante.units()).value();
  state.remaining_stacks[1] = Money::from_units(effective_stack.units() - ante.units() * 2).value();
  if (state.remaining_stacks[1].units() == 0) {
    state.all_in_players_mask |= player_bit(1);
  }
  if (!validate_state(state)) {
    return Result<PublicState, GameError>::failure(GameError::InvalidState);
  }
  return Result<PublicState, GameError>::success(state);
}

Result<PublicState, GameError> make_hu_postflop_state(const Street street, const Money initial_pot,
                                                      const Money effective_stack,
                                                      const std::uint64_t board_mask) {
  if (street == Street::Preflop || initial_pot.units() <= 0) {
    return Result<PublicState, GameError>::failure(GameError::InvalidPot);
  }
  if (effective_stack.units() <= 0) {
    return Result<PublicState, GameError>::failure(GameError::InvalidStack);
  }
  PublicState state;
  state.street = street;
  state.board_mask = board_mask;
  state.initial_pot = initial_pot;
  state.pot = initial_pot;
  state.player_to_act = static_cast<std::uint8_t>(Player::CO);
  state.remaining_stacks[0] = effective_stack;
  state.remaining_stacks[1] = effective_stack;
  const auto co_share = Money::from_units((initial_pot.units() + 1) / 2).value();
  const auto btn_share = Money::from_units(initial_pot.units() / 2).value();
  state.initial_pot_contributions[0] = co_share;
  state.initial_pot_contributions[1] = btn_share;
  if (!validate_state(state)) {
    return Result<PublicState, GameError>::failure(GameError::InvalidState);
  }
  return Result<PublicState, GameError>::success(state);
}

Money amount_to_call(const PublicState &state, const std::uint8_t player) {
  if (player >= state.player_count || state.current_bet < state.committed_this_street[player]) {
    return Money{};
  }
  return Money::from_units(state.current_bet.units() - state.committed_this_street[player].units())
      .value();
}

Result<bool, GameError> validate_state(const PublicState &state) {
  if (state.player_count < 2U || state.player_count > maximum_players) {
    return Result<bool, GameError>::failure(GameError::InvalidState);
  }
  if ((state.board_mask >> 36U) != 0U || state.raise_count_this_street > maximum_core_raise_depth) {
    return Result<bool, GameError>::failure(GameError::InvalidState);
  }
  const auto valid_mask = static_cast<std::uint8_t>((std::uint16_t{1} << state.player_count) - 1U);
  if ((state.active_players_mask & static_cast<std::uint8_t>(~valid_mask)) != 0U ||
      (state.all_in_players_mask & static_cast<std::uint8_t>(~state.active_players_mask)) != 0U ||
      (state.acted_players_mask & static_cast<std::uint8_t>(~valid_mask)) != 0U ||
      (state.terminal_winner_mask & static_cast<std::uint8_t>(~valid_mask)) != 0U) {
    return Result<bool, GameError>::failure(GameError::InvalidState);
  }
  if (state.status == HandStatus::InProgress &&
      (state.player_to_act >= state.player_count || !is_active(state, state.player_to_act) ||
       is_all_in(state, state.player_to_act))) {
    return Result<bool, GameError>::failure(GameError::InvalidState);
  }
  for (std::uint8_t player = 0; player < state.player_count; ++player) {
    if (is_all_in(state, player) && state.remaining_stacks[player].units() != 0) {
      return Result<bool, GameError>::failure(GameError::InvalidState);
    }
    if (is_active(state, player) && state.remaining_stacks[player].units() == 0 &&
        !is_all_in(state, player)) {
      return Result<bool, GameError>::failure(GameError::InvalidState);
    }
    if (state.committed_this_street[player] > state.committed_total[player]) {
      return Result<bool, GameError>::failure(GameError::InvalidState);
    }
    if (state.returned_uncalled_by_player[player] > state.committed_total[player]) {
      return Result<bool, GameError>::failure(GameError::InvalidState);
    }
  }
  const auto commitments = sum_units(state.committed_total, state.player_count);
  const auto returned = sum_units(state.returned_uncalled_by_player, state.player_count);
  const auto initial_contributions = sum_units(state.initial_pot_contributions, state.player_count);
  if (!commitments || !returned || !initial_contributions ||
      returned.value() != state.returned_uncalled.units() ||
      initial_contributions.value() != state.initial_pot.units()) {
    return Result<bool, GameError>::failure(GameError::InvalidState);
  }
  if (state.initial_pot.units() > std::numeric_limits<std::int64_t>::max() - commitments.value()) {
    return Result<bool, GameError>::failure(GameError::ArithmeticFailure);
  }
  if (state.pot.units() >
      std::numeric_limits<std::int64_t>::max() - state.returned_uncalled.units()) {
    return Result<bool, GameError>::failure(GameError::ArithmeticFailure);
  }
  if (state.initial_pot.units() + commitments.value() !=
      state.pot.units() + state.returned_uncalled.units()) {
    return Result<bool, GameError>::failure(GameError::InvalidState);
  }
  if (state.status == HandStatus::Folded && std::popcount(state.active_players_mask) != 1) {
    return Result<bool, GameError>::failure(GameError::InvalidState);
  }
  if (state.status == HandStatus::Folded &&
      state.terminal_winner_mask != state.active_players_mask) {
    return Result<bool, GameError>::failure(GameError::InvalidState);
  }
  if ((state.status == HandStatus::StreetComplete || state.status == HandStatus::Showdown ||
       state.status == HandStatus::AllInRunout) &&
      !commitments_equal(state)) {
    return Result<bool, GameError>::failure(GameError::InvalidState);
  }
  if (state.status == HandStatus::AllInRunout && state.all_in_players_mask == 0U) {
    return Result<bool, GameError>::failure(GameError::InvalidState);
  }
  return Result<bool, GameError>::success(true);
}

std::string serialize_public_state(const PublicState &state) {
  std::ostringstream output;
  output << "GTOSD-PUBLIC-1|" << static_cast<unsigned>(state.street) << '|'
         << static_cast<unsigned>(state.status) << '|' << state.board_mask << '|'
         << static_cast<unsigned>(state.player_count) << '|'
         << static_cast<unsigned>(state.player_to_act) << '|' << state.initial_pot.units() << '|'
         << state.pot.units() << '|' << state.returned_uncalled.units() << '|'
         << state.current_bet.units() << '|' << state.last_full_raise_increment.units() << '|'
         << static_cast<unsigned>(state.active_players_mask) << '|'
         << static_cast<unsigned>(state.all_in_players_mask) << '|'
         << static_cast<unsigned>(state.acted_players_mask) << '|'
         << static_cast<unsigned>(state.terminal_winner_mask) << '|'
         << static_cast<unsigned>(state.raise_count_this_street);
  const auto append_array = [&](const auto &values) {
    for (const auto value : values) {
      output << '|' << value.units();
    }
  };
  append_array(state.initial_pot_contributions);
  append_array(state.remaining_stacks);
  append_array(state.committed_this_street);
  append_array(state.committed_total);
  append_array(state.returned_uncalled_by_player);
  return output.str();
}

Result<std::vector<Action>, GameError> legal_actions(const PublicState &state,
                                                     const ActionConfig &config) {
  const bool valid_raise_schedule =
      config.aggressive_sizes_by_raise_count.empty() ||
      (config.aggressive_sizes_by_raise_count.size() == config.raise_depth &&
       std::ranges::all_of(config.aggressive_sizes_by_raise_count,
                           [](const auto &sizes) { return !sizes.empty() && sizes.size() <= 3U; }));
  const bool valid_rounding = [&config] {
    Money previous_bound{};
    for (std::size_t index = 0; index < config.aggressive_target_rounding.size(); ++index) {
      const auto &band = config.aggressive_target_rounding[index];
      const bool unbounded = band.upper_bound_exclusive.units() == 0;
      if (band.quantum.units() <= 0 ||
          (unbounded && index + 1U != config.aggressive_target_rounding.size()) ||
          (!unbounded && band.upper_bound_exclusive <= previous_bound)) {
        return false;
      }
      if (!unbounded) {
        previous_bound = band.upper_bound_exclusive;
      }
    }
    return true;
  }();
  if (!validate_state(state) || state.status != HandStatus::InProgress ||
      config.aggressive_sizes.size() > 3U || config.aggressive_targets.size() > 3U ||
      (!config.aggressive_targets.empty() &&
       (!config.aggressive_sizes.empty() || !config.aggressive_sizes_by_raise_count.empty())) ||
      config.raise_depth > maximum_core_raise_depth ||
      !valid_raise_schedule || !valid_rounding || config.minimum_bet.units() <= 0) {
    return Result<std::vector<Action>, GameError>::failure(GameError::InvalidConfiguration);
  }
  const auto player = state.player_to_act;
  const auto to_call = amount_to_call(state, player);
  const auto stack = state.remaining_stacks[player];
  std::vector<Action> actions;
  const auto round_aggressive_payment = [&](const Money payment) -> Result<Money, GameError> {
    if (config.aggressive_target_rounding.empty()) {
      return Result<Money, GameError>::success(payment);
    }
    const auto target = checked_add(state.committed_this_street[player], payment);
    if (!target) {
      return Result<Money, GameError>::failure(target.error());
    }
    for (const auto &band : config.aggressive_target_rounding) {
      if (band.upper_bound_exclusive.units() == 0 || target.value() < band.upper_bound_exclusive) {
        const auto rounded =
            round_to_quantum(target.value(), band.quantum, config.aggressive_target_rounding_mode);
        if (!rounded || rounded.value() < state.committed_this_street[player]) {
          return Result<Money, GameError>::failure(GameError::ArithmeticFailure);
        }
        const auto rounded_payment =
            subtract_checked(rounded.value(), state.committed_this_street[player]);
        return rounded_payment ? Result<Money, GameError>::success(rounded_payment.value())
                               : Result<Money, GameError>::failure(GameError::ArithmeticFailure);
      }
    }
    return Result<Money, GameError>::failure(GameError::InvalidConfiguration);
  };

  if (to_call.units() == 0) {
    actions.push_back({ActionType::Check, Money{}, AllInKind::None, 0});
  } else {
    actions.push_back({ActionType::Fold, Money{}, AllInKind::None, 0});
    const auto call_amount = Money::from_units(std::min(to_call.units(), stack.units())).value();
    actions.push_back(
        {ActionType::Call, call_amount, stack <= to_call ? AllInKind::Call : AllInKind::None, 0});
    if (stack <= to_call) {
      return Result<std::vector<Action>, GameError>::success(std::move(actions));
    }
  }

  const auto pot_after_call_result = checked_add(state.pot, to_call);
  if (!pot_after_call_result) {
    return Result<std::vector<Action>, GameError>::failure(pot_after_call_result.error());
  }
  const auto pot_after_call = pot_after_call_result.value();
  const auto push_increment =
      Money::from_units(stack.units() - std::min(stack.units(), to_call.units())).value();
  bool threshold_triggered = false;
  if (config.all_in_threshold.basis_points() > 0U && pot_after_call.units() > 0) {
    const auto threshold_amount =
        percent_of(pot_after_call, config.all_in_threshold.basis_points(), 100'000U);
    if (!threshold_amount) {
      return Result<std::vector<Action>, GameError>::failure(GameError::ArithmeticFailure);
    }
    // A raise percentage is measured after completing the call. The push size is therefore
    // the stack left above the call divided by that same pot-after-call. This is also the
    // convention used by regular percentage raises below.
    threshold_triggered = config.all_in_strict_boundary
                              ? push_increment < threshold_amount.value()
                              : push_increment <= threshold_amount.value();
  }
  const bool allow_regular = !(config.all_in_mode == AllInMode::Go && threshold_triggered);
  const bool regular_aggression_allowed =
      push_increment.units() > 0 &&
      (to_call.units() == 0 || config.raise_depth > state.raise_count_this_street);

  if (allow_regular && regular_aggression_allowed) {
    if (!config.aggressive_targets.empty()) {
      for (const auto target : config.aggressive_targets) {
        if (target <= state.current_bet) {
          continue;
        }
        const auto required_increment =
            Money::from_units(target.units() - state.current_bet.units()).value();
        const auto payment =
            Money::from_units(target.units() - state.committed_this_street[player].units()).value();
        if ((to_call.units() == 0 && required_increment < config.minimum_bet) ||
            (to_call.units() > 0 && required_increment < state.last_full_raise_increment &&
             !config.allow_incomplete_non_all_in_raise)) {
          continue;
        }
        if (payment >= stack) {
          add_unique_aggressive(actions, {ActionType::AllIn, stack, AllInKind::Raise, 0});
        } else {
          add_unique_aggressive(actions,
                                {to_call.units() == 0 ? ActionType::Bet : ActionType::Raise,
                                 payment, AllInKind::None, 0});
        }
      }
    } else {
      const auto *active_sizes = &config.aggressive_sizes;
      if (to_call.units() > 0 && !config.aggressive_sizes_by_raise_count.empty()) {
        active_sizes = &config.aggressive_sizes_by_raise_count[state.raise_count_this_street];
      }
      for (const auto size : *active_sizes) {
        const auto size_bp = size.basis_points();
        const auto increment = percent_of(pot_after_call, size_bp, 100'000U);
        if (!increment) {
          return Result<std::vector<Action>, GameError>::failure(GameError::ArithmeticFailure);
        }
        const auto required_increment = increment.value();
        if (to_call.units() == 0 && required_increment < config.minimum_bet) {
          continue;
        }
        if (to_call.units() > 0 && required_increment < state.last_full_raise_increment &&
            !config.allow_incomplete_non_all_in_raise) {
          continue;
        }
        const auto total_result = checked_add(to_call, required_increment);
        if (!total_result) {
          return Result<std::vector<Action>, GameError>::failure(GameError::ArithmeticFailure);
        }
        const auto rounded_total = round_aggressive_payment(total_result.value());
        if (!rounded_total) {
          return Result<std::vector<Action>, GameError>::failure(rounded_total.error());
        }
        if (rounded_total.value() <= to_call ||
            (to_call.units() == 0 && rounded_total.value() < config.minimum_bet) ||
            (to_call.units() > 0 &&
             rounded_total.value().units() - to_call.units() <
                 state.last_full_raise_increment.units() &&
             !config.allow_incomplete_non_all_in_raise)) {
          continue;
        }
        if (rounded_total.value() >= stack) {
          add_unique_aggressive(actions, {ActionType::AllIn, stack, AllInKind::Raise, size_bp});
        } else {
          add_unique_aggressive(
              actions, {to_call.units() == 0 ? ActionType::Bet : ActionType::Raise,
                        rounded_total.value(), AllInKind::None, size_bp});
        }
      }
    }
  }

  const bool explicit_all_in =
      (threshold_triggered &&
       (config.all_in_mode == AllInMode::Add || config.all_in_mode == AllInMode::Go)) ||
      (config.all_in_unconditional && config.all_in_mode == AllInMode::Add);
  if (push_increment.units() > 0 && explicit_all_in) {
    add_unique_aggressive(actions, {ActionType::AllIn, stack, AllInKind::Raise, 0});
  }
  return Result<std::vector<Action>, GameError>::success(std::move(actions));
}

Result<PublicState, GameError> apply_action(const PublicState &state, const Action &action,
                                            const ActionConfig &config) {
  const auto generated = legal_actions(state, config);
  if (!generated || std::ranges::find(generated.value(), action) == generated.value().end()) {
    return Result<PublicState, GameError>::failure(GameError::IllegalAction);
  }

  PublicState next = state;
  const auto player = state.player_to_act;
  const auto opponent = next_actionable_player(state, player);
  if (action.type == ActionType::Fold) {
    next.active_players_mask &= static_cast<std::uint8_t>(~player_bit(player));
    next.terminal_winner_mask = next.active_players_mask;
    const auto uncalled = settle_unmatched_commitment(next);
    if (!uncalled) {
      return Result<PublicState, GameError>::failure(uncalled.error());
    }
    next.status = HandStatus::Folded;
    if (!validate_state(next)) {
      return Result<PublicState, GameError>::failure(GameError::InvalidState);
    }
    return Result<PublicState, GameError>::success(next);
  }

  next.acted_players_mask |= player_bit(player);
  if (action.type == ActionType::Check) {
    next.player_to_act = opponent;
    close_if_complete(next);
    if (!validate_state(next)) {
      return Result<PublicState, GameError>::failure(GameError::InvalidState);
    }
    return Result<PublicState, GameError>::success(next);
  }

  const auto to_call = amount_to_call(state, player);
  const auto new_stack = checked_subtract(state.remaining_stacks[player], action.amount);
  const auto new_street = checked_add(state.committed_this_street[player], action.amount);
  const auto new_total = checked_add(state.committed_total[player], action.amount);
  const auto new_pot = checked_add(state.pot, action.amount);
  if (!new_stack || !new_street || !new_total || !new_pot) {
    return Result<PublicState, GameError>::failure(GameError::ArithmeticFailure);
  }
  next.remaining_stacks[player] = new_stack.value();
  next.committed_this_street[player] = new_street.value();
  next.committed_total[player] = new_total.value();
  next.pot = new_pot.value();
  if (next.remaining_stacks[player].units() == 0) {
    next.all_in_players_mask |= player_bit(player);
  }

  const bool aggressive =
      action.type == ActionType::Bet || action.type == ActionType::Raise ||
      (action.type == ActionType::AllIn && action.all_in_kind == AllInKind::Raise);
  if (aggressive) {
    const auto old_bet = state.current_bet;
    next.current_bet = next.committed_this_street[player];
    const auto increment = Money::from_units(next.current_bet.units() - old_bet.units()).value();
    if (increment >= state.last_full_raise_increment) {
      next.last_full_raise_increment = increment;
    }
    if (action.type == ActionType::Raise) {
      ++next.raise_count_this_street;
    }
    next.acted_players_mask = player_bit(player);
  } else if (action.type == ActionType::Call) {
    if (action.amount < to_call) {
      const auto returned = settle_unmatched_commitment(next);
      if (!returned) {
        return Result<PublicState, GameError>::failure(returned.error());
      }
    }
    close_if_complete(next);
  }

  next.player_to_act = next_actionable_player(next, player);
  close_if_complete(next);
  if (!validate_state(next)) {
    return Result<PublicState, GameError>::failure(GameError::InvalidState);
  }
  return Result<PublicState, GameError>::success(next);
}

Result<PublicState, GameError> advance_street(const PublicState &state) {
  if (state.status != HandStatus::StreetComplete || state.street == Street::River) {
    return Result<PublicState, GameError>::failure(GameError::IllegalAction);
  }
  PublicState next = state;
  next.street = static_cast<Street>(static_cast<std::uint8_t>(state.street) + 1U);
  next.status = HandStatus::InProgress;
  next.player_to_act = static_cast<std::uint8_t>(Player::CO);
  next.current_bet = Money{};
  next.last_full_raise_increment = Money{};
  next.committed_this_street = {};
  next.acted_players_mask = 0;
  next.raise_count_this_street = 0;
  if (is_all_in(next, next.player_to_act)) {
    next.player_to_act = next_actionable_player(next, next.player_to_act);
  }
  if (!validate_state(next)) {
    return Result<PublicState, GameError>::failure(GameError::InvalidState);
  }
  return Result<PublicState, GameError>::success(next);
}

Result<Money, GameError> calculate_rake(const RakeConfig &config, const Money called_pot,
                                        const bool flop_dealt) {
  if (!config.enabled || called_pot < config.minimum_pot ||
      (config.no_flop_no_drop && !flop_dealt)) {
    return Result<Money, GameError>::success(Money{});
  }
  const auto raw = percent_of(called_pot, config.percentage.basis_points(), 10'000U);
  if (!raw) {
    return Result<Money, GameError>::failure(GameError::ArithmeticFailure);
  }
  return Result<Money, GameError>::success(std::min(raw.value(), config.cap));
}

Result<std::array<Money, maximum_players>, GameError> split_pot(const Money called_pot,
                                                                const Money rake,
                                                                const WinnerMask winner_mask,
                                                                const std::size_t player_count) {
  if (player_count < 2U || player_count > maximum_players || rake > called_pot) {
    return Result<std::array<Money, maximum_players>, GameError>::failure(
        GameError::InvalidConfiguration);
  }
  const auto player_limit = static_cast<std::uint8_t>(player_count);
  std::uint8_t winner_count = 0;
  const auto valid_mask = static_cast<std::uint8_t>((std::uint16_t{1} << player_limit) - 1U);
  if ((winner_mask.bits & static_cast<std::uint8_t>(~valid_mask)) != 0U) {
    return Result<std::array<Money, maximum_players>, GameError>::failure(
        GameError::InvalidConfiguration);
  }
  for (std::uint8_t player = 0; player < player_limit; ++player) {
    winner_count += static_cast<std::uint8_t>((winner_mask.bits >> player) & 1U);
  }
  if (winner_count == 0U) {
    return Result<std::array<Money, maximum_players>, GameError>::failure(
        GameError::InvalidConfiguration);
  }
  const auto distributable = called_pot.units() - rake.units();
  const auto share = distributable / winner_count;
  auto remainder = distributable % winner_count;
  std::array<Money, maximum_players> payouts{};
  for (std::uint8_t player = 0; player < player_limit; ++player) {
    if ((winner_mask.bits & player_bit(player)) != 0U) {
      const auto extra = remainder > 0 ? 1 : 0;
      payouts[player] = Money::from_units(share + extra).value();
      remainder -= extra;
    }
  }
  return Result<std::array<Money, maximum_players>, GameError>::success(payouts);
}

Result<Settlement, GameError> settle_terminal(const PublicState &state,
                                              const RakeConfig &rake_config,
                                              const std::uint8_t showdown_winner_mask) {
  if (!validate_state(state)) {
    return Result<Settlement, GameError>::failure(GameError::InvalidState);
  }
  std::uint8_t winner_mask = showdown_winner_mask;
  if (state.status == HandStatus::Folded) {
    winner_mask = state.terminal_winner_mask;
  } else if (state.status != HandStatus::Showdown && state.status != HandStatus::AllInRunout) {
    return Result<Settlement, GameError>::failure(GameError::NotTerminal);
  }
  const bool flop_dealt =
      state.street != Street::Preflop || state.status == HandStatus::AllInRunout;
  const auto rake = calculate_rake(rake_config, state.pot, flop_dealt);
  if (!rake) {
    return Result<Settlement, GameError>::failure(rake.error());
  }
  const auto payouts =
      split_pot(state.pot, rake.value(), WinnerMask{winner_mask}, state.player_count);
  if (!payouts) {
    return Result<Settlement, GameError>::failure(payouts.error());
  }

  Settlement settlement;
  settlement.called_pot = state.pot;
  settlement.rake = rake.value();
  settlement.returned_uncalled = state.returned_uncalled;
  settlement.payouts = payouts.value();
  for (std::uint8_t player = 0; player < state.player_count; ++player) {
    settlement.payoff_units[player] =
        settlement.payouts[player].units() + state.returned_uncalled_by_player[player].units() -
        state.committed_total[player].units() - state.initial_pot_contributions[player].units();
  }
  return Result<Settlement, GameError>::success(settlement);
}

} // namespace gtosd
