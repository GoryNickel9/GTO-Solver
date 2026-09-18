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
  // Live commitment targets available to the first raiser, strictly increasing.
  std::vector<Money> open_targets;
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
  // Version 1 accepts only a disabled rake.
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
