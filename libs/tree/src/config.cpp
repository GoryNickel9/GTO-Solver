#include "gtosd/tree/config.hpp"

#include <nlohmann/json.hpp>

#include <exception>
#include <limits>
#include <string>

namespace gtosd {
namespace {

using Json = nlohmann::json;

Result<Money, TreeConfigError> parse_money(const Json &value, const bool require_positive) {
  if (!value.is_number_integer()) {
    return Result<Money, TreeConfigError>::failure(TreeConfigError::InvalidMoney);
  }
  const auto units = value.get<std::int64_t>();
  const auto money = Money::from_units(units);
  if (!money || (require_positive && units == 0)) {
    return Result<Money, TreeConfigError>::failure(TreeConfigError::InvalidMoney);
  }
  return Result<Money, TreeConfigError>::success(money.value());
}

Result<PotPercentage, TreeConfigError> parse_pot_percentage(const Json &value) {
  if (!value.is_number_integer()) {
    return Result<PotPercentage, TreeConfigError>::failure(TreeConfigError::InvalidPercentage);
  }
  const auto parsed = PotPercentage::from_basis_points(value.get<std::int64_t>());
  if (!parsed) {
    return Result<PotPercentage, TreeConfigError>::failure(TreeConfigError::InvalidPercentage);
  }
  return Result<PotPercentage, TreeConfigError>::success(parsed.value());
}

Result<RangeWeight, TreeConfigError> parse_range_weight(const Json &value) {
  if (!value.is_number_integer()) {
    return Result<RangeWeight, TreeConfigError>::failure(TreeConfigError::InvalidPercentage);
  }
  const auto parsed = RangeWeight::from_basis_points(value.get<std::int64_t>());
  if (!parsed) {
    return Result<RangeWeight, TreeConfigError>::failure(TreeConfigError::InvalidPercentage);
  }
  return Result<RangeWeight, TreeConfigError>::success(parsed.value());
}

Result<AllInMode, TreeConfigError> parse_all_in_mode(const Json &value) {
  if (!value.is_string()) {
    return Result<AllInMode, TreeConfigError>::failure(TreeConfigError::InvalidConfiguration);
  }
  const auto mode = value.get<std::string>();
  if (mode == "disabled") {
    return Result<AllInMode, TreeConfigError>::success(AllInMode::Disabled);
  }
  if (mode == "add") {
    return Result<AllInMode, TreeConfigError>::success(AllInMode::Add);
  }
  if (mode == "go") {
    return Result<AllInMode, TreeConfigError>::success(AllInMode::Go);
  }
  return Result<AllInMode, TreeConfigError>::failure(TreeConfigError::InvalidConfiguration);
}

const char *all_in_mode_name(const AllInMode mode) {
  switch (mode) {
  case AllInMode::Disabled:
    return "disabled";
  case AllInMode::Add:
    return "add";
  case AllInMode::Go:
    return "go";
  }
  return "disabled";
}

Result<ScenarioConfig, TreeConfigError> parse_scenario(const Json &value) {
  if (!value.is_object() || !value.contains("sizes_bp") || !value.contains("raise_depth") ||
      !value.contains("all_in_mode") || !value.contains("all_in_threshold_bp") ||
      !value.contains("minimum_bet_units")) {
    return Result<ScenarioConfig, TreeConfigError>::failure(TreeConfigError::MissingField);
  }
  if (!value.at("sizes_bp").is_array() || value.at("sizes_bp").size() > 3U) {
    return Result<ScenarioConfig, TreeConfigError>::failure(TreeConfigError::TooManySizes);
  }
  if (!value.at("raise_depth").is_number_unsigned() &&
      !value.at("raise_depth").is_number_integer()) {
    return Result<ScenarioConfig, TreeConfigError>::failure(TreeConfigError::InvalidRaiseDepth);
  }
  const auto raise_depth = value.at("raise_depth").get<std::int64_t>();
  if (raise_depth < 0 || raise_depth > 4) {
    return Result<ScenarioConfig, TreeConfigError>::failure(TreeConfigError::InvalidRaiseDepth);
  }

  ScenarioConfig result;
  result.raise_depth = static_cast<std::uint8_t>(raise_depth);
  for (const auto &size : value.at("sizes_bp")) {
    const auto percentage = parse_pot_percentage(size);
    if (!percentage) {
      return Result<ScenarioConfig, TreeConfigError>::failure(percentage.error());
    }
    result.aggressive_sizes.push_back(percentage.value());
  }
  const auto mode = parse_all_in_mode(value.at("all_in_mode"));
  const auto threshold = parse_pot_percentage(value.at("all_in_threshold_bp"));
  const auto minimum_bet = parse_money(value.at("minimum_bet_units"), true);
  if (!mode || !threshold || !minimum_bet) {
    return Result<ScenarioConfig, TreeConfigError>::failure(
        !mode ? mode.error() : (!threshold ? threshold.error() : minimum_bet.error()));
  }
  result.all_in_mode = mode.value();
  result.all_in_threshold = threshold.value();
  result.minimum_bet = minimum_bet.value();
  return Result<ScenarioConfig, TreeConfigError>::success(std::move(result));
}

Json scenario_to_json(const ScenarioConfig &scenario) {
  Json sizes = Json::array();
  for (const auto size : scenario.aggressive_sizes) {
    sizes.push_back(size.basis_points());
  }
  return Json{{"sizes_bp", std::move(sizes)},
              {"raise_depth", scenario.raise_depth},
              {"all_in_mode", all_in_mode_name(scenario.all_in_mode)},
              {"all_in_threshold_bp", scenario.all_in_threshold.basis_points()},
              {"minimum_bet_units", scenario.minimum_bet.units()}};
}

constexpr std::array<const char *, 3> street_names{"flop", "turn", "river"};
constexpr std::array<const char *, 2> player_names{"co", "btn"};
constexpr std::array<const char *, 3> scenario_names{"lead", "after_check", "facing_bet"};

} // namespace

Result<bool, TreeConfigError> validate_tree_config(const PostflopTreeConfig &config) {
  if (config.version != PostflopTreeConfig::current_version) {
    return Result<bool, TreeConfigError>::failure(TreeConfigError::UnsupportedVersion);
  }
  if (config.initial_pot.units() <= 0 || config.effective_stack.units() <= 0) {
    return Result<bool, TreeConfigError>::failure(TreeConfigError::InvalidMoney);
  }
  const std::vector<CardId> flop(config.flop.begin(), config.flop.end());
  if (!card_mask(flop)) {
    return Result<bool, TreeConfigError>::failure(TreeConfigError::DuplicateCard);
  }
  for (const auto &street : config.streets) {
    for (const auto &player : street.players) {
      for (const auto &scenario : player) {
        if (scenario.aggressive_sizes.size() > 3U) {
          return Result<bool, TreeConfigError>::failure(TreeConfigError::TooManySizes);
        }
        if (scenario.raise_depth > 4U) {
          return Result<bool, TreeConfigError>::failure(TreeConfigError::InvalidRaiseDepth);
        }
        if (scenario.minimum_bet.units() <= 0) {
          return Result<bool, TreeConfigError>::failure(TreeConfigError::InvalidMoney);
        }
      }
    }
  }
  return Result<bool, TreeConfigError>::success(true);
}

Result<PostflopTreeConfig, TreeConfigError>
parse_tree_config_json(const std::string_view json_text) {
  try {
    const auto root = Json::parse(json_text);
    if (!root.is_object() || !root.contains("version") || !root.contains("flop") ||
        !root.contains("initial_pot_units") || !root.contains("effective_stack_units") ||
        !root.contains("rake") || !root.contains("streets")) {
      return Result<PostflopTreeConfig, TreeConfigError>::failure(TreeConfigError::MissingField);
    }
    if (!root.at("version").is_number_unsigned() && !root.at("version").is_number_integer()) {
      return Result<PostflopTreeConfig, TreeConfigError>::failure(
          TreeConfigError::UnsupportedVersion);
    }
    const auto version = root.at("version").get<std::int64_t>();
    if (version != PostflopTreeConfig::current_version) {
      return Result<PostflopTreeConfig, TreeConfigError>::failure(
          TreeConfigError::UnsupportedVersion);
    }
    if (!root.at("flop").is_array() || root.at("flop").size() != 3U) {
      return Result<PostflopTreeConfig, TreeConfigError>::failure(TreeConfigError::InvalidCard);
    }

    PostflopTreeConfig config;
    config.version = static_cast<std::uint32_t>(version);
    for (std::size_t index = 0; index < config.flop.size(); ++index) {
      if (!root.at("flop").at(index).is_string()) {
        return Result<PostflopTreeConfig, TreeConfigError>::failure(TreeConfigError::InvalidCard);
      }
      const auto card = parse_card(root.at("flop").at(index).get<std::string>());
      if (!card) {
        return Result<PostflopTreeConfig, TreeConfigError>::failure(TreeConfigError::InvalidCard);
      }
      config.flop[index] = card.value();
    }

    const auto initial_pot = parse_money(root.at("initial_pot_units"), true);
    const auto effective_stack = parse_money(root.at("effective_stack_units"), true);
    if (!initial_pot || !effective_stack) {
      return Result<PostflopTreeConfig, TreeConfigError>::failure(
          !initial_pot ? initial_pot.error() : effective_stack.error());
    }
    config.initial_pot = initial_pot.value();
    config.effective_stack = effective_stack.value();

    const auto &rake = root.at("rake");
    if (!rake.is_object() || !rake.contains("enabled") || !rake.contains("percentage_bp") ||
        !rake.contains("cap_units") || !rake.contains("no_flop_no_drop") ||
        !rake.contains("minimum_pot_units") || !rake.at("enabled").is_boolean() ||
        !rake.at("no_flop_no_drop").is_boolean()) {
      return Result<PostflopTreeConfig, TreeConfigError>::failure(TreeConfigError::MissingField);
    }
    const auto percentage = parse_range_weight(rake.at("percentage_bp"));
    const auto cap = parse_money(rake.at("cap_units"), false);
    const auto minimum_pot = parse_money(rake.at("minimum_pot_units"), false);
    if (!percentage || !cap || !minimum_pot) {
      return Result<PostflopTreeConfig, TreeConfigError>::failure(
          !percentage ? percentage.error() : (!cap ? cap.error() : minimum_pot.error()));
    }
    config.rake.enabled = rake.at("enabled").get<bool>();
    config.rake.percentage = percentage.value();
    config.rake.cap = cap.value();
    config.rake.no_flop_no_drop = rake.at("no_flop_no_drop").get<bool>();
    config.rake.minimum_pot = minimum_pot.value();

    const auto &streets = root.at("streets");
    if (!streets.is_object()) {
      return Result<PostflopTreeConfig, TreeConfigError>::failure(TreeConfigError::MissingField);
    }
    for (std::size_t street = 0; street < street_names.size(); ++street) {
      if (!streets.contains(street_names[street]) ||
          !streets.at(street_names[street]).is_object()) {
        return Result<PostflopTreeConfig, TreeConfigError>::failure(TreeConfigError::MissingField);
      }
      const auto &street_json = streets.at(street_names[street]);
      for (std::size_t player = 0; player < player_names.size(); ++player) {
        if (!street_json.contains(player_names[player]) ||
            !street_json.at(player_names[player]).is_object()) {
          return Result<PostflopTreeConfig, TreeConfigError>::failure(
              TreeConfigError::MissingField);
        }
        const auto &player_json = street_json.at(player_names[player]);
        for (std::size_t scenario = 0; scenario < scenario_names.size(); ++scenario) {
          if (!player_json.contains(scenario_names[scenario])) {
            return Result<PostflopTreeConfig, TreeConfigError>::failure(
                TreeConfigError::MissingField);
          }
          const auto parsed = parse_scenario(player_json.at(scenario_names[scenario]));
          if (!parsed) {
            return Result<PostflopTreeConfig, TreeConfigError>::failure(parsed.error());
          }
          config.streets[street].players[player][scenario] = parsed.value();
        }
      }
    }
    const auto valid = validate_tree_config(config);
    if (!valid) {
      return Result<PostflopTreeConfig, TreeConfigError>::failure(valid.error());
    }
    return Result<PostflopTreeConfig, TreeConfigError>::success(std::move(config));
  } catch (const std::exception &) {
    return Result<PostflopTreeConfig, TreeConfigError>::failure(TreeConfigError::InvalidJson);
  }
}

std::string serialize_tree_config_json(const PostflopTreeConfig &config) {
  Json root;
  root["version"] = config.version;
  root["flop"] = Json::array(
      {format_card(config.flop[0]), format_card(config.flop[1]), format_card(config.flop[2])});
  root["initial_pot_units"] = config.initial_pot.units();
  root["effective_stack_units"] = config.effective_stack.units();
  root["rake"] = Json{{"enabled", config.rake.enabled},
                      {"percentage_bp", config.rake.percentage.basis_points()},
                      {"cap_units", config.rake.cap.units()},
                      {"no_flop_no_drop", config.rake.no_flop_no_drop},
                      {"minimum_pot_units", config.rake.minimum_pot.units()}};
  for (std::size_t street = 0; street < street_names.size(); ++street) {
    for (std::size_t player = 0; player < player_names.size(); ++player) {
      for (std::size_t scenario = 0; scenario < scenario_names.size(); ++scenario) {
        root["streets"][street_names[street]][player_names[player]][scenario_names[scenario]] =
            scenario_to_json(config.streets[street].players[player][scenario]);
      }
    }
  }
  return root.dump(2);
}

const char *tree_config_error_name(const TreeConfigError error) noexcept {
  switch (error) {
  case TreeConfigError::InvalidJson:
    return "invalid_json";
  case TreeConfigError::UnsupportedVersion:
    return "unsupported_version";
  case TreeConfigError::MissingField:
    return "missing_field";
  case TreeConfigError::InvalidCard:
    return "invalid_card";
  case TreeConfigError::DuplicateCard:
    return "duplicate_card";
  case TreeConfigError::InvalidMoney:
    return "invalid_money";
  case TreeConfigError::InvalidPercentage:
    return "invalid_percentage";
  case TreeConfigError::TooManySizes:
    return "too_many_sizes";
  case TreeConfigError::InvalidRaiseDepth:
    return "invalid_raise_depth";
  case TreeConfigError::InvalidConfiguration:
    return "invalid_configuration";
  }
  return "unknown_tree_config_error";
}

} // namespace gtosd
