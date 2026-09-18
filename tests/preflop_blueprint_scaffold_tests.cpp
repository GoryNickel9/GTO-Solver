#include "gtosd/card_abstraction/card_abstraction.hpp"
#include "gtosd/preflop_blueprint/game_config.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

namespace pb = gtosd::preflop_blueprint;

std::uint64_t assertions = 0U;

void require(const bool condition, const std::string_view message) {
  ++assertions;
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

std::string read_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot open " + path.string());
  }
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::filesystem::path fixture_path(const std::string_view name) {
  return std::filesystem::path(GTOSD_SOURCE_DIR) / "benchmarks" / "fixtures" / std::string(name);
}

gtosd::Money antes(const std::int64_t value) {
  return gtosd::Money::from_antes(value).value();
}

gtosd::Money units(const std::int64_t value) { return gtosd::Money::from_units(value).value(); }

void test_card_abstraction_identity() {
  const auto identity = gtosd::card_abstraction::library_identity();
  require(identity.starts_with("gtosd.card_abstraction/0.1|short_deck_36|"),
          "card abstraction identity names library, version and deck");
  require(identity.find("short_deck_36_flush_over_full_house_a6789_exact_v1") != std::string::npos,
          "card abstraction identity embeds the exact ruleset fingerprint");
  require(gtosd::card_abstraction::deck_size == 36U &&
              gtosd::card_abstraction::hole_card_combos == 630U &&
              gtosd::card_abstraction::preflop_hand_classes == 81U,
          "card abstraction constants match the Short Deck");
}

pb::GameConfig load_fixture(const std::string_view name) {
  const auto parsed = pb::parse_game_config_json(read_file(fixture_path(name)));
  require(parsed.has_value(), std::string("fixture parses: ") + std::string(name));
  return parsed.value();
}

void test_fixtures() {
  const auto hu10_full = load_fixture("preflop_blueprint_hu10_full_v1.json");
  require(hu10_full.player_count == 2U && hu10_full.positions.size() == 2U &&
              hu10_full.positions[0] == "CO" && hu10_full.positions[1] == "BTN",
          "HU10 full: two positions, BTN last");
  require(hu10_full.effective_stack == antes(10) && hu10_full.ante == antes(1) &&
              hu10_full.button_blind == antes(1),
          "HU10 full: stack 10a, ante 1a, button blind 1a");
  require(hu10_full.open_targets.size() == 1U && hu10_full.open_targets[0] == antes(5) &&
              hu10_full.response_targets.empty(),
          "HU10 full: open 5a (full pot), re-raise only all-in");
  require(hu10_full.postflop_sizes.size() == 3U &&
              hu10_full.postflop_sizes[0].basis_points() == 3'300U &&
              hu10_full.postflop_sizes[1].basis_points() == 6'600U &&
              hu10_full.postflop_sizes[2].basis_points() == 12'000U,
          "HU10 full: postflop 33/66/120");
  require(hu10_full.allow_configured_incomplete_raise && hu10_full.include_all_in &&
              !hu10_full.rake.enabled && hu10_full.postflop_minimum_bet == antes(1),
          "HU10 full: flags and minimum bet");

  const auto hu10_reduced = load_fixture("preflop_blueprint_hu10_reduced_v1.json");
  require(hu10_reduced.postflop_sizes.size() == 1U &&
              hu10_reduced.postflop_sizes[0].basis_points() == 6'600U,
          "HU10 reduced: single postflop size 66%");
  auto reduced_as_full = hu10_reduced;
  reduced_as_full.id = hu10_full.id;
  reduced_as_full.postflop_sizes = hu10_full.postflop_sizes;
  require(reduced_as_full == hu10_full,
          "HU10 reduced differs from HU10 full only by id and postflop sizes");

  // Sizes converted to the exact pot-raise formula on 2026-09-17 by decision of
  // the user: one size per spot, so the Monker opens 6a/10a (both decided at the
  // root) collapse onto 5a and the response becomes 17a. The limped branch has
  // no configured re-raise.
  const auto co40 = load_fixture("preflop_blueprint_co40_v1.json");
  require(co40.effective_stack == antes(40) && co40.open_targets.size() == 1U &&
              co40.open_targets[0] == antes(5) && co40.response_targets.size() == 1U &&
              co40.response_targets[0] == antes(17) &&
              co40.limp_response_targets.has_value() &&
              co40.limp_response_targets.value().empty() &&
              co40.allow_configured_incomplete_raise,
          "CO40: stack 40a, open 5a, response 17a, no limped re-raise, incomplete raise allowed");

  const auto co40_test = load_fixture("preflop_blueprint_co40_test_v1.json");
  auto co40_test_as_main = co40_test;
  co40_test_as_main.id = co40.id;
  co40_test_as_main.postflop_sizes = co40.postflop_sizes;
  require(co40_test_as_main == co40,
          "the CO40 test variant differs from CO40 only by id and postflop sizes");

  // A configuration without the optional field keeps the historical behaviour
  // and, crucially, its serialization: HU10 artifacts stay valid.
  require(!hu10_full.limp_response_targets.has_value() &&
              pb::serialize_game_config_json(hu10_full).find("limp_response_target_units") ==
                  std::string::npos,
          "HU10 does not carry the optional limped-response field");

  require(pb::game_config_fingerprint(hu10_full) != pb::game_config_fingerprint(hu10_reduced) &&
              pb::game_config_fingerprint(hu10_full) != pb::game_config_fingerprint(co40),
          "fingerprints separate the three fixtures");
  require(pb::game_config_fingerprint(co40) == pb::game_config_fingerprint(co40) &&
              pb::game_config_fingerprint(co40).starts_with("fnv1a64:") &&
              pb::game_config_fingerprint(co40).size() == 24U,
          "fingerprint is stable and well formed");
}

void test_round_trip() {
  for (const std::string_view name :
       {"preflop_blueprint_hu10_full_v1.json", "preflop_blueprint_hu10_reduced_v1.json",
        "preflop_blueprint_co40_v1.json"}) {
    const auto original = load_fixture(name);
    const auto serialized = pb::serialize_game_config_json(original);
    const auto reparsed = pb::parse_game_config_json(serialized);
    require(reparsed.has_value(), "serialized configuration parses again");
    require(reparsed.value() == original, "round trip preserves the configuration");
    require(pb::game_config_fingerprint(reparsed.value()) == pb::game_config_fingerprint(original),
            "round trip preserves the fingerprint");
    require(serialized.find("\"schema\": \"gtosd.preflop_blueprint_game.v1\"") != std::string::npos,
            "canonical serialization declares the schema identifier");
  }
}

void expect_rejection(const std::string &json, const pb::ConfigError expected,
                      const std::string_view message) {
  const auto parsed = pb::parse_game_config_json(json);
  require(!parsed.has_value(), std::string("rejected: ") + std::string(message));
  require(parsed.error() == expected,
          std::string("error code for ") + std::string(message) + " is " +
              pb::config_error_name(expected) + ", got " + pb::config_error_name(parsed.error()));
}

std::string replace_first(std::string text, const std::string_view from,
                          const std::string_view to) {
  const auto position = text.find(from);
  if (position == std::string::npos) {
    throw std::runtime_error("test replacement target missing: " + std::string(from));
  }
  return text.replace(position, from.size(), to);
}

void test_rejections() {
  const auto valid = read_file(fixture_path("preflop_blueprint_hu10_full_v1.json"));
  const auto co40 = read_file(fixture_path("preflop_blueprint_co40_v1.json"));
  expect_rejection("not json", pb::ConfigError::InvalidJson, "invalid json");
  expect_rejection("[]", pb::ConfigError::InvalidJson, "non-object root");
  expect_rejection(replace_first(valid, "gtosd.preflop_blueprint_game.v1", "gtosd.hu_preflop_game.v1"),
                   pb::ConfigError::UnsupportedSchema, "legacy schema identifier");
  expect_rejection(replace_first(valid, "\"player_count\": 2,", ""), pb::ConfigError::MissingField,
                   "missing player count");
  expect_rejection(replace_first(valid, "\"player_count\": 2", "\"player_count\": 1"),
                   pb::ConfigError::InvalidStructure, "single player");
  expect_rejection(replace_first(valid, "\"player_count\": 2", "\"player_count\": 7"),
                   pb::ConfigError::InvalidStructure, "seven players");
  expect_rejection(replace_first(valid, "\"player_count\": 2", "\"player_count\": 3"),
                   pb::ConfigError::InvalidStructure, "player count without matching positions");
  expect_rejection(replace_first(valid, "[\"CO\", \"BTN\"]", "[\"BTN\", \"CO\"]"),
                   pb::ConfigError::InvalidStructure, "BTN not last");
  expect_rejection(replace_first(valid, "[\"CO\", \"BTN\"]", "[\"BTN\", \"BTN\"]"),
                   pb::ConfigError::InvalidStructure, "duplicate positions");
  // CO40 carries one open (50000) and one response (170000) since the sizes
  // moved to the exact pot-raise formula, so the list-shape rejections are
  // written against those two lists.
  expect_rejection(replace_first(co40, "[170000]", "[170000, 200000]"),
                   pb::ConfigError::InvalidStructure, "response count mismatch");
  const auto only_all_in = pb::parse_game_config_json(replace_first(co40, "[170000]", "[]"));
  require(only_all_in.has_value() && only_all_in.value().response_targets.empty(),
          "empty response list accepted: re-raise only all-in");
  // Two opens need two responses, otherwise the shape check fires first and the
  // ordering rule is never reached.
  expect_rejection(replace_first(replace_first(co40, "\"open_target_units\": [50000]",
                                              "\"open_target_units\": [100000, 60000]"),
                                 "\"response_target_units\": [170000]",
                                 "\"response_target_units\": [170000, 200000]"),
                   pb::ConfigError::InvalidValue, "open targets not increasing");
  expect_rejection(replace_first(co40, "[170000]", "[50000]"),
                   pb::ConfigError::InvalidValue, "response not above its open");
  expect_rejection(replace_first(valid, "[50000]", "[10000]"),
                   pb::ConfigError::InvalidValue, "open target not above the button blind");
  expect_rejection(replace_first(co40, "[170000]", "[400000]"),
                   pb::ConfigError::InvalidValue, "response reaching the stack");
  // The optional limped-response list obeys the same shape rules.
  expect_rejection(replace_first(co40, "\"limp_response_target_units\": []",
                                 "\"limp_response_target_units\": [170000, 200000]"),
                   pb::ConfigError::InvalidStructure, "limped response count mismatch");
  expect_rejection(replace_first(co40, "\"limp_response_target_units\": []",
                                 "\"limp_response_target_units\": [50000]"),
                   pb::ConfigError::InvalidValue, "limped response not above its open");
  expect_rejection(replace_first(co40, "\"limp_response_target_units\": []",
                                 "\"limp_response_target_units\": [400000]"),
                   pb::ConfigError::InvalidValue, "limped response reaching the stack");
  const auto limped_configured = pb::parse_game_config_json(
      replace_first(co40, "\"limp_response_target_units\": []",
                    "\"limp_response_target_units\": [200000]"));
  require(limped_configured.has_value() &&
              limped_configured.value().limp_response_targets.has_value() &&
              limped_configured.value().limp_response_targets.value().size() == 1U,
          "a non-empty limped response list is accepted and kept apart from the open branch");
  expect_rejection(replace_first(valid, "[3300, 6600, 12000]", "[]"),
                   pb::ConfigError::InvalidStructure, "no postflop sizes");
  expect_rejection(replace_first(valid, "[3300, 6600, 12000]", "[3300, 6600, 12000, 20000]"),
                   pb::ConfigError::InvalidStructure, "four postflop sizes");
  expect_rejection(replace_first(valid, "[3300, 6600, 12000]", "[6600, 3300, 12000]"),
                   pb::ConfigError::InvalidValue, "postflop sizes not increasing");
  expect_rejection(replace_first(valid, "\"include_all_in\": true", "\"include_all_in\": false"),
                   pb::ConfigError::InvalidValue, "all-in disabled");
  expect_rejection(replace_first(valid, "\"rake_mode\": \"disabled\"", "\"rake_mode\": \"enabled\""),
                   pb::ConfigError::InvalidValue, "rake enabled");
  expect_rejection(replace_first(valid, "\"monetary_contract_revision\": 2",
                                 "\"monetary_contract_revision\": 1"),
                   pb::ConfigError::InvalidValue, "wrong monetary contract revision");
  expect_rejection(replace_first(valid, "\"effective_stack_units\": 100000",
                                 "\"effective_stack_units\": \"100000\""),
                   pb::ConfigError::InvalidValue, "stack as string");
}

} // namespace

int main() {
  try {
    test_card_abstraction_identity();
    test_fixtures();
    test_round_trip();
    test_rejections();
  } catch (const std::exception &error) {
    std::cerr << "PREFLOP_BLUEPRINT_SCAFFOLD=FAIL " << error.what() << '\n';
    return 1;
  }
  std::cout << "PREFLOP_BLUEPRINT_SCAFFOLD=PASS assertions=" << assertions << '\n';
  return 0;
}
