#include "gtosd/preflop/hu_preflop.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <new>
#include <sstream>
#include <string_view>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace gtosd {
namespace {

using Json = nlohmann::json;

std::string checksum(const std::string_view bytes) {
  std::uint64_t hash = 14'695'981'039'346'656'037ULL;
  for (const unsigned char byte : bytes) {
    hash ^= byte;
    hash *= 1'099'511'628'211ULL;
  }
  std::ostringstream output;
  output << std::hex << std::setw(16) << std::setfill('0') << hash;
  return output.str();
}

std::uint64_t whole_game_coverage_slot_hash(const std::uint64_t task, const std::uint8_t mask) {
  auto value = task ^ (static_cast<std::uint64_t>(mask) << 61U) ^ 0x9E37'79B9'7F4A'7C15ULL;
  value = (value ^ (value >> 30U)) * 0xBF58'476D'1CE4'E5B9ULL;
  value = (value ^ (value >> 27U)) * 0x94D0'49BB'1331'11EBULL;
  return value ^ (value >> 31U);
}

std::uint64_t whole_game_coverage_state_hash(const std::vector<std::uint8_t> &coverage) {
  std::uint64_t result = 0U;
  for (std::size_t task = 0U; task < coverage.size(); ++task) {
    result ^= whole_game_coverage_slot_hash(static_cast<std::uint64_t>(task), coverage[task]);
  }
  return result;
}

std::uint64_t whole_game_profile_utility_slot_hash(const std::uint64_t slot,
                                                   const double utility_antes) {
  auto value =
      slot ^ std::rotl(std::bit_cast<std::uint64_t>(utility_antes), 23) ^ 0xD6E8'FEB8'6659'FD93ULL;
  value = (value ^ (value >> 32U)) * 0xD6E8'FEB8'6659'FD93ULL;
  value = (value ^ (value >> 32U)) * 0xD6E8'FEB8'6659'FD93ULL;
  return value ^ (value >> 32U);
}

std::uint64_t whole_game_profile_utility_state_hash(const std::vector<double> &utilities_antes) {
  std::uint64_t result = 0U;
  for (std::size_t slot = 0U; slot < utilities_antes.size(); ++slot) {
    result ^= whole_game_profile_utility_slot_hash(static_cast<std::uint64_t>(slot),
                                                   utilities_antes[slot]);
  }
  return result;
}

std::uint64_t
whole_game_local_continuation_slot_hash(const std::uint64_t task,
                                        const std::uint64_t continuation_identity_hash) {
  auto value = task ^ std::rotl(continuation_identity_hash, 17) ^ 0xA076'1D64'78BD'642FULL;
  value = (value ^ (value >> 32U)) * 0xE703'7ED1'A0B4'28DBULL;
  value = (value ^ (value >> 32U)) * 0x8EBC'6AF0'9C88'C6E3ULL;
  return value ^ (value >> 32U);
}

std::uint64_t whole_game_local_continuation_state_hash(
    const std::vector<std::uint64_t> &continuation_identity_hashes) {
  std::uint64_t result = 0U;
  for (std::size_t task = 0U; task < continuation_identity_hashes.size(); ++task) {
    result ^= whole_game_local_continuation_slot_hash(static_cast<std::uint64_t>(task),
                                                      continuation_identity_hashes[task]);
  }
  return result;
}

std::string wrap_file(const std::string_view marker, const std::string &payload) {
  std::ostringstream output;
  output << marker << ' ' << payload.size() << ' ' << checksum(payload) << '\n' << payload;
  return output.str();
}

Result<std::string, HuPreflopError> unwrap_file(const std::string &serialized,
                                                const std::string_view expected_marker) {
  const auto line_end = serialized.find('\n');
  if (line_end == std::string::npos) {
    return Result<std::string, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  std::istringstream header(serialized.substr(0U, line_end));
  std::string marker;
  std::uint64_t payload_size = 0U;
  std::string expected_checksum;
  if (!(header >> marker >> payload_size >> expected_checksum) || marker != expected_marker) {
    return Result<std::string, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  header >> std::ws;
  if (!header.eof() || payload_size != serialized.size() - line_end - 1U) {
    return Result<std::string, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  auto payload = serialized.substr(line_end + 1U);
  if (checksum(payload) != expected_checksum) {
    return Result<std::string, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  return Result<std::string, HuPreflopError>::success(std::move(payload));
}

bool atomic_replace(const std::filesystem::path &temporary,
                    const std::filesystem::path &destination) {
#if defined(_WIN32)
  return MoveFileExW(temporary.c_str(), destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  std::error_code error;
  std::filesystem::rename(temporary, destination, error);
  return !error;
#endif
}

Result<bool, HuPreflopError> save_payload(const std::string &path, const std::string &contents) {
  if (path.empty() ||
      contents.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
    return Result<bool, HuPreflopError>::failure(HuPreflopError::IoFailure);
  }
  const std::filesystem::path destination(path);
  auto temporary = destination;
  temporary += ".tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output || !output.write(contents.data(), static_cast<std::streamsize>(contents.size())) ||
        !output.flush()) {
      std::error_code ignored;
      std::filesystem::remove(temporary, ignored);
      return Result<bool, HuPreflopError>::failure(HuPreflopError::IoFailure);
    }
  }
  if (!atomic_replace(temporary, destination)) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return Result<bool, HuPreflopError>::failure(HuPreflopError::IoFailure);
  }
  return Result<bool, HuPreflopError>::success(true);
}

Result<std::string, HuPreflopError> load_payload(const std::string &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return Result<std::string, HuPreflopError>::failure(HuPreflopError::IoFailure);
  }
  std::ostringstream output;
  output << input.rdbuf();
  if (!input.good() && !input.eof()) {
    return Result<std::string, HuPreflopError>::failure(HuPreflopError::IoFailure);
  }
  return Result<std::string, HuPreflopError>::success(output.str());
}

Result<std::string, HuPreflopError>
load_payload_bounded(const std::string &path, const std::uint64_t maximum_payload_bytes) {
  if (path.empty() || maximum_payload_bytes == 0U ||
      maximum_payload_bytes > std::numeric_limits<std::uint64_t>::max() - 256U) {
    return Result<std::string, HuPreflopError>::failure(HuPreflopError::InvalidConfiguration);
  }
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if (error || size > maximum_payload_bytes + 256U ||
      size > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max())) {
    return Result<std::string, HuPreflopError>::failure(error ? HuPreflopError::IoFailure
                                                              : HuPreflopError::MemoryFailure);
  }
  return load_payload(path);
}

Json cards_json(const std::array<CardId, 3> &cards) {
  return Json::array({cards[0].value(), cards[1].value(), cards[2].value()});
}

bool read_cards(const Json &source, std::array<CardId, 3> &cards) {
  if (!source.is_array() || source.size() != cards.size()) {
    return false;
  }
  for (std::size_t index = 0U; index < cards.size(); ++index) {
    const auto value = source[index].get<std::uint32_t>();
    if (value >= 36U) {
      return false;
    }
    cards[index] = CardId::from_index(static_cast<std::uint8_t>(value)).value();
  }
  return cards[0] != cards[1] && cards[0] != cards[2] && cards[1] != cards[2];
}

bool finite_nonnegative(const double value) { return std::isfinite(value) && value >= 0.0; }

Json money_json(const Money value) { return value.units(); }

bool read_money(const Json &source, Money &value) {
  if (!source.is_number_integer()) {
    return false;
  }
  const auto decoded = Money::from_units(source.get<std::int64_t>());
  if (!decoded) {
    return false;
  }
  value = decoded.value();
  return true;
}

template <std::size_t Size> Json money_array_json(const std::array<Money, Size> &values) {
  Json result = Json::array();
  for (const auto value : values) {
    result.push_back(money_json(value));
  }
  return result;
}

template <std::size_t Size>
bool read_money_array(const Json &source, std::array<Money, Size> &values) {
  if (!source.is_array() || source.size() != Size) {
    return false;
  }
  for (std::size_t index = 0U; index < Size; ++index) {
    if (!read_money(source[index], values[index])) {
      return false;
    }
  }
  return true;
}

Json public_state_json(const PublicState &state) {
  return {{"street", static_cast<std::uint8_t>(state.street)},
          {"status", static_cast<std::uint8_t>(state.status)},
          {"board_mask", state.board_mask},
          {"player_count", state.player_count},
          {"player_to_act", state.player_to_act},
          {"initial_pot", money_json(state.initial_pot)},
          {"pot", money_json(state.pot)},
          {"returned_uncalled", money_json(state.returned_uncalled)},
          {"current_bet", money_json(state.current_bet)},
          {"last_full_raise_increment", money_json(state.last_full_raise_increment)},
          {"initial_pot_contributions", money_array_json(state.initial_pot_contributions)},
          {"remaining_stacks", money_array_json(state.remaining_stacks)},
          {"committed_this_street", money_array_json(state.committed_this_street)},
          {"committed_total", money_array_json(state.committed_total)},
          {"returned_uncalled_by_player", money_array_json(state.returned_uncalled_by_player)},
          {"active_players_mask", state.active_players_mask},
          {"all_in_players_mask", state.all_in_players_mask},
          {"acted_players_mask", state.acted_players_mask},
          {"terminal_winner_mask", state.terminal_winner_mask},
          {"raise_count_this_street", state.raise_count_this_street}};
}

bool read_public_state(const Json &source, PublicState &state) {
  if (!source.is_object()) {
    return false;
  }
  const auto street = source.at("street").get<std::uint8_t>();
  const auto status = source.at("status").get<std::uint8_t>();
  if (street > static_cast<std::uint8_t>(Street::River) ||
      status > static_cast<std::uint8_t>(HandStatus::Showdown)) {
    return false;
  }
  state.street = static_cast<Street>(street);
  state.status = static_cast<HandStatus>(status);
  state.board_mask = source.at("board_mask").get<std::uint64_t>();
  state.player_count = source.at("player_count").get<std::uint8_t>();
  state.player_to_act = source.at("player_to_act").get<std::uint8_t>();
  state.active_players_mask = source.at("active_players_mask").get<std::uint8_t>();
  state.all_in_players_mask = source.at("all_in_players_mask").get<std::uint8_t>();
  state.acted_players_mask = source.at("acted_players_mask").get<std::uint8_t>();
  state.terminal_winner_mask = source.at("terminal_winner_mask").get<std::uint8_t>();
  state.raise_count_this_street = source.at("raise_count_this_street").get<std::uint8_t>();
  return read_money(source.at("initial_pot"), state.initial_pot) &&
         read_money(source.at("pot"), state.pot) &&
         read_money(source.at("returned_uncalled"), state.returned_uncalled) &&
         read_money(source.at("current_bet"), state.current_bet) &&
         read_money(source.at("last_full_raise_increment"), state.last_full_raise_increment) &&
         read_money_array(source.at("initial_pot_contributions"),
                          state.initial_pot_contributions) &&
         read_money_array(source.at("remaining_stacks"), state.remaining_stacks) &&
         read_money_array(source.at("committed_this_street"), state.committed_this_street) &&
         read_money_array(source.at("committed_total"), state.committed_total) &&
         read_money_array(source.at("returned_uncalled_by_player"),
                          state.returned_uncalled_by_player) &&
         validate_state(state).has_value();
}

Json action_json(const Action &action) {
  return {{"amount", money_json(action.amount)},
          {"requested_basis_points", action.requested_basis_points},
          {"type", static_cast<std::uint8_t>(action.type)},
          {"all_in_kind", static_cast<std::uint8_t>(action.all_in_kind)}};
}

bool read_action(const Json &source, Action &action) {
  if (!source.is_object()) {
    return false;
  }
  const auto type = source.at("type").get<std::uint8_t>();
  const auto all_in_kind = source.at("all_in_kind").get<std::uint8_t>();
  if (type > static_cast<std::uint8_t>(ActionType::AllIn) ||
      all_in_kind > static_cast<std::uint8_t>(AllInKind::Raise) ||
      !read_money(source.at("amount"), action.amount)) {
    return false;
  }
  action.type = static_cast<ActionType>(type);
  action.all_in_kind = static_cast<AllInKind>(all_in_kind);
  action.requested_basis_points = source.at("requested_basis_points").get<std::uint32_t>();
  return true;
}

Json best_response_combo_values_json(const std::vector<HuPreflopBestResponseComboValue> &values) {
  Json result = Json::array();
  for (const auto &value : values) {
    result.push_back(Json::array({value.responding_combo, value.weighted_counterfactual_reach,
                                  value.weighted_counterfactual_utility_antes,
                                  value.conditional_value_antes, value.positive_reach}));
  }
  return result;
}

bool read_best_response_combo_values(const Json &source,
                                     std::vector<HuPreflopBestResponseComboValue> &values,
                                     const std::size_t expected_size) {
  if (!source.is_array() || source.size() != expected_size) {
    return false;
  }
  values.reserve(source.size());
  for (const auto &encoded : source) {
    if (!encoded.is_array() || encoded.size() != 5U) {
      return false;
    }
    HuPreflopBestResponseComboValue value;
    value.responding_combo = encoded[0].get<ComboId>();
    value.weighted_counterfactual_reach = encoded[1].get<double>();
    value.weighted_counterfactual_utility_antes = encoded[2].get<double>();
    value.conditional_value_antes = encoded[3].get<double>();
    value.positive_reach = encoded[4].get<bool>();
    values.push_back(value);
  }
  return true;
}

Json best_response_hand_class_values_json(
    const std::vector<HuPreflopBestResponseHandClassValue> &values) {
  Json result = Json::array();
  for (const auto &value : values) {
    result.push_back(Json::array({value.responding_class, value.weighted_counterfactual_reach,
                                  value.weighted_counterfactual_utility_antes,
                                  value.conditional_value_antes, value.positive_reach}));
  }
  return result;
}

bool read_best_response_hand_class_values(
    const Json &source, std::vector<HuPreflopBestResponseHandClassValue> &values) {
  if (!source.is_array() || source.size() != hu_preflop_hand_class_count) {
    return false;
  }
  values.reserve(source.size());
  for (const auto &encoded : source) {
    if (!encoded.is_array() || encoded.size() != 5U) {
      return false;
    }
    HuPreflopBestResponseHandClassValue value;
    value.responding_class = encoded[0].get<HandClassId>();
    value.weighted_counterfactual_reach = encoded[1].get<double>();
    value.weighted_counterfactual_utility_antes = encoded[2].get<double>();
    value.conditional_value_antes = encoded[3].get<double>();
    value.positive_reach = encoded[4].get<bool>();
    values.push_back(value);
  }
  return true;
}

Json blueprint_json(const HuPreflopBlueprint &blueprint) {
  Json decisions = Json::array();
  for (const auto &decision : blueprint.decisions) {
    Json strategy = Json::array();
    for (const auto &row : decision.strategy) {
      Json probabilities = Json::array();
      for (std::size_t action = 0U; action < decision.action_count; ++action) {
        probabilities.push_back(row[action]);
      }
      strategy.push_back(std::move(probabilities));
    }
    decisions.push_back({{"node_id", decision.node_id},
                         {"player", decision.player},
                         {"action_count", decision.action_count},
                         {"strategy", std::move(strategy)}});
  }
  return {{"schema", "gtosd.hu_preflop_blueprint.v1"},
          {"major", blueprint.major},
          {"minor", blueprint.minor},
          {"tree_fingerprint", blueprint.tree_fingerprint},
          {"algorithm", blueprint.algorithm},
          {"iterations", blueprint.iterations},
          {"decisions", std::move(decisions)},
          {"fingerprint", blueprint.fingerprint}};
}

Json plan_json(const HuPreflopDecompositionPlan &plan) {
  Json entries = Json::array();
  for (const auto &entry : plan.entries) {
    entries.push_back({{"entry_node", entry.entry_node},
                       {"own_sequence_reach", entry.own_sequence_reach},
                       {"positive_combo_count", entry.positive_combo_count},
                       {"joint_entry_probability", entry.joint_entry_probability}});
  }
  Json flops = Json::array();
  for (const auto &flop : plan.canonical_flop_catalog) {
    flops.push_back({{"cards", cards_json(flop.cards)},
                     {"physical_outcome_count", flop.physical_outcome_count}});
  }
  const Json bytes{
      {"reach_template_bytes", plan.bytes.reach_template_bytes},
      {"one_flop_conditioned_range_bytes", plan.bytes.one_flop_conditioned_range_bytes},
      {"one_resolver_boundary_bytes", plan.bytes.one_resolver_boundary_bytes},
      {"all_resolver_boundaries_bytes", plan.bytes.all_resolver_boundaries_bytes},
      {"both_players_boundary_bytes", plan.bytes.both_players_boundary_bytes},
      {"canonical_all_resolver_boundaries_bytes",
       plan.bytes.canonical_all_resolver_boundaries_bytes},
      {"canonical_both_players_boundary_bytes", plan.bytes.canonical_both_players_boundary_bytes},
      {"fully_materialized_range_bytes", plan.bytes.fully_materialized_range_bytes},
      {"canonical_fully_materialized_range_bytes",
       plan.bytes.canonical_fully_materialized_range_bytes},
      {"whole_game_coverage_mask_bytes", plan.bytes.whole_game_coverage_mask_bytes},
      {"whole_game_coverage_probability_bytes", plan.bytes.whole_game_coverage_probability_bytes},
      {"whole_game_profile_utility_bytes", plan.bytes.whole_game_profile_utility_bytes},
      {"whole_game_local_continuation_identity_bytes",
       plan.bytes.whole_game_local_continuation_identity_bytes},
      {"whole_game_coverage_payload_bytes", plan.bytes.whole_game_coverage_payload_bytes},
      {"streaming_certification_live_payload_bytes",
       plan.bytes.streaming_certification_live_payload_bytes}};
  return {{"schema", "gtosd.hu_preflop_decomposition_plan.v1"},
          {"major", plan.major},
          {"minor", plan.minor},
          {"tree_fingerprint", plan.tree_fingerprint},
          {"blueprint_fingerprint", plan.blueprint_fingerprint},
          {"entries", std::move(entries)},
          {"canonical_flop_catalog", std::move(flops)},
          {"physical_private_deals", plan.physical_private_deals},
          {"physical_flops", plan.physical_flops},
          {"canonical_flops", plan.canonical_flops},
          {"public_flop_roots", plan.public_flop_roots},
          {"canonical_public_flop_roots", plan.canonical_public_flop_roots},
          {"physical_deal_flop_histories", plan.physical_deal_flop_histories},
          {"maximum_live_combos_per_player", plan.maximum_live_combos_per_player},
          {"maximum_compatible_deals_per_flop", plan.maximum_compatible_deals_per_flop},
          {"postflop_entry_probability", plan.postflop_entry_probability},
          {"terminal_fold_probability", plan.terminal_fold_probability},
          {"terminal_all_in_probability", plan.terminal_all_in_probability},
          {"total_probability", plan.total_probability},
          {"bytes", bytes},
          {"fingerprint", plan.fingerprint}};
}

Json boundary_json(const HuPreflopFlopBoundary &boundary) {
  Json values = Json::array();
  for (const auto &value : boundary.values) {
    values.push_back(
        {{"opponent_combo", value.opponent_combo},
         {"counterfactual_reach", value.counterfactual_reach},
         {"blueprint_counterfactual_value_antes", value.blueprint_counterfactual_value_antes},
         {"positive_reach", value.positive_reach}});
  }
  return {{"schema", "gtosd.hu_preflop_flop_boundary.v2"},
          {"major", boundary.major},
          {"minor", boundary.minor},
          {"tree_fingerprint", boundary.tree_fingerprint},
          {"blueprint_fingerprint", boundary.blueprint_fingerprint},
          {"continuation_fingerprint", boundary.continuation_fingerprint},
          {"continuation_checkpoint_fingerprint", boundary.continuation_checkpoint_fingerprint},
          {"blueprint_iterations", boundary.blueprint_iterations},
          {"entry_node", boundary.entry_node},
          {"flop", cards_json(boundary.flop)},
          {"resolving_player", boundary.resolving_player},
          {"opponent", boundary.opponent},
          {"values", std::move(values)},
          {"fingerprint", boundary.fingerprint}};
}

bool valid_whole_game_coverage_payload(const HuPreflopWholeGameCoverageAccumulator &accumulator) {
  constexpr std::size_t maximum_task_count = 1'000'000U;
  if (accumulator.major != HuPreflopWholeGameCoverageAccumulator::format_major ||
      accumulator.minor != HuPreflopWholeGameCoverageAccumulator::format_minor ||
      accumulator.tree_fingerprint.empty() || accumulator.blueprint_fingerprint.empty() ||
      accumulator.plan_fingerprint.empty() ||
      !finite_nonnegative(accumulator.target_normalized_nashconv) ||
      accumulator.task_side_coverage.empty() ||
      accumulator.task_side_coverage.size() > maximum_task_count ||
      accumulator.task_side_coverage.size() != accumulator.task_joint_probabilities.size() ||
      accumulator.task_side_profile_utility_antes.size() !=
          accumulator.task_side_coverage.size() * 2U ||
      accumulator.task_local_continuation_identity_hashes.size() !=
          accumulator.task_side_coverage.size() ||
      !finite_nonnegative(accumulator.covered_postflop_probability) ||
      accumulator.coverage_state_hash !=
          whole_game_coverage_state_hash(accumulator.task_side_coverage) ||
      accumulator.profile_utility_state_hash !=
          whole_game_profile_utility_state_hash(accumulator.task_side_profile_utility_antes) ||
      accumulator.local_continuation_identity_state_hash !=
          whole_game_local_continuation_state_hash(
              accumulator.task_local_continuation_identity_hashes) ||
      (accumulator.validated_boundary_count == 0U
           ? accumulator.blueprint_iterations != 0U ||
                 !accumulator.continuation_checkpoint_fingerprint.empty()
           : accumulator.blueprint_iterations == 0U ||
                 accumulator.continuation_checkpoint_fingerprint.empty()) ||
      accumulator.contribution_chain_fingerprint.empty() || accumulator.fingerprint.empty() ||
      accumulator.fingerprint !=
          fingerprint_hu_preflop_whole_game_coverage_accumulator(accumulator)) {
    return false;
  }
  std::uint64_t validated_boundaries = 0U;
  std::uint64_t fully_covered_tasks = 0U;
  double covered_probability = 0.0;
  for (std::size_t task = 0U; task < accumulator.task_side_coverage.size(); ++task) {
    const auto mask = accumulator.task_side_coverage[task];
    const auto probability = accumulator.task_joint_probabilities[task];
    const auto first_utility = accumulator.task_side_profile_utility_antes[task * 2U];
    const auto second_utility = accumulator.task_side_profile_utility_antes[task * 2U + 1U];
    const auto local_identity = accumulator.task_local_continuation_identity_hashes[task];
    if (mask > 0x3U || !finite_nonnegative(probability) || !std::isfinite(first_utility) ||
        !std::isfinite(second_utility) || ((mask == 0U) != (local_identity == 0U)) ||
        ((mask & 0x1U) == 0U && first_utility != 0.0) ||
        ((mask & 0x2U) == 0U && second_utility != 0.0)) {
      return false;
    }
    validated_boundaries += std::popcount(static_cast<unsigned>(mask));
    if (mask == 0x3U) {
      ++fully_covered_tasks;
      covered_probability += probability;
    }
  }
  return std::isfinite(covered_probability) &&
         accumulator.validated_boundary_count == validated_boundaries &&
         accumulator.fully_covered_task_count == fully_covered_tasks &&
         std::abs(accumulator.covered_postflop_probability - covered_probability) <=
             1.0e-12 * std::max(1.0, std::abs(covered_probability));
}

Json whole_game_coverage_json(const HuPreflopWholeGameCoverageAccumulator &accumulator) {
  return {{"schema", "gtosd.hu_preflop_whole_game_coverage.v1"},
          {"major", accumulator.major},
          {"minor", accumulator.minor},
          {"tree_fingerprint", accumulator.tree_fingerprint},
          {"blueprint_fingerprint", accumulator.blueprint_fingerprint},
          {"plan_fingerprint", accumulator.plan_fingerprint},
          {"target_normalized_nashconv", accumulator.target_normalized_nashconv},
          {"blueprint_iterations", accumulator.blueprint_iterations},
          {"continuation_checkpoint_fingerprint", accumulator.continuation_checkpoint_fingerprint},
          {"task_side_coverage", accumulator.task_side_coverage},
          {"task_joint_probabilities", accumulator.task_joint_probabilities},
          {"task_side_profile_utility_antes", accumulator.task_side_profile_utility_antes},
          {"task_local_continuation_identity_hashes",
           accumulator.task_local_continuation_identity_hashes},
          {"validated_boundary_count", accumulator.validated_boundary_count},
          {"fully_covered_task_count", accumulator.fully_covered_task_count},
          {"covered_postflop_probability", accumulator.covered_postflop_probability},
          {"coverage_state_hash", accumulator.coverage_state_hash},
          {"continuation_profile_hash", accumulator.continuation_profile_hash},
          {"profile_utility_state_hash", accumulator.profile_utility_state_hash},
          {"local_continuation_identity_state_hash",
           accumulator.local_continuation_identity_state_hash},
          {"contribution_chain_fingerprint", accumulator.contribution_chain_fingerprint},
          {"fingerprint", accumulator.fingerprint}};
}

Json river_scheduler_checkpoint_json(const HuPreflopRiverSchedulerCheckpoint &checkpoint) {
  return {{"schema", "gtosd.hu_preflop_river_scheduler_checkpoint.v1"},
          {"major", checkpoint.major},
          {"minor", checkpoint.minor},
          {"tree_fingerprint", checkpoint.tree_fingerprint},
          {"blueprint_fingerprint", checkpoint.blueprint_fingerprint},
          {"batch_plan_fingerprint", checkpoint.batch_plan_fingerprint},
          {"completed_batch_count", checkpoint.completed_batch_count},
          {"completed_resolver_root_count", checkpoint.completed_resolver_root_count},
          {"upper_street_accumulator_fingerprint", checkpoint.upper_street_accumulator_fingerprint},
          {"complete", checkpoint.complete},
          {"fingerprint", checkpoint.fingerprint}};
}

Json river_task_accumulator_json(const HuPreflopRiverTaskAccumulator &accumulator) {
  Json values = Json::array();
  for (const auto &player : accumulator.values) {
    Json player_values = Json::array();
    for (const auto &value : player) {
      player_values.push_back(Json::array(
          {value.weighted_reach_sum, value.weighted_reach_compensation,
           value.weighted_utility_sum_antes, value.weighted_utility_compensation_antes}));
    }
    values.push_back(std::move(player_values));
  }
  return {{"schema", "gtosd.hu_preflop_river_task_accumulator.v5"},
          {"major", accumulator.major},
          {"minor", accumulator.minor},
          {"tree_fingerprint", accumulator.tree_fingerprint},
          {"blueprint_fingerprint", accumulator.blueprint_fingerprint},
          {"root_catalog_fingerprint", accumulator.root_catalog_fingerprint},
          {"batch_plan_fingerprint", accumulator.batch_plan_fingerprint},
          {"continuation_checkpoint_fingerprint", accumulator.continuation_checkpoint_fingerprint},
          {"value_mode", static_cast<std::uint8_t>(accumulator.value_mode)},
          {"blueprint_iterations", accumulator.blueprint_iterations},
          {"task_span_index", accumulator.task_span_index},
          {"first_resolver_root", accumulator.first_resolver_root},
          {"resolver_root_count", accumulator.resolver_root_count},
          {"next_resolver_root", accumulator.next_resolver_root},
          {"entry_node", accumulator.entry_node},
          {"flop", cards_json(accumulator.flop)},
          {"accumulated_roots_by_resolver", accumulator.accumulated_roots_by_resolver},
          {"values", std::move(values)},
          {"complete", accumulator.complete},
          {"contribution_chain_fingerprint", accumulator.contribution_chain_fingerprint},
          {"fingerprint", accumulator.fingerprint}};
}

bool valid_river_best_response_leaf_accumulator_payload(
    const HuPreflopRiverBestResponseLeafAccumulator &accumulator) {
  const auto board_mask = accumulator.flop[0].mask() | accumulator.flop[1].mask() |
                          accumulator.flop[2].mask() | accumulator.turn.mask();
  if (accumulator.major != HuPreflopRiverBestResponseLeafAccumulator::format_major ||
      accumulator.minor > HuPreflopRiverBestResponseLeafAccumulator::format_minor ||
      accumulator.tree_fingerprint.empty() || accumulator.blueprint_fingerprint.empty() ||
      accumulator.root_catalog_fingerprint.empty() || accumulator.batch_plan_fingerprint.empty() ||
      accumulator.continuation_fingerprint.empty() || accumulator.blueprint_iterations == 0U ||
      accumulator.responding_player > 1U || std::popcount(board_mask) != 4 ||
      accumulator.state.board_mask != board_mask || !validate_state(accumulator.state) ||
      accumulator.history_fingerprint.empty() ||
      accumulator.expected_resolver_root_ordinals.empty() ||
      accumulator.next_boundary_index > accumulator.expected_resolver_root_ordinals.size() ||
      accumulator.complete !=
          (accumulator.next_boundary_index == accumulator.expected_resolver_root_ordinals.size()) ||
      accumulator.contribution_chain_fingerprint.empty() || accumulator.fingerprint.empty() ||
      accumulator.fingerprint !=
          fingerprint_hu_preflop_river_best_response_leaf_accumulator(accumulator)) {
    return false;
  }
  for (std::size_t index = 1U; index < accumulator.expected_resolver_root_ordinals.size();
       ++index) {
    if (accumulator.expected_resolver_root_ordinals[index] <=
        accumulator.expected_resolver_root_ordinals[index - 1U]) {
      return false;
    }
  }
  for (const auto &action : accumulator.action_history) {
    if (static_cast<std::uint8_t>(action.type) > static_cast<std::uint8_t>(ActionType::AllIn) ||
        static_cast<std::uint8_t>(action.all_in_kind) >
            static_cast<std::uint8_t>(AllInKind::Raise) ||
        action.amount.units() < 0) {
      return false;
    }
  }
  for (const auto &value : accumulator.values) {
    if (!std::isfinite(value.weighted_reach_sum) ||
        !std::isfinite(value.weighted_reach_compensation) ||
        !std::isfinite(value.weighted_utility_sum_antes) ||
        !std::isfinite(value.weighted_utility_compensation_antes)) {
      return false;
    }
  }
  return true;
}

bool valid_best_response_value(const double reach, const double utility, const double conditional,
                               const bool positive) {
  if (!finite_nonnegative(reach) || !std::isfinite(utility) || !std::isfinite(conditional) ||
      positive != (reach > 0.0)) {
    return false;
  }
  if (reach == 0.0) {
    return utility == 0.0 && conditional == 0.0;
  }
  const auto expected = utility / reach;
  const auto tolerance = 1.0e-12 * std::max({1.0, std::abs(expected), std::abs(conditional)});
  return std::abs(expected - conditional) <= tolerance;
}

bool valid_best_response_task_evaluation_payload(
    const HuPreflopBestResponseTaskEvaluation &evaluation) {
  if (evaluation.major != HuPreflopBestResponseTaskEvaluation::format_major ||
      evaluation.minor > HuPreflopBestResponseTaskEvaluation::format_minor ||
      evaluation.tree_fingerprint.empty() || evaluation.blueprint_fingerprint.empty() ||
      evaluation.continuation_fingerprint.empty() || evaluation.blueprint_iterations == 0U ||
      evaluation.responding_player > 1U || evaluation.decision_nodes_visited == 0U ||
      evaluation.decision_nodes_visited != evaluation.responder_decision_nodes_visited +
                                               evaluation.opponent_decision_nodes_visited ||
      evaluation.physical_turn_branches_visited == 0U ||
      evaluation.physical_turn_branches_visited % 33U != 0U ||
      evaluation.upper_terminal_leaves_visited == 0U ||
      evaluation.river_continuation_leaves_visited == 0U ||
      evaluation.maximum_recursion_depth == 0U || evaluation.values.size() != 528U ||
      evaluation.fingerprint.empty() ||
      evaluation.fingerprint != fingerprint_hu_preflop_best_response_task_evaluation(evaluation)) {
    return false;
  }
  const auto board_mask =
      evaluation.flop[0].mask() | evaluation.flop[1].mask() | evaluation.flop[2].mask();
  if (std::popcount(board_mask) != 3) {
    return false;
  }
  const auto combos = all_combos();
  std::array<bool, 630U> observed{};
  for (const auto &value : evaluation.values) {
    if (value.responding_combo >= combos.size() || observed[value.responding_combo] ||
        ((combos[value.responding_combo].first.mask() |
          combos[value.responding_combo].second.mask()) &
         board_mask) != 0U ||
        !valid_best_response_value(value.weighted_counterfactual_reach,
                                   value.weighted_counterfactual_utility_antes,
                                   value.conditional_value_antes, value.positive_reach)) {
      return false;
    }
    observed[value.responding_combo] = true;
  }
  return true;
}

bool valid_best_response_entry_accumulator_payload(
    const HuPreflopBestResponseEntryAccumulator &accumulator) {
  if (accumulator.major != HuPreflopBestResponseEntryAccumulator::format_major ||
      accumulator.minor > HuPreflopBestResponseEntryAccumulator::format_minor ||
      accumulator.tree_fingerprint.empty() || accumulator.blueprint_fingerprint.empty() ||
      accumulator.continuation_fingerprint.empty() || accumulator.blueprint_iterations == 0U ||
      accumulator.responding_player > 1U || accumulator.task_count == 0U ||
      accumulator.first_task_span_index >
          std::numeric_limits<std::uint64_t>::max() - accumulator.task_count ||
      accumulator.next_task_offset > accumulator.task_count ||
      accumulator.complete != (accumulator.next_task_offset == accumulator.task_count) ||
      accumulator.contribution_chain_fingerprint.empty() || accumulator.fingerprint.empty() ||
      accumulator.fingerprint !=
          fingerprint_hu_preflop_best_response_entry_accumulator(accumulator)) {
    return false;
  }
  for (const auto &value : accumulator.values) {
    const auto reach = value.weighted_reach_sum + value.weighted_reach_compensation;
    const auto utility =
        value.weighted_utility_sum_antes + value.weighted_utility_compensation_antes;
    if (!std::isfinite(value.weighted_reach_sum) ||
        !std::isfinite(value.weighted_reach_compensation) ||
        !std::isfinite(value.weighted_utility_sum_antes) ||
        !std::isfinite(value.weighted_utility_compensation_antes) || !finite_nonnegative(reach) ||
        !std::isfinite(utility) || (reach == 0.0 && utility != 0.0) ||
        (accumulator.next_task_offset == 0U &&
         (value.weighted_reach_sum != 0.0 || value.weighted_reach_compensation != 0.0 ||
          value.weighted_utility_sum_antes != 0.0 ||
          value.weighted_utility_compensation_antes != 0.0))) {
      return false;
    }
  }
  return true;
}

bool valid_best_response_entry_evaluation_payload(
    const HuPreflopBestResponseEntryEvaluation &evaluation) {
  if (evaluation.major != HuPreflopBestResponseEntryEvaluation::format_major ||
      evaluation.minor > HuPreflopBestResponseEntryEvaluation::format_minor ||
      evaluation.tree_fingerprint.empty() || evaluation.blueprint_fingerprint.empty() ||
      evaluation.continuation_fingerprint.empty() || evaluation.accumulator_fingerprint.empty() ||
      evaluation.blueprint_iterations == 0U || evaluation.responding_player > 1U ||
      evaluation.task_count == 0U || evaluation.values.size() != hu_preflop_hand_class_count ||
      evaluation.fingerprint.empty() ||
      evaluation.fingerprint != fingerprint_hu_preflop_best_response_entry_evaluation(evaluation)) {
    return false;
  }
  std::array<bool, hu_preflop_hand_class_count> observed{};
  for (const auto &value : evaluation.values) {
    if (value.responding_class >= observed.size() || observed[value.responding_class] ||
        !valid_best_response_value(value.weighted_counterfactual_reach,
                                   value.weighted_counterfactual_utility_antes,
                                   value.conditional_value_antes, value.positive_reach)) {
      return false;
    }
    observed[value.responding_class] = true;
  }
  return true;
}

Json river_best_response_leaf_accumulator_json(
    const HuPreflopRiverBestResponseLeafAccumulator &accumulator) {
  Json actions = Json::array();
  for (const auto &action : accumulator.action_history) {
    actions.push_back(action_json(action));
  }
  Json values = Json::array();
  for (const auto &value : accumulator.values) {
    values.push_back(
        Json::array({value.weighted_reach_sum, value.weighted_reach_compensation,
                     value.weighted_utility_sum_antes, value.weighted_utility_compensation_antes}));
  }
  return {{"schema", "gtosd.hu_preflop_river_best_response_leaf_accumulator.v1"},
          {"major", accumulator.major},
          {"minor", accumulator.minor},
          {"tree_fingerprint", accumulator.tree_fingerprint},
          {"blueprint_fingerprint", accumulator.blueprint_fingerprint},
          {"root_catalog_fingerprint", accumulator.root_catalog_fingerprint},
          {"batch_plan_fingerprint", accumulator.batch_plan_fingerprint},
          {"continuation_fingerprint", accumulator.continuation_fingerprint},
          {"blueprint_iterations", accumulator.blueprint_iterations},
          {"task_span_index", accumulator.task_span_index},
          {"entry_node", accumulator.entry_node},
          {"flop", cards_json(accumulator.flop)},
          {"turn", accumulator.turn.value()},
          {"responding_player", accumulator.responding_player},
          {"state", public_state_json(accumulator.state)},
          {"action_history", std::move(actions)},
          {"history_fingerprint", accumulator.history_fingerprint},
          {"expected_resolver_root_ordinals", accumulator.expected_resolver_root_ordinals},
          {"next_boundary_index", accumulator.next_boundary_index},
          {"values", std::move(values)},
          {"complete", accumulator.complete},
          {"contribution_chain_fingerprint", accumulator.contribution_chain_fingerprint},
          {"fingerprint", accumulator.fingerprint}};
}

Json best_response_task_evaluation_json(const HuPreflopBestResponseTaskEvaluation &evaluation) {
  return {{"schema", "gtosd.hu_preflop_best_response_task_evaluation.v1"},
          {"major", evaluation.major},
          {"minor", evaluation.minor},
          {"tree_fingerprint", evaluation.tree_fingerprint},
          {"blueprint_fingerprint", evaluation.blueprint_fingerprint},
          {"continuation_fingerprint", evaluation.continuation_fingerprint},
          {"blueprint_iterations", evaluation.blueprint_iterations},
          {"task_span_index", evaluation.task_span_index},
          {"entry_node", evaluation.entry_node},
          {"flop", cards_json(evaluation.flop)},
          {"responding_player", evaluation.responding_player},
          {"decision_nodes_visited", evaluation.decision_nodes_visited},
          {"responder_decision_nodes_visited", evaluation.responder_decision_nodes_visited},
          {"opponent_decision_nodes_visited", evaluation.opponent_decision_nodes_visited},
          {"physical_turn_branches_visited", evaluation.physical_turn_branches_visited},
          {"upper_terminal_leaves_visited", evaluation.upper_terminal_leaves_visited},
          {"river_continuation_leaves_visited", evaluation.river_continuation_leaves_visited},
          {"maximum_recursion_depth", evaluation.maximum_recursion_depth},
          {"values", best_response_combo_values_json(evaluation.values)},
          {"fingerprint", evaluation.fingerprint}};
}

Json best_response_entry_accumulator_json(
    const HuPreflopBestResponseEntryAccumulator &accumulator) {
  Json values = Json::array();
  for (const auto &value : accumulator.values) {
    values.push_back(
        Json::array({value.weighted_reach_sum, value.weighted_reach_compensation,
                     value.weighted_utility_sum_antes, value.weighted_utility_compensation_antes}));
  }
  return {{"schema", "gtosd.hu_preflop_best_response_entry_accumulator.v1"},
          {"major", accumulator.major},
          {"minor", accumulator.minor},
          {"tree_fingerprint", accumulator.tree_fingerprint},
          {"blueprint_fingerprint", accumulator.blueprint_fingerprint},
          {"continuation_fingerprint", accumulator.continuation_fingerprint},
          {"blueprint_iterations", accumulator.blueprint_iterations},
          {"entry_index", accumulator.entry_index},
          {"entry_node", accumulator.entry_node},
          {"responding_player", accumulator.responding_player},
          {"first_task_span_index", accumulator.first_task_span_index},
          {"task_count", accumulator.task_count},
          {"next_task_offset", accumulator.next_task_offset},
          {"values", std::move(values)},
          {"complete", accumulator.complete},
          {"contribution_chain_fingerprint", accumulator.contribution_chain_fingerprint},
          {"fingerprint", accumulator.fingerprint}};
}

Json best_response_entry_evaluation_json(const HuPreflopBestResponseEntryEvaluation &evaluation) {
  return {{"schema", "gtosd.hu_preflop_best_response_entry_evaluation.v1"},
          {"major", evaluation.major},
          {"minor", evaluation.minor},
          {"tree_fingerprint", evaluation.tree_fingerprint},
          {"blueprint_fingerprint", evaluation.blueprint_fingerprint},
          {"continuation_fingerprint", evaluation.continuation_fingerprint},
          {"accumulator_fingerprint", evaluation.accumulator_fingerprint},
          {"blueprint_iterations", evaluation.blueprint_iterations},
          {"entry_index", evaluation.entry_index},
          {"entry_node", evaluation.entry_node},
          {"responding_player", evaluation.responding_player},
          {"task_count", evaluation.task_count},
          {"values", best_response_hand_class_values_json(evaluation.values)},
          {"fingerprint", evaluation.fingerprint}};
}

Json river_task_aggregate_json(const HuPreflopRiverTaskAggregate &aggregate) {
  Json resolver_values = Json::array();
  for (const auto &player : aggregate.resolver_values) {
    Json player_values = Json::array();
    for (const auto &value : player) {
      player_values.push_back(
          Json::array({value.opponent_combo, value.weighted_counterfactual_reach,
                       value.weighted_counterfactual_utility_antes, value.conditional_value_antes,
                       value.positive_reach}));
    }
    resolver_values.push_back(std::move(player_values));
  }
  return {{"schema", "gtosd.hu_preflop_river_task_aggregate.v5"},
          {"major", aggregate.major},
          {"minor", aggregate.minor},
          {"tree_fingerprint", aggregate.tree_fingerprint},
          {"blueprint_fingerprint", aggregate.blueprint_fingerprint},
          {"accumulator_fingerprint", aggregate.accumulator_fingerprint},
          {"continuation_checkpoint_fingerprint", aggregate.continuation_checkpoint_fingerprint},
          {"value_mode", static_cast<std::uint8_t>(aggregate.value_mode)},
          {"blueprint_iterations", aggregate.blueprint_iterations},
          {"task_span_index", aggregate.task_span_index},
          {"entry_node", aggregate.entry_node},
          {"flop", cards_json(aggregate.flop)},
          {"resolver_values", std::move(resolver_values)},
          {"fingerprint", aggregate.fingerprint}};
}

} // namespace

Result<std::string, HuPreflopError>
serialize_hu_preflop_blueprint(const HuPreflopBlueprint &blueprint) {
  if (blueprint.major != HuPreflopBlueprint::format_major ||
      blueprint.minor != HuPreflopBlueprint::format_minor || blueprint.decisions.empty() ||
      blueprint.fingerprint != fingerprint_hu_preflop_blueprint(blueprint)) {
    return Result<std::string, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  return Result<std::string, HuPreflopError>::success(blueprint_json(blueprint).dump());
}

Result<HuPreflopBlueprint, HuPreflopError>
deserialize_hu_preflop_blueprint(const std::string &serialized) {
  try {
    const auto source = Json::parse(serialized);
    if (source.at("schema") != "gtosd.hu_preflop_blueprint.v1") {
      return Result<HuPreflopBlueprint, HuPreflopError>::failure(
          HuPreflopError::UnsupportedVersion);
    }
    HuPreflopBlueprint result;
    result.major = source.at("major").get<std::uint32_t>();
    result.minor = source.at("minor").get<std::uint32_t>();
    if (result.major != HuPreflopBlueprint::format_major ||
        result.minor != HuPreflopBlueprint::format_minor) {
      return Result<HuPreflopBlueprint, HuPreflopError>::failure(
          HuPreflopError::UnsupportedVersion);
    }
    result.tree_fingerprint = source.at("tree_fingerprint").get<std::string>();
    result.algorithm = source.at("algorithm").get<std::string>();
    result.iterations = source.at("iterations").get<std::uint64_t>();
    for (const auto &item : source.at("decisions")) {
      HuPreflopBlueprintDecision decision;
      decision.node_id = item.at("node_id").get<std::uint32_t>();
      decision.player = item.at("player").get<std::uint8_t>();
      decision.action_count = item.at("action_count").get<std::uint8_t>();
      const auto &strategy = item.at("strategy");
      if (decision.player > 1U || decision.action_count == 0U ||
          decision.action_count > hu_preflop_maximum_actions ||
          strategy.size() != hu_preflop_hand_class_count) {
        return Result<HuPreflopBlueprint, HuPreflopError>::failure(
            HuPreflopError::IntegrityFailure);
      }
      for (std::size_t hand = 0U; hand < strategy.size(); ++hand) {
        if (!strategy[hand].is_array() || strategy[hand].size() != decision.action_count) {
          return Result<HuPreflopBlueprint, HuPreflopError>::failure(
              HuPreflopError::IntegrityFailure);
        }
        double total = 0.0;
        for (std::size_t action = 0U; action < decision.action_count; ++action) {
          const auto probability = strategy[hand][action].get<double>();
          if (!finite_nonnegative(probability)) {
            return Result<HuPreflopBlueprint, HuPreflopError>::failure(
                HuPreflopError::IntegrityFailure);
          }
          decision.strategy[hand][action] = probability;
          total += probability;
        }
        if (std::abs(total - 1.0) > 1.0e-9) {
          return Result<HuPreflopBlueprint, HuPreflopError>::failure(
              HuPreflopError::IntegrityFailure);
        }
      }
      result.decisions.push_back(std::move(decision));
    }
    result.fingerprint = source.at("fingerprint").get<std::string>();
    if (result.decisions.empty() ||
        result.fingerprint != fingerprint_hu_preflop_blueprint(result)) {
      return Result<HuPreflopBlueprint, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
    }
    return Result<HuPreflopBlueprint, HuPreflopError>::success(std::move(result));
  } catch (const Json::exception &) {
    return Result<HuPreflopBlueprint, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
}

Result<std::string, HuPreflopError>
serialize_hu_preflop_decomposition_plan(const HuPreflopDecompositionPlan &plan) {
  if (plan.major != HuPreflopDecompositionPlan::format_major ||
      plan.minor != HuPreflopDecompositionPlan::format_minor || plan.entries.empty() ||
      plan.fingerprint != fingerprint_hu_preflop_decomposition_plan(plan)) {
    return Result<std::string, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  return Result<std::string, HuPreflopError>::success(plan_json(plan).dump());
}

Result<HuPreflopDecompositionPlan, HuPreflopError>
deserialize_hu_preflop_decomposition_plan(const std::string &serialized) {
  try {
    const auto source = Json::parse(serialized);
    if (source.at("schema") != "gtosd.hu_preflop_decomposition_plan.v1") {
      return Result<HuPreflopDecompositionPlan, HuPreflopError>::failure(
          HuPreflopError::UnsupportedVersion);
    }
    HuPreflopDecompositionPlan result;
    result.major = source.at("major").get<std::uint32_t>();
    result.minor = source.at("minor").get<std::uint32_t>();
    if (result.major != HuPreflopDecompositionPlan::format_major ||
        result.minor != HuPreflopDecompositionPlan::format_minor) {
      return Result<HuPreflopDecompositionPlan, HuPreflopError>::failure(
          HuPreflopError::UnsupportedVersion);
    }
    result.tree_fingerprint = source.at("tree_fingerprint").get<std::string>();
    result.blueprint_fingerprint = source.at("blueprint_fingerprint").get<std::string>();
    for (const auto &item : source.at("entries")) {
      HuPreflopPostflopEntryReach entry;
      entry.entry_node = item.at("entry_node").get<std::uint32_t>();
      entry.own_sequence_reach =
          item.at("own_sequence_reach").get<decltype(entry.own_sequence_reach)>();
      entry.positive_combo_count =
          item.at("positive_combo_count").get<decltype(entry.positive_combo_count)>();
      entry.joint_entry_probability = item.at("joint_entry_probability").get<double>();
      if (!finite_nonnegative(entry.joint_entry_probability)) {
        return Result<HuPreflopDecompositionPlan, HuPreflopError>::failure(
            HuPreflopError::IntegrityFailure);
      }
      for (const auto &player : entry.own_sequence_reach) {
        for (const auto reach : player) {
          if (!finite_nonnegative(reach)) {
            return Result<HuPreflopDecompositionPlan, HuPreflopError>::failure(
                HuPreflopError::IntegrityFailure);
          }
        }
      }
      result.entries.push_back(std::move(entry));
    }
    for (const auto &item : source.at("canonical_flop_catalog")) {
      HuPreflopCanonicalFlop flop;
      if (!read_cards(item.at("cards"), flop.cards)) {
        return Result<HuPreflopDecompositionPlan, HuPreflopError>::failure(
            HuPreflopError::IntegrityFailure);
      }
      flop.physical_outcome_count = item.at("physical_outcome_count").get<std::uint32_t>();
      if (flop.physical_outcome_count == 0U) {
        return Result<HuPreflopDecompositionPlan, HuPreflopError>::failure(
            HuPreflopError::IntegrityFailure);
      }
      result.canonical_flop_catalog.push_back(flop);
    }
#define GTOSD_READ_PLAN_FIELD(name) result.name = source.at(#name).get<decltype(result.name)>()
    GTOSD_READ_PLAN_FIELD(physical_private_deals);
    GTOSD_READ_PLAN_FIELD(physical_flops);
    GTOSD_READ_PLAN_FIELD(canonical_flops);
    GTOSD_READ_PLAN_FIELD(public_flop_roots);
    GTOSD_READ_PLAN_FIELD(canonical_public_flop_roots);
    GTOSD_READ_PLAN_FIELD(physical_deal_flop_histories);
    GTOSD_READ_PLAN_FIELD(maximum_live_combos_per_player);
    GTOSD_READ_PLAN_FIELD(maximum_compatible_deals_per_flop);
    GTOSD_READ_PLAN_FIELD(postflop_entry_probability);
    GTOSD_READ_PLAN_FIELD(terminal_fold_probability);
    GTOSD_READ_PLAN_FIELD(terminal_all_in_probability);
    GTOSD_READ_PLAN_FIELD(total_probability);
#undef GTOSD_READ_PLAN_FIELD
    const auto &bytes = source.at("bytes");
#define GTOSD_READ_BYTE_FIELD(name)                                                                \
  result.bytes.name = bytes.at(#name).get<decltype(result.bytes.name)>()
    GTOSD_READ_BYTE_FIELD(reach_template_bytes);
    GTOSD_READ_BYTE_FIELD(one_flop_conditioned_range_bytes);
    GTOSD_READ_BYTE_FIELD(one_resolver_boundary_bytes);
    GTOSD_READ_BYTE_FIELD(all_resolver_boundaries_bytes);
    GTOSD_READ_BYTE_FIELD(both_players_boundary_bytes);
    GTOSD_READ_BYTE_FIELD(canonical_all_resolver_boundaries_bytes);
    GTOSD_READ_BYTE_FIELD(canonical_both_players_boundary_bytes);
    GTOSD_READ_BYTE_FIELD(fully_materialized_range_bytes);
    GTOSD_READ_BYTE_FIELD(canonical_fully_materialized_range_bytes);
    GTOSD_READ_BYTE_FIELD(whole_game_coverage_mask_bytes);
    GTOSD_READ_BYTE_FIELD(whole_game_coverage_probability_bytes);
    GTOSD_READ_BYTE_FIELD(whole_game_profile_utility_bytes);
    GTOSD_READ_BYTE_FIELD(whole_game_local_continuation_identity_bytes);
    GTOSD_READ_BYTE_FIELD(whole_game_coverage_payload_bytes);
    GTOSD_READ_BYTE_FIELD(streaming_certification_live_payload_bytes);
#undef GTOSD_READ_BYTE_FIELD
    result.fingerprint = source.at("fingerprint").get<std::string>();
    const auto finite_probabilities = finite_nonnegative(result.postflop_entry_probability) &&
                                      finite_nonnegative(result.terminal_fold_probability) &&
                                      finite_nonnegative(result.terminal_all_in_probability) &&
                                      finite_nonnegative(result.total_probability);
    if (!finite_probabilities || result.entries.empty() ||
        result.canonical_flop_catalog.size() != result.canonical_flops ||
        result.fingerprint != fingerprint_hu_preflop_decomposition_plan(result)) {
      return Result<HuPreflopDecompositionPlan, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    return Result<HuPreflopDecompositionPlan, HuPreflopError>::success(std::move(result));
  } catch (const Json::exception &) {
    return Result<HuPreflopDecompositionPlan, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
}

Result<std::string, HuPreflopError>
serialize_hu_preflop_flop_boundary(const HuPreflopFlopBoundary &boundary) {
  if (boundary.major != HuPreflopFlopBoundary::format_major ||
      boundary.minor != HuPreflopFlopBoundary::format_minor || boundary.values.empty() ||
      boundary.continuation_fingerprint.empty() ||
      boundary.continuation_checkpoint_fingerprint.empty() ||
      boundary.fingerprint != fingerprint_hu_preflop_flop_boundary(boundary)) {
    return Result<std::string, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  return Result<std::string, HuPreflopError>::success(boundary_json(boundary).dump());
}

Result<HuPreflopFlopBoundary, HuPreflopError>
deserialize_hu_preflop_flop_boundary(const std::string &serialized) {
  try {
    const auto source = Json::parse(serialized);
    if (source.at("schema") != "gtosd.hu_preflop_flop_boundary.v2") {
      return Result<HuPreflopFlopBoundary, HuPreflopError>::failure(
          HuPreflopError::UnsupportedVersion);
    }
    HuPreflopFlopBoundary result;
    result.major = source.at("major").get<std::uint32_t>();
    result.minor = source.at("minor").get<std::uint32_t>();
    if (result.major != HuPreflopFlopBoundary::format_major ||
        result.minor != HuPreflopFlopBoundary::format_minor) {
      return Result<HuPreflopFlopBoundary, HuPreflopError>::failure(
          HuPreflopError::UnsupportedVersion);
    }
    result.tree_fingerprint = source.at("tree_fingerprint").get<std::string>();
    result.blueprint_fingerprint = source.at("blueprint_fingerprint").get<std::string>();
    result.continuation_fingerprint = source.at("continuation_fingerprint").get<std::string>();
    result.continuation_checkpoint_fingerprint =
        source.at("continuation_checkpoint_fingerprint").get<std::string>();
    result.blueprint_iterations = source.at("blueprint_iterations").get<std::uint64_t>();
    result.entry_node = source.at("entry_node").get<std::uint32_t>();
    if (!read_cards(source.at("flop"), result.flop)) {
      return Result<HuPreflopFlopBoundary, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    result.resolving_player = source.at("resolving_player").get<std::uint8_t>();
    result.opponent = source.at("opponent").get<std::uint8_t>();
    for (const auto &item : source.at("values")) {
      HuPreflopFlopBoundaryValue value;
      value.opponent_combo = item.at("opponent_combo").get<ComboId>();
      value.counterfactual_reach = item.at("counterfactual_reach").get<double>();
      value.blueprint_counterfactual_value_antes =
          item.at("blueprint_counterfactual_value_antes").get<double>();
      value.positive_reach = item.at("positive_reach").get<bool>();
      if (value.opponent_combo >= 630U || !finite_nonnegative(value.counterfactual_reach) ||
          !std::isfinite(value.blueprint_counterfactual_value_antes) ||
          value.positive_reach != (value.counterfactual_reach > 0.0)) {
        return Result<HuPreflopFlopBoundary, HuPreflopError>::failure(
            HuPreflopError::IntegrityFailure);
      }
      result.values.push_back(value);
    }
    result.fingerprint = source.at("fingerprint").get<std::string>();
    if (result.continuation_fingerprint.empty() ||
        result.continuation_checkpoint_fingerprint.empty() || result.resolving_player > 1U ||
        result.opponent != 1U - result.resolving_player || result.values.empty() ||
        result.fingerprint != fingerprint_hu_preflop_flop_boundary(result)) {
      return Result<HuPreflopFlopBoundary, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    return Result<HuPreflopFlopBoundary, HuPreflopError>::success(std::move(result));
  } catch (const Json::exception &) {
    return Result<HuPreflopFlopBoundary, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
}

Result<std::string, HuPreflopError> serialize_hu_preflop_whole_game_coverage_accumulator(
    const HuPreflopWholeGameCoverageAccumulator &accumulator) {
  if (!valid_whole_game_coverage_payload(accumulator)) {
    return Result<std::string, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  return Result<std::string, HuPreflopError>::success(whole_game_coverage_json(accumulator).dump());
}

Result<HuPreflopWholeGameCoverageAccumulator, HuPreflopError>
deserialize_hu_preflop_whole_game_coverage_accumulator(const std::string &serialized) {
  try {
    const auto source = Json::parse(serialized);
    if (source.at("schema") != "gtosd.hu_preflop_whole_game_coverage.v1") {
      return Result<HuPreflopWholeGameCoverageAccumulator, HuPreflopError>::failure(
          HuPreflopError::UnsupportedVersion);
    }
    HuPreflopWholeGameCoverageAccumulator result;
    result.major = source.at("major").get<std::uint32_t>();
    result.minor = source.at("minor").get<std::uint32_t>();
    if (result.major != HuPreflopWholeGameCoverageAccumulator::format_major ||
        result.minor != HuPreflopWholeGameCoverageAccumulator::format_minor) {
      return Result<HuPreflopWholeGameCoverageAccumulator, HuPreflopError>::failure(
          HuPreflopError::UnsupportedVersion);
    }
    result.tree_fingerprint = source.at("tree_fingerprint").get<std::string>();
    result.blueprint_fingerprint = source.at("blueprint_fingerprint").get<std::string>();
    result.plan_fingerprint = source.at("plan_fingerprint").get<std::string>();
    result.target_normalized_nashconv = source.at("target_normalized_nashconv").get<double>();
    result.blueprint_iterations = source.at("blueprint_iterations").get<std::uint64_t>();
    result.continuation_checkpoint_fingerprint =
        source.at("continuation_checkpoint_fingerprint").get<std::string>();
    const auto &coverage = source.at("task_side_coverage");
    const auto &probabilities = source.at("task_joint_probabilities");
    const auto &profile_utilities = source.at("task_side_profile_utility_antes");
    const auto &local_continuation_identities =
        source.at("task_local_continuation_identity_hashes");
    constexpr std::size_t maximum_task_count = 1'000'000U;
    if (!coverage.is_array() || !probabilities.is_array() || !profile_utilities.is_array() ||
        !local_continuation_identities.is_array() || coverage.empty() ||
        coverage.size() > maximum_task_count || coverage.size() != probabilities.size() ||
        profile_utilities.size() != coverage.size() * 2U ||
        local_continuation_identities.size() != coverage.size()) {
      return Result<HuPreflopWholeGameCoverageAccumulator, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    result.task_side_coverage.reserve(coverage.size());
    result.task_joint_probabilities.reserve(probabilities.size());
    result.task_side_profile_utility_antes.reserve(profile_utilities.size());
    result.task_local_continuation_identity_hashes.reserve(coverage.size());
    for (std::size_t task = 0U; task < coverage.size(); ++task) {
      const auto mask = coverage[task].get<std::uint8_t>();
      const auto probability = probabilities[task].get<double>();
      if (mask > 0x3U || !finite_nonnegative(probability)) {
        return Result<HuPreflopWholeGameCoverageAccumulator, HuPreflopError>::failure(
            HuPreflopError::IntegrityFailure);
      }
      result.task_side_coverage.push_back(mask);
      result.task_joint_probabilities.push_back(probability);
      result.task_local_continuation_identity_hashes.push_back(
          local_continuation_identities[task].get<std::uint64_t>());
    }
    for (const auto &encoded_utility : profile_utilities) {
      const auto utility = encoded_utility.get<double>();
      if (!std::isfinite(utility)) {
        return Result<HuPreflopWholeGameCoverageAccumulator, HuPreflopError>::failure(
            HuPreflopError::IntegrityFailure);
      }
      result.task_side_profile_utility_antes.push_back(utility);
    }
    result.validated_boundary_count = source.at("validated_boundary_count").get<std::uint64_t>();
    result.fully_covered_task_count = source.at("fully_covered_task_count").get<std::uint64_t>();
    result.covered_postflop_probability = source.at("covered_postflop_probability").get<double>();
    result.coverage_state_hash = source.at("coverage_state_hash").get<std::uint64_t>();
    result.continuation_profile_hash = source.at("continuation_profile_hash").get<std::uint64_t>();
    result.profile_utility_state_hash =
        source.at("profile_utility_state_hash").get<std::uint64_t>();
    result.local_continuation_identity_state_hash =
        source.at("local_continuation_identity_state_hash").get<std::uint64_t>();
    result.contribution_chain_fingerprint =
        source.at("contribution_chain_fingerprint").get<std::string>();
    result.fingerprint = source.at("fingerprint").get<std::string>();
    if (!valid_whole_game_coverage_payload(result)) {
      return Result<HuPreflopWholeGameCoverageAccumulator, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    return Result<HuPreflopWholeGameCoverageAccumulator, HuPreflopError>::success(
        std::move(result));
  } catch (const Json::exception &) {
    return Result<HuPreflopWholeGameCoverageAccumulator, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
}

Result<std::string, HuPreflopError> serialize_hu_preflop_river_scheduler_checkpoint(
    const HuPreflopRiverSchedulerCheckpoint &checkpoint) {
  if (checkpoint.major != HuPreflopRiverSchedulerCheckpoint::format_major ||
      checkpoint.minor != HuPreflopRiverSchedulerCheckpoint::format_minor ||
      checkpoint.tree_fingerprint.empty() || checkpoint.blueprint_fingerprint.empty() ||
      checkpoint.batch_plan_fingerprint.empty() ||
      checkpoint.upper_street_accumulator_fingerprint.empty() ||
      checkpoint.fingerprint != fingerprint_hu_preflop_river_scheduler_checkpoint(checkpoint)) {
    return Result<std::string, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  return Result<std::string, HuPreflopError>::success(
      river_scheduler_checkpoint_json(checkpoint).dump());
}

Result<HuPreflopRiverSchedulerCheckpoint, HuPreflopError>
deserialize_hu_preflop_river_scheduler_checkpoint(const std::string &serialized) {
  try {
    const auto source = Json::parse(serialized);
    if (source.at("schema") != "gtosd.hu_preflop_river_scheduler_checkpoint.v1") {
      return Result<HuPreflopRiverSchedulerCheckpoint, HuPreflopError>::failure(
          HuPreflopError::UnsupportedVersion);
    }
    HuPreflopRiverSchedulerCheckpoint result;
    result.major = source.at("major").get<std::uint32_t>();
    result.minor = source.at("minor").get<std::uint32_t>();
    if (result.major != HuPreflopRiverSchedulerCheckpoint::format_major ||
        result.minor != HuPreflopRiverSchedulerCheckpoint::format_minor) {
      return Result<HuPreflopRiverSchedulerCheckpoint, HuPreflopError>::failure(
          HuPreflopError::UnsupportedVersion);
    }
    result.tree_fingerprint = source.at("tree_fingerprint").get<std::string>();
    result.blueprint_fingerprint = source.at("blueprint_fingerprint").get<std::string>();
    result.batch_plan_fingerprint = source.at("batch_plan_fingerprint").get<std::string>();
    result.completed_batch_count = source.at("completed_batch_count").get<std::uint64_t>();
    result.completed_resolver_root_count =
        source.at("completed_resolver_root_count").get<std::uint64_t>();
    result.upper_street_accumulator_fingerprint =
        source.at("upper_street_accumulator_fingerprint").get<std::string>();
    result.complete = source.at("complete").get<bool>();
    result.fingerprint = source.at("fingerprint").get<std::string>();
    if (result.tree_fingerprint.empty() || result.blueprint_fingerprint.empty() ||
        result.batch_plan_fingerprint.empty() ||
        result.upper_street_accumulator_fingerprint.empty() || result.fingerprint.empty() ||
        result.fingerprint != fingerprint_hu_preflop_river_scheduler_checkpoint(result)) {
      return Result<HuPreflopRiverSchedulerCheckpoint, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    return Result<HuPreflopRiverSchedulerCheckpoint, HuPreflopError>::success(std::move(result));
  } catch (const Json::exception &) {
    return Result<HuPreflopRiverSchedulerCheckpoint, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
}

Result<std::string, HuPreflopError>
serialize_hu_preflop_river_task_accumulator(const HuPreflopRiverTaskAccumulator &accumulator) {
  if (!validate_hu_preflop_river_task_accumulator(accumulator)) {
    return Result<std::string, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  return Result<std::string, HuPreflopError>::success(
      river_task_accumulator_json(accumulator).dump());
}

Result<HuPreflopRiverTaskAccumulator, HuPreflopError>
deserialize_hu_preflop_river_task_accumulator(const std::string &serialized) {
  try {
    const auto source = Json::parse(serialized);
    if (source.at("schema") != "gtosd.hu_preflop_river_task_accumulator.v5") {
      return Result<HuPreflopRiverTaskAccumulator, HuPreflopError>::failure(
          HuPreflopError::UnsupportedVersion);
    }
    HuPreflopRiverTaskAccumulator result;
    result.major = source.at("major").get<std::uint32_t>();
    result.minor = source.at("minor").get<std::uint32_t>();
    if (result.major != HuPreflopRiverTaskAccumulator::format_major ||
        result.minor != HuPreflopRiverTaskAccumulator::format_minor) {
      return Result<HuPreflopRiverTaskAccumulator, HuPreflopError>::failure(
          HuPreflopError::UnsupportedVersion);
    }
    result.tree_fingerprint = source.at("tree_fingerprint").get<std::string>();
    result.blueprint_fingerprint = source.at("blueprint_fingerprint").get<std::string>();
    result.root_catalog_fingerprint = source.at("root_catalog_fingerprint").get<std::string>();
    result.batch_plan_fingerprint = source.at("batch_plan_fingerprint").get<std::string>();
    result.continuation_checkpoint_fingerprint =
        source.at("continuation_checkpoint_fingerprint").get<std::string>();
    const auto value_mode = source.at("value_mode").get<std::uint8_t>();
    if (value_mode > static_cast<std::uint8_t>(HuPreflopContinuationValueMode::ExactBestResponse)) {
      return Result<HuPreflopRiverTaskAccumulator, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    result.value_mode = static_cast<HuPreflopContinuationValueMode>(value_mode);
    result.blueprint_iterations = source.at("blueprint_iterations").get<std::uint64_t>();
    result.task_span_index = source.at("task_span_index").get<std::uint64_t>();
    result.first_resolver_root = source.at("first_resolver_root").get<std::uint64_t>();
    result.resolver_root_count = source.at("resolver_root_count").get<std::uint64_t>();
    result.next_resolver_root = source.at("next_resolver_root").get<std::uint64_t>();
    result.entry_node = source.at("entry_node").get<std::uint32_t>();
    if (!read_cards(source.at("flop"), result.flop)) {
      return Result<HuPreflopRiverTaskAccumulator, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    result.accumulated_roots_by_resolver =
        source.at("accumulated_roots_by_resolver")
            .get<decltype(result.accumulated_roots_by_resolver)>();
    const auto &values = source.at("values");
    if (!values.is_array() || values.size() != result.values.size()) {
      return Result<HuPreflopRiverTaskAccumulator, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    for (std::size_t player = 0U; player < result.values.size(); ++player) {
      if (!values[player].is_array() || values[player].size() != result.values[player].size()) {
        return Result<HuPreflopRiverTaskAccumulator, HuPreflopError>::failure(
            HuPreflopError::IntegrityFailure);
      }
      for (std::size_t combo = 0U; combo < result.values[player].size(); ++combo) {
        const auto &encoded = values[player][combo];
        if (!encoded.is_array() || encoded.size() != 4U) {
          return Result<HuPreflopRiverTaskAccumulator, HuPreflopError>::failure(
              HuPreflopError::IntegrityFailure);
        }
        auto &value = result.values[player][combo];
        value.weighted_reach_sum = encoded[0].get<double>();
        value.weighted_reach_compensation = encoded[1].get<double>();
        value.weighted_utility_sum_antes = encoded[2].get<double>();
        value.weighted_utility_compensation_antes = encoded[3].get<double>();
        if (!std::isfinite(value.weighted_reach_sum) ||
            !std::isfinite(value.weighted_reach_compensation) ||
            !std::isfinite(value.weighted_utility_sum_antes) ||
            !std::isfinite(value.weighted_utility_compensation_antes)) {
          return Result<HuPreflopRiverTaskAccumulator, HuPreflopError>::failure(
              HuPreflopError::IntegrityFailure);
        }
      }
    }
    result.complete = source.at("complete").get<bool>();
    result.contribution_chain_fingerprint =
        source.at("contribution_chain_fingerprint").get<std::string>();
    result.fingerprint = source.at("fingerprint").get<std::string>();
    if (result.tree_fingerprint.empty() || result.blueprint_fingerprint.empty() ||
        result.root_catalog_fingerprint.empty() || result.batch_plan_fingerprint.empty() ||
        result.blueprint_iterations == 0U || result.resolver_root_count == 0U ||
        result.contribution_chain_fingerprint.empty() || result.fingerprint.empty() ||
        !validate_hu_preflop_river_task_accumulator(result)) {
      return Result<HuPreflopRiverTaskAccumulator, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    return Result<HuPreflopRiverTaskAccumulator, HuPreflopError>::success(std::move(result));
  } catch (const Json::exception &) {
    return Result<HuPreflopRiverTaskAccumulator, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
}

Result<std::string, HuPreflopError> serialize_hu_preflop_river_best_response_leaf_accumulator(
    const HuPreflopRiverBestResponseLeafAccumulator &accumulator) {
  if (!valid_river_best_response_leaf_accumulator_payload(accumulator)) {
    return Result<std::string, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  return Result<std::string, HuPreflopError>::success(
      river_best_response_leaf_accumulator_json(accumulator).dump());
}

Result<HuPreflopRiverBestResponseLeafAccumulator, HuPreflopError>
deserialize_hu_preflop_river_best_response_leaf_accumulator(const std::string &serialized) {
  constexpr std::size_t maximum_serialized_bytes = 2U * 1024U * 1024U;
  if (serialized.size() > maximum_serialized_bytes) {
    return Result<HuPreflopRiverBestResponseLeafAccumulator, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  try {
    const auto source = Json::parse(serialized);
    if (source.at("schema") != "gtosd.hu_preflop_river_best_response_leaf_accumulator.v1") {
      return Result<HuPreflopRiverBestResponseLeafAccumulator, HuPreflopError>::failure(
          HuPreflopError::UnsupportedVersion);
    }
    HuPreflopRiverBestResponseLeafAccumulator result;
    result.major = source.at("major").get<std::uint32_t>();
    result.minor = source.at("minor").get<std::uint32_t>();
    if (result.major != HuPreflopRiverBestResponseLeafAccumulator::format_major ||
        result.minor != HuPreflopRiverBestResponseLeafAccumulator::format_minor) {
      return Result<HuPreflopRiverBestResponseLeafAccumulator, HuPreflopError>::failure(
          HuPreflopError::UnsupportedVersion);
    }
    result.tree_fingerprint = source.at("tree_fingerprint").get<std::string>();
    result.blueprint_fingerprint = source.at("blueprint_fingerprint").get<std::string>();
    result.root_catalog_fingerprint = source.at("root_catalog_fingerprint").get<std::string>();
    result.batch_plan_fingerprint = source.at("batch_plan_fingerprint").get<std::string>();
    result.continuation_fingerprint = source.at("continuation_fingerprint").get<std::string>();
    result.blueprint_iterations = source.at("blueprint_iterations").get<std::uint64_t>();
    result.task_span_index = source.at("task_span_index").get<std::uint64_t>();
    result.entry_node = source.at("entry_node").get<std::uint32_t>();
    if (!read_cards(source.at("flop"), result.flop)) {
      return Result<HuPreflopRiverBestResponseLeafAccumulator, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    const auto turn = source.at("turn").get<std::uint32_t>();
    if (turn >= 36U) {
      return Result<HuPreflopRiverBestResponseLeafAccumulator, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    result.turn = CardId::from_index(static_cast<std::uint8_t>(turn)).value();
    result.responding_player = source.at("responding_player").get<std::uint8_t>();
    if (!read_public_state(source.at("state"), result.state)) {
      return Result<HuPreflopRiverBestResponseLeafAccumulator, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    const auto &actions = source.at("action_history");
    if (!actions.is_array() || actions.size() > 256U) {
      return Result<HuPreflopRiverBestResponseLeafAccumulator, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    result.action_history.reserve(actions.size());
    for (const auto &encoded : actions) {
      Action action;
      if (!read_action(encoded, action)) {
        return Result<HuPreflopRiverBestResponseLeafAccumulator, HuPreflopError>::failure(
            HuPreflopError::IntegrityFailure);
      }
      result.action_history.push_back(action);
    }
    result.history_fingerprint = source.at("history_fingerprint").get<std::string>();
    const auto &ordinals = source.at("expected_resolver_root_ordinals");
    if (!ordinals.is_array() || ordinals.empty() || ordinals.size() > 36U) {
      return Result<HuPreflopRiverBestResponseLeafAccumulator, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    result.expected_resolver_root_ordinals = ordinals.get<std::vector<std::uint64_t>>();
    result.next_boundary_index = source.at("next_boundary_index").get<std::uint64_t>();
    const auto &values = source.at("values");
    if (!values.is_array() || values.size() != result.values.size()) {
      return Result<HuPreflopRiverBestResponseLeafAccumulator, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    for (std::size_t combo = 0U; combo < result.values.size(); ++combo) {
      const auto &encoded = values[combo];
      if (!encoded.is_array() || encoded.size() != 4U) {
        return Result<HuPreflopRiverBestResponseLeafAccumulator, HuPreflopError>::failure(
            HuPreflopError::IntegrityFailure);
      }
      auto &value = result.values[combo];
      value.weighted_reach_sum = encoded[0].get<double>();
      value.weighted_reach_compensation = encoded[1].get<double>();
      value.weighted_utility_sum_antes = encoded[2].get<double>();
      value.weighted_utility_compensation_antes = encoded[3].get<double>();
    }
    result.complete = source.at("complete").get<bool>();
    result.contribution_chain_fingerprint =
        source.at("contribution_chain_fingerprint").get<std::string>();
    result.fingerprint = source.at("fingerprint").get<std::string>();
    return valid_river_best_response_leaf_accumulator_payload(result)
               ? Result<HuPreflopRiverBestResponseLeafAccumulator, HuPreflopError>::success(
                     std::move(result))
               : Result<HuPreflopRiverBestResponseLeafAccumulator, HuPreflopError>::failure(
                     HuPreflopError::IntegrityFailure);
  } catch (const Json::exception &) {
    return Result<HuPreflopRiverBestResponseLeafAccumulator, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
}

Result<std::string, HuPreflopError> serialize_hu_preflop_best_response_task_evaluation(
    const HuPreflopBestResponseTaskEvaluation &evaluation) {
  if (!valid_best_response_task_evaluation_payload(evaluation)) {
    return Result<std::string, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  return Result<std::string, HuPreflopError>::success(
      best_response_task_evaluation_json(evaluation).dump());
}

Result<HuPreflopBestResponseTaskEvaluation, HuPreflopError>
deserialize_hu_preflop_best_response_task_evaluation(const std::string &serialized) {
  constexpr std::size_t maximum_serialized_bytes = 1024U * 1024U;
  if (serialized.size() > maximum_serialized_bytes) {
    return Result<HuPreflopBestResponseTaskEvaluation, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  try {
    const auto source = Json::parse(serialized);
    if (source.at("schema") != "gtosd.hu_preflop_best_response_task_evaluation.v1") {
      return Result<HuPreflopBestResponseTaskEvaluation, HuPreflopError>::failure(
          HuPreflopError::UnsupportedVersion);
    }
    HuPreflopBestResponseTaskEvaluation result;
    result.major = source.at("major").get<std::uint32_t>();
    result.minor = source.at("minor").get<std::uint32_t>();
    if (result.major != HuPreflopBestResponseTaskEvaluation::format_major ||
        result.minor != HuPreflopBestResponseTaskEvaluation::format_minor) {
      return Result<HuPreflopBestResponseTaskEvaluation, HuPreflopError>::failure(
          HuPreflopError::UnsupportedVersion);
    }
    result.tree_fingerprint = source.at("tree_fingerprint").get<std::string>();
    result.blueprint_fingerprint = source.at("blueprint_fingerprint").get<std::string>();
    result.continuation_fingerprint = source.at("continuation_fingerprint").get<std::string>();
    result.blueprint_iterations = source.at("blueprint_iterations").get<std::uint64_t>();
    result.task_span_index = source.at("task_span_index").get<std::uint64_t>();
    result.entry_node = source.at("entry_node").get<std::uint32_t>();
    if (!read_cards(source.at("flop"), result.flop)) {
      return Result<HuPreflopBestResponseTaskEvaluation, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    result.responding_player = source.at("responding_player").get<std::uint8_t>();
    result.decision_nodes_visited = source.at("decision_nodes_visited").get<std::uint64_t>();
    result.responder_decision_nodes_visited =
        source.at("responder_decision_nodes_visited").get<std::uint64_t>();
    result.opponent_decision_nodes_visited =
        source.at("opponent_decision_nodes_visited").get<std::uint64_t>();
    result.physical_turn_branches_visited =
        source.at("physical_turn_branches_visited").get<std::uint64_t>();
    result.upper_terminal_leaves_visited =
        source.at("upper_terminal_leaves_visited").get<std::uint64_t>();
    result.river_continuation_leaves_visited =
        source.at("river_continuation_leaves_visited").get<std::uint64_t>();
    result.maximum_recursion_depth = source.at("maximum_recursion_depth").get<std::uint64_t>();
    if (!read_best_response_combo_values(source.at("values"), result.values, 528U)) {
      return Result<HuPreflopBestResponseTaskEvaluation, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    result.fingerprint = source.at("fingerprint").get<std::string>();
    return valid_best_response_task_evaluation_payload(result)
               ? Result<HuPreflopBestResponseTaskEvaluation, HuPreflopError>::success(
                     std::move(result))
               : Result<HuPreflopBestResponseTaskEvaluation, HuPreflopError>::failure(
                     HuPreflopError::IntegrityFailure);
  } catch (const Json::exception &) {
    return Result<HuPreflopBestResponseTaskEvaluation, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
}

Result<std::string, HuPreflopError> serialize_hu_preflop_best_response_entry_accumulator(
    const HuPreflopBestResponseEntryAccumulator &accumulator) {
  if (!valid_best_response_entry_accumulator_payload(accumulator)) {
    return Result<std::string, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  return Result<std::string, HuPreflopError>::success(
      best_response_entry_accumulator_json(accumulator).dump());
}

Result<HuPreflopBestResponseEntryAccumulator, HuPreflopError>
deserialize_hu_preflop_best_response_entry_accumulator(const std::string &serialized) {
  constexpr std::size_t maximum_serialized_bytes = 256U * 1024U;
  if (serialized.size() > maximum_serialized_bytes) {
    return Result<HuPreflopBestResponseEntryAccumulator, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  try {
    const auto source = Json::parse(serialized);
    if (source.at("schema") != "gtosd.hu_preflop_best_response_entry_accumulator.v1") {
      return Result<HuPreflopBestResponseEntryAccumulator, HuPreflopError>::failure(
          HuPreflopError::UnsupportedVersion);
    }
    HuPreflopBestResponseEntryAccumulator result;
    result.major = source.at("major").get<std::uint32_t>();
    result.minor = source.at("minor").get<std::uint32_t>();
    if (result.major != HuPreflopBestResponseEntryAccumulator::format_major ||
        result.minor != HuPreflopBestResponseEntryAccumulator::format_minor) {
      return Result<HuPreflopBestResponseEntryAccumulator, HuPreflopError>::failure(
          HuPreflopError::UnsupportedVersion);
    }
    result.tree_fingerprint = source.at("tree_fingerprint").get<std::string>();
    result.blueprint_fingerprint = source.at("blueprint_fingerprint").get<std::string>();
    result.continuation_fingerprint = source.at("continuation_fingerprint").get<std::string>();
    result.blueprint_iterations = source.at("blueprint_iterations").get<std::uint64_t>();
    result.entry_index = source.at("entry_index").get<std::uint64_t>();
    result.entry_node = source.at("entry_node").get<std::uint32_t>();
    result.responding_player = source.at("responding_player").get<std::uint8_t>();
    result.first_task_span_index = source.at("first_task_span_index").get<std::uint64_t>();
    result.task_count = source.at("task_count").get<std::uint64_t>();
    result.next_task_offset = source.at("next_task_offset").get<std::uint64_t>();
    const auto &values = source.at("values");
    if (!values.is_array() || values.size() != result.values.size()) {
      return Result<HuPreflopBestResponseEntryAccumulator, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    for (std::size_t index = 0U; index < result.values.size(); ++index) {
      const auto &encoded = values[index];
      if (!encoded.is_array() || encoded.size() != 4U) {
        return Result<HuPreflopBestResponseEntryAccumulator, HuPreflopError>::failure(
            HuPreflopError::IntegrityFailure);
      }
      auto &value = result.values[index];
      value.weighted_reach_sum = encoded[0].get<double>();
      value.weighted_reach_compensation = encoded[1].get<double>();
      value.weighted_utility_sum_antes = encoded[2].get<double>();
      value.weighted_utility_compensation_antes = encoded[3].get<double>();
    }
    result.complete = source.at("complete").get<bool>();
    result.contribution_chain_fingerprint =
        source.at("contribution_chain_fingerprint").get<std::string>();
    result.fingerprint = source.at("fingerprint").get<std::string>();
    return valid_best_response_entry_accumulator_payload(result)
               ? Result<HuPreflopBestResponseEntryAccumulator, HuPreflopError>::success(
                     std::move(result))
               : Result<HuPreflopBestResponseEntryAccumulator, HuPreflopError>::failure(
                     HuPreflopError::IntegrityFailure);
  } catch (const Json::exception &) {
    return Result<HuPreflopBestResponseEntryAccumulator, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
}

Result<std::string, HuPreflopError> serialize_hu_preflop_best_response_entry_evaluation(
    const HuPreflopBestResponseEntryEvaluation &evaluation) {
  if (!valid_best_response_entry_evaluation_payload(evaluation)) {
    return Result<std::string, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  return Result<std::string, HuPreflopError>::success(
      best_response_entry_evaluation_json(evaluation).dump());
}

Result<HuPreflopBestResponseEntryEvaluation, HuPreflopError>
deserialize_hu_preflop_best_response_entry_evaluation(const std::string &serialized) {
  constexpr std::size_t maximum_serialized_bytes = 256U * 1024U;
  if (serialized.size() > maximum_serialized_bytes) {
    return Result<HuPreflopBestResponseEntryEvaluation, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
  try {
    const auto source = Json::parse(serialized);
    if (source.at("schema") != "gtosd.hu_preflop_best_response_entry_evaluation.v1") {
      return Result<HuPreflopBestResponseEntryEvaluation, HuPreflopError>::failure(
          HuPreflopError::UnsupportedVersion);
    }
    HuPreflopBestResponseEntryEvaluation result;
    result.major = source.at("major").get<std::uint32_t>();
    result.minor = source.at("minor").get<std::uint32_t>();
    if (result.major != HuPreflopBestResponseEntryEvaluation::format_major ||
        result.minor != HuPreflopBestResponseEntryEvaluation::format_minor) {
      return Result<HuPreflopBestResponseEntryEvaluation, HuPreflopError>::failure(
          HuPreflopError::UnsupportedVersion);
    }
    result.tree_fingerprint = source.at("tree_fingerprint").get<std::string>();
    result.blueprint_fingerprint = source.at("blueprint_fingerprint").get<std::string>();
    result.continuation_fingerprint = source.at("continuation_fingerprint").get<std::string>();
    result.accumulator_fingerprint = source.at("accumulator_fingerprint").get<std::string>();
    result.blueprint_iterations = source.at("blueprint_iterations").get<std::uint64_t>();
    result.entry_index = source.at("entry_index").get<std::uint64_t>();
    result.entry_node = source.at("entry_node").get<std::uint32_t>();
    result.responding_player = source.at("responding_player").get<std::uint8_t>();
    result.task_count = source.at("task_count").get<std::uint64_t>();
    if (!read_best_response_hand_class_values(source.at("values"), result.values)) {
      return Result<HuPreflopBestResponseEntryEvaluation, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    result.fingerprint = source.at("fingerprint").get<std::string>();
    return valid_best_response_entry_evaluation_payload(result)
               ? Result<HuPreflopBestResponseEntryEvaluation, HuPreflopError>::success(
                     std::move(result))
               : Result<HuPreflopBestResponseEntryEvaluation, HuPreflopError>::failure(
                     HuPreflopError::IntegrityFailure);
  } catch (const Json::exception &) {
    return Result<HuPreflopBestResponseEntryEvaluation, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
}

Result<std::string, HuPreflopError>
serialize_hu_preflop_river_task_aggregate(const HuPreflopRiverTaskAggregate &aggregate) {
  if (!validate_hu_preflop_river_task_aggregate(aggregate)) {
    return Result<std::string, HuPreflopError>::failure(HuPreflopError::IntegrityFailure);
  }
  return Result<std::string, HuPreflopError>::success(river_task_aggregate_json(aggregate).dump());
}

Result<HuPreflopRiverTaskAggregate, HuPreflopError>
deserialize_hu_preflop_river_task_aggregate(const std::string &serialized) {
  try {
    const auto source = Json::parse(serialized);
    if (source.at("schema") != "gtosd.hu_preflop_river_task_aggregate.v5") {
      return Result<HuPreflopRiverTaskAggregate, HuPreflopError>::failure(
          HuPreflopError::UnsupportedVersion);
    }
    HuPreflopRiverTaskAggregate result;
    result.major = source.at("major").get<std::uint32_t>();
    result.minor = source.at("minor").get<std::uint32_t>();
    if (result.major != HuPreflopRiverTaskAggregate::format_major ||
        result.minor != HuPreflopRiverTaskAggregate::format_minor) {
      return Result<HuPreflopRiverTaskAggregate, HuPreflopError>::failure(
          HuPreflopError::UnsupportedVersion);
    }
    result.tree_fingerprint = source.at("tree_fingerprint").get<std::string>();
    result.blueprint_fingerprint = source.at("blueprint_fingerprint").get<std::string>();
    result.accumulator_fingerprint = source.at("accumulator_fingerprint").get<std::string>();
    result.continuation_checkpoint_fingerprint =
        source.at("continuation_checkpoint_fingerprint").get<std::string>();
    const auto value_mode = source.at("value_mode").get<std::uint8_t>();
    if (value_mode > static_cast<std::uint8_t>(HuPreflopContinuationValueMode::ExactBestResponse)) {
      return Result<HuPreflopRiverTaskAggregate, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    result.value_mode = static_cast<HuPreflopContinuationValueMode>(value_mode);
    result.blueprint_iterations = source.at("blueprint_iterations").get<std::uint64_t>();
    result.task_span_index = source.at("task_span_index").get<std::uint64_t>();
    result.entry_node = source.at("entry_node").get<std::uint32_t>();
    if (!read_cards(source.at("flop"), result.flop)) {
      return Result<HuPreflopRiverTaskAggregate, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    const auto &resolver_values = source.at("resolver_values");
    if (!resolver_values.is_array() || resolver_values.size() != result.resolver_values.size()) {
      return Result<HuPreflopRiverTaskAggregate, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    for (std::size_t player = 0U; player < result.resolver_values.size(); ++player) {
      const auto &encoded_player = resolver_values[player];
      if (!encoded_player.is_array() || encoded_player.size() != 528U) {
        return Result<HuPreflopRiverTaskAggregate, HuPreflopError>::failure(
            HuPreflopError::IntegrityFailure);
      }
      auto &decoded_player = result.resolver_values[player];
      decoded_player.reserve(encoded_player.size());
      for (const auto &encoded : encoded_player) {
        if (!encoded.is_array() || encoded.size() != 5U) {
          return Result<HuPreflopRiverTaskAggregate, HuPreflopError>::failure(
              HuPreflopError::IntegrityFailure);
        }
        HuPreflopRiverTaskAggregateValue value;
        value.opponent_combo = encoded[0].get<ComboId>();
        value.weighted_counterfactual_reach = encoded[1].get<double>();
        value.weighted_counterfactual_utility_antes = encoded[2].get<double>();
        value.conditional_value_antes = encoded[3].get<double>();
        value.positive_reach = encoded[4].get<bool>();
        if (!std::isfinite(value.weighted_counterfactual_reach) ||
            !std::isfinite(value.weighted_counterfactual_utility_antes) ||
            !std::isfinite(value.conditional_value_antes)) {
          return Result<HuPreflopRiverTaskAggregate, HuPreflopError>::failure(
              HuPreflopError::IntegrityFailure);
        }
        decoded_player.push_back(value);
      }
    }
    result.fingerprint = source.at("fingerprint").get<std::string>();
    if (!validate_hu_preflop_river_task_aggregate(result)) {
      return Result<HuPreflopRiverTaskAggregate, HuPreflopError>::failure(
          HuPreflopError::IntegrityFailure);
    }
    return Result<HuPreflopRiverTaskAggregate, HuPreflopError>::success(std::move(result));
  } catch (const Json::exception &) {
    return Result<HuPreflopRiverTaskAggregate, HuPreflopError>::failure(
        HuPreflopError::IntegrityFailure);
  }
}

Result<bool, HuPreflopError> save_hu_preflop_blueprint(const HuPreflopBlueprint &blueprint,
                                                       const std::string &path) {
  const auto payload = serialize_hu_preflop_blueprint(blueprint);
  return payload ? save_payload(path, wrap_file("GTOSD_HU_PREFLOP_BLUEPRINT_FILE", payload.value()))
                 : Result<bool, HuPreflopError>::failure(payload.error());
}

Result<HuPreflopBlueprint, HuPreflopError> load_hu_preflop_blueprint(const std::string &path) {
  const auto file = load_payload(path);
  if (!file) {
    return Result<HuPreflopBlueprint, HuPreflopError>::failure(file.error());
  }
  const auto payload = unwrap_file(file.value(), "GTOSD_HU_PREFLOP_BLUEPRINT_FILE");
  return payload ? deserialize_hu_preflop_blueprint(payload.value())
                 : Result<HuPreflopBlueprint, HuPreflopError>::failure(payload.error());
}

Result<bool, HuPreflopError>
save_hu_preflop_decomposition_plan(const HuPreflopDecompositionPlan &plan,
                                   const std::string &path) {
  const auto payload = serialize_hu_preflop_decomposition_plan(plan);
  return payload
             ? save_payload(path, wrap_file("GTOSD_HU_PREFLOP_DECOMPOSITION_FILE", payload.value()))
             : Result<bool, HuPreflopError>::failure(payload.error());
}

Result<HuPreflopDecompositionPlan, HuPreflopError>
load_hu_preflop_decomposition_plan(const std::string &path) {
  const auto file = load_payload(path);
  if (!file) {
    return Result<HuPreflopDecompositionPlan, HuPreflopError>::failure(file.error());
  }
  const auto payload = unwrap_file(file.value(), "GTOSD_HU_PREFLOP_DECOMPOSITION_FILE");
  return payload ? deserialize_hu_preflop_decomposition_plan(payload.value())
                 : Result<HuPreflopDecompositionPlan, HuPreflopError>::failure(payload.error());
}

Result<bool, HuPreflopError> save_hu_preflop_flop_boundary(const HuPreflopFlopBoundary &boundary,
                                                           const std::string &path) {
  const auto payload = serialize_hu_preflop_flop_boundary(boundary);
  return payload ? save_payload(path, wrap_file("GTOSD_HU_PREFLOP_BOUNDARY_FILE", payload.value()))
                 : Result<bool, HuPreflopError>::failure(payload.error());
}

Result<HuPreflopFlopBoundary, HuPreflopError>
load_hu_preflop_flop_boundary(const std::string &path) {
  const auto file = load_payload(path);
  if (!file) {
    return Result<HuPreflopFlopBoundary, HuPreflopError>::failure(file.error());
  }
  const auto payload = unwrap_file(file.value(), "GTOSD_HU_PREFLOP_BOUNDARY_FILE");
  return payload ? deserialize_hu_preflop_flop_boundary(payload.value())
                 : Result<HuPreflopFlopBoundary, HuPreflopError>::failure(payload.error());
}

Result<bool, HuPreflopError> save_hu_preflop_whole_game_coverage_accumulator(
    const HuPreflopWholeGameCoverageAccumulator &accumulator, const std::string &path) {
  const auto payload = serialize_hu_preflop_whole_game_coverage_accumulator(accumulator);
  return payload ? save_payload(path, wrap_file("GTOSD_HU_PREFLOP_WHOLE_GAME_COVERAGE_FILE",
                                                payload.value()))
                 : Result<bool, HuPreflopError>::failure(payload.error());
}

Result<HuPreflopWholeGameCoverageAccumulator, HuPreflopError>
load_hu_preflop_whole_game_coverage_accumulator(const std::string &path) {
  const auto file = load_payload(path);
  if (!file) {
    return Result<HuPreflopWholeGameCoverageAccumulator, HuPreflopError>::failure(file.error());
  }
  const auto payload = unwrap_file(file.value(), "GTOSD_HU_PREFLOP_WHOLE_GAME_COVERAGE_FILE");
  return payload ? deserialize_hu_preflop_whole_game_coverage_accumulator(payload.value())
                 : Result<HuPreflopWholeGameCoverageAccumulator, HuPreflopError>::failure(
                       payload.error());
}

Result<bool, HuPreflopError>
save_hu_preflop_river_scheduler_checkpoint(const HuPreflopRiverSchedulerCheckpoint &checkpoint,
                                           const std::string &path) {
  const auto payload = serialize_hu_preflop_river_scheduler_checkpoint(checkpoint);
  return payload ? save_payload(path,
                                wrap_file("GTOSD_HU_PREFLOP_RIVER_SCHEDULER_FILE", payload.value()))
                 : Result<bool, HuPreflopError>::failure(payload.error());
}

Result<HuPreflopRiverSchedulerCheckpoint, HuPreflopError>
load_hu_preflop_river_scheduler_checkpoint(const std::string &path) {
  const auto file = load_payload(path);
  if (!file) {
    return Result<HuPreflopRiverSchedulerCheckpoint, HuPreflopError>::failure(file.error());
  }
  const auto payload = unwrap_file(file.value(), "GTOSD_HU_PREFLOP_RIVER_SCHEDULER_FILE");
  return payload
             ? deserialize_hu_preflop_river_scheduler_checkpoint(payload.value())
             : Result<HuPreflopRiverSchedulerCheckpoint, HuPreflopError>::failure(payload.error());
}

Result<bool, HuPreflopError>
save_hu_preflop_river_task_accumulator(const HuPreflopRiverTaskAccumulator &accumulator,
                                       const std::string &path) {
  const auto payload = serialize_hu_preflop_river_task_accumulator(accumulator);
  return payload ? save_payload(
                       path, wrap_file("GTOSD_HU_PREFLOP_RIVER_ACCUMULATOR_FILE", payload.value()))
                 : Result<bool, HuPreflopError>::failure(payload.error());
}

Result<HuPreflopRiverTaskAccumulator, HuPreflopError>
load_hu_preflop_river_task_accumulator(const std::string &path) {
  const auto file = load_payload(path);
  if (!file) {
    return Result<HuPreflopRiverTaskAccumulator, HuPreflopError>::failure(file.error());
  }
  const auto payload = unwrap_file(file.value(), "GTOSD_HU_PREFLOP_RIVER_ACCUMULATOR_FILE");
  return payload ? deserialize_hu_preflop_river_task_accumulator(payload.value())
                 : Result<HuPreflopRiverTaskAccumulator, HuPreflopError>::failure(payload.error());
}

Result<bool, HuPreflopError> save_hu_preflop_river_best_response_leaf_accumulator(
    const HuPreflopRiverBestResponseLeafAccumulator &accumulator, const std::string &path) {
  const auto payload = serialize_hu_preflop_river_best_response_leaf_accumulator(accumulator);
  return payload ? save_payload(path, wrap_file("GTOSD_HU_PREFLOP_RIVER_BR_LEAF_ACCUMULATOR_FILE",
                                                payload.value()))
                 : Result<bool, HuPreflopError>::failure(payload.error());
}

Result<HuPreflopRiverBestResponseLeafAccumulator, HuPreflopError>
load_hu_preflop_river_best_response_leaf_accumulator(const std::string &path) {
  const auto file = load_payload(path);
  if (!file) {
    return Result<HuPreflopRiverBestResponseLeafAccumulator, HuPreflopError>::failure(file.error());
  }
  const auto payload = unwrap_file(file.value(), "GTOSD_HU_PREFLOP_RIVER_BR_LEAF_ACCUMULATOR_FILE");
  return payload ? deserialize_hu_preflop_river_best_response_leaf_accumulator(payload.value())
                 : Result<HuPreflopRiverBestResponseLeafAccumulator, HuPreflopError>::failure(
                       payload.error());
}

Result<bool, HuPreflopError>
save_hu_preflop_best_response_task_evaluation(const HuPreflopBestResponseTaskEvaluation &evaluation,
                                              const std::string &path) {
  const auto payload = serialize_hu_preflop_best_response_task_evaluation(evaluation);
  return payload ? save_payload(
                       path, wrap_file("GTOSD_HU_PREFLOP_BR_TASK_EVALUATION_FILE", payload.value()))
                 : Result<bool, HuPreflopError>::failure(payload.error());
}

Result<HuPreflopBestResponseTaskEvaluation, HuPreflopError>
load_hu_preflop_best_response_task_evaluation(const std::string &path) {
  const auto file = load_payload(path);
  if (!file) {
    return Result<HuPreflopBestResponseTaskEvaluation, HuPreflopError>::failure(file.error());
  }
  const auto payload = unwrap_file(file.value(), "GTOSD_HU_PREFLOP_BR_TASK_EVALUATION_FILE");
  return payload ? deserialize_hu_preflop_best_response_task_evaluation(payload.value())
                 : Result<HuPreflopBestResponseTaskEvaluation, HuPreflopError>::failure(
                       payload.error());
}

Result<bool, HuPreflopError> save_hu_preflop_best_response_entry_accumulator(
    const HuPreflopBestResponseEntryAccumulator &accumulator, const std::string &path) {
  const auto payload = serialize_hu_preflop_best_response_entry_accumulator(accumulator);
  return payload ? save_payload(path, wrap_file("GTOSD_HU_PREFLOP_BR_ENTRY_ACCUMULATOR_FILE",
                                                payload.value()))
                 : Result<bool, HuPreflopError>::failure(payload.error());
}

Result<HuPreflopBestResponseEntryAccumulator, HuPreflopError>
load_hu_preflop_best_response_entry_accumulator(const std::string &path) {
  const auto file = load_payload(path);
  if (!file) {
    return Result<HuPreflopBestResponseEntryAccumulator, HuPreflopError>::failure(file.error());
  }
  const auto payload = unwrap_file(file.value(), "GTOSD_HU_PREFLOP_BR_ENTRY_ACCUMULATOR_FILE");
  return payload ? deserialize_hu_preflop_best_response_entry_accumulator(payload.value())
                 : Result<HuPreflopBestResponseEntryAccumulator, HuPreflopError>::failure(
                       payload.error());
}

Result<bool, HuPreflopError> save_hu_preflop_best_response_entry_evaluation(
    const HuPreflopBestResponseEntryEvaluation &evaluation, const std::string &path) {
  const auto payload = serialize_hu_preflop_best_response_entry_evaluation(evaluation);
  return payload ? save_payload(path, wrap_file("GTOSD_HU_PREFLOP_BR_ENTRY_EVALUATION_FILE",
                                                payload.value()))
                 : Result<bool, HuPreflopError>::failure(payload.error());
}

Result<HuPreflopBestResponseEntryEvaluation, HuPreflopError>
load_hu_preflop_best_response_entry_evaluation(const std::string &path) {
  const auto file = load_payload(path);
  if (!file) {
    return Result<HuPreflopBestResponseEntryEvaluation, HuPreflopError>::failure(file.error());
  }
  const auto payload = unwrap_file(file.value(), "GTOSD_HU_PREFLOP_BR_ENTRY_EVALUATION_FILE");
  return payload ? deserialize_hu_preflop_best_response_entry_evaluation(payload.value())
                 : Result<HuPreflopBestResponseEntryEvaluation, HuPreflopError>::failure(
                       payload.error());
}

Result<bool, HuPreflopError>
save_hu_preflop_river_task_aggregate(const HuPreflopRiverTaskAggregate &aggregate,
                                     const std::string &path) {
  const auto payload = serialize_hu_preflop_river_task_aggregate(aggregate);
  return payload ? save_payload(path,
                                wrap_file("GTOSD_HU_PREFLOP_RIVER_AGGREGATE_FILE", payload.value()))
                 : Result<bool, HuPreflopError>::failure(payload.error());
}

Result<HuPreflopRiverTaskAggregate, HuPreflopError>
load_hu_preflop_river_task_aggregate(const std::string &path) {
  const auto file = load_payload(path);
  if (!file) {
    return Result<HuPreflopRiverTaskAggregate, HuPreflopError>::failure(file.error());
  }
  const auto payload = unwrap_file(file.value(), "GTOSD_HU_PREFLOP_RIVER_AGGREGATE_FILE");
  return payload ? deserialize_hu_preflop_river_task_aggregate(payload.value())
                 : Result<HuPreflopRiverTaskAggregate, HuPreflopError>::failure(payload.error());
}

} // namespace gtosd
