#pragma once

#include "gtosd/core/game.hpp"
#include "gtosd/core/money.hpp"
#include "gtosd/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Declarative game configuration of the preflop blueprint solver.
//
// Schema `gtosd.preflop_blueprint_game.v1` keeps the monetary contract of the
// legacy CO40 fixture (dead antes, live button blind, live commitment targets)
// and generalizes it to N players and variable-length size lists. The
// positions list is the acting order UTG..BTN; the BTN posts the button blind
// and always acts last, preflop and postflop (decision D10).
namespace gtosd::preflop_blueprint {

enum class ConfigError : std::uint8_t {
  InvalidJson,
  UnsupportedSchema,
  MissingField,
  InvalidValue,
  InvalidStructure
};

inline constexpr std::string_view game_schema_id = "gtosd.preflop_blueprint_game.v1";
inline constexpr std::uint32_t monetary_contract_revision = 2U;
inline constexpr std::size_t minimum_players = 2U;
inline constexpr std::size_t maximum_players = gtosd::maximum_players;
// Fold/check/call plus sizes plus all-in must fit the compiled action arrays.
inline constexpr std::size_t maximum_postflop_sizes = 3U;
inline constexpr std::size_t maximum_preflop_targets = 4U;
// The core accepts at most three percentage sizes per decision.
inline constexpr std::size_t maximum_preflop_sizes = 3U;

struct GameConfig {
  std::string id;
  std::uint8_t player_count{2U};
  // Acting order, first to act first. The last entry must be "BTN".
  std::vector<std::string> positions;
  Money effective_stack{};
  // Dead ante posted by every player.
  Money ante{};
  // Live blind posted by the BTN; the incremental call of the first actor
  // equals this amount.
  Money button_blind{};
  // Live commitment targets available to the first raiser, strictly increasing;
  // empty when open_sizes is set.
  std::vector<Money> open_targets;
  // Sizes of the first raise of the hand (aggression level 0: the open, the
  // isolation over limpers, the BTN raise over limps) in basis points of the
  // pot after the call, MonkerSolver's pot-relative convention: raise to =
  // current bet + size x (pot + amount to call), dead antes included in the pot
  // (10000 = a pot raise: 6a UTG open, 7a isolation over one limp and BTN raise
  // over two limps in the 3-way 50a trees). Strictly increasing, at most
  // maximum_preflop_sizes. Mutually exclusive with open_targets, which must be
  // empty when this list is set; the response to that raise is then fold, call
  // or all-in only (response_targets empty, limp_response_targets absent or
  // empty), so allow_configured_incomplete_raise is never read. Serialized,
  // and therefore part of the fingerprint, only when set.
  std::vector<PotPercentage> open_sizes;
  // Index-matched re-raise target available when facing open_targets[i].
  // After that re-raise only fold, call and all-in remain.
  std::vector<Money> response_targets;
  // Same, but for the response to a raise made in a limped pot (a player
  // called the button blind before the first raise). The two public states
  // are identical up to which seat holds which commitment, so the branch
  // cannot be read off the state: the compiler carries the flag. Absent means
  // "use response_targets", which is what every configuration did before this
  // field existed; present and empty means fold, call or all-in only.
  std::optional<std::vector<Money>> limp_response_targets;
  bool allow_configured_incomplete_raise{false};
  // Bet and raise sizes in basis points of the pot, strictly increasing.
  std::vector<PotPercentage> postflop_sizes;
  Money postflop_minimum_bet{};
  bool include_all_in{true};
  // False removes the donk bet (MonkerSolver trees): on a postflop street,
  // while nobody has bet on it, a player may only check when the last
  // aggressor of the previous betting round (bet, raise or all-in) is still in
  // the hand, is not all-in and acts later on this street. A previous round
  // without aggression restricts nothing. Serialized, and therefore part of
  // the fingerprint, only when false.
  bool postflop_donk_bets{true};
  // Largest postflop all-in, as a fraction of the pot in basis points
  // (50,000 = 5x the pot; user decision of 2026-09-29 for the MonkerSolver
  // trees). The all-in is offered only when the chips it adds above the call
  // are at most this fraction of the pot after the call, the convention of
  // the percentage sizes; an all-in of exactly that size is allowed. A size
  // whose bet reaches the stack still becomes the all-in. Preflop is not
  // affected. Absent means an all-in at every postflop decision; serialized,
  // and therefore part of the fingerprint, only when set.
  std::optional<PotPercentage> postflop_all_in_max_pot;
  // Rake settled by core settle_terminal at every terminal. Disabled means
  // exactly the RakeConfig defaults; serialized as "rake_mode" plus, only when
  // enabled, the percentage (basis points), cap, no-flop-no-drop flag and
  // minimum pot, so every configuration without rake keeps its fingerprint.
  // An enabled rake needs a positive percentage and a positive cap. A flop is
  // dealt, hence the hand raked under no-flop-no-drop, at every terminal
  // except a preflop fold (a called preflop all-in and a checkdown leaf are
  // raked).
  RakeConfig rake{};
};

[[nodiscard]] bool operator==(const GameConfig &left, const GameConfig &right);
[[nodiscard]] inline bool operator!=(const GameConfig &left, const GameConfig &right) {
  return !(left == right);
}

[[nodiscard]] Result<bool, ConfigError> validate_game_config(const GameConfig &config);
[[nodiscard]] Result<GameConfig, ConfigError> parse_game_config_json(std::string_view json_text);
[[nodiscard]] std::string serialize_game_config_json(const GameConfig &config);
// FNV-1a over the canonical serialization: "fnv1a64:<16 hex digits>".
[[nodiscard]] std::string game_config_fingerprint(const GameConfig &config);
[[nodiscard]] const char *config_error_name(ConfigError error) noexcept;

} // namespace gtosd::preflop_blueprint
