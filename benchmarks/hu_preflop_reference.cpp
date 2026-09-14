#include "gtosd/core/game.hpp"
#include "gtosd/core/ranges.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Json = nlohmann::json;

constexpr std::array<std::string_view, 5> action_ids{"all_in", "raise_6", "raise_10", "call",
                                                     "fold"};

[[noreturn]] void fail(const std::string &message) { throw std::runtime_error(message); }

std::string read_text(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    fail("cannot open fixture: " + path.string());
  }
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::string_view trim(std::string_view value) {
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
    value.remove_prefix(1U);
  }
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
    value.remove_suffix(1U);
  }
  return value;
}

std::vector<std::string_view> split(const std::string_view value, const char separator) {
  std::vector<std::string_view> parts;
  std::size_t begin = 0U;
  while (begin <= value.size()) {
    const auto end = value.find(separator, begin);
    parts.push_back(
        value.substr(begin, end == std::string_view::npos ? value.size() - begin : end - begin));
    if (end == std::string_view::npos) {
      break;
    }
    begin = end + 1U;
  }
  return parts;
}

std::map<std::string, std::uint8_t> class_ids_by_name() {
  std::map<std::string, std::uint8_t> result;
  for (std::uint8_t id = 0U; id < 81U; ++id) {
    result.emplace(gtosd::class_name(id), id);
  }
  return result;
}

double parse_percentage(const std::string_view text) {
  std::size_t consumed = 0U;
  const auto owned = std::string(text);
  const double value = std::stod(owned, &consumed);
  if (consumed != owned.size() || !std::isfinite(value) || value < 0.0 || value > 100.0) {
    fail("invalid percentage: " + owned);
  }
  return value;
}

std::pair<std::string, double> parse_range_token(const std::string_view raw_token) {
  const auto token = trim(raw_token);
  if (token.empty()) {
    fail("empty strategy token");
  }
  if (token.front() != '[') {
    return {std::string(token), 100.0};
  }
  const auto opening_end = token.find(']');
  if (opening_end == std::string_view::npos || opening_end == 1U) {
    fail("invalid opening weight tag: " + std::string(token));
  }
  const auto closing_begin = token.find("[/", opening_end + 1U);
  if (closing_begin == std::string_view::npos || token.back() != ']') {
    fail("missing closing weight tag: " + std::string(token));
  }
  const auto opening_text = token.substr(1U, opening_end - 1U);
  const auto class_name = token.substr(opening_end + 1U, closing_begin - opening_end - 1U);
  const auto closing_text = token.substr(closing_begin + 2U, token.size() - closing_begin - 3U);
  if (opening_text != closing_text || class_name.empty()) {
    fail("mismatched weight tag: " + std::string(token));
  }
  return {std::string(class_name), parse_percentage(opening_text)};
}

struct ReferenceProfile {
  std::array<std::array<double, action_ids.size()>, 81> raw_percentages{};
  std::array<std::array<double, action_ids.size()>, 81> normalized_probabilities{};
  std::array<double, action_ids.size()> root_action_probabilities{};
  std::vector<std::string> rounded_rows;
};

struct CandidateProfile {
  std::array<std::array<double, action_ids.size()>, 81> probabilities{};
  std::array<double, action_ids.size()> root_action_probabilities{};
  double root_ev_ante{0.0};
  double normalized_nashconv{0.0};
  bool nashconv_certified{false};
  std::string rake_mode;
  std::string tree_fingerprint;
};

struct ComparisonMetrics {
  double weighted_mean_absolute_action_error_pp{0.0};
  double weighted_mean_class_total_variation_pp{0.0};
  double root_action_max_absolute_error_pp{0.0};
  double class_total_variation_p95_pp{0.0};
  double root_ev_absolute_error_ante{0.0};
  bool passes_rake{false};
  bool passes_strategy{false};
  bool passes_ev{false};
  bool passes_nashconv{false};
  bool qualifies{false};
};

struct PreflopChanceLowerBound {
  std::uint64_t ordered_private_deals{0U};
  std::uint64_t private_flop_outcomes{0U};
  std::uint64_t private_complete_board_outcomes{0U};
  std::uint64_t minimum_flop_orbits_under_global_suits{0U};
  std::uint64_t minimum_complete_board_orbits_under_global_suits{0U};
};

constexpr std::uint64_t choose(std::uint64_t count, std::uint64_t selected) {
  if (selected > count) {
    return 0U;
  }
  if (selected > count - selected) {
    selected = count - selected;
  }
  std::uint64_t result = 1U;
  for (std::uint64_t index = 1U; index <= selected; ++index) {
    result = result * (count - selected + index) / index;
  }
  return result;
}

PreflopChanceLowerBound calculate_chance_lower_bound() {
  const auto combos = gtosd::all_combos();
  PreflopChanceLowerBound result;
  for (const auto first : combos) {
    for (const auto second : combos) {
      const bool collide = first.first == second.first || first.first == second.second ||
                           first.second == second.first || first.second == second.second;
      if (!collide) {
        ++result.ordered_private_deals;
      }
    }
  }
  constexpr std::uint64_t flops_after_two_private_hands = choose(32U, 3U);
  constexpr std::uint64_t boards_after_two_private_hands = choose(32U, 5U);
  constexpr std::uint64_t global_suit_permutations = 24U;
  result.private_flop_outcomes = result.ordered_private_deals * flops_after_two_private_hands;
  result.private_complete_board_outcomes =
      result.ordered_private_deals * boards_after_two_private_hands;
  result.minimum_flop_orbits_under_global_suits =
      (result.private_flop_outcomes + global_suit_permutations - 1U) / global_suit_permutations;
  result.minimum_complete_board_orbits_under_global_suits =
      (result.private_complete_board_outcomes + global_suit_permutations - 1U) /
      global_suit_permutations;
  if (result.ordered_private_deals != 353'430U || flops_after_two_private_hands != 4'960U ||
      boards_after_two_private_hands != 201'376U) {
    fail("HU preflop physical chance counts changed");
  }
  return result;
}

ReferenceProfile parse_reference_profile(const Json &fixture) {
  const auto names = class_ids_by_name();
  const auto &strategy = fixture.at("reference").at("strategy");
  ReferenceProfile profile;
  std::array<std::array<bool, action_ids.size()>, 81> seen{};
  for (std::size_t action = 0U; action < action_ids.size(); ++action) {
    const auto text = strategy.at(std::string(action_ids[action])).get<std::string>();
    for (const auto token : split(text, ',')) {
      const auto [name, percentage] = parse_range_token(token);
      const auto found = names.find(name);
      if (found == names.end()) {
        fail("unknown Short Deck class: " + name);
      }
      const auto class_id = static_cast<std::size_t>(found->second);
      if (seen[class_id][action]) {
        fail("duplicate class in action " + std::string(action_ids[action]) + ": " + name);
      }
      seen[class_id][action] = true;
      profile.raw_percentages[class_id][action] = percentage;
    }
  }

  std::uint64_t physical_mass = 0U;
  for (std::uint8_t class_id = 0U; class_id < 81U; ++class_id) {
    double total = 0.0;
    for (const auto percentage : profile.raw_percentages[class_id]) {
      total += percentage;
    }
    if (std::abs(total - 100.0) > 1.0e-9 && std::abs(total - 101.0) > 1.0e-9) {
      std::ostringstream message;
      message << "class " << gtosd::class_name(class_id) << " totals " << total
              << " percent; expected source rounding total 100 or 101";
      fail(message.str());
    }
    if (std::abs(total - 101.0) <= 1.0e-9) {
      profile.rounded_rows.push_back(gtosd::class_name(class_id));
    }
    const auto mass = static_cast<std::uint64_t>(gtosd::class_mass(class_id));
    physical_mass += mass;
    for (std::size_t action = 0U; action < action_ids.size(); ++action) {
      const double probability = profile.raw_percentages[class_id][action] / total;
      profile.normalized_probabilities[class_id][action] = probability;
      profile.root_action_probabilities[action] += static_cast<double>(mass) * probability;
    }
  }
  if (physical_mass != 630U) {
    fail("Short Deck class masses do not sum to 630 physical combos");
  }
  for (auto &probability : profile.root_action_probabilities) {
    probability /= static_cast<double>(physical_mass);
  }
  return profile;
}

CandidateProfile parse_candidate_profile(const Json &candidate, const std::string &benchmark_id) {
  if (candidate.at("schema") != "gtosd.hu_preflop_candidate.v1" ||
      candidate.at("benchmark_id") != benchmark_id) {
    fail("candidate schema or benchmark id does not match");
  }
  CandidateProfile profile;
  profile.root_ev_ante = candidate.at("root_ev_ante").get<double>();
  profile.normalized_nashconv = candidate.at("normalized_nashconv").get<double>();
  profile.nashconv_certified = candidate.at("nashconv_certified").get<bool>();
  profile.rake_mode = candidate.at("rake_mode").get<std::string>();
  profile.tree_fingerprint = candidate.at("tree_fingerprint").get<std::string>();
  if (!std::isfinite(profile.root_ev_ante) || !std::isfinite(profile.normalized_nashconv) ||
      profile.normalized_nashconv < 0.0 || profile.rake_mode.empty() ||
      profile.tree_fingerprint.empty()) {
    fail("candidate metadata is incomplete or non-finite");
  }

  const auto &strategy = candidate.at("strategy");
  if (!strategy.is_object() || strategy.size() != 81U) {
    fail("candidate strategy must contain exactly 81 class rows");
  }
  const auto names = class_ids_by_name();
  std::array<bool, 81> seen{};
  for (const auto &[name, row] : strategy.items()) {
    const auto found = names.find(name);
    if (found == names.end() || !row.is_object() || row.size() != action_ids.size()) {
      fail("candidate contains an invalid class row: " + name);
    }
    const auto class_id = static_cast<std::size_t>(found->second);
    if (seen[class_id]) {
      fail("candidate class row is duplicated: " + name);
    }
    seen[class_id] = true;
    double total = 0.0;
    for (std::size_t action = 0U; action < action_ids.size(); ++action) {
      const double probability = row.at(std::string(action_ids[action])).get<double>();
      if (!std::isfinite(probability) || probability < 0.0 || probability > 1.0) {
        fail("candidate probability is outside [0,1]: " + name);
      }
      profile.probabilities[class_id][action] = probability;
      total += probability;
    }
    if (std::abs(total - 1.0) > 1.0e-9) {
      fail("candidate class probabilities do not sum to one: " + name);
    }
  }

  constexpr double physical_combos = 630.0;
  for (std::uint8_t class_id = 0U; class_id < 81U; ++class_id) {
    if (!seen[class_id]) {
      fail("candidate class row is missing: " + gtosd::class_name(class_id));
    }
    const double mass = static_cast<double>(gtosd::class_mass(class_id));
    for (std::size_t action = 0U; action < action_ids.size(); ++action) {
      profile.root_action_probabilities[action] +=
          mass * profile.probabilities[class_id][action] / physical_combos;
    }
  }
  return profile;
}

ComparisonMetrics compare_profiles(const Json &fixture, const ReferenceProfile &reference,
                                   const CandidateProfile &candidate) {
  double weighted_absolute_error = 0.0;
  double weighted_total_variation = 0.0;
  std::vector<double> physical_total_variations;
  physical_total_variations.reserve(630U);
  for (std::uint8_t class_id = 0U; class_id < 81U; ++class_id) {
    double class_l1 = 0.0;
    for (std::size_t action = 0U; action < action_ids.size(); ++action) {
      class_l1 += std::abs(candidate.probabilities[class_id][action] -
                           reference.normalized_probabilities[class_id][action]);
    }
    const double class_total_variation_pp = class_l1 * 50.0;
    const auto mass = static_cast<std::uint64_t>(gtosd::class_mass(class_id));
    weighted_absolute_error += static_cast<double>(mass) * class_l1 * 100.0;
    weighted_total_variation += static_cast<double>(mass) * class_total_variation_pp;
    for (std::uint64_t combo = 0U; combo < mass; ++combo) {
      physical_total_variations.push_back(class_total_variation_pp);
    }
  }

  ComparisonMetrics metrics;
  metrics.weighted_mean_absolute_action_error_pp =
      weighted_absolute_error / (630.0 * static_cast<double>(action_ids.size()));
  metrics.weighted_mean_class_total_variation_pp = weighted_total_variation / 630.0;
  for (std::size_t action = 0U; action < action_ids.size(); ++action) {
    metrics.root_action_max_absolute_error_pp =
        std::max(metrics.root_action_max_absolute_error_pp,
                 std::abs(candidate.root_action_probabilities[action] -
                          reference.root_action_probabilities[action]) *
                     100.0);
  }
  std::ranges::sort(physical_total_variations);
  const auto p95_index = (physical_total_variations.size() * 95U + 99U) / 100U - 1U;
  metrics.class_total_variation_p95_pp = physical_total_variations[p95_index];
  metrics.root_ev_absolute_error_ante =
      std::abs(candidate.root_ev_ante - fixture.at("reference").at("root_ev_ante").get<double>());
  metrics.passes_rake = candidate.rake_mode == "disabled";

  const auto &limits = fixture.at("acceptance");
  metrics.passes_strategy =
      metrics.weighted_mean_absolute_action_error_pp <=
          limits.at("weighted_mean_absolute_action_error_percentage_points_max").get<double>() &&
      metrics.weighted_mean_class_total_variation_pp <=
          limits.at("weighted_mean_class_total_variation_percentage_points_max").get<double>() &&
      metrics.root_action_max_absolute_error_pp <=
          limits.at("root_action_frequency_max_absolute_error_percentage_points_max")
              .get<double>() &&
      metrics.class_total_variation_p95_pp <=
          limits.at("class_total_variation_p95_percentage_points_max").get<double>();
  metrics.passes_ev = metrics.root_ev_absolute_error_ante <=
                      limits.at("root_ev_absolute_error_ante_max").get<double>();
  metrics.passes_nashconv =
      candidate.nashconv_certified &&
      candidate.normalized_nashconv <= limits.at("candidate_normalized_nashconv_max").get<double>();
  metrics.qualifies = metrics.passes_rake && metrics.passes_strategy && metrics.passes_ev &&
                      metrics.passes_nashconv;
  return metrics;
}

Json self_candidate(const Json &fixture, const ReferenceProfile &reference) {
  Json strategy = Json::object();
  for (std::uint8_t class_id = 0U; class_id < 81U; ++class_id) {
    Json row = Json::object();
    for (std::size_t action = 0U; action < action_ids.size(); ++action) {
      row[std::string(action_ids[action])] = reference.normalized_probabilities[class_id][action];
    }
    strategy[gtosd::class_name(class_id)] = std::move(row);
  }
  return {{"schema", "gtosd.hu_preflop_candidate.v1"},
          {"benchmark_id", fixture.at("id")},
          {"tree_fingerprint", "self-check"},
          {"rake_mode", "disabled"},
          {"root_ev_ante", fixture.at("reference").at("root_ev_ante")},
          {"normalized_nashconv", 0.0},
          {"nashconv_certified", true},
          {"strategy", std::move(strategy)}};
}

void write_report(const std::filesystem::path &path, const Json &report) {
  auto temporary = path;
  temporary += ".tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
      fail("cannot create comparison report: " + temporary.string());
    }
    output << report.dump(2) << '\n';
    if (!output) {
      fail("cannot write comparison report: " + temporary.string());
    }
  }
  std::error_code error;
  std::filesystem::remove(path, error);
  error.clear();
  std::filesystem::rename(temporary, path, error);
  if (error) {
    fail("cannot replace comparison report: " + error.message());
  }
}

std::string action_type_name(const gtosd::ActionType type) {
  switch (type) {
  case gtosd::ActionType::Fold:
    return "fold";
  case gtosd::ActionType::Call:
    return "call";
  case gtosd::ActionType::Raise:
    return "raise";
  case gtosd::ActionType::AllIn:
    return "all_in";
  case gtosd::ActionType::Check:
    return "check";
  case gtosd::ActionType::Bet:
    return "bet";
  }
  fail("unknown action type");
}

void validate_root_actions(const Json &fixture) {
  const auto &game = fixture.at("game");
  const auto &components = game.at("forced_contribution_components_ante");
  const auto co_ante = components.at("CO").at("ante").get<std::int64_t>();
  const auto co_button_blind = components.at("CO").at("button_blind").get<std::int64_t>();
  const auto co_total = components.at("CO").at("total").get<std::int64_t>();
  const auto btn_ante = components.at("BTN").at("ante").get<std::int64_t>();
  const auto btn_button_blind = components.at("BTN").at("button_blind").get<std::int64_t>();
  const auto btn_total = components.at("BTN").at("total").get<std::int64_t>();
  if (game.at("monetary_contract_revision") != 2 ||
      game.at("ante_accounting") != "dead_initial_pot_contribution" ||
      game.at("preflop_target_basis") != "live_commitment_excluding_dead_ante" ||
      co_ante != 1 || co_button_blind != 0 || co_total != co_ante + co_button_blind ||
      btn_ante != 1 || btn_button_blind != 1 || btn_total != btn_ante + btn_button_blind ||
      co_total != game.at("forced_contributions_ante").at("CO").get<std::int64_t>() ||
      btn_total != game.at("forced_contributions_ante").at("BTN").get<std::int64_t>() ||
      game.at("root_amount_to_call_ante").get<std::int64_t>() != btn_total - co_total) {
    fail("forced contribution components or root call amount are inconsistent");
  }
  const auto stack = gtosd::Money::from_antes(game.at("effective_stack_ante").get<int>());
  const auto ante = gtosd::Money::from_antes(1);
  if (!stack || !ante) {
    fail("invalid stack or ante");
  }
  const auto state = gtosd::make_hu_preflop_state(stack.value(), ante.value());
  if (!state) {
    fail("cannot build HU preflop root");
  }
  if (state.value().initial_pot != gtosd::Money::from_antes(2).value() ||
      state.value().initial_pot_contributions[0] != ante.value() ||
      state.value().initial_pot_contributions[1] != ante.value() ||
      state.value().committed_this_street[0].units() != 0 ||
      state.value().committed_this_street[1] != ante.value()) {
    fail("HU preflop root does not separate dead antes from the live button blind");
  }
  gtosd::ActionConfig config;
  config.aggressive_targets = {gtosd::Money::from_antes(6).value(),
                               gtosd::Money::from_antes(10).value()};
  config.raise_depth = 4U;
  config.all_in_mode = gtosd::AllInMode::Add;
  config.all_in_threshold = gtosd::PotPercentage::from_basis_points(100'000).value();
  config.minimum_bet = ante.value();
  const auto legal = gtosd::legal_actions(state.value(), config);
  if (!legal || legal.value().size() != action_ids.size()) {
    fail("root action catalog does not produce the five reference actions");
  }

  struct ExpectedAction {
    std::string type;
    std::int64_t live_commitment;
    std::int64_t total_contribution;
  };
  std::map<std::string, ExpectedAction> expected;
  for (const auto &entry : fixture.at("game").at("root_actions")) {
    if (entry.at("id") == "call" &&
        (entry.at("incremental_amount_ante").get<std::int64_t>() !=
             game.at("root_amount_to_call_ante").get<std::int64_t>() ||
         entry.at("target_live_commitment_ante").get<std::int64_t>() != btn_button_blind ||
         entry.at("target_total_contribution_ante").get<std::int64_t>() != co_ante +
                                                                                 btn_button_blind)) {
      fail("root call must pay one ante to match the BTN live button blind");
    }
    const auto live = entry.at("target_live_commitment_ante").get<std::int64_t>();
    const auto total = entry.at("target_total_contribution_ante").get<std::int64_t>();
    if (total != co_ante + live) {
      fail("root action total contribution must include the dead CO ante exactly once");
    }
    expected.emplace(entry.at("id").get<std::string>(),
                     ExpectedAction{entry.at("type").get<std::string>(), live, total});
  }
  if (expected.size() != action_ids.size()) {
    fail("fixture root action catalog must contain five unique actions");
  }
  for (const auto &action : legal.value()) {
    const auto live_units = state.value().committed_this_street[0].units() + action.amount.units();
    if (live_units % gtosd::Money::units_per_ante != 0) {
      fail("root action is not an integral ante amount");
    }
    const auto live_ante = live_units / gtosd::Money::units_per_ante;
    const auto total_ante = co_ante + live_ante;
    std::string id;
    if (action.type == gtosd::ActionType::Raise) {
      id = live_ante == 6 ? "raise_6" : live_ante == 10 ? "raise_10" : "unknown_raise";
    } else {
      id = action_type_name(action.type);
    }
    const auto found = expected.find(id);
    if (found == expected.end() || found->second.type != action_type_name(action.type) ||
        found->second.live_commitment != live_ante ||
        found->second.total_contribution != total_ante) {
      fail("generated root action does not match fixture: " + id);
    }
    if (id == "call" && action.amount != ante.value()) {
      fail("generated root call must cost one ante");
    }
  }
}

std::uint64_t fingerprint(const std::string_view value) {
  std::uint64_t hash = 14'695'981'039'346'656'037ULL;
  for (const auto character : value) {
    hash ^= static_cast<std::uint8_t>(character);
    hash *= 1'099'511'628'211ULL;
  }
  return hash;
}

int run(const int argc, char **argv) {
  const bool preflight = argc == 4 && std::string_view(argv[1]) == "--fixture" &&
                         std::string_view(argv[3]) == "--preflight-only";
  const bool comparison = argc == 7 && std::string_view(argv[1]) == "--fixture" &&
                          std::string_view(argv[3]) == "--candidate" &&
                          std::string_view(argv[5]) == "--output";
  if (!preflight && !comparison) {
    std::cerr << "usage: gtosd_hu_preflop_reference --fixture FILE --preflight-only\n"
                 "   or: gtosd_hu_preflop_reference --fixture FILE --candidate FILE --output "
                 "FILE\n";
    return 3;
  }
  const auto text = read_text(argv[2]);
  const auto fixture = Json::parse(text);
  if (fixture.at("schema") != "gtosd.hu_preflop_reference.v1" ||
      fixture.at("id") != "GTP-HU-PREFLOP-CO40-001" ||
      fixture.at("game").at("root_player") != "CO" ||
      fixture.at("game").at("external_preflop_tree_status") != "user_confirmed_match_2026-09-10" ||
      fixture.at("game").at("external_postflop_tree_status") != "awaiting_information_2026-09-10" ||
      fixture.at("game").at("current_local_tree_fingerprint") !=
          "fnv1a64:a68337fa567aa2d9" ||
      fixture.at("game").at("rake").at("status") != "user_confirmed_zero_2026-09-07" ||
      fixture.at("game").at("rake").at("enabled").get<bool>() ||
      fixture.at("game").at("rake").at("percentage_basis_points").get<int>() != 0 ||
      fixture.at("game").at("rake").at("cap_ante").get<int>() != 0 ||
      fixture.at("reference").at("root_ev_ante").get<double>() != -0.3) {
    fail("fixture identity or fixed root contract is invalid");
  }
  validate_root_actions(fixture);
  const auto profile = parse_reference_profile(fixture);
  const auto chance = calculate_chance_lower_bound();
  const auto self = parse_candidate_profile(self_candidate(fixture, profile),
                                            fixture.at("id").get<std::string>());
  const auto self_metrics = compare_profiles(fixture, profile, self);
  if (!self_metrics.qualifies || self_metrics.weighted_mean_absolute_action_error_pp != 0.0 ||
      self_metrics.root_ev_absolute_error_ante != 0.0) {
    fail("reference self-comparison is not exact");
  }
  auto rake_mismatch_json = self_candidate(fixture, profile);
  rake_mismatch_json["rake_mode"] = "enabled_test_mismatch";
  const auto rake_mismatch =
      parse_candidate_profile(rake_mismatch_json, fixture.at("id").get<std::string>());
  const auto rake_mismatch_metrics = compare_profiles(fixture, profile, rake_mismatch);
  if (rake_mismatch_metrics.passes_rake || rake_mismatch_metrics.qualifies ||
      !rake_mismatch_metrics.passes_strategy || !rake_mismatch_metrics.passes_ev ||
      !rake_mismatch_metrics.passes_nashconv) {
    fail("non-zero rake candidate is not isolated by the rake gate");
  }

  if (comparison) {
    const auto candidate_text = read_text(argv[4]);
    const auto candidate_json = Json::parse(candidate_text);
    const auto candidate =
        parse_candidate_profile(candidate_json, fixture.at("id").get<std::string>());
    const auto metrics = compare_profiles(fixture, profile, candidate);
    const bool current_tree =
        candidate.tree_fingerprint ==
        fixture.at("game").at("current_local_tree_fingerprint").get<std::string>();
    Json root_action_deltas = Json::object();
    for (std::size_t action = 0U; action < action_ids.size(); ++action) {
      root_action_deltas[std::string(action_ids[action])] =
          (candidate.root_action_probabilities[action] -
           profile.root_action_probabilities[action]) *
          100.0;
    }
    const Json report{
        {"schema", "gtosd.hu_preflop_comparison_report.v1"},
        {"benchmark_id", fixture.at("id")},
        {"reference_fingerprint", "fnv1a64:" +
                                      [&text] {
                                        std::ostringstream value;
                                        value << std::hex << fingerprint(text);
                                        return value.str();
                                      }()},
        {"candidate_tree_fingerprint", candidate.tree_fingerprint},
        {"candidate_rake_mode", candidate.rake_mode},
        {"configuration_comparability", "REFERENCE_CONFIG_INCOMPLETE"},
        {"metrics",
         {{"weighted_mean_absolute_action_error_percentage_points",
           metrics.weighted_mean_absolute_action_error_pp},
          {"weighted_mean_class_total_variation_percentage_points",
           metrics.weighted_mean_class_total_variation_pp},
          {"root_action_max_absolute_error_percentage_points",
           metrics.root_action_max_absolute_error_pp},
          {"class_total_variation_p95_percentage_points", metrics.class_total_variation_p95_pp},
          {"root_ev_absolute_error_ante", metrics.root_ev_absolute_error_ante},
          {"normalized_nashconv", candidate.normalized_nashconv},
          {"nashconv_certified", candidate.nashconv_certified},
          {"root_action_delta_percentage_points", std::move(root_action_deltas)}}},
        {"gates",
         {{"tree_fingerprint", current_tree},
          {"rake", metrics.passes_rake},
          {"strategy", metrics.passes_strategy},
          {"root_ev", metrics.passes_ev},
          {"nashconv", metrics.passes_nashconv}}},
        {"status", !current_tree ? "STALE_TREE" : metrics.qualifies ? "QUALIFIED" : "REJECTED"}};
    write_report(argv[6], report);
    std::cout << "HU_PREFLOP_REFERENCE_COMPARISON="
              << (!current_tree ? "STALE_TREE" : metrics.qualifies ? "QUALIFIED" : "REJECTED")
              << " configuration_comparability=REFERENCE_CONFIG_INCOMPLETE"
              << " report=" << argv[6] << '\n';
    return current_tree && metrics.qualifies ? 0 : 2;
  }

  std::ostringstream rounded;
  for (std::size_t index = 0U; index < profile.rounded_rows.size(); ++index) {
    rounded << (index == 0U ? "" : ",") << profile.rounded_rows[index];
  }
  std::cout << "HU_PREFLOP_REFERENCE_PREFLIGHT=PASS"
            << " id=" << fixture.at("id").get<std::string>()
            << " classes=81 physical_combos=630 actions=5 root_ev_ante=-0.3"
            << " rake=DISABLED_USER_CONFIRMED rounded_101_rows=" << profile.rounded_rows.size()
            << " rounded_classes=" << rounded.str() << " fingerprint=fnv1a64:" << std::hex
            << fingerprint(text) << std::dec << '\n';
  std::cout << "HU_PREFLOP_REFERENCE_ROOT_MARGINALS";
  for (std::size_t action = 0U; action < action_ids.size(); ++action) {
    std::cout << ' ' << action_ids[action] << '='
              << profile.root_action_probabilities[action] * 100.0;
  }
  std::cout << '\n';
  std::cout << "HU_PREFLOP_CHANCE_LOWER_BOUND"
            << " ordered_private_deals=" << chance.ordered_private_deals
            << " private_flop_outcomes=" << chance.private_flop_outcomes
            << " private_complete_board_outcomes=" << chance.private_complete_board_outcomes
            << " minimum_flop_orbits_under_24_suits="
            << chance.minimum_flop_orbits_under_global_suits
            << " minimum_complete_board_orbits_under_24_suits="
            << chance.minimum_complete_board_orbits_under_global_suits << '\n';
  return 0;
}

} // namespace

int main(const int argc, char **argv) {
  try {
    return run(argc, argv);
  } catch (const std::exception &error) {
    std::cerr << "HU_PREFLOP_REFERENCE_ERROR=" << error.what() << '\n';
    return 3;
  }
}
