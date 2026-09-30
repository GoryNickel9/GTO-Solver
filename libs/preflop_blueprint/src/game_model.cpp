#include "gtosd/preflop_blueprint/game_model.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <utility>

namespace gtosd::preflop_blueprint {
namespace {

using StateResult = Result<PublicState, GameModelError>;
using ConfigResult = Result<ActionConfig, GameModelError>;

constexpr std::uint8_t player_bit(const std::uint8_t player) noexcept {
  return static_cast<std::uint8_t>(std::uint8_t{1} << player);
}

bool is_active(const PublicState &state, const std::uint8_t player) noexcept {
  return (state.active_players_mask & player_bit(player)) != 0U;
}

bool is_all_in(const PublicState &state, const std::uint8_t player) noexcept {
  return (state.all_in_players_mask & player_bit(player)) != 0U;
}

std::uint8_t actionable_mask(const PublicState &state) noexcept {
  return static_cast<std::uint8_t>(state.active_players_mask &
                                   static_cast<std::uint8_t>(~state.all_in_players_mask));
}

bool commitments_equal(const PublicState &state) noexcept {
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

void close_if_complete(PublicState &state) noexcept {
  const auto can_act = actionable_mask(state);
  const bool all_acted = (state.acted_players_mask & can_act) == can_act;
  if (!commitments_equal(state) || (can_act != 0U && !all_acted)) {
    return;
  }
  if (state.street == Street::River) {
    state.status = HandStatus::Showdown;
  } else if (state.all_in_players_mask != 0U) {
    state.status = HandStatus::AllInRunout;
  } else {
    state.status = HandStatus::StreetComplete;
  }
}

std::uint8_t next_actionable_player(const PublicState &state, const std::uint8_t actor) noexcept {
  for (std::uint8_t offset = 1; offset <= state.player_count; ++offset) {
    const auto candidate = static_cast<std::uint8_t>((actor + offset) % state.player_count);
    if (is_active(state, candidate) && !is_all_in(state, candidate)) {
      return candidate;
    }
  }
  return actor;
}

Money units(const std::int64_t value) { return Money::from_units(value).value(); }

PotPercentage always_add_all_in() { return PotPercentage::from_basis_points(100'000).value(); }

ActionConfig passive_config(const GameConfig &config) {
  ActionConfig result;
  result.minimum_bet = config.button_blind;
  return result;
}

ActionConfig all_in_config(const GameConfig &config) {
  auto result = passive_config(config);
  if (config.include_all_in) {
    result.all_in_mode = AllInMode::Add;
    result.all_in_threshold = always_add_all_in();
    // The all-in is available at every decision (user decision of 2026-09-28,
    // MonkerSolver trees): the strict 1000 % cap of the threshold dropped the
    // open-shove and the shove over a limp from 42 antes up.
    result.all_in_unconditional = true;
  }
  return result;
}

ConfigResult target_config(const GameConfig &config, const PublicState &state,
                           std::vector<Money> targets, const bool allow_incomplete) {
  auto result = all_in_config(config);
  result.raise_depth = maximum_core_raise_depth;
  result.allow_incomplete_non_all_in_raise = allow_incomplete;
  for (const auto target : targets) {
    if (target <= state.current_bet) {
      return ConfigResult::failure(GameModelError::InvalidConfiguration);
    }
  }
  result.aggressive_targets = std::move(targets);
  return ConfigResult::success(std::move(result));
}

// Pot-relative first raise (MonkerSolver trees, open_sizes): the core raises
// to current bet + size x (pot + call), half up; a size that reaches the
// stack becomes the all-in, which all_in_config offers anyway. With nothing
// to call (the BTN over limps) it is a bet of at least the button blind.
ActionConfig pot_size_config(const GameConfig &config) {
  auto result = all_in_config(config);
  result.raise_depth = maximum_core_raise_depth;
  result.aggressive_sizes.assign(config.open_sizes.begin(), config.open_sizes.end());
  return result;
}

// Donk-bet rule of a configuration with postflop_donk_bets false. Postflop
// streets are played in seat order (decision D10), so before the first bet of
// the street the aggressor acts later than the actor exactly when its seat is
// higher.
bool donk_bet_forbidden(const GameConfig &config, const PublicState &state,
                        const std::uint8_t previous_round_aggressor) noexcept {
  if (config.postflop_donk_bets || state.street == Street::Preflop ||
      state.current_bet.units() != 0 || previous_round_aggressor == no_aggressor) {
    return false;
  }
  return is_active(state, previous_round_aggressor) &&
         !is_all_in(state, previous_round_aggressor) &&
         previous_round_aggressor > state.player_to_act;
}

// Generalized fold: removes the actor; with one player left the hand ends and
// the uncalled excess of that player's street commitment returns to them.
StateResult fold_multiway(const PublicState &state) {
  PublicState next = state;
  const auto player = state.player_to_act;
  next.active_players_mask &= static_cast<std::uint8_t>(~player_bit(player));
  if (std::popcount(next.active_players_mask) >= 2) {
    next.player_to_act = next_actionable_player(next, player);
    close_if_complete(next);
    if (!validate_state(next)) {
      return StateResult::failure(GameModelError::GameFailure);
    }
    return StateResult::success(next);
  }
  const auto winner = static_cast<std::uint8_t>(std::countr_zero(next.active_players_mask));
  Money best_other{};
  for (std::uint8_t other = 0; other < next.player_count; ++other) {
    if (other != winner) {
      best_other = std::max(best_other, next.committed_this_street[other]);
    }
  }
  const auto excess = next.committed_this_street[winner].units() - best_other.units();
  if (excess > 0) {
    const auto amount = units(excess);
    const auto pot = subtract_checked(next.pot, amount);
    const auto street = subtract_checked(next.committed_this_street[winner], amount);
    const auto stack = add_checked(next.remaining_stacks[winner], amount);
    const auto returned = add_checked(next.returned_uncalled, amount);
    const auto returned_player = add_checked(next.returned_uncalled_by_player[winner], amount);
    if (!pot || !street || !stack || !returned || !returned_player) {
      return StateResult::failure(GameModelError::GameFailure);
    }
    next.pot = pot.value();
    next.committed_this_street[winner] = street.value();
    next.remaining_stacks[winner] = stack.value();
    next.returned_uncalled = returned.value();
    next.returned_uncalled_by_player[winner] = returned_player.value();
    if (next.remaining_stacks[winner].units() > 0) {
      next.all_in_players_mask &= static_cast<std::uint8_t>(~player_bit(winner));
    }
  }
  next.current_bet = next.committed_this_street[winner];
  next.terminal_winner_mask = next.active_players_mask;
  next.status = HandStatus::Folded;
  if (!validate_state(next)) {
    return StateResult::failure(GameModelError::GameFailure);
  }
  return StateResult::success(next);
}

} // namespace

Result<PublicState, GameModelError> make_preflop_state(const GameConfig &config) {
  if (!validate_game_config(config)) {
    return StateResult::failure(GameModelError::InvalidConfiguration);
  }
  const auto players = config.player_count;
  const auto ante = config.ante.units();
  const auto blind = config.button_blind.units();
  const auto stack = config.effective_stack.units();
  if (ante > std::numeric_limits<std::int64_t>::max() / (players + 1) || stack < ante + blind) {
    return StateResult::failure(GameModelError::InvalidConfiguration);
  }
  PublicState state;
  state.street = Street::Preflop;
  state.status = HandStatus::InProgress;
  state.player_count = players;
  state.player_to_act = 0U;
  state.initial_pot = units(ante * players);
  state.pot = units(ante * players + blind);
  state.current_bet = config.button_blind;
  state.last_full_raise_increment = config.button_blind;
  state.active_players_mask = static_cast<std::uint8_t>((std::uint16_t{1} << players) - 1U);
  const auto button = static_cast<std::uint8_t>(players - 1U);
  for (std::uint8_t player = 0; player < players; ++player) {
    state.initial_pot_contributions[player] = config.ante;
    const auto live = player == button ? blind : 0;
    state.committed_this_street[player] = units(live);
    state.committed_total[player] = units(live);
    state.remaining_stacks[player] = units(stack - ante - live);
    if (state.remaining_stacks[player].units() == 0) {
      state.all_in_players_mask |= player_bit(player);
    }
  }
  if (!validate_state(state)) {
    return StateResult::failure(GameModelError::GameFailure);
  }
  return StateResult::success(state);
}

bool facing_all_in(const PublicState &state) noexcept {
  if (state.current_bet.units() == 0) {
    return false;
  }
  for (std::uint8_t player = 0; player < state.player_count; ++player) {
    if (is_active(state, player) && is_all_in(state, player) &&
        state.committed_this_street[player] == state.current_bet) {
      return true;
    }
  }
  return false;
}

Result<ActionConfig, GameModelError>
action_config_at(const GameConfig &config, const PublicState &state, const AggressionLevel level,
                 const bool limped_pot, const std::uint8_t previous_round_aggressor) {
  if (state.status != HandStatus::InProgress) {
    return ConfigResult::failure(GameModelError::GameFailure);
  }
  if (previous_round_aggressor != no_aggressor && previous_round_aggressor >= state.player_count) {
    return ConfigResult::failure(GameModelError::InvalidConfiguration);
  }
  if (state.street != Street::Preflop) {
    ActionConfig result;
    result.minimum_bet = config.postflop_minimum_bet;
    if (donk_bet_forbidden(config, state, previous_round_aggressor)) {
      // No size and no all-in: the check is the only legal action.
      return ConfigResult::success(std::move(result));
    }
    result.aggressive_sizes.assign(config.postflop_sizes.begin(), config.postflop_sizes.end());
    result.raise_depth = maximum_core_raise_depth;
    if (config.include_all_in) {
      result.all_in_mode = AllInMode::Add;
      if (config.postflop_all_in_max_pot.has_value()) {
        // Capped all-in (user decision of 2026-09-29): the core adds it when
        // the stack left above the call is at most the cap times the pot
        // after the call, boundary included.
        result.all_in_threshold = config.postflop_all_in_max_pot.value();
        result.all_in_strict_boundary = false;
        result.all_in_unconditional = false;
      } else {
        result.all_in_threshold = always_add_all_in();
        result.all_in_unconditional = true;
      }
    }
    return ConfigResult::success(std::move(result));
  }
  if (facing_all_in(state)) {
    return ConfigResult::success(passive_config(config));
  }
  if (level == 0U) {
    if (!config.open_sizes.empty()) {
      return ConfigResult::success(pot_size_config(config));
    }
    return target_config(config, state, config.open_targets, false);
  }
  if (level == 1U) {
    // In a limped pot the responder uses its own list when one is configured.
    // Pot mode validates with both lists empty: fold, call or all-in.
    const auto &targets = (limped_pot && config.limp_response_targets.has_value())
                              ? config.limp_response_targets.value()
                              : config.response_targets;
    if (targets.empty()) {
      // No configured re-raise size over an open: fold, call or all-in.
      return ConfigResult::success(all_in_config(config));
    }
    const auto found = std::ranges::find(config.open_targets, state.current_bet);
    if (found == config.open_targets.end()) {
      return ConfigResult::failure(GameModelError::InvalidConfiguration);
    }
    const auto index = static_cast<std::size_t>(found - config.open_targets.begin());
    if (index >= targets.size()) {
      return ConfigResult::failure(GameModelError::InvalidConfiguration);
    }
    return target_config(config, state, {targets[index]},
                         config.allow_configured_incomplete_raise);
  }
  return ConfigResult::success(all_in_config(config));
}

Result<PublicState, GameModelError> apply_action_at(const PublicState &state, const Action &action,
                                                    const ActionConfig &action_config) {
  if (state.player_count == 2U) {
    const auto next = apply_action(state, action, action_config);
    if (!next) {
      return StateResult::failure(GameModelError::GameFailure);
    }
    return StateResult::success(next.value());
  }
  const auto legal = legal_actions(state, action_config);
  if (!legal || std::ranges::find(legal.value(), action) == legal.value().end()) {
    return StateResult::failure(GameModelError::GameFailure);
  }
  if (action.type == ActionType::Fold) {
    return fold_multiway(state);
  }
  if (action.type == ActionType::Call &&
      action.amount < amount_to_call(state, state.player_to_act)) {
    return StateResult::failure(GameModelError::UnsupportedTransition);
  }
  const auto next = apply_action(state, action, action_config);
  if (!next) {
    return StateResult::failure(GameModelError::GameFailure);
  }
  return StateResult::success(next.value());
}

Result<PublicState, GameModelError> advance_to_next_street(const PublicState &state) {
  if (state.status != HandStatus::StreetComplete || state.street == Street::River) {
    return StateResult::failure(GameModelError::GameFailure);
  }
  PublicState next = state;
  next.street = static_cast<Street>(static_cast<std::uint8_t>(state.street) + 1U);
  next.status = HandStatus::InProgress;
  next.current_bet = Money{};
  next.last_full_raise_increment = Money{};
  next.committed_this_street = {};
  next.acted_players_mask = 0U;
  next.raise_count_this_street = 0U;
  next.player_to_act = 0U;
  if (!is_active(next, 0U) || is_all_in(next, 0U)) {
    next.player_to_act = next_actionable_player(next, 0U);
  }
  if (!validate_state(next)) {
    return StateResult::failure(GameModelError::GameFailure);
  }
  return StateResult::success(next);
}

const char *game_model_error_name(const GameModelError error) noexcept {
  switch (error) {
  case GameModelError::InvalidConfiguration:
    return "invalid_configuration";
  case GameModelError::GameFailure:
    return "game_failure";
  case GameModelError::UnsupportedTransition:
    return "unsupported_transition";
  case GameModelError::NodeOverflow:
    return "node_overflow";
  }
  return "unknown";
}

} // namespace gtosd::preflop_blueprint
