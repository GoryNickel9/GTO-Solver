#pragma once

#include "gtosd/core/game.hpp"
#include "gtosd/preflop_blueprint/game_config.hpp"

#include <cstdint>
#include <vector>

// Rules layer of the preflop blueprint solver.
//
// The betting rules are the ones of gtosd::core (legal_actions, apply_action,
// settle_terminal). This layer adds what the core engine leaves to the
// caller: the initial N-player preflop state, the action abstraction that
// applies at a decision as a function of the public state (aggression level
// of the street, all-in facing) and the transitions the core only implements
// for two players (a fold that leaves two or more active players, the street
// advance when the first seat is no longer active). Every rule is written
// for player_count in [2, 6]; the two-player path delegates to the core and
// is verified against it transition by transition.
namespace gtosd::preflop_blueprint {

enum class GameModelError : std::uint8_t {
  InvalidConfiguration,
  GameFailure,
  UnsupportedTransition,
  NodeOverflow
};

// Number of aggressive actions (bet, raise, all-in raise) already taken on
// the current street along the path. It is the state variable that selects
// the preflop abstraction: 0 opens, 1 responds to an open, 2+ leaves only
// fold, call and all-in. The core does not track bets in
// raise_count_this_street, so the compiler carries this counter itself.
using AggressionLevel = std::uint8_t;

// Every player posts the dead ante into initial_pot; the last seat (BTN) posts
// the live button blind; seat 0 acts first. For two players with a button
// blind equal to the ante it reproduces the two-player constructor of the core.
[[nodiscard]] Result<PublicState, GameModelError> make_preflop_state(const GameConfig &config);

// True when the current bet of the street was set by a player who is all-in.
[[nodiscard]] bool facing_all_in(const PublicState &state) noexcept;

// Action abstraction at a decision node.
[[nodiscard]] Result<ActionConfig, GameModelError>
// `limped_pot` marks the preflop branch in which a player called the button
// blind before the first raise; it only affects the level 1 response and only
// when the configuration carries limp_response_targets.
action_config_at(const GameConfig &config, const PublicState &state, AggressionLevel level,
                 bool limped_pot = false);

[[nodiscard]] constexpr bool is_aggressive(const Action &action) noexcept {
  return action.type == ActionType::Bet || action.type == ActionType::Raise ||
         (action.type == ActionType::AllIn && action.all_in_kind == AllInKind::Raise);
}

// Applies an action legal under action_config. Two players: gtosd::apply_action.
// More players: the same rules, with the fold generalized (the hand ends only
// when one player remains; the uncalled excess returns to that player).
// A call for less than the amount to call is rejected for more than two
// players: with equal stacks it cannot occur and side pots are not modeled.
[[nodiscard]] Result<PublicState, GameModelError>
apply_action_at(const PublicState &state, const Action &action, const ActionConfig &action_config);

// Moves a StreetComplete state to the next street; the first active player
// who is not all-in acts first (UTG..BTN order, decision D10).
[[nodiscard]] Result<PublicState, GameModelError> advance_to_next_street(const PublicState &state);

[[nodiscard]] const char *game_model_error_name(GameModelError error) noexcept;

} // namespace gtosd::preflop_blueprint
