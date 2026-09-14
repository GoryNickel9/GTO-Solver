#include "gtosd/preflop/hu_preflop.hpp"

#include <nlohmann/json.hpp>

#include <exception>

namespace gtosd {
namespace {

using Json = nlohmann::json;

Result<Money, HuPreflopError> parse_money(const Json &value) {
  if (!value.is_number_integer()) {
    return Result<Money, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
  }
  const auto money = Money::from_units(value.get<std::int64_t>());
  return money ? Result<Money, HuPreflopError>::success(money.value())
               : Result<Money, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
}

Result<PotPercentage, HuPreflopError> parse_percentage(const Json &value) {
  if (!value.is_number_integer()) {
    return Result<PotPercentage, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
  }
  const auto percentage = PotPercentage::from_basis_points(value.get<std::int64_t>());
  return percentage
             ? Result<PotPercentage, HuPreflopError>::success(percentage.value())
             : Result<PotPercentage, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
}

Result<RangeWeight, HuPreflopError> parse_range_weight(const Json &value) {
  if (!value.is_number_integer()) {
    return Result<RangeWeight, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
  }
  const auto weight = RangeWeight::from_basis_points(value.get<std::int64_t>());
  return weight ? Result<RangeWeight, HuPreflopError>::success(weight.value())
                : Result<RangeWeight, HuPreflopError>::failure(
                      HuPreflopError::InvalidConfiguration);
}

} // namespace

Result<HuPreflopConfig, HuPreflopError>
deserialize_hu_preflop_config_json(const std::string &serialized) {
  try {
    const auto root = Json::parse(serialized);
    if (!root.is_object() || !root.contains("schema") ||
        (root.at("schema") != "gtosd.hu_preflop_game.v1" &&
         root.at("schema") != "gtosd.hu_preflop_game.v2") ||
        !root.contains("monetary_contract_revision") ||
        root.at("monetary_contract_revision") != 2 || !root.contains("ante_accounting") ||
        root.at("ante_accounting") != "dead_initial_pot_contribution" ||
        !root.contains("preflop_target_basis") ||
        root.at("preflop_target_basis") != "live_commitment_excluding_dead_ante" ||
        root.at("raise_termination") != "natural_stack") {
      return Result<HuPreflopConfig, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
    }
    const bool version_two = root.at("schema") == "gtosd.hu_preflop_game.v2";
    if ((!version_two && root.at("rake_mode") != "disabled") ||
        (version_two && !root.contains("rake"))) {
      return Result<HuPreflopConfig, HuPreflopError>::failure(
          HuPreflopError::InvalidConfiguration);
    }
    HuPreflopConfig config;
    const auto stack = parse_money(root.at("effective_stack_units"));
    const auto ante = parse_money(root.at("ante_units"));
    const auto minimum_bet = parse_money(root.at("postflop_minimum_bet_units"));
    const auto &open_targets = root.at("open_target_units");
    const auto &response_targets = root.at("response_target_units");
    const auto &postflop_sizes = root.at("postflop_sizes_basis_points");
    if (!stack || !ante || !minimum_bet || !open_targets.is_array() || open_targets.size() != 2U ||
        !response_targets.is_array() || response_targets.size() != 2U ||
        !postflop_sizes.is_array() || postflop_sizes.size() != 3U ||
        !root.at("include_all_in").is_boolean() ||
        !root.at("allow_configured_incomplete_raise").is_boolean()) {
      return Result<HuPreflopConfig, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
    }
    config.effective_stack = stack.value();
    config.ante = ante.value();
    config.postflop_minimum_bet = minimum_bet.value();
    for (std::size_t index = 0; index < 2U; ++index) {
      const auto open = parse_money(open_targets[index]);
      const auto response = parse_money(response_targets[index]);
      if (!open || !response) {
        return Result<HuPreflopConfig, HuPreflopError>::failure(
            HuPreflopError::InvalidConfiguration);
      }
      config.open_targets[index] = open.value();
      config.response_targets[index] = response.value();
    }
    for (std::size_t index = 0; index < 3U; ++index) {
      const auto size = parse_percentage(postflop_sizes[index]);
      if (!size) {
        return Result<HuPreflopConfig, HuPreflopError>::failure(size.error());
      }
      config.postflop_sizes[index] = size.value();
    }
    config.include_all_in = root.at("include_all_in").get<bool>();
    config.allow_configured_incomplete_raise =
        root.at("allow_configured_incomplete_raise").get<bool>();
    if (version_two) {
      const auto &rake = root.at("rake");
      if (!rake.is_object() || !rake.contains("enabled") ||
          !rake.contains("percentage_bp") || !rake.contains("cap_units") ||
          !rake.contains("no_flop_no_drop") || !rake.contains("minimum_pot_units") ||
          !rake.at("enabled").is_boolean() ||
          !rake.at("no_flop_no_drop").is_boolean()) {
        return Result<HuPreflopConfig, HuPreflopError>::failure(
            HuPreflopError::InvalidConfiguration);
      }
      const auto percentage = parse_range_weight(rake.at("percentage_bp"));
      const auto cap = parse_money(rake.at("cap_units"));
      const auto minimum_pot = parse_money(rake.at("minimum_pot_units"));
      if (!percentage || !cap || !minimum_pot) {
        return Result<HuPreflopConfig, HuPreflopError>::failure(
            HuPreflopError::InvalidConfiguration);
      }
      config.rake.enabled = rake.at("enabled").get<bool>();
      config.rake.percentage = percentage.value();
      config.rake.cap = cap.value();
      config.rake.no_flop_no_drop = rake.at("no_flop_no_drop").get<bool>();
      config.rake.minimum_pot = minimum_pot.value();
    }
    const auto valid = validate_hu_preflop_config(config);
    return valid ? Result<HuPreflopConfig, HuPreflopError>::success(config)
                 : Result<HuPreflopConfig, HuPreflopError>::failure(valid.error());
  } catch (const std::exception &) {
    return Result<HuPreflopConfig, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
  }
}

} // namespace gtosd
