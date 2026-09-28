#pragma once

#include "gtosd/core/money.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace gtosd {

constexpr std::size_t maximum_players = 6;
// The generic state machine must be able to represent naturally terminating
// betting sequences. Product tree configurations may impose a lower cap.
constexpr std::uint8_t maximum_core_raise_depth = 63;
enum class Player : std::uint8_t { CO = 0, BTN = 1 };
enum class Street : std::uint8_t { Preflop, Flop, Turn, River };
enum class ActionType : std::uint8_t { Fold, Check, Call, Bet, Raise, AllIn };
enum class AllInMode : std::uint8_t { Disabled, Add, Go };
enum class AllInKind : std::uint8_t { None, Call, Raise };
enum class HandStatus : std::uint8_t { InProgress, StreetComplete, Folded, AllInRunout, Showdown };
enum class GameError : std::uint8_t {
  InvalidStack,
  InvalidPot,
  InvalidConfiguration,
  InvalidState,
  IllegalAction,
  ArithmeticFailure,
  NotTerminal
};

struct WinnerMask {
  std::uint8_t bits{0};
};

struct Action {
  Money amount{};
  std::uint32_t requested_basis_points{0};
  ActionType type{ActionType::Check};
  AllInKind all_in_kind{AllInKind::None};

  constexpr Action() = default;
  constexpr Action(const ActionType action_type, const Money action_amount,
                   const AllInKind action_all_in_kind,
                   const std::uint32_t action_requested_basis_points) noexcept
      : amount(action_amount), requested_basis_points(action_requested_basis_points),
        type(action_type), all_in_kind(action_all_in_kind) {}

  friend bool operator==(const Action &, const Action &) = default;
};
static_assert(sizeof(Action) == 16U);

struct PublicState {
  Street street{Street::Preflop};
  HandStatus status{HandStatus::InProgress};
  std::uint64_t board_mask{0};
  std::uint8_t player_count{2};
  std::uint8_t player_to_act{0};
  Money initial_pot{};
  Money pot{};
  Money returned_uncalled{};
  Money current_bet{};
  Money last_full_raise_increment{};
  std::array<Money, maximum_players> initial_pot_contributions{};
  std::array<Money, maximum_players> remaining_stacks{};
  std::array<Money, maximum_players> committed_this_street{};
  std::array<Money, maximum_players> committed_total{};
  std::array<Money, maximum_players> returned_uncalled_by_player{};
  std::uint8_t active_players_mask{0b11};
  std::uint8_t all_in_players_mask{0};
  std::uint8_t acted_players_mask{0};
  std::uint8_t terminal_winner_mask{0};
  std::uint8_t raise_count_this_street{0};

  friend bool operator==(const PublicState &, const PublicState &) = default;
};

struct ActionConfig {
  std::vector<PotPercentage> aggressive_sizes;
  std::uint8_t raise_depth{0};
  AllInMode all_in_mode{AllInMode::Disabled};
  PotPercentage all_in_threshold{PotPercentage::from_basis_points(0).value()};
  bool all_in_strict_boundary{true};
  Money minimum_bet{};
  // Optional per-depth schedule for decisions facing a bet. Entry zero is the
  // first raise, entry one the 3-bet, and so on. An empty schedule preserves
  // the legacy behavior of reusing aggressive_sizes at every raise depth.
  std::vector<std::vector<PotPercentage>> aggressive_sizes_by_raise_count;
  struct RoundingBand {
    // Zero means no upper bound and is valid only for the final band.
    Money upper_bound_exclusive{};
    Money quantum{};
    friend bool operator==(const RoundingBand &, const RoundingBand &) = default;
  };
  std::vector<RoundingBand> aggressive_target_rounding;
  MoneyRoundingMode aggressive_target_rounding_mode{MoneyRoundingMode::Nearest};
  // Disabled for normal poker rules. A benchmark may explicitly reproduce an
  // external action abstraction containing a non-all-in raise smaller than
  // the previous full raise. Such an action does not lower the remembered
  // full-raise increment.
  bool allow_incomplete_non_all_in_raise{false};
  // Optional exact total live commitments for absolute bet/raise abstractions.
  // Dead money in initial_pot_contributions is deliberately excluded. This
  // mode is mutually exclusive with percentage sizes and per-depth schedules.
  std::vector<Money> aggressive_targets;
  // Add mode only: offer the all-in at every decision with chips behind,
  // whatever its size relative to the pot (the threshold then only matters
  // for Go mode). Last member so positional initializers stay valid.
  bool all_in_unconditional{false};
};

struct RakeConfig {
  bool enabled{false};
  RangeWeight percentage{RangeWeight::from_basis_points(0).value()};
  Money cap{};
  bool no_flop_no_drop{true};
  Money minimum_pot{};
};

struct Settlement {
  Money called_pot{};
  Money rake{};
  Money returned_uncalled{};
  std::array<Money, maximum_players> payouts{};
  std::array<std::int64_t, maximum_players> payoff_units{};
};

[[nodiscard]] Result<PublicState, GameError> make_hu_preflop_state(Money effective_stack,
                                                                   Money ante);
[[nodiscard]] Result<PublicState, GameError> make_hu_postflop_state(Street street,
                                                                    Money initial_pot,
                                                                    Money effective_stack,
                                                                    std::uint64_t board_mask = 0);
[[nodiscard]] Money amount_to_call(const PublicState &state, std::uint8_t player);
[[nodiscard]] Result<bool, GameError> validate_state(const PublicState &state);
[[nodiscard]] std::string serialize_public_state(const PublicState &state);
[[nodiscard]] Result<std::vector<Action>, GameError> legal_actions(const PublicState &state,
                                                                   const ActionConfig &config);
[[nodiscard]] Result<PublicState, GameError>
apply_action(const PublicState &state, const Action &action, const ActionConfig &config);
[[nodiscard]] Result<PublicState, GameError> advance_street(const PublicState &state);
[[nodiscard]] Result<Money, GameError> calculate_rake(const RakeConfig &config, Money called_pot,
                                                      bool flop_dealt);
[[nodiscard]] Result<std::array<Money, maximum_players>, GameError>
split_pot(Money called_pot, Money rake, WinnerMask winner_mask, std::size_t player_count);
[[nodiscard]] Result<Settlement, GameError> settle_terminal(const PublicState &state,
                                                            const RakeConfig &rake_config,
                                                            std::uint8_t showdown_winner_mask = 0);

} // namespace gtosd
