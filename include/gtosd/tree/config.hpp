#pragma once

#include "gtosd/core/cards.hpp"
#include "gtosd/core/game.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gtosd {

enum class BettingScenario : std::uint8_t { Lead = 0, AfterCheck = 1, FacingBet = 2 };

enum class TreeConfigError : std::uint8_t {
  InvalidJson,
  UnsupportedVersion,
  MissingField,
  InvalidCard,
  DuplicateCard,
  InvalidMoney,
  InvalidPercentage,
  TooManySizes,
  InvalidRaiseDepth,
  InvalidConfiguration
};

struct ScenarioConfig {
  std::vector<PotPercentage> aggressive_sizes;
  std::uint8_t raise_depth{0};
  AllInMode all_in_mode{AllInMode::Disabled};
  PotPercentage all_in_threshold{PotPercentage::from_basis_points(0).value()};
  Money minimum_bet{};
};

struct StreetActionConfig {
  std::array<std::array<ScenarioConfig, 3>, 2> players{};
};

struct PostflopTreeConfig {
  static constexpr std::uint32_t current_version = 1;

  std::uint32_t version{current_version};
  std::array<CardId, 3> flop{};
  Money initial_pot{};
  Money effective_stack{};
  RakeConfig rake{};
  std::array<StreetActionConfig, 3> streets{};
};

[[nodiscard]] Result<bool, TreeConfigError> validate_tree_config(const PostflopTreeConfig &config);

[[nodiscard]] Result<PostflopTreeConfig, TreeConfigError>
parse_tree_config_json(std::string_view json_text);

[[nodiscard]] std::string serialize_tree_config_json(const PostflopTreeConfig &config);

[[nodiscard]] const char *tree_config_error_name(TreeConfigError error) noexcept;

} // namespace gtosd
