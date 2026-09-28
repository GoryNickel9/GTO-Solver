#include "gtosd/preflop_blueprint/game_config.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace gtosd::preflop_blueprint {
namespace {

using Json = nlohmann::json;
using OrderedJson = nlohmann::ordered_json;

constexpr std::uint64_t fnv_offset = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;
constexpr std::string_view button_position = "BTN";

template <typename T> using ConfigResult = Result<T, ConfigError>;

ConfigResult<const Json *> field(const Json &root, const std::string_view key) {
  const auto found = root.find(std::string(key));
  if (found == root.end()) {
    return ConfigResult<const Json *>::failure(ConfigError::MissingField);
  }
  return ConfigResult<const Json *>::success(&*found);
}

ConfigResult<bool> expect_constant(const Json &root, const std::string_view key,
                                   const std::string_view expected) {
  const auto value = field(root, key);
  if (!value) {
    return ConfigResult<bool>::failure(value.error());
  }
  if (!value.value()->is_string() || value.value()->get<std::string>() != expected) {
    return ConfigResult<bool>::failure(ConfigError::InvalidValue);
  }
  return ConfigResult<bool>::success(true);
}

ConfigResult<Money> money_from(const Json &value) {
  if (!value.is_number_integer()) {
    return ConfigResult<Money>::failure(ConfigError::InvalidValue);
  }
  const auto money = Money::from_units(value.get<std::int64_t>());
  return money ? ConfigResult<Money>::success(money.value())
               : ConfigResult<Money>::failure(ConfigError::InvalidValue);
}

ConfigResult<Money> money_field(const Json &root, const std::string_view key) {
  const auto value = field(root, key);
  if (!value) {
    return ConfigResult<Money>::failure(value.error());
  }
  return money_from(*value.value());
}

ConfigResult<std::vector<Money>> money_list_field(const Json &root, const std::string_view key) {
  const auto value = field(root, key);
  if (!value) {
    return ConfigResult<std::vector<Money>>::failure(value.error());
  }
  if (!value.value()->is_array()) {
    return ConfigResult<std::vector<Money>>::failure(ConfigError::InvalidValue);
  }
  std::vector<Money> result;
  result.reserve(value.value()->size());
  for (const auto &entry : *value.value()) {
    const auto money = money_from(entry);
    if (!money) {
      return ConfigResult<std::vector<Money>>::failure(money.error());
    }
    result.push_back(money.value());
  }
  return ConfigResult<std::vector<Money>>::success(std::move(result));
}

ConfigResult<bool> bool_field(const Json &root, const std::string_view key) {
  const auto value = field(root, key);
  if (!value) {
    return ConfigResult<bool>::failure(value.error());
  }
  if (!value.value()->is_boolean()) {
    return ConfigResult<bool>::failure(ConfigError::InvalidValue);
  }
  return ConfigResult<bool>::success(value.value()->get<bool>());
}

template <typename Value> bool strictly_increasing(const std::vector<Value> &values) {
  return std::adjacent_find(values.begin(), values.end(),
                            [](const Value &left, const Value &right) {
                              return !(left < right);
                            }) == values.end();
}

std::uint64_t fnv1a(const std::string_view text, std::uint64_t hash = fnv_offset) {
  for (const auto character : text) {
    hash ^= static_cast<std::uint8_t>(character);
    hash *= fnv_prime;
  }
  return hash;
}

std::string hex64(std::uint64_t value) {
  constexpr std::string_view digits = "0123456789abcdef";
  std::string result(16U, '0');
  for (std::size_t index = 0; index < result.size(); ++index) {
    result[result.size() - 1U - index] = digits[static_cast<std::size_t>(value & 0xFU)];
    value >>= 4U;
  }
  return result;
}

OrderedJson to_ordered_json(const GameConfig &config) {
  OrderedJson root;
  root["schema"] = std::string(game_schema_id);
  root["id"] = config.id;
  root["monetary_contract_revision"] = monetary_contract_revision;
  root["ante_accounting"] = "dead_initial_pot_contribution";
  root["preflop_target_basis"] = "live_commitment_excluding_dead_ante";
  root["player_count"] = static_cast<unsigned>(config.player_count);
  root["positions"] = config.positions;
  root["effective_stack_units"] = config.effective_stack.units();
  root["ante_units"] = config.ante.units();
  root["button_blind_units"] = config.button_blind.units();
  OrderedJson open_targets = OrderedJson::array();
  for (const auto target : config.open_targets) {
    open_targets.push_back(target.units());
  }
  root["open_target_units"] = std::move(open_targets);
  OrderedJson response_targets = OrderedJson::array();
  for (const auto target : config.response_targets) {
    response_targets.push_back(target.units());
  }
  root["response_target_units"] = std::move(response_targets);
  // Emitted only when set, so a configuration that predates this field keeps
  // its serialization and therefore its fingerprint and its artifacts.
  if (config.limp_response_targets.has_value()) {
    OrderedJson limp_targets = OrderedJson::array();
    for (const auto target : config.limp_response_targets.value()) {
      limp_targets.push_back(target.units());
    }
    root["limp_response_target_units"] = std::move(limp_targets);
  }
  root["allow_configured_incomplete_raise"] = config.allow_configured_incomplete_raise;
  OrderedJson sizes = OrderedJson::array();
  for (const auto size : config.postflop_sizes) {
    sizes.push_back(size.basis_points());
  }
  root["postflop_sizes_basis_points"] = std::move(sizes);
  root["postflop_minimum_bet_units"] = config.postflop_minimum_bet.units();
  root["include_all_in"] = config.include_all_in;
  // Emitted only when false, for the same reason: every configuration that
  // allows donk bets keeps the fingerprint it had before this field existed.
  if (!config.postflop_donk_bets) {
    root["postflop_donk_bets"] = false;
  }
  root["raise_termination"] = "natural_stack";
  root["rake_mode"] = config.rake.enabled ? "enabled" : "disabled";
  return root;
}

} // namespace

bool operator==(const GameConfig &left, const GameConfig &right) {
  return left.id == right.id && left.player_count == right.player_count &&
         left.positions == right.positions && left.effective_stack == right.effective_stack &&
         left.ante == right.ante && left.button_blind == right.button_blind &&
         left.open_targets == right.open_targets &&
         left.response_targets == right.response_targets &&
         left.limp_response_targets == right.limp_response_targets &&
         left.allow_configured_incomplete_raise == right.allow_configured_incomplete_raise &&
         left.postflop_sizes == right.postflop_sizes &&
         left.postflop_minimum_bet == right.postflop_minimum_bet &&
         left.include_all_in == right.include_all_in &&
         left.postflop_donk_bets == right.postflop_donk_bets &&
         left.rake.enabled == right.rake.enabled &&
         left.rake.percentage == right.rake.percentage && left.rake.cap == right.rake.cap &&
         left.rake.no_flop_no_drop == right.rake.no_flop_no_drop &&
         left.rake.minimum_pot == right.rake.minimum_pot;
}

Result<bool, ConfigError> validate_game_config(const GameConfig &config) {
  using Validation = Result<bool, ConfigError>;
  if (config.id.empty()) {
    return Validation::failure(ConfigError::InvalidValue);
  }
  if (config.player_count < minimum_players || config.player_count > maximum_players ||
      config.positions.size() != config.player_count) {
    return Validation::failure(ConfigError::InvalidStructure);
  }
  std::set<std::string> distinct_positions;
  for (const auto &position : config.positions) {
    if (position.empty() || !distinct_positions.insert(position).second) {
      return Validation::failure(ConfigError::InvalidStructure);
    }
  }
  if (config.positions.back() != button_position) {
    return Validation::failure(ConfigError::InvalidStructure);
  }
  const Money zero{};
  if (!(config.effective_stack > zero) || !(config.ante > zero) || !(config.button_blind > zero) ||
      !(config.postflop_minimum_bet > zero) || config.button_blind >= config.effective_stack) {
    return Validation::failure(ConfigError::InvalidValue);
  }
  // An empty response list means that the only re-raise over an open is the
  // all-in; otherwise there is one response target per open target.
  if (config.open_targets.empty() || config.open_targets.size() > maximum_preflop_targets ||
      (!config.response_targets.empty() &&
       config.response_targets.size() != config.open_targets.size())) {
    return Validation::failure(ConfigError::InvalidStructure);
  }
  if (!strictly_increasing(config.open_targets)) {
    return Validation::failure(ConfigError::InvalidValue);
  }
  for (std::size_t index = 0; index < config.open_targets.size(); ++index) {
    const auto open = config.open_targets[index];
    if (open <= config.button_blind || open >= config.effective_stack) {
      return Validation::failure(ConfigError::InvalidValue);
    }
    if (config.response_targets.empty()) {
      continue;
    }
    const auto response = config.response_targets[index];
    if (response <= open || response >= config.effective_stack) {
      return Validation::failure(ConfigError::InvalidValue);
    }
  }
  // The limped-pot response list, when present, obeys the same shape rules as
  // response_targets: empty means all-in only, otherwise one target per open.
  if (config.limp_response_targets.has_value()) {
    const auto &targets = config.limp_response_targets.value();
    if (!targets.empty() && targets.size() != config.open_targets.size()) {
      return Validation::failure(ConfigError::InvalidStructure);
    }
    for (std::size_t index = 0; index < targets.size(); ++index) {
      if (targets[index] <= config.open_targets[index] ||
          targets[index] >= config.effective_stack) {
        return Validation::failure(ConfigError::InvalidValue);
      }
    }
  }
  if (config.postflop_sizes.empty() || config.postflop_sizes.size() > maximum_postflop_sizes) {
    return Validation::failure(ConfigError::InvalidStructure);
  }
  for (std::size_t index = 0; index < config.postflop_sizes.size(); ++index) {
    if (config.postflop_sizes[index].basis_points() == 0U ||
        (index > 0U && config.postflop_sizes[index].basis_points() <=
                           config.postflop_sizes[index - 1U].basis_points())) {
      return Validation::failure(ConfigError::InvalidValue);
    }
  }
  if (!config.include_all_in || config.rake.enabled) {
    return Validation::failure(ConfigError::InvalidValue);
  }
  return Validation::success(true);
}

Result<GameConfig, ConfigError> parse_game_config_json(const std::string_view json_text) {
  using Parsed = Result<GameConfig, ConfigError>;
  try {
    const auto root = Json::parse(json_text);
    if (!root.is_object()) {
      return Parsed::failure(ConfigError::InvalidJson);
    }
    const auto schema = field(root, "schema");
    if (!schema) {
      return Parsed::failure(schema.error());
    }
    if (!schema.value()->is_string() || schema.value()->get<std::string>() != game_schema_id) {
      return Parsed::failure(ConfigError::UnsupportedSchema);
    }
    const auto revision = field(root, "monetary_contract_revision");
    if (!revision) {
      return Parsed::failure(revision.error());
    }
    if (!revision.value()->is_number_integer() ||
        revision.value()->get<std::int64_t>() !=
            static_cast<std::int64_t>(monetary_contract_revision)) {
      return Parsed::failure(ConfigError::InvalidValue);
    }
    for (const auto &[key, expected] :
         {std::pair<std::string_view, std::string_view>{"ante_accounting",
                                                        "dead_initial_pot_contribution"},
          std::pair<std::string_view, std::string_view>{"preflop_target_basis",
                                                        "live_commitment_excluding_dead_ante"},
          std::pair<std::string_view, std::string_view>{"raise_termination", "natural_stack"},
          std::pair<std::string_view, std::string_view>{"rake_mode", "disabled"}}) {
      const auto constant = expect_constant(root, key, expected);
      if (!constant) {
        return Parsed::failure(constant.error());
      }
    }

    GameConfig config;
    const auto id = field(root, "id");
    if (!id) {
      return Parsed::failure(id.error());
    }
    if (!id.value()->is_string()) {
      return Parsed::failure(ConfigError::InvalidValue);
    }
    config.id = id.value()->get<std::string>();

    const auto player_count = field(root, "player_count");
    if (!player_count) {
      return Parsed::failure(player_count.error());
    }
    if (!player_count.value()->is_number_integer()) {
      return Parsed::failure(ConfigError::InvalidValue);
    }
    const auto players = player_count.value()->get<std::int64_t>();
    if (players < static_cast<std::int64_t>(minimum_players) ||
        players > static_cast<std::int64_t>(maximum_players)) {
      return Parsed::failure(ConfigError::InvalidStructure);
    }
    config.player_count = static_cast<std::uint8_t>(players);

    const auto positions = field(root, "positions");
    if (!positions) {
      return Parsed::failure(positions.error());
    }
    if (!positions.value()->is_array()) {
      return Parsed::failure(ConfigError::InvalidValue);
    }
    for (const auto &position : *positions.value()) {
      if (!position.is_string()) {
        return Parsed::failure(ConfigError::InvalidValue);
      }
      config.positions.push_back(position.get<std::string>());
    }

    const auto stack = money_field(root, "effective_stack_units");
    const auto ante = money_field(root, "ante_units");
    const auto button_blind = money_field(root, "button_blind_units");
    const auto minimum_bet = money_field(root, "postflop_minimum_bet_units");
    const auto open_targets = money_list_field(root, "open_target_units");
    const auto response_targets = money_list_field(root, "response_target_units");
    const auto allow_incomplete = bool_field(root, "allow_configured_incomplete_raise");
    const auto include_all_in = bool_field(root, "include_all_in");
    for (const auto error : {stack ? std::optional<ConfigError>{} : stack.error(),
                             ante ? std::optional<ConfigError>{} : ante.error(),
                             button_blind ? std::optional<ConfigError>{} : button_blind.error(),
                             minimum_bet ? std::optional<ConfigError>{} : minimum_bet.error(),
                             open_targets ? std::optional<ConfigError>{} : open_targets.error(),
                             response_targets ? std::optional<ConfigError>{}
                                              : response_targets.error(),
                             allow_incomplete ? std::optional<ConfigError>{}
                                              : allow_incomplete.error(),
                             include_all_in ? std::optional<ConfigError>{}
                                            : include_all_in.error()}) {
      if (error.has_value()) {
        return Parsed::failure(error.value());
      }
    }
    config.effective_stack = stack.value();
    config.ante = ante.value();
    config.button_blind = button_blind.value();
    config.postflop_minimum_bet = minimum_bet.value();
    config.open_targets = open_targets.value();
    config.response_targets = response_targets.value();
    // Optional: a missing key keeps the historical behaviour (the limped-pot
    // response reuses response_targets); an explicit empty list removes the
    // configured re-raise on that branch.
    if (root.contains("limp_response_target_units")) {
      const auto limp_targets = money_list_field(root, "limp_response_target_units");
      if (!limp_targets) {
        return Parsed::failure(limp_targets.error());
      }
      config.limp_response_targets = limp_targets.value();
    }
    config.allow_configured_incomplete_raise = allow_incomplete.value();
    config.include_all_in = include_all_in.value();
    // Optional boolean: a missing key allows donk bets, the behaviour of every
    // configuration written before this field existed.
    if (root.contains("postflop_donk_bets")) {
      const auto donk_bets = bool_field(root, "postflop_donk_bets");
      if (!donk_bets) {
        return Parsed::failure(donk_bets.error());
      }
      config.postflop_donk_bets = donk_bets.value();
    }

    const auto sizes = field(root, "postflop_sizes_basis_points");
    if (!sizes) {
      return Parsed::failure(sizes.error());
    }
    if (!sizes.value()->is_array()) {
      return Parsed::failure(ConfigError::InvalidValue);
    }
    for (const auto &size : *sizes.value()) {
      if (!size.is_number_integer()) {
        return Parsed::failure(ConfigError::InvalidValue);
      }
      const auto percentage = PotPercentage::from_basis_points(size.get<std::int64_t>());
      if (!percentage) {
        return Parsed::failure(ConfigError::InvalidValue);
      }
      config.postflop_sizes.push_back(percentage.value());
    }
    config.rake = RakeConfig{};

    const auto valid = validate_game_config(config);
    if (!valid) {
      return Parsed::failure(valid.error());
    }
    return Parsed::success(std::move(config));
  } catch (const std::exception &) {
    return Parsed::failure(ConfigError::InvalidJson);
  }
}

std::string serialize_game_config_json(const GameConfig &config) {
  return to_ordered_json(config).dump(2) + '\n';
}

std::string game_config_fingerprint(const GameConfig &config) {
  return "fnv1a64:" + hex64(fnv1a(to_ordered_json(config).dump()));
}

const char *config_error_name(const ConfigError error) noexcept {
  switch (error) {
  case ConfigError::InvalidJson:
    return "invalid_json";
  case ConfigError::UnsupportedSchema:
    return "unsupported_schema";
  case ConfigError::MissingField:
    return "missing_field";
  case ConfigError::InvalidValue:
    return "invalid_value";
  case ConfigError::InvalidStructure:
    return "invalid_structure";
  }
  return "unknown";
}

} // namespace gtosd::preflop_blueprint
